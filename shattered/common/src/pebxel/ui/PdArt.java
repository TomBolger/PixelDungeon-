package pebxel.ui;

import com.badlogic.gdx.graphics.Pixmap;
import com.watabou.gltextures.SmartTexture;
import com.watabou.gltextures.TextureCache;
import com.watabou.noosa.Image;
import pebxel.WatchFiles;

/** Pebble PD's own interface art (common/assets/pebxel/pd), already in Pebble colours. */
public final class PdArt {
	public static String key(String name) { return "pebxel/pd/" + name + ".png"; }

	/** The texture, named by its path so the link sends it by its picture id. */
	public static synchronized SmartTexture tex(String name) {
		String k = key(name);
		if (!TextureCache.contains(k)) {
			Pixmap src = new Pixmap(WatchFiles.get(k));
			SmartTexture t = TextureCache.create(k, src.getWidth(), src.getHeight());
			t.bitmap.drawPixmap(src, 0, 0);
			src.dispose();
		}
		return TextureCache.get(k);
	}

	/** A piece of a picture, scaled (Pebble PD draws icons at 2x, the status pane at 1.5x). */
	public static Image image(String name, int x, int y, int w, int h, float scale) {
		Image im = new Image(tex(name), x, y, w, h);
		im.scale.set(scale);
		return im;
	}
}
