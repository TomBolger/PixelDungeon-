package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import java.lang.management.ManagementFactory;
import java.lang.management.ThreadMXBean;
import java.lang.reflect.Field;
import java.util.concurrent.locks.LockSupport;

/**
 * Headless frames in lockstep with Shattered's own threads, so a bot game plays the same however loaded the machine
 * is, and as fast as the CPU allows. Each frame is a fixed 1/30 s of game time (WatchGraphics.delta); after it, the
 * actor thread runs until it waits again (Actor.pebxelBusy, set by the wake-ups in GameScene and the sprites), and a
 * floor being built between floors (InterlevelScene's thread) is waited for. No wall clock decides anything: the only
 * time limits here are on CPU time spent by those threads (a real endless loop), never on how long we waited.
 * Env SIMSEED=n (mixed with the dungeon seed) also seeds Shattered's unseeded random numbers (combat, AI), for repeatable games.
 */
public final class Lockstep {
	private Lockstep() { }

	public static volatile long frames;
	public static long waits;
	/** What the bot was doing (for the watchdog's report), set as it goes. */
	public static volatile String doing = "starting";

	/**
	 * The last resort (one game hung on Monolith for an hour with the actor thread blocked, not spinning, so the CPU
	 * limits never fired): a watchdog that ends the game when no frame at all has finished for HANGSECS (default 120)
	 * seconds of wall time. Frames take well under a millisecond, so this never decides anything in a game that runs,
	 * however loaded the machine; it only turns a deadlock into a report (every thread's stack) and an exit.
	 */
	public static void watchdog(String who) {
		int secs = Integer.parseInt(System.getenv().getOrDefault("HANGSECS", "120"));
		Thread t = new Thread(() -> {
			long last = -1, since = System.nanoTime();
			while (true) {
				try { Thread.sleep(5000); } catch (InterruptedException e) { return; }
				long f = frames;
				if (f != last) { last = f; since = System.nanoTime(); continue; }
				if (System.nanoTime() - since < secs * 1_000_000_000L) continue;
				System.out.println("HUNG: no frame finished in " + secs + " s (frame " + f + ", doing: " + doing + ")");
				for (java.util.Map.Entry<Thread, StackTraceElement[]> e : Thread.getAllStackTraces().entrySet()) {
					System.out.println(" thread " + e.getKey().getName() + " " + e.getKey().getState());
					for (int i = 0; i < Math.min(25, e.getValue().length); i++) System.out.println("    " + e.getValue()[i]);
				}
				int d = -1; try { d = com.shatteredpixel.shatteredpixeldungeon.Dungeon.depth; } catch (Throwable x) { }
				System.out.println("COVER end=hung;class=" + who.split(" ")[1] + ";depth=" + d + ";problems=watchdog: no frame in " + secs + " s doing " + doing.replace(';', ',').replace('=', ':') + ";");
				System.out.println("RESULT " + who + ": hung (watchdog: no frame in " + secs + " s, doing " + doing + ") depth " + d + " frames " + f);
				System.out.flush();
				Runtime.getRuntime().halt(3);
			}
		}, "pebxel watchdog");
		t.setDaemon(true);
		t.start();
	}
	/** Set when a thread ran away (minutes of CPU without waiting): the bot should report it and stop. */
	public static String stuck;
	private static Field levelThread;
	private static final ThreadMXBean MX = ManagementFactory.getThreadMXBean();
	static final long RUNAWAY_NS = 120_000_000_000L;   // 2 CPU minutes on one wake-up

	/** Seeds the base random generator (once, before the game starts), when SIMSEED is set. */
	public static void seed(long gameSeed) {
		String s = System.getenv("SIMSEED");
		if (s == null || s.isEmpty()) return;
		com.watabou.utils.Random.pebxelSource = new java.util.Random(Long.parseLong(s) * 1_000_003L + gameSeed);
		com.watabou.utils.Random.resetGenerators();
	}

	public static void frame(WatchApp app) { Actor.pebxelLockstep = true; Game.pebxelCreated = Lockstep::settle; app.frame(); frames++; settle(); }

	/** Waits (without the wall clock) until the actor thread and any floor building are idle. */
	public static void settle() {
		Thread lt = levelThread();
		if (lt != null && lt.isAlive()) {
			long c0 = cpu(lt);
			while (lt.isAlive()) {
				try { lt.join(200); } catch (InterruptedException e) { return; }
				if (lt.isAlive() && cpu(lt) - c0 > RUNAWAY_NS) { stuck = "floor building ran " + RUNAWAY_NS / 1_000_000_000 + " s of CPU"; dump(lt); return; }
			}
		}
		while (Actor.pebxelWakeLater() || Actor.pebxelBusy) awaitActor();
	}

	private static void awaitActor() {
		if (!Actor.pebxelBusy) return;
		waits++;
		Thread at = actorThread();
		long c0 = at == null ? 0 : cpu(at), n = 0;
		while (Actor.pebxelBusy) {
			n++;
			if (n < 2000) Thread.onSpinWait(); else LockSupport.parkNanos(20_000);
			if ((n & 4095) == 0) {
				if (at == null) at = actorThread();
				if (at == null || !at.isAlive()) return;
				if (cpu(at) - c0 > RUNAWAY_NS) { stuck = "actor thread ran " + RUNAWAY_NS / 1_000_000_000 + " s of CPU without waiting"; dump(at); return; }
			}
		}
	}

	private static long cpu(Thread t) { try { long v = MX.getThreadCpuTime(t.getId()); return v < 0 ? 0 : v; } catch (Throwable e) { return 0; } }

	private static Field actorField;
	static Thread actorThread() {
		try {
			if (actorField == null) { actorField = com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.class.getDeclaredField("actorThread"); actorField.setAccessible(true); }
			return (Thread) actorField.get(null);
		} catch (Exception e) { return null; }
	}

	private static Thread levelThread() {
		if (!(Game.scene() instanceof InterlevelScene)) return null;
		try {
			if (levelThread == null) { levelThread = InterlevelScene.class.getDeclaredField("thread"); levelThread.setAccessible(true); }
			return (Thread) levelThread.get(null);
		} catch (Exception e) { return null; }
	}

	private static void dump(Thread t) {
		System.out.println("RUNAWAY " + t.getName() + ": " + stuck);
		for (StackTraceElement e : t.getStackTrace()) System.out.println("    " + e);
	}
}
