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
		super(WatchWindow.left(W), 0, W, HEAD + 2 * ROW + GAP, true);   // (Pebble PD's tab panel)
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
		wipe(page);
		pageButtons.clear();
		String title = p < 0 ? "Game Over" : p == 0 ? "Shattered PD" : p == 1 ? "Catalogue & Journal" : "Audio";
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
		tabs(p);
	}

	/** The tabs' pictures, made once: changing page only moves them (a few bytes for the watch, rather than all
	 *  of them deleted and sent again, and no churn in its memory). */
	private com.watabou.noosa.ColorBlock[][] tabArt;
	private Image[] tabIcons;

	/** The tabs under the window: the floppy disk (game), the quill (records), the gear (settings); p's is open. */
	private void tabs(int p) {
		int tw = pebxel.Screen.round ? 50 : TAB_W;
		if (tabArt == null) {
			tabArt = new com.watabou.noosa.ColorBlock[3][TAB_RECTS];
			tabIcons = new Image[3];
			for (int k = 0; k < 3; k++) {
				final int kk = k;
				int[][] rs = tabRects(0, 0, tw, TAB_H, false);
				for (int i = 0; i < TAB_RECTS; i++) { tabArt[k][i] = WatchHud.block(rs[i][4], 0, 0, 1, 1); tabs.add(tabArt[k][i]); }
				tabIcons[k] = PdArt.image("menu_icons", k * 16, 0, 16, 16, 2); tabs.add(tabIcons[k]);
				Button b = new Button() { @Override protected void onClick() { show(kk); focusIdx = 0; } };
				b.setRect(tabX(k), ch + 1, tw, TAB_H - 4); tabs.add(b);
			}
		}
		for (int k = 0; k < 3; k++) {
			int x = tabX(k), y = ch + 1;   // over the frame's bottom edge: the chosen one joins the window (Pebble PD)
			int[][] rs = tabRects(x, y - 1, tw, TAB_H, k == p);
			for (int i = 0; i < TAB_RECTS; i++) {
				com.watabou.noosa.ColorBlock c = tabArt[k][i];
				c.x = rs[i][0]; c.y = rs[i][1]; c.size(Math.max(0, rs[i][2]), Math.max(0, rs[i][3]));   // (an empty piece: drawn as nothing)
			}
			Image ic = tabIcons[k];
			ic.x = x + (tw - 32) / 2; ic.y = y + (k == p ? 3 : 5) + (pebxel.Screen.round ? 1 : 0);
		}
	}

	private int tabX(int k) { return pebxel.Screen.round ? 54 + 51 * k - contentX : -4 + k * (TAB_W + 1); }

	/**
	 * The settings page (Pebble PD's: two headed rows of two tiles, a switched-off one with its icon crossed out):
	 * audio (music on or off; sound loud, medium, quiet or off) and the assistant (how far it looks, whether it offers items).
	 */
	private void settings() {
		final int TH = 56, HH = 14, half = (W - GAP) / 2;
		int y = HEAD;
		boolean music = pebxel.WatchSettings.music();
		int snd = pebxel.WatchSettings.sound();
		tile(0, y, half, TH, PdArt.image("menu_icons", 3 * 16, 0, 16, 16, 2), !music, "Music: " + (music ? "On" : "Off"),
				() -> { pebxel.WatchSettings.music(!pebxel.WatchSettings.music()); show(2); }, true);
		tile(half + GAP + 1, y, half, TH, PdArt.image("menu_icons", 4 * 16, 0, 16, 16, 2), snd == 3,
				"Sound: " + new String[]{"Loud", "Medium", "Quiet", "Off"}[snd],
				() -> { pebxel.WatchSettings.sound((pebxel.WatchSettings.sound() + 1) % 4); show(2); }, true);   // Loud, Medium, Quiet, Off
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
		tile(half + GAP + 1, y, half, TH, bagIc, !items || a == 0, "Item assist: " + (items ? "On" : "Off"),
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

	/** Pebble PD's "Return to Game" picture: a big white arrow pointing right, outlined in black (34 x 26; with the
	 *  outline 36 x 27, from (-1, 0)). One picture made here (the watch is sent its pixels once, about 150 bytes),
	 *  rather than the 54 one-pixel-high blocks it was drawn with (2 KB each time the page was shown, and 5 KB of the
	 *  watch's memory while it showed). */
	static com.watabou.gltextures.SmartTexture arrowTex() {
		String k = "pebxel/arrow";
		if (!com.watabou.gltextures.TextureCache.contains(k)) {
			com.watabou.gltextures.SmartTexture t = com.watabou.gltextures.TextureCache.create(k, 36, 27);
			com.badlogic.gdx.graphics.Pixmap px = t.bitmap;
			px.setBlending(com.badlogic.gdx.graphics.Pixmap.Blending.None);
			px.setColor(0); px.fill();
			for (int pass = 0; pass < 2; pass++) {
				int o = pass == 0 ? 1 : 0;
				px.setColor(pass == 0 ? 0x000000FF : 0xFFFFFFFF);   // (RGBA)
				px.fillRectangle(1 - o, 8 - o, 19 + 2 * o, 10 + 2 * o);   // the shaft
				for (int dy = -13; dy <= 13; dy++) {                       // the head, row by row
					int len = 16 - Math.abs(dy) * 16 / 13;
					if (len + o * 2 > 0) px.fillRectangle(1 + 18 - o, 13 + dy, len + 2 * o, 1);
				}
			}
		}
		return com.watabou.gltextures.TextureCache.get(k);
	}

	private void arrow(float x, float y) {
		Image a = new Image(arrowTex());
		a.x = (float) Math.floor(x - 1 + 0.5f); a.y = (float) Math.floor(y + 0.5f); page.add(a);   // (on whole pixels, as the blocks were drawn)
	}

	private WatchButton big(int x, int y, int w, int h, int icon, String label, Runnable r) {
		WatchButton b = new WatchButton(label) { @Override protected void onClick() { r.run(); } };
		b.labelTop = 35 - 2;
		b.setRect(x, y, w, h);
		fit(b);
		if (icon == 9) {
			// (drawn: Pebble PD's arrow, not the small icon)
			b.label(label); page.add(b); pageButtons.add(b);
			arrow(b.left() + (b.width() - 34) / 2, b.top() + 4);
			return b;
		}
		b.icon(PdArt.image("menu_icons", icon * 16, 0, 16, 16, 2));
		b.label(label);
		page.add(b); pageButtons.add(b);
		return b;
	}
}
