package pebxel.ui;

import com.watabou.noosa.Visual;
import pebxel.WatchFonts;

/**
 * One line of text in a watch font, placed in watch pixels (use on a camera at zoom 2, where one unit is one
 * watch pixel). y is the top of the capitals. The watch draws it with its own system font.
 * Fonts: 0 Gothic 9, 1 Gothic 14, 2 Gothic 14 bold, 3 Gothic 18, 4 Gothic 18 bold, 5 Gothic 24, 6 Gothic 24 bold.
 */
public class WatchText extends Visual {
	public static final int G09 = 0, G14 = 1, G14B = 2, G18 = 3, G18B = 4, G24 = 5, G24B = 6;
	public int font;
	public boolean outline;
	private String text = "";

	public WatchText(int font, String text, int rgb) {
		super(0, 0, 0, 0);
		this.font = font;
		color(rgb);
		text(text);
	}

	public void color(int rgb) {
		rm = ((rgb >> 16) & 255) / 255f; gm = ((rgb >> 8) & 255) / 255f; bm = (rgb & 255) / 255f;
		ra = ga = ba = 0;
	}

	public String text() { return text; }

	public void text(String t) {
		text = t == null ? "" : t;
		width = width(font, text);
		height = WatchFonts.line(font);
	}

	/** Width of a text in watch pixels. */
	public static int width(int font, String s) {
		int w = 0;
		for (int i = 0; i < s.length(); i++) if (s.charAt(i) != '_') w += WatchFonts.advance(font, s.charAt(i));
		return w;
	}
}
