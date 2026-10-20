package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Camera;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Group;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.NinePatch;
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
	protected final ColorBlock dim;

	/** A window w x h (content) at x, y on the screen (watch pixels; -1: centred). Tabbed windows use the grey-bodied frame. */
	public WatchWindow(int x, int y, int w, int h, boolean tabbed) {
		super(w, h, new NinePatch(PdArt.tex("chrome"), tabbed ? 64 : 0, 0, 22, 22, 7));
		cw = w; ch = h;
		shadow.visible = false;
		dim = new ColorBlock(1, 1, 0xFF000000);
		dim.camera = PixelScene.uiCamera;
		dim.size(PixelScene.uiCamera.width, PixelScene.uiCamera.height);
		dim.alpha(0.5f);
		GroupAccess.addAfter(this, dim, shadow);
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
	}

	@Override
	public void resize(int w, int h) {
		int cx = (int) (camera.x / 2 - chrome.x), cy = (int) (camera.y / 2 - chrome.y);
		super.resize(w, h);
		cw = w; ch = h;
		place(cx, cy);
	}

	/** Black under a stretch of the body (Pebble PD's WELL: 2 px wider each side). */
	protected ColorBlock well(int x, int y, int w, int h) {
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

	public static int lineHeight(int font) { return WatchFonts.line(font) - (font <= 2 ? 2 : 0); }

	public static int textHeight(String s, int font, int w) { return layout(s, font, w).size() * lineHeight(font); }

	/** Lines of segments {text, highlighted?} no wider than w. */
	static List<List<String[]>> layout(String s, int font, int w) {
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
				if (!cur.isEmpty() && curW + space + ww > w) { lines.add(cur); cur = new ArrayList<>(); curW = 0; space = 0; }
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
