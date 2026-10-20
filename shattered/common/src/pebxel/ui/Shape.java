package pebxel.ui;

import com.watabou.noosa.Visual;

/**
 * Round-screen pieces Pebble PD draws itself (no picture): a filled disc, or a band between two radii and two
 * bearings (tenths of a degree, clockwise from straight up; the end bearing may lean outward by `slant` tenths
 * per pixel of radius, as the health bar's cut end does). Placed by x, y: the centre. Colour: 0xRRGGBB.
 */
public class Shape extends Visual {
	public static final int DISC = 1, ARC = 2, CUT = 3;
	public final int kind, rgb, r0, r1, b0, b1, slant;
	public int p2, p3;   // CUT: the box's width and height

	private Shape(int kind, int rgb, int r0, int r1, int b0, int b1, int slant) {
		super(0, 0, 0, 0);
		this.kind = kind; this.rgb = rgb; this.r0 = r0; this.r1 = r1; this.b0 = b0; this.b1 = b1; this.slant = slant;
	}

	public static Shape disc(int cx, int cy, int r, int rgb) { Shape s = new Shape(DISC, rgb, r, r, 0, 0, 0); s.x = cx; s.y = cy; return s; }

	/** A box cut by a circle (Pebble PD's round windows): the circle of radius r centred at cx, cy; the box at
	 *  (bx, by) w x h relative to that centre. */
	public static Shape cut(int cx, int cy, int r, int bx, int by, int w, int h, int rgb) {
		Shape s = new Shape(CUT, rgb, r, r, bx, by, 0); s.p2 = w; s.p3 = h; s.x = cx; s.y = cy; return s;
	}

	/** Above the centre only (as Pebble PD's arc): radii r0..r1, bearings b0..b1 (+ slant per pixel out). */
	public static Shape arc(int cx, int cy, int r0, int r1, int b0, int b1, int slant, int rgb) {
		Shape s = new Shape(ARC, rgb, r0, r1, b0, b1, slant); s.x = cx; s.y = cy; return s;
	}
}
