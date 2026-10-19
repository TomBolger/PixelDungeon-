package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;

/** Shattered PD exactly as shipped, except it never draws. */
public class WatchGame extends ShatteredPixelDungeon {
	public WatchGame() { super(new WatchPlatform()); }
	@Override protected void draw() { }
}
