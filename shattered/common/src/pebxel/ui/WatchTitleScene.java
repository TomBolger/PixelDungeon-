package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;
import com.shatteredpixel.shatteredpixeldungeon.effects.BannerSprites;
import com.shatteredpixel.shatteredpixeldungeon.scenes.HeroSelectScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.RankingsScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.StartScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Image;

/**
 * Pebble PD's title screen: the arches, the logo, two big pictures (Play, Rankings) with labels, the version.
 * (Runs at interface zoom 2: one unit, one watch pixel.)
 */
public class WatchTitleScene extends PixelScene {
	@Override
	public void create() {
		super.create();
		com.shatteredpixel.shatteredpixeldungeon.Badges.loadGlobal();
		com.shatteredpixel.shatteredpixeldungeon.journal.Journal.loadGlobal();
		arches(this);
		Image logo = BannerSprites.get(BannerSprites.Type.TITLE_PORT);
		float s = logo.width > 200 ? 0.5f : 1f;   // whole pixels only
		logo.scale.set(s);
		logo.x = Math.round((200 - logo.width * s) / 2); logo.y = 6;
		add(logo);
		add(new Big(26, 104, 0, "Play") {
			@Override protected void onClick() {
				if (GamesInProgress.checkAll().size() == 0) {
					GamesInProgress.selectedClass = null;
					GamesInProgress.curSlot = 1;
					ShatteredPixelDungeon.switchScene(HeroSelectScene.class);
				} else ShatteredPixelDungeon.switchNoFade(StartScene.class);
			}
		});
		add(new Big(110, 104, 1, "Rankings") {
			@Override protected void onClick() { ShatteredPixelDungeon.switchNoFade(RankingsScene.class); }
		});
		WatchText v = new WatchText(WatchText.G14B, "v" + Game.version, 0xAAAAAA);
		WatchHud.place(v, 0, 228 - 18, 196, 2);
		add(v);
		fadeIn();
	}

	/** The arches background (Pebble PD's arcs.png, 64 px tiles). */
	static void arches(com.watabou.noosa.Group g) {
		for (int y = 0; y < 228; y += 64) for (int x = 0; x < 200; x += 64) {
			Image a = PdArt.image("arcs", 0, 0, 64, 64, 1); a.x = x; a.y = y; g.add(a);
		}
	}

	@Override
	protected void onBackPressed() { }   // (the watch's own Back leaves the app)

	/** A big picture (play.png at 2x) with its label under it; focus: white label and a white underline. */
	static class Big extends Button implements Focusable {
		private final WatchText label;
		private final ColorBlock line;
		Big(int x, int y, int pic, String text) {
			super();
			Image im = PdArt.image("play", pic * 32, 0, 32, 32, 2); im.x = x; im.y = y; add(im);
			label = new WatchText(WatchText.G18B, text, 0xAAAAAA);
			WatchHud.place(label, x, y + 62, 64, 1); add(label);
			line = WatchHud.block(0xFFFFFF, x + 12, 188, 40, 2); line.visible = false; add(line);
			setRect(x, y, 64, 84);
		}
		public void focus(boolean on) { label.color(on ? 0xFFFFFF : 0xAAAAAA); line.visible = on; }
	}
}
