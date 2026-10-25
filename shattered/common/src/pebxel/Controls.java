package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.SPDAction;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroAction;
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
import com.watabou.utils.PointF;
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
	/** With the button: pressed while the watch showed the hero walking (main.c: a walk worked out ahead plays there
	 *  after it is over here). It stops the walk, as in Pebble PD: nothing more (Back and long presses do theirs too). */
	public static final int WALKING = 32;

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
		public boolean zoomLocked() { return zlock && WatchSettings.assist() == 2; }
		public void tag(int which, boolean held) {
			switch (which) {
				case 0: zoom(); break;
				case 5: { int c = currentTarget(); if (c >= 0) { zman = false; view(c, TURN); } break; }   // the target's pointer: the view to it (Far: with the hero)
				case 6: view(-1, TURN); break;   // the hero's: back on him, at the player's zoom (Pebble PD's cam_center)
				case 1: dungeonButton(SELECT, false); break;
				case 2: dungeonButton(DOWN, held); break;
				case 3: key(SPDAction.WAIT_OR_PICKUP); break;
				case 4: if (Dungeon.hero.ready) Dungeon.hero.resume(); break;   // (as Shattered's ResumeIndicator)
			}
		}
		public boolean resumeShown() {   // Pebble PD's ResumeButton: Off only
			Hero h = Dungeon.hero;
			return WatchSettings.assist() == 0 && h != null && h.isAlive() && h.ready && h.lastAction != null && !aiming();
		}
		public int aimSlot() {   // the quickslot being aimed with at an enemy (Shattered's crossB), -1 none
			int q = com.shatteredpixel.shatteredpixeldungeon.ui.QuickSlotButton.targetingSlot;
			Char t = com.shatteredpixel.shatteredpixeldungeon.ui.QuickSlotButton.lastTarget;
			return aiming() && q >= 0 && q < pebxel.ui.WatchHud.TB_Q.length && t != null && t.isAlive() ? q : -1;
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
		// Pebble PD: any button stops a walk or a rest (the step under way still ends). Up, Down and Select do nothing
		// more; Back and the long presses go on to do theirs. (A walk worked out ahead, Ahead: the phone's hero is up to
		// a few steps further on than the watch shows, and stops there; the watch walks him there and no further. If he
		// arrived here already, the press, marked WALKING by the watch, is the stop all the same)
		Hero h = Dungeon.hero; HeroAction a = h.curAction;
		boolean going = !h.ready && (h.resting || (a != null && !(a instanceof HeroAction.Attack) && a.dst != h.pos));
		if (going) { h.interrupt(); targetsStale = true; }
		if ((going || (code & WALKING) != 0) && !held && b != BACK) return;
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
		// (a spot over the window's frame moves inside it: the frame's edge is never drawn over, nor the tag cut off
		// where a tall window's frame ends are held)
		float[] in = innerRect(w);
		for (int i = 0; i < spots.length; i++) {
			int[] sp = spots[i];
			if (in != null && sp[0] < in[4] && sp[0] + 24 > in[5] && sp[1] < in[6] && sp[1] + 24 > in[7]
					&& !(sp[0] >= in[0] && sp[0] + 24 <= in[2] && sp[1] >= in[1] && sp[1] + 24 <= in[3])) {
				spots[i] = new int[]{(int) Math.max(Math.ceil(in[0]), Math.min(Math.floor(in[2]) - 24, sp[0])), (int) Math.max(Math.ceil(in[1]), Math.min(Math.floor(in[3]) - 24, sp[1]))};
			}
		}
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

	/** A window's frame on the screen (watch px): inside its edges (left, top, right, bottom; a tall one's between its
	 *  held ends), then its outer edges (right, left, bottom, top). Null: no frame picture (round: drawn by shapes). */
	public static float[] innerRect(Window w) {
		com.watabou.noosa.NinePatch ch = pebxel.ui.Caps.chrome(w);
		Camera c = w.camera;
		if (ch == null || !ch.visible || c == null) return null;
		float x0 = (c.x + (ch.x - c.scroll.x) * c.zoom) / 2, y0 = (c.y + (ch.y - c.scroll.y) * c.zoom) / 2;
		float x1 = x0 + ch.width * c.zoom / 2, y1 = y0 + ch.height * c.zoom / 2;
		float l = x0 + ch.marginLeft() * c.zoom / 2, r = x1 - ch.marginRight() * c.zoom / 2;
		float t = y0 + ch.marginTop() * c.zoom / 2, b = y1 - ch.marginBottom() * c.zoom / 2;
		if (tall(w)) { int[] a = pebxel.ui.Caps.inner(w); t = a[0] / 2f; b = a[1] / 2f; y0 = t - ch.marginTop() * c.zoom / 2; y1 = b + ch.marginBottom() * c.zoom / 2; }
		return new float[]{l, t, r, b, x1, x0, y1, y0};
	}

	/** (tests) The yellow box round the framed button: its group, and its x, y, w, h there. */
	Object[] highlight() { return frame.box(); }

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
	public static int viewTop(Window w) { return Screen.round && !(w instanceof pebxel.ui.WatchWindow) ? 72 : 0; }
	public static int viewBottom(Window w) { return Screen.round && !(w instanceof pebxel.ui.WatchWindow) ? Game.height - 72 : Game.height; }

	public static boolean tall(Window w) {
		if (w instanceof pebxel.ui.WatchWindow && ((pebxel.ui.WatchWindow) w).fixed) return false;
		return w.camera != null && w.camera.screenHeight() > viewBottom(w) - viewTop(w) + 1;
	}

	/** Is the component fully on screen (window camera units -> phone pixels; the camera starts at the frame's edge,
	 *  above the content's 0: left out, a button at a tall window's foot counted as shown while cut off)? */
	private static boolean visible(Window w, Component c) {
		float top = w.camera.y + (c.top() - w.camera.scroll.y) * w.camera.zoom, bottom = w.camera.y + (c.bottom() - w.camera.scroll.y) * w.camera.zoom;
		int[] in = area(w);
		return top >= in[0] && bottom <= in[1];
	}

	/** Where a window's buttons can be seen whole (phone px: top, bottom): a tall one's between its frame's held ends. */
	static int[] area(Window w) { return tall(w) ? pebxel.ui.Caps.inner(w) : new int[]{viewTop(w), viewBottom(w)}; }

	/** How far (phone px) a tall window would have to scroll to show the component whole. */
	private static float reach(Window w, Component c) {
		float top = w.camera.y + (c.top() - w.camera.scroll.y) * w.camera.zoom, bottom = w.camera.y + (c.bottom() - w.camera.scroll.y) * w.camera.zoom;
		int[] in = area(w);
		return Math.max(0, Math.max(bottom - in[1], in[0] - top));
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
			int[] in = area(w);
			int vt = in[0], vb = in[1];
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
			// (Item assist: in the Assistant's order, best first, hlRank; Off: Down goes on to the next too)
			boolean items = itemAssist();
			switch (b) {
				case BACK: if (items) hud.highlight = ++hlp < hlOrder.length ? hlOrder[hlp] : -1; else hud.highlight++; if (hud.highlight > pebxel.ui.WatchHud.B_PORTRAIT || hud.highlight < 0) { hud.highlight = -1; onToolbar = false; } return;
				case UP: hud.highlight = items ? (hlp > 0 ? hlOrder[--hlp] : -1) : hud.highlight - 1; if (hud.highlight < 0) onToolbar = false; return;
				case DOWN: if (WatchSettings.assist() == 0 && hud.highlight < pebxel.ui.WatchHud.B_PORTRAIT) hud.highlight++; else { hud.highlight = -1; onToolbar = false; } return;
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
				int was = Math.min(target, targets.size() - 1);
				target = held ? (target + targets.size() - 1) % targets.size() : (target + 1) % targets.size();
				// (the entry cycled past: passed over, unless it is chosen after all: select, obeyed)
				if (!aiming && was < plan.size() && plan.size() == targets.size() && !skipped.contains(plan.get(was))) skipped.add(plan.get(was));
				zman = false;   // (Pebble PD 0.99: another target: Far picks the zoom again)
				view(cellOf(targets.get(target)), PRESS);
				keepEnemy();
				return;
			}
			case SELECT:
				if (held && !aiming) { GameScene.show(new WndBag(Dungeon.hero.belongings.backpack)); return; }
				refreshTargets(aiming);
				if (targets.isEmpty()) {
					if (!aiming) nothingListed();
					return;
				}
				int i = Math.min(target, targets.size() - 1), cell = targets.get(i);
				Assist.Target t = current();
				view(-1, ACT);
				if (t != null) select(t, aiming);
				else if (cell == -2) { Dungeon.hero.search(true); }   // (a test's bare cells)
				else if (shot(cell)) shoot(cellOf(cell));
				else GameScene.handleCell(cell);
				targetsStale = true; acted();
				return;
			case UP:
				if (held && WatchSettings.assist() == 2) { lock(); return; }   // (Pebble PD 1.0: Far: the zoom locked; the menu: Back's buttons, or a tap)
				if (held) { GameScene.show(new WndGame()); return; }
				zoom();
				return;
			case BACK:
				if (aiming) { GameScene.cancelCellSelector(); targetsStale = true; return; }
				// Pebble PD: the list back to its first suggestion too (Downs undone: not passed over)
				if (target != 0 && !targets.isEmpty()) { target = 0; skipped.clear(); zman = false; view(cellOf(targets.get(0)), PRESS); keepEnemy(); }
				onToolbar = true; focus = 0; hlp = 0;
				if (itemAssist()) hlRank();
				if (hud != null) hud.highlight = itemAssist() ? hlOrder[0] : 0;
				return;
		}
	}

	/** Pebble PD's Select with nothing listed: the camera back on the hero if it was moved off him; else the stairs
	 *  down or what lies underfoot; else a search (a closed room hides a door). */
	private void nothingListed() {
		Hero h = Dungeon.hero; Camera c = Camera.main;
		if (h == null || !h.ready || h.sprite == null || c == null) return;
		PointF r = restCentre(c), hc = heroCentre(c, c.zoom);
		if (Math.abs(r.x - hc.x) > 2 || Math.abs(r.y - hc.y) > 2) { zman = false; view(-1, TURN); return; }
		if (h.pos == Dungeon.level.exit() || Dungeon.level.heaps.get(h.pos) != null) GameScene.handleCell(h.pos);
		else if (!GameScene.cancel()) h.search(true);
		targetsStale = true; acted();
	}

	// ---- Back's buttons with Item assist: the Assistant's order (Pebble PD 0.54, tgt.c hl_rank) ----
	private final int[] hlOrder = {0, 1, 2, 3, 4, 5, 6, 7};
	private int hlp;   // where Back is in hlOrder
	static boolean itemAssist() { return WatchSettings.assist() != 0 && WatchSettings.itemAssist(); }
	/** The buttons best first: the hands' item's button (its quickslot, else the backpack); wait with a slow foe two
	 *  steps off or a rest in hand, else when hurt and alone; search when plan A is to search here, more when alone; a
	 *  quickslot's throw or zap with an enemy in sight; the backpack; the portrait and the menu last. */
	private void hlRank() {
		Hero h = Dungeon.hero; if (h == null || Dungeon.level == null) return;
		boolean seen = false, foe2 = false;
		for (Mob m : Dungeon.level.mobs) {
			if (m.alignment != Char.Alignment.ENEMY || m.invisible > 0 || m.pos < 0 || !Dungeon.level.heroFOV[m.pos] || m.state == m.PASSIVE) continue;
			seen = true;
			if (m.state == m.HUNTING && Dungeon.level.distance(m.pos, h.pos) == 2 && m.speed() <= 1f) foe2 = true;   // (it walks into the first blow)
		}
		Assist.Target a = plan.isEmpty() || plan.size() != targets.size() ? null : plan.get(0);
		int k = a == null ? 0 : kindOf(a), hands = a == null ? -1 : a.kind == Assist.USE && a.item != null ? buttonOf(a) : k == K_TALENT ? pebxel.ui.WatchHud.B_PORTRAIT : -1;
		int[] sc = new int[8];
		sc[pebxel.ui.WatchHud.TB_WAIT] = k == K_REST ? 92 : foe2 ? 90 : !seen && h.HP < h.HT ? 50 : 25;
		sc[pebxel.ui.WatchHud.TB_SEARCH] = k == K_SEARCH ? 80 : seen ? 10 : 35;
		sc[pebxel.ui.WatchHud.TB_BAG] = 40; sc[pebxel.ui.WatchHud.B_MENU] = 2; sc[pebxel.ui.WatchHud.B_PORTRAIT] = 3;
		for (int q = 0; q < pebxel.ui.WatchHud.TB_Q.length; q++) {
			com.shatteredpixel.shatteredpixeldungeon.items.Item it = Dungeon.quickslot.getItem(q);
			boolean ranged = it instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.MissileWeapon || it instanceof com.shatteredpixel.shatteredpixeldungeon.items.wands.Wand
					|| it instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow;
			sc[pebxel.ui.WatchHud.TB_Q[q]] = it == null || !Dungeon.quickslot.isNonePlaceholder(q) ? 5 : seen && ranged ? 60 : 20;
		}
		if (hands >= 0) sc[hands] = 100;
		for (int i = 0; i < 8; i++) { int b = 0; for (int j = 1; j < 8; j++) if (sc[j] > sc[b]) b = j; hlOrder[i] = b; sc[b] = -1; }
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
		// (Near, Off, locked: the list is what shows at the player's zoom, made again when it changes; Far: a zoom keeps it)
		int settings = WatchSettings.assist() * 2 + (WatchSettings.itemAssist() ? 1 : 0) + (far() ? 0 : 8 * (userZoom() + 1) + (zlock ? 32 : 0));
		if (waiting(now, aiming)) return;
		if (!targetsStale && Dungeon.hero.pos == lastPos && Dungeon.depth == lastDepth && aiming == lastAiming && now == lastNow && settings == lastSettings) return;
		String why = log ? (targetsStale ? "stale " : "") + (Dungeon.hero.pos != lastPos ? "pos " : "") + (now != lastNow ? "time " + lastNow + "->" + now + " " : "") + (aiming != lastAiming ? "aim " : "") : "";
		obeyed(Dungeon.hero.pos != lastPos || now != lastNow, Dungeon.depth == lastDepth);
		targetsStale = false; lastPos = Dungeon.hero.pos; lastDepth = Dungeon.depth; lastAiming = aiming; lastNow = now; lastSettings = settings;
		plans++;
		zman = false;   // (Pebble PD 0.99: a turn: Far picks the zoom again)
		if (!far()) zoomTo(userZoom());   // (the Near list is what shows at the player's zoom)
		if (legacy != null) { targets = legacy.apply(aiming); plan = new ArrayList<>(); target = 0; view(targets.isEmpty() ? -1 : cellOf(targets.get(0)), TURN); return; }
		plan = new ArrayList<>(Assist.plan(aiming, Controls::onScreenFromHero));
		if (Assist.lastError != null) { System.out.println("pebxel: assist " + Assist.lastError); Assist.lastError = null; }
		if (!aiming) { once(plan); steady(plan); shots(plan); if (WatchSettings.assist() != 0) { veins(plan); talents(plan); } }   // (Off: the enemies only)
		targets = new ArrayList<>();
		for (Assist.Target t : plan) targets.add(aiming ? t.cell : markCell(t));
		target = 0;   // (Pebble PD 0.40: after each step the first one, what a good player would do next)
		if (WatchSettings.assist() == 0 && keptId >= 0)   // (Off: the enemy chosen stays chosen, as PD's last target)
			for (int j = 0; j < plan.size(); j++) { Char c = plan.get(j).kind == Assist.ENEMY ? com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(plan.get(j).cell) : null; if (c != null && c.id() == keptId) { target = j; break; } }
		keepEnemy();
		if (!aiming && !plan.isEmpty() && plan.size() == targets.size()) { offered = plan.get(0); offeredPos = Dungeon.hero.pos; }
		// the camera shows the target on show whole, clear of the HUD (Far: wherever it is, Pebble PD 0.76); else it stays on the hero
		view(targets.isEmpty() ? -1 : cellOf(targets.get(target)), TURN);
		if (log) System.out.println("pebxel: plan d" + Dungeon.depth + " t" + Math.round(now) + " hero " + Dungeon.hero.pos + (aiming ? " aiming" : "") + " (" + why.trim() + "): " + plan);
	}
	// ---- obeying the player (Pebble PD's pass_hands): what he passes over isn't pressed on him again ----
	private Assist.Target offered;         // plan A of the list on show, till the hero acts
	private int offeredPos = -1;
	private final List<Assist.Target> skipped = new ArrayList<>();   // entries Down cycled past
	private boolean decided, honoured, windowSince;   // Select answered the list (plan A itself: honoured); a window came up since
	/** A new list is due: if the hero acted (moved / time passed, same floor) without Select answering the list (a tap,
	 *  the toolbar, the backpack...), plan A and the entries cycled past were passed over; plan A's own use chosen, but
	 *  its window closed with nothing done (no time passed), was passed over too. */
	private void obeyed(boolean acted, boolean sameFloor) {
		Hero h = Dungeon.hero;
		if (offered != null && sameFloor && h != null) {
			if (acted && !decided) { passOne(offered, h.pos, offeredPos); for (Assist.Target e : skipped) if (e != offered) passOne(e, h.pos, offeredPos); }
			else if (!acted && decided && honoured && windowSince && offered.kind == Assist.USE) passOne(offered, h.pos, offeredPos);
		}
		offered = null; skipped.clear(); decided = honoured = windowSince = false;
	}
	private void passOne(Assist.Target e, int went, int from) {
		// (a throw or zap at an enemy: the fight's, not a choice to remember. Aimed: at a cell other than where the hero
		// stood when it was offered; where he is now would take a use offered on himself for an aimed one once he
		// walked off: a tapped walk never passed the hands' suggestion over, the buttons did)
		if (e == null || (e.kind == Assist.USE && e.item != null && e.cell >= 0 && e.cell != from)) return;
		if (TALENTS.equals(e.action)) { talentsPassed = talentPoints(); return; }
		if (e.kind == Assist.GO || e.kind == Assist.PICKUP) { int o = e.obj >= 0 ? e.obj : e.cell; if (o == shownObj) shownObj = -1; }
		Assist.passed(e, went, from);
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
			// the others cycled past on the way to this one: passed over too (plan A: chose)
			int h = Dungeon.hero.pos;
			for (Assist.Target e : skipped) if (e != t && e != a) passOne(e, t.cell >= 0 ? t.cell : h, h);
			skipped.clear();
			decided = true; honoured = t == a;
			if (a != t) shownObj = -1;   // (steady() mustn't bring the one passed over back)
		}
		if (TALENTS.equals(t.action)) { talentsOpen = true; talentsPassed = talentPoints(); talentFocus = Build.nextTalent(Dungeon.hero); talentTries = 0; pebxel.ui.WndWatchHero.openTalents(); return; }
		Assist.act(t, true);
		if (t.on != null) pick = t.on;
	}
	private com.shatteredpixel.shatteredpixeldungeon.items.Item pick;   // the bag that opened: the frame on this
	private com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent talentFocus; private int talentTries; private boolean talentsOpen;
	private static java.lang.reflect.Field talentF;
	private static Object talentOf(com.watabou.noosa.Gizmo g) {
		if (!(g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.TalentButton)) return null;
		try { if (talentF == null) { talentF = com.shatteredpixel.shatteredpixeldungeon.ui.TalentButton.class.getDeclaredField("talent"); talentF.setAccessible(true); } return talentF.get(g); }
		catch (Exception e) { return null; }
	}

	/** Plan A's way kept steady: when the Assistant's plan A goes back to a place it left for another a turn or two ago
	 *  (two spots to explore, each nearer from where the other led: the hero walked back and forth between them), the
	 *  objective shown last stays plan A while it is still offered. Only for walks with no enemy about. */
	private int shownObj = -1, shownDepth = -1; private final ArrayDeque<int[]> leftObjs = new ArrayDeque<>();   // {objective, game time left}
	private void steady(List<Assist.Target> ts) {
		float now = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now();
		if (Dungeon.depth != shownDepth) { leftObjs.clear(); shownObj = -1; shownDepth = Dungeon.depth; }
		while (!leftObjs.isEmpty() && now - leftObjs.peekFirst()[1] > 30) leftObjs.pollFirst();
		if (ts.isEmpty()) { shownObj = -1; return; }
		Assist.Target a = ts.get(0);
		boolean walk = a.kind == Assist.GO && a.cell != Dungeon.hero.pos;
		for (Assist.Target t : ts) if (t.kind == Assist.ENEMY) walk = false;
		int ao = a.obj >= 0 ? a.obj : a.cell;
		if (walk && shownObj >= 0 && ao != shownObj) {
			boolean back = false; for (int[] l : leftObjs) back |= l[0] == ao;
			if (back) for (int j = 1; j < ts.size(); j++) {
				Assist.Target t = ts.get(j);
				if ((t.kind == Assist.GO || t.kind == Assist.PICKUP) && (t.obj >= 0 ? t.obj : t.cell) == shownObj && t.cell != Dungeon.hero.pos) { ts.remove(j); ts.add(0, t); a = t; ao = shownObj; break; }
			}
		}
		if (shownObj >= 0 && ao != shownObj) leftObjs.addLast(new int[]{shownObj, Math.round(now)});
		while (leftObjs.size() > 4) leftObjs.pollFirst();
		shownObj = a.kind == Assist.GO || a.kind == Assist.PICKUP ? ao : -1;
	}

	/** One entry an objective: a walk towards one already offered is left out (plan A's "read my mind" and "your choice"
	 *  could both stand for one: a Down spent for nothing). */
	private static void once(List<Assist.Target> ts) {
		Set<Integer> objs = new HashSet<>();
		for (Iterator<Assist.Target> it = ts.iterator(); it.hasNext(); ) {
			Assist.Target t = it.next();
			if (t.kind != Assist.GO && t.kind != Assist.PICKUP) continue;
			int o = t.obj >= 0 ? t.obj : t.cell;
			if (!objs.add(o) || !objs.add(-2 - t.cell)) it.remove();   // (the same objective, or the same cell)
		}
	}

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
	 *  until passed over or opened for these points (closed unspent: the player's call, not offered again till the next). */
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

	// ---- the play area: the screen less the HUD, where the target box must show whole ----
	// Emery: left of the tags (x 178), above the toolbar (y 190), under the status pane's top strip (y 28) and, at the
	// left, under the portrait and its level badge (52 x 52). Round: inside the circle's middle.
	static final int PLAY_R = 22, PLAY_B = 38, PLAY_T = 28, PORTRAIT = 52, ROUND_R = 100;
	/** Half the target box (watch px): 16 px across, a whole cell at 32 px tiles. */
	static float markHalf() { Camera c = Camera.main; return Math.max(8, 4 * (c == null ? 2 : c.zoom)); }
	/** Does a box centred at (sx, sy) (watch px) show whole in the play area? */
	static boolean inPlay(float sx, float sy, float half) {
		if (Screen.round) { float dx = sx - Screen.w / 2f, dy = sy - Screen.h / 2f; return (float) Math.sqrt(dx * dx + dy * dy) + half <= ROUND_R + 0.5f; }
		float l = sx - half, t = sy - half, r = sx + half, b = sy + half;
		return l >= -0.5f && r <= Screen.w - PLAY_R + 0.5f && b <= Screen.h - PLAY_B + 0.5f && t >= (l < PORTRAIT ? PORTRAIT : PLAY_T) - 0.5f;
	}
	/** Is the cell's box whole in the play area, with the camera as it is now? */
	static boolean onScreen(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0) return true;
		PointF p = DungeonTilemap.tileCenterToWorld(cell);
		float sx = (c.x + (p.x - c.scroll.x) * c.zoom) / 2f, sy = (c.y + (p.y - c.scroll.y) * c.zoom) / 2f;   // watch pixels
		return inPlay(sx, sy, markHalf());
	}
	/** As onScreen, with the camera where it comes to rest: on the hero (the list is made as a walk ends, while the
	 *  camera is still catching him up; a move for the last target may have it elsewhere). */
	static boolean onScreenFromHero(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0 || Dungeon.hero == null || Dungeon.level == null) return true;
		int w = Dungeon.level.width(), h = Dungeon.hero.pos;
		float z = restZoom > 0 ? restZoom : c.zoom, t = DungeonTilemap.SIZE * z / 2f;   // a cell in watch pixels (at the zoom on its way)
		return inPlay(Screen.w / 2f + (cell % w - h % w) * t, Screen.h / 2f + (cell / w - h / w) * t, markHalf(z));
	}

	/** The camera's centre on the hero (world units) at zoom z, as SPD's follow puts it (its offset scales with the zoom). */
	private static PointF heroCentre(Camera c, float z) {
		PointF p = Dungeon.hero.sprite.center(); float k = c.zoom / z;
		return new PointF(p.x + c.centerOffset.x * k, p.y + c.centerOffset.y * k);
	}
	/** Where the camera's centre comes to rest (world units): on what it follows, at the point it pans to, else where
	 *  it is (the map dragged by a finger). */
	private static PointF restCentre(Camera c) {
		float[] pan = com.watabou.noosa.NoosaAccess.pan(c); com.watabou.noosa.Visual f = com.watabou.noosa.NoosaAccess.followTarget(c);
		if (pan[2] > 0 && f != null && f.visible) return new PointF(f.x + f.width() / 2 + c.centerOffset.x, f.y + f.height() / 2 + c.centerOffset.y);
		if (pan[2] > 0 && f == null) return new PointF(pan[0], pan[1]);
		return new PointF(c.scroll.x + c.width / 2f, c.scroll.y + c.height / 2f);
	}
	/** Where the cell's box shows (watch px) with the camera's centre at q, at zoom z. */
	private static float[] seen(int cell, PointF q, float z) {
		PointF p = DungeonTilemap.tileCenterToWorld(cell); float k = z / 2f;
		return new float[]{Screen.w / 2f + (p.x - q.x) * k, Screen.h / 2f + (p.y - q.y) * k};
	}
	private static boolean inPlay(int cell, PointF q, float z) { float[] s = seen(cell, q, z); return inPlay(s[0], s[1], markHalf(z)); }
	static float markHalf(float z) { return Math.max(8, 4 * z); }
	/** The nearest place (watch px) where a box shows whole in the play area. */
	private static float[] clampPlay(float sx, float sy, float half) {
		if (Screen.round) {
			float ex = sx - Screen.w / 2f, ey = sy - Screen.h / 2f, d = (float) Math.sqrt(ex * ex + ey * ey), r = ROUND_R - half;
			return d <= r ? new float[]{sx, sy} : new float[]{Screen.w / 2f + ex * r / d, Screen.h / 2f + ey * r / d};
		}
		float x = Math.max(half, Math.min(Screen.w - PLAY_R - half, sx));
		float y = Math.max((x - half < PORTRAIT ? PORTRAIT : PLAY_T) + half, Math.min(Screen.h - PLAY_B - half, sy));
		return new float[]{x, y};
	}
	/** The least move of the camera's centre from q that shows the cell's box whole (zoom z); null: none needed. */
	private static PointF shiftFor(int cell, PointF q, float z) {
		float[] s = seen(cell, q, z); float half = markHalf(z);
		if (inPlay(s[0], s[1], half)) return null;
		float[] in = clampPlay(s[0], s[1], half); float k = z / 2f;
		return new PointF(q.x + (s[0] - in[0]) / k, q.y + (s[1] - in[1]) / k);
	}
	private static PointF shiftFor(int cell, PointF q, Camera c) { return shiftFor(cell, q, c.zoom); }
	/** At zoom z, where the camera's centre shows both the hero's box and the cell's whole in the play area: the hero's
	 *  own view (hc itself) when it shows the cell; else moved as little as shows it; else the two centred in the play
	 *  area (Pebble PD 0.93: centred between them). null: they don't both fit at this zoom. */
	private static PointF fit(int cell, float z, Camera c) {
		int h = Dungeon.hero.pos;
		PointF hc = heroCentre(c, z), q = shiftFor(cell, hc, z);
		if (q == null) return hc;
		if (inPlay(h, q, z)) return q;
		float[] a = seen(cell, hc, z), b = seen(h, hc, z); float k = z / 2f;
		float cx = Screen.round ? Screen.w / 2f : (Screen.w - PLAY_R) / 2f, cy = Screen.round ? Screen.h / 2f : (PLAY_T + Screen.h - PLAY_B) / 2f;
		q = new PointF(hc.x + ((a[0] + b[0]) / 2 - cx) / k, hc.y + ((a[1] + b[1]) / 2 - cy) / k);
		for (int i = 0; i < 3; i++) {   // (then nudged for the corner under the portrait)
			if (inPlay(cell, q, z) && inPlay(h, q, z)) return q;
			PointF n = shiftFor(i == 1 ? h : cell, q, z); if (n != null) q = n;
		}
		return inPlay(cell, q, z) && inPlay(h, q, z) ? q : null;
	}

	// ---- the camera and its zoom, as Pebble PD's (main.c cam_to, zoom_cycle, zoom_go, g_zlock) ----
	/** The zoom steps, as camera zooms: 8 px tiles on the watch, 16, 32. */
	static final int[] ZOOMS = {1, 2, 4};
	static int zi(float z) { return z < 1.5f ? 0 : z < 3f ? 1 : 2; }
	static final int TURN = 0, PRESS = 1, ACT = 2, UPZ = 3;   // why the view is worked out: the list made again, a Down, Select, Up
	private int userZoom = -1;     // the player's zoom (Up), 0..2: Far zooms out from it only as far as it takes, and back
	private int zdir = 1;          // Up's way: far, middle, close, middle, far... (Pebble PD 0.99)
	private boolean zman;          // Up zoomed this turn: Far keeps to that zoom (the target in the middle when both don't fit)
	static boolean zlock;          // long Up in Far: the camera locked on the hero at the player's zoom; Far rests, as Near (Pebble PD 1.0)
	private int zto = -1; private float zwait; private PointF zq; private boolean zqSet;   // the middle zoom's moment: the zoom and view on the way
	private int viewCell = -1; private float seenZoom = -1;
	/** The zoom the camera is at or on its way to (the Near list is made for it). */
	static float restZoom = -1;
	static boolean far() { return WatchSettings.assist() == 2 && !zlock; }
	private int userZoom() {
		if (userZoom < 0) userZoom = WatchSettings.zoom();   // (kept across sessions)
		int lo = zi(PixelScene.minZoom), hi = zi(PixelScene.maxZoom);
		return Math.max(lo, Math.min(hi, userZoom));
	}

	/** Up: the next zoom, far, middle, close, middle... from the one shown (Pebble PD 0.99); unlocks. Far: that zoom on the
	 *  target this turn. */
	private void zoom() {
		Camera c = Camera.main;
		if (c == null) return;
		int z = zto >= 0 ? zto : zi(c.zoom);
		zlock = false; zman = true;
		if (z == 0) zdir = 1; else if (z == 2) zdir = -1;
		userZoom = Math.max(zi(PixelScene.minZoom), Math.min(zi(PixelScene.maxZoom), z + zdir)); WatchSettings.zoom(userZoom);
		view(viewCell, UPZ);
	}
	/** Long Up in Far: the zoom locked on the hero at the zoom shown, the targets those on screen; again: Far again. */
	private void lock() {
		Camera c = Camera.main;
		if (c == null) return;
		zlock = !zlock;
		if (zlock) { userZoom = zto >= 0 ? zto : zi(c.zoom); WatchSettings.zoom(userZoom); }
		targetsStale = true; zman = false;
		view(-1, TURN);
	}

	/** The camera for the target on show (-1: none). Far (Pebble PD's cam_to): the closest zoom, from the player's own
	 *  out, at which the hero's box and the target's both show whole in the play area, the view moved as little as that
	 *  takes (none fits: the farthest, on the target); back in when the target is near again. Not on every Down: a Down
	 *  only zooms out (the list made again next turn picks the zoom afresh), and Select leaves the zoom as it is (the
	 *  next list picks it), so a walk doesn't zoom in only to zoom out again (radio time). Up: the player's zoom this
	 *  turn. Near, Off, locked: the player's zoom on the hero, moved as little as shows the target clear of the HUD. */
	private void view(int cell, int how) {
		Camera c = Camera.main;
		if (c == null || Dungeon.hero == null || Dungeon.hero.sprite == null || !(Game.scene() instanceof GameScene)) return;
		viewCell = cell;
		int uz = userZoom(), cz = zi(c.zoom), z = uz; PointF q = null;
		if (zto >= 0) cz = zto;
		if (!far()) { if (cell >= 0) q = shiftFor(cell, heroCentre(c, ZOOMS[z]), ZOOMS[z]); }
		else if (how == ACT) z = cz;
		else if (cell >= 0) {
			int top = zman ? uz : how == PRESS ? Math.min(uz, cz) : uz, lo = zman ? uz : zi(PixelScene.minZoom);
			for (z = top; z >= lo; z--) { q = fit(cell, ZOOMS[z], c); if (q != null) break; }
			if (q == null) {   // (none fits: the target in the middle of the play area, farthest out)
				z = lo; PointF hc = heroCentre(c, ZOOMS[z]); float[] s = seen(cell, hc, ZOOMS[z]); float k = ZOOMS[z] / 2f;
				float cx = Screen.round ? Screen.w / 2f : (Screen.w - PLAY_R) / 2f, cy = Screen.round ? Screen.h / 2f : (PLAY_T + Screen.h - PLAY_B) / 2f;
				q = new PointF(hc.x + (s[0] - cx) / k, hc.y + (s[1] - cy) / k);
			}
		}
		if (q != null && q.x == heroCentre(c, ZOOMS[z]).x && q.y == heroCentre(c, ZOOMS[z]).y) q = null;
		zq = q; zqSet = true;
		if (zoomTo(z)) return;   // (the middle zoom first: the view once the zoom is there)
		look(c, q);
	}
	/** Follow the hero (q null), or pan to show q at the screen's centre. */
	private void look(Camera c, PointF q) {
		if (q == null) { if (c.followTarget() != Dungeon.hero.sprite || com.watabou.noosa.NoosaAccess.pan(c)[2] <= 0) c.panFollow(Dungeon.hero.sprite, 5f); }
		else c.panTo(new PointF(q.x - c.centerOffset.x, q.y - c.centerOffset.y), 5f);
	}
	/** Pebble PD's zoom_go: to zoom step i; from closest to farthest or back, the middle one for a moment first (true:
	 *  the zoom is on its way). */
	private boolean zoomTo(int i) {
		Camera c = Camera.main;
		int now = zi(c.zoom);
		if (zto >= 0) { if (zto == i) return true; zto = -1; }
		if (Math.abs(i - now) == 2) { zto = i; zwait = 0; setZoom(1); return true; }
		setZoom(i);
		return false;
	}
	/** Set the map's zoom at once, as Shattered's zoom keys do (CellSelector.zoom: the zoom kept, the sprites set). */
	private void setZoom(int i) {
		Camera c = Camera.main; float v = Math.max(PixelScene.minZoom, Math.min(PixelScene.maxZoom, ZOOMS[i]));
		restZoom = zto >= 0 ? ZOOMS[zto] : v;
		if (c.zoom == v) return;
		try {
			aiming();   // (looks up the cell selector's field)
			Object sel = selF.get(null);
			if (zoomM == null) { zoomM = com.shatteredpixel.shatteredpixeldungeon.scenes.CellSelector.class.getDeclaredMethod("zoom", float.class); zoomM.setAccessible(true); }
			zoomM.invoke(sel, v);
		} catch (Exception e) {
			int z = Math.round(c.zoom), to = Math.round(v);
			for (int k = z; k < to; k++) key(SPDAction.ZOOM_IN);
			for (int k = z; k > to; k--) key(SPDAction.ZOOM_OUT);
		}
		seenZoom = c.zoom;
	}
	private static java.lang.reflect.Method zoomM;
	/** Every frame: the middle zoom's moment over (150 ms), the zoom on to where it was going and the view with it; a
	 *  zoom not the Assistant's (a pinch, Shattered's keys) is the player's. */
	private void zoomed() {
		Camera c = Camera.main; if (c == null) return;
		if (zto >= 0) {
			zwait += Game.elapsed / Math.max(0.01f, Game.pebxelSpeed);
			if (zwait >= 0.15f || Ui.topWindow() != null) { int z = zto; zto = -1; setZoom(z); if (zqSet && Dungeon.hero != null && Dungeon.hero.sprite != null) look(c, zq); }
			return;
		}
		if (c.zoom == seenZoom) return;
		boolean first = seenZoom < 0; seenZoom = c.zoom; restZoom = c.zoom;
		if (!first) { userZoom = zi(c.zoom); WatchSettings.zoom(userZoom); if (Ui.topWindow() == null) view(viewCell, TURN); }
	}
	/** The target outside the play area where the camera comes to rest (the map dragged by a finger, the zoom locked):
	 *  the HUD frames its picture at the edge nearest it (true: the box on the map is hidden); and the hero's portrait,
	 *  framed the same way, when he is out of the view. -1: no target. */
	private boolean edge(int cell) {
		if (hud == null) return false;
		Camera c = Camera.main;
		if (c == null || Dungeon.hero == null || Dungeon.hero.sprite == null) { hud.edge(-999, 0, 0, 0, -1); hud.heroEdge(-999, 0); return false; }
		PointF rest = restCentre(c);
		float[] h = seen(Dungeon.hero.pos, rest, c.zoom);
		if (inPlay(h[0], h[1], markHalf(c.zoom))) hud.heroEdge(-999, 0);
		else { int[] at = pebxel.ui.WatchHud.pointerAt(h[0], h[1]); hud.heroEdge(at[0], at[1]); }
		if (cell < 0) { hud.edge(-999, 0, 0, 0, -1); return false; }
		float[] s = seen(cell, rest, c.zoom);
		if (inPlay(s[0], s[1], markHalf(c.zoom))) { hud.edge(-999, 0, 0, 0, -1); return false; }
		int[] at = pebxel.ui.WatchHud.pointerAt(s[0], s[1]);
		hud.edge(at[0], at[1], dir(s[0] - at[0]), dir(s[1] - at[1]), cell);
		return true;
	}
	private static int dir(float d) { return d > 14 ? 1 : d < -14 ? -1 : 0; }   // (beyond the frame's side)

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
		Feedback.frame();
		if (pending) pendingFrames++;
		keepHud();
		Window w = Ui.topWindow();
		closeTag(w);
		if (talentsOpen) {   // the talents the Assistant opened, the last point spent: back to the game (two Backs saved)
			if (w == null) talentsOpen = false;
			else if (w instanceof com.shatteredpixel.shatteredpixeldungeon.windows.WndHero && talentPoints() == 0) { w.hide(); talentsOpen = false; w = Ui.topWindow(); }
		}
		if (pick != null) {   // the backpack a use opened to choose in (what to identify...): the frame on the Assistant's pick
			if (w instanceof pebxel.ui.WndWatchBag) { ((pebxel.ui.WndWatchBag) w).focusOn(pick); pick = null; }
			else if (w == null && Dungeon.hero != null && Dungeon.hero.ready) pick = null;
		}
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (touching) { frame.hide(); if (menu != null) { marker.hide(); return; } }
		else if (menu != null) {
			if (w != null && decided) windowSince = true;
			pending = false;   // (a window came up: the press has been taken; the list is made again once it goes)
			onToolbar = false;
			if (hud != null) hud.highlight = -1;
			marker.hide();
			List<Ui.Part> bs = buttons(menu);
			if (focusOwner != menu) {
				focusOwner = menu; focus = 0;
				if (w != null && tall(w)) scroll(w, 100000);   // a new tall window starts at its top
			}
			if (bs.isEmpty()) { frame.hide(); if (w != null) keepInView(w, null); return; }
			if (talentFocus != null) {   // the talents the Assistant opened: the frame on the guide's next talent (Build), the choice the player's
				for (int i = 0; i < bs.size(); i++) if (talentOf(bs.get(i).g) == talentFocus) { focus = i; talentFocus = null; break; }
				if (talentFocus != null && ++talentTries > 30) talentFocus = null;
			}
			Component c = (Component) bs.get(Math.max(0, Math.min(bs.size() - 1, focus))).g;
			if (menu instanceof pebxel.ui.Navigable && ((pebxel.ui.Navigable) menu).focused() != null) c = ((pebxel.ui.Navigable) menu).focused();
			// (a tall window opens at its top for reading, even when its first button is further down; one only a little
			// too tall shows its framed button whole: the game slot's Continue and Erase were cut by the frame's foot)
			if (w != null && keptFor != w) { keptFor = w; keepInView(w, !tall(w) || reach(w, c) <= (viewBottom(w) - viewTop(w)) / 4f ? c : null); }
			for (Ui.Part p : bs) if (p.g instanceof pebxel.ui.Focusable) ((pebxel.ui.Focusable) p.g).focus(p.g == c);
			if (c instanceof pebxel.ui.Focusable) { frame.hide(); return; }
			if (menu instanceof pebxel.ui.WatchWindow) frame.around(menu, c.left(), c.top(), c.width(), c.height(), 0xFFFFAA00, 2);   // Pebble PD: a 2 px yellow box
			else {   // (kept inside the window's frame: a button against its edge has its box there, not on the edge)
				float x0 = c.left() - 1, y0 = c.top() - 1, x1 = c.right() + 1, y1 = c.bottom() + 1;
				float[] in = w != null ? innerRect(w) : null;
				if (in != null) {
					Camera k = w.camera;
					float ib = (in[3] * 2 - k.y) / k.zoom + k.scroll.y, ob = (in[6] * 2 - k.y) / k.zoom + k.scroll.y;
					if (c.top() >= ib - 2 && c.bottom() > ob) y0 = Math.max(y0, ob);   // a tab hanging under the frame: its box under the frame's foot
					else {
						x0 = Math.max(x0, (in[0] * 2 - k.x) / k.zoom + k.scroll.x); x1 = Math.min(x1, (in[2] * 2 - k.x) / k.zoom + k.scroll.x);
						if (!tall(w)) { y0 = Math.max(y0, (in[1] * 2 - k.y) / k.zoom + k.scroll.y); y1 = Math.min(y1, ib); }
					}
				}
				frame.around(menu, x0, y0, x1 - x0, y1 - y0, 0xFFFFFF44, 1);
			}
			return;
		}
		frame.hide();
		if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) { marker.hide(); edge(-1); return; }
		zoomed();
		if (onToolbar || !Dungeon.hero.ready) { marker.hide(); edge(-1); return; }   // (the toolbar's highlight is drawn by the HUD)
		refreshTargets(aiming());
		int cell = targets.isEmpty() || pending ? -1 : cellOf(targets.get(Math.min(target, targets.size() - 1)));
		if (cell < 0) { marker.hide(); edge(-1); return; }   // (an action of the toolbar's: its button's reticle shows it, WatchHud)
		marker.at(cell);
		if (edge(cell)) marker.hide();
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
		Camera cam = Camera.main; PointF rest = cam == null ? null : restCentre(cam);
		for (int i = 0; i < n; i++) {
			int cell = cellOf(targets.get(i));
			if (cell < 0) { w16(b, 0x7FFF); w16(b, 0x7FFF); b.write(0); continue; }
			float[] xy = Marker.place(cell);
			// (a box the view must move for, or one hidden by the HUD: no box at once; the phone's frames bring the
			// view and the box together, rather than the box first under the HUD and the view a round trip later)
			if (rest != null && shiftFor(cell, rest, cam) != null) { w16(b, 0x7FFF); w16(b, 0x7FFF); }
			else { w16(b, Math.round(xy[0] * 8)); w16(b, Math.round(xy[1] * 8)); }
			Assist.Target t = i < plan.size() && plan.size() == targets.size() ? plan.get(i) : null;
			boolean walks = t == null || t.kind == Assist.GO || t.kind == Assist.PICKUP || (t.kind == Assist.ENEMY && t.action == null);   // (an item's use, a wait: no walk)
			List<Integer> path = aiming || hid < 0 || i >= PLAN_WALKS || !walks ? null : walk(h, cell);
			int k = path == null ? 0 : Math.min(PLAN_STEPS, path.size());
			byte[] dirs = new byte[k];
			int from = h.pos, w = Dungeon.level.width();
			for (int j = 0; j < k; j++) {
				int to = path.get(j), dx = to % w - from % w, dy = to / w - from / w;
				if (Math.abs(dx) > 1 || Math.abs(dy) > 1) { k = j; break; }   // (not a step: stop the plan there)
				dirs[j] = (byte) ((dx + 1) + 3 * (dy + 1)); from = to;
			}
			b.write(k); b.write(dirs, 0, k);   // (the count as written: stopped short, it used to say more steps than came)
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
				GameScene.effectOverFog(img);   // (over the fog of war: a target in a remembered place showed through its checker, faint)
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
					GameScene.effectOverFog(bar[i]);   // (full brightness on a remembered cell too)
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

		/** (tests) The box drawn: its group, then x, y, w, h in that group's units (null: none). */
		Object[] box() {
			if (bar[0] == null || !bar[0].visible || bar[0].parent == null) return null;
			return new Object[]{bar[0].parent, new float[]{bar[0].x, bar[0].y, bar[0].width(), bar[3].y + bar[3].height() - bar[0].y}};
		}
	}
}
