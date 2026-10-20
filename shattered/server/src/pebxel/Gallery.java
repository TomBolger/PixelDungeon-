package pebxel;

import com.badlogic.gdx.Files;
import com.shatteredpixel.shatteredpixeldungeon.*;
import com.shatteredpixel.shatteredpixeldungeon.actors.Actor;
import com.shatteredpixel.shatteredpixeldungeon.actors.blobs.Blob;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Buff;
import com.shatteredpixel.shatteredpixeldungeon.actors.buffs.FlavourBuff;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.Hero;
import com.shatteredpixel.shatteredpixeldungeon.actors.hero.HeroClass;
import com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Mob;
import com.shatteredpixel.shatteredpixeldungeon.items.Item;
import com.shatteredpixel.shatteredpixeldungeon.items.armor.Armor;
import com.shatteredpixel.shatteredpixeldungeon.levels.Level;
import com.shatteredpixel.shatteredpixeldungeon.scenes.GameScene;
import com.shatteredpixel.shatteredpixeldungeon.scenes.InterlevelScene;
import com.shatteredpixel.shatteredpixeldungeon.sprites.HeroSprite;
import com.shatteredpixel.shatteredpixeldungeon.ui.Window;
import com.shatteredpixel.shatteredpixeldungeon.windows.WndStory;
import com.watabou.noosa.Camera;
import com.watabou.noosa.Game;
import com.watabou.utils.FileUtils;
import com.watabou.utils.PathFinder;
import java.io.File;
import java.util.*;

/**
 * Staged views for the visual audit: each region with a line-up of its monsters, NPCs and bosses, items on the
 * floor, gases, buffs, every hero class in every armour, and the windows that still show. The same stage works in
 * the simulation (main: pictures through WatchSim) and on the emulator (Server with GALLERY=name: the game sets it
 * up after it starts, so the real watch shows it).
 * Args: picture-folder [stage names...] (none: all). SCREEN=gabbro for the round watch.
 */
public class Gallery {
	static final String P = "com.shatteredpixel.shatteredpixeldungeon.";
	static final String M = P + "actors.mobs.", N = M + "npcs.", I = P + "items.", B = P + "actors.buffs.", G = P + "actors.blobs.";

	/** Stage name -> {depth, what to do}. */
	public static final LinkedHashMap<String, Object[]> STAGES = new LinkedHashMap<>();
	static {
		st("sewers", 2, "mobs", "Rat Albino Snake Gnoll Swarm Crab Slime CausticSlime");
		st("sewers2", 4, "mobs", "npcs.Ghost FetidRat GnollTrickster GreatCrab HermitCrab npcs.RatKing Goo");
		st("prison", 7, "mobs", "Skeleton Thief Bandit DM100 Guard Necromancer SpectralNecromancer");
		st("prison2", 8, "mobs", "npcs.Wandmaker RotLasher RotHeart Elemental$NewbornFireElemental Tengu npcs.Shopkeeper");
		st("caves", 12, "mobs", "Bat Brute ArmoredBrute Shaman$RedShaman Shaman$BlueShaman Shaman$PurpleShaman Spinner DM200 DM201");
		st("caves2", 13, "mobs", "npcs.Blacksmith GnollGuard GnollSapper GnollGeomancer CrystalWisp CrystalGuardian FungalSentry FungalSpinner DM300 Pylon");
		st("city", 17, "mobs", "Ghoul Elemental$FireElemental Elemental$FrostElemental Elemental$ShockElemental Elemental$ChaosElemental Warlock Monk Senior Golem");
		st("city2", 18, "mobs", "npcs.Imp npcs.ImpShopkeeper DwarfKing Statue ArmoredStatue Wraith TormentedSpirit");
		st("halls", 22, "mobs", "Succubus Eye Scorpio Acidic RipperDemon DemonSpawner YogDzewa$Larva");
		st("halls2", 23, "mobs", "YogDzewa YogFist$BurningFist YogFist$SoiledFist YogFist$RottingFist YogFist$RustedFist YogFist$BrightFist YogFist$DarkFist");
		st("misc", 3, "mobs", "Mimic GoldenMimic CrystalMimic EbonyMimic Bee Piranha npcs.Sheep npcs.MirrorImage");
		st("items", 1, "items", "");
		st("items-caves", 12, "items", "");
		st("gas", 2, "gas", "");
		st("buffs", 1, "buffs", "");
		st("heroes", 6, "heroes", "");
		st("bossbar", 2, "boss", "Goo");
		st("windows", 1, "windows", "");
	}
	static void st(String name, int depth, String kind, String arg) { STAGES.put(name, new Object[]{depth, kind, arg}); }

	// ---------------------------------------------------------------- staging (shared by the sim and the server)

	/** Steps still to do; run one each time the game is ready (in the dungeon, hero waiting, no story window). */
	static final ArrayDeque<Runnable> steps = new ArrayDeque<>();
	static int waitFrames;

	/** Sets a stage up: goes to its depth, then places what it shows. */
	public static void stage(String name) {
		Object[] s = STAGES.get(name);
		if (s == null) { System.out.println("no stage " + name); return; }
		int depth = (Integer) s[0]; String kind = (String) s[1], arg = (String) s[2];
		steps.add(() -> go(depth));
		steps.add(Gallery::calm);
		switch (kind) {
			case "mobs": steps.add(() -> lineup(arg.split(" "), 2)); break;
			case "boss": steps.add(() -> lineup(arg.split(" "), 2));
				steps.add(() -> { for (Mob m : Dungeon.level.mobs) if (m.properties().contains(com.shatteredpixel.shatteredpixeldungeon.actors.Char.Property.BOSS)) com.shatteredpixel.shatteredpixeldungeon.ui.BossHealthBar.assignBoss(m); }); break;
			case "items": steps.add(Gallery::items); break;
			case "gas": steps.add(Gallery::gas); break;
			case "buffs": steps.add(Gallery::buffs); break;
		}
	}

	/** Call every frame: true while steps remain. */
	public static boolean tick() {
		if (steps.isEmpty()) return false;
		if (Game.scene() instanceof InterlevelScene) { HandoffTest.pressOn(); return true; }
		if (!(Game.scene() instanceof GameScene) || Dungeon.hero == null || !Dungeon.hero.ready) return true;
		Window w = Ui.topWindow();
		if (w != null) { w.hide(); return true; }
		if (waitFrames > 0) { waitFrames--; return true; }
		Runnable r = steps.poll();
		try { r.run(); } catch (Exception e) { e.printStackTrace(); }
		waitFrames = 20;
		return true;
	}

	static void go(int depth) {
		if (Dungeon.depth == depth) return;
		InterlevelScene.mode = InterlevelScene.Mode.RETURN;
		InterlevelScene.returnDepth = depth; InterlevelScene.returnBranch = 0; InterlevelScene.returnPos = -1;
		Game.switchScene(InterlevelScene.class);
	}

	/** The level's own monsters go away (so the line-up is all there is), and the hero sees everything near. */
	static void calm() {
		for (Mob m : Dungeon.level.mobs.toArray(new Mob[0])) { m.destroy(); if (m.sprite != null) m.sprite.killAndErase(); }
		Dungeon.hero.HP = Dungeon.hero.HT = 1000;
	}

	/** Free cells round the hero, nearest first, each at least `gap` from the ones already taken. */
	static List<Integer> spots(int gap, int n) {
		Level L = Dungeon.level; int w = L.width(); int h = Dungeon.hero.pos;
		List<Integer> cand = new ArrayList<>();
		for (int r = 1; r <= 6; r++)
			for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
				if (Math.max(Math.abs(dx), Math.abs(dy)) != r) continue;
				int x = h % w + dx, y = h / w + dy;
				if (x < 1 || y < 1 || x >= w - 1 || y >= L.height() - 1) continue;
				int c = y * w + x;
				if (L.passable[c] && Actor.findChar(c) == null && L.heroFOV[c] && L.heaps.get(c) == null) cand.add(c);
			}
		List<Integer> out = new ArrayList<>();
		for (int c : cand) {
			boolean ok = true;
			for (int o : out) if (Math.max(Math.abs(o % w - c % w), Math.abs(o / w - c / w)) < gap) ok = false;
			if (ok) out.add(c);
			if (out.size() >= n) break;
		}
		return out;
	}

	static Object make(String cls) throws Exception {
		return Class.forName(cls).getDeclaredConstructor().newInstance();
	}

	static void lineup(String[] names, int gap) {
		List<Integer> cells = spots(gap, names.length);
		for (int i = 0; i < names.length && i < cells.size(); i++) {
			try {
				Mob m = (Mob) make(names[i].startsWith("npcs.") ? N + names[i].substring(5) : M + names[i]);
				m.pos = cells.get(i);
				GameScene.add(m);
				m.state = m.PASSIVE;
				if (m.sprite != null) m.sprite.idle();
			} catch (Throwable e) { System.out.println("mob " + names[i] + ": " + e); }
		}
	}

	static final String[] ITEMS = {"weapon.melee.Longsword", "weapon.melee.Greataxe", "armor.PlateArmor", "armor.LeatherArmor",
			"potions.PotionOfHealing", "potions.PotionOfStrength", "scrolls.ScrollOfUpgrade", "scrolls.ScrollOfIdentify",
			"wands.WandOfFireblast", "rings.RingOfMight", "artifacts.CloakOfShadows", "food.Food", "food.MysteryMeat", "Gold",
			"keys.IronKey", "bombs.Bomb", "Honeypot", "stones.StoneOfBlink", "weapon.missiles.Javelin", "Ankh", "Torch",
			"spells.PhaseShift", "potions.exotic.PotionOfDivineInspiration", "Dewdrop"};

	static void items() {
		List<Integer> cells = spots(1, ITEMS.length);
		for (int i = 0; i < ITEMS.length && i < cells.size(); i++) {
			try {
				Item it = ITEMS[i].equals("keys.IronKey") ? new com.shatteredpixel.shatteredpixeldungeon.items.keys.IronKey(Dungeon.depth) : (Item) make(I + ITEMS[i]);
				if (it instanceof com.shatteredpixel.shatteredpixeldungeon.items.Gold) it.quantity(50);
				Dungeon.level.drop(it, cells.get(i)).sprite.drop();
			} catch (Throwable e) { System.out.println("item " + ITEMS[i] + ": " + e); }
		}
	}

	static final String[] GASES = {"ToxicGas", "ParalyticGas", "ConfusionGas", "Fire", "Web", "SmokeScreen", "StormCloud", "Electricity", "Freezing", "CorrosiveGas", "Regrowth", "Blizzard", "Inferno", "SacrificialFire", "Foliage"};

	@SuppressWarnings("unchecked")
	static void gas() {
		List<Integer> cells = spots(3, GASES.length);
		for (int i = 0; i < GASES.length && i < cells.size(); i++) {
			try { GameScene.add(Blob.seed(cells.get(i), 300, (Class<? extends Blob>) Class.forName(G + GASES[i]))); }
			catch (Throwable e) { System.out.println("gas " + GASES[i] + ": " + e); }
		}
	}

	static final String[] BUFFS = {"Poison", "Burning", "Bleeding", "Cripple", "Paralysis", "Levitation", "MindVision", "Invisibility",
			"Blindness", "Roots", "Slow", "Barkskin", "Haste", "Ooze", "Weakness", "Vertigo", "Terror", "Amok", "Charm", "Corrosion",
			"Doom", "Drowsy", "Hex", "Vulnerable", "Bless", "Adrenaline", "Light", "Recharging", "ArcaneArmor", "Foresight", "Hunger"};

	@SuppressWarnings("unchecked")
	static void buffs() {
		Hero h = Dungeon.hero;
		for (String b : BUFFS) {
			try {
				Class<? extends Buff> c = (Class<? extends Buff>) Class.forName(B + b);
				Buff x = FlavourBuff.class.isAssignableFrom(c) ? Buff.prolong(h, (Class<? extends FlavourBuff>) c, 20f) : Buff.affect(h, c);
				if (x instanceof com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger)
					((com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger) x).satisfy(-400);
			} catch (Throwable e) { System.out.println("buff " + b + ": " + e); }
		}
	}

	static final String[] ARMOURS = {null, "ClothArmor", "LeatherArmor", "MailArmor", "ScaleArmor", "PlateArmor", "*"};

	/** The hero in class c's armour tier t (t 6: the class armour). */
	static void dress(HeroClass c, int t) throws Exception {
		Hero h = Dungeon.hero;
		h.heroClass = c;
		String a = ARMOURS[t];
		if (a != null && a.equals("*")) a = c.name().charAt(0) + c.name().substring(1).toLowerCase() + "Armor";
		h.belongings.armor = a == null ? null : (Armor) make(I + "armor." + a);
		((HeroSprite) h.sprite).disguise(c);
	}

	/** Where a sprite is on the watch (watch pixels). */
	static int[] onWatch(com.watabou.noosa.Visual v) {
		Camera c = v.camera();
		float k = c.zoom / 2f;
		return new int[]{Math.round(c.x / 2f + (v.x - c.scroll.x) * k), Math.round(c.y / 2f + (v.y - c.scroll.y) * k), Math.round(v.width * k), Math.round(v.height * k)};
	}

	// ---------------------------------------------------------------- the simulation's pictures

	static WatchApp app; static Link link; static WatchSim sim; static Controls controls; static File dir;

	public static void main(String[] a) throws Exception {
		dir = new File(a[0]); dir.mkdirs();
		List<String> only = Arrays.asList(a).subList(1, a.length);
		Game.version = "4.0.2"; Game.versionCode = 922;
		Screen.set(System.getenv().getOrDefault("SCREEN", "emery"));
		WatchGame game = new WatchGame(); game.drawAlways = true;
		app = new WatchApp(game, ".pebxel-gallery", true);
		SPDSettings.set(app.getPreferences(SPDSettings.DEFAULT_PREFS_FILE));
		FileUtils.setDefaultFileProperties(Files.FileType.External, ".pebxel-gallery/");
		app.create();
		link = new Link(new Assets()); sim = new WatchSim(new File("."), new Assets());
		app.graphics.soft.raster = false;
		frames(5);
		SPDSettings.intro(false);
		GamesInProgress.selectedClass = HeroClass.WARRIOR; GamesInProgress.curSlot = 1;
		Dungeon.hero = null; Dungeon.daily = Dungeon.dailyReplay = false; Dungeon.initSeed();
		Dungeon.seed = Long.parseLong(System.getenv().getOrDefault("SEED", "84"));
		InterlevelScene.mode = InterlevelScene.Mode.DESCEND;
		Game.switchScene(InterlevelScene.class);
		for (int k = 0; k < 4000 && !(Game.scene() instanceof GameScene && Dungeon.hero != null && Dungeon.hero.ready); k++) {
			frames(1); Thread.sleep(2);
			if (Game.scene() instanceof InterlevelScene) HandoffTest.pressOn();
		}
		controls = new Controls();
		frames(30);
		for (String name : STAGES.keySet()) {
			if (!only.isEmpty() && !only.contains(name)) continue;
			stage(name);
			for (int k = 0; k < 6000 && tick(); k++) { frames(1); if (Game.scene() instanceof InterlevelScene) Thread.sleep(2); }
			frames(200);   // (the log's messages fade first)
			String kind = (String) STAGES.get(name)[1];
			if (kind.equals("heroes")) heroes();
			else if (kind.equals("windows")) windows();
			else {
				shot(name);
				if (kind.equals("mobs") || kind.equals("boss")) {
					// zoomed in a step, as Up does (then back to the default)
					controls.button(Controls.UP); frames(30); shot(name + "-z32");
					controls.button(Controls.UP); frames(10); controls.button(Controls.UP); frames(30);
				}
				if (kind.equals("buffs")) { GameScene.show(new com.shatteredpixel.shatteredpixeldungeon.windows.WndHero()); frames(20);
					controls.button(Controls.DOWN); frames(10); controls.button(Controls.DOWN); frames(30); shot("buffs-hero"); closeAll(); }
			}
		}
		System.exit(0);
	}

	static void heroTab(int t) throws Exception {
		java.lang.reflect.Field f = pebxel.ui.WndWatchHero.class.getDeclaredField("lastTab"); f.setAccessible(true); f.setInt(null, t);
	}

	static void closeAll() { while (Ui.topWindow() != null) { Ui.topWindow().hide(); frames(2); } frames(10); }

	static void heroes() throws Exception {
		for (Buff b : Dungeon.hero.buffs().toArray(new Buff[0])) if (b.icon() != com.shatteredpixel.shatteredpixeldungeon.ui.BuffIndicator.NONE) b.detach();
		int hc = spots(1, 1).get(0); Dungeon.hero.pos = hc; Dungeon.hero.sprite.place(hc); Dungeon.observe();
		frames(60);
		Dungeon.hero.sprite.visible = false; frames(2); shot("hero-none"); Dungeon.hero.sprite.visible = true;
		StringBuilder sb = new StringBuilder();
		for (HeroClass c : HeroClass.values()) for (int t = 0; t < 7; t++) {
			dress(c, t); frames(10);
			String n = "hero-" + c.name().toLowerCase() + "-" + t;
			shot(n);
			int[] p = onWatch(Dungeon.hero.sprite);
			sb.append(n).append(' ').append(p[0]).append(' ').append(p[1]).append(' ').append(p[2]).append(' ').append(p[3]).append('\n');
		}
		dress(HeroClass.WARRIOR, 1);
		java.nio.file.Files.write(new File(dir, "heroes.txt").toPath(), sb.toString().getBytes());
	}

	static void windows() throws Exception {
		Hero h = Dungeon.hero;
		LinkedHashMap<String, java.util.concurrent.Callable<Window>> w = new LinkedHashMap<>();
		String W = P + "windows.";
		w.put("w-sadghost", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndSadGhost(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Ghost(), 1));
		w.put("w-wandmaker", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndWandmaker(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Wandmaker(), new com.shatteredpixel.shatteredpixeldungeon.items.quest.CorpseDust()));
		w.put("w-blacksmith", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndBlacksmith(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Blacksmith(), h));
		w.put("w-quest", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndQuest(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.npcs.Imp(), "Are you an adventurer? I love adventurers! You can always rely on them if something needs to be killed. Am I right?"));
		w.put("w-subclass", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndChooseSubclass(new com.shatteredpixel.shatteredpixeldungeon.items.TengusMask(), h));
		w.put("w-ability", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndChooseAbility(new com.shatteredpixel.shatteredpixeldungeon.items.KingsCrown(), h.belongings.armor, h));
		w.put("w-resurrect", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndResurrect(new com.shatteredpixel.shatteredpixeldungeon.items.Ankh()));
		w.put("w-infocell", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoCell(h.pos));
		w.put("w-trade", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndTradeItem(new com.shatteredpixel.shatteredpixeldungeon.items.potions.PotionOfHealing(), null));
		w.put("w-upgrade", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndUpgrade(new com.shatteredpixel.shatteredpixeldungeon.items.scrolls.ScrollOfUpgrade(), h.belongings.weapon, false));
		w.put("w-trap", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoTrap(new com.shatteredpixel.shatteredpixeldungeon.levels.traps.BurningTrap().set(h.pos)));
		w.put("w-plant", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoPlant(new com.shatteredpixel.shatteredpixeldungeon.plants.Firebloom()));
		w.put("w-badge", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndBadge(Badges.Badge.MONSTERS_SLAIN_1, true));
		w.put("w-score", com.shatteredpixel.shatteredpixeldungeon.windows.WndScoreBreakdown::new);
		w.put("w-goo", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoMob(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.Goo()));
		w.put("w-dm300", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoMob(new com.shatteredpixel.shatteredpixeldungeon.actors.mobs.DM300()));
		w.put("w-list", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndList(new String[]{"One thing to know", "Another thing, a bit longer than the first"}));
		w.put("w-buff", () -> new com.shatteredpixel.shatteredpixeldungeon.windows.WndInfoBuff(Buff.affect(h, com.shatteredpixel.shatteredpixeldungeon.actors.buffs.Hunger.class)));
		for (Map.Entry<String, java.util.concurrent.Callable<Window>> e : w.entrySet()) {
			try { GameScene.show(e.getValue().call()); } catch (Throwable x) { System.out.println(e.getKey() + ": " + x); continue; }
			frames(40); shot(e.getKey()); closeAll();
		}
		try { Game.switchScene(com.shatteredpixel.shatteredpixeldungeon.scenes.AlchemyScene.class); frames(60); shot("alchemy"); }
		catch (Throwable x) { System.out.println("alchemy: " + x); }
	}

	static void frames(int k) {
		for (int f = 0; f < k; f++) {
			app.frame(); if (controls != null) controls.frame();
			sim.advance(Game.elapsed); sim.apply(link.sync(Game.elapsed));
		}
	}

	static void shot(String name) throws Exception {
		sim.render();
		sim.save(new File(dir, name + ".png"), 2);
	}
}
