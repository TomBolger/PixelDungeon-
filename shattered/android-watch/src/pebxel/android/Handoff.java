package pebxel.android;

import android.content.Context;
import android.content.Intent;
import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene;
import com.watabou.noosa.Game;
import java.io.File;

/**
 * Passing the game between the phone's screen and the watch. Both play from the same saves; only one
 * plays at a time. The phone screen tells the watch when it comes and goes; the watch leaves a mark
 * when it saves, so the phone screen knows to reload rather than carry on from an old copy.
 */
public final class Handoff {
	static final String ACTION_PHONE = "pebxel.PHONE", EXTRA_ACTIVE = "active";
	private static long phoneSeen;

	static File mark(Context c) { return new File(c.getFilesDir(), "watch-played"); }

	/** The watch saved the game (called in the watch process). */
	static void watchSaved(Context c) {
		try { File f = mark(c); if (!f.exists()) f.createNewFile(); f.setLastModified(System.currentTimeMillis()); } catch (Exception e) { }
	}

	/** The phone screen is back (main process). */
	public static void phoneResumed(Context c) {
		tellWatch(c, true);
		File f = mark(c);
		if (f.exists() && f.lastModified() > phoneSeen && Dungeon.hero != null) {
			// the game in memory is older than the save the watch made: go to the title screen to load it
			Game.runOnRenderThread(() -> Game.switchScene(TitleScene.class));
		}
		phoneSeen = System.currentTimeMillis();
	}

	public static void phonePaused(Context c) {
		phoneSeen = System.currentTimeMillis();
		tellWatch(c, false);
	}

	private static void tellWatch(Context c, boolean active) {
		try {
			c.startService(new Intent(c, WatchService.class).setAction(ACTION_PHONE).putExtra(EXTRA_ACTIVE, active));
		} catch (Exception e) { /* not allowed right now (background limits): the watch carries on */ }
	}
}
