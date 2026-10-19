package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.util.*;

/**
 * How smooth does the hero walk look on a watch that hears from the phone late? Messages reach the
 * stand-in watch LAG ms after sending, and the next one goes only after the watch took the last
 * (like the Bluetooth link). Measures the hero's path on the watch against the real one:
 *   travel ratio (watch path length / real path length: 1 = no wobble), backward jerks, biggest jump.
 * Args: lag-ms steps
 */
public class LagTest {
	public static void main(String[] a) throws Exception {
		int lagMs = Integer.parseInt(a[0]), steps = Integer.parseInt(a[1]);
		Game.version = "4.0.2"; Game.versionCode = 922;
		WatchGame game = new WatchGame(); game.drawAlways = true;
		Smoke.app = new WatchApp(game, ".pebxel-lag", false);
		SPDSettings.set(Smoke.app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-lag/");
		Smoke.app.create();
		for (int i = 0; i < 5; i++) Smoke.app.frame();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			Smoke.app.frame(); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
		}
		Assets as = new Assets();
		Link link = new Link(as);
		WatchSim sim = new WatchSim(new java.io.File("."), as);
		float clock = 0, lag = lagMs / 1000f, sinceSync = 0;
		ArrayDeque<Object[]> wire = new ArrayDeque<>();   // {arrival time, bytes}
		boolean inFlight = false;
		Random rng = new Random(1);
		int heroId = -1;
		double realPath = 0, watchPath = 0, maxJump = 0; int backward = 0, frames = 0;
		float[] lastReal = null, lastWatch = null, lastDir = null;
		Set<Integer> bad = new HashSet<>();
		float warm = -1;
		List<float[]> reals = new ArrayList<>(), watches = new ArrayList<>();
		int done = 0, home = -1, away = -1, lastPos = -1;
		while (done < steps && frames < 20000) {
			// the player: walk somewhere new whenever the hero is ready
			if (Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.isAlive()) {
				Window w = Ui.topWindow();
				if (w != null) w.onBackPressed();
				else if (Dungeon.hero.ready) {
					// walk back and forth between two spots a few steps apart
					if (home < 0) {
						home = Dungeon.hero.pos;
						com.watabou.utils.PathFinder.buildDistanceMap(home, com.watabou.utils.BArray.or(Dungeon.level.passable, Dungeon.level.avoid, null));
						for (int c = 0; c < Dungeon.level.length(); c++)
							if (com.watabou.utils.PathFinder.distance[c] >= 5 && com.watabou.utils.PathFinder.distance[c] <= 7 && Dungeon.level.map[c] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.EMPTY && Dungeon.level.heroFOV[c]) { away = c; break; }
					}
					int t = Dungeon.hero.pos == away ? home : away;
					if (Dungeon.hero.pos != lastPos) { done++; lastPos = Dungeon.hero.pos; }
					if (Dungeon.hero.handle(t)) Dungeon.hero.next();
				}
			}
			Smoke.app.frame(); frames++;
			float dt = Game.elapsed; clock += dt; sinceSync += dt;
			sim.advance(dt);
			// delivery
			while (!wire.isEmpty() && (float) wire.peek()[0] <= clock) { sim.receive((byte[]) wire.poll()[1]); if (wire.isEmpty()) inFlight = false; }
			link.frame(dt);
			if (!inFlight && link.pending()) {
				// one message at a time, each taking the lag plus its own radio time (about 10 KB/s), with a little jitter
				float t = clock;
				for (byte[] p : link.take(4000)) {
					t += lag * (0.7f + 0.6f * rng.nextFloat()) + p.length / 10000f; wire.add(new Object[]{t, p});
					if (System.getenv("SIZES") != null) System.out.println("send " + p.length + " bytes at " + clock);
				}
				inFlight = true;
			}
			// measure the hero
			Map<Integer, Object[]> truth = link.truth();
			if (heroId < 0) for (Map.Entry<Integer, Object[]> e : truth.entrySet()) if (e.getValue()[0].equals("HeroSprite")) heroId = e.getKey();
			if (heroId < 0 || !truth.containsKey(heroId)) continue;
			if (warm < 0 && clock > 4) warm = clock;
			if (warm < 0 || clock < warm + 2) continue;   // the first whole screen takes a while
			float[] real = (float[]) truth.get(heroId)[1], g = sim.guess(heroId);
			if (g == null) continue;
			float[] rw = {real[0] / 8, real[1] / 8}, ww = {g[0] / 8, g[1] / 8};
			if (lastReal != null) {
				double dr = Math.hypot(rw[0] - lastReal[0], rw[1] - lastReal[1]), dw = Math.hypot(ww[0] - lastWatch[0], ww[1] - lastWatch[1]);
				if (dr > 20) { lastReal = rw; lastWatch = ww; continue; }   // a teleport, not a walk
				realPath += dr; watchPath += dw; maxJump = Math.max(maxJump, dw);
				if (dr > 0.01) lastDir = new float[]{rw[0] - lastReal[0], rw[1] - lastReal[1]};
				if (lastDir != null && dw > 0.5 && (ww[0] - lastWatch[0]) * lastDir[0] + (ww[1] - lastWatch[1]) * lastDir[1] < 0) backward++;
			}
			lastReal = rw; lastWatch = ww;
			reals.add(rw); watches.add(ww);
			if (System.getenv("TRACE") != null && frames > 400 && frames < 460) System.out.printf("f %d t %.3f real %.1f,%.1f watch %.1f,%.1f vel %.0f,%.0f wire %d%n", frames, clock, rw[0], rw[1], ww[0], ww[1], g.length > 9 ? g[9] : 0f, 0f, wire.size());
			Thread.sleep(1);
		}
		if (System.getenv("SIZES") != null) link.stats.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue()[1], x.getValue()[1])).limit(15)
				.forEach(e -> System.out.printf("  %-40s %7d times %9d bytes%n", e.getKey(), e.getValue()[0], e.getValue()[1]));
		// the watch's path against the real one played back with the best fitting delay
		int bestD = 0; double best = 1e9;
		for (int d = 0; d < 40; d++) {
			double e = 0; int n = 0;
			for (int t = d; t < reals.size(); t++) { e += Math.hypot(watches.get(t)[0] - reals.get(t - d)[0], watches.get(t)[1] - reals.get(t - d)[1]); n++; }
			if (n > 0 && e / n < best) { best = e / n; bestD = d; }
		}
		double worst = 0; int off = 0;
		for (int t = bestD; t < reals.size(); t++) {
			double e = Math.hypot(watches.get(t)[0] - reals.get(t - bestD)[0], watches.get(t)[1] - reals.get(t - bestD)[1]);
			worst = Math.max(worst, e); if (e > 4) off++;
		}
		System.out.printf("RESULT lag %d ms: watch runs %d ms behind; off from the real walk by %.1f on average, %.1f at worst; frames off by more than 4: %d of %d%n",
				lagMs, bestD * 33, best, worst, off, reals.size() - bestD);
		System.out.printf("lag %d ms, %d steps, %d frames: travel ratio %.2f, backward jerks %d, biggest jump %.1f units%n",
				lagMs, done, frames, watchPath / Math.max(1, realPath), backward, maxJump);
		System.exit(0);
	}
}
