package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.watabou.utils.PathFinder;
import java.util.*;

/**
 * The watch's Down list as it was before the Assistant drove it (main 500effa, Controls.walkTargets / aimTargets /
 * reach): enemies nearest first, items, the nearest unexplored edge, the stairs, friends. Kept for AssistBot's press
 * counts (OLDLIST=1: Controls.legacy), to compare the two lists on the same seeds. Not on the phone.
 */
final class OldTargets {
	private OldTargets() { }

	static void install() { Controls.legacy = aiming -> aiming ? aimTargets() : reach(walkTargets()); }

	static List<Integer> reach(List<Integer> ts) {
		int mode = WatchSettings.assist();
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		if (mode == 0) {
			// off: the enemies in sight, nothing else
			for (int c : ts) if (c >= 0 && com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(Controls.cellOf(c)) instanceof Mob && ((Mob) com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(Controls.cellOf(c))).alignment == Char.Alignment.ENEMY) out.add(c);
			return out;
		}
		if (mode == 2) return ts;
		// near: a goal off the screen becomes the farthest step towards it that's on it
		int[] dist = Controls.walk(L, h);
		for (int c : ts) {
			int t = c;
			if (Controls.shot(t)) { if (!out.contains(t)) out.add(t); continue; }   // (a shot: at an enemy in sight)
			if (t >= 0 && !Controls.onScreen(t)) {
				if (dist[t] <= 0) continue;
				int guard = 0;
				while (t != h && !Controls.onScreen(t) && guard++ < 400) {
					int next = -1;
					for (int k : PathFinder.NEIGHBOURS8) { int n = t + k; if (n >= 0 && n < dist.length && dist[n] > 0 && dist[n] == dist[t] - 1) { next = n; break; } }
					if (next < 0) break;
					t = next;
				}
				if (t == h || !Controls.onScreen(t)) continue;
			}
			if (!out.contains(t)) out.add(t);
		}
		return out;
	}
	static List<Integer> aimTargets() {
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		List<Mob> ms = new ArrayList<>();
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) ms.add(m);
		ms.sort(Comparator.comparingInt(m -> L.distance(h, m.pos)));
		for (Mob m : ms) out.add(m.pos);
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment != Char.Alignment.ENEMY && !out.contains(m.pos)) out.add(m.pos);
		for (Heap hp : L.heaps.valueList()) if (L.heroFOV[hp.pos] && !out.contains(hp.pos)) out.add(hp.pos);
		if (!out.contains(h)) out.add(h);
		return out;
	}
	static List<Integer> walkTargets() {
		Level L = Dungeon.level; int h = Dungeon.hero.pos;
		List<Integer> out = new ArrayList<>();
		// enemies in sight, nearest first
		List<Mob> ms = new ArrayList<>();
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) ms.add(m);
		ms.sort(Comparator.comparingInt(m -> L.distance(h, m.pos)));
		// the Huntress's bow: a shot at each one an arrow would reach (first when it's further than a step, after the
		// melee attack when it's beside the hero)
		boolean bow = Controls.bow() != null;
		for (Mob m : ms) {
			boolean reach = bow && new com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica(h, m.pos, com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica.PROJECTILE).collisionPos == m.pos;
			boolean near = L.adjacent(h, m.pos);
			if (reach && !near) out.add(m.pos | Controls.SHOT);
			out.add(m.pos);
			if (reach && near) out.add(m.pos | Controls.SHOT);
		}
		// an item under you, then known items by walking distance
		if (L.heaps.get(h) != null) out.add(h);
		int[] dist = Controls.walk(L, h);
		List<Integer> items = new ArrayList<>();
		for (Heap hp : L.heaps.valueList()) if (hp.pos != h && dist[hp.pos] > 0 && (L.visited[hp.pos] || L.heroFOV[hp.pos])) items.add(hp.pos);
		items.sort(Comparator.comparingInt(c -> dist[c]));
		out.addAll(items);
		// the Blacksmith's mine: the dark gold veins known, nearest first (Select digs one: the pickaxe walks there and
		// mines), a few; nothing offered them before, so on the buttons the quest couldn't be done
		boolean mine = L instanceof com.shatteredpixel.shatteredpixeldungeon.levels.MiningLevel;
		if (mine && Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.quest.Pickaxe.class) != null) {
			List<int[]> veins = new ArrayList<>();
			for (int c = 0; c < L.length(); c++) {
				if (L.map[c] != com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.WALL_DECO || !(L.visited[c] || L.mapped[c] || L.heroFOV[c])) continue;
				int best = 0;   // (reached from the nearest floor beside it)
				for (int k : PathFinder.NEIGHBOURS8) { int n = c + k; if (n >= 0 && n < dist.length && dist[n] > 0 && (best == 0 || dist[n] < best)) best = dist[n]; }
				if (best > 0) veins.add(new int[]{c, best});
			}
			veins.sort(Comparator.comparingInt(v -> v[1]));
			for (int i = 0; i < Math.min(3, veins.size()); i++) out.add(veins.get(i)[0]);
		}
		// the nearest edge of the unexplored, then the way on: down (the mine: the ladder up, its only way out)
		int edge = Explorer.nearestUnexplored(L, h, dist);
		if (edge >= 0) out.add(edge);
		// (with the Amulet the way is up, every floor to the surface: the stairs up instead)
		boolean up = mine || Dungeon.hero.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.Amulet.class) != null;
		int exit = up ? L.entrance() : L.exit();
		if (exit >= 0 && (L.visited[exit] || L.mapped[exit]) && !out.contains(exit)) out.add(exit);
		// friends (shopkeepers, quest givers) to talk to
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment != Char.Alignment.ENEMY && !out.contains(m.pos)) out.add(m.pos);
		if (out.isEmpty()) out.add(-2);   // nothing known: search
		return out;
	}
}
