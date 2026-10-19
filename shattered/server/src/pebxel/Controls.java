package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndGame;
import com.watabou.noosa.ColorBlock;
import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.Group;
import com.watabou.noosa.ui.Component;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;

/**
 * The watch's buttons, turned into Shattered PD actions on the phone. First version:
 *  - any window or menu screen: Up/Down move a highlight between its buttons, Select presses,
 *    hold Select for the long press, Back closes the window;
 *  - in the dungeon: Select takes one sensible step (explore, fight, pick up), Up searches,
 *    Down opens the backpack, Back opens the game menu.
 * The highlight is an ordinary picture added to the scene, so it reaches the watch like everything else.
 */
public class Controls {
	public static final int UP = 1, DOWN = 2, SELECT = 3, BACK = 4, LONG = 16;

	private Group focusOwner;
	private int focus;
	private final ColorBlock[] frame = new ColorBlock[4];

	public void button(int code) {
		boolean held = (code & LONG) != 0;
		int b = code & 15;
		Window w = Ui.topWindow();
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		if (menu != null) { menuButton(menu, b, held, w); return; }
		if (Dungeon.hero == null || !Dungeon.hero.isAlive()) return;
		switch (b) {
			case SELECT:
				if (!Dungeon.hero.ready) return;
				int t = Smoke.choose(new HashSet<>());
				if (t == -2) Dungeon.hero.search(true);
				else if (t >= 0 && Dungeon.hero.handle(t)) Dungeon.hero.next();
				break;
			case UP: if (Dungeon.hero.ready) Dungeon.hero.search(true); break;
			case DOWN: GameScene.show(new WndBag(Dungeon.hero.belongings.backpack)); break;
			case BACK: GameScene.show(new WndGame()); break;
		}
	}

	private void menuButton(Group menu, int b, boolean held, Window w) {
		List<Ui.Part> bs = buttons(menu);
		if (b == BACK) { if (w != null) w.onBackPressed(); else com.watabou.noosa.NoosaAccess.back(Game.scene()); return; }
		if (bs.isEmpty()) return;
		if (focusOwner != menu) { focusOwner = menu; focus = 0; }
		focus = Math.max(0, Math.min(bs.size() - 1, focus));
		if (b == UP) focus = (focus + bs.size() - 1) % bs.size();
		if (b == DOWN) focus = (focus + 1) % bs.size();
		if (b == SELECT) { if (held) Ui.hold(bs.get(focus).g); else Ui.press(bs.get(focus).g); }
	}

	/** Pressable things in reading order (top to bottom, then left to right). */
	private List<Ui.Part> buttons(Group g) {
		List<Ui.Part> out = new ArrayList<>();
		for (Ui.Part p : Ui.parts(g)) if (p.button && p.g instanceof Component) out.add(p);
		out.sort((a, c) -> {
			Component x = (Component) a.g, y = (Component) c.g;
			if (Math.abs(x.top() - y.top()) > 4) return Float.compare(x.top(), y.top());
			return Float.compare(x.left(), y.left());
		});
		return out;
	}

	/** Every frame: keep the highlight around the focused button. */
	public void frame() {
		Window w = Ui.topWindow();
		Group menu = w != null ? w : !(Game.scene() instanceof GameScene) ? Game.scene() : null;
		List<Ui.Part> bs = menu == null ? new ArrayList<>() : buttons(menu);
		if (menu == null || bs.isEmpty()) { hide(); return; }
		if (focusOwner != menu) { focusOwner = menu; focus = 0; }
		Component c = (Component) bs.get(Math.max(0, Math.min(bs.size() - 1, focus))).g;
		for (int i = 0; i < 4; i++) {
			if (frame[i] == null || frame[i].parent != menu) {
				if (frame[i] != null) frame[i].killAndErase();
				frame[i] = new ColorBlock(1, 1, 0xFFFFFF44);
				menu.add(frame[i]);
			}
			frame[i].visible = true;
		}
		// keep it on top
		for (ColorBlock f : frame) if (f.parent != null) f.parent.bringToFront(f);
		float x = c.left() - 1, y = c.top() - 1, wd = c.width() + 2, h = c.height() + 2;
		place(frame[0], x, y, wd, 1); place(frame[1], x, y + h - 1, wd, 1);
		place(frame[2], x, y, 1, h); place(frame[3], x + wd - 1, y, 1, h);
	}

	private static void place(ColorBlock b, float x, float y, float w, float h) { b.x = x; b.y = y; b.size(w, h); }

	private void hide() { for (ColorBlock f : frame) if (f != null) f.visible = false; }
}
