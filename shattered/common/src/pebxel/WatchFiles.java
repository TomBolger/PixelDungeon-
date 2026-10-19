package pebxel;

import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.files.FileHandle;
import java.io.File;

/** Files the watch link adds to Shattered's (fonts, the picture list): in the app's assets on a phone,
 *  in shattered/common/assets when running on a computer. */
public final class WatchFiles {
	public static String pcRoot = System.getProperty("pebxel.common", "../../../../../common/assets/");

	/** Set by the phone app: read from its assets. */
	public static boolean inApp;

	public static FileHandle get(String path) {
		if (inApp) return Gdx.files.internal(path);
		return Gdx.files.absolute(new File(pcRoot, path).getAbsolutePath());
	}
}
