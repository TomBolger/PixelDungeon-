package sim;

import java.util.HashMap;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

import com.watabou.gltextures.TextureCache;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Game;
import com.watabou.noosa.audio.Music;
import com.watabou.noosa.audio.Sample;
import com.watabou.pixeldungeon.Dungeon;
import com.watabou.pixeldungeon.PixelDungeon;
import com.watabou.pixeldungeon.actors.Actor;
import com.watabou.pixeldungeon.actors.Char;
import com.watabou.pixeldungeon.actors.blobs.Blob;
import com.watabou.pixeldungeon.actors.blobs.Fire;
import com.watabou.pixeldungeon.actors.blobs.ToxicGas;
import com.watabou.pixeldungeon.actors.buffs.Buff;
import com.watabou.pixeldungeon.actors.buffs.Burning;
import com.watabou.pixeldungeon.actors.hero.Hero;
import com.watabou.pixeldungeon.actors.hero.HeroClass;
import com.watabou.pixeldungeon.actors.mobs.Mob;
import com.watabou.pixeldungeon.levels.Level;
import com.watabou.pixeldungeon.scenes.GameScene;
import com.watabou.pixeldungeon.scenes.StartScene;
import com.watabou.pixeldungeon.sprites.HeadlessSprite;
import com.watabou.utils.BitmapCache;
import com.watabou.utils.Random;

/**
 * Headless Pixel Dungeon 1.9.1 scenario runner.
 *
 *   java -cp build/classes sim.Sim melee rat   [runs] [--seed N] [--trace] [--class warrior]
 *   java -cp build/classes sim.Sim melee crab  [runs] ...
 *   java -cp build/classes sim.Sim burn  door|grass|floor [runs] ...
 *   java -cp build/classes sim.Sim gas   [runs] ...
 */
public class Sim {

	// ------------------------------------------------------------------ hero with damage log

	/** Hero that records every damage instance it actually takes (after resistances). */
	public static class SimHero extends Hero {
		public int dmgTotal = 0;
		public int dmgEvents = 0;
		public final Map<String, int[]> bySrc = new TreeMap<String, int[]>(); // name -> {events, total}

		@Override
		public void damage( int dmg, Object src ) {
			int before = HP;
			super.damage( dmg, src );
			int taken = before - HP;
			if (before > 0) {
				dmgTotal += taken;
				dmgEvents++;
				String k = src == null ? "null" : src.getClass().getSimpleName();
				int[] a = bySrc.get( k );
				if (a == null) bySrc.put( k, a = new int[2] );
				a[0]++; a[1] += taken;
				if (TRACE) System.out.println( "    hero takes " + taken + " from " + k + " -> HP " + HP );
			}
		}
	}

	static boolean TRACE = false;
	static HeroClass HERO_CLASS = HeroClass.WARRIOR;

	// ------------------------------------------------------------------ engine bootstrap

	static boolean booted = false;

	static void boot() {
		if (booted) return;
		booted = true;
		Game.instance = new PixelDungeon();          // gives Preferences / file access stubs a Context
		TextureCache.context = BitmapCache.context = Game.instance;
		Game.width = 480; Game.height = 800;
		Camera.reset( new Camera( 0, 0, 480, 800, 1 ) );
		Sample.INSTANCE.enable( false );
		Music.INSTANCE.enable( false );
	}

	/** Fresh game + hero + the given level. Hero is placed at '@'. */
	static SimHero newGame( TestLevel level ) {
		boot();
		HeadlessSprite.clearPending();
		StartScene.curClass = HERO_CLASS;
		Dungeon.init();
		// replace the hero created by Dungeon.init() with our logging subclass, initialised identically
		SimHero hero = new SimHero();
		Dungeon.hero = hero;
		hero.live();
		HERO_CLASS.initHero( hero );

		Dungeon.depth = 1;
		Dungeon.level = level;
		level.create();
		return hero;
	}

	/** Puts mobs (already added to level.mobs) and the hero on the level, starts the actor system. */
	static void start( TestLevel level, SimHero hero ) {
		Dungeon.switchLevel( level, level.cell( '@' ) );
		Dungeon.nightMode = false;
		HeadlessSprite.attach( hero );
		for (Mob m : level.mobs) HeadlessSprite.attach( m );
		Dungeon.observe();
	}

	static Mob addMob( TestLevel level, Mob m, int pos, boolean awake ) {
		m.pos = pos;
		if (awake) m.state = m.HUNTING;
		level.mobs.add( m );
		return m;
	}

	// ------------------------------------------------------------------ turn loop

	/** Runs actors until the hero waits for input (or dies). */
	static void runUntilHeroReady() {
		Hero hero = Dungeon.hero;
		for (int guard = 0; guard < 1000000; guard++) {
			Actor.process();
			boolean ran = HeadlessSprite.flush();
			if (!hero.isAlive()) return;
			if (!ran && hero.ready) return;
		}
		throw new IllegalStateException( "actor loop stuck" );
	}

	interface Policy { void act( SimHero hero ); }

	/** Rest one turn (the 'z' / hourglass button, short wait). */
	static void rest( SimHero h ) { h.rest( false ); }

	/** Tap a cell (attack a mob there, walk there...) exactly like GameScene's default cell listener. */
	static void tap( SimHero h, int cell ) {
		if (h.handle( cell )) h.next();
	}

	// ------------------------------------------------------------------ scenarios

	static final String[] ROOM7 = {
		"#########",
		"#.......#",
		"#.......#",
		"#.......#",
		"#..@a...#",
		"#.......#",
		"#.......#",
		"#.......#",
		"#########",
	};

	static Mob makeMob( String kind ) {
		switch (kind) {
		case "rat":   return new com.watabou.pixeldungeon.actors.mobs.Rat();
		case "crab":  return new com.watabou.pixeldungeon.actors.mobs.Crab();
		case "gnoll": return new com.watabou.pixeldungeon.actors.mobs.Gnoll();
		default:
			try { return (Mob)Class.forName( "com.watabou.pixeldungeon.actors.mobs." + kind ).newInstance(); }
			catch (Exception e) { throw new IllegalArgumentException( "unknown mob " + kind ); }
		}
	}

	static void melee( String kind, int runs, boolean awake ) {
		int wins = 0, deaths = 0, timeouts = 0;
		long sumTurns = 0, sumTurnsWin = 0, sumHpLeftWin = 0, sumHeroDmg = 0, sumHeroHits = 0;
		int[] hpHist = new int[64];
		int heroHT = 0; String mobName = "";
		for (int r = 0; r < runs; r++) {
			TestLevel level = new TestLevel( ROOM7 );
			SimHero hero = newGame( level );
			final Mob mob = addMob( level, makeMob( kind ), level.cell( 'a' ), awake );
			mobName = mob.name;
			start( level, hero );
			heroHT = hero.HT;
			if (TRACE) System.out.println( "run " + r + ": hero HP " + hero.HP + "/" + hero.HT + " vs " + mob.name + " HP " + mob.HP );
			int turns = 0;
			while (turns < 500) {
				runUntilHeroReady();
				if (!hero.isAlive() || !mob.isAlive()) break;
				turns++;
				if (TRACE) System.out.println( "  hero action " + turns + ": hero HP " + hero.HP + ", " + mob.name + " HP " + mob.HP );
				if (Level.adjacent( hero.pos, mob.pos )) tap( hero, mob.pos ); else rest( hero );
			}
			sumTurns += turns;
			sumHeroDmg += hero.dmgTotal; sumHeroHits += hero.dmgEvents;
			if (!hero.isAlive()) deaths++;
			else if (!mob.isAlive()) {
				wins++; sumTurnsWin += turns; sumHpLeftWin += hero.HP;
				hpHist[Math.min( hero.HP, 63 )]++;
			} else timeouts++;
		}
		System.out.printf( Locale.US, "MELEE %s vs %s (%s), runs=%d%n", HERO_CLASS.title(), mobName, awake ? "mob awake/hunting" : "mob asleep", runs );
		System.out.printf( Locale.US, "  hero win rate     : %.2f%%  (wins %d, deaths %d, timeouts %d)%n", 100.0 * wins / runs, wins, deaths, timeouts );
		System.out.printf( Locale.US, "  mean hero actions : %.3f (all runs), %.3f (wins)%n", (double)sumTurns / runs, wins > 0 ? (double)sumTurnsWin / wins : 0 );
		System.out.printf( Locale.US, "  mean HP left (win): %.3f / %d%n", wins > 0 ? (double)sumHpLeftWin / wins : 0, heroHT );
		System.out.printf( Locale.US, "  mean dmg taken    : %.3f in %.3f hits/run%n", (double)sumHeroDmg / runs, (double)sumHeroHits / runs );
		StringBuilder sb = new StringBuilder( "  HP-left histogram (wins): " );
		for (int i = 0; i < 64; i++) if (hpHist[i] > 0) sb.append( i ).append( ':' ).append( hpHist[i] ).append( ' ' );
		System.out.println( sb );
	}

	static final String[] BURN_DOOR = {
		"#########",
		"#.......#",
		"#.......#",
		"#.......@....",
		"#.......#",
		"#.......#",
		"#########",
	};
	static final String[] BURN_GRASS = {
		"#########",
		"#.......#",
		"#..\"\"\"..#",
		"#..\"@\"..#",
		"#..\"\"\"..#",
		"#.......#",
		"#########",
	};
	static final String[] BURN_FLOOR = {
		"#########",
		"#.......#",
		"#.......#",
		"#...@...#",
		"#.......#",
		"#.......#",
		"#########",
	};

	static void burn( String where, int runs, int fireSeed ) {
		String[] art; char tile;
		switch (where) {
		case "door":  art = BURN_DOOR;  tile = '+'; break;
		case "grass": art = BURN_GRASS; tile = '"'; break;
		case "floor": art = BURN_FLOOR; tile = '.'; break;
		default: throw new IllegalArgumentException( where );
		}
		long sumBurnTicks = 0, sumDmg = 0, sumTurns = 0, sumFireDmg = 0; int deaths = 0;
		int[] tickHist = new int[200];
		int terrainBefore = -1; Map<Integer,Integer> terrHist = new TreeMap<Integer,Integer>();
		for (int r = 0; r < runs; r++) {
			TestLevel level = new TestLevel( tile, art );
			SimHero hero = newGame( level );
			start( level, hero );
			int pos = hero.pos;
			Dungeon.level.press( pos, hero );   // as if the hero had stepped here (DOOR -> OPEN_DOOR)
			if (r == 0) terrainBefore = Dungeon.level.map[pos];
			// set the hero on fire
			Buff.affect( hero, Burning.class ).reignite( hero );
			if (fireSeed > 0) GameScene.add( Blob.seed( pos, fireSeed, Fire.class ) );
			int turns = 0;
			while (turns < 300) {
				runUntilHeroReady();
				if (!hero.isAlive()) break;
				Blob fire = Dungeon.level.blobs.get( Fire.class );
				boolean fireLeft = fire != null && fire.volume > 0;
				if (hero.buff( Burning.class ) == null && !fireLeft) break;
				rest( hero );
				turns++;
			}
			int[] b = hero.bySrc.get( "Burning" );
			int ticks = b == null ? 0 : b[0];
			sumBurnTicks += ticks; tickHist[Math.min( ticks, 199 )]++;
			sumDmg += hero.dmgTotal; sumTurns += turns;
			if (b != null) sumFireDmg += b[1];
			if (!hero.isAlive()) deaths++;
			Integer t = Dungeon.level.map[pos];
			terrHist.put( t, terrHist.getOrDefault( t, 0 ) + 1 );
			if (TRACE) System.out.println( "run " + r + ": burn ticks " + ticks + ", dmg " + hero.dmgTotal + ", turns " + turns + ", HP " + hero.HP + "/" + hero.HT + " " + hero.bySrc.keySet() );
		}
		System.out.printf( Locale.US, "BURN hero(%s) standing on %s, initial Fire seed %d, resting; runs=%d%n", HERO_CLASS.title(), where, fireSeed, runs );
		System.out.printf( Locale.US, "  mean burn ticks (Burning dmg events): %.3f%n", (double)sumBurnTicks / runs );
		System.out.printf( Locale.US, "  mean Burning damage                 : %.3f%n", (double)sumFireDmg / runs );
		System.out.printf( Locale.US, "  mean total damage taken             : %.3f%n", (double)sumDmg / runs );
		System.out.printf( Locale.US, "  mean hero turns until fire out      : %.3f   deaths: %d%n", (double)sumTurns / runs, deaths );
		StringBuilder sb = new StringBuilder( "  burn-ticks histogram: " );
		for (int i = 0; i < 200; i++) if (tickHist[i] > 0) sb.append( i ).append( ':' ).append( tickHist[i] ).append( ' ' );
		System.out.println( sb );
		System.out.println( "  terrain under hero: before=" + terrainBefore + ", afterwards (Terrain id:count) " + terrHist + "   [1=EMPTY 2=GRASS 5=DOOR 6=OPEN_DOOR 9=EMBERS]" );
	}

	static final String[] GAS_ROOM = {
		"#########",
		"#.......#",
		"#.......#",
		"#.......#",
		"#.@.a...#",
		"#.......#",
		"#.......#",
		"#.......#",
		"#########",
	};

	static void gas( int runs, int amount ) {
		long sumDmg = 0, sumHits = 0, sumTurns = 0; int deaths = 0;
		int[] hist = new int[200];
		for (int r = 0; r < runs; r++) {
			TestLevel level = new TestLevel( GAS_ROOM );
			SimHero hero = newGame( level );
			start( level, hero );
			GameScene.add( Blob.seed( level.cell( 'a' ), amount, ToxicGas.class ) );
			int turns = 0;
			while (turns < 1000) {
				runUntilHeroReady();
				if (!hero.isAlive()) break;
				Blob g = Dungeon.level.blobs.get( ToxicGas.class );
				if (g == null || g.volume <= 0) break;
				rest( hero );
				turns++;
			}
			int[] t = hero.bySrc.get( "ToxicGas" );
			int dmg = t == null ? 0 : t[1];
			sumDmg += dmg; sumHits += t == null ? 0 : t[0]; sumTurns += turns;
			hist[Math.min( dmg, 199 )]++;
			if (!hero.isAlive()) deaths++;
			if (TRACE) System.out.println( "run " + r + ": gas dmg " + dmg + " in " + (t == null ? 0 : t[0]) + " hits, turns " + turns + ", HP " + hero.HP + "/" + hero.HT );
		}
		System.out.printf( Locale.US, "TOXIC GAS seed %d at room centre, hero(%s) 2 cells away resting; runs=%d%n", amount, HERO_CLASS.title(), runs );
		System.out.printf( Locale.US, "  mean ToxicGas damage : %.3f in %.3f hits%n", (double)sumDmg / runs, (double)sumHits / runs );
		System.out.printf( Locale.US, "  mean turns until gas gone: %.3f   deaths: %d%n", (double)sumTurns / runs, deaths );
		StringBuilder sb = new StringBuilder( "  damage histogram: " );
		for (int i = 0; i < 200; i++) if (hist[i] > 0) sb.append( i ).append( ':' ).append( hist[i] ).append( ' ' );
		System.out.println( sb );
	}

	// ------------------------------------------------------------------ main

	public static void main( String[] args ) {
		java.util.List<String> pos = new java.util.ArrayList<String>();
		Long seed = null; boolean awake = true; int fireSeed = 0; int gasAmount = 300;
		for (int i = 0; i < args.length; i++) {
			switch (args[i]) {
			case "--seed": seed = Long.parseLong( args[++i] ); break;
			case "--trace": TRACE = true; HeadlessSprite.TRACE = true; android.util.Log.ENABLED = true; break;
			case "--class": HERO_CLASS = HeroClass.valueOf( args[++i].toUpperCase() ); break;
			case "--asleep": awake = false; break;
			case "--fire": fireSeed = Integer.parseInt( args[++i] ); break;
			case "--amount": gasAmount = Integer.parseInt( args[++i] ); break;
			default: pos.add( args[i] );
			}
		}
		if (seed != null) Random.seed( seed );
		if (pos.isEmpty()) {
			System.out.println( "usage: sim.Sim melee <rat|crab|MobClass> [runs] | burn <door|grass|floor> [runs] | gas [runs]" );
			System.out.println( "  options: --seed N  --trace  --class warrior|mage|rogue|huntress  --asleep  --fire N (initial Fire seed for burn)  --amount N (gas)" );
			return;
		}
		String cmd = pos.get( 0 );
		long t0 = System.currentTimeMillis();
		switch (cmd) {
		case "melee":
			melee( pos.get( 1 ), pos.size() > 2 ? Integer.parseInt( pos.get( 2 ) ) : 2000, awake );
			break;
		case "burn":
			burn( pos.get( 1 ), pos.size() > 2 ? Integer.parseInt( pos.get( 2 ) ) : 2000, fireSeed );
			break;
		case "gas":
			gas( pos.size() > 1 ? Integer.parseInt( pos.get( 1 ) ) : 2000, gasAmount );
			break;
		default:
			System.out.println( "unknown scenario " + cmd );
		}
		System.out.printf( Locale.US, "  (%.1fs)%n", (System.currentTimeMillis() - t0) / 1000.0 );
	}
}
