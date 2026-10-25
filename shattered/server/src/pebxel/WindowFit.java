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
			String shots = System.getenv("WINSHOTS");   // (a folder: a picture of each window, at the top and foot of its scroll)
			String file = e.getKey().replaceAll("[^a-z]+", "_");
			if (shots != null) try { RefinementTest.pic(shots, file); } catch (Exception x) { throw new RuntimeException(x); }
			edges(w, name, oc);
			if (!Controls.tall(w)) {
				check(pebxel.ui.Caps.clip(c) == null, name + ": fits, no caps");
				check(c.y >= vt - 2 && c.y + c.screenHeight() <= vb + 2, name + ": fits, inside the view (" + c.y + ".." + (c.y + c.screenHeight()) + " of " + vt + ".." + vb + ")");
				System.out.println("  fit: " + name + ": fits (" + (int) (c.screenHeight() / 2) + " px tall)");
				continue;
			}
			tall++;
			for (int end : new int[]{100000, -60, -100000}) {   // its top, a little down, its foot
				Controls.scroll(w, end); frame.run(); frame.run();
				if (shots != null && end == -100000) try { RefinementTest.pic(shots, file + "_foot"); } catch (Exception x) { throw new RuntimeException(x); }
				pebxel.ui.Caps caps = null;
				for (Gizmo g : GroupAccess.members(w)) if (g instanceof pebxel.ui.Caps) caps = (pebxel.ui.Caps) g;
				if (Screen.round && w instanceof pebxel.ui.WatchWindow) { check(false, name + ": a tall window of ours on a round screen has no caps"); }
				check(caps != null && caps.visible, name + ": tall, its frame's ends held on the screen");
				float[] ends = caps.ends();   // (phone px)
				check(Math.abs(ends[0] - vt) <= c.zoom && Math.abs(ends[1] - vb) <= c.zoom, name + ": caps at the view's ends (" + ends[0] + ".." + ends[1] + " of " + vt + ".." + vb + ")");
				edges(w, name + " scrolled " + end, oc);
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

	/**
	 * Nothing pressable, nor the yellow box round the framed button, is drawn over the window's frame (Thomas: "Continue
	 * and Delete and the exit button are all intersecting with the border, as is the highlight box"): each one as drawn
	 * (cut to where its camera draws: a tall window's between its frame's held ends) clear of the frame's edges.
	 */
	static void edges(Window w, String name, Controls oc) {
		float[] in = Controls.innerRect(w);   // (watch px)
		if (in == null) return;   // (a round screen's own windows: the frame is drawn by shapes)
		float l = in[0], t = in[1], r = in[2], b = in[3], x1 = in[4], x0 = in[5], y1 = in[6], y0 = in[7];
		float[][] border = {{x0, y0, x1, t}, {x0, b, x1, y1}, {x0, t, l, b}, {r, t, x1, b}};
		java.util.List<Object[]> drawn = new java.util.ArrayList<>();
		for (Ui.Part p : Controls.buttons(w)) drawn.add(new Object[]{"button " + label(p.g), p.g, null});
		for (Gizmo g : GroupAccess.members(w)) if (g instanceof Controls.CloseTag && g.visible) drawn.add(new Object[]{"exit tag", g, null});
		Object[] hl = oc.highlight();
		if (hl != null && hl[0] == w) drawn.add(new Object[]{"highlight box", null, hl[1]});
		Camera c = w.camera;
		int[] cut = pebxel.ui.Caps.clip(c);
		float ct = cut != null ? cut[0] / 2f : -1e9f, cb = cut != null ? cut[1] / 2f : 1e9f;
		java.util.List<Component> tabs = w instanceof WndTabbed ? tabs(w) : java.util.Collections.emptyList();
		for (Object[] d : drawn) {
			float[] q;
			if (d[1] != null) { Component k = (Component) d[1]; if (tabs.contains(k)) continue; q = new float[]{k.left(), k.top(), k.width(), k.height()}; }
			else q = (float[]) d[2];
			float qx0 = (c.x + (q[0] - c.scroll.x) * c.zoom) / 2, qy0 = (c.y + (q[1] - c.scroll.y) * c.zoom) / 2;
			float qx1 = qx0 + q[2] * c.zoom / 2, qy1 = qy0 + q[3] * c.zoom / 2;
			qy0 = Math.max(qy0, ct); qy1 = Math.min(qy1, cb);
			if (qy1 - qy0 < 0.5f || qx1 - qx0 < 0.5f) continue;   // (scrolled out of sight)
			if (d[1] != null && qy0 >= b - 2 && qy1 > y1) continue;   // (a tab hanging under the frame, joined to it by design)
			for (float[] e : border) {
				boolean hit = Math.min(qx1, e[2]) - Math.max(qx0, e[0]) > 0.5f && Math.min(qy1, e[3]) - Math.max(qy0, e[1]) > 0.5f;
				check(!hit, name + ": the " + d[0] + " (" + qx0 + "," + qy0 + ".." + qx1 + "," + qy1 + ") over the frame's edge (" + e[0] + "," + e[1] + ".." + e[2] + "," + e[3] + ")");
			}
		}
	}

	static String label(Gizmo g) {
		for (Gizmo m : GroupAccess.members((com.watabou.noosa.Group) g)) if (m instanceof com.shatteredpixel.shatteredpixeldungeon.ui.RenderedTextBlock) return "'" + ((com.shatteredpixel.shatteredpixeldungeon.ui.RenderedTextBlock) m).text() + "'";
		return g.getClass().getSimpleName();
	}

	@SuppressWarnings("unchecked")
	static java.util.List<Component> tabs(Window w) {
		try { java.lang.reflect.Field f = WndTabbed.class.getDeclaredField("tabs"); f.setAccessible(true); return (java.util.List<Component>) f.get(w); }
		catch (Exception e) { throw new RuntimeException(e); }
	}

	static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }
}
