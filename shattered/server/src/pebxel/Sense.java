package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.keys.*;
import com.shatteredpixel.shatteredpixeldungeon.journal.Notes;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.Terrain;
import java.util.*;

/**
 * Is plan A sensible (AssistBot SENSE=1)? A plain rule, from what the hero knows only, for turns with no enemy in sight:
 * - "stairs": the way down offered while a reachable spot beside the unknown, a seen key, or a door a held key opens is left;
 * - "key": a seen key lying about passed for something further away;
 * - "door": a held key's door passed for something further away;
 * - "far": a walk to explore 8+ steps and twice as far as the nearest spot beside the unknown.
 * Prints "SENSE graded N bad B (stairs s key k door d far f)" at the end; SENSEV=1 prints each bad one.
 */
final class Sense {
	private Sense() { }
	static final boolean ON = System.getenv("SENSE") != null, V = System.getenv("SENSEV") != null;
	static int graded, stairs, key, door, far;
	static final int INF = 1 << 20;

	static void grade(List<Assist.Target> plan) {
		if (!ON || plan.isEmpty()) return;
		Level L = Dungeon.level; int hp = Dungeon.hero.pos, W = L.width(), LEN = L.length();
		for (Mob m : L.mobs) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0 && m.isAlive()) return;
		Assist.Target a = plan.get(0);
		if (a.kind == Assist.USE || a.kind == Assist.WAIT || a.kind == Assist.ENEMY || a.why.startsWith("escape") || a.why.startsWith("tactic") || a.why.equals("your choice")) return;   // (the player's own choice is followed till met)
		boolean[] known = new boolean[LEN];
		for (int c = 0; c < LEN; c++) known[c] = L.visited[c] || L.mapped[c];
		boolean[] ap = (boolean[]) f("pass");
		int[] d = new int[LEN]; Arrays.fill(d, INF); int[] q = new int[LEN]; int qh = 0, qt = 0; d[hp] = 0; q[qt++] = hp;
		int[] n8 = { -W, 1, W, -1, -W - 1, -W + 1, W + 1, W - 1 };
		while (qh < qt) { int c = q[qh++];
			for (int k = 0; k < 8; k++) { int n = c + n8[k]; if (n < 0 || n >= LEN || Math.abs(n % W - c % W) > 1 || d[n] <= d[c] + 1) continue;
				if (!known[n] || !ap[n]) continue; d[n] = d[c] + 1; q[qt++] = n; } }
		// walk to a cell or beside it (a door, a heap on the stairs...)
		int[] w = d.clone();
		for (int c = 0; c < LEN; c++) if (d[c] < INF) for (int k = 0; k < 8; k++) { int n = c + n8[k]; if (n >= 0 && n < LEN && Math.abs(n % W - c % W) <= 1 && d[c] + 1 < w[n]) w[n] = d[c] + 1; }
		int F = INF, fc = -1;
		for (int c = 0; c < LEN; c++) {
			if (d[c] >= INF || d[c] == 0 || c == L.exit() || c == L.entrance()) continue;
			for (int k = 0; k < 8; k++) { int n = c + n8[k];
				if (n < 0 || n >= LEN || Math.abs(n % W - c % W) > 1 || known[n] || Dungeon.level.distance(n, hp) <= 1) continue;
				if (k >= 4) { int p = n % W + (c / W) * W, r = c % W + (n / W) * W; if (known[p] && !L.passable[p] && known[r] && !L.passable[r]) continue; }
				if (d[c] < F) { F = d[c]; fc = c; } break; }
		}
		int K = INF, kc = -1;
		for (Heap h : L.heaps.valueList()) {
			if (!h.seen || !known[h.pos] || w[h.pos] >= INF || h.type != Heap.Type.HEAP) continue;
			for (Item i : h.items) if (i instanceof Key && ((Key) i).depth == Dungeon.depth && w[h.pos] < K) { K = w[h.pos]; kc = h.pos; }
		}
		int D = INF;
		boolean iron = Notes.keyCount(new IronKey(Dungeon.depth)) > 0, crystal = Notes.keyCount(new CrystalKey(Dungeon.depth)) > 0;
		int dc = -1;
		for (int c = 0; c < LEN; c++) if (known[c] && w[c] < INF && ((L.map[c] == Terrain.LOCKED_DOOR && iron) || (L.map[c] == Terrain.CRYSTAL_DOOR && crystal)) && w[c] < D) { D = w[c]; dc = c; }
		int o = a.obj >= 0 ? a.obj : a.cell, P = o >= 0 && o < LEN ? w[o] : INF;
		graded++;
		String bad = null;
		if (o == L.exit() && (F < INF || K < INF || D < INF)) { stairs++; bad = "stairs"; }
		else if (K < INF && o != kc && P > K + 2) { key++; bad = "key"; }
		else if (D < INF && P > D + 2 && L.map[o] != Terrain.LOCKED_DOOR && L.map[o] != Terrain.CRYSTAL_DOOR) { door++; bad = "door"; }
		else if (a.kind == Assist.GO && F < INF && P >= 8 && P > 2 * F + 2) { far++; bad = "far"; }
		if (bad != null && V) System.out.println("  SENSE " + bad + " d" + Dungeon.depth + " t" + Math.round(com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now()) + " hero " + hp + ": " + a + " walk " + P + " frontier " + F + " key " + K + " door " + D + (plan.size() > 1 ? " | B " + plan.get(1) : "") + "\n  SENSEWHY frontier " + why(fc) + " door " + why(dc) + " key " + why(kc) + (kc >= 0 ? " heapOk " + h("heapOk", L.heaps.get(kc)) + " passed " + ((Set<?>) f("passed")).contains(kc) + " type " + L.heaps.get(kc).type + " items " + L.heaps.get(kc).items + " danger " + ((boolean[]) f("danger"))[kc] + " avoid " + L.avoid[kc] + " trap " + L.traps.get(kc) : "") + " state " + st());
	}

	static Object f(String n) { try { java.lang.reflect.Field x = Assist.class.getDeclaredField(n); x.setAccessible(true); return x.get(null); } catch (Exception e) { return e; } }
	static Object m(String n, int c) { try { java.lang.reflect.Method x = Assist.class.getDeclaredMethod(n, int.class); x.setAccessible(true); return x.invoke(null, c); } catch (Exception e) { return e; } }
	static Object h(String n, Heap c) { try { java.lang.reflect.Method x = Assist.class.getDeclaredMethod(n, Heap.class); x.setAccessible(true); return x.invoke(null, c); } catch (Exception e) { return e; } }
	static String why(int c) {
		if (c < 0) return "-";
		int[] dist = (int[]) f("dist"); boolean[] open = (boolean[]) f("open");
		return c + "(t" + Dungeon.level.map[c] + " dist " + dist[c] + " open " + open[c] + " unseenBy " + m("unseenBy", c) + " ahead " + m("ahead", c) + " direct " + m("direct", c) + " bad " + ((Map<?, ?>) f("bad")).containsKey(c) + " demoted " + Assist.demoted(c) + ")";
	}
	static String st() { return "od " + f("od") + " ovrC " + f("ovrC") + " ovrDir " + f("ovrDir") + " goal " + f("goal") + " wayOn " + f("wayOn") + " head " + f("head") + " fish " + f("fish"); }

	static String line() { int b = stairs + key + door + far; return "SENSE graded " + graded + " bad " + b + " (" + (graded == 0 ? 0 : 100 * b / graded) + "%: stairs " + stairs + " key " + key + " door " + door + " far " + far + ")"; }
}
