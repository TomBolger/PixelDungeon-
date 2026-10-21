package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.Statistics;
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
	static Mob fightOn; static int fightHP, fightStill;
	static int floorDecisions, lastDepth2 = -1, lastFrom = -1, lastTo = -1, noMove;

	static int bossLow = Integer.MAX_VALUE;
	static long progress() {
		Hero h = Dungeon.hero; Level L = Dungeon.level; long v = 0;
		for (boolean b : L.visited) if (b) v++;
		int bh = 0; for (Mob m : L.mobs) if (m.isAlive() && Char.hasProp(m, Char.Property.BOSS)) bh += m.HP;
		if (bh > 0 && bh < bossLow) bossLow = bh;
		return v * 1_000_003L + h.exp * 7919L + h.lvl * 104729L + Statistics.enemiesSlain * 31L + (bossLow == Integer.MAX_VALUE ? 0 : bossLow) * 13L;
	}

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
		if (!on) return null;
		Hero h = Dungeon.hero; Level L = Dungeon.level;
		Mob near = null, boss = null;
		for (Mob m : L.mobs) {
			if (!m.isAlive() || m.alignment != Char.Alignment.ENEMY) continue;
			if (L.adjacent(m.pos, h.pos) && (!Char.hasProp(m, Char.Property.IMMOVABLE) || Char.hasProp(m, Char.Property.BOSS))) near = m;
			if (Char.hasProp(m, Char.Property.BOSS)) boss = m;
		}
		if (near != null) {
			// one that never gets hurt (the Ghost's crab parries every blow from one it sees): a moment unseen (the cheat)
			if (near == fightOn && near.HP >= fightHP) { if (++fightStill >= 15) { com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff.affect(h, com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Invisibility.class, 3f); fightStill = 0; Coverage.problem("unhurt " + near.getClass().getSimpleName() + ": made the hero unseen"); } }
			else { fightOn = near; fightHP = near.HP; fightStill = 0; }
			return new Assist.Target(near.pos, Assist.ENEMY, "rescue: fight");
		}
		if (boss != null) return new Assist.Target(boss.pos, Assist.GO, "rescue: the boss");
		boolean up = h.belongings.getItem(Amulet.class) != null;
		LevelTransition tr = StairsTest.stairs(up);
		if (tr == null) return null;
		int c = tr.cell();
		if (c != h.pos && h.pos == lastFrom && c == lastTo && ++noMove >= 2) {   // no way there that the hero can walk: put it beside the stairs (the cheat)
			for (int d : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = c + d;
				if (n >= 0 && n < L.length() && L.passable[n] && com.shatteredpixel.shatteredpixeldungeon.actors.Actor.findChar(n) == null) {
					com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfTeleportation.appear(h, n); L.occupyCell(h); Dungeon.observe();
					Coverage.problem("no walk to the stairs on depth " + Dungeon.depth + ": put beside them"); break; } }
			noMove = 0;
		} else if (h.pos != lastFrom || c != lastTo) noMove = 0;
		lastFrom = h.pos; lastTo = c;
		return new Assist.Target(c, c == h.pos ? Assist.PICKUP : Assist.GO, up ? "rescue: up" : "rescue: down");
	}
}
