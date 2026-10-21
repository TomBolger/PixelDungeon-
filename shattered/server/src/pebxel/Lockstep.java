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

	public static long frames, waits;
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
