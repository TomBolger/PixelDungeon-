package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.food.*;
import com.shatteredpixel.shatteredpixeldungeon.items.armor.*;
import com.shatteredpixel.shatteredpixeldungeon.items.weapon.melee.MagesStaff;
import com.shatteredpixel.shatteredpixeldungeon.scenes.*;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.watabou.input.PointerEvent;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfUpgrade;
import java.lang.reflect.*;
import java.util.*;

/** Regression checks against the real game: safe food/rest/gear, complete drags and delayed menu touches.
 * Run from spd/core/src/main/assets with the same classpath as MenuTest. Fails with a nonzero exit status. */
public final class RefinementTest {
    static void check(boolean ok, String why) { if (!ok) throw new AssertionError(why); }
    static Object get(Object o, String name) throws Exception {
        Field f = o.getClass().getDeclaredField(name); f.setAccessible(true); return f.get(o);
    }
    static Assist.Target hands() throws Exception {
        Method setup = Assist.class.getDeclaredMethod("setup", java.util.function.IntPredicate.class);
        setup.setAccessible(true); setup.invoke(null, new Object[]{null});
        Method hands = Assist.class.getDeclaredMethod("hands", Assist.Target.class);
        hands.setAccessible(true); return (Assist.Target) hands.invoke(null, new Object[]{null});
    }
    static void show(Object w, int page) throws Exception {
        Method m = w.getClass().getDeclaredMethod("show", int.class); m.setAccessible(true); m.invoke(w, page);
    }
    static int seq;
    static void touch(Touches touches, Link link, Controls controls, int kind, int x, int y) {
        touches.add(new byte[]{(byte)kind, (byte)++seq, (byte)255, 0, (byte)x, 0, (byte)y, 0, 0, 0, 0, 0});
        touches.frame(link, controls, 1f/30); PointerEvent.processPointerEvents();
    }
    // ---- the Assistant obeys: what the player passes over isn't pressed on him again (Pebble PD's pass_hands) ----
    static Controls oc;
    @SuppressWarnings("unchecked")
    static java.util.List<Assist.Target> list() throws Exception { return (java.util.List<Assist.Target>) get(oc, "plan"); }
    static void frames(WatchApp app, int n) { for (int i = 0; i < n; i++) { app.frame(); oc.frame(); } }
    /** Frames until the hero is ready with a list made since game time `after` (or his place changed from `from`). */
    static Assist.Target planA(WatchApp app, float after, int from) throws Exception {
        for (int f = 0; f < 3000; f++) {
            frames(app, 1);
            Hero h = Dungeon.hero;
            if (h.ready && Ui.topWindow() == null && (com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now() > after || h.pos != from) && !list().isEmpty()
                    && !(Boolean) get(oc, "pending")) { frames(app, 2); return list().isEmpty() ? null : list().get(0); }
        }
        return null;
    }
    static float now() { return com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now(); }
    static void descend(WatchApp app) throws Exception {
        int d0 = Dungeon.depth;
        StairsTest.jump(StairsTest.stairs(false));   // (the real way down: the exit's transition)
        for (int f = 0; f < 5000; f++) {
            app.frame(); if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
            if (Dungeon.depth > d0 && Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready) break;
            Thread.sleep(1);
        }
        check(Dungeon.depth > d0, "down a floor");
        while (Ui.topWindow() != null) Ui.topWindow().hide();
        Gallery.calm();
    }
    static void obedience(WatchApp app) throws Exception {
        Hero h = Dungeon.hero; oc = new Controls(); h.lvl = 1; h.exp = 0;   // (no talent points to offer first)
        WatchSettings.assist(1); WatchSettings.itemAssist(true); Assist.reset();
        h.belongings.weapon = null; h.belongings.armor = new ClothArmor(); h.belongings.armor.identify();
        ScrollOfUpgrade up = new ScrollOfUpgrade(); up.identify(); h.belongings.backpack.items.add(up);
        Assist.Target a = planA(app, -1, -1);
        check(a != null && a.item == up, "the upgrade scroll is offered first: " + a);
        // skipped with Down, the next one chosen: not first again
        float t0 = now(); int p0 = h.pos;
        oc.button(Controls.DOWN); oc.button(Controls.SELECT);
        a = planA(app, t0, p0);
        check(a != null && a.item != up, "an upgrade passed over with Down is no longer first: " + a);
        for (Assist.Target e : list()) check(e.item != up, "an upgrade passed over is not offered again on this floor: " + list());
        // an objective to explore (or grass) passed over: not offered first again at once
        a = list().get(0);
        check(a.kind == Assist.GO || a.kind == Assist.PICKUP, "plan A walks somewhere: " + a);
        int o = a.obj >= 0 ? a.obj : a.cell; boolean grass = Dungeon.level.map[o] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.HIGH_GRASS;
        t0 = now(); p0 = h.pos;
        oc.button(Controls.DOWN); oc.button(Controls.SELECT);
        a = planA(app, t0, p0);
        check(a != null && (a.obj >= 0 ? a.obj : a.cell) != o, "a place passed over is not first again at once: " + o + " vs " + a);
        check(grass ? (Boolean) get(null, "grassSkip", Assist.class) : Assist.demoted(o), "a place passed over is held back: " + o + " now " + a + " list " + list());
        // a new floor: things stand afresh, the upgrade first again
        descend(app); h = Dungeon.hero;
        a = planA(app, -1, -1);
        check(a != null && a.item == up, "a new floor offers the upgrade again: " + a);
        // ignored: something else done instead (the toolbar's search, held) passes it over too
        t0 = now(); p0 = h.pos;
        Object hud0 = get(oc, "hud"); Field hlF = pebxel.ui.WatchHud.class.getField("highlight");
        for (int i = 0; i < 8 && (Integer) hlF.get(hud0) != pebxel.ui.WatchHud.TB_SEARCH; i++) oc.button(Controls.BACK);   // (Item assist: Back walks the Assistant's order)
        check((Integer) hlF.get(hud0) == pebxel.ui.WatchHud.TB_SEARCH, "Back reaches the search button");
        oc.button(Controls.SELECT | Controls.LONG);
        a = planA(app, t0, p0);
        check(a == null || a.item != up, "an upgrade ignored for something else is no longer first: " + a);
        // chosen, it is honoured (the bag opens on the armour for the player to confirm)
        Assist.reset(); Field st = Controls.class.getDeclaredField("targetsStale"); st.setAccessible(true); st.set(oc, true);
        a = planA(app, -1, h.pos);
        check(a != null && a.item == up, "after a reset the upgrade is offered: " + a);
        oc.button(Controls.SELECT); frames(app, 5);
        check(Ui.topWindow() != null, "Select on the upgrade opens its choice for the player");
        while (Ui.topWindow() != null) Ui.topWindow().hide();
        h.belongings.backpack.items.remove(up);
        // the camera: every target Down shows lies whole in the play area (never under the HUD), Near and Far
        for (int mode = 1; mode <= 2; mode++) {
            WatchSettings.assist(mode); Assist.reset(); st.set(oc, true);
            planA(app, -1, h.pos);
            int n = list().size();
            check(n > 1, "a list to cycle (" + mode + ")");
            for (int i = 0; i <= n; i++) {
                frames(app, 90);   // (the camera settles)
                Method ct = Controls.class.getDeclaredMethod("currentTarget"); ct.setAccessible(true);
                int c = (Integer) ct.invoke(oc);
                if (c >= 0) check(Controls.onScreen(c), "target " + c + " shows whole, clear of the HUD (assist " + mode + ", entry " + i + ")");
                oc.button(Controls.DOWN);
            }
        }
        // the map dragged away from the target: the HUD points to it from the edge, the box on the map hidden
        com.watabou.noosa.Camera.main.shift(new com.watabou.utils.PointF(400, 0)); frames(app, 3);
        Object hud = get(oc, "hud"), edge = get(hud, "edge", pebxel.ui.WatchHud.class), mk = get(oc, "marker");
        check(((com.watabou.noosa.Group) edge).visible, "a target off the view is pointed to from the edge");
        check(!((com.watabou.noosa.Image) get(mk, "img", mk.getClass())).visible, "its box on the map is hidden meanwhile");
        oc.button(Controls.DOWN); frames(app, 90);
        check(!((com.watabou.noosa.Group) edge).visible, "a Down brings the view back to show the target");
        // the hero too: after each Down the hero and the target both show whole in the play area (Near and Far)
        for (int mode = 1; mode <= 2; mode++) {
            WatchSettings.assist(mode); Assist.reset(); st.set(oc, true);
            planA(app, -1, h.pos);
            for (int i = 0; i < list().size(); i++) {
                oc.button(Controls.DOWN); frames(app, 90);
                Method ct = Controls.class.getDeclaredMethod("currentTarget"); ct.setAccessible(true);
                int c = (Integer) ct.invoke(oc);
                if (c >= 0) check(Controls.onScreen(c) && Controls.onScreen(h.pos), "after Down the hero and target " + c + " both show whole (assist " + mode + ", entry " + i + ")");
            }
        }
        // Far (Pebble PD's cam_to): a far target zooms out as far as it takes to show both; a Down to a near one doesn't
        // zoom (no churn); the list made again with a near target first zooms back in to the player's zoom
        com.watabou.noosa.Camera cam = com.watabou.noosa.Camera.main;
        Field uz = Controls.class.getDeclaredField("userZoom"); uz.setAccessible(true); uz.set(oc, 1);   // (16 px tiles)
        int W = Dungeon.level.width(), hp = h.pos, far = -1, near = -1;
        for (int d = 14; d >= 12 && far < 0; d--) for (int sgn : new int[]{1, -1}) { int x = hp % W + sgn * d; if (far < 0 && x > 0 && x < W - 1) far = hp + sgn * d; }
        for (int n : new int[]{1, -1, W, -W}) if (near < 0 && n + hp >= 0 && n + hp < Dungeon.level.length()) near = hp + n;
        check(far >= 0 && near >= 0, "cells for the zoom check");
        final int fFar = far, fNear = near;
        Controls.legacy = aim -> new ArrayList<>(Arrays.asList(fNear, fFar));
        WatchSettings.assist(2); st.set(oc, true); frames(app, 90);
        check(cam.zoom == 2, "Far, a near target: the player's zoom (" + cam.zoom + ")");
        oc.button(Controls.DOWN); frames(app, 90);
        check(cam.zoom < 2 && Controls.onScreen(far) && Controls.onScreen(hp), "Far, a far target: zoomed out (" + cam.zoom + "), the hero and the target both whole");
        oc.button(Controls.DOWN); frames(app, 90);
        check(cam.zoom < 2 && Controls.onScreen(near) && Controls.onScreen(hp), "a Down to a near target doesn't zoom back and forth (" + cam.zoom + ")");
        st.set(oc, true); frames(app, 90);
        check(cam.zoom == 2 && Controls.onScreen(near) && Controls.onScreen(hp), "the list made again, a near target: zoomed back in (" + cam.zoom + ")");
        // Up keeps the player's zoom this turn (the target in the middle); long Up locks the zoom on the hero
        oc.button(Controls.DOWN); frames(app, 90); float zf = cam.zoom;
        oc.button(Controls.UP | Controls.LONG); frames(app, 30);
        check(Controls.zlock && cam.zoom == zf, "long Up in Far: the zoom locked where it is (" + cam.zoom + ")");
        oc.button(Controls.UP | Controls.LONG); frames(app, 30);
        check(!Controls.zlock, "long Up again: unlocked");
        check((Integer) uz.get(oc) == 0, "the zoom locked became the player's (Pebble PD 1.0)"); uz.set(oc, 1);
        // the pointers: the map dragged away, the target's and the hero's framed at the edge; a tap on the target's
        // brings the view to it, on the hero's back to him
        cam.shift(new com.watabou.utils.PointF(600, 0)); frames(app, 3);
        Object heroEdge = get(hud, "heroEdge", pebxel.ui.WatchHud.class);
        check(((com.watabou.noosa.Group) edge).visible && ((com.watabou.noosa.Group) heroEdge).visible, "dragged away: both pointers framed at the edge");
        Object hooks = get(oc, "hooks"); Method tag = hooks.getClass().getMethod("tag", int.class, boolean.class); tag.setAccessible(true);
        tag.invoke(hooks, 5, false); frames(app, 90);
        check(!((com.watabou.noosa.Group) edge).visible && !((com.watabou.noosa.Group) heroEdge).visible, "a tap on the target's pointer: the view on the target and the hero");
        cam.shift(new com.watabou.utils.PointF(600, 0)); frames(app, 3);
        tag.invoke(hooks, 6, false); frames(app, 90);
        check(!((com.watabou.noosa.Group) heroEdge).visible && cam.zoom == 2, "a tap on the hero's pointer: back on him at the player's zoom (" + cam.zoom + " " + ((com.watabou.noosa.Group) heroEdge).visible + " " + cam.followTarget() + ")");
        Controls.legacy = null;
        WatchSettings.assist(2); oc = null;
        System.out.println("obedience: passed over with Down, ignored, a new floor, chosen; camera: targets clear of the HUD, hero and target in view, Far's zoom out and back in, lock, pointers: ok");
    }
    // ---- Pebble PD's buttons: Back resets the list and walks the toolbar in the Assistant's order; Select with nothing
    // listed: the camera back, else what lies underfoot, else a search; any button stops a walk; a blow buzzes ----
    static void buttons(WatchApp app) throws Exception {
        Hero h = Dungeon.hero; oc = new Controls();
        WatchSettings.assist(1); WatchSettings.itemAssist(true); Assist.reset();
        planA(app, -1, -1); frames(app, 3);
        Field tf = Controls.class.getDeclaredField("target"); tf.setAccessible(true);
        Object hud = get(oc, "hud"); Field hlF = pebxel.ui.WatchHud.class.getField("highlight");
        if (list().size() > 1) {
            oc.button(Controls.DOWN); check((Integer) tf.get(oc) == 1, "Down: the second suggestion");
            oc.button(Controls.BACK); check((Integer) tf.get(oc) == 0, "Back: the list back to its first suggestion");
        } else oc.button(Controls.BACK);
        int[] order = (int[]) get(oc, "hlOrder");
        check((Integer) hlF.get(hud) == order[0], "Item assist: Back starts on the Assistant's first button " + Arrays.toString(order));
        Set<Integer> seen = new HashSet<>(); seen.add(order[0]);
        for (int i = 1; i < 8; i++) { oc.button(Controls.BACK); seen.add((Integer) hlF.get(hud)); check((Integer) hlF.get(hud) == order[i], "Back walks the ranked order"); }
        check(seen.size() == 8, "every button once: " + seen);
        oc.button(Controls.BACK); check((Integer) hlF.get(hud) == -1, "then none");
        // Select with nothing listed
        Controls.legacy = aim -> new ArrayList<>();
        Field st = Controls.class.getDeclaredField("targetsStale"); st.setAccessible(true); st.set(oc, true); frames(app, 3);
        com.watabou.noosa.Camera cam = com.watabou.noosa.Camera.main;
        cam.shift(new com.watabou.utils.PointF(300, 0)); frames(app, 3);
        float t0 = now(); oc.button(Controls.SELECT); frames(app, 60);
        check(now() == t0 && Math.abs(cam.scroll.x + cam.width / 2f - h.sprite.center().x) < 40, "Select, nothing listed, the map dragged off: the camera back on the hero, no turn");
        Item it = new com.shatteredpixel.shatteredpixeldungeon.items.food.Food(); Dungeon.level.drop(it, h.pos);
        oc.button(Controls.SELECT); frames(app, 30);
        check(Dungeon.level.heaps.get(h.pos) == null && h.belongings.contains(it), "Select, nothing listed: what lies underfoot picked up");
        while (Ui.topWindow() != null) Ui.topWindow().hide();
        t0 = now(); int p0 = h.pos; oc.button(Controls.SELECT); frames(app, 30);
        check(now() > t0 && h.pos == p0, "Select, nothing listed, nothing underfoot: a search");
        Controls.legacy = null; st.set(oc, true);
        // any button stops a walk (the step under way ends)
        com.shatteredpixel.shatteredpixeldungeon.levels.Level L0 = Dungeon.level; Arrays.fill(L0.visited, true);
        int[] d = walks(L0, h.pos); int far = -1;
        for (int c = 0; c < d.length; c++) if (d[c] >= 4 && (L0.visited[c] || L0.mapped[c]) && (far < 0 || d[c] > d[far])) far = c;
        check(far >= 0, "somewhere to walk: reach " + Arrays.stream(d).max().getAsInt());
        GameScene.handleCell(far);
        for (int f = 0; f < 300 && h.pos == p0; f++) frames(app, 1);
        int p1 = h.pos; oc.button(Controls.UP);
        for (int f = 0; f < 120 && !h.ready; f++) frames(app, 1);
        check(h.ready && Dungeon.level.distance(h.pos, p1) <= 1 && h.pos != far, "Up stopped the walk where he was (" + p1 + " -> " + h.pos + ", going to " + far + ")");
        // a blow: a short buzz and the light (C_EVENT); death: the long one
        Feedback.take(); h.HP -= 1; frames(app, 1);
        check((Feedback.take() & (Feedback.HURT | Feedback.LIGHT)) == (Feedback.HURT | Feedback.LIGHT), "hurt: a buzz and the light");
        WatchSettings.assist(2); oc = null;
        System.out.println("buttons: Back resets the list and walks the ranked toolbar; Select with nothing listed re-centres, picks up, searches; a press stops a walk; a blow buzzes: ok");
    }
    // ---- the Assistant's choice of places: near before far, keys, a held key's door, no stairs with the floor not done ----
    static int[] walks(com.shatteredpixel.shatteredpixeldungeon.levels.Level L, int from) {
        int[] d = new int[L.length()]; Arrays.fill(d, -1); ArrayDeque<Integer> q = new ArrayDeque<>(); d[from] = 0; q.add(from);
        while (!q.isEmpty()) { int c = q.poll(); for (int k : com.watabou.utils.PathFinder.NEIGHBOURS8) { int n = c + k;
            if (n < 0 || n >= d.length || d[n] >= 0 || !L.passable[n] || L.avoid[n] || Math.abs(n % L.width() - c % L.width()) > 1) continue; d[n] = d[c] + 1; q.add(n); } }
        return d;
    }
    /** A floor cell at about walk w (the nearest above it), not the stairs, none of `not`. */
    static int at(com.shatteredpixel.shatteredpixeldungeon.levels.Level L, int[] d, int w, int... not) {
        int best = -1;
        outer: for (int c = 0; c < d.length; c++) {
            if (d[c] < w || c == L.exit() || c == L.entrance() || !(L.map[c] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.EMPTY || L.map[c] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.EMPTY_DECO || L.map[c] == com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.GRASS) || L.heaps.get(c) != null || L.traps.get(c) != null) continue;
            for (int x : not) if (L.distance(c, x) <= 2) continue outer;
            if (best < 0 || d[c] < d[best]) best = c; }
        return best;
    }
    static Assist.Target first() { java.util.List<Assist.Target> l = Assist.plan(false, null); return l.isEmpty() ? null : l.get(0); }
    static int objOf(Assist.Target t) { return t == null ? -1 : t.obj >= 0 ? t.obj : t.cell; }
    static void places() throws Exception {
        com.shatteredpixel.shatteredpixeldungeon.levels.Level L = Dungeon.level; Hero h = Dungeon.hero; int dep = Dungeon.depth;
        WatchSettings.assist(2); WatchSettings.itemAssist(false); Assist.reset(); Gallery.calm();
        L.heaps.clear(); h.buff(Hunger.class).satisfy(1000);
        int pos0 = h.pos, bestN = -1;   // (the hero set down where most of the floor is a walk away: the room he arrived in may be shut off)
        for (int c = 0; c < L.length(); c += 7) if (L.passable[c] && !L.avoid[c] && L.map[c] != com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.DOOR && c != L.exit() && c != L.entrance()) {
            long n = Arrays.stream(walks(L, c)).filter(x -> x >= 0).count(); if (n > bestN) { bestN = (int) n; h.pos = c; } }
        int[] d = walks(L, h.pos);
        Arrays.fill(L.visited, true); Arrays.fill(L.mapped, true);
        int near = at(L, d, 5), far = at(L, d, 22, near);
        check(near >= 0 && far >= 0, "a floor to set the scenes on: " + near + " " + far + " hero " + h.pos + " reach " + Arrays.stream(d).filter(x -> x >= 0).count() + " max " + Arrays.stream(d).max().getAsInt() + " " + L.getClass().getSimpleName());
        // 1. a held iron key's door far off, unexplored ground a few steps away: the near ground first
        com.shatteredpixel.shatteredpixeldungeon.levels.Level.set(far, com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.LOCKED_DOOR);
        com.shatteredpixel.shatteredpixeldungeon.items.keys.IronKey iron = new com.shatteredpixel.shatteredpixeldungeon.items.keys.IronKey(dep);
        com.shatteredpixel.shatteredpixeldungeon.journal.Notes.add(iron);
        L.visited[near] = L.mapped[near] = false;
        Assist.Target a = first(); int o = objOf(a);
        check(o >= 0 && o != far && d[o] >= 0 && d[o] <= 6, "unexplored ground nearby before a far door (" + d[far] + " steps): " + a);
        // 2. explored: the held key makes its door the objective, before the stairs
        L.visited[near] = L.mapped[near] = true; Assist.reset();
        a = first(); check(objOf(a) == far, "a held iron key's locked door is plan A once the floor is explored, not the stairs: " + a);
        com.shatteredpixel.shatteredpixeldungeon.journal.Notes.remove(iron);
        com.shatteredpixel.shatteredpixeldungeon.levels.Level.set(far, com.shatteredpixel.shatteredpixeldungeon.levels.Terrain.EMPTY);
        // 3. a key lying about: picked up before the stairs; passed over, it is held back a while, not forgotten
        int kc = at(L, d, 10, near);
        L.drop(new com.shatteredpixel.shatteredpixeldungeon.items.keys.IronKey(dep), kc).seen = true; Assist.reset();
        a = first(); check(objOf(a) == kc, "a seen key is plan A before the stairs: " + a);
        Assist.passed(a, h.pos, h.pos);
        check(Assist.demoted(kc), "a key passed over is held back a while");
        boolean still = false; for (Assist.Target t : Assist.plan(false, null)) still |= objOf(t) == kc;
        check(still, "a key passed over is still in the list (not dropped for the floor)");
        L.heaps.clear(); Assist.reset();
        // 4. unexplored ground left only far off: that, never the stairs
        L.visited[far] = L.mapped[far] = false;
        a = first(); check(objOf(a) != L.exit() && objOf(a) != L.entrance(), "no stairs while reachable unexplored ground remains: " + a);
        // 5. a tap steers: the next plan follows the cell tapped
        Assist.reset(); L.visited[near] = L.mapped[near] = false;
        first(); Assist.tapped(far);   // (after a list: a new floor forgets the player's way)
        a = first(); check(objOf(a) == far, "a tap on the map: its way is plan A: " + a);
        Arrays.fill(L.visited, true); Arrays.fill(L.mapped, true); Assist.reset(); WatchSettings.itemAssist(true);
        h.pos = pos0;
        System.out.println("places: near ground before a far door, the held key's door before the stairs, keys before the stairs and kept, no stairs with ground left, a tap steers: ok");
    }
    static Object get(Object o, String name, Class<?> c) throws Exception { Field f = c.getDeclaredField(name); f.setAccessible(true); return f.get(o); }

    public static void main(String[] args) {
        try { run(); } catch (Throwable e) { e.printStackTrace(); System.exit(1); }
    }
    static void run() throws Exception {
        Game.version = "4.0.2"; Game.versionCode = 922;
        Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
        String dir = ".pebxel-refinement-" + ProcessHandle.current().pid();
        WatchGame game = new WatchGame(); game.drawAlways = true;
        WatchApp app = new WatchApp(game, dir, true);
        SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
        FileUtils.setDefaultFileProperties(Files.FileType.External, dir + "/");
        app.create(); app.graphics.soft.raster = false;
        SPDSettings.intro(false); GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
        Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed(); Dungeon.seed = 42;
        InterlevelScene.mode = InterlevelScene.Mode.DESCEND; Game.switchScene(InterlevelScene.class);
        for (int f = 0; f < 5000; f++) {
            app.frame(); if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
            if (Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready) break;
            Thread.sleep(1);
        }
        check(Game.scene() instanceof GameScene, "game ready");
        while (Ui.topWindow() != null) Ui.topWindow().hide();
        Gallery.calm(); Hero h = Dungeon.hero; h.belongings.backpack.items.clear();
        h.HP = 40; h.HT = 100; h.buff(Hunger.class).satisfy(1000);
        WatchSettings.assist(2); Assist.reset();
        check(Assist.REST_ACT.equals(hands().action), "hurt hero rests on an unlocked floor");
        Dungeon.level.locked = true;
        Assist.Target t = hands(); check(t == null || !Assist.REST_ACT.equals(t.action), "never rest in a locked fight");
        Dungeon.level.locked = false; h.HP = 100;
        h.buff(Hunger.class).affectHunger(-Hunger.STARVING);
        Item raw = new MysteryMeat(), cooked = new ChargrilledMeat();
        h.belongings.backpack.items.add(raw); h.belongings.backpack.items.add(cooked);
        check(hands().item == cooked, "starving: other food before raw meat");
        h.buff(Hunger.class).satisfy(1000); h.belongings.backpack.items.clear();
        h.STR = 13; Armor hidden = new MailArmor(); hidden.upgrade(); hidden.levelKnown = false;
        h.belongings.backpack.items.add(hidden);
        t = hands(); check(t == null || t.item != hidden, "unknown upgrade must not reveal lower STR requirement");
        hidden.levelKnown = true; check(hands().item == hidden, "known fitting mail is offered");
        h.belongings.backpack.items.clear();
        h.belongings.weapon = new MagesStaff(); h.belongings.weapon.identify();
        ScrollOfUpgrade up = new ScrollOfUpgrade(); up.identify(); h.belongings.backpack.items.add(up);
        t = hands(); check(t != null && t.item == up && t.on == h.belongings.weapon, "Mage invests in the staff first");
        h.belongings.backpack.items.clear();
        obedience(app);
        buttons(app);
        places();

        Link link = new Link(new Assets()); Controls controls = new Controls(); Touches touches = new Touches();
        pebxel.ui.WndWatchMenu menu = new pebxel.ui.WndWatchMenu(); GameScene.show(menu);
        controls.frame(); link.frame(1f/30); link.take(7984);
        int stamp = link.nowMs(), cam = link.camIdOf(menu.camera);
        check(link.remap(cam, stamp, 100, 100) != null, "current-page touch accepted");
        Object token = menu.touchPage(); show(menu, 1);
        check(menu.touchPage() != token && link.remap(cam, stamp, 100, 100) == null, "old-page touch rejected");
        token = menu.touchPage(); Controls.fingerLast = true;
        java.util.List<Ui.Part> buttons = Controls.buttons(menu);
        Ui.press(buttons.get(buttons.size() - 2).g);
        check(menu.touchPage() == token, "tapping the active menu tab keeps its pictures");
        touches = new Touches();
        touch(touches, link, controls, 3, 30, 50);
        check((Boolean)get(touches, "dragDown"), "interface drag starts");
        touch(touches, link, controls, 4, 35, 60);
        check((Integer)get(touches, "dragY") == 121, "interface drag moves");
        touch(touches, link, controls, 5, 35, 60);
        check(!(Boolean)get(touches, "dragDown"), "interface drag releases");
        Field active = PointerEvent.class.getDeclaredField("activePointers"); active.setAccessible(true);
        check(((Map<?,?>)active.get(null)).isEmpty(), "no pointer left pressed after release");
        while (Ui.topWindow() != null) Ui.topWindow().hide();
        menu = new pebxel.ui.WndWatchMenu(); GameScene.show(menu);
        touch(touches, link, controls, 3, 30, 50); show(menu, 2);
        touches.frame(link, controls, 1f/30); PointerEvent.processPointerEvents();
        check(!(Boolean)get(touches, "dragDown") && ((Map<?,?>)active.get(null)).isEmpty(), "page change cancels drag");
        Object sound = null;
        for (Ui.Part p : Ui.parts(menu)) if (p.button && p.text.startsWith("Sound:")) sound = p.g;
        int listenerCount = MenuTest.listeners();
        for (Ui.Part p : Ui.parts(menu)) if (p.button && p.text.startsWith("Music:")) { Ui.press(p.g); break; }
        Object soundAfter = null;
        for (Ui.Part p : Ui.parts(menu)) if (p.button && p.text.startsWith("Sound:")) soundAfter = p.g;
        check(sound != null && sound == soundAfter, "music changes preserve the other settings buttons");
        check(MenuTest.listeners() == listenerCount, "settings rebuild frees old touch listeners");
        touches.add(new byte[]{1, (byte)++seq, (byte)255, 1, 30, 0, 50, 0, 0, 0, 0, 0});
        touches.frame(link, controls, 1f/30); PointerEvent.processPointerEvents();
        show(menu, 1); touches.frame(link, controls, 1f/30); PointerEvent.processPointerEvents();
        check(((Map<?,?>)active.get(null)).isEmpty(), "a pending long tap is cancelled on page change");
        menu.hide();
        System.out.println("PASS: assistant policy, menu page stamps, drag move/release/cancel (" + Screen.w + "px)");
        System.exit(0);
    }
}
