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
 *   bridge -> server: 'A' the watch took the last message, 'N' it didn't (refused, lost: Pacer sends it again),
 *                     'H' + u16 size: the watch (re)started,
 *                     'B' + u8 button code, 'T' + 12 bytes: a touch (see Touches)
 */
public class Server {
	static WatchApp app;
	static Link link;
	static Controls controls;
	static final Touches touches = new Touches();
	static Pacer pacer;
	static volatile boolean connected;
	static DataOutputStream outStream;
	static final ArrayDeque<Integer> buttons = new ArrayDeque<>();

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
		pacer = new Pacer(link, Server::transmit);
		pacer.hello(2016);
		if (System.getenv("ASSIST") != null) WatchSettings.assist(Integer.parseInt(System.getenv("ASSIST")));   // (tests: the Assistant's reach, 0 off 1 near 2 far)
		Controls.log = System.getenv("PLANLOG") != null;
		if (System.getenv("ITEMS") != null) WatchSettings.itemAssist(!"0".equals(System.getenv("ITEMS")));
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
		while (!app.exited) {
			Integer b;
			synchronized (buttons) { b = buttons.poll(); }
			if (b != null) controls.button(b);
			touches.frame(link, controls, Game.elapsed);
			app.frame();
			controls.frame();
			Gallery.tick();
			stairsTick();
			pacer.frame(Game.elapsed, connected);
			if (++frames % 150 == 0 && connected)
				System.out.printf("link: %.0f bytes/s, %.1f messages/s, radio %.0f bytes/s, %d frames merged, %d sent again, %d whole screens%n",
						(pacer.bytes - lastBytes) / 5.0, (pacer.msgs - lastMsgs) / 5.0, pacer.rate(), pacer.merged, pacer.retries, pacer.resets);
			if (frames % 150 == 0 && connected && System.getenv("STATS") != null && pacer.bytes - lastBytes > 500) {
				// STATS=1: what took the bytes these 5 s
				StringBuilder w = new StringBuilder("  most:");
				link.stats.entrySet().stream().sorted((x, y) -> Long.compare(y.getValue()[1], x.getValue()[1])).limit(5)
						.forEach(e -> w.append(" ").append(e.getKey()).append(" ").append(e.getValue()[1]));
				System.out.println(w);
			}
			if (frames % 150 == 0) { lastBytes = pacer.bytes; lastMsgs = pacer.msgs; link.stats.clear(); }
			next += frameNs;
			long sleep = next - System.nanoTime();
			if (sleep > 0) Thread.sleep(sleep / 1_000_000, (int) (sleep % 1_000_000));
			else next = System.nanoTime();
		}
		System.exit(0);
	}

	static long frames, lastBytes, lastMsgs;
	// STAIRS=pattern (u/d letters, repeated) and STAIRS_EVERY=seconds (default 8): take the stairs by themselves, for
	// testing floor changes on the emulator (see StairsTest)
	static final String STAIRS = System.getenv("STAIRS");
	static int stairsNo; static long stairsAt;
	static long storyAt, wndAt;
	static void stairsTick() {
		if (STAIRS == null) return;
		long t = System.currentTimeMillis();
		if (Game.scene() instanceof InterlevelScene) {
			// on through the story pages, a little slower than a reader
			if (storyAt == 0) storyAt = t;
			if (t - storyAt > 2500) { storyAt = t; for (Ui.Part p : Ui.parts(Game.scene())) if (p.button && !p.text.isEmpty() && !p.text.equals("Button")) { Ui.press(p.g); break; } }
		} else storyAt = 0;
		if (!(Game.scene() instanceof com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene)) { stairsAt = t; return; }
		if (Ui.topWindow() != null) { if (wndAt == 0) wndAt = t; if (t - wndAt > 3000) { Ui.topWindow().hide(); wndAt = 0; } return; }
		wndAt = 0;
		if (Dungeon.hero == null || !Dungeon.hero.ready) return;
		long every = (long) (1000 * Float.parseFloat(System.getenv().getOrDefault("STAIRS_EVERY", "8")));
		if (System.currentTimeMillis() - stairsAt < every) return;
		stairsAt = t;
		boolean up = STAIRS.charAt(stairsNo++ % STAIRS.length()) == 'u';
		com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(up);
		if (tr == null || tr.type == com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition.Type.SURFACE) return;
		System.out.println("stairs " + stairsNo + ": " + (up ? "up" : "down") + " from depth " + Dungeon.depth);
		StairsTest.jump(tr);
	}

	/** One message to the bridge (its answer comes back as 'A' or 'N'). */
	static void transmit(byte[] p) {
		try {
			synchronized (Server.class) {
				outStream.writeShort(p.length + 1); outStream.write('D'); outStream.write(p); outStream.flush();
			}
		} catch (Exception e) { connected = false; }
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
					pacer.hello(2016); connected = true;   // the watch may hold an old picture: start it over
					while (true) {
						int len = in.readUnsignedShort();
						byte[] f = new byte[len]; in.readFully(f);
						switch (f[0]) {
							case 'A': pacer.done(true); break;
							case 'N': pacer.done(false); break;
							case 'H':
								int size = (f[1] & 255) << 8 | (f[2] & 255);
								int ver = f.length > 3 ? f[3] & 255 : 1;
								if (ver != Link.PROTOCOL) {
									System.out.println("watch app speaks version " + ver + ", phone " + Link.PROTOCOL);
									byte[] e = Link.wrongVersion(ver);
									synchronized (Server.class) { outStream.writeShort(e.length + 1); outStream.write('D'); outStream.write(e); outStream.flush(); }
									break;
								}
								System.out.println("watch says hello, messages up to " + size + " bytes");
								pacer.hello(size); connected = true; break;
							case 'B': synchronized (buttons) { buttons.add(f[1] & 255); } break;
							case 'T': touches.add(java.util.Arrays.copyOfRange(f, 1, f.length)); break;
						}
					}
				} catch (IOException e) {
					System.out.println("bridge gone: " + e);
					connected = false;
				}
			}
		} catch (IOException e) { throw new RuntimeException(e); }
	}
}
