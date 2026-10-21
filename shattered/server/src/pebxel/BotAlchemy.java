package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.Recipe;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.Terrain;
import com.watabou.noosa.Gizmo;
import java.util.*;

/**
 * Cheat games: the alchemy pot, used. On a floor with a pot the hero holds something to brew with, it goes to the
 * pot; in the alchemy screen it picks a recipe from what it carries (an output kind not made yet this game first),
 * puts the ingredients in (an input slot, then the item in the selector that opens), and presses the recipe's
 * button; then leaves. The steps are buttons, so the watch path does them with the watch's buttons too.
 */
public final class BotAlchemy {
	private BotAlchemy() { }

	static final Set<String> made = new TreeSet<>();
	static List<Item> plan; static int planDepth = -1, visits, lastVisitDepth = -1;
	static Item want; static Object lastScene;   // what the selector that's open should be answered with
	static String planOut; static int makes;

	static void newGame() { made.clear(); plan = null; want = null; visits = 0; lastVisitDepth = -1; }

	/** A recipe the pack can make now (ingredients, in order), or null. */
	static List<Item> findPlan() {
		Hero h = Dungeon.hero; List<Item> pool = new ArrayList<>();
		for (Item i : h.belongings.backpack) if (!(i instanceof Bag) && Recipe.usableInRecipe(i)) pool.add(i);
		for (Bag b : h.belongings.getBags()) if (b != h.belongings.backpack) for (Item i : b.items) if (Recipe.usableInRecipe(i)) pool.add(i);
		List<Item> best = null; boolean bestNew = false;
		int n = Math.min(pool.size(), 24);
		for (int a = 0; a < n; a++) for (int b = a; b < n + 1; b++) for (int c = b; c < n + 1; c++) {
			List<Item> pick = new ArrayList<>(); pick.add(pool.get(a));
			if (b < n) pick.add(pool.get(b)); if (c < n && b < n) pick.add(pool.get(c));
			if (b == n && c != n) continue;
			// (one unit each: the same stack twice needs two in it)
			Map<Item, Integer> need = new IdentityHashMap<>(); boolean ok = true;
			for (Item i : pick) { int k = need.merge(i, 1, Integer::sum); if (k > i.quantity() && !(i instanceof com.shatteredpixel.shatteredpixeldungeon.items.LiquidMetal)) ok = false; }
			if (!ok) continue;
			ArrayList<Item> ing = new ArrayList<>();
			for (Item i : pick) { Item d = i.duplicate(); if (d == null) { ok = false; break; } if (!(i instanceof com.shatteredpixel.shatteredpixeldungeon.items.LiquidMetal || i instanceof com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.MissileWeapon)) d.quantity(1); ing.add(d); }
			if (!ok) continue;
			ArrayList<Recipe> rs;
			try { rs = Recipe.findRecipes(ing); } catch (Throwable e) { continue; }
			if (rs.isEmpty()) continue;
			Recipe r = rs.get(0);
			int cost; Item out;
			try { cost = r.cost(ing); out = r.sampleOutput(ing); } catch (Throwable e) { continue; }
			if (out == null || cost > Dungeon.energy) continue;
			boolean nw = !made.contains(out.getClass().getSimpleName());
			if (best == null || (nw && !bestNew)) { best = pick; bestNew = nw; planOut = out.getClass().getSimpleName(); if (nw) return best; }
		}
		return best;
	}

	/** In the dungeon: go to the pot? (once a floor, while there's something to make) */
	static Assist.Target pick() {
		Hero h = Dungeon.hero; Level L = Dungeon.level;
		if (h == null || !h.ready || lastVisitDepth == Dungeon.depth) return null;
		int pot = -1;
		for (int c = 0; c < L.length(); c++) if (L.map[c] == Terrain.ALCHEMY && (L.visited[c] || L.mapped[c])) pot = c;
		if (pot < 0) return null;
		if (Dungeon.energy < 30) Dungeon.energy = 30;   // (the cheat: energy to spend)
		if (findPlan() == null) return null;
		return Rescue.walkOrPut(h, L, pot, "alchemy: to the pot");
	}

	/** In the alchemy screen: the button to press (or null: leave). */
	static BotWindows.Answer scene(com.watabou.noosa.Scene sc) {
		lastVisitDepth = Dungeon.depth;
		Object[] inputs = (Object[]) WatchDriver.getStatic(com.shatteredpixel.shatteredpixeldungeon.scenes.AlchemyScene.class, "inputs");
		Object[] combines = (Object[]) WatchDriver.getStatic(com.shatteredpixel.shatteredpixeldungeon.scenes.AlchemyScene.class, "combines");
		if (inputs == null || combines == null) return new BotWindows.Answer(null, null, "alchemy: can't see its slots");
		if (sc != lastScene) { lastScene = sc; makes = 0; plan = null; planDepth = -1; }
		if (plan == null && planDepth != Dungeon.depth) { plan = findPlan(); planDepth = Dungeon.depth; if (plan != null) visits++; }
		if (plan == null) return new BotWindows.Answer(null, null, "alchemy: nothing to make: leave");
		int filled = 0;
		for (Object in : inputs) if (in != null && WatchDriver.get(in, "item") != null) filled++;
		if (filled < plan.size()) {
			for (Object in : inputs) if (in != null && WatchDriver.get(in, "item") == null) {
				want = plan.get(filled);
				Object slot = WatchDriver.get(in, "slot");
				if (slot instanceof Gizmo) return new BotWindows.Answer((Gizmo) slot, null, "alchemy: put in " + BotItems.kind(want));
			}
		}
		Object b = WatchDriver.get(combines[0], "button");
		if (b instanceof com.shatteredpixel.shatteredpixeldungeon.ui.RedButton && ((com.shatteredpixel.shatteredpixeldungeon.ui.RedButton) b).active) {
			made.add(planOut); plan = null; planDepth = -2;   // (made: next time, a new plan from what's left)
			if (++makes >= 3) planDepth = Dungeon.depth;   // (three a visit at most)
			return new BotWindows.Answer((Gizmo) b, null, "alchemy: make " + planOut);
		}
		plan = null;
		return new BotWindows.Answer(null, null, "alchemy: no recipe after all: leave");
	}

	static String summary() { return String.join(",", made); }
}
