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
	static boolean v;
	static Object lastScene; static int sceneFrames;
	static final ArrayDeque<String> log = new ArrayDeque<>(), recent = new ArrayDeque<>();

	public static void main(String[] a) throws Exception {
		HeroClass cls = HeroClass.values()[a.length > 0 ? Integer.parseInt(a[0]) : 0];
		long seed = a.length > 1 ? Long.parseLong(a[1]) : 1;
		int maxTurns = a.length > 2 ? Integer.parseInt(a[2]) : 3000;
		int maxDepth = Integer.parseInt(System.getenv().getOrDefault("MAXDEPTH", "26"));
		v = System.getenv("V") != null;
		String dir = ".pebxel-assistbot-" + cls.ordinal() + "-" + seed + System.getenv().getOrDefault("SAVETAG", "");
		Game.version = "4.0.2"; Game.versionCode = 922;
		WatchGame game = new WatchGame();
		app = new WatchApp(game, dir, false);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		app.create();
		Lockstep.seed(seed);
		for (int i = 0; i < 5; i++) Lockstep.frame(app);
		SPDSettings.intro(false);
		WatchSettings.assist(Integer.parseInt(System.getenv().getOrDefault("MODE", "1")));
		WatchSettings.itemAssist(!"0".equals(System.getenv("ITEMS")));
		Assist.reset();
		BotWindows.newGame(seed); BotItems.newGame(); Coverage.newGame();
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
		for (long f = 0; f < 20_000_000; f++) {
			Lockstep.frame(app);
			if (Lockstep.stuck != null) { end = "runaway: " + Lockstep.stuck; break; }
			if (Game.scene() instanceof InterlevelScene) {
				if (Ui.topWindow() != null) { end = "error between floors: " + Ui.topWindow().getClass().getName(); break; }
				if (++betweenF > HANG_FRAMES) { end = "stuck between floors"; break; }
				if (f % 3 == 0) HandoffTest.pressOn();
				continue;
			}
			betweenF = 0;
			if (Statistics.gameWon || Game.scene() instanceof com.shatteredpixel.shatteredpixeldungeon.scenes.SurfaceScene) { end = "WIN"; break; }
			if (!(Game.scene() instanceof GameScene)) {   // another screen (the Amulet's): answered like a window
				Coverage.scene(Game.scene());
				if (Game.scene() != lastScene) { lastScene = Game.scene(); sceneFrames = 0; }
				if (++sceneFrames % 30 == 2) { BotWindows.Answer an = BotWindows.decideScene(Game.scene()); System.out.println("  SCENE " + Game.scene().getClass().getSimpleName() + " -> " + an); if (an.g != null) Ui.press(an.g); else com.watabou.noosa.NoosaAccess.back(Game.scene()); }
				if (sceneFrames > 3000) { end = "stuck on " + Game.scene().getClass().getSimpleName(); break; }
				continue;
			}
			if (Dungeon.hero == null) continue;
			if (!Dungeon.hero.isAlive()) { end = "died"; break; }
			if (Dungeon.depth != depth) {
				if (depth > 0) System.out.printf("  floor %d done in %.0f turns%n", depth, Statistics.duration - floorStart);
				depth = Dungeon.depth; floorDecisions = 0; floorFrames = 0; maxD = Math.max(maxD, depth); floorStart = Statistics.duration;
				GLog.update.remove(logL); GLog.update.add(logL);
				System.out.printf("depth %d  turn %.0f  hp %d/%d  lvl %d  str %d  %.1fs%n", depth, Statistics.duration, Dungeon.hero.HP, Dungeon.hero.HT, Dungeon.hero.lvl, Dungeon.hero.STR(), (System.currentTimeMillis() - t0) / 1000.0);
				if (depth > maxDepth) { end = "depth limit"; break; }
				if (v) { StringBuilder sb = new StringBuilder("  level: heaps"); for (Heap h : Dungeon.level.heaps.valueList()) sb.append(' ').append(h.pos).append(':').append(h.peek().getClass().getSimpleName());
					sb.append(" mobs"); for (com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob m : Dungeon.level.mobs) sb.append(' ').append(m.pos).append(':').append(m.getClass().getSimpleName()); System.out.println(sb); }
			}
			if (Statistics.duration > maxTurns) { end = "turn limit"; break; }
			if (++floorFrames > FLOOR_FRAMES) { end = "floor timeout (" + FLOOR_FRAMES + " frames on depth " + depth + ")"; break; }
			if (Statistics.duration - floorStart > FLOOR_TURNS) { end = "floor timeout (" + FLOOR_TURNS + " turns on depth " + depth + ")"; break; }
			Window w = Ui.topWindow();
			if (w != null) {
				if (w != lastWnd) { lastWnd = w; wndFrames = 0; windows++; }
				wndFrames++;
				// answered on its 2nd frame (laid out by then), again every 30 frames it stays; for ever: gone
				if (wndFrames % 30 == 2) { BotWindows.Answer an = BotWindows.decide(w); if (v) System.out.println("    -> " + an); BotWindows.doDirect(w, an); }
				else if (wndFrames > 400) { System.out.println("  WINDOW STUCK " + w.getClass().getName() + ": hidden"); Coverage.problem("window stuck: " + w.getClass().getName()); w.hide(); }
				continue;
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
				continue;
			}
			readyF = f; readyNow = Actor.now();
			Coverage.look(); BotWindows.turn();
			if (Cheat.ON) { Cheat.apply(); Cheat.spendTalents(talentR); }
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
			if (depthDecisions.merge(depth, 1, Integer::sum) > FLOOR_TURNS * 2) { end = "depth timeout (" + FLOOR_TURNS * 2 + " decisions on depth " + depth + " over all visits)"; break; }
			if (++floorDecisions > FLOOR_TURNS) { end = "floor timeout (" + FLOOR_TURNS + " decisions on depth " + depth + ")"; break; }
			Assist.Target t = Cheat.ON ? Rescue.check() : null;
			if (t == null && Cheat.ON) t = BotItems.pick();
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
			boolean ok = Assist.act(t);
			// (the actor thread picks the action up on a later frame: until then hero.ready still reads true)
			for (int k = 0; k < 150 && Dungeon.hero.ready && Actor.now() == tn && Dungeon.hero.pos == pn && Ui.topWindow() == null && Dungeon.hero.isAlive(); k++) Lockstep.frame(app);
			if (!ok) { actFails++; if (Assist.lastError != null) { System.out.println("  ACT ERROR " + Assist.lastError); Assist.lastError.printStackTrace(System.out); Assist.lastError = null; } Dungeon.hero.rest(false); }
		}
		if (Dungeon.hero != null) System.out.println("state: ready " + Dungeon.hero.ready + " action " + Dungeon.hero.curAction + " aiming " + Controls.aiming() + " window " + Ui.topWindow() + " scene " + Game.scene() + " resting " + Dungeon.hero.resting + " now " + Actor.now());
		String cause = "";
		if (end.equals("died")) synchronized (log) { cause = String.join(" / ", log); }
		System.out.println("decisions by reason: " + whys);
		System.out.printf("plan time: avg %.2f ms, max %.1f ms over %d plans%n", planNs / 1e6 / Math.max(1, plans), maxPlanNs / 1e6, plans);
		if (!end.equals("WIN") && !end.equals("died") && !v) { System.out.println("last decisions:"); for (String l : recent) System.out.println(l); }
		Coverage.report(end);
		System.out.printf("RESULT class %s seed %d: %s depth %d (max %d) turns %.0f lvl %d hp %d/%d picked %d stuck %d actfails %d windows %d decisions %d frames %d  %.1fs%s%n",
				cls, seed, end, Dungeon.depth, maxD, Statistics.duration, Dungeon.hero == null ? 0 : Dungeon.hero.lvl, Dungeon.hero == null ? 0 : Dungeon.hero.HP, Dungeon.hero == null ? 0 : Dungeon.hero.HT,
				picked, stuckEvents, actFails, windows, decisions, Lockstep.frames, (System.currentTimeMillis() - t0) / 1000.0, cause.isEmpty() ? "" : "\n  last log: " + cause);
		System.exit(0);
	}

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
	static final int FLOOR_FRAMES = 150_000;   // (about 3 game hours on a floor)
	static final int HANG_FRAMES = 3000;   // 100 s of game time with the hero waiting and the clock not moving
}
