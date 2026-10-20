package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.Rankings;
import com.shatteredpixel.shatteredpixeldungeon.SPDSettings;
import com.shatteredpixel.shatteredpixeldungeon.ShatteredPixelDungeon;
import com.shatteredpixel.shatteredpixeldungeon.effects.BannerSprites;
import com.shatteredpixel.shatteredpixeldungeon.journal.Document;
import com.shatteredpixel.shatteredpixeldungeon.messages.Messages;
import com.shatteredpixel.shatteredpixeldungeon.scenes.HeroSelectScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.WelcomeScene;
import com.watabou.noosa.Group;
import com.watabou.noosa.Image;
import java.util.ArrayList;
import java.util.List;

/**
 * Shattered's first screen (the story so far, or what changed in an update), for the watch: the text in pages that
 * fit the screen, a "More" button through them and "Enter the Dungeon" on the last. Afterwards it does what
 * Shattered's own does (records the version, starts a first game at hero select).
 */
public class WatchWelcome extends PixelScene {
	private int previous;
	private final List<List<List<String[]>>> pages = new ArrayList<>();
	private int page;
	private final Group shown = new Group();
	private WatchButton next;

	@Override
	public void create() {
		super.create();
		pebxel.Sounds.theme();
		previous = SPDSettings.version();
		if (ShatteredPixelDungeon.versionCode == previous && !SPDSettings.intro()) {
			ShatteredPixelDungeon.switchNoFade(TitleScene.class);
			return;
		}
		boolean intro = previous == 0 || SPDSettings.intro();
		String msg;
		if (intro) msg = Document.INTROS.pageBody(0);
		else if (previous <= ShatteredPixelDungeon.versionCode) msg = Messages.get(WelcomeScene.class, "update_intro") + "\n\n" + Messages.get(WelcomeScene.class, "update_msg");
		else msg = Messages.get(WelcomeScene.class, "what_msg");

		WatchTitleScene.arches(this);
		add(shown);
		// pages: the first under the logo, the rest the whole text area
		List<List<String[]>> lines = WatchWindow.layout(msg, WatchText.G14, textW());
		int lh = WatchWindow.lineHeight(WatchText.G14), i = 0;
		while (i < lines.size()) {
			if (!pages.isEmpty()) while (i < lines.size() && blank(lines.get(i))) i++;   // (no page starts with a gap)
			if (i >= lines.size()) break;
			int fit = Math.max(1, (textBottom() - textTop(pages.isEmpty())) / lh);
			List<List<String[]>> pg = new ArrayList<>(lines.subList(i, Math.min(lines.size(), i + fit)));
			pages.add(pg); i += pg.size();
		}
		String enter = intro ? Messages.get(TitleScene.class, "enter") : Messages.get(WelcomeScene.class, "continue");
		next = new WatchButton("More") {
			@Override protected void onClick() {
				if (page < pages.size() - 1) { page++; show(enter); } else done(intro);
			}
		};
		if (pebxel.Screen.round) next.setRect(50, 200, 160, 38); else next.setRect(4, 186, 192, 38);
		add(next);
		show(enter);
		fadeIn();
	}

	private static boolean blank(List<String[]> line) { for (String[] s : line) if (!s[0].trim().isEmpty()) return false; return true; }
	private int textW() { return pebxel.Screen.round ? 184 : 188; }
	private int textX() { return (pebxel.Screen.w - textW()) / 2; }
	private int textTop(boolean first) { return first ? (pebxel.Screen.round ? 132 : 110) : (pebxel.Screen.round ? 40 : 8); }
	private int textBottom() { return pebxel.Screen.round ? 196 : 178; }

	private void show(String enter) {
		shown.clear();
		boolean first = page == 0;
		if (first) {
			Image logo = BannerSprites.get(BannerSprites.Type.TITLE_PORT);
			logo.x = Math.round((pebxel.Screen.w - logo.width) / 2); logo.y = pebxel.Screen.round ? 26 : 4;
			shown.add(logo);
		}
		int lh = WatchWindow.lineHeight(WatchText.G14), y = textTop(first);
		List<List<String[]>> pg = pages.get(page);
		// a black backing, so the text reads over the arches
		shown.add(WatchHud.block(0x000000, textX() - 4, y - 3, textW() + 8, pg.size() * lh + 6));
		for (List<String[]> line : pg) {
			int x = textX();
			for (String[] seg : line) {
				WatchText t = new WatchText(WatchText.G14, seg[0], seg[1] != null ? WatchWindow.ICTERINE : 0xFFFFFF);
				WatchHud.place(t, x, y, 0, 0); shown.add(t);
				x += WatchText.width(WatchText.G14, seg[0]);
			}
			y += lh;
		}
		next.label(page < pages.size() - 1 ? "More" : enter);
	}

	private void done(boolean intro) {
		if (intro) {
			SPDSettings.version(ShatteredPixelDungeon.versionCode);
			GamesInProgress.selectedClass = null;
			GamesInProgress.curSlot = GamesInProgress.firstEmpty();
			if (GamesInProgress.curSlot == -1 || Rankings.INSTANCE.totalNumber > 0) {
				SPDSettings.intro(false);
				ShatteredPixelDungeon.switchScene(TitleScene.class);
			} else ShatteredPixelDungeon.switchScene(HeroSelectScene.class);
		} else {
			// Shattered's own bookkeeping for an update (private there)
			try {
				java.lang.reflect.Method m = WelcomeScene.class.getDeclaredMethod("updateVersion", int.class);
				m.setAccessible(true); m.invoke(new WelcomeScene(), previous);
			} catch (Exception e) { SPDSettings.version(ShatteredPixelDungeon.versionCode); }
			ShatteredPixelDungeon.switchScene(TitleScene.class);
		}
	}

	@Override
	protected void onBackPressed() { }
}
