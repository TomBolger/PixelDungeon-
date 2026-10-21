package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.Statistics;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Amulet;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition;

/**
 * CHEAT=1 only: when the Assistant goes round in circles on a floor (no new cell seen, nothing picked up, no kill,
 * no experience for STALL decisions), the floor's map is revealed and the hero heads for the way on (the stairs
 * down, or up with the Amulet), fighting only what stands beside it, and on a boss floor for the boss. The
 * Assistant's dithering is a finding of its own (counted in the report): this just lets the smoke test go on.
 */
public final class Rescue {
	private Rescue() { }

	static final int STALL = 150;
	static int floorsRescued, lastDepth = -1, still;
	static long lastProgress = Long.MIN_VALUE;
	static boolean on;
	static Mob fightOn; static int fightHP, fightStill, upPos = -1; static float upNow = -1;
	static int floorDecisions, lastDepth2 = -1, lastFrom = -1, lastTo = -1, noMove;

	static int bossLow = Integer.MAX_VALUE;
	static long progress() {
		Hero h = Dungeon.hero; Level L = Dungeon.level; long v = 0;
		for (boolean b : L.visited) if (b) v++;
		int bh = 0; for (Mob m : L.mobs) if (m.isAlive() && Char.hasProp(m, Char.Property.BOSS)) bh += m.HP;
		if (bh > 0 && bh < bossLow) bossLow = bh;
		return v * 1_000_003L + h.exp * 7919L + h.lvl * 104729L + Statistics.enemiesSlain * 31L + (bossLow == Integer.MAX_VALUE ? 0 : bossLow) * 13L;
	}

	/** Walk to c; when the hero hasn't moved for two tries at it (no way it can walk), put it beside c (the cheat). */
	static Assist.Target walkOrPut(Hero h, Level L, int c, String why) {
		if (h.pos == lastFrom && c == lastTo && ++noMove >= 2) {
			for (int d : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = c + d;
				if (n >= 0 && n < L.length() && L.passable[n] && com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(n) == null) {
					com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfTeleportation.appear(h, n); L.occupyCell(h); Dungeon.observe();
					Coverage.problem("no walk (" + why + ") on depth " + Dungeon.depth + ": put there"); break; } }
			noMove = 0;
		} else if (h.pos != lastFrom || c != lastTo) noMove = 0;
		lastFrom = h.pos; lastTo = c;
		return new Assist.Target(c, Assist.GO, why);
	}

	/** The nearest cell (walking) at least d from cell far, or -1. */
	static int farFrom(Level L, int from, int far, int d) {
		int[] seen = new int[L.length()]; java.util.ArrayDeque<Integer> q = new java.util.ArrayDeque<>(); q.add(from); seen[from] = 1;
		while (!q.isEmpty()) { int c = q.poll(); if (L.distance(c, far) >= d) return c;
			for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = c + k; if (n < 0 || n >= L.length() || seen[n] != 0 || !L.passable[n] || !(L.visited[n] || L.mapped[n])) continue; seen[n] = 1; q.add(n); } }
		return -1;
	}

	/** The Assistant went round in circles (the bot's loop check): rescue from now on, this floor. */
	static void force() { on = true; floorsRescued++; }

	/** Every decision (cheat games): null, or what to do instead of the Assistant's plan. */
	static Assist.Target check() {
		if (Dungeon.depth != lastDepth) { lastDepth = Dungeon.depth; still = 0; on = false; lastProgress = Long.MIN_VALUE; bossLow = Integer.MAX_VALUE; }
		long p = progress();
		if (p != lastProgress) { lastProgress = p; if (!on) still = 0; } else still++;
		floorDecisions = Dungeon.depth != lastDepth2 ? 0 : floorDecisions + 1; lastDepth2 = Dungeon.depth;
		if (!on && (still >= STALL || floorDecisions > 2500)) {
			on = true; floorsRescued++;
			Coverage.problem("assistant stalled on depth " + Dungeon.depth);
			Level L = Dungeon.level;
			for (int c = 0; c < L.length(); c++) if (L.discoverable[c]) L.mapped[c] = true;   // (the cheat: the map, as Magic Mapping shows it)
			com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.updateFog();
		}
		Hero h = Dungeon.hero; Level L = Dungeon.level;
		boolean up = h.belongings.getItem(Amulet.class) != null;
		if (up && !on && Dungeon.depth < 26) on = true;   // the climb back with the Amulet (enemies grow ever stronger): straight for the stairs
		if (!on) return null;
		Mob near = null, boss = null, other = null; int od = 999; boolean anyInv = false;
		// (a boss floor's boss: the mine's quest bosses are optional, and sleep in the rock)
		if (Dungeon.bossLevel()) for (Mob m : L.mobs) if (m.isAlive() && m.alignment == Char.Alignment.ENEMY && Char.hasProp(m, Char.Property.BOSS)) boss = m;
		// a boss that can't be hurt now (DM-300 charged by a pylon, Yog with fists, the Dwarf King on his throne): the others first
		boolean bossOff = boss != null && boss.isInvulnerable(h.getClass());
		for (Mob m : L.mobs) {
			if (!m.isAlive() || m.alignment != Char.Alignment.ENEMY) continue;
			boolean pylon = m instanceof com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Pylon;
			if (m == boss && bossOff) continue;
			boolean inv = m.isInvulnerable(h.getClass());
			if (inv) anyInv = true;
			if (L.adjacent(m.pos, h.pos) && !inv && (!Char.hasProp(m, Char.Property.IMMOVABLE) || Char.hasProp(m, Char.Property.BOSS) || pylon) && (near == null || m.HP < near.HP)) near = m;
			if (bossOff && m != boss && (pylon || !Char.hasProp(m, Char.Property.IMMOVABLE))) { int d = L.distance(m.pos, h.pos) - (pylon ? 50 : 0); if (d < od) { od = d; other = m; } }
		}
		// Yog's fists can't be hurt near him: lead them away (the nearest cell 8 or more from him)
		if (near == null && anyInv && boss != null && boss.isInvulnerable(h.getClass())) {
			int c = farFrom(L, h.pos, boss.pos, 8);
			if (c >= 0 && c != h.pos) return walkOrPut(h, L, c, "rescue: lead them away from the boss");
		}
		if (near == null && other != null) return walkOrPut(h, L, other.pos, "rescue: the boss can't be hurt: " + other.getClass().getSimpleName());
		// climbing: no fights, unless the last step went nowhere (webbed, rooted, blocked)
		boolean stuckUp = up && h.pos == upPos && Actor.now() == upNow;
		upPos = h.pos; upNow = Actor.now();
		if (near != null && (!up || stuckUp || h.rooted)) {
			if (h.buff(com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Charm.class) != null) { com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff.detach(h, com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Charm.class); Coverage.problem("charmed: charm taken off"); }
			// one that never gets hurt (the Ghost's crab parries every blow from one it sees): a moment unseen (the cheat)
			if (near == fightOn && near.HP >= fightHP) { if (++fightStill >= 15) { com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff.affect(h, com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Invisibility.class, 3f); fightStill = 0; Coverage.problem("unhurt " + near.getClass().getSimpleName() + ": made the hero unseen"); } }
			else { fightOn = near; fightHP = near.HP; fightStill = 0; }
			return new Assist.Target(near.pos, Assist.ENEMY, "rescue: fight");
		}
		if (boss != null) return walkOrPut(h, L, boss.pos, "rescue: the boss");
		LevelTransition tr = StairsTest.stairs(up);
		if (tr == null) return null;
		int c = tr.cell();
		if (c == h.pos) return new Assist.Target(c, Assist.PICKUP, up ? "rescue: up" : "rescue: down");
		return walkOrPut(h, L, c, up ? "rescue: up" : "rescue: down");
	}
}
