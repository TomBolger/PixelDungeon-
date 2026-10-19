package com.watabou.noosa;

import com.watabou.gltextures.SmartTexture;
import com.watabou.utils.RectF;

/** Reads drawing state that Shattered PD keeps protected, so the watch link can mirror it. */
public final class NoosaAccess {
	public static RectF frame(Image i) { return i.frame; }
	public static float[] matrix(Visual v) { v.updateMatrix(); return v.matrix; }

	public static SmartTexture texture(Tilemap t) { return t.texture; }
	public static TextureFilm film(Tilemap t) { return t.tileset; }
	public static int[] data(Tilemap t) { return t.data; }
	public static int mapWidth(Tilemap t) { return t.mapWidth; }
	public static int mapHeight(Tilemap t) { return t.mapHeight; }
	public static float cellW(Tilemap t) { return t.width / t.mapWidth; }
	public static float cellH(Tilemap t) { return t.height / t.mapHeight; }
	public static boolean needsRender(Tilemap t, int pos) { return t.needsRender(pos); }

	public static RectF outer(NinePatch n) { return n.outterF; }
	public static int[] margins(NinePatch n) { return new int[]{n.marginLeft, n.marginTop, n.marginRight, n.marginBottom}; }
	public static boolean flipH(NinePatch n) { return n.flipHorizontal; }
	public static boolean flipV(NinePatch n) { return n.flipVertical; }

	public static MovieClip.Animation curAnim(MovieClip m) { return m.curAnim; }
	public static int curFrame(MovieClip m) { return m.curFrame; }
	public static float frameTimer(MovieClip m) { return m.frameTimer; }

	public static Visual followTarget(Camera c) { return c.followTarget; }
	public static float[] pan(Camera c) { return new float[]{c.panTarget.x, c.panTarget.y, c.panIntensity, c.followDeadzone}; }

	public static float[] skin(SkinnedBlock s) { return new float[]{s.offsetX, s.offsetY, s.scaleX, s.scaleY}; }
}
