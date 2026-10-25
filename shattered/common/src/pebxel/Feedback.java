package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Char;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.sprites.CharSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.GameLog;
import com.watabou.noosa.Game;
import com.watabou.noosa.MovieClip;
import java.lang.reflect.Field;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * What the watch should feel and light up for, as Pebble PD: a short buzz when the hero is hurt, a long one when he
 * dies (main.c ui_fx FX_HURT, after_turn); the backlight (if the watch's settings have it on) when something happens:
 * hurt, a monster comes into view or attacks, a walk or a rest ends, gold picked up, a new floor, death, a message in
 * the log (pick-ups say so there). Noted every frame (Controls.frame); Link sends it with the frame (C_EVENT), so the
 * watch buzzes as it shows the blow. The watch adds its own buttons and touches, and keeps the light to once a second.
 */
public final class Feedback {
	public static final int HURT = 1, DEAD = 2, LIGHT = 4;
	private static int pending;
	private static Hero hero; private static int hp, gold, depth = -1, foes; private static boolean alive;
	private static float busy, lightAgo = 99;
	private static Object lastLog;
	private static Set<Integer> attacking = new HashSet<>(), attackingNow = new HashSet<>();

	/** The flags since the last message (cleared). */
	public static synchronized int take() { int e = pending; pending = 0; return e; }

	private static synchronized void note(int e) {
		if ((e & ~LIGHT) == 0 && lightAgo < 0.5f) return;   // (the light alone at most twice a second: the watch keeps it to once)
		if ((e & LIGHT) != 0) lightAgo = 0;
		pending |= e;
	}

	/** Every game frame. */
	public static void frame() {
		lightAgo += Game.elapsed;
		Hero h = Dungeon.hero;
		if (h == null || !(Game.scene() instanceof GameScene) || Dungeon.level == null) return;
		int ev = 0;
		if (h != hero) { hero = h; hp = h.HP; alive = h.isAlive(); gold = Dungeon.gold; foes = -1; lastLog = log(); busy = 0; }
		if (h.HP < hp) ev |= HURT | LIGHT;
		if (alive && !h.isAlive()) ev |= DEAD | LIGHT;
		hp = h.HP; alive = h.isAlive();
		if (Dungeon.depth != depth) { if (depth >= 0) ev |= LIGHT; depth = Dungeon.depth; foes = -1; }
		if (Dungeon.gold > gold) ev |= LIGHT;
		gold = Dungeon.gold;
		// a walk or a rest over (busy half a second or more: not each blow of a fight)
		if (!h.ready) busy += Game.elapsed;
		else { if (busy >= 0.5f) ev |= LIGHT; busy = 0; }
		// monsters in sight: one more than before; one starting a blow or a zap
		int n = 0;
		attackingNow.clear();
		for (Mob m : Dungeon.level.mobs.toArray(new Mob[0])) {
			if (m.alignment != Char.Alignment.ENEMY || m.invisible > 0 || m.pos < 0 || m.pos >= Dungeon.level.length() || !Dungeon.level.heroFOV[m.pos]) continue;
			n++;
			if (m.sprite != null && blow(m.sprite)) { attackingNow.add(m.id()); if (!attacking.contains(m.id())) ev |= LIGHT; }
		}
		Set<Integer> t = attacking; attacking = attackingNow; attackingNow = t;
		if (foes >= 0 && n > foes) ev |= LIGHT;
		foes = n;
		Object l = log();
		if (l != null && l != lastLog) ev |= LIGHT;
		lastLog = l;
		if (ev != 0) note(ev);
	}

	private static Field attackF, zapF, entriesF;
	private static boolean blow(CharSprite s) {
		try {
			if (attackF == null) { attackF = CharSprite.class.getDeclaredField("attack"); attackF.setAccessible(true); zapF = CharSprite.class.getDeclaredField("zap"); zapF.setAccessible(true); }
			MovieClip.Animation a = com.watabou.noosa.NoosaAccess.curAnim(s);
			return a != null && (a == attackF.get(s) || a == zapF.get(s));
		} catch (Exception e) { return false; }
	}

	/** The newest entry of Shattered's game log (the object itself: the same words again are a new one). */
	@SuppressWarnings("unchecked")
	private static Object log() {
		try {
			if (entriesF == null) { entriesF = GameLog.class.getDeclaredField("entries"); entriesF.setAccessible(true); }
			List<Object> es = (List<Object>) entriesF.get(null);
			return es.isEmpty() ? null : es.get(es.size() - 1);
		} catch (Exception e) { return null; }
	}

	/** The watch started over: nothing stale. */
	public static synchronized void reset() { pending = 0; }
}
