package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.io.File;

/**
 * Presses watch buttons in a script and saves what the watch would show after each press.
 * Args: picture-folder, then presses: u d s b (up down select back), U D S (long), tX,Y a tap (tlX,Y long),
 * or a number = wait that many frames.
 * SEED=n NEWGAME=class to start straight in a new game.
 */
public class Buttons {
	public static void main(String[] a) throws Exception {
		File dir = new File(a[0]); dir.mkdirs();
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		WatchApp app = new WatchApp(game, ".pebxel-buttons", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-buttons/");
		app.create();
		Link link = new Link(new Assets());
		WatchSim sim = new WatchSim(new File("."), new Assets());
		Controls controls = new Controls();
		Touches touches = new Touches(); int seq = 0;
		for (int i = 0; i < 5; i++) app.frame();
		if (System.getenv("ASSIST") != null) WatchSettings.assist(Integer.parseInt(System.getenv("ASSIST")));
		if (System.getenv("NEWGAME") != null) {
			SPDSettings.intro(false);
			GamesInProgress.selectedClass = HeroClass.values()[Integer.parseInt(System.getenv("NEWGAME"))];
			GamesInProgress.curSlot = 1;
			Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
			if (System.getenv("SEED") != null) Dungeon.seed = Long.parseLong(System.getenv("SEED"));
			InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
			Game.switchScene(InterlevelScene.class);
		}
		int shot = 0;
		app.graphics.soft.raster = false;
		for (int i = 1; i < a.length; i++) {
			String p = a[i];
			int wait = 30;
			if (p.matches("\\d+")) wait = Integer.parseInt(p);
			else if (p.startsWith("t") && p.contains(",")) {
				// a tap at watch x,y (tl: a long one)
				String[] xy = p.substring(p.startsWith("tl") ? 2 : 1).split(",");
				int x = Integer.parseInt(xy[0]), y = Integer.parseInt(xy[1]);
				byte[] t = {1, (byte) ++seq, (byte) 0xFF, (byte) (p.startsWith("tl") ? 1 : 0), (byte) x, (byte) (x >> 8), (byte) y, (byte) (y >> 8), 0, 0, 0, 0};
				touches.add(t);
			}
			else {
				int code = 0;
				switch (p.toLowerCase().charAt(0)) { case 'u': code = 1; break; case 'd': code = 2; break; case 's': code = 3; break; case 'b': code = 4; break; }
				if (Character.isUpperCase(p.charAt(0))) code |= Controls.LONG;
				controls.button(code);
			}
			for (int f = 0; f < wait; f++) {
				touches.frame(link, controls, Game.elapsed);
				app.frame(); controls.frame();
				if (Game.scene() instanceof InterlevelScene) Thread.sleep(3); else Thread.sleep(1);
				sim.advance(Game.elapsed); link.frame(Game.elapsed); for (byte[] m : link.take(100000)) sim.apply(m);
			}
			if (System.getenv("NOSHOT") != null && i < a.length - 1) continue;   // (long scripts: only the last picture)
			sim.render();
			sim.save(new File(dir, String.format("%02d-%s.png", shot++, p)), 2);
		}
		System.out.println("scene " + Game.scene().getClass().getSimpleName() + ", " + shot + " pictures");
		System.exit(0);
	}
}
