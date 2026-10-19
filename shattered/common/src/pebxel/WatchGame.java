package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;

/** Shattered PD exactly as shipped, except it never draws. */
public class WatchGame extends ShatteredPixelDungeon {
	public WatchGame() { super(new WatchPlatform()); }
	/** Set to draw the next frame (only when taking pictures); normally nothing is drawn. */
	public boolean drawNext;
	/** Run the drawing code every frame (with the do-nothing graphics chip): some pictures, like
	 *  the fog of war, only bring themselves up to date while being drawn. The watch link needs this. */
	public boolean drawAlways;
	@Override protected void draw() { if (drawNext || drawAlways) { drawNext = false; super.draw(); } }
}
