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
        check(grass ? (Boolean) get(null, "grassSkip", Assist.class) : Assist.demoted(o), "a place passed over is held back: " + o);
        // a new floor: things stand afresh, the upgrade first again
        descend(app); h = Dungeon.hero;
        a = planA(app, -1, -1);
        check(a != null && a.item == up, "a new floor offers the upgrade again: " + a);
        // ignored: something else done instead (the toolbar's search, held) passes it over too
        t0 = now(); p0 = h.pos;
        oc.button(Controls.BACK); oc.button(Controls.BACK); oc.button(Controls.SELECT | Controls.LONG);
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
