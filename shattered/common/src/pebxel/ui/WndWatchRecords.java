package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.items.Generator;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.potions.Potion;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll;
import com.shatteredpixel.shatteredpixeldungeon.journal.Notes;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.watabou.noosa.Group;
import com.watabou.utils.Reflection;
import java.util.ArrayList;
import java.util.List;

/**
 * Pebble PD's catalogue and journal (sheet.c), on black.
 * Catalogue: "Catalogue: Potions" / "Scrolls" in pastel yellow, two columns of six 22 px rows (the item at 1x, its
 * name without "Potion of", white when known, grey when not); tabs Potions / Scrolls underneath.
 * Journal: the special places found (18 px rows: the name, its depth by a stairs icon), or "No special places
 * found yet." Up/Down go between the tabs; Select on the shown one, or Back, closes.
 */
public class WndWatchRecords extends WatchWindow {
	static final int W = 188, ROW_C = 22, ROW_J = 18, TAB_W = 61, TAB_H = 26;
	private final boolean journal;
	private final Group page = new Group(), tabs = new Group();
	private int tab;

	public static WndWatchRecords catalogue() { return new WndWatchRecords(false); }
	public static WndWatchRecords journal() { return new WndWatchRecords(true); }

	private WndWatchRecords(boolean journal) {
		super((pebxel.Screen.w - W) / 2, 0, W, 100, false);
		this.journal = journal;
		add(page); add(tabs);
		if (journal) {
			List<Notes.Record> rs = records();
			int h = 22 + Math.max(1, Math.min(8, rs.size())) * ROW_J + 4;
			resize(W, h);
			place((pebxel.Screen.w - W) / 2, (pebxel.Screen.h - h) / 2);
		} else {
			int h = 20 + 6 * ROW_C;
			below = TAB_H + 4;
			resize(W, h);
			place((pebxel.Screen.w - W) / 2, (pebxel.Screen.h - h - TAB_H) / 2);
		}
		show(0);
	}

	private static List<Notes.Record> records() {
		List<Notes.Record> out = new ArrayList<>();
		try { out.addAll(Notes.getRecords(Notes.LandmarkRecord.class)); } catch (Exception e) { }
		try { out.addAll(Notes.getRecords(Notes.KeyRecord.class)); } catch (Exception e) { }
		out.sort((a, b) -> a.depth() - b.depth());
		return out;
	}

	private void show(int t) {
		if (parent == null && camera != null && page.parent == null) return;   // (closed already: a late tap)
		tab = t;
		pageChanged();
		wipe(page); wipe(tabs);
		page.add(WatchHud.block(0x555555, -1, -1, W + 2, 17));
		if (journal) { journalPage(); return; }
		boolean potions = t == 0;
		title("Catalogue: " + (potions ? "Potions" : "Scrolls"));
		List<Class<? extends Item>> cls = new ArrayList<>();
		for (Class<?> c : (potions ? Generator.Category.POTION : Generator.Category.SCROLL).classes) cls.add((Class<? extends Item>) c);
		for (int i = 0; i < cls.size() && i < 12; i++) {
			Item it = Reflection.newInstance(cls.get(i));
			if (it == null) continue;
			boolean known = potions ? Potion.getKnown().contains(cls.get(i)) : Scroll.getKnown().contains(cls.get(i));
			int col = i / 6, row = i % 6, x = 2 + col * (W / 2), y = 20 + row * ROW_C;
			ItemSprite s = new ItemSprite(it); s.x = x + Math.round((16 - s.width()) / 2); s.y = y + Math.round((ROW_C - s.height()) / 2); page.add(s);
			String name = Messages.titleCase(it.trueName()).replaceFirst("^(Potion|Scroll) [Oo]f ", "");
			List<String> nl = WatchHud.wrap(name, WatchText.G14, W / 2 - 22);
			WatchText n = new WatchText(WatchText.G14, nl.get(0), known ? WHITE : LIGHT);
			WatchHud.place(n, x + 18, y + 4, 0, 0); page.add(n);
		}
		String[] names = {"Potions", "Scrolls"};
		int x0 = (W - 2 * TAB_W - 1) / 2;
		for (int k = 0; k < 2; k++) {
			int tx = x0 + k * (TAB_W + 1), ty = ch + 1;
			tabArt(tabs, tx, ty - 1, TAB_W, TAB_H, k == t);
			WatchText l = new WatchText(WatchText.G14B, names[k], k == t ? WHITE : LIGHT);
			WatchHud.place(l, tx, ty + (k == t ? 2 : 4), TAB_W, 1); tabs.add(l);
			Tab b = new Tab(k); b.setRect(tx, ty, TAB_W, TAB_H - 2); tabs.add(b);
		}
	}

	private void title(String s) {
		WatchText t = new WatchText(WatchText.G14B, s, PASTEL);
		WatchHud.place(t, 0, 0, W, 1); page.add(t);
	}

	private void journalPage() {
		title("Journal");
		List<Notes.Record> rs = records();
		if (rs.isEmpty()) {
			WatchText n = new WatchText(WatchText.G14, "No special places found yet.", LIGHT);
			WatchHud.place(n, 0, 24, W, 1); page.add(n);
			return;
		}
		for (int i = 0; i < rs.size() && i < 8; i++) {
			Notes.Record r = rs.get(i);
			int y = 22 + i * ROW_J;
			List<String> nl = WatchHud.wrap(Messages.titleCase(r.title()), WatchText.G14, W - 44);
			WatchText n = new WatchText(WatchText.G14, nl.get(0), WHITE);
			WatchHud.place(n, 4, y, 0, 0); page.add(n);
			WatchText d = new WatchText(WatchText.G14B, Integer.toString(r.depth()), WHITE);
			WatchHud.place(d, 0, y, W - 22, 2); page.add(d);
			com.watabou.noosa.Image st = com.shatteredpixel.shatteredpixeldungeon.ui.Icons.STAIRS.get();
			st.x = W - 18; st.y = y; page.add(st);
		}
	}

	/** A tab: being framed shows it (Up/Down); pressing the shown one closes the window. */
	private class Tab extends Button implements Focusable {
		final int k;
		Tab(int k) { super(); this.k = k; }
		public void focus(boolean on) { if (on && k != tab) show(k); }
		@Override protected void onClick() { if (k == tab) { if (!pebxel.Controls.fingerLast) hide(); } else show(k); }
	}
}
