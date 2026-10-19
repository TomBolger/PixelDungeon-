package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Badges;
import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.GamesInProgress;
import com.shatteredpixel.shatteredpixeldungeon.Rankings;
import com.shatteredpixel.shatteredpixeldungeon.journal.Journal;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.TitleScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.lang.reflect.Field;

/**
 * One game, two screens: the phone's own Shattered and the watch's. Both play from the same save files,
 * one at a time. Whoever stops playing saves and writes a note (who, which save slot, in a game or not);
 * whoever starts next forgets its old copies and carries on that game.
 * Call these on the game's own thread.
 */
public final class Shared {
	public static final String FILE = "pebxel-handoff.txt", PHONE = "phone", WATCH = "watch";
	/** Little files that say "the phone screen is open", "the watch game is running", and "the watch has
	 *  saved for request n". Separate files, so the two sides never overwrite each other's news. */
	public static final String PHONE_ON = "pebxel-phone-on", WATCH_ON = "pebxel-watch-on", WATCH_ACK = "pebxel-watch-ack";

	public static final class State {
		public String who = "";
		public boolean playing;
		public int slot;
		public long at;
	}

	public static State read() {
		State s = new State();
		String t = readText(FILE);
		if (t == null) return s;
		for (String line : t.split("\n")) {
			String[] kv = line.split("=", 2);
			if (kv.length < 2) continue;
			try {
				switch (kv[0].trim()) {
					case "who": s.who = kv[1].trim(); break;
					case "playing": s.playing = Boolean.parseBoolean(kv[1].trim()); break;
					case "slot": s.slot = Integer.parseInt(kv[1].trim()); break;
					case "at": s.at = Long.parseLong(kv[1].trim()); break;
				}
			} catch (Exception e) { }
		}
		return s;
	}

	public static void write(State s) {
		s.at = System.currentTimeMillis();
		writeText(FILE, "who=" + s.who + "\nplaying=" + s.playing + "\nslot=" + s.slot + "\nat=" + s.at + "\n");
	}

	public static String readText(String name) {
		try { return FileUtils.fileExists(name) ? FileUtils.getFileHandle(name).readString("UTF-8") : null; }
		catch (Exception e) { return null; }
	}

	/** Written to a spare file and then renamed, so the other side never reads half of it. */
	public static void writeText(String name, String text) {
		try {
			com.badlogic.gdx.files.FileHandle tmp = FileUtils.getFileHandle(name + ".new");
			tmp.writeString(text, false, "UTF-8");
			tmp.moveTo(FileUtils.getFileHandle(name));
		} catch (Exception e) { }
	}

	public static void mark(String name, boolean on) {
		try {
			if (on) writeText(name, Long.toString(System.currentTimeMillis()));
			else if (FileUtils.fileExists(name)) FileUtils.getFileHandle(name).delete();
		} catch (Exception e) { }
	}

	public static boolean marked(String name) { return FileUtils.fileExists(name); }

	public static long number(String name) {
		String t = readText(name);
		try { return t == null ? 0 : Long.parseLong(t.trim()); } catch (Exception e) { return 0; }
	}

	/**
	 * This side has just saved (Game.pause) and stops playing. From now on its copy in memory may go out
	 * of date, so it won't be saved again until takeOver.
	 */
	public static void release(String me) {
		if (Dungeon.pebxelHold) return;   // already stepped aside: the note is someone else's now
		State s = read();
		s.who = me;
		s.playing = Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.isAlive();
		s.slot = GamesInProgress.curSlot;
		write(s);
		Dungeon.pebxelHold = true;
	}

	/**
	 * This side starts playing. If the other side played last: forget the remembered copies of what it
	 * may have changed, and carry on its game (or go to the title screen if it wasn't in one).
	 * Returns true if anything was reloaded.
	 */
	public static boolean takeOver(String me) {
		State s = read();
		if (s.who.isEmpty() || s.who.equals(me)) { Dungeon.pebxelHold = false; return false; }
		forgetCopies();
		Dungeon.pebxelHold = false;
		boolean inGame = Game.scene() instanceof GameScene;
		// the other side's game if it was playing one; otherwise our own, as it was last saved (it may have
		// been played on over there and left at the title screen)
		int slot = s.playing ? s.slot : (inGame ? GamesInProgress.curSlot : 0);
		Dungeon.hero = null;
		if (slot > 0 && GamesInProgress.check(slot) != null) {
			GamesInProgress.curSlot = slot;
			InterlevelScene.mode = InterlevelScene.Mode.CONTINUE;
			Game.switchScene(InterlevelScene.class);
		} else {
			Game.switchScene(TitleScene.class);
		}
		// we're the one playing now
		s.who = me; s.playing = slot > 0; s.slot = slot;
		write(s);
		return true;
	}

	/** Shattered keeps some files in memory once read: save slot summaries, badges, journal, rankings. */
	public static void forgetCopies() {
		for (int i = 1; i <= GamesInProgress.MAX_SLOTS; i++) GamesInProgress.setUnknown(i);
		try {
			Field f = Badges.class.getDeclaredField("global"); f.setAccessible(true); f.set(null, null);
			Field j = Journal.class.getDeclaredField("loaded"); j.setAccessible(true); j.set(null, false);
			Field r = Rankings.class.getDeclaredField("records"); r.setAccessible(true); r.set(Rankings.INSTANCE, null);
		} catch (Exception e) { /* older or newer Shattered: these just stay cached */ }
		Badges.loadGlobal();
		Journal.loadGlobal();
	}
}
