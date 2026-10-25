package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;

/** Shattered PD exactly as shipped, except it never draws. */
public class WatchGame extends ShatteredPixelDungeon {
	/** One step from cell to cell (CharSprite's move), in game seconds: about 133 ms of real time, as in Pebble PD (132:
	 *  the watch is told it in 4 ms units, Controls.plan). */
	public static final float STEP = System.getenv("PEBXEL_STEP") != null ? Float.parseFloat(System.getenv("PEBXEL_STEP")) : 0.132f;   // (tests: PEBXEL_STEP)
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
		// the game's animations at Shattered's own pace (Thomas, 2026-10-25: "let's reset animations": they ran 1.5 x, which
		// once hid slowness since fixed). The watch's clock must match (scene.h GAME_SPEED)
		com.watabou.noosa.Game.pebxelSpeed = 1f;
		// but not walking: a step takes Pebble PD's 133 ms of real time (STEP), each one easy to follow, as Thomas approved
		com.shatteredpixel.shatteredpixeldungeon.sprites.CharSprite.setMoveInterval(STEP);
		Sounds.install();
		// the story on the loading screen in pages that fit
		com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene.pebxelPages = pebxel.ui.StoryPages::split;
	}
	/** Set to draw the next frame (only when taking pictures); normally nothing is drawn. */
	public boolean drawNext;
	/** Run the drawing code every frame (with the do-nothing graphics chip): some pictures, like
	 *  the fog of war, only bring themselves up to date while being drawn. The watch link needs this. */
	public boolean drawAlways;
	@Override protected void update() {
		super.update();
		pebxel.ui.Caps.keep();   // (a window taller than the screen: its frame's ends held at the screen's)
	}
	@Override protected void draw() { if (drawNext || drawAlways) { drawNext = false; super.draw(); } }
}
