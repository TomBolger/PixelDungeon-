package pebxel;

import com.watabou.noosa.audio.Music;
import com.watabou.noosa.audio.Sample;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The watch's sound, as in the Pixel Dungeon port: Pixel Dungeon's own 45 sound effects and its two pieces of
 * music (the theme on the title screens, the game's tune in the dungeon), played by the watch's speaker.
 * Shattered's sounds are passed on as they play (C_SOUND, in the frame they belong to, so they line up with what
 * is shown); Shattered's newer sounds take the nearest of Pixel Dungeon's, and all its dungeon music is the one tune.
 */
public final class Sounds {
	private Sounds() { }

	/** Pixel Dungeon's effects, in the watch's numbering (tools/build_audio.py in the Pixel Dungeon port). */
	static final String[] PD = {"click", "step", "water", "hit", "miss", "gold", "item", "door_open", "descend", "levelup",
			"death", "drink", "eat", "read", "alert", "trap", "shatter", "burning", "secret", "puff", "falling", "lightning",
			"challenge", "lullaby", "blast", "meld", "cursed", "degrade", "evoke", "unlock", "tomb", "bones", "dewdrop",
			"teleport", "zap", "ray", "rocks", "plant", "ghost", "boss", "beacon", "bee", "charms", "mimic", "mastery"};
	/** Shattered's newer sounds: the nearest of Pixel Dungeon's (absent: none). */
	static final String[][] NEAR = {{"grass", "step"}, {"trample", "step"}, {"sturdy", "step"},
			{"hit_slash", "hit"}, {"hit_stab", "hit"}, {"hit_crush", "hit"}, {"hit_magic", "hit"}, {"hit_strong", "hit"},
			{"hit_parry", "hit"}, {"hit_arrow", "hit"}, {"atk_spiritbow", "miss"}, {"atk_crossbow", "miss"},
			{"debuff", "cursed"}, {"chargeup", "evoke"}, {"gas", "puff"}, {"chains", "unlock"}, {"scan", "beacon"}, {"mine", "rocks"}};
	static final Map<String, Integer> IDS = new HashMap<>();
	static {
		for (int i = 0; i < PD.length; i++) IDS.put(PD[i], i);
		for (String[] n : NEAR) IDS.put(n[0], IDS.get(n[1]));
	}
	public static final int MUS_THEME = 0, MUS_GAME = 1;

	private static final List<int[]> events = new ArrayList<>();
	private static int track = -1;

	/** Listen to the game's sound and music (once, at start). */
	public static void install() {
		Sample.pebxelPlay = Sounds::effect;
		Music.pebxelPlay = Sounds::music;
	}

	/** An effect's number on the watch (-1: none). */
	static int id(Object asset) {
		if (!(asset instanceof String)) return -1;
		String s = (String) asset;
		s = s.substring(s.lastIndexOf('/') + 1);
		int dot = s.lastIndexOf('.'); if (dot > 0) s = s.substring(0, dot);
		Integer i = IDS.get(s);
		return i == null ? -1 : i;
	}

	static synchronized void effect(Object asset) {
		int i = id(asset);
		if (i >= 0 && events.size() < 16) events.add(new int[]{0, i});
	}

	/** The title screens' music (the watch's own title screens don't go through Shattered's). */
	public static void theme() { music("theme"); }

	static synchronized void music(String asset) {
		int t = asset == null ? -1 : asset.contains("theme") ? MUS_THEME : MUS_GAME;
		if (t == track) return;
		track = t;
		events.add(new int[]{1, t < 0 ? 255 : t});
	}

	/** The settings as the watch takes them: bit 0 music, bit 1 sound, bits 2-3 volume (loud, medium, quiet). */
	public static int mask() {
		if (System.getenv("PEBXEL_NOSOUND") != null) return 0;
		int v;
		try { v = WatchSettings.sound(); } catch (Throwable t) { return 3; }   // (settings not open yet: on, loud)
		boolean m; try { m = WatchSettings.music(); } catch (Throwable t) { m = true; }
		return (m ? 1 : 0) | (v < 3 ? 2 | v << 2 : 0);
	}

	/** The settings changed: tell the watch. */
	public static synchronized void settingsChanged() { events.add(new int[]{2, mask()}); }

	/** What the watch should hear since the last call (kind, value). */
	static synchronized List<int[]> take() {
		List<int[]> r = new ArrayList<>(events); events.clear(); return r;
	}

	/** A watch starting over: the settings and the music now. */
	static synchronized List<int[]> state() {
		List<int[]> r = new ArrayList<>();
		r.add(new int[]{2, mask()}); r.add(new int[]{1, track < 0 ? 255 : track});
		return r;
	}
}
