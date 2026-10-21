package com.watabou.noosa.particles;

import com.watabou.noosa.Visual;

/** The watch link's look at an emitter's protected parts (pebxel.Particles). */
public final class EmitterAccess {
	private EmitterAccess() { }
	public static Emitter.Factory factory(Emitter e) { return e.factory; }
	public static float interval(Emitter e) { return e.interval; }
	public static int quantity(Emitter e) { return e.quantity; }
	public static int count(Emitter e) { return e.count; }
	public static Visual target(Emitter e) { return e.target; }
}
