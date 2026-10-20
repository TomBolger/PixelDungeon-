package pebxel;

import com.watabou.utils.GameSettings;

/** The watch's own settings (the menu's Settings page), kept with Shattered's. */
public final class WatchSettings {
	private WatchSettings() { }

	/** The assistant: 0 off (only enemies are offered), 1 near (targets on screen), 2 far (anywhere). */
	public static int assist() { return GameSettings.getInt("pebxel_assist", 1, 0, 2); }
	public static void assist(int v) { GameSettings.put("pebxel_assist", v); }

	/** Item assist: the assistant also offers items to use (eat, drink, throw...). */
	public static boolean itemAssist() { return GameSettings.getBoolean("pebxel_item_assist", true); }
	public static void itemAssist(boolean v) { GameSettings.put("pebxel_item_assist", v); }
}
