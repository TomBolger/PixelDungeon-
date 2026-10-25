package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Assets;
import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.keys.IronKey;
import com.shatteredpixel.shatteredpixeldungeon.items.wands.Wand;
import com.shatteredpixel.shatteredpixeldungeon.journal.Notes;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.sprites.CharSprite;
import com.shatteredpixel.shatteredpixeldungeon.sprites.HeroSprite;
import com.shatteredpixel.shatteredpixeldungeon.sprites.ItemSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.BuffIcon;
import com.shatteredpixel.shatteredpixeldungeon.ui.BuffIndicator;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.ui.GameLog;
import com.watabou.noosa.Camera;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Group;
import com.watabou.noosa.Image;
import com.watabou.noosa.NinePatch;
import com.watabou.noosa.Visual;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;
import pebxel.WatchFonts;

/**
 * Pebble PD's in-game screen furniture (Emery, 200x228), drawn with Pebble PD's own art, for Shattered:
 * the status pane (portrait, level, health, experience, depth, keys, buffs), the red menu button, the six
 * toolbar buttons in Shattered's order (wait, search | quickslots 1-3, backpack), the buttons' tags at the right
 * edge, the loot and resume tags at the left, and the last two log messages, outlined, over the map.
 * Numbers and places follow watch/src/c/main.c (draw_hud_art, draw_hud_text, ctag, tag_rect, tb_x).
 * Shattered's own status pane, menu, toolbar, log and tags still run (unseen: Link skips them); the buttons here
 * press theirs, so everything behaves as Shattered does.
 */
public class WatchHud extends Group {
	public static int W = 200, H = 228;   // the watch's screen (Screen)
	static int SC(int v) { return v * 3 / 2; }
	static final int SP_H = 35, TB_H = 38, TB_W = 32, TB_GAP = 8, TB_N = 6;
	static int TB_Y = H - TB_H;
	public static final int TB_WAIT = 0, TB_SEARCH = 1, TB_Q1 = 2, TB_Q2 = 3, TB_Q3 = 4, TB_BAG = 5, B_MENU = 6, B_PORTRAIT = 7;
	/** The quickslots' buttons, slot by slot. */
	public static final int[] TB_Q = {TB_Q1, TB_Q2, TB_Q3};
	static final int NQ = 3;
	/** The toolbar's pictures: wait (clock), search (magnifier), backpack, from PD's toolbar.png; their buttons. */
	static final int[][] TB_ICON = {{5, 8, 10, 10}, {24, 7, 12, 12}, {65, 5, 14, 15}};
	static final int[] TB_ICON_AT = {TB_WAIT, TB_SEARCH, TB_BAG};
	static final int YELLOW = 0xFFFF55, LIGHT = 0xAAAAAA, WHITE = 0xFFFFFF;

	public final Camera cam;
	/** What the Back button has highlighted (-1 none): toolbar 0-5, menu 6, portrait 7. */
	public int highlight = -1;
	private final Hooks hooks;

	/** What the buttons do (Controls supplies these, so the watch buttons and taps agree). */
	public interface Hooks {
		void press(int button, boolean held);
		/** The cell Select acts on, and what it is: 0 nothing, 1 enemy, 2 item, 3 go there, 4 an item aimed at the cell
		 *  (a zap, a throw, a shot), 5 an item's use (eat, drink, read...), 6 wait, 7 search, 8 rest, 9 the talents. */
		int targetCell();
		int targetKind();
		/** The item Select uses (kinds 4 and 5), or null. */
		Item targetItem();
		/** The button whose action Select is (its reticle shows it, as Pebble PD's hands): toolbar 0-5, portrait 7; -1 none. */
		int toolbarHint();
		int targetCount();
		int mapZoomLevel();
		void tag(int which, boolean held);   // 0 zoom, 1 act, 2 next target, 3 loot, 4 resume
	}

	private Image avatar; private int avatarClass = -1, avatarTier = -1;
	private Image hpBar;
	private ColorBlock xpBar;
	private WatchText level, hp, depth, keys;
	private final List<Image> buffs = new ArrayList<>(); private String buffKey = "";
	private final ItemSprite[] quick = new ItemSprite[NQ];
	private final ColorBlock[] quickFog = new ColorBlock[NQ];
	private final int[][] quickAt = new int[NQ][];
	private final WatchText[] quickText = new WatchText[NQ];
	private final Group tags = new Group();
	private final Group log = new Group(); private String logKey = "";
	private final Group reticle = new Group();
	private final HudButton[] buttons = new HudButton[8];
	private final HudButton[] tagButtons = new HudButton[5];

	public WatchHud(Hooks hooks) {
		this.hooks = hooks;
		W = pebxel.Screen.w; H = pebxel.Screen.h; TB_Y = H - TB_H;
		cam = new Camera(0, 0, W, H, 2);   // two phone pixels per watch pixel: one unit here is one watch pixel
		Camera.add(cam);
		camera = cam;
		round = pebxel.Screen.round;
		if (round) { buildRound(); return; }

		// the status pane: PD's art at 1.5x, its left 80 px, a stretched column, its right 48 px
		add(PdArt.image("status_pane", 0, 0, 80, SP_H, 1.5f));
		Image mid = PdArt.image("status_pane", 80, 0, 1, SP_H, 1.5f); mid.x = SC(80); mid.scale.x = W - SC(128); add(mid);
		Image right = PdArt.image("status_pane", 80, 0, 48, SP_H, 1.5f); right.x = W - SC(48); add(right);
		avatarAnchor = right;
		// the menu button: a red button with three white bars
		NinePatch menu = new NinePatch(PdArt.tex("chrome"), 58, 0, 6, 6, 2);
		menu.x = W - 27; menu.y = 2; menu.size(27, 30); add(menu);
		for (int i = 0; i < 3; i++) add(block(WHITE, W - 27 + 7, 2 + 8 + i * 6, 27 - 14, 3));
		hpBar = PdArt.image("hp_bar", 0, 0, 64, 4, 1.5f); hpBar.x = SC(30); hpBar.y = SC(3); add(hpBar);
		xpBar = block(YELLOW, SC(30), SC(8), 0, 2); add(xpBar);
		add(level = new WatchText(WatchText.G14B, "", WHITE)); level.outline = true;
		add(hp = new WatchText(WatchText.G14B, "", WHITE)); hp.outline = true;
		add(depth = new WatchText(WatchText.G14B, "", LIGHT)); depth.outline = true;
		add(keys = new WatchText(WatchText.G14B, "", LIGHT)); keys.outline = true;

		// the toolbar: PD's toolbar frame stretched to 32x38, icons at 2x
		for (int i = 0; i < TB_N; i++) {
			NinePatch b = new NinePatch(PdArt.tex("toolbar"), 83, 0, 23, 25, 4);
			b.x = tbX(i); b.y = TB_Y; b.size(TB_W, TB_H); add(b);
		}
		for (int j = 0; j < TB_ICON.length; j++) {
			int i = TB_ICON_AT[j], w = TB_ICON[j][2], h = TB_ICON[j][3];
			Image ic = PdArt.image("toolbar", TB_ICON[j][0], TB_ICON[j][1], w, h, iconScale(j));
			ic.x = tbX(i) + Math.round((TB_W - iconScale(j) * w) / 2); ic.y = TB_Y + Math.round((TB_H - 2 - iconScale(j) * h) / 2); add(ic);
		}
		for (int q = 0; q < NQ; q++) {
			int bx = tbX(TB_Q[q]);
			quick[q] = new ItemSprite(); quick[q].scale.set(ICON_SCALE); quick[q].visible = quick[q].active = false; add(quick[q]);
			quickAt[q] = new int[]{bx + TB_W / 2, TB_Y + (TB_H - 2) / 2};
			int fs = Math.round(16 * ICON_SCALE);
			quickFog[q] = block(0x000000, bx + (TB_W - fs) / 2, quickAt[q][1] - fs / 2, fs, fs); quickFog[q].alpha(0.5f); quickFog[q].visible = false; add(quickFog[q]);
			add(quickText[q] = new WatchText(WatchText.G14B, "", WHITE)); quickText[q].outline = true;
		}
		add(tags);
		add(log);
		add(reticle);
		add(edge);

		// what fingers and the Back button press
		for (int i = 0; i < TB_N; i++) addButton(i, tbX(i), TB_Y, TB_W, TB_H);
		addButton(B_MENU, W - 27, 2, 27, 30);
		addButton(B_PORTRAIT, 0, 0, SC(30), SC(30));
		for (int k = 0; k < 5; k++) {
			final int which = k;
			HudButton b = new HudButton() {
				@Override protected void onClick() { WatchHud.this.hooks.tag(which, false); }
				@Override protected boolean onLongClick() { WatchHud.this.hooks.tag(which, true); return true; }
			};
			b.visible = b.active = false;
			tagButtons[k] = b; add(b);
		}
		refresh();
	}

	// ---- Gabbro: everything round the rim in bubbles (Pebble PD 0.83, main.c's PBL_ROUND branches) ----
	static boolean round;
	static final int[] TBX = {19, 29, 52, 92, 130, 168}, TBY = {142, 179, 211, 235, 242, 235};
	static final int TB_R = 17;
	private final Group bars = new Group();
	private String barsKey = "";

	/** Tangent x256 (Pebble PD's arc bearings) to tenths of a degree. */
	static int deg(int t) { return (int) Math.round(Math.toDegrees(Math.atan(t / 256.0)) * 10); }

	/** Pebble PD's bubble: a round frame (black edge, a ring, the fill) for the box (x, y, d, d), grown outward
	 *  past the rim; returns where its picture goes (the box moved most of the way it grew). */
	/** Where bubble() puts a bubble's box (pushed out towards the rim). */
	static int[] bubbleRect(int x, int y, int d) {
		int r = d / 2, cx = x + r, cy = y + r, dx = cx - 130, dy = cy - 130, dist = (int) Math.sqrt(dx * dx + dy * dy), o = r / 3;
		if (dist > 40) { o -= 2; x += dx * o / dist; y += dy * o / dist; }
		return new int[]{x, y, d, d};
	}

	static int[] bubble(Group g, int x, int y, int d, int fill, int ring) {
		int r = d / 2, cx = x + r, cy = y + r, dx = cx - 130, dy = cy - 130, dist = (int) Math.sqrt(dx * dx + dy * dy), o = r / 3;
		if (dist > 40) { cx += dx * o / dist; cy += dy * o / dist; o -= 2; x += dx * o / dist; y += dy * o / dist; r += r / 3; }
		g.add(Shape.disc(cx, cy, r + 1, 0x000000)); g.add(Shape.disc(cx, cy, r, ring)); g.add(Shape.disc(cx, cy, r - 2, fill));
		return new int[]{x, y, d, d};
	}

	private void buildRound() {
		// the strip over the top and the shelf the bars lie on
		add(Shape.arc(130, 130, 125, 131, deg(-365), deg(305), 0, 0x555555));
		add(Shape.arc(130, 130, 115, 125, deg(-365), 50, 5, 0xAAAAAA));
		add(Shape.arc(130, 130, 115, 125, deg(-365), 40, 5, 0x555555));
		add(Shape.arc(130, 130, 115, 117, deg(-365), deg(17), 0, 0x555555));
		add(Shape.arc(130, 130, 117, 118, deg(-365), deg(24), 0, 0xAAAAAA));
		add(bars);
		Image stairs = PdArt.image("status_pane", 87, 5, 8, 8, 1.5f); stairs.x = 158; stairs.y = 24; add(stairs);
		Image key = PdArt.image("status_pane", 103, 5, 7, 8, 1.5f); key.scale.x = 11 / 7f; key.x = 177; key.y = 32; add(key);
		// the portrait, the level, the menu button
		int[] pb = bubble(this, 26, 23, 52, 0x555555, 0xAAAAAA);
		portraitBox = pb; avatarAnchor = members.get(members.size() - 1);
		bubble(this, 63, 61, 18, 0x555555, 0xAAAAAA);
		int[] mb = bubble(this, 188, 29, 40, 0xAA0000, 0xFF5500);
		for (int i = 0; i < 3; i++) add(block(WHITE, mb[0] + 10, mb[1] + 12 + i * 6, 20, 3));
		add(level = new WatchText(WatchText.G14B, "", WHITE)); level.outline = true;
		add(hp = new WatchText(WatchText.G14B, "", WHITE)); hp.outline = true;
		add(depth = new WatchText(WatchText.G14B, "", LIGHT)); depth.outline = true;
		add(keys = new WatchText(WatchText.G14B, "", LIGHT)); keys.outline = true;
		// the toolbar round the bottom arc
		for (int i = 0; i < TB_N; i++) tbBox[i] = bubble(this, TBX[i] - TB_R, TBY[i] - TB_R, 2 * TB_R, 0x555555, 0xAAAAAA);
		for (int j = 0; j < TB_ICON.length; j++) {
			int[] c = tbBox[TB_ICON_AT[j]]; int w = TB_ICON[j][2], h = TB_ICON[j][3];
			Image ic = PdArt.image("toolbar", TB_ICON[j][0], TB_ICON[j][1], w, h, iconScale(j));
			ic.x = c[0] + Math.round((c[2] - iconScale(j) * w) / 2); ic.y = c[1] + Math.round((c[3] - iconScale(j) * h) / 2); add(ic);
		}
		for (int q = 0; q < NQ; q++) {
			int[] c = tbBox[TB_Q[q]];
			int fs = Math.round(16 * ICON_SCALE);
			quick[q] = new ItemSprite(); quick[q].scale.set(ICON_SCALE); quick[q].visible = quick[q].active = false; add(quick[q]);
			quickAt[q] = new int[]{c[0] + c[2] / 2, c[1] + c[3] / 2};
			quickFog[q] = block(0x000000, quickAt[q][0] - fs / 2, quickAt[q][1] - fs / 2, fs, fs); quickFog[q].alpha(0.5f); quickFog[q].visible = false; add(quickFog[q]);
			add(quickText[q] = new WatchText(WatchText.G14B, "", WHITE)); quickText[q].outline = true;
		}
		add(tags);
		add(log);
		add(reticle);
		add(edge);
		for (int i = 0; i < TB_N; i++) addButton(i, TBX[i] - TB_R, TBY[i] - TB_R, 2 * TB_R, 2 * TB_R);
		addButton(B_MENU, 188, 29, 40, 40);
		addButton(B_PORTRAIT, 26, 23, 52, 52);
		for (int k = 0; k < 5; k++) {
			final int which = k;
			HudButton b = new HudButton() {
				@Override protected void onClick() { WatchHud.this.hooks.tag(which, false); }
				@Override protected boolean onLongClick() { WatchHud.this.hooks.tag(which, true); return true; }
			};
			b.visible = b.active = false;
			tagButtons[k] = b; add(b);
		}
		refresh();
	}
	private int[] portraitBox;
	private com.watabou.noosa.Gizmo avatarAnchor;
	private final int[][] tbBox = new int[TB_N][];

	/** Gabbro: health along the shelf (hp_bar's three shades, its end cut at a slant), experience under it. */
	private void refreshBars(Hero h) {
		int hpNow = Math.max(0, h.HP), max = Math.max(1, h.HT), ex = h.exp, mx = Math.max(1, h.maxExp());
		String k = hpNow + "/" + max + "/" + ex + "/" + mx;
		if (k.equals(barsKey)) return;
		barsKey = k;
		bars.clear();
		if (hpNow > 0) {
			int f = -290 + (55 + 290) * hpNow / max, b0 = deg(-166);
			bars.add(Shape.arc(130, 130, 119, 120, b0, f, 5, 0x550000));
			bars.add(Shape.arc(130, 130, 120, 124, b0, f + 5, 5, 0xAA0000));
			bars.add(Shape.arc(130, 130, 124, 125, b0, f + 25, 5, 0xAA5555));
		}
		if (ex > 0) bars.add(Shape.arc(130, 130, 113, 115, deg(-166), -290 + (60 + 290) * ex / mx, 0, YELLOW));
	}

	/** The items' and the backpack's size on the toolbar: 2x, as Pebble PD (at 1.5x every texel came out 1 or 2 px
	 *  wide and the art looked lumpy). */
	static float ICON_SCALE = 2f;
	static float iconScale(int j) { return TB_ICON_AT[j] == TB_BAG ? ICON_SCALE : 2f; }
	static int tbX(int i) { return (W - TB_N * TB_W - TB_GAP) / 2 + i * TB_W + (i >= 2 ? TB_GAP : 0); }   // wait, search | the rest
	public static int[] buttonRect(int k) {
		if (round) {
			if (k < TB_N) return new int[]{TBX[k] - TB_R, TBY[k] - TB_R, 2 * TB_R, 2 * TB_R};
			if (k == B_MENU) return new int[]{188, 29, 40, 40};
			return new int[]{26, 23, 52, 52};
		}
		if (k < TB_N) return new int[]{tbX(k), TB_Y, TB_W, TB_H};
		if (k == B_MENU) return new int[]{W - 27, 2, 27, 30};
		return new int[]{0, 0, SC(30), SC(30)};
	}

	private void addButton(int k, int x, int y, int w, int h) {
		HudButton b = new HudButton() {
			@Override protected void onClick() { hooks.press(k, false); }
			@Override protected boolean onLongClick() { hooks.press(k, true); return true; }
		};
		b.setRect(x, y, w, h);
		buttons[k] = b; add(b);
	}

	/** A pressable area with no picture of its own (the pictures are drawn above). */
	public static class HudButton extends Button {
		@Override protected void layout() { super.layout(); }
	}

	static ColorBlock block(int rgb, float x, float y, float w, float h) {
		ColorBlock b = new ColorBlock(1, 1, 0xFF000000 | rgb);
		b.x = x; b.y = y; b.size(w, h);
		return b;
	}

	/** Place a text by the top of its box (as Pebble's graphics_draw_text), left, centred or right in it. */
	static void place(WatchText t, int x, int y, int w, int align) {
		t.y = y + WatchFonts.capTop(t.font);
		t.x = align == 0 ? x : align == 1 ? x + (w - t.width) / 2 : x + w - t.width;
	}

	@Override
	public void update() {
		refresh();
		placeBossBar();
		super.update();
	}

	/** Shattered's boss health bar: under the status pane, centred (Shattered put it where the buffs are). */
	private com.shatteredpixel.shatteredpixeldungeon.ui.BossHealthBar bossBar;
	private void placeBossBar() {
		try {
			if (bossBar == null) {
				if (!(Game.scene() instanceof com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene)) return;
				java.lang.reflect.Field f = com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.class.getDeclaredField("boss");
				f.setAccessible(true);
				bossBar = (com.shatteredpixel.shatteredpixeldungeon.ui.BossHealthBar) f.get(Game.scene());
				if (bossBar == null) return;
			}
			Camera c = bossBar.camera(); if (c == null) return;
			float k = c.zoom / 2f;   // watch pixels per unit of its camera
			float wx = Math.round((W - bossBar.width() * k) / 2), wy = round ? 47 : SC(SP_H) + 3;
			float x = Math.round((wx * 2 - c.x) / c.zoom + c.scroll.x), y = Math.round((wy * 2 - c.y) / c.zoom + c.scroll.y);
			if (bossBar.left() != x || bossBar.top() != y) bossBar.setPos(x, y);
		} catch (Exception e) { bossBar = null; }
	}

	private void refresh() {
		Hero h = Dungeon.hero;
		if (h == null) return;
		// the portrait: the hero at 2x, as dressed
		int tier = h.tier(), cls = h.heroClass.ordinal();
		if (avatar == null || tier != avatarTier || cls != avatarClass) {
			if (avatar != null) avatar.killAndErase();
			avatar = HeroSprite.avatar(h); avatar.scale.set(2);
			if (round) { avatar.x = portraitBox[0] + (portraitBox[2] - 2 * avatar.width) / 2; avatar.y = portraitBox[1] + (portraitBox[3] - 2 * avatar.height) / 2; }
			else { avatar.x = SC(15) - avatar.width; avatar.y = SC(16) - avatar.height; }
			com.watabou.noosa.GroupAccess.addAfter(this, avatar, avatarAnchor);
			avatarTier = tier; avatarClass = cls;
		}
		int hpNow = Math.max(0, h.HP);
		if (round) {
			refreshBars(h);
			level.text(Integer.toString(h.lvl)); place(level, 62, 60, 20, 1);
			hp.text(hpNow + "/" + h.HT); place(hp, 86, 24, 50, 0);
			depth.text(Integer.toString(Dungeon.depth)); place(depth, 133, 18, 24, 2);
			keys.text(Integer.toString(Notes.keyCount(new IronKey(Dungeon.depth)))); place(keys, 152, 27, 24, 2);
		} else {
		hpBar.scale.x = SC(64) * hpNow / (float) Math.max(1, h.HT) / 64f;
		hpBar.visible = hpNow > 0;
		xpBar.size(SC(64) * h.exp / (float) Math.max(1, h.maxExp()), 2);
		level.text(Integer.toString(h.lvl)); place(level, SC(22) - 2, SC(21), SC(12) + 4, 1);
		hp.text(hpNow + "/" + h.HT); place(hp, SC(31), SC(7), 60, 0);
		depth.text(Integer.toString(Dungeon.depth)); place(depth, W - SC(42) - 24, -1, 24, 2);
		keys.text(Integer.toString(Notes.keyCount(new IronKey(Dungeon.depth)))); place(keys, W - SC(26) - 24, -1, 24, 2);
		}

		// buffs, 7x7 at 2x, as many as fit beside the level box
		StringBuilder key = new StringBuilder();
		List<Buff> bs = new ArrayList<>();
		for (Buff b : h.buffs()) if (b.icon() != BuffIndicator.NONE) { bs.add(b); key.append(b.icon()).append(','); }
		int fit = round ? 6 : (W - SC(38) - SC(48) + 2) / 16;
		final int[] BX = {29, 24, 20, 45, 39, 36}, BY = {83, 98, 113, 88, 102, 117};
		if (!key.toString().equals(buffKey)) {
			buffKey = key.toString();
			for (Image i : buffs) i.killAndErase();
			buffs.clear();
			for (int i = 0; i < bs.size() && i < fit; i++) {
				BuffIcon ic = new BuffIcon(bs.get(i), false);
				ic.scale.set(2);
				if (round) { ic.x = BX[i] - 7; ic.y = BY[i] - 7; } else { ic.x = SC(38) + i * 16; ic.y = SC(22); }
				buffs.add(ic); com.watabou.noosa.GroupAccess.addAfter(this, ic, keys);
			}
		}

		// quickslots: the item at 2x; greyed when not in the backpack; its count or charges
		for (int q = 0; q < NQ; q++) {
			Item it = Dungeon.quickslot.getItem(q);
			int bx = round ? tbBox[TB_Q[q]][0] : tbX(TB_Q[q]);
			int by = round ? tbBox[TB_Q[q]][1] : TB_Y, bh = round ? 2 * TB_R : TB_H, bw = round ? 2 * TB_R : TB_W;
			quick[q].visible = quick[q].active = it != null;   // (an ItemSprite turns itself visible each update: stop it)
			quickText[q].text("");
			if (it == null) { quickFog[q].visible = false; continue; }
			quick[q].view(it); quick[q].scale.set(ICON_SCALE);
			// centred by its own size (not all item pictures are 16x16)
			quick[q].x = quickAt[q][0] - Math.round(quick[q].width() / 2); quick[q].y = quickAt[q][1] - Math.round(quick[q].height() / 2);
			boolean have = Dungeon.quickslot.isNonePlaceholder(q);
			quickFog[q].visible = !have;
			if (!have) continue;
			if (it instanceof Wand && it.levelKnown) {
				quickText[q].text(it.status()); place(quickText[q], bx + 3, by - 1, bw - 4, 0);
			} else if (it.quantity() > 1) {
				quickText[q].text(Integer.toString(it.quantity())); place(quickText[q], bx, by + bh - 18, bw - 4, 2);
			}
		}

		refreshTags(h);
		refreshLog();
		refreshReticle();
	}

	// ---- the tags ----

	private static int[] ctag(int k) {
		if (round) { int[] CX = {238, 246, 234}, CY = {87, 134, 181}; return new int[]{CX[k] - 13, CY[k] - 13, 26, 26}; }
		int bot = TB_Y - 2, top = H - bot;
		return new int[]{W - 22, k == 0 ? top : k == 1 ? H / 2 - 12 : bot - 24, 22, 24};
	}

	private static int[] tagRect(int which, boolean lootShown) {
		if (round) return which == 1 || !lootShown ? new int[]{206 - 14, 212 - 14, 28, 28} : new int[]{180 - 14, 184 - 14, 28, 28};
		int y = SC(33) + 2;
		if (which > 1 && lootShown) y += 38;
		return new int[]{-4, y, 40, which == 1 ? 30 : 36};
	}

	// each tag is its own group, rebuilt only when what it shows changes (not as the target moves about: every
	// rebuilt picture is radio time)
	private final Group[] tagGroups = new Group[4];
	private final String[] tagKeys = {"", "", "", ""};
	private Group into;

	private boolean tagChanged(int slot, String key) {
		if (tagGroups[slot] == null) { tagGroups[slot] = new Group(); tags.add(tagGroups[slot]); }
		if (key.equals(tagKeys[slot])) return false;
		tagKeys[slot] = key; tagGroups[slot].clear(); into = tagGroups[slot];
		return true;
	}

	private void refreshTags(Hero h) {
		boolean alive = h.isAlive();
		int kind = hooks.targetKind(), cell = hooks.targetCell(), n = hooks.targetCount(), z = hooks.mapZoomLevel();
		Heap under = Dungeon.level.heaps.get(h.pos);
		boolean loot = alive && under != null && !(cell == h.pos && kind == 2);
		Item weapon = h.belongings.weapon();
		Heap there = cell >= 0 && kind == 2 ? Dungeon.level.heaps.get(cell) : null;
		// zoom (Up): the glass and a pip per level
		int[] a = ctag(0);
		if (tagChanged(0, alive + "/" + z)) {
			if (alive) {
				tag(a, "tag_red");
				Image glass = PdArt.image("tag_red", 16, 1, 12, 12, 1); glass.x = a[0] + 3; glass.y = a[1] + 6; into.add(glass);
				for (int i = 0; i <= z; i++) into.add(block(YELLOW, a[0] + 17, a[1] + 17 - i * 5, 3, 3));
			}
		}
		if (alive) tagButton(0, ctag(0)); else tagButtons[0].visible = tagButtons[0].active = false;
		// the next target (Down)
		boolean next = alive && n > 1;
		if (tagChanged(2, "" + next) && next) {
			a = ctag(2); tag(a, "tag_red");
			Image t = PdArt.image("icons", 67, 0, 16, 16, 1); t.x = a[0] + 2 + (a[2] - 16) / 2; t.y = a[1] + (a[3] - 16) / 2; into.add(t);
		}
		if (next) tagButton(2, ctag(2)); else tagButtons[2].visible = tagButtons[2].active = false;
		// what Select does: attack (red, the weapon), pick up (blue, the thing), go there (pale, the walk arrow); an item
		// aimed at an enemy (red, the item), an item's use (pale, the item), wait or rest (pale, the clock), search (pale,
		// the magnifier), the talents (pale, the talent icon)
		boolean act = alive && kind > 0 && (cell >= 0 || kind >= 4);
		Item use = kind == 4 || kind == 5 ? hooks.targetItem() : null;
		if (kind == 4 || kind == 5) act &= use != null;
		String actKey = !act ? "-" : kind == 4 || kind == 5 ? kind + "/" + use.image() : kind == 1 ? "1/" + (weapon == null ? 0 : weapon.image()) : kind == 2 && there != null ? "2/" + there.peek().image() : "" + kind;
		if (tagChanged(1, actKey) && act) {
			a = ctag(1);
			if (kind == 1 || kind == 4) {
				tag(a, "tag_red");
				Item shown = kind == 4 ? use : weapon;
				if (shown != null) { ItemSprite s = new ItemSprite(shown); s.x = a[0] + 2 + Math.round((a[2] - s.width()) / 2); s.y = a[1] + Math.round((a[3] - s.height()) / 2); into.add(s); }
			} else if (kind == 2 && there != null) {
				tag(a, "tag_blue");
				ItemSprite s = new ItemSprite(there); s.x = a[0] + 2 + Math.round((a[2] - s.width()) / 2); s.y = a[1] + Math.round((a[3] - s.height()) / 2); into.add(s);
			} else if (kind == 5) {
				tag(a, "tag_white");
				ItemSprite s = new ItemSprite(use); s.x = a[0] + 2 + Math.round((a[2] - s.width()) / 2); s.y = a[1] + Math.round((a[3] - s.height()) / 2); into.add(s);
			} else if (kind >= 6 && kind <= 8) {   // (the toolbar's own pictures: the clock for wait and rest, the magnifier)
				tag(a, "tag_white");
				int j = kind == 7 ? 1 : 0, w = TB_ICON[j][2], hh = TB_ICON[j][3];
				Image ic = PdArt.image("toolbar", TB_ICON[j][0], TB_ICON[j][1], w, hh, 1);   // (1x, as the other tag pictures)
				ic.x = a[0] + 2 + (a[2] - w) / 2; ic.y = a[1] + (a[3] - hh) / 2; into.add(ic);
				if (kind == 8) { WatchText zz = new WatchText(WatchText.G14B, "z", YELLOW); zz.outline = true; place(zz, a[0] + a[2] - 8, a[1] - 2, 8, 0); into.add(zz); }
			} else if (kind == 9) {
				tag(a, "tag_white");
				Image ic = com.shatteredpixel.shatteredpixeldungeon.ui.Icons.get(com.shatteredpixel.shatteredpixeldungeon.ui.Icons.TALENT);
				ic.x = a[0] + 2 + Math.round((a[2] - ic.width()) / 2); ic.y = a[1] + Math.round((a[3] - ic.height()) / 2); into.add(ic);
			} else {
				tag(a, "tag_white");
				Image arrow = PdArt.image("icons", 83, 0, 12, 11, 1); arrow.x = a[0] + 2 + (a[2] - 12) / 2; arrow.y = a[1] + (a[3] - 11) / 2; into.add(arrow);
			}
		}
		if (act) tagButton(1, ctag(1)); else tagButtons[1].visible = tagButtons[1].active = false;
		// what lies underfoot (left edge)
		if (tagChanged(3, loot ? "" + under.peek().image() : "-") && loot) {
			a = tagRect(1, true); tag(a, "tag_blue");
			ItemSprite s = new ItemSprite(under); s.scale.set(2); s.x = a[0] + Math.round((a[2] - s.width()) / 2) + 2; s.y = a[1] + Math.round((a[3] - s.height()) / 2); into.add(s);
		}
		if (loot) tagButton(3, tagRect(1, true)); else tagButtons[3].visible = tagButtons[3].active = false;
	}

	/** A tag: PD's 16x14 tag art stretched, reaching 4 px past its rectangle (off the screen's edge). */
	private void tag(int[] a, String art) {
		if (round) {   // a bubble: red, blue or pale
			int[] m = bubble(into, a[0], a[1], a[2], art.equals("tag_red") ? 0xAA0000 : art.equals("tag_blue") ? 0x0055AA : 0xAAAAAA, art.equals("tag_red") ? 0xFF5500 : 0xFFFFFF);
			a[0] = m[0] - 2; a[1] = m[1];
			return;
		}
		NinePatch n = new NinePatch(PdArt.tex(art), 0, 0, 16, 14, 3);
		n.x = a[0]; n.y = a[1]; n.size(a[2] + 4, a[3]);
		into.add(n);
	}

	private void tagButton(int k, int[] a) {
		a = a.clone();
		if (round) { int[] m = bubbleRect(a[0], a[1], a[2]); a[0] = m[0] - 2; a[1] = m[1]; }   // (where tag() puts it)
		HudButton b = tagButtons[k];
		b.setRect(Math.max(0, a[0]), a[1], a[2], a[3]); b.visible = b.active = true;
	}

	// ---- the log: the last two messages over the map, outlined ----
	private float logAge;

	private void refreshLog() {
		List<String[]> es = logEntries();
		String k = es.size() + (es.isEmpty() ? "" : es.get(es.size() - 1)[0]);
		logAge += Game.elapsed;
		log.visible = logAge < 4 * Game.pebxelSpeed;   // gone 4 seconds after the newest message (Pebble PD; game time runs faster)
		if (k.equals(logKey)) return;
		logKey = k; logAge = 0; log.visible = true;
		log.clear();
		if (es.isEmpty()) return;
		int mw = round ? 124 : W - 8 - 22, mx = round ? (W - 124) / 2 : 4;
		String[] l1 = es.get(es.size() - 1), l0 = es.size() > 1 ? es.get(es.size() - 2) : null;
		List<String> w1 = wrap(l1[0], WatchText.G14, mw), w0 = l0 == null ? new ArrayList<>() : wrap(l0[0], WatchText.G14, mw);
		int lh = WatchFonts.line(WatchText.G14) - 2;
		int h1 = w1.size() * lh, h0 = w0.size() * lh;
		if (h0 + h1 > 52) { w0.clear(); h0 = 0; }
		int y1 = (round ? 190 : TB_Y) - h1 - 4, y0 = y1 - h0;
		lines(w0, mx, y0, lh, l0 == null ? WHITE : Integer.parseInt(l0[1]));
		lines(w1, mx, y1, lh, Integer.parseInt(l1[1]));
	}

	private void lines(List<String> ls, int x, int y, int lh, int rgb) {
		for (String s : ls) {
			WatchText t = new WatchText(WatchText.G14, s, rgb); t.outline = true;
			place(t, x, y, 0, 0); log.add(t); y += lh;
		}
	}

	/** Words to lines no wider than w. */
	public static List<String> wrap(String text, int font, int w) {
		List<String> out = new ArrayList<>();
		for (String para : text.split("\n")) {
			StringBuilder line = new StringBuilder();
			for (String word : para.split(" ")) {
				String tryLine = line.length() == 0 ? word : line + " " + word;
				if (WatchText.width(font, tryLine) > w && line.length() > 0) { out.add(line.toString()); line = new StringBuilder(word); }
				else line = new StringBuilder(tryLine);
			}
			out.add(line.toString());
		}
		return out;
	}

	private static Field entriesF, textF, colorF;
	/** Shattered's game log (its newest messages): text and colour. */
	@SuppressWarnings("unchecked")
	static List<String[]> logEntries() {
		List<String[]> out = new ArrayList<>();
		try {
			if (entriesF == null) { entriesF = GameLog.class.getDeclaredField("entries"); entriesF.setAccessible(true); }
			for (Object e : (List<Object>) entriesF.get(null)) {
				if (textF == null) { textF = e.getClass().getDeclaredField("text"); textF.setAccessible(true); colorF = e.getClass().getDeclaredField("color"); colorF.setAccessible(true); }
				int c = (int) colorF.get(e);
				int rgb = c == CharSprite.POSITIVE ? 0x55FF55 : c == CharSprite.NEGATIVE ? 0xFF5555 : c == CharSprite.WARNING ? 0xFFAA55 : c == CharSprite.NEUTRAL ? 0xFFFF55 : WHITE;
				out.add(new String[]{(String) textF.get(e), Integer.toString(rgb)});
			}
		} catch (Exception ex) { }
		return out;
	}

	// ---- the Back button's highlight: Icons.TARGET's corners at 2x around the button ----
	private int shownHl = -2;
	private void refreshReticle() {
		// the Back button's highlight, else the button Select's action belongs to (Pebble PD 0.48: the hands' reticle)
		int hl = highlight >= 0 ? highlight : hooks.toolbarHint();
		if (hl == shownHl) return;
		shownHl = hl;
		reticle.clear();
		if (hl < 0) return;
		int[] r = buttonRect(hl);
		for (int i = 0; i < 4; i++) {
			Image c = PdArt.image("icons", 67 + (i & 1) * 10, (i & 2) * 5, 6, 6, 2);
			c.x = r[0] + (i & 1) * (r[2] - 12); c.y = r[1] + (i >> 1) * (r[3] - 12);
			reticle.add(c);
		}
	}

	// ---- a target the view can't show: its mark at the play area's edge nearest it, a pip on the side it lies ----
	private final Group edge = new Group();
	private Image edgeMark; private ColorBlock edgePip; private int edgeKey = Integer.MIN_VALUE;
	/** x, y: the mark's centre (watch px), x < -900 none; dx, dy: which way the target lies (-1, 0, 1). The pictures
	 *  are made once and only moved (radio time). */
	public void edge(int x, int y, int dx, int dy) {
		int k = x < -900 ? -1 : (x << 16) ^ (y << 4) ^ ((dx + 1) << 2) ^ (dy + 1);
		if (k == edgeKey) return;
		edgeKey = k;
		if (edgeMark == null) {
			edgeMark = PdArt.image("icons", 67, 0, 16, 16, 1); edge.add(edgeMark);
			edgePip = block(YELLOW, 0, 0, 4, 4); edge.add(edgePip);
		}
		edge.visible = x > -900;
		if (!edge.visible) return;
		edgeMark.x = x - 8; edgeMark.y = y - 8;
		edgePip.x = x - 2 + dx * 11; edgePip.y = y - 2 + dy * 11;
	}

	public void pressHighlighted(boolean held) { if (highlight >= 0) hooks.press(highlight, held); }

	@Override
	public void destroy() {
		super.destroy();
		Camera.remove(cam);
	}
}
