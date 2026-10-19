package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Game;
import com.watabou.utils.PointF;
import java.util.ArrayDeque;

/**
 * Touches from the watch, fed to Shattered as a real finger: its own buttons, map taps, long presses and
 * list drags all work as on a phone. Message (12 bytes, see the watch's main.c):
 * [kind 1 tap, 2 map drag (dx, dy), 3 down, 4 move, 5 up][seq][camera hit][flags: 1 long][x s16][y s16][stamp u32]
 * Coordinates are watch pixels; Shattered's hidden screen has two phone pixels per watch pixel.
 */
public class Touches {
	private static final int ID = 0;
	private final ArrayDeque<byte[]> in = new ArrayDeque<>();
	private float clock, upAt = -1;
	private int upX, upY;
	private float dragDx, dragDy;
	private int lastY = Integer.MIN_VALUE;

	public synchronized void add(byte[] b) { if (b != null && b.length >= 12) in.add(b.clone()); }
	private synchronized byte[] poll() { return in.poll(); }

	/** Call on the game thread before each frame, with the time since the last call. */
	public void frame(Link link, Controls controls, float dt) {
		clock += dt;
		if (upAt >= 0 && clock >= upAt) { Game.inputHandler.touchUp(upX, upY, ID, 0); upAt = -1; }
		byte[] b;
		while ((b = poll()) != null) {
			int kind = b[0], seq = b[1] & 255, cam = b[2] & 255;
			boolean lng = (b[3] & 1) != 0;
			int x = (short) ((b[4] & 255) | (b[5] << 8)), y = (short) ((b[6] & 255) | (b[7] << 8));
			int stamp = (b[8] & 255) | (b[9] & 255) << 8 | (b[10] & 255) << 16 | (b[11] & 255) << 24;
			controls.touched();
			int px = x * 2 + 1, py = y * 2 + 1;
			switch (kind) {
				case 1: { // a tap: press, and let go next frame (or after a long press)
					int[] p = link.remap(cam, stamp, px, py);
					if (p == null) break;   // the window it was aimed at has gone: don't guess
					if (upAt >= 0) Game.inputHandler.touchUp(upX, upY, ID, 0);
					Game.inputHandler.touchDown(p[0], p[1], ID, 0);
					upX = p[0]; upY = p[1];
					upAt = clock + (lng ? Button.longClick + 0.1f : 0.001f);
					break;
				}
				case 2: // the map dragged (as Shattered's own map drag does)
					if (Ui.topWindow() == null && Game.scene() instanceof GameScene && Camera.main != null) {
						Camera m = Camera.main;
						m.shift(new PointF(-x * 2 / m.zoom, -y * 2 / m.zoom));
					}
					break;
				case 3: { // a drag on the interface starts
					Window w = Ui.topWindow();
					lastY = py;
					if (w != null && Controls.tall(w)) break;   // a tall window scrolls itself instead
					int[] p = link.remap(cam, stamp, px, py);
					if (p == null) { dragDx = Float.NaN; break; }
					dragDx = p[0] - px; dragDy = p[1] - py;
					Game.inputHandler.touchDown(p[0], p[1], ID, 0);
					break;
				}
				case 4: { // moves on
					Window w = Ui.topWindow();
					if (w != null && Controls.tall(w)) { if (lastY != Integer.MIN_VALUE) Controls.scroll(w, py - lastY); lastY = py; break; }
					if (!Float.isNaN(dragDx)) Game.inputHandler.touchDragged(Math.round(px + dragDx), Math.round(py + dragDy), ID);
					break;
				}
				case 5: { // and ends
					Window w = Ui.topWindow();
					lastY = Integer.MIN_VALUE;
					if (w != null && Controls.tall(w)) break;
					if (!Float.isNaN(dragDx)) Game.inputHandler.touchUp(Math.round(px + dragDx), Math.round(py + dragDy), ID, 0);
					break;
				}
			}
			link.ack(seq);
		}
	}
}
