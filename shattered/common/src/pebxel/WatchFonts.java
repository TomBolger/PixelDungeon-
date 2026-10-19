package pebxel;

import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import java.util.HashMap;
import org.json.JSONObject;

/**
 * The watch's own fonts (Pebble's system Gothic), measured on the watch (pebxel/fonts: fonts.json and glyph pictures). Shattered measures and wraps every text with these, so its windows are
 * laid out for exactly the letters the watch will draw.
 */
public final class WatchFonts {
	public static final String[] NAMES = {"gothic09", "gothic14", "gothic14b", "gothic18", "gothic18b", "gothic24", "gothic24b"};

	/**
	 * Which watch font draws Shattered's text of a given size (in watch pixels; SPD's body text is 6,
	 * titles 9). The watch's pixels are small, so body text goes up a size for reading on the wrist.
	 */
	public static int forSize(int size) {
		if (size <= 5) return 0;      // tiny labels: quantities, levels
		if (size <= 8) return 1;      // body text
		if (size <= 10) return 2;     // titles
		if (size <= 13) return 4;
		return 6;
	}

	private static final HashMap<Integer, BitmapFont> fonts = new HashMap<>();

	/** One font's numbers: line height, cell height, cap top, baseline, and per character (x in picture, width). */
	private static final class Metrics { int line, cell, capTop, base; HashMap<Integer, int[]> glyphs = new HashMap<>(); }
	private static Metrics[] metrics;

	// (Android's org.json declares checked exceptions; read everything once, here)
	private static Metrics m(int font) {
		if (metrics == null) {
			try {
				JSONObject j = new JSONObject(WatchFiles.get("pebxel/fonts/fonts.json").readString("UTF-8"));
				Metrics[] ms = new Metrics[NAMES.length];
				for (int i = 0; i < NAMES.length; i++) {
					JSONObject f = j.getJSONObject(NAMES[i]);
					Metrics m = new Metrics();
					m.line = f.getInt("line"); m.cell = f.getInt("cell"); m.capTop = f.getInt("capTop"); m.base = f.getInt("base");
					JSONObject gl = f.getJSONObject("glyphs");
					for (java.util.Iterator<String> it = gl.keys(); it.hasNext(); ) {
						String k = it.next();
						m.glyphs.put(Integer.parseInt(k), new int[]{gl.getJSONArray(k).getInt(0), gl.getJSONArray(k).getInt(1)});
					}
					ms[i] = m;
				}
				metrics = ms;
			} catch (Exception e) { throw new RuntimeException("watch fonts", e); }
		}
		return metrics[font];
	}

	public static int[] glyph(int font, char c) { return m(font).glyphs.get((int) c); }
	public static int line(int font) { return m(font).line; }
	public static int advance(int font, char c) { int[] g = m(font).glyphs.get((int) c); return g == null ? 0 : g[1]; }

	/** Pixels above the capital letters in the watch font (the watch draws text that much higher). */
	public static int capTop(int font) { return m(font).capTop; }

	/**
	 * A libGDX font for Shattered's text code, at the phone's scale (zoom x watch pixels).
	 * Glyph boxes run from the top of the capitals, so SPD's own alignment lands the capitals
	 * exactly where the watch draws them.
	 */
	public static synchronized BitmapFont font(int font, int zoom) {
		int key = font * 16 + zoom;
		BitmapFont f = fonts.get(key);
		if (f != null) return f;
		Metrics m = m(font);
		int cell = m.cell, cap = m.capTop, base = m.base, line = m.line;
		Pixmap src = new Pixmap(WatchFiles.get("pebxel/fonts/" + NAMES[font] + ".png"));
		Pixmap pm = new Pixmap(src.getWidth() * zoom, src.getHeight() * zoom, Pixmap.Format.RGBA8888);
		pm.setBlending(Pixmap.Blending.None);
		for (int y = 0; y < src.getHeight(); y++) for (int x = 0; x < src.getWidth(); x++) {
			int c = src.getPixel(x, y);
			for (int k = 0; k < zoom * zoom; k++) pm.drawPixel(x * zoom + k % zoom, y * zoom + k / zoom, c);
		}
		src.dispose();
		Texture tex = new Texture(pm);
		BitmapFont.BitmapFontData d = new BitmapFont.BitmapFontData();
		d.flipped = true;
		d.lineHeight = line * zoom;
		d.capHeight = (base - cap + 1) * zoom;
		d.ascent = 0;
		d.descent = -(cell - base - 1) * zoom;
		d.down = d.lineHeight;
		for (java.util.Map.Entry<Integer, int[]> e : m.glyphs.entrySet()) {
			BitmapFont.Glyph g = new BitmapFont.Glyph();
			g.id = e.getKey();
			g.srcX = e.getValue()[0] * zoom; g.srcY = cap * zoom;
			g.width = e.getValue()[1] * zoom; g.height = (cell - cap) * zoom;
			g.xadvance = g.width; g.xoffset = 0; g.yoffset = 0;
			d.setGlyph(g.id, g);
		}
		d.spaceXadvance = d.getGlyph(' ').xadvance;
		d.missingGlyph = d.getGlyph('?');
		f = new BitmapFont(d, new TextureRegion(tex), true);
		fonts.put(key, f);
		return f;
	}
}
