package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndTabbed;
import com.watabou.noosa.Camera;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.Group;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.NinePatch;
import com.watabou.noosa.ui.Component;
import java.util.List;

/**
 * A window taller than the screen scrolls (Controls.scroll: Up/Down, a finger's drag) with its frame. Left at that,
 * its frame's top and bottom went off the screen and the window had no ends (Thomas: "no top and no bottom").
 * These are its frame's top and bottom edges held at the top and bottom of the screen's view over it (a camera of
 * their own that never moves), so the content scrolls inside a frame that stays whole. At either end of the scroll
 * they lie exactly on the window's own edges. A tabbed window's tabs stay under the bottom edge, over a dark strip
 * (the content scrolls behind). Made once per window (a few pictures, sent once); hidden while it fits.
 */
public final class Caps extends Group {
	private final Window w;
	private final NinePatch chrome, top, bot;
	private final ColorBlock strip, above, below, back;
	private final Camera fc;
	private final int mt, mb;

	/** Every frame (WatchGame.update): the top window's caps up to date. */
	public static void keep() {
		CLIP.clear();
		Window w = pebxel.Ui.topWindow();
		if (w == null || w.camera == null || !w.exists) return;
		Caps c = null;
		for (Gizmo g : GroupAccess.members(w)) if (g instanceof Caps) c = (Caps) g;
		boolean need = pebxel.Controls.tall(w) && !(w instanceof WatchWindow && ((WatchWindow) w).below > 0);   // (tabs hang under the frame: none of ours is ever tall)
		if (!need) { if (c != null && c.visible) { c.visible = false; c.pin(0); } return; }
		if (c == null) {
			NinePatch ch = chromeOf(w);
			if (ch == null || !ch.visible) return;   // (a round screen's frame is drawn by shapes: it has none)
			c = new Caps(w, ch); w.add(c);
		}
		c.visible = true;
		c.place();
		c.front();
		// the window's pictures (and its lists') are drawn only between the frame's ends, inside their margins: nothing
		// shows over or under the frame (a tabbed window's tabs hang under its foot: down to the view's foot there,
		// the foot's backing and the strip cover the rest)
		int[] in = inner(w);
		int[] clip = {in[0], tabsOf(w) != null ? pebxel.Controls.viewBottom(w) : in[1]};
		CLIP.put(w.camera, clip);
		panes(w, clip);
	}

	/** Cameras drawn only between two heights on the screen (phone px), this frame: Link sends them cut to that. */
	private static final java.util.Map<Camera, int[]> CLIP = new java.util.HashMap<>();
	public static int[] clip(Camera c) { return CLIP.isEmpty() ? null : CLIP.get(c); }

	private static void panes(Group g, int[] clip) {
		for (Gizmo m : GroupAccess.members(g)) {
			if (m instanceof com.shatteredpixel.shatteredpixeldungeon.ui.ScrollPane) {
				Camera pc = ((com.shatteredpixel.shatteredpixeldungeon.ui.ScrollPane) m).content().camera;
				if (pc != null) CLIP.put(pc, clip);
			}
			if (m instanceof Group && !(m instanceof Caps)) panes((Group) m, clip);
		}
	}

	/** A tall window's scroll area on the screen (phone px: top, bottom): between its frame's held ends, inside their
	 *  margins (the same rounding as place()). The view's ends for a window with no frame of this kind. */
	public static int[] inner(Window w) {
		int vt = pebxel.Controls.viewTop(w), vb = pebxel.Controls.viewBottom(w);
		NinePatch ch = chromeOf(w);
		Camera wc = w.camera;
		if (ch == null || !ch.visible || wc == null) return new int[]{vt, vb};
		float z = wc.zoom, under = (wc.scroll.y + wc.height) - (ch.y + ch.height);
		return new int[]{Math.round((Math.round(vt / z) + ch.marginTop()) * z), Math.round(Math.round(vb / z - under - ch.marginBottom()) * z)};
	}

	private Caps(Window w, NinePatch chrome) {
		this.w = w; this.chrome = chrome;
		mt = chrome.marginTop(); mb = chrome.marginBottom();
		int[] r = rect(chrome);
		strip = WatchHud.block(0x000000, 0, 0, 0, 0); add(strip);
		above = WatchHud.block(0x000000, 0, 0, 0, 0); add(above);
		below = WatchHud.block(0x000000, 0, 0, 0, 0); add(below);
		back = WatchHud.block(0x000000, 0, 0, 0, 0); add(back);   // (under the foot's picture: a tabbed window's content, cut lower down, never shows through it)
		top = new NinePatch(chrome.texture, r[0], r[1], r[2], mt, chrome.marginLeft(), mt, chrome.marginRight(), 0);
		bot = new NinePatch(chrome.texture, r[0], r[1] + r[3] - mb, r[2], mb, chrome.marginLeft(), 0, chrome.marginRight(), mb);
		for (NinePatch n : new NinePatch[]{top, bot}) {
			n.rm = chrome.rm; n.gm = chrome.gm; n.bm = chrome.bm; n.am = chrome.am;
			n.ra = chrome.ra; n.ga = chrome.ga; n.ba = chrome.ba; n.aa = chrome.aa;
			add(n);
		}
		Camera wc = w.camera;
		fc = new Camera(wc.x, 0, wc.width, (int) Math.ceil(Game.height / wc.zoom), wc.zoom);   // (the whole screen's height: never moves)
		Camera.add(fc);
		camera = fc;
	}

	/** Lined up with the window's camera (across) and the screen's view (down; Controls.viewTop/viewBottom). */
	private void place() {
		Camera wc = w.camera;
		float z = wc.zoom, vt = pebxel.Controls.viewTop(w) / z, vb = pebxel.Controls.viewBottom(w) / z;
		if (fc.width != wc.width) fc.resize(wc.width, fc.height);
		fc.x = wc.x; fc.y = 0;
		fc.scroll.set(wc.scroll.x, 0);
		// how far the window's camera reaches under its frame (a tabbed window's tabs), and how far it is scrolled
		// up from its lowest place (both in its units)
		float under = (wc.scroll.y + wc.height) - (chrome.y + chrome.height);
		float off = (wc.y + wc.screenHeight() - pebxel.Controls.viewBottom(w)) / z;
		float x = chrome.x, wd = chrome.width;
		top.x = bot.x = strip.x = above.x = below.x = x;
		if (top.width != wd) { top.size(wd, mt); bot.size(wd, mb); }
		top.y = Math.round(vt);
		bot.y = Math.round(vb - under - mb);
		set(strip, bot.y + mb, wd, under > 0 ? vb - (bot.y + mb) : 0);
		set(above, 0, wd, vt > 0 ? vt : 0);   // (round: the view is a band; nothing scrolls past its ends into sight)
		set(below, vb, wd, vb < fc.height ? fc.height - vb : 0);
		set(back, bot.y, wd - chrome.marginLeft() - chrome.marginRight(), under > 0 ? mb : 0);
		back.x = x + chrome.marginLeft();
		pin(off);
	}

	/** (tests) Where the frame's top and its foot (under a tabbed window's tabs) are on the screen, phone px. */
	public float[] ends() {
		return new float[]{fc.y + (top.y - fc.scroll.y) * fc.zoom, fc.y + (bot.y + mb + (strip.visible ? strip.height() : 0) - fc.scroll.y) * fc.zoom};
	}

	private static void set(ColorBlock b, float y, float w, float h) { b.y = y; b.size(w, Math.max(0, h)); b.visible = h > 0; }

	/** A tabbed window's tabs held at the view's foot (off: how far the window is scrolled up from its lowest place). */
	private void pin(float off) {
		List<Component> tabs = tabsOf(w);
		if (tabs == null) return;
		float y = Math.round(chrome.y + chrome.height - mb - off);
		for (Component t : tabs) if (t.top() != y) t.setPos(t.left(), y);
	}

	/** Over everything in the window but its tabs (over the caps) and the red close tag (Controls brings that to the
	 *  front each frame: no tug of war). */
	private void front() {
		List<Gizmo> ms = GroupAccess.members(w);
		List<Component> tabs = tabsOf(w);
		int i = ms.indexOf(this);
		boolean ok = true;
		for (int k = i + 1; k < ms.size() && ok; k++) {
			Gizmo g = ms.get(k);
			if (g != null && !(tabs != null && tabs.contains(g)) && !g.getClass().getSimpleName().equals("CloseTag")) ok = false;
		}
		if (ok && tabs != null) for (Component t : tabs) if (ms.indexOf(t) < i) ok = false;
		if (ok) return;
		w.bringToFront(this);
		if (tabs != null) for (Component t : tabs) w.bringToFront(t);
	}

	@Override
	public void destroy() {
		super.destroy();
		Camera.remove(fc);
	}

	private static java.lang.reflect.Field chromeF, outerF, tabsF;

	public static NinePatch chrome(Window w) { return chromeOf(w); }

	private static NinePatch chromeOf(Window w) {
		try {
			if (chromeF == null) { chromeF = Window.class.getDeclaredField("chrome"); chromeF.setAccessible(true); }
			return (NinePatch) chromeF.get(w);
		} catch (Exception e) { return null; }
	}

	@SuppressWarnings("unchecked")
	private static List<Component> tabsOf(Window w) {
		if (!(w instanceof WndTabbed)) return null;
		try {
			if (tabsF == null) { tabsF = WndTabbed.class.getDeclaredField("tabs"); tabsF.setAccessible(true); }
			return (List<Component>) tabsF.get(w);
		} catch (Exception e) { return null; }
	}

	/** The frame's picture in its texture: x, y, w, h (pixels). */
	private static int[] rect(NinePatch n) {
		try {
			if (outerF == null) { outerF = NinePatch.class.getDeclaredField("outterF"); outerF.setAccessible(true); }
			com.watabou.utils.RectF f = (com.watabou.utils.RectF) outerF.get(n);
			int tw = n.texture.width, th = n.texture.height;
			return new int[]{Math.round(f.left * tw), Math.round(f.top * th), Math.round(f.width() * tw), Math.round(f.height() * th)};
		} catch (Exception e) { throw new RuntimeException(e); }
	}
}
