package pebxel;

import com.badlogic.gdx.graphics.Pixmap;
import java.util.*;

/**
 * The art both sides share. Every picture file in Shattered's asset folders gets a
 * number (sorted by path, starting at 1); the watch app is built with the same list.
 */
public final class Assets {
	public final List<String> paths = new ArrayList<>();
	private final HashMap<String, Integer> ids = new HashMap<>();

	/** The list made by tools/pack_art.py (pebxel/art.txt): one picture path per line, id = line number. */
	public Assets() {
		for (String line : WatchFiles.get("pebxel/art.txt").readString("UTF-8").split("\n")) {
			line = line.trim();
			if (!line.isEmpty()) paths.add(line);
		}
		for (int i = 0; i < paths.size(); i++) ids.put(paths.get(i), i + 1);
		if (paths.size() > 199) throw new IllegalStateException("too many pictures for one-byte ids: " + paths.size());
	}

	public Integer id(String path) { return ids.get(path); }

	/** One byte per pixel in Pebble's format: 2 bits each of alpha, red, green, blue. */
	public static byte[] pebble(Pixmap p) {
		int w = p.getWidth(), h = p.getHeight();
		byte[] out = new byte[w * h];
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			int c = p.getPixel(x, y); // RGBA8888
			out[y * w + x] = pebble((c >>> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, c & 255);
		}
		return out;
	}

	public static byte pebble(int r, int g, int b, int a) {
		return (byte) ((q(a) << 6) | (q(r) << 4) | (q(g) << 2) | q(b));
	}

	private static int q(int v) { return Math.min(3, (v + 42) / 85); }
}
