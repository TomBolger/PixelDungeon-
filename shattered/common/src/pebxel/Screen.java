package pebxel;

/** The watch's screen: Emery 200x228, Gabbro 260x260 round. Told by the watch in its hello (KEY_SCREEN). */
public final class Screen {
	public static int w = 200, h = 228;
	public static boolean round;

	/** From the hello: round << 24 | width << 12 | height. */
	public static void set(int code) {
		if (code <= 0) return;
		round = (code >> 24 & 1) != 0; w = code >> 12 & 0xFFF; h = code & 0xFFF;
	}

	public static void set(String name) {
		if ("gabbro".equals(name)) { w = 260; h = 260; round = true; }
		else if ("emery".equals(name)) { w = 200; h = 228; round = false; }
	}

	public static int code() { return (round ? 1 << 24 : 0) | w << 12 | h; }
}
