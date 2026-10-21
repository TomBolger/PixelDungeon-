package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.ui.Button;
import com.shatteredpixel.shatteredpixeldungeon.ui.RenderedTextBlock;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.Group;
import com.watabou.noosa.GroupAccess;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/** Reads any SPD window as plain parts: text and pressable buttons. Pressing calls the button's own code. */
public final class Ui {
	public static final class Part {
		public final Gizmo g; public final String text; public final boolean button;
		Part(Gizmo g, String t, boolean b) { this.g = g; text = t; button = b; }
	}

	public static Window topWindow() {
		Group s = Game.scene(); if (s == null) return null;
		Window w = null;
		for (Gizmo g : GroupAccess.members(s)) {
			if (g instanceof Window && g.exists) w = (Window) g;
		}
		return w;
	}

	public static List<Part> parts(Group root) {
		List<Part> out = new ArrayList<>(); walk(root, out); return out;
	}

	private static void walk(Group grp, List<Part> out) {
		for (Gizmo g : GroupAccess.members(grp)) {
			if (g == null || !g.exists || !g.visible) continue;
			if (g instanceof Button) {
				if (g.active) out.add(new Part(g, label((Group) g), true));
				continue;
			}
			if (g instanceof RenderedTextBlock) { String t = ((RenderedTextBlock) g).text(); if (t != null && !t.isEmpty()) out.add(new Part(g, t, false)); continue; }
			if (g instanceof pebxel.ui.WatchText) { String t = ((pebxel.ui.WatchText) g).text(); if (t != null && !t.isEmpty()) out.add(new Part(g, t, false)); continue; }
			if (g instanceof Group) walk((Group) g, out);
		}
	}

	public static String label(Group b) {
		if (b instanceof pebxel.ui.WatchButton && !((pebxel.ui.WatchButton) b).text().isEmpty()) return ((pebxel.ui.WatchButton) b).text();
		StringBuilder sb = new StringBuilder(); collect(b, sb);
		return sb.length() == 0 ? b.getClass().getSimpleName() : sb.toString().trim();
	}

	private static void collect(Group grp, StringBuilder sb) {
		for (Gizmo g : GroupAccess.members(grp)) {
			if (g == null || !g.visible) continue;
			if (g instanceof RenderedTextBlock) sb.append(((RenderedTextBlock) g).text()).append(' ');
			else if (g instanceof pebxel.ui.WatchText) sb.append(((pebxel.ui.WatchText) g).text()).append(' ');
			else if (g instanceof Group) collect((Group) g, sb);
		}
	}

	public static void press(Gizmo button) { call(button, "onClick"); }
	public static void hold(Gizmo button) { call(button, "onLongClick"); }

	private static void call(Gizmo o, String name) {
		for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass()) {
			try { Method m = c.getDeclaredMethod(name); m.setAccessible(true); m.invoke(o); return; }
			catch (NoSuchMethodException e) { }
			catch (Exception e) { throw new RuntimeException(e); }
		}
	}
}
