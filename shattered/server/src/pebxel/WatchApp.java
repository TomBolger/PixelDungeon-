package pebxel;

import com.badlogic.gdx.*;
import com.badlogic.gdx.backends.headless.*;
import com.badlogic.gdx.backends.headless.mock.audio.MockAudio;
import com.badlogic.gdx.backends.headless.mock.input.MockInput;
import com.badlogic.gdx.utils.Array;
import com.badlogic.gdx.utils.Clipboard;

/** A libGDX "application" with no window: we call frame() ourselves, as fast or slow as we like. */
public class WatchApp implements Application {
	public final ApplicationListener listener;
	public final WatchGraphics graphics = new WatchGraphics();
	private final Array<Runnable> runnables = new Array<>(), running = new Array<>();
	private final java.util.HashMap<String, Preferences> prefs = new java.util.HashMap<>();
	private final String prefDir;
	public volatile boolean exited;

	public WatchApp(ApplicationListener l, String prefDir) { this(l, prefDir, false); }

	public WatchApp(ApplicationListener l, String prefDir, boolean pictures) {
		listener = l; this.prefDir = prefDir;
		if (pictures) graphics.useSoftGL();
		HeadlessNativesLoader.load();
		Gdx.app = this; Gdx.files = new HeadlessFiles(); Gdx.net = new HeadlessNet(new HeadlessApplicationConfiguration());
		Gdx.audio = new MockAudio(); Gdx.input = new MockInput();
		Gdx.graphics = graphics; Gdx.gl = Gdx.gl20 = graphics.getGL20();
	}

	public void create() { listener.create(); listener.resize(graphics.w, graphics.h); }

	public void frame() {
		synchronized (runnables) { running.clear(); running.addAll(runnables); runnables.clear(); }
		for (Runnable r : running) r.run();
		listener.render();
	}

	@Override public ApplicationListener getApplicationListener() { return listener; }
	@Override public Graphics getGraphics() { return graphics; }
	@Override public Audio getAudio() { return Gdx.audio; }
	@Override public Input getInput() { return Gdx.input; }
	@Override public Files getFiles() { return Gdx.files; }
	@Override public Net getNet() { return Gdx.net; }
	@Override public ApplicationType getType() { return ApplicationType.HeadlessDesktop; }
	@Override public int getVersion() { return 0; }
	@Override public long getJavaHeap() { return Runtime.getRuntime().totalMemory(); }
	@Override public long getNativeHeap() { return 0; }
	@Override public Preferences getPreferences(String name) {
		return prefs.computeIfAbsent(name, n -> new HeadlessPreferences(n, prefDir));
	}
	private String clip = "";
	@Override public Clipboard getClipboard() { return new Clipboard() {
		public boolean hasContents() { return !clip.isEmpty(); }
		public String getContents() { return clip; }
		public void setContents(String s) { clip = s; } }; }
	@Override public void postRunnable(Runnable r) { synchronized (runnables) { runnables.add(r); } }
	@Override public void debug(String t, String m) { }
	@Override public void debug(String t, String m, Throwable e) { }
	@Override public void log(String t, String m) { System.out.println("[" + t + "] " + m); }
	@Override public void log(String t, String m, Throwable e) { log(t, m); e.printStackTrace(System.out); }
	@Override public void error(String t, String m) { System.err.println("[" + t + "] " + m); }
	@Override public void error(String t, String m, Throwable e) { error(t, m); e.printStackTrace(); }
	@Override public void setLogLevel(int l) { }
	@Override public int getLogLevel() { return LOG_INFO; }
	@Override public void setApplicationLogger(ApplicationLogger l) { }
	@Override public ApplicationLogger getApplicationLogger() { return null; }
	@Override public void exit() { exited = true; }
	@Override public void addLifecycleListener(LifecycleListener l) { }
	@Override public void removeLifecycleListener(LifecycleListener l) { }
}
