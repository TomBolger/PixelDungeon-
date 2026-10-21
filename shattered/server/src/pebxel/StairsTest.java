package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfTeleportation;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndMessage;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import com.watabou.utils.PathFinder;
import com.watabou.utils.Random;
import java.io.File;
import java.util.*;

/**
 * Up and down the stairs many times, the way the phone's watch game runs (WatchSession: a frame that throws is
 * noted and the game goes on; five in 3 s, or the link throwing, end it), with the stand-in watch applying every
 * message. Reports any exception, the watch's rough memory after each floor change, and (SHOTS=dir) pictures of
 * the loading screens as the watch shows them.
 * Env: SEED, CLASS (0-5), TRIPS (default 30), PATTERN (u/d letters repeated, default "ddud"; r = random),
 *      MOBS=n (monsters put round the hero after arriving), WND=1 (a window opens as the hero takes the stairs),
 *      PRESS=1 (random buttons while loading), MAXMSG (default 7984), SHOTS=dir (a picture every SHOTEVERY frames
 *      while loading, default 3), SCREEN.
 */
public class StairsTest {
	static WatchApp app; static Link link; static WatchSim sim; static Controls controls;
	static int frameErrors, linkErrors, frameNo, peak, maxMsg;
	static File shots; static int shotEvery, shotNo;
	static long lastErr; static int errRun;
	static String tripName = "";
	static final double RATE = Double.parseDouble(System.getenv().getOrDefault("RATE", "0")), LAT = Double.parseDouble(System.getenv().getOrDefault("LAT", "0.08"));
	static final ArrayDeque<byte[]> radioQ = new ArrayDeque<>();
	static byte[] inFlight; static double busyUntil, realNow;
	static boolean radioIdle() { return inFlight == null && radioQ.isEmpty() && !link.pending(); }

	public static void main(String[] a) throws Exception {
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		String dir = ".pebxel-stairs-" + System.getenv().getOrDefault("TAG", "0");
		app = new WatchApp(game, dir, true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		app.create();
		app.graphics.soft.raster = false;
		link = new Link(new Assets());
		sim = new WatchSim(new File("."), new Assets());
		controls = new Controls();
		link.planner = controls::plan;
		maxMsg = Integer.parseInt(System.getenv().getOrDefault("MAXMSG", "7984"));
		if (System.getenv("SHOTS") != null) { shots = new File(System.getenv("SHOTS")); shots.mkdirs(); }
		shotEvery = Integer.parseInt(System.getenv().getOrDefault("SHOTEVERY", "3"));
		int trips = Integer.parseInt(System.getenv().getOrDefault("TRIPS", "30"));
		String pattern = System.getenv().getOrDefault("PATTERN", "ddud");
		int mobs = Integer.parseInt(System.getenv().getOrDefault("MOBS", "0"));
		boolean wnd = System.getenv("WND") != null, press = System.getenv("PRESS") != null;
		java.util.Random rnd = new java.util.Random(Long.parseLong(System.getenv().getOrDefault("SEED", "1")));

		for (int i = 0; i < 5; i++) frame();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.values()[Integer.parseInt(System.getenv().getOrDefault("CLASS", "0"))];
		GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
		if (System.getenv("SEED") != null) Dungeon.seed = Long.parseLong(System.getenv("SEED"));
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		tripName = "start";
		arrive(rnd, press);

		for (int t = 0; t < trips; t++) {
			char c = pattern.charAt(t % pattern.length());
			if (c == 'f' || c == 'c') {
				tripName = String.format("t%02d-%s%d", t, c == 'f' ? "fall" : "continue", Dungeon.depth);
				if (!waitReady(300)) continue;
				if (c == 'f') com.shatteredpixel.shatteredpixeldungeon.levels.features.Chasm.heroFall(Dungeon.hero.pos);
				else {
					// the game saved and picked up again (as when the watch app opens on a saved game)
					Dungeon.saveAll(); Dungeon.hero = null;
					InterlevelScene.mode = InterlevelScene.Mode.CONTINUE; Game.switchScene(InterlevelScene.class);
				}
				arrive(rnd, press);
				for (int k = 0; k < 20; k++) frame();
				System.out.printf("%s -> depth %d  frame %d  watch ~%d bytes (peak %d)%n", tripName, Dungeon.depth, frameNo, sim.heapEstimate(), peak);
				continue;
			}
			boolean up = c == 'r' ? rnd.nextBoolean() : c == 'u';
			if (Dungeon.depth <= 1 && up && c == 'r') up = false;
			int from = Dungeon.depth;
			tripName = String.format("t%02d-%s%d", t, up ? "up" : "down", from);
			Map<String, long[]> before = new TreeMap<>(); for (Map.Entry<String, long[]> e : link.stats.entrySet()) before.put(e.getKey(), e.getValue().clone());
			long b0 = link.bytesTotal;
			if (!take(up, wnd && rnd.nextInt(3) == 0)) { System.out.println(tripName + ": no stairs taken"); continue; }
			arrive(rnd, press);
			if (System.getenv("BYTES") != null) {
				System.out.printf("  %d bytes for the floor change; biggest:", link.bytesTotal - b0);
				link.stats.entrySet().stream().map(e -> new Object[]{e.getKey(), e.getValue()[1] - before.getOrDefault(e.getKey(), new long[2])[1]})
					.sorted((x, y) -> Long.compare((Long) y[1], (Long) x[1])).limit(10).forEach(x -> System.out.printf(" %s %d,", x[0], x[1]));
				System.out.println();
			}
			if (!(Game.scene() instanceof GameScene)) { System.out.println(tripName + ": stuck in " + Game.scene().getClass().getSimpleName()); break; }
			if (mobs > 0) spawn(mobs);
			for (int k = 0; k < 20; k++) frame();
			int est = sim.heapEstimate();
			System.out.printf("%s -> depth %d  frame %d  watch: %d pictures, %d anims, %d dyn, ~%d bytes (peak %d)%n",
					tripName, Dungeon.depth, frameNo, sim.objCount(), sim.animCount(), sim.dynCount(), est, peak);
			System.out.println("    " + sim.heapParts() + " level " + Dungeon.level.width() + "x" + Dungeon.level.height());
		}
		if (System.getenv("DUMP") != null) { sim.dump(); if (shots != null) shot("end"); }
		System.out.printf("RESULT frames %d frameErrors %d linkErrors %d peak %d unknown %s%n", frameNo, frameErrors, linkErrors, peak, link.unknown);
		System.exit(0);
	}

	/** The floor's up or down stairs (depth 1's way up is the way out), or null. */
	public static LevelTransition stairs(boolean up) {
		LevelTransition tr = null;
		for (LevelTransition x : Dungeon.level.transitions) {
			boolean isUp = x.type == LevelTransition.Type.REGULAR_ENTRANCE || x.type == LevelTransition.Type.SURFACE || x.type == LevelTransition.Type.BRANCH_ENTRANCE;
			if (isUp == up && (tr == null || x.type == LevelTransition.Type.REGULAR_ENTRANCE || x.type == LevelTransition.Type.REGULAR_EXIT)) tr = x;
		}
		return tr;
	}

	/** Stand on the stairs and take them, as the hero's own step onto them does (Hero.actTransition). */
	public static void jump(LevelTransition tr) {
		ScrollOfTeleportation.appear(Dungeon.hero, tr.cell());
		Dungeon.level.occupyCell(Dungeon.hero);
		Dungeon.observe();
		Dungeon.level.activateTransition(Dungeon.hero, tr);
	}

	/** Go next to the stairs, then take them; false if there are none. */
	static boolean take(boolean up, boolean window) {
		Level L = Dungeon.level;
		LevelTransition tr = stairs(up);
		if (tr == null) return false;
		int cell = tr.cell(), next = -1;
		for (int d : PathFinder.NEIGHBOURS8) {
			int n = cell + d;
			if (n >= 0 && n < L.length() && L.passable[n] && Actor.findChar(n) == null) { next = n; break; }
		}
		if (next < 0) return false;
		ScrollOfTeleportation.appear(Dungeon.hero, next);
		Dungeon.level.occupyCell(Dungeon.hero);
		Dungeon.observe();
		for (int k = 0; k < 5; k++) frame();
		if (!waitReady(300)) return false;
		jump(tr);
		if (window) GameScene.show(new WndMessage("A window open while taking the stairs"));
		// depth 1's way out asks first (a window): that is the trip
		for (int k = 0; k < 300 && Game.scene() instanceof GameScene; k++) {
			frame();
			if (tr.type == LevelTransition.Type.SURFACE && Ui.topWindow() != null && k > 30) { Ui.topWindow().hide(); return true; }
		}
		return true;
	}

	/** Frames until the new floor is up and the hero can act (pressing on through story pages). */
	static void arrive(java.util.Random rnd, boolean press) throws Exception {
		int loading = 0;
		for (int k = 0; k < 3000; k++) {
			boolean inter = Game.scene() instanceof InterlevelScene;
			if (inter) {
				loading++;
				if (shots != null && loading % shotEvery == 1) shot("load" + loading);
				Thread.sleep(rnd.nextInt(6));   // (the loading thread runs alongside, in real time)
				if (press && rnd.nextInt(8) == 0) controls.button(1 + rnd.nextInt(4));
				// the story pages: Select through them (Controls presses the shown button)
				else if (loading % 40 == 39) controls.button(3);
			}
			frame();
			if (!inter && Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready && Ui.topWindow() == null) {
				if (shots != null) for (int s = 0; s < 400 && (s < 4 || !radioIdle()); s++) { for (int f = 0; f < shotEvery; f++) frame(); shot("after" + s); }
				return;
			}
			if (!inter && Game.scene() instanceof GameScene && Ui.topWindow() != null && k % 30 == 29) Ui.topWindow().hide();
		}
	}

	static boolean waitReady(int max) {
		for (int k = 0; k < max; k++) { if (Dungeon.hero.ready && Ui.topWindow() == null) return true; frame(); if (Ui.topWindow() != null && k % 10 == 9) Ui.topWindow().hide(); }
		return false;
	}

	static void spawn(int n) {
		Level L = Dungeon.level;
		for (int i = 0; i < n; i++) {
			Mob m = L.createMob();
			int cell = -1;
			for (int tries = 0; tries < 50 && cell < 0; tries++) {
				int c = Dungeon.hero.pos + PathFinder.NEIGHBOURS8[Random.Int(8)] * (1 + Random.Int(3));
				if (c >= 0 && c < L.length() && L.passable[c] && Actor.findChar(c) == null) cell = c;
			}
			if (cell < 0) continue;
			m.pos = cell; GameScene.add(m); m.beckon(Dungeon.hero.pos);
		}
	}

	/** One frame as WatchSession runs it. */
	static void frame() {
		frameNo++;
		try { Thread.sleep(1); } catch (InterruptedException e) { }   // (the turn thread runs alongside, in real time)
		try {
			Controls cc = controls;
			app.frame(); cc.frame();
		} catch (Throwable t) {
			frameErrors++;
			System.out.println("FRAME ERROR in " + tripName + " (scene " + Game.scene().getClass().getSimpleName() + "):");
			t.printStackTrace(System.out);
			long now = System.currentTimeMillis();
			if (now - lastErr > 3000) errRun = 0;
			lastErr = now;
			if (++errRun >= 5) { System.out.println("SESSION WOULD STOP (5 errors in a row)"); System.exit(2); }
			try { com.shatteredpixel.shatteredpixeldungeon.ui.Window w = Ui.topWindow(); if (w != null) w.hide(); } catch (Throwable t2) { }
		}
		try {
			sim.advance(Game.elapsed);
			link.frame(Game.elapsed);
			realNow += 1 / 30.0;
			if (RATE <= 0) for (byte[] m : link.take(maxMsg)) { sim.receive(m); sim.advance(0.001f); }
			else {
				// the radio: one message at a time, each taking LAT plus its bytes at RATE (as WatchSession sends)
				while (true) {
					if (inFlight != null && realNow >= busyUntil) { sim.receive(inFlight); inFlight = null; }
					if (inFlight != null) break;
					if (radioQ.isEmpty() && link.pending()) radioQ.addAll(link.take(maxMsg));
					if (radioQ.isEmpty()) break;
					inFlight = radioQ.poll(); busyUntil = Math.max(busyUntil, realNow - 1 / 30.0) + LAT + inFlight.length / RATE;
				}
			}
		} catch (Throwable t) {
			linkErrors++;
			System.out.println("LINK ERROR in " + tripName + " (the session would stop):");
			t.printStackTrace(System.out);
			System.exit(3);
		}
		int est = sim.heapEstimate(); if (est > peak) peak = est;
	}

	static void shot(String what) {
		try { if (!sim.partial || System.getenv("NOHOLD") != null) sim.render(); sim.save(new File(shots, String.format("%04d-%s-%s.png", shotNo++, tripName, what)), 1); }
		catch (Exception e) { throw new RuntimeException(e); }
	}
}
