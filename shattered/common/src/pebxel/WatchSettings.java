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

	/** The player's map zoom (Up): 0 far (8 px tiles), 1 middle (16), 2 close (32); kept across sessions (Pebble PD's KEY_ZOOM). */
	public static int zoom() { return GameSettings.getInt("pebxel_zoom", 1, 0, 2); }
	public static void zoom(int v) { if (v != zoom()) GameSettings.put("pebxel_zoom", v); }

	/** Music (Pixel Dungeon's two tunes) on the watch's speaker. */
	public static boolean music() { return GameSettings.getBoolean("pebxel_music", true); }
	public static void music(boolean v) { GameSettings.put("pebxel_music", v); Sounds.settingsChanged(); }

	/** Sound: 0 loud, 1 medium, 2 quiet, 3 off (all sound, music too). */
	public static int sound() { return GameSettings.getInt("pebxel_sound", 0, 0, 3); }
	public static void sound(int v) { GameSettings.put("pebxel_sound", v); Sounds.settingsChanged(); }
}
