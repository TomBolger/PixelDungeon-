package pebxel;

/** Where the game reports things the watch should show (buzz, sounds, effects). */
public final class Watch {
	public static boolean verbose = false;
	public static void event(String what, Object arg) { if (verbose) System.out.println("event " + what + " " + arg); }
}
