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
public class WndWatchBag extends WatchWindow {
	static final int W = 179, CELL_W = 35, CELL_H = 34, HEAD = 15, ROWS = 5, TAB_W = 36, TAB_H = 28;
	private final Bag bag;
	private final WndBag.ItemSelector selector;
	private final Group grid = new Group();

	public WndWatchBag(Bag bag, WndBag.ItemSelector selector) {
		super(WatchWindow.left(W), 7, W, HEAD + ROWS * (CELL_H + 1) - 1, true);
		this.bag = bag == null ? Dungeon.hero.belongings.backpack : bag;
		this.selector = selector;
		below = TAB_H + 2;
		resize(cw, ch);
		place(WatchWindow.left(W), 7 + 2);
		add(grid);
		build();
	}

	private void build() {
		grid.clear();
		// the title, on a tab over the frame
		String title = selector != null ? selector.textPrompt() : bag == Dungeon.hero.belongings.backpack ? "Backpack" : Messages(bag);
		int tw = Math.min(W, WatchText.width(selector != null ? WatchText.G14 : WatchText.G14B, title) + 16);
		ColorBlock band = WatchHud.block(0x555555, 0, -7, tw, 7 + HEAD - 1); grid.add(band);
		WatchText t = new WatchText(selector != null ? WatchText.G14 : WatchText.G14B, title, ICTERINE);
		WatchHud.place(t, 4, -6, 0, 0); grid.add(t);
		WatchText gold = new WatchText(WatchText.G14B, Integer.toString(Dungeon.gold), 0xFFAA00);
		WatchHud.place(gold, 0, -6, W - 2, 2); gold.outline = true;
		if (selector == null) grid.add(gold);
		// the cells: dark grey on black
		grid.add(WatchHud.block(0x000000, -1, HEAD - 1, W + 2, ROWS * (CELL_H + 1) + 1));
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
		int n = ROWS * 5;
		boolean exit = cells.size() < n;
		for (int i = 0; i < n; i++) {
			int col = i % 5, row = i / 5;
			int x = col * (CELL_W + 1), y = HEAD + row * (CELL_H + 1);
			if (exit && i == n - 1) { exitCell(x, y); continue; }
			cell(x, y, i < cells.size() ? cells.get(i) : null);
		}
		// the bags as tabs underneath
		List<Bag> bags = new ArrayList<>();
		bags.add(b.backpack);
		for (Item it : b.backpack.items) if (it instanceof Bag) bags.add((Bag) it);
		for (int k = 0; k < bags.size(); k++) {
			final Bag bg = bags.get(k);
			int x = -4 + k * (TAB_W + 1), y = ch + 7;
			tabArt(grid, x, y - 1, TAB_W, TAB_H, bg == bag);
			int pi = bg == b.backpack ? 0 : bg instanceof VelvetPouch ? 1 : bg instanceof ScrollHolder ? 2 : bg instanceof MagicalHolster ? 3 : -1;
			if (pi >= 0) {
				Image ic = PdArt.image("icons", 17 + 10 * pi, 0, 10, 10, 2);
				ic.x = x + (TAB_W - 20) / 2; ic.y = y + (bg == bag ? 2 : 4); grid.add(ic);
			} else {
				ItemSprite s = new ItemSprite(bg); s.x = x + (TAB_W - 16) / 2; s.y = y + (bg == bag ? 4 : 6); grid.add(s);
			}
			Button tb = new Button() { @Override protected void onClick() { if (bg != bag) { hide(); GameScene.show(new WndWatchBag(bg, selector)); } } };
			tb.setRect(x, y, TAB_W, TAB_H - 4); grid.add(tb);
		}
	}

	private static String Messages(Bag b) { return com.shatteredpixel.shatteredpixeldungeon.messages.Messages.titleCase(b.name()); }

	private void exitCell(int x, int y) {
		grid.add(WatchHud.block(0xAA0000, x, y, CELL_W, CELL_H));
		Image ic = PdArt.image("menu_icons", 128, 0, 16, 16, 2); ic.x = x + (CELL_W - 32) / 2; ic.y = y + 1; grid.add(ic);
		Button bt = new Button() { @Override protected void onClick() { onBackPressed(); } };
		bt.setRect(x, y, CELL_W, CELL_H); grid.add(bt);
	}

	private void cell(int x, int y, Object o) {
		Item item = o instanceof Item ? (Item) o : null;
		boolean usable = item != null && (selector == null || selector.itemSelectable(item));
		grid.add(WatchHud.block(0x555555, x, y, CELL_W, CELL_H));
		if (o == null) return;
		ItemSprite s = item != null ? new ItemSprite(item) : new ItemSprite((Integer) o, null);
		s.scale.set(2); s.x = x + (CELL_W - 32) / 2f; s.y = y + (CELL_H - 32) / 2f;
		if (item == null || !usable) s.alpha(0.4f);
		grid.add(s);
		if (item == null) return;
		if (item.cursed && item.cursedKnown) { ColorBlock c = WatchHud.block(0x550000, x, y, CELL_W, CELL_H); c.alpha(0.5f); grid.add(c); }
		// top left: how many, or a wand's charges
		String st = item.status();
		if (st != null && !st.isEmpty()) label(st, 0xFFFFFF, x + 1, y - 2, 0);
		// top right: strength needed
		if (item instanceof Weapon || item instanceof Armor) {
			int req = item instanceof Weapon ? ((Weapon) item).STRReq() : ((Armor) item).STRReq();
			boolean known = item.levelKnown || true;
			if (known) label(":" + req, req > Dungeon.hero.STR() ? 0xFF5555 : 0xFFFFFF, x, y - 2, CELL_W - 1);
		}
		// bottom right: its level
		if (item.levelKnown && item.level() != 0 && !(item instanceof Bag)) {
			int lv = item.visiblyUpgraded();
			if (lv != 0) label((lv > 0 ? "+" : "") + lv, lv > 0 ? 0x55FF55 : 0xFF5555, x, y + CELL_H - 15, CELL_W - 1);
		} else if (!item.isIdentified() && (item instanceof EquipableItem) && !(item instanceof Bag) && item.level() == 0) {
			// nothing: unknown level shows no number
		}
		final Item it = item;
		Button bt = new Button() {
			@Override protected void onClick() { choose(it); }
			@Override protected boolean onLongClick() { choose(it); return true; }
		};
		bt.setRect(x, y, CELL_W, CELL_H);
		if (!usable) bt.active = false;
		grid.add(bt);
	}

	private void label(String s, int rgb, int x, int y, int rightEdge) {
		WatchText t = new WatchText(WatchText.G14B, s, rgb); t.outline = true;
		if (rightEdge > 0) WatchHud.place(t, x, y, rightEdge, 2); else WatchHud.place(t, x, y, 0, 0);
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
		if (selector != null) selector.onSelect(null);
		super.onBackPressed();
	}
}
