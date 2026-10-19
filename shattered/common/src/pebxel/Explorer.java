package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.watabou.utils.PathFinder;
import java.util.*;

/** A very simple player: fight what is next to you, pick things up, explore, search walls, take the stairs. */
public final class Explorer {
	static Set<Integer> searched = new HashSet<>();
	private static int depth = -1;

	/** The cell to act on, or -2 to search where you stand. */
	public static int choose(Set<Integer> bad) {
		if (Dungeon.depth != depth) { depth = Dungeon.depth; searched.clear(); }
		Level L = Dungeon.level; int h = Dungeon.hero.pos, W = L.width();
		for (Mob m : L.mobs)
			if (m.alignment == Char.Alignment.ENEMY && L.heroFOV[m.pos] && Dungeon.level.adjacent(h, m.pos)) return m.pos;
		boolean[] known = new boolean[L.length()];
		for (int i = 0; i < known.length; i++) known[i] = L.visited[i] || L.mapped[i];
		int[] dist = new int[L.length()]; Arrays.fill(dist, -1);
		ArrayDeque<Integer> q = new ArrayDeque<>(); q.add(h); dist[h] = 0;
		while (!q.isEmpty()) {
			int c = q.poll();
			if (c != h && !bad.contains(c)) {
				if (L.heaps.get(c) != null) return c;
				for (int d : PathFinder.NEIGHBOURS8) { int n = c + d; if (n >= 0 && n < known.length && !known[n]) return c; }
			}
			for (int d : PathFinder.NEIGHBOURS8) {
				int n = c + d;
				if (n < 0 || n >= known.length || dist[n] >= 0 || !known[n] || !(L.passable[n] || L.avoid[n])) continue;
				dist[n] = dist[c] + 1; q.add(n);
			}
		}
		if (L.visited[L.exit()] || L.mapped[L.exit()]) return L.exit();
		//nothing left to explore: walk the walls searching for hidden doors (SPD searches 1 tile around you)
		q.clear(); Arrays.fill(dist, -1); q.add(h); dist[h] = 0;
		while (!q.isEmpty()) {
			int c = q.poll();
			if (!searched.contains(c)) {
				boolean wall = false;
				for (int d : PathFinder.NEIGHBOURS8) { int n = c + d; if (n >= 0 && n < known.length && L.solid[n] && !L.passable[n]) wall = true; }
				if (wall) { if (c == h) { searched.add(c); return -2; } return c; }
			}
			for (int d : PathFinder.NEIGHBOURS8) {
				int n = c + d;
				if (n < 0 || n >= known.length || dist[n] >= 0 || !known[n] || !L.passable[n]) continue;
				dist[n] = dist[c] + 1; q.add(n);
			}
		}
		searched.clear();
		return -2;
	}
}
