package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.SPDSettings;
import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.HeroSelectScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.RankingsScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndChallenges;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndJournal;
import com.watabou.noosa.Game;
import com.watabou.noosa.Group;
import com.watabou.noosa.Image;

/**
 * Pebble PD's game menu (long Up, or the menu button): a window of big picture buttons (menu_icons.png at 2x,
 * labels in Gothic 18 bold) with its pages on tabs underneath: the game (return, save and leave), records
 * (catalogue, journal). When the hero is dead: one page, new game, rankings, main menu.
 */
public class WndWatchMenu extends WatchWindow {
	static final int W = 179, HEAD = 16, ROW = 64, GAP = 2, TAB_W = 61, TAB_H = 42;
	static int lastPage = 0;
	private final Group page = new Group(), tabs = new Group();
	private final boolean dead;
	private int pageNo;

	public WndWatchMenu() {
		super(WatchWindow.left(W), 0, W, HEAD + 2 * ROW + GAP, false);
		dead = Dungeon.hero == null || !Dungeon.hero.isAlive();
		add(page); add(tabs);
		if (dead) {
			resize(W, HEAD + 3 * ROW + 2 * GAP);
			place(WatchWindow.left(W), (WatchHud.H - ch) / 2);
			show(-1);
		} else {
			below = TAB_H;
			resize(W, HEAD + 2 * ROW + GAP);
			place(WatchWindow.left(W), (WatchHud.H - ch - TAB_H) / 2 + (pebxel.Screen.round ? 0 : 2));
			show(lastPage);
		}
	}

	private void show(int p) {
		pageNo = p; if (p >= 0) lastPage = p;
		page.clear();
		tabs.clear();
		String title = p < 0 ? "Game Over" : p == 0 ? "Shattered PD" : "Records";
		WatchText t = new WatchText(WatchText.G14B, title, ICTERINE);
		WatchHud.place(t, 0, -4, W, 1); page.add(t);
		if (p < 0) {
			big(0, HEAD, W, ROW, 6, "New Game", () -> {
				GamesInProgress.selectedClass = Dungeon.hero.heroClass;
				GamesInProgress.curSlot = GamesInProgress.firstEmpty();
				ShatteredPixelDungeon.switchScene(HeroSelectScene.class);
			});
			big(0, HEAD + ROW + GAP, W, ROW, 7, "Rankings", () -> { InterlevelScene.mode = InterlevelScene.Mode.DESCEND; Game.switchScene(RankingsScene.class); });
			big(0, HEAD + 2 * (ROW + GAP), W, ROW, 8, "Main Menu", () -> Game.switchScene(TitleScene.class));
			return;
		}
		if (p == 0) {
			big(0, HEAD, W, ROW, 9, "Return to Game", this::hide);
			big(0, HEAD + ROW + GAP, W, ROW, 0, "Save & Main Menu", () -> {
				try { Dungeon.saveAll(); } catch (Exception e) { ShatteredPixelDungeon.reportException(e); }
				Game.switchScene(TitleScene.class);
			}).active = !SPDSettings.intro();
		} else {
			big(0, HEAD, (W - GAP) / 2, ROW, 12, "Catalogue", () -> { hide(); WndJournal.last_index = 3; GameScene.show(new WndJournal()); });
			big((W - GAP) / 2 + GAP + 1, HEAD, (W - GAP) / 2, ROW, 11, "Journal", () -> { hide(); WndJournal.last_index = 0; GameScene.show(new WndJournal()); });
			if (Dungeon.challenges > 0)
				big(0, HEAD + ROW + GAP, W, ROW, 5, "Challenges", () -> { hide(); GameScene.show(new WndChallenges(Dungeon.challenges, false)); });
			else
				big(0, HEAD + ROW + GAP, W, ROW, 7, "Badges", () -> { hide(); WndJournal.last_index = 4; GameScene.show(new WndJournal()); });
		}
		// the tabs under the window: the floppy disk (game), the quill (records)
		for (int k = 0; k < 2; k++) {
			final int kk = k;
			int tw = pebxel.Screen.round ? 50 : TAB_W;
			int x = pebxel.Screen.round ? 54 + 51 * k - contentX : -4 + k * (TAB_W + 1), y = ch + 7;   // from the frame's bottom line
			tabArt(tabs, x, y - 1, tw, TAB_H, k == p);
			Image ic = PdArt.image("menu_icons", k * 16, 0, 16, 16, 2);
			ic.x = x + (tw - 32) / 2; ic.y = y + (k == p ? 3 : 5) + (pebxel.Screen.round ? 1 : 0); tabs.add(ic);
			Button b = new Button() { @Override protected void onClick() { show(kk); } };
			b.setRect(x, y, tw, TAB_H - 4); tabs.add(b);
		}
	}

	private WatchButton big(int x, int y, int w, int h, int icon, String label, Runnable r) {
		WatchButton b = new WatchButton(label) { @Override protected void onClick() { r.run(); } };
		b.labelTop = 35 - 2;
		b.setRect(x, y, w, h);
		fit(b);
		b.icon(PdArt.image("menu_icons", icon * 16, 0, 16, 16, 2));
		b.label(label);
		page.add(b);
		return b;
	}
}
