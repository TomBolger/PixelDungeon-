package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.SPDAction;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap;
import com.shatteredpixel.shatteredpixeldungeon.ui.Toolbar;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndGame;
import com.watabou.input.GameAction;
import com.watabou.input.KeyBindings;
import com.watabou.input.KeyEvent;
import com.watabou.noosa.Camera;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Group;
import com.watabou.noosa.Image;
import com.watabou.noosa.ui.Component;
import com.watabou.utils.PathFinder;
import java.util.*;

/**
 * The watch's buttons, as the Pixel Dungeon watch port uses them, applied to Shattered PD (on the phone).
 *
 * In the dungeon:
 *  - Down / long Down: next / previous target (enemies, items, unexplored areas, stairs). A frame marks it.
 *  - Select: act on the target (attack, pick up, walk there, take the stairs).
 *  - Up: zoom. Long Up: game menu.
 *  - Back: step across the toolbar and status buttons (a frame marks the button); Select presses it.
 *  - Long Select: the backpack, or the long press of the framed button.
 * When a spell or throw asks for a cell, Down cycles the cells worth aiming at, Select aims, Back cancels.
 * In any window or menu screen: Up/Down move the frame between buttons, Select presses (long: long press),
 * Back closes.
 */
public class Controls {
	public static final int UP = 1, DOWN = 2, SELECT = 3, BACK = 4, LONG = 16;

	// ---- what the frame is around ----
	private final Frame frame = new Frame();
	private Group focusOwner;
	private int focus;                 // in a menu, or on the toolbar
	private boolean onToolbar;
	/** The Assistant's list (Assist.plan): entry 0 is plan A, what Select does without a Down. */
	private List<Assist.Target> plan = new ArrayList<>();
	/** Each entry's cell for the marker (-1: an action on the toolbar, shown there instead). */
	private List<Integer> targets = new ArrayList<>();
	private int target;
	private boolean targetsStale = true;
	private int lastPos = -1, lastDepth = -1, lastSettings = -1; private boolean lastAiming; private float lastNow = -1;

	// ---- Pebble PD's screen furniture (pebxel.ui.WatchHud) ----
	private pebxel.ui.WatchHud hud;

	private final pebxel.ui.WatchHud.Hooks hooks = new pebxel.ui.WatchHud.Hooks() {
		public void press(int k, boolean held) { hudPress(k, held); }
		public int targetCell() { return currentTarget(); }
		public int targetKind() {
			if (Dungeon.hero == null || !Dungeon.hero.ready || targets.isEmpty() || Dungeon.level == null) return 0;
			Assist.Target t = current();
			if (t != null) return kindOf(t);
			int c = currentTarget();   // (a bare cell: a test's list)
			if (c < 0) return 0;
			Char ch = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(c);
			if (ch instanceof Mob && ((Mob) ch).alignment == Char.Alignment.ENEMY) return 1;
			if (Dungeon.level.heaps.get(c) != null) return 2;
			return c == Dungeon.hero.pos ? 0 : 3;
		}
		public com.shatteredpixel.shatteredpixeldungeon.items.Item targetItem() { Assist.Target t = current(); return t == null || !Dungeon.hero.ready ? null : t.item; }
		public int toolbarHint() { Assist.Target t = current(); return t == null || !Dungeon.hero.ready || onToolbar || touching ? -1 : buttonOf(t); }
		public int targetCount() { return Dungeon.hero != null && Dungeon.hero.ready ? targets.size() : 0; }
		public int mapZoomLevel() { Camera c = Camera.main; return c == null ? 1 : c.zoom < 1.5f ? 0 : c.zoom < 3f ? 1 : 2; }
		public void tag(int which, boolean held) {
			switch (which) {
				case 0: zoom(); break;
				case 1: dungeonButton(SELECT, false); break;
				case 2: dungeonButton(DOWN, held); break;
				case 3: key(SPDAction.WAIT_OR_PICKUP); break;
			}
		}
	};

	/** The entry Select acts on (null: none, or a bare cell). */
	private Assist.Target current() {
		if (targets.isEmpty() || plan.size() != targets.size()) return null;
		return plan.get(Math.min(target, plan.size() - 1));
	}

	/** The cell Select acts on (-1 none, or an action on the toolbar). */
	private int currentTarget() {
		if (Dungeon.hero == null || !Dungeon.hero.ready || targets.isEmpty()) return -1;
		int c = cellOf(targets.get(Math.min(target, targets.size() - 1)));
		return c < 0 ? -1 : c;
	}

	// (the Select tag's pictures, WatchHud.Hooks.targetKind)
	public static final int K_ENEMY = 1, K_ITEM = 2, K_GO = 3, K_AIM = 4, K_USE = 5, K_WAIT = 6, K_SEARCH = 7, K_REST = 8, K_TALENT = 9;
	static int kindOf(Assist.Target t) {
		if (aiming()) return t.kind == Assist.ENEMY ? K_ENEMY : K_GO;   // (the aiming list: cells to aim at)
		switch (t.kind) {
			case Assist.ENEMY: return Assist.WAIT_ACT.equals(t.action) ? K_WAIT : K_ENEMY;
			case Assist.PICKUP: return Dungeon.level.heaps.get(t.cell) != null ? K_ITEM : K_GO;
			case Assist.GO: return t.cell == Dungeon.hero.pos ? K_SEARCH : K_GO;
			case Assist.USE: return TALENTS.equals(t.action) ? K_TALENT : aimed(t) ? K_AIM : K_USE;
			default: return Assist.REST_ACT.equals(t.action) ? K_REST : Assist.WAIT_ACT.equals(t.action) ? K_WAIT : K_SEARCH;
		}
	}

	/** An item use aimed at a cell (a zap, a throw, a shot): the marker goes on that cell. */
	static boolean aimed(Assist.Target t) { return t.kind == Assist.USE && t.item != null && t.cell >= 0 && Dungeon.hero != null && t.cell != Dungeon.hero.pos; }

	/** Where the marker goes for an entry: its cell, or -1 for an action of the toolbar's (eat, drink, wait, search). */
	static int markCell(Assist.Target t) {
		switch (t.kind) {
			case Assist.USE: return aimed(t) ? t.cell : -1;
			case Assist.WAIT: return -1;
			case Assist.GO: return Dungeon.hero != null && t.cell == Dungeon.hero.pos ? -1 : t.cell;
			default: return t.cell;
		}
	}

	/** The toolbar or status button an entry is the action of (its reticle shows it), or -1. As Pebble PD's hand_btn:
	 *  an item in a quickslot is that slot's, anything else the backpack's. */
	static int buttonOf(Assist.Target t) {
		int k = kindOf(t);
		if (k == K_WAIT || k == K_REST) return pebxel.ui.WatchHud.TB_WAIT;
		if (k == K_SEARCH) return pebxel.ui.WatchHud.TB_SEARCH;
		if (k == K_TALENT) return pebxel.ui.WatchHud.B_PORTRAIT;
		if (t.kind != Assist.USE || t.item == null) return -1;
		for (int q = 0; q < pebxel.ui.WatchHud.TB_Q.length; q++) if (Dungeon.quickslot.getItem(q) == t.item && Dungeon.quickslot.isNonePlaceholder(q)) return pebxel.ui.WatchHud.TB_Q[q];
		return pebxel.ui.WatchHud.TB_BAG;
	}

	// A test's bare cells: SHOT marks the Huntress's spirit bow aimed at the enemy there (the list before Assist's)
	static final int SHOT = 1 << 20;
	static boolean shot(int t) { return t >= 0 && (t & SHOT) != 0; }
	static int cellOf(int t) { return t < 0 ? t : t & (SHOT - 1); }
	static com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow bow() {
		Hero h = Dungeon.hero; return h == null ? null : h.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow.class);
	}
	/** The bow, drawn and loosed at a cell (as its Shoot action, then a tap on the cell). */
	static void shoot(int cell) {
		com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow b = bow();
		if (b == null || Dungeon.hero == null || !Dungeon.hero.ready) return;
		b.execute(Dungeon.hero, com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow.AC_SHOOT);
		if (aiming()) GameScene.handleCell(cell);
	}

	private void hudPress(int k, boolean held) {
		if (Dungeon.hero == null) return;
		boolean ready = Dungeon.hero.ready;
		switch (k) {
			// as Shattered's own wait button does (its key, SPDAction.WAIT, has no key bound by default: sent as a key,
			// the watch's Wait did nothing at all, and only a held one rested)
			case pebxel.ui.WatchHud.TB_WAIT: if (ready && !GameScene.cancel()) Dungeon.hero.rest(held); break;
			// Shattered's one search button: pressed, it asks which cell to examine (pressed again: search); held: search
			case pebxel.ui.WatchHud.TB_SEARCH:
				if (held) { if (ready && !GameScene.cancel()) Dungeon.hero.search(true); }
				else key(SPDAction.EXAMINE);
				break;
			case pebxel.ui.WatchHud.TB_BAG:
				if (held) GameScene.show(pebxel.ui.WndWatchRecords.catalogue());   // (Pebble PD: the backpack held is the catalogue)
				else key(SPDAction.INVENTORY);
				break;
			case pebxel.ui.WatchHud.TB_Q1: case pebxel.ui.WatchHud.TB_Q2: case pebxel.ui.WatchHud.TB_Q3: {
				int slot = k == pebxel.ui.WatchHud.TB_Q1 ? 0 : k == pebxel.ui.WatchHud.TB_Q2 ? 1 : 2;
				if (held) { Object b = quickslotButton(slot); if (b != null) Ui.hold((com.watabou.noosa.Gizmo) b); }
				else key(slot == 0 ? SPDAction.QUICKSLOT_1 : slot == 1 ? SPDAction.QUICKSLOT_2 : SPDAction.QUICKSLOT_3);
				break;
			}
			case pebxel.ui.WatchHud.B_MENU: GameScene.show(new WndGame()); break;
			case pebxel.ui.WatchHud.B_PORTRAIT: GameScene.show(new com.shatteredpixel.shatteredpixeldungeon.windows.WndHero()); break;
		}
		targetsStale = true; acted();
	}

	private static java.lang.reflect.Field qsInstance;
	private static Object quickslotButton(int slot) {
		try {
			if (qsInstance == null) { qsInstance = com.shatteredpixel.shatteredpixeldungeon.ui.QuickSlotButton.class.getDeclaredField("instance"); qsInstance.setAccessible(true); }
			return ((Object[]) qsInstance.get(null))[slot];
		} catch (Exception e) { return null; }
	}

	/** Pebble PD's furniture over the game, made once per game screen. */
	private void keepHud() {
		if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) { hud = null; return; }
		if (hud != null && hud.parent == Game.scene()) return;
		hud = new pebxel.ui.WatchHud(hooks);
		// just above Shattered's own (unseen) furniture: under windows and the map's effects stay below
		Group s = Game.scene();
		com.watabou.noosa.Gizmo after = null;
		for (com.watabou.noosa.Gizmo g : com.watabou.noosa.GroupAccess.members(s))
			if (g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.Toolbar || g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.StatusPane
					|| g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.GameLog || g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.MenuPane) after = g;
		if (after != null) com.watabou.noosa.GroupAccess.addAfter(s, hud, after); else s.add(hud);
	}

	/** A finger is in use: the button frames step aside until the next button press (as in Pebble PD). */
	/** Was the last input a finger (rather than a button)? */
	public static boolean fingerLast;

	public void touched() { fingerLast = true; onToolbar = false; touching = true; if (hud != null) hud.highlight = -1; }
	private boolean touching;

	public void button(int code) {
		touching = false; fingerLast = false;
		boolean held = (code & LONG) != 0;
		int b = code & 15;
		Window w = Ui.topWindow();
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (menu != null) { menuButton(menu, b, held, w); return; }
		if (Dungeon.hero != null && !Dungeon.hero.isAlive() && Game.scene() instanceof GameScene) {
			// the game is over: any button opens the menu's last page (New Game, Rankings, Main Menu), as in Pebble PD
			GameScene.show(new com.shatteredpixel.shatteredpixeldungeon.windows.WndGame());
			return;
		}
		if (Dungeon.hero == null || !Dungeon.hero.isAlive()) return;
		dungeonButton(b, held);
	}

	// ---- menus ----

	private void menuButton(Group menu, int b, boolean held, Window w) {
		List<Ui.Part> bs = buttons(menu);
		if (b == BACK) { if (w != null) w.onBackPressed(); else com.watabou.noosa.NoosaAccess.back(Game.scene()); return; }
		if (focusOwner != menu) { focusOwner = menu; focus = 0; }
		if (menu instanceof pebxel.ui.Navigable) {
			pebxel.ui.Navigable n = (pebxel.ui.Navigable) menu;
			if (b == UP || b == DOWN) n.nav(b == UP ? -1 : 1);
			else if (b == SELECT && n.focused() != null) { if (held) Ui.hold(n.focused()); else Ui.press(n.focused()); }
			return;
		}
		if (b == UP || b == DOWN) {
			// a window taller than the screen scrolls half a screen at a time towards the next button
			int next = bs.isEmpty() ? -1 : b == UP ? (focus + bs.size() - 1) % bs.size() : (focus + 1) % bs.size();
			if (w != null && tall(w)) {
				Component n = next < 0 ? null : (Component) bs.get(next).g;
				boolean wraps = next >= 0 && (b == DOWN ? next < focus : next > focus);
				if (n == null || (!wraps && !visible(w, n))) {
					scroll(w, (b == DOWN ? -1 : 1) * (viewBottom(w) - viewTop(w)) / 2);
					if (n == null || !visible(w, n)) return;   // keep reading; the button comes into view later
				}
				if (wraps) scroll(w, b == DOWN ? 100000 : -100000);
			}
			if (next >= 0) focus = next;
			return;
		}
		if (bs.isEmpty()) return;
		focus = Math.max(0, Math.min(bs.size() - 1, focus));
		if (b == SELECT) { if (held) Ui.hold(bs.get(focus).g); else Ui.press(bs.get(focus).g); }
	}

	// ---- a close button for a finger on windows that fill the screen (no outside left to tap) ----
	private CloseTag close;

	private void closeTag(Window w) {
		boolean need = w != null && !(w instanceof pebxel.ui.WatchWindow) && w.camera != null
				&& (tall(w) || w.camera.screenWidth() >= Game.width - 24 || w.camera.screenHeight() >= Game.height - 24);
		if (!need) { if (close != null && close.parent != null && close.parent != w) close.killAndErase(); if (close != null && close.parent == null) close = null; return; }
		if (close == null || close.parent != w) {
			if (close != null) close.killAndErase();
			close = new CloseTag(w); w.add(close);
		}
		w.bringToFront(close);
		// a fixed spot on the screen, wherever the window has scrolled to: the first corner (round: spot on the rim)
		// with none of the window's own buttons under it (watch pixels -> the window's units)
		Camera c = w.camera;
		int[][] spots = Screen.round ? new int[][]{{186, 40}, {50, 40}, {222, 118}, {14, 118}, {186, 196}, {50, 196}}
				: new int[][]{{Screen.w - 24, 0}, {0, 0}, {Screen.w - 24, Screen.h - 24}, {0, Screen.h - 24}};
		int[] at = spots[0];
		for (int[] sp : spots) {
			boolean free = true;
			for (Ui.Part p : buttons(w)) {
				Component b = (Component) p.g;
				float bx = c.x / 2f + (b.left() - c.scroll.x) * c.zoom / 2, by = c.y / 2f + (b.top() - c.scroll.y) * c.zoom / 2;
				float bw = b.width() * c.zoom / 2, bh = b.height() * c.zoom / 2;
				if (bx < sp[0] + 24 && bx + bw > sp[0] && by < sp[1] + 24 && by + bh > sp[1]) { free = false; break; }
			}
			if (free) { at = sp; break; }
		}
		close.setRect((at[0] * 2 - c.x) / c.zoom + c.scroll.x, (at[1] * 2 - c.y) / c.zoom + c.scroll.y, 24 * 2 / c.zoom, 24 * 2 / c.zoom);
	}

	/** Pebble PD's red exit tag. */
	static class CloseTag extends com.shatteredpixel.shatteredpixeldungeon.ui.Button {
		private final Window w;
		private com.watabou.noosa.NinePatch bg;
		private Image icon;
		CloseTag(Window w) { super(); this.w = w; }
		@Override protected void createChildren() {
			super.createChildren();
			bg = com.shatteredpixel.shatteredpixeldungeon.Chrome.get(com.shatteredpixel.shatteredpixeldungeon.Chrome.Type.RED_BUTTON); add(bg);
			icon = pebxel.ui.PdArt.image("menu_icons", 128, 0, 16, 16, 1); add(icon);
		}
		@Override protected void layout() {
			super.layout();
			bg.x = x; bg.y = y; bg.size(width, height);
			icon.scale.set(width / 20f); icon.x = x + (width - 16 * icon.scale.x) / 2; icon.y = y + (height - 16 * icon.scale.y) / 2;
		}
		@Override protected void onClick() { w.onBackPressed(); }
	}

	// ---- windows taller than the screen (long texts with big letters) ----

	// (a round screen's corners are gone: there, windows are read in the band across its middle)
	// (the watch's own windows are laid out for the whole screen, round or not)
	static int viewTop(Window w) { return Screen.round && !(w instanceof pebxel.ui.WatchWindow) ? 72 : 0; }
	static int viewBottom(Window w) { return Screen.round && !(w instanceof pebxel.ui.WatchWindow) ? Game.height - 72 : Game.height; }

	static boolean tall(Window w) {
		if (w instanceof pebxel.ui.WatchWindow && ((pebxel.ui.WatchWindow) w).fixed) return false;
		return w.camera != null && w.camera.screenHeight() > viewBottom(w) - viewTop(w) + 1;
	}

	/** Is the component fully on screen (window camera units -> phone pixels; the camera starts at the frame's edge,
	 *  above the content's 0: left out, a button at a tall window's foot counted as shown while cut off)? */
	private static boolean visible(Window w, Component c) {
		float top = w.camera.y + (c.top() - w.camera.scroll.y) * w.camera.zoom, bottom = w.camera.y + (c.bottom() - w.camera.scroll.y) * w.camera.zoom;
		return top >= viewTop(w) && bottom <= viewBottom(w);
	}

	/** Move a tall window up (negative) or down by some phone pixels, within its ends. */
	static void scroll(Window w, int dy) {
		int min = (int) (viewBottom(w) - w.camera.screenHeight()), max = viewTop(w);
		int y = Math.max(min, Math.min(max, w.camera.y + dy)), moved = y - w.camera.y;
		w.camera.y = y;
		if (moved != 0) moveInner(w, moved);
	}

	/** Lists inside a window have cameras of their own, placed when laid out: they move with it. */
	private static void moveInner(Group g, int dy) {
		for (com.watabou.noosa.Gizmo m : com.watabou.noosa.GroupAccess.members(g)) {
			if (m instanceof com.shatteredpixel.shatteredpixeldungeon.ui.ScrollPane) {
				Camera c = ((com.shatteredpixel.shatteredpixeldungeon.ui.ScrollPane) m).content().camera;
				if (c != null) c.y += dy;
			}
			if (m instanceof Group) moveInner((Group) m, dy);
		}
	}

	/** Keep a tall window inside its ends and its framed button on screen. */
	private static void keepInView(Window w, Component c) {
		if (!tall(w)) return;
		scroll(w, 0);
		if (c != null && !visible(w, c)) {
			float top = w.camera.y + (c.top() - w.camera.scroll.y) * w.camera.zoom, bottom = w.camera.y + (c.bottom() - w.camera.scroll.y) * w.camera.zoom;
			int vt = viewTop(w), vb = viewBottom(w);
			if (bottom > vb && top > vt) scroll(w, (int) -Math.min(top - vt, bottom - vb));
			else if (top < vt && bottom < vb) scroll(w, (int) Math.min(vt - top, vb - bottom));
		}
	}

	/** Pressable things in reading order (top to bottom, then left to right). */
	static List<Ui.Part> buttons(Group g) {
		List<Ui.Part> out = new ArrayList<>();
		// (a screen's exit button is left out: Back does that, and Select shouldn't start on it)
		for (Ui.Part p : Ui.parts(g)) if (p.button && p.g instanceof Component && !(p.g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.ExitButton) && !(p.g instanceof CloseTag)) out.add(p);
		out.sort((a, c) -> {
			Component x = (Component) a.g, y = (Component) c.g;
			if (Math.abs(x.top() - y.top()) > 4) return Float.compare(x.top(), y.top());
			return Float.compare(x.left(), y.left());
		});
		return out;
	}

	// ---- the dungeon ----

	private void dungeonButton(int b, boolean held) {
		boolean aiming = aiming();
		if (onToolbar && hud != null) {
			// Back walks a highlight round the buttons: the toolbar left to right, the menu, the portrait, then none;
			// Select presses it (held: a long press), Up goes back one, Down drops it (as Pebble PD)
			switch (b) {
				case BACK: hud.highlight++; if (hud.highlight > pebxel.ui.WatchHud.B_PORTRAIT) { hud.highlight = -1; onToolbar = false; } return;
				case UP: if (hud.highlight > 0) hud.highlight--; return;
				case DOWN: hud.highlight = -1; onToolbar = false; return;
				case SELECT: { int k = hud.highlight; hud.highlight = -1; onToolbar = false; hudPress(k, held); return; }
			}
		}
		switch (b) {
			case DOWN: {
				refreshTargets(aiming);
				if (targets.isEmpty()) {
					// Pebble PD: with nothing to offer (assist off, no enemy), Down goes to the toolbar
					if (!aiming && WatchSettings.assist() == 0 && hud != null) { onToolbar = true; hud.highlight = 0; }
					return;
				}
				target = held ? (target + targets.size() - 1) % targets.size() : (target + 1) % targets.size();
				if (!aiming) farView(cellOf(targets.get(target)), true);
				keepEnemy();
				return;
			}
			case SELECT:
				if (held && !aiming) { GameScene.show(new WndBag(Dungeon.hero.belongings.backpack)); return; }
				refreshTargets(aiming);
				if (targets.isEmpty()) {
					if (!aiming) key(SPDAction.WAIT_OR_PICKUP);
					return;
				}
				int i = Math.min(target, targets.size() - 1), cell = targets.get(i);
				Assist.Target t = current();
				farView(-1, false);
				if (t != null) select(t, aiming);
				else if (cell == -2) { Dungeon.hero.search(true); }   // (a test's bare cells)
				else if (shot(cell)) shoot(cellOf(cell));
				else GameScene.handleCell(cell);
				targetsStale = true; acted();
				return;
			case UP:
				if (held) { GameScene.show(new WndGame()); return; }
				zoom();
				return;
			case BACK:
				if (aiming) { GameScene.cancelCellSelector(); targetsStale = true; return; }
				onToolbar = true; focus = 0;
				if (hud != null) hud.highlight = 0;
				return;
		}
	}

	/** Is a spell, throw or wand asking for a cell? (GameScene keeps its cell selector private) */
	static boolean aiming() {
		try {
			if (selF == null) {
				selF = GameScene.class.getDeclaredField("cellSelector"); selF.setAccessible(true);
				defF = GameScene.class.getDeclaredField("defaultCellListener"); defF.setAccessible(true);
			}
			Object sel = selF.get(null);
			if (sel == null) return false;
			Object l = ((com.shatteredpixel.shatteredpixeldungeon.scenes.CellSelector) sel).listener;
			return l != null && l != defF.get(null);
		} catch (Exception e) { return false; }
	}
	private static java.lang.reflect.Field selF, defF;

	/** Shattered's own key actions (it supports keyboards): press and release. */
	private static void key(GameAction a) {
		int k = KeyBindings.getFirstKeyForAction(a, false);
		if (k == 0) return;
		KeyEvent.addKeyEvent(new KeyEvent(k, true));
		KeyEvent.addKeyEvent(new KeyEvent(k, false));
	}

	/** Zoom steps as Pebble PD: 16 px tiles, 32, 8, and back to 16. */
	private void zoom() {
		Camera c = Camera.main;
		if (c == null) return;
		int z = Math.round(c.zoom), to = z < 2 ? 2 : z < 4 ? 4 : 1;
		to = Math.max(Math.round(PixelScene.minZoom), Math.min(Math.round(PixelScene.maxZoom), to));
		for (int i = z; i < to; i++) key(SPDAction.ZOOM_IN);
		for (int i = z; i > to; i--) key(SPDAction.ZOOM_OUT);
	}


	/** The toolbar and status buttons, left to right, then the top bar. */
	private List<Ui.Part> toolbar() {
		List<Ui.Part> out = new ArrayList<>();
		for (Ui.Part p : Ui.parts(Game.scene())) if (p.button && p.g instanceof Component && ((Component) p.g).visible) out.add(p);
		out.sort((a, c) -> {
			Component x = (Component) a.g, y = (Component) c.g;
			// bottom row first (the toolbar), then the rest top to bottom
			boolean bx = x.top() > 150, by = y.top() > 150;
			if (bx != by) return bx ? -1 : 1;
			if (Math.abs(x.top() - y.top()) > 4) return Float.compare(x.top(), y.top());
			return Float.compare(x.left(), y.left());
		});
		return out;
	}

	// ---- targets: the Assistant's list (Assist.plan), what the player most likely wants next first ----

	/** A test's own list in place of the Assistant's (bare cells, as before it: OldTargets). Never set on the phone. */
	public static java.util.function.Function<Boolean, List<Integer>> legacy;

	/** The list, made again once a turn: when game time has passed, the hero moved, a floor, aiming started or ended,
	 *  the settings changed, or a press may have changed something (targetsStale). Assist.plan costs about half a
	 *  millisecond; every frame would be 15 ms a second for nothing. */
	private void refreshTargets(boolean aiming) {
		// (aiming started or ended since: the other list. A zap or throw chosen in the item window kept the walking
		// list, so Down offered items and the way on, not the enemies, until the hero moved)
		float now = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now();
		int settings = WatchSettings.assist() * 2 + (WatchSettings.itemAssist() ? 1 : 0);
		if (waiting(now, aiming)) return;
		if (!targetsStale && Dungeon.hero.pos == lastPos && Dungeon.depth == lastDepth && aiming == lastAiming && now == lastNow && settings == lastSettings) return;
		String why = log ? (targetsStale ? "stale " : "") + (Dungeon.hero.pos != lastPos ? "pos " : "") + (now != lastNow ? "time " + lastNow + "->" + now + " " : "") + (aiming != lastAiming ? "aim " : "") : "";
		targetsStale = false; lastPos = Dungeon.hero.pos; lastDepth = Dungeon.depth; lastAiming = aiming; lastNow = now; lastSettings = settings;
		plans++;
		if (legacy != null) { targets = legacy.apply(aiming); plan = new ArrayList<>(); target = 0; farView(-1, false); return; }
		plan = new ArrayList<>(Assist.plan(aiming, Controls::onScreenFromHero));
		if (Assist.lastError != null) { System.out.println("pebxel: assist " + Assist.lastError); Assist.lastError = null; }
		if (!aiming) { shots(plan); if (WatchSettings.assist() != 0) { veins(plan); talents(plan); } }   // (Off: the enemies only)
		targets = new ArrayList<>();
		for (Assist.Target t : plan) targets.add(aiming ? t.cell : markCell(t));
		target = 0;   // (Pebble PD 0.40: after each step the first one, what a good player would do next)
		if (WatchSettings.assist() == 0 && keptId >= 0)   // (Off: the enemy chosen stays chosen, as PD's last target)
			for (int j = 0; j < plan.size(); j++) { Char c = plan.get(j).kind == Assist.ENEMY ? com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(plan.get(j).cell) : null; if (c != null && c.id() == keptId) { target = j; break; } }
		keepEnemy();
		// Far: the camera shows plan A wherever it is (Pebble PD 0.76: on whatever is chosen, cycled to or not); else back on the hero
		farView(aiming || targets.isEmpty() ? -1 : targets.get(0), false);
		if (log) System.out.println("pebxel: plan d" + Dungeon.depth + " t" + Math.round(now) + " hero " + Dungeon.hero.pos + (aiming ? " aiming" : "") + " (" + why.trim() + "): " + plan);
	}
	/** A press acted: no new list until the game has taken it (game time moved, the hero moved, a window or the aim
	 *  came up) or a few frames went by with nothing (it came to nothing: then the Assistant hears so). Shattered's
	 *  actor thread takes a tap's walk up a frame or two later; till then the hero still reads as ready, and a list
	 *  made then was made on a turn already over (and counted the Assistant's waits twice, read its "mind" against
	 *  itself...), while the actor thread moved the hero under it. */
	private boolean pending; private int pendingFrames, pendingPos; private float pendingNow; private boolean pendingAim;
	private void acted() {
		if (Dungeon.hero == null) return;
		pending = true; pendingFrames = 0; pendingPos = Dungeon.hero.pos; pendingNow = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now(); pendingAim = aiming();
	}
	private boolean waiting(float now, boolean aiming) {
		if (!pending) return false;
		Hero h = Dungeon.hero;
		if (now != pendingNow || h.pos != pendingPos || aiming != pendingAim || !h.ready || Ui.topWindow() != null || pendingFrames > PENDING_FRAMES) pending = false;
		return pending;
	}
	static final int PENDING_FRAMES = 10;
	/** Lists made (tests: once a turn, never a frame). */
	public static long plans;
	/** Tests: print each list made. */
	public static boolean log;
	private int keptId = -1;
	private void keepEnemy() {
		Assist.Target t = current();
		Char c = t != null && t.kind == Assist.ENEMY ? com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(t.cell) : null;
		keptId = c == null ? -1 : c.id();
	}

	/** Select on an entry: the Assistant hears the choice (a different one than plan A: that way, and that objective,
	 *  are followed from now on), then does it. A window it opens is left to the player: the backpack to choose what to
	 *  identify (on the Assistant's pick, if it had one), the talents, the subclass, a shop's buy window. */
	private void select(Assist.Target t, boolean aiming) {
		Assist.Target a = plan.isEmpty() ? null : plan.get(0);
		if (!aiming && a != null) {
			if (a != t && TALENTS.equals(a.action)) talentsPassed = talentPoints();
			Assist.chose(t, a);
		}
		if (TALENTS.equals(t.action)) { pebxel.ui.WndWatchHero.openTalents(); return; }
		Assist.act(t, true);
		if (t.on != null) pick = t.on;
	}
	private com.shatteredpixel.shatteredpixeldungeon.items.Item pick;   // the bag that opened: the frame on this

	/** The Huntress's bow: a shot at each enemy in the list an arrow would reach (before the blow when it's further
	 *  than a step, after it when beside the hero), unless the Assistant offers that shot already. */
	private static void shots(List<Assist.Target> ts) {
		com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow b = bow();
		Hero h = Dungeon.hero;
		if (b == null || h == null) return;
		for (int i = 0; i < ts.size() && ts.size() < PLAN_TARGETS; i++) {
			Assist.Target e = ts.get(i);
			if (e.kind != Assist.ENEMY || e.action != null) continue;
			boolean have = false;
			for (Assist.Target o : ts) have |= o.kind == Assist.USE && o.item == b && o.cell == e.cell;
			if (have || new com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica(h.pos, e.cell, com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica.PROJECTILE).collisionPos != e.cell) continue;
			Assist.Target s = new Assist.Target(e.cell, Assist.USE, "shoot");
			s.item = b; s.action = com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow.AC_SHOOT; s.obj = -1;
			if (Dungeon.level.adjacent(h.pos, e.cell)) ts.add(i + 1, s); else ts.add(i, s);
			i++;
		}
	}

	/** The Blacksmith's mine: the dark gold veins known, nearest first, a few (Select: the pickaxe walks there and
	 *  mines; the Assistant never offers a wall). */
	private static void veins(List<Assist.Target> ts) {
		Level L = Dungeon.level; Hero h = Dungeon.hero;
		if (!(L instanceof com.shatteredpixel.shatteredpixeldungeon.levels.MiningLevel) || h.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.quest.Pickaxe.class) == null) return;
		int[] dist = walk(L, h.pos);
		List<int[]> vs = new ArrayList<>();
		for (int c = 0; c < L.length(); c++) {
			if (L.map[c] != com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.WALL_DECO || !(L.visited[c] || L.mapped[c] || L.heroFOV[c])) continue;
			int best = 0;   // (reached from the nearest floor beside it)
			for (int k : PathFinder.NEIGHBOURS8) { int n = c + k; if (n >= 0 && n < dist.length && dist[n] > 0 && (best == 0 || dist[n] < best)) best = dist[n]; }
			if (best > 0) vs.add(new int[]{c, best});
		}
		vs.sort(Comparator.comparingInt(v -> v[1]));
		boolean fight = false; for (Assist.Target t : ts) fight |= t.kind == Assist.ENEMY;
		for (int i = 0; i < Math.min(3, vs.size()) && ts.size() < PLAN_TARGETS; i++) {
			Assist.Target v = new Assist.Target(vs.get(i)[0], Assist.GO, "gold vein");
			if (fight) ts.add(v); else ts.add(Math.min(i, ts.size()), v);   // (no fight: the mining is what the floor is for)
		}
	}

	/** Talent points to spend, nothing in sight, item assist on: the talents first (one press, the choice the player's),
	 *  until passed over for these points. */
	static final String TALENTS = "TALENTS";
	private int talentsPassed = -1;
	private static int talentPoints() { Hero h = Dungeon.hero; int n = 0; for (int t = 1; t <= 4; t++) n += Math.max(0, h.talentPointsAvailable(t)); return n; }
	private void talents(List<Assist.Target> ts) {
		if (WatchSettings.assist() == 0 || !WatchSettings.itemAssist()) return;
		int n = talentPoints();
		if (n <= 0 || n == talentsPassed) { if (n <= 0) talentsPassed = -1; return; }
		for (Assist.Target t : ts) if (t.kind == Assist.ENEMY || (t.kind == Assist.USE && t.item != null)) return;   // (a fight, or the hands have something: later)
		for (Mob m : Dungeon.level.mobs) if (Dungeon.level.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) return;
		Assist.Target t = new Assist.Target(Dungeon.hero.pos, Assist.USE, "talents"); t.action = TALENTS; t.obj = -1;
		ts.add(0, t);
		if (ts.size() > PLAN_TARGETS) ts.remove(ts.size() - 1);
	}

	// ---- the assistant's reach (Settings): Near keeps to the screen, Far looks anywhere, Off offers only enemies ----

	/** Is the cell well inside what the watch shows (clear of the HUD; round: inside the circle's middle)? */
	static boolean onScreen(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0) return true;
		com.watabou.utils.PointF p = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.tileCenterToWorld(cell);
		float sx = (c.x + (p.x - c.scroll.x) * c.zoom) / 2f, sy = (c.y + (p.y - c.scroll.y) * c.zoom) / 2f;   // watch pixels
		return inView(sx, sy);
	}
	private static boolean inView(float sx, float sy) {
		if (Screen.round) { float dx = sx - Screen.w / 2f, dy = sy - Screen.h / 2f; return dx * dx + dy * dy <= 95 * 95; }
		return sx >= 8 && sx <= Screen.w - 26 && sy >= 38 && sy <= Screen.h - 42;
	}
	/** As onScreen, with the camera where it comes to rest: on the hero (the list is made as a walk ends, while the
	 *  camera is still catching him up; Far's swoop may have it elsewhere). */
	static boolean onScreenFromHero(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0 || Dungeon.hero == null || Dungeon.level == null) return true;
		int w = Dungeon.level.width(), h = Dungeon.hero.pos;
		float t = DungeonTilemap.SIZE * c.zoom / 2f;   // a cell in watch pixels
		return inView(Screen.w / 2f + (cell % w - h % w) * t, Screen.h / 2f + (cell / w - h / w) * t);
	}

	/** Far: a goal off the screen brings the camera over to show it and the hero (on a Down, zooming out a step if they
	 *  won't both fit); -1, or a goal on the screen, gives the camera back to the hero (and the zoom it had). */
	private int farZoomFrom = -1;
	private void farView(int cell, boolean zoomOut) {
		Camera c = Camera.main;
		if (c == null || Dungeon.hero == null || Dungeon.hero.sprite == null) return;
		if (cell < 0 || WatchSettings.assist() != 2 || onScreenFromHero(cell)) {
			if (c.followTarget() != Dungeon.hero.sprite) c.panFollow(Dungeon.hero.sprite, 5f);
			if (farZoomFrom > 0) { int z = Math.round(c.zoom); for (int i = z; i < farZoomFrom; i++) key(SPDAction.ZOOM_IN); farZoomFrom = -1; }
			return;
		}
		com.watabou.utils.PointF hp = Dungeon.hero.sprite.center(), tp = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.tileCenterToWorld(cell);
		// do they fit in the middle of the view at this zoom? (world units: two phone pixels a watch pixel)
		float span = Math.max(Math.abs(hp.x - tp.x), Math.abs(hp.y - tp.y)) * c.zoom / 2f;
		if (zoomOut && span > (Screen.round ? 150 : 130) && Math.round(c.zoom) > Math.round(PixelScene.minZoom)) {
			if (farZoomFrom < 0) farZoomFrom = Math.round(c.zoom);
			key(SPDAction.ZOOM_OUT);
		}
		c.panTo(new com.watabou.utils.PointF((hp.x + tp.x) / 2, (hp.y + tp.y) / 2), 5f);
	}

	/** Walking distance from the hero over known, walkable cells (0 = unreachable, the hero's cell = 1). */
	static int[] walk(Level L, int from) {
		int[] d = new int[L.length()];
		ArrayDeque<Integer> q = new ArrayDeque<>(); q.add(from); d[from] = 1;
		while (!q.isEmpty()) {
			int c = q.poll();
			for (int k : PathFinder.NEIGHBOURS8) {
				int n = c + k;
				if (n < 0 || n >= d.length || d[n] != 0) continue;
				if (!(L.visited[n] || L.mapped[n]) || !(L.passable[n] || L.avoid[n])) continue;
				d[n] = d[c] + 1; q.add(n);
			}
		}
		return d;
	}

	// ---- every frame: keep the frame where it belongs ----

	public void frame() {
		if (pending) pendingFrames++;
		keepHud();
		Window w = Ui.topWindow();
		closeTag(w);
		if (pick != null) {   // the backpack a use opened to choose in (what to identify...): the frame on the Assistant's pick
			if (w instanceof pebxel.ui.WndWatchBag) { ((pebxel.ui.WndWatchBag) w).focusOn(pick); pick = null; }
			else if (w == null && Dungeon.hero != null && Dungeon.hero.ready) pick = null;
		}
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (touching) { frame.hide(); if (menu != null) { marker.hide(); return; } }
		else if (menu != null) {
			onToolbar = false;
			if (hud != null) hud.highlight = -1;
			marker.hide();
			List<Ui.Part> bs = buttons(menu);
			if (focusOwner != menu) {
				focusOwner = menu; focus = 0;
				if (w != null && tall(w)) scroll(w, 100000);   // a new tall window starts at its top
			}
			if (bs.isEmpty()) { frame.hide(); if (w != null) keepInView(w, null); return; }
			Component c = (Component) bs.get(Math.max(0, Math.min(bs.size() - 1, focus))).g;
			if (menu instanceof pebxel.ui.Navigable && ((pebxel.ui.Navigable) menu).focused() != null) c = ((pebxel.ui.Navigable) menu).focused();
			// (a tall window opens at its top for reading, even when its first button is further down)
			if (w != null && keptFor != w) { keptFor = w; keepInView(w, tall(w) ? null : c); }
			for (Ui.Part p : bs) if (p.g instanceof pebxel.ui.Focusable) ((pebxel.ui.Focusable) p.g).focus(p.g == c);
			if (c instanceof pebxel.ui.Focusable) { frame.hide(); return; }
			if (menu instanceof pebxel.ui.WatchWindow) frame.around(menu, c.left(), c.top(), c.width(), c.height(), 0xFFFFAA00, 2);   // Pebble PD: a 2 px yellow box
			else frame.around(menu, c.left() - 1, c.top() - 1, c.width() + 2, c.height() + 2, 0xFFFFFF44, 1);
			return;
		}
		frame.hide();
		if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) { marker.hide(); return; }
		if (onToolbar || !Dungeon.hero.ready) { marker.hide(); return; }   // (the toolbar's highlight is drawn by the HUD)
		refreshTargets(aiming());
		int cell = targets.isEmpty() || pending ? -1 : cellOf(targets.get(Math.min(target, targets.size() - 1)));
		if (cell < 0) { marker.hide(); return; }   // (an action of the toolbar's: its button's reticle shows it, WatchHud)
		marker.at(cell);
	}

	// ---- what the watch may do by itself before hearing back (its predict.c) ----

	static final int PLAN_TARGETS = 12, PLAN_WALKS = 6, PLAN_STEPS = 32, STEP_MS = Math.round(WatchGame.STEP * 1000);   // (CharSprite's move a cell, game ms)

	/**
	 * The plan for the watch's next press, so the target box and the hero answer at once rather than a radio
	 * round trip later (the phone's own frames then confirm it, or the watch puts things back):
	 * 0 = nothing to plan; else 1, marker id u16, hero id u16 (0xFFFF none), step ms / 4, n, current target, then per
	 * target: marker x, y (s16, x8, 0x7FFF: none), steps k, k directions ((dx + 1) + 3 (dy + 1)).
	 */
	public byte[] plan(Link link) {
		Hero h = Dungeon.hero;
		// (the marker hidden: plan A is an action of the toolbar's; a Down to a cell still moves the marker there)
		if (!(Game.scene() instanceof GameScene) || h == null || !h.isAlive() || !h.ready || Ui.topWindow() != null
				|| onToolbar || touching || targets.isEmpty() || marker.img == null || marker.img.parent == null) return new byte[]{0};
		int mid = link.idOf(marker.img), hid = link.idOf(h.sprite);
		if (mid < 0) return new byte[]{0};
		boolean aiming = aiming();
		java.io.ByteArrayOutputStream b = new java.io.ByteArrayOutputStream();
		int n = Math.min(targets.size(), PLAN_TARGETS);
		b.write(1); w16(b, mid); w16(b, hid < 0 ? 0xFFFF : hid); b.write(STEP_MS / 4); b.write(n); b.write(Math.min(target, n - 1));
		for (int i = 0; i < n; i++) {
			int cell = cellOf(targets.get(i));
			if (cell < 0) { w16(b, 0x7FFF); w16(b, 0x7FFF); b.write(0); continue; }
			float[] xy = Marker.place(cell);
			w16(b, Math.round(xy[0] * 8)); w16(b, Math.round(xy[1] * 8));
			Assist.Target t = i < plan.size() && plan.size() == targets.size() ? plan.get(i) : null;
			boolean walks = t == null || t.kind == Assist.GO || t.kind == Assist.PICKUP || (t.kind == Assist.ENEMY && t.action == null);   // (an item's use, a wait: no walk)
			List<Integer> path = aiming || hid < 0 || i >= PLAN_WALKS || !walks ? null : walk(h, cell);
			int k = path == null ? 0 : Math.min(PLAN_STEPS, path.size());
			b.write(k);
			int from = h.pos, w = Dungeon.level.width();
			for (int j = 0; j < k; j++) {
				int to = path.get(j), dx = to % w - from % w, dy = to / w - from / w;
				if (Math.abs(dx) > 1 || Math.abs(dy) > 1) { k = j; break; }   // (not a step: stop the plan there)
				b.write((dx + 1) + 3 * (dy + 1)); from = to;
			}
		}
		return b.toByteArray();
	}

	private static void w16(java.io.ByteArrayOutputStream b, int v) { b.write(v & 255); b.write((v >> 8) & 255); }

	/** The cells Select would walk the hero through to a cell (as Hero.getCloser finds them), or null: not a walk. */
	private static List<Integer> walk(Hero h, int cell) {
		Level lv = Dungeon.level;
		if (cell == h.pos || com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(cell) != null || h.rooted) return null;
		if (lv.adjacent(h.pos, cell)) return lv.passable[cell] || lv.avoid[cell] ? Collections.singletonList(cell) : null;
		int len = lv.length();
		boolean[] pass = new boolean[len];
		for (int i = 0; i < len; i++) pass[i] = lv.passable[i] && (lv.visited[i] || lv.mapped[i]);
		PathFinder.Path p = Dungeon.findPath(h, cell, pass, h.fieldOfView, true);
		return p == null || p.isEmpty() ? null : new ArrayList<>(p);
	}

	// ---- the chosen cell: Icons.TARGET over it, as in Pebble PD (never smaller than at 16 px tiles) ----
	private final Marker marker = new Marker();
	private static class Marker {
		private Image img;
		void at(int cell) {
			if (img == null || img.parent == null || !Frame.inScene(img)) {
				img = pebxel.ui.PdArt.image("icons", 67, 0, 16, 16, 1);
				Link.JUMPS.add(img);
				GameScene.effect(img);
			}
			float z = Camera.main == null ? 2 : Camera.main.zoom, s = z < 2 ? 2 / z : 1;
			img.scale.set(s);
			float[] xy = place(cell);
			img.x = xy[0]; img.y = xy[1];
			img.visible = true;
		}
		/** Where the marker stands over a cell (its top left, in the map's units). */
		static float[] place(int cell) {
			float z = Camera.main == null ? 2 : Camera.main.zoom, s = z < 2 ? 2 / z : 1;
			return new float[]{(cell % Dungeon.level.width()) * DungeonTilemap.SIZE - (16 * s - 16) / 2,
					(cell / Dungeon.level.width()) * DungeonTilemap.SIZE - (16 * s - 16) / 2};
		}
		void hide() { if (img != null) img.visible = false; }
	}


	private Window keptFor;
	private Group overlay;

	/** A group on top of the scene, seen through the interface camera (for frames around toolbar buttons). */
	private Group overlay() {
		if (overlay == null || overlay.parent != Game.scene()) {
			overlay = new Group();
			overlay.camera = PixelScene.uiCamera;
			Game.scene().add(overlay);
		}
		Game.scene().bringToFront(overlay);
		return overlay;
	}

	/** Four thin bars around a rectangle: in a window or menu (its own camera), or around a map cell. */
	private static class Frame {
		private final ColorBlock[] bar = new ColorBlock[4];
		private final int[] colour = new int[4];

		void around(Group owner, float x, float y, float w, float h, int color, float t) {
			for (int i = 0; i < 4; i++) {
				if (bar[i] == null || bar[i].parent != owner || colour[i] != color) {
					if (bar[i] != null) bar[i].killAndErase();
					bar[i] = new ColorBlock(1, 1, color); colour[i] = color;
					owner.add(bar[i]);
				}
				bar[i].visible = true;
				owner.bringToFront(bar[i]);
			}
			place(x, y, w, h, t);
		}

		void inWorld(float x, float y, float w, float h, int color) {
			for (int i = 0; i < 4; i++) {
				if (bar[i] == null || bar[i].parent == null || !inScene(bar[i]) || colour[i] != color || bar[i].camera() != Camera.main) {
					if (bar[i] != null) bar[i].killAndErase();
					bar[i] = new ColorBlock(1, 1, color); colour[i] = color;
					GameScene.effect(bar[i]);
				}
				bar[i].visible = true;
			}
			place(x, y, w, h, 1);
		}

		private void place(float x, float y, float w, float h, float t) {
			set(bar[0], x, y, w, t); set(bar[1], x, y + h - t, w, t);
			set(bar[2], x, y, t, h); set(bar[3], x + w - t, y, t, h);
		}

		private static void set(ColorBlock b, float x, float y, float w, float h) { b.x = x; b.y = y; b.size(w, h); }

		static boolean inScene(com.watabou.noosa.Gizmo g) {
			for (Group p = g.parent; p != null; p = p.parent) if (p == Game.scene()) return true;
			return false;
		}

		void hide() { for (ColorBlock b : bar) if (b != null) b.visible = false; }
	}
}
