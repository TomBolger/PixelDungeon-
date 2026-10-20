package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.RenderedTextBlock;
import java.util.ArrayList;
import java.util.List;

/**
 * The story on the loading screen (a new part of the dungeon) in pages that fit the watch, with the Continue button
 * under them: whole sentences to a page where they fit, otherwise words. (Sizes in Shattered's units.)
 */
public class StoryPages {
	static final int MAX_H = 84;

	public static String[] split(String text) {
		int w = pebxel.Screen.round ? 110 : 125;
		InterlevelScene.pebxelStoryWidth = w;
		List<String> pages = new ArrayList<>();
		String cur = "";
		// pieces: sentences (a paragraph break kept with the sentence after it)
		for (String piece : text.split("(?<=[.!?]) +")) {
			String tryIt = cur.isEmpty() ? piece : cur + " " + piece;
			if (cur.isEmpty() || fits(tryIt, w)) { cur = tryIt; continue; }
			pages.add(cur.trim()); cur = piece;
		}
		if (!cur.isEmpty()) pages.add(cur.trim());
		// a sentence too long for a page on its own: by words
		List<String> out = new ArrayList<>();
		for (String pg : pages) {
			if (fits(pg, w)) { out.add(pg); continue; }
			String part = "";
			for (String word : pg.split(" ")) {
				String tryIt = part.isEmpty() ? word : part + " " + word;
				if (part.isEmpty() || fits(tryIt, w)) part = tryIt; else { out.add(part); part = word; }
			}
			if (!part.isEmpty()) out.add(part);
		}
		return out.toArray(new String[0]);
	}

	private static boolean fits(String s, int w) {
		RenderedTextBlock b = PixelScene.renderTextBlock(s.replaceFirst("^\\n+", ""), 6);
		b.maxWidth(w);
		boolean ok = b.height() <= MAX_H;
		b.destroy();
		return ok;
	}
}
