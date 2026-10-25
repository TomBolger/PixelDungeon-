package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.*;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.ui.Component;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.Supplier;

/**
 * Every kind of window, with long content, against the screen (Thomas: "several menus open taller than the whole
 * screen, giving them no top and no bottom"): one that fits lies inside the view; a taller one (it scrolls) keeps its
 * frame's top and bottom at the view's ends (pebxel.ui.Caps), at the top of its scroll, the foot and between, and a
 * tabbed one its tabs in sight. Called by RefinementTest (Emery, and SCREEN=gabbro). Prints a line per window.
 */
public final class WindowFit {
	static final String LONG = "The Dungeon lies right beneath the City, its upper levels actually constitute the City's sewer system. "
			+ "As dark energy has crept up from below the usually harmless sewer creatures have become more and more dangerous. "
			+ "The city sends guard patrols down here to try and maintain safety for those above, but they are slowly failing. "
			+ "This place is dangerous, but at least the evil magic at work here is weak, and the _treasures_ are many. "
			+ "Many adventurers have come before you; few came back, and none of them spoke of the deepest floors.";

	public static void check(Runnable frame, Controls oc) {
		Item weapon = Dungeon.hero.belongings.weapon;
		com.shatteredpixel.shatteredpixeldungeon.items.artifacts.TimekeepersHourglass longItem = new com.shatteredpixel.shatteredpixeldungeon.items.artifacts.TimekeepersHourglass();
		longItem.identify();
		LinkedHashMap<String, Supplier<Window>> wins = new LinkedHashMap<>();
		wins.put("bag", () -> new WndBag(Dungeon.hero.belongings.backpack));
		wins.put("item (long)", () -> new WndUseItem(null, longItem));
		wins.put("item info (long)", () -> new WndInfoItem(longItem));
		wins.put("hero sheet", WndHero::new);
		wins.put("talents", () -> { WndHero.lastIdx = 1; pebxel.ui.Swaps.nativeHero = true; return new WndHero(); });
		wins.put("menu", WndGame::new);
		wins.put("catalogue", pebxel.ui.WndWatchRecords::catalogue);
		wins.put("journal", pebxel.ui.WndWatchRecords::journal);
		wins.put("badges", () -> { WndJournal.last_index = 4; return new WndJournal(); });
		wins.put("guide", () -> { WndJournal.last_index = 1; return new WndJournal(); });
		wins.put("settings", WndSettings::new);
		wins.put("challenges", () -> new WndChallenges(0, false));
		wins.put("hero info", () -> new WndHeroInfo(HeroClass.WARRIOR));
		wins.put("game in progress", () -> new WndGameInProgress(1));
		wins.put("story (long)", () -> new WndStory(LONG));
		wins.put("message (long)", () -> new WndMessage(LONG));
		wins.put("titled (long)", () -> new WndTitledMessage(com.shatteredpixel.shatteredpixeldungeon.ui.Icons.get(com.shatteredpixel.shatteredpixeldungeon.ui.Icons.JOURNAL), "A long title for a window", LONG + " " + LONG));
		wins.put("options (many)", () -> new WndOptions("A question", LONG, "One", "Two", "Three", "Four", "Five", "Six"));
		wins.put("mob", () -> new WndInfoMob(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Goo()));
		wins.put("quest", () -> new WndQuest(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Ghost(), LONG));
		wins.put("ghost reward", () -> new WndSadGhost(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Ghost(), 1));
		wins.put("wandmaker", () -> new WndWandmaker(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Wandmaker(), new com.shatteredpixel.shatteredpixeldungeon.items.quest.CorpseDust()));
		wins.put("blacksmith", () -> new WndBlacksmith(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Blacksmith(), Dungeon.hero));
		wins.put("shop item", () -> new WndTradeItem(longItem, null));
		int tall = 0;
		for (Map.Entry<String, Supplier<Window>> e : wins.entrySet()) {
			while (Ui.topWindow() != null) { Ui.topWindow().hide(); frame.run(); }
			Window made;
			try { made = e.getValue().get(); } catch (Throwable t) { System.out.println("  fit: " + e.getKey() + ": not made here (" + t + ")"); continue; }
			GameScene.show(made);
			pebxel.ui.Swaps.nativeHero = false;
			for (int i = 0; i < 6; i++) frame.run();
			Window w = Ui.topWindow();
			check(w != null, e.getKey() + ": shown");
			String name = e.getKey() + " (" + w.getClass().getSimpleName() + ")";
			if (w instanceof pebxel.ui.WatchWindow && ((pebxel.ui.WatchWindow) w).fixed) { System.out.println("  fit: " + name + ": fills the screen by design"); continue; }
			Camera c = w.camera;
			int vt = Controls.viewTop(w), vb = Controls.viewBottom(w);
			if (!Controls.tall(w)) {
				check(c.y >= vt - 2 && c.y + c.screenHeight() <= vb + 2, name + ": fits, inside the view (" + c.y + ".." + (c.y + c.screenHeight()) + " of " + vt + ".." + vb + ")");
				System.out.println("  fit: " + name + ": fits (" + (int) (c.screenHeight() / 2) + " px tall)");
				continue;
			}
			tall++;
			for (int end : new int[]{100000, -60, -100000}) {   // its top, a little down, its foot
				Controls.scroll(w, end); frame.run(); frame.run();
				pebxel.ui.Caps caps = null;
				for (Gizmo g : GroupAccess.members(w)) if (g instanceof pebxel.ui.Caps) caps = (pebxel.ui.Caps) g;
				if (Screen.round && w instanceof pebxel.ui.WatchWindow) { check(false, name + ": a tall window of ours on a round screen has no caps"); }
				check(caps != null && caps.visible, name + ": tall, its frame's ends held on the screen");
				float[] ends = caps.ends();   // (phone px)
				check(Math.abs(ends[0] - vt) <= c.zoom && Math.abs(ends[1] - vb) <= c.zoom, name + ": caps at the view's ends (" + ends[0] + ".." + ends[1] + " of " + vt + ".." + vb + ")");
				if (w instanceof WndTabbed) for (Component t : tabs(w)) {
					float ty = c.y + (t.top() - c.scroll.y) * c.zoom, tb = c.y + (t.bottom() - c.scroll.y) * c.zoom;
					check(ty >= vt && tb <= vb + 1, name + ": its tabs in sight (" + ty + ".." + tb + ")");
				}
			}
			System.out.println("  fit: " + name + ": " + (int) (c.screenHeight() / 2) + " px tall, scrolls inside its frame");
		}
		while (Ui.topWindow() != null) { Ui.topWindow().hide(); frame.run(); }
		System.out.println("windows fit the screen or scroll inside a whole frame: ok (" + wins.size() + " kinds, " + tall + " tall, " + Screen.w + "px)");
	}

	@SuppressWarnings("unchecked")
	static java.util.List<Component> tabs(Window w) {
		try { java.lang.reflect.Field f = WndTabbed.class.getDeclaredField("tabs"); f.setAccessible(true); return (java.util.List<Component>) f.get(w); }
		catch (Exception e) { throw new RuntimeException(e); }
	}

	static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }
}
