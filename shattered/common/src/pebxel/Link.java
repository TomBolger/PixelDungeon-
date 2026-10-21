package pebxel;

import com.badlogic.gdx.graphics.Pixmap;
import com.watabou.gltextures.SmartTexture;
import com.watabou.gltextures.TextureCache;
import com.watabou.noosa.*;
import com.watabou.utils.RectF;
import java.io.ByteArrayOutputStream;
import java.lang.reflect.Field;
import java.nio.charset.StandardCharsets;
import java.util.*;

/**
 * Mirrors Shattered PD's live scene (everything it would draw) onto the watch.
 *
 * The phone keeps a copy of what the watch currently shows. Each call to sync()
 * walks the scene in draw order and writes commands only for what changed. To keep
 * the radio quiet, the watch moves pictures and plays animations by itself: the
 * phone sends a position with a speed, or an animation with its frame rate, and
 * only corrects the watch when its prediction drifts.
 *
 * Units on the wire: watch pixels for the screen, and each camera's own units for
 * things it shows, times 8 (eighths of a pixel). Little-endian. See PROTOCOL.md.
 */
public final class Link {

	// ---- commands ----
	public static final int C_RESET = 1, C_CAM = 2, C_NEW = 3, C_SET = 4, C_DEL = 5, C_ORDER = 6,
			C_TILES = 7, C_DTEX = 8, C_MOVE = 9, C_ANIM = 10, C_TIME = 11, C_INPUT = 12, C_LATEST = 13, C_SOUND = 14, C_COMMIT = 15, C_PREDICT = 16, C_TILES2 = 17, C_ANIMTO = 18;
	// ---- picture kinds ----
	public static final int K_IMG = 1, K_RECT = 2, K_NINE = 3, K_TILES = 4, K_TEXT = 5, K_SKIN = 6, K_QUADS = 7, K_SHAPE = 8;
	public static final int F_FLIPH = 1, F_FLIPV = 2, F_ANIM = 16, F_FOG = 32;   // F_FOG: the fog of war (the watch draws it as Pebble PD does)
	public static final int NONE = 0xFFFF;
	/** The command format's version; the watch app sends its own in its hello. */
	public static final int PROTOCOL = 10;   // 10: tile maps in runs (C_TILES2), animation changes (C_ANIMTO)

	/** What to show on a watch whose app speaks another version of the commands. */
	public static byte[] wrongVersion(int watch) {
		return errorScreen("This watch app doesn't match\nthe phone app.\n\nPlease install the watch app\nthat came with this phone app.\n\n(watch " + watch + ", phone " + PROTOCOL + ")");
	}

	// moving parts of a picture: position x8, alpha 0-255, scale x256, angle in 1/64 degree, water offset x8
	static final int D_X = 0, D_Y = 1, D_A = 2, D_SX = 3, D_SY = 4, D_ANG = 5, D_OX = 6, D_OY = 7, N = 8;
	// how far the watch's guess may drift before the phone corrects it
	static final float[] TOL = {4, 4, 20, 8, 8, 64, 4, 4};

	private final ByteArrayOutputStream out = new ByteArrayOutputStream();

	/** What the watch currently believes about one picture. */
	private static final class Obj {
		int id, kind; byte[] stat; int[] tiles; int tilesKey, tilesDims;
		float[] dyn = new float[N], vel = new float[N];   // see D_* below
		float sentAt, until;                                 // speeds last until (0: no end)
		com.watabou.noosa.tweeners.Tweener tw;              // the movement/fade those speeds came from
		float[] prev; float prevAt;                          // last frame's real values, for speeds
		MovieClip.Animation anim; int frame; float timer;   // animation the watch is playing
	}

	private final IdentityHashMap<Gizmo, Obj> objs = new IdentityHashMap<>();
	static final boolean DEBUG_NEW = System.getenv("DEBUG_NEW") != null;
	static final boolean NO_UNTIL = System.getenv("NOUNTIL") != null;   // for comparing (LagTest)
	static final boolean NO_REUSE = System.getenv("NOREUSE") != null || Pacer.OLD;   // (RadioTest: as it was)
	/** Movements, fades and growths in progress this round, by picture: they say exactly where it is heading and when it arrives. */
	private final IdentityHashMap<Gizmo, com.watabou.noosa.tweeners.Tweener> tweens = new IdentityHashMap<>();
	private final HashMap<Integer, Integer> shadowPrev = new HashMap<>(), shadowNext = new HashMap<>();
	private int shadowHead = NONE;
	private final ArrayDeque<Integer> freeIds = new ArrayDeque<>();
	private int nextId = 0;

	private final IdentityHashMap<Camera, Integer> camIds = new IdentityHashMap<>();
	private final HashMap<Integer, byte[]> camState = new HashMap<>();

	private final IdentityHashMap<MovieClip.Animation, Integer> anims = new IdentityHashMap<>();
	private final HashMap<String, Integer> animsByContent = new HashMap<>();

	private final Assets assets;
	private final IdentityHashMap<SmartTexture, Integer> dynTex = new IdentityHashMap<>();
	private final HashMap<Integer, byte[]> dynPixels = new HashMap<>();
	private final HashMap<Integer, int[]> dynPal = new HashMap<>();
	private int nextDyn = 200;
	/** When each run-time picture was last drawn (sync rounds); unused ones are freed on the watch. */
	private final HashMap<Integer, Integer> dynUsed = new HashMap<>();
	private int rounds;
	static final int DYN_KEEP = 90;   // rounds unused before a run-time picture is let go (3 s)

	/** Pictures that are pure scenery and cost more radio time than they are worth (none by default). */
	public final Set<String> skip = new HashSet<>(Arrays.asList(
			"BusyIndicator",  // the phone's 'thinking' spinner: meaningless on the watch
			"CellSelector",   // an invisible touch area
			// each floor's background decoration (dripping sewer pipes, prison torches, cave sparkles, city
			// flames and smoke, hall fire streams) and water ripples: a steady stream of tiny pictures that
			// would hold up what matters on a slow radio link
			"Sink", "Torch", "Vein", "GreenFlame", "Smoke", "Stream", "Ripple",
			// the menus' drifting rubble background: busy behind text, and always moving
			"TitleBackground",
			// a new screen's fade from black (PixelScene.Fader): dithered on the watch it came out as a second of thinning
			// dots over the new floor, after the loading screen; the watch shows the new screen whole instead
			"Fader",
			// Shattered's own screen furniture: the watch shows Pebble PD's instead (pebxel.ui.WatchHud), which
			// presses these unseen ones
			"StatusPane", "MenuPane", "Toolbar", "GameLog", "AttackIndicator", "LootIndicator", "ResumeIndicator",
			"ActionIndicator", "RightClickMenu", "InventoryPane"
	));

	public final Set<String> unknown = new TreeSet<>();
	public long bytesTotal, syncs;
	private float now;

	public Link(Assets assets) { this.assets = assets; reset(); }

	/** Forget everything the watch had (it restarted, or a new connection). */
	public void reset() {
		objs.clear(); shadowPrev.clear(); shadowNext.clear(); shadowHead = NONE;
		freeIds.clear(); nextId = 0; camIds.clear(); camState.clear(); camObjs.clear(); dynTex.clear(); dynPixels.clear(); dynPal.clear(); dynUsed.clear(); dynShrink.clear(); nextDyn = 200;
		anims.clear(); animsByContent.clear(); animUsed.clear();
		out.reset(); marks.clear(); dropPending(); cmd(C_RESET); lastPlan = null;
		Sounds.take();   // (effects from while the watch was away are stale)
		for (int[] e : Sounds.state()) sound(e);
	}

	/** Compare the scene with the watch's copy; returns the commands to send (may be empty). */
	public byte[] sync() { return sync(Game.elapsed); }

	/** As sync(), when dt seconds passed since the last call (the link may run slower than the game). */
	public byte[] sync(float dt) {
		now += dt;
		for (int[] e : Sounds.take()) sound(e);
		Scene scene = Game.scene();
		if (scene != null) {
			List<Gizmo> order = new ArrayList<>();
			tweens.clear();
			walk(scene, order);
			// removed pictures
			Set<Gizmo> present = Collections.newSetFromMap(new IdentityHashMap<>());
			present.addAll(order);
			HashMap<Integer, ArrayDeque<Obj>> gone = new HashMap<>();
			for (Iterator<Map.Entry<Gizmo, Obj>> it = objs.entrySet().iterator(); it.hasNext(); ) {
				Map.Entry<Gizmo, Obj> e = it.next();
				if (!present.contains(e.getKey())) { Obj o = e.getValue(); gone.computeIfAbsent(Arrays.hashCode(o.stat), x -> new ArrayDeque<>()).add(o); it.remove(); }
			}
			// a new picture just like one that went (a window page or a menu built again on each change, every
			// picture of it new): the watch keeps the old one, moved if it must be, rather than getting it all again
			if (!gone.isEmpty() && !NO_REUSE) for (Gizmo g : order) {
				if (objs.containsKey(g)) continue;
				byte[] st = state(g, new float[N]);
				ArrayDeque<Obj> same = st == null ? null : gone.get(Arrays.hashCode(st));
				if (same == null) continue;
				for (Iterator<Obj> it = same.iterator(); it.hasNext(); ) {
					Obj o = it.next();
					if (!Arrays.equals(o.stat, st)) continue;
					it.remove(); o.prev = null; o.tw = null; objs.put(g, o); stat(g, "reused", 0);
					break;
				}
			}
			for (ArrayDeque<Obj> q : gone.values()) for (Obj o : q) { cmd(C_DEL); u16(o.id); unlink(o.id); freeIds.add(o.id); }
			// new, moved and changed pictures, in draw order
			int prev = NONE;
			for (Gizmo g : order) {
				Obj o = objs.get(g);
				int before = out.size();
				float[] dyn = new float[N];
				byte[] st = state(g, dyn);
				if (st == null) continue;
				if (o == null) {
					o = new Obj(); o.id = freeIds.isEmpty() ? nextId++ : freeIds.poll(); o.kind = st[0];
					objs.put(g, o);
					if (DEBUG_NEW && g instanceof RenderedText) System.out.println("NEWTEXT " + ((RenderedText) g).text() + " in " + g.parent.getClass().getName());
					cmd(C_NEW); u16(o.id); u16(prev); out.write(st, 0, st.length); o.stat = st;
					sendDyn(g, o, dyn); stat(g, "new", out.size() - before);
					link(o.id, prev);
				} else {
					if (!Objects.equals(shadowPrev.get(o.id), prev)) { cmd(C_ORDER); u16(o.id); u16(prev); unlink(o.id); link(o.id, prev); }
					if (!Arrays.equals(st, o.stat) && !(colourOnly(st, o.stat) && now - o.sentAt < COLOUR_GAP)) {
						if (System.getenv("WHY") != null && g.getClass().getSimpleName().equals(System.getenv("WHY"))) {
							StringBuilder w = new StringBuilder("why " + (st.length > 2 && st[0] != K_RECT ? assets.paths.get(Math.max(0, (st[2] & 255) - 1)) : "") + " bytes");
							for (int i = 0; i < Math.min(st.length, o.stat.length); i++) if (st[i] != o.stat[i]) w.append(" ").append(i).append(":").append(o.stat[i]).append("->").append(st[i]);
							System.out.println(w);
						}
						if (animOnly(st, o.stat)) {
							// a sprite changing animation (or turning): C_ANIMTO id, flags, animation, then the moving part
							cmd(C_ANIMTO); u16(o.id); u8(st[3]); out.write(st, 12, 2); o.stat = st;
							sendDyn(g, o, dyn); stat(g, "animation to", out.size() - before);
						} else {
							cmd(C_SET); u16(o.id); out.write(st, 0, st.length); o.stat = st;
							sendDyn(g, o, dyn); stat(g, "set", out.size() - before);
						}
					} else if (drifted(g, o, dyn) || (tweens.get(g) != null && tweens.get(g) != o.tw && !NO_UNTIL)) {
						// (a new movement starting is sent at once, not a frame later when it shows as a drift)
						cmd(C_MOVE); u16(o.id); sendDyn(g, o, dyn); stat(g, "move", out.size() - before);
					}
				}
				o.prev = dyn; o.prevAt = now;
				if (g instanceof Tilemap) { int b0 = out.size(); tiles((Tilemap) g, o); if (out.size() > b0) stat(g, "tiles", out.size() - b0); }
				prev = o.id;
			}
		}
		cameras(dt);
		freeDyn();
		// what the watch may do by itself on the next press (Controls.plan): sent when it changes, after this
		// frame's pictures (so the ones it names exist)
		if (planner != null) {
			byte[] pl;
			try { pl = planner.apply(this); } catch (Throwable t) { pl = null; }
			if (pl == null) pl = new byte[]{0};
			if (!Arrays.equals(pl, lastPlan)) { lastPlan = pl; cmd(C_PREDICT); out.write(pl, 0, pl.length); stat("press plan", pl.length + 1); }
		}
		if (out.size() == 0) { marks.clear(); return new byte[0]; }
		cmd(C_COMMIT);
		stat("frame stamps", 6);
		// stamped with the game clock: the watch plays each frame's changes at the same pace they happened
		int ms = Math.round(now * 1000);
		byte[] body = out.toByteArray(); out.reset();
		byte[] b = new byte[body.length + 5];
		b[0] = C_TIME; b[1] = (byte) ms; b[2] = (byte) (ms >> 8); b[3] = (byte) (ms >> 16); b[4] = (byte) (ms >> 24);
		System.arraycopy(body, 0, b, 5, body.length);
		lastMarks = new ArrayList<>(); lastMarks.add(0);
		for (int mk : marks) lastMarks.add(mk + 5);
		marks.clear();
		bytesTotal += b.length; syncs++;
		return b;
	}

	/** Two states of an animated sprite that differ only in its animation and flips (bytes 3, 12, 13). */
	private static boolean animOnly(byte[] a, byte[] b) {
		if (NO_REUSE || a.length != b.length || a[0] != b[0] || (a[0] != K_IMG && a[0] != K_SKIN)) return false;
		if ((a[3] & F_ANIM) == 0 || (b[3] & F_ANIM) == 0) return false;
		for (int i = 0; i < a.length; i++) if (a[i] != b[i] && i != 3 && i != 12 && i != 13) return false;
		return true;
	}

	/** A picture that only pulses in colour is updated at most this often (seconds). */
	static final float COLOUR_GAP = 0.25f;

	/** Where the 7 colour bytes start in each kind's unchanging part. */
	private static int colourAt(int kind) {
		switch (kind) { case K_IMG: case K_SKIN: return 20; case K_RECT: return 14; case K_NINE: return 24; case K_TILES: return 9; case K_TEXT: return 3; case K_QUADS: return -1; case K_SHAPE: return -1; }
		return -1;
	}

	private static boolean colourOnly(byte[] a, byte[] b) {
		if (a.length != b.length || a[0] != b[0]) return false;
		int c = colourAt(a[0]);
		if (c < 0) return false;
		for (int i = 0; i < a.length; i++) if (a[i] != b[i] && (i < c || i >= c + 7)) return false;
		return true;
	}

	/** A whole watch screen holding just a message (white text, top left), for reporting a crash. */
	public static byte[] errorScreen(String message) {
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		b.write(C_RESET);
		// camera 0: the whole screen, 1:1
		b.write(C_CAM); b.write(0);
		int[] cam = {0, 0, Screen.w * 8, Screen.h * 8, 256, 0, 0};
		for (int v : cam) { b.write(v & 255); b.write((v >> 8) & 255); }
		b.write(0);
		int id = 0, y = 2;
		for (String line : message.split("\n")) {
			while (!line.isEmpty() && y < 220 && id < 30) {
				String part = line.length() > 34 ? line.substring(0, 34) : line;
				line = line.substring(part.length());
				byte[] t = part.getBytes(StandardCharsets.UTF_8);
				b.write(C_NEW); b.write(id & 255); b.write(id >> 8); int after = id == 0 ? NONE : id - 1; b.write(after & 255); b.write(after >> 8);
				b.write(K_TEXT); b.write(0); b.write(0);        // camera 0, the smallest font
				b.write(255); b.write(255); b.write(255); b.write(0); b.write(0); b.write(0); b.write(0);
				b.write(t.length); b.write(t, 0, t.length);
				b.write(1); int x8 = 2 * 8, y8 = y * 8; b.write(x8 & 255); b.write(x8 >> 8); b.write(y8 & 255); b.write(y8 >> 8);
				id++; y += 9;
			}
		}
		b.write(C_COMMIT);
		return b.toByteArray();
	}

	/** For tests: each picture's id, class and real moving parts right now. */
	public Map<Integer, Object[]> truth() {
		Map<Integer, Object[]> m = new HashMap<>();
		for (Map.Entry<Gizmo, Obj> e : objs.entrySet()) {
			float[] dyn = new float[N];
			if (state(e.getKey(), dyn) != null) m.put(e.getValue().id, new Object[]{e.getKey().getClass().getSimpleName(), dyn});
		}
		camsThisRound.clear();
		return m;
	}

	// ---- prediction ----

	/** Does the watch's guess (last values plus speed x time) differ visibly from the real picture? */
	private boolean drifted(Gizmo g, Obj o, float[] dyn) {
		float tAll = now - o.sentAt, t = tAll;
		if (o.until > 0) t = Math.min(t, o.until);
		// specks of dust, blood and sparks live a moment: a few pixels off isn't worth radio time
		// so for the little icons over monsters (EmoIcon: a sleeper's "Zz" pulses in size, its place following in whole
		// pixels): exact, they cost several corrections a second, as long as a monster slept in view, and kept the
		// watch awake drawing
		float loose = g.parent instanceof com.watabou.noosa.particles.Emitter
				|| (g instanceof com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon && !NO_REUSE) ? 6 : 1;
		for (int i = 0; i < N; i++) {
			float guess = o.dyn[i] + o.vel[i] * (i >= D_OX ? tAll : t);   // (water drifts on for good: scene.c)
			if (i == D_A) guess = Math.max(0, Math.min(255, guess));
			boolean off = i == D_A ? Math.abs(guess - dyn[i]) > 85 * loose / 2 && Math.round(guess / 85) != Math.round(dyn[i] / 85) : Math.abs(wrap(g, i, guess - dyn[i])) > TOL[i] * loose;
			if (off) {
				if (System.getenv("DRIFT") != null && g.getClass().getName().replaceAll(".*\\.", "").equals(System.getenv("DRIFT")))
					System.out.printf("drift %s field %d guess %.0f real %.0f (sent %.0f vel %.0f t %.3f)%n", g.getClass().getSimpleName(), i, guess, dyn[i], o.dyn[i], o.vel[i], t);
				return true;
			}
		}
		if (o.anim != null && g instanceof MovieClip) {
			if (playAnim(o.anim, o.frame, o.timer, t) != NoosaAccess.curFrame((MovieClip) g)) return true;
		}
		return false;
	}

	/** Which frame an animation shows t seconds on (the watch steps the same way as MovieClip). */
	static int playAnim(MovieClip.Animation a, int frame, float timer, float t) {
		int n = a.frames.length;
		if (a.delay <= 0) return frame;
		int steps = (int) ((timer + t) / a.delay);
		if (a.looped) return (frame + steps) % n;
		return Math.min(n - 1, frame + steps);
	}

	/**
	 * Write the moving parts with their speeds. A mask byte says which are present:
	 * 1 always (x, y), 2 alpha, 4 scale, 8 angle, 16 x/y speed, 32 alpha speed, 64 scale speed, 128 angle speed.
	 * Water adds its offset and speed; an animation adds its frame and timer.
	 */
	private void sendDyn(Gizmo g, Obj o, float[] dyn) {
		float[] vel = new float[N];
		Visual v = (Visual) g;
		// guessing a speed from the last two places suits things moving in the dungeon; on the interface (a focus box
		// jumping from cell to cell) it sends the box sailing on past its place until corrected: there, no guess
		if (o.prev != null && now > o.prevAt && v.camera() == Camera.main && !JUMPS.contains(g)) {
			float dt = now - o.prevAt;
			for (int i = 0; i < N; i++) vel[i] = wrap(g, i, dyn[i] - o.prev[i]) / dt;
		}
		// damage numbers and the like (FloatingText) rise a tile a second while they last: say so from the start
		// (guessed from their last two places, it came out wrong when one appeared and after each push up the stack:
		// two or three corrections each, the biggest share of a fight's radio time)
		if (g.parent instanceof com.shatteredpixel.shatteredpixeldungeon.effects.FloatingText && !NO_REUSE)
			vel[D_Y] = -com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.SIZE * 8;
		// motion SPD integrates itself is exact; use it (acceleration is left to corrections)
		if (v.speed.x != 0 || v.speed.y != 0) { vel[D_X] = v.speed.x * 8; vel[D_Y] = v.speed.y * 8; }
		if (v.angularSpeed != 0) vel[D_ANG] = v.angularSpeed * 64;
		// a movement, fade or growth in progress: exactly to where it ends, stopping there (no overshoot
		// while the watch waits to hear that it stopped)
		float until = 0;
		com.watabou.noosa.tweeners.Tweener tw = NO_UNTIL ? null : tweens.get(g);
		if (tw == null && o.tw != null) {
			// a movement just ended: it stopped where it was going (any next one is its own movement)
			vel[D_X] = vel[D_Y] = vel[D_A] = vel[D_SX] = vel[D_SY] = 0;
		}
		o.tw = tw;
		if (tw != null) {
			float left = tw.interval - tw.elapsed;
			if (left > 0.005f) {
				until = left;
				if (tw instanceof com.watabou.noosa.tweeners.PosTweener) {
					com.watabou.utils.PointF e = ((com.watabou.noosa.tweeners.PosTweener) tw).end;
					vel[D_X] = (e.x - v.x) * 8 / left; vel[D_Y] = (e.y - v.y) * 8 / left;
				} else if (tw instanceof com.watabou.noosa.tweeners.AlphaTweener) {
					com.watabou.noosa.tweeners.AlphaTweener a = (com.watabou.noosa.tweeners.AlphaTweener) tw;
					vel[D_A] = Math.max(0, Math.min(255, (a.start + a.delta) * 255)) - dyn[D_A]; vel[D_A] /= left;
				} else if (tw instanceof com.watabou.noosa.tweeners.ScaleTweener) {
					com.watabou.utils.PointF e = ((com.watabou.noosa.tweeners.ScaleTweener) tw).end;
					vel[D_SX] = (e.x * 256 - dyn[D_SX]) / left; vel[D_SY] = (e.y * 256 - dyn[D_SY]) / left;
				}
			}
		}
		for (int i = 0; i < N; i++) if (Math.abs(vel[i]) > 30000) vel[i] = 0;  // a jump, not a movement
		// the little icons over monsters (a sleeper's "Zz") pulse and step in whole pixels: no speed is better than one
		// guessed from two frames (wrong a moment later, and corrected several times a second for as long as they show)
		if (g instanceof com.shatteredpixel.shatteredpixeldungeon.effects.EmoIcon && !NO_REUSE) Arrays.fill(vel, 0);
		// a guessed speed (no movement says where it ends) holds GUESS_HOLD at most: if no word comes (a message lost,
		// a slow radio), the watch stops it there rather than carrying it on (a pulsing icon grew to fill the screen)
		if (until == 0 && !NO_REUSE) for (int i = 0; i < D_OX; i++) if (r(vel[i]) != 0) { until = GUESS_HOLD; break; }
		// blinking (alpha jumping between levels) is not a fade: hold the new value
		if (o.prev != null && Math.abs(dyn[D_A] - o.prev[D_A]) > 60) vel[D_A] = 0;
		o.dyn = dyn.clone(); o.vel = vel; o.sentAt = now;
		int untilMs = until > 0 ? Math.max(1, Math.min(255, (int) Math.ceil(until * 100))) : 0;   // 10 ms units
		o.until = untilMs / 100f;
		if (DEBUG_NEW && g.getClass().getSimpleName().equals("HeroSprite")) System.out.printf("HERO at %.3f x %.1f vel %.0f until %d tween %s%n", now, dyn[D_X] / 8, vel[D_X] / 8, untilMs, tw == null ? "-" : tw.getClass().getSimpleName() + " " + tw.elapsed + "/" + tw.interval);
		int mask = 1;
		if (dyn[D_A] != 255) mask |= 2;
		if (dyn[D_SX] != 256 || dyn[D_SY] != 256) mask |= 4;
		if (dyn[D_ANG] != 0) mask |= 8;
		if (r(vel[D_X]) != 0 || r(vel[D_Y]) != 0) mask |= 16;
		if (r(vel[D_A]) != 0) mask |= 32;
		if (r(vel[D_SX]) != 0 || r(vel[D_SY]) != 0) mask |= 64 | 4;
		if (r(vel[D_ANG]) != 0) mask |= 128 | 8;
		u8(mask);
		s16o(r(dyn[D_X])); s16o(r(dyn[D_Y]));
		if ((mask & 2) != 0) u8(Math.max(0, Math.min(255, r(dyn[D_A]))));
		if ((mask & 4) != 0) { s16o(r(dyn[D_SX])); s16o(r(dyn[D_SY])); }
		if ((mask & 8) != 0) s16o(r(dyn[D_ANG]));
		if ((mask & 16) != 0) { s16o(r(vel[D_X])); s16o(r(vel[D_Y])); }
		if ((mask & 32) != 0) s16o(r(vel[D_A]));
		if ((mask & 64) != 0) { s16o(r(vel[D_SX])); s16o(r(vel[D_SY])); }
		if ((mask & 128) != 0) s16o(r(vel[D_ANG]));
		if ((mask & 0xF0) != 0) u8(untilMs);
		if (o.kind == K_SKIN) { s16o(r(dyn[D_OX])); s16o(r(dyn[D_OY])); s16o(r(vel[D_OX])); s16o(r(vel[D_OY])); }
		// what the watch will believe (values as rounded on the wire, missing ones at their defaults)
		for (int i = 0; i < N; i++) { o.dyn[i] = r(o.dyn[i]); o.vel[i] = r(o.vel[i]); }
		if ((mask & 16) == 0) { o.vel[D_X] = o.vel[D_Y] = 0; }
		if ((mask & 32) == 0) o.vel[D_A] = 0;
		if ((mask & 64) == 0) { o.vel[D_SX] = o.vel[D_SY] = 0; }
		if ((mask & 128) == 0) o.vel[D_ANG] = 0;
		if ((mask & 0xF0) == 0) o.until = 0;
		if ((o.kind == K_IMG || o.kind == K_SKIN) && (o.stat[3] & F_ANIM) != 0) {
			MovieClip mc = (MovieClip) g;
			o.anim = NoosaAccess.curAnim(mc); o.frame = NoosaAccess.curFrame(mc); o.timer = NoosaAccess.frameTimer(mc);
			u8(o.frame); u8(Math.min(255, Math.round(o.timer / o.anim.delay * 256)));
		} else o.anim = null;
	}

	private static int r(float f) { return Math.round(f); }
	static final float GUESS_HOLD = 1.5f;   // game seconds (one of real time)

	/** A difference in water's drift goes round its texture (SkinnedBlock wraps it there; so does the watch): a
	 *  wrap is no change (it was sent as two corrections, and a wild speed between them, every few seconds). */
	private static float wrap(Gizmo g, int i, float d) {
		if ((i != D_OX && i != D_OY) || !(g instanceof SkinnedBlock)) return d;
		SmartTexture t = ((SkinnedBlock) g).texture;
		float per = (i == D_OX ? t.width : t.height) * 8;
		return per > 0 ? d - per * Math.round(d / per) : d;
	}

	// ---- scene walk ----

	private void walk(Group grp, List<Gizmo> order) {
		for (Gizmo g : GroupAccess.members(grp)) {
			if (g == null || !g.exists || !g.visible) continue;
			if (!skip.isEmpty() && skip.contains(g.getClass().getSimpleName())) continue;
			if (g instanceof Group) { walk((Group) g, order); continue; }
			if (g instanceof Visual) order.add(g);
			else if (g instanceof com.watabou.noosa.tweeners.Tweener) {
				com.watabou.noosa.tweeners.Tweener t = (com.watabou.noosa.tweeners.Tweener) g;
				Gizmo v = t instanceof com.watabou.noosa.tweeners.PosTweener ? ((com.watabou.noosa.tweeners.PosTweener) t).visual
						: t instanceof com.watabou.noosa.tweeners.AlphaTweener ? ((com.watabou.noosa.tweeners.AlphaTweener) t).image
						: t instanceof com.watabou.noosa.tweeners.ScaleTweener ? ((com.watabou.noosa.tweeners.ScaleTweener) t).visual : null;
				if (v != null && t.elapsed >= 0 && t.elapsed < t.interval) {
					com.watabou.noosa.tweeners.Tweener had = tweens.get(v);
					// a movement wins over a fade; otherwise the one ending first
					if (had == null || (t instanceof com.watabou.noosa.tweeners.PosTweener && !(had instanceof com.watabou.noosa.tweeners.PosTweener))
							|| (t.getClass() == had.getClass() && t.interval - t.elapsed < had.interval - had.elapsed)) tweens.put(v, t);
				}
			}
		}
	}

	private static boolean inWindow(Gizmo g) {
		for (Group p = g.parent; p != null; p = p.parent) if (p instanceof com.shatteredpixel.shatteredpixeldungeon.ui.Window) return true;
		return false;
	}

	// ---- picture state ----

	private final ByteArrayOutputStream sb = new ByteArrayOutputStream();

	/** The unchanging part of how a picture looks (first byte = kind), or null to skip it; fills dyn. */
	private byte[] state(Gizmo g, float[] dyn) {
		Visual v = (Visual) g;
		Camera c = v.camera();
		if (c == null) return null;
		int cam = cam(c);
		sb.reset();
		float[] m = NoosaAccess.matrix(v);
		// text and tile maps are placed by their matrix; other pictures by position, origin, scale and angle
		dyn[D_X] = m[12] * 8; dyn[D_Y] = m[13] * 8; dyn[D_A] = Math.max(0, Math.min(255, v.am * 255)); dyn[D_SX] = 256; dyn[D_SY] = 256;
		boolean shaped = g instanceof NinePatch || (g instanceof Image && !(g instanceof RenderedText));
		if (shaped) {
			dyn[D_X] = v.x * 8; dyn[D_Y] = v.y * 8;
			dyn[D_SX] = v.scale.x * 256; dyn[D_SY] = v.scale.y * 256; dyn[D_ANG] = v.angle * 64;
		}
		if (g instanceof pebxel.ui.Shape) {
			pebxel.ui.Shape sh = (pebxel.ui.Shape) g;
			int rgb = sh.rgb, pc = 0xC0 | ((((rgb >> 16) & 255) + 42) / 85) << 4 | ((((rgb >> 8) & 255) + 42) / 85) << 2 | (((rgb & 255) + 42) / 85);
			s8(K_SHAPE); s8(cam); s8(sh.kind); s8(pc); s8(sh.r0); s8(sh.r1); s16(sh.b0); s16(sh.b1); s16(sh.p2); s16(sh.p3); s8(sh.slant);
			dyn[D_X] = v.x * 8; dyn[D_Y] = v.y * 8;
		} else if (g instanceof pebxel.ui.WatchText) {
			pebxel.ui.WatchText wt = (pebxel.ui.WatchText) g;
			if (wt.text().isEmpty()) return null;
			s8(K_TEXT); s8(cam); s8(wt.font | (wt.outline ? 0x80 : 0)); color(v);
			byte[] txt = wt.text().getBytes(StandardCharsets.UTF_8);
			int n = Math.min(255, txt.length); s8(n); sb.write(txt, 0, n);
		} else if (g instanceof RenderedText || g instanceof BitmapText) {
			String s = g instanceof RenderedText ? ((RenderedText) g).text() : ((BitmapText) g).text();
			if (s == null || s.isEmpty()) return null;
			int size = g instanceof RenderedText ? fontSize((RenderedText) g) : 6;
			// which watch font (WatchFonts), the same one Shattered measured this text with
			s8(K_TEXT); s8(cam); s8(WatchFonts.forSize(Math.round(size * m[5]))); color(v);
			byte[] txt = s.getBytes(StandardCharsets.UTF_8);
			int n = Math.min(255, txt.length); s8(n); sb.write(txt, 0, n);
		} else if (g instanceof Tilemap) {
			Tilemap t = (Tilemap) g;
			SmartTexture tx = NoosaAccess.texture(t);
			RectF r0 = NoosaAccess.film(t).get(0);
			s8(K_TILES); s8(cam); s8(tex(tx));
			s8(Math.round(NoosaAccess.cellW(t) * m[0])); s8(Math.round(NoosaAccess.cellH(t) * m[5]));
			s8(r0 == null ? 16 : Math.round(r0.width() * tx.width)); s8(r0 == null ? 16 : Math.round(r0.height() * tx.height));
			s8(NoosaAccess.mapWidth(t)); s8(NoosaAccess.mapHeight(t));
			color(v);
		} else if (g instanceof NinePatch) {
			NinePatch n = (NinePatch) g;
			s8(K_NINE); s8(cam); s8(tex(n.texture));
			s8((NoosaAccess.flipH(n) ? F_FLIPH : 0) | (NoosaAccess.flipV(n) ? F_FLIPV : 0));
			origin(v); size(n.width, n.height); texRect(NoosaAccess.outer(n), n.texture);
			for (int k : NoosaAccess.margins(n)) s8(k);
			color(v);
		} else if (g instanceof Image) {
			Image im = (Image) g;
			if (im.texture == null) return null;
			String key = key(im.texture);
			if (key != null && key.startsWith("1x1:")) {
				int col = (int) Long.parseLong(key.substring(4));
				float alpha = ((col >>> 24) / 255f) * v.alpha();
				// (not the shade over the game behind a window, which is on Shattered's interface camera: that one stays
				// see-through, a checker over the game, as in Pebble PD)
				if (alpha > 0.05f && alpha < 0.95f && inWindow(g) && v.camera() != com.shatteredpixel.shatteredpixeldungeon.scenes.PixelScene.uiCamera) {
					// see-through panels inside windows (item slots, list rows) dither into a noisy check on the
					// watch: make them solid, black or light grey by how light they are
					int lum = (((col >> 16) & 255) * 3 + ((col >> 8) & 255) * 5 + (col & 255) * 2) / 10;
					col = lum < 128 ? 0xFF000000 : 0xFFAAAAAA;
					dyn[D_A] = 255;
				}
				// a big stretch (a wide block made of a 1x1 one) doesn't fit the wire's 16 bits: send it as its size
				boolean fold = Math.abs(v.scale.x) > 100 || Math.abs(v.scale.y) > 100;
				s8(K_RECT); s8(cam);
				if (fold) {
					s16(Math.round(v.origin.x * v.scale.x * 8)); s16(Math.round(v.origin.y * v.scale.y * 8));
					size(im.width * v.scale.x, im.height * v.scale.y);
					dyn[D_SX] = dyn[D_SY] = 256;
				} else { origin(v); size(im.width, im.height); }
				s8((col >> 16) & 255); s8((col >> 8) & 255); s8(col & 255); s8(col >>> 24);
				color(v);
			} else {
				boolean skin = g instanceof SkinnedBlock;
				MovieClip.Animation a = g instanceof MovieClip ? NoosaAccess.curAnim((MovieClip) g) : null;
				boolean animated = a != null && !((MovieClip) g).paused && a.delay > 0 && a.frames.length > 1;
				// (a big glow may go smaller; the fog of war never: the watch reads it cell by cell)
				int tx = tex(im.texture, !(g instanceof com.shatteredpixel.shatteredpixeldungeon.tiles.FogOfWar) && !skin);
				s8(skin ? K_SKIN : K_IMG); s8(cam); s8(tx);
				s8((im.flipHorizontal ? F_FLIPH : 0) | (im.flipVertical ? F_FLIPV : 0) | (animated ? F_ANIM : 0)
					| (g instanceof com.shatteredpixel.shatteredpixeldungeon.tiles.FogOfWar ? F_FOG : 0));
				origin(v); size(im.width, im.height);
				if (animated) { s16(anim(a, tx, im.texture)); s16(0); s16(0); s16(0); }
				else texRect(NoosaAccess.frame(im), im.texture);
				if (skin) { float[] k = NoosaAccess.skin((SkinnedBlock) g); dyn[D_OX] = k[0] * 8; dyn[D_OY] = k[1] * 8; }
				color(v);
			}
		} else {
			// a picture that draws itself its own way: watch what it draws
			List<float[]> qs = capture(v);
			if (qs == null) { unknown.add(g.getClass().getName()); return null; }
			if (qs.isEmpty()) return null;
			SmartTexture t = byHandle((int) qs.get(0)[0]);
			if (t == null) { unknown.add(g.getClass().getName() + " (texture)"); return null; }
			int n = Math.min(40, qs.size());
			s8(K_QUADS); s8(cam); s8(tex(t)); s8(n);
			float sk = shrinkOf(t);
			for (int i = 0; i < n; i++) {
				float[] q = qs.get(i);
				s16(Math.round(q[1] / sk)); s16(Math.round(q[2] / sk)); s16(Math.round(q[3] / sk)); s16(Math.round(q[4] / sk));
				s16(Math.round(q[5] * 8)); s16(Math.round(q[6] * 8)); s16(Math.round((q[7] - q[5]) * 8)); s16(Math.round((q[8] - q[6]) * 8));
			}
			float[] q = qs.get(0);
			s8(c(q[9])); s8(c(q[10])); s8(c(q[11])); s8(c(q[13])); s8(c(q[14])); s8(c(q[15])); s8(c(q[16]));
			dyn[D_X] = 0; dyn[D_Y] = 0; dyn[D_A] = Math.max(0, Math.min(255, q[12] * 255));
		}
		return sb.toByteArray();
	}

	/** Runs a picture's own drawing on the software chip and returns its quads (null if it can't be watched). */
	private static List<float[]> capture(Visual v) {
		if (!(com.badlogic.gdx.Gdx.graphics instanceof WatchGraphics)) return null;
		SoftGL soft = ((WatchGraphics) com.badlogic.gdx.Gdx.graphics).soft;
		if (soft == null) return null;
		soft.capture = new ArrayList<>();
		try { v.draw(); } catch (Exception e) { soft.capture = null; return null; }
		List<float[]> r = soft.capture;
		soft.capture = null;
		return r;
	}

	private SmartTexture byHandle(int handle) {
		key(null);
		synchronized (TextureCache.class) {
			for (SmartTexture t : cacheMap.values()) if (t.id == handle) return t;
		}
		return null;
	}

	/** Unscaled size in the camera's units, x8. */
	private void size(float w, float h) { s16(Math.round(w * 8)); s16(Math.round(h * 8)); }

	/** The point it scales and turns around, x8. */
	private void origin(Visual v) { s16(Math.round(v.origin.x * 8)); s16(Math.round(v.origin.y * 8)); }

	private void texRect(RectF r, SmartTexture t) {
		float k = shrinkOf(t);
		s16(Math.round(r.left * t.width / k)); s16(Math.round(r.top * t.height / k));
		s16(Math.round(r.width() * t.width / k)); s16(Math.round(r.height() * t.height / k));
	}

	/** Colour multiply (r, g, b) and add (r, g, b, a); alpha multiply moves separately. */
	private void color(Visual v) {
		// "inverted" (dark on a light scroll) text: the watch's scroll is black like its windows, so it stays white
		if (v.rm < 0 || v.gm < 0 || v.bm < 0) { s8(255); s8(255); s8(255); s8(0); s8(0); s8(0); s8(c(v.aa)); return; }
		s8(c(v.rm)); s8(c(v.gm)); s8(c(v.bm)); s8(c(v.ra)); s8(c(v.ga)); s8(c(v.ba)); s8(c(v.aa));
	}

	/** Colour amounts to 16 steps: the watch shows only 4 levels per channel, finer pulses would be wasted radio time. */
	private static int c(float f) { return Math.max(0, Math.min(255, Math.round(f * 15)) * 17); }

	private static Field sizeField;
	private static int fontSize(RenderedText t) {
		try {
			if (sizeField == null) { sizeField = RenderedText.class.getDeclaredField("size"); sizeField.setAccessible(true); }
			return sizeField.getInt(t);
		} catch (Exception e) { return 12; }
	}

	// ---- animations ----

	private int anim(MovieClip.Animation a, int tx, SmartTexture t) {
		Integer id = anims.get(a);
		if (id != null) { animUsed.put(id, rounds); return id; }
		// many monsters share the same animation: send each distinct one once
		StringBuilder k = new StringBuilder().append(tx).append(':').append(a.delay).append(':').append(a.looped);
		for (RectF r : a.frames) k.append(':').append(r.left).append(',').append(r.top).append(',').append(r.right).append(',').append(r.bottom);
		id = animsByContent.get(k.toString());
		if (id != null) { anims.put(a, id); return id; }
		java.util.Set<Integer> taken = new java.util.HashSet<>(animsByContent.values());
		id = 0; while (taken.contains(id)) id++;   // the lowest free number (old levels' animations are let go: freeAnims)
		animsByContent.put(k.toString(), id);
		anims.put(a, id); animUsed.put(id, rounds);
		int b0 = out.size();
		cmd(C_ANIM); u16(id); u8(tx); u8(a.frames.length); u16(Math.round(a.delay * 1000)); u8(a.looped ? 1 : 0);
		float sk = shrinkOf(t);
		for (RectF r : a.frames) {
			u16(Math.round(r.left * t.width / sk)); u16(Math.round(r.top * t.height / sk));
			u16(Math.round(r.width() * t.width / sk)); u16(Math.round(r.height() * t.height / sk));
		}
		stat("animation", out.size() - b0);
		return id;
	}

	// ---- tiles ----

	private void tiles(Tilemap t, Obj o) {
		int[] d = NoosaAccess.data(t);
		int n = d.length;
		int[] cur = new int[n];
		boolean[] clear = clearTiles(t);
		for (int i = 0; i < n; i++) cur[i] = (d[i] < 0 || (d[i] < clear.length && clear[d[i]]) || !NoosaAccess.needsRender(t, i)) ? NONE : d[i];
		if (Pacer.OLD) {
			if (o.tiles == null || o.tiles.length != n) { o.tiles = new int[n]; Arrays.fill(o.tiles, -2); }
			oldTiles(cur, o); return;
		}
		// a new map on the watch starts with every cell empty (scene.c): only the others are sent. (Sending every
		// cell made a floor change 20-40 KB, most of it empty cells, several seconds of radio.) The same for a map
		// of a new size; a map whose pictures changed: everything again, as the watch may have kept or cleared it
		int key = (o.stat[2] & 255) | (o.stat[5] & 255) << 8 | (o.stat[6] & 255) << 16, dims = (o.stat[7] & 255) | (o.stat[8] & 255) << 8;
		if (o.tiles == null || o.tilesKey != key || o.tilesDims != dims || o.tiles.length != n) {
			boolean empty = o.tiles == null || o.tilesKey == key;
			o.tiles = new int[n]; Arrays.fill(o.tiles, empty ? NONE : -2); o.tilesKey = key; o.tilesDims = dims;
		}
		int i = 0;
		while (i < n) {
			if (cur[i] == o.tiles[i]) { i++; continue; }
			// a stretch of changed cells (short unchanged gaps in it cost less than a new command)
			int j = i, last = i;
			while (j < n && j - i < TILE_RUN) { if (cur[j] != o.tiles[j]) last = j; else if (j - last > 6) break; j++; }
			j = last + 1;
			boolean wide = false;
			for (int k = i; k < j; k++) if (cur[k] != NONE && cur[k] > 254) wide = true;
			// C_TILES2: id, first cell, count, 1 if two bytes a cell (else one: 255 = empty), then runs: a byte
			// h < 128: h + 1 cells follow; h >= 128: one cell for h - 126 in a row
			cmd(C_TILES2); u16(o.id); u16(i); u16(j - i); u8(wide ? 1 : 0);
			int k = i;
			while (k < j) {
				int r = 1;
				while (k + r < j && cur[k + r] == cur[k] && r < 129) r++;
				if (r >= 2) { u8(126 + r); cell(cur[k], wide); k += r; continue; }
				int s0 = k, m = 0;
				while (k < j && m < 128 && !(k + 1 < j && cur[k + 1] == cur[k])) { k++; m++; }
				if (m == 0) { k++; m = 1; }
				u8(m - 1); for (int q = s0; q < s0 + m; q++) cell(cur[q], wide);
			}
			for (int q = i; q < j; q++) o.tiles[q] = cur[q];
			i = j;
		}
	}

	/** As tile maps went before protocol 10 (RadioTest's PACE_OLD, for comparing): every cell, two bytes each. */
	private void oldTiles(int[] cur, Obj o) {
		for (int i = 0, n = cur.length; i < n; ) {
			if (cur[i] == o.tiles[i]) { i++; continue; }
			int j = i;
			while (j < n && j - i < 120 && (cur[j] != o.tiles[j] || (j + 1 < n && cur[j + 1] != o.tiles[j + 1]))) j++;
			cmd(C_TILES); u16(o.id); u16(i); u8(j - i);
			for (int k = i; k < j; k++) { u16(cur[k]); o.tiles[k] = cur[k]; }
			i = j;
		}
	}

	static final int TILE_RUN = 512;
	private void cell(int v, boolean wide) { if (wide) u16(v); else u8(v == NONE ? 255 : v); }
	/** Tiles of a map's picture that show nothing on the watch (all see-through: WallOcclusionTilemap's most
	 *  common tile is an empty one): sent as no tile. */
	private final IdentityHashMap<SmartTexture, boolean[]> clearTiles = new IdentityHashMap<>();
	private boolean[] clearTiles(Tilemap t) {
		SmartTexture tx = NoosaAccess.texture(t);
		boolean[] c = clearTiles.get(tx);
		if (c != null) return c;
		TextureFilm film = NoosaAccess.film(t);
		Pixmap p = tx.bitmap;
		int cells = 0;
		if (p != null) for (int i = 0; i < 4096 && film.get(i) != null; i++) cells = i + 1;
		c = new boolean[cells];
		for (int i = 0; i < cells; i++) {
			RectF r = film.get(i);
			int x0 = Math.round(r.left * tx.width), y0 = Math.round(r.top * tx.height), x1 = Math.round(r.right * tx.width), y1 = Math.round(r.bottom * tx.height);
			boolean see = x1 > x0 && y1 > y0;
			for (int y = y0; y < y1 && see; y++) for (int x = x0; x < x1 && see; x++) if ((p.getPixel(x, y) & 255) >= 43) see = false;   // (alpha to the watch's 4 levels: 0)
			c[i] = see;
		}
		clearTiles.put(tx, c);
		return c;
	}

	// ---- cameras ----

	/** What the watch believes about a camera. */
	private static final class CamObj { byte[] fixed; float sx, sy; }
	private final HashMap<Integer, CamObj> camObjs = new HashMap<>();
	private final LinkedHashMap<Integer, Camera> camsThisRound = new LinkedHashMap<>();
	static final float TOL_CAM = 8;   // one watch pixel

	private int cam(Camera c) {
		Integer id = camIds.get(c);
		if (id == null) {
			// the lowest number free (cameras come and go with windows; the watch keeps only MAX_CAM)
			java.util.Collection<Integer> used = camIds.values();
			id = 0; while (used.contains(id)) id++;
			camIds.put(c, id); camObjs.remove(id);
		}
		camsThisRound.put(id, c);
		return id;
	}

	/**
	 * After the pictures: cameras. The game camera eases after the hero, so the watch runs SPD's own
	 * follow rule (Camera.update) against its copy of the followed picture, and the phone only corrects
	 * the scroll when the watch's result would be off by more than a pixel.
	 * C_CAM: id, x, y, w, h (watch px x8), zoom (x256), scroll x, y (x8), mode (0 still, 1 follow, 2 pan to a point),
	 *        then for 1: picture id, offset x, y (x8); for 2: point x, y (x8); for 1 and 2: intensity (x16), dead zone (x255).
	 */
	private void cameras(float dt) {
		for (Map.Entry<Integer, Camera> e : camsThisRound.entrySet()) {
			int id = e.getKey(); Camera c = e.getValue();
			CamObj k = camObjs.computeIfAbsent(id, x -> new CamObj());
			ByteArrayOutputStream b = new ByteArrayOutputStream();
			int[] v = {c.x * 4, c.y * 4, Math.round(c.width * c.zoom * 4), Math.round(c.height * c.zoom * 4), Math.round(c.zoom * 128)};
			for (int x : v) { b.write(x & 255); b.write((x >> 8) & 255); }
			float[] pan = NoosaAccess.pan(c);
			Visual f = NoosaAccess.followTarget(c);
			Obj fo = f != null && f.visible ? objs.get(f) : null;
			int mode = pan[2] <= 0 ? 0 : fo != null ? 1 : f == null ? 2 : 0;
			b.write(mode | (c == Camera.main ? 0x80 : 0));   // 0x80: the map (touch drags pan it)
			if (mode == 1) { b.write(fo.id & 255); b.write(fo.id >> 8); w16(b, c.centerOffset.x * 8); w16(b, c.centerOffset.y * 8); }
			if (mode == 2) { w16(b, pan[0] * 8); w16(b, pan[1] * 8); }
			if (mode != 0) { b.write(Math.min(255, Math.round(pan[2] * 16))); b.write(Math.min(255, Math.round(pan[3] * 255))); }
			byte[] fixed = b.toByteArray();
			// the watch's result: the same easing step on its copy
			if (k.fixed != null && mode != 0) {
				float tx = mode == 1 ? (f.x + f.width() / 2 + c.centerOffset.x) : pan[0];
				float ty = mode == 1 ? (f.y + f.height() / 2 + c.centerOffset.y) : pan[1];
				float dx = dead(tx - (k.sx / 8 + c.width / 2f), mode == 1 ? c.width * pan[3] / 2 : 0);
				float dy = dead(ty - (k.sy / 8 + c.height / 2f), mode == 1 ? c.height * pan[3] / 2 : 0);
				float s = Math.min(1f, dt * pan[2]);
				k.sx += dx * s * 8; k.sy += dy * s * 8;
			}
			float sx = c.scroll.x * 8, sy = c.scroll.y * 8;
			if (!Arrays.equals(fixed, k.fixed) || Math.abs(k.sx - sx) > TOL_CAM || Math.abs(k.sy - sy) > TOL_CAM) {
				k.fixed = fixed; k.sx = Math.round(sx); k.sy = Math.round(sy);
				int b0 = out.size();
				cmd(C_CAM); u8(id); out.write(fixed, 0, 10); s16o(Math.round(sx)); s16o(Math.round(sy)); out.write(fixed, 10, fixed.length - 10);
				stats.computeIfAbsent("camera", x -> new long[2]); stats.get("camera")[0]++; stats.get("camera")[1] += out.size() - b0;
			}
		}
		// cameras nothing was drawn with this time (a closed window's) give up their numbers
		camIds.values().removeIf(id -> !camsThisRound.containsKey(id));
		camsThisRound.clear();
	}

	private static float dead(float p, float dz) { return p > dz ? p - dz : p < -dz ? p + dz : 0; }
	private static void w16(ByteArrayOutputStream b, float f) { int v = Math.round(f); b.write(v & 255); b.write((v >> 8) & 255); }

	// ---- textures ----

	private static HashMap<Object, SmartTexture> cacheMap;
	private final IdentityHashMap<SmartTexture, String> keys = new IdentityHashMap<>();

	@SuppressWarnings("unchecked")
	private String key(SmartTexture t) {
		if (t == null) {
			try { if (cacheMap == null) { Field f = TextureCache.class.getDeclaredField("all"); f.setAccessible(true); cacheMap = (HashMap<Object, SmartTexture>) f.get(null); } }
			catch (Exception e) { throw new RuntimeException(e); }
			return null;
		}
		String k = keys.get(t);
		if (k != null || keys.containsKey(t)) return k;
		try {
			if (cacheMap == null) { Field f = TextureCache.class.getDeclaredField("all"); f.setAccessible(true); cacheMap = (HashMap<Object, SmartTexture>) f.get(null); }
			synchronized (TextureCache.class) {
				for (Map.Entry<Object, SmartTexture> e : cacheMap.entrySet())
					if (e.getValue() == t) { k = e.getKey() instanceof String ? (String) e.getKey() : null; break; }
			}
		} catch (Exception e) { throw new RuntimeException(e); }
		keys.put(t, k);
		return k;
	}

	private int tex(SmartTexture t) { return tex(t, false); }

	/**
	 * Run-time pictures bigger than this many pixels are sent shrunk, if they may be (a glow: ShieldHalo and the other
	 * Halos are 257 x 257, 33 KB on the watch at 4 bits a pixel, which with a big floor's tile maps ran its memory out).
	 * Texture coordinates for them are sent in the shrunk picture's pixels (shrinkOf).
	 */
	static final int DYN_BIG = 64 * 64;
	private final HashMap<Integer, Integer> dynShrink = new HashMap<>();

	/** How much a picture is shrunk on the watch (1: not). */
	private int shrinkOf(SmartTexture t) { Integer d = dynTex.get(t); return d == null ? 1 : dynShrink.getOrDefault(d, 1); }

	private int tex(SmartTexture t, boolean shrinkable) {
		String k = key(t);
		Integer id = k == null ? null : assets.id(k);
		if (id != null) return id;
		// made at run time (fog of war, glows): send its pixels when they change
		Integer d = dynTex.get(t);
		if (d == null) {   // the lowest number not in use (a level's fog is a new texture each time: numbers are recycled)
			java.util.Set<Integer> taken = new java.util.HashSet<>(dynTex.values());   // (an IdentityHashMap's containsValue compares Integers by identity)
			d = 200; while (taken.contains(d) && d < 254) d++;
			dynTex.put(t, d); dynPixels.remove(d); dynPal.remove(d);
			Pixmap p0 = t.bitmap;
			int sk = 1;
			if (shrinkable && p0 != null) while ((p0.getWidth() / sk) * (p0.getHeight() / sk) > DYN_BIG) sk++;
			dynShrink.put(d, sk);
		}
		dynUsed.put(d, rounds);
		Pixmap p = t.bitmap;
		if (p != null) {
			int sk = dynShrink.getOrDefault(d, 1);
			byte[] px = sk > 1 ? shrunk(p, sk) : Assets.pebble(p);
			int pw = (p.getWidth() + sk - 1) / sk, ph = (p.getHeight() + sk - 1) / sk;
			byte[] old = dynPixels.get(d);
			if (!Arrays.equals(px, old)) {
				dynPixels.put(d, px);
				// few colours (fog of war: black at four strengths) are sent and kept as a palette and small indices
				TreeSet<Integer> cols = new TreeSet<>();
				for (byte v : px) { cols.add(v & 255); if (cols.size() > 16) break; }
				int[] pal = cols.size() <= 16 ? cols.stream().mapToInt(Integer::intValue).toArray() : new int[0];
				boolean samePal = Arrays.equals(pal, dynPal.get(d)) && old != null && old.length == px.length;
				dynPal.put(d, pal);
				byte[] idx = px;
				if (pal.length > 0) {
					idx = new byte[px.length];
					for (int i = 0; i < px.length; i++) idx[i] = (byte) Arrays.binarySearch(pal, px[i] & 255);
				}
				for (int at = 0; at < px.length; at += DTEX_PIECE) {
					int end = Math.min(px.length, at + DTEX_PIECE);
					if (samePal && Arrays.equals(Arrays.copyOfRange(px, at, end), Arrays.copyOfRange(old, at, end))) continue;
					int b0 = out.size();
					cmd(C_DTEX); u8(d); u16(pw); u16(ph); u16(at / DTEX_PIECE);
					u8(pal.length); for (int c : pal) u8(c);
					rle(Arrays.copyOfRange(idx, at, end));
					stat("run-time picture " + (k == null ? t.getClass().getSimpleName() : k), out.size() - b0);
				}
			}
		}
		return d;
	}

	/** A picture made smaller k times, each pixel the average of a k x k block, in Pebble colours. */
	private static byte[] shrunk(Pixmap p, int k) {
		int w = p.getWidth(), h = p.getHeight(), sw = (w + k - 1) / k, sh = (h + k - 1) / k;
		byte[] out = new byte[sw * sh];
		for (int y = 0; y < sh; y++) for (int x = 0; x < sw; x++) {
			int r = 0, g = 0, b = 0, a = 0, n = 0;
			for (int j = y * k; j < Math.min(h, y * k + k); j++) for (int i = x * k; i < Math.min(w, x * k + k); i++) {
				int c = p.getPixel(i, j), ca = c & 255;   // RGBA8888
				r += ((c >>> 24) & 255) * ca; g += ((c >> 16) & 255) * ca; b += ((c >> 8) & 255) * ca; a += ca; n++;
			}
			out[y * sw + x] = a == 0 ? 0 : Assets.pebble(r / a, g / a, b / a, a / n);
		}
		return out;
	}

	/** Run-time pictures nothing has drawn for a while (an old level's fog of war): freed on the watch (C_DTEX with
	 *  no size), so their memory doesn't run out after a few level changes (the next level's fog then failed to fit
	 *  and the water under it showed through). */
	/** Animations nothing has played for a while (an earlier level's monsters): freed on the watch (C_ANIM with no
	 *  frames) and their numbers reused, so a long game neither fills the watch's memory nor runs out of numbers. */
	private final HashMap<Integer, Integer> animUsed = new HashMap<>();
	static final int ANIM_KEEP = 600;   // 20 s
	private void freeAnims() {
		if (rounds % 30 != 0) return;
		java.util.Set<Integer> gone = new java.util.HashSet<>();
		for (Map.Entry<Integer, Integer> e : animUsed.entrySet()) if (rounds - e.getValue() >= ANIM_KEEP) gone.add(e.getKey());
		if (gone.isEmpty()) return;
		animsByContent.values().removeIf(gone::contains);
		anims.values().removeIf(gone::contains);
		for (int id : gone) { animUsed.remove(id); cmd(C_ANIM); u16(id); u8(0); u8(0); u16(0); u8(0); }
	}

	/** The screen (scene) being shown, and the round it came. */
	private Object lastScene; private int sceneRound;

	private void freeDyn() {
		freeAnims();
		Scene sc = Game.scene();
		if (sc != lastScene) { lastScene = sc; sceneRound = rounds; }   // (pictures drawn this round have last == rounds)
		rounds++;
		Iterator<Map.Entry<SmartTexture, Integer>> it = dynTex.entrySet().iterator();
		while (it.hasNext()) {
			int d = it.next().getValue();
			Integer last = dynUsed.get(d);
			// (an earlier screen's, a floor left behind say, go at once: on a floor change the old fog and glows
			// were still held on the watch while the new floor's tile maps came in)
			boolean oldScreen = last != null && last < sceneRound && rounds > sceneRound;
			if (last != null && rounds - last < DYN_KEEP && !oldScreen) continue;
			it.remove(); dynUsed.remove(d); dynPixels.remove(d); dynPal.remove(d); dynShrink.remove(d);
			cmd(C_DTEX); u8(d); u16(0); u16(0); u16(0); u8(0); u16(0);
		}
	}

	/**
	 * Fog over remembered places: one alpha level lighter than on a phone. The watch dithers see-through
	 * pixels, and Shattered's dimming at full strength hides remembered rooms in 64 colours.
	 */
	/** Run-time pictures go in pieces of this many pixels.
	 *  C_DTEX: id, w, h, piece number, palette size n (0 = plain colour bytes), n colours, length, run-length data. */
	static final int DTEX_PIECE = 1024;

	/** Pebble colours, run-length coded: count byte (1-128 literal, 129-255 = repeat next byte count-127 times). */
	private void rle(byte[] px) {
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		int i = 0;
		while (i < px.length) {
			int r = 1;
			while (i + r < px.length && px[i + r] == px[i] && r < 128) r++;
			if (r >= 3) { b.write(127 + r); b.write(px[i]); i += r; continue; }
			int s = i, n = 0;
			while (i < px.length && n < 128) {
				int rr = 1; while (i + rr < px.length && px[i + rr] == px[i] && rr < 3) rr++;
				if (rr >= 3) break;
				i++; n++;
			}
			b.write(n); b.write(px, s, n);
		}
		byte[] bb = b.toByteArray();
		u16(bb.length); out.write(bb, 0, bb.length);
	}

	// ---- draw-order shadow (the watch's linked list) ----

	private void link(int id, int after) {
		int nx = after == NONE ? shadowHead : shadowNext.getOrDefault(after, NONE);
		shadowPrev.put(id, after); shadowNext.put(id, nx);
		if (after == NONE) shadowHead = id; else shadowNext.put(after, id);
		if (nx != NONE) shadowPrev.put(nx, id);
	}

	private void unlink(int id) {
		int p = shadowPrev.getOrDefault(id, NONE), n = shadowNext.getOrDefault(id, NONE);
		if (p == NONE) shadowHead = n; else shadowNext.put(p, n);
		if (n != NONE) shadowPrev.put(n, p);
		shadowPrev.remove(id); shadowNext.remove(id);
	}

	// ---- stats ----

	public final TreeMap<String, long[]> stats = new TreeMap<>();
	private static final boolean STAT_PARENTS = System.getenv("STAT_PARENTS") != null;
	private void stat(String k, int bytes) { stats.computeIfAbsent(k, x -> new long[2]); stats.get(k)[0]++; stats.get(k)[1] += bytes; }
	private void stat(Gizmo g, String what, int bytes) {
		String k = what + " " + g.getClass().getName().replaceAll(".*\\.", "");
		if (STAT_PARENTS && g.parent != null) k += " in " + g.parent.getClass().getName().replaceAll(".*\\.", "") + (g.parent.parent != null ? " < " + g.parent.parent.getClass().getName().replaceAll(".*\\.", "") : "");
		stats.computeIfAbsent(k, x -> new long[2]); stats.get(k)[0]++; stats.get(k)[1] += bytes;
	}

	// ---- bytes ----

	private void u8(int v) { out.write(v); }

	/** Starts a command (remembers where, so messages can be cut between commands). */
	private void cmd(int c) { marks.add(out.size()); out.write(c); }
	private final List<Integer> marks = new ArrayList<>();
	private List<Integer> lastMarks = new ArrayList<>();

	// ---- the outbox: every frame's changes, kept until the watch can take more ----
	private final ByteArrayOutputStream pend = new ByteArrayOutputStream();
	private final List<Integer> pendMarks = new ArrayList<>();

	/** Call every game frame: notes this frame's changes (with their time) for the watch. */
	private int pendNewest;

	public void frame(float dt) {
		byte[] b = sync(dt);
		remember();
		if (b.length == 0) return;
		pendNewest = Math.round(now * 1000);
		int base = pend.size();
		for (int mk : lastMarks) pendMarks.add(base + mk);
		pend.write(b, 0, b.length);
	}

	public boolean pending() { return pend.size() > 0; }
	public int pendingBytes() { return pend.size(); }

	// ---- touch: placing a tap on what the watch was showing ----
	private static final class Snap { int ms; Object top; Map<Integer, float[]> cams = new HashMap<>(); }
	private final ArrayDeque<Snap> hist = new ArrayDeque<>();

	private static Object top() {
		com.shatteredpixel.shatteredpixeldungeon.ui.Window w = Ui.topWindow();
		return w != null ? w : Game.scene();
	}

	/** Cameras as they are this frame (kept 3 seconds). */
	private void remember() {
		Snap sn = new Snap(); sn.ms = Math.round(now * 1000); sn.top = top();
		for (Map.Entry<Camera, Integer> e : camIds.entrySet()) {
			Camera c = e.getKey();
			sn.cams.put(e.getValue(), new float[]{c.x, c.y, c.zoom, c.scroll.x, c.scroll.y});
		}
		hist.add(sn);
		while (!hist.isEmpty() && sn.ms - hist.peek().ms > 3000) hist.poll();
	}

	/**
	 * A point the watch touched while showing its frame `stamp` (phone pixels), moved to where that same spot
	 * is now (the map may have scrolled since). Null: the window or screen it was aimed at has gone.
	 */
	public int[] remap(int camId, int stamp, int px, int py) {
		if (camId == 0xFF) return new int[]{px, py};
		Camera cur = null;
		for (Map.Entry<Camera, Integer> e : camIds.entrySet()) if (e.getValue() == camId) cur = e.getKey();
		if (cur == null) return null;
		Snap then = null;
		for (Snap sn : hist) if (sn.ms <= stamp) then = sn;
		if (then == null) return new int[]{px, py};
		if (then.top != top()) return null;
		float[] c = then.cams.get(camId);
		if (c == null) return null;
		float wx = (px - c[0]) / c[2] + c[3], wy = (py - c[1]) / c[2] + c[4];
		return new int[]{Math.round((wx - cur.scroll.x) * cur.zoom + cur.x), Math.round((wy - cur.scroll.y) * cur.zoom + cur.y)};
	}

	/** Pictures that jump from place to place (the target marker): never given a guessed speed. */
	public static final Set<Gizmo> JUMPS = Collections.newSetFromMap(new java.util.WeakHashMap<>());

	/** The plan for the watch's next press (Controls.plan), and the last one sent. */
	public java.util.function.Function<Link, byte[]> planner;
	private byte[] lastPlan;

	/** A picture's number on the watch (-1: not there). */
	public int idOf(Gizmo g) { Obj o = g == null ? null : objs.get(g); return o == null ? -1 : o.id; }

	/** For tests: a camera's number on the watch (0xFF: none), and the game clock (ms). */
	public int camIdOf(Camera c) { Integer i = camIds.get(c); return i == null ? 0xFF : i; }
	public int nowMs() { return Math.round(now * 1000); }

	/** A sound for the watch (Sounds): kind (0 effect, 1 music, 2 settings), value. */
	private void sound(int[] e) { cmd(C_SOUND); out.write(e[0]); out.write(e[1]); }

	/** Tell the watch a touch has been applied (in this frame's changes). */
	public void ack(int seq) { cmd(C_INPUT); out.write(seq & 255); }

	/** Everything noted since the last call, cut into messages of at most max bytes. */
	public List<byte[]> take(int max) {
		byte[] all = pend.toByteArray(); pend.reset();
		lastMarks = new ArrayList<>(pendMarks); pendMarks.clear();
		if (all.length == 0) return new ArrayList<>();
		// the first message starts with the newest frame's time (C_LATEST): the watch plays the newest at once and the
		// older ones as already past (their moves carry on from where they would be by now), not a round trip late.
		// The rest just carry on (each marked newest, they arrived ever later than their time: on a slow radio the
		// watch then held everything after a big change, a level, seconds late)
		List<byte[]> out = new ArrayList<>();
		int ms = pendNewest;
		for (byte[] pk : packets(all, max - 5)) {
			if (!out.isEmpty() && !Pacer.OLD) { out.add(pk); continue; }
			byte[] b = new byte[pk.length + 5];
			b[0] = (byte) C_LATEST; b[1] = (byte) ms; b[2] = (byte) (ms >> 8); b[3] = (byte) (ms >> 16); b[4] = (byte) (ms >> 24);
			System.arraycopy(pk, 0, b, 5, pk.length);
			out.add(b);
		}
		return out;
	}

	/** Forget what was waiting (the watch starts over). */
	public void dropPending() { pend.reset(); pendMarks.clear(); }

	/** The last sync's bytes cut into messages of at most max bytes, each holding whole commands. */
	public List<byte[]> packets(byte[] all, int max) {
		List<byte[]> res = new ArrayList<>();
		int start = 0;
		List<Integer> m = new ArrayList<>(lastMarks); m.add(all.length);
		for (int i = 1; i < m.size(); i++) {
			if (m.get(i) - start > max && m.get(i - 1) > start) { res.add(Arrays.copyOfRange(all, start, m.get(i - 1))); start = m.get(i - 1); }
			if (m.get(i) - start > max) throw new IllegalStateException("command too big: " + (m.get(i) - start));
		}
		if (start < all.length) res.add(Arrays.copyOfRange(all, start, all.length));
		return res;
	}
	private void u16(int v) { out.write(v & 255); out.write((v >> 8) & 255); }
	private void s16o(int v) { v = Math.max(-32768, Math.min(32767, v)); out.write(v & 255); out.write((v >> 8) & 255); }
	private void s8(int v) { sb.write(v); }
	private void s16(int v) { sb.write(v & 255); sb.write((v >> 8) & 255); }
}
