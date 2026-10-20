package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.SPDSettings;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.io.File;
import java.util.Map;
import java.util.TreeMap;

/**
 * How many bytes the watch link sends for each button press in an ordinary game (Down+Select = a step towards the
 * next target), by kind of picture. Args: presses (default 40).  SCREEN=gabbro|emery
 */
public class Traffic {
	public static void main(String[] a) throws Exception {
		int presses = a.length > 0 ? Integer.parseInt(a[0]) : 40;
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		WatchApp app = new WatchApp(game, ".pebxel-traffic", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-traffic/");
		app.create();
		app.graphics.soft.raster = false;
		Link link = new Link(new Assets());
		Controls controls = new Controls();
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		long[] total = {0};
		Runnable frame = () -> {
			app.frame(); controls.frame();
			link.frame(Game.elapsed);
			for (byte[] m : link.take(100000)) total[0] += m.length;
		};
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			frame.run(); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
		}
		for (int f = 0; f < 120; f++) frame.run();
		link.stats.clear(); total[0] = 0;
		long idle0 = total[0];
		for (int f = 0; f < 250; f++) frame.run();   // 10 s idle
		System.out.println("idle 10 s: " + (total[0] - idle0) + " bytes");
		link.stats.clear(); total[0] = 0;
		int[] per = new int[presses];
		for (int p = 0; p < presses; p++) {
			long before = total[0];
			controls.button(p % 2 == 0 ? Controls.DOWN : Controls.SELECT);
			for (int f = 0; f < 25; f++) frame.run();   // one second at 25 fps
			while (!Dungeon.hero.ready) frame.run();
			per[p] = (int) (total[0] - before);
		}
		java.util.Arrays.sort(per);
		System.out.println(presses + " presses: " + total[0] + " bytes, median " + per[presses / 2] + ", 90% " + per[presses * 9 / 10] + ", max " + per[presses - 1]);
		TreeMap<Long, String> top = new TreeMap<>();
		for (Map.Entry<String, long[]> e : link.stats.entrySet()) top.put(-e.getValue()[1] * 1000 - top.size(), e.getKey() + " x" + e.getValue()[0] + " = " + e.getValue()[1] + " B");
		int n = 0; for (String s : top.values()) { System.out.println("  " + s); if (++n == 25) break; }
		System.out.println("depth " + Dungeon.depth + " pos " + Dungeon.hero.pos);
		System.exit(0);
	}
}
