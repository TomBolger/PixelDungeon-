package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.SPDAction;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
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

	public void button(int code) {
		boolean held = (code & LONG) != 0;
		int b = code & 15;
		Window w = Ui.topWindow();
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (menu != null) { menuButton(menu, b, held, w); return; }
		if (Dungeon.hero == null || !Dungeon.hero.isAlive()) return;
		dungeonButton(b, held);
	}

	// ---- menus ----

	private void menuButton(Group menu, int b, boolean held, Window w) {
		List<Ui.Part> bs = buttons(menu);
		if (b == BACK) { if (w != null) w.onBackPressed(); else com.watabou.noosa.NoosaAccess.back(Game.scene()); return; }
		if (focusOwner != menu) { focusOwner = menu; focus = 0; }
		if (b == UP || b == DOWN) {
			// a window taller than the screen scrolls half a screen at a time towards the next button
			int next = bs.isEmpty() ? -1 : b == UP ? (focus + bs.size() - 1) % bs.size() : (focus + 1) % bs.size();
			if (w != null && tall(w)) {
				Component n = next < 0 ? null : (Component) bs.get(next).g;
				boolean wraps = next >= 0 && (b == DOWN ? next < focus : next > focus);
				if (n == null || (!wraps && !visible(w, n))) {
					scroll(w, b == DOWN ? -Game.height / 2 : Game.height / 2);
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

	// ---- windows taller than the screen (long texts with big letters) ----

	private static boolean tall(Window w) { return w.camera != null && w.camera.screenHeight() > Game.height + 1; }

	/** Is the component fully on screen (window camera units -> phone pixels)? */
	private static boolean visible(Window w, Component c) {
		float top = w.camera.y + c.top() * w.camera.zoom, bottom = w.camera.y + c.bottom() * w.camera.zoom;
		return top >= 0 && bottom <= Game.height;
	}

	/** Move a tall window up (negative) or down by some phone pixels, within its ends. */
	private static void scroll(Window w, int dy) {
		int min = (int) (Game.height - w.camera.screenHeight()), max = 0;
		w.camera.y = Math.max(min, Math.min(max, w.camera.y + dy));
	}

	/** Keep a tall window inside its ends and its framed button on screen. */
	private static void keepInView(Window w, Component c) {
		if (!tall(w)) return;
		scroll(w, 0);
		if (c != null && !visible(w, c)) {
			float top = w.camera.y + c.top() * w.camera.zoom, bottom = w.camera.y + c.bottom() * w.camera.zoom;
			if (bottom > Game.height && top > 0) scroll(w, (int) -Math.min(top, bottom - Game.height));
			else if (top < 0 && bottom < Game.height) scroll(w, (int) Math.min(-top, Game.height - bottom));
		}
	}

	/** Pressable things in reading order (top to bottom, then left to right). */
	static List<Ui.Part> buttons(Group g) {
		List<Ui.Part> out = new ArrayList<>();
		for (Ui.Part p : Ui.parts(g)) if (p.button && p.g instanceof Component) out.add(p);
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
		if (onToolbar) {
			List<Ui.Part> bs = toolbar();
			if (bs.isEmpty()) { onToolbar = false; }
			else {
				focus = Math.max(0, Math.min(bs.size() - 1, focus));
				switch (b) {
					case BACK: if (++focus >= bs.size()) onToolbar = false; return;
					case SELECT: onToolbar = false; if (held) Ui.hold(bs.get(focus).g); else Ui.press(bs.get(focus).g); return;
					case UP: case DOWN: onToolbar = false; break;   // back to the map, and do what the button does there
				}
			}
		}
		switch (b) {
			case DOWN: {
				refreshTargets(aiming);
				if (targets.isEmpty()) return;
				target = held ? (target + targets.size() - 1) % targets.size() : (target + 1) % targets.size();
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
				if (cell == -2) { Dungeon.hero.search(true); }
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

	/** Zoom steps: normal (16 px tiles), 1.5x, 2x, then far out (8 px) and back to normal. */
	private void zoom() {
		Camera c = Camera.main;
		if (c == null) return;
		if (c.zoom < PixelScene.maxZoom - 0.1f) key(SPDAction.ZOOM_IN);
		else for (float z = c.zoom; z > PixelScene.minZoom + 0.1f; z--) key(SPDAction.ZOOM_OUT);
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
		targets = aiming ? aimTargets() : walkTargets();
		target = 0;
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
		for (Mob m : ms) out.add(m.pos);
		// an item under you, then known items by walking distance
		if (L.heaps.get(h) != null) out.add(h);
		int[] dist = walk(L, h);
		List<Integer> items = new ArrayList<>();
		for (Heap hp : L.heaps.valueList()) if (hp.pos != h && dist[hp.pos] > 0 && (L.visited[hp.pos] || L.heroFOV[hp.pos])) items.add(hp.pos);
		items.sort(Comparator.comparingInt(c -> dist[c]));
		out.addAll(items);
		// the nearest edge of the unexplored, then the way down
		int edge = Explorer.nearestUnexplored(L, h, dist);
		if (edge >= 0) out.add(edge);
		int exit = L.exit();
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
		Window w = Ui.topWindow();
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (menu != null) {
			onToolbar = false;
			List<Ui.Part> bs = buttons(menu);
			if (focusOwner != menu) {
				focusOwner = menu; focus = 0;
				if (w != null && tall(w)) scroll(w, 100000);   // a new tall window starts at its top
			}
			if (bs.isEmpty()) { frame.hide(); if (w != null) keepInView(w, null); return; }
			Component c = (Component) bs.get(Math.max(0, Math.min(bs.size() - 1, focus))).g;
			if (w != null && keptFor != w) { keptFor = w; keepInView(w, c); }
			frame.around(menu, c.left() - 1, c.top() - 1, c.width() + 2, c.height() + 2, 0xFFFFFF44);
			return;
		}
		if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) { frame.hide(); return; }
		if (onToolbar) {
			List<Ui.Part> bs = toolbar();
			if (!bs.isEmpty()) {
				Component c = (Component) bs.get(Math.max(0, Math.min(bs.size() - 1, focus))).g;
				// on a layer of its own above the whole interface
				frame.around(overlay(), c.left() - 1, c.top() - 1, c.width() + 2, c.height() + 2, 0xFFFFFF44);
				return;
			}
		}
		if (!Dungeon.hero.ready) { frame.hide(); return; }
		refreshTargets(aiming());
		if (targets.isEmpty() || targets.get(Math.min(target, targets.size() - 1)) < 0) { frame.hide(); return; }
		int cell = targets.get(Math.min(target, targets.size() - 1));
		float x = (cell % Dungeon.level.width()) * DungeonTilemap.SIZE, y = (cell / Dungeon.level.width()) * DungeonTilemap.SIZE;
		frame.inWorld(x, y, DungeonTilemap.SIZE, DungeonTilemap.SIZE, aiming() ? 0xFFFF6644 : 0xFFFFFF44);
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

		void around(Group owner, float x, float y, float w, float h, int color) {
			for (int i = 0; i < 4; i++) {
				if (bar[i] == null || bar[i].parent != owner || colour[i] != color) {
					if (bar[i] != null) bar[i].killAndErase();
					bar[i] = new ColorBlock(1, 1, color); colour[i] = color;
					owner.add(bar[i]);
				}
				bar[i].visible = true;
				owner.bringToFront(bar[i]);
			}
			place(x, y, w, h, 1);
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

		private static boolean inScene(com.watabou.noosa.Gizmo g) {
			for (Group p = g.parent; p != null; p = p.parent) if (p == Game.scene()) return true;
			return false;
		}

		void hide() { for (ColorBlock b : bar) if (b != null) b.visible = false; }
	}
}
