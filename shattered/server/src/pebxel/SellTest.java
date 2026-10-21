package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Shopkeeper;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfTeleportation;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.GroupAccess;
import com.watabou.utils.FileUtils;
import com.watabou.utils.PathFinder;
import java.util.*;

/**
 * A human-length sell session at the first shop, played with the watch's buttons only (WatchDriver: the Link, the
 * WatchSim model): Down to the shopkeeper, Select, "Sell", then round and round (the item's cell in the bag, Select,
 * "Sell" in the trade window) and Back out at the end. After every sale it prints what the watch holds (the Link's
 * pictures, the model's objects and memory) and how many windows the scene has: all of them should stay flat.
 * Args: sales (default 36), class (default 0), seed (default 3). Env SCREEN (emery). Exit 1 if anything grew.
 */
public final class SellTest {
	public static void main(String[] a) throws Exception {
		int sales = a.length > 0 ? Integer.parseInt(a[0]) : 36;
		HeroClass cls = HeroClass.values()[a.length > 1 ? Integer.parseInt(a[1]) : 0];
		long seed = a.length > 2 ? Long.parseLong(a[2]) : 3;
		String dir = ".pebxel-selltest-" + ProcessHandle.current().pid();
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		WatchApp app = new WatchApp(game, dir, true);
		app.graphics.soft.raster = false;
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		app.create();
		Lockstep.watchdog("class " + cls + " seed " + seed);
		WatchDriver w = new WatchDriver(app);
		for (int i = 0; i < 5; i++) w.frame();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = cls; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = seed;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		int code = 0;
		try {
			// straight down to the first shop (depth 6)
			for (long f = 0; f < 200_000; f++) {
				w.frame();
				if (Game.scene() instanceof InterlevelScene) { if (f % 40 == 39) w.press(Controls.SELECT); continue; }
				if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null) continue;
				if (Ui.topWindow() != null) { w.press(Controls.BACK); continue; }
				if (!Dungeon.hero.ready) continue;
				Dungeon.hero.HP = Dungeon.hero.HT;
				if (shop() != null) break;
				com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(false);
				if (tr != null) StairsTest.jump(tr);
			}
			Shopkeeper sk = shop();
			if (sk == null) { System.out.println("no shop reached"); System.exit(2); }
			System.out.println("shop at depth " + Dungeon.depth);
			// next to the shopkeeper, with things to sell: a few kinds and a big stack (sold one at a time)
			int next = -1;
			for (int d : PathFinder.NEIGHBOURS8) { int n = sk.pos + d; if (Dungeon.level.passable[n] && Actor.findChar(n) == null && Dungeon.level.heaps.get(n) == null) { next = n; break; } }
			ScrollOfTeleportation.appear(Dungeon.hero, next); Dungeon.level.occupyCell(Dungeon.hero); Dungeon.observe();
			new com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.darts.Dart().quantity(sales).collect();
			for (int k = 0; k < 6; k++) new com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.Dagger().identify().collect();
			w.frames(30); w.toolbar(pebxel.ui.WatchHud.TB_WAIT, false); w.frames(30);
			int first = -1, firstObjs = -1, firstWnd = -1, sold = 0, startGold = Dungeon.gold;
			int[] objsAt = new int[sales + 1];
			// Down to the shopkeeper, Select: its window
			if (!w.targetTo(sk.pos, false)) { System.out.println("shopkeeper at " + sk.pos + " hero " + Dungeon.hero.pos + " not in the Down list: targets " + w.targets() + " " + Assist.plan(false, null)); System.exit(2); }
			w.frames(20);
			Window o = Ui.topWindow();
			Gizmo sell = o == null ? null : BotWindows.byText(Controls.buttons(o), "sell");
			if (sell == null || !w.focusTo(o, sell, 10)) { System.out.println("no Sell in " + o); System.exit(2); }
			w.press(Controls.SELECT); w.frames(10);
			while (sold < sales) {
				Window b = Ui.topWindow();
				if (!(b instanceof pebxel.ui.WndWatchBag)) { System.out.println("sale " + sold + ": no bag (" + b + ")"); code = 1; break; }
				Item it = null;
				for (Item i : Dungeon.hero.belongings.backpack.items) if (i instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.Dagger) { it = i; break; }
				if (it == null || sold % 2 == 1) it = Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.darts.Dart.class);
				if (it == null) break;
				if (!w.bagTo(it, true)) { System.out.println("sale " + sold + ": couldn't reach " + it); code = 1; break; }
				w.frames(10);
				Window t = Ui.topWindow();
				Gizmo s = t == null ? null : BotWindows.byText(Controls.buttons(t), "sell");
				if (s == null || !w.focusTo(t, s, 10)) { System.out.println("sale " + sold + ": no Sell in " + t); code = 1; break; }
				w.press(Controls.SELECT); w.frames(15);
				sold++;
				int objs = linkObjs(w), wins = windows();
				objsAt[sold] = objs;
				if (sold == 3) { first = w.sim.heapEstimate(); firstObjs = objs; firstWnd = wins; }
				System.out.printf("sale %2d: link pictures %d, watch objects %d, watch memory %d, windows %d, gold %d%n", sold, objs, w.sim.objCount(), w.sim.heapEstimate(), wins, Dungeon.gold);
			}
			// Back out: how many presses until the game is back
			int backs = 0;
			while (Ui.topWindow() != null && backs < 60) { w.press(Controls.BACK); w.frames(5); backs++; }
			w.frames(30);
			System.out.printf("sold %d for %d gold; Back pressed %d times to leave; then link pictures %d, watch objects %d, memory %d, windows %d%n",
					sold, Dungeon.gold - startGold, backs, linkObjs(w), w.sim.objCount(), w.sim.heapEstimate(), windows());
			System.out.println("WATCH " + w.summary());
			if (sold >= 4 && (objsAt[sold] > firstObjs + 20 || windows() > 0 || backs > 3)) { System.out.println("GREW: pictures " + firstObjs + " -> " + objsAt[sold] + ", windows " + firstWnd + " -> " + windows()); code = 1; }
			else System.out.println(code == 0 ? "FLAT" : "FAILED");
		} catch (Throwable e) { e.printStackTrace(System.out); code = 3; }
		try { com.badlogic.gdx.Gdx.files.external(dir).deleteDirectory(); com.badlogic.gdx.Gdx.files.local(dir).deleteDirectory(); } catch (Throwable e) { }
		System.exit(code);
	}

	static Shopkeeper shop() {
		for (Mob m : Dungeon.level.mobs) if (m instanceof Shopkeeper) return (Shopkeeper) m;
		return null;
	}

	static int linkObjs(WatchDriver w) { Object m = WatchDriver.get(w.link, "objs"); return m instanceof Map ? ((Map<?, ?>) m).size() : -1; }

	static int windows() {
		int n = 0;
		for (Gizmo g : GroupAccess.members(Game.scene())) if (g instanceof Window && g.exists) n++;
		return n;
	}
}
