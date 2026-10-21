package pebxel;

import com.badlogic.gdx.Files;
import com.badlogic.gdx.Gdx;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.Badges;
import com.shatteredpixel.shatteredpixeldungeon.Rankings;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.utils.GLog;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndOptions;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import com.watabou.utils.Signal;
import java.util.*;

/**
 * Plays Shattered PD by the Assistant alone: whenever the hero is ready, plan A ({@link Assist#plan}) is acted on
 * ({@link Assist#act}), as if the player only ever pressed Select. Windows are answered (closed, or their first button).
 * Args: class (0 warrior, 1 mage, 2 rogue, 3 huntress, 4 duelist, 5 cleric), seed, maxTurns.
 * Env: MODE (assist 1 near / 2 far, default 1), ITEMS (0/1, default 1), MAXDEPTH (default 26), V=1 (every decision),
 * SIMSEED=n (repeatable games: see Lockstep), CHEAT=1 (the whole-game smoke test: Cheat, BotWindows, BotItems, BotQuests,
 * BotAlchemy, Rescue), WATCH=1 (played through the watch: WatchDriver), STARTDEPTH=n (cheat: straight down to n),
 * ASCEND=0 (cheat: win at the Amulet instead of climbing out), MINES=0 (keep out of the Blacksmith's mine), MAXFRAMES,
 * MAXWATCHFRAMES. Frames are a fixed 1/30 s of game time, in lockstep with the actor thread: no wall clock decides anything.
 * Ends with "COVER ..." (sim/cover.py adds them up) and "RESULT ..." lines for scripts.
 */
public class AssistBot {
	static WatchApp app;
	static boolean v;
	static final boolean WATCH = "1".equals(System.getenv("WATCH"));
	static WatchDriver watch;
	/** FAST (default unless WATCH; FAST=0 for the old way): frames of FRAMEDT (default 0.2 s, the most Shattered itself
	 *  takes in one frame) in place of 1/30 s, and emitters make no particles (PARTICLES=1 keeps them). Nothing the
	 *  game decides waits on wall time or frame count, only on animations ending, and those end in a frame or two
	 *  (a step's 0.2 s is one frame). Idea from Shatterfish's SceneStepper (github.com/watchthelight/shatterfish, GPL-3),
	 *  which steps headless Shattered at 0.2 s a frame. */
	static final boolean FAST = !WATCH && !"0".equals(System.getenv("FAST"));
	static void step() { if (watch != null) watch.frame(); else Lockstep.frame(app); }
	static Object lastScene; static int sceneFrames, branch;
	static final ArrayDeque<String> log = new ArrayDeque<>(), recent = new ArrayDeque<>();

	/** The game being played (for the exception handler and the watchdog). */
	static volatile HeroClass curCls; static volatile long curSeed;

	/**
	 * One game (args: class seed maxTurns), or with "--games FIRST END" (sim/simbot) games FIRST..END-1 one after
	 * another in this one JVM: class CLS (default the game's number mod 6), seed SEEDBASE (default 1000) + number, turn
	 * limit MAXTURNS (default 200000), each between "== game N class C seed S at T" and "== game N ended at T" lines.
	 * A warm JVM plays a short game several times quicker than a new one (no start-up, the JIT's work kept), and a game
	 * plays the same either way: every game starts from the same state (Statics, newProfile). After an ending that
	 * leaves the game's threads in doubt (fatal) the JVM stops, and simbot starts another for the games left.
	 */
	public static void main(String[] a) throws Exception {
		boolean batch = a.length > 0 && a[0].equals("--games") && !WATCH;
		int first = batch ? Integer.parseInt(a[1]) : 0, last = batch ? Integer.parseInt(a[2]) : 1;
		v = System.getenv("V") != null;
		String dir0 = ".pebxel-assistbot-" + ProcessHandle.current().pid() + System.getenv().getOrDefault("SAVETAG", "");
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame();
		game.drawAlways = WATCH;   // (the link needs the drawing code run: the fog and some pictures update while drawn)
		app = new WatchApp(game, dir0, WATCH);
		if (WATCH) app.graphics.soft.raster = false;
		if (FAST) {   // (the default without the watch: see FAST below)
			app.graphics.delta = Float.parseFloat(System.getenv().getOrDefault("FRAMEDT", "0.2"));
			com.watabou.noosa.particles.Emitter.pebxelOff = !"1".equals(System.getenv("PARTICLES"));
		}
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir0 + "/");
		app.create();
		Lockstep.watchdog(() -> "class " + curCls + " seed " + curSeed);
		if (WATCH) watch = new WatchDriver(app);
		for (int i = 0; i < 5; i++) step();
		// (any thread dying of an exception ends the game with a report: the render thread dying once left the actor
		// thread waiting for ever, and the game hung, using no CPU, until it was cancelled)
		Thread.setDefaultUncaughtExceptionHandler((th, ex) -> { System.out.println("UNCAUGHT in " + th.getName() + ": " + ex); ex.printStackTrace(System.out);
			System.out.println("RESULT class " + curCls + " seed " + curSeed + ": crashed (" + ex + " in " + th.getName() + ")"); System.out.flush(); Runtime.getRuntime().halt(4); });
		Statics.snapshot();
		for (int g = first; g < last; g++) {
			HeroClass cls; long seed; int maxTurns;
			if (batch) {
				cls = HeroClass.values()[System.getenv("CLS") != null ? Integer.parseInt(System.getenv("CLS")) : g % 6];
				seed = Long.parseLong(System.getenv().getOrDefault("SEEDBASE", "1000")) + g;
				maxTurns = Integer.parseInt(System.getenv().getOrDefault("MAXTURNS", "200000"));
				System.out.println("== game " + g + " class " + cls.ordinal() + " seed " + seed + " at " + System.currentTimeMillis() / 1000);
			} else {
				cls = HeroClass.values()[a.length > 0 ? Integer.parseInt(a[0]) : 0];
				seed = a.length > 1 ? Long.parseLong(a[1]) : 1;
				maxTurns = a.length > 2 ? Integer.parseInt(a[2]) : 3000;
			}
			String end = play(cls, seed, maxTurns);
			if (batch) System.out.println("== game " + g + " ended at " + System.currentTimeMillis() / 1000);
			System.out.flush();
			if (fatal(end)) break;
			if (g + 1 < last) { Game.switchScene(com.watabou.noosa.Scene.class); for (int i = 0; i < 3; i++) step(); }   // (the game's scene and windows gone)
			try { Gdx.files.external(saveDir).deleteDirectory(); Gdx.files.local(saveDir).deleteDirectory(); } catch (Throwable e) { }
		}
		try { Gdx.files.external(saveDir).deleteDirectory(); Gdx.files.local(saveDir).deleteDirectory(); Gdx.files.local(dir0).deleteDirectory(); } catch (Throwable e) { }
		System.exit(0);
	}

	/** Endings after which the game's threads can't be trusted to start another game in this JVM. */
	static boolean fatal(String end) {
		return end.startsWith("runaway") || end.startsWith("bot exception") || end.startsWith("hung") || end.contains("between floors");
	}

	/**
	 * A fresh profile for the next game, as a new JVM would have it: its own save folder and settings, and what one game
	 * leaves in Shattered's statics for the next put back. The list is Shatterfish's (RunStatics and Profile in
	 * github.com/watchthelight/shatterfish, GPL-3), checked against v4.0.2: the journal's guide pages decide what a floor
	 * drops, a dead hero's bones would turn up in the next game, and so on.
	 */
	static void newProfile(String dir) throws Exception {
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		app.newPrefs(dir);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		com.shatteredpixel.shatteredpixeldungeon.levels.features.Chasm.jumpConfirmed = false;
		com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.darts.TippedDart.lostDarts = 0;
		com.shatteredpixel.shatteredpixeldungeon.items.trinkets.WondrousResin.forcePositive = false;
		com.shatteredpixel.shatteredpixeldungeon.actors.hero.abilities.Ratmogrify.useRatroicEnergy = false;
		com.shatteredpixel.shatteredpixeldungeon.journal.Bestiary.skipCountingEncounters = false;
		setStatic(com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Snake.class, "dodges", 0);
		setStatic(com.shatteredpixel.shatteredpixeldungeon.actors.mobs.GnollGeomancer.class, "rocksInFlight", 0);
		((Collection<?>) getStatic(com.shatteredpixel.shatteredpixeldungeon.actors.mobs.GnollGeomancer.class, "knockedChars")).clear();
		setStatic(com.shatteredpixel.shatteredpixeldungeon.actors.Char.class, "hitMissIcon", -1);
		setStatic(com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.Flail.class, "spinBoost", 0);
		setStatic(com.shatteredpixel.shatteredpixeldungeon.items.rings.RingOfWealth.class, "latestDropTier", 0);
		Dungeon.customSeedText = "";
		boolean daily = Dungeon.daily; Dungeon.daily = true;   // (Bones has no reset: a daily game's leave() puts it back to "none")
		try { com.shatteredpixel.shatteredpixeldungeon.Bones.leave(); } finally { Dungeon.daily = daily; }
		setStatic(Badges.class, "global", null); setStatic(Badges.class, "saveNeeded", false); Badges.loadGlobal();
		for (com.shatteredpixel.shatteredpixeldungeon.journal.Document d : com.shatteredpixel.shatteredpixeldungeon.journal.Document.values())
			for (String p : new ArrayList<>(d.pageNames())) d.deletePage(p);
		for (Object c : com.shatteredpixel.shatteredpixeldungeon.journal.Catalog.values()) { zero(c, "seen", false); zero(c, "useCount", 0); }
		for (Object c : com.shatteredpixel.shatteredpixeldungeon.journal.Bestiary.values()) { zero(c, "seen", false); zero(c, "encounterCount", 0); }
		setStatic(com.shatteredpixel.shatteredpixeldungeon.journal.Journal.class, "saveNeeded", false);
		Rankings.INSTANCE.records = null;
		for (int s = 1; s <= GamesInProgress.MAX_SLOTS; s++) GamesInProgress.setUnknown(s);
		com.shatteredpixel.shatteredpixeldungeon.ui.ActionIndicator.clearAction();
		setStatic(Actor.class, "pebxelHashes", 0);   // (actors numbered from 1 again)
	}
	static Object getStatic(Class<?> c, String n) throws Exception { java.lang.reflect.Field f = c.getDeclaredField(n); f.setAccessible(true); return f.get(null); }
	static void setStatic(Class<?> c, String n, Object v) throws Exception { java.lang.reflect.Field f = c.getDeclaredField(n); f.setAccessible(true); f.set(null, v); }
	@SuppressWarnings("unchecked")
	static void zero(Object o, String n, Object v) throws Exception { java.lang.reflect.Field f = o.getClass().getDeclaredField(n); f.setAccessible(true); ((Map<Object, Object>) f.get(o)).replaceAll((k, x) -> v); }

	static String play(HeroClass cls, long seed, int maxTurns) throws Exception {
		Statics.restore();   // (our own classes' statics as before the first game)
		curCls = cls; curSeed = seed;
		int maxDepth = Integer.parseInt(System.getenv().getOrDefault("MAXDEPTH", "26"));
		// (its own save folder: two runs of one job play the same seeds at once, and shared folders broke their saves)
		String dir = ".pebxel-assistbot-" + cls.ordinal() + "-" + seed + "-" + ProcessHandle.current().pid() + System.getenv().getOrDefault("SAVETAG", "");
		saveDir = dir;
		newProfile(dir);
		Lockstep.seed(seed);
		Lockstep.gameStart = System.nanoTime();
		long frames0 = Lockstep.frames;
		SPDSettings.intro(false);
		WatchSettings.assist(Integer.parseInt(System.getenv().getOrDefault("MODE", "1")));
		WatchSettings.itemAssist(!"0".equals(System.getenv("ITEMS")));
		Assist.reset();
		BotWindows.newGame(seed); BotItems.newGame(); Coverage.newGame(); BotAlchemy.newGame();
		Random talentR = new Random(seed * 13 + 5);

		GamesInProgress.selectedClass = cls; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = seed;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);

		Signal.Listener<String> logL = s -> { synchronized (log) { log.addLast(s); while (log.size() > 8) log.removeFirst(); } return false; };
		int maxD = 0, depth = 0, decisions = 0, stuckEvents = 0, sameRun = 0, picked = 0, actFails = 0, windows = 0;
		float floorStart = 0; int lastPos = -1; String lastKey = "";
		Map<String, Integer> whys = new TreeMap<>();
		Map<Integer, Integer> depthDecisions = new HashMap<>();
		Heap pickHeap = null; int pickSize = 0;
		String end = "turn limit";
		long planNs = 0, plans = 0, maxPlanNs = 0;
		Window lastWnd = null; int wndFrames = 0;
		long t0 = System.currentTimeMillis();
		long readyF = 0, betweenF = 0, floorDecisions = 0, floorFrames = 0; float readyNow = -1;   // (the hang check counts frames in which game time stood still: no wall clock)
		try {
		for (long f = 0; f < 20_000_000; f++) {
			Lockstep.doing = "frame";
			step();
			if (f % 20000 == 0 && f > 0) { Window hw = Ui.topWindow(); System.out.printf("  .. frame %d depth %d %s%s decisions %d %.0fs%n", f, Dungeon.depth, Game.scene().getClass().getSimpleName(), hw == null ? "" : " window " + hw.getClass().getName(), decisions, (System.currentTimeMillis() - t0) / 1000.0); }
			if (Lockstep.stuck != null) { end = "runaway: " + Lockstep.stuck; break; }
			if (Game.scene() instanceof InterlevelScene) {
				if (Ui.topWindow() != null) { end = "error between floors: " + Ui.topWindow().getClass().getName(); break; }
				if (++betweenF > HANG_FRAMES) { end = "stuck between floors"; break; }
				if (watch != null) { if (f % 40 == 39) watch.press(Controls.SELECT); }   // (the story pages: Select, as on the watch)
				else if (f % 3 == 0) HandoffTest.pressOn();
				fk[0]++; continue;
			}
			betweenF = 0;
			if (Statistics.gameWon || Game.scene() instanceof com.shatteredpixel.shatteredpixeldungeon.scenes.SurfaceScene) { end = "WIN"; break; }
			if (!(Game.scene() instanceof GameScene) && Ui.topWindow() == null) {   // another screen (the Amulet's, alchemy): answered like a window
				Coverage.scene(Game.scene());
				if (Game.scene() != lastScene) { lastScene = Game.scene(); sceneFrames = 0; }
				if (++sceneFrames % 30 == 2) { BotWindows.Answer an = BotWindows.decideScene(Game.scene()); System.out.println("  SCENE " + Game.scene().getClass().getSimpleName() + " -> " + an);
					if (watch == null || !watch.answerScene(Game.scene(), an)) { if (an.g != null) Ui.press(an.g); else com.watabou.noosa.NoosaAccess.back(Game.scene()); } }
				if (sceneFrames > 3000) { end = "stuck on " + Game.scene().getClass().getSimpleName(); break; }
				fk[1]++; continue;
			}
			if (Dungeon.hero == null) continue;
			if (!Dungeon.hero.isAlive()) { end = "died"; break; }
			if (Dungeon.depth != depth || Dungeon.branch != branch) {
				if (depth > 0) System.out.printf("  floor %d done in %.0f turns%n", depth, Statistics.duration - floorStart);
				depth = Dungeon.depth; branch = Dungeon.branch; floorDecisions = 0; floorFrames = 0; maxD = Math.max(maxD, depth); floorStart = Statistics.duration;
				GLog.update.remove(logL); GLog.update.add(logL);
				System.out.printf("depth %d  turn %.0f  hp %d/%d  lvl %d  str %d  %.1fs%n", depth, Statistics.duration, Dungeon.hero.HP, Dungeon.hero.HT, Dungeon.hero.lvl, Dungeon.hero.STR(), (System.currentTimeMillis() - t0) / 1000.0);
				if (depth > maxDepth) { end = "depth limit"; break; }
				if (v) { StringBuilder sb = new StringBuilder("  level: heaps"); for (Heap h : Dungeon.level.heaps.valueList()) sb.append(' ').append(h.pos).append(':').append(h.peek().getClass().getSimpleName());
					sb.append(" mobs"); for (com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob m : Dungeon.level.mobs) sb.append(' ').append(m.pos).append(':').append(m.getClass().getSimpleName()); System.out.println(sb); }
			}
			if (Statistics.duration > maxTurns) { end = "turn limit"; break; }
			if (watch != null && watch.frames > WATCH_FRAMES) { end = "game frame limit (" + WATCH_FRAMES + " watch frames)"; break; }
			if (f > GAME_FRAMES) { end = "game frame limit (" + GAME_FRAMES + ")"; break; }
			if (++floorFrames > (Dungeon.bossLevel() ? FLOOR_FRAMES * 3 : FLOOR_FRAMES)) { end = "floor timeout (" + FLOOR_FRAMES + " frames on depth " + depth + ")"; break; }
			if (Statistics.duration - floorStart > FLOOR_TURNS) { end = "floor timeout (" + FLOOR_TURNS + " turns on depth " + depth + ")"; break; }
			Window w = Ui.topWindow();
			if (w != null) {
				if (w != lastWnd) { lastWnd = w; wndFrames = 0; windows++; }
				wndFrames++;
				// answered on its 2nd frame (laid out by then), again every 30 frames it stays; for ever: gone
				if (wndFrames % 30 == 2) { Lockstep.doing = "window " + w.getClass().getName(); BotWindows.Answer an = BotWindows.decide(w); if (v) System.out.println("    -> " + an); if (watch == null || !watch.answer(w, an)) { if (Ui.topWindow() == w) BotWindows.doDirect(w, an); } }
				else if (wndFrames > 400 || BotWindows.sinceTurn > 40) { if (BotWindows.sinceTurn > 40) { Coverage.problem("windows round and round: " + w.getClass().getName()); BotWindows.sinceTurn = 0; while (Ui.topWindow() != null) Ui.topWindow().hide(); continue; } System.out.println("  WINDOW STUCK " + w.getClass().getName() + ": hidden"); Coverage.problem("window stuck: " + w.getClass().getName()); w.hide(); }
				fk[2]++; continue;
			}
			lastWnd = null;
			if (!Dungeon.hero.ready) {
				if (Actor.now() != readyNow) { readyNow = Actor.now(); readyF = f; }
				if (f - readyF > HANG_FRAMES) {
					end = "hung (hero not ready, game time stood still for " + HANG_FRAMES + " frames)";
					System.out.println("HUNG: action " + Dungeon.hero.curAction + " sprite " + Dungeon.hero.sprite + " anim " + (Dungeon.hero.sprite == null ? null : Dungeon.hero.sprite.parent));
					for (Map.Entry<Thread, StackTraceElement[]> e : Thread.getAllStackTraces().entrySet()) { System.out.println(" thread " + e.getKey().getName()); for (int i = 0; i < Math.min(12, e.getValue().length); i++) System.out.println("    " + e.getValue()[i]); }
					break;
				}
				fk[3]++; continue;
			}
			readyF = f; readyNow = Actor.now();
			Coverage.look(); BotWindows.turn(); BotItems.settle();
			if (Cheat.ON && Dungeon.depth < START_DEPTH && !Dungeon.bossLevel() && Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.Amulet.class) == null) {   // (tests: straight down to START_DEPTH)
				com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(false);
				if (tr != null) { Cheat.apply(); StairsTest.jump(tr); continue; }
			}
			if (Cheat.ON) { Cheat.apply(); if (watch == null || !watch.talents()) Cheat.spendTalents(talentR); if (Ui.topWindow() != null) continue; }
			if (Controls.aiming()) {   // (left over: aim at plan A of the aiming list, else cancel)
				List<Assist.Target> al = Assist.plan(true, null);
				if (!al.isEmpty() && al.get(0).kind == Assist.ENEMY) { if (watch == null || !watch.act(al.get(0))) Assist.act(al.get(0)); }
				else if (watch != null) watch.press(Controls.BACK); else GameScene.cancelCellSelector();
				continue;
			}
			if (pickHeap != null) { if (Dungeon.level.heaps.get(pickHeap.pos) != pickHeap || pickHeap.size() < pickSize) picked++; pickHeap = null; }
			long n0 = System.nanoTime();
			int hx = Dungeon.hero.pos % Dungeon.level.width(), hy = Dungeon.hero.pos / Dungeon.level.width(), W = Dungeon.level.width();
			List<Assist.Target> plan = Assist.plan(false, c -> { int dx = c % W - hx, dy = c / W - hy; return dx >= -6 && dx <= 4 && dy >= -4 && dy <= 4; });
			long dn = System.nanoTime() - n0; planNs += dn; plans++; maxPlanNs = Math.max(maxPlanNs, dn);
			if (Assist.lastError != null) { System.out.println("  ASSIST ERROR " + Assist.lastError); Assist.lastError.printStackTrace(System.out); Assist.lastError = null; }
			decisions++;
			for (int k = 0; k < PERTURB; k++) System.identityHashCode(new Object());   // (tests: the game must not care)
			if (depthDecisions.merge(depth, 1, Integer::sum) > FLOOR_TURNS * 2) { end = "depth timeout (" + FLOOR_TURNS * 2 + " decisions on depth " + depth + " over all visits)"; break; }
			if (++floorDecisions > FLOOR_TURNS) { end = "floor timeout (" + FLOOR_TURNS + " decisions on depth " + depth + ")"; break; }
			// (cheat games, in this order: a quest step, an item kind to try now and then, the rescue from a stall, the pot)
			Assist.Target t = Cheat.ON ? BotQuests.pick() : null;
			if (t == null && Cheat.ON) t = BotItems.pick();
			Assist.Target rs = Cheat.ON ? Rescue.check(t == null) : null;   // (every decision: it counts the stall)
			if (t == null) t = rs;
			if (t == null && Cheat.ON) t = BotAlchemy.pick();
			if (t == null && plan.isEmpty()) { Dungeon.hero.rest(false); continue; }
			if (t == null) t = plan.get(0);
			String key = t.kind + ":" + t.cell + ":" + t.action + ":" + (t.item == null ? "" : t.item.getClass().getSimpleName());
			boolean still = Dungeon.hero.pos == lastPos && key.equals(lastKey) && t.kind != Assist.WAIT && !"wait".equals(t.action);
			if (still) { stuckEvents++; sameRun++; } else sameRun = 0;
			if (sameRun >= (t.kind == Assist.ENEMY ? 300 : 50) && Cheat.ON && !Rescue.on) { Coverage.problem("assistant loop on depth " + depth + ": " + t.why); Rescue.force(); sameRun = 0; }
			if (sameRun >= (t.kind == Assist.ENEMY ? 300 : 50)) {
				end = "loop at " + t;
				com.shatteredpixel.shatteredpixeldungeon.actors.Char ch = t.cell >= 0 ? Actor.findChar(t.cell) : null;
				if (ch instanceof com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob) { com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob m = (com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob) ch;
					end += " (" + m.getClass().getSimpleName() + " hp " + m.HP + "/" + m.HT + " state " + (m.state == null ? null : m.state.getClass().getSimpleName()) + " alignment " + m.alignment + ")"; }
				StringBuilder ms = new StringBuilder("  mobs at the loop:");
				for (com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob m : Dungeon.level.mobs) ms.append(' ').append(m.getClass().getSimpleName()).append('@').append(m.pos).append(" hp").append(m.HP).append(m.isInvulnerable(Dungeon.hero.getClass()) ? " INV" : "").append(m.alignment == com.shatteredpixel.shatteredpixeldungeon.actors.Char.Alignment.ENEMY ? "" : " " + m.alignment).append(',');
				System.out.println(ms + " hero@" + Dungeon.hero.pos);
				break;
			}
			lastPos = Dungeon.hero.pos; lastKey = key;
			whys.merge(t.why, 1, Integer::sum);
			if (v && System.getenv("VMOBS") != null) { StringBuilder sb = new StringBuilder("    mobs"); for (com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob m : Dungeon.level.mobs) sb.append(' ').append(m.pos).append(':').append(m.HP); sb.append(" f").append(Lockstep.frames); System.out.println(sb); }
			String line = String.format("  t%.0f/%.0f pos %d hp %d/%d: %s%s", Statistics.duration, Actor.now(), Dungeon.hero.pos, Dungeon.hero.HP, Dungeon.hero.HT, t, plan.size() > 1 ? "   | B: " + plan.get(1) : "");
			if (VRND) line += " r" + rnd() + " f" + Lockstep.frames + " w" + Lockstep.waits;
			recent.addLast(line); while (recent.size() > 12) recent.removeFirst();
			if (v) System.out.println(line);
			if (t.kind == Assist.PICKUP) { Heap hh = Dungeon.level.heaps.get(t.cell); if (hh != null && t.cell == Dungeon.hero.pos) { pickHeap = hh; pickSize = hh.size(); } }
			float tn = Actor.now(); int pn = Dungeon.hero.pos;
			if (t.kind == Assist.USE && t.item != null) BotItems.pending(t.item, t.action);
			Lockstep.doing = "acting on " + t + " at depth " + Dungeon.depth;
			if (decisions == TEST_HANG) synchronized (AssistBot.class) { try { AssistBot.class.wait(); } catch (InterruptedException e) { } }   // (tests: the watchdog)
			boolean ok = watch != null && Controls.aiming() == false && watch.act(t);
			if (!ok && Ui.topWindow() == null && Dungeon.hero.ready && Actor.now() == tn && Dungeon.hero.pos == pn) ok = Assist.act(t);
			// (the actor thread picks the action up on a later frame: until then hero.ready still reads true. Once it has
			// and the action came to nothing, the hero is the actor waiting again with no action: no use waiting longer.
			// Before, 150 frames were played out each time, for a fight with a foe out of reach half a cheat game's frames)
			long fa = fk[4];
			for (int k = 0; k < 150 && Dungeon.hero.ready && Actor.now() == tn && Dungeon.hero.pos == pn && Ui.topWindow() == null && Dungeon.hero.isAlive()
					&& !(k > 0 && Dungeon.hero.curAction == null && Actor.processing()); k++) { step(); fk[4]++; }
			if (FRAMESBY) { fkWhy.merge(t.why, fk[4] - fa, Long::sum); if (fk[4] - fa >= 150) fkWhy.merge("150: " + t.why, 1L, Long::sum); }
			if (!ok) { actFails++; if (Assist.lastError != null) { System.out.println("  ACT ERROR " + Assist.lastError); Assist.lastError.printStackTrace(System.out); Assist.lastError = null; } Dungeon.hero.rest(false); }
		}
		} catch (Throwable ex) {
			System.out.println("BOT EXCEPTION: " + ex); ex.printStackTrace(System.out);
			end = "bot exception (" + ex.toString().replace(';', ',') + ")";
			Coverage.problem("bot exception: " + ex);
		}
		if (Dungeon.hero != null) System.out.println("state: ready " + Dungeon.hero.ready + " action " + Dungeon.hero.curAction + " aiming " + Controls.aiming() + " window " + Ui.topWindow() + " scene " + Game.scene() + " resting " + Dungeon.hero.resting + " now " + Actor.now());
		String cause = "";
		if (end.equals("died")) synchronized (log) { cause = String.join(" / ", log); }
		System.out.println("decisions by reason: " + whys);
		System.out.printf("frames: between floors %d, other screens %d, windows %d, hero busy %d, after acting %d; decisions %d%n", fk[0], fk[1], fk[2], fk[3], fk[4], decisions);
		if (FRAMESBY) { List<Map.Entry<String, Long>> l = new ArrayList<>(fkWhy.entrySet()); l.sort((x, y) -> Long.compare(y.getValue(), x.getValue())); System.out.println("frames after acting, by reason: " + l.subList(0, Math.min(25, l.size()))); }
		System.out.printf("plan time: avg %.2f ms, max %.1f ms over %d plans%n", planNs / 1e6 / Math.max(1, plans), maxPlanNs / 1e6, plans);
		if (!end.equals("WIN") && !end.equals("died") && !v) { System.out.println("last decisions:"); for (String l : recent) System.out.println(l); }
		if (watch != null) System.out.println("WATCH " + watch.summary());
		Coverage.watch = watch == null ? "" : watch.summary();
		Coverage.report(end);
		System.out.printf("RESULT class %s seed %d: %s depth %d (max %d) turns %.0f lvl %d hp %d/%d picked %d stuck %d actfails %d windows %d decisions %d frames %d  %.1fs%s%n",
				cls, seed, end, Dungeon.depth, maxD, Statistics.duration, Dungeon.hero == null ? 0 : Dungeon.hero.lvl, Dungeon.hero == null ? 0 : Dungeon.hero.HP, Dungeon.hero == null ? 0 : Dungeon.hero.HT,
				picked, stuckEvents, actFails, windows, decisions, Lockstep.frames - frames0, (System.currentTimeMillis() - t0) / 1000.0, cause.isEmpty() ? "" : "\n  last log: " + cause);
		GLog.update.remove(logL);
		return end;
	}
	static String saveDir;
	static final Map<String, Long> fkWhy = new HashMap<>(); static final boolean FRAMESBY = System.getenv("FRAMESBY") != null;
	static final long[] fk = new long[5];   // (frames by what the bot waited for)

	static final int PERTURB = Integer.parseInt(System.getenv().getOrDefault("PERTURB", "0"));
	static final boolean VRND = System.getenv("VRND") != null;
	/** The random numbers' state (debugging repeatability: needs --add-opens java.base/java.util=ALL-UNNAMED and java.util.concurrent.atomic). */
	static String rnd() {
		try {
			java.lang.reflect.Field g = com.watabou.utils.Random.class.getDeclaredField("generators"); g.setAccessible(true);
			StringBuilder sb = new StringBuilder();
			for (Object r : (java.util.ArrayDeque<?>) g.get(null)) { java.lang.reflect.Field f = java.util.Random.class.getDeclaredField("seed"); f.setAccessible(true); sb.append(Long.toHexString(((java.util.concurrent.atomic.AtomicLong) f.get(r)).get() & 0xFFFFFF)).append('/'); }
			return sb.toString();
		} catch (Throwable e) { return e.toString(); }
	}
	static final int FLOOR_TURNS = Cheat.ON ? 6000 : 2500;
	static final int FLOOR_FRAMES = WATCH ? 200_000 : Cheat.ON ? 60_000 : 150_000;
	static final long WATCH_FRAMES = Long.parseLong(System.getenv().getOrDefault("MAXWATCHFRAMES", "900000"));
	static final long GAME_FRAMES = Long.parseLong(System.getenv().getOrDefault("MAXFRAMES", "1200000"));
	static final int TEST_HANG = Integer.parseInt(System.getenv().getOrDefault("TESTHANG", "-1"));
	static final int START_DEPTH = Integer.parseInt(System.getenv().getOrDefault("STARTDEPTH", "0"));   // (about 3 game hours on a floor)
	static final int HANG_FRAMES = 3000;   // 100 s of game time with the hero waiting and the clock not moving
}
