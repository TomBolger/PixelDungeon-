package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Belongings;
import com.shatteredpixel.shatteredpixeldungeon.items.EquipableItem;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.armor.Armor;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.MagicalHolster;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.PotionBandolier;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.ScrollHolder;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.VelvetPouch;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.Weapon;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSpriteSheet;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndUseItem;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Group;
import com.watabou.noosa.Image;
import java.util.ArrayList;
import java.util.List;

/**
 * Pebble PD's backpack: a 5x5 grid of 35x34 cells (items at 2x) under a "Backpack" tab; the top row the worn
 * things, then the bag's items, then Exit in the bottom right; the bags as tabs underneath. With a selector (pick
 * an item to throw, zap, upgrade...) its prompt is the title and only the items it takes can be chosen.
 */
public class WndWatchBag extends WatchWindow implements Navigable {
	static final int W = 179, CELL_W = 35, CELL_H = 34, HEAD = 15, ROWS = 5, TAB_W = 36, TAB_H = 28;
	private final Bag bag;
	private final WndBag.ItemSelector selector;
	private final Group grid = new Group();

	// ---- Pebble PD's two-step choosing: Up/Down pick a row, Select goes into it; Up/Down pick a cell (empty ones
	// skipped), Select opens it; Back goes back out to the rows, then closes ----
	private final List<Button> rowAreas = new ArrayList<>();
	private final List<List<Button>> rowCells = new ArrayList<>();
	private boolean rowMode = true;
	private int row, col;
	/** Choosing: the first row with something to choose, where the frame starts (no Downs past the worn row). */
	private int firstRow = -1;

	/** Forget the rows; the areas only Up/Down uses (never in the window, so never destroyed with it) are destroyed
	 *  here: their touch areas stayed listening otherwise, one more each time a bag opened. */
	private void dropRows() { for (Button a : rowAreas) if (a.parent == null) a.destroy(); rowAreas.clear(); rowCells.clear(); cellItem.clear(); }

	@Override public void destroy() { dropRows(); super.destroy(); }

	/** No longer the open bag (Shattered's WndBag.INSTANCE: see the constructor). */
	@Override public void hide() { super.hide(); if (WndBag.INSTANCE == this) WndBag.INSTANCE = null; }

	private void newRow(Button area) { rowAreas.add(area); rowCells.add(new ArrayList<>()); }
	private void addToRow(Button b) { rowCells.get(rowCells.size() - 1).add(b); }

	public void nav(int dir) {
		if (rowAreas.isEmpty()) return;
		if (rowMode) { row = (row + dir + rowAreas.size()) % rowAreas.size(); return; }
		List<Button> cs = rowCells.get(row);
		for (int k = 0; k < cs.size(); k++) {
			col = (col + dir + cs.size()) % cs.size();
			if (cs.get(col).active) return;
		}
	}

	public com.watabou.noosa.ui.Component focused() {
		if (rowAreas.isEmpty()) return null;
		if (rowMode) return rowAreas.get(row);
		List<Button> cs = rowCells.get(row);
		return cs.isEmpty() ? rowAreas.get(row) : cs.get(Math.max(0, Math.min(cs.size() - 1, col)));
	}

	/** The frame on an item's cell (the Assistant's pick in a choosing it opened), if it is shown and can be chosen. */
	public void focusOn(Item item) {
		for (int r = 0; r < rowCells.size(); r++) {
			List<Button> cs = rowCells.get(r);
			for (int k = 0; k < cs.size(); k++) if (cellItem.get(cs.get(k)) == item && cs.get(k).active) { row = r; col = k; rowMode = false; return; }
		}
	}
	private final java.util.IdentityHashMap<Button, Item> cellItem = new java.util.IdentityHashMap<>();

	/** Into a row: on its first cell that can be chosen. */
	private void enter(int r) {
		row = r;
		List<Button> cs = rowCells.get(r);
		for (int k = 0; k < cs.size(); k++) if (cs.get(k).active) { col = k; rowMode = false; return; }
	}

	private Button rowArea(int r, int x, int y, int w, int h) {
		Button a = new Button() { @Override protected void onClick() { enter(r); } };
		a.setRect(x, y, w, h);
		return a;
	}

	public WndWatchBag(Bag bag, WndBag.ItemSelector selector) {
		super(pebxel.Screen.round ? 0 : 10, pebxel.Screen.round ? 0 : 7, pebxel.Screen.round ? pebxel.Screen.w : W,
				pebxel.Screen.round ? WB : HEAD + ROWS * (CELL_H + 1) - 1, true);
		fixed = true;
		this.bag = bag == null ? Dungeon.hero.belongings.backpack : bag;
		this.selector = selector;
		below = pebxel.Screen.round ? pebxel.Screen.h - WB : TAB_H;
		resize(cw, ch);
		if (pebxel.Screen.round) place(0, 0); else { chrome.visible = false; place(10, 7); }
		add(grid);
		if (pebxel.Screen.round) buildRound(); else build();
		if (firstRow > 0) row = firstRow;
		// the one open bag, as Shattered's own WndBag is: a new one closes the last. Shattered keeps the bag it
		// opened and hides it later (the shop's trade window, the energy one), but that was the WndBag swapped for
		// this one, so this one stayed: one more under the next after every sale, 70 pictures each, and as many Backs
		// to leave. (The WndBag being swapped isn't in the scene: Swaps destroys it.)
		Window prev = WndBag.INSTANCE;
		if (prev != null && prev != this && prev.parent != null) prev.hide();
		WndBag.INSTANCE = this;
	}

	/**
	 * Emery, as Pebble PD's bag.c: the frame round the cells only (content at 10,7: a 15 px head, then 5 rows of
	 * 35x34 cells with 1 px gaps), "Backpack" on an upside-down tab centred over it, the bags as tabs under it.
	 */
	private void build() {
		wipe(grid); dropRows();
		Belongings b = Dungeon.hero.belongings;
		int gh = ROWS * (CELL_H + 1) - 1;
		// the frame and the black between the cells
		com.watabou.noosa.NinePatch frame = new com.watabou.noosa.NinePatch(PdArt.tex("chrome"), 64, 0, 22, 22, 7);
		frame.x = -7; frame.y = HEAD - 7; frame.size(W + 14, gh + 14); grid.add(frame);
		grid.add(WatchHud.block(0x000000, -1, HEAD - 1, W + 2, gh + 2));
		// the title on its tab
		String title = selector != null ? selector.textPrompt() : bag == b.backpack ? "Backpack" : Messages(bag);
		if (title == null) title = "";
		int font = selector != null ? WatchText.G14 : WatchText.G14B;
		int tw = Math.min(W - 10, WatchText.width(font, title) + 20), tx = (W - tw) / 2;
		tabUp(grid, tx, HEAD, tw, HEAD + 4);
		WatchText t = new WatchText(font, title, PASTEL);
		WatchHud.place(t, tx, -1, tw, 1); grid.add(t);
		if (selector == null) {
			WatchText gold = new WatchText(WatchText.G14B, Integer.toString(Dungeon.gold), 0xFFFF55); gold.outline = true;
			WatchHud.place(gold, 0, 0, W - 1, 2); grid.add(gold);
		}
		// the rows (their areas first: the cells over them take a finger first)
		List<Object> cells = contents();
		int n = ROWS * 5;
		boolean exit = cells.size() < n;
		for (int r = 0; r < ROWS; r++) {
			Button area = rowArea(r, -1, HEAD + r * (CELL_H + 1) - 1, W + 2, CELL_H + 2);
			grid.add(area); newRow(area);
			for (int c = 0; c < 5; c++) {
				int i = r * 5 + c, x = c * (CELL_W + 1), y = HEAD + r * (CELL_H + 1);
				if (exit && i == n - 1) { exitCell(x, y); continue; }
				cell(x, y, i < cells.size() ? cells.get(i) : null);
			}
		}
		// the bags as tabs underneath (Pebble PD: 36x28 at 6 + 37 k on the screen)
		List<Bag> bags = new ArrayList<>();
		bags.add(b.backpack);
		for (Item it : b.backpack.items) if (it instanceof Bag) bags.add((Bag) it);
		if (bags.size() > 1) {
			Button area = rowArea(ROWS, -4, ch, bags.size() * (TAB_W + 1), TAB_H);
			newRow(area);
		}
		for (int k = 0; k < bags.size(); k++) {
			final Bag bg = bags.get(k);
			int x = -4 + k * (TAB_W + 1), y = gh + HEAD;
			tabArt(grid, x, y, TAB_W, TAB_H, bg == bag);
			int pi = bg == b.backpack ? 0 : bg instanceof VelvetPouch ? 1 : bg instanceof ScrollHolder ? 2 : bg instanceof MagicalHolster ? 3 : -1;
			if (pi >= 0) {
				Image ic = PdArt.image("icons", 17 + 10 * pi, 0, 10, 10, 2);
				ic.x = x + (TAB_W - 20) / 2; ic.y = y + (bg == bag ? 3 : 5); grid.add(ic);
			} else {
				ItemSprite sp = new ItemSprite(bg); sp.x = x + (TAB_W - 16) / 2; sp.y = y + (bg == bag ? 5 : 7); grid.add(sp);
			}
			Button tb = new Button() { @Override protected void onClick() { if (bg != bag) { hide(); show(new WndWatchBag(bg, selector)); } } };
			tb.setRect(x, y, TAB_W, TAB_H - 3); grid.add(tb);
			if (bags.size() > 1) addToRow(tb);
		}
	}

	/** An upside-down tab (the title's): tabArt flipped, its open side down on the frame. */
	static void tabUp(Group into, int x, int bottom, int w, int h) {
		Group tmp = new Group();
		tabArt(tmp, x, 0, w, h, true);
		for (com.watabou.noosa.Gizmo g : com.watabou.noosa.GroupAccess.members(tmp)) {
			ColorBlock c = (ColorBlock) g;
			c.y = bottom - (c.y + c.height());
			into.add(c);
		}
	}

	// ---- Gabbro: the backpack fills the screen in centred rows of 32 px cells (Pebble PD's round bag) ----
	static final int WB = 188;
	static int rowY(int r) { return 15 + 33 * r; }
	static int cellX(int col, int n) { return 130 - (33 * n - 1) / 2 + 33 * col; }

	/** The bag's frame: light edge, grey, black, grey, each clipped to a circle (radius 130, 128, 125, 124). */
	@Override
	protected void roundPanel() {
		roundFrame.clear();
		int y0 = rowY(0) + 4, w = pebxel.Screen.w;
		roundFrame.add(Shape.cut(130, 130, 130, -130, y0 - 130, w, WB - y0, 0xAAAAAA));
		roundFrame.add(Shape.cut(130, 130, 128, -130, y0 + 1 - 130, w, WB - y0 - 2, 0x555555));
		roundFrame.add(Shape.cut(130, 130, 125, -130, y0 + 5 - 130, w, WB - y0 - 10, 0x000000));
		roundFrame.add(Shape.cut(130, 130, 124, -130, y0 + 6 - 130, w, WB - y0 - 12, 0x555555));
	}

	private void buildRound() {
		wipe(grid);
		String title = selector != null ? selector.textPrompt() : bag == Dungeon.hero.belongings.backpack ? "Backpack" : Messages(bag);
		if (selector != null) {
			List<String> ls = WatchHud.wrap(title, WatchText.G14, 150);
			for (int i = 0; i < Math.min(2, ls.size()); i++) { WatchText t = new WatchText(WatchText.G14, ls.get(i), ICTERINE); WatchHud.place(t, 55, rowY(0) - 6 + i * 13, 150, 1); grid.add(t); }
		} else {
			WatchText t = new WatchText(WatchText.G14B, title, ICTERINE); WatchHud.place(t, 50, rowY(0) + 10, 160, 1); grid.add(t);
		}
		List<Object> cells = contents();
		int[] perRow = {5, 7, 7, 6};
		int i = 0;
		dropRows();
		for (int r = 0; r < perRow.length; r++) {
			int n = perRow[r], y = rowY(r + 1);
			grid.add(WatchHud.block(0x000000, cellX(0, n) - 2, y - 2, 33 * n + 3, 36));
			Button area = rowArea(r, cellX(0, n) - 2, y - 2, 33 * n + 3, 36); grid.add(area); newRow(area);
			for (int c = 0; c < n; c++, i++) cell(cellX(c, n), y, 32, 32, i < cells.size() ? cells.get(i) : null);
		}
		// the last row: the bags and Exit
		Button lastArea = rowArea(perRow.length, 40, 180, 200, 60); newRow(lastArea);
		// the bags: bubbles hanging under the frame
		Belongings b = Dungeon.hero.belongings;
		List<Bag> bags = new ArrayList<>();
		bags.add(b.backpack);
		for (Item it : b.backpack.items) if (it instanceof Bag) bags.add((Bag) it);
		int n = bags.size(), x0 = Math.min(130 - 18 * (n - 1), 184 - 36 * (n - 1));
		for (int k = 0; k < n; k++) {
			final Bag bg = bags.get(k);
			int cx = x0 + 36 * k;
			grid.add(Shape.disc(cx, 200, 18, 0x000000)); grid.add(Shape.disc(cx, 200, 17, bg == bag ? 0xFFFFFF : 0xAAAAAA)); grid.add(Shape.disc(cx, 200, 15, 0x555555));
			int pi = bg == b.backpack ? 0 : bg instanceof VelvetPouch ? 1 : bg instanceof ScrollHolder ? 2 : bg instanceof MagicalHolster ? 3 : -1;
			if (pi >= 0) { Image ic = PdArt.image("icons", 17 + 10 * pi, 0, 10, 10, 2); ic.x = cx - 10; ic.y = 190; grid.add(ic); }
			else { ItemSprite sp = new ItemSprite(bg); sp.x = cx - 8; sp.y = 192; grid.add(sp); }
			Button tb = new Button() { @Override protected void onClick() { if (bg != bag) { hide(); show(new WndWatchBag(bg, selector)); } } };
			tb.setRect(cx - 17, 183, 34, 34); grid.add(tb); addToRow(tb);
		}
		// Exit: a red bubble at the bottom right
		grid.add(Shape.disc(213, 213, 23, 0x000000)); grid.add(Shape.disc(213, 213, 22, 0xFF5500)); grid.add(Shape.disc(213, 213, 20, 0xAA0000));
		Image ex = PdArt.image("menu_icons", 128, 0, 16, 16, 2); ex.x = 195; ex.y = 195; grid.add(ex);
		Button bt = new Button() { @Override protected void onClick() { close(); } };
		bt.setRect(191, 191, 44, 44); grid.add(bt); addToRow(bt);
	}

	/** What the cells hold: the worn things (or their places), then the bag's items. */
	private List<Object> contents() {
		Belongings b = Dungeon.hero.belongings;
		List<Object> cells = new ArrayList<>();
		cells.add(b.weapon != null ? b.weapon : (Object) ItemSpriteSheet.WEAPON_HOLDER);
		cells.add(b.armor != null ? b.armor : (Object) ItemSpriteSheet.ARMOR_HOLDER);
		cells.add(b.artifact != null ? b.artifact : (Object) ItemSpriteSheet.ARTIFACT_HOLDER);
		cells.add(b.misc != null ? b.misc : (Object) ItemSpriteSheet.SOMETHING);
		cells.add(b.ring != null ? b.ring : (Object) ItemSpriteSheet.RING_HOLDER);
		if (bag != b.backpack) cells.add(bag);
		else if (b.secondWep != null) cells.add(b.secondWep);
		for (Item it : bag.items) if (!(it instanceof Bag)) cells.add(it);
		return cells;
	}

	private static String Messages(Bag b) { return com.shatteredpixel.shatteredpixeldungeon.messages.Messages.titleCase(b.name()); }

	private void exitCell(int x, int y) {
		grid.add(WatchHud.block(0xAA0000, x, y, CELL_W, CELL_H));
		Image ic = PdArt.image("menu_icons", 128, 0, 16, 16, 2); ic.x = x + (CELL_W - 32) / 2; ic.y = y + 1; grid.add(ic);
		Button bt = new Button() { @Override protected void onClick() { super.onClick(); close(); } };
		bt.setRect(x, y, CELL_W, CELL_H); grid.add(bt);
		addToRow(bt);
	}

	/** Another bag in this one's place: on the alchemy screen too (GameScene.show shows nothing there). */
	static void show(Window w) {
		if (com.watabou.noosa.Game.scene() instanceof GameScene) GameScene.show(w); else com.watabou.noosa.Game.scene().addToFront(w);
	}

	/** Closing for good (Exit), not just back out to the rows. */
	private void close() { rowMode = true; onBackPressedClose(); }
	private void onBackPressedClose() { if (selector != null) selector.onSelect(null); hide(); }

	private void cell(int x, int y, Object o) { cell(x, y, CELL_W, CELL_H, o); }

	private void cell(int x, int y, int CELL_W, int CELL_H, Object o) {
		Item item = o instanceof Item ? (Item) o : null;
		// (with a choosing open, the bag shown isn't one of the choices: it's where they are)
		boolean usable = item != null && (selector == null || (item != bag && selector.itemSelectable(item)));
		// (an item that can't be chosen here sits on black: see-through sprites would dither into fuzz)
		// (a known curse: the cell is dark red, Pebble PD's BulgarianRose, under the picture)
		grid.add(WatchHud.block(item != null && !usable ? 0x000000 : item != null && item.cursed && item.cursedKnown ? 0x550000 : 0x555555, x, y, CELL_W, CELL_H));
		if (o == null) return;
		ItemSprite s = item != null ? new ItemSprite(item) : new ItemSprite((Integer) o, null);
		// centred by its own size (Shattered's item pictures aren't all 16x16), on whole pixels
		s.scale.set(2); s.x = x + Math.round((CELL_W - s.width()) / 2f); s.y = y + Math.round((CELL_H - s.height()) / 2f);
		grid.add(s);
		if (item == null) return;
		// Pebble PD's labels (bag.c), Gothic 14 bold, outlined:
		// top left: how many (when more than one), a wand's charges ("?/3" until known), the waterskin's drops
		String st = item.status();
		if (st != null && !st.isEmpty()) label(st, WHITE, x + 2, y - 4, CELL_W - 2, 0);
		// top right: strength needed (":12", orange if too heavy; "12?" in yellow while its level is unknown), or an
		// orange "?" on a potion or scroll not yet known
		if (item instanceof Weapon || item instanceof Armor) {
			boolean lk = item.levelKnown || item instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.MissileWeapon;
			int req = item instanceof Weapon ? (lk ? ((Weapon) item).STRReq() : ((Weapon) item).STRReq(0)) : (lk ? ((Armor) item).STRReq() : ((Armor) item).STRReq(0));
			label(lk ? ":" + req : req + "?", !lk ? CHROME_YELLOW : req > Dungeon.hero.STR() ? 0xFF5555 : WHITE, x, y - 4, CELL_W - 1, 2);
		} else if ((item instanceof com.shatteredpixel.shatteredpixeldungeon.items.potions.Potion || item instanceof com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll) && !item.isIdentified()) {
			label("?", CHROME_YELLOW, x, y - 4, CELL_W - 3, 2);
		}
		// bottom right: its level when known ("+2" green, "-1" orange; a known curse shows "+0")
		if (item.levelKnown && item.isUpgradable()) {
			int lv = item.buffedVisiblyUpgraded();
			if (lv != 0 || (item.cursed && item.cursedKnown))
				label((lv >= 0 ? "+" : "") + lv, lv > 0 ? 0x55FF55 : 0xFF5555, x, y + CELL_H - 18, CELL_W - 1, 2);
		}
		final Item it = item;
		Button bt = new Button() {
			@Override protected void onClick() { choose(it); }
			@Override protected boolean onLongClick() { choose(it); return true; }
		};
		bt.setRect(x, y, CELL_W, CELL_H);
		cellItem.put(bt, item);
		if (!usable) bt.active = false;
		else if (selector != null && firstRow < 0) firstRow = rowCells.size() - 1;
		grid.add(bt);
		if (!rowCells.isEmpty()) addToRow(bt);
	}

	/** A label in a cell's box (x, y: the box's top-left, as Pebble's text boxes; align 0 left, 2 right). */
	private void label(String s, int rgb, int x, int y, int w, int align) {
		WatchText t = new WatchText(WatchText.G14B, s, rgb); t.outline = true;
		WatchHud.place(t, x, y, w, align);
		grid.add(t);
	}

	private void choose(Item item) {
		if (selector != null) {
			if (selector.hideAfterSelecting()) hide();
			selector.onSelect(item);
		} else {
			GameScene.show(new WndUseItem(this, item));
		}
	}

	@Override
	public void onBackPressed() {
		// the Back button: out of a row first (a finger, or a tap outside, closes at once)
		if (!rowMode && !pebxel.Controls.fingerLast) { rowMode = true; return; }
		onBackPressedClose();
	}
}
