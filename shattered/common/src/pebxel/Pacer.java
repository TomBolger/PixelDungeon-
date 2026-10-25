package pebxel;

import java.util.ArrayDeque;
import java.util.List;

/**
 * The phone's outbox to the watch, paced to what the radio can carry. Used by the Android app (WatchSession),
 * the emulator's server (Server) and the radio tests (RadioTest), so all three behave the same.
 *
 * A Pebble takes one AppMessage at a time and acknowledges each one; over real Bluetooth that is a few KB a
 * second, with 50-150 ms per round trip. The game makes changes faster than that at busy moments (a fight, a
 * window opening over animations). Noting every frame's changes while a message is out would build a backlog
 * that the watch then plays "fast then slow", ever later. So:
 *  - while a message is out, each frame's changes are still noted (they play back at their own pace) but only
 *    while what waits is small (what the radio carries in about TARGET ms, as measured; at most BUDGET bytes);
 *    past that, frames are skipped and their changes merge into the next one (the Link's copy of the watch
 *    makes the next sync the difference to the newest state);
 *  - when the watch takes the message, everything noted goes in the next one (cut to at most CAP bytes each);
 *  - a refused or lost message is sent again (after 0.25, 0.5, 1 s); only after that is the watch's whole
 *    screen sent again, a little later;
 *  - a message the radio never answers counts as lost after WATCHDOG ms (the game must not wait forever).
 * frame() runs on the game thread; done() may come from any thread.
 */
public final class Pacer {
	/** Sends one message to the watch; the radio must later call done(ok) once (from any thread). */
	public interface Radio { void send(byte[] msg); }

	static int env(String k, int d) { try { String v = System.getenv(k); return v == null ? d : Integer.parseInt(v); } catch (Exception e) { return d; } }
	/** Bytes of noted changes beyond which further frames merge while the radio is busy: what the radio carries in
	 *  TARGET ms (a press waits behind about one such message), between MIN_BUDGET and BUDGET. */
	public static int BUDGET = env("PACE_BUDGET", 1200), MIN_BUDGET = 300, TARGET = env("PACE_TARGET", 350);
	/** Largest message (smaller ones cost less when lost, and less of the watch's memory at once). */
	public static int CAP = env("PACE_CAP", 2400);
	public static int WATCHDOG = 10000;
	static final int[] RETRY = {250, 500, 1000};
	/** For comparing with how it was before (RadioTest): every frame noted, everything sent, a full resend on a failure. */
	public static boolean OLD = System.getenv("PACE_OLD") != null;

	private final Link link;
	private final Radio radio;
	private final ArrayDeque<byte[]> queue = new ArrayDeque<>();
	private byte[] out;            // the message the radio has now (or that waits to be tried again)
	private long outAt, retryAt;
	private int fails;
	private boolean restart, stale;   // stale: what is out was from before a restart (its answer only frees the radio)
	private int maxMsg = 1000;
	private float sinceSync;
	private long nowMs, sentAt;
	private float rate;            // bytes a second the radio has carried lately (0: not known yet)
	/** The clock (ms); RadioTest's runs in simulated time. */
	public java.util.function.LongSupplier clock = System::currentTimeMillis;

	public long bytes, msgs, merged, retries, resets;

	public Pacer(Link link, Radio radio) { this.link = link; this.radio = radio; }

	/** The watch (re)started and takes messages up to size bytes: start it over with the whole screen. */
	public synchronized void hello(int size) { maxMsg = Math.max(200, size - 16); restart = true; }

	/** Whether a message is out or waiting to go. */
	public synchronized boolean busy() { return out != null || !queue.isEmpty() || retryAt != 0; }

	/** Bytes waiting for the radio (noted changes and cut messages). */
	public synchronized int waiting() { int n = link.pendingBytes(); for (byte[] m : queue) n += m.length; return n; }

	/**
	 * Every game frame (dt: game seconds since the last call). sync=false: the watch is away
	 * (or the phone has the game): nothing new is noted or sent, only what is out is seen through.
	 */
	public void frame(float dt, boolean sync) {
		long now = clock.getAsLong();
		sinceSync += dt;
		boolean busy, reset;
		synchronized (this) {
			nowMs = now;
			if (out != null && retryAt == 0 && now - outAt > WATCHDOG) failed();   // the radio never answered
			if (retryAt != 0 && now >= retryAt) {
				retryAt = 0;
				if (out != null) { retries++; transmit(out); }
				else restart = true;     // gave up on the last one: the whole screen again
			}
			busy = out != null || !queue.isEmpty() || retryAt != 0;
			reset = restart && sync; if (reset) restart = false;
		}
		if (reset) {
			// (a message still out keeps the radio until it is answered: never two at once)
			synchronized (this) { queue.clear(); if (retryAt != 0) out = null; stale = out != null; retryAt = 0; fails = 0; }
			link.reset(); resets++;
			busy = false;
		}
		if (!sync) { sinceSync = 0; return; }
		if (OLD || !busy || link.pendingBytes() < budget()) { link.frame(sinceSync); sinceSync = 0; }
		else merged++;
		synchronized (this) {
			if (out == null && retryAt == 0 && queue.isEmpty() && !restart && link.pending()) {
				queue.addAll(link.take(OLD ? maxMsg : Math.min(maxMsg, CAP)));
				next();
			}
		}
	}

	/** In place of whatever waits: this one message (a notice on an empty screen), then nothing until hello(). */
	public synchronized void replace(byte[] msg) {
		queue.clear(); link.dropPending();
		if (out == null && retryAt == 0) { out = msg; transmit(msg); } else queue.add(msg);
	}

	/** The radio's answer for the message it was given. */
	public synchronized void done(boolean ok) {
		if (out == null || retryAt != 0) return;   // (an answer after the watchdog gave up on it)
		if (stale) { stale = false; out = null; next(); return; }
		if (ok) {
			// the radio's pace, from messages big enough that their bytes, not the round trip, take most of the time
			long took = clock.getAsLong() - sentAt;
			if (fails == 0 && out.length >= 400 && took > 0) { float r = out.length * 1000f / took; rate = rate == 0 ? r : rate * 0.75f + r * 0.25f; }
			if (fails == 0 && took > 0) msgMs = msgMs == 0 ? took : msgMs * 0.8f + took * 0.2f;
			fails = 0; out = null; next(); return;
		}
		failed();
	}

	private void failed() {
		if (stale) { stale = false; out = null; next(); return; }
		fails++;
		if (!OLD && fails <= RETRY.length) { retryAt = nowMs + RETRY[fails - 1]; return; }
		// the watch isn't taking anything (closed, out of range): start it over in a moment
		queue.clear(); out = null; fails = 0;
		retryAt = nowMs + 2000;
	}

	private void next() {
		out = queue.poll();
		if (out != null) transmit(out);
	}

	private void transmit(byte[] m) { outAt = nowMs; sentAt = clock.getAsLong(); bytes += m.length; msgs++; radio.send(m); }

	/** See BUDGET. */
	public synchronized int budget() { return rate == 0 ? BUDGET : Math.max(MIN_BUDGET, Math.min(BUDGET, Math.round(rate * TARGET / 1000))); }
	public synchronized float rate() { return rate; }
	/** Real ms a message has taken lately, sent to answered (0: not known yet). */
	public synchronized float rtt() { return msgMs; }
	private float msgMs;
}
