package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.util.*;

/**
 * What the watch menu (WndWatchMenu) costs the link: bytes and messages for opening it and for each change of
 * tab (by Up/Down past a page's end, and by taps on the tabs), how many pictures the watch holds with it open, and
 * how much it sends per second while it just sits open. Args: switches (default 30).  SCREEN=gabbro|emery
 */
public class MenuTest {
	public static void main(String[] a) throws Exception {
		int switches = a.length > 0 ? Integer.parseInt(a[0]) : 30;
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		WatchApp app = new WatchApp(game, ".pebxel-menutest", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-menutest/");
		app.create();
		app.graphics.soft.raster = false;
		Link link = new Link(new Assets());
		Controls controls = new Controls();
		link.planner = controls::plan;
		Touches touches = new Touches();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		long[] tot = {0, 0, 0};   // bytes, messages, biggest message
		int max = 8000 - 16;
		Runnable frame = () -> {
			touches.frame(link, controls, Game.elapsed);
			app.frame(); controls.frame();
			link.frame(Game.elapsed);
			for (byte[] m : link.take(max)) { tot[0] += m.length; tot[1]++; tot[2] = Math.max(tot[2], m.length); }
		};
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			frame.run(); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
		}
		for (int f = 0; f < 120; f++) frame.run();
		int seq = 0;
		System.out.println("dungeon: " + link.truth().size() + " pictures");
		Runnable reset = () -> { tot[0] = tot[1] = tot[2] = 0; link.stats.clear(); };
		reset.run();
		controls.button(Controls.UP | Controls.LONG);
		for (int f = 0; f < 30; f++) frame.run();
		System.out.println("open menu: " + tot[0] + " B in " + tot[1] + " msgs; " + link.truth().size() + " pictures; window " + Ui.topWindow());
		reset.run();
		for (int f = 0; f < 150; f++) frame.run();
		System.out.println("menu open, idle 5 s: " + tot[0] + " B in " + tot[1] + " msgs");
		top(link);
		int[] per = new int[switches];
		int[] tabX = Screen.round ? new int[]{79, 130, 181} : new int[]{36, 98, 160};
		reset.run();
		for (int s = 0; s < switches; s++) {
			long b0 = tot[0];
			if (s % 2 == 0) controls.button(Controls.DOWN);
			else {
				int k = (s / 2) % 3, x = tabX[k], y = Screen.round ? 210 : 190;
				byte[] t = {1, (byte) ++seq, (byte) 0xFF, 0, (byte) x, (byte) (x >> 8), (byte) y, (byte) (y >> 8), 0, 0, 0, 0};
				touches.add(t);
			}
			for (int f = 0; f < 5; f++) frame.run();
			per[s] = (int) (tot[0] - b0);
		}
		System.out.println(switches + " switches: " + tot[0] + " B in " + tot[1] + " msgs (biggest " + tot[2] + "), per switch " + Arrays.toString(per) + "; " + link.truth().size() + " pictures");
		top(link);
		System.out.println("window " + Ui.topWindow() + ", touch listeners " + listeners() + ", full-screen shades " + shades());
		controls.button(Controls.BACK);
		for (int f = 0; f < 30; f++) frame.run();
		System.out.println("menu closed: window " + Ui.topWindow() + ", touch listeners " + listeners() + ", " + link.truth().size() + " pictures");
		// the other watch windows: open, Up/Down through them, a tap on each tab, close
		java.util.Set<Object> seen = java.util.Collections.newSetFromMap(new java.util.IdentityHashMap<>()); seen.addAll(listenerList());
		String[] names = {"bag", "hero", "catalogue", "journal", "item"};
		for (String name : names) {
			int l0 = listeners();
			reset.run();
			com.shatteredpixel.shatteredpixeldungeon.ui.Window w =
					name.equals("bag") ? new com.shatteredpixel.shatteredpixeldungeon.windows.WndBag(Dungeon.hero.belongings.backpack)
					: name.equals("hero") ? new com.shatteredpixel.shatteredpixeldungeon.windows.WndHero()
					: name.equals("catalogue") ? pebxel.ui.WndWatchRecords.catalogue() : name.equals("journal") ? pebxel.ui.WndWatchRecords.journal()
					: new com.shatteredpixel.shatteredpixeldungeon.windows.WndUseItem(null, Dungeon.hero.belongings.backpack.items.get(0));
			GameScene.show(w);
			for (int f = 0; f < 30; f++) frame.run();
			com.shatteredpixel.shatteredpixeldungeon.ui.Window top = Ui.topWindow();
			System.out.printf("%s (%s): open %d B in %d msgs, %d pictures, full-screen shades %d%n", name, top == null ? "-" : top.getClass().getSimpleName(), tot[0], tot[1], link.truth().size(), shades());
			reset.run();
			for (int k = 0; k < 6; k++) { controls.button(k < 3 ? Controls.DOWN : Controls.UP); for (int f = 0; f < 5; f++) frame.run(); }
			int y = Screen.round ? 225 : 205;
			for (int x : new int[]{30, 90, 150, 30}) {
				byte[] t = {1, (byte) ++seq, (byte) 0xFF, 0, (byte) x, (byte) (x >> 8), (byte) y, (byte) (y >> 8), 0, 0, 0, 0};
				touches.add(t); for (int f = 0; f < 5; f++) frame.run();
			}
			top = Ui.topWindow();
			System.out.printf("   moves and taps: %d B in %d msgs (biggest %d), %d pictures, top %s%n", tot[0], tot[1], tot[2], link.truth().size(), top == null ? "-" : top.getClass().getSimpleName());
			for (int k = 0; k < 4 && Ui.topWindow() != null; k++) { controls.button(Controls.BACK); for (int f = 0; f < 15; f++) frame.run(); }
			System.out.printf("   closed: touch listeners %d (before %d)%n", listeners(), l0);
			if (System.getenv("LEAKS") != null) for (Object o : listenerList()) if (!seen.contains(o)) System.out.println("   new listener " + o.getClass().getName() + (o instanceof com.watabou.noosa.Gizmo && ((com.watabou.noosa.Gizmo) o).parent != null ? " in " + ((com.watabou.noosa.Gizmo) o).parent.getClass().getName() : ""));
			seen.addAll(listenerList());
		}
		System.exit(0);
	}

	static java.util.List<Object> listenerList() {
		try {
			java.lang.reflect.Field f = com.watabou.input.PointerEvent.class.getDeclaredField("pointerSignal"); f.setAccessible(true);
			Object sig = f.get(null);
			java.lang.reflect.Field l = com.watabou.utils.Signal.class.getDeclaredField("listeners"); l.setAccessible(true);
			return new java.util.ArrayList<>((java.util.Collection<?>) l.get(sig));
		} catch (Exception e) { return new java.util.ArrayList<>(); }
	}

	static int listeners() {
		try {
			java.lang.reflect.Field f = com.watabou.input.PointerEvent.class.getDeclaredField("pointerSignal"); f.setAccessible(true);
			Object sig = f.get(null);
			java.lang.reflect.Field l = com.watabou.utils.Signal.class.getDeclaredField("listeners"); l.setAccessible(true);
			return ((java.util.Collection<?>) l.get(sig)).size();
		} catch (Exception e) { return -1; }
	}

	/** Full-screen see-through shades in the top window (Pebble PD's windows have one). */
	static int shades() {
		com.shatteredpixel.shatteredpixeldungeon.ui.Window w = Ui.topWindow();
		if (w == null) return 0;
		int n = 0;
		for (com.watabou.noosa.Gizmo g : com.watabou.noosa.GroupAccess.members(w))
			if (g instanceof com.watabou.noosa.ColorBlock && ((com.watabou.noosa.ColorBlock) g).width() >= com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.uiCamera.width - 1 && ((com.watabou.noosa.ColorBlock) g).alpha() < 0.9f) n++;
		return n;
	}

	static void top(Link link) {
		TreeMap<Long, String> top = new TreeMap<>();
		for (Map.Entry<String, long[]> e : link.stats.entrySet()) top.put(-e.getValue()[1] * 1000 - top.size(), e.getKey() + " x" + e.getValue()[0] + " = " + e.getValue()[1] + " B");
		int n = 0; for (String s : top.values()) { System.out.println("  " + s); if (++n == 12) break; }
	}
}
