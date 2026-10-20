package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.io.*;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.ArrayDeque;
import java.util.List;

/**
 * The game for a watch, on a computer: runs Shattered PD in real time with no screen and serves
 * its picture stream over TCP to tools/bridge.py, which relays it to a Pebble (or the emulator).
 * This is what the Android app will do, minus the phone parts.
 *
 * TCP frames, both ways: u16 length (big-endian), then a type byte and its data.
 *   server -> bridge: 'D' + one AppMessage worth of link commands
 *   bridge -> server: 'A' the watch took the last message, 'H' + u16 size: the watch (re)started,
 *                     'B' + u8 button code, 'T' + 12 bytes: a touch (see Touches)
 */
public class Server {
	static WatchApp app;
	static Link link;
	static Controls controls;
	static final Touches touches = new Touches();
	static final ArrayDeque<byte[]> queue = new ArrayDeque<>();
	static volatile boolean inFlight, connected;
	static volatile int maxMsg = 2000;
	static DataOutputStream outStream;
	static final ArrayDeque<Integer> buttons = new ArrayDeque<>();
	static volatile boolean helloPending;

	public static void main(String[] a) throws Exception {
		int port = a.length > 0 ? Integer.parseInt(a[0]) : 9180;
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));   // the watch's screen (emery, gabbro)
		WatchGame game = new WatchGame();
		game.drawAlways = true;
		app = new WatchApp(game, ".pebxel-server");
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-server/");
		app.create();
		Assets assets = new Assets();
		link = new Link(assets);
		controls = new Controls();
		link.planner = controls::plan;
		if (System.getenv("NEWGAME") != null) {
			for (int i = 0; i < 5; i++) app.frame();
			SPDSettings.intro(false);
			GamesInProgress.selectedClass = HeroClass.values()[Integer.parseInt(System.getenv("NEWGAME"))];
			GamesInProgress.curSlot = 1;
			Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
			if (System.getenv("SEED") != null) Dungeon.seed = Long.parseLong(System.getenv("SEED"));
			InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
			Game.switchScene(InterlevelScene.class);
			// GALLERY=stage: set up one of the visual-check views (see Gallery) once the game has started
			if (System.getenv("GALLERY") != null) Gallery.stage(System.getenv("GALLERY"));
		}

		Thread net = new Thread(() -> serve(port), "bridge");
		net.setDaemon(true); net.start();

		// the game loop: real time, 30 frames a second; the link sends whenever the watch has taken the last message
		long frameNs = 1_000_000_000L / 30, next = System.nanoTime();
		float sinceSync = 0;
		while (!app.exited) {
			Integer b;
			synchronized (buttons) { b = buttons.poll(); }
			if (b != null) controls.button(b);
			touches.frame(link, controls, Game.elapsed);
			app.frame();
			controls.frame();
			Gallery.tick();
			sinceSync += Game.elapsed;
			if (helloPending) { helloPending = false; synchronized (queue) { queue.clear(); } link.reset(); inFlight = false; }
			if (connected) link.frame(sinceSync); sinceSync = 0;
			if (connected && !inFlight && queueEmpty() && link.pending()) {
				List<byte[]> ps = link.take(maxMsg);
				synchronized (queue) { queue.addAll(ps); }
				sendNext();
			}
			next += frameNs;
			long sleep = next - System.nanoTime();
			if (sleep > 0) Thread.sleep(sleep / 1_000_000, (int) (sleep % 1_000_000));
			else next = System.nanoTime();
		}
		System.exit(0);
	}

	static boolean queueEmpty() { synchronized (queue) { return queue.isEmpty(); } }

	static void sendNext() {
		byte[] p;
		synchronized (queue) { p = queue.poll(); }
		if (p == null) { inFlight = false; return; }
		inFlight = true;
		try {
			synchronized (Server.class) {
				outStream.writeShort(p.length + 1); outStream.write('D'); outStream.write(p); outStream.flush();
			}
		} catch (IOException e) { connected = false; inFlight = false; }
	}

	static void serve(int port) {
		try (ServerSocket ss = new ServerSocket(port)) {
			System.out.println("waiting for the bridge on port " + port);
			while (true) {
				try (Socket s = ss.accept()) {
					s.setTcpNoDelay(true);
					DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream()));
					outStream = new DataOutputStream(new BufferedOutputStream(s.getOutputStream()));
					System.out.println("bridge connected");
					helloPending = true; connected = true;   // the watch may hold an old picture: start it over
					while (true) {
						int len = in.readUnsignedShort();
						byte[] f = new byte[len]; in.readFully(f);
						switch (f[0]) {
							case 'A': if (queueEmpty()) inFlight = false; else sendNext(); break;
							case 'H':
								maxMsg = Math.max(200, ((f[1] & 255) << 8 | (f[2] & 255)) - 16);
								int ver = f.length > 3 ? f[3] & 255 : 1;
								if (ver != Link.PROTOCOL) {
									System.out.println("watch app speaks version " + ver + ", phone " + Link.PROTOCOL);
									byte[] e = Link.wrongVersion(ver);
									synchronized (Server.class) { outStream.writeShort(e.length + 1); outStream.write('D'); outStream.write(e); outStream.flush(); }
									break;
								}
								System.out.println("watch says hello, messages up to " + maxMsg + " bytes");
								helloPending = true; connected = true; break;
							case 'B': synchronized (buttons) { buttons.add(f[1] & 255); } break;
							case 'T': touches.add(java.util.Arrays.copyOfRange(f, 1, f.length)); break;
						}
					}
				} catch (IOException e) {
					System.out.println("bridge gone: " + e);
					connected = false; inFlight = false;
				}
			}
		} catch (IOException e) { throw new RuntimeException(e); }
	}
}
