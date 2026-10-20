package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndGame;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndUseItem;
import java.lang.reflect.Field;

/** Pebble PD's windows in place of Shattered's (GameScene.pebxelWindows). */
public final class Swaps {
	/** Set while the hero sheet opens Shattered's own (for its talents page). */
	public static boolean nativeHero;

	public static Window window(Window w) {
		try {
			Window mine = null;
			if (w instanceof WndUseItem) mine = new WndWatchItem(((WndUseItem) w).owner, ((WndUseItem) w).item);
			else if (w instanceof WndGame) mine = new WndWatchMenu();
			else if (w instanceof com.shatteredpixel.shatteredpixeldungeon.windows.WndHero && !nativeHero) mine = new WndWatchHero();
			else if (w instanceof WndBag) mine = new WndWatchBag((Bag) get(WndBag.class, null, "lastBag"), (WndBag.ItemSelector) get(WndBag.class, w, "selector"));
			if (mine == null) { dim(w); return w; }
			w.destroy();   // (it had set up its camera and key listener)
			return mine;
		} catch (Exception e) {
			e.printStackTrace();
			return w;
		}
	}

	/** Shattered's own windows get Pebble PD's shade over the game behind them (its border would run into the HUD's). */
	static void dim(Window w) {
		com.watabou.noosa.ColorBlock d = new com.watabou.noosa.ColorBlock(1, 1, 0xFF000000);
		d.camera = com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.uiCamera;
		d.size(com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.uiCamera.width, com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.uiCamera.height);
		d.alpha(0.5f);
		com.watabou.noosa.Gizmo first = com.watabou.noosa.GroupAccess.members(w).isEmpty() ? null : com.watabou.noosa.GroupAccess.members(w).get(0);
		if (first != null) com.watabou.noosa.GroupAccess.addAfter(w, d, first); else w.add(d);
	}

	/** Pebble PD's screens in place of Shattered's (Game.pebxelScenes). */
	public static Class<? extends com.watabou.noosa.Scene> scene(Class<? extends com.watabou.noosa.Scene> c) {
		if (c == com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene.class) return WatchTitleScene.class;
		if (c == com.shatteredpixel.shatteredpixeldungeon.scenes.WelcomeScene.class) return WatchWelcome.class;
		if (c == com.shatteredpixel.shatteredpixeldungeon.scenes.HeroSelectScene.class) return WatchHeroSelect.class;
		return c;
	}

	private static Object get(Class<?> c, Object o, String name) throws Exception {
		Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o);
	}
}
