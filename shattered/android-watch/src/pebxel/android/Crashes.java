package pebxel.android;

import android.content.Context;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

/** Keeps the last few errors in a file (pebxel-errors.txt, in the app's files) so they can be looked at later. */
final class Crashes {
	private Crashes() { }

	static synchronized void note(Context c, Throwable t) {
		try {
			File f = new File(c.getExternalFilesDir(null) != null ? c.getExternalFilesDir(null) : c.getFilesDir(), "pebxel-errors.txt");
			if (f.length() > 200_000) f.delete();
			try (PrintWriter w = new PrintWriter(new FileWriter(f, true))) {
				w.println("---- " + new java.util.Date());
				t.printStackTrace(w);
			}
		} catch (Throwable ignored) { }
	}
}
