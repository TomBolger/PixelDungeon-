# Shattered PD v4.0.2: a strategy reference for the bot

This is a reference for programmers writing `AssistBot` / `Assist` (the planner) and the watch assistant. It covers Shattered Pixel Dungeon **v4.0.2** (`shattered/spd/build.gradle:20`, `appVersionName = '4.0.2'`, code 922). It merges community knowledge (wiki, Reddit, Steam, the official blog) with a check of every important claim against the source in this repo.

## Conventions

* **Paths.** `S/` = `shattered/spd/core/src/main/java/com/shatteredpixel/shatteredpixeldungeon/`. `M/` = `shattered/spd/core/src/main/assets/messages/` (the in-game English text, which is part of the source). `CL4` = `S/ui/changelist/v4_X_Changes.java`, `CL3` = `S/ui/changelist/v3_X_Changes.java` (the official changelog, shipped in-tree).
* **Tags.**
  * ✔ means checked against the source, with a `file:line` cite.
  * ☐ means community consensus that was not (or cannot be) checked mechanically: a judgement call, not a fact.
  * ⚠ means the web and the source disagree. **The source wins**, and the disagreement is noted. Section 10 collects them all.
* **Bot rules (Thomas).** The bot may know the whole source but must never read hidden state:
  * unidentified item identities;
  * monsters out of view;
  * the contents of unopened heaps;
  * hidden traps and doors not yet found.
  
  Everything below that depends on probabilities (deck odds, drop tables) is *legal inference* from source knowledge. Using the hidden state itself is not.

---

## 1. Win conditions and the run's arc

### 1.1 Floors

✔ `S/Dungeon.java:297-386`

| Depth | Region | Level class | Boss / special |
|---|---|---|---|
| 1-4 | Sewers | `SewerLevel` | Ghost quest on 2-4 |
| 5 | | `SewerBossLevel` | **Goo** |
| 6-9 | Prison | `PrisonLevel` | Shop on 6, Wandmaker on 7-9 |
| 10 | | `PrisonBossLevel` | **Tengu** (drops Tengu's Mask, which unlocks the subclass) |
| 11-14 | Caves | `CavesLevel` | Shop on 11, Blacksmith on 12-14; Mines branch (`branch 1`, `MiningLevel`) |
| 15 | | `CavesBossLevel` | **DM-300** |
| 16-19 | City | `CityLevel` | Shop on 16, Imp on 17-19; Vault branch (`branch 1`, `VaultLevel`) |
| 20 | | `CityBossLevel` | **Dwarf King** (drops the King's Crown, which unlocks the armor ability). The **Imp's shop is on this floor** |
| 21-24 | Demon Halls | `HallsLevel` | Demon spawners |
| 25 | | `HallsBossLevel` | **Yog-Dzewa** |
| 26 | | `LastLevel` | The Amulet of Yendor |

* Shops: `Dungeon.shopOnLevel()` is true on depths 6, 11 and 16 (`S/Dungeon.java:433-435`). The Imp's shop is on depth 20 (`S/levels/CityBossLevel.java:128`, `:377`; it spawns only if `Imp.Quest.earnedShop()`). ⚠ Older guides put it on floor 21.
* Boss floors: 5, 10, 15, 20 and 25 (`S/Dungeon.java:441-443`). Boss floors and branch floors spawn no guaranteed food, Scroll of Upgrade (SoU) or Potion of Strength (PoS) (`S/levels/Level.java:224`).
* While a floor is locked (a boss fight), hunger does not tick (`S/actors/buffs/Hunger.java:66-71`).
* Natural regeneration on a locked floor stops if you go about 50 turns without damaging the boss. The `LockedFloor` timer starts at 50, damage to the boss refills it, and the cap is 50 (`S/actors/buffs/LockedFloor.java:33,48-58`, `S/actors/buffs/Regeneration.java:113-118`). **You cannot rest mid-fight**: heal before you enter.

### 1.2 How the game is won

✔ Picking up the Amulet on depth 26 opens `AmuletScene` (`S/items/Amulet.java:72-99`). Its **Exit** button calls `Dungeon.win(Amulet.class)` (`S/scenes/AmuletScene.java:78-82`). **That is a win.**

* The **Stay** button keeps playing. The Amulet's `AC_END` action can end the game later (`S/items/Amulet.java:51-68`).
* **Ascending is optional.** Taking the depth-25 entrance upward while holding the Amulet asks to start the ascension challenge (`S/levels/HallsBossLevel.java:334-356`).
  * Ascending to the depth-1 surface gives the "happy end" win with a higher score (`S/levels/SewerLevel.java:146-170`).
  * Ascension adds stacks: enemies get stat multipliers of x10 for rats down to x1.1 for scorpios (`S/actors/buffs/AscensionChallenge.java:43-77`).
  * At 4+ stacks enemies move fast, at 6+ the hero is slowed, and at 8+ the hero takes damage over time.
  * Respawns become very fast (`S/levels/Level.java:752-761`).
  * You cannot teleport between floors while holding the Amulet (`S/Dungeon.java:455-460`).
* **Bot rule: pick up the Amulet, choose Exit.** Do not ascend unless score matters.

### 1.3 The resource arc (fixed drops)

These are **guaranteed per region**, not random. They are legal inference for the bot.

| Item | Rule | Source |
|---|---|---|
| Scroll of Upgrade | **3 per 5-floor region**, placed on floors 1-4 of the region; each floor's chance = scrolls left / floors left | ✔ `S/Dungeon.java:545-554`, `S/levels/Level.java:232-241` |
| Potion of Strength | **2 per region**: one on floors 1-2 and one on 3-4 (50/50 which floor) | ✔ `S/Dungeon.java:529-543` |
| Arcane Stylus | 1 per region | ✔ `S/Dungeon.java:556-564` |
| Stone of Enchantment | 1 in total, on floors 6-19 | ✔ `S/Dungeon.java:566-577` |
| Stone of Intuition, Trinket Catalyst | 1 each, on floors 1-3 (≤4) | ✔ `S/Dungeon.java:579-587` |
| Laboratory (alchemy pot and 5 energy) | 1 per region, on floor 3 or 4 | ✔ `S/Dungeon.java:589-597`, `S/levels/rooms/special/LaboratoryRoom.java:63-74` |
| Food | 1 random food per regular floor, +1 on "large" floors | ✔ `S/levels/Level.java:226,277-278` |

Over a full run that is **15 SoU** (12 by the Dwarf King) and **10 PoS**. Starting STR is 10 (`S/actors/hero/Hero.java:207`), so STR is 14 by depth 10, 16 by 15, 18 by 20 and 20 by 25.

STR needed for gear of tier *t* is `8+2t`: T1 10, T2 12, T3 14, T4 16, T5 18. Upgrades lower it by 1 at +1, +3, +6 and +10 (`S/items/weapon/Weapon.java:358-363`, `S/items/armor/Armor.java:698-703`). **So T3 gear fits by depth ~10, T4 by ~15 and T5 by ~20.**

A missing STR point is costly:
* weapon accuracy is divided by 1.5^n and attack delay multiplied by 1.2^n (`S/items/weapon/Weapon.java:291-317`);
* armor evasion is divided by 1.5^n (`S/items/armor/Armor.java:422`).

**Never wear gear more than 1 STR short.**

### 1.4 Hero growth

* HP = 20 + 5·(level−1) (`S/actors/hero/Hero.java:263`). XP to the next level = 5 + 5·level (`:2114-2116`). Max level is 30 (`:205`).
* Each level gives +1 accuracy and +1 evasion (`:2075-2076`).
* Talent points:
  * tier 1 at levels 2-6 (5 points);
  * tier 2 at 7-12 (6 points);
  * tier 3 at 13-20 (8 points, **needs a subclass**);
  * tier 4 at 21-30 (**needs an armor ability**).
  
  Sources: `S/actors/hero/Talent.java:436`, `S/actors/hero/Hero.java:393-403`.
* **Monsters give 0 XP once the hero's level exceeds the monster's `maxLvl`** (`S/actors/mobs/Mob.java:988`). Grinding is pointless; keep moving down.

---

## 2. General principles

### 2.1 Upgrade economy (what to put Scrolls of Upgrade into)

**Consensus** (Reddit, Steam, kosgames, Steam guide via gamepretty) ☐:
* Concentrate upgrades into **one armor and one weapon**.
* Armor is the most common fix for mid-game deaths: "Warrior without plate is a struggle"; "+2 mail isn't going to do much by floor 19, hold out for plate"; "defense over offense".
* A typical finished run is about +6 armor, with the rest in the weapon or a key item.
* Don't spread upgrades into rings or minor wands early.
* Save SoU until you know your final piece (by DM-300 or the city shop), but **don't hoard them past the point where they would save you**.

Source facts that shape this:
* An SoU is `unique`, so it never burns or freezes (`S/items/scrolls/ScrollOfUpgrade.java:51`). Hoarding a few is safe.
* Upgrading **un-curses** the item, so it can be removed. On a curse enchantment there is a 33% chance to erase it (`S/items/weapon/Weapon.java:390-392`, `:405`).
* Upgrading an **enchanted** weapon or armor from +4/+5/+6/+7/+8 has a 10/20/40/80/100% chance to **lose the enchantment** (`S/items/weapon/Weapon.java:394-396`).
  * A Blacksmith *harden* protects it, but that protection itself can be lost from +6 (`:384-388`).
  * **Magical Infusion** (SoU + 12 energy) never erases enchantments (`S/items/spells/MagicalInfusion.java:131-137`, `M/items/items.properties` desc).
  * **Bot rule:** past +3 on an enchanted item you like, use Magical Infusion if you have the energy.
* The class item is a natural upgrade target:
  * Mage: the staff. Upgrades persist through re-imbues: the new level = max(staff, wand), +1 if the wand ≥ the staff (`S/items/weapon/melee/MagesStaff.java:244-250`).
  * Warrior: the seal transfers one upgrade (`M/actors/actors.properties:808`).
  * Huntress: the spirit bow. Its damage also grows with hero level: 1+lvl/5 to 6+lvl/2.5 base (`S/items/weapon/SpiritBow.java:189-200`).
  * Rogue and Cleric: the cloak and tome level up by use, not by SoU.
* Wands cap at 10 charges (`S/items/wands/Wand.java:429`).
* Ring effects are multiplicative per level (`S/items/rings/Ring*.java`, e.g. Haste 1.15^lvl, Evasion 1.125^lvl, Wealth 1.2^lvl). One ring at +3 to +6 is good value late, not early ☐.
* Arcane resin levels do not count when imbuing a wand into the staff (dev, Steam thread).

**Suggested bot policy** ☐ (consensus plus the numbers above):
1. Floors 1-4: hold SoU, or put them into the armor if a T2+ armor is worn (Ghost reward, mail found).
2. From floor 5 on: armor first until about +3 to +5, then the weapon.
3. Mage: the first 2-3 into the staff.
4. Put an SoU in immediately whenever HP is being lost faster than food allows (see 2.6).

### 2.2 Identify-by-use rules

**Base rates** (legal inference) ✔. Potions and scrolls use two 15-item decks (`S/items/Generator.java:342-343,376-377`). Per 30 random potions:

| Potion | Count | Notes |
|---|---|---|
| Healing | 6 | |
| Mind Vision | 4 | |
| Frost | 3 | |
| Liquid Flame | 3 | |
| Toxic Gas | 3 | |
| Haste | 2 | |
| Invisibility | 2 | |
| Levitation | 2 | |
| Paralytic Gas | 2 | |
| Purity | 2 | |
| Experience | 1 | |
| Strength | 0 | comes from fixed drops instead |

Per 30 random scrolls:

| Scroll | Count | Notes |
|---|---|---|
| Identify | 6 | |
| Remove Curse | 4 | |
| Mirror Image | 3 | |
| Recharging | 3 | |
| Teleportation | 3 | |
| Lullaby | 2 | |
| Magic Mapping | 2 | |
| Rage | 2 | |
| Retribution | 2 | |
| Terror | 2 | |
| Transmutation | 1 | |
| Upgrade | 0 | comes from fixed drops instead |

So the **most common unknown potion colour is probably Healing** and the most common unknown scroll rune is probably Identify (or SoU, given three fixed drops per region).

**Each class starts with Scroll of Identify plus two more types known** ✔ (`S/actors/hero/HeroClass.java:117,186-260`):

| Class | Known at start |
|---|---|
| Warrior | Healing, Rage |
| Mage | **Upgrade**, Liquid Flame |
| Rogue | Magic Mapping, Invisibility |
| Huntress | Mind Vision, Lullaby |
| Duelist | **Strength**, Mirror Image |
| Cleric | Purity, Remove Curse |

**Clues from drop sources** ✔ (all are visible facts, so they are legal):
* A potion dropped by a swarm, bat, necromancer or demon spawner is always **Healing** (`S/actors/mobs/Swarm.java:148-154`, `Bat.java:87-93`, `Necromancer.java:103-109`, `DemonSpawner.java:54`).
* A scorpio's potion is never Healing or Strength (`S/actors/mobs/Scorpio.java:109-113`).
* A succubus's scroll is never Identify or Upgrade (`S/actors/mobs/Succubus.java:175-177`).
* A scroll that burns can't be SoU, because SoU is `unique` (`S/actors/buffs/Burning.java:129`).
* Brewing 3 seeds of the same kind makes and **identifies** that seed's potion (`S/items/potions/Potion.java:534-536`).
* Energizing an item in the alchemy pot identifies it (`S/scenes/AlchemyScene.java:921`).
* A Stone of Intuition lets you guess an item's type (one stone is guaranteed on floors 1-3).
* Throwing an unknown potion **identifies it if it is one of the harmful ones**: Toxic, Liquid Flame, Paralytic, Frost (`S/items/potions/PotionOfToxicGas.java:43` etc.). A beneficial potion just splashes and is wasted (`S/items/potions/Potion.java:309-327`). A harmless splash still tells you it was not one of the harmful four.

**Drink-ID protocol** ☐ + ✔. Community consensus is: "Drink unknown potions near water, ideally next to the exit"; "explore before drinking". The mechanics behind it:
* Do it at **full HP, with no enemy in view**, after the floor is explored. Also stand away from heaps and scrolls lying on grass, and **not on or near grass**.
* Liquid Flame sets you burning (8 turns, 1 to 3+depth/4 damage a turn). After 4+ turns of burning a non-unique scroll in the main pack can burn (`S/actors/buffs/Burning.java:54,105,120-140`).
  * Standing in **water** puts the fire out the next turn (`:98-99`).
  * So: drink while standing on water, or next to it.
  * A Scroll Holder bag protects scrolls (they don't reach inside containers, `:126-127`).
* Toxic gas (1000 volume): walk out of the cloud at once, away from the gas.
* Paralytic gas: harmless if nothing is near.
* Frost freezes you. Freezing shatters one random non-unique potion in the main backpack *at your feet* (`S/actors/buffs/Frost.java:60-75`). Carry potions in the Potion Bandolier.
* Unknown drinkable potions are all safe to drink.

**Read-ID protocol** ✔:
* Reading an unknown scroll identifies it. If it is an *inventory* scroll (Upgrade, Identify, Remove Curse, Transmutation, and the exotics), the item picker opens with that scroll's prompt, and **cancelling wastes it** (`S/items/scrolls/InventoryScroll.java:33-41,127-128`).
* So before reading unknowns, decide:
  * an upgrade target (main armor or weapon);
  * an identify target (the most valuable unknown);
  * a remove-curse target (any cursed worn item);
  * a transmute target (**junk**: never your main gear).
* Rage aggroes the level, so read with a safe spot near; Teleportation moves you. Read in a cleared area, not mid-fight.

**Equipment** ✔:
* Random weapons, armor, rings, wands and artifacts are **30% cursed** (`S/items/weapon/Weapon.java:439-444`, `armor/Armor.java:671-676`, `rings/Ring.java:272-274`, `wands/Wand.java:561-563`, `artifacts/Artifact.java:221-223`).
* A cursed equipped item can't be removed (`S/items/EquipableItem.java:124-130`).
* A cursed ring at +0 gives −2 (`S/items/rings/Ring.java:389-395`).
* A cursed wand zaps random cursed effects.
* Trap, sentry and pool room prizes are **never cursed** (`S/levels/rooms/special/TrapsRoom.java:136`, `SentryRoom.java:177`, `PoolRoom.java:113`).
* Statue weapons are uncursed and always enchanted (`S/actors/mobs/Statue.java:70-71`).
* Ghost, Wandmaker and Blacksmith rewards are uncursed (`Ghost.java:336`, `Wandmaker.java:335,347`, `Blacksmith.java:400`).
* Crypt and sacrifice room armor is cursed unless it has a glyph (`CryptRoom.java:83-90`).
* Skeleton/remains heaps holding a cursed item are *haunted*: a wraith appears (`S/levels/RegularLevel.java:443-445`, `Heap.setHauntedIfCursed`).
* Community ☐: "Don't fear cursed items; most curses are minor". Equip an unknown higher-tier item if you have Remove Curse or an SoU to spare, or if STR allows and the current item is clearly worse.

### 2.3 When to fight or flee

The mechanics ✔:
* Hit chance = `rand(acc) ≥ rand(def)` (`S/actors/Char.java:619-679`).
* Magic attacks (wands) get **2x accuracy** (`:615-617`).
* An invisible attacker or a **surprise attack** always hits (`:628-630`, `S/actors/mobs/Mob.java:819-840`).
* A mob is surprised if it hasn't seen you (`enemySeen` false), or **its last-computed field of view doesn't contain your cell**, or you're invisible (`S/actors/mobs/Mob.java:910-914`).
* A mob's field of view is recomputed at the *start* of its turn, before it moves (`S/actors/Char.java:198-202`, `Mob.java:262-291`).

**Consequence:** a monster that steps around a corner or through a door to stand next to you hasn't "seen" you yet. Your next attack is a guaranteed-hit surprise attack. This is the mechanical basis of the doorway and corner ambush (2.4).

Flee or avoid ☐ + ✔:
* Don't fight two or more enemies in the open.
* Don't melee ranged and kiting enemies: Gnoll Trickster, Scorpio, Shaman, Warlock, DM-100, Eye. **Break line of sight** and make them come to you: "move door to door", "break LoS with ranged enemies".
* Avoid early fights you can't win: animated statues before your gear can handle them (15+5·depth HP; `S/actors/mobs/Statue.java`); mimics (4 in 20 chests are real; a mimic hits for 2+2·level with **infinite accuracy**, `Mimic.java`).
* **Escape tools, in order of reliability:** Teleportation (not on locked/boss floors, `S/items/scrolls/ScrollOfTeleportation.java:100-121`), Invisibility (20 turns), Haste, Blink stone, Fadeleaf seed, Rogue's cloak, stairs.
* Taking the stairs is a real escape: only allies change floors with you (`Mob.holdAllies`, `S/scenes/InterlevelScene.java:698-851`), enemies stay behind. Expect them near the stairs when you come back.

**HP thresholds** ☐ (consensus: "use a healing potion when death is near, don't hold it too long"): drink Healing at ≤30-35% HP in a fight you must win. It heals 0.8·HT+14 over a few turns and cures many debuffs (`S/items/potions/PotionOfHealing.java:54-64`).

### 2.4 Doorway and corridor fighting

✔ Movement and attacks are 8-directional, with no diagonal restriction at doors. A closed door blocks line of sight (`S/levels/Terrain.java:90`).

* Standing *in* a doorway exposes you to 3 cells on the room side.
* The strong position is **one step inside a 1-wide corridor or room behind the door**, so an attacker must stand *in* the door to reach you. That gives one at a time, and its first step into view is a surprise attack (2.3).

☐ Community practice:
* "Fight in corridors and doorways to avoid being surrounded."
* "Open doors to attack unaware enemies."
* "Circle tall grass": tall grass blocks LoS, so a monster chasing you round it keeps losing sight of you, and you get repeated surprise hits. This works on Goo.
* Snakes (evasion 25) are best killed with surprise attacks.

Bot pattern: when a hunting enemy approaches and a door or corner is within 1-2 steps, step out of its line of sight next to the opening and **wait**. Attack the turn it becomes adjacent. `Assist` already has a "doorway rule" (`stepBackWaited`, `Assist.java:126`).

### 2.5 Search

✔ `S/actors/hero/Hero.java:2490-2620`

* A deliberate search takes **2 turns** and costs **6 hunger** (`:210-211,2612-2620`).
* It **always** finds secret doors and normal traps in radius 1 (3x3) of visible cells. The Rogue searches radius 2 (5x5); the Wide Search talent adds +1 (`:2496-2498,2563-2564`).
* Passive (unintentional) detection while walking:
  * hidden traps 40% → 30% (depth 0→25);
  * hidden **doors 20% → 0% at depth 20** (`:2570-2576`).
  * After the prison, hidden doors are essentially only found by searching, Magic Mapping, Foresight or the Talisman.
* Some traps can't be found by searching at all (Tengu's dart traps; `canBeSearched`, `:2558-2559`).

☐ Where to search:
* dead-end corridors;
* walls of rooms that "should" connect;
* blank map regions bounded by known walls;
* burnt floor often hides traps.

**Don't search everywhere: it costs 6 food a time.** Search only when the floor is otherwise finished and the exit or a big unexplored region is plausibly behind a wall. Magic Mapping reveals all secrets (`M/items/items.properties`, `scrollofmagicmapping.desc`).

### 2.6 Hunger and food

✔ Hunger rises 1 per turn:
* "Hungry" at 300 (a warning only);
* "Starving" at 450, where you take HT/1000 damage per turn and natural regen stops.

Sources: `S/actors/buffs/Hunger.java:40-41,78-104`, `S/actors/buffs/Regeneration.java:56`. The Rogue's cloak while invisible slows hunger x1.5 (`Hunger.java:90-92`).

Food values ✔:

| Food | Value | Source |
|---|---|---|
| Ration | 300 | `Food.java:51` |
| Pasty | 450 | `Pasty.java:50` |
| Meat pie | 900, plus WellFed: 1 HP / 18 turns for 450 turns | `MeatPie.java:37-43`, `WellFed.java:67-69` |
| Small ration, mystery meat, cooked meat | 150 | |
| Berry | 100 | |
| Supply ration | 200 | |

* Eating takes 3 turns (1 with some talents) (`Food.java:47,101-110`).
* Raw mystery meat has a 4/5 chance of a bad effect: burning, roots, poison or slow (`MysteryMeat.java:54-70`). Cook it: burn it, freeze it, or use the alchemy pot.

**The real constraint is healing, not starvation.** Natural regen is **1 HP per 10 turns at every level** (`Regeneration.java:44`). Resting from 50% to full at HT 100 costs 500 turns, which is 500 hunger, about 1.7 rations. You get 1 food per floor (+ shops), so **HP lost in fights is paid for in food**.

Bot implications:
* avoid chip damage;
* prefer surprise and doorway kills;
* use Dew and Healing instead of long rests when food is short;
* eat at "Hungry" or just before resting, never while starving.

☐ Community: "use rations only when hungry/starving"; "don't eat a meat pie right before a boss, the timer runs" (boss floors freeze hunger anyway).

### 2.7 Shops and gold

✔ Buy price = `value × 5 × (depth/5 + 1)` (`S/actors/mobs/npcs/Shopkeeper.java:201-203`).

Every shop has (`S/levels/rooms/special/ShopRoom.java:219-330`):
* a tiered armor (Leather / Mail / Scale / Plate on 6 / 11 / 16 / 20);
* a T2-T5 weapon and a thrown set;
* 2 tipped darts and 2-3 Alchemize;
* **one bag you don't have** (`:361-395`);
* Potion of Healing, 2 random potions, Identify, Remove Curse, **Magic Mapping**, 2 random potions or scrolls;
* 2 small rations, a bomb (or double bomb, or honeypot), an **Ankh** and a Stone of Augmentation;
* hourglass sand bags (if you own the hourglass);
* a 30% "rare": a wand, a ring or an artifact.

Priorities ☐ (consensus plus mechanics):
1. **Bags**: the Scroll Holder protects scrolls from fire and the Potion Bandolier protects potions from frost (2.2). The Velvet Pouch is free at start (`HeroClass.java:111`).
2. **Ankh**.
3. Healing potion.
4. Magic Mapping (for the Halls).
5. Food if low.
6. Torches before the Halls (dark floors).

The Ankh ✔:
* A *blessed* Ankh (bless it with a **full 20-dew Waterskin**, `S/items/Ankh.java:65-80`) revives you in place at 25% HP, cured, briefly invulnerable (`S/actors/hero/Hero.java:2184-2197`).
* An unblessed one opens `WndResurrect` (`:2208-2222`), which sends you back with a loss of items.
* ☐ "Ankhs give you free mistakes; buy and bless them."

Never let damage or a debuff touch a shopkeeper. Area effects (gas, fire, bombs, lightning) make it **flee and destroy all its stock** (`Shopkeeper.java:93-186`).

Gold has few other uses: shops, and Blacksmith cash-out. Sell junk: a shopkeeper buys at `value()`.

### 2.8 Alchemy and energy

✔ One alchemy pot per region (the laboratory, on floor 3 or 4, with 5 energy crystals, `LaboratoryRoom.java:63-74`).

Energy values when you energize an item (`S/items/**/energyVal`):

| Item | Energy |
|---|---|
| Potion or scroll | 6 |
| Known SoU, PoS, Transmutation or Experience | 10 |
| Exotic | base + 4 (potion) / +6 (scroll) |
| Runestone | 3 (Augmentation and Enchantment: 5) |
| Seed | 2 |
| Brew, elixir or most spells | 12 |
| Goo blob, metal shard | 3 |
| Equipment | 0 |

Core recipes ✔ (`S/items/Recipe.java:170-220` and each item's `Recipe`):

| Recipe | Energy | Notes |
|---|---|---|
| 3 seeds → potion | 0 | All 3 the same kind = that potion, identified. Otherwise 25% (2 kinds) or 50% (3 kinds) random. Seed → potion: Sungrass→Healing, Fadeleaf→Mind Vision, Icecap→Frost, Firebloom→Liquid Flame, Sorrowmoss→Toxic, Swiftthistle→Haste, Blindweed→Invisibility, Stormvine→Levitation, Earthroot→Paralytic, Mageroyal→Purity, Starflower→Experience, Rotberry→Strength. Cooking Healing has diminishing odds. (`S/items/potions/Potion.java:472-546`) |
| scroll → 2 runestones | 0 | Identify→Intuition, Lullaby→Deep Sleep, Magic Mapping→Clairvoyance, Mirror Image→Flock, Retribution→Blast, Rage→Aggression, Recharging→Shock, Remove Curse→Detect Magic, Teleportation→Blink, Terror→Fear, Transmutation→Augmentation, Upgrade→Enchantment (`S/items/scrolls/Scroll.java:312-370`) |
| potion → exotic | 4 | Healing→Shielding, Mind Vision→Magical Sight, Frost→Snap Freeze, Liquid Flame→Dragon's Breath, Toxic→Corrosive, Haste→Stamina, Invisibility→Shrouding Fog, Levitation→Storm Clouds, Paralytic→Earthen Armor, Purity→Cleansing, Experience→Divine Inspiration, Strength→Mastery (`ExoticPotion.java:53-87,137-139`) |
| scroll → exotic | 6 | Upgrade→Enchantment, Identify→Divination, Remove Curse→Anti-Magic, … (`ExoticScroll.java:50-…,133-135`) |
| SoU → Magical Infusion | 12 | Upgrade that keeps the enchantment |
| PoS → Elixir of Might | 16 | +1 STR and a temporary HP boost |
| Healing + Goo blob → Aquatic Rejuvenation | 6 | |
| Healing + honeypot shard → Honeyed Healing | 2 | |
| Earthen Armor + Goo blob → Arcane Armor | 8 | |
| Dragon's Breath → Dragon's Blood | 10 | |
| Snap Freeze → Icy Touch | 6 | |
| Corrosive → Toxic Essence | 8 | |
| Levitation → Feather Fall | 10 | |
| Passage → Beacon of Returning | 12 | |
| Teleportation → Phase Shift | 10 | |
| Transmutation → Recycle | 12 | |
| Remove Curse + metal shard → Curse Infusion | 6 | +1 + level/6 upgrades while cursed (`Weapon.java:368`) |
| Recharging + shard → Wild Energy | 4 | |
| Magic Mapping + shard → Reclaim Trap | 8 | |
| Embers → Summon Elemental | 10 | |
| Toxic + Goo blob → Caustic Brew | 1 | |
| Frost → Blizzard | 8 | |
| Liquid Flame → Infernal | 12 | |
| Paralytic → Shocking | 10 | |
| Storm Clouds → Aqua Brew | 8 | |
| Ration + pasty + meat → Meat Pie | | `Recipe.java:219` |

**Tipping darts needs no pot**: 1 seed + 2 darts → 2 tipped darts (`S/items/weapon/missiles/darts/Dart.java:288-310`). Seed → dart: Rotberry→Rot, Sungrass→Healing, Fadeleaf→Displacing, Icecap→Chilling, Firebloom→Incendiary, Sorrowmoss→Poison, Swiftthistle→Adrenaline, Blindweed→Blinding, Stormvine→Shocking, Earthroot→Paralytic, Mageroyal→Cleansing, Starflower→Holy (`TippedDart.java:225-236`).

☐ Community:
* "Don't ignore alchemy."
* Make Magical Infusion for enchanted gear.
* Turn spare scrolls into stones (Blink, Deep Sleep and Augmentation are useful).
* Elixirs give boss immunities: Dragon's Blood vs fire (Tengu, Burning Fist); Icy Touch vs cold; Toxic Essence vs gas (DM-300, Rotting Fist).
* Turn junk potions into energy.

### 2.9 Dew and the Waterskin

✔
* Each dewdrop is 5% of max HP. The Waterskin holds 20 (a full heal) (`S/items/Waterskin.java:41,89-118`, `Dewdrop.java:83-86`).
* A drop picked up goes into the Waterskin if it isn't full. Otherwise it heals you at once (`Dewdrop.java:53-79`).
* Drinking spends only the drops needed (`Waterskin.java:91-118`).
* A **full** Waterskin is also how you bless an Ankh (2.7).
* Dew comes from trampled tall grass. Evil Eyes always drop something: dew, a seed or a stone (`Eye.java:64-65,224-230`).

Bot policy ☐: keep the Waterskin topped up; drink it in emergencies or before a boss; bless an Ankh when you hold one and the skin is full; collect dew on the way.

### 2.10 Exploration order

☐ + ✔:
* **Explore every regular floor fully** before descending. The fixed SoU/PoS drops (1.3) are somewhere on floors 1-4 of each region, and food is 1 per floor.
* Order within a floor:
  1. Clear and loot the rooms near the entrance.
  2. Explore outward, preferring rooms with visible items and unopened doors.
  3. Leave locked, trap and special rooms (pools, sentries, fire) for last, when you have the right tool.
  4. Search the dead ends (2.5).
  5. Take the exit.
* Use the time limit honestly: monsters respawn about every 50 turns up to the floor's limit (`S/levels/Level.java:149,752-767`). Dawdling costs food and adds fights.
* The **Demon Halls (21-24)** are an exception ☐: the community advice is "Demon Halls are usually best rushed"; "save Magic Mapping scrolls for them". Halls loot matters less than survival.
  * Even so, **kill the demon spawners** if you can. Each one alive when you fight Yog adds Ripper Demons to Yog's summon pool, instead of weak larvae (`S/actors/mobs/YogDzewa.java:129-149`). Each also drops a Healing potion (`DemonSpawner.java:54`).
* Pick up everything that isn't a known hazard. The Velvet Pouch (seeds/stones) and other bags extend the pack.
* Watch the **level feeling** messages:
  * *dark*: shorter vision, faster respawn (`Level.java:762-763`);
  * *large*: more items and +1 food;
  * *traps*: many traps, so walk the known path;
  * *secrets*: more hidden doors, worth searching.

---

## 3. Classes

### 3.0 What every class shares

✔ `S/actors/hero/HeroClass.java:100-145`

* **Starting items:** cloth armor, 1 ration, the Velvet Pouch, an empty Waterskin, and a known Scroll of Identify.
* **Subclass:** chosen by reading **Tengu's Mask**. The mask drops only if the hero has no subclass (`S/actors/mobs/Tengu.java:211-214`).
* **Armor ability:** chosen by using the **King's Crown** on armor. The crown drops from the Dwarf King (`DwarfKing.java:563-565`). Options per class (`HeroClass.java:280-292`); Ratmogrify is a 4th crown option in some cases (`S/items/KingsCrown.java:101`).
* **T4 talents:** the generic `HEROIC_ENERGY` (12/23/32/40% cheaper ability, `M/…/talent heroic energy`) plus 3 per ability.

**How to read the talent tables:**
* ✔ = the number comes from the talent text in `M/actors/actors.properties` (checked against code where noted).
* ☐ = a pick recommendation: community consensus plus reasoning for a bot that wants low-variance survival.
* "**bot-pick**" = the default for the bot: high passive value, no micro needed.

### 3.1 Warrior

**Kit** ✔ `HeroClass.java:174-187`:
* worn shortsword;
* 3 throwing stones (quickslot 0);
* the **Broken Seal**, fixed to his armor;
* knows Healing and Rage.

**Signature: the Broken Seal (v3.1 rework)** ✔:
* When an attack would take him to ≤50% HP, it gives a burst of shielding of `3 + 2·armorTier + IronWill` (`S/items/BrokenSeal.java:95-98`).
* Then a **150-turn cooldown**, which ticks only while regen is on (`:261,319-349`).
* The seal moves between armors and carries **one upgrade** (`M/actors/actors.properties:808-809`). Runic Transference also carries glyphs.
* ⚠ Pre-v3.1 guides describe a shield that builds up passively. That is gone (`CL3:508`).
* ☐ "Unequip the seal before using an SoU on armor, because upgrades transfer" (Steam guide). This is mostly moot now that the seal's level is capped at the single carried upgrade.

**Subclasses** ✔ `M/actors/actors.properties:838-842`:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Berserker** | Rage from physical damage taken (even blocked). Up to +50% damage. At 100% you can *berserk* for a big shield. ⚠ v3.2 cut rage gain and loss 25% and the shield to 8+2·lvl (steamdb v3.2.0 notes). | **Bot default.** Little micro; heavy armor makes it shine. "Berserker: armor to +10, then weapon." |
| **Gladiator** | Combo +1 per hit. It resets after 5 turns without a hit (15 after a kill). Spend combo at 2 (Clobber knockback), 4 (Slam, armor-based damage), 6 (Parry), 8 (Crush, area), 10 (Fury, multi-hit). | Stronger in skilled hands. Pairs with fast, accurate weapons (sai, gauntlets, ring of accuracy). |

**Talents** ✔ / ☐:

| Tier | Talents | Bot-pick |
|---|---|---|
| T1 | Hearty Meal (eating heals 4/6 HP at ≤33% HP); Veteran's Intuition (ID weapons faster, armor on equip at +2); Provoked Anger (+3/+5 damage after a shield breaks); Iron Will (+1/+2 seal shield) | **Iron Will 2, Hearty Meal 2, Provoked Anger 1** |
| T2 | Iron Stomach (eat in 1 turn, 75/100% damage resist while eating); Liquid Willpower (6.5/10% max-HP shield per potion); Runic Transference; Lethal Momentum (a kill costs 0 time at 67/100%); Improvised Projectiles (throw a non-weapon to blind 2/3 turns, 50-turn cooldown) | **Lethal Momentum 2, Iron Stomach 2, Liquid Willpower 2** |
| T3 | Hold Fast (waiting with the seal gives 1-2/2-4/3-6 armor and slows shield/combo decay 50/75/100%, ✔ `S/actors/buffs/HoldFast.java:72-82`; ⚠ CL3:433 says 33/67/100, the code says 50/75/100); Strongman (+8/13/18% STR, rounded down) | **Strongman 3** first (it unlocks heavier gear sooner), then Hold Fast |
| Berserker | Endless Rage (max rage 116/133/150%); Deathless Fury (auto-berserk on lethal damage at 100% rage, cooldown 3/2/1 levels); Enraged Catalyst | **Deathless Fury 3** (a passive death save) |
| Gladiator | Cleave (combo lasts 30/45/60 turns after a kill); Lethal Defense; Enhanced Combo | Cleave, Enhanced Combo |

**Armor abilities** ✔ `M/actors/actors.properties` short_desc:
* **Heroic Leap**: jump over enemies and hazards. Body Slam and Impact Wave add damage and knockback on landing. Also an escape.
* **Shockwave**: cone damage plus cripple.
* **Endure**: skip turns with high damage resistance, then deal bonus damage.

☐ Leap is the most flexible (escape + gap close). Shockwave is good for crowds. Endure suits bosses.

**Recommended build** ☐:
* Berserker; Plate or Scale armor taken to +6 to +10; a T4/T5 weapon; Strongman.
* Throw stones at range early.
* Warrior is the easiest class for a bot.

### 3.2 Mage

**Kit** ✔ `HeroClass.java:190-201`:
* the **Mage's Staff** holding a wand of Magic Missile (quickslot 0);
* knows Upgrade and Liquid Flame.

**Signature: the staff** ✔ `M/actors/actors.properties:814`, `S/items/weapon/melee/MagesStaff.java`:
* The staff has **1 more charge** than its wand and recharges much faster.
* Imbuing any wand replaces the effect. Level = max(staff, wand), +1 if the wand ≥ the staff and the staff > 0 (`:244-250`). **Re-imbuing never loses levels.**
* The staff is a T1 melee weapon with base max 6 (`:90-92`).
* ⚠ Speedrun guide: "put all early upgrades into the staff". This is still widely shared and still sound, because staff levels carry over.
* Wands' magic attacks have 2x accuracy (`S/actors/Char.java:615-617`).

**Subclasses** ✔:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Battlemage** | Melee hits with the staff trigger a wand-specific on-hit effect and give +0.5 staff charge | "Battlemage is designed to melee with the staff." Use it when the staff is your weapon and armor is good. |
| **Warlock** | Wand hits may *soul mark* an enemy. Physical damage to a marked enemy heals 2 HP per 5 damage. | "Warlock is meant to use a real melee weapon": open with a zap, then melee. **Bot default**: the sustain fixes the food-limited regen (2.6). |

**Talents**:

| Tier | Talents | Bot-pick ☐ |
|---|---|---|
| T1 | Empowering Meal; Scholar's Intuition (ID wands 3x / on use); Lingering Magic (+1-2/+2 melee after a zap); **Backup Barrier** (3/5 shield when the last staff charge is spent) | Backup Barrier 2, Scholar's 2 (Reddit recommends both) |
| T2 | Energizing Meal; Inscribed Power; Wand Preservation; Arcane Vision (mind vision on zapped targets); **Shield Battery** (self-zap turns charges into 4/6% HP shield each) | Shield Battery 2, Energizing Meal 2 |
| T3 | Desperate Power (+1/+2/+3 levels on the last charge); Ally Warp | Desperate Power |
| Battlemage | Empowered Strike; Mystical Charge; Excess Charge (shield on full-charge zap) | Empowered Strike, Excess Charge |
| Warlock | Soul Eater (satiety from marked damage); Soul Siphon (allies trigger marks); Necromancer's Minions (13/27/40% to raise a corrupted wraith) | Soul Siphon, Soul Eater (food!) |

**Wand choice for the staff** ☐ (Reddit tier list, Jan 2025):
* Top: Corrosion, Regrowth (OP: "the best ones"), **Blast Wave** ("for many runs the best wand"), **Disintegration** (pierces, more damage per enemy and wall), **Lightning** (strong when upgraded).
* Fireblast is strong but burns scrolls and loot.
* Weak: Living Earth (disputed), Corruption in the staff ("the worst").
* Frost, Corruption and Prismatic Light are named in other threads as good staff wands.
* For a bot, prefer simple direct damage: **Lightning** (watch the self-arc in water, ✔ `wandoflightning.stats_desc`), **Disintegration**, **Blast Wave** (knockback is also an escape), **Frost**.

**Armor abilities**:
* **Elemental Blast**: a large area effect keyed to the staff's wand.
* **Wild Magic**: fires your wands several times.
* **Warp Beacon**: a teleport anchor; at T4 it can go between floors.

☐ Elemental Blast is the strongest general pick.

### 3.3 Rogue

**Kit** ✔ `HeroClass.java:204-218`:
* dagger (surprise-attack bonus);
* **Cloak of Shadows**, worn as an artifact;
* 3 throwing knives;
* knows Magic Mapping and Invisibility.
* Searches at radius 2 instead of 1 (`Hero.java:2497`).

**Signature: the cloak** ✔ `S/items/artifacts/CloakOfShadows.java:55-60,202-204`:
* Charges = level+3, capped at 10.
* It levels up by being used (up to +10), not by SoU.
* Invisibility ⇒ every attack is a guaranteed-hit surprise attack (`Char.java:628-630`).
* ☐ "Use the cloak often, since use upgrades it."

**Subclasses**:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Assassin** | While invisible, preparation builds for up to 9 turns: bonus damage, a blink to the target, and an **instant kill** below 3/10/20/50% HP by preparation level | **Bot default**: cloak, prep, strike is easy to plan |
| **Freerunner** | Momentum while moving becomes freerunning: 2x speed and bonus evasion | Kiting and thrown builds |

**Talents**:

| Tier | Talents | Bot-pick ☐ |
|---|---|---|
| T1 | Cached Rations (2/3 supply rations in chests); Thief's Intuition; Sucker Punch; **Protective Shadows** (shield while invisible) | Cached Rations 2 (food!), Protective Shadows 2 |
| T2 | Mystical Meal (artifact recharge on eating); Inscribed Stealth (3/5 turns invisibility after a scroll); Wide Search; Silent Steps (don't wake sleepers unless adjacent at +2); Rogue's Foresight | Mystical Meal 2, Silent Steps 2, Wide Search 1 |
| T3 | Enhanced Rings; Light Cloak (use the cloak unequipped) | Light Cloak (frees the artifact slot) |
| Assassin | Enhanced Lethality (execute thresholds up to 6/20/40/100%); Assassin's Reach; Bounty Hunter | Enhanced Lethality 3 |
| Freerunner | Evasive Armor; Projectile Momentum; Speedy Stealth | |

**Armor abilities**:
* **Smoke Bomb**: blink plus blind; an escape.
* **Death Mark**: the mark makes the target take more damage, and it can't die until the mark ends.
* **Shadow Clone**: an ally.

☐ Smoke Bomb for safety.

☐ Build: "prioritize a strong melee weapon over armor" (gamepretty/Steam guide). Assassin's Blade (T4: sneak plus big surprise damage) is the classic end weapon.

### 3.4 Huntress

**Kit** ✔ `HeroClass.java:221-231`:
* studded gloves (T1, attack twice);
* the **Spirit Bow** (infinite arrows);
* knows Mind Vision and Lullaby.
* She **does not trample tall grass**: it stays LoS-blocking around her.

**Signature: the Spirit Bow** ✔ `S/items/weapon/SpiritBow.java:189-200`:
* Damage 1+lvl/5 to 6+lvl/2.5, plus SoU levels.
* Thrown and bow attacks get +50% accuracy at range and −50% adjacent; Point Blank fixes the penalty (`M/…/talent point blank`).
* The bow itself is an SoU target ☐.
* Ranged attacks are weak against other ranged enemies (warlocks, scorpios, eyes), so break line of sight for those.

**Subclasses**:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Sniper** | Ranged attacks pierce armor. After a thrown-weapon hit, a special bow shot: snapshot (unaugmented), volley (speed) or sniper shot (damage). | **Bot default**: simple and strong |
| **Warden** | Sees through tall grass. Seeds she throws or plants sprout grass. Trampled plants give buffs instead of their normal effect, so no plant is harmful to her. ⚠ v4 nerf: Barkskin no longer triggers from furrowed grass, only uncut grass and plants, and gives 33% of level per point (`CL4:367`). | Plant-heavy play |

**Talents**:

| Tier | Talents | Bot-pick ☐ |
|---|---|---|
| T1 | **Nature's Bounty** (4/6 berries in grass, i.e. food); Survivalist's Intuition; Followup Strike (+2/+3 melee after a ranged hit); Nature's Aid | Nature's Bounty 2, Followup 2 |
| T2 | Invigorating Meal; Liquid Nature; Rejuvenating Steps; **Heightened Senses** (mind vision in 2/3 tiles); Durable Projectiles (+50/+75% durability; ✔ the text matches CL3:228; ⚠ v3.2 notes on steamdb say +33/+50, reverted in v3.3) | Heightened Senses 2, Durable Projectiles 2 |
| T3 | **Point Blank**; Seer Shot | Point Blank 3 |
| Sniper | Farsight; Shared Enchantment; Shared Upgrades | Shared Upgrades |

**Armor abilities**:
* **Spectral Blades**: thrown blades based on the melee weapon.
* **Nature's Power**: speed plus bow rate of fire.
* **Spirit Hawk**: a scout and distraction.

☐ Steam: "Huntress recommended against Tengu". Another opinion: "Huntress falls off later".

### 3.5 Duelist

**Kit** ✔ `HeroClass.java:233-246`:
* rapier;
* 2 throwing spikes;
* knows **Strength** and Mirror Image.

**Signature: weapon abilities** ✔ `M/items/items.properties` (`*.ability_desc`):
* Every melee weapon has an active ability that uses weapon charges.
* Charges cap at 2+(lvl−1)/3, max 8 (Champion: 4+…, max 10). They recharge at about 1 per 60−1.5·missing turns (`S/items/weapon/melee/MeleeWeapon.java:423-495`).

Useful abilities for a bot (all guaranteed hits unless noted):

| Ability | Weapons | Effect |
|---|---|---|
| **Lunge** | Rapier, Katana | Move 1 tile and hit |
| **Cleave** | Sword family | Free and instant if it kills |
| **Spike** | Spear, Glaive | Reach attack plus knockback |
| **Heavy Blow** | Mace, axes, cudgel | Big damage on a surprised target, plus Daze |
| **Guard** | Round shield, Greatshield | Negate all attacks for a few turns |
| **Sneak** | Dagger family | Blink plus invisibility; an escape |
| **Harvest** | Sickle, Scythe | Big bleed |
| **Retribution** | Greataxe | Below 50% HP; instant if it kills |
| **Combo Strike** | Gloves, Sai, Gauntlet | Damage grows with hits landed |
| **Sword Dance** | Scimitar | +60% attack speed |
| **Defensive Stance** | Quarterstaff | 3x evasion |

⚠ The developer's announcement blog lists different Monk abilities (Flurry, Cleanse, Dash, Focus). The current code and text are: 1 Flurry, 2 Focus (dodge the next attack), 3 Dash, 4 Dragon Kick, 5 Meditate (`M/actors/actors.properties:866`).

**Subclasses**:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Champion** | A second weapon; swap instantly; use either ability; +2 max charges; +50% recharge | **Bot default** (more abilities = more guaranteed hits) |
| **Monk** | Energy from kills, capped by level, never decays. ⚠ v4: no longer kept over revives or into the vault (`CL4:373`). | Low-gear runs |

**Talents**:

| Tier | Talents | Bot-pick ☐ |
|---|---|---|
| T1 | Strengthening Meal; Adventurer's Intuition; Patient Strike (+1-2/+2 after waiting; pairs with doorway waits); Aggressive Barrier (3/5 shield when using an ability at ≤50% HP) | Patient Strike 2, Aggressive Barrier 2 |
| T2 | Focused Meal (+0.67/1 charge on eating); Liquid Agility; Weapon Recharging; **Lethal Haste** (instant movement after an ability kill); Swift Equip | Focused Meal 2, Lethal Haste 2 |
| T3 | Precise Assault; Deadly Followup | Precise Assault |
| Champion | Varied Charge; **Twin Upgrades** (the lower weapon copies the higher's level); Combined Lethality | Twin Upgrades 3 (one upgrade stack for two weapons) |
| Monk | Unencumbered Spirit; Monastic Vigor; Combined Energy | |

**Armor abilities**:
* **Challenge**: duel one enemy, freezing the others. v4 cost 50 (`CL4:348`).
* **Elemental Strike**: a cone keyed to the enchantment.
* **Feint**: an afterimage decoy. v4 cost 35.

**Build** ☐: Champion with a high-tier main weapon plus a utility second weapon (shield or spear). Put SoU into the main weapon; Twin Upgrades covers the second. Wear armor too.

### 3.6 Cleric

**Kit** ✔ `HeroClass.java:248-261`:
* cudgel;
* the **Holy Tome** (artifact; quickslot 0);
* knows Purity and Remove Curse.

**Signature: the tome** ✔:
* Charges = level+3, max 10. It levels up by use (`S/items/artifacts/HolyTome.java:55-59,173`).
* T1 base spells (`S/actors/hero/spells/ClericSpell.java:104-120`):
  * **Guiding Light**: ranged magic damage, guaranteed hit. It illuminates the target, giving it **0 defense** against the Cleric's next attack (`S/actors/mobs/Mob.java:819-830`).
  * **Holy Weapon**: boosts weapon damage.
  * **Holy Ward**: boosts armor.
* More spells come from talents.

**Subclasses** ✔ `M/actors/actors.properties:868-872`:

| Subclass | Mechanic | When ☐ |
|---|---|---|
| **Priest** | Free Guiding Light every 50 turns; Radiance (illuminate and stun everything visible, 2 charges); long-range spells (Holy Lance) | Ranged and wand synergy |
| **Paladin** | Stronger Holy Weapon and Ward that stack with enchantments; Smite (guaranteed hit with bonus damage) | **Bot default** (melee, simple) |

**Talents** (Reddit, Dec 2024: "use talents to add spells… Bless and Sunray make melee safer; Cleanse is one of the best T3"):

| Tier | Talents | Bot-pick ☐ |
|---|---|---|
| T1 | Satiated Spells; Holy Intuition (detect a curse for 3/2 charges); Searing Light (+3/+5 damage on illuminated enemies); Shield of Light | **Holy Intuition 1** (safe equipping!), Searing Light 2, Shield of Light 2 |
| T2 | Enlightening Meal; Recall Inscription; **Sunray** (4-8/6-12 damage plus blind; paralysis on the second cast); Divine Sense (mind vision); **Bless** | Sunray 2, Bless 2, Enlightening Meal 2 |
| T3 | **Cleanse**; Light Reading | Cleanse |
| Priest | Holy Lance (30-55 / 45-83 / 60-110, 30-turn cooldown); Hallowed Ground; Mnemonic Prayer | Holy Lance |
| Paladin | Lay on Hands (15/20/25 heal or shield); Aura of Protection; Wall of Light | Lay on Hands 3 |

**Armor abilities**:
* **Ascended Form**: new spells, attack range and shielding (Reddit: "very strong").
* **Trinity**: copies the effect of gear you've seen.
* **Power of Many**: empower or create an ally.

---

## 4. Bosses

All five bosses: their HP, phases and attacks come from the source. Tactics are community advice, tagged as such.

**Universal boss prep** ☐:
* Arrive at full HP: you can't rest mid-fight (1.1).
* Bring 2+ Healing potions, a full Waterskin, and ideally a blessed Ankh.
* Arrive at or near the region's level cap.
* Have the region's SoU and PoS used.
* Remember you **can't teleport on locked boss floors** (`S/items/scrolls/ScrollOfTeleportation.java:100-121`).
* Boss floors don't consume hunger while locked.

### 4.1 Goo (depth 5)

✔ `S/actors/mobs/Goo.java`

* **Stats:** 100 HP (54), defense 8 (×1.5 at ≤50% HP, `:93`). Damage 1-8, or 1-12 at ≤50% HP (`:68-79`).
* Each hit has a 1/3 chance to apply **Ooze** (`:157-158`): 1 damage a turn (vs Goo) until you stand in water (`S/actors/buffs/Ooze.java:93-103`).
* **Pump-up:**
  * Instead of a normal attack it may pump up: 1/5 of the time, 1/2 at ≤50% HP (`:187`).
  * The next turn it charges (`:180-185`). Then it unleashes a **×3 damage, ×2 accuracy** blast that reaches **2 tiles** in a clear line (`:142-151`, `:77`).
  * Moving out of range, or Goo having to move, **cancels the pump** (`getCloser`/`getFurther` reset it, `:240-258`).
* **Water heals Goo** +1 HP a turn while it stands in water (`:109-120`).
* At 50% it enrages (shown on the boss bar).

**Tactics** ☐ (Reddit "how to defeat Goo", Steam):
* When Goo pumps ("!!!" and a message), **step to 3+ tiles away or break line of sight**.
* Hit and run around corners and doorways.
* Circle tall grass for repeated surprise hits.
* Fight on dry ground and keep Goo off water.
* Step into water yourself only to clear Ooze.
* Fire works (a firebloom, a Liquid Flame potion), then keep it away from water.
* Bring Healing.
* ⚠ The speedrun guide says "fight near water". The source says Goo heals in water; only *you* benefit from water, against Ooze.
* Avoid slow weapons (spear) that give it double turns.

**Drops:** the **Worn Key** (opens the floor's locked door; ⚠ renamed from "skeleton key" in v3.3, CL3:17) and a Goo blob (`:291-300`).

### 4.2 Tengu (depth 10)

✔ `S/actors/mobs/Tengu.java`, `S/levels/PrisonBossLevel.java`

* **Stats:** 200 HP (90), defense 15, damage 6-12 (`:102-103`).
* **Accuracy is 10 when adjacent and 20 at range** (`:106-112`): stand next to him.
* **He can't lose more than 1/8 of his HP (25) in one hit**; extra damage is wasted (`:139-149`).
* **Phase 1** (the prison block, until 50%, `:180-184`):
  * Every 25 HP lost, he jumps (teleports).
  * Each jump scatters hidden **Tengu dart traps** in the 7x7 cell between you and him. They show for a moment and then fade (`PrisonBossLevel.java:668-726`).
  * Each dart is 8 poison, about 17 damage (`S/levels/traps/TenguDartTrap.java:37-42`).
  * **The bot should record the trap cells when they appear.** That is visible information, not cheating: the developer says "you never need to take poison damage… watch for them and take the clear path".
* **Phase 2** (the arena) uses abilities. The schedule is 1 + 2 per jump, more after jump 2 (`:482-490`). The first is always a bomb, the second a shocker (`:496-530`).
  * **Bomb:** a 3-turn fuse with "3… 2… 1…" shown. Radius 2 (path distance), 15-30 damage minus armor at depth 10 (`:607-640`).
  * **Fire:** a spreading wave of fire in one direction (`:746-905`).
  * **Shocker:** alternates diagonals and orthogonals each turn; 12 damage at depth 10 (`:975-1069`).
* Drops Tengu's Mask (if you have no subclass) (`:211-214`).

**Tactics** ☐ (Steam):
* Phase 1: melee, walk only on cells you saw were clear, and set him on fire if you can.
* Phase 2:
  * close in, using Blink stones and walking through his smoke/fire walls as they move toward you;
  * Frost puts you out if you catch fire;
  * stay out of the shocker's current pattern;
  * leave the bomb's 2-tile radius by turn 3.
* Save Invisibility; heal before the fight (the Sungrass and sleep trick); keep the Waterskin full.
* ⚠ Old wiki pages (vanilla PD Tengu, 120 HP, shuriken, poison traps "in the middle of the room") are outdated.

### 4.3 DM-300 (depth 15)

✔ `S/actors/mobs/DM300.java`, `S/levels/CavesBossLevel.java`

* **Stats:** 300 HP (84), defense 15, damage 15-25 (`:95`). Abilities every 5-9 turns (`:112-116`), more aggressive if it can't reach you (`:192-235`).
  * **Toxic gas vent** aimed at you (`:373-400`).
  * **Rockfall:** knocks you back 2 tiles if adjacent, then drops rocks over a 7x7 area. The targeted cells are shown and **one neighbour cell is always safe** (`:409-470`). Each rock does 6-12 and **3 turns of paralysis** (`:688-699`).
* **Barrier from the floor:** when DM walks onto an arena trap tile (`INACTIVE_TRAP`) while hunting, it gains a barrier of 30+(missing HP)/10 (`:323-343`). **Fight it away from those tiles.**
* **Supercharge (pylons):**
  * At 200 and 100 HP it becomes **invulnerable and supercharged** (`:497-506`).
  * One pylon activates. If there's a choice it is **never the one closest to you** (`CavesBossLevel.java:382-401`).
  * While supercharged DM moves at **2x speed** and chases you (`:286-303,349-351`).
  * Trap tiles and water become **electrified** (6-12 a turn, `CavesBossLevel.java:403-407,914`).
  * The **pylon** has 50 HP. It zaps rotating adjacent cells for 10-20 (`S/actors/mobs/Pylon.java:54,95-138`).
  * Destroying the pylon ends the supercharge (`:545-569`). After the last pylon, DM is vulnerable to the end.

**Tactics** ☐:
* Bring Purity (or Toxic Essence) for gas; armor matters most.
* On rockfall, step into the safe cell or out of the marked area.
* On supercharge, **run to the active pylon**, avoiding trap and water tiles, and kill it while DM chases.
* Melee DM when it is not charged, on plain floor.
* ⚠ The old wiki (pre-v0.8: "repairs itself on triggered traps", "avalanche on every step") is outdated. The barrier and rockfall above are the current behaviour.

**Drops:** Metal Shards, an alchemy ingredient (`DM300.java:587`). The Cape of Thorns artifact exists in the source but isn't generated normally.

### 4.4 Dwarf King (depth 20)

✔ `S/actors/mobs/DwarfKing.java`

* **Stats:** 300 HP (85), defense 22, damage 15-25 (`:95`).
* **Phase 1** (300 → 50 HP):
  * **Summons** through summoning circles (a 3-turn delay, `:161-165`): every 3rd summon is a monk or warlock, every 9th a golem, the rest ghouls (`:303-306`).
  * **Life Link** to his farthest minion, or **teleport/swap** with one (`:173-198,347-400`).
  * Every 8 damage you deal shortens both cooldowns (`:491-493`).
* **Phase 2** (at 50 HP):
  * He teleports to the throne, becomes **immovable and invulnerable**, and gains a barrier equal to his max HP (300) (`:495-516,446-452`).
  * Waves of summons: 4 ghouls, then 4 more (one monk or warlock), then 4 warlocks/monks/ghouls (`:255-292`).
  * **Each phase-2 minion that dies or is converted removes HT/12 = 25 of the barrier** (`KingDamager`, `:770-794`). Killing all 12 breaks it.
* **Phase 3:**
  * He moves again; up to 4 concurrent summons (`:296-298`).
  * **Damage to him is deferred** as damage over time, like Viscosity (`:472-479`).
* **Standing on a summoning circle when it completes deals 20-40 damage** to whoever is there (`:690-719`). Never end a turn on a circle that is about to complete.

**Tactics** ☐ (Reddit):
* Kill minions first and keep them between you and the warlocks.
* In phase 2, killing the minions is the *only* way forward: fight at a chokepoint.
* ⚠ Old wiki tips ("plant earthroot on a pedestal", "flock on the opposite pedestal", "stone of aggression on the boss") are outdated. v4 made **Stones of Aggression unable to target bosses** directly; they still work on minions (`CL4:359`).
* Honeypots or bees at the spawn points (Reddit) and toxic gas plus invisibility have been used.
* Bring area damage (bombs, Lightning, Disintegration) and Healing.

**Drops:** the **King's Crown** (armor ability) (`:563-565`). The Imp's shop on this floor opens afterwards, if he earned it (1.1).

### 4.5 Yog-Dzewa (depth 25)

✔ `S/actors/mobs/YogDzewa.java`, `YogFist.java`

* **Stats:** 1000 HP (72).
* **Phase 0:** dormant and invulnerable until you approach (`:179,388`).
* **Every 300 HP lost** (`:411-430`): darkness grows (vision shrinks to 4/3/2/1, `:474-478`), and **one fist** appears from each pair, shuffled: Burning or Soiled, Rotting or Rusted, Bright or Dark (`:107-115`).
  * **Yog is invulnerable while any fist lives** (`:387-389`).
  * ⚠ Wiki: "damage divided while fists remain". The source says fully invulnerable.
* Phase 4: Yog's HP is floored at 100 until the last fist dies, then phase 5 is a summon burst (`:399-403,360-366`).
* **Death beams:** 1+(1000−HP)/400 beams (1, then 2 below 600 HP, 3 below 200). 20-30 damage each, on telegraphed cells, destroying flammable terrain (`:196-291`). ⚠ Wiki says 700/300 thresholds.
* **Summons** every 10-15 turns: **Ripper Demons for each demon spawner still alive, larvae otherwise** (`:129-149`). ⚠ Wiki: "a larva spawns whenever he takes damage". That is vanilla PD behaviour.
* **Fists:** 300 HP, defense 20, 18-36 melee, ranged attack every 8-12 turns (`YogFist.java:65-190`). **Invulnerable within 4 tiles of Yog** (`:114-125`): lure them away.

| Fist | Trait | Source |
|---|---|---|
| Burning | Fire around itself; evaporates water; immune to frost | `:216-287` |
| Soiled | Takes less damage near grass; roots you; burning hurts it | `:289-397` |
| Rotting | Converts damage to bleed (60%); heals in water; toxic gas; ooze | `:399-464` |
| Rusted | All damage deferred; 22-44 hits; cripple | `:466-499` |
| Bright | Light beam 10-20 + blind. At 50% it teleports and blinds you long. | `:501-567` |
| Dark | Dark bolt; drains Light. At 50% it teleports and darkens. | `:569-645` |

**Tactics** ☐ (wiki, Reddit):
* Pull each fist far from Yog before damaging it.
* Use pillars and corners against the beams.
* Fight the Rotting fist off water.
* Bring Healing, Invisibility, Mind Vision, Magic Mapping, Torches, and burst items (bombs, upgraded Disintegration/Lightning/Corrosion).
* **Kill the demon spawners on 21-24 first.**

### 4.6 Quest bosses (short)

| Boss | Quest | Mechanics (✔) | Tactic ☐ |
|---|---|---|---|
| Fetid Rat | Ghost, depth 2 | 20 HP; ooze on hit; **paralytic stench gas** when hit (`S/actors/mobs/FetidRat.java:42-83`) | Ranged, or fight in water (washes ooze) from a spot outside the gas. Never melee it in a dead end. |
| Gnoll Trickster | Ghost, depth 3 | 20 HP; **keeps its distance** and throws darts. Combo poisons, and at 6+ burns (`GnollTrickster.java:80-110`). | Corner it; break LoS and ambush; use thrown weapons. |
| Great Crab | Ghost, depth 4 | 25 HP; **blocks every attack and wand from its target while it sees you** (`GreatCrab.java:75-112`). Moves 2 of 3 turns. | Surprise-attack it while it is asleep or unaware; or invisibility, paralysis, allies, or area damage (bombs, gas). Kosgames: "the giant crab is the exception" to surprise rules: ⚠ only once it has seen you. |
| Newborn fire elemental | Wandmaker (embers) | Telegraphed charged bolt (`Elemental.java:260-290`) | Step off the targeted cell. Frost or water. |
| Rot Heart | Wandmaker (seed) | 80 HP, immobile; toxic gas when hit (`RotHeart.java:42-83`). **Fire destroys it with no seed** (`:61-68,96-104` vs `die` `:108-111`). | **Never use fire there.** Kill it with melee or wands; step out of the gas. Lashers: 80 HP, immobile, regenerate, cripple. |
| Crystal Spire / Gnoll Geomancer | Blacksmith | Spire: only the pickaxe hurts it, and hitting it alerts the cave. Geomancer: armored, rock attacks, sappers empower it. (`M/actors/actors.properties:1545-1669`) | Optional: +1000 favor (5.3) |
| Vault boss | Imp | New in v4; swaps forms (`CL4` bug notes) | See 5.4 |


---

## 5. Quests

### 5.1 Sad Ghost (Sewers)

✔ `S/actors/mobs/npcs/Ghost.java`

**Trigger.** It spawns on depth 2, 3 or 4: on depth *d* with chance 1/(5−d), so it is certain by depth 4 if not yet spawned (`:303-320`). It cannot wander onto heaps or the exit (`:76-77`).

**Steps.**
1. Talk to the ghost.
2. A quest boss spawns on the same floor; its type depends on the depth:
   * depth 2: Fetid Rat;
   * depth 3: Gnoll Trickster;
   * depth 4: Great Crab (`:313-316,172-185`).
3. Kill it, then talk to the ghost again.

**Reward.** Choose **one** of a weapon or an armor (`:321-361`):
* uncursed;
* tier 2/3/4/5 at 50/30/15/5%;
* level +0/+1/+2/+3 at 50/30/15/5%;
* a 20% chance of an enchantment or glyph.

**Choice** ☐: "Always help the ghost, it may give you usable armor." "Mages should take armor." Bot rule:
* Take the **armor** unless the weapon is ≥2 tiers above your current weapon and the armor is ≤ your armor.
* Duelists may prefer the weapon.
* Check STR (8+2·tier).

**Pitfalls.**
* The crab blocks everything from its target while it sees you (4.6), so open with a surprise attack or use area damage.
* The trickster kites; corner it.
* The Fetid Rat's gas paralyses, so never fight it at a choke where the gas pools on you.

### 5.2 Old Wandmaker (Prison)

✔ `S/actors/mobs/npcs/Wandmaker.java`

**Trigger.** It spawns on depth 7, 8 or 9: chance 1/(10−d), certain by 9 (`:353-372`). Three variants are chosen at random (`:358-370`).

**Reward.** Two different uncursed **+1 wands**; you choose one (`:334-348`).

| Variant | Room | What to bring back | Steps and dangers |
|---|---|---|---|
| 1 | Mass Grave | **Corpse dust** | Pick up the dust from the grave. While you carry it, **wraiths keep spawning**: spawn power grows each turn, and one appears when power ≥ (wraiths alive)² (`S/items/quest/CorpseDust.java:84-140`). Walk straight back to the Wandmaker. |
| 2 | Ritual Site | **Embers** | Place the 4 ceremonial candles on the 4 cells around the ritual spot (N/E/S/W, `S/items/quest/CeremonialCandle.java:123-140`). A **Newborn Fire Elemental** appears. Kill it for the embers. Its charged bolt is telegraphed, so step off the cell. Speedrun guide: "use a freeze or chill item"; ☐ "fresh embers is fastest". |
| 3 | Rot Garden | **Rotberry seed** | Kill the **Rot Heart** (80 HP, immobile, toxic gas when hit) among **Rot Lashers** (80 HP, immobile, regenerate 5 a turn, cripple). **Fire destroys the heart without dropping the seed** (`S/actors/mobs/RotHeart.java:61-111`). ⚠ Speedrun guide: "rotberry is slowest". |

**Which wand** ☐ (Reddit, Jan 2025):
* Mage: the wand you want in the staff.
* Others prefer Blast Wave, Disintegration, Lightning, Corrosion or Frost; Regrowth is strong but tricky.
* Avoid Warding and Living Earth unless your build uses them.

**Pitfall.** Keep the rotberry seed if you want STR: a Rotberry seed brews into Strength (2.8). Kosgames: "combine it with blandfruit … for +1 strength". But it's the quest item, so hand it over unless you'd rather keep the STR (☐ the wand is usually worth more).

### 5.3 Troll Blacksmith (Caves): the v2 quest

✔ `S/actors/mobs/npcs/Blacksmith.java`, `S/windows/WndBlacksmith.java`, `S/levels/MiningLevel.java`

**Trigger.** It spawns on depth 12, 13 or 14: chance 1/(15−d), certain by 14 (`:347-354`). Quest type is **Crystal** or **Gnoll** (`:354`). Fungi exists in the code but is "not implemented" (`:205`).

**Steps.**
1. Talk; you get a **pickaxe**.
2. Take the ladder down into the **Mines** (a branch floor).
3. You get **one trip** only ("I'm only lettin' you down der once!", `M/actors/actors.properties:1330`), so clear pack space for the gold first.
4. Mine **dark gold** veins (wall-deco tiles).
   * Picking up gold is free (`Hero.java:1344`).
   * Mining a plain wall costs 4 hunger in total (`Hero.java:1356-1362`).
   * **40 ore = 2000 favor** (cap) (`Blacksmith.java:446-450`).
5. Optional quest boss for **+1000 favor** (`:463`):
   * Crystal: the **Crystal Spire**. Only the pickaxe damages it, and hitting it wakes the crystal guardians. "Perhaps the spire's own attacks could be used against its minions" (`M/actors/actors.properties:1549`). Mining crystals next to guardians alerts them (`Hero.java:1317-1330`).
   * Gnoll: the **Gnoll Geomancer**. It is encased in rock (break it with the pickaxe); a sapper nearby makes it immune; it throws boulders and drops rockfall, against which **armor doesn't help** (`M/levels/levels.properties`, `gnollrockfalltrap`).
6. Return and talk. Favor = min(2000, 50·ore) + 1000 if the boss died. A total ≥ 2500 gives a free pickaxe (`:443-470`).

**What the favor buys** ✔ `WndBlacksmith.java:79-189,268-440`:

| Service | Cost | Effect |
|---|---|---|
| Pickaxe | 250 (free at ≥2500) | Keep the pickaxe (a weak T2) |
| **Reforge** | 500, then 1500, 2500 | Consume a second item of the same type; the first gets **+1** (keeps a good enchantment) |
| Harden | 500, then 1500 | The enchantment/glyph resists upgrades (2.1) |
| **Upgrade** | 1000, then 2000 | +1 to an identified, uncursed item **at +0 or +1 only** (`:422-427`) |
| **Smith** | 2000 | Choose one of: 2 melee weapons, 1 thrown set, 1 armor. Tier odds T3/T4/T5 = 20/40/40%; level +0/+1/+2/+3 at 30/45/20/5%; 30% enchanted; uncursed (`Blacksmith.java:362-415`, `Generator.java:613-619`) |
| Cash out | all | 1 gold per favor |

**Choice** ☐ (cheapest upgrades first):
* With duplicates: Reforge 500, then Upgrade 1000 on a +0/+1 item, then Reforge 1500 (3000 total = 3 upgrades).
* Without duplicates: Upgrade (1000) a +0/+1 main armor, then **Smith** (2000) if a T4/T5 piece beats your gear. Otherwise do a second Upgrade on the same item if it is still below +2.
* Reddit: "use spare scale mail with the troll to raise it to +7 or +8".
* ⚠ Old guides: "mine 15 dark gold / kill a bat with a pickaxe" or "only useful with duplicate gear". That is the pre-v2.2 quest.

**Pitfalls.**
* The single trip.
* Inventory space.
* Waking guardians.
* Hunger from digging.
* Normal enemies respawn slowly in the mines (`MiningLevel.java:173-175`).

### 5.4 Ambitious Imp (City): the v4 Vault quest

✔ `S/actors/mobs/npcs/Imp.java`, `S/levels/VaultLevel.java`, `M/actors/actors.properties:1367-1376`, `M/items/items.properties:946-980`

**Trigger.** It spawns on depth 17, 18 or 19: chance 1/(20−d), certain by 19 (`Imp.java:306-308`). ⚠ Every pre-v4 guide describes the old quest: kill 4 golems or 5 monks for dwarf tokens, reward a random cursed or upgraded ring. That survives only for old saves (`Imp.java:129-152,370-388`).

**Steps.**
1. Talk to the Imp, then step into the vault entrance.
2. **All your gear is stored.** You enter the vault (`VaultLevel`, branch 1, about three floors in size) with **cloth armor and an Escape Crystal** (`:1370`).
3. The vault uses **stealth AI** (CL4 "stealth" entry):
   * nearby enemies are "heard" through walls;
   * sleepers notice you only at short range;
   * wanderers patrol routes and barely see behind themselves;
   * a new **investigating** state follows you without attacking and loses you easily behind doors and corners.
   * Hazards: **scanning sentries** (you are zapped if detected; anything that blocks vision helps), **disintegration sentries** (fixed beams on a timer), fire vents.
4. Loot treasure rooms (tier 1 → tier 3). Vault enemies always drop **dwarf tokens**; 10 open a **token door** (`actors.properties:1857-1865`).
5. A **Strange Mirror** returns your class item (seal, staff, cloak, bow, …) (`:1867-1874`).
6. Beat the vault boss, take the **obsidian statuette**, and use the Escape Crystal.

**Reward.**
* You keep **one item from the vault**, depending on how well you did (`items.properties:955-960`):
  * leave from the start room: nothing;
  * partly explored: a potion or scroll;
  * fully explored: nothing upgraded;
  * "partial victory": an item at +1 or less;
  * statuette: anything.
* The pool includes a +5 artifact (or a +2..+4 ring), a +2..+4 ring, an enchanted T5 weapon at +2..+4 or T4 at +3..+5, enchanted thrown weapons, a **+2..+4 inscribed Plate**, and a +2..+4 wand (`Imp.java:313-353`).
* **The Imp's shop on depth 20 opens only if score > 2000** (`Imp.java:397-399`, `CityBossLevel.java:128`).

**Safety.**
* Below 1/3 HP the game offers the escape advice (`Hero.java:950-975`).
* **Death in the vault is real.**
* Vault beacons in the entry room teleport you back (`VaultEntranceRoom.java:122-132`).
* v4.0.1 added a healing potion near the entrance and keeps awake caves+ enemies out of rooms near it (`CL4:124-125`).
* Fire doesn't burn scrolls and frost doesn't shatter potions in the vault (`Burning.java:120`, `Frost.java:60`).

**Bot recommendation** ☐: the vault is a separate stealth puzzle. A first-generation bot should **skip it**: don't talk to the Imp, or don't step in. The cost is the Imp's shop and one strong item. If attempted:
* stay out of sentry scan cones and enemy facing;
* move door to door;
* bail with the crystal at <1/3 HP.

---

## 6. Items by category

Effects come from the source; usage advice is ☐ unless cited. "ID" = how to identify safely.

### 6.1 Potions

✔ `S/items/potions/*`, `M/items/items.properties`

| Potion | Effect | Use | ID / danger |
|---|---|---|---|
| Healing | Heals 0.8·HT+14 over time; cures poison, bleeding, etc. (`PotionOfHealing.java:54-64`) | The emergency button at ≤30-35% HP | Safe to drink. The most common colour (6/30). Swarm/bat/necro/spawner drops are always this one. |
| Strength | +1 STR permanently. `unique`. (`PotionOfStrength.java:37`) | Drink at once ("excess strength adds bonus damage") | Safe. 2 per region. |
| Mind Vision | 20 turns: see creatures through walls (`MindVision.java:30`) | Scouting, bosses, the Halls | Safe |
| Frost | Freezing cloud (`PotionOfFrost.java:52`) | Throw it: freezes enemies, puts out fire | Harmful if drunk; it identifies when thrown |
| Liquid Flame | Fire (`PotionOfLiquidFlame.java:53`) | Throw at groups, Goo, Tengu; burn doors and grass | Harmful; drink only on water (2.2) |
| Toxic Gas | 1000-volume toxic cloud (`PotionOfToxicGas.java:49`) | Throw at a group in a room you then leave; good on bosses (they resist) | Harmful; walk out at once |
| Paralytic Gas | 1000-volume paralysis cloud (`PotionOfParalyticGas.java:49`) | Throw from range; don't stand in it | Harmful only with enemies near |
| Haste | 20 turns of fast movement (`Haste.java:33`) | Escape, closing gaps | Safe |
| Invisibility | 20 turns; attacking breaks it (`Invisibility.java:38`) | Escape; a guaranteed surprise hit; Tengu; the vault | Safe |
| Levitation | 20 turns; float over traps and chasms (`Levitation.java:40`) | Trap rooms, pits, Tengu traps | Safe; thrown it confuses |
| Purity | Clears gases within 3 tiles; immunity if drunk (`PotionOfPurity.java:44-57`) | DM-300 gas, any gas | Safe |
| Experience | Instantly gain a level | Drink | Safe (1/30) |

**Exotic potions** (potion + 4 energy, 2.8):
* Shielding (a big shield);
* Magical Sight (see through walls, 12 radius);
* Snap Freeze (freeze and root);
* Dragon's Breath (a cone of fire);
* Corrosive Gas (stronger, shorter toxic);
* Stamina (long haste);
* Shrouding Fog (blocks enemy vision: the counter to scanning sentries);
* Storm Clouds (floods the area, breaks traps);
* Earthen Armor (natural armor);
* Cleansing (debuff immunity);
* Divine Inspiration (+2 talent points in one tier);
* Mastery (−2 STR requirement on one item).

**Brews** (thrown):
* Blizzard and Infernal (spreading frost or fire);
* Shocking;
* Caustic;
* Aqua (water burst: puts out fire, disarms traps);
* Unstable (a random potion: good if drunk, harmful if thrown).

**Elixirs:**
* Might (+1 STR and +HP);
* Dragon's Blood (fire immunity; hits ignite);
* Icy Touch;
* Toxic Essence;
* Feather Fall;
* Honeyed Healing (healing plus food; can be thrown at allies);
* Aquatic Rejuvenation (heals over time in water);
* Arcane Armor (magic resistance).

### 6.2 Scrolls

✔ `S/items/scrolls/*`

| Scroll | Effect | Use | ID / danger |
|---|---|---|---|
| Upgrade | +1 to an item or a thrown set; un-curses (2.1) | See 2.1 | Inventory scroll: have a target ready. Mage knows it. |
| Identify | Fully identifies one item | Unknown worn gear first, then rings and wands | Inventory scroll; 6/30, the commonest |
| Remove Curse | Removes curses from one item | Keep one spare ("curses are sticky") | Inventory scroll |
| Transmutation | Same-category random item that keeps its upgrades; potion ↔ exotic | Turn a bad ring, wand or artifact into a different one | Inventory scroll: **never pick main gear** |
| Mirror Image | 2 illusory clones (`M/…scrollofmirrorimage.desc`) | Tank and distract; strong on bosses | Safe |
| Recharging | Recharges wands over time | Wand builds, bosses | Safe |
| Teleportation | Random teleport, preferring unvisited areas; **not on locked or non-regular floors** (`ScrollOfTeleportation.java:100-121`) | **The best escape** | Safe outside boss floors |
| Lullaby | Makes everyone in sight drowsy (and you) | Escape or setup | Safe-ish |
| Magic Mapping | Reveals the layout and **all secrets** | Hidden-door floors; the Halls ("save 3-5 for the Halls") | Safe |
| Rage | All enemies are drawn to you; nearby ones get 5 turns of Amok (they fight each other) (`ScrollOfRage.java:47-49`) | Combo with area damage | Don't read unknowns hurt |
| Retribution | Damage to visible enemies that grows the lower your HP is; it blinds and weakens you (`ScrollOfRetribution.java:52-68`) | A last resort at low HP | |
| Terror | Enemies in sight flee | Escape | Safe |

**Exotic scrolls** (scroll + 6 energy):
* Enchantment (choose an enchantment);
* Divination (identifies 4 random unknown types);
* Anti-Magic;
* Prismatic Image (a persistent defender);
* Psionic Blast;
* Passage (go to the region's first floor: shop runs);
* Foresight (long secret detection);
* Mystical Energy (artifact recharge);
* Siren's Song (an enemy becomes an ally);
* Dread (enemies flee the floor);
* Challenge (an arena with −33% damage);
* Metamorphosis (swap a talent).

### 6.3 Runestones

✔ `S/items/stones/*`; scroll → 2 stones (2.8)

| Stone | Effect | Bot use ☐ |
|---|---|---|
| Intuition | Guess an item's type (correct = identified) | Use on the most valuable unknown potion or scroll |
| Detect Magic | Is an item cursed, upgraded, enchanted? | Before equipping an unknown |
| Augmentation | Weapon: speed↔damage; armor: defence↔evasion | Damage on slow and heavy weapons ☐ |
| Enchantment | Random enchantment on a weapon or armor | Main gear |
| Blink | Teleport to where it lands | Escape; Tengu gap close |
| Deep Sleep | Magical sleep until disturbed | Remove a dangerous enemy, then ambush (surprise attack) |
| Shock | Stun all nearby, plus wand charge | Crowd control |
| Clairvoyance | Reveal a wide area through walls | Scouting |
| Aggression | Enemies near the target attack it; **v4: no direct effect on bosses** (`CL4:359`) | Turn minions on each other |
| Blast | Explodes like a bomb | Damage |
| Fear | The target flees | Escape |
| Flock | Temporary sheep that block | Block a corridor |

### 6.4 Wands

✔ `S/items/wands/*`, `M/items/items.properties` (`*.stats_desc`)

**Shared rules:**
* Base 2 charges + level, max 10 (`Wand.java:429-435`).
* Recharge time per charge = 10 + 40·0.875^missing turns (`Wand.java:803-851`).
* 30% cursed (`:561-563`).
* Identifies after 10 uses, gated to 5 uses per hero level of XP (`:93-95,283-288,455-470`).
* **Magic hits get 2x accuracy.**

| Wand | Effect | Notes ☐ |
|---|---|---|
| Magic Missile | Plain damage; when upgraded, briefly empowers other wands | Mage start |
| Lightning | Damage that arcs to nearby characters, more in water; **can hit you** | Top tier when upgraded; don't zap in water next to the target |
| Disintegration | A piercing beam, bonus per enemy and wall pierced | Top tier |
| Fireblast | A cone of fire using several charges | Strong; **burns scrolls and loot**, burns grass |
| Corrosion | A corrosive gas cloud with rising damage | Top tier |
| Blast Wave | A blast plus knockback | "For many runs the best wand"; knockback is an escape |
| Living Earth | Rock bolts that give armor; builds a guardian | Weak (disputed) |
| Frost | Damage plus chill; freezes in water | Good control |
| Prismatic Light | Damage plus blind; reveals hidden things; bonus vs demons and undead | Good in the Halls |
| Warding | Summons wards and sentries | Niche, underrated |
| Transfusion | Heal or shield allies; charm enemies (you get a shield); damages undead | Support |
| Corruption | Debuffs, then turns enemies into allies | Necromancer builds; "worst in the staff" |
| Regrowth | A cone of grass, roots and rare plants | "Best, not debatable" (OP) but needs a plan |

### 6.5 Rings

✔ `S/items/rings/*`

Scaling is per effective level (+1 at +0); a cursed ring is ≤0 (`Ring.java:389-395`).

| Ring | Per level | Bot value ☐ |
|---|---|---|
| Accuracy | ×1.3 accuracy | Good with multi-hit weapons |
| Evasion | ×1.125 evasion | Solid defence |
| Haste | ×1.15 speed (⚠ v4: down from 1.175, `CL4:360`) | Escape and kiting; widely liked |
| Furor | ×1.0905 attack speed | Good on enchant procs |
| Might | +STR and HP | Strong all-round |
| Force | Unarmed or weapon damage | Duelist/Monk niche |
| Elements | ×0.825 elemental damage and duration | Halls, bosses |
| Energy | ×1.175 wand/artifact recharge | Mage, Cleric |
| Arcana | ×1.175 enchantment power | With good enchantments |
| Sharpshooting | ×1.2 thrown | Huntress |
| Tenacity | Less damage the lower your HP (×0.85) | Berserker |
| Wealth | ×1.2 loot | Early "scroll dumping", late farming |

### 6.6 Artifacts

✔ `S/items/artifacts/*`

The generated pool (`Generator.java:560-576`): the Cloak, Rose and Tome have 0 weight because they come from class or quest. Artifacts level up by use, not by SoU (usually). 30% cursed.

| Artifact | Effect | Use ☐ |
|---|---|---|
| Alchemist's Toolkit | Alchemy anywhere | Energy economy |
| Chalice of Blood | Prick yourself (it damages you) to level it up; boosts regen up to 500% at +10 (`Regeneration.java:70-78`) | "Prick yourself! An unupgraded chalice doesn't do much." It **fixes the food-limited regen**. Prick at full HP in safety. |
| Cloak of Shadows | Invisibility (Rogue) | 3.3 |
| Dried Rose | Ghost ally; v4 HP 40+10·lvl, damage scales with STR (`CL4:338`) | Strong ally |
| Ethereal Chains | Pull yourself to terrain or enemies to you, even through walls | Mobility and escape |
| Holy Tome | Cleric | 3.6 |
| Horn of Plenty | Food from XP | Solves food |
| Master Thieves' Armband | Steal from enemies or shops; v4 recharge scales with level | Gold |
| Sandals (Footwear) of Nature | Plant effects from seeds | |
| Skeleton Key (v3.3) | Open locks, lock doors, magic walls | Control |
| Talisman of Foresight | Detects secrets and traps | Search replacement |
| Timekeeper's Hourglass | Stop or freeze time; sand bags in shops | Escape |
| Unstable Spellbook | Random scroll effects | |

### 6.7 Seeds and plants

✔ `M/plants/plants.properties`, `S/plants/*`

**A plant triggers when anything steps on it.** Throw an item onto it to trigger it remotely (Steam guide). Planting a seed takes a turn.

| Plant | Effect | Use |
|---|---|---|
| Sungrass | Slow but strong healing while you stand still | "Plant it, step on it, and wait" before bosses. Heals well if not interrupted. |
| Earthroot | Immobile armor | Tank a hard fight standing on it |
| Fadeleaf | Teleports the toucher | Escape (or banish an enemy) |
| Firebloom | Fire | Trap enemies; burn Goo |
| Icecap | Freezing pollen, stronger when wet | Control |
| Sorrowmoss | Poison | |
| Swiftthistle | Several instant actions | |
| Blindweed | Blinding flash | Escape |
| Stormvine | Vertigo | |
| Mageroyal | Cleanses debuffs | |
| Starflower | Bless | |
| Rotberry | Toxic puff (quest) | |

### 6.8 Bombs, thrown weapons, darts

* **Bomb:** damages everything adjacent and destroys some terrain and items (`M/items/items.properties` `bomb.desc`). Shops sell bombs.
* **Custom bombs** (crafted): Arcane (Goo-like magic blast in 2 tiles), Fire, Frost, Flashbang (stun 10 turns), Holy (+50% vs undead and demons), Noisemaker (a lure), Smoke (shrouding fog), Regrowth (heals allies), Shrapnel (huge range: hide behind cover), Woolly (sheep wall).
* **Thrown weapons (v3.2 overhaul)** ✔ `CL3` / steamdb v3.2.0:
  * They spawn in **sets of 3**. An SoU upgrades the whole set and fully repairs it.
  * Tiers (`Generator.java:497-534`):

| Tier | Thrown weapons |
|---|---|
| T1 | stone, knife, spike |
| T2 | fishing spear, club, shuriken |
| T3 | throwing spear, kunai, bolas |
| T4 | javelin, tomahawk, heavy boomerang |
| T5 | trident, hammer, force cube |

  * Thrown weapons (and the bow) have a 1-turn delay when not aimed at a target.
  * At range they get +50% accuracy; adjacent, −50% (Huntress text).
  * **Pick thrown weapons back up after fights.**
* **Darts:** unlimited-ish (1000 base uses, `Dart.java:60`).
  * **Tip** them with seeds without a pot: 1 seed → 2 darts (2.8).
  * Tipped darts have low durability (`TippedDart.java:63`).
  * Best tipped darts ☐: Paralytic (Earthroot), Poison (Sorrowmoss), Chilling (Icecap), Blinding (Blindweed), Displacing (Fadeleaf: teleport an enemy away), Healing (Sungrass: heals the *target*, so use it on allies), Holy (bless), Cleansing, Rot, Shocking, Incendiary, Adrenaline.

### 6.9 Spells

✔ `S/items/spells/*`, `M/items/items.properties`

| Spell | Effect |
|---|---|
| Alchemize | Turn items into gold or energy; identifies potions and scrolls. Shops sell 2-3. |
| Magical Infusion | Upgrade that keeps the enchantment |
| Phase Shift | Teleport a target (or yourself) and stun it |
| Beacon of Returning | A set/return teleport |
| Recycle | Reroll a consumable |
| Telekinetic Grab | Grab items from range or from a stuck enemy |
| Summon Elemental | |
| Reclaim Trap | Store a trap and fire it later |
| Curse Infusion | +1(+lvl/6) while cursed |
| Wild Energy | Recharge plus a cursed effect |
| Unstable Spell | A random scroll: combat effect if enemies are in view |

### 6.10 Trinkets

✔ The Trinket Catalyst comes once, on floors 1-3, in a locked chest; its key is elsewhere on the floor (`RegularLevel.java:450-458`). It offers a choice of trinkets (17 types, `Generator.java:582-603`). Upgrade trinkets with energy. Reddit: "pick good trinkets and upgrade them to +3 right away".

**Bot-safe trinkets** ☐:
* Thirteen-Leaf Clover;
* Salt Cube (less hunger, also less regen; auto-off in boss fights, `CL3:530`);
* Mossy Clump ("still far and away the strongest", CL3:611: more grass/water floors).

Avoid the Mimic Tooth and Cracked Spyglass for a bot: they mean more mimics and hard-to-see items.

---

## 7. Monsters by region

Stats ✔ from `S/actors/mobs/<Name>.java`. "acc" = attackSkill, "def" = defenseSkill, "DR" = armor roll, "max lvl" = hero level above which it gives 0 XP. Spawn tables: `S/actors/mobs/MobSpawner.java:12-120`. Rare variants appear 1/50 of the time (`:245-275`).

### 7.1 Sewers (1-4)

| Monster | HP | def | acc | dmg | DR | max lvl | Threat and counter |
|---|---|---|---|---|---|---|---|
| Sewer rat (Albino: +bleed) | 8 | 2 | 8 | 1-4 | 0-1 | 5 | Trivial |
| Snake | 4 | **25** | 10 | 1-4 | | 7 | Very evasive: **surprise-attack it**, or skip it ("snakes have a very high evasion rate") |
| Gnoll scout | 12 | 4 | 10 | 1-6 | 0-2 | 8 | |
| Swarm of flies | 50 | 5 | 10 | 1-4 | | 9 | **Splits on hit**: fight it in a corridor. Drops are Healing (limited). |
| Sewer crab | 15 | 5 | 12 | 1-7 | 0-4 | 9 | Fast. Drops mystery meat. |
| Slime (Caustic: ooze) | 20 | 5 | 12 | 2-5 | | 9 | |
| Goo, depth 5 | 100 | 8 | | 1-8 / 1-12 | 0-2 | | 4.1 |

### 7.2 Prison (6-9)

| Monster | HP | def | acc | dmg | DR | max lvl | Threat and counter |
|---|---|---|---|---|---|---|---|
| Skeleton | 25 | 9 | 12 | 2-10 | 0-5 | 10 | **Explodes on death** for 6-12 to adjacent cells, armor applied twice (`Skeleton.java:81-126`): don't stand next to it at low HP |
| Thief (Bandit: debuffs) | 20 | 12 | 12 | 1-10 | 0-3 | 11 | **Steals** a random non-unique, un-upgraded, unequipped item and flees (`Thief.java:146-160`). If it gets 6+ tiles away, out of sight, it can escape with the item (`:199-222`). Kill it at range or in a corridor. |
| DM-100 | 20 | 8 | 11 | 2-8 | 0-4 | 13 | Ranged lightning: close in or break LoS |
| Prison guard | 40 | 10 | 12 | 4-12 | 0-7 | 14 | **Chains (pulls) you from range**. Drops armor. |
| Necromancer (Spectral variant) | 40 | 14 | | | 0-5 | 14 | Summons a skeleton: kill the necro first. Drops Healing (limited). |
| Tengu, depth 10 | 200 | 15 | 10/20 | 6-12 | 0-5 | | 4.2 |

### 7.3 Caves (11-14)

| Monster | HP | def | acc | dmg | DR | max lvl | Threat and counter |
|---|---|---|---|---|---|---|---|
| Vampire bat | 30 | 15 | 16 | 5-18 | 0-4 | 15 | Fast and erratic; drains HP. Fight in a corridor. Drops Healing (limited). |
| Gnoll brute (Armored variant) | 40 | 15 | 20 | **15-40** | 0-8 | 16 | **Enrages (shields) instead of dying the first time** (`Brute.java:55-104`). Big hits: doorway plus surprise. |
| Gnoll shaman | 35 | 15 | 18 | 5-10 | 0-6 | 16 | Ranged magic plus a debuff: break LoS, ambush |
| Cave spinner | 50 | 17 | 22 | 10-20 | 0-6 | 17 | Webs and poison; it backs off |
| DM-200 (DM-201) | 80 (120) | 12 | 20 | 10-25 | 0-8 | 17 | Vents toxic gas: fight it outside the cloud |
| DM-300, depth 15 | 300 | 15 | | 15-25 | 0-10 | | 4.3 |

### 7.4 Dwarven City (16-19)

| Monster | HP | def | acc | dmg | DR | max lvl | Threat and counter |
|---|---|---|---|---|---|---|---|
| Ghoul | 45 | 20 | 24 | 16-22 | 0-4 | 20 | **Gets back up** if a partner ghoul is alive and near (`Ghoul.java:84-125`): kill pairs together, or finish the partner |
| Elementals (fire, frost, shock, chaos) | 60 | 20 | 25 | 20-25 | 0-5 | 20 | Fire: use water or frost. Drops: fire → Liquid Flame, frost → Frost, shock → Recharging, chaos → Transmutation (`Elemental.java` loot). |
| Dwarf warlock | 70 | 18 | 25 | 12-18 | 0-8 | 21 | Ranged bolt that **degrades** your gear: break LoS. Drops potions. |
| Dwarf monk (Senior) | 70 | 30 | 30 | 12-25 | 0-2 | 21 | Very evasive and quick: surprise attacks. Drops food. |
| Golem | 120 | 15 | 28 | 25-30 | 0-12 | 22 | **Teleports you to it, or itself to you** (`Golem.java:100-120`). Slow but tanky. Drops weapons or armor. |
| Dwarf King, depth 20 | 300 | 22 | | 15-25 | 0-10 | | 4.4 |

### 7.5 Demon Halls (21-24)

| Monster | HP | def | acc | dmg | DR | max lvl | Threat and counter |
|---|---|---|---|---|---|---|---|
| Succubus | 80 | 25 | 40 | 25-30 | 0-10 | 25 | **Charms** you and heals off the charmed (`Succubus.java:80-98`). **Blinks** at 3+ tiles (`:112-151`). "Easy in a hallway with a corner." Drops scrolls (never ID/SoU). |
| Evil eye | 100 | 20 | 30 | 20-30 | 0-10 | 26 | **Death gaze beam, 30-50**, charged with a visible cooldown (`Eye.java:87-120,193`): **break LoS while it charges**. Always drops dew, a seed or a stone. |
| Scorpio (Acidic) | 110 | 24 | 36 | 30-40 | 0-16 | 27 | Ranged; cripples; flees from melee. Fight it at a door. Drops potions (never Healing or Strength). |
| Ripper demon | 60 | 22 | 30 | 15-25 | 0-4 | | Leaps (telegraphed). Spawned by demon spawners. |
| Demon spawner | 120 | 0 | | | 0-12 | 29 | Immobile; spawns rippers about every 60 turns. Damage delays it. **Kill it**: it drops Healing and weakens Yog (4.5). |
| Yog, depth 25 | 1000 | | | | | | 4.5 |

### 7.6 Anywhere

* **Mimic:** 6·(1+level) HP; a chest that bites with **infinite accuracy** (`Mimic.java`). 1 in 5 regular chests on depth >1 is a mimic (`RegularLevel.java:410-421`). Golden mimics guard locked-chest loot (`:427-436`). Crystal mimics live in crystal vaults.
  * ☐ Approach unknown chests at good HP.
  * Mimics stay disguised until touched or they act; there is no legal way to tell them apart except Mind Vision or examining. ⚠ Using `instanceof Mimic` on a heap would be cheating.
* **Animated statue:** HP 15+5·depth; a guaranteed enchanted, uncursed weapon. **Passive until attacked** (`Statue.java:47,120-132`).
  * Fight it when your gear outclasses its depth; skip it on early floors with weak gear.
  * "Search a statue to read what it holds." Alarm traps can summon statues.
* **Wraith:** from haunted remains (cursed items) or carrying corpse dust. 1 HP, but accurate.
* **Piranha:** water-only; HP 10+5·depth, hits hard. **Never fight or swim in deep water with them**; they die out of water. `Assist` tracks `fishSeen` (`Assist.java:130`).
* **Bee:** from honeypots; attacks anything nearby.

---

## 8. Traps, hazards and special rooms

### 8.1 Traps

✔ Per-region pools (weights in brackets) from `S/levels/*Level.java` `trapClasses()`/`trapChances()`. Descriptions are from `M/levels/levels.properties`.

| Region | Common (weight 4) | Less common (2) | Rare (1) |
|---|---|---|---|
| Sewers | Chilling, Shocking, Toxic, Worn Dart (floor 1: Worn Dart only) | Alarm, Ooze | Confusion, Flock, Summoning, Teleportation, Gateway |
| Prison | Chilling, Shocking, Toxic, Burning, Poison Dart | Alarm, Ooze, Gripping | Confusion, Flock, Summoning, Teleportation, Gateway, Geyser |
| Caves | Burning, Poison Dart, Frost, Storm, Corrosion | Gripping, Rockfall, Guardian | Confusion, Summoning, Warping, Pitfall, Gateway, Geyser |
| City | Frost, Storm, Corrosion, Blazing, Disintegration | Rockfall, Flashing, Guardian, Weakening | Disarming, Summoning, Warping, Cursing, Pitfall, Distortion, Gateway, Geyser |
| Halls | as City | as City | as City, + **Grim** |

**Most dangerous** ☐ + ✔:
* **Grim**: can one-shot all but the healthiest heroes. Visible.
* **Disintegration**: big damage, destroys items. Visible.
* **Rockfall**: the whole room.
* **Storm, Blazing, Corrosion**.
* **Summoning, Alarm, Guardian, Distortion**: they bring monsters.
* **Warping**: teleports you *and wipes your floor map*.
* **Pitfall**: drops you to the next floor (fall damage).
* **Disarming**: teleports your weapon elsewhere.
* **Cursing**: curses items in the area.

**Rules:**
* Never step on a known trap. You can trigger it deliberately by **throwing any item onto it** (from range).
* Burnt floor often hides traps.
* Trap-feeling floors: walk known cells.
* Searching finds hidden traps (2.5).
* Levitation crosses traps.
* Rogue and Talisman make them visible earlier.

### 8.2 Hazards

* **Fire** spreads over flammable cells (grass, doors, bookshelves) (`S/actors/blobs/Fire.java:41-80`). Water stops it. Burning: 2.2.
* **Gases:**
  * toxic damages per turn;
  * paralytic paralyses (lethal with enemies near);
  * corrosive is stronger;
  * confusion randomizes movement (don't fight near chasms).
  * **Leave gas the way it isn't spreading.** Purity clears it.
* **Chasms**: falling = descending with fall damage (a legal shortcut ☐ when the floor is done and HP is high).
* **Deep water**: piranhas; items can't be picked up underwater.
* **Darkness** (dark floors, Yog's phases): shorter vision. Torches and Light help.

### 8.3 Special rooms

✔ `S/levels/rooms/special/*`, `S/levels/rooms/secret/*`

| Room | What | Danger and requirement ☐ |
|---|---|---|
| Shop | 2.7 | Don't hurt the shopkeeper |
| Laboratory | Alchemy pot plus 5 energy plus guide pages | |
| Armory, Treasury, Storage, Library, Runestone, Garden | Free loot (locked rooms need a key from the floor) | |
| **Traps room** | A corridor of traps to a prize (67% item, uncursed, one floor-set higher) (`TrapsRoom.java:129-151`) | Levitation, or throw items to trigger, or search; ⚠ don't walk it blind |
| **Sentry room** | Sentries zap a dangerous path to the prize (50%, uncursed) (`SentryRoom.java:72-177`) | Invisibility / out-of-sight path |
| **Pool room** | The prize under deep water with **piranhas** (33%, uncursed) (`PoolRoom.java:106-113`) | Haste, invisibility, or skip |
| **Magical fire room** | A wall of fire in front of loot | Frost potion, or (kosgames) throw an Icecap seed and trigger it with an item |
| **Toxic gas room** | A gas-filled room; a skeleton with 2x gold plus 2 chests (no mimics) (`ToxicGasRoom.java:59-78`) | Purity, or hold your breath (short trips) |
| **Pit room** | A hole down to a pit with loot and a wraith | Feather Fall, or jump at high HP |
| **Weak floor room** | A collapsing floor over a well | Levitation |
| **Statue room** | An animated statue guarding its weapon | 7.6 |
| **Crypt** | Remains plus a **cursed armor** (with a free upgrade) unless glyphed (`CryptRoom.java:76-90`) | Remove Curse ready |
| **Sacrifice room** | An altar: kill enemies on it for a prize (cursed unless glyphed) | |
| **Magic well** | Water of Health (heal, feed, **uncurse worn items**, `S/actors/blobs/WaterOfHealth.java:58-65`) or Water of Awareness (identify) | Health: save it for an emergency or before leaving. "Throwing the waterskin into it fills it" (Steam guide, unverified). |
| **Crystal vault / choice / path** | Crystal doors and keys; a choice of loot; crystal mimics | |
| **Demon spawner room** (Halls) | The spawner | Kill it |
| **Secret rooms** (hidden door) | Garden, hoard, honeypot, larder, library, maze, runestone, summoning, well, artillery, chest chasm, laboratory | Found by searching or mapping. ☐ Worth it only when cheap. |


---

## 9. Bot priorities: the 20 behaviours that matter most

These are ranked by expected impact on a bot that currently dies on floors 3-8. Each has the evidence for it and a concrete rule.

1. **Fight at doors and corners; ambush.**
   * Evidence: a mob whose last field of view (computed *before* it moved) doesn't include you is surprised. A surprise attack has 0 defense, so it is a guaranteed hit plus class bonuses (`Mob.java:819-840,910-914`, `Char.java:198-202,628-630`). In a 1-wide spot only one attacker reaches you.
   * Community: "fight in corridors and doorways", "move door to door", "open doors to attack unaware enemies".
   * **Rule:** when a hunting enemy approaches, step to a cell out of its LoS beside the opening it must come through, wait, and hit the turn it arrives. Never fight 2+ enemies in the open.
2. **Spend consumables before dying.**
   * Evidence: Healing gives 0.8·HT+14 and cures (`PotionOfHealing.java:54-64`). Most early deaths in community threads are "unused tools".
   * **Rule:** in a fight you can't leave, drink known Healing at ≤35% HP. With no Healing, use the escape order (2.3). As a last resort, drink the *most common unknown* potion colour (by deck odds, Healing is 6/30, the likeliest).
3. **Upgrade armor, and use what you find.**
   * Evidence: armor DR max = tier·(2+level) (`Armor.java:384`), so a plate gains +5 max block per SoU. The consensus fix for mid-game deaths is "armor first". 3 SoU and 2 PoS per region are guaranteed (1.3).
   * **Rule:** drink PoS at once. Put SoU into worn armor (T2+) until about +3 to +5 by the caves, then the weapon. Don't hold more than 1-2 unused SoU.
4. **Explore floors 1-4 of each region completely and pick everything up.**
   * Evidence: the fixed SoU/PoS/stylus/food are placed there (`Level.java:226-245`); 3-5 random items per floor (`RegularLevel.java:379-385`).
   * **Rule:** don't take the exit until every reachable room is seen and every reachable heap picked up, except known hazards.
5. **Retreat and escape logic.**
   * Evidence: Teleportation (except locked floors), Invisibility (20 turns), Haste, Blink and stairs all exist for this.
   * **Rule:** if expected damage taken before the enemy dies is more than current HP minus a margin, disengage: step back through a door, or use an escape item, or go upstairs.
6. **Break line of sight with ranged and kiting enemies.**
   * Evidence: DM-100, Shaman, Warlock (degrades gear), Gnoll Trickster, Scorpio and Eye (30-50 beam) all attack from range (section 7).
   * **Rule:** never trade shots in the open with a ranged enemy you outclass in melee. Pull back behind a corner or door so it must approach (then rule 1).
7. **The HP-food economy.**
   * Evidence: regen is a flat 1 HP / 10 turns (`Regeneration.java:44`); hunger is 1 a turn; food is about 1 per floor (`Level.java:226`). Every lost HP costs about 10 turns of food to rest back.
   * **Rule:**
     * avoid chip damage;
     * rest only in safe, cleared spots and only as much as needed for the next fight;
     * keep the Waterskin full and drink it in emergencies;
     * eat at Hungry (300), never reach Starving (450);
     * prefer sustain sources (Warlock, Chalice, Horn, Sungrass).
8. **The Goo protocol (floor 5 is the first big killer).**
   * Evidence: `Goo.java:142-258`. The pumped blast is ×3 damage within 2 tiles; moving away cancels the pump; Goo heals on water.
   * **Rule:**
     * enter at full HP with Healing;
     * when Goo shows the pump ("!!!"), step to distance ≥3 or out of LoS;
     * fight it on dry floor;
     * use water only to wash Ooze;
     * circle tall grass or a door for surprise hits.
9. **The identification protocol** (2.2).
   * **Rule:**
     * drink-ID only at full HP, nothing in view, standing on water, away from grass, with potions in the bandolier;
     * read-ID only with upgrade, identify and transmute targets ready (cancelling wastes the scroll);
     * use drop-source clues;
     * brew 3 identical seeds for a guaranteed identified potion;
     * an Identify scroll goes on unknown worn gear first.
10. **Free damage before melee.**
    * Evidence: magic has 2x accuracy (`Char.java:615-617`); thrown weapons get +50% accuracy at range.
    * **Rule:** zap or throw at enemies walking toward you (the Warrior's stones, Rogue's knives, Duelist's spikes, Huntress's bow, Mage's staff), then fight. **Always pick thrown weapons back up.**
11. **Skip fights that aren't worth it.**
    * Animated statues before your gear outclasses their depth (HP 15+5·depth, `Statue.java`).
    * Chests when HP is low (1 in 5 is a mimic with infinite accuracy, `RegularLevel.java:410-421`).
    * The Great Crab head-on (it blocks everything once it sees you).
    * Piranha water.
    * Kill Thieves before they flee (`Thief.java:146-222`).
12. **Hazard reflexes.**
    * Burning → step into water (`Burning.java:98-99`).
    * Ooze → water.
    * Gas → step out of the cloud, away from its spread; never melee inside it.
    * Paralytic gas near enemies is deadly.
    * Skeletons explode on death (`Skeleton.java:81-126`).
    * Don't end a turn on a Dwarf King summoning circle (20-40 damage).
13. **Trap discipline.** Never step on a known trap; route around them. On *traps*-feeling floors, prefer known cells. Trigger a trap remotely by throwing an item at it. Search dead ends, not everywhere: each search costs 6 hunger (`Hero.java:2612-2620`). Passive door detection is ~0 by depth 20, so use Magic Mapping where the exit is missing.
14. **The STR-aware gear choice.** Never wear gear >1 STR short: each missing point divides accuracy/evasion by 1.5 (`Weapon.java:291-317`, `Armor.java:422`). Plan T3 at depth ~10, T4 at ~15, T5 at ~20 (1.3). Equip an unknown higher-tier armor/weapon when a Remove Curse or spare SoU is in hand.
15. **The Tengu protocol.** Melee from adjacent cells: his accuracy halves (`Tengu.java:106-112`). Record the flashed dart-trap cells after each jump and path only on cleared cells (visible information). In phase 2, leave bomb radius 2 before "1…", avoid the active shocker cells and the fire wave, and close distance.
16. **Shopping.** Buy the bags (Scroll Holder, Potion Bandolier), then an Ankh, Healing, Magic Mapping and food. Bless the Ankh with a full Waterskin (`Ankh.java:65-80`): a blessed Ankh means revival at 25% HP in place (`Hero.java:2184-2197`). Never hit the shopkeeper with area effects: it flees and the stock is destroyed (`Shopkeeper.java:93-186`).
17. **Passive-survival talents and subclasses.**
    * Warrior: Berserker with Deathless Fury, Iron Will, Strongman.
    * Mage: Warlock with Backup Barrier and Shield Battery.
    * Rogue: Assassin with Cached Rations and Protective Shadows.
    * Huntress: Sniper with Nature's Bounty and Point Blank.
    * Duelist: Champion with Aggressive Barrier and Twin Upgrades.
    * Cleric: Paladin with Holy Intuition, Bless and Cleanse.
    
    Section 3 has the detail.
18. **Quests.**
    * **Always do the Ghost** (an armor reward).
    * Do the Wandmaker (and never burn the Rot Heart).
    * Do the Blacksmith (single trip, 40 ore, spend favor cheapest-first).
    * **Skip the Imp's vault** for now: all gear stripped, a stealth puzzle, real death.
19. **Don't waste turns.** No grinding: monsters give 0 XP once you're above their max level (`Mob.java:988`), and respawns about every 50 turns just add fights (`Level.java:149`). Descend when the floor is done. Rush the Halls with Magic Mapping, **but kill the demon spawners**: fewer Rippers at Yog (`YogDzewa.java:129-149`) and Healing drops.
20. **Late-boss protocols.**
    * DM-300: fight off the trap tiles; on supercharge run to the pylon, avoiding water and trap tiles; dodge rockfall to the safe cell; Purity for the gas.
    * Dwarf King: kill minions; break the phase-2 barrier by killing all 12 wave minions; expect deferred damage in phase 3.
    * Yog: lure fists ≥5 tiles from Yog; dodge the telegraphed beams; Yog is untouchable while a fist lives.

---

## 10. Where the web and the source disagree

The source wins in every case.

| # | Web / older advice | v4.0.2 source | Where |
|---|---|---|---|
| 1 | The Imp's shop is on floor 21 | It is on **depth 20** (CityBossLevel), and only if the vault score > 2000 | `CityBossLevel.java:128,377`; `Imp.java:397-399` |
| 2 | Imp quest: kill 5 monks / 4 golems for tokens; reward a (cursed) upgraded ring | **The vault quest**: gear stripped, stealth AI, tokens from vault enemies, choose one vault item | `Imp.java:306-353`; `CL4:169-179` |
| 3 | Blacksmith: "mine 15 dark gold or kill a bat"; only useful with duplicates | v2 quest: pickaxe into the Mines, 40 ore = 2000 favor, +1000 for the boss; reforge, harden, upgrade, smith, cash out | `Blacksmith.java:443-470`; `WndBlacksmith.java:79-189` |
| 4 | Goo: "fight near water" | Goo **heals on water**; water only helps you against Ooze | `Goo.java:109-120`; `Ooze.java:93-94` |
| 5 | DM-300: repairs itself on traps; avalanche on every step | Gets a **barrier** walking onto trap tiles; rockfall and gas are **abilities**; two **pylon** supercharge phases | `DM300.java:323-343,409-470,497-526` |
| 6 | Dwarf King: summons from pedestals, more as HP drops; earthroot on a pedestal | Three phases: summoning circles, an invulnerable throne phase with a 300 barrier broken by 12 minion kills, then deferred damage | `DwarfKing.java:159-298,446-530` |
| 7 | Yog: "a larva spawns each time it takes damage" | Summons on a 10-15 turn cooldown: **rippers per living demon spawner**, larvae otherwise | `YogDzewa.java:129-149,297-345` |
| 8 | Yog: damage divided while fists live; beams at 700/300 HP | **Invulnerable** while any fist lives; beams = 1+(1000−HP)/400 (thresholds 600/200) | `YogDzewa.java:248,387-389` |
| 9 | Tengu: 120 HP, shurikens, traps in the room centre (vanilla wiki) | 200 HP, two phases, 25-HP brackets, dart traps flashed per jump, bomb/fire/shocker | `Tengu.java:90,139-200,445-560` |
| 10 | The Warrior's seal shield builds passively | A burst at ≤50% HP, then a 150-turn cooldown | `BrokenSeal.java:95-98,261-349`; `CL3:508` |
| 11 | Hold Fast slows decay 33/67/100% (CL3 text) | The code gives **50/75/100%** | `HoldFast.java:72-82` vs `CL3:433` |
| 12 | Durable Projectiles +33/+50% (v3.2 notes) | +50/+75%, reverted in v3.3 | `CL3:228` |
| 13 | Throw Stones of Aggression at bosses | v4: they no longer affect bosses directly | `CL4:359` |
| 14 | Monk: Flurry / Cleanse / Dash / Focus (dev blog) | Flurry 1, Focus 2, Dash 3, Dragon Kick 4, Meditate 5 | `M/actors/actors.properties:866` |
| 15 | "Curse infusion gives +2" | +1 + level/6 (so +2 only from +6) | `Weapon.java:368`, `Armor.java:449`, `Wand.java:353` |
| 16 | Dew Vial, Potion of Purification, Wand of Slowness/Reach/Avalanche/Flock/Amok, Greaves, Curare darts (old guides) | Vanilla-PD or removed items. Now: Waterskin, Purity, etc. | Item lists in `Generator.java` |
| 17 | "The giant crab can't be surprise-attacked" | It blocks only while it sees its target; asleep or unaware it can be | `GreatCrab.java:75-112` |
| 18 | The Ring of Haste (older numbers) | v4: ×1.15 per level (from 1.175) | `CL4:360`, `RingOfHaste.java` |
| 19 | "Put the seal aside before upgrading armor" | Moot: the seal carries one upgrade only | `M/actors/actors.properties:808` |
| 20 | The Huntress identifies "all items 1.75x faster" | v3.2+: thrown weapons only (3x, or on use) | talent text; steamdb v3.2.0 |
| 21 | Goo's drop is the "skeleton key" | Renamed **Worn Key** in v3.3 (the Skeleton Key is now an artifact) | `CL3:17` |

---

## 11. Sources used

**Official:**
* the in-tree changelogs `CL3` and `CL4` (v3.0-v4.0.2);
* [v0.4.0 blog](https://shatteredpixel.com/blog/shattered-pixel-dungeon-v040.html) (an old equipment rework, used for history only);
* [v0.8.0 blog](https://ShatteredPixel.com/blog/shattered-pixel-dungeon-v080.html) (the boss overhauls);
* [City quest preview](https://shatteredpixel.com/blog/coming-soon-to-shattered-a-new-city-quest.html);
* [Enchants & art preview](https://shatteredpixel.com/blog/coming-soon-to-shattered-new-enchants-and-pixel-art.html);
* [Duelist preview](https://shatteredpixel.com/blog/coming-soon-to-shattered-the-duelist.html);
* [GitHub releases](https://github.com/00-Evan/shattered-pixel-dungeon/releases);
* [v3.2.0 patch notes (SteamDB)](https://steamdb.info/patchnotes/19462333).

**Wiki** (Fandom; partly pre-v0.8, used mostly to find out-of-date claims):
* [Tengu](https://pixeldungeon.fandom.com/wiki/Tengu);
* [DM-300](https://pixeldungeon.fandom.com/wiki/DM-300);
* [King of Dwarves](https://pixeldungeon.fandom.com/wiki/King_of_Dwarves);
* [Yog-Dzewa](https://pixeldungeon.fandom.com/wiki/Yog-Dzewa);
* [SPD Weapons](https://pixeldungeon.fandom.com/wiki/Shattered_Pixel_Dungeon/Weapons).

**Reddit (r/PixelDungeon, r/ShatteredPD, via mirrors):**
* [What should I upgrade](https://lr.in.psf.lt/r/PixelDungeon/comments/1drsr8p/what_should_i_upgrade);
* [What am I doing wrong](https://lr.in.psf.lt/r/PixelDungeon/comments/1cg82vp/what_am_i_doing_wrong);
* [How do I beat the game](https://reddit.birdcat.cafe/r/ShatteredPD/comments/1jylli3/how_d_o_i_beat_the_game);
* [How much do I suck](https://lr.in.psf.lt/r/PixelDungeon/comments/1k6vc92/how_much_do_i_suck);
* [Wands tier list](https://redlib.ssps.io/r/ShatteredPD/comments/1i1j6k1/wands_tier_list);
* [Cleric](https://lr.psf.lt/r/ShatteredPD/comments/1hm3xyo/cleric);
* [How to defeat Goo](https://nyc1.lr.ggtyler.dev/r/PixelDungeon/comments/1gr9h1o/how_to_defeat_goo).

**Steam:**
* [General tips thread](https://steamcommunity.com/app/1769170/discussions/0/3976176932750559101/);
* [Tengu thread (with dev reply)](https://steamcommunity.com/app/1769170/discussions/0/5260782129621111243);
* [Resin and imbue (dev reply)](https://steamcommunity.com/app/1769170/discussions/0/3942398662634667076).

**Guides:**
* [Beginners' guide (gamepretty, from RiptheBlood's Steam guide)](https://gamepretty.com/shattered-pixel-dungeon-beginners-guide/);
* [Tips & tricks (kosgames)](https://kosgames.com/shattered-pixel-dungeon-tips-and-tricks-for-beginner-21095/);
* [Speedrun.com quick guide](https://www.speedrun.com/shpd/guides/h39dc) (old).

The source was checked file by file under `S/` and `M/`. Every ✔ claim cites the line it rests on.
