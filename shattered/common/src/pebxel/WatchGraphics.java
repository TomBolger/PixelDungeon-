package pebxel;

import com.badlogic.gdx.Graphics;
import com.badlogic.gdx.backends.headless.mock.graphics.MockGraphics;
import com.badlogic.gdx.graphics.GL20;

/** Pretends to be a phone screen that is never shown. */
public class WatchGraphics extends MockGraphics {
	//twice the watch's screen (two phone pixels per watch pixel)
	public int w = Screen.w * 2, h = Screen.h * 2;
	public float delta = 1 / 30f;
	public SoftGL soft;
	private GL20 gl = NoGL.make();
	private final DisplayMode mode = new DisplayMode(w, h, 60, 32) {};

	public void useSoftGL() { soft = new SoftGL(w, h); gl = soft.proxy(); }

	/** Bots in a hurry: a chip that does nothing at all (the tracking one still cleared its picture every frame). */
	public void useNoGL() { soft = null; gl = NoGL.make(); }

	/** On the phone: the software chip only keeps track (no pictures), so the link can capture odd pictures. */
	public void useTrackingGL() { useSoftGL(); soft.keepPixels = false; soft.raster = false; }

	@Override public GL20 getGL20() { return gl; }
	@Override public int getWidth() { return w; }
	@Override public int getHeight() { return h; }
	@Override public int getBackBufferWidth() { return w; }
	@Override public int getBackBufferHeight() { return h; }
	@Override public float getDeltaTime() { return delta; }
	@Override public float getDensity() { return 1f; }
	@Override public float getPpiX() { return 160; }
	@Override public float getPpiY() { return 160; }
	@Override public DisplayMode getDisplayMode() { return mode; }
	@Override public DisplayMode getDisplayMode(Monitor m) { return mode; }
}
