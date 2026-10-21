package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.Statistics;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.*;
import com.shatteredpixel.shatteredpixeldungeon.journal.Catalog;
import java.util.*;

/**
 * What a bot game got round to (for the coverage audit): quests started and finished, bosses beaten, the subclass
 * and armour ability, item kinds seen and used (the game's own catalogue counts, plus the bot's tries), windows and
 * scenes met, and problems. One line "COVER k=v;k=v..." at the end, for sim/cover.py to add up over many games.
 */
public final class Coverage {
	private Coverage() { }

	static final Map<String, Mob> bosses = new LinkedHashMap<>();
	static final Set<String> beaten = new LinkedHashSet<>(), scenes = new TreeSet<>(), problems = new LinkedHashSet<>();
	static final Set<Integer> floors = new TreeSet<>();

	static int keyMax, keyAt;
	static String watch = "";
	/** Shattered's key listeners (each button adds one; destroy removes it): a count that only grows is a leak. */
	static int keyListeners() {
		try {
			java.lang.reflect.Field f = com.watabou.input.KeyEvent.class.getDeclaredField("keySignal"); f.setAccessible(true);
			Object sig = f.get(null); java.lang.reflect.Field l = com.watabou.utils.Signal.class.getDeclaredField("listeners"); l.setAccessible(true);
			return ((java.util.List<?>) l.get(sig)).size();
		} catch (Throwable e) { return -1; }
	}

	static void newGame() { bosses.clear(); beaten.clear(); scenes.clear(); problems.clear(); floors.clear(); }

	static void problem(String p) { if (problems.size() < 40) problems.add(p.replace(';', ',').replace('=', ':')); }

	static void scene(Object s) { if (s != null) scenes.add(s.getClass().getSimpleName()); }

	/** Every decision: the bosses in sight (and those gone dead since), what's carried. */
	static void look() {
		if (Dungeon.level == null) return;
		floors.add(Dungeon.depth);
		if (++keyAt % 200 == 0) { int k = keyListeners(); if (k > keyMax) { keyMax = k; if (k > 3000 && AssistBot.v) System.out.println("  KEY LISTENERS " + k); } }
		for (Mob m : Dungeon.level.mobs) if (Char.hasProp(m, Char.Property.BOSS) && m.isAlive()) bosses.putIfAbsent(m.getClass().getSimpleName() + "@" + Dungeon.depth, m);
		for (Map.Entry<String, Mob> e : bosses.entrySet()) if (!e.getValue().isAlive()) beaten.add(e.getKey().substring(0, e.getKey().indexOf('@')));
		BotItems.look();
	}

	static boolean flag(Class<?> npc, String field) {
		try { Class<?> q = Class.forName(npc.getName() + "$Quest"); java.lang.reflect.Field f = q.getDeclaredField(field); f.setAccessible(true); return f.getBoolean(null); }
		catch (Throwable e) { return false; }
	}
	static Object obj(Class<?> npc, String field) {
		try { Class<?> q = Class.forName(npc.getName() + "$Quest"); java.lang.reflect.Field f = q.getDeclaredField(field); f.setAccessible(true); return f.get(null); }
		catch (Throwable e) { return null; }
	}

	static void report(String end) {
		look();
		StringBuilder sb = new StringBuilder("COVER ");
		kv(sb, "end", end.startsWith("WIN") ? "win" : end.split(" ")[0]);
		kv(sb, "class", Dungeon.hero == null ? "?" : Dungeon.hero.heroClass.name());
		kv(sb, "depth", Integer.toString(Dungeon.depth));
		kv(sb, "maxdepth", Integer.toString(Statistics.deepestFloor));
		kv(sb, "amulet", Boolean.toString(Statistics.amuletObtained));
		kv(sb, "subclass", Dungeon.hero == null ? "" : Dungeon.hero.subClass.name());
		kv(sb, "ability", Dungeon.hero == null || Dungeon.hero.armorAbility == null ? "" : Dungeon.hero.armorAbility.getClass().getSimpleName());
		String q = "Ghost:" + st(flag(Ghost.class, "given"), Ghost.Quest.completed())
				+ ",Wandmaker:" + st(flag(Wandmaker.class, "given"), flag(Wandmaker.class, "given") && obj(Wandmaker.class, "wand1") == null)
				+ ",Blacksmith:" + st(Blacksmith.Quest.given(), Blacksmith.Quest.completed())
				+ ",Imp:" + st(Imp.Quest.given(), Imp.Quest.isCompleted());
		kv(sb, "quests", q);
		kv(sb, "bosses", String.join(",", beaten));
		kv(sb, "bossesseen", String.join(",", bosses.keySet()));
		// the game's catalogue: per kind of thing, those seen and those used
		StringBuilder seen = new StringBuilder(), used = new StringBuilder();
		for (Catalog c : Catalog.values()) for (Class<?> cl : c.items()) {
			String nm = cl.getName().substring(cl.getName().lastIndexOf('.') + 1).replace('$', '.');
			if (Catalog.isSeen(cl)) seen.append(c.name()).append('.').append(nm).append(',');
			if (Catalog.useCount(cl) > 0) used.append(c.name()).append('.').append(nm).append(',');
		}
		kv(sb, "seen", seen.toString());
		kv(sb, "used", used.toString());
		kv(sb, "tried", BotItems.summary().replace(' ', ','));
		kv(sb, "carried", String.join(",", BotItems.carried));
		kv(sb, "windows", BotWindows.summary().replace(" ", "").replace("{", "").replace("}", ""));
		kv(sb, "scenes", String.join(",", scenes));
		kv(sb, "floors", floors.toString().replace(" ", ""));
		if (!watch.isEmpty()) kv(sb, "watch", watch.replace(' ', ':'));
		kv(sb, "keylisteners", Integer.toString(Math.max(keyMax, keyListeners())));
		kv(sb, "problems", String.join(",", problems));
		System.out.println(sb);
	}

	static String st(boolean given, boolean done) { return done ? "done" : given ? "started" : "no"; }

	static void kv(StringBuilder sb, String k, String v) { sb.append(k).append('=').append(v.replace(';', ',')).append(';'); }
}
