package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashSet;
import java.util.Set;

/**
 * Test of passing one game between two screens: run two of these at once on the same save folder
 * (one "phone", one "watch"), each a separate program with its own memory like the two Android
 * processes, and drive them with commands on standard input:
 *   new | play N | pause | take | report | quit
 */
public class HandoffTest {
	static WatchGame game;
	public static void main(String[] a) throws Exception {
		String me = a[0], dir = a[1];
		Game.version = "4.0.2"; Game.versionCode = 922;
		game = new WatchGame();
		Smoke.app = new WatchApp(game, dir, false);
		SPDSettings.set(Smoke.app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
		Smoke.app.create();
		for (int i = 0; i < 5; i++) Smoke.app.frame();
		SPDSettings.intro(false);
		BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
		System.out.println("READY " + me);
		String line;
		while ((line = in.readLine()) != null) {
			String[] c = line.trim().split(" ");
			switch (c[0]) {
				case "new":
					GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = Integer.parseInt(c.length > 1 ? c[1] : "1");
					Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
					InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
					Game.switchScene(InterlevelScene.class);
					untilSettled();
					break;
				case "play": play(Integer.parseInt(c[1])); break;
				case "pause": game.pause(); Shared.release(me); break;
				case "take": Shared.takeOver(me); untilSettled(); break;
				case "report": report(me); break;
				case "quit": System.out.println("BYE"); System.exit(0);
			}
			System.out.println("OK " + line);
		}
	}

	static void untilSettled() {
		for (int k = 0; k < 3000; k++) {
			Smoke.step(); sleep(1);
			if (Game.scene() instanceof InterlevelScene) { pressOn(); continue; }
			if (Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready && k > 20) return;
			if (!(Game.scene() instanceof GameScene) && k > 60) return;
		}
	}

	static void play(int n) {
		Set<Integer> bad = new HashSet<>();
		for (int d = 0, guard = 0; d < n && guard < 20000; guard++) {
			Smoke.step();
			if (Game.scene() instanceof InterlevelScene) { sleep(2); pressOn(); }
			if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null || !Dungeon.hero.isAlive()) continue;
			Window w = Ui.topWindow();
			if (w != null) { w.onBackPressed(); continue; }
			if (!Dungeon.hero.ready) continue;
			int t = Smoke.choose(bad);
			if (t == -2) { Dungeon.hero.search(true); Smoke.settle(); d++; }
			else if (t >= 0 && Dungeon.hero.handle(t)) { Dungeon.hero.next(); Smoke.settle(); d++; }
			else bad.add(t);
		}
	}

	static void report(String me) {
		String scene = Game.scene() == null ? "none" : Game.scene().getClass().getSimpleName();
		if (Dungeon.hero == null) { System.out.println("REPORT " + me + " " + scene + " no-hero"); return; }
		System.out.println("REPORT " + me + " " + scene + " seed " + Dungeon.seed + " depth " + Dungeon.depth + " pos " + Dungeon.hero.pos
				+ " hp " + Dungeon.hero.HP + " exp " + Dungeon.hero.exp + " gold " + Dungeon.gold + " slot " + GamesInProgress.curSlot);
	}

	/** The story screen before a floor waits for its button. */
	public static void pressOn() {
		for (Ui.Part p : Ui.parts(Game.scene()))
			if (p.button && !p.text.isEmpty() && !p.text.equals("Button")) { Ui.press(p.g); break; }
	}

	static void sleep(int ms) { try { Thread.sleep(ms); } catch (InterruptedException e) { } }
}
