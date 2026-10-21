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
	private List<Integer> targets = new ArrayList<>();
	private int target;
	private boolean targetsStale = true;
	private int lastPos = -1, lastDepth = -1;

	// ---- Pebble PD's screen furniture (pebxel.ui.WatchHud) ----
	private pebxel.ui.WatchHud hud;

	private final pebxel.ui.WatchHud.Hooks hooks = new pebxel.ui.WatchHud.Hooks() {
		public void press(int k, boolean held) { hudPress(k, held); }
		public int targetCell() { return currentTarget(); }
		public int targetKind() {
			int c = currentTarget();
			if (c < 0 || Dungeon.level == null) return 0;
			if (shot(targets.get(Math.min(target, targets.size() - 1)))) return 4;   // the bow at an enemy
			Char ch = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(c);
			if (ch instanceof Mob && ((Mob) ch).alignment == Char.Alignment.ENEMY) return 1;
			if (Dungeon.level.heaps.get(c) != null) return 2;
			return c == Dungeon.hero.pos ? 0 : 3;
		}
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

	/** The cell Select acts on (-1 none). */
	private int currentTarget() {
		if (Dungeon.hero == null || !Dungeon.hero.ready || targets.isEmpty()) return -1;
		int c = cellOf(targets.get(Math.min(target, targets.size() - 1)));
		return c < 0 ? -1 : c;
	}

	// A target is a cell; SHOT marks the Huntress's spirit bow aimed at the enemy there (a shot rather than a melee attack)
	static final int SHOT = 1 << 20;
	static boolean shot(int t) { return t >= 0 && (t & SHOT) != 0; }
	static int cellOf(int t) { return t < 0 ? t : t & (SHOT - 1); }
	private static com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow bow() {
		Hero h = Dungeon.hero; return h == null ? null : h.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow.class);
	}
	/** The bow, drawn and loosed at a cell (as its Shoot action, then a tap on the cell). */
	private static void shoot(int cell) {
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
		targetsStale = true;
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

	/** Is the component fully on screen (window camera units -> phone pixels)? */
	private static boolean visible(Window w, Component c) {
		float top = w.camera.y + c.top() * w.camera.zoom, bottom = w.camera.y + c.bottom() * w.camera.zoom;
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
			float top = w.camera.y + c.top() * w.camera.zoom, bottom = w.camera.y + c.bottom() * w.camera.zoom;
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
				if (!aiming) farView(cellOf(targets.get(target)));
				return;
			}
			case SELECT:
				if (held && !aiming) { GameScene.show(new WndBag(Dungeon.hero.belongings.backpack)); return; }
				refreshTargets(aiming);
				if (targets.isEmpty()) {
					if (!aiming) key(SPDAction.WAIT_OR_PICKUP);
					return;
				}
				int cell = targets.get(Math.min(target, targets.size() - 1));
				farView(-1);
				if (cell == -2) { Dungeon.hero.search(true); }
				else if (shot(cell)) shoot(cellOf(cell));
				else GameScene.handleCell(cell);
				targetsStale = true;
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

	// ---- targets: what the player most likely wants next, first ----

	private void refreshTargets(boolean aiming) {
		if (!targetsStale && Dungeon.hero.pos == lastPos && Dungeon.depth == lastDepth) return;
		targetsStale = false; lastPos = Dungeon.hero.pos; lastDepth = Dungeon.depth;
		targets = aiming ? aimTargets() : reach(walkTargets());
		target = 0;
		farView(-1);   // (a new turn: the camera goes back to the hero)
	}

	// ---- the assistant's reach (Settings): Near keeps to the screen, Far looks anywhere, Off offers only enemies ----

	/** Is the cell well inside what the watch shows (clear of the HUD; round: inside the circle's middle)? */
	static boolean onScreen(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0) return true;
		com.watabou.utils.PointF p = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.tileCenterToWorld(cell);
		float sx = (c.x + (p.x - c.scroll.x) * c.zoom) / 2f, sy = (c.y + (p.y - c.scroll.y) * c.zoom) / 2f;   // watch pixels
		if (Screen.round) { float dx = sx - Screen.w / 2f, dy = sy - Screen.h / 2f; return dx * dx + dy * dy <= 95 * 95; }
		return sx >= 8 && sx <= Screen.w - 26 && sy >= 38 && sy <= Screen.h - 42;
	}

	private List<Integer> reach(List<Integer> ts) {
		int mode = WatchSettings.assist();
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		if (mode == 0) {
			// off: the enemies in sight, nothing else
			for (int c : ts) if (c >= 0 && com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(cellOf(c)) instanceof Mob && ((Mob) com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(cellOf(c))).alignment == Char.Alignment.ENEMY) out.add(c);
			return out;
		}
		if (mode == 2) return ts;
		// near: a goal off the screen becomes the farthest step towards it that's on it
		int[] dist = walk(L, h);
		for (int c : ts) {
			int t = c;
			if (shot(t)) { if (!out.contains(t)) out.add(t); continue; }   // (a shot: at an enemy in sight)
			if (t >= 0 && !onScreen(t)) {
				if (dist[t] <= 0) continue;
				int guard = 0;
				while (t != h && !onScreen(t) && guard++ < 400) {
					int next = -1;
					for (int k : PathFinder.NEIGHBOURS8) { int n = t + k; if (n >= 0 && n < dist.length && dist[n] > 0 && dist[n] == dist[t] - 1) { next = n; break; } }
					if (next < 0) break;
					t = next;
				}
				if (t == h || !onScreen(t)) continue;
			}
			if (!out.contains(t)) out.add(t);
		}
		return out;
	}

	/** Far: a chosen goal off the screen brings the camera over to show it and the hero (zooming out a step if they
	 *  won't both fit); -1, or a goal on the screen, gives the camera back to the hero (and the zoom it had). */
	private int farZoomFrom = -1;
	private void farView(int cell) {
		Camera c = Camera.main;
		if (c == null || Dungeon.hero == null || Dungeon.hero.sprite == null) return;
		if (cell < 0 || WatchSettings.assist() != 2 || onScreen(cell)) {
			if (c.followTarget() != Dungeon.hero.sprite) c.panFollow(Dungeon.hero.sprite, 5f);
			if (farZoomFrom > 0) { int z = Math.round(c.zoom); for (int i = z; i < farZoomFrom; i++) key(SPDAction.ZOOM_IN); farZoomFrom = -1; }
			return;
		}
		com.watabou.utils.PointF hp = Dungeon.hero.sprite.center(), tp = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.tileCenterToWorld(cell);
		// do they fit in the middle of the view at this zoom? (world units: two phone pixels a watch pixel)
		float span = Math.max(Math.abs(hp.x - tp.x), Math.abs(hp.y - tp.y)) * c.zoom / 2f;
		if (span > (Screen.round ? 150 : 130) && Math.round(c.zoom) > Math.round(PixelScene.minZoom)) {
			if (farZoomFrom < 0) farZoomFrom = Math.round(c.zoom);
			key(SPDAction.ZOOM_OUT);
		}
		c.panTo(new com.watabou.utils.PointF((hp.x + tp.x) / 2, (hp.y + tp.y) / 2), 5f);
	}

	private static List<Integer> aimTargets() {
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		List<Mob> ms = new ArrayList<>();
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) ms.add(m);
		ms.sort(Comparator.comparingInt(m -> L.distance(h, m.pos)));
		for (Mob m : ms) out.add(m.pos);
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment != Char.Alignment.ENEMY && !out.contains(m.pos)) out.add(m.pos);
		for (Heap hp : L.heaps.valueList()) if (L.heroFOV[hp.pos] && !out.contains(hp.pos)) out.add(hp.pos);
		if (!out.contains(h)) out.add(h);
		return out;
	}

	private static List<Integer> walkTargets() {
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		// enemies in sight, nearest first
		List<Mob> ms = new ArrayList<>();
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) ms.add(m);
		ms.sort(Comparator.comparingInt(m -> L.distance(h, m.pos)));
		// the Huntress's bow: a shot at each one an arrow would reach (first when it's further than a step, after the
		// melee attack when it's beside the hero)
		boolean bow = bow() != null;
		for (Mob m : ms) {
			boolean reach = bow && new com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica(h, m.pos, com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica.PROJECTILE).collisionPos == m.pos;
			boolean near = L.adjacent(h, m.pos);
			if (reach && !near) out.add(m.pos | SHOT);
			out.add(m.pos);
			if (reach && near) out.add(m.pos | SHOT);
		}
		// an item under you, then known items by walking distance
		if (L.heaps.get(h) != null) out.add(h);
		int[] dist = walk(L, h);
		List<Integer> items = new ArrayList<>();
		for (Heap hp : L.heaps.valueList()) if (hp.pos != h && dist[hp.pos] > 0 && (L.visited[hp.pos] || L.heroFOV[hp.pos])) items.add(hp.pos);
		items.sort(Comparator.comparingInt(c -> dist[c]));
		out.addAll(items);
		// the Blacksmith's mine: the dark gold veins known, nearest first (Select digs one: the pickaxe walks there and
		// mines), a few; nothing offered them before, so on the buttons the quest couldn't be done
		boolean mine = L instanceof com.shatteredpixel.shatteredpixeldungeon.levels.MiningLevel;
		if (mine && Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.quest.Pickaxe.class) != null) {
			List<int[]> veins = new ArrayList<>();
			for (int c = 0; c < L.length(); c++) {
				if (L.map[c] != com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.WALL_DECO || !(L.visited[c] || L.mapped[c] || L.heroFOV[c])) continue;
				int best = 0;   // (reached from the nearest floor beside it)
				for (int k : PathFinder.NEIGHBOURS8) { int n = c + k; if (n >= 0 && n < dist.length && dist[n] > 0 && (best == 0 || dist[n] < best)) best = dist[n]; }
				if (best > 0) veins.add(new int[]{c, best});
			}
			veins.sort(Comparator.comparingInt(v -> v[1]));
			for (int i = 0; i < Math.min(3, veins.size()); i++) out.add(veins.get(i)[0]);
		}
		// the nearest edge of the unexplored, then the way on: down (the mine: the ladder up, its only way out)
		int edge = Explorer.nearestUnexplored(L, h, dist);
		if (edge >= 0) out.add(edge);
		// (with the Amulet the way is up, every floor to the surface: the stairs up instead)
		boolean up = mine || Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.Amulet.class) != null;
		int exit = up ? L.entrance() : L.exit();
		if (exit >= 0 && (L.visited[exit] || L.mapped[exit]) && !out.contains(exit)) out.add(exit);
		// friends (shopkeepers, quest givers) to talk to
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment != Char.Alignment.ENEMY && !out.contains(m.pos)) out.add(m.pos);
		if (out.isEmpty()) out.add(-2);   // nothing known: search
		return out;
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
		keepHud();
		Window w = Ui.topWindow();
		closeTag(w);
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
		int cell = targets.isEmpty() ? -1 : cellOf(targets.get(Math.min(target, targets.size() - 1)));
		if (cell < 0) { marker.hide(); return; }
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
		if (!(Game.scene() instanceof GameScene) || h == null || !h.isAlive() || !h.ready || Ui.topWindow() != null
				|| onToolbar || touching || targets.isEmpty() || marker.img == null || !marker.img.visible) return new byte[]{0};
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
			List<Integer> path = aiming || hid < 0 || i >= PLAN_WALKS ? null : walk(h, cell);
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
