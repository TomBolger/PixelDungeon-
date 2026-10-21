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
		// already one of ours (the menu's Catalogue and Journal, another bag): it has its shade. (dim() gave it a
		// second full-screen one: no darker on the watch's dither, just drawn twice every frame.)
		if (w instanceof WatchWindow) return w;
		try {
			Window mine = null;
			if (w instanceof WndUseItem) mine = new WndWatchItem(((WndUseItem) w).owner, ((WndUseItem) w).item);
			else if (w instanceof WndGame) mine = new WndWatchMenu();
			else if (w instanceof com.shatteredpixel.shatteredpixeldungeon.windows.WndHero && !nativeHero) mine = new WndWatchHero();
			else if (w instanceof WndBag) { WndBag.ItemSelector sel = (WndBag.ItemSelector) get(WndBag.class, w, "selector"); mine = new WndWatchBag(startBag((Bag) get(WndBag.class, null, "lastBag"), sel), sel); ((WndBag) w).pebxelSwap = mine; }
			if (mine == null) { dim(w); return w; }
			w.destroy();   // (it had set up its camera and key listener)
			return mine;
		} catch (Exception e) {
			e.printStackTrace();
			return w;
		}
	}

	/** The bag a choosing opens on: the one Shattered asked for, but when nothing in it (nor worn) can be chosen and
	 *  something in another bag can, that bag (the alchemy pot's seeds are in the pouch: four presses saved). */
	static Bag startBag(Bag asked, WndBag.ItemSelector sel) {
		com.shatteredpixel.shatteredpixeldungeon.actors.hero.Belongings b = com.shatteredpixel.shatteredpixeldungeon.Dungeon.hero.belongings;
		Bag bag = asked == null ? b.backpack : asked;
		if (sel == null || bag != b.backpack) return bag;
		try {
			for (com.shatteredpixel.shatteredpixeldungeon.items.Item i : new com.shatteredpixel.shatteredpixeldungeon.items.Item[]{b.weapon, b.armor, b.artifact, b.misc, b.ring, b.secondWep})
				if (i != null && sel.itemSelectable(i)) return bag;
			for (com.shatteredpixel.shatteredpixeldungeon.items.Item i : bag.items) if (!(i instanceof Bag) && sel.itemSelectable(i)) return bag;
			for (com.shatteredpixel.shatteredpixeldungeon.items.Item i : bag.items)
				if (i instanceof Bag) for (com.shatteredpixel.shatteredpixeldungeon.items.Item j : ((Bag) i).items) if (sel.itemSelectable(j)) return (Bag) i;
		} catch (Exception e) { }
		return bag;
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
		if (c == com.shatteredpixel.shatteredpixeldungeon.scenes.RankingsScene.class) return WatchRankings.class;
		return c;
	}

	private static Object get(Class<?> c, Object o, String name) throws Exception {
		Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o);
	}
}
