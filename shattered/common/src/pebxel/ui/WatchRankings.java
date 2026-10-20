package pebxel.ui;

import com.shatteredpixel.shatteredpixeldungeon.Rankings;
import com.shatteredpixel.shatteredpixeldungeon.scenes.RankingsScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.RenderedTextBlock;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.ui.Component;
import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.List;

/**
 * Shattered's rankings, its rows laid out for the watch's bigger text (square screen): rows the full width under the
 * title, each as tall as its text needs (a cause that wraps gets two lines), and as many of the best as fit, as
 * Pebble PD showed its top rows. Shattered's own layout squeezed 11 rows in, so each "Slain by" wrapped onto the row
 * below and the first row ran into the title.
 */
public class WatchRankings extends RankingsScene {
	static final int GAP = 4, TOP = 22;

	@Override
	public void create() {
		super.create();
		if (pebxel.Screen.round) return;
		try {
			List<Component> rows = new ArrayList<>();
			for (Gizmo g : GroupAccess.members(this)) if (g != null && g.getClass().getSimpleName().equals("Record")) rows.add((Component) g);
			if (rows.isEmpty()) return;
			Field df = rows.get(0).getClass().getDeclaredField("desc"); df.setAccessible(true);
			int w = Camera.main.width, h = Camera.main.height;
			int bottom = Rankings.INSTANCE.totalNumber >= Rankings.TABLE_SIZE ? 18 : 2;   // (the games-played line)
			float need = 18;
			for (Component r : rows) {
				r.setRect(GAP, 0, w - 2 * GAP, 18);
				need = Math.max(need, ((RenderedTextBlock) df.get(r)).height() + 3);
			}
			need = (float) Math.ceil(need);
			int fit = Math.max(1, (int) ((h - TOP - bottom) / need));
			for (int i = 0; i < rows.size(); i++) {
				Component r = rows.get(i);
				if (i >= fit) { r.killAndErase(); continue; }
				r.setRect(GAP, TOP + i * need, w - 2 * GAP, need);
			}
		} catch (Exception e) { e.printStackTrace(); }
	}
}
