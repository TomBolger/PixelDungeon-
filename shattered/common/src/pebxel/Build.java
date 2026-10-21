package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroSubClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent;
import java.util.*;

import static com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent.*;

/**
 * The build a good player would pick (docs/strategy-reference.md section 3, the "bot-pick" columns, leaning to talents
 * that work without being used by hand): the subclass for Tengu's mask, and the order to spend talent points in.
 * Player choices, so no hidden information. The bot spends points with {@link #spendTalents}; the watch's assistant can
 * offer {@link #nextTalent} the same way. Env SUBCLASS=0/1 (tests) forces the class's first or second subclass.
 */
public final class Build {
	private Build() { }

	/** Each class's talents, most wanted first, per tier (tier 3 includes both subclasses' own: only those the hero has count). */
	static final Map<HeroClass, Talent[][]> ORDER = new EnumMap<>(HeroClass.class);
	static {
		ORDER.put(HeroClass.WARRIOR, new Talent[][]{
				{IRON_WILL, HEARTY_MEAL, IRON_WILL, HEARTY_MEAL, PROVOKED_ANGER, VETERANS_INTUITION},
				{LETHAL_MOMENTUM, IRON_STOMACH, LETHAL_MOMENTUM, IRON_STOMACH, LIQUID_WILLPOWER, LIQUID_WILLPOWER, RUNIC_TRANSFERENCE, IMPROVISED_PROJECTILES},
				{STRONGMAN, DEATHLESS_FURY, CLEAVE, STRONGMAN, DEATHLESS_FURY, CLEAVE, STRONGMAN, DEATHLESS_FURY, CLEAVE, ENDLESS_RAGE, ENHANCED_COMBO, HOLD_FAST, LETHAL_DEFENSE, ENRAGED_CATALYST},
				{HEROIC_ENERGY}});
		ORDER.put(HeroClass.MAGE, new Talent[][]{
				{BACKUP_BARRIER, SCHOLARS_INTUITION, BACKUP_BARRIER, LINGERING_MAGIC, EMPOWERING_MEAL, SCHOLARS_INTUITION},
				{ENERGIZING_MEAL, ENERGIZING_MEAL, WAND_PRESERVATION, ARCANE_VISION, INSCRIBED_POWER, SHIELD_BATTERY},
				{SOUL_EATER, EMPOWERED_STRIKE, DESPERATE_POWER, SOUL_SIPHON, EXCESS_CHARGE, SOUL_EATER, EMPOWERED_STRIKE, DESPERATE_POWER, MYSTICAL_CHARGE, NECROMANCERS_MINIONS, ALLY_WARP},
				{HEROIC_ENERGY}});
		ORDER.put(HeroClass.ROGUE, new Talent[][]{
				{CACHED_RATIONS, PROTECTIVE_SHADOWS, CACHED_RATIONS, PROTECTIVE_SHADOWS, SUCKER_PUNCH, THIEFS_INTUITION},
				{MYSTICAL_MEAL, SILENT_STEPS, MYSTICAL_MEAL, SILENT_STEPS, WIDE_SEARCH, INSCRIBED_STEALTH, ROGUES_FORESIGHT},
				{ENHANCED_LETHALITY, EVASIVE_ARMOR, LIGHT_CLOAK, ENHANCED_LETHALITY, EVASIVE_ARMOR, LIGHT_CLOAK, ENHANCED_LETHALITY, EVASIVE_ARMOR, ASSASSINS_REACH, SPEEDY_STEALTH, ENHANCED_RINGS, BOUNTY_HUNTER, PROJECTILE_MOMENTUM},
				{HEROIC_ENERGY}});
		ORDER.put(HeroClass.HUNTRESS, new Talent[][]{
				{NATURES_BOUNTY, FOLLOWUP_STRIKE, NATURES_BOUNTY, FOLLOWUP_STRIKE, NATURES_AID, SURVIVALISTS_INTUITION},
				{HEIGHTENED_SENSES, DURABLE_PROJECTILES, HEIGHTENED_SENSES, DURABLE_PROJECTILES, INVIGORATING_MEAL, REJUVENATING_STEPS, LIQUID_NATURE},
				{POINT_BLANK, SHARED_UPGRADES, BARKSKIN, POINT_BLANK, SHARED_UPGRADES, BARKSKIN, POINT_BLANK, SHARED_UPGRADES, SHIELDING_DEW, FARSIGHT, SEER_SHOT, DURABLE_TIPS, SHARED_ENCHANTMENT},
				{HEROIC_ENERGY}});
		ORDER.put(HeroClass.DUELIST, new Talent[][]{
				{PATIENT_STRIKE, AGGRESSIVE_BARRIER, PATIENT_STRIKE, STRENGTHENING_MEAL, AGGRESSIVE_BARRIER, ADVENTURERS_INTUITION},
				{FOCUSED_MEAL, LETHAL_HASTE, FOCUSED_MEAL, LIQUID_AGILITY, LIQUID_AGILITY, LETHAL_HASTE, WEAPON_RECHARGING, SWIFT_EQUIP},
				{PRECISE_ASSAULT, TWIN_UPGRADES, UNENCUMBERED_SPIRIT, PRECISE_ASSAULT, TWIN_UPGRADES, MONASTIC_VIGOR, TWIN_UPGRADES, VARIED_CHARGE, COMBINED_LETHALITY, COMBINED_ENERGY, DEADLY_FOLLOWUP},
				{HEROIC_ENERGY}});
		ORDER.put(HeroClass.CLERIC, new Talent[][]{
				{SATIATED_SPELLS, HOLY_INTUITION, SEARING_LIGHT, SHIELD_OF_LIGHT, SATIATED_SPELLS, SEARING_LIGHT},
				{ENLIGHTENING_MEAL, ENLIGHTENING_MEAL, BLESS, SUNRAY, DIVINE_SENSE, RECALL_INSCRIPTION},
				{LAY_ON_HANDS, HOLY_LANCE, CLEANSE, LAY_ON_HANDS, AURA_OF_PROTECTION, HALLOWED_GROUND, LAY_ON_HANDS, CLEANSE, WALL_OF_LIGHT, MNEMONIC_PRAYER, LIGHT_READING},
				{HEROIC_ENERGY}});
	}

	/** Tengu's mask: the subclass to take (the guide's default, or SUBCLASS=0/1). */
	public static HeroSubClass subclass(HeroClass c) {
		HeroSubClass[] s = c.subClasses();
		String f = null; try { f = System.getenv("SUBCLASS"); } catch (Throwable e) { }
		if (f != null && (f.equals("0") || f.equals("1"))) return s[Integer.parseInt(f)];
		switch (c) {
			case WARRIOR: return HeroSubClass.BERSERKER;
			case MAGE: return HeroSubClass.WARLOCK;
			case ROGUE: return HeroSubClass.ASSASSIN;
			case HUNTRESS: return HeroSubClass.SNIPER;
			case DUELIST: return HeroSubClass.CHAMPION;
			default: return HeroSubClass.PALADIN;
		}
	}

	/** The next talent point to spend, or null (none to spend, or none left to raise). */
	public static Talent nextTalent(Hero h) {
		Talent[][] ord = ORDER.get(h.heroClass);
		for (int tier = 1; tier <= Math.min(Talent.MAX_TALENT_TIERS, h.talents.size()); tier++) {
			if (h.talentPointsAvailable(tier) <= 0) continue;
			LinkedHashMap<Talent, Integer> have = h.talents.get(tier - 1);
			Map<Talent, Integer> want = new HashMap<>();
			if (ord != null && tier <= ord.length) for (Talent t : ord[tier - 1]) {
				want.merge(t, 1, Integer::sum);
				Integer p = have.get(t);
				if (p != null && p < t.maxPoints() && p < want.get(t)) return t;
			}
			for (Map.Entry<Talent, Integer> e : have.entrySet()) if (e.getValue() < e.getKey().maxPoints()) return e.getKey();
		}
		return null;
	}

	/** Every point there is to spend, spent. Returns how many. */
	public static int spendTalents(Hero h) {
		int n = 0;
		for (Talent t; n < 40 && (t = nextTalent(h)) != null; n++) h.upgradeTalent(t);
		return n;
	}
}
