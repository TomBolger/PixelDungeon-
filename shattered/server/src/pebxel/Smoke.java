package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import com.watabou.utils.PathFinder;
import java.util.*;

/** Proof test: play Shattered PD with no screen, using a dumb explorer for input. */
public class Smoke {
	static WatchApp app;
	static int depthGoal;

	public static void main(String[] a) {
		HeroClass cls = HeroClass.values()[a.length > 0 ? Integer.parseInt(a[0]) : 0];
		depthGoal = a.length > 1 ? Integer.parseInt(a[1]) : 5;
		long maxFrames = a.length > 2 ? Long.parseLong(a[2]) : 200000;
		Game.version = "4.0.2"; Game.versionCode = 922;
		WatchGame game = new WatchGame();
		boolean measure = System.getenv("MEASURE") != null;
		String linkDir = System.getenv("LINK");
		app = new WatchApp(game, ".pebxel-spd", measure || linkDir != null);
		if (measure) { app.graphics.soft.raster = false; app.graphics.soft.record = new ArrayList<>(); }
		if (linkDir != null) {
			java.io.File root = new java.io.File(".");
			Assets as = new Assets();
			link = new Link(as); sim = new WatchSim(root, as); shotDir = new java.io.File(linkDir); shotDir.mkdirs();
		}
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-spd/");
		app.create();
		long t0 = System.currentTimeMillis();
		for (int i = 0; i < 5; i++) app.frame();

		SPDSettings.intro(false); //skip the tutorial floor (it waits for the journal to be opened)
		boolean resume = a.length > 3 && a[3].equals("resume");
		if (resume) {
			//pick up the saved game, exactly like tapping it on the phone's start screen
			GamesInProgress.curSlot = 1;
			System.out.println("resuming saved game: " + GamesInProgress.check(1).heroClass + " depth " + GamesInProgress.check(1).depth);
			Dungeon.hero = null;
			InterlevelScene.mode = InterlevelScene.Mode.CONTINUE;
		} else {
		GamesInProgress.selectedClass = cls; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
		if (System.getenv("SEED") != null) Dungeon.seed = Long.parseLong(System.getenv("SEED"));
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		}
		Game.switchScene(InterlevelScene.class);

		Set<String> seenWnd = new HashSet<>();
		int lastDepth = 0, decisions = 0, stuck = 0, lastPos = -1, lastTarget = -1;
		Set<Integer> bad = new HashSet<>();
		long f;
		for (f = 0; f < maxFrames; f++) {
			step();
			if (Game.scene() instanceof InterlevelScene) {
				try { Thread.sleep(2); } catch (InterruptedException e) { }
				for (Ui.Part p : Ui.parts(Game.scene()))
					if (p.button && !p.text.isEmpty() && !p.text.equals("Button")) { Ui.press(p.g); break; }
			}
			if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) continue;
			if (!Dungeon.hero.isAlive()) { System.out.println("DIED on depth " + Dungeon.depth + " turns " + (int) Statistics.duration); break; }
			if (Dungeon.depth != lastDepth) {
				lastDepth = Dungeon.depth; bad.clear(); Explorer.searched.clear();
				System.out.printf("depth %d  frame %d  %.1fs  hp %d/%d%n", Dungeon.depth, f, (System.currentTimeMillis() - t0) / 1000.0, Dungeon.hero.HP, Dungeon.hero.HT);
				if (Dungeon.depth > depthGoal) { try { Dungeon.saveAll(); } catch (Exception e) { throw new RuntimeException(e); } System.out.println("saved and quit on depth " + Dungeon.depth + ", hp " + Dungeon.hero.HP + ", gold " + Dungeon.gold); break; }
			}
			Window w = Ui.topWindow();
			if (w != null) {
				String k = w.getClass().getSimpleName();
				if (seenWnd.add(k)) {
					StringBuilder sb = new StringBuilder("  window " + k + ":");
					for (Ui.Part p : Ui.parts(w)) sb.append(p.button ? " [" : " \"").append(p.text.replace('\n', ' ')).append(p.button ? "]" : "\"");
					System.out.println(sb.length() > 300 ? sb.substring(0, 300) + "…" : sb);
				}
				w.onBackPressed();
				continue;
			}
			if (!Dungeon.hero.ready) continue;
			if (link != null && System.getenv("BENCH") != null && Dungeon.depth == 1 && !benched) { bench(); benched = true; }
			int pos = Dungeon.hero.pos;
			if (pos == lastPos && lastTarget >= 0) { if (++stuck > 5) { bad.add(lastTarget); stuck = 0; } } else stuck = 0;
			lastPos = pos;
			int t = choose(bad);
			lastTarget = t; decisions++;
			if (t == -2) { boolean fnd = Dungeon.hero.search(true); if (System.getenv("DBG") != null) System.out.println("  search at " + pos + " found " + fnd + " ready " + Dungeon.hero.ready); settle(); }
			else if (t >= 0 && Dungeon.hero.handle(t)) { Dungeon.hero.next(); settle(); }
		}
		if (link != null) link.stats.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue()[1], x.getValue()[1])).limit(25)
				.forEach(e -> System.out.printf("  %-40s %7d times %9d bytes%n", e.getKey(), e.getValue()[0], e.getValue()[1]));
		if (link != null) System.out.printf("link: %d bytes over %.0f game seconds = %.0f bytes/s; %d sends, %.0f%% non-empty, biggest %d%n  unknown %s%n", link.bytesTotal, gameTime, link.bytesTotal / gameTime, linkFrames, 100.0 * linkBusy / linkFrames, linkMax, link.unknown);
		if (app.graphics.soft != null && link == null) System.out.printf("quads/frame %.0f  changed/frame %.1f  frames with change %.0f%%  (game frames %d)%n", qTotal / (double) qFrames, qChanged / (double) qFrames, 100.0 * qBusyFrames / qFrames, qFrames);
		System.out.printf("END frame %d  %.1fs  depth %d  decisions %d  windows %s%n", f, (System.currentTimeMillis() - t0) / 1000.0, Dungeon.depth, decisions, seenWnd);
		System.exit(0);
	}

	/** Run frames until the hero wants input again (the turn thread only wakes on frames, like on a real screen). */
	static void settle() {
		for (int k = 0; k < 600 && !Dungeon.hero.ready && Dungeon.hero.isAlive(); k++) { step(); sleep(1); }
		for (int k = 0; k < 3; k++) step();
	}

	//measuring mode: how many drawn quads change from frame to frame (what a naive watch link would send)
	static Map<String, Integer> prev = new HashMap<>();
	static long qFrames, qChanged, qTotal, qBusyFrames;
	static final int LINK_EVERY = Integer.parseInt(System.getenv().getOrDefault("LINK_EVERY", "1"));
	static int linkTick; static float linkTime, gameTime;
	static Link link; static WatchSim sim; static java.io.File shotDir; static int shotNo; static long linkFrames, linkBusy, linkMax;

	/** Send this frame's changes to the stand-in watch; every so often save both pictures side by side. */
	static void linkFrame(boolean picture) {
		if (link == null) return;
		linkTime += com.watabou.noosa.Game.elapsed;
		if (!picture && ++linkTick % LINK_EVERY != 0) return;
		float dtSent = linkTime; gameTime += linkTime; sim.advance(linkTime); linkTime = 0;
		byte[] cmds = link.sync(dtSent);
		linkFrames++; if (cmds.length > 0) linkBusy++; linkMax = Math.max(linkMax, cmds.length);
		sim.apply(cmds);
		if (!picture) return;
		for (java.util.Map.Entry<Integer, Object[]> e : link.truth().entrySet()) {
			float[] real = (float[]) e.getValue()[1], g = sim.guess(e.getKey());
			if (g == null) { System.out.println("CHECK missing " + e.getValue()[0]); continue; }
			for (int i = 0; i < Link.N; i++) {
				boolean off = i == Link.D_A ? Math.round(g[i] / 85) != Math.round(real[i] / 85) : Math.abs(g[i] - real[i]) > 12;
				if (off) System.out.printf("CHECK %s field %d watch %.0f real %.0f%n", e.getValue()[0], i, g[i], real[i]);
			}
		}
		try {
			sim.render();
			SoftGL g = app.graphics.soft;
			java.awt.image.BufferedImage img = new java.awt.image.BufferedImage(WatchSim.W * 4 + 8, WatchSim.H * 2, java.awt.image.BufferedImage.TYPE_INT_RGB);
			for (int y = 0; y < WatchSim.H * 2; y++) for (int x = 0; x < WatchSim.W * 2; x++) {
				img.setRGB(x, y, sim.fb[(y / 2) * WatchSim.W + x / 2]);
				int c = g.fb[y * g.W + x];
				img.setRGB(WatchSim.W * 2 + 8 + x, y, c);
			}
			javax.imageio.ImageIO.write(img, "png", new java.io.File(shotDir, String.format("%03d.png", shotNo++)));
			if (System.getenv("DUMP") != null && shotNo == 1) {
				sim.dump();
				for (String part : System.getenv("DUMP").split(";")) {
					if (part.isEmpty() || part.equals("1")) continue;
					sim.only = new HashSet<>(); for (String x : part.split(",")) sim.only.add(Integer.parseInt(x));
					sim.render(); sim.save(new java.io.File(shotDir, "only-" + part.replace(',', '_') + ".png"), 2);
				}
				sim.only = null;
			}
		} catch (Exception e) { throw new RuntimeException(e); }
	}

	static void step() {
		frames++;
		if (link != null) {
			boolean pic = frames % 400 == 0;
			app.graphics.soft.raster = pic;
			((WatchGame) com.watabou.noosa.Game.instance).drawAlways = true; app.frame();
			linkFrame(pic); return;
		}
		if (app.graphics.soft == null) { app.frame(); return; }
		app.graphics.soft.record.clear(); ((WatchGame) com.watabou.noosa.Game.instance).drawNext = true; app.frame();
		Map<String, Integer> cur = new HashMap<>();
		for (String q : app.graphics.soft.record) cur.merge(q, 1, Integer::sum);
		int changed = 0;
		for (Map.Entry<String, Integer> e : cur.entrySet()) changed += Math.max(0, e.getValue() - prev.getOrDefault(e.getKey(), 0));
		for (Map.Entry<String, Integer> e : prev.entrySet()) changed += Math.max(0, e.getValue() - cur.getOrDefault(e.getKey(), 0));
		prev = cur; qFrames++; qChanged += changed; qTotal += app.graphics.soft.record.size(); if (changed > 0) qBusyFrames++;
	}
	static long frames;

	static boolean benched;

	/** Link traffic while standing still for 10 seconds, then for single steps. */
	static void bench() {
		for (int i = 0; i < 60; i++) step();
		long b0 = link.bytesTotal; float t0 = gameTime;
		for (int i = 0; i < 300; i++) step();
		System.out.printf("BENCH idle: %.0f bytes/s%n", (link.bytesTotal - b0) / (gameTime - t0));
		java.util.TreeMap<String, long[]> before = new java.util.TreeMap<>();
		for (java.util.Map.Entry<String, long[]> e : link.stats.entrySet()) before.put(e.getKey(), e.getValue().clone());
		b0 = link.bytesTotal; t0 = gameTime; int steps = 0;
		for (int k = 0; k < 10; k++) {
			int h = Dungeon.hero.pos, t = -1;
			for (int d : PathFinder.NEIGHBOURS4) if (Dungeon.level.passable[h + d] && Dungeon.level.findMob(h + d) == null) { t = h + d; break; }
			if (t < 0) break;
			if (Dungeon.hero.handle(t)) { Dungeon.hero.next(); settle(); steps++; }
			for (int i = 0; i < 15; i++) step();
		}
		System.out.printf("BENCH %d steps: %.0f bytes per step (%.0f bytes/s)%n", steps, (link.bytesTotal - b0) / (double) Math.max(1, steps), (link.bytesTotal - b0) / (gameTime - t0));
		for (java.util.Map.Entry<String, long[]> e : link.stats.entrySet()) {
			long[] o = before.getOrDefault(e.getKey(), new long[2]);
			if (e.getValue()[1] - o[1] > 200) System.out.printf("BENCH   %-40s %6d bytes%n", e.getKey(), e.getValue()[1] - o[1]);
		}
	}

	static void sleep(int ms) { try { Thread.sleep(ms); } catch (InterruptedException e) { } }

	static int choose(Set<Integer> bad) { return Explorer.choose(bad); }
}
