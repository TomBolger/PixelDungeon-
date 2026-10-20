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
public class WndWatchMenu extends WatchWindow implements Navigable {
	static final int W = 179, HEAD = 16, ROW = 64, GAP = 2, TAB_W = 61, TAB_H = 42;
	public static int lastPage = 0;
	private final Group page = new Group(), tabs = new Group();
	private final boolean dead;
	private int pageNo;
	/** The page's buttons in order (not the tabs), and which one has the focus. */
	private final java.util.List<WatchButton> pageButtons = new java.util.ArrayList<>();
	private int focusIdx;

	public void nav(int dir) {
		if (pageNo < 0) { if (!pageButtons.isEmpty()) focusIdx = (focusIdx + dir + pageButtons.size()) % pageButtons.size(); return; }
		focusIdx += dir;
		// past the page's ends: on to the next page (round the three), focus on its first or last button
		if (focusIdx >= pageButtons.size()) { show((pageNo + 1) % 3); focusIdx = 0; }
		else if (focusIdx < 0) { show((pageNo + 2) % 3); focusIdx = pageButtons.size() - 1; }
	}

	public com.watabou.noosa.ui.Component focused() {
		if (pageButtons.isEmpty()) return null;
		return pageButtons.get(Math.max(0, Math.min(pageButtons.size() - 1, focusIdx)));
	}

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
		if (parent == null && camera != null && page.parent == null) return;   // (closed already: a late tap)
		pageNo = p; if (p >= 0) lastPage = p;
		page.clear();
		tabs.clear();
		pageButtons.clear();
		String title = p < 0 ? "Game Over" : p == 0 ? "Shattered PD" : p == 1 ? "Catalogue & Journal" : "Watch";
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
		} else if (p == 2) {
			settings();
		} else {
			big(0, HEAD, (W - GAP) / 2, ROW, 12, "Catalogue", () -> { hide(); GameScene.show(WndWatchRecords.catalogue()); });
			big((W - GAP) / 2 + GAP + 1, HEAD, (W - GAP) / 2, ROW, 11, "Journal", () -> { hide(); GameScene.show(WndWatchRecords.journal()); });
			if (Dungeon.challenges > 0)
				big(0, HEAD + ROW + GAP, W, ROW, 5, "Challenges", () -> { hide(); GameScene.show(new WndChallenges(Dungeon.challenges, false)); });
			else
				big(0, HEAD + ROW + GAP, W, ROW, 7, "Badges", () -> { hide(); WndJournal.last_index = 4; GameScene.show(new WndJournal()); });
		}
		// the tabs under the window: the floppy disk (game), the quill (records), the gear (settings)
		for (int k = 0; k < 3; k++) {
			final int kk = k;
			int tw = pebxel.Screen.round ? 50 : TAB_W;
			int x = pebxel.Screen.round ? 54 + 51 * k - contentX : -4 + k * (TAB_W + 1), y = ch + 7;   // from the frame's bottom line
			tabArt(tabs, x, y - 1, tw, TAB_H, k == p);
			Image ic = PdArt.image("menu_icons", k * 16, 0, 16, 16, 2);
			ic.x = x + (tw - 32) / 2; ic.y = y + (k == p ? 3 : 5) + (pebxel.Screen.round ? 1 : 0); tabs.add(ic);
			Button b = new Button() { @Override protected void onClick() { show(kk); focusIdx = 0; } };
			b.setRect(x, y, tw, TAB_H - 4); tabs.add(b);
		}
	}

	/**
	 * The settings page (Pebble PD's: two headed rows of two tiles, a switched-off one with its icon crossed out):
	 * the watch (vibration, the map's grid lines) and the assistant (how far it looks, whether it offers items).
	 */
	private void settings() {
		final int TH = 56, HH = 14, half = (W - GAP) / 2;
		int y = HEAD;
		boolean vib = SPDSettings.vibration(), grid = SPDSettings.visualGrid() > 0;
		tile(0, y, half, TH, PdArt.image("menu_icons", 4 * 16, 0, 16, 16, 2), !vib, "Vibrate: " + (vib ? "On" : "Off"),
				() -> { SPDSettings.vibration(!SPDSettings.vibration()); if (SPDSettings.vibration()) Game.vibrate(60); show(2); }, true);
		Image gi = WatchHud.block(0x000000, 0, 0, 30, 30);
		// a picture of the map's grid: stone squares with lines between
		decorate = () -> { for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) page.add(WatchHud.block(0x555555, gi.x + 1 + k * 10, gi.y + 1 + j * 10, 8, 8)); };
		tile(half + GAP + 1, y, half, TH, gi, !grid, "Grid: " + (grid ? "On" : "Off"),
				() -> { SPDSettings.visualGrid(SPDSettings.visualGrid() > 0 ? 0 : 2); show(2); }, true);
		y += TH + GAP;
		heading("Assistant", y); y += HH;
		int a = pebxel.WatchSettings.assist();
		boolean items = pebxel.WatchSettings.itemAssist();
		Image ret = PdArt.image("icons", 67, 0, 16, 16, 2);
		// inside the reticle: the hero (near: around you) or the way down (far: anywhere on the floor)
		if (a > 0) decorate = () -> {
			Image in = a == 1 ? com.shatteredpixel.shatteredpixeldungeon.sprites.HeroSprite.avatar(Dungeon.hero) : PdArt.image("menu_icons", 6 * 16, 0, 16, 16, 1);
			in.x = ret.x + (32 - in.width()) / 2; in.y = ret.y + (32 - in.height()) / 2; page.add(in);
		};
		tile(0, y, half, TH, ret, a == 0, "Assist: " + (a == 0 ? "Off" : a == 1 ? "Near" : "Far"),
				() -> { pebxel.WatchSettings.assist((pebxel.WatchSettings.assist() + 1) % 3); show(2); }, true);
		com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite bagIc = new com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite(
				com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSpriteSheet.BACKPACK, null);
		bagIc.scale.set(2);
		tile(half + GAP + 1, y, half, TH, bagIc, !items || a == 0, "Items: " + (items ? "On" : "Off"),
				() -> { pebxel.WatchSettings.itemAssist(!pebxel.WatchSettings.itemAssist()); show(2); }, a > 0);
	}

	private void heading(String s, int y) {
		WatchText t = new WatchText(WatchText.G14B, s, ICTERINE);
		WatchHud.place(t, 0, y - 1, W, 1); page.add(t);
	}

	private Runnable decorate;

	private void tile(int x, int y, int w, int h, Image icon, boolean off, String label, Runnable r, boolean enabled) {
		WatchButton b = new WatchButton(label) { @Override protected void onClick() { if (enabled) r.run(); } };
		b.small = true;
		b.setRect(x, y, w, h);
		fit(b);
		b.icon(icon);
		b.label(label);
		if (!enabled) b.labelColor = LIGHT;
		page.add(b); pageButtons.add(b);
		if (decorate != null) { decorate.run(); decorate = null; }
		if (off) {
			// switched off: Pebble PD crosses the picture out
			Image xx = PdArt.image("menu_icons", 10 * 16, 0, 16, 16, 2);
			xx.x = icon.x + (icon.width() - 32) / 2; xx.y = icon.y + (icon.height() - 32) / 2; page.add(xx);
		}
	}

	private WatchButton big(int x, int y, int w, int h, int icon, String label, Runnable r) {
		WatchButton b = new WatchButton(label) { @Override protected void onClick() { r.run(); } };
		b.labelTop = 35 - 2;
		b.setRect(x, y, w, h);
		fit(b);
		b.icon(PdArt.image("menu_icons", icon * 16, 0, 16, 16, 2));
		b.label(label);
		page.add(b); pageButtons.add(b);
		return b;
	}
}
