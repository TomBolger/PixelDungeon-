package pebxel.android;

import android.content.Context;
import com.badlogic.gdx.Files;
import com.badlogic.gdx.backends.android.DefaultAndroidFiles;
import android.content.ContextWrapper;
import com.badlogic.gdx.backends.android.AndroidPreferences;
import com.badlogic.gdx.utils.GdxNativesLoader;
import com.shatteredpixel.shatteredpixeldungeon.SPDSettings;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.util.ArrayDeque;
import java.util.List;
import pebxel.*;
import pebxel.Shared;

/**
 * One game for the watch: Shattered PD running with no screen, in real time, its pictures going to the
 * watch through the link. Uses the same saves and settings as the phone's own Shattered screen.
 */
class WatchSession extends Thread {
	private final WatchService service;
	private volatile boolean running = true, closing, helloPending, inFlight;
	private volatile int maxMsg = 1000;
	private volatile long retryAt;
	private final ArrayDeque<Integer> buttons = new ArrayDeque<>();
	private final ArrayDeque<byte[]> queue = new ArrayDeque<>();
	private WatchApp app;

	WatchSession(WatchService s) { super("Watch game"); service = s; }

	private volatile int screenCode;
	private volatile boolean started;
	void screen(int code) { if (!started) screenCode = code; }
	int screen() { return screenCode; }
	boolean started() { return started; }

	void hello(int size) { maxMsg = Math.max(200, size - 16); helloPending = true; opening = true; }
	void button(int code) { synchronized (buttons) { buttons.add(code); } }
	private final Touches touches = new Touches();
	void touch(byte[] b) { touches.add(b); }
	void watchClosed() { closing = true; opening = false; }
	/** The watch app opened again while this game was still running. */
	void watchOpened() { opening = true; closing = false; }
	private volatile boolean opening;

	private volatile Boolean phoneChange;
	private volatile long phoneAsk;
	private boolean phoneHas;
	/** The phone's own screen opened (true: it waits for us to save, see Handoff) or closed (false). */
	void phoneActive(boolean active, long ask) { if (active) phoneAsk = ask; phoneChange = active; }
	void stopSession() { running = false; }
	void stopNow() { running = false; interrupt(); }

	@Override
	public void run() {
		try { play(); }
		catch (Throwable t) {
			// show what went wrong on the watch, so it can be reported
			android.util.Log.e("PebxelWatch", "watch game crashed", t);
			Crashes.note(service.getApplicationContext(), t);
			java.io.StringWriter w = new java.io.StringWriter(); t.printStackTrace(new java.io.PrintWriter(w));
			String msg = w.toString().replace("com.shatteredpixel.shatteredpixeldungeon.", "").replace("\t", " ");
			service.send(Link.errorScreen("Shattered stopped:\n" + msg), ok -> { });
		}
	}

	private void play() {
		Context c = service.getApplicationContext();
		try {
			Game.version = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName;
			Game.versionCode = c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionCode;
		} catch (Exception e) { Game.version = "???"; Game.versionCode = 0; }

		// the watch's screen comes with its hello: wait a moment for it (Emery if it doesn't come)
		for (int i = 0; i < 60 && screenCode == 0 && running; i++) { try { Thread.sleep(50); } catch (InterruptedException e) { return; } }
		started = true;
		if (screenCode != 0) Screen.set(screenCode); else screenCode = Screen.code();
		WatchGame game = new WatchGame();
		game.drawAlways = true;
		Files files = new DefaultAndroidFiles(c.getAssets(), new ContextWrapper(c), false);
		app = new WatchApp(game, "", false, files, GdxNativesLoader::load,
				name -> new AndroidPreferences(c.getSharedPreferences(name, Context.MODE_PRIVATE)));
		WatchFiles.inApp = true;
		FileUtils.setDefaultFileProperties(Files.FileType.Local, "");
		SPDSettings.set(app.getPreferences("ShatteredPixelDungeon"));
		app.create();
		Link link = new Link(new Assets());
		Controls controls = new Controls();

		Shared.mark(Shared.WATCH_ON, true);
		try { loop(c, game, link, controls); }
		finally {
			// stopping: save unless the phone has the game
			if (!phoneHas) { game.pause(); Shared.release(Shared.WATCH); }
			Shared.mark(Shared.WATCH_ON, false);
		}
	}

	private long lastError; private int errorRun;

	private void standAside() {
		synchronized (queue) { queue.clear(); }
		service.send(Link.errorScreen("Playing on your phone.\n\nLeave the game there\nto carry on here."), ok -> { });
	}

	private void loop(Context c, WatchGame game, Link link, Controls controls) {
		long frameNs = 1_000_000_000L / 30, next = System.nanoTime();
		float sinceSync = 0;
		if (Shared.marked(Shared.PHONE_ON)) {
			// the phone screen has the game: wait for it (our copy in memory is nothing yet)
			phoneHas = true;
			com.shatteredpixel.shatteredpixeldungeon.Dungeon.pebxelHold = true;
			standAside();
		} else {
			// carry on whatever the phone played last
			Shared.takeOver(Shared.WATCH);
		}
		while (running && !app.exited) {
			Boolean pc = phoneChange;
			if (pc != null) {
				phoneChange = null;
				if (pc) {
					if (!phoneHas) {
						// save and step aside while the phone plays
						phoneHas = true;
						game.pause();
						Shared.release(Shared.WATCH);
						standAside();
					}
					Shared.writeText(Shared.WATCH_ACK, Long.toString(phoneAsk));   // the phone may go ahead
				} else if (phoneHas) {
					// back to the watch: carry on from what the phone saved
					phoneHas = false;
					Shared.takeOver(Shared.WATCH);
					helloPending = true;
				}
			}
			if (phoneHas) {
				// still standing aside; if the phone screen went away without telling us, notice from its file
				if (!Shared.marked(Shared.PHONE_ON)) phoneChange = false;
				try { Thread.sleep(100); } catch (InterruptedException e) { break; }
				continue;
			}
			if (opening) {
				// the watch app came back: the phone may have played meanwhile (or not: then nothing changes)
				opening = false;
				Shared.takeOver(Shared.WATCH);
			}
			Integer b;
			synchronized (buttons) { b = buttons.poll(); }
			try {
				if (b != null) controls.button(b);
				touches.frame(link, controls, Game.elapsed);
				app.frame();
				controls.frame();
			} catch (Throwable t) {
				// one bad frame (a window that broke, say) shouldn't end the game: note it, close the top window and go on;
				// only a run of them stops the session (and the watch's next start makes a new one)
				android.util.Log.e("PebxelWatch", "frame failed", t);
				Crashes.note(c, t);
				long nowMs = System.currentTimeMillis();
				if (nowMs - lastError > 3000) errorRun = 0;
				lastError = nowMs;
				if (++errorRun >= 5) throw t;
				try { com.shatteredpixel.shatteredpixeldungeon.ui.Window w = pebxel.Ui.topWindow(); if (w != null) w.hide(); } catch (Throwable t2) { }
			}
			sinceSync += Game.elapsed;
			if (helloPending) { helloPending = false; synchronized (queue) { queue.clear(); } link.reset(); inFlight = false; }
			if (closing) {
				// the watch app was closed: save, like the phone does when it goes to the background
				closing = false;
				game.pause();
				Shared.release(Shared.WATCH);
			}
			if (retryAt != 0 && System.currentTimeMillis() > retryAt) { retryAt = 0; helloPending = true; }
			if (retryAt == 0) link.frame(sinceSync); sinceSync = 0;
			if (!inFlight && isEmpty() && retryAt == 0 && link.pending()) {
				List<byte[]> ps = link.take(maxMsg);
				synchronized (queue) { queue.addAll(ps); }
				sendNext();
			}
			next += frameNs;
			long sleep = next - System.nanoTime();
			try {
				if (sleep > 0) Thread.sleep(sleep / 1_000_000, (int) (sleep % 1_000_000));
				else next = System.nanoTime();
			} catch (InterruptedException e) { break; }
		}
	}

	private boolean isEmpty() { synchronized (queue) { return queue.isEmpty(); } }

	private void sendNext() {
		byte[] p;
		synchronized (queue) { p = queue.poll(); }
		if (p == null) { inFlight = false; return; }
		inFlight = true;
		service.send(p, ok -> {
			if (ok) sendNext();
			else {
				// the watch didn't take it (closed, out of range): send it the whole screen again in a moment
				synchronized (queue) { queue.clear(); }
				retryAt = System.currentTimeMillis() + 2000;
				inFlight = false;
			}
		});
	}
}
