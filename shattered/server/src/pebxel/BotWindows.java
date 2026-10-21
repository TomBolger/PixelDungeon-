package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndBag;
import com.watabou.noosa.Gizmo;
import java.util.*;

/**
 * What the bot answers a window with: one of its buttons (by what it says), an item for an item selector, or Back.
 * The same answer is carried out by a direct press (AssistBot) or by watch buttons (AssistBot WATCH=1, through
 * Controls), so every window the bot meets is also checked to be operable with the watch's four buttons.
 */
public final class BotWindows {
	private BotWindows() { }

	/** An answer: press g (a button of w), or pick item from the selector, or (both null) Back. */
	public static final class Answer {
		public Gizmo g; public Item item; public String why;
		Answer(Gizmo g, Item item, String why) { this.g = g; this.item = item; this.why = why; }
		@Override public String toString() { return why + (g != null ? " [" + Ui.label((com.watabou.noosa.Group) g) + "]" : item != null ? " " + item.getClass().getSimpleName() : " (back)"); }
	}

	static final Map<String, Integer> seen = new TreeMap<>();   // window kinds met (coverage)
	static final Set<String> logged = new HashSet<>();
	static Window lastW; static int visits; static String lastKind = ""; static int sameKind;
	static Random rnd = new Random(1);

	static final String[] NEGATIVE = {"cancel", "no", "back", "close", "leave", "not now", "nevermind", "never mind", "return", "i'll decide later", "go to patreon", "not yet"};

	static boolean negative(String s) {
		String l = s.toLowerCase(Locale.ROOT).trim();
		for (String n : NEGATIVE) if (l.equals(n) || l.startsWith(n + " ") || l.startsWith(n + ",")) return true;
		return false;
	}

	static boolean lastBack;
	static Answer answer(Gizmo g, String why) { lastBack = g == null; return new Answer(g, null, why); }

	/** The answer for window w (first look at it this frame). */
	public static Answer decide(Window w) {
		Answer a = decide0(w);
		lastBack = a.g == null && a.item == null;
		return a;
	}

	static Answer decide0(Window w) {
		String kind = w.getClass().getSimpleName().isEmpty() ? w.getClass().getSuperclass().getSimpleName() + "$" : w.getClass().getSimpleName();
		if (w != lastW) {
			lastW = w; visits = 0; lastBack = false; seen.merge(kind, 1, Integer::sum);
			if (w.getClass().getName().equals(lastKind)) sameKind++; else { lastKind = w.getClass().getName(); sameKind = 0; }
		}
		visits++;
		List<Ui.Part> bs = Controls.buttons(w);
		List<String> labels = new ArrayList<>(); for (Ui.Part p : bs) labels.add(p.text);
		if (logged.add(w.getClass().getName()) || AssistBot.v) {
			StringBuilder tx = new StringBuilder(); for (Ui.Part p : Ui.parts(w)) if (!p.button) tx.append(p.text.replace('\n', ' ')).append(" | ");
			System.out.println("  WINDOW " + kind + " (" + w.getClass().getName() + ") buttons " + labels + " text: " + (tx.length() > 240 ? tx.substring(0, 240) : tx));
		}
		if (visits > 1 && lastBack) {   // Back didn't close it (it wants an answer): a "no" if there is one, else the first button
			for (Ui.Part p : bs) if (negative(p.text)) return answer(p.g, "back did nothing: no");
			if (!bs.isEmpty()) return answer(bs.get(0).g, "back did nothing: first");
		}
		if (sameKind > 6 || visits > 4) return answer(null, "same window again and again: back");
		// item selectors (a scroll, the alchemy, a quest asking for an item)
		WndBag.ItemSelector sel = selector(w);
		if (sel != null) {
			Item it = BotItems.answerSelector(sel);
			return it != null ? new Answer(null, it, "select") : new Answer(null, null, "nothing to select: back");
		}
		String n = w.getClass().getName();
		// windows that only tell something
		if (bs.isEmpty() || kind.startsWith("WndInfo") || kind.equals("WndMessage") || kind.equals("WndStory") || kind.equals("WndTitledMessage")
				|| kind.equals("WndJournal") || kind.equals("WndWatchHero") || kind.equals("WndHero") || kind.equals("WndWatchRecords") || kind.equals("WndKeyBindings")
				|| kind.equals("WndWatchMenu") || kind.equals("WndGame") || kind.equals("WndSettings") || kind.equals("WndChallenges") || kind.equals("WndSupportPrompt"))
			return new Answer(null, null, "read: back");
		// buying in a shop: buy (the cheat has gold)
		if (kind.equals("WndTradeItem")) { Gizmo b = byText(bs, "buy"); return b != null ? new Answer(b, null, "buy") : new Answer(null, null, "can't buy: back"); }
		// the hero's subclass and armour ability, quest rewards: one of the choices (each game its own), confirmed
		if (kind.equals("WndChooseSubclass") || kind.equals("WndChooseAbility") || n.contains("WndSadGhost") || n.contains("WndWandmaker") || n.contains("WndImp") || n.contains("WndBlacksmith")) {
			List<Gizmo> pos = positives(bs);
			if (pos.isEmpty()) for (Ui.Part p : bs) if (!negative(p.text)) pos.add(p.g);   // (item pictures: no words on them)
			if (pos.isEmpty()) return new Answer(null, null, "no choice: back");
			return new Answer(pos.get(rnd.nextInt(pos.size())), null, "choose");
		}
		// the Blacksmith's mine: MINES=0 keeps out of it
		if (n.contains("CavesLevel") && "0".equals(System.getenv("MINES"))) { Gizmo b = byText(bs, "not yet"); if (b != null) return new Answer(b, null, "not the mine"); }
		// questions (WndOptions and the like): the first positive answer; asked over and over: the next one
		List<Gizmo> pos = positives(bs);
		if (pos.isEmpty()) return new Answer(null, null, "only no: back");
		return new Answer(pos.get(Math.min(sameKind, pos.size() - 1)), null, "yes");
	}

	/** A screen other than the dungeon's (the Amulet's: Stay, to climb back to the surface with it; ASCEND=0: Exit, the win there). */
	public static Answer decideScene(com.watabou.noosa.Scene sc) {
		List<Ui.Part> bs = Controls.buttons(sc);
		String kind = sc.getClass().getSimpleName();
		if (kind.equals("AlchemyScene")) return BotAlchemy.scene(sc);
		if (kind.equals("AmuletScene")) {
			Gizmo b = byText(bs, "0".equals(System.getenv("ASCEND")) ? "call it a day" : "not done");
			if (b != null) return new Answer(b, null, "the Amulet");
		}
		List<Gizmo> pos = positives(bs);
		return pos.isEmpty() ? new Answer(null, null, "back") : new Answer(pos.get(0), null, "first");
	}

	static List<Gizmo> positives(List<Ui.Part> bs) {
		List<Gizmo> out = new ArrayList<>();
		for (Ui.Part p : bs) if (!negative(p.text) && !p.text.trim().isEmpty() && !p.text.equals(p.g.getClass().getSimpleName())) out.add(p.g);
		return out;
	}

	static Gizmo byText(List<Ui.Part> bs, String t) {
		for (Ui.Part p : bs) if (p.text.toLowerCase(Locale.ROOT).contains(t)) return p.g;
		return null;
	}

	@SuppressWarnings("unchecked")
	static WndBag.ItemSelector selector(Window w) {
		for (Class<?> c = w.getClass(); c != null && c != Object.class; c = c.getSuperclass())
			for (java.lang.reflect.Field f : c.getDeclaredFields())
				if (WndBag.ItemSelector.class.isAssignableFrom(f.getType())) {
					try { f.setAccessible(true); return (WndBag.ItemSelector) f.get(w); } catch (Exception e) { return null; }
				}
		return null;
	}

	/** Carried out directly (no watch): press, select, or Back. */
	public static void doDirect(Window w, Answer a) {
		if (a.item != null) {
			WndBag.ItemSelector sel = selector(w);
			if (sel != null) { if (sel.hideAfterSelecting()) w.hide(); sel.onSelect(a.item); return; }
		}
		if (a.g != null) { Ui.press(a.g); return; }
		w.onBackPressed();
	}

	/** A turn went by with no window: windows after it are not "the same one again". */
	static void turn() { lastKind = ""; sameKind = 0; }

	static void newGame(long seed) { seen.clear(); logged.clear(); lastW = null; lastKind = ""; sameKind = 0; rnd = new Random(seed * 31 + 7); }

	static String summary() { return seen.toString(); }

}
