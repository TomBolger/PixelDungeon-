package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import java.util.ArrayList;

/**
 * Pebble PD's item window: the item at 2x and its name (Gothic 14 bold, pale yellow, up to two lines), the
 * description in Gothic 14 on black, then the actions as big red buttons, two to a row (an odd last one full
 * width). Shattered's WndUseItem does the actions.
 */
public class WndWatchItem extends WatchWindow {
	static final int W = 179, BTN_H = 34, GAP = 2;

	/** "DROP" -> "Drop" (Pebble PD's labels). */
	static String capital(String s) {
		if (s == null || s.isEmpty()) return "";
		if (!s.equals(s.toUpperCase())) return s;
		String l = s.toLowerCase();
		return Character.toUpperCase(l.charAt(0)) + l.substring(1);
	}

	public WndWatchItem(final Window owner, final Item item) {
		super(WatchWindow.left(W), 4, W, 100, false);
		// the head: the item, its name
		ItemSprite spr = new ItemSprite(item); spr.scale.set(2); spr.x = 0; spr.y = 0; add(spr);
		String name = Messages.titleCase(item.title());
		int color = item.levelKnown && item.level() > 0 ? 0x55FF55 : item.levelKnown && item.level() < 0 ? 0xFF5555 : PASTEL;
		java.util.List<String> nl = WatchHud.wrap(name, WatchText.G14B, W - 36);
		int y = 0;
		for (int i = 0; i < Math.min(2, nl.size()); i++) { text(nl.get(i), WatchText.G14B, color, 36, y + 2, 0, 0); y += lineHeight(WatchText.G14B); }
		int head = Math.max(36, y + 4);
		// the description, on black
		String info = item.info();
		int th = textHeight(info, WatchText.G14, W - 4);
		well(0, head, W, th + 4);
		textBlock(this, info, WatchText.G14, WHITE, 2, head + 2, W - 4);
		y = head + th + 4 + GAP;
		// the actions
		ArrayList<WatchButton> bs = new ArrayList<>();
		if (Dungeon.hero.isAlive() && Dungeon.hero.belongings.contains(item)) {
			for (final String action : item.actions(Dungeon.hero)) {
				WatchButton b = new WatchButton(capital(item.actionName(action, Dungeon.hero))) {
					@Override protected void onClick() {
						hide();
						if (owner != null && owner.parent != null) owner.hide();
						if (Dungeon.hero.isAlive() && Dungeon.hero.belongings.contains(item)) item.execute(Dungeon.hero, action);
						Item.updateQuickslot();
					}
				};
				if (action.equals(item.defaultAction())) b.labelColor = ICTERINE;
				bs.add(b);
			}
		}
		for (int i = 0; i < bs.size(); i++) {
			WatchButton b = bs.get(i);
			boolean full = i == bs.size() - 1 && i % 2 == 0;
			int bw = full ? W : (W - GAP) / 2;
			b.setRect(i % 2 == 0 ? 0 : (W - GAP) / 2 + GAP + 1, y, full ? W : bw, BTN_H);
			b.label(b.label);
			add(b);
			if (i % 2 == 1 || full) y += BTN_H + GAP;
		}
		resize(W, y);
		place(WatchWindow.left(W), Math.max(4, (WatchHud.H - y) / 2));
	}
}
