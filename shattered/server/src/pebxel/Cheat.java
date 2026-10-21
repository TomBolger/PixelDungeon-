package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import java.util.*;

/**
 * CHEAT=1: a smoke test of the whole game, not a way to play it. The hero can't really be hurt (thousands of HP,
 * topped up between turns), is strong and a few levels ahead of the floor, knows every item it carries (in games with an even seed), never
 * starves and has gold to spend; everything else is the game as shipped. Results made this way never count as the
 * bot getting better.
 */
public final class Cheat {
	private Cheat() { }

	public static final boolean ON = "1".equals(System.getenv("CHEAT"));
	static int levelsGiven, healed, upgrades;

	/** Between turns (the actor thread waiting). */
	public static void apply() {
		Hero h = Dungeon.hero;
		if (!ON || h == null || !h.isAlive()) return;
		if (h.HTBoost < 5000) { h.HTBoost = 5000; h.updateHT(true); }
		if (h.HP < h.HT) { h.HP = h.HT; healed++; }   // (always full: resting to full on thousands of HP took for ever)
		if (h.STR < 22) h.STR = 22;
		int want = Math.min(30, Dungeon.depth + 4);
		for (int k = 0; k < 30 && h.lvl < want; k++) { h.earnExp(h.maxExp() - h.exp, Cheat.class); levelsGiven++; }
		// (every other game: so Identify, Detect Magic and Intuition get used in the rest)
		if (Dungeon.seed % 2 == 0) for (Item i : h.belongings) if (!i.isIdentified()) i.identify(false);
		Hunger hu = h.buff(Hunger.class);
		if (hu != null && hu.hunger() >= Hunger.STARVING - 5) hu.satisfy(Hunger.STARVING);
		if (Dungeon.gold < 5000) Dungeon.gold = 5000;
		// gear that keeps up (as Scrolls of Upgrade would): the weapon and armour worn at +depth/3 at least
		Item wpn = h.belongings.weapon(), arm = h.belongings.armor();
		for (Item i : new Item[]{wpn, arm}) if (i != null && i.isUpgradable() && i.level() < Dungeon.depth / 3) { i.upgrade(); upgrades++; }
	}

	/** The talent points there are to spend, spent (each game its own picks, from the dungeon seed). Returns how many. */
	public static int spendTalents(Random r) {
		Hero h = Dungeon.hero; int n = 0;
		if (h == null) return 0;
		for (int tier = 1; tier <= Math.min(Talent.MAX_TALENT_TIERS, h.talents.size()); tier++) {
			for (int guard = 0; guard < 20 && h.talentPointsAvailable(tier) > 0; guard++) {
				List<Talent> can = new ArrayList<>();
				for (Map.Entry<Talent, Integer> e : h.talents.get(tier - 1).entrySet()) if (e.getValue() < e.getKey().maxPoints()) can.add(e.getKey());
				if (can.isEmpty()) break;
				h.upgradeTalent(can.get(r.nextInt(can.size()))); n++;
			}
		}
		return n;
	}
}
