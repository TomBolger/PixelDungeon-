package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import java.io.File;

/**
 * Presses watch buttons in a script and saves what the watch would show after each press.
 * Args: picture-folder, then presses: u d s b (up down select back), U D S (long), tX,Y a tap (tlX,Y long),
 * or a number = wait that many frames.
 * SEED=n NEWGAME=class to start straight in a new game.
 */
public class Buttons {
	static int wantDepth; static boolean wantMine, flashProbe;

	public static void main(String[] a) throws Exception {
		File dir = new File(a[0]); dir.mkdirs();
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		WatchApp app = new WatchApp(game, ".pebxel-buttons", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-buttons/");
		app.create();
		Link link = new Link(new Assets());
		WatchSim sim = new WatchSim(new File("."), new Assets());
		Controls controls = new Controls();
		link.planner = controls::plan;
		Touches touches = new Touches(); int seq = 0;
		for (int i = 0; i < 5; i++) app.frame();
		if (System.getenv("ASSIST") != null) WatchSettings.assist(Integer.parseInt(System.getenv("ASSIST")));
		Controls.log = System.getenv("PLANLOG") != null;
		if (System.getenv("NEWGAME") != null) {
			SPDSettings.intro(false);
			GamesInProgress.selectedClass = HeroClass.values()[Integer.parseInt(System.getenv("NEWGAME"))];
			GamesInProgress.curSlot = 1;
			Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
			if (System.getenv("SEED") != null) Dungeon.seed = Long.parseLong(System.getenv("SEED"));
			InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
			Game.switchScene(InterlevelScene.class);
		}
		int shot = 0; wantDepth = 0;
		app.graphics.soft.raster = false;
		for (int i = 1; i < a.length; i++) {
			String p = a[i];
			int wait = 30;
			if (p.matches("\\d+")) wait = Integer.parseInt(p);
			else if (p.startsWith("@give:")) { ((com.shatteredpixel.shatteredpixeldungeon.items.Item) Class.forName("com.shatteredpixel.shatteredpixeldungeon.items." + p.substring(6)).getDeclaredConstructor().newInstance()).identify().collect(); }   // (tests: an item, e.g. @give:Amulet)
			else if (p.startsWith("@read:")) {   // (tests: a scroll read at once, e.g. @read:ScrollOfMagicMapping)
				com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll sc = (com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll) Class.forName("com.shatteredpixel.shatteredpixeldungeon.items.scrolls." + p.substring(6)).getDeclaredConstructor().newInstance();
				sc.identify(); sc.collect();
				sc.execute(Dungeon.hero, com.shatteredpixel.shatteredpixeldungeon.items.scrolls.Scroll.AC_READ);
			}
			else if (p.equals("@flash")) {   // (tests: every cell within 20 flashed, as a Stone of Clairvoyance or the Talisman of Foresight do)
				int made = 0;
				for (int c = 0; c < Dungeon.level.length(); c++) if (Dungeon.level.distance(c, Dungeon.hero.pos) <= 20 && com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.checkedCell(c, Dungeon.hero.pos) != null) made++;
				System.out.println("flashes " + made);
				flashProbe = true; wait = 1;
			}
			else if (p.equals("@mine")) { wantMine = true; wantDepth = 99; wait = 8000; }   // (tests: down to the Blacksmith, his quest taken, a pickaxe, into his mine)
			else if (p.startsWith("@depth")) { wantDepth = Integer.parseInt(p.substring(6)); wait = 4000; }   // (tests: down to that depth, story pages and all)
			else if (p.equals("@fire")) {   // (tests: particles: a fire beside the hero, and the hero alight)
				int c = Dungeon.hero.pos + 1;
				com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene.add(com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Blob.seed(c, 30, com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Fire.class));
				com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff.affect(Dungeon.hero, com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Burning.class).reignite(Dungeon.hero);
			}
			else if (p.equals("@up")) {   // (tests: up the stairs, as the hero's step onto them)
				com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(true);
				if (tr != null) StairsTest.jump(tr);
			}
			else if (p.equals("@down")) {   // (tests: straight down the stairs, as the hero's step onto them)
				com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(false);
				if (tr != null) StairsTest.jump(tr);
			}
			else if (p.equals("@alchemy")) {   // (tests: only three seeds and some energy, then the alchemy screen, as at a pot)
				Dungeon.hero.belongings.backpack.items.removeIf(it -> !(it instanceof com.shatteredpixel.shatteredpixeldungeon.items.bags.Bag));
				for (int k = 0; k < 3; k++) new com.shatteredpixel.shatteredpixeldungeon.plants.Sungrass.Seed().collect();
				new com.shatteredpixel.shatteredpixeldungeon.plants.Firebloom.Seed().collect();
				Dungeon.energy = Math.max(Dungeon.energy, 10);
				Game.switchScene(com.shatteredpixel.shatteredpixeldungeon.scenes.AlchemyScene.class);
			}
			else if ((p.startsWith("t") || p.startsWith("m")) && p.contains(",")) {
				// a tap at watch x,y (tl: a long one; m: aimed at the map's camera, as the watch sends it, stamped now)
				boolean map = p.startsWith("m");
				String[] xy = p.substring(p.startsWith("tl") ? 2 : 1).split(",");
				int x = Integer.parseInt(xy[0]), y = Integer.parseInt(xy[1]);
				int cam = map ? link.camIdOf(com.watabou.noosa.Camera.main) : 0xFF, st = map ? link.nowMs() : 0;
				byte[] t = {1, (byte) ++seq, (byte) cam, (byte) (p.startsWith("tl") ? 1 : 0), (byte) x, (byte) (x >> 8), (byte) y, (byte) (y >> 8), (byte) st, (byte) (st >> 8), (byte) (st >> 16), (byte) (st >> 24)};
				touches.add(t);
			}
			else {
				int code = 0;
				switch (p.toLowerCase().charAt(0)) { case 'u': code = 1; break; case 'd': code = 2; break; case 's': code = 3; break; case 'b': code = 4; break; }
				if (Character.isUpperCase(p.charAt(0))) code |= Controls.LONG;
				controls.button(code);
			}
			long t0 = System.nanoTime();
			for (int f = 0; f < wait; f++) {
				if (wantDepth > 0) {
					if (Game.scene() instanceof InterlevelScene) { if (f % 40 == 39) controls.button(Controls.SELECT); }
					else if (Ui.topWindow() != null) { if (f % 20 == 19) controls.button(Controls.BACK); }
					else if (Dungeon.hero != null && Dungeon.hero.ready && Dungeon.depth < wantDepth && f % 10 == 9) {
						com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition tr = StairsTest.stairs(false);
						if (wantMine) {
							if (Dungeon.level instanceof com.shatteredpixel.shatteredpixeldungeon.levels.MiningLevel) { wantMine = false; wantDepth = 0; continue; }
							for (com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition x : Dungeon.level.transitions)
								if (x.type == com.shatteredpixel.shatteredpixeldungeon.levels.features.LevelTransition.Type.BRANCH_EXIT) {
									Class<?> q = com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Blacksmith.Quest.class;
									for (String fl : new String[]{"given", "started"}) { java.lang.reflect.Field fd = q.getDeclaredField(fl); fd.setAccessible(true); fd.setBoolean(null, true); }
									new com.shatteredpixel.shatteredpixeldungeon.items.quest.Pickaxe().identify().collect();
									tr = x;
								}
						}
						if (tr != null) StairsTest.jump(tr);
					} else if (Dungeon.depth >= wantDepth && f > 60) { wantDepth = 0; }
				}
				touches.frame(link, controls, Game.elapsed);
				app.frame(); controls.frame();
				if (System.getenv("TRACE_HERO") != null && Dungeon.hero != null && Dungeon.hero.sprite != null)
					System.out.printf("hero t=%.3f pos=%d x=%.2f y=%.2f ready=%b%n", Game.timeTotal, Dungeon.hero.pos, Dungeon.hero.sprite.x, Dungeon.hero.sprite.y, Dungeon.hero.ready);
				if (Game.scene() instanceof InterlevelScene) Thread.sleep(3); else Thread.sleep(1);
				sim.advance(Game.elapsed); link.frame(Game.elapsed); for (byte[] m : link.take(100000)) sim.apply(m);
			}
			if (flashProbe) {
				int n = 0, shown = 0, nocam = 0, ex = 0;
				for (com.watabou.noosa.Gizmo g : Ui.parts(Game.scene()).stream().map(q -> q.g).toArray(com.watabou.noosa.Gizmo[]::new)) { }
				java.util.ArrayDeque<com.watabou.noosa.Group> st = new java.util.ArrayDeque<>(); st.add(Game.scene());
				while (!st.isEmpty()) { com.watabou.noosa.Group gr = st.poll(); for (com.watabou.noosa.Gizmo g : com.watabou.noosa.GroupAccess.members(gr)) { if (g == null) continue; if (g instanceof com.watabou.noosa.Group) st.add((com.watabou.noosa.Group) g);
					if (g instanceof com.shatteredpixel.shatteredpixeldungeon.effects.CheckedCell) { n++; if (g.exists) ex++; if (((com.watabou.noosa.Visual) g).alpha() > 0.01f) shown++; if (g.camera() == null) nocam++; } } }
				System.out.println("  flashes in the scene " + n + ", existing " + ex + ", showing " + shown + ", no camera " + nocam);
			}
			if (System.getenv("TIMING") != null) System.out.printf("%s: %d frames in %.0f ms, watch pictures %d, link pictures %d, depth %d, scene %s%n", p, wait, (System.nanoTime() - t0) / 1e6, sim.objCount(),
					((java.util.Map<?, ?>) WatchDriver.get(link, "objs")).size(), Dungeon.depth, Game.scene().getClass().getSimpleName());
			if (System.getenv("FOCUS") != null && Ui.topWindow() == null && Game.scene() instanceof com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene && Dungeon.hero != null) {   // (the Down list: cell, terrain)
				StringBuilder sb = new StringBuilder(p + ": Down list");
				Object ts = WatchDriver.get(controls, "targets");
				if (ts instanceof java.util.List) for (Object t : (java.util.List<?>) ts) { int c = Controls.cellOf((Integer) t); sb.append(' ').append(c).append(c >= 0 ? ":" + Dungeon.level.map[c] : ""); }
				System.out.println(sb + " (hero " + Dungeon.hero.pos + ", " + Dungeon.level.getClass().getSimpleName() + ")");
				Object pl = WatchDriver.get(controls, "plan");
				if (pl instanceof java.util.List && !((java.util.List<?>) pl).isEmpty()) System.out.println("  plan (target " + WatchDriver.get(controls, "target") + "): " + pl);
			}
			if (System.getenv("FOCUS") != null) {   // (what Up/Down go through here, and which has the frame)
				com.shatteredpixel.shatteredpixeldungeon.ui.Window tw = Ui.topWindow();
				com.watabou.noosa.Group menu = tw != null ? tw : Game.scene();
				StringBuilder sb = new StringBuilder(p + ": " + menu.getClass().getSimpleName() + " focus " + WatchDriver.get(controls, "focus") + (tw != null && tw.camera != null ? " camera y " + tw.camera.y + " height " + tw.camera.screenHeight() + " of " + Game.height + (Controls.tall(tw) ? " tall" : "") : "") + " [");
				for (Ui.Part pt : Controls.buttons(menu)) sb.append(pt.g.getClass().getSimpleName()).append(':').append(pt.text.replace('\n', ' ')).append(", ");
				System.out.println(sb.append(']'));
			}
			if (System.getenv("NOSHOT") != null && i < a.length - 1) continue;   // (long scripts: only the last picture)
			sim.render();
			sim.save(new File(dir, String.format("%02d-%s.png", shot++, p)), 2);
		}
		System.out.println("scene " + Game.scene().getClass().getSimpleName() + ", " + shot + " pictures" + (Dungeon.hero != null ? ", game time " + com.shatteredpixel.shatteredpixeldungeon.actors.Actor.now() : ""));
		System.out.println("sounds " + sim.sounds);
		if (System.getenv("DUMP") != null) sim.dump();
		if (System.getenv("PLANS") != null) for (String pl : sim.plans) System.out.println("plan " + pl);
		System.exit(0);
	}
}
