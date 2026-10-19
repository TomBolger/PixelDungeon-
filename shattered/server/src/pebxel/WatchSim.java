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
	public static final int W = 200, H = 228;
	public final int[] fb = new int[W * H];

	private static final class Cam { int x, y, w, h, zoom, mode, follow; float sx, sy, ofx, ofy, px, py, intensity, dead; }
	private static final class Anim { int tex, delayMs; boolean looped; int[] rects; }
	private static final class Obj {
		int kind; byte[] st; int[] tiles; int prev = Link.NONE, next = Link.NONE;
		float[] dyn = new float[Link.N], vel = new float[Link.N]; float at, until;
		int frame; float timer; // animation
	}

	private final Cam[] cams = new Cam[32];
	private final HashMap<Integer, Obj> objs = new HashMap<>();
	private final HashMap<Integer, Anim> anims = new HashMap<>();
	private int head = Link.NONE;
	private final HashMap<Integer, int[]> dyn = new HashMap<>(); // [w, h, px...]
	private final Assets assets;
	private final HashMap<Integer, BufferedImage> texCache = new HashMap<>();
	/** Pictures already converted to Pebble colours by tools/convert_art.py. */
	public File art = new File(System.getProperty("pebxel.art", "../../../../../build/art"));
	public float now;

	public WatchSim(File root, Assets assets) { this.assets = assets; }

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
	private float off, curAt; private boolean haveOff;
	static final float MARGIN = 0.040f;
	private static long stamp(byte[] d, int i) { return (d[i + 1] & 255L) | (d[i + 2] & 255L) << 8 | (d[i + 3] & 255L) << 16 | (d[i + 4] & 255L) << 24; }

	/** A message arrives from the phone now. */
	public void receive(byte[] msg) {
		if (msg.length >= 5 && msg[0] == Link.C_TIME) {
			float want = now - stamp(msg, 0) / 1000f + MARGIN;
			if (!haveOff || want > off) off = want; else off -= (off - want) / 8;
			haveOff = true;
			if (System.getenv("OFFS") != null) System.out.printf("recv at %.3f stamp %.3f want %.3f off %.3f len %d%n", now, stamp(msg, 0) / 1000f, want, off, msg.length);
		}
		q.add(msg); qPos.add(new int[]{0});
		pump();
	}

	private void pump() {
		while (!q.isEmpty()) {
			byte[] m = q.peek(); int[] pos = qPos.peek();
			if (pos[0] < m.length && m[pos[0]] == Link.C_TIME) {
				float due = stamp(m, pos[0]) / 1000f + off;
				if (due > now + 1e-4f) return;
				curAt = due;
			}
			int used = apply(m, pos[0], true);
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
			switch (c) {
				case Link.C_TIME: p += 4; break;
				case Link.C_RESET: objs.clear(); head = Link.NONE; dyn.clear(); anims.clear(); Arrays.fill(cams, null); break;
				case Link.C_CAM: {
					int id = u8(); Cam k = new Cam(); k.x = s16(); k.y = s16(); k.w = s16(); k.h = s16(); k.zoom = u16(); k.sx = s16(); k.sy = s16();
					k.mode = u8();
					if (k.mode == 1) { k.follow = u16(); k.ofx = s16() / 8f; k.ofy = s16() / 8f; }
					if (k.mode == 2) { k.px = s16() / 8f; k.py = s16() / 8f; }
					if (k.mode != 0) { k.intensity = u8() / 16f; k.dead = u8() / 255f; }
					cams[id] = k; break;
				}
				case Link.C_NEW: { int id = u16(), after = u16(); Obj o = new Obj(); o.st = readStatic(); o.kind = o.st[0]; objs.put(id, o); readDyn(o); link(id, after); break; }
				case Link.C_SET: { int id = u16(); Obj o = objs.get(id); o.st = readStatic(); o.kind = o.st[0]; readDyn(o); break; }
				case Link.C_MOVE: { int id = u16(); readDyn(objs.get(id)); break; }
				case Link.C_DEL: { int id = u16(); unlink(id); objs.remove(id); break; }
				case Link.C_ORDER: { int id = u16(), after = u16(); unlink(id); link(id, after); break; }
				case Link.C_TILES: {
					int id = u16(), start = u16(), n = u8(); Obj o = objs.get(id);
					int need = (o.st[7] & 255) * (o.st[8] & 255);
					if (o.tiles == null || o.tiles.length != need) { o.tiles = new int[need]; Arrays.fill(o.tiles, Link.NONE); }
					for (int i = 0; i < n; i++) o.tiles[start + i] = u16();
					break;
				}
				case Link.C_DTEX: {
					int id = u8(), w = u16(), h = u16(), piece = u16();
					int[] pal = new int[u8()]; for (int i = 0; i < pal.length; i++) pal[i] = u8();
					int len = u16(); int end = p + len;
					int[] px = dyn.get(id);
					if (px == null || px[0] != w || px[1] != h) { px = new int[2 + w * h]; px[0] = w; px[1] = h; }
					int k = 2 + piece * Link.DTEX_PIECE;
					while (p < end) { int n = u8(); if (n > 128) { int v = u8(); for (int i = 0; i < n - 127; i++) px[k++] = pal.length > 0 ? pal[v] : v; } else for (int i = 0; i < n; i++) { int v = u8(); px[k++] = pal.length > 0 ? pal[v] : v; } }
					dyn.put(id, px); texCache.remove(id); break;
				}
				case Link.C_ANIM: {
					int id = u16(); Anim a = new Anim(); a.tex = u8(); int n = u8(); a.delayMs = u16(); a.looped = u8() != 0;
					a.rects = new int[n * 4]; for (int i = 0; i < n * 4; i++) a.rects[i] = u16();
					anims.put(id, a); break;
				}
				case Link.C_COMMIT: break;
				default: throw new IllegalStateException("bad command " + c + " at " + (p - 1));
			}
		}
		return p;
	}

	/** One picture's unchanging part (its length depends on its kind). */
	private byte[] readStatic() {
		int start = p, kind = b[p];
		switch (kind) {
			case Link.K_IMG: case Link.K_SKIN: p += 27; break;
			case Link.K_RECT: p += 21; break;
			case Link.K_NINE: p += 31; break;
			case Link.K_TILES: p += 16; break;
			case Link.K_TEXT: p += 11 + (b[p + 10] & 255); break;
			case Link.K_QUADS: p += 4 + (b[p + 3] & 255) * 16 + 7; break;
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

	public void dump() {
		for (int id = head; id != Link.NONE; id = objs.get(id).next) {
			Obj o = objs.get(id); byte[] s = o.st;
			String t = "";
			if (o.kind != Link.K_RECT && o.kind != Link.K_TEXT) { int tx = s[2] & 255; t = tx >= 200 ? "dyn" + tx : tx == 0 ? "?" : assets.paths.get(tx - 1); }
			System.out.println("obj " + id + " kind " + o.kind + " cam " + s[1] + " " + t + " at " + o.dyn[0] / 8 + "," + o.dyn[1] / 8 + " scale " + o.dyn[3] + " ang " + o.dyn[5] + " alpha " + d(o, 2) + " vA " + o.vel[2] + " col " + (o.kind == 1 ? (s[20]&255) + "," + (s[26]&255) + " rect " + s16(s,12) + "," + s16(s,14) + "," + s16(s,16) + "," + s16(s,18) + " size " + s16(s,8)/8f + "x" + s16(s,10)/8f + " flags " + s[3] : "") + (o.tiles != null ? " tiles[" + o.tiles.length + "]" : ""));
		}
	}

	public void save(File f, int scale) throws Exception {
		BufferedImage img = new BufferedImage(W * scale, H * scale, BufferedImage.TYPE_INT_RGB);
		for (int y = 0; y < H * scale; y++) for (int x = 0; x < W * scale; x++) img.setRGB(x, y, fb[(y / scale) * W + x / scale]);
		ImageIO.write(img, "png", f);
	}
}
