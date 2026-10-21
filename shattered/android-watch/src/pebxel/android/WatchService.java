package pebxel.android;

import io.rebble.pebblekit2.client.java.BaseJavaPebbleListenerService;
import io.rebble.pebblekit2.client.java.DefaultJavaPebbleSender;
import io.rebble.pebblekit2.client.java.JavaPebbleSender;
import io.rebble.pebblekit2.common.model.PebbleDictionaryItem;
import io.rebble.pebblekit2.common.model.ReceiveResult;
import io.rebble.pebblekit2.common.model.TransmissionResult;
import io.rebble.pebblekit2.common.model.WatchIdentifier;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;

/**
 * The phone side of the watch app. The Pebble app wakes this service when Shattered PD is opened on
 * the watch, and hands it the watch's messages; the game itself runs in a WatchSession thread.
 * (Runs in its own process, ":watch", so it never shares memory with the phone's own game screen.)
 */
public class WatchService extends BaseJavaPebbleListenerService {
	public static final UUID APP = UUID.fromString("84d52e02-b98a-4d9c-b021-a9e3c442d0ca");
	static final int KEY_DATA = 1, KEY_HELLO = 2, KEY_BUTTON = 3, KEY_VERSION = 5, KEY_TOUCH = 6, KEY_SCREEN = 7;

	private JavaPebbleSender sender;
	private static WatchSession session;

	@Override
	public void onCreate() {
		super.onCreate();
		sender = new DefaultJavaPebbleSender(this);
	}

	@Override
	public void onDestroy() {
		if (session != null) { session.stopNow(); session = null; }
		try { sender.close(); } catch (Exception e) { }
		super.onDestroy();
	}

	private synchronized WatchSession session() {
		// (a session that stopped, after a crash say, is replaced: the next start of the watch app plays on)
		if (session == null || !session.isAlive()) { session = new WatchSession(this); session.start(); }
		return session;
	}

	/** The phone's own Shattered screen came or went (see Handoff). */
	@Override
	public int onStartCommand(android.content.Intent intent, int flags, int startId) {
		if (intent != null && Handoff.ACTION_PHONE.equals(intent.getAction()) && session != null)
			session.phoneActive(intent.getBooleanExtra(Handoff.EXTRA_ACTIVE, false), intent.getLongExtra(Handoff.EXTRA_ASK, 0));
		if (session == null) stopSelf(startId);
		return START_NOT_STICKY;
	}

	@Override
	protected void onAppOpened(UUID app, String watch) {
		if (!APP.equals(app)) return;
		boolean had;
		synchronized (this) { had = session != null; }
		WatchSession s = session();
		if (had) s.watchOpened();
	}

	@Override
	protected void onAppClosed(UUID app, String watch) {
		if (APP.equals(app) && session != null) session.watchClosed();
	}

	@Override
	protected void onMessageReceived(UUID app, Map<Integer, ? extends PebbleDictionaryItem> data, String watch, Consumer<ReceiveResult> responder) {
		if (!APP.equals(app)) { responder.accept(ReceiveResult.Nack.INSTANCE); return; }
		// always answer: an exception here would leave the watch's outbox waiting ~10 s with no buttons or taps going out
		try { received(data); } catch (Throwable e) { Crashes.note(getApplicationContext(), e); }
		responder.accept(ReceiveResult.Ack.INSTANCE);
	}

	private void received(Map<Integer, ? extends PebbleDictionaryItem> data) {
		WatchSession s = session();
		Integer hello = number(data.get(KEY_HELLO)), button = number(data.get(KEY_BUTTON)), version = number(data.get(KEY_VERSION));
		if (hello != null) {
			int v = version == null ? 1 : version;
			Integer scr = number(data.get(KEY_SCREEN));
			if (v != pebxel.Link.PROTOCOL) send(pebxel.Link.wrongVersion(v), ok -> { });
			else if (scr != null && s.started() && scr != s.screen()) {
				// another watch (a round one, say): start the game over for its screen
				synchronized (this) { s.stopNow(); session = null; }
				WatchSession n = session(); n.screen(scr); n.hello(hello);
			}
			else { if (scr != null) s.screen(scr); s.hello(hello); }
		}
		if (button != null) s.button(button);
		PebbleDictionaryItem t = data.get(KEY_TOUCH);
		if (t instanceof PebbleDictionaryItem.Bytes) s.touch(((PebbleDictionaryItem.Bytes) t).getValue());
	}

	private static Integer number(PebbleDictionaryItem item) {
		if (item instanceof PebbleDictionaryItem.Int32) return ((PebbleDictionaryItem.Int32) item).getValue();
		return null;
	}

	/** Sends one message of picture commands; done(true) once the watch has taken it. */
	void send(byte[] bytes, Consumer<Boolean> done) {
		Map<Integer, PebbleDictionaryItem> msg = Collections.singletonMap(KEY_DATA, new PebbleDictionaryItem.Bytes(bytes));
		sender.sendDataToPebble(APP, msg, result -> {
			boolean ok = false;
			if (result != null) for (Map.Entry<WatchIdentifier, TransmissionResult> e : result.entrySet())
				if (e.getValue() instanceof TransmissionResult.Success) ok = true;
			done.accept(ok);
		});
	}
}
