package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndGame;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndUseItem;
import java.lang.reflect.Field;

/** Pebble PD's windows in place of Shattered's (GameScene.pebxelWindows). */
public final class Swaps {
	public static Window window(Window w) {
		try {
			Window mine = null;
			if (w instanceof WndUseItem) mine = new WndWatchItem(((WndUseItem) w).owner, ((WndUseItem) w).item);
			else if (w instanceof WndGame) mine = new WndWatchMenu();
			else if (w instanceof WndBag) mine = new WndWatchBag((Bag) get(WndBag.class, null, "lastBag"), (WndBag.ItemSelector) get(WndBag.class, w, "selector"));
			if (mine == null) return w;
			w.destroy();   // (it had set up its camera and key listener)
			return mine;
		} catch (Exception e) {
			e.printStackTrace();
			return w;
		}
	}

	private static Object get(Class<?> c, Object o, String name) throws Exception {
		Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o);
	}
}
