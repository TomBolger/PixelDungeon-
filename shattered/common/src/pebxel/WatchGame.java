package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;

/** Shattered PD exactly as shipped, except it never draws. */
public class WatchGame extends ShatteredPixelDungeon {
	public WatchGame() {
		super(new WatchPlatform());
		String z = System.getenv("PEBXEL_ZOOM");
		// one and a half times Shattered's own size on the watch (like the Pixel Dungeon port's big buttons); the
		// title screen, first-run welcome and credits need more room than that leaves, so they stay at 2
		com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.pebxelZoom = z != null ? Integer.parseInt(z) : 3;
		java.util.Collections.addAll(com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.pebxelSmaller,
				"TitleScene", "WelcomeScene", "AboutScene", "ChangesScene", "NewsScene", "SupporterScene", "WatchTitleScene", "WatchHeroSelect", "WatchWelcome");
		// Pebble PD's own windows in place of some of Shattered's
		com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.pebxelWindows = pebxel.ui.Swaps::window;
		com.watabou.noosa.Game.pebxelScenes = pebxel.ui.Swaps::scene;
	}
	/** Set to draw the next frame (only when taking pictures); normally nothing is drawn. */
	public boolean drawNext;
	/** Run the drawing code every frame (with the do-nothing graphics chip): some pictures, like
	 *  the fog of war, only bring themselves up to date while being drawn. The watch link needs this. */
	public boolean drawAlways;
	@Override protected void draw() { if (drawNext || drawAlways) { drawNext = false; super.draw(); } }
}
