package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.scenes.*;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.*;
import com.watabou.noosa.Game;
import com.watabou.noosa.Scene;
import com.watabou.utils.FileUtils;
import java.io.File;
import java.util.*;
import java.util.function.Supplier;

/**
 * A picture of every main screen and window as the watch would show it, for checking layouts.
 * Args: picture-folder [names to include...]
 */
public class Tour {
	static WatchApp app; static Link link; static WatchSim sim; static File dir; static int n;

	public static void main(String[] a) throws Exception {
		dir = new File(a[0]); dir.mkdirs();
		Set<String> only = new HashSet<>(Arrays.asList(a).subList(1, a.length));
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		app = new WatchApp(game, ".pebxel-tour", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-tour/");
		app.create();
		link = new Link(new Assets()); sim = new WatchSim(new File("."), new Assets());
		app.graphics.soft.raster = false;
		frames(5);
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			frames(1); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) {
				// the story pages on the loading screen: a picture of each, then on
				boolean ready = false;
				for (Ui.Part p : Ui.parts(Game.scene())) if (p.button && !p.text.isEmpty() && !p.text.equals("Button") && p.g instanceof com.shatteredpixel.shatteredpixeldungeon.ui.Button && ((com.shatteredpixel.shatteredpixeldungeon.ui.Button) p.g).active) ready = true;
				if (ready && pages < 6) { frames(40); shot("loading" + pages++); }
				HandoffTest.pressOn();
				if (ready) frames(5);
			}
		}
		controls = new Controls();
		frames(30);
		if (System.getenv("TOURITEMS") != null) {
			// a fuller pack, to see the labels: unknown potion and scroll, a wand, an upgraded sword, a heavy axe, a cursed ring
			String P = "com.shatteredpixel.shatteredpixeldungeon.items.";
			for (String c : new String[]{"potions.PotionOfHealing", "potions.PotionOfStrength", "scrolls.ScrollOfIdentify", "wands.WandOfMagicMissile",
					"weapon.melee.Longsword", "weapon.melee.Greataxe", "rings.RingOfMight", "weapon.missiles.Javelin", "food.Food"}) {
				com.shatteredpixel.shatteredpixeldungeon.items.Item it = (com.shatteredpixel.shatteredpixeldungeon.items.Item) Class.forName(P + c).getDeclaredConstructor().newInstance();
				if (c.endsWith("Longsword")) { it.upgrade(); it.upgrade(); it.identify(); }
				if (c.endsWith("RingOfMight")) { it.cursed = true; it.cursedKnown = true; it.levelKnown = true; it.degrade(); }
				if (c.endsWith("WandOfMagicMissile")) it.identify();
				it.collect();
			}
		}
		LinkedHashMap<String, Supplier<Window>> wins = new LinkedHashMap<>();
		Item weapon = Dungeon.hero.belongings.weapon;
		wins.put("bag", () -> new WndBag(Dungeon.hero.belongings.backpack));
		wins.put("item", () -> new WndUseItem(null, weapon));
		wins.put("iteminfo", () -> new WndInfoItem(weapon));
		wins.put("hero", WndHero::new);
		wins.put("journal", WndJournal::new);
		wins.put("game", WndGame::new);
		wins.put("talents", () -> { com.shatteredpixel.shatteredpixeldungeon.windows.WndHero.lastIdx = 1; pebxel.ui.Swaps.nativeHero = true; return new WndHero(); });
		wins.put("catalogue", pebxel.ui.WndWatchRecords::catalogue);
		wins.put("journal2", pebxel.ui.WndWatchRecords::journal);
		wins.put("menu-records", () -> { pebxel.ui.WndWatchMenu.lastPage = 1; return new WndGame(); });
		wins.put("menu-settings", () -> { pebxel.ui.WndWatchMenu.lastPage = 2; return new WndGame(); });
		wins.put("settings", WndSettings::new);
		wins.put("options", () -> new WndOptions("A question", "Do you want to do the thing? It is a long question with some words.", "Yes", "No"));
		wins.put("challenges", () -> new WndChallenges(0, false));
		wins.put("story", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndStory("The Dungeon lies right beneath the City, its upper levels actually constitute the City's sewer system. As dark energy has crept up from below the usually harmless sewer creatures have become more and more dangerous. The city sends guard patrols down here to try and maintain safety for those above, but they are slowly failing."));
		wins.put("mob", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoMob(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Rat()));
		wins.put("heroinfo", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndHeroInfo(HeroClass.WARRIOR));
		wins.put("gameinfo", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndGameInProgress(1));
		wins.put("message", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndMessage("You found a secret door! Secrets like this one can hide all sorts of things."));
		shot("map");
		if (System.getenv("DUMPMAP") != null) sim.dump();
		for (Map.Entry<String, Supplier<Window>> e : wins.entrySet()) {
			if (!only.isEmpty() && !only.contains(e.getKey())) continue;
			Window w = e.getValue().get();
			GameScene.show(w);
			pebxel.ui.Swaps.nativeHero = false;
			frames(40);
			shot(e.getKey());
			if (e.getKey().equals("item") && Ui.topWindow() != null) {
				// a long description's next page (round), pressing the text
				List<Ui.Part> bs = Controls.buttons(Ui.topWindow());
				if (!bs.isEmpty() && !(bs.get(0).g instanceof pebxel.ui.WatchButton)) { Ui.press(bs.get(0).g); frames(10); shot("item-page2"); }
			}
			if (System.getenv("BACKTEST") != null) {
				Window before = Ui.topWindow();
				controls.button(Controls.BACK); frames(10);
				System.out.println("BACK on " + e.getKey() + ": " + (Ui.topWindow() == before ? "STILL OPEN" : "closed"));
				if (Ui.topWindow() != before) continue;
			}
			Window tw = Ui.topWindow();
			if (tw != null) System.out.println("window " + e.getKey() + " " + tw.getClass().getSimpleName() + " at " + tw.camera.x / 2 + "," + tw.camera.y / 2 + " size " + tw.camera.width * tw.camera.zoom / 2 + "x" + tw.camera.height * tw.camera.zoom / 2);
			if (System.getenv("DUMPWIN") != null) sim.dump();
			while (Ui.topWindow() != null) { Ui.topWindow().hide(); frames(2); }
			frames(10);
		}
		if (only.isEmpty() || only.contains("death")) {
			// the end of a game: the banner, then any button opens the menu's last page; then the rankings entry
			com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Rat killer = new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Rat();
			Dungeon.fail(killer);   // (what a killing blow does: the rankings entry)
			Dungeon.hero.die(killer);
			frames(90); shot("death");
			controls.button(Controls.SELECT); frames(40); shot("death-menu");
			while (Ui.topWindow() != null) { Ui.topWindow().hide(); frames(2); }
			Game.switchScene(RankingsScene.class); frames(60); shot("rankings-entry");
			List<Ui.Part> rs = Controls.buttons(Game.scene());
			if (!rs.isEmpty()) { Ui.press(rs.get(0).g); frames(40); shot("ranking"); }
			while (Ui.topWindow() != null) { Ui.topWindow().hide(); frames(2); }
		}
		LinkedHashMap<String, Class<? extends Scene>> scenes = new LinkedHashMap<>();
		scenes.put("title", TitleScene.class); scenes.put("start", StartScene.class); scenes.put("heroselect", HeroSelectScene.class);
		scenes.put("rankings", RankingsScene.class); scenes.put("journalscene", JournalScene.class); scenes.put("about", AboutScene.class);
		scenes.put("changes", ChangesScene.class); scenes.put("welcome", WelcomeScene.class);
		for (Map.Entry<String, Class<? extends Scene>> e : scenes.entrySet()) {
			if (!only.isEmpty() && !only.contains(e.getKey())) continue;
			if (e.getKey().equals("welcome")) SPDSettings.intro(true);   // (else it goes straight on to the title)
			Game.switchScene(e.getValue());
			frames(60);
			shot(e.getKey());
			if (e.getKey().equals("welcome")) {
				// its later pages
				for (int k = 1; k < 4 && Game.scene() instanceof pebxel.ui.WatchWelcome; k++) {
					for (Ui.Part p : Controls.buttons(Game.scene())) Ui.press(p.g);
					frames(30); shot("welcome" + k);
				}
			}
		}
		System.out.println(n + " pictures");
		System.exit(0);
	}

	static Controls controls;
	static int pages;   // (the watch's HUD and its window handling, as in a real game)

	static void frames(int k) {
		for (int f = 0; f < k; f++) {
			app.frame(); if (controls != null) controls.frame();
			sim.advance(Game.elapsed); sim.apply(link.sync(Game.elapsed));
		}
	}

	static void shot(String name) throws Exception {
		sim.render();
		sim.save(new File(dir, String.format("%02d-%s.png", n++, name)), 2);
	}
}
