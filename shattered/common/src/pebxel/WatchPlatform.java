package pebxel;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.graphics.Pixmap;
import com.badlogic.gdx.graphics.g2d.PixmapPacker;
import com.badlogic.gdx.graphics.g2d.freetype.FreeTypeFontGenerator;
import com.watabou.utils.PlatformSupport;
import java.util.HashMap;
import java.util.regex.Pattern;

/** Platform hooks for the screenless game. Fonts still load: SPD measures text to lay out windows. */
public class WatchPlatform extends PlatformSupport {
	@Override public void updateDisplaySize() { }
	@Override public void updateSystemUI() { }
	@Override public boolean connectedToUnmeteredNetwork() { return false; }
	@Override public boolean supportsVibration() { return true; }
	@Override public void vibrate(int ms) { Watch.event("vibe", ms); }

	private static FreeTypeFontGenerator basic, asian;

	@Override public void setupFontGenerators(int pageSize, boolean systemfont) {
		if (fonts != null && this.pageSize == pageSize && this.systemfont == systemfont) return;
		this.pageSize = pageSize; this.systemfont = systemfont;
		resetGenerators(false);
		fonts = new HashMap<>();
		//text is only measured here (the watch draws its own), so the pixel font serves every language
		basic = asian = new FreeTypeFontGenerator(Gdx.files.internal("fonts/pixel_font.ttf"));
		fonts.put(basic, new HashMap<>());
		packer = new PixmapPacker(pageSize, pageSize, Pixmap.Format.RGBA8888, 1, false);
	}

	private static final Pattern ASIAN = Pattern.compile("\\p{InHangul_Syllables}|\\p{InCJK_Unified_Ideographs}|" +
			"\\p{InCJK_Symbols_and_Punctuation}|\\p{InHalfwidth_and_Fullwidth_Forms}|\\p{InHiragana}|\\p{InKatakana}");
	@Override protected FreeTypeFontGenerator getGeneratorForString(String s) {
		return ASIAN.matcher(s).find() ? asian : basic;
	}

	/** Shattered asks for a font in phone pixels (UI zoom 2); answer with the watch font for that size. */
	@Override
	public com.badlogic.gdx.graphics.g2d.BitmapFont getFont(int size, String text, boolean flipped, boolean border) {
		return WatchFonts.font(font(size), 2);
	}

	// Shattered's text layout, with the watch's font sizes (see the patch to RenderedText/RenderedTextBlock)
	@Override public float textHeight(int size) { return (WatchFonts.line(font(size)) - 2) * 2; }
	@Override public float textSpace(int size) { return WatchFonts.advance(font(size), ' '); }
	@Override public float textKern(int size) { return 0; }
	private static int font(int size) { return WatchFonts.forSize(Math.round(size / 2f)); }

	private static final Pattern SPLIT = Pattern.compile("(?<=\n)|(?=\n)|(?<=_)|(?=_)|(?<=\\*\\*)|(?=\\*\\*)");
	private static final Pattern SPLIT_ML = Pattern.compile("(?<= )|(?= )|(?<=\n)|(?=\n)|(?<=_)|(?=_)|(?<=\\*\\*)|(?=\\*\\*)");
	@Override public String[] splitforTextBlock(String text, boolean multiline) {
		return (multiline ? SPLIT_ML : SPLIT).split(text);
	}
}
