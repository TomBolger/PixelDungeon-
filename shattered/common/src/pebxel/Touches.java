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
	private Object upOwner;
	private float dragDx, dragDy;
	private int lastY = Integer.MIN_VALUE;
	private byte[] last;            // the last message (the watch sends one again when it heard nothing back)
	private boolean dragDown;       // an interface drag is pressed (its lift-off still to come)
	private int dragX, dragY;
	private Object dragOwner;
	private Window scrollWindow;
	private Object owner() {
		Window w = Ui.topWindow();
		return w instanceof pebxel.ui.WatchWindow ? ((pebxel.ui.WatchWindow) w).touchPage() : w != null ? w : Game.scene();
	}
	private void cancelDrag() {
		if (dragDown) Game.inputHandler.touchCancelled(dragX, dragY, ID, 0);
		dragDown = false; scrollWindow = null; dragOwner = null; lastY = Integer.MIN_VALUE;
	}
	private void endTap(boolean cancel) {
		if (upAt < 0) return;
		if (cancel || upOwner != owner()) Game.inputHandler.touchCancelled(upX, upY, ID, 0);
		else {
			Game.inputHandler.touchUp(upX, upY, ID, 0);
			if (upOwner instanceof GameScene && Ui.topWindow() == null && com.shatteredpixel.shatteredpixeldungeon.Dungeon.hero != null && !Controls.aiming()) { tapBefore = com.shatteredpixel.shatteredpixeldungeon.Dungeon.hero.curAction; tapWatch = true; }
		}
		upAt = -1; upOwner = null;
	}
	/** A tap on the map (Pebble PD's on_tap): once the game has taken it (the next frame), the walk it set off on tells
	 *  the Assistant the player's way, as a choice from its list does: its plan follows where the player tapped. */
	private boolean tapWatch; private Object tapBefore;
	private void tapTaken() {
		if (!tapWatch) return;
		tapWatch = false;
		com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero h = com.shatteredpixel.shatteredpixeldungeon.Dungeon.hero;
		com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroAction a = h == null ? null : h.curAction;
		if (a == null || a == tapBefore || a.dst < 0 || a instanceof com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroAction.Attack) return;   // (a blow is a fight, not a way)
		Assist.tapped(a.dst);
	}

	public synchronized void add(byte[] b) { if (b != null && b.length >= 12) in.add(b.clone()); }
	private synchronized byte[] poll() { return in.poll(); }

	/** Call on the game thread before each frame, with the time since the last call. */
	public void frame(Link link, Controls controls, float dt) {
		clock += dt;
		tapTaken();
		if (dragOwner != null && dragOwner != owner()) cancelDrag();
		if (upAt >= 0 && (clock >= upAt || upOwner != owner())) endTap(false);
		byte[] b;
		while ((b = poll()) != null) {
			int kind = b[0], seq = b[1] & 255, cam = b[2] & 255;
			boolean lng = (b[3] & 1) != 0;
			int x = (short) ((b[4] & 255) | (b[5] << 8)), y = (short) ((b[6] & 255) | (b[7] << 8));
			int stamp = (b[8] & 255) | (b[9] & 255) << 8 | (b[10] & 255) << 16 | (b[11] & 255) << 24;
			if (java.util.Arrays.equals(b, last)) { link.ack(seq); continue; }   // a repeat: done already
			last = b;
			// a new press while a drag's lift-off never came (lost on the way): let that one go first, without a click,
			// or Shattered takes the new press for more of the old drag
			if (kind == 1 || kind == 3) cancelDrag();
			controls.touched();
			int px = x * 2 + 1, py = y * 2 + 1;
			switch (kind) {
				case 1: { // a tap: press, and let go next frame (or after a long press)
					int[] p = link.remap(cam, stamp, px, py);
					if (p == null) break;   // the window it was aimed at has gone: don't guess
					endTap(false);
					Game.inputHandler.touchDown(p[0], p[1], ID, 0);
					upX = p[0]; upY = p[1];
					upOwner = owner();
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
					int[] p = link.remap(cam, stamp, px, py);
					if (p == null) { dragDx = Float.NaN; break; }
					endTap(true);
					dragOwner = owner(); lastY = py;
					if (w != null && Controls.tall(w)) { scrollWindow = w; break; }
					dragDx = p[0] - px; dragDy = p[1] - py;
					dragX = p[0]; dragY = p[1]; dragDown = true;
					Game.inputHandler.touchDown(p[0], p[1], ID, 0);
					break;
				}
				case 4: { // moves on
					if (dragOwner != owner()) { cancelDrag(); break; }
					if (scrollWindow != null) { Controls.scroll(scrollWindow, py - lastY); lastY = py; break; }
					if (!Float.isNaN(dragDx) && dragDown) { dragX = Math.round(px + dragDx); dragY = Math.round(py + dragDy); Game.inputHandler.touchDragged(dragX, dragY, ID); }
					break;
				}
				case 5: { // and ends
					if (dragOwner != owner()) { cancelDrag(); break; }
					if (!Float.isNaN(dragDx) && dragDown) Game.inputHandler.touchUp(Math.round(px + dragDx), Math.round(py + dragDy), ID, 0);
					dragDown = false; cancelDrag();
					break;
				}
			}
			link.ack(seq);
		}
	}
}
