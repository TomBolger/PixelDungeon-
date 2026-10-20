package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.Statistics;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.BuffIcon;
import com.shatteredpixel.shatteredpixeldungeon.ui.BuffIndicator;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndHero;
import com.watabou.noosa.Group;
import java.util.ArrayList;
import java.util.List;

/**
 * Pebble PD's hero sheet (sheet.c): "LEVEL 1 MAGE" in Icterine, then Strength, Health, Experience, Gold Collected and
 * Maximum Depth as big rows (the label in Gothic 18, the value in Gothic 18 bold from 64 % of the width); tabs
 * underneath. Shattered adds talents: their tab opens Shattered's own talent page (where points are spent).
 * Up/Down go between the tabs and show them; Select on the shown tab, or Back, closes.
 */
public class WndWatchHero extends WatchWindow {
	static final int W = 172, H = 150, TAB_W = 54, TAB_H = 26;
	private static int lastTab = 0;
	private final Group page = new Group(), tabs = new Group();
	private int tab;

	public WndWatchHero() {
		super(x0(), 0, W, H, false);   // (the black-bodied frame: small text reads best on black)
		below = TAB_H + 4;
		resize(W, H);
		place(x0(), pebxel.Screen.round ? (pebxel.Screen.h - H - TAB_H) / 2 : 22);
		add(page); add(tabs);
		show(lastTab == 1 ? 0 : lastTab);
	}

	private static int x0() { return (pebxel.Screen.w - W) / 2; }

	private void show(int t) {
		tab = t; lastTab = t;
		page.clear(); tabs.clear();
		Hero h = Dungeon.hero;
		if (h == null) return;
		if (t == 0) stats(h); else buffs(h);
		// the tabs under the window: Stats, Talents (a dot while points wait to be spent), Buffs
		String[] names = {Messages.get(WndHero.class, "stats"), Messages.get(WndHero.class, "talents"), Messages.get(WndHero.class, "buffs")};
		int x = (W - 3 * TAB_W - 2) / 2;
		boolean points = false;
		for (int tier = 1; tier <= 4; tier++) if (h.talentPointsAvailable(tier) > 0) points = true;
		for (int k = 0; k < 3; k++) {
			final int kk = k;
			int tx = x + k * (TAB_W + 1), ty = ch + 7;
			tabArt(tabs, tx, ty - 1, TAB_W, TAB_H, k == t);
			WatchText l = new WatchText(WatchText.G14B, names[k], k == t ? WHITE : LIGHT);
			WatchHud.place(l, tx, ty + (k == t ? 2 : 4), TAB_W, 1); tabs.add(l);
			if (k == 1 && points) tabs.add(WatchHud.block(0xFFFF55, tx + TAB_W - 10, ty + 5, 4, 4));
			Tab b = new Tab(kk);
			b.setRect(tx, ty, TAB_W, TAB_H - 2); tabs.add(b);
		}
	}

	/** A tab: being framed shows it (Up/Down); pressing the one shown closes the sheet. */
	private class Tab extends Button implements Focusable {
		final int k;
		Tab(int k) { super(); this.k = k; }
		public void focus(boolean on) { if (on && k != tab && k != 1) show(k); }
		@Override protected void onClick() {
			if (k == 1) { talents(); return; }
			if (k == tab && !pebxel.Controls.fingerLast) hide(); else show(k);
		}
	}

	private void talents() {
		hide();
		WndHero.lastIdx = 1;
		Swaps.nativeHero = true;
		try { GameScene.show(new WndHero()); } finally { Swaps.nativeHero = false; }
	}

	private static Class<?> statsTab() {
		try { return Class.forName("com.shatteredpixel.shatteredpixeldungeon.windows.WndHero$StatsTab"); } catch (Exception e) { return WndHero.class; }
	}

	private void stats(Hero h) {
		String title = Messages.get(statsTab(), "title", h.lvl, h.className()).toUpperCase();
		if (title.contains("!!!")) title = "LEVEL " + h.lvl + " " + h.className().toUpperCase();
		page.add(WatchHud.block(0x555555, -1, -1, W + 2, 17));   // Pebble PD's grey title band
		WatchText t = new WatchText(WatchText.G14B, title, ICTERINE);
		WatchHud.place(t, pebxel.Screen.round ? 0 : 4, 0, W, pebxel.Screen.round ? 1 : 0); page.add(t);
		int strBonus = h.STR() - h.STR;
		String str = strBonus > 0 ? h.STR + "+" + strBonus : strBonus < 0 ? h.STR + "-" + -strBonus : Integer.toString(h.STR());
		String hp = h.shielding() > 0 ? h.HP + "+" + h.shielding() + "/" + h.HT : h.HP + "/" + h.HT;
		String[][] rows = {
				{Messages.get(statsTab(), "str"), str},
				{Messages.get(statsTab(), "health"), hp},
				{Messages.get(statsTab(), "exp"), h.exp + "/" + h.maxExp()},
				{Messages.get(statsTab(), "gold"), Integer.toString(Statistics.goldCollected)},
				{Messages.get(statsTab(), "depth"), Integer.toString(Statistics.deepestFloor)}};
		for (int i = 0; i < rows.length; i++) {
			int y = 18 + 24 * i + (i >= 3 ? 6 : 0);
			String label = rows[i][0];
			if (label.contains("!!!")) label = new String[]{"Strength", "Health", "Experience", "Gold Collected", "Maximum Depth"}[i];
			WatchText l = new WatchText(WatchText.G18, label, WHITE);
			WatchHud.place(l, 4, y, 0, 0); page.add(l);
			WatchText v = new WatchText(WatchText.G18B, rows[i][1], WHITE);
			WatchHud.place(v, 0, y, W - 6, 2); page.add(v);
		}
	}

	private void buffs(Hero h) {
		List<Buff> bs = new ArrayList<>();
		for (Buff b : h.buffs()) if (b.icon() != BuffIndicator.NONE) bs.add(b);
		if (bs.isEmpty()) {
			WatchText none = new WatchText(WatchText.G18, "No effects", LIGHT);
			WatchHud.place(none, 0, H / 2 - 12, W, 1); page.add(none);
			return;
		}
		boolean two = bs.size() > 5;
		int colW = two ? W / 2 : W;
		for (int i = 0; i < bs.size() && i < 14; i++) {
			Buff b = bs.get(i);
			int col = two ? i / 7 : 0, row = two ? i % 7 : i;
			int x = 4 + col * colW, y = 4 + row * 20;
			BuffIcon ic = new BuffIcon(b, false); ic.scale.set(2); ic.x = x; ic.y = y + 2; page.add(ic);
			String name = Messages.titleCase(b.name());
			List<String> nl = WatchHud.wrap(name, WatchText.G14B, colW - 24);
			WatchText n = new WatchText(WatchText.G14B, nl.get(0), WHITE);
			WatchHud.place(n, x + 20, y + 2, 0, 0); page.add(n);
		}
	}
}
