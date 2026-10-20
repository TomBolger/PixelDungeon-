package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Camera;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Group;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.NinePatch;
import com.watabou.noosa.Visual;
import java.util.ArrayList;
import java.util.List;
import pebxel.WatchFonts;

/**
 * A Pebble PD window: its chrome frame round the content, a black well under the body, the game behind dimmed by
 * a 50% black checker, laid out in watch pixels (a camera at zoom 2: one unit, one watch pixel).
 * Content coordinates start at (0, 0) in the window's top-left corner inside the frame.
 */
public class WatchWindow extends Window {
	public static final int ICTERINE = 0xFFFF55, PASTEL = 0xFFFFAA, CHROME_YELLOW = 0xFFAA00, WHITE = 0xFFFFFF, LIGHT = 0xAAAAAA;
	protected int cw, ch;
	/** Room under the frame (tabs hang there). */
	protected int below;
	/** Laid out to fill the screen exactly (its frame may go past the edges): never scrolled. */
	public boolean fixed;
	protected final ColorBlock dim;
	protected final boolean tabbed;
	/** Where the content's top-left is on the screen (watch pixels). */
	protected int contentX, contentY;
	/** Round screens: Pebble PD's round frame (a box cut by circles) in place of the chrome. */
	protected final Group roundFrame = new Group();
	public static final int R_OUT = 127, R_IN = 121;

	/** A window w x h (content) at x, y on the screen (watch pixels; -1: centred). Tabbed windows use the grey-bodied frame. */
	public WatchWindow(int x, int y, int w, int h, boolean tabbed) {
		super(w, h, new NinePatch(PdArt.tex("chrome"), tabbed ? 64 : 0, 0, 22, 22, 7));
		this.tabbed = tabbed;
		cw = w; ch = h;
		shadow.visible = false;
		dim = new ColorBlock(1, 1, 0xFF000000);
		dim.camera = PixelScene.uiCamera;
		dim.size(PixelScene.uiCamera.width, PixelScene.uiCamera.height);
		dim.alpha(0.5f);
		GroupAccess.addAfter(this, dim, shadow);
		// a tap outside closes the window, but its tabs hang below the frame: they count as inside
		remove(blocker); blocker.destroy();   // (remove, not erase: an erased slot is reused by the next add, under the frame)
		blocker = new com.watabou.noosa.PointerArea(0, 0, PixelScene.uiCamera.width, PixelScene.uiCamera.height) {
			@Override protected void onClick(com.watabou.input.PointerEvent e) {
				if (WatchWindow.this.parent == null) return;
				float x = e.current.x, y = e.current.y;
				Camera c = WatchWindow.this.camera;
				boolean inside = x >= c.x && x < c.x + c.width * c.zoom && y >= c.y && y < c.y + c.height * c.zoom;
				if (!inside) onBackPressed();
			}
		};
		blocker.camera = PixelScene.uiCamera;
		GroupAccess.addAfter(this, blocker, shadow);
		if (pebxel.Screen.round) { chrome.visible = false; GroupAccess.addAfter(this, roundFrame, chrome); }
		place(x, y);
	}

	/** Where a window w wide starts: Pebble PD's x 10 on Emery; centred on a round screen. */
	public static int left(int w) { return pebxel.Screen.round ? (pebxel.Screen.w - w) / 2 : 10; }

	/** Put the content's top-left at x, y (watch pixels; -1 centres). */
	public void place(int x, int y) {
		Camera.remove(camera);
		camera = new Camera(0, 0, (int) chrome.width, (int) chrome.height + below, 2);
		int cx = x < 0 ? (WatchHud.W - cw) / 2 : x, cy = y < 0 ? (WatchHud.H - ch) / 2 : y;
		camera.x = (int) ((cx + chrome.x) * 2);
		camera.y = (int) ((cy + chrome.y) * 2);
		camera.scroll.set(chrome.x, chrome.y);
		Camera.add(camera);
		contentX = cx; contentY = cy;
		if (pebxel.Screen.round) roundPanel();
	}

	/** Pebble PD's rpanel: light edge and grey border cut by R_OUT, black under the body cut by R_IN + 1, the body
	 *  (black, or grey with tabs) cut by R_IN. Coordinates relative to the screen's centre. */
	protected void roundPanel() {
		roundFrame.clear();
		int ox = pebxel.Screen.w / 2 - contentX, oy = pebxel.Screen.h / 2 - contentY;   // the screen's centre in content coordinates
		int x0 = -6, y0 = -6, x1 = cw + 6, y1 = ch + 6;
		roundFrame.add(Shape.cut(ox, oy, R_OUT, x0 - ox, y0 - oy, x1 - x0, y1 - y0, 0xAAAAAA));
		roundFrame.add(Shape.cut(ox, oy, R_OUT - 2, x0 + 1 - ox, y0 + 1 - oy, x1 - x0 - 2, y1 - y0 - 2, 0x555555));
		roundFrame.add(Shape.cut(ox, oy, R_IN + 1, -1 - ox, -1 - oy, cw + 2, ch + 2, 0x000000));
		roundFrame.add(Shape.cut(ox, oy, R_IN, -ox, -oy, cw, ch, tabbed ? 0x555555 : 0x000000));
	}

	@Override
	public void resize(int w, int h) {
		int cx = (int) (camera.x / 2 - chrome.x), cy = (int) (camera.y / 2 - chrome.y);
		super.resize(w, h);
		cw = w; ch = h;
		place(cx, cy);
	}

	/** Black under a stretch of the body (Pebble PD's WELL: 2 px wider each side). */
	protected Visual well(int x, int y, int w, int h) {
		if (pebxel.Screen.round) {
			int ox = pebxel.Screen.w / 2 - contentX, oy = pebxel.Screen.h / 2 - contentY;
			Shape sh = Shape.cut(ox, oy, R_IN, x - 2 - ox, y - oy, w + 4, h, 0x000000);
			add(sh);
			return sh;
		}
		ColorBlock b = WatchHud.block(0x000000, x - 2, y, w + 4, h);
		add(b);
		return b;
	}

	/** A line of text by the top of its box: align 0 left, 1 centre, 2 right in width w. */
	protected WatchText text(String s, int font, int rgb, int x, int y, int w, int align) {
		WatchText t = new WatchText(font, s, rgb);
		WatchHud.place(t, x, y, w, align);
		add(t);
		return t;
	}

	/** Wrapped text with Shattered's _highlights_ in yellow; returns its height. */
	public static int textBlock(Group into, String s, int font, int rgb, int x, int y, int w) {
		int lh = lineHeight(font);
		int line = 0;
		for (List<String[]> segs : layout(s, font, w)) {
			int px = x;
			for (String[] seg : segs) {
				WatchText t = new WatchText(font, seg[0], seg[1] != null ? ICTERINE : rgb);
				WatchHud.place(t, px, y + line * lh, 0, 0);
				into.add(t);
				px += WatchText.width(font, seg[0]);
			}
			line++;
		}
		return line * lh;
	}

	// ---- round screens: lines shortened to the circle (Pebble PD's screen text flow, 12 px in from the edge) ----

	/** The x range (content coordinates) a row band y..y+h of content may use inside a circle of radius r. */
	public int[] chord(int y, int h, int r, int x, int w) {
		int ox = pebxel.Screen.w / 2 - contentX, oy = pebxel.Screen.h / 2 - contentY;
		int d = Math.max(Math.abs(y - oy), Math.abs(y + h - oy));
		int half = d >= r ? 0 : (int) Math.sqrt(r * r - d * d);
		int a = Math.max(x, ox - half), b = Math.min(x + w, ox + half);
		return new int[]{a, Math.max(0, b - a)};
	}

	/** Wrapped text that follows the circle on a round screen (as textBlock elsewhere); returns its height. */
	public int flowText(Group into, String s, int font, int rgb, int x, int y, int w) {
		if (!pebxel.Screen.round) return textBlock(into, s, font, rgb, x, y, w);
		int lh = lineHeight(font);
		int r = pebxel.Screen.w / 2 - 12;
		List<List<String[]>> lines = layoutBy(s, font, i -> chord(y + i * lh, lh, r, x, w)[1]);
		for (int i = 0; i < lines.size(); i++) {
			int px = chord(y + i * lh, lh, r, x, w)[0];
			for (String[] seg : lines.get(i)) {
				WatchText t = new WatchText(font, seg[0], seg[1] != null ? ICTERINE : rgb);
				WatchHud.place(t, px, y + i * lh, 0, 0);
				into.add(t);
				px += WatchText.width(font, seg[0]);
			}
		}
		return lines.size() * lh;
	}

	/** Height flowText would take. */
	public int flowHeight(String s, int font, int x, int y, int w) {
		if (!pebxel.Screen.round) return textHeight(s, font, w);
		int lh = lineHeight(font);
		return layoutBy(s, font, i -> chord(y + i * lh, lh, pebxel.Screen.w / 2 - 12, x, w)[1]).size() * lh;
	}

	/** A button narrowed to the circle (Pebble PD's rfit: radius R_IN - 2 at its farther row). */
	public void fit(com.shatteredpixel.shatteredpixeldungeon.ui.Button b) {
		if (!pebxel.Screen.round) return;
		int[] c = chord((int) b.top(), (int) b.height() - 1, R_IN - 2, (int) b.left(), (int) b.width());
		b.setRect(c[0], b.top(), c[1], b.height());
	}

	public static int lineHeight(int font) { return WatchFonts.line(font) - (font <= 2 ? 2 : 0); }

	public static int textHeight(String s, int font, int w) { return layout(s, font, w).size() * lineHeight(font); }

	/** Lines of segments {text, highlighted?} no wider than w. */
	static List<List<String[]>> layout(String s, int font, int w) { return layoutBy(s, font, i -> w); }

	/** As layout, each line i no wider than width(i). */
	static List<List<String[]>> layoutBy(String s, int font, java.util.function.IntUnaryOperator width) {
		List<List<String[]>> lines = new ArrayList<>();
		boolean hl = false;
		for (String para : s.split("\n", -1)) {
			List<String[]> cur = new ArrayList<>();
			int curW = 0;
			for (String word : para.split(" ")) {
				// the word's pieces (underscores switch highlighting)
				List<String[]> parts = new ArrayList<>();
				StringBuilder b = new StringBuilder();
				for (char c : word.toCharArray()) {
					if (c == '_') { if (b.length() > 0) parts.add(new String[]{b.toString(), hl ? "" : null}); b.setLength(0); hl = !hl; }
					else b.append(c);
				}
				if (b.length() > 0) parts.add(new String[]{b.toString(), hl ? "" : null});
				int ww = 0; for (String[] p : parts) ww += WatchText.width(font, p[0]);
				int space = cur.isEmpty() ? 0 : WatchText.width(font, " ");
				if (!cur.isEmpty() && curW + space + ww > width.applyAsInt(lines.size())) { lines.add(cur); cur = new ArrayList<>(); curW = 0; space = 0; }
				if (space > 0) { cur.add(new String[]{" ", null}); curW += space; }
				cur.addAll(parts); curW += ww;
			}
			lines.add(cur);
		}
		// merge neighbouring pieces of the same colour (fewer pictures for the watch)
		for (int k = 0; k < lines.size(); k++) {
			List<String[]> merged = new ArrayList<>();
			for (String[] seg : lines.get(k)) {
				String[] last = merged.isEmpty() ? null : merged.get(merged.size() - 1);
				if (last != null && ((last[1] == null) == (seg[1] == null) || seg[0].equals(" "))) last[0] = last[0] + seg[0];
				else merged.add(new String[]{seg[0], seg[1]});
			}
			lines.set(k, merged);
		}
		return lines;
	}

	/** Pebble PD's tab (tab_art): fills joining the window's bottom edge; the chosen one opens into the window.
	 *  x, y: the tab's top-left in content coordinates (y = the frame's bottom line). */
	public static void tabArt(Group into, int x, int cb, int w, int h, boolean selected) {
		int L = 0xAAAAAA, D = 0x555555, K = 0x000000;
		int bot = cb + h - (selected ? 0 : 1), top = selected ? cb : cb + 4;
		int kt = selected ? cb : top, lt = selected ? cb + 1 : top, dt = selected ? cb + 2 : top;
		if (selected) fill(into, x + 4, cb, w - 8, 4, D);
		fill(into, x + 4, top, w - 8, bot - 4 - top, D);
		fill(into, x, lt, 1, bot - 1 - lt, L); fill(into, x + 1, dt, 2, bot - dt, D); fill(into, x + 3, kt, 1, bot - 4 - kt, K);
		fill(into, x + w - 4, kt, 1, bot - 4 - kt, K); fill(into, x + w - 3, lt, 1, bot - 2 - lt, L); fill(into, x + w - 2, dt, 2, bot - dt, D);
		fill(into, x, bot - 1, 1, 1, D); fill(into, x + w - 3, bot - 2, 1, 2, D);
		fill(into, x + 3, bot - 4, w - 6, 1, K); fill(into, x + 3, bot - 3, w - 5, 1, L); fill(into, x + 1, bot - 2, w - 4, 2, D);
	}

	static void fill(Group into, int x, int y, int w, int h, int rgb) { if (w > 0 && h > 0) into.add(WatchHud.block(rgb, x, y, w, h)); }

	/** Is this window taller than the screen (it scrolls)? */
	public boolean tall() { return camera.screenHeight() > Game.height + 1; }
}
