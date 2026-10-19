package pebxel;

import com.badlogic.gdx.graphics.GL20;
import com.badlogic.gdx.graphics.GL30;
import java.lang.reflect.Proxy;
import java.nio.*;
import java.util.HashMap;

/**
 * A tiny software stand-in for the phone's graphics chip. It implements exactly the
 * one shader Shattered PD uses (NoosaScript: textured quads x colour multiply + add),
 * so the game's own drawing code produces real pictures with no GPU. Used for
 * screenshots and as the reference the watch renderer is checked against.
 */
public final class SoftGL {
	public final int W, H;
	public final int[] fb;

	private static final class Tex { int w, h; int[] px; }
	private final HashMap<Integer, Tex> tex = new HashMap<>();
	private final HashMap<Integer, ByteBuffer> bufs = new HashMap<>();
	private final HashMap<String, Integer> uniformLoc = new HashMap<>();
	private final HashMap<Integer, float[]> uniforms = new HashMap<>();
	private int ids = 1, boundTex, arrayBuf, elemBuf, locUCam = -1, locUModel = -1, locColorM = -1, locColorA = -1;
	private boolean blend, scissor, additive;
	private int sx, sy, sw, sh;
	private final Buffer[] attrPtr = new Buffer[2];
	private final int[] attrOff = new int[2], attrStride = new int[2], attrBufId = new int[2];
	private int clear = 0xFF000000, program;
	private final java.util.Set<Integer> lit = new java.util.HashSet<>();

	public SoftGL(int w, int h) { W = w; H = h; fb = new int[w * h]; }

	public GL20 proxy() {
		return (GL20) Proxy.newProxyInstance(SoftGL.class.getClassLoader(), new Class[]{GL20.class, GL30.class},
				(p, m, a) -> {
					Object r = call(m.getName(), a);
					if (r != null) return r;
					Class<?> t = m.getReturnType();
					if (t == int.class) return 0;
					if (t == boolean.class) return false;
					if (t == float.class) return 0f;
					if (t == long.class) return 0L;
					if (t == String.class) return "";
					return null;
				});
	}

	private Object call(String n, Object[] a) {
		switch (n) {
			case "hashCode": return System.identityHashCode(this);
			case "equals": return false;
			case "toString": return "SoftGL";
			case "glGenTexture": case "glGenBuffer": case "glCreateShader": case "glCreateProgram": return ids++;
			case "glGetShaderiv": case "glGetProgramiv": { IntBuffer b = (IntBuffer) a[2]; b.put(b.position(), 1); return null; }
			case "glGetIntegerv": { IntBuffer b = (IntBuffer) a[1]; b.put(b.position(), 4096); return null; }
			case "glUseProgram": program = (Integer) a[0]; return null;
			case "glGetUniformLocation": {
				String name = (String) a[1];
				if (name.equals("uColorM")) lit.add((Integer) a[0]);
				Integer l = uniformLoc.get(name);
				if (l == null) { l = uniformLoc.size(); uniformLoc.put(name, l); }
				if (name.equals("uCamera")) locUCam = l; else if (name.equals("uModel")) locUModel = l;
				else if (name.equals("uColorM")) locColorM = l; else if (name.equals("uColorA")) locColorA = l;
				return l;
			}
			case "glGetAttribLocation": return ((String) a[1]).equals("aUV") ? 1 : 0;
			case "glBindTexture": boundTex = (Integer) a[1]; return null;
			case "glDeleteTexture": tex.remove((Integer) a[0]); return null;
			case "glTexImage2D": texImage(a); return null;
			case "glTexSubImage2D": texSub(a); return null;
			case "glBindBuffer": if ((Integer) a[0] == GL20.GL_ARRAY_BUFFER) arrayBuf = (Integer) a[1]; else elemBuf = (Integer) a[1]; return null;
			case "glBufferData": bufferData(a); return null;
			case "glBufferSubData": bufferSub(a); return null;
			case "glVertexAttribPointer": {
				int i = (Integer) a[0];
				attrStride[i] = (Integer) a[4];
				if (a[5] instanceof Buffer) { attrPtr[i] = (Buffer) a[5]; attrOff[i] = ((Buffer) a[5]).position() * 4; attrBufId[i] = 0; }
				else { attrPtr[i] = null; attrOff[i] = (Integer) a[5]; attrBufId[i] = arrayBuf; }
				return null;
			}
			case "glUniformMatrix4fv": {
				float[] v = new float[16];
				if (a[3] instanceof float[]) System.arraycopy((float[]) a[3], (Integer) a[4], v, 0, 16);
				else { FloatBuffer b = (FloatBuffer) a[3]; for (int i = 0; i < 16; i++) v[i] = b.get(b.position() + i); }
				uniforms.put(program * 64 + (Integer) a[0], v); return null;
			}
			case "glUniform4f": uniforms.put(program * 64 + (Integer) a[0], new float[]{(Float) a[1], (Float) a[2], (Float) a[3], (Float) a[4]}); return null;
			case "glEnable": if ((Integer) a[0] == GL20.GL_BLEND) blend = true; else if ((Integer) a[0] == GL20.GL_SCISSOR_TEST) scissor = true; return null;
			case "glDisable": if ((Integer) a[0] == GL20.GL_BLEND) blend = false; else if ((Integer) a[0] == GL20.GL_SCISSOR_TEST) scissor = false; return null;
			case "glBlendFunc": additive = (Integer) a[1] == GL20.GL_ONE; return null;
			case "glScissor": sx = (Integer) a[0]; sy = (Integer) a[1]; sw = (Integer) a[2]; sh = (Integer) a[3]; return null;
			case "glClearColor": clear = 0xFF000000 | (c8((Float) a[0]) << 16) | (c8((Float) a[1]) << 8) | c8((Float) a[2]); return null;
			case "glClear": java.util.Arrays.fill(fb, clear); return null;
			case "glDrawElements": draw(a); return null;
		}
		return null;
	}

	private static int c8(float f) { return Math.max(0, Math.min(255, Math.round(f * 255))); }

	/** False on the phone: only sizes are kept (no pictures are drawn there, only captured as quads). */
	public boolean keepPixels = true;

	private void texImage(Object[] a) {
		Tex t = new Tex(); t.w = (Integer) a[3]; t.h = (Integer) a[4];
		tex.put(boundTex, t);
		if (!keepPixels) return;
		t.px = new int[t.w * t.h];
		if (a[8] != null) upload(t, 0, 0, t.w, t.h, (Integer) a[6], (Integer) a[7], (Buffer) a[8]);
	}

	private void texSub(Object[] a) {
		Tex t = tex.get(boundTex); if (t == null || t.px == null || a[8] == null) return;
		upload(t, (Integer) a[2], (Integer) a[3], (Integer) a[4], (Integer) a[5], (Integer) a[6], (Integer) a[7], (Buffer) a[8]);
	}

	private static void upload(Tex t, int x0, int y0, int w, int h, int format, int type, Buffer src) {
		ByteBuffer b;
		if (src instanceof ByteBuffer) b = ((ByteBuffer) src).duplicate();
		else if (src instanceof IntBuffer) { IntBuffer ib = ((IntBuffer) src).duplicate(); b = ByteBuffer.allocate(ib.remaining() * 4).order(ByteOrder.nativeOrder()); b.asIntBuffer().put(ib); }
		else return;
		b.order(ByteOrder.LITTLE_ENDIAN);
		int base = b.position();
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
			int c, k = y * w + x;
			if (format == GL20.GL_ALPHA) { int al = b.get(base + k) & 255; c = (al << 24) | 0xFFFFFF; }
			else if (format == GL20.GL_LUMINANCE) { int l = b.get(base + k) & 255; c = 0xFF000000 | l * 0x010101; }
			else if (format == GL20.GL_LUMINANCE_ALPHA) { int l = b.get(base + k * 2) & 255; c = ((b.get(base + k * 2 + 1) & 255) << 24) | l * 0x010101; }
			else if (format == GL20.GL_RGB && type == GL20.GL_UNSIGNED_BYTE) { int i = base + k * 3; c = 0xFF000000 | ((b.get(i) & 255) << 16) | ((b.get(i + 1) & 255) << 8) | (b.get(i + 2) & 255); }
			else if (format == GL20.GL_RGB) { int v = b.getShort(base + k * 2) & 0xFFFF; c = 0xFF000000 | ((v >> 11) * 255 / 31 << 16) | (((v >> 5) & 63) * 255 / 63 << 8) | (v & 31) * 255 / 31; }
			else { int i = base + k * 4; c = ((b.get(i + 3) & 255) << 24) | ((b.get(i) & 255) << 16) | ((b.get(i + 1) & 255) << 8) | (b.get(i + 2) & 255); }
			int tx = x0 + x, ty = y0 + y;
			if (tx < t.w && ty < t.h) t.px[ty * t.w + tx] = c;
		}
	}

	private void bufferData(Object[] a) {
		int id = (Integer) a[0] == GL20.GL_ARRAY_BUFFER ? arrayBuf : elemBuf;
		int size = (Integer) a[1];
		ByteBuffer dst = ByteBuffer.allocate(size).order(ByteOrder.nativeOrder());
		if (a[2] != null) copyInto(dst, 0, (Buffer) a[2], size);
		bufs.put(id, dst);
	}

	private void bufferSub(Object[] a) {
		int id = (Integer) a[0] == GL20.GL_ARRAY_BUFFER ? arrayBuf : elemBuf;
		ByteBuffer dst = bufs.get(id); if (dst == null) return;
		copyInto(dst, (Integer) a[1], (Buffer) a[3], (Integer) a[2]);
	}

	private static void copyInto(ByteBuffer dst, int off, Buffer src, int bytes) {
		if (src instanceof FloatBuffer) {
			FloatBuffer f = (FloatBuffer) src;
			for (int i = 0; i < bytes / 4; i++) dst.putFloat(off + i * 4, f.get(f.position() + i));
		} else if (src instanceof ShortBuffer) {
			ShortBuffer s = (ShortBuffer) src; for (int i = 0; i < bytes / 2; i++) dst.putShort(off + i * 2, s.get(s.position() + i));
		} else if (src instanceof ByteBuffer) {
			ByteBuffer s = (ByteBuffer) src; for (int i = 0; i < bytes; i++) dst.put(off + i, s.get(s.position() + i));
		}
	}

	private float attr(int i, int vert, int comp) {
		if (attrPtr[i] != null) {
			FloatBuffer f = (FloatBuffer) attrPtr[i];
			return f.get(attrOff[i] / 4 + vert * attrStride[i] / 4 + comp);
		}
		ByteBuffer b = bufs.get(attrBufId[i]);
		return b.getFloat(attrOff[i] + vert * attrStride[i] + comp * 4);
	}

	/** Optional: every quad drawn this frame, in pre-camera coordinates, for measuring what a watch link must carry. */
	public java.util.List<String> record;
	public boolean raster = true;

	/**
	 * When set, every quad drawn is added here as {GL texture, u, v, w, h (texels), x0, y0, x1, y1 (the
	 * picture's camera units), colour multiply r g b a, add r g b a}: how the link mirrors pictures
	 * that draw themselves in unusual ways (the key display).
	 */
	public java.util.List<float[]> capture;

	private void draw(Object[] a) {
		int count = (Integer) a[1];
		ShortBuffer idx; int first;
		if (a[3] instanceof Buffer) { idx = (ShortBuffer) a[3]; first = idx.position(); }
		else { ByteBuffer e = bufs.get(elemBuf); if (e == null) return; idx = e.asShortBuffer(); first = (Integer) a[3] / 2; }
		Tex t = tex.get(boundTex); if (t == null) return;
		float[] cam = uniforms.get(program * 64 + locUCam), mod = uniforms.get(program * 64 + locUModel);
		boolean l = lit.contains(program);
		float[] cm = l && uniforms.get(program * 64 + locColorM) != null ? uniforms.get(program * 64 + locColorM) : new float[]{1, 1, 1, 1};
		float[] ca = l && uniforms.get(program * 64 + locColorA) != null ? uniforms.get(program * 64 + locColorA) : new float[]{0, 0, 0, 0};
		if (cam == null || mod == null) return;
		float[] mvp = mul(cam, mod);
		if (record != null) {
			String camKey = Math.round(cam[0] * 1000) + "," + Math.round(cam[12] * 1000) + "," + Math.round(cam[13] * 1000);
			String col = java.util.Arrays.toString(cm) + java.util.Arrays.toString(ca);
			for (int k = 0; k + 5 < count; k += 6) {
				int v0 = idx.get(first + k) & 0xFFFF, v2 = idx.get(first + k + 2) & 0xFFFF;
				float x0 = attr(0, v0, 0), y0 = attr(0, v0, 1), x2 = attr(0, v2, 0), y2 = attr(0, v2, 1);
				float mx0 = mod[0] * x0 + mod[4] * y0 + mod[12], my0 = mod[1] * x0 + mod[5] * y0 + mod[13];
				float mx2 = mod[0] * x2 + mod[4] * y2 + mod[12], my2 = mod[1] * x2 + mod[5] * y2 + mod[13];
				record.add(camKey + "|" + boundTex + "|" + Math.round(attr(1, v0, 0) * t.w) + "," + Math.round(attr(1, v0, 1) * t.h) + "|"
						+ Math.round(mx0) + "," + Math.round(my0) + "," + Math.round(mx2) + "," + Math.round(my2) + "|" + col);
			}
		}
		if (capture != null) {
			for (int k = 0; k + 5 < count; k += 6) {
				int v0 = idx.get(first + k) & 0xFFFF, v2 = idx.get(first + k + 2) & 0xFFFF;
				float x0 = attr(0, v0, 0), y0 = attr(0, v0, 1), x2 = attr(0, v2, 0), y2 = attr(0, v2, 1);
				float u0 = attr(1, v0, 0) * t.w, w0 = attr(1, v0, 1) * t.h, u2 = attr(1, v2, 0) * t.w, w2 = attr(1, v2, 1) * t.h;
				capture.add(new float[]{boundTex, Math.min(u0, u2), Math.min(w0, w2), Math.abs(u2 - u0), Math.abs(w2 - w0),
						mod[0] * x0 + mod[4] * y0 + mod[12], mod[1] * x0 + mod[5] * y0 + mod[13],
						mod[0] * x2 + mod[4] * y2 + mod[12], mod[1] * x2 + mod[5] * y2 + mod[13],
						cm[0], cm[1], cm[2], cm[3], ca[0], ca[1], ca[2], ca[3]});
			}
		}
		if (!raster || t.px == null) return;
		for (int k = 0; k + 2 < count; k += 3) {
			float[] xs = new float[3], ys = new float[3], us = new float[3], vs = new float[3];
			for (int j = 0; j < 3; j++) {
				int v = idx.get(first + k + j) & 0xFFFF;
				float x = attr(0, v, 0), y = attr(0, v, 1);
				float cx = mvp[0] * x + mvp[4] * y + mvp[12], cy = mvp[1] * x + mvp[5] * y + mvp[13], cw = mvp[3] * x + mvp[7] * y + mvp[15];
				xs[j] = (cx / cw + 1) / 2 * W; ys[j] = (1 - cy / cw) / 2 * H;
				us[j] = attr(1, v, 0); vs[j] = attr(1, v, 1);
			}
			tri(xs, ys, us, vs, t, cm, ca);
		}
	}

	private static float[] mul(float[] a, float[] b) {
		float[] r = new float[16];
		for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
			float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s;
		}
		return r;
	}

	private void tri(float[] x, float[] y, float[] u, float[] v, Tex t, float[] cm, float[] ca) {
		int minX = (int) Math.max(0, Math.floor(Math.min(x[0], Math.min(x[1], x[2]))));
		int maxX = (int) Math.min(W - 1, Math.ceil(Math.max(x[0], Math.max(x[1], x[2]))));
		int minY = (int) Math.max(0, Math.floor(Math.min(y[0], Math.min(y[1], y[2]))));
		int maxY = (int) Math.min(H - 1, Math.ceil(Math.max(y[0], Math.max(y[1], y[2]))));
		if (scissor) { // GL scissor is bottom-left based
			minX = Math.max(minX, sx); maxX = Math.min(maxX, sx + sw - 1);
			minY = Math.max(minY, H - sy - sh); maxY = Math.min(maxY, H - sy - 1);
		}
		float area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
		if (Math.abs(area) < 1e-6f) return;
		for (int py = minY; py <= maxY; py++) for (int px = minX; px <= maxX; px++) {
			float fx = px + 0.5f, fy = py + 0.5f;
			float w0 = ((x[1] - fx) * (y[2] - fy) - (x[2] - fx) * (y[1] - fy)) / area;
			float w1 = ((x[2] - fx) * (y[0] - fy) - (x[0] - fx) * (y[2] - fy)) / area;
			float w2 = 1 - w0 - w1;
			if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;
			float uu = w0 * u[0] + w1 * u[1] + w2 * u[2], vv = w0 * v[0] + w1 * v[1] + w2 * v[2];
			int tx = Math.floorMod((int) Math.floor(uu * t.w), t.w), ty = Math.floorMod((int) Math.floor(vv * t.h), t.h);
			int c = t.px[ty * t.w + tx];
			float sa = ((c >>> 24) / 255f) * cm[3] + ca[3];
			float sr = (((c >> 16) & 255) / 255f) * cm[0] + ca[0];
			float sg = (((c >> 8) & 255) / 255f) * cm[1] + ca[1];
			float sb = ((c & 255) / 255f) * cm[2] + ca[2];
			sa = Math.max(0, Math.min(1, sa));
			if (sa <= 0) continue;
			int d = fb[py * W + px];
			float dr = ((d >> 16) & 255) / 255f, dg = ((d >> 8) & 255) / 255f, db = (d & 255) / 255f;
			float k = blend ? (additive ? 1 : 1 - sa) : 0, s = blend ? sa : 1;
			int r = c8(sr * s + dr * k), g = c8(sg * s + dg * k), b = c8(sb * s + db * k);
			fb[py * W + px] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}
	}
}
