package pebxel;

import java.awt.*;
import java.awt.image.BufferedImage;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.util.*;
import javax.imageio.ImageIO;

/**
 * A stand-in for the watch app, in Java: applies the link's commands, moves and
 * animates pictures between updates the way the watch will, and draws the 200x228
 * screen. Used to test the link before (and alongside) the C version.
 */
public final class WatchSim {
	/** What the watch was told to play (kind:value), for tests. */
	public final java.util.List<String> sounds = new java.util.ArrayList<>();
	/** The plans the watch was sent (Controls.plan), as text, for tests. */
	public final java.util.List<String> plans = new java.util.ArrayList<>();
	private String readPlan() {
		if (u8() == 0) return "none";
		StringBuilder sb = new StringBuilder();
		int mid = u16(), hid = u16(), step = u8() * 4, n = u8(), cur = u8();
		sb.append("marker ").append(mid).append(" hero ").append(hid).append(" step ").append(step).append(" cur ").append(cur);
		for (int i = 0; i < n; i++) {
			int x = (short) u16(), y = (short) u16(), k = u8();
			sb.append(" | ").append(x / 8f).append(",").append(y / 8f);
			if (k > 0) { sb.append(" walk "); for (int j = 0; j < k; j++) sb.append(u8()); }
		}
		return sb.toString();
	}
	public static int W = Screen.w, H = Screen.h;
	public final int[] fb = new int[Screen.w * Screen.h];

	private static final class Cam { int x, y, w, h, zoom, mode, follow; float sx, sy, ofx, ofy, px, py, intensity, dead; }
	private static final class Anim { int tex, delayMs; boolean looped; int[] rects; }
	private static final class Obj {
		int kind; byte[] st; int[] tiles; int prev = Link.NONE, next = Link.NONE;
		float[] dyn = new float[Link.N], vel = new float[Link.N]; float at, until;
		int frame; float timer; // animation
		Emit em;   // K_EMIT: its particles' maker (as particles.c)
	}

	private final Cam[] cams = new Cam[16];   // (as many as the watch keeps: MAX_CAM)
	private final HashMap<Integer, Obj> objs = new HashMap<>();
	private final HashMap<Integer, Anim> anims = new HashMap<>();
	private int head = Link.NONE;
	private final HashMap<Integer, int[]> dyn = new HashMap<>(); // [w, h, px...]
	private final HashMap<Integer, Integer> dynBits = new HashMap<>();   // bits a pixel on the watch
	private final Assets assets;
	private final HashMap<Integer, BufferedImage> texCache = new HashMap<>();
	/** Pictures already converted to Pebble colours by tools/convert_art.py. */
	public File art = new File(System.getProperty("pebxel.art", "../../../../../build/art"));
	public float now;
	/** Part of a frame has been applied, not yet its end (C_COMMIT): the watch keeps showing the last whole one. */
	public boolean partial;

	public WatchSim(File root, Assets assets) { this.assets = assets; W = Screen.w; H = Screen.h; }

	/** The watch's clock moves on (it keeps moving and animating pictures by itself). */
	public void advance(float dt) {
		// SPD's camera easing (Camera.update), run in small steps like the game's frames
		for (float left = dt; left > 1e-6f; left -= 1 / 30f) {
			float step = Math.min(left, 1 / 30f);
			now += step;
			if (!q.isEmpty()) pump();
			for (Cam c : cams) if (c != null && c.mode != 0) follow(c, step);
		}
	}

	private void follow(Cam c, float dt) {
		float cw = c.w / (c.zoom / 32f), ch = c.h / (c.zoom / 32f); // camera size in its own units
		float tx = c.px, ty = c.py, dz = 0;
		if (c.mode == 1) {
			Obj o = objs.get(c.follow); if (o == null) return;
			float w = (o.kind == Link.K_IMG || o.kind == Link.K_SKIN) ? s16(o.st, 8) / 8f : 0, h = (o.kind == Link.K_IMG || o.kind == Link.K_SKIN) ? s16(o.st, 10) / 8f : 0;
			tx = d(o, Link.D_X) / 8 + w * d(o, Link.D_SX) / 256 / 2 + c.ofx;
			ty = d(o, Link.D_Y) / 8 + h * d(o, Link.D_SY) / 256 / 2 + c.ofy;
			dz = c.dead;
		}
		float dx = dead(tx - (c.sx / 8 + cw / 2), cw * dz / 2), dy = dead(ty - (c.sy / 8 + ch / 2), ch * dz / 2);
		float s = Math.min(1f, dt * c.intensity);
		c.sx += dx * s * 8; c.sy += dy * s * 8;
	}

	private static float dead(float p, float dz) { return p > dz ? p - dz : p < -dz ? p + dz : 0; }

	// ---------------- applying commands ----------------

	private byte[] b; private int p; private float applyAt;
	private int u8() { return b[p++] & 255; }
	private int u16() { int v = (b[p] & 255) | ((b[p + 1] & 255) << 8); p += 2; return v; }
	private int s16() { return (short) u16(); }
	private static int s16(byte[] a, int i) { return (short) ((a[i] & 255) | ((a[i + 1] & 255) << 8)); }

	public void apply(byte[] cmds) { apply(cmds, 0, false); }

	// ---- play-out, as on the watch (main.c): messages wait and each stamped frame plays at its time ----
	private final ArrayDeque<int[]> qPos = new ArrayDeque<>();
	private final ArrayDeque<byte[]> q = new ArrayDeque<>();
	private float off, base, jit, offAt, curAt; private boolean haveOff;
	static final float LATE_MAX = 0.450f, DRIFT = 16, SNAP = 0.9f;
	static final boolean OLD_OFF = Pacer.OLD;   // (RadioTest: as the watch app was)
	static final float MARGIN = 0.040f;
	private static long stamp(byte[] d, int i) { return (d[i + 1] & 255L) | (d[i + 2] & 255L) << 8 | (d[i + 3] & 255L) << 16 | (d[i + 4] & 255L) << 24; }

	/** A message arrives from the phone now. */
	public void receive(byte[] msg) {
		if (msg.length >= 5 && msg[0] == Link.C_LATEST && OLD_OFF) {   // (as the watch was: each message set the delay)
			float lat = now - stamp(msg, 0) / 1000f, want = lat + MARGIN;
			if (!haveOff || lat < base) base = lat; else base += (lat - base) / 32;
			if (!haveOff || want > off) off = want; else off -= (off - want) / 3;
			haveOff = true;
		} else if (msg.length >= 5 && msg[0] == Link.C_LATEST) {   // (as main.c enqueue: a steady delay)
			float lat = now - stamp(msg, 0) / 1000f;
			int t5 = msg.length >= 10 && msg[5] == Link.C_READY ? 10 : 5;
			float old = msg.length >= t5 + 5 && msg[t5] == Link.C_TIME ? now - stamp(msg, t5) / 1000f : lat;
			if (!haveOff || lat < base) base = lat; else base += (lat - base) / 32;
			float need = Math.max(0, Math.min(LATE_MAX, old - base));
			if (!haveOff) jit = need; else if (need > jit) jit += (need - jit) / 4; else jit -= (jit - need) / 32;
			if (!haveOff) { off = base + MARGIN + jit; offAt = now; }
			haveOff = true;
			offMove();
			if (System.getenv("OFFS") != null) System.out.printf("recv at %.3f stamp %.3f off %.3f len %d%n", now, stamp(msg, 0) / 1000f, off, msg.length);
		}
		q.add(msg); qPos.add(new int[]{0});
		pump();
	}

	/** The newest phone frame (its stamp, ms) played so far. */
	public long played = -1;
	/** Messages that didn't fit what the watch had (the picture would be wrong until a full resend). */
	public int errors;

	/** As main.c off_move: the delay towards where the messages want it, at most 1/DRIFT of the time passing. */
	private void offMove() {
		if (OLD_OFF || !haveOff) return;
		float want = base + MARGIN + jit, d = want - off, step = (now - offAt) / DRIFT;
		offAt = now;
		if (Math.abs(d) > SNAP) off = want; else off += Math.max(-step, Math.min(step, d));
	}

	private void pump() {
		offMove();
		while (!q.isEmpty()) {
			byte[] m = q.peek(); int[] pos = qPos.peek();
			if (pos[0] < m.length && m[pos[0]] == Link.C_TIME) {
				float due = stamp(m, pos[0]) / 1000f + off;
				if (due > now + 1e-4f) return;
				curAt = OLD_OFF || now - due <= LATE_MAX ? due : now - LATE_MAX;
				played = Math.max(played, stamp(m, pos[0]));
			}
			int used;
			// (a command for a picture the watch doesn't have: the watch drops the rest of that message; count it)
			try { used = apply(m, pos[0], true); } catch (RuntimeException e) { errors++; used = m.length; }
			pos[0] = used > pos[0] ? used : m.length;
			if (pos[0] >= m.length) { q.poll(); qPos.poll(); }
		}
	}

	/** Applies commands from start; with stopAtTime, only up to the next frame's stamp. Returns where it stopped. */
	private int apply(byte[] cmds, int from, boolean stopAtTime) {
		b = cmds; p = from;
		float at = stopAtTime ? curAt : now;
		applyAt = at;
		while (p < b.length) {
			if (stopAtTime && b[p] == Link.C_TIME && p > from) return p;
			int c = u8();
			// (as the watch: a frame only half here isn't drawn; the last whole one stays on screen)
			if (c == Link.C_COMMIT) partial = false; else if (c != Link.C_TIME && c != Link.C_LATEST) partial = true;
			switch (c) {
				case Link.C_TIME: p += 4; break;
				case Link.C_LATEST: p += 4; break;
				case Link.C_RESET: objs.clear(); head = Link.NONE; dyn.clear(); anims.clear(); Arrays.fill(cams, null); parts.clear(); Arrays.fill(recipes, null); fogTex = -1; break;
				case Link.C_CAM: {
					int id = u8(); Cam k = new Cam(); k.x = s16(); k.y = s16(); k.w = s16(); k.h = s16(); k.zoom = u16(); k.sx = s16(); k.sy = s16();
					k.mode = u8() & 0x7F;
					if (k.mode == 1) { k.follow = u16(); k.ofx = s16() / 8f; k.ofy = s16() / 8f; }
					if (k.mode == 2) { k.px = s16() / 8f; k.py = s16() / 8f; }
					if (k.mode != 0) { k.intensity = u8() / 16f; k.dead = u8() / 255f; }
					cams[id] = k; break;
				}
				case Link.C_NEW: { int id = u16(), after = u16(); if (objs.containsKey(id)) { unlink(id); particlesFree(id); } Obj o = new Obj(); o.st = readStatic(); o.kind = o.st[0]; readEmit(o); objs.put(id, o); readDyn(o); link(id, after); break; }
				case Link.C_SET: { int id = u16(); Obj o = objs.get(id); o.st = readStatic(); o.kind = o.st[0]; readEmit(o); readDyn(o); break; }
				case Link.C_MOVE: { int id = u16(); readDyn(objs.get(id)); break; }
				case Link.C_ANIMTO: { int id = u16(); Obj o = objs.get(id); o.st = o.st.clone(); o.st[3] = (byte) u8(); o.st[12] = (byte) u8(); o.st[13] = (byte) u8(); readDyn(o); break; }
				case Link.C_DEL: { int id = u16(); unlink(id); objs.remove(id); particlesFree(id); break; }
				case Link.C_ORDER: { int id = u16(), after = u16(); unlink(id); link(id, after); break; }
				case Link.C_TILES: {
					int id = u16(), start = u16(), n = u8(); Obj o = objs.get(id);
					int need = (o.st[7] & 255) * (o.st[8] & 255);
					if (o.tiles == null || o.tiles.length != need) { o.tiles = new int[need]; Arrays.fill(o.tiles, Link.NONE); }
					for (int i = 0; i < n; i++) o.tiles[start + i] = u16();
					break;
				}
				case Link.C_TILES2: {
					int id = u16(), start = u16(), n = u16(), two = u8() & 1; Obj o = objs.get(id);
					int need = (o.st[7] & 255) * (o.st[8] & 255);
					if (o.tiles == null || o.tiles.length != need) { o.tiles = new int[need]; Arrays.fill(o.tiles, Link.NONE); }
					int k = 0, v = 0;
					while (k < n) {
						int h = u8(), cnt = h < 128 ? h + 1 : h - 126;
						for (int i = 0; i < cnt && k < n; i++, k++) {
							if (h < 128 || i == 0) { v = two == 1 ? u16() : u8(); if (two == 0 && v == 255) v = Link.NONE; }
							o.tiles[start + k] = v;
						}
					}
					break;
				}
				case Link.C_DTEX: {
					int id = u8(), w = u16(), h = u16(), piece = u16();
					int[] pal = new int[u8()]; for (int i = 0; i < pal.length; i++) pal[i] = u8();
					int len = u16(); int end = p + len;
					if (w == 0 || h == 0) { dyn.remove(id); texCache.remove(id); p = end; break; }   // let go
					int[] px = dyn.get(id);
					if (px == null || px[0] != w || px[1] != h) { px = new int[2 + w * h]; px[0] = w; px[1] = h; }
					int k = 2 + piece * Link.DTEX_PIECE;
					while (p < end) { int n = u8(); if (n > 128) { int v = u8(); for (int i = 0; i < n - 127; i++) px[k++] = pal.length > 0 ? pal[v] : v; } else for (int i = 0; i < n; i++) { int v = u8(); px[k++] = pal.length > 0 ? pal[v] : v; } }
					dyn.put(id, px); texCache.remove(id); dynBits.put(id, pal.length == 0 || pal.length > 16 ? 8 : pal.length <= 4 ? 2 : 4); break;
				}
				case Link.C_FOG: {   // (as scene.c read_fog: the fog's texel states, cell by cell where they changed)
					int id = u8(), w = u16(), h = u16(), len = u16(), end = p + len, cw = w / 2, cells = cw * (h / 2);
					int[] px = dyn.get(id);
					if (px == null || px[0] != w || px[1] != h) { px = new int[2 + w * h]; px[0] = w; px[1] = h; Arrays.fill(px, 2, px.length, 0xC0); }
					final int[] FS = {0x00, 0x40, 0xC0, 0xC0};
					int cell = -1, prev = 0;
					while (p < end) {
						int v = varint(), s4 = v & 3, run = 1, t = 0;
						cell += v >> 2;
						if (s4 == 3) { int e = varint(); if ((e & 1) != 0) { run = e >> 1; s4 = prev; } else { t = e >> 1; s4 = -1; } } else prev = s4;
						for (int r = 0; r < run; r++, cell++) {
							if (cell < 0 || cell >= cells) continue;
							int k = 2 + cell / cw * 2 * w + cell % cw * 2;
							if (s4 >= 0) px[k] = px[k + 1] = px[k + w] = px[k + w + 1] = FS[s4];
							else { px[k] = FS[t >> 6 & 3]; px[k + 1] = FS[t >> 4 & 3]; px[k + w] = FS[t >> 2 & 3]; px[k + w + 1] = FS[t & 3]; }
						}
						cell--;
					}
					p = end; dyn.put(id, px); texCache.remove(id); dynBits.put(id, 2); break;
				}
				case Link.C_ANIM: {
					int id = u16(); Anim a = new Anim(); a.tex = u8(); int n = u8(); a.delayMs = u16(); a.looped = u8() != 0;
					a.rects = new int[n * 4]; for (int i = 0; i < n * 4; i++) a.rects[i] = u16();
					anims.put(id, a); break;
				}
				case Link.C_INPUT: p++; break;
				case Link.C_PREDICT: plans.add(readPlan()); break;
				case Link.C_RECIPE: { int id = u8(), n = u8(); if (p + n > b.length) n = b.length - p; recipeSet(id, b, p, n); p += n; break; }
				case Link.C_READY: p += 4; break;
				case Link.C_SOUND: { int k = u8(), v = u8(); sounds.add(k + ":" + v); break; }
				case Link.C_COMMIT: break;
				default: throw new IllegalStateException("bad command " + c + " at " + (p - 1));
			}
		}
		return p;
	}

	private int varint() { int v = 0, sh = 0, c; do { c = u8(); v |= (c & 127) << sh; sh += 7; } while ((c & 128) != 0); return v; }

	/** One picture's unchanging part (its length depends on its kind). */
	private byte[] readStatic() {
		int start = p, kind = b[p];
		switch (kind) {
			case Link.K_IMG: case Link.K_SKIN: if ((b[p + 3] & Link.F_FOG) != 0) fogTex = b[p + 2] & 255; p += 27; break;
			case Link.K_RECT: p += 21; break;
			case Link.K_NINE: p += 31; break;
			case Link.K_TILES: p += 16; break;
			case Link.K_TEXT: p += 11 + (b[p + 10] & 255); break;
			case Link.K_QUADS: p += 4 + (b[p + 3] & 255) * 16 + 7; break;
			case Link.K_SHAPE: p += 15; break;
			case Link.K_EMIT: {   // (particles the watch makes: readEmit, drawEmit)
				boolean cells = (b[p + 3] & 4) != 0;
				int cw = cells ? b[p + 18] & 255 : 0, ch = cells ? b[p + 19] & 255 : 0;
				p += 15 + (cells ? 9 + (cw * ch + 7) / 8 : 0); break;
			}
			default: throw new IllegalStateException("bad kind " + kind);
		}
		return Arrays.copyOfRange(b, start, p);
	}

	private void readDyn(Obj o) {
		int m = u8();
		Arrays.fill(o.vel, 0);
		o.dyn[Link.D_X] = s16(); o.dyn[Link.D_Y] = s16();
		o.dyn[Link.D_A] = (m & 2) != 0 ? u8() : 255;
		if ((m & 4) != 0) { o.dyn[Link.D_SX] = s16(); o.dyn[Link.D_SY] = s16(); } else o.dyn[Link.D_SX] = o.dyn[Link.D_SY] = 256;
		o.dyn[Link.D_ANG] = (m & 8) != 0 ? s16() : 0;
		if ((m & 16) != 0) { o.vel[Link.D_X] = s16(); o.vel[Link.D_Y] = s16(); }
		if ((m & 32) != 0) o.vel[Link.D_A] = s16();
		if ((m & 64) != 0) { o.vel[Link.D_SX] = s16(); o.vel[Link.D_SY] = s16(); }
		if ((m & 128) != 0) o.vel[Link.D_ANG] = s16();
		o.until = (m & 0xF0) != 0 ? u8() / 100f : 0;
		if (o.kind == Link.K_SKIN) { o.dyn[Link.D_OX] = s16(); o.dyn[Link.D_OY] = s16(); o.vel[Link.D_OX] = s16(); o.vel[Link.D_OY] = s16(); }
		if (animated(o)) { o.frame = u8(); o.timer = u8() / 256f; }
		o.at = applyAt;
	}

	private static boolean animated(Obj o) { return (o.kind == Link.K_IMG || o.kind == Link.K_SKIN) && (o.st[3] & Link.F_ANIM) != 0; }

	private void link(int id, int after) {
		Obj o = objs.get(id);
		int nx = after == Link.NONE ? head : objs.get(after).next;
		o.prev = after; o.next = nx;
		if (after == Link.NONE) head = id; else objs.get(after).next = id;
		if (nx != Link.NONE) objs.get(nx).prev = id;
	}

	private void unlink(int id) {
		Obj o = objs.get(id);
		if (o.prev == Link.NONE) head = o.next; else objs.get(o.prev).next = o.next;
		if (o.next != Link.NONE) objs.get(o.next).prev = o.prev;
		o.prev = o.next = Link.NONE;
	}

	// ---------------- drawing ----------------

	private int clipX0, clipY0, clipX1, clipY1;
	private float alpha; private byte[] col; private int ci;

	public Set<Integer> only;

	public void render() {
		Arrays.fill(fb, 0xFF000000);
		for (int id = head; id != Link.NONE; id = objs.get(id).next) {
			Obj o = objs.get(id);
			if (only != null && !only.contains(id)) continue;
			draw(o);
		}
	}

	private float d(Obj o, int i) { float t = now - o.at; if (o.until > 0) t = Math.min(t, o.until); return o.dyn[i] + o.vel[i] * t; }

	// the picture being drawn: camera, place, turn and scale (local units -> camera units x8)
	private Cam cam; private float px0, py0, orx, ory, scx, scy, cs, sn;

	private void draw(Obj o) {
		byte[] s = o.st; Cam c = cams[s[1] & 255]; if (c == null) return;
		cam = c;
		clipX0 = Math.max(0, c.x / 8); clipY0 = Math.max(0, c.y / 8);
		clipX1 = Math.min(W, (c.x + c.w) / 8); clipY1 = Math.min(H, (c.y + c.h) / 8);
		alpha = Math.max(0, Math.min(255, d(o, Link.D_A))) / 255f;
		px0 = d(o, Link.D_X) / 8f; py0 = d(o, Link.D_Y) / 8f;
		scx = d(o, Link.D_SX) / 256f; scy = d(o, Link.D_SY) / 256f;
		double ang = Math.toRadians(d(o, Link.D_ANG) / 64f); cs = (float) Math.cos(ang); sn = (float) Math.sin(ang);
		orx = ory = 0;
		switch (o.kind) {
			case Link.K_EMIT: drawEmit(o); break;
			case Link.K_SHAPE: shape(o, s); break;
			case Link.K_IMG: case Link.K_SKIN: {
				orx = s16(s, 4) / 8f; ory = s16(s, 6) / 8f;
				float w = s16(s, 8) / 8f, h = s16(s, 10) / 8f;
				int tex = s[2] & 255, flip = s[3];
				int tx = s16(s, 12), ty = s16(s, 14), tw = s16(s, 16), th = s16(s, 18);
				if (animated(o)) {
					Anim a = anims.get(s16(s, 12) & 0xFFFF);
					if (a == null) return;
					int n = a.rects.length / 4, f = o.frame;
					float delay = a.delayMs / 1000f;
					if (delay > 0) {
						int steps = (int) ((o.timer * delay + (now - o.at)) / delay);
						f = a.looped ? (o.frame + steps) % n : Math.min(n - 1, o.frame + steps);
					}
					tx = a.rects[f * 4]; ty = a.rects[f * 4 + 1]; tw = a.rects[f * 4 + 2]; th = a.rects[f * 4 + 3];
				}
				col = s; ci = 20;
				BufferedImage t = tex(tex);
				if (t == null) { flat = new float[]{1, 0, 1, 1}; quad(w, h, null); break; }
				final int TX = tx, TY = ty, TW = tw, TH = th; final boolean fx = (flip & 1) != 0, fy = (flip & 2) != 0;
				if ((flip & Link.F_FOG) != 0) { float[] q0 = toScreen(0, 0), q1 = toScreen(w, h); fog(t, tx, ty, tw, th, q0[0], q0[1], q1[0], q1[1]); break; }
				if (o.kind == Link.K_SKIN) {
					final float ox = d(o, Link.D_OX) / 8f, oy = d(o, Link.D_OY) / 8f;
					quad(w, h, (u, v) -> t.getRGB(Math.floorMod((int) (u + ox), t.getWidth()), Math.floorMod((int) (v + oy), t.getHeight())));
				} else quad(w, h, (u, v) -> {
					float uu = fx ? w - u : u, vv = fy ? h - v : v;
					int x = TX + (int) (uu / w * TW), y = TY + (int) (vv / h * TH);
					return x < 0 || y < 0 || x >= t.getWidth() || y >= t.getHeight() ? 0 : t.getRGB(x, y);
				});
				break;
			}
			case Link.K_RECT: {
				orx = s16(s, 2) / 8f; ory = s16(s, 4) / 8f;
				float w = s16(s, 6) / 8f, h = s16(s, 8) / 8f;
				int r = s[10] & 255, g = s[11] & 255, bb = s[12] & 255, a = s[13] & 255;
				col = s; ci = 14;
				flat = new float[]{r / 255f * (s[14] & 255) / 255f + (s[17] & 255) / 255f, g / 255f * (s[15] & 255) / 255f + (s[18] & 255) / 255f,
						bb / 255f * (s[16] & 255) / 255f + (s[19] & 255) / 255f, Math.min(1, a / 255f * alpha + (s[20] & 255) / 255f)};
				quad(w, h, null);
				break;
			}
			case Link.K_NINE: nine(s); break;
			case Link.K_QUADS: {
				int tex = s[2] & 255, n = s[3] & 255;
				col = s; ci = 4 + n * 16;
				BufferedImage t = tex(tex); if (t == null) break;
				for (int i = 0; i < n; i++) {
					int at = 4 + i * 16;
					final int TX = s16(s, at), TY = s16(s, at + 2), TW = s16(s, at + 4), TH = s16(s, at + 6);
					px0 = s16(s, at + 8) / 8f; py0 = s16(s, at + 10) / 8f; float w = s16(s, at + 12) / 8f, h = s16(s, at + 14) / 8f;
					orx = ory = 0; scx = scy = 1; cs = 1; sn = 0;
					quad(w, h, (u, v) -> { int x = TX + (int) (u / w * TW), y = TY + (int) (v / h * TH); return x < t.getWidth() && y < t.getHeight() ? t.getRGB(x, y) : 0; });
				}
				break;
			}
			case Link.K_TILES: tiles(c, o); break;
			case Link.K_TEXT: text(sx(c, d(o, Link.D_X)), sy(c, d(o, Link.D_Y)), s); break;
		}
	}

	// ---------------- particles the watch makes (particles.c, draw.c draw_emit), mirrored ----------------

	static final int MAX_PART = 128, MAX_RECIPE = 64;
	static final int R_FRAME = 1, R_FLICKER = 2, R_RANDOM = 4, R_FADE = 8, R_BOX = 16, R_PULL = 32;
	static final int E_FILL = 1, E_ON = 2, E_CELLS = 4, E_SIGHT = 8;
	private static final class Recipe { int flags, tex, tx, ty, tw, th, life0, life1, colA, colB, mul0, mul1, ax, ay, ox, oy, back; int[] s = new int[4], a = new int[4], v = new int[4]; }
	private static final class Emit { int recipe, flags, quantity, starts, pre, follow, interval, count, w, h, cs, cl, ct, cw, ch; boolean fresh; long next; int[] bound = new int[4]; byte[] bits; }
	private static final class Part { int owner, x, y, vx, vy, mul, mix; long born, life; }
	private final Recipe[] recipes = new Recipe[MAX_RECIPE];
	private final ArrayList<Part> parts = new ArrayList<>();
	private int fogTex = -1;
	private int seed = 0x2545F491;
	private int rnd() { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed; }
	private int rndIn(int a, int b) { return b <= a ? a : a + (int) (Integer.toUnsignedLong(rnd()) % (b - a + 1)); }
	/** The watch's game clock, ms. */
	private long nowMs() { return Math.round(now * 1000); }

	private void recipeSet(int id, byte[] bb, int at, int len) {
		if (id < 0 || id >= MAX_RECIPE || len < 1) return;
		Recipe r = new Recipe();
		int[] q = {at}; int end = at + len;
		java.util.function.IntSupplier U8 = () -> q[0] < end ? bb[q[0]++] & 255 : 0;
		java.util.function.IntSupplier U16 = () -> { q[0] += 2; return q[0] <= end ? (bb[q[0] - 2] & 255) | (bb[q[0] - 1] & 255) << 8 : 0; };
		r.flags = U8.getAsInt();
		if ((r.flags & R_FRAME) != 0) { r.tex = U8.getAsInt(); r.tx = U16.getAsInt(); r.ty = U16.getAsInt(); r.tw = U8.getAsInt(); r.th = U8.getAsInt(); }
		r.life0 = U8.getAsInt() * 20; r.life1 = U8.getAsInt() * 20;
		r.colA = U16.getAsInt(); r.colB = U16.getAsInt();
		for (int i = 0; i < 4; i++) r.s[i] = U8.getAsInt();
		r.mul0 = U8.getAsInt(); r.mul1 = U8.getAsInt();
		for (int i = 0; i < 4; i++) r.a[i] = U8.getAsInt();
		for (int i = 0; i < 4; i++) r.v[i] = (short) U16.getAsInt();
		if ((r.flags & R_PULL) != 0) { r.ax = (short) U16.getAsInt(); r.ay = (short) U16.getAsInt(); }
		r.ox = (byte) U8.getAsInt(); r.oy = (byte) U8.getAsInt(); r.back = (byte) U8.getAsInt();
		if (r.life1 == 0) r.life1 = 20;
		if (r.life0 > r.life1) r.life0 = r.life1;
		recipes[id] = r;
	}

	/** An emitter's unchanging part (as scene.c read_emit): a new start of it, or another recipe, makes it afresh. */
	private void readEmit(Obj o) {
		if (o.kind != Link.K_EMIT) { o.em = null; return; }
		byte[] st = o.st; int k = 2;
		int rec = st[k] & 255, fl = st[k + 1] & 255, follow = (st[k + 2] & 255) | (st[k + 3] & 255) << 8, w = s16(st, k + 4), h = s16(st, k + 6),
				iv = (st[k + 8] & 255) | (st[k + 9] & 255) << 8, qn = st[k + 10] & 255, sts = st[k + 11] & 255, pre = st[k + 12] & 255;
		Emit e = o.em;
		boolean fresh = e == null || e.starts != sts || e.recipe != rec;
		if (e == null) e = o.em = new Emit();
		e.recipe = rec; e.flags = fl; e.follow = follow; e.w = w; e.h = h; e.interval = iv; e.quantity = qn; e.starts = sts; e.pre = pre;
		if ((fl & E_CELLS) != 0) {
			e.cs = st[15] & 255; e.cl = st[16] & 255; e.ct = st[17] & 255; e.cw = st[18] & 255; e.ch = st[19] & 255;
			for (int i = 0; i < 4; i++) e.bound[i] = st[20 + i];
			e.bits = Arrays.copyOfRange(st, 24, 24 + (e.cw * e.ch + 7) / 8);
		} else { e.cs = e.cl = e.ct = e.cw = e.ch = 0; e.bound = new int[]{0, 0, 64, 64}; e.bits = new byte[0]; }
		if (fresh) e.fresh = true;
	}

	private void particlesFree(int owner) { parts.removeIf(q -> q.owner == owner); }

	/** As scene.c scene_in_sight: the fog's texel for the cell is clear. */
	private boolean inSight(int cx, int cy) {
		int[] d = fogTex < 0 ? null : dyn.get(fogTex);
		if (d == null) return true;
		int x = cx * 2, y = cy * 2;
		if (x < 0 || y < 0 || x >= d[0] || y >= d[1]) return false;
		return (d[2 + y * d[0] + x] >> 6) == 0;
	}

	private void spawn(int owner, Recipe r, int x, int y, long at) {
		if (parts.size() >= MAX_PART) return;   // (the watch's share is full)
		Part q = new Part();
		int vx, vy;
		if ((r.flags & R_BOX) != 0) { vx = rndIn(r.v[0], r.v[1]); vy = rndIn(r.v[2], r.v[3]); }
		else {
			int a = (r.v[0] & 0xFFFF) + rndIn(0, r.v[1] & 0xFFFF), sp = rndIn(r.v[2] & 0xFFFF, r.v[3] & 0xFFFF);
			double ang = (a & 0xFFFF) * Math.PI * 2 / 65536;
			vx = (int) (sp * Math.cos(ang)); vy = (int) (sp * Math.sin(ang));
		}
		int life = rndIn(r.life0, r.life1);
		q.owner = owner; q.vx = vx; q.vy = vy; q.born = at; q.life = life;
		q.mul = rndIn(r.mul0, r.mul1); q.mix = rnd() & 255;
		q.x = x + r.ox + vx * life / 1000 * r.back / 64;
		q.y = y + r.oy + vy * life / 1000 * r.back / 64;
		parts.add(q);
	}

	private void spawnAll(int id, Obj o, Emit e, Recipe r, long at) {
		if ((e.flags & E_CELLS) != 0) {
			int cs = e.cs * 8;
			for (int j = 0; j < e.ch; j++) for (int i = 0; i < e.cw; i++) {
				int k = j * e.cw + i;
				if ((e.bits[k >> 3] & (1 << (k & 7))) == 0) continue;
				if ((e.flags & E_SIGHT) != 0 && !inSight(e.cl + i, e.ct + j)) continue;
				int fx = rndIn(e.bound[0], e.bound[2]), fy = rndIn(e.bound[1], e.bound[3]);
				spawn(id, r, (e.cl + i) * cs + cs * fx / 64, (e.ct + j) * cs + cs * fy / 64, at);
			}
			return;
		}
		// its area, or its followed picture's
		int x = Math.round(d(o, Link.D_X)), y = Math.round(d(o, Link.D_Y)), w = e.w, h = e.h;
		if (e.follow != Link.NONE) {
			Obj t = objs.get(e.follow);
			if (t == null) return;
			int tx = Math.round(d(t, Link.D_X)), ty = Math.round(d(t, Link.D_Y));
			if ((e.flags & E_FILL) != 0) {
				int tw = t.kind == Link.K_IMG || t.kind == Link.K_SKIN || t.kind == Link.K_NINE ? s16(t.st, t.kind == Link.K_NINE ? 8 : 8) : t.kind == Link.K_RECT ? s16(t.st, 6) : 0;
				int th = t.kind == Link.K_IMG || t.kind == Link.K_SKIN || t.kind == Link.K_NINE ? s16(t.st, 10) : t.kind == Link.K_RECT ? s16(t.st, 8) : 0;
				x = tx; y = ty; w = (int) (tw * d(t, Link.D_SX) / 256); h = (int) (th * d(t, Link.D_SY) / 256);
			} else { x += tx; y += ty; }
		}
		spawn(id, r, x + rndIn(0, Math.max(w, 0)), y + rndIn(0, Math.max(h, 0)), at);
	}

	private void emitUpdate(int id, Obj o, long now) {
		Emit e = o.em; Recipe r = e == null || e.recipe >= MAX_RECIPE ? null : recipes[e.recipe];
		if (r == null) return;
		int step = e.interval;
		if (e.fresh) {
			e.fresh = false;
			if (e.quantity != 0) {
				for (int i = 0; i < e.pre; i++) spawnAll(id, o, e, r, now);
				e.count = e.pre; e.next = now + step;
			} else if (step != 0) {
				for (int t = step; t < r.life1 && t / step <= 8; t += step) spawnAll(id, o, e, r, now - t);
				e.next = now + rndIn(0, step);
			}
		}
		if ((e.flags & E_ON) == 0 || step == 0) return;
		if (e.quantity != 0 && e.count >= e.quantity) return;
		if (now - e.next > r.life1) e.next = now - r.life1;
		int made = 0;
		while (now - e.next >= 0 && made < 24) {
			spawnAll(id, o, e, r, e.next);
			e.next += step; e.count++; made++;
			if (e.quantity != 0 && e.count >= e.quantity) break;
		}
	}

	private static int curve(int[] c, int u) {
		int k = c[1];
		if (u <= k) return k != 0 ? c[0] + (c[2] - c[0]) * u / k : c[2];
		return k < 255 ? c[2] + (c[3] - c[2]) * (u - k) / (255 - k) : c[2];
	}
	private static int[] colour(int v) { return new int[]{((v >> 4) & 3) * 85, ((v >> 2) & 3) * 85, (v & 3) * 85, ((v >> 10) & 3) * 85, ((v >> 8) & 3) * 85, ((v >> 6) & 3) * 85}; }

	/** As draw.c draw_emit: the particles due made, then each drawn by its age (a square, or the recipe's frame). */
	private void drawEmit(Obj o) {
		int id = -1;
		for (Map.Entry<Integer, Obj> en : objs.entrySet()) if (en.getValue() == o) { id = en.getKey(); break; }
		long tn = nowMs();
		emitUpdate(id, o, tn);
		Emit e = o.em; Recipe r = e == null || e.recipe >= MAX_RECIPE ? null : recipes[e.recipe];
		if (r == null) return;
		int[] ca = colour(r.colA), cb = colour(r.colB);
		for (int i = 0; i < parts.size(); i++) {
			Part q = parts.get(i);
			if (q.owner != id) continue;
			long age = tn - q.born;
			if (age >= q.life) { parts.set(i, parts.get(parts.size() - 1)); parts.remove(parts.size() - 1); i--; continue; }
			if (age < 0) continue;   // (not born yet)
			long t = age;
			long x = q.x + q.vx * t / 1000 + r.ax * t / 1000 * t / 2000, y = q.y + q.vy * t / 1000 + r.ay * t / 1000 * t / 2000;
			int u = (int) (t * 255 / q.life);
			int sz = curve(r.s, u) * q.mul;
			if ((r.flags & R_FLICKER) != 0) sz = rndIn(0, sz);
			int size = (r.flags & R_FRAME) != 0 ? sz : sz / 64, al = curve(r.a, u);
			if (al < 30 || size <= 0) continue;
			int m = (r.flags & R_RANDOM) != 0 ? q.mix : (r.flags & R_FADE) != 0 ? u : 0;
			byte[] c7 = new byte[7];
			for (int k = 0; k < 6; k++) c7[k] = (byte) (ca[k] + (cb[k] - ca[k]) * m / 255);
			if ((r.flags & R_FRAME) != 0) {
				float hw = r.tw * 4f * size / 4096, hh = r.th * 4f * size / 4096;   // half its size, x8
				if (hw < 1 || hh < 1) continue;
				float L = sx(cam, x - hw), R = sx(cam, x + hw), T = sy(cam, y - hh), B = sy(cam, y + hh);
				col = c7; ci = 0; alpha = al / 255f;
				blit(r.tex, L, T, R - L, B - T, r.tx, r.ty, r.tw, r.th, false, false);
			} else {
				float hs = size / 2f;
				float L = sx(cam, x - hs), R = sx(cam, x + hs), T = sy(cam, y - hs), B = sy(cam, y + hs);
				int x0 = Math.round(L), x1 = Math.round(R), y0 = Math.round(T), y1 = Math.round(B);
				if (x1 == x0 && R > L) x1 = x0 + 1;
				if (y1 == y0 && B > T) y1 = y0 + 1;
				float rr = Math.min(255, (c7[0] & 255) + (c7[3] & 255)) / 255f, gg = Math.min(255, (c7[1] & 255) + (c7[4] & 255)) / 255f, bl = Math.min(255, (c7[2] & 255) + (c7[5] & 255)) / 255f;
				for (int py = Math.max(clipY0, y0); py < Math.min(clipY1, y1); py++)
					for (int px = Math.max(clipX0, x0); px < Math.min(clipX1, x1); px++) put(px, py, rr, gg, bl, al / 255f);
			}
		}
	}

	interface Sampler { int at(float u, float v); }
	private float[] flat;

	/** Draws a w x h picture through the current place/turn/scale, sampling each covered pixel. */
	private void quad(float w, float h, Sampler smp) {
		// corners on screen
		float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
		for (int k = 0; k < 4; k++) {
			float u = (k & 1) * w, v = (k >> 1) * h;
			float[] q = toScreen(u, v);
			minX = Math.min(minX, q[0]); maxX = Math.max(maxX, q[0]); minY = Math.min(minY, q[1]); maxY = Math.max(maxY, q[1]);
		}
		int x0 = Math.max(clipX0, (int) Math.floor(minX)), x1 = Math.min(clipX1, (int) Math.ceil(maxX));
		int y0 = Math.max(clipY0, (int) Math.floor(minY)), y1 = Math.min(clipY1, (int) Math.ceil(maxY));
		float z = cam.zoom / 256f;
		for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
			// screen -> camera units -> local
			float cx = ((px + 0.5f) * 8 - cam.x) / z / 8 + cam.sx / 8f, cy = ((py + 0.5f) * 8 - cam.y) / z / 8 + cam.sy / 8f;
			float dx = cx - px0 - orx, dy = cy - py0 - ory;
			float rx = cs * dx + sn * dy, ry = -sn * dx + cs * dy;
			if (scx == 0 || scy == 0) continue;
			float u = rx / scx + orx, v = ry / scy + ory;
			if (u < 0 || v < 0 || u >= w || v >= h) continue;
			if (smp == null) put(px, py, flat[0], flat[1], flat[2], flat[3]);
			else plot(px, py, smp.at(u, v));
		}
	}

	private float[] toScreen(float u, float v) {
		float dx = (u - orx) * scx, dy = (v - ory) * scy;
		float cx = px0 + orx + cs * dx - sn * dy, cy = py0 + ory + sn * dx + cs * dy;
		return new float[]{sx(cam, cx * 8), sy(cam, cy * 8)};
	}

	private float sx(Cam c, float x8) { return (c.x + (x8 - c.sx) * c.zoom / 256f) / 8f; }
	private float sy(Cam c, float y8) { return (c.y + (y8 - c.sy) * c.zoom / 256f) / 8f; }

	private BufferedImage tex(int id) {
		return texCache.computeIfAbsent(id, k -> {
			if (k >= 200) {
				int[] d = dyn.get(k); if (d == null) return null;
				BufferedImage im = new BufferedImage(d[0], d[1], BufferedImage.TYPE_INT_ARGB);
				for (int i = 0; i < d[0] * d[1]; i++) im.setRGB(i % d[0], i / d[0], argb(d[2 + i]));
				return im;
			}
			try {
				String path = assets.paths.get(k - 1);
				return ImageIO.read(new File(art, path.substring(0, path.lastIndexOf('.')) + ".png"));
			} catch (Exception e) { return null; }
		});
	}

	/** Pebble colour byte to a normal ARGB int. */
	static int argb(int p) { return ((p >> 6 & 3) * 85 << 24) | ((p >> 4 & 3) * 85 << 16) | ((p >> 2 & 3) * 85 << 8) | (p & 3) * 85; }

	private void blit(int texId, float x, float y, float w, float h, int tx, int ty, int tw, int th, boolean fx, boolean fy) {
		BufferedImage t = tex(texId);
		if (w < 0) { x += w; w = -w; fx = !fx; }
		if (h < 0) { y += h; h = -h; fy = !fy; }
		if (t == null) { fill(x, y, w, h, 1, 0, 1, 1); return; }
		int x0 = Math.max(clipX0, (int) Math.floor(x)), x1 = Math.min(clipX1, (int) Math.ceil(x + w));
		int y0 = Math.max(clipY0, (int) Math.floor(y)), y1 = Math.min(clipY1, (int) Math.ceil(y + h));
		for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
			float u = (px + 0.5f - x) / w, v = (py + 0.5f - y) / h;
			if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
			if (fx) u = 1 - u; if (fy) v = 1 - v;
			int sxp = tx + (int) (u * tw), syp = ty + (int) (v * th);
			if (sxp < 0 || syp < 0 || sxp >= t.getWidth() || syp >= t.getHeight()) continue;
			plot(px, py, t.getRGB(sxp, syp));
		}
	}

	// the fog of war as the watch draws it (draw.c draw_fog): remembered under a fine checker, never seen black, a soft
	// dithered edge (rounded at corners, a third of a cell wide) where the seen meets the never seen
	private static final int[] B16 = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
	private static int fogAt(BufferedImage t, int x, int y) {
		if (x < 0 || y < 0 || x >= t.getWidth() || y >= t.getHeight()) return 16;
		int a = (t.getRGB(x, y) >>> 24) / 85;
		return a >= 3 ? 16 : a > 0 ? 8 : 0;
	}
	private void fog(BufferedImage t, int tx, int ty, int tw, int th, float L, float T, float R, float B) {
		if (R <= L || B <= T || tw <= 0 || th <= 0) return;
		for (int j = 0; j < th; j++) {
			int y0 = Math.round(T + (B - T) * j / th), y1 = Math.round(T + (B - T) * (j + 1) / th);
			if (y1 <= clipY0 || y0 >= clipY1 || y1 <= y0) continue;
			for (int i = 0; i < tw; i++) {
				int x0 = Math.round(L + (R - L) * i / tw), x1 = Math.round(L + (R - L) * (i + 1) / tw);
				if (x1 <= clipX0 || x0 >= clipX1 || x1 <= x0) continue;
				int cx = tx + i, cy = ty + j, own = fogAt(t, cx, cy);
				int nw = x1 - x0, nh = y1 - y0, w = Math.max(2, 2 * Math.min(nw, nh) / 3), m = 0;
				if (own < 16) {
					int[][] nb = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
					for (int k = 0; k < 8; k++) if (fogAt(t, cx + nb[k][0], cy + nb[k][1]) == 16) m |= 1 << k;
					if (own == 0 && m == 0) continue;
				}
				for (int y = Math.max(y0, clipY0); y < Math.min(y1, clipY1); y++) {
					int py = y - y0, fy = py < w ? py : nh - 1 - py;
					for (int x = Math.max(x0, clipX0); x < Math.min(x1, clipX1); x++) {
						int dd = w;
						if (own < 16 && m != 0) {
							int px = x - x0, fx = px < w ? px : nw - 1 - px;
							if (fx < w || fy < w) {
								boolean l = px < w, tp = py < w;
								if (fy < w && (m & (tp ? 4 : 8)) != 0) dd = fy;
								if (fx < w && (m & (l ? 1 : 2)) != 0 && fx < dd) dd = fx;
								if (fx < w && fy < w && (m & (tp ? (l ? 16 : 32) : (l ? 64 : 128))) != 0) { int c = (int) Math.sqrt(fx * fx + fy * fy); if (c < dd) dd = c; }
							}
						}
						if (own == 16 || B16[(y & 3) * 4 + (x & 3)] * w < own * w + 8 * (w - dd)) fb[y * W + x] = 0xFF000000;
					}
				}
			}
		}
	}

	private void skin(int texId, float x, float y, float w, float h, float ox, float oy) {
		BufferedImage t = tex(texId); if (t == null) return;
		int x0 = Math.max(clipX0, (int) Math.floor(x)), x1 = Math.min(clipX1, (int) Math.ceil(x + w));
		int y0 = Math.max(clipY0, (int) Math.floor(y)), y1 = Math.min(clipY1, (int) Math.ceil(y + h));
		for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
			int sxp = Math.floorMod((int) (px - x + ox), t.getWidth()), syp = Math.floorMod((int) (py - y + oy), t.getHeight());
			plot(px, py, t.getRGB(sxp, syp));
		}
	}

	/** One texture pixel through the picture's colour multiply/add and alpha. */
	private void plot(int px, int py, int c) {
		byte[] s = col;
		float a = (c >>> 24) / 255f * alpha + (s[ci + 6] & 255) / 255f;
		if (a <= 0.01f) return;
		float r = ((c >> 16) & 255) / 255f * (s[ci] & 255) / 255f + (s[ci + 3] & 255) / 255f;
		float g = ((c >> 8) & 255) / 255f * (s[ci + 1] & 255) / 255f + (s[ci + 4] & 255) / 255f;
		float bb = (c & 255) / 255f * (s[ci + 2] & 255) / 255f + (s[ci + 5] & 255) / 255f;
		put(px, py, r, g, bb, Math.min(1, a));
	}

	// like the watch: see-through pixels are dithered with a 4x4 ordered pattern, not blended
	private static final int[] BAYER = {8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88};
	/** A disc or a band above the centre (Pebble PD's round pieces). */
	private void shape(Obj o, byte[] s) {
		int kind = s[2], pc = s[3] & 255;
		float r = ((pc >> 4) & 3) / 3f, g = ((pc >> 2) & 3) / 3f, bl = (pc & 3) / 3f;
		int cx = (int) Math.floor(sx(cam, d(o, Link.D_X))), cy = (int) Math.floor(sy(cam, d(o, Link.D_Y)));
		int r0 = s[4] & 255, r1 = s[5] & 255, b0 = s16(s, 6), b1 = s16(s, 8), sl = s[14];
		if (kind == 3) {
			int w = s16(s, 10), h = s16(s, 12), x0 = cx + b0, y0 = cy + b1;
			for (int y = y0; y < y0 + h; y++) {
				int d = y - cy, q = r0 * r0 - d * d; if (q <= 0) continue;
				int hw = (int) Math.sqrt(q);
				for (int x = Math.max(x0, cx - hw); x <= Math.min(x0 + w - 1, cx + hw); x++) dot(x, y, r, g, bl);
			}
			return;
		}
		if (kind == 1) {
			for (int dy = -r0; dy <= r0; dy++) { int w = (int) Math.sqrt(r0 * r0 - dy * dy); for (int x = cx - w; x <= cx + w; x++) dot(x, cy + dy, r, g, bl); }
			return;
		}
		for (int rr = r0; rr < r1; rr++) {
			int rin = rr, rout = rr + 1;
			double t0 = Math.tan(Math.toRadians(b0 / 10.0)), t1 = Math.tan(Math.toRadians((b1 + (rr - r0) * sl) / 10.0));
			if (sl == 0) { rin = r0; rout = r1; rr = r1; }
			for (int y = 1; y <= rout; y++) {
				int xo = (int) Math.sqrt(rout * rout - y * y), xi = y < rin ? (int) Math.sqrt(rin * rin - y * y) : 0;
				int a = (int) Math.floor(y * t0), bb = (int) Math.floor(y * t1);
				for (int sd = 0; sd < 2; sd++) {
					int lo = sd == 1 ? xi : -xo, hi = sd == 1 ? xo : -xi;
					if (lo < a) lo = a; if (hi > bb) hi = bb;
					for (int x = lo; x <= hi; x++) dot(cx + x, cy - y, r, g, bl);
				}
			}
		}
	}

	private void dot(int x, int y, float r, float g, float b) {
		if (x < clipX0 || y < clipY0 || x >= clipX1 || y >= clipY1) return;
		fb[y * W + x] = 0xFF000000 | (q(r) << 16) | (q(g) << 8) | q(b);
	}

	private void put(int px, int py, float r, float g, float b, float a) {
		int A = Math.round(a * 255);
		if (A <= 247 && A <= BAYER[((py & 3) << 2) | (px & 3)]) return;
		fb[py * W + px] = 0xFF000000 | (q(r) << 16) | (q(g) << 8) | q(b);
	}

	private static int q(float v) { return Math.round(Math.max(0, Math.min(1, v)) * 3) * 85; }

	private void fill(float x, float y, float w, float h, float r, float g, float b, float a) {
		if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
		int x0 = Math.max(clipX0, Math.round(x)), x1 = Math.min(clipX1, Math.round(x + w));
		int y0 = Math.max(clipY0, Math.round(y)), y1 = Math.min(clipY1, Math.round(y + h));
		for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) put(px, py, r, g, b, a);
	}

	private void nine(byte[] s) {
		orx = s16(s, 4) / 8f; ory = s16(s, 6) / 8f;
		int tex = s[2] & 255; boolean fx = (s[3] & 1) != 0, fy = (s[3] & 2) != 0;
		float w = s16(s, 8) / 8f, h = s16(s, 10) / 8f;
		int tx = s16(s, 12), ty = s16(s, 14), tw = s16(s, 16), th = s16(s, 18);
		int ml = s[20], mt = s[21], mr = s[22], mb = s[23];
		col = s; ci = 24;
		BufferedImage t = tex(tex); if (t == null) return;
		quad(w, h, (u, v) -> {
			if (fx) u = w - u; if (fy) v = h - v;
			int x = u < ml ? (int) u : u >= w - mr ? tw - (int) Math.ceil(w - u) : ml + (int) ((u - ml) / (w - ml - mr) * (tw - ml - mr));
			int y = v < mt ? (int) v : v >= h - mb ? th - (int) Math.ceil(h - v) : mt + (int) ((v - mt) / (h - mt - mb) * (th - mt - mb));
			x = Math.max(0, Math.min(tw - 1, x)); y = Math.max(0, Math.min(th - 1, y));
			return t.getRGB(tx + x, ty + y);
		});
	}

	private void tiles(Cam c, Obj o) {
		float x = sx(c, d(o, Link.D_X)), y = sy(c, d(o, Link.D_Y)), z = c.zoom / 256f;
		byte[] s = o.st; if (o.tiles == null) return;
		int tex = s[2] & 255; BufferedImage t = tex(tex); if (t == null) return;
		int cw = s[3] & 255, ch = s[4] & 255, fw = s[5] & 255, fh = s[6] & 255, mw = s[7] & 255, mh = s[8] & 255;
		int cols = t.getWidth() / fw;
		col = s; ci = 9;
		for (int i = 0; i < mw * mh; i++) {
			int d = o.tiles[i]; if (d == Link.NONE) continue;
			float px = x + (i % mw) * cw * z, py = y + (i / mw) * ch * z;
			if (px > clipX1 || py > clipY1 || px + cw * z < clipX0 || py + ch * z < clipY0) continue;
			blit(tex, px, py, cw * z, ch * z, (d % cols) * fw, (d / cols) * fh, fw, fh, false, false);
		}
	}

	private final HashMap<Integer, BufferedImage> fontPics = new HashMap<>();

	/** Text the way the watch draws it: Pebble's Gothic glyphs (captured from the watch), capitals at y. */
	private void text(float x, float y, byte[] s) {
		int font = s[2] & 127, len = s[10] & 255;
		if ((s[2] & 128) != 0) {
			// outlined: black copies around it first (as the watch does)
			byte[] k = s.clone(); k[2] = (byte) font; for (int i = 3; i < 9; i++) k[i] = 0;
			text(x - 1, y, k); text(x + 1, y, k); text(x, y - 1, k); text(x, y + 1, k);
		}
		String str = new String(s, 11, len, StandardCharsets.UTF_8).replace("_", "");
		BufferedImage pic = fontPics.computeIfAbsent(font, f -> {
			try { return ImageIO.read(WatchFiles.get("pebxel/fonts/" + WatchFonts.NAMES[f] + ".png").file()); } catch (Exception e) { return null; }
		});
		if (pic == null || alpha < 0.25f) return;
		int top = Math.round(y) - WatchFonts.capTop(font), px = Math.round(x);
		float r = Math.min(1, (s[3] & 255) / 255f + (s[6] & 255) / 255f), g = Math.min(1, (s[4] & 255) / 255f + (s[7] & 255) / 255f), b = Math.min(1, (s[5] & 255) / 255f + (s[8] & 255) / 255f);
		for (char ch : str.toCharArray()) {
			int[] gg = WatchFonts.glyph(font, ch); if (gg == null) gg = WatchFonts.glyph(font, '?');
			int gx = gg[0], gw = gg[1];
			for (int yy = 0; yy < pic.getHeight(); yy++) for (int xx = 0; xx < gw; xx++) {
				int X = px + xx, Y = top + yy;
				if (X < clipX0 || Y < clipY0 || X >= clipX1 || Y >= clipY1) continue;
				if ((pic.getRGB(gx + xx, yy) >>> 24) > 128) put(X, Y, r, g, b, 1);
			}
			px += gw;
		}
	}

	/** For tests: what the watch would draw for this picture right now (null if it doesn't know it). */
	public float[] guess(int id) {
		Obj o = objs.get(id); if (o == null) return null;
		float[] g = new float[Link.N];
		for (int i = 0; i < Link.N; i++) g[i] = d(o, i);
		g[Link.D_A] = Math.max(0, Math.min(255, g[Link.D_A]));
		return g;
	}

	/** Roughly what the C watch app would hold for this screen (scene.c's pictures, animations and run-time
	 *  pictures, 8 bytes of heap bookkeeping each, tiles as 16-bit cells) plus messages waiting to play. */
	public int heapEstimate() {
		int b = 0;
		for (Obj o : objs.values()) {
			b += 8 + 80;
			switch (o.kind) {
				case Link.K_TILES: if (o.tiles != null) b += 8 + 8 + (o.st[7] & 255) * (o.st[8] & 255) * cellBytes(o); break;   // (cells only once one is set)
				case Link.K_TEXT: b += 8 + (o.st[10] & 255) + 1; break;
				case Link.K_NINE: b += 8 + 4; break;
				case Link.K_QUADS: b += 8 + 2 + (o.st[3] & 255) * 16; break;
			}
		}
		for (Anim a : anims.values()) b += 8 + 8 + a.rects.length * 2;
		for (Map.Entry<Integer, int[]> e : dyn.entrySet()) b += 8 + 20 + (e.getValue()[0] * e.getValue()[1] * dynBits.getOrDefault(e.getKey(), 8) + 7) / 8;
		for (byte[] m : q) b += 8 + 8 + m.length;
		return b;
	}
	public String heapParts() {
		int pics = 0, tiles = 0, an = 0, dy = 0, qq = 0; StringBuilder t = new StringBuilder();
		for (Obj o : objs.values()) {
			pics += 88;
			if (o.kind == Link.K_TILES) { int n = o.tiles == null ? 0 : 16 + (o.st[7] & 255) * (o.st[8] & 255) * cellBytes(o); tiles += n; t.append(" ").append(assets.paths.get((o.st[2] & 255) - 1).replaceAll(".*/", "")).append(":").append(o.st[7] & 255).append("x").append(o.st[8] & 255).append(":nonempty").append(o.tiles == null ? 0 : Arrays.stream(o.tiles).filter(v -> v != Link.NONE).count()); }
		}
		for (Anim a : anims.values()) an += 16 + a.rects.length * 2;
		for (Map.Entry<Integer, int[]> e : dyn.entrySet()) { int[] d = e.getValue(); int bits = dynBits.getOrDefault(e.getKey(), 8); dy += 28 + (d[0] * d[1] * bits + 7) / 8; t.append(" dyn").append(d[0]).append("x").append(d[1]).append("@").append(bits); }
		for (byte[] m : q) qq += 16 + m.length;
		return "pics " + pics + " tiles " + tiles + " anims " + an + " dyn " + dy + " queue " + qq + " [" + t.toString().trim() + "]";
	}
	public int objCount() { return objs.size(); }
	/** Bytes a tile map's cell takes on the watch (scene.c: two for pictures of more than 256 tiles). */
	private final HashMap<Integer, Integer> cellsOf = new HashMap<>();
	private int cellBytes(Obj o) {
		int tex = o.st[2] & 255, fw = o.st[5] & 255, fh = o.st[6] & 255;
		int n = cellsOf.computeIfAbsent(tex * 65536 + fw * 256 + fh, k -> {
			try { BufferedImage t = ImageIO.read(new File(assets.paths.get(tex - 1))); return (t.getWidth() / fw) * (t.getHeight() / fh); }   // (the phone's own picture: the folder we run in)
			catch (Exception e) { return 9999; }
		});
		return n > 256 ? 2 : 1;
	}
	public int animCount() { return anims.size(); }
	public int dynCount() { return dyn.size(); }

	public void dump() {
		System.out.println("particles " + parts.size() + " recipes " + Arrays.stream(recipes).filter(Objects::nonNull).count());
		for (Map.Entry<Integer, Obj> en : objs.entrySet()) { Emit e = en.getValue().em; if (e != null) System.out.println("emitter " + en.getKey() + " recipe " + e.recipe + (recipes[e.recipe] == null ? "(none)" : " flags " + recipes[e.recipe].flags + " life " + recipes[e.recipe].life0 + "-" + recipes[e.recipe].life1) + " eflags " + e.flags + " interval " + e.interval + " q " + e.quantity + " follow " + e.follow + " at " + en.getValue().dyn[0] / 8 + "," + en.getValue().dyn[1] / 8 + " cam " + en.getValue().st[1]); }
		for (int id = head; id != Link.NONE; id = objs.get(id).next) {
			Obj o = objs.get(id); byte[] s = o.st;
			String t = "";
			if (o.kind != Link.K_RECT && o.kind != Link.K_TEXT) { int tx = s[2] & 255; t = tx >= 200 ? "dyn" + tx : tx == 0 ? "?" : assets.paths.get(tx - 1); }
			System.out.println("obj " + id + " kind " + o.kind + " cam " + s[1] + " " + t + " at " + o.dyn[0] / 8 + "," + o.dyn[1] / 8 + " scale " + o.dyn[3] + " ang " + o.dyn[5] + " alpha " + d(o, 2) + " vA " + o.vel[2] + " col " + (o.kind == 1 ? (s[20]&255) + "," + (s[26]&255) + " rect " + s16(s,12) + "," + s16(s,14) + "," + s16(s,16) + "," + s16(s,18) + " size " + s16(s,8)/8f + "x" + s16(s,10)/8f + " flags " + s[3] : "") + (o.tiles != null ? " tiles[" + o.tiles.length + "]" : "") + (o.kind == Link.K_RECT ? " rect " + s16(s, 6) / 8f + "x" + s16(s, 8) / 8f + " rgba " + (s[10] & 255) + "," + (s[11] & 255) + "," + (s[12] & 255) + "," + (s[13] & 255) : ""));
		}
	}

	public void save(File f, int scale) throws Exception {
		BufferedImage img = new BufferedImage(W * scale, H * scale, BufferedImage.TYPE_INT_RGB);
		for (int y = 0; y < H * scale; y++) for (int x = 0; x < W * scale; x++) {
			int px = x / scale, py = y / scale; double dx = px + 0.5 - W / 2.0, dy = py + 0.5 - H / 2.0;
			img.setRGB(x, y, Screen.round && dx * dx + dy * dy > (W / 2.0) * (W / 2.0) ? 0x404040 : fb[py * W + px]);
		}
		ImageIO.write(img, "png", f);
	}
}
