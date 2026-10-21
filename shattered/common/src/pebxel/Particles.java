package pebxel;

import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.GroupAccess;
import com.watabou.noosa.Image;
import com.watabou.noosa.PseudoPixel;
import com.watabou.noosa.Visual;
import com.watabou.noosa.particles.Emitter;
import com.watabou.noosa.particles.EmitterAccess;
import com.watabou.utils.Random;
import java.io.ByteArrayOutputStream;
import java.util.*;

/**
 * Particles made on the watch. Shattered's emitters (torches, sewer drips, pit winds and flows, city smoke, gas,
 * fire, sparks, blood, spells...) pour out tiny pictures by the dozen a second; sent one by one, each a new picture
 * and its corrections, they took most of the radio wherever there were any, 1-2.5 KB/s on most floors. Instead
 * the watch gets each emitter once (where, how often, how many) and a recipe for its particles, and makes and
 * moves them itself (watch particles.c).
 *
 * A recipe isn't written by hand for each of Shattered's particle classes: it is measured. The emitter's own
 * factory makes a handful of particles off screen, and their own update() is run through their lives; the recipe is
 * what they did: how long they live, how they start moving and fall, how their size, see-through-ness and colour go
 * over their lives (each as a curve of three points), and whether each one differs (a random size or colour). A
 * particle that moves some other way (its position not what start speed and pull say) isn't made on the watch: its
 * emitter is sent as before, particle by particle.
 *
 * Recipe (C_RECIPE id, length, bytes):
 *   flags: 1 a picture's frame (else a solid square), 2 its size flickers (random up to the curve each frame),
 *          4 its colour is picked at birth between A and B, 8 its colour goes from A to B, 16 speeds in a box
 *          (else by angle and speed), 32 a steady pull (acc) follows
 *   [frame: texture u8, x u16, y u16, w u8, h u8 (texels)]
 *   life: shortest, longest u8 (20 ms)
 *   colours A, B u16: multiply r g b (2 bits each, bits 0-5), add r g b (bits 6-11), 0..3 = 0, 1/3, 2/3, 1
 *   size curve: at birth, knee age (x255), at the knee, at death u8 (square: 1/8 px; frame: scale x64)
 *   size factor, each one's own: lowest, highest u8 (x64)
 *   alpha curve: at birth, knee age, at the knee, at death u8 (0-255)
 *   speed: box: x lowest, highest, y lowest, highest s16 (1/8 px a second) | angle start, span u16 (1/65536 turn),
 *          speed lowest, highest u16 (1/8 px a second)
 *   [pull x, y s16 (1/8 px a second squared)]
 *   start offset x, y s8 (1/8 px), and back along its path by its speed x life x s8 (/64)
 */
final class Particles {
	static final boolean OFF = System.getenv("WATCH_PARTICLES") != null && System.getenv("WATCH_PARTICLES").equals("0");

	/** A recipe, as measured: its bytes, and the class of the particles it stands for. */
	static final class Recipe { byte[] bytes; Class<?> cls; float life; }

	private static final int NS = 24;           // particles measured
	private static final float DT = 1 / 60f;    // their update step (game seconds)
	private static final int MAX_STEPS = 360;   // (6 s: longer lives, or none, aren't made on the watch)

	private final Link link;
	Particles(Link link) { this.link = link; }

	// ---- measuring ----

	private static final class Cached { Recipe r; float[] sig; }
	private final IdentityHashMap<Object, Cached> cache = new IdentityHashMap<>();

	/** The recipe for what this factory makes (null: not one the watch can make). */
	Recipe recipe(Emitter.Factory f) {
		if (f == null) return null;
		Cached c = cache.get(f);
		// a factory can make different particles on different calls (Splash's colour and direction are static fields
		// set before each burst): a quick look at a few first particles says whether it changed
		float[] sig = signature(f);
		if (c != null && near(c.sig, sig)) return c.r;
		c = new Cached(); c.sig = sig; c.r = measure(f);
		cache.put(f, c);
		return c.r;
	}

	/** The recipe for a level decoration that makes its particles itself (no emitter): one particle's class. */
	Recipe recipe(Class<? extends Visual> cls) {
		Cached c = cache.get(cls);
		if (c != null) return c.r;
		c = new Cached();
		c.r = measure(new Emitter.Factory() {
			@Override public void emit(Emitter e, int index, float x, float y) {
				Visual v = (Visual) e.recycle(cls);
				try { cls.getMethod("reset", float.class, float.class).invoke(v, x, y); } catch (Exception ex) { throw new RuntimeException(ex); }
			}
		});
		cache.put(cls, c);
		return c.r;
	}

	/** Two looks at a factory's first particles alike: the same colours and kind, places and speeds close. (The pit
	 *  winds' direction drifts a little with every particle: measured again each time, each wind got a recipe of its
	 *  own, sent anew, and the emitter sent again with it, 300 bytes a second in the halls.) */
	private static boolean near(float[] a, float[] b) {
		if (a == null || b == null || a.length != b.length) return false;
		for (int i = 0; i < a.length; i++) {
			int f = i % 12;
			boolean exact = f == 8 || f == 9 || f == 10 || f == 11;   // colours, frame, class
			float tol = exact ? 1e-4f : 0.3f * Math.max(Math.abs(a[i]), Math.abs(b[i])) + 1f;
			if (Math.abs(a[i] - b[i]) > tol) return false;
		}
		return true;
	}

	private static float[] signature(Emitter.Factory f) {
		Emitter scratch = new Emitter();
		Random.pushGenerator(0x5EEDL);
		float el = Game.elapsed;
		try {
			for (int i = 0; i < 3; i++) f.emit(scratch, i, 0, 0);
			List<Gizmo> ps = GroupAccess.members(scratch);
			float[] s = new float[ps.size() * 12];
			int k = 0;
			for (Gizmo g : ps) {
				if (!(g instanceof Visual)) return null;
				Visual v = (Visual) g;
				float[] q = {v.x, v.y, v.speed.x, v.speed.y, v.acc.x, v.acc.y, v.scale.x, v.am, v.rm + v.gm * 2 + v.bm * 4, v.ra + v.ga * 2 + v.ba * 4,
						g instanceof Image && ((Image) g).frame() != null ? ((Image) g).frame().left : 0, g.getClass().hashCode()};
				System.arraycopy(q, 0, s, k, 12); k += 12;
			}
			return s;
		} catch (Throwable t) {
			return null;
		} finally { Game.elapsed = el; Random.popGenerator(); }
	}

	private Recipe measure(Emitter.Factory f) {
		Emitter scratch = new Emitter();
		Random.pushGenerator(0xC0FFEEL);
		float el = Game.elapsed;
		try {
			for (int i = 0; i < NS; i++) f.emit(scratch, i, 0, 0);
			List<Gizmo> ps = GroupAccess.members(scratch);
			if (ps.isEmpty()) return null;
			Class<?> cls = ps.get(0).getClass();
			for (Gizmo g : ps) if (g.getClass() != cls || !(g instanceof Image)) return null;
			Image first = (Image) ps.get(0);
			boolean pixel = first instanceof PseudoPixel;
			int tex = -1; int[] rect = null;
			if (!pixel) {
				tex = link.assetTex(first.texture);
				if (tex < 0 || first.frame() == null) return null;
				com.watabou.utils.RectF fr = first.frame();
				rect = new int[]{Math.round(fr.left * first.texture.width), Math.round(fr.top * first.texture.height),
						Math.round(fr.width() * first.texture.width), Math.round(fr.height() * first.texture.height)};
				for (Gizmo g : ps) {   // (one frame for all: Speck's type picks it)
					com.watabou.utils.RectF r = ((Image) g).frame();
					if (r == null || Math.abs(r.left - fr.left) > 1e-4 || Math.abs(r.top - fr.top) > 1e-4 || ((Image) g).texture != first.texture) return null;
				}
				if (rect[2] > 255 || rect[3] > 255) return null;
			}
			int n = ps.size();
			float[] life = new float[n], vx = new float[n], vy = new float[n], cx = new float[n], cy = new float[n], ax = new float[n], ay = new float[n];
			float[][] size = new float[n][17], alpha = new float[n][17];
			float[][][] col = new float[n][2][6];   // at birth, at death: rm gm bm ra ga ba
			int[] flick = new int[n];
			float worst = 0;
			for (int i = 0; i < n; i++) {
				Image p = (Image) ps.get(i);
				float w = p.width;
				vx[i] = p.speed.x; vy[i] = p.speed.y; ax[i] = p.acc.x; ay[i] = p.acc.y;
				cx[i] = p.x + w / 2; cy[i] = p.y + p.height / 2;
				col[i][0] = new float[]{p.rm, p.gm, p.bm, p.ra, p.ga, p.ba};
				// (its size, alpha and colour as drawn: after each update, as SPD draws it; reset() often leaves them
				// for the first update to set)
				List<float[]> steps = new ArrayList<>();   // age, size, alpha
				float x0 = p.x, y0 = p.y;
				int k = 0;
				Game.elapsed = DT;
				while (p.alive && k < MAX_STEPS) {
					p.update(); k++;
					if (p.alive) {
						steps.add(new float[]{k * DT, p.scale.x, p.am});
						if (steps.size() == 1) col[i][0] = new float[]{p.rm, p.gm, p.bm, p.ra, p.ga, p.ba};
						col[i][1] = new float[]{p.rm, p.gm, p.bm, p.ra, p.ga, p.ba};
					}
				}
				if (k >= MAX_STEPS) return null;   // (lives on, or far too long)
				if (steps.isEmpty()) steps.add(new float[]{DT, p.scale.x, 0});
				life[i] = k * DT;
				if (col[i][1] == null || col[i][1].length == 0) col[i][1] = col[i][0];
				// where it got to, against where its start speed and pull alone would take it
				float t = (k - 1) * DT;
				float px = x0 + vx[i] * t + ax[i] * DT * DT * k * (k - 1) / 2, py = y0 + vy[i] * t + ay[i] * DT * DT * k * (k - 1) / 2;
				worst = Math.max(worst, Math.max(Math.abs(px - p.x), Math.abs(py - p.y)));
				// its size and alpha over its life, at 17 ages
				for (int j = 0; j <= 16; j++) {
					float a = life[i] * j / 16f;
					float[] s = at(steps, a);
					size[i][j] = s[1]; alpha[i][j] = s[2];
				}
				for (int j = 2; j < steps.size(); j++) {
					float d1 = steps.get(j - 1)[1] - steps.get(j - 2)[1], d2 = steps.get(j)[1] - steps.get(j - 1)[1];
					if (d1 * d2 < 0) flick[i]++;
				}
			}
			if (worst > 4) return null;   // (it moves some other way: not made on the watch)
			Recipe r = new Recipe();
			r.cls = cls;
			r.bytes = encode(pixel, tex, rect, first.width, life, vx, vy, cx, cy, ax, ay, size, alpha, col, flick);
			float lm = 0; for (float l : life) lm = Math.max(lm, l);
			r.life = lm;
			return r;
		} catch (Throwable t) {
			return null;
		} finally { Game.elapsed = el; Random.popGenerator(); }
	}

	/** Size and alpha at an age, between the steps measured. */
	private static float[] at(List<float[]> steps, float a) {
		for (int j = 1; j < steps.size(); j++) {
			float[] p = steps.get(j - 1), q = steps.get(j);
			if (q[0] >= a) { float f = q[0] > p[0] ? Math.max(0, (a - p[0]) / (q[0] - p[0])) : 0; return new float[]{a, p[1] + (q[1] - p[1]) * f, p[2] + (q[2] - p[2]) * f}; }
		}
		return steps.get(a <= steps.get(0)[0] ? 0 : steps.size() - 1);
	}

	private byte[] encode(boolean pixel, int tex, int[] rect, float w, float[] life, float[] vx, float[] vy, float[] cx, float[] cy, float[] ax, float[] ay,
			float[][] size, float[][] alpha, float[][][] col, int[] flick) {
		int n = life.length;
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		// size: the mean curve, and each one's own factor against it
		float[] ms = new float[17], ma = new float[17];
		for (int j = 0; j <= 16; j++) { for (int i = 0; i < n; i++) { ms[j] += size[i][j]; ma[j] += alpha[i][j]; } ms[j] /= n; ma[j] /= n; }
		float sum = 0; for (float v : ms) sum += v;
		float m0 = 1, m1 = 1;
		if (sum > 0) {
			m0 = Float.MAX_VALUE; m1 = 0;
			for (int i = 0; i < n; i++) { float s = 0; for (float v : size[i]) s += v; float m = s / sum; m0 = Math.min(m0, m); m1 = Math.max(m1, m); }
			if (m1 - m0 < 0.15f) m0 = m1 = 1;
		}
		int flicks = 0, steps = 0; for (int i = 0; i < n; i++) { flicks += flick[i]; steps += Math.round(life[i] / DT); }
		boolean jitter = flicks > steps / 4;
		if (jitter) for (int j = 0; j <= 16; j++) ms[j] *= 2;   // (random up to the curve: the measured mean is half of it)
		// colour
		float[] lo = col[0][0], hi = col[0][0];
		boolean random = false;
		for (int i = 1; i < n; i++) if (dist(col[i][0], col[0][0]) > 0.2f) random = true;
		if (random) for (int i = 0; i < n; i++) { if (lum(col[i][0]) < lum(lo)) lo = col[i][0]; if (lum(col[i][0]) > lum(hi)) hi = col[i][0]; }
		float[] death = new float[6];
		for (int i = 0; i < n; i++) for (int c = 0; c < 6; c++) death[c] += col[i][1][c] / n;
		boolean interp = !random && dist(death, col[0][0]) > 0.15f;
		// speed: a box or angles and speeds, whichever covers less
		float vx0 = min(vx), vx1 = max(vx), vy0 = min(vy), vy1 = max(vy);
		double[] ang = new double[n]; float s0 = Float.MAX_VALUE, s1 = 0;
		for (int i = 0; i < n; i++) { ang[i] = Math.atan2(vy[i], vx[i]); float s = (float) Math.hypot(vx[i], vy[i]); s0 = Math.min(s0, s); s1 = Math.max(s1, s); }
		double[] sa = ang.clone(); Arrays.sort(sa);
		double gap = sa[0] + 2 * Math.PI - sa[n - 1], start = sa[0];
		for (int i = 1; i < n; i++) if (sa[i] - sa[i - 1] > gap) { gap = sa[i] - sa[i - 1]; start = sa[i]; }
		double span = 2 * Math.PI - gap;
		double volBox = (vx1 - vx0 + 0.5) * (vy1 - vy0 + 0.5), volPolar = span / 2 * (s1 * s1 - s0 * s0) + 0.25;
		boolean box = s1 < 0.01f || volBox <= volPolar;
		boolean pull = Math.abs(mean(ax)) > 0.01f || Math.abs(mean(ay)) > 0.01f;
		// start offset: a constant, and back along its path by speed x life
		float f = 0, num = 0, den = 0, mqx = 0, mqy = 0, mcx = mean(cx), mcy = mean(cy);
		for (int i = 0; i < n; i++) { mqx += vx[i] * life[i] / n; mqy += vy[i] * life[i] / n; }
		for (int i = 0; i < n; i++) {
			float qx = vx[i] * life[i] - mqx, qy = vy[i] * life[i] - mqy;
			num += (cx[i] - mcx) * qx + (cy[i] - mcy) * qy; den += qx * qx + qy * qy;
		}
		if (den > 1e-3f) f = num / den;
		f = Math.max(-2, Math.min(1.9f, f));
		float ox = mcx - f * mqx, oy = mcy - f * mqy;

		b.write((pixel ? 0 : 1) | (jitter ? 2 : 0) | (random ? 4 : 0) | (interp ? 8 : 0) | (box ? 16 : 0) | (pull ? 32 : 0));
		if (!pixel) { b.write(tex); w16(b, rect[0]); w16(b, rect[1]); b.write(rect[2]); b.write(rect[3]); }
		b.write(u8(Math.round(min(life) / 0.02f))); b.write(u8(Math.round(max(life) / 0.02f)));
		w16(b, colour(random ? lo : col[0][0])); w16(b, colour(random ? hi : interp ? death : col[0][0]));
		curve(b, ms, pixel ? 8 : 64);
		b.write(u8(Math.round(m0 * 64))); b.write(u8(Math.round(m1 * 64)));
		curve(b, ma, 255);
		if (box) { w16(b, Math.round(vx0 * 8)); w16(b, Math.round(vx1 * 8)); w16(b, Math.round(vy0 * 8)); w16(b, Math.round(vy1 * 8)); }
		else {
			w16(b, (int) Math.round(((start % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI) / (2 * Math.PI) * 65536) & 0xFFFF);
			w16(b, (int) Math.min(65535, Math.round(span / (2 * Math.PI) * 65536)));
			w16(b, Math.round(s0 * 8)); w16(b, Math.round(s1 * 8));
		}
		if (pull) { w16(b, Math.round(mean(ax) * 8)); w16(b, Math.round(mean(ay) * 8)); }
		b.write(s8(Math.round(ox * 8))); b.write(s8(Math.round(oy * 8))); b.write(s8(Math.round(f * 64)));
		return b.toByteArray();
	}

	/** Three points through a curve of 17 (birth to death): the knee where they fit it best. */
	private static void curve(ByteArrayOutputStream b, float[] v, float k) {
		int best = 8; float bestErr = Float.MAX_VALUE;
		for (int j = 1; j < 16; j++) {
			float e = 0;
			for (int i = 0; i <= 16; i++) {
				float p = i <= j ? v[0] + (v[j] - v[0]) * i / j : v[j] + (v[16] - v[j]) * (i - j) / (16 - j);
				e += (p - v[i]) * (p - v[i]);
			}
			if (e < bestErr) { bestErr = e; best = j; }
		}
		b.write(u8(Math.round(v[0] * k))); b.write(u8(Math.round(best * 255 / 16f))); b.write(u8(Math.round(v[best] * k))); b.write(u8(Math.round(v[16] * k)));
	}

	private static int colour(float[] c) {
		int v = 0;
		for (int i = 0; i < 6; i++) v |= Math.max(0, Math.min(3, Math.round(c[i] * 3))) << (2 * (2 - i % 3) + (i >= 3 ? 6 : 0));
		return v;
	}
	private static float dist(float[] a, float[] b) { float d = 0; for (int i = 0; i < 6; i++) d = Math.max(d, Math.abs(a[i] - b[i])); return d; }
	private static float lum(float[] c) { return c[0] + c[1] + c[2] + c[3] + c[4] + c[5]; }
	private static float min(float[] a) { float m = Float.MAX_VALUE; for (float v : a) m = Math.min(m, v); return m; }
	private static float max(float[] a) { float m = -Float.MAX_VALUE; for (float v : a) m = Math.max(m, v); return m; }
	private static float mean(float[] a) { float m = 0; for (float v : a) m += v; return m / a.length; }
	private static int u8(int v) { return Math.max(0, Math.min(255, v)); }
	private static int s8(int v) { return Math.max(-128, Math.min(127, v)) & 255; }
	private static void w16(ByteArrayOutputStream b, int v) { b.write(v & 255); b.write((v >> 8) & 255); }

	/** A recipe in words (tests: DEBUG_RECIPES=1). */
	static String describe(byte[] b) {
		int[] p = {0};
		java.util.function.IntSupplier u8 = () -> b[p[0]++] & 255, s8 = () -> b[p[0]++];
		java.util.function.IntSupplier u16 = () -> { int v = (b[p[0]] & 255) | (b[p[0] + 1] & 255) << 8; p[0] += 2; return v; };
		java.util.function.IntSupplier s16 = () -> (short) u16.getAsInt();
		int f = u8.getAsInt();
		StringBuilder s = new StringBuilder((f & 1) != 0 ? "frame" : "square");
		if ((f & 1) != 0) s.append(String.format(" tex %d at %d,%d %dx%d", u8.getAsInt(), u16.getAsInt(), u16.getAsInt(), u8.getAsInt(), u8.getAsInt()));
		if ((f & 2) != 0) s.append(" flickers");
		s.append(String.format(" life %d-%d ms", u8.getAsInt() * 20, u8.getAsInt() * 20));
		s.append(String.format(" colour %03x%s%03x", u16.getAsInt(), (f & 4) != 0 ? " or " : (f & 8) != 0 ? " to " : " / ", u16.getAsInt()));
		s.append(String.format(" size %d (%d: %d) %d", u8.getAsInt(), u8.getAsInt(), u8.getAsInt(), u8.getAsInt()));
		s.append(String.format(" x%d-%d", u8.getAsInt(), u8.getAsInt()));
		s.append(String.format(" alpha %d (%d: %d) %d", u8.getAsInt(), u8.getAsInt(), u8.getAsInt(), u8.getAsInt()));
		if ((f & 16) != 0) s.append(String.format(" speed x %d..%d y %d..%d", s16.getAsInt(), s16.getAsInt(), s16.getAsInt(), s16.getAsInt()));
		else s.append(String.format(" angle %d+%d speed %d..%d", u16.getAsInt(), u16.getAsInt(), u16.getAsInt(), u16.getAsInt()));
		if ((f & 32) != 0) s.append(String.format(" pull %d,%d", s16.getAsInt(), s16.getAsInt()));
		s.append(String.format(" offset %d,%d back %d", s8.getAsInt(), s8.getAsInt(), s8.getAsInt()));
		return s.toString();
	}

	// ---- emitters ----

	/** What the watch is told about one emitter (or a level decoration that acts as one). */
	static final class Desc {
		Recipe recipe; Visual follow; boolean fill, on, cells, sight;
		float x, y, w, h, interval; int quantity, starts, pre;
		int cellSize = 16, cl, ct, cw, ch; float[] bound = {0, 0, 1, 1}; byte[] bits;
	}

	private final WeakHashMap<Gizmo, Object[]> recipeOf = new WeakHashMap<>();
	// ---- the floors' decorations, one emitter a kind for the whole floor ----
	// Each pit's wind and water flow, torch, sewer drip, city flame and smoke, cave sparkle and hall fire stream is its
	// own emitter, shown only while its cell is in sight. Sent one by one as they came into sight and went out of it,
	// they took 150-250 bytes a second of a walk. So each kind is sent once a floor, as one emitter over all its cells
	// (K_EMIT over cells, E_SIGHT): the watch makes particles only in the cells it shows in sight (its fog of war).

	static final String[] DECOR = {"WindParticle$Wind", "FlowParticle$Flow", "SewerLevel$Sink", "PrisonLevel$Torch",
			"CityLevel$GreenFlame", "CityLevel$Smoke", "CavesLevel$Vein", "HallsLevel$Stream"};
	private static final Map<Class<?>, Boolean> isDecor = new HashMap<>();
	static boolean decoration(Gizmo g) {
		return isDecor.computeIfAbsent(g.getClass(), c -> { String n = c.getName(); for (String d : DECOR) if (n.endsWith("." + d)) return true; return false; });
	}

	/** One kind of decoration over a floor: the pseudo-picture Link keeps for it, and what it is. */
	static final class Decor extends com.watabou.noosa.Group { Desc desc; Class<?> cls; Decor companion; }
	/** A group's decorations (a floor puts them in two: the city's flames by the walls, its smoke on the floor). */
	private static final class DecorSet { int count = -1, round = -1; final Map<Class<?>, Decor> byClass = new HashMap<>(); }
	private final WeakHashMap<com.watabou.noosa.Group, DecorSet> decorSets = new WeakHashMap<>();
	private int round;
	/** A new round of the link (each group's decorations are counted once a round). */
	void round() { round++; }

	/** The floor-wide emitter for a decoration's kind (its group's decorations looked at again when their number changes). */
	Decor decor(Gizmo g) {
		com.watabou.noosa.Group grp = g.parent;
		if (grp == null) return null;
		DecorSet ds = decorSets.computeIfAbsent(grp, k -> new DecorSet());
		if (ds.round == round) return ds.byClass.get(g.getClass());
		ds.round = round;
		List<Gizmo> all = GroupAccess.members(grp);
		int n = 0; for (Gizmo x : all) if (x != null && decoration(x) && x.exists) n++;
		if (n != ds.count) {
			ds.count = n;
			Map<Class<?>, List<Gizmo>> by = new LinkedHashMap<>();
			for (Gizmo x : all) if (x != null && decoration(x) && x.exists) by.computeIfAbsent(x.getClass(), k -> new ArrayList<>()).add(x);
			Map<Class<?>, Decor> old = new HashMap<>(ds.byClass);
			ds.byClass.clear();
			for (Map.Entry<Class<?>, List<Gizmo>> e : by.entrySet()) {
				Decor d = old.getOrDefault(e.getKey(), new Decor());
				d.cls = e.getKey(); d.desc = floorWide(e.getValue());
				d.camera = e.getValue().get(0).camera();
				ds.byClass.put(e.getKey(), d);
				// a sewer drip's ripples, in the water below it, made on the watch too (each one sent was 30 bytes,
				// two a second for each drip in sight)
				if (d.desc != null && e.getKey().getName().endsWith("SewerLevel$Sink")) {
					Decor c = d.companion != null ? d.companion : new Decor();
					c.cls = com.shatteredpixel.shatteredpixeldungeon.effects.Ripple.class; c.desc = sinkRipples(e.getValue()); c.camera = d.camera;
					d.companion = c.desc != null ? c : null;
				}
			}
		}
		return ds.byClass.get(g.getClass());
	}

	/** One kind's decorations as one emitter over their cells (null: not one the watch can make). */
	@SuppressWarnings("unchecked")
	private Desc floorWide(List<Gizmo> all) {
		try {
			Gizmo g = all.get(0);
			Recipe r; float interval; float[] bound;
			int W = com.shatteredpixel.shatteredpixeldungeon.Dungeon.level.width();
			float S = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.SIZE;
			int pos0 = pos(g);
			if (g instanceof Emitter) {
				Emitter e = (Emitter) g;
				r = recipe(EmitterAccess.factory(e));
				interval = EmitterAccess.interval(e);
				float tx = (pos0 % W) * S, ty = (pos0 / W) * S;
				bound = new float[]{(e.x - tx) / S, (e.y - ty) / S, (e.x + e.width - tx) / S, (e.y + e.height - ty) / S};
			} else {
				boolean caves = g.getClass().getName().endsWith("CavesLevel$Vein");
				r = recipe((Class<? extends Visual>) Class.forName(caves ? "com.shatteredpixel.shatteredpixeldungeon.levels.CavesLevel$Sparkle" : "com.shatteredpixel.shatteredpixeldungeon.levels.HallsLevel$FireParticle"));
				interval = caves ? 0.5f : 1f;
				bound = new float[]{0, 0, 1, 1};
			}
			if (r == null) return null;
			int x0 = Integer.MAX_VALUE, y0 = Integer.MAX_VALUE, x1 = -1, y1 = -1;
			int[] cells = new int[all.size()];
			for (int i = 0; i < all.size(); i++) { int c = pos(all.get(i)); cells[i] = c; x0 = Math.min(x0, c % W); x1 = Math.max(x1, c % W); y0 = Math.min(y0, c / W); y1 = Math.max(y1, c / W); }
			Desc d = new Desc();
			d.recipe = r; d.on = true; d.cells = true; d.sight = true; d.interval = interval; d.bound = bound;
			d.cl = x0; d.ct = y0; d.cw = Math.min(255, x1 - x0 + 1); d.ch = Math.min(255, y1 - y0 + 1);
			d.bits = new byte[(d.cw * d.ch + 7) / 8];
			for (int c : cells) { int k = (c / W - y0) * d.cw + (c % W - x0); if (k >= 0 && k < d.cw * d.ch) d.bits[k >> 3] |= 1 << (k & 7); }
			return d;
		} catch (Exception e) { return null; }
	}

	// ---- ripples (on water: a step, a drop, a sewer drip): one particle each, made on the watch ----

	private Recipe rippleRecipe;
	private Recipe rippleRecipe() {
		if (rippleRecipe == null) rippleRecipe = measure(new Emitter.Factory() {
			@Override public void emit(Emitter e, int index, float x, float y) {
				((com.shatteredpixel.shatteredpixeldungeon.effects.Ripple) e.recycle(com.shatteredpixel.shatteredpixeldungeon.effects.Ripple.class)).reset(0);
			}
		});
		return rippleRecipe;
	}
	/** Where the sewer drips' ripples come (x8): the watch makes those (sinkRipples), so they aren't sent. */
	private final Set<Long> sinkSpots = new HashSet<>();
	private Desc sinkRipples(List<Gizmo> sinks) {
		Recipe r = rippleRecipe();
		if (r == null) return null;
		try {
			int W = com.shatteredpixel.shatteredpixeldungeon.Dungeon.level.width();
			float S = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.SIZE;
			List<Gizmo> below = new ArrayList<>();
			int x0 = Integer.MAX_VALUE, y0 = Integer.MAX_VALUE, x1 = -1, y1 = -1;
			List<Integer> cells = new ArrayList<>();
			for (Gizmo g : sinks) { int c = pos(g) + W; cells.add(c); x0 = Math.min(x0, c % W); x1 = Math.max(x1, c % W); y0 = Math.min(y0, c / W); y1 = Math.max(y1, c / W);
				sinkSpots.add(spot((c % W) * S, (c / W) * S - S / 2)); }
			Desc d = new Desc();
			d.recipe = r; d.on = true; d.cells = true; d.sight = true; d.interval = 0.5f; d.bound = new float[]{0, -0.5f, 0, -0.5f};
			d.cl = x0; d.ct = y0; d.cw = Math.min(255, x1 - x0 + 1); d.ch = Math.min(255, y1 - y0 + 1);
			d.bits = new byte[(d.cw * d.ch + 7) / 8];
			for (int c : cells) { int k = (c / W - y0) * d.cw + (c % W - x0); if (k >= 0 && k < d.cw * d.ch) d.bits[k >> 3] |= 1 << (k & 7); }
			return d;
		} catch (Exception e) { return null; }
	}
	private static long spot(float x, float y) { return (long) Math.round(x * 8) << 32 | (Math.round(y * 8) & 0xFFFFFFFFL); }

	/** A ripple, as one particle the watch makes (null: a sewer drip's, which the watch makes itself; or as before). */
	Desc ripple(Visual g) {
		if (OFF) return null;
		Recipe r = rippleRecipe();
		if (r == null) return null;
		Desc d = new Desc();
		if (sinkSpots.contains(spot(g.x, g.y))) { d.recipe = null; return d; }   // (made on the watch already)
		d.recipe = r; d.on = true; d.quantity = 1; d.pre = 1; d.x = g.x; d.y = g.y;
		return d;
	}

	private static int pos(Gizmo g) throws Exception {
		java.lang.reflect.Field f = g.getClass().getDeclaredField("pos"); f.setAccessible(true); return f.getInt(g);
	}

	/** Per emitter: the start last seen and how many it had made by then. */
	private final WeakHashMap<Gizmo, int[]> starts = new WeakHashMap<>();

	/** The emitter as the watch makes it, or null: its particles go as pictures, as before. */
	Desc describe(Gizmo g) {
		if (OFF) return null;
		if (g instanceof Decor) return ((Decor) g).desc;
		if (g instanceof Emitter) return emitter((Emitter) g);
		return null;
	}

	private static final Map<Class<?>, Boolean> ownEmit = new HashMap<>();
	/** Does this emitter class place its particles its own way (an emit() of its own)? */
	private static boolean ownEmit(Class<?> c) {
		return ownEmit.computeIfAbsent(c, k -> {
			for (Class<?> x = k; x != null && x != Emitter.class; x = x.getSuperclass())
				try { x.getDeclaredMethod("emit", int.class); return true; } catch (NoSuchMethodException e) { }
			return false;
		});
	}

	private Desc emitter(Emitter e) {
		Emitter.Factory f = EmitterAccess.factory(e);
		if (f == null) return null;
		boolean blob = e instanceof com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter;
		if (!blob && ownEmit(e.getClass())) return null;
		// (measured again only for a new start: a factory's particles may differ from start to start)
		Object[] rc = recipeOf.get(e);
		Recipe r;
		if (rc != null && rc[0] == f && (int) rc[1] == e.pebxelStarts) r = (Recipe) rc[2];
		else { r = recipe(f); recipeOf.put(e, new Object[]{f, e.pebxelStarts, r}); }
		if (r == null) return null;
		Desc d = new Desc();
		d.recipe = r; d.on = e.on; d.interval = EmitterAccess.interval(e); d.quantity = EmitterAccess.quantity(e);
		d.starts = e.pebxelStarts;
		int[] st = starts.get(e);
		if (st == null || st[0] != d.starts) { st = new int[]{d.starts, d.quantity > 0 ? Math.min(d.quantity, EmitterAccess.count(e)) : 0}; starts.put(e, st); }
		d.pre = st[1];
		Visual t = EmitterAccess.target(e);
		d.fill = e.fillTarget;
		if (t != null) { d.follow = t; d.x = e.x; d.y = e.y; d.w = e.width; d.h = e.height; }
		else { d.x = e.x; d.y = e.y; d.w = e.width; d.h = e.height; }
		if (blob) {
			try {
				java.lang.reflect.Field bf = com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter.class.getDeclaredField("blob"); bf.setAccessible(true);
				com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Blob b = (com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Blob) bf.get(e);
				com.shatteredpixel.shatteredpixeldungeon.levels.Level L = com.shatteredpixel.shatteredpixeldungeon.Dungeon.level;
				if (b == null || L == null || b.cur == null) return null;
				d.cells = true; d.follow = null; d.sight = !b.alwaysVisible;
				com.watabou.utils.Rect a = b.area;
				int W = L.width();
				d.cl = Math.max(0, a.left); d.ct = Math.max(0, a.top);
				d.cw = Math.max(0, Math.min(255, a.right - d.cl)); d.ch = Math.max(0, Math.min(255, a.bottom - d.ct));
				d.bound = new float[]{e instanceof com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter ? ((com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter) e).bound.left : 0,
						((com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter) e).bound.top, ((com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter) e).bound.right, ((com.shatteredpixel.shatteredpixeldungeon.effects.BlobEmitter) e).bound.bottom};
				d.bits = new byte[(d.cw * d.ch + 7) / 8];
				boolean any = false;
				for (int j = 0; j < d.ch; j++) for (int i = 0; i < d.cw; i++) {
					int cell = d.cl + i + (d.ct + j) * W;
					if (cell < 0 || cell >= L.length() || cell >= b.cur.length) continue;
					// (in sight is for the watch to say, by its fog: else the cells went again at every step of the hero)
					boolean vis = !b.alwaysVisible || L.heroFOV[cell] || L.mapped[cell] || L.visited[cell];
					if (vis && b.cur[cell] > 0) { int k = j * d.cw + i; d.bits[k >> 3] |= 1 << (k & 7); any = true; }
				}
				if (!any || b.volume <= 0) d.on = false;
			} catch (Exception x) { return null; }
		}
		return d;
	}
}
