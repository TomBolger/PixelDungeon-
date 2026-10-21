package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.Statistics;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.potions.Potion;
import com.shatteredpixel.shatteredpixeldungeon.items.potions.PotionOfHealing;
import com.shatteredpixel.shatteredpixeldungeon.items.potions.PotionOfStrength;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfUpgrade;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import java.util.*;

/**
 * Where an honest bot game's health and food went (for the batch reports, sim/hstats.py): health lost between two
 * decisions, put down to what stood beside the hero (else a burning / poison / bleeding buff, starving, the nearest
 * enemy in sight, or "other"); turns spent resting and starving; what was left unused at the end. One "STATS" line.
 */
public final class BotStats {
	private BotStats() { }

	static final Map<String, Integer> lost = new TreeMap<>();
	static int lastHP = -1, healed, restTurns, starveTurns, decisions;
	static float lastT = -1; static String lastSrc = "other"; static boolean lastRest;
	static final int[] lostAt = new int[27], turnsAt = new int[27];

	static void newGame() { lost.clear(); lastHP = -1; healed = 0; restTurns = 0; starveTurns = 0; lastT = -1; Arrays.fill(lostAt, 0); Arrays.fill(turnsAt, 0); }

	/** Each decision (the hero ready), with the decision about to be acted on. */
	static void decision(Assist.Target t) {
		Hero h = Dungeon.hero; Level L = Dungeon.level; if (h == null || L == null) return;
		float now = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now(); int d = Math.max(0, Math.min(26, Dungeon.depth));
		if (lastHP >= 0) {
			int dh = h.HP - lastHP; float dt = Math.max(0, now - lastT);
			if (dh < 0) { lost.merge(lastSrc, -dh, Integer::sum); lostAt[d] -= dh; } else healed += dh;
			if (lastRest && dt > 0) restTurns += (int) dt;
			Hunger hu = h.buff(Hunger.class); if (hu != null && hu.isStarving() && dt > 0) starveTurns += (int) dt;
			if (dt > 0) turnsAt[d] += (int) dt;
		}
		lastHP = h.HP; lastT = now;
		lastRest = t != null && t.kind == Assist.WAIT && Assist.REST_ACT.equals(t.action);
		lastSrc = source(h, L);
	}

	static String source(Hero h, Level L) {
		Mob near = null; int nd = 99;
		for (Mob m : L.mobs) {
			if (!m.isAlive() || m.alignment != Char.Alignment.ENEMY || !L.heroFOV[m.pos]) continue;
			int dd = L.distance(m.pos, h.pos);
			if (dd < nd) { nd = dd; near = m; }
		}
		if (near != null && nd <= 1) return near.getClass().getSimpleName();
		for (Buff b : h.buffs()) { String n = b.getClass().getSimpleName();
			if (n.equals("Burning") || n.equals("Poison") || n.equals("Bleeding") || n.equals("Ooze") || n.equals("Corrosion")) return n; }
		Hunger hu = h.buff(Hunger.class); if (hu != null && hu.isStarving()) return "Starving";
		if (near != null) return "far:" + near.getClass().getSimpleName();
		return "other";
	}

	static String line() {
		Hero h = Dungeon.hero; StringBuilder sb = new StringBuilder("STATS ");
		int tot = 0; for (int v : lost.values()) tot += v;
		sb.append("lost=").append(tot).append(";healed=").append(healed).append(";rest=").append(restTurns).append(";starve=").append(starveTurns).append(";");
		sb.append("by="); for (Map.Entry<String, Integer> e : lost.entrySet()) sb.append(e.getKey()).append(':').append(e.getValue()).append(','); sb.append(';');
		sb.append("lostAt="); for (int i = 1; i < 27; i++) if (lostAt[i] > 0 || turnsAt[i] > 0) sb.append(i).append(':').append(lostAt[i]).append('/').append(turnsAt[i]).append(','); sb.append(';');
		if (h != null) {
			int heal = 0, sou = 0, pos = 0, unkP = 0, unkS = 0;
			for (Item i : h.belongings) {
				if (i instanceof PotionOfHealing && ((Potion) i).isKnown()) heal += i.quantity();
				else if (i instanceof ScrollOfUpgrade && ((Scroll) i).isKnown()) sou += i.quantity();
				else if (i instanceof PotionOfStrength && ((Potion) i).isKnown()) pos += i.quantity();
				else if (i instanceof Potion && !((Potion) i).isKnown()) unkP += i.quantity();
				else if (i instanceof Scroll && !((Scroll) i).isKnown()) unkS += i.quantity();
			}
			sb.append("left=heal:").append(heal).append(",sou:").append(sou).append(",pos:").append(pos).append(",unkP:").append(unkP).append(",unkS:").append(unkS).append(';');
			sb.append("str=").append(h.STR()).append(";lvl=").append(h.lvl).append(";sub=").append(h.subClass.name()).append(';');
			sb.append("arm=").append(h.belongings.armor() == null ? "-" : h.belongings.armor().getClass().getSimpleName() + "+" + h.belongings.armor().buffedLvl()).append(';');
			sb.append("wpn=").append(h.belongings.weapon() == null ? "-" : h.belongings.weapon().getClass().getSimpleName() + "+" + h.belongings.weapon().buffedLvl()).append(';');
			int tp = 0; for (int tier = 1; tier <= h.talents.size(); tier++) tp += h.talentPointsAvailable(tier);
			sb.append("talentsUnspent=").append(tp).append(';');
		}
		return sb.toString();
	}
}
