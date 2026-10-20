package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Group;
import java.util.ArrayList;
import java.util.List;

/**
 * Pebble PD's item window: the item at 2x and its name (Gothic 14 bold, pale yellow, up to two lines), the
 * description in Gothic 14 on black, then the actions as big red buttons, two to a row (an odd last one full
 * width). On a round screen it is 216 wide, its text follows the circle and its buttons are narrowed to it.
 */
public class WndWatchItem extends WatchWindow {
	static final int BTN_H = 34, GAP = 2;
	private final Window owner;
	private final Item item;
	private final int w;
	private final Group body = new Group();

	/** "DROP" -> "Drop" (Pebble PD's labels). */
	static String capital(String s) {
		if (s == null || s.isEmpty()) return "";
		if (!s.equals(s.toUpperCase())) return s;
		String l = s.toLowerCase();
		return Character.toUpperCase(l.charAt(0)) + l.substring(1);
	}

	static int width() { return pebxel.Screen.round ? 216 : 179; }

	public WndWatchItem(final Window owner, final Item item) {
		super(pebxel.Screen.round ? 22 : 10, 4, width(), 100, false);
		this.owner = owner; this.item = item; this.w = width();
		add(body);
		int h = build();
		// (on a round screen the text's lines depend on where the window sits: settle it)
		for (int k = 0; k < 3; k++) {
			resize(w, h);
			place(pebxel.Screen.round ? 22 : 10, Math.max(4, (WatchHud.H - h) / 2));
			int h2 = build();
			if (h2 == h) break;
			h = h2;
		}
	}

	private List<String> acts() { return Dungeon.hero.isAlive() && Dungeon.hero.belongings.contains(item) ? item.actions(Dungeon.hero) : new ArrayList<>(); }

	private int build() {
		body.clear();
		boolean round = pebxel.Screen.round;
		// the head: the item, its name
		ItemSprite spr = new ItemSprite(item); spr.scale.set(2); spr.y = round ? 1 : 0; body.add(spr);
		String name = Messages.titleCase(item.title());
		int color = item.levelKnown && item.level() > 0 ? 0x55FF55 : item.levelKnown && item.level() < 0 ? 0xFF5555 : PASTEL;
		List<String> nl = WatchHud.wrap(name, WatchText.G14B, round ? 124 : w - 36);
		if (round) {
			// the head sits in the circle's narrow top: centred under it
			int nw = 0;
			for (int i = 0; i < Math.min(2, nl.size()); i++) nw = Math.max(nw, WatchText.width(WatchText.G14B, nl.get(i)));
			spr.x = pebxel.Screen.w / 2 - contentX - (36 + nw) / 2;
		}
		int y = 0;
		for (int i = 0; i < Math.min(2, nl.size()); i++) {
			WatchText t = new WatchText(WatchText.G14B, nl.get(i), color);
			WatchHud.place(t, (int) spr.x + 36, y + 2, 0, 0); body.add(t);
			y += lineHeight(WatchText.G14B);
		}
		int head = Math.max(36, y + 4);
		// the description, on black
		String info = item.info();
		int tx = round ? 5 : 2, tw = w - 2 * tx;
		int th = flowHeight(info, WatchText.G14, tx, head + 2, tw);
		if (round) {
			// keep the window inside the circle's middle: the description gives way (cut short with "...")
			int rows = (acts().size() + 1) / 2, max = 200 - head - 2 - rows * (BTN_H + GAP);
			while (th > max && info.length() > 20) {
				int cut = info.lastIndexOf(' ', info.length() - 2);
				if (cut < 10) break;
				info = info.substring(0, cut) + "...";
				th = flowHeight(info, WatchText.G14, tx, head + 2, tw);
			}
		}
		if (round) body.add(Shape.cut(pebxel.Screen.w / 2 - contentX, pebxel.Screen.h / 2 - contentY, R_IN, -2 - (pebxel.Screen.w / 2 - contentX), head - (pebxel.Screen.h / 2 - contentY), w + 4, th + 4, 0x000000));
		else body.add(WatchHud.block(0x000000, -2, head, w + 4, th + 4));
		flowText(body, info, WatchText.G14, WHITE, tx, head + 2, tw);
		y = head + th + 4 + GAP;
		// the actions
		List<String> acts = acts();
		int half = (w - GAP) / 2;
		for (int i = 0; i < acts.size(); i++) {
			final String action = acts.get(i);
			WatchButton b = new WatchButton(capital(item.actionName(action, Dungeon.hero))) {
				@Override protected void onClick() {
					hide();
					if (owner != null && owner.parent != null) owner.hide();
					if (Dungeon.hero.isAlive() && Dungeon.hero.belongings.contains(item)) item.execute(Dungeon.hero, action);
					Item.updateQuickslot();
				}
			};
			if (action.equals(item.defaultAction())) b.labelColor = ICTERINE;
			boolean full = i == acts.size() - 1 && i % 2 == 0;
			b.setRect(i % 2 == 0 ? 0 : w - half, y, full ? w : half, BTN_H);
			fit(b);
			b.label(b.label);
			body.add(b);
			if (i % 2 == 1 || full) y += BTN_H + GAP;
		}
		return y;
	}
}
