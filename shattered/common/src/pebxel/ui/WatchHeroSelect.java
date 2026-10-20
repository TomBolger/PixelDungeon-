package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Assets;
import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.ActionIndicator;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndHeroInfo;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Image;

/**
 * Pebble PD's hero select: the arches, the "Select Your Hero" banner, the classes in a grid (Shattered has six:
 * three by two), each its portrait at 2x and its name; the chosen one boxed in yellow; a New Game button under them.
 * Choosing a chosen class again shows what it starts with.
 */
public class WatchHeroSelect extends PixelScene {
	private final java.util.List<Cell> cells = new java.util.ArrayList<>();

	@Override
	public void create() {
		super.create();
		pebxel.Sounds.theme();
		Dungeon.hero = null;
		com.shatteredpixel.shatteredpixeldungeon.Badges.loadGlobal();
		com.shatteredpixel.shatteredpixeldungeon.journal.Journal.loadGlobal();
		WatchTitleScene.arches(this);
		boolean round = pebxel.Screen.round;
		Image banner = PdArt.image("banner_select", 0, 0, 128, 21, 1); banner.x = (pebxel.Screen.w - 128) / 2; banner.y = round ? 22 : 4; add(banner);
		HeroClass[] cls = HeroClass.values();
		for (int i = 0; i < cls.length && i < 6; i++) {
			// (round: narrower cells, closer together, so the top row's corners stay inside the circle)
			Cell c = round ? new Cell(cls[i], 42 + (i % 3) * 60, 46 + (i / 3) * 76, 56) : new Cell(cls[i], 4 + (i % 3) * 64, 28 + (i / 3) * 78, 64);
			cells.add(c); add(c);
		}
		WatchButton start = new WatchButton("New Game") {
			@Override protected void onClick() { start(); }
		};
		if (round) start.setRect(60, 200, 140, 38); else start.setRect(4, 186, 192, 38);
		add(start);
		if (GamesInProgress.selectedClass == null) GamesInProgress.selectedClass = HeroClass.WARRIOR;
		refresh();
		fadeIn();
	}

	private void start() {
		if (GamesInProgress.selectedClass == null || !GamesInProgress.selectedClass.isUnlocked()) return;
		Dungeon.hero = null;
		Dungeon.daily = Dungeon.dailyReplay = false;
		Dungeon.initSeed();
		ActionIndicator.clearAction();
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
	}

	private void refresh() { for (Cell c : cells) c.chosen(c.cls == GamesInProgress.selectedClass); }

	@Override
	protected void onBackPressed() { ShatteredPixelDungeon.switchScene(TitleScene.class); }

	private class Cell extends Button implements Focusable {
		final HeroClass cls;
		private final WatchText name;
		private final ColorBlock[] box = new ColorBlock[4];
		private boolean chosen;
		Cell(HeroClass cls, int x, int y, int w) {
			super();
			this.cls = cls;
			// (the portraits are 24x32)
			Image av = new Image(Assets.Sprites.AVATARS, cls.ordinal() * 24, 0, 24, 32);
			av.scale.set(2); av.x = x + (w - 48) / 2; av.y = y;
			if (!cls.isUnlocked()) av.tint(0x555555, 1f);   // locked: a grey shape
			add(av);
			name = new WatchText(WatchText.G09, Messages.titleCase(cls.title()).toUpperCase(), 0xAAAAAA);
			WatchHud.place(name, x, y + 64, w, 1); add(name);   // (clear of the yellow box's bottom edge)
			int[][] r = {{x - 1, y - 1, w + 2, 3}, {x - 1, y + 74, w + 2, 3}, {x - 1, y - 1, 3, 78}, {x + w - 2, y - 1, 3, 78}};
			for (int i = 0; i < 4; i++) { box[i] = WatchHud.block(0xFFFF00, r[i][0], r[i][1], r[i][2], r[i][3]); box[i].visible = false; add(box[i]); }
			setRect(x, y, w, 76);
		}
		void chosen(boolean on) { chosen = on; for (ColorBlock b : box) b.visible = on; name.color(on ? 0xFFFFFF : 0xAAAAAA); }
		public void focus(boolean on) { if (on && !chosen) { GamesInProgress.selectedClass = cls; refresh(); } }
		@Override protected void onClick() {
			// a finger on the chosen class: what it starts with; the Select button on it: start (Pebble PD)
			if (GamesInProgress.selectedClass != cls) { GamesInProgress.selectedClass = cls; refresh(); }
			else if (pebxel.Controls.fingerLast) WatchHeroSelect.this.add(new WndHeroInfo(cls));
			else start();
		}
		@Override protected boolean onLongClick() { WatchHeroSelect.this.add(new WndHeroInfo(cls)); return true; }
	}
}
