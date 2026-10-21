package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.*;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.*;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import java.util.*;

/**
 * The cheat bot's hands for coverage: now and then, with nothing hunting it, it tries an action of an item kind it
 * hasn't used yet this game (drink, read, zap, throw, plant, equip, an artifact's own actions...), aimed at the
 * nearest enemy or a free cell in sight, and answers whatever item selector that opens. The point is that every
 * kind of item gets used at least some of the time (so we know the watch can), not that it's used well.
 */
public final class BotItems {
	private BotItems() { }

	/** Item kind -> actions done with it (this game). */
	static final Map<String, Set<String>> done = new TreeMap<>();
	static final Set<String> carried = new TreeSet<>();   // kinds held at some point
	static int tries;
	private static int lastTryTurn = -100;

	// never: dropping things, ending the game, things that only show information or would undo the run
	static final Set<String> SKIP = new HashSet<>(Arrays.asList("DROP", "END", "INFO", "UNEQUIP", "AC_INFO", "REMOVE", "CHOOSE", "SET", "RETURN"));

	static void newGame() { done.clear(); carried.clear(); tries = 0; lastTryTurn = -100; }

	static String kind(Item i) { String n = i.getClass().getName(); return n.substring(n.lastIndexOf('.') + 1).replace('$', '.'); }

	static void note(Item i, String action) { if (i != null) done.computeIfAbsent(kind(i), k -> new TreeSet<>()).add(action); }

	/** Everything carried (for the coverage count). */
	static void look() {
		Hero h = Dungeon.hero; if (h == null) return;
		for (Item i : h.belongings) carried.add(kind(i));
	}

	/** A use to try now, or null. Turn-limited (every 6 turns at most) and only with no enemy hunting in sight. */
	static Assist.Target pick() {
		Hero h = Dungeon.hero; Level L = Dungeon.level;
		if (h == null || L == null || !h.ready) return null;
		int turn = (int) Actor.now();
		if (turn - lastTryTurn < 6) return null;
		for (Mob m : L.mobs) if (m.alignment == Char.Alignment.ENEMY && L.heroFOV[m.pos] && m.state == m.HUNTING && L.distance(m.pos, h.pos) <= 2) return null;
		List<Item> all = new ArrayList<>();
		for (Item i : h.belongings) all.add(i);
		Collections.shuffle(all, new Random(Dungeon.seed * 7 + turn));
		for (Item i : all) {
			if (i instanceof Bag || i instanceof Amulet || i instanceof Gold || i instanceof com.shatteredpixel.shatteredpixeldungeon.items.keys.Key || i instanceof DwarfToken || i instanceof CorpseDust || i instanceof Embers || i instanceof Pickaxe) continue;
			List<String> acts;
			try { acts = new ArrayList<>(i.actions(h)); } catch (Throwable e) { continue; }
			Set<String> d = done.getOrDefault(kind(i), Collections.emptySet());
			String pickA = null;
			for (String a : acts) {
				if (SKIP.contains(a) || d.contains(a)) continue;
				if (a.equals(Item.AC_THROW) && !throwy(i) && !(i instanceof com.shatteredpixel.shatteredpixeldungeon.items.potions.Potion && d.size() > 0)) continue;   // (a potion is thrown after one's been drunk)
				pickA = a; break;
			}
			if (pickA == null) continue;
			lastTryTurn = turn; tries++;
			note(i, pickA);
			Assist.Target t = new Assist.Target(aimCell(h, L), Assist.USE, "try " + kind(i) + " " + pickA);
			t.item = i; t.action = pickA; t.obj = -1;
			return t;
		}
		return null;
	}

	private static boolean hasOther(List<String> acts, Set<String> d) {
		for (String a : acts) if (!SKIP.contains(a) && !a.equals(Item.AC_THROW) && !d.contains(a)) return true;
		return false;
	}

	static boolean throwy(Item i) {
		return i instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.MissileWeapon || i instanceof com.shatteredpixel.shatteredpixeldungeon.items.bombs.Bomb
				|| i instanceof com.shatteredpixel.shatteredpixeldungeon.items.stones.Runestone || i instanceof Honeypot;
	}

	/** Where an aimed use goes: the nearest enemy in sight, else a free cell in sight 2-4 steps away, else the hero. */
	static int aimCell(Hero h, Level L) {
		int best = -1, bd = 99;
		for (Mob m : L.mobs) if (m.alignment == Char.Alignment.ENEMY && L.heroFOV[m.pos] && m.invisible <= 0) { int d = L.distance(m.pos, h.pos); if (d < bd) { bd = d; best = m.pos; } }
		if (best >= 0) return best;
		for (int c = 0; c < L.length(); c++) {
			int d = L.distance(c, h.pos);
			if (d >= 2 && d <= 4 && L.heroFOV[c] && L.passable[c] && Actor.findChar(c) == null && L.heaps.get(c) == null) return c;
		}
		return h.pos;
	}

	/** The answer to an item selector: worn things first (upgrades, curses, enchants), then the backpack, then null. */
	static Item answerSelector(WndBag.ItemSelector sel) {
		Hero h = Dungeon.hero; if (h == null) return null;
		List<Item> worn = new ArrayList<>();
		if (h.belongings.weapon() != null) worn.add(h.belongings.weapon());
		if (h.belongings.armor() != null) worn.add(h.belongings.armor());
		if (h.belongings.artifact() != null) worn.add(h.belongings.artifact());
		if (h.belongings.ring() != null) worn.add(h.belongings.ring());
		if (h.belongings.misc() != null) worn.add(h.belongings.misc());
		try {
			for (Item i : worn) if (sel.itemSelectable(i)) return i;
			for (Item i : h.belongings.backpack) if (!(i instanceof Bag) && sel.itemSelectable(i)) return i;
			for (Item i : h.belongings) if (sel.itemSelectable(i)) return i;
		} catch (Throwable e) { }
		return null;
	}

	static String summary() {
		StringBuilder sb = new StringBuilder();
		for (Map.Entry<String, Set<String>> e : done.entrySet()) sb.append(e.getKey()).append(':').append(String.join("/", e.getValue())).append(' ');
		return sb.toString().trim();
	}
}
