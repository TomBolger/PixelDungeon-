package pebxel;

import com.shatteredpixel.shatteredpixeldungeon.Dungeon;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Game;
import com.watabou.noosa.Gizmo;
import com.watabou.noosa.Group;
import java.lang.reflect.Field;
import java.util.*;

/**
 * AssistBot WATCH=1: the bot plays through the watch, as a player with a Pebble would. Every frame runs as the
 * phone's watch session does (the game, Controls, the Link's diff, and WatchSim applying each message); every
 * decision is carried out with the watch's buttons through Controls (Down to the target, Select; Back along the
 * toolbar; long Select for the backpack; Up/Down/Select/Back in windows), a tap on the touch screen only where
 * no button reaches. Whatever the buttons can't reach is noted as a problem (and then done directly, so the
 * game goes on): that is what this mode is for.
 */
public final class WatchDriver {
	final WatchApp app; final Link link; final WatchSim sim; final Controls controls; final Touches touches = new Touches();
	static final int MAXMSG = 7984;
	int seq, peak, linkErrors, frameErrors; long peakNoted = -1000; String peakAt = "";
	static final int PEAK_NOTE = 70_000;   // (the watch has about 85 KB free; past this the model's memory is worth a look)
	long presses, taps, frames, bytes;
	static final Map<String, Integer> fallbacks = new TreeMap<>();

	WatchDriver(WatchApp app) {
		this.app = app;
		link = new Link(new Assets());
		sim = new WatchSim(new java.io.File("."), new Assets());
		controls = new Controls();
		link.planner = controls::plan;
	}

	/** One frame as WatchSession runs it, with the watch model applying every message. */
	void frame() {
		frames++;
		try {
			touches.frame(link, controls, Game.elapsed);
			Lockstep.frame(app);
			controls.frame();
		} catch (Throwable t) {
			frameErrors++;
			if (frameErrors < 5) { System.out.println("FRAME ERROR (scene " + Game.scene().getClass().getSimpleName() + "):"); t.printStackTrace(System.out); }
			Coverage.problem("frame error: " + t);
		}
		try {
			sim.advance(Game.elapsed);
			link.frame(Game.elapsed);
			for (byte[] m : link.take(MAXMSG)) { bytes += m.length; sim.receive(m); }
		} catch (Throwable t) {
			linkErrors++;
			if (linkErrors < 5) { System.out.println("LINK ERROR:"); t.printStackTrace(System.out); }
			Coverage.problem("link error: " + t);
		}
		int est = sim.heapEstimate();
		if (est > peak) {
			peak = est;
			if (est > PEAK_NOTE && frames - peakNoted > 300) {   // (where the watch's memory went: the model's parts, the screen)
				peakNoted = frames;
				com.shatteredpixel.shatteredpixeldungeon.ui.Window w = Ui.topWindow();
				peakAt = "depth " + Dungeon.depth + " " + Game.scene().getClass().getSimpleName() + (w == null ? "" : " " + w.getClass().getSimpleName()) + " objs " + sim.objCount() + " " + sim.heapParts();
				System.out.println("  WATCH PEAK " + est + " at " + peakAt);
				System.out.println("    the link's pictures by kind: " + census());
			}
		}
	}

	/** The Link's pictures (what the watch holds) by kind of thing, most first. */
	String firstMissile = "";
	String census() {
		Object m = get(link, "objs");
		if (!(m instanceof Map)) return "?";
		Map<String, Integer> c = new HashMap<>();
		for (Object g : ((Map<?, ?>) m).keySet()) {
			Gizmo gz = (Gizmo) g; String k = gz.getClass().getSimpleName().isEmpty() ? gz.getClass().getName().replaceAll(".*\\.", "") : gz.getClass().getSimpleName();
			k += "<" + (gz.parent == null ? "-" : gz.parent.getClass().getSimpleName().isEmpty() ? gz.parent.getClass().getName().replaceAll(".*\\.", "") : gz.parent.getClass().getSimpleName());
			c.merge(k, 1, Integer::sum);
		}
		int on = 0, living = 0, mm = 0;
		for (Object g : ((Map<?, ?>) m).keySet()) if (g instanceof com.shatteredpixel.shatteredpixeldungeon.effects.MagicMissile) { mm++; com.watabou.noosa.particles.Emitter e = (com.watabou.noosa.particles.Emitter) g; if (e.on) on++; living += e.countLiving();
			if (mm == 1) { Object st = null; try { java.lang.reflect.Field f = com.watabou.noosa.particles.Emitter.class.getDeclaredField("started"); f.setAccessible(true); st = f.get(e); } catch (Exception x) { }
				StringBuilder up = new StringBuilder(); for (com.watabou.noosa.Group p = e.parent; p != null; p = p.parent) up.append(p.getClass().getSimpleName()).append(p.active ? "" : "(inactive)").append(p.exists ? "" : "(dead)").append('/');
				firstMissile = ("(first missile: started " + st + " autoKill " + e.autoKill + " active " + e.active + " x " + (int) e.x + "," + (int) e.y + " up " + up + ")"); } }
		if (mm > 0) c.put("(missiles on " + on + " particles " + living + " frozen " + com.watabou.noosa.particles.Emitter.freezeEmitters + ")", 0);
		List<Map.Entry<String, Integer>> l = new ArrayList<>(c.entrySet()); l.sort((a, b) -> b.getValue() - a.getValue());
		return ((Map<?, ?>) m).size() + " " + l.subList(0, Math.min(12, l.size())) + (mm > 0 ? " missiles on " + on + " particles " + living + " frozen " + com.watabou.noosa.particles.Emitter.freezeEmitters + " " + firstMissile : "");
	}

	void frames(int n) { for (int i = 0; i < n; i++) frame(); }

	void press(int code) { controls.button(code); presses++; frames(2); }

	static void fallback(String what) { fallbacks.merge(what, 1, Integer::sum); Coverage.problem("watch can't: " + what); }

	// ---- reflection on Controls and the watch's windows ----
	static Object get(Object o, String name) {
		for (Class<?> c = o.getClass(); c != null; c = c.getSuperclass()) {
			try { Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o); } catch (NoSuchFieldException e) { } catch (Exception e) { return null; }
		}
		return null;
	}
	static Object getStatic(Class<?> c, String name) {
		try { Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(null); } catch (Exception e) { return null; }
	}
	/** The value of the first field of a type (a button's captured item or bag). */
	static Object captured(Object o, Class<?> type) {
		for (Field f : o.getClass().getDeclaredFields()) if (type.isAssignableFrom(f.getType())) { try { f.setAccessible(true); return f.get(o); } catch (Exception e) { } }
		return null;
	}
	int focus() { Object v = get(controls, "focus"); return v instanceof Integer ? (Integer) v : 0; }
	@SuppressWarnings("unchecked") List<Integer> targets() { Object v = get(controls, "targets"); return v instanceof List ? (List<Integer>) v : Collections.emptyList(); }
	int target() { Object v = get(controls, "target"); return v instanceof Integer ? (Integer) v : 0; }

	// ---- windows and other screens ----

	/** Bring the frame onto g in the window or screen menu (Down, at most n presses); true if it got there. */
	boolean focusTo(Group menu, Gizmo g, int n) {
		for (int i = 0; i <= n; i++) {
			if (Ui.topWindow() != (menu instanceof Window ? menu : null) && menu instanceof Window) return false;   // (it went away)
			if (menu instanceof pebxel.ui.Navigable) { if (((pebxel.ui.Navigable) menu).focused() == g) return true; }
			else {
				List<Ui.Part> bs = Controls.buttons(menu);
				int f = Math.max(0, Math.min(bs.size() - 1, focus()));
				if (!bs.isEmpty() && bs.get(f).g == g) return true;
			}
			if (i < n) press(Controls.DOWN);
		}
		return false;
	}

	/** A tall window: Down reads on to its end (half a screen a press). True if its end came into view. */
	boolean readToEnd(Window w) {
		if (!Controls.tall(w)) return true;
		int min = (int) (Controls.viewBottom(w) - w.camera.screenHeight());
		for (int i = 0; i < 40; i++) {
			if (w.camera.y <= min + 1) return true;
			int y = w.camera.y; press(Controls.DOWN);
			if (Ui.topWindow() != w) return true;
			if (w.camera.y == y && Controls.buttons(w).isEmpty()) return false;
		}
		return w.camera.y <= min + 1;
	}
	static final Set<String> readChecked = new HashSet<>();

	/** The bot's answer to window w, with the buttons. False: they couldn't (then it's done directly). */
	boolean answer(Window w, BotWindows.Answer a) {
		String kind = w.getClass().getName();
		if (readChecked.add(kind) && Controls.tall(w) && !readToEnd(w)) Coverage.problem("tall window won't scroll to its end: " + kind);
		if (Ui.topWindow() != w) return true;
		if (a.item != null) return bagTo(a.item, true);
		if (a.g == null) {
			press(Controls.BACK);
			return true;   // (whether it closed is the next look's business: BotWindows notices Back doing nothing)
		}
		if (!focusTo(w, a.g, 60)) { fallback("focus " + Ui.label((Group) a.g) + " in " + w.getClass().getSimpleName()); return false; }
		press(Controls.SELECT);
		return true;
	}

	boolean answerScene(com.watabou.noosa.Scene sc, BotWindows.Answer a) {
		if (a.g == null) { press(Controls.BACK); return true; }
		if (!focusTo(sc, a.g, 40)) { fallback("focus " + Ui.label((Group) a.g) + " on " + sc.getClass().getSimpleName()); return false; }
		press(Controls.SELECT);
		return true;
	}

	// ---- the backpack ----

	/** In the watch's backpack (open, or opened with a long Select), go to the item's cell and Select it. */
	boolean bagTo(Item item, boolean selecting) {
		Hero h = Dungeon.hero;
		for (int hop = 0; hop < 4; hop++) {
			Window w = Ui.topWindow();
			if (w == null) { press(Controls.SELECT | Controls.LONG); w = Ui.topWindow(); }
			if (!(w instanceof pebxel.ui.WndWatchBag)) { fallback("backpack (got " + (w == null ? "nothing" : w.getClass().getName()) + ")"); return false; }
			@SuppressWarnings("unchecked") List<com.shatteredpixel.shatteredpixeldungeon.ui.Button> areas = (List<com.shatteredpixel.shatteredpixeldungeon.ui.Button>) get(w, "rowAreas");
			@SuppressWarnings("unchecked") List<List<com.shatteredpixel.shatteredpixeldungeon.ui.Button>> cells = (List<List<com.shatteredpixel.shatteredpixeldungeon.ui.Button>>) get(w, "rowCells");
			int r = -1, k = -1;
			for (int i = 0; i < cells.size() && r < 0; i++) for (int j = 0; j < cells.get(i).size(); j++) if (captured(cells.get(i).get(j), Item.class) == item) { r = i; k = j; break; }
			if (r < 0) {   // in another bag: its tab
				Bag holder = null;
				for (Bag b : h.belongings.getBags()) if (b != h.belongings.backpack && b.items.contains(item)) holder = b;
				Bag cur = (Bag) get(w, "bag");
				if (holder == null && cur != h.belongings.backpack) holder = h.belongings.backpack;
				if (holder == null) { fallback("find " + BotItems.kind(item) + " in the backpack"); return false; }
				for (int i = 0; i < cells.size() && r < 0; i++) for (int j = 0; j < cells.get(i).size(); j++) if (captured(cells.get(i).get(j), Bag.class) == holder) { r = i; k = j; break; }
				if (r < 0) { fallback("bag tab for " + holder.getClass().getSimpleName()); return false; }
				if (!cellTo(w, areas, r, k)) return false;
				press(Controls.SELECT);   // (the tab: that bag opens in its place)
				continue;
			}
			if (!cellTo(w, areas, r, k)) return false;
			press(Controls.SELECT);
			return true;
		}
		fallback("reach " + BotItems.kind(item) + " in the bags");
		return false;
	}

	/** Pebble PD's two steps: Up/Down to the row, Select into it, Down to the cell. */
	private boolean cellTo(Window w, List<com.shatteredpixel.shatteredpixeldungeon.ui.Button> areas, int r, int k) {
		pebxel.ui.Navigable n = (pebxel.ui.Navigable) w;
		if (!Boolean.TRUE.equals(get(w, "rowMode"))) press(Controls.BACK);   // out to the rows first
		for (int i = 0; i < 12 && n.focused() != areas.get(r); i++) press(Controls.DOWN);
		if (n.focused() != areas.get(r)) { fallback("bag row " + r); return false; }
		press(Controls.SELECT);
		@SuppressWarnings("unchecked") List<List<com.shatteredpixel.shatteredpixeldungeon.ui.Button>> cells = (List<List<com.shatteredpixel.shatteredpixeldungeon.ui.Button>>) get(w, "rowCells");
		com.shatteredpixel.shatteredpixeldungeon.ui.Button want = cells.get(r).get(k);
		for (int i = 0; i < 8 && n.focused() != want; i++) press(Controls.DOWN);
		if (n.focused() != want) { fallback("bag cell " + r + "," + k); return false; }
		return true;
	}

	// ---- the dungeon ----

	/** Down until Controls' target is the cell (the shot at it when shot), then Select. False: it isn't offered. */
	boolean targetTo(int cell, boolean shot) {
		List<Integer> ts = targets();
		int j = -1;
		for (int i = 0; i < ts.size(); i++) if (Controls.cellOf(ts.get(i)) == cell && Controls.shot(ts.get(i)) == shot) { j = i; break; }
		if (j < 0) for (int i = 0; i < ts.size(); i++) if (Controls.cellOf(ts.get(i)) == cell) { j = i; break; }
		if (j < 0) return false;
		for (int i = 0; i < ts.size() + 1 && target() != j; i++) { controls.button(Controls.DOWN); presses++; frames(1); if (targets() != ts) return false; }
		if (target() != j) return false;
		press(Controls.SELECT);
		return true;
	}

	/** A tap on the cell, as the watch's touch screen sends it (aimed at the map's camera). False: not on the screen. */
	boolean tap(int cell) {
		Camera c = Camera.main;
		if (c == null || cell < 0 || !Controls.onScreen(cell)) return false;
		com.watabou.utils.PointF p = com.shatteredpixel.shatteredpixeldungeon.tiles.DungeonTilemap.tileCenterToWorld(cell);
		int x = Math.round((c.x + (p.x - c.scroll.x) * c.zoom) / 2f), y = Math.round((c.y + (p.y - c.scroll.y) * c.zoom) / 2f);
		int cam = link.camIdOf(c), st = link.nowMs();
		touches.add(new byte[]{1, (byte) ++seq, (byte) cam, 0, (byte) x, (byte) (x >> 8), (byte) y, (byte) (y >> 8), (byte) st, (byte) (st >> 8), (byte) (st >> 16), (byte) (st >> 24)});
		taps++; frames(3);
		return true;
	}

	/** Back along the toolbar to button k (WatchHud TB_..., B_...), then Select (held: a long press). */
	void toolbar(int k, boolean held) {
		for (int i = 0; i <= k; i++) press(Controls.BACK);
		press(Controls.SELECT | (held ? Controls.LONG : 0));
	}

	/** The bot's choice, with the buttons. False: they couldn't (then the caller does it directly). */
	boolean act(Assist.Target t) {
		Hero h = Dungeon.hero;
		if (t == null || h == null) return false;
		Assist.noted(t);
		boolean aiming = Controls.aiming();
		if (aiming) {
			if (targetTo(t.cell, false) || tap(t.cell)) return true;
			fallback("aim at a cell not offered (" + t.why + ")"); return false;
		}
		switch (t.kind) {
			case Assist.USE: return use(t);
			case Assist.WAIT:
				if (Assist.SEARCH_ACT.equals(t.action) || (t.action == null && t.cell == h.pos)) toolbar(pebxel.ui.WatchHud.TB_SEARCH, true);
				else { float n0 = com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now(); toolbar(pebxel.ui.WatchHud.TB_WAIT, Assist.REST_ACT.equals(t.action)); waitCheck(n0); }
				return true;
			case Assist.ENEMY:
				if (Assist.WAIT_ACT.equals(t.action)) { toolbar(pebxel.ui.WatchHud.TB_WAIT, false); return true; }
				break;
			case Assist.GO:
				if (t.cell == h.pos) { toolbar(pebxel.ui.WatchHud.TB_SEARCH, true); return true; }
				break;
		}
		if (t.cell < 0) return false;
		if (targetTo(t.cell, false)) return true;
		if (tap(t.cell)) { notOffered++; return true; }
		// off the screen and not in the list: the list's target that's the fewest steps from it (the watch's own way
		// there), or a tap on the furthest cell of the way that's on the screen
		int[] dg = stepsFrom(t.cell);
		List<Integer> ts = targets(); int best = -1, bd = dg[h.pos];
		for (int i = 0; i < ts.size(); i++) { int c = Controls.cellOf(ts.get(i)); if (c < 0 || c == h.pos) continue; if (dg[c] < bd) { bd = dg[c]; best = c; } }
		if (best >= 0 && targetTo(best, false)) { notOffered++; return true; }
		int on = -1, od = Integer.MAX_VALUE;
		for (int c = 0; c < dg.length; c++) if (dg[c] < od && dg[c] < dg[h.pos] && Controls.onScreen(c) && Dungeon.level.passable[c]) { od = dg[c]; on = c; }
		if (on >= 0 && tap(on)) { notOffered++; return true; }
		fallback("reach a cell (" + t.why + ")");
		return false;
	}
	int notOffered, waitsLost;

	/** Who hears the keys first (a window no longer shown but still listening takes them all). */
	static String keyHolders() {
		try {
			java.lang.reflect.Field f = com.watabou.input.KeyEvent.class.getDeclaredField("keySignal"); f.setAccessible(true);
			Object sig = f.get(null); java.lang.reflect.Field l = com.watabou.utils.Signal.class.getDeclaredField("listeners"); l.setAccessible(true);
			StringBuilder sb = new StringBuilder();
			int k = 0;
			for (Object o : new ArrayList<>((List<?>) l.get(sig))) {
				if (k++ >= 6) break;
				sb.append(o.getClass().getName().replaceAll(".*\\.", ""));
				if (o instanceof Gizmo) sb.append(((Gizmo) o).parent == null ? "(not shown)" : "(shown)");
				sb.append(' ');
			}
			return sb.toString();
		} catch (Throwable e) { return e.toString(); }
	}

	/** The toolbar's Wait pressed: a turn should go by (else say why not, the first few times). */
	void waitCheck(float n0) {
		Hero h = Dungeon.hero;
		for (int i = 0; i < 20 && h.ready && com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now() == n0; i++) frames(1);
		if (!h.ready || com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now() != n0) return;
		waitsLost++;
		Coverage.problem("toolbar Wait passed no time");
		if (waitsLost <= 3) {
			Object tb = getStatic(com.shatteredpixel.shatteredpixeldungeon.ui.Toolbar.class, "instance");
			Object bw = tb == null ? null : get(tb, "btnWait");
			System.out.println("  WAIT LOST: curAction " + h.curAction + " resting " + h.resting + " aiming " + Controls.aiming() + " window " + Ui.topWindow() + " onToolbar " + get(controls, "onToolbar")
					+ " toolbar " + (tb == null ? "none" : "visible " + ((Gizmo) tb).visible + " active " + ((Gizmo) tb).active) + " btnWait " + (bw == null ? "none" : "active " + ((Gizmo) bw).active + " visible " + ((Gizmo) bw).visible)
					+ " paralysed " + h.paralysed + " key listeners (first 6) " + keyHolders());
		}
	}

	/** Steps from a cell over the known, walkable map (Integer.MAX_VALUE: no way). */
	static int[] stepsFrom(int goal) {
		com.shatteredpixel.shatteredpixeldungeon.levels.Level L = Dungeon.level;
		int[] d = new int[L.length()]; Arrays.fill(d, Integer.MAX_VALUE);
		if (goal < 0 || goal >= d.length) return d;
		ArrayDeque<Integer> q = new ArrayDeque<>(); q.add(goal); d[goal] = 0;
		while (!q.isEmpty()) { int c = q.poll();
			for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = c + k;
				if (n < 0 || n >= d.length || d[n] != Integer.MAX_VALUE || !(L.passable[n] || L.avoid[n]) || !(L.visited[n] || L.mapped[n])) continue;
				d[n] = d[c] + 1; q.add(n); } }
		return d;
	}

	/** An item's action: the backpack (long Select), its cell, the action's button in the item window, then aim. */
	boolean use(Assist.Target t) {
		Hero h = Dungeon.hero;
		if (t.item == null || t.action == null) return false;
		if (!bagTo(t.item, false)) return false;
		Window w = Ui.topWindow();
		if (!(w instanceof pebxel.ui.WndWatchItem)) { fallback("item window for " + BotItems.kind(t.item) + " (got " + (w == null ? "nothing" : w.getClass().getSimpleName()) + ")"); return false; }
		String label = t.item.actionName(t.action, h);
		Gizmo b = null;
		for (Ui.Part p : Controls.buttons(w)) if (p.text.equalsIgnoreCase(label) || p.text.toUpperCase(Locale.ROOT).contains(label.toUpperCase(Locale.ROOT))) { b = p.g; break; }
		if (b == null) { fallback("action " + t.action + " of " + BotItems.kind(t.item) + " in its window"); press(Controls.BACK); press(Controls.BACK); return true; }
		if (!focusTo(w, b, 30)) { fallback("focus action " + t.action); return false; }
		press(Controls.SELECT);
		// the bag may stay open under it (Pebble PD: Back leaves it); then aim if asked
		Window after = Ui.topWindow();
		if (after instanceof pebxel.ui.WndWatchBag && !Controls.aiming()) { Object sel = get(after, "selector"); if (sel == null) press(Controls.BACK); }
		if (Controls.aiming()) {
			int c = t.cell >= 0 && t.cell != h.pos ? t.cell : -1;
			if (c >= 0 && (targetTo(c, false) || tap(c))) return true;
			if (c < 0 && targetTo(h.pos, false)) return true;
			press(Controls.BACK);
		}
		return true;
	}

	/** Spend talent points the watch's way: the portrait, the hero's Talents tab, a talent, Upgrade. */
	int talentTries, talentsSpent;
	boolean talents() {
		Hero h = Dungeon.hero;
		boolean any = false; for (int tier = 1; tier <= 4; tier++) if (h.talentPointsAvailable(tier) > 0) any = true;
		if (!any || Ui.topWindow() != null || talentTries > 400) return false;
		talentTries++;
		toolbar(pebxel.ui.WatchHud.B_PORTRAIT, false);
		Window w = Ui.topWindow();
		if (!(w instanceof pebxel.ui.WndWatchHero)) { fallback("hero window (got " + (w == null ? "nothing" : w.getClass().getSimpleName()) + ")"); if (w != null) press(Controls.BACK); return false; }
		Gizmo tab = null;   // (the tabs' words sit beside them: the Talents tab is number 1)
		for (Ui.Part p : Controls.buttons(w)) if (p.g.getClass().getSimpleName().equals("Tab") && Integer.valueOf(1).equals(get(p.g, "k"))) tab = p.g;
		if (tab == null || !focusTo(w, tab, 10)) { fallback("talents tab"); press(Controls.BACK); return false; }
		press(Controls.SELECT);
		Window th = Ui.topWindow();
		if (th == null || th == w) { fallback("talents page"); press(Controls.BACK); return false; }
		// a talent button that can take a point
		Gizmo pick = null;
		for (Ui.Part p : Controls.buttons(th)) {
			Object tal = get(p.g, "talent");
			if (!(tal instanceof com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent)) continue;
			com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent tt = (com.shatteredpixel.shatteredpixeldungeon.actors.hero.Talent) tal;
			int tier = 0; for (int i = 0; i < h.talents.size(); i++) if (h.talents.get(i).containsKey(tt)) tier = i + 1;
			if (tier > 0 && h.talentPointsAvailable(tier) > 0 && h.pointsInTalent(tt) < tt.maxPoints()) { pick = p.g; if (BotWindows.rnd.nextInt(3) == 0) break; }
		}
		if (pick == null) { fallback("a talent to upgrade on the page"); press(Controls.BACK); press(Controls.BACK); return false; }
		if (!focusTo(th, pick, 40)) { fallback("focus a talent"); press(Controls.BACK); press(Controls.BACK); return false; }
		press(Controls.SELECT);
		Window info = Ui.topWindow();
		Gizmo up = info == null ? null : BotWindows.byText(Controls.buttons(info), "upgrade");
		if (up == null || !focusTo(info, up, 10)) { fallback("talent upgrade button"); for (int i = 0; i < 3 && Ui.topWindow() != null; i++) press(Controls.BACK); return false; }
		press(Controls.SELECT);
		talentsSpent++;
		for (int i = 0; i < 4 && Ui.topWindow() != null; i++) press(Controls.BACK);
		return true;
	}

	String summary() {
		return (peakAt.isEmpty() ? "" : "peakat " + peakAt.replace(' ', '_').replace(':', '-').replace(',', '.').replace('=', '-') + " ") + String.format("talents %d presses %d taps %d notoffered %d frames %d bytes %d watchpeak %d linkerrors %d frameerrors %d unknown %s fallbacks %s",
				talentsSpent, presses, taps, notOffered, frames, bytes, peak, linkErrors, frameErrors, link.unknown, fallbacks);
	}
}
