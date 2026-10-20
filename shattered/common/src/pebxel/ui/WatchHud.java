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
 * toolbar buttons (wait, search, inspect | backpack, quickslot 2, quickslot 1), the buttons' tags at the right
 * edge, the loot and resume tags at the left, and the last two log messages, outlined, over the map.
 * Numbers and places follow watch/src/c/main.c (draw_hud_art, draw_hud_text, ctag, tag_rect, tb_x).
 * Shattered's own status pane, menu, toolbar, log and tags still run (unseen: Link skips them); the buttons here
 * press theirs, so everything behaves as Shattered does.
 */
public class WatchHud extends Group {
	public static final int W = 200, H = 228;
	static int SC(int v) { return v * 3 / 2; }
	static final int SP_H = 35, TB_H = 38, TB_W = 32, TB_GAP = 8, TB_N = 6, TB_Y = H - TB_H;
	public static final int TB_WAIT = 0, TB_SEARCH = 1, TB_INFO = 2, TB_BAG = 3, TB_Q2 = 4, TB_Q1 = 5, B_MENU = 6, B_PORTRAIT = 7;
	static final int YELLOW = 0xFFFF55, LIGHT = 0xAAAAAA, WHITE = 0xFFFFFF;

	public final Camera cam;
	/** What the Back button has highlighted (-1 none): toolbar 0-5, menu 6, portrait 7. */
	public int highlight = -1;
	private final Hooks hooks;

	/** What the buttons do (Controls supplies these, so the watch buttons and taps agree). */
	public interface Hooks {
		void press(int button, boolean held);
		/** The cell Select acts on, and what it is: 0 nothing, 1 enemy, 2 item, 3 go there. */
		int targetCell();
		int targetKind();
		int targetCount();
		int mapZoomLevel();
		void tag(int which, boolean held);   // 0 zoom, 1 act, 2 next target, 3 loot, 4 resume
	}

	private Image avatar; private int avatarClass = -1, avatarTier = -1;
	private final Image hpBar;
	private final ColorBlock xpBar;
	private final WatchText level, hp, depth, keys;
	private final List<Image> buffs = new ArrayList<>(); private String buffKey = "";
	private final ItemSprite[] quick = new ItemSprite[2];
	private final ColorBlock[] quickFog = new ColorBlock[2];
	private final WatchText[] quickText = new WatchText[2];
	private final Group tags = new Group();
	private final Group log = new Group(); private String logKey = "";
	private final Group reticle = new Group();
	private final HudButton[] buttons = new HudButton[8];
	private final HudButton[] tagButtons = new HudButton[5];

	public WatchHud(Hooks hooks) {
		this.hooks = hooks;
		cam = new Camera(0, 0, W, H, 2);   // two phone pixels per watch pixel: one unit here is one watch pixel
		Camera.add(cam);
		camera = cam;

		// the status pane: PD's art at 1.5x, its left 80 px, a stretched column, its right 48 px
		add(PdArt.image("status_pane", 0, 0, 80, SP_H, 1.5f));
		Image mid = PdArt.image("status_pane", 80, 0, 1, SP_H, 1.5f); mid.x = SC(80); mid.scale.x = W - SC(128); add(mid);
		Image right = PdArt.image("status_pane", 80, 0, 48, SP_H, 1.5f); right.x = W - SC(48); add(right);
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
		int[][] icon = {{5, 8, 10, 10}, {24, 7, 12, 12}, {46, 7, 8, 12}, {65, 5, 14, 15}};
		for (int i = 0; i < TB_N; i++) {
			NinePatch b = new NinePatch(PdArt.tex("toolbar"), 83, 0, 23, 25, 4);
			b.x = tbX(i); b.y = TB_Y; b.size(TB_W, TB_H); add(b);
			if (i < 4) {
				Image ic = PdArt.image("toolbar", icon[i][0], icon[i][1], icon[i][2], icon[i][3], 2);
				ic.x = tbX(i) + (TB_W - 2 * icon[i][2]) / 2; ic.y = TB_Y + (TB_H - 2 * icon[i][3]) / 2; add(ic);
			}
		}
		for (int q = 0; q < 2; q++) {
			int bx = tbX(q == 0 ? TB_Q1 : TB_Q2);
			quick[q] = new ItemSprite(); quick[q].scale.set(2); quick[q].x = bx + (TB_W - 32) / 2; quick[q].y = TB_Y + 3; quick[q].visible = false; add(quick[q]);
			quickFog[q] = block(0x000000, bx + (TB_W - 32) / 2, TB_Y + 3, 32, 32); quickFog[q].alpha(0.5f); quickFog[q].visible = false; add(quickFog[q]);
			add(quickText[q] = new WatchText(WatchText.G14B, "", WHITE)); quickText[q].outline = true;
		}
		add(tags);
		add(log);
		add(reticle);

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

	static int tbX(int i) { return (W - TB_N * TB_W - TB_GAP) / 2 + i * TB_W + (i >= 3 ? TB_GAP : 0); }
	public static int[] buttonRect(int k) {
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
		super.update();
	}

	private void refresh() {
		Hero h = Dungeon.hero;
		if (h == null) return;
		// the portrait: the hero at 2x, as dressed
		int tier = h.tier(), cls = h.heroClass.ordinal();
		if (avatar == null || tier != avatarTier || cls != avatarClass) {
			if (avatar != null) avatar.killAndErase();
			avatar = HeroSprite.avatar(h); avatar.scale.set(2);
			avatar.x = SC(15) - avatar.width; avatar.y = SC(16) - avatar.height;
			com.watabou.noosa.GroupAccess.addAfter(this, avatar, members.get(2));
			avatarTier = tier; avatarClass = cls;
		}
		int hpNow = Math.max(0, h.HP);
		hpBar.scale.x = SC(64) * hpNow / (float) Math.max(1, h.HT) / 64f;
		hpBar.visible = hpNow > 0;
		xpBar.size(SC(64) * h.exp / (float) Math.max(1, h.maxExp()), 2);
		level.text(Integer.toString(h.lvl)); place(level, SC(22) - 2, SC(21), SC(12) + 4, 1);
		hp.text(hpNow + "/" + h.HT); place(hp, SC(31), SC(7), 60, 0);
		depth.text(Integer.toString(Dungeon.depth)); place(depth, W - SC(42) - 24, -1, 24, 2);
		keys.text(Integer.toString(Notes.keyCount(new IronKey(Dungeon.depth)))); place(keys, W - SC(26) - 24, -1, 24, 2);

		// buffs, 7x7 at 2x, as many as fit beside the level box
		StringBuilder key = new StringBuilder();
		List<Buff> bs = new ArrayList<>();
		for (Buff b : h.buffs()) if (b.icon() != BuffIndicator.NONE) { bs.add(b); key.append(b.icon()).append(','); }
		int fit = (W - SC(38) - SC(48) + 2) / 16;
		if (!key.toString().equals(buffKey)) {
			buffKey = key.toString();
			for (Image i : buffs) i.killAndErase();
			buffs.clear();
			for (int i = 0; i < bs.size() && i < fit; i++) {
				BuffIcon ic = new BuffIcon(bs.get(i), false);
				ic.scale.set(2); ic.x = SC(38) + i * 16; ic.y = SC(22);
				buffs.add(ic); com.watabou.noosa.GroupAccess.addAfter(this, ic, keys);
			}
		}

		// quickslots: the item at 2x; greyed when not in the backpack; its count or charges
		for (int q = 0; q < 2; q++) {
			Item it = Dungeon.quickslot.getItem(q);
			int bx = tbX(q == 0 ? TB_Q1 : TB_Q2);
			quick[q].visible = it != null;
			quickText[q].text("");
			if (it == null) { quickFog[q].visible = false; continue; }
			quick[q].view(it); quick[q].scale.set(2);
			boolean have = Dungeon.quickslot.isNonePlaceholder(q);
			quickFog[q].visible = !have;
			if (!have) continue;
			if (it instanceof Wand && it.levelKnown) {
				quickText[q].text(it.status()); place(quickText[q], bx + 3, TB_Y - 1, TB_W - 4, 0);
			} else if (it.quantity() > 1) {
				quickText[q].text(Integer.toString(it.quantity())); place(quickText[q], bx, TB_Y + TB_H - 18, TB_W - 4, 2);
			}
		}

		refreshTags(h);
		refreshLog();
		refreshReticle();
	}

	// ---- the tags ----
	private String tagKey = "";

	private static int[] ctag(int k) {
		int bot = TB_Y - 2, top = H - bot;
		return new int[]{W - 22, k == 0 ? top : k == 1 ? H / 2 - 12 : bot - 24, 22, 24};
	}

	private static int[] tagRect(int which, boolean lootShown) {
		int y = SC(33) + 2;
		if (which > 1 && lootShown) y += 38;
		return new int[]{-4, y, 40, which == 1 ? 30 : 36};
	}

	private void refreshTags(Hero h) {
		boolean alive = h.isAlive();
		int kind = hooks.targetKind(), cell = hooks.targetCell(), n = hooks.targetCount(), z = hooks.mapZoomLevel();
		Heap under = Dungeon.level.heaps.get(h.pos);
		boolean loot = alive && under != null && !(cell == h.pos && kind == 2);
		Item weapon = h.belongings.weapon();
		String k = alive + "/" + kind + "/" + cell + "/" + n + "/" + z + "/" + loot + "/" + (under != null ? under.peek().image() : 0) + "/" + (weapon == null ? 0 : weapon.image());
		if (cell >= 0 && kind == 2 && Dungeon.level.heaps.get(cell) != null) k += "/" + Dungeon.level.heaps.get(cell).peek().image();
		if (k.equals(tagKey)) return;
		tagKey = k;
		tags.clear();
		for (HudButton b : tagButtons) { b.visible = b.active = false; }
		if (!alive) return;
		// zoom (Up): the glass and a pip per level
		int[] a = ctag(0);
		tag(a, "tag_red"); tagButton(0, a);
		Image glass = PdArt.image("tag_red", 16, 1, 12, 12, 1); glass.x = a[0] + 3; glass.y = a[1] + 6; tags.add(glass);
		for (int i = 0; i <= z; i++) tags.add(block(YELLOW, a[0] + 17, a[1] + 17 - i * 5, 3, 3));
		// the next target (Down)
		if (n > 1) {
			a = ctag(2); tag(a, "tag_red"); tagButton(2, a);
			Image t = PdArt.image("icons", 67, 0, 16, 16, 1); t.x = a[0] + 2 + (a[2] - 16) / 2; t.y = a[1] + (a[3] - 16) / 2; tags.add(t);
		}
		// what Select does: attack (red, the weapon), pick up (blue, the thing), go there (pale, the walk arrow)
		if (cell >= 0 && kind > 0) {
			a = ctag(1); tagButton(1, a);
			if (kind == 1) {
				tag(a, "tag_red");
				if (weapon != null) { ItemSprite s = new ItemSprite(weapon); s.x = a[0] + 2 + (a[2] - 16) / 2; s.y = a[1] + (a[3] - 16) / 2; tags.add(s); }
			} else if (kind == 2 && Dungeon.level.heaps.get(cell) != null) {
				tag(a, "tag_blue");
				ItemSprite s = new ItemSprite(Dungeon.level.heaps.get(cell)); s.x = a[0] + 2 + (a[2] - 16) / 2; s.y = a[1] + (a[3] - 16) / 2; tags.add(s);
			} else {
				tag(a, "tag_white");
				Image arrow = PdArt.image("icons", 83, 0, 12, 11, 1); arrow.x = a[0] + 2 + (a[2] - 12) / 2; arrow.y = a[1] + (a[3] - 11) / 2; tags.add(arrow);
			}
		}
		// what lies underfoot (left edge)
		if (loot) {
			a = tagRect(1, true); tag(a, "tag_blue"); tagButton(3, a);
			ItemSprite s = new ItemSprite(under); s.scale.set(2); s.x = a[0] + (a[2] - 32) / 2 + 2; s.y = a[1] + (a[3] - 32) / 2; tags.add(s);
		}
	}

	/** A tag: PD's 16x14 tag art stretched, reaching 4 px past its rectangle (off the screen's edge). */
	private void tag(int[] a, String art) {
		NinePatch n = new NinePatch(PdArt.tex(art), 0, 0, 16, 14, 3);
		n.x = a[0]; n.y = a[1]; n.size(a[2] + 4, a[3]);
		tags.add(n);
	}

	private void tagButton(int k, int[] a) {
		HudButton b = tagButtons[k];
		b.setRect(Math.max(0, a[0]), a[1], a[2], a[3]); b.visible = b.active = true;
	}

	// ---- the log: the last two messages over the map, outlined ----
	private void refreshLog() {
		List<String[]> es = logEntries();
		String k = es.size() + (es.isEmpty() ? "" : es.get(es.size() - 1)[0]);
		if (k.equals(logKey)) return;
		logKey = k;
		log.clear();
		if (es.isEmpty()) return;
		int mw = W - 8 - 22, mx = 4;
		String[] l1 = es.get(es.size() - 1), l0 = es.size() > 1 ? es.get(es.size() - 2) : null;
		List<String> w1 = wrap(l1[0], WatchText.G14, mw), w0 = l0 == null ? new ArrayList<>() : wrap(l0[0], WatchText.G14, mw);
		int lh = WatchFonts.line(WatchText.G14) - 2;
		int h1 = w1.size() * lh, h0 = w0.size() * lh;
		if (h0 + h1 > 52) { w0.clear(); h0 = 0; }
		int y1 = TB_Y - h1 - 4, y0 = y1 - h0;
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
		if (highlight == shownHl) return;
		shownHl = highlight;
		reticle.clear();
		if (highlight < 0) return;
		int[] r = buttonRect(highlight);
		for (int i = 0; i < 4; i++) {
			Image c = PdArt.image("icons", 67 + (i & 1) * 10, (i & 2) * 5, 6, 6, 2);
			c.x = r[0] + (i & 1) * (r[2] - 12); c.y = r[1] + (i >> 1) * (r[3] - 12);
			reticle.add(c);
		}
	}

	public void pressHighlighted(boolean held) { if (highlight >= 0) hooks.press(highlight, held); }

	@Override
	public void destroy() {
		super.destroy();
		Camera.remove(cam);
	}
}
