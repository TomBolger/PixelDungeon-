package pebxel;

import com.badlogic.gdx.Graphics;
import com.badlogic.gdx.backends.headless.mock.graphics.MockGraphics;
import com.badlogic.gdx.graphics.GL20;

/** Pretends to be a phone screen that is never shown. */
public class WatchGraphics extends MockGraphics {
	public int w = 480, h = 800;
	public float delta = 1 / 30f;
	private final GL20 gl = NoGL.make();
	private final DisplayMode mode = new DisplayMode(480, 800, 60, 32) {};

	@Override public GL20 getGL20() { return gl; }
	@Override public int getWidth() { return w; }
	@Override public int getHeight() { return h; }
	@Override public int getBackBufferWidth() { return w; }
	@Override public int getBackBufferHeight() { return h; }
	@Override public float getDeltaTime() { return delta; }
	@Override public float getDensity() { return 2f; }
	@Override public float getPpiX() { return 320; }
	@Override public float getPpiY() { return 320; }
	@Override public DisplayMode getDisplayMode() { return mode; }
	@Override public DisplayMode getDisplayMode(Monitor m) { return mode; }
}
