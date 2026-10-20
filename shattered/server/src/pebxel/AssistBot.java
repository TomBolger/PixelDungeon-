package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
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
 * Env: MODE (assist 1 near / 2 far, default 1), ITEMS (0/1, default 1), MAXDEPTH (default 26), V=1 (every decision).
 * Ends with one line "RESULT ..." for scripts.
 */
public class AssistBot {
	static WatchApp app;
	static final ArrayDeque<String> log = new ArrayDeque<>();

	public static void main(String[] a) throws Exception {
		HeroClass cls = HeroClass.values()[a.length > 0 ? Integer.parseInt(a[0]) : 0];
		long seed = a.length > 1 ? Long.parseLong(a[1]) : 1;
		int maxTurns = a.length > 2 ? Integer.parseInt(a[2]) : 3000;
		int maxDepth = Integer.parseInt(System.getenv().getOrDefault("MAXDEPTH", "26"));
		boolean v = System.getenv("V") != null;
		String dir = ".pebxel-assistbot-" + cls.ordinal() + "-" + seed;
		Game.version = "4.0.2"; Game.versionCode = 922;
		WatchGame game = new WatchGame();
		app = new WatchApp(game, dir, false);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		app.create();
		for (int i = 0; i < 5; i++) app.frame();
		SPDSettings.intro(false);
		WatchSettings.assist(Integer.parseInt(System.getenv().getOrDefault("MODE", "1")));
		WatchSettings.itemAssist(!"0".equals(System.getenv("ITEMS")));
		Assist.reset();

		GamesInProgress.selectedClass = cls; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = seed;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);

		Signal.Listener<String> logL = s -> { synchronized (log) { log.addLast(s); while (log.size() > 8) log.removeFirst(); } return false; };
		int maxD = 0, depth = 0, decisions = 0, stuckEvents = 0, sameRun = 0, picked = 0, actFails = 0, windows = 0;
		float floorStart = 0; int lastPos = -1; String lastKey = "";
		Map<String, Integer> whys = new TreeMap<>();
		Heap pickHeap = null; int pickSize = 0;
		String end = "turn limit";
		long planNs = 0, plans = 0, maxPlanNs = 0;
		Window lastWnd = null; int wndFrames = 0;
		long t0 = System.currentTimeMillis();
		for (long f = 0; f < 3_000_000; f++) {
			app.frame();
			if (Game.scene() instanceof InterlevelScene) { if (f % 3 == 0) HandoffTest.pressOn(); continue; }
			if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) continue;
			if (!Dungeon.hero.isAlive()) { end = "died"; break; }
			if (Dungeon.depth != depth) {
				if (depth > 0) System.out.printf("  floor %d done in %.0f turns%n", depth, Actor.now() - floorStart);
				depth = Dungeon.depth; maxD = Math.max(maxD, depth); floorStart = Actor.now();
				GLog.update.remove(logL); GLog.update.add(logL);
				System.out.printf("depth %d  turn %.0f  hp %d/%d  lvl %d  str %d  %.1fs%n", depth, Actor.now(), Dungeon.hero.HP, Dungeon.hero.HT, Dungeon.hero.lvl, Dungeon.hero.STR(), (System.currentTimeMillis() - t0) / 1000.0);
				if (depth > maxDepth) { end = "depth limit"; break; }
			}
			if (Actor.now() > maxTurns) { end = "turn limit"; break; }
			if (Actor.now() - floorStart > 2500) { end = "floor timeout (2500 turns on depth " + depth + ")"; break; }
			Window w = Ui.topWindow();
			if (w != null) {
				if (w != lastWnd) { lastWnd = w; wndFrames = 0; windows++; if (v) System.out.println("  window " + w.getClass().getSimpleName()); }
				wndFrames++;
				List<Ui.Part> bs = Controls.buttons(w);
				if (wndFrames == 1 && w instanceof WndOptions && !bs.isEmpty()) Ui.press(bs.get(0).g);
				else if (wndFrames == 1) w.onBackPressed();
				else if (wndFrames == 10 && !bs.isEmpty()) Ui.press(bs.get(0).g);
				else if (wndFrames == 20) w.hide();
				continue;
			}
			lastWnd = null;
			if (!Dungeon.hero.ready) continue;
			if (Controls.aiming()) {   // (left over: aim at plan A of the aiming list, else cancel)
				List<Assist.Target> al = Assist.plan(true, null);
				if (!al.isEmpty() && al.get(0).kind == Assist.ENEMY) Assist.act(al.get(0)); else GameScene.cancelCellSelector();
				continue;
			}
			if (pickHeap != null) { if (Dungeon.level.heaps.get(pickHeap.pos) != pickHeap || pickHeap.size() < pickSize) picked++; pickHeap = null; }
			long n0 = System.nanoTime();
			int hx = Dungeon.hero.pos % Dungeon.level.width(), hy = Dungeon.hero.pos / Dungeon.level.width(), W = Dungeon.level.width();
			List<Assist.Target> plan = Assist.plan(false, c -> { int dx = c % W - hx, dy = c / W - hy; return dx >= -6 && dx <= 4 && dy >= -4 && dy <= 4; });
			long dn = System.nanoTime() - n0; planNs += dn; plans++; maxPlanNs = Math.max(maxPlanNs, dn);
			if (Assist.lastError != null) { System.out.println("  ASSIST ERROR " + Assist.lastError); Assist.lastError.printStackTrace(System.out); Assist.lastError = null; }
			decisions++;
			if (plan.isEmpty()) { Dungeon.hero.rest(false); continue; }
			Assist.Target t = plan.get(0);
			String key = t.kind + ":" + t.cell + ":" + t.action + ":" + (t.item == null ? "" : t.item.getClass().getSimpleName());
			boolean still = Dungeon.hero.pos == lastPos && key.equals(lastKey) && t.kind != Assist.WAIT && !"wait".equals(t.action);
			if (still) { stuckEvents++; sameRun++; } else sameRun = 0;
			if (sameRun >= 50) { end = "loop at " + t; break; }
			lastPos = Dungeon.hero.pos; lastKey = key;
			whys.merge(t.why, 1, Integer::sum);
			if (v) System.out.printf("  t%.0f pos %d hp %d/%d: %s%s%n", Actor.now(), Dungeon.hero.pos, Dungeon.hero.HP, Dungeon.hero.HT, t, plan.size() > 1 ? "   | B: " + plan.get(1) : "");
			if (t.kind == Assist.PICKUP) { Heap hh = Dungeon.level.heaps.get(t.cell); if (hh != null && t.cell == Dungeon.hero.pos) { pickHeap = hh; pickSize = hh.size(); } }
			if (!Assist.act(t)) { actFails++; if (Assist.lastError != null) { System.out.println("  ACT ERROR " + Assist.lastError); Assist.lastError.printStackTrace(System.out); Assist.lastError = null; } Dungeon.hero.rest(false); }
		}
		String cause = "";
		if (end.equals("died")) synchronized (log) { cause = String.join(" / ", log); }
		System.out.println("decisions by reason: " + whys);
		System.out.printf("plan time: avg %.2f ms, max %.1f ms over %d plans%n", planNs / 1e6 / Math.max(1, plans), maxPlanNs / 1e6, plans);
		System.out.printf("RESULT class %s seed %d: %s depth %d (max %d) turns %.0f lvl %d hp %d/%d picked %d stuck %d actfails %d windows %d decisions %d  %.1fs%s%n",
				cls, seed, end, Dungeon.depth, maxD, Actor.now(), Dungeon.hero == null ? 0 : Dungeon.hero.lvl, Dungeon.hero == null ? 0 : Dungeon.hero.HP, Dungeon.hero == null ? 0 : Dungeon.hero.HT,
				picked, stuckEvents, actFails, windows, decisions, (System.currentTimeMillis() - t0) / 1000.0, cause.isEmpty() ? "" : "\n  last log: " + cause);
		System.exit(0);
	}
}
