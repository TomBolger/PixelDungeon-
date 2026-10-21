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
