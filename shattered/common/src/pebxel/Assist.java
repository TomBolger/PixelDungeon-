package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.QuickSlot;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.blobs.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroAction;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.*;
import com.shatteredpixel.shatteredpixeldungeon.items.*;
import com.shatteredpixel.shatteredpixeldungeon.items.armor.Armor;
import com.shatteredpixel.shatteredpixeldungeon.items.armor.ClassArmor;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.items.food.Blandfruit;
import com.shatteredpixel.shatteredpixeldungeon.items.food.Food;
import com.shatteredpixel.shatteredpixeldungeon.items.food.MysteryMeat;
import com.shatteredpixel.shatteredpixeldungeon.items.keys.*;
import com.shatteredpixel.shatteredpixeldungeon.items.potions.*;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.CorpseDust;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.DwarfToken;
import com.shatteredpixel.shatteredpixeldungeon.items.quest.Embers;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.*;
import com.shatteredpixel.shatteredpixeldungeon.items.wands.*;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.SpiritBow;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.MagesStaff;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.MeleeWeapon;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.missiles.MissileWeapon;
import com.shatteredpixel.shatteredpixeldungeon.journal.Notes;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.levels.Terrain;
import com.shatteredpixel.shatteredpixeldungeon.mechanics.Ballistica;
import com.shatteredpixel.shatteredpixeldungeon.plants.Rotberry;
import com.shatteredpixel.shatteredpixeldungeon.plants.Sungrass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import java.util.*;
import java.util.function.IntPredicate;

/**
 * Pebble PD's Assistant (watch/src/ovl/tgt/tgt.c: tgt plan A, tgt2 plans B, C..., tgt3 the hands, tgt4 escape and
 * tactics) for Shattered PD: a ranked list of at most 8 targets; entry 0 ("plan A") is what a good player would click
 * next. It never acts by itself: the buttons pick an entry and {@link #act} does it as a tap or an item use would.
 *
 * Settings: {@link WatchSettings#assist()} (0 off: only enemies, Select attacks; 1 near: only on-screen targets, an
 * off-screen objective is offered at the farthest on-screen step of the walk there; 2 far: no screen limit) and
 * {@link WatchSettings#itemAssist()} (item / toolbar actions pinned in front).
 *
 * Use (once per turn, while the hero is ready): {@code list = plan(aiming, onScreen)}; show list.get(0) chosen; Down
 * cycles; on Select: {@code chose(list.get(i), list.get(0)); act(list.get(i));}.
 */
public final class Assist {
	private Assist() { }

	public static final int ENEMY = 1, PICKUP = 2, GO = 3, USE = 4, WAIT = 5;
	public static final String WAIT_ACT = "wait", SEARCH_ACT = "search", REST_ACT = "rest";
	public static final int CAP = 8;

	/** One entry of the list. */
	public static final class Target {
		/** The map cell (the marker's place; for an aimed item use, the cell aimed at), or -1. */
		public int cell = -1;
		/** ENEMY attack, PICKUP, GO walk, USE item, WAIT wait / search / rest (cell: where, or the enemy waited for). */
		public int kind;
		/** USE: the item, from the backpack or worn. */
		public Item item;
		/** USE: the Item action (Food.AC_EAT, Item.AC_THROW, Wand.AC_ZAP...); WAIT: WAIT_ACT, SEARCH_ACT or REST_ACT;
		 *  ENEMY: null (attack) or WAIT_ACT (doorway wait: let it step up first). */
		public String action;
		/** USE: what an inventory selector that opens should be answered with (the armour for an upgrade...), or null. */
		public Item on;
		/** The objective this entry stands for (an off-screen place: cell is a step towards it), -1 none. */
		public int obj = -1;
		/** USE: the quickslot holding the item (0..), or -1. */
		public int quickslot = -1;
		/** Why (for debugging). */
		public String why = "";
		int key;

		Target(int cell, int kind, String why) { this.cell = cell; this.kind = kind; this.why = why; obj = cell; }

		/** Is this entry a toolbar / backpack action rather than a place on the map? */
		public boolean toolbar() { return kind == USE || (kind == WAIT && action != null && !WAIT_ACT.equals(action)) || (kind == WAIT && cell == heroPos()); }

		public boolean same(Target o) {
			return o != null && o.kind == kind && o.cell == cell && o.item == item && Objects.equals(o.action, action);
		}

		@Override public String toString() {
			String k = kind == ENEMY ? "enemy" : kind == PICKUP ? "pickup" : kind == GO ? "go" : kind == USE ? "use" : "wait";
			return k + "@" + cell + (obj != cell && obj >= 0 ? "(->" + obj + ")" : "") + (item != null ? " " + item.getClass().getSimpleName() : "")
					+ (action != null ? " " + action : "") + (on != null ? " on " + on.getClass().getSimpleName() : "") + " [" + why + "]";
		}
	}

	static int heroPos() { return Dungeon.hero == null ? -1 : Dungeon.hero.pos; }

	public static int mode() { try { return WatchSettings.assist(); } catch (Throwable e) { return 1; } }
	public static boolean itemsOn() { try { return WatchSettings.itemAssist(); } catch (Throwable e) { return true; } }

	/** The last error caught (debugging). */
	public static Throwable lastError;

	// =============================================================================================================
	// state kept between turns (reset per floor)
	// =============================================================================================================
	private static Level floor;                    // the floor this state is for
	private static int goal = -1;                  // look() / explore: the spot chosen before (anti-dither)
	private static final Set<Integer> passed = new HashSet<>();     // things put down on purpose or passed over
	private static final ArrayDeque<String> declined = new ArrayDeque<>();   // hands suggestions passed over (class:action)
	private static boolean upgradeHeld;            // an upgrade was passed over: only for worn-down gear from now on
	private static int ovrC = -1;                  // the player's choice: -1 none, WAY: only the way, else an objective cell
	private static final int WAY = -2;
	private static int ovrDir = -1;                // its way (pang 0..63)
	private static boolean ovrObj, ovrUnseen;      // chosen from the list (an objective) / it was unseen when chosen
	private static int wayOn = -1;                 // the player's way ran out: tgt2 offers the next click that way, once
	private static boolean grassSkip, planAGrass; private static int grassKey;
	private static int vetoId = -1; private static float vetoUntil;
	private static int vetoStand = -1; private static float vetoStandUntil;   // tactics' spot overridden
	private static int resumeCell = -1; private static float resumeUntil;
	private static boolean stepBackWaited;         // the doorway rule: waited once after a step back
	private static int waitId = -1, waitCount;     // (the same enemy waited for again and again: stop)
	private static int lastPos = -1, from = -1;
	private static boolean[] srchFrom, checked;    // searched from / cells searched
	private static boolean fishSeen; private static float fishAt = -99;               // a piranha seen on this floor: keep out of the water
	private static boolean[] hazMem;               // gas / fire seen on the cell and not seen gone since (only what the hero saw)
	private static final Set<Integer> sentries = new HashSet<>();   // sentries seen (they never move)
	private static boolean[] sentryZone; private static int sentryKey = -1;
	private static final Map<Integer, Integer> talked = new HashMap<>();
	private static final Map<Integer, Float> bad = new HashMap<>();   // targets that did nothing when acted on (until when)
	private static Set<Item> pack = Collections.newSetFromMap(new IdentityHashMap<>());
	private static int actPos = -1, actCell = -1, actKind; private static float actTime = -1; private static Item actItem;
	private static int doorSpot = -1; private static float doorUntil;   // tactics: the spot waited at
	private static float floorT0;
	private static int eatBand;
	// mind reading: the list as it was last taken
	private static int mindPos = -1; private static Level mindFloor;
	private static int[] mindObj = new int[0], mindWalk = new int[0];
	// the last plan
	private static List<Target> last = new ArrayList<>();
	private static boolean searchLast, ambushing, fishRelaxed; private static float waterChosen = -99;   // (plan A is an ambush: no hands, no resumed walk)

	// per-plan scratch
	private static Level L; private static Hero H; private static int W, LEN, hp;
	private static int[] dist, wlk, N8;
	private static boolean[] known, open, pass, danger;
	private static boolean inDanger, anyDanger;
	private static IntPredicate screen;
	private static int head, od;
	private static boolean fish, seenFoe, zero;
	private static List<Target> out;
	private static int h0;                         // entries pinned in front
	private static final int INF = 0x7FFF;
	static final boolean DBG = opt("ADBG", false);
	/** A/B switches for the bot's batches (env AS_NAME=0/1; the watch has no environment, so it gets the default). */
	static boolean opt(String name, boolean def) {
		try { String v = System.getenv(name.startsWith("ADBG") ? name : "AS_" + name); return v == null ? def : !v.equals("0"); } catch (Throwable e) { return def; }
	}
	static final boolean AMBUSH = opt("AMBUSH", false);   // (A/B H-amb1, 120 paired games: -0.16 depth, more starving: off)
	static final boolean CRABAMB = opt("CRABAMB", true);
	static final boolean SHORE = opt("SHORE", true);   // with piranhas about, the shore too (they bite from the water)
	static final boolean OOZE = opt("OOZE", true);
	static final boolean GRASS = opt("GRASS", true);
	static final boolean STAIRS = opt("STAIRS", true);
	static final boolean ADJZAP = opt("ADJZAP", true);
	static final boolean STAFFUP = opt("STAFFUP", true);
	static final int RESTPCT = optInt("RESTPCT", 50);
	static final boolean CLOAK = opt("CLOAK", true);
	// (H-fix3, 120 paired: these four together -0.11, 30/32, DM-100 deaths 11 -> 14: off)
	static final boolean FIRE2 = opt("FIRE2", false);
	static final boolean SHOOTERS = opt("SHOOTERS", false);
	static final boolean DEWREST = opt("DEWREST", false);
	static final boolean FAIRHUNT = opt("FAIRHUNT", true);
	static final boolean GOOREACH = opt("GOOREACH", true);
	static final boolean SHOP = opt("SHOP", true);
	static final boolean CLERIC = opt("CLERIC", true);   // the Cleric casts Guiding Light   // buy by the guide's order with the gold there is   // Goo's blast by its own reach test; no ooze washing with Goo close   // "hunting" only as drawn (emotes, steps), not the AI state   // hurt, nothing about: the waterskin (keeping 5 drops) before resting   // DM-100 and the Gnoll Trickster too: out of their line, round a corner   // the fire escape: water, else out of fire's reach
	static final boolean MEAT = opt("MEAT", false);     // raw mystery meat (4 in 5 a bad effect, burning among them): eaten standing in water   // the Rogue's cloak before a fight   // nothing in sight: rest below this share of health   // the Mage's first 3 upgrades into the staff
	static final int HEALBOSS = optInt("HEALBOSS", 55);    // a boss in sight: Healing below this share (Goo's pumped blow is triple)   // the Mage's staff zapped point blank too (magic: double accuracy, more damage than the staff's own blow)   // low and losing: up the stairs (only allies follow), rest there, come back   // tall grass walked for dew and seeds (off: only within 2 steps)     // Goo's ooze: into water to wash it off   // the Great Crab: ambushes are the only blows it doesn't block
	static int optInt(String name, int def) { try { String v = System.getenv("AS_" + name); return v == null ? def : Integer.parseInt(v); } catch (Throwable e) { return def; } }
	static final int HEALPCT = optInt("HEALPCT", 40);   // (a known Healing potion is drunk in a fight below this share of health)

	// =============================================================================================================
	// the API
	// =============================================================================================================

	/** The ranked list (at most 8; entry 0 is plan A). aiming: a throw / zap / spell asks for a cell. onScreen: is the
	 *  cell on the watch's screen (null: everything is). Never throws. */
	public static List<Target> plan(boolean aiming, IntPredicate onScreen) {
		try {
			if (Dungeon.hero == null || Dungeon.level == null || !Dungeon.hero.isAlive()) return new ArrayList<>();
			setup(onScreen);
			List<Target> r = aiming ? aimList() : planList();
			last = r;
			return r;
		} catch (Throwable e) {
			lastError = e;
			try { return fallback(aiming); } catch (Throwable e2) { return new ArrayList<>(); }
		}
	}

	/** The player chose t while planA was entry 0 (call before act; nothing happens when t is plan A). */
	public static void chose(Target t, Target planA) {
		try {
			if (t == null || planA == null || t.same(planA) || Dungeon.hero == null) return;
			Hero h = Dungeon.hero; Level l = Dungeon.level; int p = h.pos;
			resumeCell = -1;
			// a hands suggestion passed over: not again this floor (pass_hands)
			if (planA.kind == USE && planA.item != null) decline(planA.item.getClass(), planA.action);
			if (planA.kind == WAIT && REST_ACT.equals(planA.action)) decline(null, REST_ACT);
			// a thing passed over (not walked towards): not offered again
			if (planA.kind == PICKUP && planA.obj >= 0 && t.cell >= 0 && cheb(l, t.cell, planA.obj) >= cheb(l, p, planA.obj)) passed.add(planA.obj);
			if (planA.kind == ENEMY && t.cell >= 0 && cheb(l, t.cell, p) <= 1 && t.kind == GO) return;   // a step back: the doorway fight goes on
			if (planA.kind == ENEMY && planA.cell >= 0) {
				Char c = Actor.findChar(planA.cell);
				if (c != null) { vetoId = c.id(); vetoUntil = Actor.now() + 15; }
			}
			if (planA.why.startsWith("tactic")) { vetoStand = planA.cell; vetoStandUntil = Actor.now() + 10; }
			if (planAGrass) grassSkip = true; grassKey = h.exp + h.lvl * 101;
			if (t.kind == ENEMY || t.kind == USE || t.kind == WAIT || t.cell < 0) return;
			int o = t.obj >= 0 ? t.obj : t.cell;
			int dx = l.cellToPoint(o).x - l.cellToPoint(p).x, dy = l.cellToPoint(o).y - l.cellToPoint(p).y;
			if (Math.abs(dx) < 2 && Math.abs(dy) < 2 && seenFoe) return;   // a step away from an enemy: no way
			ovrC = o; ovrObj = true; ovrDir = pang(dx, dy); goal = -1;
			ovrUnseen = setupOk(l) && unseenBy(o);
		} catch (Throwable e) { lastError = e; }
	}

	/** The player tapped a cell on the touch screen (optional): its way is followed like a choice from the list. */
	public static void tapped(int cell) {
		try {
			Hero h = Dungeon.hero; Level l = Dungeon.level; if (h == null || l == null || cell < 0) return;
			int dx = l.cellToPoint(cell).x - l.cellToPoint(h.pos).x, dy = l.cellToPoint(cell).y - l.cellToPoint(h.pos).y;
			if (Math.abs(dx) < 2 && Math.abs(dy) < 2) return;
			ovrC = cell; ovrObj = false; ovrDir = pang(dx, dy); goal = -1; resumeCell = -1;
		} catch (Throwable e) { lastError = e; }
	}

	/** The hero searched (the toolbar's search button too): the walls around count as searched. */
	public static void noteSearch() {
		try {
			Hero h = Dungeon.hero; Level l = Dungeon.level; if (h == null || l == null) return;
			ensureFloor(l);
			srchFrom[h.pos] = true;
			int r = h.heroClass == HeroClass.ROGUE ? 2 : 1;
			for (int c = 0; c < l.length(); c++) if (l.distance(c, h.pos) <= r) checked[c] = true;
		} catch (Throwable e) { lastError = e; }
	}

	/** Nothing to do between turns: plan() notices new turns and floors itself (kept for symmetry). */
	public static void newTurn() { }

	/** Forget everything (a new game). */
	public static void reset() { floor = null; mindFloor = null; last = new ArrayList<>(); }

	/** Do it, as the game would on a tap / item use. Returns false when nothing could be done. Never throws. */
	public static boolean act(Target t) {
		try {
			Hero h = Dungeon.hero;
			if (t == null || h == null || !h.isAlive()) return false;
			actPos = h.pos; actTime = Actor.now(); actCell = t.cell; actKind = t.kind; actItem = t.item;
			boolean aimingNow = Controls.aiming();
			if (aimingNow) { if (t.cell < 0) return false; GameScene.handleCell(t.cell); return true; }
			if (!h.ready) return false;
			switch (t.kind) {
				case USE: {
					if (t.item == null || t.action == null) return false;
					if (Wand.AC_ZAP.equals(t.action) || MagesStaff.AC_ZAP.equals(t.action)) zapped.put(t.item, Actor.now());
					if (SPELL_GL.equals(t.action) && t.item instanceof com.shatteredpixel.shatteredpixeldungeon.items.artifacts.HolyTome)
						com.shatteredpixel.shatteredpixeldungeon.actors.hero.spells.GuidingLight.INSTANCE.onCast((com.shatteredpixel.shatteredpixeldungeon.items.artifacts.HolyTome) t.item, h);   // (the tome's window, skipped: the same spell, cast)
					else t.item.execute(h, t.action);
					for (int i = 0; i < 4; i++) if (!followUp(t)) break;
					return true;
				}
				case WAIT:
					if (SEARCH_ACT.equals(t.action) || (t.action == null && t.cell == h.pos)) { noteSearch(); h.search(true); }
					else if (REST_ACT.equals(t.action)) h.rest(true);
					else h.rest(false);
					return true;
				case ENEMY:
					if (WAIT_ACT.equals(t.action)) { h.rest(false); return true; }
					break;
				case GO:
					if (t.cell == h.pos || (h.rooted && Dungeon.level.adjacent(t.cell, h.pos) && Actor.findChar(t.cell) == null)) { noteSearch(); h.search(true); return true; }
					break;
			}
			if (t.cell < 0) return false;
			noteTalk(t.cell);
			if (anyDanger) clearPath(h);   // (the hero keeps its last walk to the same cell: one planned before a hazard was seen)
			GameScene.handleCell(t.cell);
			return true;
		} catch (Throwable e) { lastError = e; return false; }
	}

	/** t was carried out some other way (the watch's own buttons, in a test): what act notes, noted. */
	public static void noted(Target t) {
		try {
			Hero h = Dungeon.hero; if (t == null || h == null) return;
			actPos = h.pos; actTime = Actor.now(); actCell = t.cell; actKind = t.kind; actItem = t.item;
			if (t.kind == WAIT && (SEARCH_ACT.equals(t.action) || (t.action == null && t.cell == h.pos))) noteSearch();
			if (t.kind != USE && t.kind != WAIT && t.cell >= 0) noteTalk(t.cell);
			if (t.item != null && (Wand.AC_ZAP.equals(t.action) || MagesStaff.AC_ZAP.equals(t.action))) zapped.put(t.item, Actor.now());
		} catch (Throwable e) { lastError = e; }
	}

	/** After an item use: aim it, answer the item selector, confirm an upgrade. True if something was done. */
	private static boolean followUp(Target t) {
		if (Controls.aiming()) {
			int c = t.cell >= 0 && t.cell != Dungeon.hero.pos ? t.cell : -1;
			if (c < 0) { GameScene.cancelCellSelector(); return false; }
			GameScene.handleCell(c); return true;
		}
		Window w = Ui.topWindow();
		if (w == null) return false;
		if (w.getClass().getSimpleName().equals("WndUpgrade")) {
			Object b = field(w, "btnUpgrade");
			if (b instanceof com.watabou.noosa.Gizmo) { Ui.press((com.watabou.noosa.Gizmo) b); return true; }
			return false;
		}
		WndBag.ItemSelector sel = selector(w);
		if (sel == null) return false;
		Item pick = t.on != null && sel.itemSelectable(t.on) ? t.on : choose(sel, t.item);
		if (sel.hideAfterSelecting()) w.hide();
		sel.onSelect(pick);
		if (pick == null) {   // "the scroll will be lost": yes
			Window c = Ui.topWindow();
			if (c instanceof InventoryScroll.WndConfirmCancel) { List<Ui.Part> bs = Controls.buttons(c); if (!bs.isEmpty()) Ui.press(bs.get(0).g); }
		}
		return true;
	}

	/** What to answer an item selector with, from the scroll that asked. */
	private static Item choose(WndBag.ItemSelector sel, Item src) {
		Hero h = Dungeon.hero; List<Item> worn = new ArrayList<>();
		if (h.belongings.armor() != null) worn.add(h.belongings.armor());
		if (h.belongings.weapon() != null) worn.add(h.belongings.weapon());
		if (h.belongings.ring() != null) worn.add(h.belongings.ring());
		if (h.belongings.artifact() != null) worn.add(h.belongings.artifact());
		if (h.belongings.misc() != null) worn.add(h.belongings.misc());
		if (src instanceof ScrollOfUpgrade) {
			for (Item i : worn) if (sel.itemSelectable(i) && !(i.cursed && i.cursedKnown)) return i;
		}
		if (src instanceof ScrollOfIdentify || src instanceof ScrollOfRemoveCurse) {
			for (Item i : worn) if (sel.itemSelectable(i)) return i;
			for (Item i : h.belongings.backpack) if (sel.itemSelectable(i)) return i;
		}
		return null;   // (transmutation and the rest: the player's own choice)
	}

	@SuppressWarnings("unchecked")
	private static WndBag.ItemSelector selector(Window w) {
		for (Class<?> c = w.getClass(); c != null && c != Object.class; c = c.getSuperclass())
			for (java.lang.reflect.Field f : c.getDeclaredFields())
				if (WndBag.ItemSelector.class.isAssignableFrom(f.getType())) {
					try { f.setAccessible(true); return (WndBag.ItemSelector) f.get(w); } catch (Exception e) { return null; }
				}
		return null;
	}

	private static void clearPath(Hero h) {
		try { java.lang.reflect.Field f = Char.class.getDeclaredField("path"); f.setAccessible(true); f.set(h, null); } catch (Throwable e) { }
	}

	private static Object field(Object o, String name) {
		for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass()) {
			try { java.lang.reflect.Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o); }
			catch (NoSuchFieldException e) { } catch (Exception e) { return null; }
		}
		return null;
	}

	// =============================================================================================================
	// set-up for a list
	// =============================================================================================================

	private static boolean setupOk(Level l) { return L == l && dist != null && dist.length == l.length(); }

	private static void ensureFloor(Level l) {
		if (floor == l && srchFrom != null && srchFrom.length == l.length()) return;
		floor = l; goal = -1; passed.clear(); declined.clear(); upgradeHeld = false; ovrC = -1; wayOn = -1; grassSkip = false;
		vetoId = -1; vetoStand = -1; resumeCell = -1; stepBackWaited = false; ambGoal = -1; ambId = -1; waitId = -1; lastPos = -1; from = -1;
		srchFrom = new boolean[l.length()]; checked = new boolean[l.length()]; fishSeen = false; fishAt = -99; talked.clear(); bad.clear();
		huntMem.clear(); mobLast.clear();
		hazMem = new boolean[l.length()]; sentries.clear(); sentryZone = new boolean[l.length()]; sentryKey = -1; crabSeen = -1; bossSeen = -1;
		doorSpot = -1; floorT0 = Actor.now(); actCell = -1; actTime = -1; eatBand = 0;
	}

	private static void setup(IntPredicate onScreen) {
		H = Dungeon.hero; L = Dungeon.level; W = L.width(); LEN = L.length(); hp = H.pos;
		ensureFloor(L);
		screen = mode() == 2 ? null : onScreen;
		N8 = new int[] { -W, 1, W, -1, -W - 1, -W + 1, W + 1, W - 1 };
		if (dist == null || dist.length != LEN) { dist = new int[LEN]; wlk = new int[LEN]; known = new boolean[LEN]; open = new boolean[LEN]; pass = new boolean[LEN]; danger = new boolean[LEN]; }
		// the way the hero last went
		if (lastPos >= 0 && lastPos != hp) from = lastPos;
		lastPos = hp;
		head = -1;
		if (from >= 0 && from != hp) { int dx = x(hp) - x(from), dy = y(hp) - y(from); if (Math.abs(dx) <= 8 && Math.abs(dy) <= 8) head = pang(dx, dy); }
		seenFoe = false; fish = fishSeen;
		for (Mob m : mobs()) if (L.heroFOV[m.pos] && m.isAlive()) {
			if (m instanceof Piranha) { fish = true; fishSeen = true; fishAt = Actor.now(); }
			if (m instanceof com.shatteredpixel.shatteredpixeldungeon.levels.rooms.special.SentryRoom.Sentry) sentries.add(m.pos);
		}
		for (int c = 0; c < LEN; c++) {
			known[c] = L.visited[c] || L.mapped[c];
			if (L.heroFOV[c]) hazMem[c] = blobAt(c);
		}
		hazMem[hp] = blobAt(hp);   // (felt, even blind)
		watchMobs();
		for (Mob m : mobs()) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) { seenFoe = true; if (Char.hasProp(m, Char.Property.BOSS) && m.isAlive()) bossSeen = Actor.now(); }
		walls(fish);
		// the last act did nothing (no time passed, nowhere moved): that target is no use for a while
		if (actTime >= 0 && actPos == hp && Actor.now() == actTime && actCell >= 0 && actKind != USE && actKind != WAIT) {
			bad.put(actCell, Actor.now() + 30);
			if (actKind == PICKUP) passed.add(actCell);
		}
		actTime = -1;
		bad.values().removeIf(t -> t < Actor.now());
		// things put down on purpose: a heap where the hero stands holding something that was in the pack
		Heap hh = L.heaps.get(hp);
		if (hh != null) for (Item i : hh.items) if (pack.contains(i)) { passed.add(hp); break; }
		Set<Item> np = Collections.newSetFromMap(new IdentityHashMap<>());
		for (Item i : H.belongings.backpack) np.add(i);
		pack = np;
		// the player's choice
		if (ovrC >= 0 && ovrC == hp) ovrC = ovrObj ? -1 : WAY;
		od = ovrC != -1 ? ovrDir : -1;
		if (grassSkip && H.exp + H.lvl * 101 != grassKey) grassSkip = false;
		zero = false; h0 = 0;
	}

	private static List<Mob> mobs() { return new ArrayList<>(L.mobs); }
	/** A mimic still hiding: on screen it is a chest (sprites/MimicSprite), so it is treated as one. */
	private static boolean hiddenMimic(Mob m) { return m instanceof Mimic && m.alignment == Char.Alignment.NEUTRAL; }
	private static boolean disguised(Mob m) {
		if (!(m instanceof Mimic) || m.alignment != Char.Alignment.NEUTRAL) return false;
		if (m instanceof GoldenMimic) return Notes.keyCount(new GoldenKey(Dungeon.depth)) > 0;   // (drawn as a locked chest)
		if (m instanceof CrystalMimic) return Notes.keyCount(new CrystalKey(Dungeon.depth)) > 0;
		return true;
	}
	/** The heap on c, if the hero has seen it. */
	private static Heap seenHeap(int c) { Heap h = L.heaps.get(c); return h != null && h.seen ? h : null; }
	/** The character on c as the hero sees it (none out of sight or invisible: Thomas's rule). */
	private static Char seenChar(int c) {
		Char ch = Actor.findChar(c);
		if (ch == null || ch == H) return ch;
		return c >= 0 && c < LEN && L.heroFOV[c] && ch.invisible <= 0 ? ch : null;
	}

	/** The cells to keep off: gas and fire seen (hazMem), a seen sentry's beat (the special floor in its sight: it zaps
	 *  whoever stands there), and with piranhas about the water. They are walls for the plan, except when the hero
	 *  already stands in one (then the way out is through them). Then the walk (bfs). */
	private static void hazards(boolean water) {
		if (sentries.hashCode() != sentryKey) {
			sentryKey = sentries.hashCode(); Arrays.fill(sentryZone, false);
			for (int s : sentries) for (int c = 0; c < LEN; c++)
				if (L.map[c] == Terrain.EMPTY_SP && distance(c, s) <= 12 && sight(s, c)) sentryZone[c] = true;
		}
		anyDanger = false;
		for (int c = 0; c < LEN; c++) { danger[c] = hazMem[c] || sentryZone[c] || (water && wet(c)); anyDanger |= danger[c] && known[c]; }
		inDanger = danger[hp];
		for (int c = 0; c < LEN; c++) {
			boolean ok = L.passable[c] && !L.avoid[c];
			open[c] = ok && !danger[c];
			pass[c] = ok && (!danger[c] || inDanger) && (known[c] || L.heroFOV[c]);
		}
	}

	/** The hazards, the characters in sight (in the way), the walk. */
	private static void walls(boolean water) {
		hazards(water);
		for (Mob m : mobs()) if (L.heroFOV[m.pos] && m.pos != hp) pass[m.pos] = false;
		bfs();
	}

	private static void bfs() {
		Arrays.fill(dist, INF);
		int[] q = new int[LEN]; int qh = 0, qt = 0;
		dist[hp] = 0; q[qt++] = hp;
		while (qh < qt) {
			int c = q[qh++], d = dist[c] + 1;
			for (int k = 0; k < 8; k++) {
				int n = c + N8[k];
				if (!nb(c, n) || dist[n] <= d || !pass[n]) continue;
				dist[n] = d; q[qt++] = n;
			}
		}
		for (int c = 0; c < LEN; c++) wlk[c] = dist[c];
		for (int c = 0; c < LEN; c++) {
			if (dist[c] >= INF) continue;
			int d = dist[c] + 1;
			for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && d < wlk[n]) wlk[n] = d; }
		}
	}

	// =============================================================================================================
	// geometry
	// =============================================================================================================

	private static int x(int c) { return c % W; }
	private static int y(int c) { return c / W; }
	private static boolean nb(int c, int n) { return n >= 0 && n < LEN && Math.abs(n % W - c % W) <= 1; }
	private static int cheb(Level l, int a, int b) { return l.distance(a, b); }
	private static int distance(int a, int b) { return Math.max(Math.abs(x(a) - x(b)), Math.abs(y(a) - y(b))); }
	private static boolean adjacent(int a, int b) { return distance(a, b) == 1; }
	private static int walk(int c) { return c < 0 || c >= LEN ? INF : wlk[c]; }

	/** A pseudo-angle 0..63, clockwise on screen from the east. */
	static int pang(int dx, int dy) {
		int s = Math.abs(dx) + Math.abs(dy); if (s == 0) return 0;
		return dx > 0 && dy >= 0 ? 16 * dy / s : dx <= 0 && dy > 0 ? 16 + 16 * -dx / s : dx < 0 ? 32 + 16 * -dy / s : 48 + 16 * dx / s;
	}
	private static int off(int c, int a) { return Math.abs(((pang(x(c) - x(hp), y(c) - y(hp)) - a + 32) & 63) - 32); }
	private static final int CONE = 12;
	private static boolean ahead(int c) { return od < 0 || c == hp || off(c, od) <= CONE || dist[c] <= 3; }
	private static boolean onScreen(int c) { try { return screen == null || screen.test(c); } catch (Throwable e) { return true; } }
	private static boolean direct(int c) {
		if (od < 0) return true;
		if (dist[c] > 2 * distance(c, hp) + 2) return false;
		while (!onScreen(c)) { int b = c; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && dist[n] < dist[b]) b = n; } if (b == c) break; c = b; }
		return ahead(c);
	}

	// =============================================================================================================
	// the list
	// =============================================================================================================

	private static void del(int j) { out.remove(j); }

	private static Target add(int c, int kind, int key, int obj, String why) {
		if (c < 0 || c >= LEN) return null;
		int m = L.map[c];
		if (m == Terrain.WALL || m == Terrain.WALL_DECO || m == Terrain.SECRET_DOOR) return null;   // never a wall
		if (bad.containsKey(c) && kind != ENEMY) return null;
		if (zero) key = 0;
		for (int j = h0; j < out.size(); j++) if (out.get(j).cell == c && out.get(j).kind != USE) { if (key >= out.get(j).key) return null; del(j); break; }
		if (out.size() >= CAP) { if (key >= out.get(out.size() - 1).key) return null; out.remove(out.size() - 1); }
		Target t = new Target(c, kind, why); t.key = key; t.obj = obj;
		int i = out.size();
		while (i > h0 && out.get(i - 1).key > key) i--;
		out.add(i, t);
		return t;
	}

	/** t_put: off screen, the farthest step on screen along the walk there (not onto stairs on the way). */
	private static Target put(int c, int kind, int key, String why) {
		int obj = c, c0 = c;
		int ent = L.entrance(), ex = L.exit();
		for (int guard = 0; guard < LEN && (!onScreen(c) || (c != c0 && (c == ent || c == ex))); guard++) {
			int b = c;
			if (onScreen(c))
				for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && onScreen(n) && open[n] && n != ent && n != ex && dist[n] >= dist[c] && dist[n] < INF) { b = n; break; } }
			if (b != c) { c = b; kind = GO; break; }
			for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && dist[n] < dist[b]) b = n; }
			if (b == c) return null;
			c = b; kind = GO;
		}
		if (c != c0) for (int t = 0; t < 4; t++) {     // two far objectives standing in at one cell: the second a step nearer
			boolean used = false; for (Target e : out) used |= e.cell == c;
			if (!used) break;
			int b = c; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && dist[n] < dist[b]) b = n; }
			if (b == c || b == hp) break;
			c = b;
		}
		if (c != hp || kind == PICKUP || zero) return add(c, kind, key, obj, why);
		return null;
	}

	private static Target front(Target t) {   // t_front: pinned, after those pinned before it
		for (int j = h0; j < out.size(); j++) if (out.get(j).cell == t.cell && out.get(j).kind == t.kind && t.kind != USE) { del(j); break; }
		if (out.size() >= CAP) out.remove(out.size() - 1);
		t.key = 0; out.add(h0++, t);
		return t;
	}

	private static List<Target> planList() {
		out = new ArrayList<>(); h0 = 0;
		int[] kind = new int[1]; String[] why = new String[1]; boolean[] waitF = new boolean[1];
		searchLast = false; ambushing = false; fishRelaxed = false;
		int c0 = first(kind, why, waitF);
		zero = true;
		Target a = c0 >= 0 ? put(c0, kind[0], 0, why[0]) : null;
		zero = false;
		if (a != null && (waitF[0] || kind[0] == WAIT)) a.action = WAIT_ACT;
		if (a != null && kind[0] == GO && c0 == hp) { a.kind = WAIT; a.action = SEARCH_ACT; }
		plansB();
		if (mode() != 0) {
			Target first = out.isEmpty() ? null : out.get(0);
			if (itemsOn()) { Target hk = hands(first); if (hk != null) front(hk); }
			boolean pinned = h0 > 0 && !(Item.AC_THROW.equals(out.get(0).action) || Wand.AC_ZAP.equals(out.get(0).action) || SpiritBow.AC_SHOOT.equals(out.get(0).action) || SPELL_GL.equals(out.get(0).action));
			if (!pinned) h0 = 0;
			Target e = escape();
			if (e != null) front(e);
			else if (!(vetoStand >= 0 && Actor.now() < vetoStandUntil)) { Target tc = tactics(); if (tc != null) front(tc); }
			h0 = 0;
		}
		if (anyDanger && !inDanger) for (Target t : out) if (t.kind != USE && t.kind != WAIT && t.cell >= 0) t.cell = safeWay(t.cell);
		mind();
		return out;
	}

	/** The game walks its own shortest way (Hero.getCloser): one through a hazard is replaced by a cell on the safe way
	 *  there, the farthest one the game's walk to which stays clear (the objective is kept). */
	private static int safeWay(int c) {
		if (c == hp || adjacent(c, hp)) return c;
		boolean gs = gameSafe(c);
		if (DBG) System.out.println("    safeWay " + c + " safe " + gs + " dist " + dist[c] + " danger " + danger[c] + " inDanger " + inDanger);
		if (gs) return c;
		List<Integer> way = new ArrayList<>();
		int cur = c;
		if (dist[cur] >= INF) { int b = -1; for (int k = 0; k < 8; k++) { int n = cur + N8[k]; if (nb(cur, n) && dist[n] < INF && (b < 0 || dist[n] < dist[b])) b = n; } if (b < 0) return c; cur = b; }
		for (int g = 0; g < LEN && cur != hp; g++) {
			way.add(cur);
			int b = cur; for (int k = 0; k < 8; k++) { int n = cur + N8[k]; if (nb(cur, n) && dist[n] < dist[b]) b = n; }
			if (b == cur) break; cur = b;
		}
		if (way.isEmpty()) return c;
		for (int i = 0, step = 1, tries = 0; i < way.size() && tries < 8; i += step, step++, tries++) { int w = way.get(i); if (w == c) continue; if (adjacent(w, hp) || gameSafe(w)) return w; }
		return way.get(way.size() - 1);
	}
	private static boolean gameSafe(int c) {
		try {
			boolean[] p = new boolean[LEN];
			for (int i = 0; i < LEN; i++) p[i] = L.passable[i] && (L.visited[i] || L.mapped[i]);
			if (!H.flying) for (Mob ch : mobs()) if (ch.pos >= 0 && ch.pos < LEN && H.fieldOfView != null && H.fieldOfView[ch.pos]) p[ch.pos] = false;   // (as Dungeon.findPassable; not Actor.chars(): a new HashSet hashes every character, and with -XX:hashCode=3 that changes the game's own hash orders)
			List<Integer> path = gamePath(hp, c, p);
			if (path == null) return true;
			for (int q : path) if (q != c && danger[q]) return false;
			return true;
		} catch (Throwable e) { return true; }
	}

	/** The walk PathFinder.find would give (the same shortest way and the same choice among equals), without touching
	 *  PathFinder's shared arrays: the game reads PathFinder.distance after its own builds, and a plan run in between
	 *  changed what the game did. null: no way. */
	private static List<Integer> gamePath(int from, int to, boolean[] p) {
		if (from == to) return null;
		int[] d = new int[LEN]; Arrays.fill(d, Integer.MAX_VALUE);
		int[] q = new int[LEN]; int qh = 0, qt = 0;
		d[to] = 0; q[qt++] = to; boolean found = false;
		while (qh < qt && !found) {
			int c = q[qh++], nd = d[c] + 1;
			for (int k = 0; k < 8; k++) { int n = c + N8[k];
				if (!nb(c, n) || d[n] <= nd) continue;
				if (n == from) { d[n] = nd; found = true; break; }
				if (!p[n]) continue;
				d[n] = nd; q[qt++] = n; }
		}
		if (!found) return null;
		int[] dir = { -1, 1, -W, W, -W - 1, -W + 1, W - 1, W + 1 };   // (PathFinder's order)
		List<Integer> r = new ArrayList<>();
		for (int s = from, g = 0; s != to && g < LEN; g++) {
			int m = d[s], b = s;
			for (int k = 0; k < 8; k++) { int n = s + dir[k]; if (n >= 0 && n < LEN && nb(s, n) && d[n] < m) { m = d[n]; b = n; } }
			if (b == s) return null;
			s = b; r.add(s);
		}
		return r;
	}

	private static List<Target> aimList() {
		out = new ArrayList<>();
		List<Mob> ms = new ArrayList<>();
		for (Mob m : mobs()) if (L.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && m.invisible <= 0 && m.isAlive()) ms.add(m);
		ms.sort(Comparator.comparingInt((Mob m) -> (line(hp, m.pos) ? 0 : 100) + (m.state == m.HUNTING ? 0 : 50) + distance(hp, m.pos)));
		for (Mob m : ms) if (out.size() < CAP) { Target t = new Target(m.pos, ENEMY, line(hp, m.pos) ? "in line" : "enemy"); out.add(t); }
		for (Mob m : mobs()) if (out.size() < CAP && L.heroFOV[m.pos] && m.alignment != Char.Alignment.ENEMY && !hiddenMimic(m) && !has(m.pos)) out.add(new Target(m.pos, GO, "char"));
		for (Heap hh : L.heaps.valueList()) if (out.size() < CAP && L.heroFOV[hh.pos] && !has(hh.pos)) out.add(new Target(hh.pos, GO, "heap"));
		if (out.size() < CAP && !has(hp)) out.add(new Target(hp, GO, "self"));
		return out;
	}
	private static boolean has(int c) { for (Target t : out) if (t.cell == c) return true; return false; }

	private static List<Target> fallback(boolean aiming) {
		List<Target> r = new ArrayList<>();
		Level l = Dungeon.level; int h = Dungeon.hero.pos;
		for (Mob m : new ArrayList<>(l.mobs)) if (l.heroFOV[m.pos] && m.alignment == Char.Alignment.ENEMY && r.size() < CAP) r.add(new Target(m.pos, ENEMY, "fallback"));
		if (aiming) return r;
		int[] d = Controls.walk(l, h);
		int e = Explorer.nearestUnexplored(l, h, d);
		if (e >= 0) r.add(new Target(e, GO, "fallback explore"));
		else if (l.exit() > 0 && (l.visited[l.exit()] || l.mapped[l.exit()])) r.add(new Target(l.exit(), GO, "fallback exit"));
		Target s = new Target(h, WAIT, "fallback search"); s.action = SEARCH_ACT; r.add(s);
		return r;
	}

	// =============================================================================================================
	// what's what
	// =============================================================================================================

	private static boolean enemy(Mob m) { return m.isAlive() && m.alignment == Char.Alignment.ENEMY && L.heroFOV[m.pos] && m.invisible <= 0; }
	/** Is it after the hero, as the screen shows it (Shatterfish's Observer rule: no AI state, only what is drawn): the
	 *  Zz of a sleeper, the "!" of one that noticed, the "?" of one that lost track, and one stepping nearer. */
	private static boolean hunting(Mob m) { return FAIRHUNT ? huntMem.getOrDefault(m.id(), false) : m.state == m.HUNTING; }
	private static final Map<Integer, Boolean> huntMem = new HashMap<>();
	private static final Map<Integer, Integer> mobLast = new HashMap<>();
	private static void watchMobs() {
		for (Mob m : mobs()) {
			if (!m.isAlive() || !L.heroFOV[m.pos] || m.alignment != Char.Alignment.ENEMY) continue;
			int id = m.id(); Integer lp = mobLast.get(id);
			Object e = m.sprite == null ? null : field(m.sprite, "emo");
			boolean shown = e instanceof com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon && ((com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon) e).alive;
			Boolean h = huntMem.get(id);
			if (m.state == m.SLEEPING) h = false;   // (Zz)
			else if (shown && e instanceof com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon.Alert) h = true;
			else if (shown && e instanceof com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon.Lost) h = false;
			else if (lp != null && lp != m.pos && L.heroFOV[lp] && distance(m.pos, hp) < distance(lp, hp)) h = true;   // (it stepped nearer)
			else if (h == null) h = adjacent(m.pos, hp) && m.state != m.PASSIVE;
			huntMem.put(id, h); mobLast.put(id, m.pos);
		}
	}
	private static boolean boss(Char m) { return Char.hasProp(m, Char.Property.BOSS) || Char.hasProp(m, Char.Property.MINIBOSS); }
	private static boolean ranged(Mob m) {
		return m instanceof Shaman || m instanceof Warlock || m instanceof Eye || m instanceof Scorpio || m instanceof GnollTrickster || m instanceof DM100
				|| m instanceof DM200 || m instanceof Tengu || m instanceof Necromancer || m instanceof Elemental || m instanceof FungalSentry
				|| m instanceof GnollGeomancer || m instanceof YogFist || m instanceof DwarfKing || m instanceof Goo || m instanceof Spinner;
	}
	private static boolean held(Char m) {
		for (Buff b : m.buffs()) { String n = b.getClass().getSimpleName(); if (n.equals("Paralysis") || n.equals("Roots") || n.equals("Frost") || n.equals("Terror") || n.equals("Dread")) return true; }
		return false;
	}
	private static boolean water(int c) { return L.water[c]; }
	/** Water or beside it: a piranha bites from the water onto the shore. */
	private static boolean wet(int c) { if (L.water[c]) return true; if (!SHORE || Actor.now() - fishAt > 20) return false; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && L.water[n]) return true; } return false; }

	/** Gas or fire on c, as the hero last saw it. */
	private static boolean foul(int c) { return c >= 0 && c < LEN && hazMem != null && c < hazMem.length && hazMem[c]; }
	/** (the game's own state: only for cells in sight) */
	private static boolean blobAt(int c) {
		for (Blob b : L.blobs.values()) {
			if (b == null || b.volume <= 0 || b.cur == null || c >= b.cur.length || b.cur[c] <= 0) continue;
			if (b instanceof ToxicGas || b instanceof CorrosiveGas || b instanceof ConfusionGas || b instanceof Fire || b instanceof ParalyticGas
					|| b instanceof StenchGas || b instanceof Electricity || b instanceof Inferno || b instanceof Blizzard || b instanceof Freezing) return true;
		}
		return false;
	}
	private static boolean burning() { for (Buff b : H.buffs()) if (b.getClass().getSimpleName().equals("Burning")) return true; return false; }
	private static boolean fireAt(int c) { Blob f = L.blobs.get(Fire.class); return c >= 0 && c < LEN && L.heroFOV[c] && f != null && f.volume > 0 && f.cur != null && f.cur[c] > 0; }
	private static boolean fireNear(int c) { if (fireAt(c)) return true; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && fireAt(n) && L.flamable[c]) return true; } return false; }
	private static boolean oozed() { for (Buff b : H.buffs()) if (b.getClass().getSimpleName().equals("Ooze")) return true; return false; }
	private static boolean blind() { for (Buff b : H.buffs()) if (b.getClass().getSimpleName().equals("Blindness")) return true; return false; }

	private static boolean unseenBy(int c) {   // c is beside the unknown (not just beside the hero)
		if (!known[c] || !open[c] || L.map[c] == Terrain.ALCHEMY || c == L.entrance() || c == L.exit() || (fish && water(c))) return false;
		for (int k = 0; k < 8; k++) {
			int n = c + N8[k]; if (!nb(c, n) || known[n] || distance(n, hp) <= 1) continue;
			if (k >= 4) { int a = x(n) + y(c) * W, b = x(c) + y(n) * W;   // a diagonal unknown between two known walls: a room's corner
				if (known[a] && !L.passable[a] && !L.avoid[a] && known[b] && !L.passable[b] && !L.avoid[b]) continue; }
			return true;
		}
		return false;
	}
	private static boolean opening(int c) {
		if (!unseenBy(c)) return false;
		for (int k = 0; k < 4; k++) { int n = c + N8[k]; if (nb(c, n) && !known[n]) return true; }
		return false;
	}
	private static boolean wall(int c) { int t = L.map[c]; return t == Terrain.WALL || t == Terrain.WALL_DECO || t == Terrain.SECRET_DOOR; }
	private static boolean searched(int w) { return checked[w]; }

	private static boolean deadEnd(int c) {   // a corridor that just stops: a hidden door
		if (!known[c] || !open[c] || c == L.entrance() || c == L.exit() || srchFrom[c]) return false;
		int o = 0, oo = 0;
		for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb(c, n)) continue; if (!known[n]) return false;
			if (!wall(n)) { o++; if (k < 4) oo++; } }
		return o == 1 && oo == 1;
	}

	private static int wallWorth(int w) {
		int t = L.map[w];
		if ((t != Terrain.WALL && t != Terrain.SECRET_DOOR) || searched(w)) return 0;
		int best = 0;
		for (int k = 0; k < 4; k++) {
			int a = w - N8[k], b = w + N8[k]; if (!nb(w, a) || !nb(w, b)) continue;
			int bx = x(b), by = y(b); if (bx <= 0 || by <= 0 || bx >= W - 1 || by >= L.height() - 1) continue;
			if (!known[a] || known[b] || !open[a]) continue;
			int x0 = 2 * bx - x(w), y0 = 2 * by - y(w), u = 0;
			for (int yy = y0 - 1; yy <= y0 + 1; yy++) for (int xx = x0 - 1; xx <= x0 + 1; xx++) u += xx >= 0 && xx < W && yy >= 0 && yy < L.height() && !known[xx + yy * W] ? 1 : 0;
			int v = 1 + u / 3 + (deadEnd(a) ? 12 : 0); if (v > best) best = v;
		}
		return best;
	}

	private static int srchSpot(int x1, int x2) {
		int best = -1, fb = -1; long bk = 0;
		for (int c = 0; c < LEN; c++) {
			if (dist[c] >= INF || !open[c] || L.map[c] == Terrain.ALCHEMY || c == L.entrance() || c == L.exit() || (fish && water(c)) || (c != hp && seenChar(c) != null) || bad.containsKey(c)) continue;
			if ((x1 >= 0 && distance(c, x1) <= 2) || (x2 >= 0 && distance(c, x2) <= 2)) continue;
			Heap hh = seenHeap(c); if (hh != null && hh.type == Heap.Type.FOR_SALE) continue;
			int walls = 0, g = 0;
			for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && (L.map[n] == Terrain.WALL || L.map[n] == Terrain.SECRET_DOOR) && !searched(n)) { walls++; g += wallWorth(n); } }
			if (walls == 0) continue;
			if (g > 0) { long sc = (long) g * 1024 / (dist[c] + 2); if (sc > bk) { bk = sc; best = c; } }
			if (fb < 0 || dist[c] < dist[fb]) fb = c;
		}
		return best < 0 ? fb : best;
	}

	private static boolean foodItem(Item i) { return i instanceof Food && !(i instanceof Blandfruit); }
	private static boolean known(Item i) {
		if (i instanceof Potion) return ((Potion) i).isKnown();
		if (i instanceof Scroll) return ((Scroll) i).isKnown();
		return true;
	}
	private static boolean precious(Item i) { return (i instanceof ScrollOfUpgrade || i instanceof PotionOfStrength) && known(i); }
	private static <T extends Item> T bag(Class<T> c) { return H.belongings.getItem(c); }
	private static boolean hasFood() { for (Item i : H.belongings.backpack) if (foodItem(i)) return true; return false; }

	private static boolean roomFor(Item it) {
		Bag bp = H.belongings.backpack; int n = bp.items.size(), cap = bp.capacity();
		if (n < cap - 2) return true;
		for (Bag b : H.belongings.getBags()) if (b != bp && b.canHold(it)) return true;
		if (n < cap && (foodItem(it) || precious(it))) return true;
		if (it instanceof Gold || it instanceof Dewdrop || it instanceof Key || it instanceof EnergyCrystal || it instanceof Amulet) return true;
		return it.stackable && H.belongings.getSimilar(it) != null;
	}
	private static boolean junk(Item it) { return it.visiblyCursed() || (it.levelKnown && it.level() < 0 && (it instanceof EquipableItem || it instanceof Wand)); }

	private static boolean risky(Heap h) {
		if (h.type == Heap.Type.TOMB) return H.lvl < Dungeon.depth + 3;
		if ((h.type == Heap.Type.SKELETON || h.type == Heap.Type.REMAINS) && H.HP * 3 < H.HT * 2) return true;
		return false;
	}

	private static boolean heapOk(Heap h) {
		if (h == null || h.isEmpty() || !h.seen) return false;
		int c = h.pos;
		if (c < 0 || c >= LEN || risky(h) || h.type == Heap.Type.FOR_SALE || !known[c] || L.avoid[c] || passed.contains(c) || bad.containsKey(c) || (danger[c] && c != hp) || (fishRelaxed && wet(c) && c != hp)) return false;
		if (h.type == Heap.Type.LOCKED_CHEST && Notes.keyCount(new GoldenKey(Dungeon.depth)) <= 0) return false;
		if (h.type == Heap.Type.CRYSTAL_CHEST && Notes.keyCount(new CrystalKey(Dungeon.depth)) <= 0) return false;
		if (h.type == Heap.Type.HEAP) {
			Item top = h.peek();
			if (!roomFor(top) || junk(top)) return false;
			// the Wandmaker's candles, set round the ritual spot: they stay there
			if (top instanceof com.shatteredpixel.shatteredpixeldungeon.items.quest.CeremonialCandle && L.adjacent(c, com.shatteredpixel.shatteredpixeldungeon.items.quest.CeremonialCandle.ritualPos)) return false;
			if (top instanceof Dewdrop) {   // with room in the waterskin it goes there; else it heals (at full health it stays put)
				Waterskin ws = bag(Waterskin.class);
				if (!(ws != null && !ws.isFull()) && H.HP * 2 >= H.HT) return false;
			}
		}
		return true;
	}

	private static boolean floorLeft() {
		for (int c = 0; c < LEN; c++) if (dist[c] < INF && dist[c] > 0 && unseenBy(c)) return true;
		for (Heap h : L.heaps.valueList()) if (heapOk(h) && walk(h.pos) < INF) return true;
		return false;
	}

	private static int hunger() { Hunger hu = H.buff(Hunger.class); return hu == null ? 0 : hu.hunger(); }
	private static boolean hungry() { return hunger() >= Hunger.HUNGRY; }
	private static boolean starving() { return hunger() >= Hunger.STARVING; }

	private static int foodNear() {   // hungry, none in the pack: the nearest food lying about (12 steps at most)
		if (!hungry() || hasFood()) return -1;
		int best = -1, bd = 13;
		for (Heap h : L.heaps.valueList()) { int c = h.pos;
			if (h.type == Heap.Type.HEAP && foodItem(h.peek()) && heapOk(h) && walk(c) < bd && (c == hp || seenChar(c) == null)) { bd = walk(c); best = c; } }
		return best;
	}

	private static boolean questFlag(Class<?> npc, String field, boolean def) {
		try {
			Class<?> q = Class.forName(npc.getName() + "$Quest");
			java.lang.reflect.Field f = q.getDeclaredField(field); f.setAccessible(true); return f.getBoolean(null);
		} catch (Throwable e) { return def; }
	}
	private static boolean npcWanted(Mob m) {   // a quest person worth talking to now
		if (talked.getOrDefault(m.id(), 0) >= 2) return false;
		if (m instanceof Ghost) return !questFlag(Ghost.class, "given", true) || Ghost.Quest.processed();
		if (m instanceof Wandmaker) return !questFlag(Wandmaker.class, "given", true) || bag(CorpseDust.class) != null || bag(Embers.class) != null || bag(Rotberry.Seed.class) != null;
		if (m instanceof Blacksmith) return !questFlag(Blacksmith.class, "given", true);
		if (m instanceof Imp) { DwarfToken t = bag(DwarfToken.class); return !Imp.Quest.given() || (t != null && t.quantity() >= 5); }
		return false;
	}

	private static boolean line(int a, int b) { try { Ballistica bl = new Ballistica(a, b, Ballistica.PROJECTILE); return bl.collisionPos != null && bl.collisionPos == b; } catch (Throwable e) { return false; } }

	private static String dk(Class<?> c, String a) { return (c == null ? "-" : c.getSimpleName()) + ":" + a; }
	private static void decline(Class<?> c, String a) { declined.addLast(dk(c, a)); while (declined.size() > 8) declined.removeFirst(); if (c == ScrollOfUpgrade.class) upgradeHeld = true; }
	private static boolean declined(Class<?> c, String a) { return declined.contains(dk(c, a)); }

	// =============================================================================================================
	// plan A (tgt.c first())
	// =============================================================================================================

	private static int first(int[] kind, String[] why, boolean[] waitF) {
		int best = -1, bk = INF; Mob bm = null;
		planAGrass = false;
		boolean crabNow = false; for (Mob m : mobs()) if (m instanceof GreatCrab && enemy(m)) crabNow = true;
		if (!crabNow) crabVisSince = -1; else if (crabVisSince < 0) crabVisSince = Actor.now();
		for (Mob m : mobs()) {   // fight: a boss beside you, the weakest beside you, else the nearest
			if (!enemy(m)) continue;
			boolean adj = adjacent(m.pos, hp);
			if (!adj && vetoId == m.id() && Actor.now() < vetoUntil && !(hunting(m) && walk(m.pos) <= 4)) continue;
			if (bad.containsKey(m.pos) && (!adj || Actor.now() < bad.get(m.pos) - 25)) continue;   // (acted on, nothing happened: one it can't get at, even beside it: round a wall's corner)
			if (adj) stepBackWaited = false;
			if ((m.state == m.PASSIVE && (!adj || m instanceof Statue)) || (m instanceof Piranha && !adj) || (Char.hasProp(m, Char.Property.IMMOVABLE) && !adj)) continue;
			if (!adj && walk(m.pos) > 2 * distance(m.pos, hp) + 4) continue;
			if (!adj && !hunting(m) && walk(m.pos) > distance(m.pos, hp) + 1) continue;
			if (m.state == m.SLEEPING && !adj && !boss(m) && distance(m.pos, hp) > 2 && floorLeft()) continue;
			int k = adj ? (boss(m) ? 0 : 1 + m.HP) : walk(m.pos) < INF ? 1000 + walk(m.pos) * 64 + m.HP : INF;
			if (k < bk) { bk = k; best = m.pos; bm = m; }
		}
		// hidden for an ambush, the hunter out of sight now: wait for it a few turns (it was seen coming)
		if (best < 0 && (AMBUSH || CRABAMB) && ambId >= 0 && ambGoal == hp && Actor.now() < ambUntil && !seenFoe) { kind[0] = WAIT; why[0] = "ambush: wait for it"; ambushing = true; return hp; }
		int fd = foodNear();
		if (fd >= 0 && bk >= 1000 && (best < 0 || walk(fd) <= 3) && ahead(fd)) { kind[0] = PICKUP; why[0] = "hungry: food"; return fd; }
		if (best >= 0) {
			kind[0] = ENEMY; why[0] = "fight";
			if (mode() == 0) return best;
			Mob m = bm; int w = walk(best);
			boolean fast = m.speed() >= 2 * H.speed() && !H.rooted;
			if (bk >= 1000 && (w == 2 || (fast && w == 3)) && hunting(m) && !held(m) && !ranged(m) && !Char.hasProp(m, Char.Property.IMMOVABLE)) {
				if (fast && w == 2) {   // waiting, it closes and strikes in one turn; a step back, it spends both moves closing
					for (int k = 0; k < 8; k++) { int c = hp + N8[k];
						if (!nb(hp, c) || !open[c] || seenChar(c) != null || water(c) || distance(c, best) < 3) continue;
						boolean near = false; for (Mob o : mobs()) if (o != m && enemy(o) && hunting(o) && distance(o.pos, c) <= 2) near = true;
						if (!near) { kind[0] = GO; why[0] = "step back from a fast one"; return c; } }
				} else waitF[0] = true;
			}
			boolean crab = m instanceof GreatCrab;   // (it blocks every blow from one it saw at its turn's start: only ambushes hurt it)
			if (crab && bk < 1000 && crabVisSince == Actor.now()) { }   // (it stepped up blind: hit it now)
			else if (crab && CRABAMB) { int s0 = ambushSpot(m); if (s0 >= 0 && s0 != hp) { kind[0] = GO; why[0] = "ambush: out of the crab's sight"; ambushing = true; return s0; } if (s0 == hp) { waitF[0] = true; why[0] = "ambush: wait for the crab"; ambushing = true; return best; }
				// no hidden spot near: away from it (it is slower), till one turns up (a blow it sees is always blocked)
				int fc = -1, fs = -INF;
				for (int c = 0; c < LEN; c++) if (dist[c] >= 1 && dist[c] <= 6 && open[c] && seenChar(c) == null && !danger[c]) {
					int sc = 3 * distance(c, m.pos) - 2 * dist[c] + (sight(m.pos, c) ? 0 : 20);
					if (sc > fs) { fs = sc; fc = c; } }
				if (fc >= 0 && fs > 3 * distance(hp, m.pos)) { kind[0] = GO; why[0] = "away from the crab"; return fc; } }
			int amb = AMBUSH && !crab && bk >= 1000 && w <= 8 && hunting(m) && !held(m) && !ranged(m) && !fast && !Char.hasProp(m, Char.Property.IMMOVABLE) ? ambushSpot(m) : -1;
			if (amb == hp && waitId == m.id() && waitCount > 2 * w + 2) amb = -1;   // (it isn't coming)
			if (amb == hp) { waitF[0] = true; if (waitId == m.id()) waitCount++; else { waitId = m.id(); waitCount = 1; } why[0] = "ambush: wait unseen"; ambushing = true; return best; }
			if (amb >= 0) { kind[0] = GO; why[0] = "ambush: a step out of its sight"; ambushing = true; return amb; }
			if (!waitF[0] && bk >= 1000 && w <= 3 && from >= 0 && distance(from, best) <= 1 && !stepBackWaited) { waitF[0] = true; stepBackWaited = true; }
			if (bk >= 1000 && hunting(m)) for (int k = 0; k < 8; k++) { int n = hp + N8[k]; if (nb(hp, n) && foul(n)) waitF[0] = true; }
			if (waitF[0]) { if (waitId == m.id()) { if (++waitCount > 3) waitF[0] = false; } else { waitId = m.id(); waitCount = 1; } why[0] = "doorway: let it come"; }
			else waitId = -1;
			if (bk < 1000 && m instanceof Piranha) {   // a piranha beside you: out of its reach, the shortest way
				int c0 = -1, d0 = INF;
				for (int c = 0; c < LEN; c++) if (dist[c] < d0 && dist[c] > 0 && open[c] && c != L.entrance() && c != L.exit()) {
					boolean wet = water(c); for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n)) wet |= water(n); }
					if (!wet) { d0 = dist[c]; c0 = c; } }
				if (c0 >= 0) { kind[0] = GO; why[0] = "out of the water"; waitF[0] = false; return c0; }
			}
			return best;
		}
		waitId = -1;
		boolean amulet = bag(Amulet.class) != null;
		for (int pass2 = 0; pass2 < 3; pass2++) {
			// things worth picking up (underfoot first), the nearest
			kind[0] = PICKUP; bk = INF; best = -1;
			for (Heap h : L.heaps.valueList()) {
				int c = h.pos;
				if (!heapOk(h) || L.map[c] == Terrain.ALCHEMY || (c != hp && seenChar(c) != null) || (fish && water(c))) continue;
				int d = c == hp ? 0 : walk(c);
				if (d >= INF) continue;
				if (d < bk && (d == 0 || (direct(c) && (od < 0 || (d <= 4 && off(c, od) <= 6))) || (h.type == Heap.Type.HEAP && precious(h.peek())))) { bk = d; best = c; }
			}
			if (SHOP && best < 0) { int sb = -1, sk = INF;
				for (Heap h : L.heaps.valueList()) {   // the shop, by the guide's order (2.7): what's wanted and affordable (the price is shown)
					if (h.type != Heap.Type.FOR_SALE || !h.seen || h.pos == hp || passed.contains(h.pos) || bad.containsKey(h.pos) || seenChar(h.pos) != null) continue;
					int d = walk(h.pos), pr = buyRank(h.peek());
					if (pr <= 0 || d >= INF) continue;
					int sc = d - pr * 8;
					if (sc < sk) { sk = sc; sb = h.pos; }
				}
				if (sb >= 0) { why[0] = "buy"; return sb; } }
			for (Mob m : mobs()) {   // a hidden mimic looks like a chest: opened like one (MimicSprite draws it so; knowing better would be cheating)
				if (!disguised(m) || !L.heroFOV[m.pos] || passed.contains(m.pos) || bad.containsKey(m.pos) || H.HP * 3 < H.HT * 2 || danger[m.pos]) continue;
				int d = walk(m.pos);
				if (d < bk && direct(m.pos)) { bk = d; best = m.pos; }
			}
			if (best >= 0) { why[0] = "pick up"; return best; }
			kind[0] = GO;
			if (ovrC >= 0 && ovrC < LEN) {   // the cell the player chose: plan A till met
				int c = ovrC;
				boolean useCell = !L.passable[c] || L.map[c] == Terrain.ALCHEMY;
				boolean met = ovrObj && (ovrUnseen ? !unseenBy(c) : useCell && walk(c) <= 1) && seenHeap(c) == null && seenChar(c) == null;
				if (met || walk(c) >= INF) ovrC = WAY;
				else { why[0] = "your choice"; return c; }
			}
			for (Mob m : mobs()) {   // the quest people
				if (m.isAlive() && L.heroFOV[m.pos] && npcWanted(m) && walk(m.pos) < bk && ahead(m.pos)) { bk = walk(m.pos); best = m.pos; }
			}
			if (best >= 0) { why[0] = "talk"; return best; }
			for (int c = 0; c < LEN; c++) {   // a door to unlock, a well worth a drink
				if (!known[c] || walk(c) >= bk || !ahead(c) || bad.containsKey(c)) continue;
				int t = L.map[c];
				if ((t == Terrain.LOCKED_DOOR && Notes.keyCount(new IronKey(Dungeon.depth)) > 0) || (t == Terrain.CRYSTAL_DOOR && Notes.keyCount(new CrystalKey(Dungeon.depth)) > 0)
						|| (t == Terrain.WELL && wellUseful(c))) { bk = walk(c); best = c; }
			}
			if (best >= 0) { why[0] = "unlock / well"; return best; }
			int st = amulet ? L.entrance() : L.exit();
			boolean way = st > 0 && known[st] && walk(st) < INF && (L.map[st] != Terrain.LOCKED_EXIT || Notes.keyCount(new WornKey(Dungeon.depth)) > 0);
			if (!way) { int d0 = 9; for (int c = 0; c < LEN; c++) if (walk(c) < d0 && deadEnd(c) && ahead(c) && !bad.containsKey(c)) { d0 = walk(c); best = c; }
				if (best >= 0) { why[0] = "dead end: search"; return best; } }
			Waterskin ws = bag(Waterskin.class);
			if ((best = look(ws != null && !ws.isFull())) >= 0) { why[0] = planAGrass ? "look: grass" : "look"; return best; }
			if (!amulet || !way) {   // explore: the nearest spot beside the unknown (the one chosen before while no other is 3 nearer)
				int g = goal; bk = INF;
				for (int p = 0; p < 2 && best < 0; p++)
					for (int c = 0; c < LEN; c++) if (dist[c] < bk && dist[c] > 0 && !bad.containsKey(c) && unseenBy(c) && ahead(c) && (p > 0 || direct(c))) { bk = dist[c]; best = c; }
				if (g >= 0 && g < LEN && dist[g] < INF && dist[g] <= bk + 3 && dist[g] > 0 && !bad.containsKey(g) && unseenBy(g) && ahead(g) && direct(g)) best = g;
				if (best >= 0) { goal = best; why[0] = "explore"; return best; }
			}
			bk = INF;
			for (int c = 0; c < LEN; c++) {   // a known trap the only way on: over it
				if (!known[c] || !L.avoid[c] || L.pit[c] || L.solid[c] || L.traps.get(c) == null || walk(c) >= bk || !ahead(c) || bad.containsKey(c)) continue;
				for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb(c, n) && (!known[n] || (open[n] && dist[n] >= INF))) { bk = walk(c); best = c; break; } }
			}
			if (best >= 0) { why[0] = "over the trap"; return best; }
			if (od >= 0) { wayOn = od; ovrC = -1; od = -1; continue; }   // nothing left the player's way: the plan again
			if ((best = look(true)) >= 0) { why[0] = "grass"; return best; }
			if (way) { if (st == hp) kind[0] = PICKUP; why[0] = amulet ? "up" : "stairs"; return st; }
			if (st > 0 && L.map[st] == Terrain.LOCKED_EXIT) {   // the boss's key
				for (Heap h : L.heaps.valueList()) if (h.seen && h.type == Heap.Type.HEAP && h.peek() instanceof WornKey && known[h.pos] && walk(h.pos) < INF) { kind[0] = PICKUP; why[0] = "the key"; return h.pos; }
			}
			if (fish) { fish = false; fishRelaxed = true; waterChosen = Actor.now(); walls(false); continue; }   // only water left, piranhas about: into it all the same (to explore or leave, not for loot)
			break;
		}
		searchLast = true;   // search for secret doors (plansB moves it to the best spot)
		kind[0] = GO; why[0] = "search";
		return hp;
	}

	/** The ambush (strategy-reference 2.3/2.4): a mob sees at the start of its turn, then moves; one that steps up beside
	 *  the hero from a cell with no sight of it is surprised, and the hero's next blow can't miss. Where to stand for that:
	 *  here (hp) or a free step away, so that the mob's walk there (the known map only) comes up beside it from a cell
	 *  out of its sight. -1: no such spot. */
	private static int ambushSpot(Mob m) {
		boolean[] p = new boolean[LEN];
		for (int i = 0; i < LEN; i++) p[i] = L.passable[i] && (known[i] || L.heroFOV[i]);
		int best = -1, bd = INF;
		List<Integer> cand = new ArrayList<>(); cand.add(hp);
		if (ambGoal >= 0 && ambGoal != hp && ambId == m.id() && Actor.now() < ambUntil) cand.add(0, ambGoal);
		boolean slow = m instanceof GreatCrab; int r = slow ? 5 : AMBR;
		for (int c = 0; c < LEN; c++) if (c != hp && dist[c] <= r && dist[c] < INF && !cand.contains(c)) cand.add(c);
		for (int s : cand) {
			int k = s == hp ? -1 : 0;
			if (k >= 0 && (!open[s] || seenChar(s) != null || (fish && water(s)) || danger[s] || dist[s] >= INF)) continue;
			boolean other = false;
			for (Mob o : mobs()) if (o != m && enemy(o) && hunting(o) && distance(o.pos, s) <= 2) other = true;
			if (other) continue;
			p[s] = true;
			List<Integer> path = gamePath(m.pos, s, p);
			if (path == null || path.size() > 12) continue;
			int q = m.pos;
			for (int c : path) { if (distance(c, s) <= 1) break; q = c; }
			if (distance(q, s) <= 1) continue;   // (beside it already)
			if (sight(q, s)) continue;
			if (k >= 0 && dist[s] >= (slow ? path.size() * 3 / 2 : path.size()) - 1) continue;   // (it would be there first)
			int d = k < 0 ? 0 : s == ambGoal ? 0 : dist[s];
			if (d < bd) { bd = d; best = s; }
		}
		if (best >= 0 && best != hp) { if (ambGoal != best || ambId != m.id()) ambUntil = Actor.now() + (slow ? 14 : 8); ambGoal = best; ambId = m.id(); }
		return best;
	}
	private static int ambGoal = -1, ambId = -1; private static float ambUntil;
	static final int AMBR = opt("AMB3", true) ? 3 : 1;

	/** How much a shop item is wanted (0: not, or can't pay): Ankh, Healing, food, a bag, Magic Mapping. */
	private static int buyRank(Item i) {
		int price; try { price = com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Shopkeeper.sellPrice(i); } catch (Throwable e) { return 0; }
		if (price > Dungeon.gold || !roomFor(i)) return 0;
		if (i instanceof Ankh && bag(Ankh.class) == null) return 6;
		if (i instanceof PotionOfHealing && known(i)) return 5;
		if (foodItem(i) && !(i instanceof MysteryMeat)) return 4;
		if (i instanceof Bag && !(i instanceof com.shatteredpixel.shatteredpixeldungeon.items.bags.MagicalHolster)) { for (Bag b : H.belongings.getBags()) if (b.getClass() == i.getClass()) return 0; return 3; }
		if (i instanceof ScrollOfMagicMapping && known(i)) return 2;
		return 0;
	}

	private static boolean wellUseful(int c) {
		Blob h = L.blobs.get(WaterOfHealth.class), a = L.blobs.get(WaterOfAwareness.class);
		if (h != null && h.volume > 0 && h.cur != null && h.cur[c] > 0) return H.HP < H.HT * 2 / 3;
		if (a != null && a.volume > 0 && a.cur != null && a.cur[c] > 0) return true;
		return false;
	}

	/** On screen first: the spots beside the unknown and the tall grass, nearest first, those ahead before the rest. */
	private static int look(boolean all) {
		int best = -1, bk = INF; boolean gb = false, gr = !grassSkip;
		for (int c = 0; c < LEN; c++) {
			int d = dist[c];
			if (d >= INF || d == 0 || !ahead(c) || bad.containsKey(c)) continue;
			boolean k = c == goal;
			boolean g = gr && L.map[c] == Terrain.HIGH_GRASS && (GRASS ? (all || k || (onScreen(c) && d <= 8)) : d <= 2) && seenChar(c) == null && seenHeap(c) == null;
			if (!g && !((onScreen(c) || k) && unseenBy(c))) continue;
			if (k) d = d > 3 ? d - 3 : 0;
			else { if (od < 0 && head >= 0 && off(c, head) > 16) d += d / 4 + 2;
				if (!onScreen(c)) d += 512; }
			if (d < bk) { bk = d; best = c; gb = g; }
		}
		planAGrass = gb;
		if (best >= 0) goal = best;
		return best;
	}

	// =============================================================================================================
	// plans B, C... (tgt2)
	// =============================================================================================================

	private static void plansB() {
		boolean hunt = false;
		for (Mob m : mobs()) if (enemy(m) && hunting(m) && !(m instanceof NPC)) hunt = true;
		if (searchLast) {   // plan A's last resort: the best spot to search for secret doors from
			int a = srchSpot(-1, -1);
			if (a >= 0 && a != hp && !out.isEmpty() && out.get(0).cell == hp) {
				del(0); zero = true; Target t = put(a, GO, 0, "search spot"); zero = false;
				if (t == null) { Target s = new Target(hp, WAIT, "search here"); s.action = SEARCH_ACT; front(s); h0 = 0; }
				else { out.remove(t); out.add(0, t); }
			}
		}
		// the player's way ran out: the next click that way
		int wd = hunt ? -1 : wayOn >= 0 ? wayOn : ovrC == WAY ? ovrDir : -1;
		if (wd >= 0 && (out.isEmpty() || (out.get(0).kind != ENEMY && out.get(0).cell != hp))) {
			int nt = -1, ns = INF;
			for (int c = 0; c < LEN; c++) {
				int sd = dist[c]; if (sd < 1 || sd >= INF || off(c, wd) > CONE || bad.containsKey(c)) continue;
				Heap hh = seenHeap(c); Char ch = seenChar(c);
				int w = (hh != null && heapOk(hh)) || (ch instanceof NPC && L.heroFOV[c]) ? 3 : c == L.exit() || unseenBy(c) || (L.map[c] == Terrain.LOCKED_DOOR && Notes.keyCount(new IronKey(Dungeon.depth)) > 0) ? 4 : 0;
				if (w > 0 && sd * w < ns) { ns = sd * w; nt = c; }
			}
			if (nt >= 0) { zero = true; Target t = put(nt, seenHeap(nt) != null ? PICKUP : GO, 0, "your way on"); zero = false; if (t != null) { out.remove(t); out.add(0, t); } ovrC = WAY; }
			wayOn = -1;
		}
		// the interrupted walk, taken up again (30 turns), the fight first
		HeroAction la = H.lastAction;
		if (la instanceof HeroAction.Move && la.dst != hp && la.dst >= 0 && la.dst < LEN && resumeCell != la.dst) { resumeCell = la.dst; resumeUntil = Actor.now() + 30; }
		if (resumeCell >= 0 && (resumeCell == hp || Actor.now() > resumeUntil || dist[resumeCell] >= INF)) resumeCell = -1;
		if (resumeCell >= 0 && !hunt && !ambushing && !(out.size() > 0 && out.get(0).kind == ENEMY) && mode() != 0) {
			zero = true; Target t = put(resumeCell, seenHeap(resumeCell) != null ? PICKUP : GO, 0, "resume"); zero = false;
			if (t != null) { out.remove(t); out.add(0, t); }
		}
		int ne = 0;
		for (Mob m : mobs()) {
			if (!m.isAlive() || !L.heroFOV[m.pos]) continue;
			int d = distance(hp, m.pos);
			if (m.alignment == Char.Alignment.ENEMY && m.invisible <= 0) { ne++; if (onScreen(m.pos) && (d < 2 || !(m instanceof Piranha)) && (d < 2 || !bad.containsKey(m.pos))) add(m.pos, ENEMY, 1 << 10 | d, m.pos, "enemy"); }
			else if ((npcWanted(m) || m instanceof Shopkeeper) && walk(m.pos) < INF) put(m.pos, GO, 4 << 10 | walk(m.pos), "talk");
		}
		for (Heap h : L.heaps.valueList()) {
			if (!heapOk(h)) continue;
			int d = h.pos == hp ? 0 : walk(h.pos);
			if (d < INF) put(h.pos, PICKUP, 3 << 10 | d, "item");
		}
		for (Mob m : mobs()) if (disguised(m) && L.heroFOV[m.pos] && walk(m.pos) < INF && !passed.contains(m.pos)) put(m.pos, PICKUP, 3 << 10 | walk(m.pos), "item");   // (a chest, as far as anyone can see)
		int ent = L.entrance(), ex = L.exit();
		for (int c = 0; c < LEN; c++) {
			int t = L.map[c], d = walk(c);
			if (d >= INF || c == hp || bad.containsKey(c)) continue;
			if ((t == Terrain.WELL && wellUseful(c)) || (t == Terrain.LOCKED_DOOR && Notes.keyCount(new IronKey(Dungeon.depth)) > 0) || (t == Terrain.CRYSTAL_DOOR && Notes.keyCount(new CrystalKey(Dungeon.depth)) > 0)) put(c, GO, 4 << 10 | d, "unlock / well");
			else if (deadEnd(c)) put(c, GO, 4 << 10 | d, "dead end");
			else if (c == ex && known[c] && (t != Terrain.LOCKED_EXIT || Notes.keyCount(new WornKey(Dungeon.depth)) > 0)) put(c, GO, 6 << 10 | d, "stairs down");
			else if (c == ent && known[c]) put(c, GO, 7 << 10 | d, "stairs up");
			else if ((t == Terrain.DOOR || t == Terrain.OPEN_DOOR) && onScreen(c)) put(c, GO, 8 << 10 | d, "door");
		}
		boolean[] cov = new boolean[LEN]; int nreg = 0;
		for (int n = 0; n < CAP; n++, nreg++) {   // the unexplored regions: one entry each
			int best = -1, bd = INF;
			for (int c = 0; c < LEN; c++) if (!cov[c] && dist[c] < bd && dist[c] > 0 && !bad.containsKey(c) && opening(c)) { bd = dist[c]; best = c; }
			if (best < 0) break;
			cov[best] = true;
			for (int c = 0; c < LEN; c++)
				if (dist[c] < INF && Math.abs(dist[c] - bd) <= 3 && opening(c) && L.map[c] != Terrain.DOOR && L.map[best] != Terrain.DOOR && distance(c, best) <= 3) cov[c] = true;
			put(best, GO, 5 << 10 | bd, "unexplored");
		}
		if (nreg == 0 && (ex <= 0 || !known[ex] || walk(ex) >= INF)) {   // all explored, the way on hidden: where to search
			int a = srchSpot(-1, -1), b = a >= 0 ? srchSpot(a, -1) : -1, c = b >= 0 ? srchSpot(a, b) : -1;
			if (a >= 0) put(a, GO, 5 << 10 | walk(a), "search spot");
			if (b >= 0) put(b, GO, 5 << 10 | walk(b), "search spot");
			if (c >= 0) put(c, GO, 5 << 10 | walk(c), "search spot");
		}
		if (ne > 0) {   // a step away: the free neighbour farthest from every enemy in sight
			int best = -1, bs = 0;
			for (int k = 0; k < 8; k++) {
				int n = hp + N8[k], s = 99;
				if (!nb(hp, n) || !open[n] || seenChar(n) != null) continue;
				for (Target t : out) if (t.kind == ENEMY) { int e = distance(n, t.cell); if (e < s) s = e; }
				s = s * 2 + (L.map[n] == Terrain.DOOR || L.map[n] == Terrain.OPEN_DOOR ? 1 : 0);
				if (s > bs) { bs = s; best = n; }
			}
			if (best >= 0) add(best, GO, 2 << 10, best, "step away");
		}
		if (mode() == 0) {   // Off: the enemies only, Select attacks
			out.removeIf(t -> t.kind != ENEMY);
			for (Target t : out) t.action = null;
		}
	}

	/** "Read my mind": two steps or more nearer one of the list's other objectives (and plan A's not nearer): that's plan A. */
	private static void mind() {
		if (mode() == 0) return;
		boolean up = false; int pr = -1;
		if (mindFloor == L && mindPos != hp) {
			for (int j = 0; j < mindObj.length; j++) {
				int c = mindObj[j]; int g = mindWalk[j] - (c >= 0 && c < LEN ? walk(c) : INF);
				if (j == 0 && c < 0) break;
				if (g > 0) { if (j == 0) break; up = true; if (g >= 2 && (od < 0 || off(c, od) <= 8)) { pr = c; up = false; break; } }
			}
		}
		if (pr >= 0) {
			for (int j = 1; j < out.size(); j++) if (out.get(j).obj == pr) { Target t = out.remove(j); out.add(0, t); break; }
			int dx = x(pr) - x(hp), dy = y(pr) - y(hp);
			ovrC = pr; ovrObj = true; ovrDir = pang(dx, dy); ovrUnseen = unseenBy(pr);
		}
		if (!up) {
			mindFloor = L; mindPos = hp; mindObj = new int[out.size()]; mindWalk = new int[out.size()];
			for (int i = 0; i < out.size(); i++) { Target t = out.get(i); int c = t.kind == ENEMY || t.kind == USE || t.kind == WAIT ? -1 : t.obj;
				mindObj[i] = c; mindWalk[i] = c >= 0 && c < LEN ? walk(c) : INF; }
		}
	}

	// =============================================================================================================
	// escape and tactics (tgt4)
	// =============================================================================================================

	private static Target escape() {
		int best = -1, bk = INF;
		if (FIRE2 && (burning() || fireNear(hp))) {   // fire: water if near (it puts you out), else a cell fire can't reach (no grass on or beside it), away from the flames
			int bs = INF;
			for (int c = 0; c < LEN; c++) {
				if (dist[c] >= INF || dist[c] > 10 || (!open[c] && !(water(c) && pass[c])) || seenChar(c) != null || bad.containsKey(c) || foul(c)) continue;
				if (c == hp && !burning() && !L.flamable[c]) continue;
				int sc = dist[c] * 2 + (L.flamable[c] ? 12 : 0) + (water(c) && burning() ? -10 : 0);
				for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb(c, n)) continue; if (L.flamable[n]) sc += 2; if (fireAt(n)) sc += 10; }
				if (sc < bs) { bs = sc; best = c; }
			}
			if (best >= 0 && best != hp) return new Target(best, GO, "escape the fire");
			best = -1;
		}
		if (foul(hp) || burning()) {   // out of the gas / fire: the nearest clean cell (alight: water, if near)
			boolean w = burning();
			for (int p = 0; p < 2 && best < 0; p++, w = false)
				for (int c = 0; c < LEN; c++) if (dist[c] < bk && dist[c] > 0 && (!w || dist[c] <= 4) && open[c] && !foul(c) && seenChar(c) == null && (!w || water(c)) && !bad.containsKey(c)) { bk = dist[c]; best = c; }
			if (best >= 0) { Target t = new Target(best, GO, "escape the gas / fire"); return t; }
		}
		boolean adjFoe = false; for (Mob m : mobs()) if (enemy(m) && adjacent(m.pos, hp)) adjFoe = true;
		boolean gooNear = false; for (Mob m : mobs()) if (m instanceof Goo && enemy(m) && (pumped(m) || distance(m.pos, hp) <= 3)) gooNear = true;
		if (OOZE && oozed() && !water(hp) && !(GOOREACH && gooNear)) {   // Goo's ooze eats away till washed off: into water (strategy-reference 2.6/9.12), mid-fight only if it's a step or two
			for (int c = 0; c < LEN; c++) if (dist[c] < bk && dist[c] > 0 && dist[c] <= (adjFoe ? 2 : 12) && water(c) && pass[c] && seenChar(c) == null && !foul(c) && !bad.containsKey(c)) { bk = dist[c]; best = c; }
			if (best >= 0) return new Target(best, GO, "escape: wash off the ooze");
		}
		Target up = STAIRS ? upstairs() : null;
		if (up != null) return up;
		if (inDanger && (sentryZone[hp] || hazMem[hp] || (Actor.now() - fishAt <= 5 && !fishRelaxed && !(Actor.now() - waterChosen <= 10 && Actor.now() >= waterChosen)))) {   // (relaxed: the plan has chosen the water, the only way on)   // on a sentry's floor, or by the water with a piranha just seen: off it, the shortest way
			for (int c = 0; c < LEN; c++) if (dist[c] < bk && dist[c] > 0 && !danger[c] && open[c] && seenChar(c) == null && !bad.containsKey(c)) { bk = dist[c]; best = c; }
			if (best >= 0) return new Target(best, GO, sentryZone[hp] ? "escape: off the sentry's floor" : "escape: out of the water");
		}
		return null;
	}

	/** Low, an enemy after the hero, no known Healing: the way up is a sure escape (enemies stay behind; strategy-reference 2.3).
	 *  Only from depth 2, not on a boss floor, not with the Amulet, and only when the stairs are nearer than the danger. */
	private static Target upstairs() {
		if (Dungeon.depth <= 1 || Dungeon.bossLevel() || bag(Amulet.class) != null || H.HP * 100 >= H.HT * 35) return null;
		PotionOfHealing heal = bag(PotionOfHealing.class); if (heal != null && heal.isKnown()) return null;
		int ent = L.entrance(); if (L.locked || ent < 0 || ent >= LEN || !known[ent] || dist[ent] >= INF || dist[ent] > 8 || bad.containsKey(ent)) return null;
		int near = INF; boolean hunted = false;
		for (Mob m : mobs()) if (enemy(m) && hunting(m) && !(m instanceof NPC)) { hunted = true; near = Math.min(near, distance(m.pos, ent)); }
		if (!hunted || near < dist[ent]) return null;
		Target t = new Target(ent, ent == hp ? PICKUP : GO, "escape: up the stairs");
		return t;
	}

	private static Target tactics() {
		if (H.rooted) return null;
		int adj = 0, os = 0;
		for (Mob m : mobs()) if (enemy(m) && hunting(m) && adjacent(m.pos, hp)) adj++;
		for (int k = 0; k < 8; k++) os += nb(hp, hp + N8[k]) && open[hp + N8[k]] ? 1 : 0;
		if (adj >= 2 && os > 2) {   // two or more beside you in the open: back into a narrow spot none touches
			int bc = -1, bo = 9;
			for (int k = 0; k < 8; k++) { int c = hp + N8[k]; if (!nb(hp, c) || !open[c] || seenChar(c) != null || water(c)) continue;
				boolean touch = false; for (Mob m : mobs()) if (enemy(m) && hunting(m) && adjacent(m.pos, c)) touch = true;
				int o = 0; for (int j = 0; j < 8; j++) o += nb(c, c + N8[j]) && open[c + N8[j]] ? 1 : 0;
				if (!touch && o <= 2 && o < bo) { bo = o; bc = c; } }
			if (bc >= 0) return new Target(bc, GO, "tactic: narrow spot");
		}
		Mob sh = null, goo = null, crab = null;
		for (Mob m : mobs()) if (m instanceof GreatCrab && m.isAlive() && hunting(m) && L.heroFOV[m.pos]) { crab = m; crabSeen = Actor.now(); }
		boolean crabWasUnseen = crabUnseen; crabUnseen = crab == null;
		if (!CRABAMB && (crab != null || (crabSeen >= 0 && Actor.now() - crabSeen < 20))) {   // the Ghost's crab blocks every blow from one it sees: out of its sight, then strike as it steps up blind
			if (crab == null) {   // (out of sight now: it was coming)
				if (doorSpot == hp && Actor.now() < doorUntil) { Target t = new Target(hp, WAIT, "tactic: wait for the crab"); t.action = WAIT_ACT; return t; }   // (not for ever: a crab that never comes)
				return null;
			}
			if (adjacent(crab.pos, hp) && crabWasUnseen) return null;   // the ambush: hit it now
			int best = -1, bk = INF;
			for (int c = 0; c < LEN; c++) if (c != hp && walk(c) <= 8 && walk(c) < bk && standOk(c) && !adjacent(c, crab.pos) && !sight(crab.pos, c)) { bk = walk(c); best = c; }
			if (best >= 0) { doorSpot = best; doorUntil = Actor.now() + 20; return new Target(best, GO, "tactic: out of the crab's sight"); }
		}
		for (Mob m : mobs()) {
			if (!enemy(m)) continue;
			int d = distance(m.pos, hp);
			if (m instanceof Goo && pumped(m)) goo = m;
			else if (d < 2) return null;   // one beside you: fight it
			if ((m instanceof Eye || m instanceof Scorpio || m instanceof Shaman || m instanceof Warlock || (SHOOTERS && (m instanceof DM100 || m instanceof GnollTrickster))) && hunting(m) && (sh == null || d < distance(sh.pos, hp))) sh = m;
		}
		if (goo != null) {   // Goo pumped up hits hard and reaches 2 cells: out of its reach and let it waste it
			if (!gooReach(goo.pos, hp)) { Target t = new Target(hp, WAIT, "tactic: let Goo waste it"); t.action = WAIT_ACT; return t; }
			int best = -1, bk = 99;
			for (int c = 0; c < LEN; c++) if (walk(c) <= 2 && walk(c) < bk && standOk(c) && !gooReach(goo.pos, c)) { bk = walk(c); best = c; }
			return best >= 0 ? new Target(best, GO, "tactic: out of Goo's reach") : null;
		}
		if (sh != null && line(sh.pos, hp)) {   // a shooter: out of its line, round a corner; it comes to you
			int best = -1, bk = INF;
			for (int c = 0; c < LEN; c++) if (c != hp && walk(c) <= 5 && standOk(c) && !water(c) && walk(c) < bk && !line(sh.pos, c)) { bk = walk(c); best = c; }
			if (best >= 0) { doorSpot = best; doorUntil = Actor.now() + distance(sh.pos, best) + 4; return new Target(best, GO, "tactic: out of its line"); }
		}
		if (doorSpot == hp && Actor.now() < doorUntil && sh != null) { Target t = new Target(hp, WAIT, "tactic: wait round the corner"); t.action = WAIT_ACT; return t; }
		return null;
	}
	private static boolean crabUnseen = true; private static float crabSeen = -1, bossSeen = -1;
	private static float crabVisSince = -1;   // (the crab in sight since when)
	private static boolean sight(int a, int b) { try { Ballistica bl = new Ballistica(a, b, Ballistica.STOP_TARGET | Ballistica.STOP_SOLID); return bl.collisionPos != null && bl.collisionPos == b; } catch (Throwable e) { return true; } }
	/** Goo's pumped blast reaches c: Goo.canAttack's own test (2 cells, a clear line both ways). */
	private static boolean gooReach(int g, int c) {
		if (!GOOREACH) return distance(g, c) <= 2 && line(g, c);
		try {
			int f = Ballistica.STOP_TARGET | Ballistica.STOP_SOLID | Ballistica.IGNORE_SOFT_SOLID;
			return distance(g, c) <= 2 && new Ballistica(g, c, f).collisionPos == c && new Ballistica(c, g, f).collisionPos == g;
		} catch (Throwable e) { return distance(g, c) <= 2; }
	}
	private static boolean pumped(Mob m) { Object v = field(m, "pumpedUp"); return v instanceof Integer && (Integer) v > 0; }
	private static boolean standOk(int c) { return c == hp || (open[c] && seenChar(c) == null && walk(c) <= 6); }

	// =============================================================================================================
	// the hands (tgt3): item and toolbar actions
	// =============================================================================================================

	private static Target use(Item it, String action, int aim, String why) {
		Target t = new Target(aim >= 0 ? aim : hp, USE, why); t.item = it; t.action = action; t.obj = -1;
		try { for (int i = 0; i < QuickSlot.SIZE; i++) if (Dungeon.quickslot.getItem(i) == it) { t.quickslot = i; break; } } catch (Throwable e) { }
		return t;
	}

	private static Target hands(Target planA) {
		boolean near = false, seen = false, adj = false; int hard = 0;
		for (Mob m : mobs()) { if (!enemy(m) || m.state == m.PASSIVE || m instanceof NPC) continue; seen = true;
			int d = distance(m.pos, hp); if (d <= 3) near = true; if (d <= 1) adj = true;
			hard += boss(m) ? 2 : hunting(m) ? 1 : 0; }
		if (foul(hp) || burning()) return null;   // out of the gas / fire first
		if (ambushing && H.HP * 3 > H.HT) return null;   // lying in wait: nothing that would give it away
		boolean scrollsOk = !blind();
		// eat once starving (never with one within 3); starving, the stairs next: down first
		int st = bag(Amulet.class) != null ? L.entrance() : L.exit();
		boolean down = planA != null && (planA.obj == st || planA.cell == st) && H.HP * 2 >= H.HT && !seen;
		boolean eat = starving() && !near && !down;
		int band = H.HP * 4 < H.HT ? 2 : H.HP * 2 < H.HT ? 1 : 0;
		boolean passedEat = declinedAny(Food.AC_EAT);
		if (!passedEat) eatBand = 0;
		else if (eatBand == 0) eatBand = band + 1;
		else if (band + 1 > eatBand) { passedEat = false; declined.removeIf(s -> s.endsWith(":" + Food.AC_EAT)); eatBand = 0; }
		if (eat && !passedEat) {
			Item f = null;
			for (Item i : H.belongings.backpack) if (foodItem(i) && (f == null || (f instanceof MysteryMeat && !(i instanceof MysteryMeat)))) f = i;
			if (f instanceof MysteryMeat && MEAT && !water(hp)) {   // (in water the fire goes out at once)
				int wc = -1, wd = 16; for (int c = 0; c < LEN; c++) if (water(c) && pass[c] && dist[c] < wd && !danger[c] && seenChar(c) == null) { wd = dist[c]; wc = c; }
				if (wc >= 0) return new Target(wc, GO, "to water, to eat the meat");
			}
			if (f != null) return use(f, Food.AC_EAT, -1, "starving: eat");
		}
		PotionOfHealing heal = bag(PotionOfHealing.class); boolean pot = heal != null && heal.isKnown();
		boolean bossSeenNow = false; for (Mob m : mobs()) if (enemy(m) && Char.hasProp(m, Char.Property.BOSS)) bossSeenNow = true;
		if (seen && H.HP * 100 < H.HT * (bossSeenNow ? HEALBOSS : HEALPCT) && pot && (adj || hard > 0)) return use(heal, Potion.AC_DRINK, -1, "low: heal");
		Waterskin ws = bag(Waterskin.class);
		if (adj && H.HP * 4 < H.HT && !pot && ws != null && ws.actions(H).contains("DRINK")) return use(ws, "DRINK", -1, "low: waterskin");
		if (adj && H.HP * 4 < H.HT && !pot) {   // dying beside one: a known way out first
			Class<?>[] esc = { ScrollOfTeleportation.class, PotionOfInvisibility.class, ScrollOfTerror.class, ScrollOfLullaby.class, ScrollOfMirrorImage.class };
			for (Class<?> c : esc) { @SuppressWarnings("unchecked") Item i = bag((Class<Item>) c);
				if (i != null && known(i) && (i instanceof Potion || scrollsOk)) return use(i, i instanceof Potion ? Potion.AC_DRINK : Scroll.AC_READ, -1, "dying: escape"); }
			for (Item i : H.belongings.backpack) if (i instanceof Potion && !known(i)) return use(i, Potion.AC_DRINK, -1, "dying: gamble");
			if (scrollsOk) for (Item i : H.belongings.backpack) if (i instanceof Scroll && !known(i)) return use(i, Scroll.AC_READ, -1, "dying: gamble");
		}
		if (seen && CLOAK) {   // the Rogue: unseen, the next blow is a sure surprise one (attacking ends it: one charge, one blow)
			com.shatteredpixel.shatteredpixeldungeon.items.artifacts.CloakOfShadows ck = bag(com.shatteredpixel.shatteredpixeldungeon.items.artifacts.CloakOfShadows.class);
			Object ch = ck == null ? null : field(ck, "charge");
			boolean on = H.buff(com.shatteredpixel.shatteredpixeldungeon.items.artifacts.CloakOfShadows.cloakStealth.class) != null || H.invisible > 0;
			int hn = 0, close = INF; for (Mob m : mobs()) if (enemy(m) && hunting(m) && !(m instanceof NPC) && !(m instanceof Piranha)) { hn++; close = Math.min(close, walk(m.pos)); }
			if (ck != null && !on && !ck.cursed && ch instanceof Integer && (Integer) ch > 0 && hn > 0 && close <= 4 && (ck.isEquipped(H) || H.hasTalent(com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent.LIGHT_CLOAK)) && !declined(ck.getClass(), "STEALTH"))
				return use(ck, "STEALTH", -1, "cloak: a sure first blow");
		}
		if (seen) return ranged();
		// nothing in view
		for (Class<? extends Potion> c : Arrays.asList(PotionOfStrength.class, PotionOfExperience.class)) {
			Potion p = bag(c); if (p != null && p.isKnown() && !declined(c, Potion.AC_DRINK)) return use(p, Potion.AC_DRINK, -1, "drink");
		}
		TengusMask mask = bag(TengusMask.class);   // the subclass, at once
		if (mask != null && H.subClass == com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroSubClass.NONE && !declined(TengusMask.class, "WEAR")) return use(mask, "WEAR", -1, "subclass");
		ScrollOfUpgrade up = bag(ScrollOfUpgrade.class);
		if (scrollsOk && up != null && up.isKnown() && !declined(ScrollOfUpgrade.class, Scroll.AC_READ)) {
			Armor a = H.belongings.armor(); Item wpn = H.belongings.weapon();
			Item on = a != null && !(a.cursed && a.cursedKnown) ? a : wpn;
			if (STAFFUP && wpn instanceof MagesStaff && wpn.level() < 3) on = wpn;   // (the Mage: the staff first, strategy-reference 2.1; its level is shown)
			boolean behind = a == null || !a.levelKnown || a.level() * 3 < Dungeon.depth;
			if (on != null && (!upgradeHeld || behind)) { Target t = use(up, Scroll.AC_READ, -1, "upgrade"); t.on = on; return t; }
		}
		if (scrollsOk) {   // the known scrolls that sat unread: Remove Curse, Identify, Magic Mapping
			boolean cw = false; Item unid = null;
			for (Item i : H.belongings) {
				if (!(i instanceof EquipableItem || i instanceof Wand)) continue;
				boolean worn = i instanceof EquipableItem && ((EquipableItem) i).isEquipped(H);
				if (worn && i.visiblyCursed()) cw = true;
				if (!i.isIdentified() && (unid == null || worn)) unid = i;
			}
			ScrollOfRemoveCurse rc = bag(ScrollOfRemoveCurse.class);
			if (rc != null && rc.isKnown() && cw && !declined(ScrollOfRemoveCurse.class, Scroll.AC_READ)) return use(rc, Scroll.AC_READ, -1, "remove curse");
			ScrollOfIdentify id = bag(ScrollOfIdentify.class);
			if (id != null && id.isKnown() && unid != null && !declined(ScrollOfIdentify.class, Scroll.AC_READ)) { Target t = use(id, Scroll.AC_READ, -1, "identify"); t.on = unid; return t; }
			ScrollOfMagicMapping mm = bag(ScrollOfMagicMapping.class);
			if (mm != null && mm.isKnown() && (L.exit() <= 0 || !known[L.exit()]) && Actor.now() - floorT0 > 250 && !declined(ScrollOfMagicMapping.class, Scroll.AC_READ)) return use(mm, Scroll.AC_READ, -1, "magic mapping");
			// read unknown scrolls to learn them while it's safe, early on a floor (mapping is worth most then)
			if (H.HP * 5 >= H.HT * 4 && !starving() && (L.exit() <= 0 || !known[L.exit()]))
				for (Item i : H.belongings.backpack) if (i instanceof Scroll && !known(i) && !declined(i.getClass(), Scroll.AC_READ)) return use(i, Scroll.AC_READ, -1, "read to learn");
		}
		int ent = L.entrance(), ex = L.exit();
		if (H.HP * 5 < H.HT * 3 && L.plants.get(hp) == null && L.heaps.get(hp) == null && hp != ent && hp != ex && !declined(Sungrass.Seed.class, "PLANT")) {
			Sungrass.Seed s = bag(Sungrass.Seed.class); if (s != null) return use(s, "PLANT", -1, "sungrass");
		}
		boolean left = floorLeft();
		if (H.HP * 2 >= H.HT && !starving() && !left) {   // drink unknown potions to learn them at the floor's end, safely
			boolean scr = false, burn = false;
			for (Item i : H.belongings.backpack) scr |= i instanceof Scroll;
			for (int k = -1; k < 8; k++) { int n = k < 0 ? hp : hp + N8[k]; if (k < 0 || nb(hp, n)) burn |= L.flamable[n]; }
			PotionOfLiquidFlame lf = new PotionOfLiquidFlame();
			if (lf.isKnown() || ((water(hp) || !scr) && !burn))
				for (Item i : H.belongings.backpack) if (i instanceof Potion && !known(i) && !declined(i.getClass(), Potion.AC_DRINK)) return use(i, Potion.AC_DRINK, -1, "drink to learn");
		}
		for (Item i : H.belongings.backpack) {   // better gear (an unknown piece a tier better is worth wearing)
			if (!(i instanceof Armor || i instanceof MeleeWeapon) || i instanceof ClassArmor || i instanceof MagesStaff) continue;
			Item cur = i instanceof Armor ? H.belongings.armor() : H.belongings.weapon();
			if (cur != null && cur.cursed && cur.cursedKnown) continue;
			if ((i.cursedKnown && i.cursed) || (i.levelKnown && i.level() < 0)) continue;
			int str = i instanceof Armor ? ((Armor) i).STRReq(i.levelKnown ? i.level() : 0) : ((MeleeWeapon) i).STRReq(i.levelKnown ? i.level() : 0);   // (unknown level: the +0 figure, as shown)
			if (str > H.STR()) continue;
			int a = 2 * tier(i) + (i.levelKnown ? i.level() : -1), b = cur == null ? -1 : 2 * tier(cur) + (cur.levelKnown ? cur.level() : 0);
			if (cur instanceof ClassArmor || cur instanceof MagesStaff) continue;
			if (a > b && !declined(i.getClass(), EquipableItem.AC_EQUIP)) return use(i, EquipableItem.AC_EQUIP, -1, "better gear");
		}
		boolean bossHere = bossSeen >= 0 && Actor.now() - bossSeen < 100;   // (a boss seen on this floor lately)
		if (Dungeon.bossLevel() && bossHere && H.HP * 20 < H.HT * 19 && !starving() && !declined(null, REST_ACT)) { Target t = new Target(hp, WAIT, "rest before the boss"); t.action = REST_ACT; t.obj = -1; return t; }
		if (DEWREST && H.HP * 100 < H.HT * RESTPCT && ws != null && field(ws, "volume") instanceof Integer && (Integer) field(ws, "volume") > 5 && !declined(Waterskin.class, "DRINK")) return use(ws, "DRINK", -1, "hurt: dew, not food");   // (resting costs 10 turns of food a point)
		if (H.HP * 100 < H.HT * RESTPCT && !starving() && !declined(null, REST_ACT)) { Target t = new Target(hp, WAIT, "hurt: rest"); t.action = REST_ACT; t.obj = -1; return t; }
		return null;
	}
	private static boolean declinedAny(String a) { for (String s : declined) if (s.endsWith(":" + a)) return true; return false; }
	private static int tier(Item i) { return i instanceof Armor ? ((Armor) i).tier : i instanceof MeleeWeapon ? ((MeleeWeapon) i).tier : 0; }

	/** Has the wand a charge? Its count is shown only once its level is known; till then a guess: not zapped in 30 turns. */
	private static boolean charged(Wand w, Item holder) {
		if (w.levelKnown && w.curChargeKnown) return w.curCharges > 0;
		Float t = zapped.get(holder); return t == null || Actor.now() - t > 30 || Actor.now() < t;
	}
	private static final Map<Item, Float> zapped = new IdentityHashMap<>();

	private static boolean attackWand(Class<?> c, int ed) {
		if (c == null) return false;
		if (c == WandOfLightning.class || c == WandOfCorrosion.class) return ed >= 3;
		if (c == WandOfFireblast.class) return ed >= 2 && !L.flamable[hp];
		return c == WandOfMagicMissile.class || c == WandOfDisintegration.class || c == WandOfFrost.class || c == WandOfBlastWave.class || c == WandOfPrismaticLight.class;
	}

	public static final String SPELL_GL = "SPELL:GuidingLight";
	/** The Cleric's Guiding Light (a sure magic hit; the target, lit, can't dodge the next blow), whenever the tome can cast it. */
	private static Target guiding(Mob e) {
		if (!CLERIC) return null;
		com.shatteredpixel.shatteredpixeldungeon.items.artifacts.HolyTome tome = bag(com.shatteredpixel.shatteredpixeldungeon.items.artifacts.HolyTome.class);
		if (tome == null || tome.cursed) return null;
		try { if (!tome.canCast(H, com.shatteredpixel.shatteredpixeldungeon.actors.hero.spells.GuidingLight.INSTANCE)) return null; } catch (Throwable x) { return null; }
		if (new Ballistica(hp, e.pos, Ballistica.MAGIC_BOLT).collisionPos != e.pos) return null;
		return use(tome, SPELL_GL, e.pos, "guiding light");
	}

	/** From afar: what's in a quickslot first, the bow, charged wands, known harmful potions, missiles. */
	private static Target ranged() {
		Mob e = null; int ed = 99;
		for (Mob m : mobs()) { if (!enemy(m) || !hunting(m) || m instanceof NPC) continue; int d = distance(m.pos, hp); if (d < ed && line(hp, m.pos)) { ed = d; e = m; } }
		if (e == null) return null;
		Target gl = guiding(e); if (gl != null) return gl;
		if (ed < 2) {   // beside the hero: only the staff, and only an attack bolt
			MagesStaff st = ADJZAP ? bag(MagesStaff.class) : null;
			Object w = st == null ? null : field(st, "wand");
			if (w instanceof Wand && charged((Wand) w, st) && attackWand(w.getClass(), ed) && !(w instanceof WandOfLightning) && !(w instanceof WandOfFireblast) && !(w instanceof WandOfCorrosion)) return use(st, MagesStaff.AC_ZAP, e.pos, "zap point blank");
			return null;
		}
		if (e instanceof Shaman || e instanceof Warlock) return null;   // those want melee from round a corner, not a duel
		boolean shooter = e instanceof Tengu || e instanceof Eye || e instanceof Scorpio;
		for (int q = 0; q < Math.min(QuickSlot.SIZE, 6); q++) {
			Item it = Dungeon.quickslot.getItem(q);
			if (it == null || !H.belongings.contains(it)) continue;
			if (it instanceof MissileWeapon && !shooter && !declined(it.getClass(), Item.AC_THROW)) return use(it, Item.AC_THROW, e.pos, "throw");
			if (it instanceof Wand && charged((Wand) it, it) && attackWand(it.getClass(), ed)) return use(it, Wand.AC_ZAP, e.pos, "zap");
			if (it instanceof MagesStaff) { Object w = field(it, "wand"); if (w instanceof Wand && charged((Wand) w, it) && attackWand(w.getClass(), ed)) return use(it, MagesStaff.AC_ZAP, e.pos, "zap"); }
		}
		SpiritBow bow = bag(SpiritBow.class);
		if (bow != null && !shooter) return use(bow, SpiritBow.AC_SHOOT, e.pos, "shoot");
		MagesStaff staff = bag(MagesStaff.class);
		if (staff != null) { Object w = field(staff, "wand"); if (w instanceof Wand && charged((Wand) w, staff) && attackWand(w.getClass(), ed)) return use(staff, MagesStaff.AC_ZAP, e.pos, "zap"); }
		for (Item it : H.belongings.backpack) if (it instanceof Wand) { Wand w = (Wand) it;
			if (charged(w, w) && attackWand(w.getClass(), ed) && ((w.levelKnown && w.curChargeKnown ? w.curCharges >= w.maxCharges : charged(w, w)) || H.HP * 2 < H.HT)) return use(w, Wand.AC_ZAP, e.pos, "zap"); }
		for (Item it : H.belongings.backpack) {
			if (!(it instanceof Potion) || !known(it)) continue;
			if (((it instanceof PotionOfToxicGas || it instanceof PotionOfParalyticGas) && ed >= 4) || ((it instanceof PotionOfLiquidFlame || it instanceof PotionOfFrost) && ed >= 3 && !L.flamable[hp]))
				return use(it, Potion.AC_THROW, e.pos, "throw a potion");
		}
		if (ed >= 3 && !shooter) for (Item it : H.belongings.backpack) if (it instanceof MissileWeapon && !declined(it.getClass(), Item.AC_THROW)) return use(it, Item.AC_THROW, e.pos, "throw");
		return null;
	}

	/** The talks done (counted per NPC so a window closed without effect isn't offered for ever). */
	static void noteTalk(int cell) { Char c = Actor.findChar(cell); if (c instanceof NPC) talked.merge(c.id(), 1, Integer::sum); }
}
