package pebxel.android;

import android.content.Context;
import android.content.Intent;
import com.watabou.noosa.Game;
import pebxel.Shared;

/**
 * Passing the game between the phone's screen and the watch (see pebxel.Shared). Both play from the same
 * saves; only one plays at a time.
 *  - The phone screen opens: it asks the watch to save and step aside, waits for it to say it has, then
 *    carries on whatever the watch was playing.
 *  - The phone screen closes: Shattered has just saved; the note says so, and the watch carries on from it.
 */
public final class Handoff {
	static final String ACTION_PHONE = "pebxel.PHONE", EXTRA_ACTIVE = "active", EXTRA_ASK = "ask";
	private static volatile long asking;

	/** The phone screen is back (main process, after Shattered resumed). */
	public static void phoneResumed(Context c) {
		final long ask = System.currentTimeMillis();
		asking = ask;
		Shared.mark(Shared.PHONE_ON, true);
		tellWatch(c, true, ask);
		new Thread(() -> {
			// the watch saves first, if it is running; give it a few seconds at most
			long until = ask + 4000;
			while (Shared.marked(Shared.WATCH_ON) && Shared.number(Shared.WATCH_ACK) < ask && System.currentTimeMillis() < until) {
				try { Thread.sleep(50); } catch (InterruptedException e) { return; }
			}
			if (asking != ask) return;   // closed again meanwhile
			Game.runOnRenderThread(() -> Shared.takeOver(Shared.PHONE));
		}, "handoff").start();
	}

	/** The phone screen went away (main process, after Shattered saved). */
	public static void phonePaused(Context c) {
		asking = 0;
		Shared.release(Shared.PHONE);
		Shared.mark(Shared.PHONE_ON, false);
		tellWatch(c, false, 0);
	}

	private static void tellWatch(Context c, boolean active, long ask) {
		try {
			c.startService(new Intent(c, WatchService.class).setAction(ACTION_PHONE)
					.putExtra(EXTRA_ACTIVE, active).putExtra(EXTRA_ASK, ask));
		} catch (Exception e) { /* not allowed right now (background limits): the watch finds out from the files */ }
	}
}
