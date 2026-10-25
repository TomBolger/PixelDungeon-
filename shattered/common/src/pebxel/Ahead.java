package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroAction;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.watabou.noosa.Game;

/**
 * Walks worked out ahead, like a video buffering (used by WatchSession, Server and RadioTest alike).
 *
 * Once the hero has set off on a walk the game itself carries on (a tap far off, the Assistant's GO, Select on an
 * item across the room), what happens next is fixed until the game stops him (a monster comes into view, an item is
 * seen, he arrives). The phone runs those turns ahead of time, faster than real time, as far as the radio can carry
 * them (and at most `max()` ahead), so the watch has the walk in hand before it shows it: a slow or held-up message
 * no longer stops the hero, and where he stops is known before he gets there.
 *  - The frames keep their game-clock stamps (so everything in them moves at its own pace), but the head of each
 *    message (C_LATEST) gives the clock as it would be without running ahead: the watch keeps its play-out delay.
 *  - The frames of the walk are marked (C_AHEAD: from its first frame's stamp to its last): the watch plays them
 *    from when it began the walk on the watch (predict.c), not a radio round trip and its play-out delay later.
 *  - When the walk is over, the game waits (no frames, no presses taken) until real time has caught up with it.
 *    A press made while the watch still shows the walk is taken then: the hero stops where the phone stopped him,
 *    and the watch, which only ever shows the phone's own frames, walks him exactly there. Nothing is undone.
 * Nothing seen along the way reaches the screen early: it is in the frame of the step that sees it.
 */
public final class Ahead {
	static int env(String k, int d) { try { String v = System.getenv(k); return v == null ? d : Integer.parseInt(v); } catch (Exception e) { return d; } }
	/** AHEAD=0: never run ahead (as before). */
	public static boolean ON = env("AHEAD", 1) != 0;
	/** Extra frames a loop round may run (each one's game and link work: a few ms on a phone). */
	public static int MAX_EXTRA = 6;
	/** Real ms a loop round may spend on extra frames (the loop's own frame needs the rest of its 33 ms). */
	public static int BUDGET_MS = 18;

	private final Link link;
	private final Pacer pacer;
	/** Game seconds the game is ahead of real time. */
	public float lead;
	private boolean walking;
	public long extra, held;   // (counts, for tests)

	public Ahead(Link link, Pacer pacer) { this.link = link; this.pacer = pacer; }

	/** How far ahead to run (game seconds): enough for a couple of the radio's round trips, within 0.3-0.8 s of real time. */
	public float max() {
		float rtt = pacer.rtt();   // real ms a message takes, lately
		float s = rtt <= 0 ? 0.6f : 2 * rtt / 1000 + 0.1f;
		return Math.max(0.3f, Math.min(0.8f, s)) * Game.pebxelSpeed;
	}

	/** Is the hero on a walk the game carries on by itself, step by step? (not a fight: an attack is not run ahead) */
	public static boolean committed() {
		if (!(Game.scene() instanceof GameScene) || Ui.topWindow() != null) return false;
		Hero h = Dungeon.hero;
		if (h == null || !h.isAlive() || h.ready || h.sprite == null) return false;
		HeroAction a = h.curAction;
		// (stopped by a press, Controls.button: the step under way still ends where it was going. Counted as over at
		// once, its last frames played at the usual delay, after the walk's: the hero stood still mid-step, then went on)
		if (a == null && h.sprite.isMoving && !h.resting) return true;
		if (a instanceof HeroAction.Move || a instanceof HeroAction.PickUp || a instanceof HeroAction.OpenChest) return true;
		// (the stairs: up to the step before them; the floor change itself happens in real time)
		return a instanceof HeroAction.LvlTransition && Dungeon.level != null && !Dungeon.level.adjacent(h.pos, a.dst) && h.pos != a.dst;
	}

	/** At the start of a loop round of dt game seconds: true while the game waits for real time (skip its frame). */
	public boolean hold(float dt) {
		// (a walk that ended since the last frame is over before any press is taken: else the next walk, set off at
		// once, counted as the same one, from its start)
		if (ON) mark();
		if (lead <= 0) return false;
		if (walking) return false;
		lead = Math.max(0, lead - dt);
		link.aheadMs = Math.round(lead * 1000);
		held++;
		return true;
	}

	/** After the round's own frame (its changes noted): marks the walk, and runs more of it while the radio has room. */
	public void after(Runnable frame, boolean sync, long roundStartNs) {
		if (!ON) return;
		// (the hero's next step is worked out between frames: on the round of the press, his first one, so the walk
		// shows as one from the start)
		boolean idle = settle(roundStartNs);
		mark();
		if (!sync || !walking) return;
		for (int i = 0; i < MAX_EXTRA; i++) {
			if (!idle || !committed() || lead >= max()) return;
			if (pacer.waiting() >= pacer.budget()) return;   // (more would only wait here, or be merged)
			if ((System.nanoTime() - roundStartNs) / 1_000_000 > BUDGET_MS) return;
			frame.run();
			settle(roundStartNs);   // (his next step set off in the frame the last one ended, as below)
			lead += Game.elapsed;
			link.aheadMs = Math.round(lead * 1000);
			pacer.frame(Game.elapsed, true);
			extra++;
			idle = settle(roundStartNs);
			mark();
		}
	}

	/**
	 * Before a round's own frame is noted (Pacer.frame): the game's actor thread is given the moment it
	 * needs to set the hero off on his next step (it does so as his last one ends, in that frame). Noted before
	 * that, the frame had him standing at the end of the step, and the next one had him a part of a step on: on the
	 * watch, a stop and a hop at every step.
	 */
	public void beforeSync(long roundStartNs) {
		if (!ON) return;
		settle(roundStartNs);   // (at once when nothing is going on)
		mark();                 // (on the round of the press: the walk marked in the very first message about it)
	}

	/** The walk's first and last frames, for the watch (Link.walkFrom / walkTo). */
	private void mark() {
		boolean c = committed();
		if (c && !walking) link.walkStart();
		else if (!c && walking) link.walkEnd();
		walking = c;
		pacer.walking = c;
	}

	/** Waits until the game's actor thread waits again (it works out the turns between frames); false if it takes too long. */
	private static boolean settle(long roundStartNs) {
		long until = roundStartNs + (BUDGET_MS + 10) * 1_000_000L;
		while (Actor.pebxelBusy) {
			if (System.nanoTime() > until) return false;
			Thread.yield();
		}
		return true;
	}

	/** The watch started over (a whole new screen): nothing of a walk is pending there. */
	public void reset() { walking = false; pacer.walking = false; }
}
