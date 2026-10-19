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
		app = new WatchApp(game, ".pebxel-spd");
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
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		}
		Game.switchScene(InterlevelScene.class);

		Set<String> seenWnd = new HashSet<>();
		int lastDepth = 0, decisions = 0, stuck = 0, lastPos = -1, lastTarget = -1;
		Set<Integer> bad = new HashSet<>();
		long f;
		for (f = 0; f < maxFrames; f++) {
			app.frame();
			if (Game.scene() instanceof InterlevelScene) {
				try { Thread.sleep(2); } catch (InterruptedException e) { }
				for (Ui.Part p : Ui.parts(Game.scene()))
					if (p.button && !p.text.isEmpty() && !p.text.equals("Button")) { Ui.press(p.g); break; }
			}
			if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) continue;
			if (!Dungeon.hero.isAlive()) { System.out.println("DIED on depth " + Dungeon.depth + " turns " + (int) Statistics.duration); break; }
			if (Dungeon.depth != lastDepth) {
				lastDepth = Dungeon.depth; bad.clear(); searched.clear();
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
			int pos = Dungeon.hero.pos;
			if (pos == lastPos && lastTarget >= 0) { if (++stuck > 5) { bad.add(lastTarget); stuck = 0; } } else stuck = 0;
			lastPos = pos;
			int t = choose(bad);
			lastTarget = t; decisions++;
			if (t == -2) { boolean fnd = Dungeon.hero.search(true); if (System.getenv("DBG") != null) System.out.println("  search at " + pos + " found " + fnd + " ready " + Dungeon.hero.ready); settle(); }
			else if (t >= 0 && Dungeon.hero.handle(t)) { Dungeon.hero.next(); settle(); }
		}
		System.out.printf("END frame %d  %.1fs  depth %d  decisions %d  windows %s%n", f, (System.currentTimeMillis() - t0) / 1000.0, Dungeon.depth, decisions, seenWnd);
		System.exit(0);
	}

	/** Run frames until the hero wants input again (the turn thread only wakes on frames, like on a real screen). */
	static void settle() {
		for (int k = 0; k < 600 && !Dungeon.hero.ready && Dungeon.hero.isAlive(); k++) { app.frame(); frames++; sleep(1); }
		for (int k = 0; k < 3; k++) { app.frame(); frames++; }
	}
	static long frames;

	static void sleep(int ms) { try { Thread.sleep(ms); } catch (InterruptedException e) { } }

	static int choose(Set<Integer> bad) {
		Level L = Dungeon.level; int h = Dungeon.hero.pos, W = L.width();
		for (Mob m : L.mobs)
			if (m.alignment == Char.Alignment.ENEMY && L.heroFOV[m.pos] && Dungeon.level.adjacent(h, m.pos)) return m.pos;
		boolean[] known = new boolean[L.length()];
		for (int i = 0; i < known.length; i++) known[i] = L.visited[i] || L.mapped[i];
		int[] dist = new int[L.length()]; Arrays.fill(dist, -1);
		ArrayDeque<Integer> q = new ArrayDeque<>(); q.add(h); dist[h] = 0;
		while (!q.isEmpty()) {
			int c = q.poll();
			if (c != h && !bad.contains(c)) {
				if (L.heaps.get(c) != null) return c;
				for (int d : PathFinder.NEIGHBOURS8) { int n = c + d; if (n >= 0 && n < known.length && !known[n]) return c; }
			}
			for (int d : PathFinder.NEIGHBOURS8) {
				int n = c + d;
				if (n < 0 || n >= known.length || dist[n] >= 0 || !known[n] || !(L.passable[n] || L.avoid[n])) continue;
				dist[n] = dist[c] + 1; q.add(n);
			}
		}
		if (L.visited[L.exit()] || L.mapped[L.exit()]) return L.exit();
		//nothing left to explore: walk the walls searching for hidden doors (SPD searches 1 tile around you)
		q.clear(); Arrays.fill(dist, -1); q.add(h); dist[h] = 0;
		while (!q.isEmpty()) {
			int c = q.poll();
			if (!searched.contains(c)) {
				boolean wall = false;
				for (int d : PathFinder.NEIGHBOURS8) { int n = c + d; if (n >= 0 && n < known.length && L.solid[n] && !L.passable[n]) wall = true; }
				if (wall) { if (c == h) { searched.add(c); return -2; } return c; }
			}
			for (int d : PathFinder.NEIGHBOURS8) {
				int n = c + d;
				if (n < 0 || n >= known.length || dist[n] >= 0 || !known[n] || !L.passable[n]) continue;
				dist[n] = dist[c] + 1; q.add(n);
			}
		}
		searched.clear();
		return -2;
	}
	static Set<Integer> searched = new HashSet<>();
	static int searches;
}
