package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;

/** Takes pictures of Shattered PD laid out for a 200x228 watch, in the watch's 64 colours. */
public class Shots {
	static WatchApp app; static WatchGame game; static File out;

	public static void main(String[] a) throws Exception {
		out = new File(a.length > 0 ? a[0] : "shots"); out.mkdirs();
		Game.version = "4.0.2"; Game.versionCode = 922;
		game = new WatchGame();
		app = new WatchApp(game, ".pebxel-shots", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-shots/");
		app.create();
		frames(60); shot("01-first-screen");
		dump();
		System.exit(0);
	}

	static void frames(int n) { for (int i = 0; i < n; i++) { app.frame(); try { Thread.sleep(1); } catch (InterruptedException e) { } } }

	static void dump() {
		System.out.println("scene " + Game.scene().getClass().getSimpleName());
		for (Ui.Part p : Ui.parts(Game.scene())) System.out.println(p.button ? "  [" + p.text + "]" : "  \"" + p.text.replace('\n', ' ') + "\"");
	}

	static void shot(String name) throws Exception {
		game.drawNext = true; app.frame();
		SoftGL g = app.graphics.soft;
		int w = g.W / 2, h = g.H / 2;
		BufferedImage img = new BufferedImage(w * 3, h * 3, BufferedImage.TYPE_INT_RGB);
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			int r = 0, gg = 0, b = 0;
			for (int k = 0; k < 4; k++) { int c = g.fb[(y * 2 + k / 2) * g.W + x * 2 + k % 2]; r += (c >> 16) & 255; gg += (c >> 8) & 255; b += c & 255; }
			int c = (q(r / 4) << 16) | (q(gg / 4) << 8) | q(b / 4);
			for (int k = 0; k < 9; k++) img.setRGB(x * 3 + k % 3, y * 3 + k / 3, c);
		}
		ImageIO.write(img, "png", new File(out, name + ".png"));
		System.out.println("shot " + name);
	}

	/** Pebble colour: each channel is one of 0, 85, 170, 255. */
	static int q(int v) { return Math.round(v / 85f) * 85; }
}
