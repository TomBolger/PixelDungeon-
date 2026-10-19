package pebxel;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.files.FileHandle;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.Texture;
import com.badlogic.gdx.graphics.g2d.BitmapFont;
import com.badlogic.gdx.graphics.g2d.TextureRegion;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import org.json.JSONObject;

/**
 * The watch's own fonts (Pebble's system Gothic), measured on the watch (tools: fonts.json and glyph
 * pictures in server/fonts). Shattered measures and wraps every text with these, so its windows are
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

	static File dir = new File(System.getProperty("pebxel.fonts", "../../../../../server/fonts"));
	private static JSONObject json;
	private static final HashMap<Integer, BitmapFont> fonts = new HashMap<>();

	private static JSONObject meta() {
		if (json == null) {
			try { json = new JSONObject(new String(java.nio.file.Files.readAllBytes(new File(dir, "fonts.json").toPath()), StandardCharsets.UTF_8)); }
			catch (Exception e) { throw new RuntimeException("watch fonts not found in " + dir.getAbsolutePath(), e); }
		}
		return json;
	}

	public static int line(int font) { return meta().getJSONObject(NAMES[font]).getInt("line"); }
	public static int advance(int font, char c) { return meta().getJSONObject(NAMES[font]).getJSONObject("glyphs").getJSONArray("" + (int) c).getInt(1); }

	/** Pixels above the capital letters in the watch font (the watch draws text that much higher). */
	public static int capTop(int font) { return meta().getJSONObject(NAMES[font]).getInt("capTop"); }

	/**
	 * A libGDX font for Shattered's text code, at the phone's scale (zoom x watch pixels).
	 * Glyph boxes run from the top of the capitals, so SPD's own alignment lands the capitals
	 * exactly where the watch draws them.
	 */
	public static synchronized BitmapFont font(int font, int zoom) {
		int key = font * 16 + zoom;
		BitmapFont f = fonts.get(key);
		if (f != null) return f;
		JSONObject m = meta().getJSONObject(NAMES[font]);
		int cell = m.getInt("cell"), cap = m.getInt("capTop"), base = m.getInt("base"), line = m.getInt("line");
		Pixmap src = new Pixmap(new FileHandle(new File(dir, NAMES[font] + ".png")));
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
		JSONObject gl = m.getJSONObject("glyphs");
		for (String k : gl.keySet()) {
			int ch = Integer.parseInt(k);
			BitmapFont.Glyph g = new BitmapFont.Glyph();
			g.id = ch;
			g.srcX = gl.getJSONArray(k).getInt(0) * zoom; g.srcY = cap * zoom;
			g.width = gl.getJSONArray(k).getInt(1) * zoom; g.height = (cell - cap) * zoom;
			g.xadvance = g.width; g.xoffset = 0; g.yoffset = 0;
			d.setGlyph(ch, g);
		}
		d.spaceXadvance = d.getGlyph(' ').xadvance;
		d.missingGlyph = d.getGlyph('?');
		f = new BitmapFont(d, new TextureRegion(tex), true);
		fonts.put(key, f);
		return f;
	}
}
