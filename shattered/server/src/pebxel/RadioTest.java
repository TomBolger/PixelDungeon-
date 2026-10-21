package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.util.*;

/**
 * The game as the watch gets it over a real radio (Bluetooth through the phone's Pebble app), in simulated time:
 * the phone side is the real Pacer and Link, the watch is WatchSim (with main.c's play-out), and in between a
 * model of AppMessage: one message at a time, each answered after a round trip plus its bytes at the radio's
 * rate; now and then one refused (nack) or lost (no answer until a timeout).
 * For each scenario: bytes and messages a second, and the time from a press on the watch until the watch plays
 * the phone's first frame made after it (what the press changed), plus what took the bytes.
 *
 * Args: scenarios (walk fight menu bag floor idle; default all). Env, as tools/bridge.py:
 *   RTT_MS=100 JITTER_MS=40 RATE=3000 (bytes/s) NACK_PCT=1 DROP_PCT=0.5 TIMEOUT_MS=3000 INBOX=8000
 *   PACE_OLD=1: the pacing as it was (every frame noted, all sent, a whole new screen on any failure)
 *   SEED=n (radio dice)  VERBOSE=1
 */
public class RadioTest {
	static double env(String k, double d) { String v = System.getenv(k); return v == null ? d : Double.parseDouble(v); }
	static final double RTT = env("RTT_MS", 100) / 1000, JIT = env("JITTER_MS", 40) / 1000, RATE = env("RATE", 3000),
			NACK = env("NACK_PCT", 1) / 100, DROP = env("DROP_PCT", 0.5) / 100, TIMEOUT = env("TIMEOUT_MS", 3000) / 1000;
	static final int INBOX = (int) env("INBOX", 8000);
	static final boolean VERBOSE = System.getenv("VERBOSE") != null;
	static final double FRAME = 1 / 30.0;
	static final Random dice = new Random((long) env("SEED", 1));

	static WatchApp app;
	static Link link;
	static Pacer pacer;
	static WatchSim sim;
	static Controls controls;
	static double t;        // real seconds
	static float gclock;    // game seconds (the phone's link clock)

	// ---- the radio ----
	static final class Ev { double at; int kind; byte[] m; Runnable r; Ev(double at, int kind, byte[] m) { this.at = at; this.kind = kind; this.m = m; } }
	static final int ARRIVE = 0, ANSWER_OK = 1, ANSWER_FAIL = 2, PRESS = 3;
	static final PriorityQueue<Ev> events = new PriorityQueue<>((a, b) -> Double.compare(a.at, b.at));
	static double half() { return Math.max(0.005, RTT / 2 + (dice.nextDouble() - 0.5) * JIT); }
	static long nacks, drops;

	static void radioSend(byte[] m) {
		double transit = half() + m.length / RATE;
		if (m.length + 8 > INBOX) { events.add(new Ev(t + 2 * half(), ANSWER_FAIL, m)); nacks++; return; }
		double r = dice.nextDouble();
		if (r < DROP) { events.add(new Ev(t + TIMEOUT, ANSWER_FAIL, m)); drops++; return; }
		if (r < DROP + NACK) { events.add(new Ev(t + transit + half(), ANSWER_FAIL, m)); nacks++; return; }
		double arrive = t + transit;
		events.add(new Ev(arrive, ARRIVE, m));
		events.add(new Ev(arrive + half() + 0.002, ANSWER_OK, m));
	}

	// ---- presses ----
	static final class Press { double at, appliedAt = -1; float target; double seen = -1; String what; }
	static final List<Press> presses = new ArrayList<>();
	static void press(String what, Runnable action) {
		Press p = new Press(); p.at = t; p.what = what; presses.add(p);
		Ev e = new Ev(t + half() + 0.004, PRESS, null);   // (the watch's own outbox, then half a round trip)
		e.r = () -> { action.run(); p.appliedAt = t; p.target = gclock; };
		events.add(e);
	}

	/** Runs the radio up to time `until`, delivering and answering on the way. */
	static void radio(double until) {
		while (!events.isEmpty() && events.peek().at <= until) {
			Ev e = events.poll();
			if (e.kind == PRESS) { pendingActions.add(e.r); continue; }
			double was = t; t = e.at;
			sim.advance((float) ((t - was) * Game.pebxelSpeed)); seen();
			if (e.kind == ARRIVE) { sim.receive(e.m); seen(); }
			else pacer.done(e.kind == ANSWER_OK);
		}
	}
	static final List<Runnable> pendingActions = new ArrayList<>();

	static void seen() {
		for (Press p : presses) if (p.appliedAt >= 0 && p.seen < 0 && sim.played >= Math.round(p.target * 1000) - 2) p.seen = t;
	}

	/** One frame of real time: the phone's loop (as WatchSession), the radio, the watch. */
	static void frame() {
		double next = t + FRAME;
		radio(next);
		t = next;
		for (Runnable r : pendingActions) { try { r.run(); } catch (Throwable e) { e.printStackTrace(); } }
		pendingActions.clear();
		app.frame(); controls.frame();
		gclock += Game.elapsed;
		pacer.frame(Game.elapsed, true);
		sim.advance((float) (t * Game.pebxelSpeed - sim.now)); seen();
		if (Game.scene() instanceof InterlevelScene) { try { Thread.sleep(2); } catch (InterruptedException e) { } HandoffTest.pressOn(); }
		peak = Math.max(peak, pacer.waiting());
	}
	static int peak;

	static void run(double seconds) { for (double end = t + seconds; t < end; ) frame(); }

	/** Until the radio has carried everything (or a while). */
	static void settle(double max) {
		double end = t + max;
		run(0.2);
		while (t < end && (pacer.busy() || link.pending() || !(Game.scene() instanceof GameScene) || Dungeon.hero == null || !Dungeon.hero.ready)) frame();
		run(0.5);
	}

	// ---- scenarios ----
	static int home = -1, away = -1;
	static void walk() {
		for (int i = 0; i < 10; i++) {
			if (home < 0) {
				home = Dungeon.hero.pos;
				com.watabou.utils.PathFinder.buildDistanceMap(home, com.watabou.utils.BArray.or(Dungeon.level.passable, Dungeon.level.avoid, null));
				for (int c = 0; c < Dungeon.level.length(); c++)
					if (com.watabou.utils.PathFinder.distance[c] >= 4 && com.watabou.utils.PathFinder.distance[c] <= 6 && Dungeon.level.map[c] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.EMPTY) { away = c; break; }
			}
			int to = i % 2 == 0 ? away : home;
			press("walk", () -> { if (Ui.topWindow() != null) Ui.topWindow().hide(); if (Dungeon.hero.handle(to)) Dungeon.hero.next(); });
			run(1.6);
		}
	}

	static void fight() {
		Gallery.calm();
		List<Mob> mobs = new ArrayList<>();
		for (int i = 0; i < 12; i++) {
			mobs.removeIf(m -> !m.isAlive());
			while (mobs.size() < 4) {
				int cell = -1;
				for (int d : com.watabou.utils.PathFinder.NEIGHBOURS8) {
					int c = Dungeon.hero.pos + d;
					if (Dungeon.level.passable[c] && Actor.findChar(c) == null) { cell = c; break; }
				}
				if (cell < 0) break;
				Mob m = new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Gnoll();
				m.pos = cell; m.HP = m.HT = 40; GameScene.add(m); m.state = m.HUNTING; m.aggro(Dungeon.hero);
				mobs.add(m);
			}
			if (mobs.isEmpty()) break;
			Mob target = mobs.get(0);
			press("attack", () -> { if (target.isAlive() && Dungeon.hero.ready && Dungeon.hero.handle(target.pos)) Dungeon.hero.next(); });
			run(1.0);
		}
		Gallery.calm();
		for (Mob m : Dungeon.level.mobs.toArray(new Mob[0])) { m.destroy(); if (m.sprite != null) m.sprite.killAndErase(); }
	}

	static void menu() {
		press("menu", () -> controls.button(Controls.UP | Controls.LONG)); run(1.5);
		for (int i = 0; i < 9; i++) { press("menu down", () -> controls.button(Controls.DOWN)); run(0.8); }
		press("menu back", () -> controls.button(Controls.BACK)); run(1.5);
	}

	static void bag() {
		press("bag", () -> controls.button(Controls.SELECT | Controls.LONG)); run(1.5);
		for (int i = 0; i < 6; i++) { press("bag down", () -> controls.button(Controls.DOWN)); run(0.7); }
		press("bag back", () -> controls.button(Controls.BACK)); run(1.5);
		while (Ui.topWindow() != null) { Ui.topWindow().hide(); run(0.3); }
	}

	static void floor() {
		Press[] p = new Press[1];
		press("descend", () -> Gallery.go(Dungeon.depth + 1));
		p[0] = presses.get(presses.size() - 1);
		double end = t + 60; float shown = -1; double settled = -1;
		while (t < end) {
			frame();
			if (shown < 0 && p[0].appliedAt >= 0 && Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready) shown = gclock;
			if (shown >= 0 && sim.played >= Math.round(shown * 1000) - 2 && !pacer.busy() && !link.pending()) { settled = t; break; }
		}
		System.out.printf("  floor: loading screen shown after %.2f s, new level fully shown after %.2f s%n", p[0].seen - p[0].at, settled - p[0].at);
		run(1);
	}

	static void idle() { run(10); }

	/** Idle beside sleeping monsters (their "Zz" pulses). */
	static void sleepers() {
		Gallery.calm();
		for (int d : new int[]{2, -2}) {
			int c = Dungeon.hero.pos + d;
			if (!Dungeon.level.passable[c] || Actor.findChar(c) != null) continue;
			Mob m = new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Rat();
			m.pos = c; GameScene.add(m); m.state = m.SLEEPING;
		}
		run(1);
		long b0 = pacer.bytes, m0 = pacer.msgs; double t0 = t;
		run(10);
		System.out.printf("  sleepers: %.0f B/s, %.1f msg/s once settled%n", (pacer.bytes - b0) / (t - t0), (pacer.msgs - m0) / (t - t0));
	}

	public static void main(String[] a) {
		try { test(a); } catch (Throwable e) { e.printStackTrace(); }
		System.exit(0);
	}

	static void test(String[] a) throws Exception {
		List<String> which = a.length > 0 ? Arrays.asList(a) : Arrays.asList("idle", "walk", "fight", "menu", "bag", "floor");
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		app = Smoke.app = new WatchApp(game, ".pebxel-radio", false);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-radio/");
		app.create();
		for (int i = 0; i < 5; i++) app.frame();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			app.frame(); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
		}
		Assets as = new Assets();
		link = new Link(as);
		controls = new Controls();
		link.planner = controls::plan;
		sim = new WatchSim(new java.io.File("."), as);
		pacer = new Pacer(link, RadioTest::radioSend);
		pacer.clock = () -> Math.round(t * 1000);
		pacer.hello(INBOX);
		System.out.printf("radio: round trip %.0f ms (+-%.0f), %.0f bytes/s, %.1f%% nack, %.1f%% lost (%.0f s timeout); %s pacing%n",
				RTT * 1000, JIT * 500, RATE, NACK * 100, DROP * 100, TIMEOUT, Pacer.OLD ? "OLD" : "new");
		settle(30);
		System.out.printf("first screen: %.1f s, %d bytes%n", t, pacer.bytes);
		for (String s : which) {
			long b0 = pacer.bytes, m0 = pacer.msgs, r0 = pacer.resets, mg0 = pacer.merged; double t0 = t; int p0 = presses.size(); peak = 0;
			for (int k = 0; k < 10 && Ui.topWindow() != null; k++) { Ui.topWindow().hide(); run(0.3); }
			settle(20);
			b0 = pacer.bytes; m0 = pacer.msgs; r0 = pacer.resets; mg0 = pacer.merged; t0 = t; p0 = presses.size(); peak = 0;
			link.stats.clear();
			switch (s) {
				case "walk": walk(); break; case "fight": fight(); break; case "menu": menu(); break;
				case "bag": bag(); break; case "floor": floor(); break; case "idle": idle(); break; case "sleepers": sleepers(); break;
				default: System.out.println("no scenario " + s); continue;
			}
			double dt = t - t0;
			List<Double> lat = new ArrayList<>(); int unseen = 0;
			for (Press p : presses.subList(p0, presses.size())) if (p.seen >= 0) lat.add(p.seen - p.at); else unseen++;
			Collections.sort(lat);
			double mean = lat.stream().mapToDouble(x -> x).average().orElse(0);
			System.out.printf("RESULT %-6s %5.0f B/s %4.1f msg/s | press->shown mean %4.0f ms, median %4.0f, worst %5.0f%s | peak wait %5d B, full resends %d, frames merged %d%n",
					s, (pacer.bytes - b0) / dt, (pacer.msgs - m0) / dt, mean * 1000, lat.isEmpty() ? 0 : lat.get(lat.size() / 2) * 1000,
					lat.isEmpty() ? 0 : lat.get(lat.size() - 1) * 1000, unseen > 0 ? " (" + unseen + " never)" : "", peak, pacer.resets - r0, pacer.merged - mg0);
			TreeMap<Long, String> top = new TreeMap<>();
			for (Map.Entry<String, long[]> e : link.stats.entrySet()) top.put(-e.getValue()[1] * 1000 - top.size(), String.format("%-34s x%-5d %7d B", e.getKey(), e.getValue()[0], e.getValue()[1]));
			int n = 0; for (String x : top.values()) { System.out.println("    " + x); if (++n == (VERBOSE ? 20 : 6)) break; }
			if (VERBOSE) System.out.println("    now: " + Game.scene().getClass().getSimpleName() + (Ui.topWindow() != null ? " + " + Ui.topWindow().getClass().getSimpleName() : ""));
			if (VERBOSE) for (Press p : presses.subList(p0, presses.size())) System.out.printf("      %s at %.2f: %.0f ms%n", p.what, p.at, (p.seen - p.at) * 1000);
		}
		System.out.printf("radio measured %.0f bytes/s (budget %d)%n", pacer.rate(), pacer.budget());
		System.out.printf("radio: %d nacks, %d lost; watch got %d messages it couldn't apply%n", nacks, drops, sim.errors);
		System.exit(0);
	}
}
