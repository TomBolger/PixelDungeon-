package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.RotHeart;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Wandmaker;
import com.shatteredpixel.shatteredpixeldungeon.items.Heap;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.CeremonialCandle;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.CorpseDust;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.Terrain;

/**
 * The quest steps the Assistant doesn't take by itself (cheat games, for coverage): the Wandmaker's three tasks.
 * The Ghost's it already does (talk, kill, talk), the Blacksmith's mine and the Imp's vault are left out for now
 * (the bot can't dig for gold or sneak yet). Only what the hero could see is used, as a player would.
 */
public final class BotQuests {
	private BotQuests() { }

	static int litAt = -1, litTurn;
	static int candlesThrown, heartsAttacked, barricadesLit, steps, stepsDepth;

	static int wandType() { Object v = Coverage.obj(Wandmaker.class, "type"); return v instanceof Integer ? (Integer) v : 0; }

	static boolean wandmakerHere() { for (Mob m : Dungeon.level.mobs) if (m instanceof Wandmaker) return true; return false; }

	/** A quest step to take now, or null. */
	static Assist.Target pick() {
		Hero h = Dungeon.hero; Level L = Dungeon.level;
		if (h == null || L == null || !h.ready || !Coverage.flag(Wandmaker.class, "given") || !wandmakerHere()) return null;
		if (Wandmaker.Quest.wand1 == null) return null;   // (done)
		if (Dungeon.depth != stepsDepth) { stepsDepth = Dungeon.depth; steps = 0; }
		if (++steps > 400) return null;   // (a step that goes nowhere: let the rescue have it)
		int type = wandType();
		boolean seen = false;
		for (Mob m : L.mobs) if (m.alignment == com.shatteredpixel.shatteredpixeldungeon.actors.Char.Alignment.ENEMY && L.heroFOV[m.pos] && L.adjacent(m.pos, h.pos) && !(m instanceof RotHeart)) seen = true;
		boolean carrying = h.belongings.getItem(CorpseDust.class) != null;   // (the dust's wraiths never stop coming: walk on through them)
		if (seen && !carrying) return null;   // (the fight beside it first)
		// the thing he wants, in hand: back to him (where he was last seen: he never moves)
		if (h.belongings.getItem(CorpseDust.class) != null || h.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.items.quest.Embers.class) != null
				|| (type == 3 && h.belongings.getItem(com.shatteredpixel.shatteredpixeldungeon.plants.Rotberry.Seed.class) != null)) {
			for (Mob m : L.mobs) if (m instanceof Wandmaker && (L.visited[m.pos] || L.heroFOV[m.pos])) {
				if (L.adjacent(m.pos, h.pos)) return new Assist.Target(m.pos, Assist.GO, "quest: back to the Wandmaker");
				return walk(h, L, m.pos, "quest: back to the Wandmaker");
			}
		}
		if (type == 2) return candles(h, L);
		if (type == 3) return heart(h, L);
		if (type == 1) return grave(h, L);
		return null;
	}

	/** A walk there (the hero stuck for two tries: put there, the cheat, as the rescue does). */
	static Assist.Target walk(Hero h, Level L, int c, String why) { return Rescue.walkOrPut(h, L, c, why); }

	/** The ritual: a candle thrown onto each of the four cells round the ritual spot. */
	static Assist.Target candles(Hero h, Level L) {
		CeremonialCandle c = h.belongings.getItem(CeremonialCandle.class);
		int rp = CeremonialCandle.ritualPos;
		if (c == null || rp <= 0 || rp >= L.length() || !(L.visited[rp] || L.mapped[rp])) return null;
		int[] around = {rp - L.width(), rp + 1, rp + L.width(), rp - 1};
		if (AssistBot.v) { StringBuilder sb = new StringBuilder("    ritual " + rp + ":"); for (int cell : around) { Heap hp = L.heaps.get(cell); sb.append(' ').append(cell).append(hp == null ? "-" : hp.type + "/" + hp.items.size() + "/" + hp.peek().getClass().getSimpleName()); } sb.append(" candles ").append(c.quantity()); System.out.println(sb); }
		for (int cell : around) {
			Heap hp = L.heaps.get(cell);
			boolean has = false;
			if (hp != null) for (Item i : hp.items) if (i instanceof CeremonialCandle) has = true;
			if (has) continue;
			boolean clear = L.heroFOV[cell] && cell != h.pos && Actor.findChar(cell) == null && L.distance(h.pos, cell) <= 3
					&& new com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica(h.pos, cell, com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica.PROJECTILE).collisionPos == cell;
			if (!clear) {   // beside it, off the ritual's cells, then throw
				int best = -1, bd = 1 << 30; int[] d = WatchDriver.stepsFrom(h.pos);
				for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = cell + k; boolean ritual = n == rp; for (int a : around) ritual |= a == n;
					if (n >= 0 && n < L.length() && !ritual && L.passable[n] && Actor.findChar(n) == null && d[n] < bd) { bd = d[n]; best = n; } }
				if (best >= 0 && best != h.pos && bd < Integer.MAX_VALUE) return walk(h, L, best, "quest: beside a ritual cell");
			}
			if (clear) {
				candlesThrown++;
				Assist.Target t = new Assist.Target(cell, Assist.USE, "quest: a candle for the ritual"); t.item = c; t.action = Item.AC_THROW; t.obj = -1; return t;
			}
			return walk(h, L, rp, "quest: to the ritual site");
		}
		return null;
	}

	/** The rot garden: the Rot Heart, struck (never burnt: that loses the seed). */
	static Assist.Target heart(Hero h, Level L) {
		for (Mob m : L.mobs) {
			if (!(m instanceof RotHeart) || !m.isAlive() || !(L.visited[m.pos] || L.heroFOV[m.pos])) continue;
			if (L.adjacent(m.pos, h.pos)) { heartsAttacked++; return new Assist.Target(m.pos, Assist.ENEMY, "quest: the rot heart"); }
			int best = -1, bd = 1 << 30; int[] d = WatchDriver.stepsFrom(h.pos);
			for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = m.pos + k; if (n >= 0 && n < L.length() && L.passable[n] && Actor.findChar(n) == null && d[n] < bd) { bd = d[n]; best = n; } }
			if (best >= 0 && bd < Integer.MAX_VALUE) return walk(h, L, best, "quest: to the rot heart");
		}
		return null;
	}

	/** The mass grave: behind a barricade (it burns): set it alight, a fire item if there is one, then the dust. */
	static Assist.Target grave(Hero h, Level L) {
		if (h.belongings.getItem(CorpseDust.class) != null) return null;   // (the Assistant takes it back)
		Heap dust = null;
		for (Heap hp : L.heaps.valueList()) for (Item i : hp.items) if (i instanceof CorpseDust) dust = hp;
		if (dust == null) return null;
		int[] d = WatchDriver.stepsFrom(h.pos);
		int bar = -1, bd = 1 << 30;
		for (int c = 0; c < L.length(); c++) if (L.map[c] == Terrain.BARRICADE && (L.visited[c] || L.mapped[c]) && L.distance(c, dust.pos) < bd) { bd = L.distance(c, dust.pos); bar = c; }
		if (bar < 0 || bd > 10) {   // the barricade's gone (burnt): in, for the dust
			if (!(L.visited[dust.pos] || L.mapped[dust.pos])) for (int c = 0; c < L.length(); c++) if (L.distance(c, dust.pos) <= 5 && L.discoverable[c]) L.mapped[c] = true;   // (the cheat: the room's map)
			return dust.pos == h.pos ? new Assist.Target(dust.pos, Assist.PICKUP, "quest: the corpse dust") : walk(h, L, dust.pos, "quest: the corpse dust");
		}
		if (!L.heroFOV[bar] || L.distance(h.pos, bar) > 4) {
			int best = -1, bb = 1 << 30;
			for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = bar + k; if (n >= 0 && n < L.length() && d[n] < bb && L.passable[n]) { bb = d[n]; best = n; } }
			return best >= 0 && best != h.pos ? new Assist.Target(best, Assist.GO, "quest: to the barricade") : null;
		}
		Item fire = null;
		for (Item i : h.belongings) if (i instanceof com.shatteredpixel.shatteredpixeldungeon.items.potions.PotionOfLiquidFlame || i instanceof com.shatteredpixel.shatteredpixeldungeon.plants.Firebloom.Seed
				|| i instanceof com.shatteredpixel.shatteredpixeldungeon.items.bombs.Firebomb) fire = i;
		barricadesLit++;
		if (fire != null && !L.adjacent(h.pos, bar)) {
			Assist.Target t = new Assist.Target(bar, Assist.USE, "quest: burn the barricade"); t.item = fire; t.action = Item.AC_THROW; t.obj = -1; return t;
		}
		// (no fire to hand: the cheat lights it, as a thrown Liquid Flame would)
		if (litAt != bar) { litAt = bar; litTurn = (int) Actor.now(); }
		if ((int) Actor.now() - litTurn > 20) { L.destroy(bar); com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.updateMap(bar); Coverage.problem("barricade didn't burn: taken away by the cheat"); return null; }
		int fc = -1; for (int k : com.watabou.utils.PathFinder.NEIGHBOURS4) { int n = bar + k; if (n >= 0 && n < L.length() && L.passable[n] && d[n] < Integer.MAX_VALUE && n != h.pos) fc = n; }
		if (fc >= 0) com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.add(com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Blob.seed(fc, 4, com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Fire.class));
		Coverage.problem("no fire for the barricade: lit by the cheat");
		Assist.Target t = new Assist.Target(h.pos, Assist.WAIT, "quest: wait for the fire"); t.action = Assist.WAIT_ACT; return t;
	}
}
