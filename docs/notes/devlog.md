# Overnight plan (user asleep, 2026-10-09): finish the whole game

User's standing orders: work autonomously through the night, ship test builds at milestones
(pkg.sh <ver> → /home/claude/out, SendUserFile proactive), test carefully as you go (host bot +
emulator via /home/claude/tap.py for touch, shot.sh for buttons), don't use the tracker artifact,
don't worry about the 256 KB store limit (sideload/GitHub is fine). Permission granted for everything.

## Tools
- Emulator touch: `DISPLAY=:99 python3 /home/claude/tap.py emery X Y [hold_ms]` (mouse = touch)
- Test kit defines: add `#define TEST_BUILD` + `#define DEBUG_TOOLS` after `#include "text_ids.h"` in pd.h
  (REMOVE before pkg.sh — pkg.sh adds TEST_BUILD itself for the kit)
- Host bot: /home/claude/hosttest (build.sh, bot N, W=1 bot N for wand/ring bags). Add new overlay .c
  symlinks + host_ovl.c mapping + pebble.h resource ids when adding overlays.
- Version label lives in src/ovl/ui/ui.c ("v0.7.1") + package.json version.

## Architecture notes
- App code ≤ 64 KB (uint16). Moving code to overlays frees app space; RAM is shared (heap = total - app),
  so a *resident* overlay costs the same RAM as app code. Use resident overlays for big new systems.
- Persist: 4 KB total per app. Level records must be evicted (farthest depth first) when over budget.

## Milestones
- [x] 0.7.1 user bugs: door fire, HP bar scale, ring/wand info, quickslots
- [x] 0.8 Sewers complete: Swarm, Albino, Ghost quest (FetidRat / DriedRose), Goo + SewerBossLevel,
      plants+seeds, dew vial, alchemy pot, garden; persist eviction; LAST_DEPTH 5+
- [x] 0.9 Prison 6-10: tiles, Skeleton, Thief/Bandit, Shaman, Wandmaker quest, shop (depth 6),
      Tengu + PrisonBossLevel, Tome of Mastery (Gladiator/Berserker), all special rooms, shops, wells, alchemy, altar
- [x] 0.10 Caves 11-15: Bat, Brute/Shielded, Spinner, Blacksmith quest, DM-300
- [x] 0.11 City 16-21: Elemental, Warlock, Monk/Senior, Golem, Imp quest, King + Undead, CityBossLevel, LastShopLevel (imp shop)
- [x] 0.12 Halls 22-26: Succubus, Eye, Scorpio/Acidic, Yog + fists + larvae, HallsBossLevel, LastLevel, Amulet, AmuletScene, SurfaceScene
- [ ] Special rooms (all painters), remaining traps, Mimic/Wraith/Statue/Piranha, bags, ankh, etc.
- [x] Other hero classes (Mage, Rogue, Huntress) with subclasses and class armours (0.13)

## Progress log
(append one line per completed step)
- 0.7.1 shipped (door fire, HP bar, ring/wand info, quickslots)
- 0.8 shipped. Infrastructure added: floor overlay (saves/floors/new game), trap overlay (traps, plants, landing),
  gen overlay (Generator), mobs overlay (resident; MobHooks g_mh in game_int.h), mob table tools/mobs.py
  (stats+art+texts generated; MOB_DEFS + CharArt loaded from resources), item table item_defs.inc ->
  item_defs.bin via tools/dump_defs.c, item sprites as raw 4-bit LRU cache (items_raw.bin), save v9
  (hero extras, mob aux/aux2/vertigo/HT, keys, quests, dew, plants, veg 2-bit state), persist budget eviction.
- Host bot: hosttest/gen.sh links all overlays automatically; GOD=1 bot goes deep; W=1 wands/rings bags.
- Debug (DEBUG_TOOLS): long-press Down on the entrance = next floor; elsewhere reveal map then spawn each kind.
- 0.9: band overlays (src/ovl/sewers, prison; caves/city/halls next) chain onto the common mobs overlay via g_mh_base;
  shared creature code in src/c/mob_*.inc. Level overlay split: level (shape) + content (items/mobs). hero overlay
  (pickup/open/zap/throw/item menu/trade/tome/debug). Overlay globals must not clash with app names (build_ovl checks).
  Keep table replaced by -Wl,-u flags (keep_syms.txt). Save v10 (only v10 loads: special rooms changed floors).
  Memory: snd overlay preloaded at init; band overlay dropped while bag/menu/info open (mreg_drop/mreg_ensure).
- Test kit (TEST_BUILD) has DEBUG_JUMP: long-press Down on the up stairs = next floor; elsewhere 1st press = map + 2000 gold
  + a few items, later presses teleport next to the next place of interest; on the down stairs = spawn a local creature.
- 0.10: caves band overlay (Bat, Brute, Spinner, DM-300 + CavesBossLevel, Blacksmith quest/reforge), caves tiles.
- 0.11: city band overlay (Elemental, Warlock, Monk/Senior, Golem, Succubus inc, Imp quest, King of Dwarves + undead,
  CityBossLevel arena), LastShopLevel on 21 with the imp shop after the quest. Status pane shows iron keys for the floor.
  Host tests: imptest.c (quest -> ring -> d21 imp shop), bot KLOW=1/KT=1 for the King fight.
- 0.12: halls band overlay (Eye deathgaze, Scorpio/Acidic, Succubus inc, Yog + Rotting/Burning fist + Larva), HallsBossLevel
  (sealed room opens on the first step; stairs back when Yog drops the key), LastLevel with the Amulet; AmuletScene
  (pickup / End the game) and SurfaceScene (ascending from depth 1 with the Amulet). LAST_DEPTH 26.
  Memory: the level generator's scratch now lives in game.c's s_queue (overlay needs 18.9K, was 20.6K); a continued
  game releases the save buffer while the floor is generated; game_erase deletes KEY_LEN directly.
  Halls floor recoloured maroon (convert_regions). Death lines: "Killed by the <boss>", "an <vowel...>".
  Test kit: holding the Amulet, long-press Down = straight to the SurfaceScene.
- Next: ArmorKit + WarriorArmor, torch, weightstone, Lloyd's beacon, ankh, bags; Rankings; other classes.
- 0.13: Mage (Wand of Magic Missile + disenchant, wands as melee, faster recharge, food recharges wands), Rogue (Ring of
  Shadows+1, light-armour dodge, keener search, slower hunger, rings known on wearing), Huntress (15 HP, boomerang,
  missile proficiency, +1 dew, senses creatures 2 steps away; unlocked by killing DM-300, persist key 201).
  Subclasses: Warlock, Battlemage, Assassin, Freerunner, Sniper (mark in Hero.spare[0..1]), Warden. Class armours:
  armor kit -> ClassArmor (dur = DR, hits = STR); heroic leap, smoke bomb, molten earth, spectral blades.
  Items: torch, weightstone (IF_FAST / IF_ACCURATE), Lloyd's beacon (ARRIVE_CELL + cell), ankh (FO_RESURRECT).
  Fixed: persist keys 102/103 (zoom, sound) overlapped floor 20/21 records; app deleted key 100 (floor 20) at launch.
  App texts moved to text.bin (TXC(id) for death causes). Level generator zeroes its scratch (s_queue) per floor.
- Next: bags (seed pouch, scroll holder, wand holster), Rankings, polish.
- 0.13.1: FIX "empty square floor" (user report, Gabbro): the blitter's row table was malloc'd at the first frame, after
  the title art, and split the heap in two (largest block 17.4K < level overlay 19K). Now blit_init() at start-up.
  The fallback floor's message now says "out of memory (largest/needed)". Bags: seed pouch / scroll holder / wand
  holster (G.pack, slots 32..63, save v11; tabs in the backpack and in selectors). item_price moved to the hero overlay.
- 0.13.2: user report (real Emery): backpack/menu showed nothing, audio choppy, grey top bar. Windows split into "ui"
  (title, select, confirm, endings) and "uiw" (in-game windows, 7.4K, was 10.7K together); if uiw still can't load,
  the floor's tiles are dropped and it retries, else an "out of memory" note is drawn. Save & Main Menu frees the
  window first (the save's overlay needs the room). Status pane drawn without its grey bar. Sound lead 140 -> 200 ms.

## 0.14
- Rankings (Rankings.java / RankingsScene): persist keys 203-204 hold the top six games (score, class, win, result line),
  plus games played / won and the latest entry. Score = (gold collected + level x deepest floor x 100) x 2 on a win;
  gold collected is Statistics.goldCollected, kept in Hero.spare[2..5] (no save change). Recorded on death (unless an
  Ankh is offered; "No, I give up" records it), on "Let's call it a day" (then the rankings, as PD) and on the surface.
  Floor records' budget 2300 -> 2000 B to leave the room.
- Title: PD's dashboard Play and Rankings (the podium icon); Up/Down picks Play / Rankings / the sound corner.
- "newgame" overlay split out of "floor" (5.6K) so saves/floors need less heap.

## 0.15 (memory on the real watch; user: Badges / About / Challenges cut from scope)
- User's watch at 0.14: "RAM free 7.6 KB, Save 0 B FAIL" with Diagnostics open (the 7.4 KB window overlay + floor
  overlay 5.7 KB + 2.4 KB save buffer did not fit). Fixes:
  - saves pack into the path finder's scratch (s_dist + s_queue now one 4 KB block, idle between turns): no buffer to find
  - in-game windows split into three overlays: uiw (menu, diag, info, options, dead) 2.9K, bag (bag, item) 2.8K,
    sheet (WndHero, Catalogus, Journal) 4.2K
  - ovl_load low-memory fallback: drop monster sheets + item sprites, then the floor's tiles (redraw reloads them)
  - item sprite cache 28 -> 16
  - front screens' input moved into the ui overlay (app code -880 B)
- Saves on any exit (deinit) while a living hero is in the game: no rewinding to the last 20-turn autosave.
- Sound: the speaker buffer is no longer inflated when it is small (that starved it); lead = min(7/8 buffer, 200 ms);
  the speaker is also fed every frame and around saves.
- Catalogus (Potions / Scrolls tabs, "?" for unknown kinds, tap a known one for its info) and Journal (wells, altar,
  alchemy pot, garden, statue, the four NPCs; save v12 G.journal) from WndHero's buttons and the game menu.
- WndHero's "Gold Collected" now is Statistics.goldCollected.

## 0.16
- Floors keep what is left on them: records are mask-packed (a byte per 8 marking the non-zero ones; ~60%), and when
  the floors outgrow LEVEL_BUDGET (now 2500) the farthest ones become "lite" (no map memory, no ordinary monsters, trampled
  grass regrows; items, plants, doors, shops, NPCs, bosses and statues stay). Only when even that doesn't fit is the
  farthest floor forgotten, with a log line if anything lay there. Failing writes (storage full) make far floors give way
  and retry. The save itself is mask-packed too (KEY_LEN negative). Host test hosttest/rectest.c: 23 floors each with
  ~10-30 heaps: the nearest 15 kept everything.
- Emery: the status pane's grey bar is back (Gabbro keeps the bar-less one).
- Sound: the speaker is also fed right after the map is drawn each frame.

## 0.17 (user: test kit with everything; resume button; find stragglers; badges/about/challenges out of scope)
- ResumeButton, DangerIndicator (tap: next enemy, camera on it), HealthIndicator over the attack target; danger / loot /
  resume tags stack top-right under the status pane (PD StatusPane.layoutTags). Boss-slain banner. WndStory chapters,
  IntroScene (once, persist 205), WndClass perks (tap the chosen class), WndInfoPlant, region tile names/descriptions,
  wells / altar / garden / web / confusion descriptions, chest/tomb/mimic/crystal texts, statue's weapon, thief's loot,
  curse personification, mage wand-melee and beacon info. Dew vial volume, keyring keys with depths.
- Gameplay: Bones (persist 206: a fallen hero's item, cursed, in remains on that depth); items fall down chasms (save v13);
  Amulet: every creature beckoned on arrival, respawns twice as fast and come for you; night mode (before 7 am) likewise;
  signs on all 21 floors (deeper ones burn); Rotberry seed roar; Honeypot "Shatter"; missiles held as melee weapons
  (confirm, effects, one used per blow); stairs cost STARVING/10 hunger; scroll-cancel confirmation; everything
  identified at the end (and the backpack opens after death); pain snapping paralysis, fury, vertigo, blindness, charm,
  starvation messages; full level-up text; death menu has Rankings; long-press backpack = Catalogus, long-press
  quickslot = choose its item; inspecting yourself opens WndHero; a tap outside a two-choice window closes it.
- Memory: hero overlay split into hero (4.1K) and hero2 (6.1K) (hero_impl.h, HERO_PART); buffs' turn moved from the
  app into the mobs overlay (g_mh.buffs); strchr / memcmp no longer pulled into the app.
- Test kit: level 30, STR 25, every class unlocked, first floor laid out room by room (MAX_HEAPS 128 in TEST_BUILD) with
  plate armour of every glyph, war hammers of every enchantment, every wand, ring, scroll, potion, seed, food, weapon,
  armour, missile and misc item, all identified, gear at +3.
- Deliberately not done: PD's per-hit combat log lines (2-line log on the watch), shop price on inspect (WndTradeItem
  canBuy=false), "I'll decide later" button (Back does it).

## 0.18 (user: bigger UI for the tiny watch; ranged attack tag; enemy-cycling reticle)
- Backpack: almost full screen, 5x5 grid of 35x34 cells with 2x icons (weapon, armour, rings, gold on top; 19 slots;
  Exit cell with PD's exit icon); all 5 tabs fit under it. Layout and input moved into the bag overlay (CMD protocol).
- Item window: big action buttons two to a row (an odd last one spans the row), 2x icon beside the name.
- Game menu: tabbed pages of big picture buttons (menu_icons.png): Game (Return, Save & Main Menu), Catalogue & Journal,
  Settings (Music, Sound, Diagnostics; a red X when switched off). After death: New Game, Rankings, Main Menu.
- Options windows (chasm, potion warnings, rings, quickslots, quests): big stacked buttons, side by side when the text is long.
- "Catalogus" -> "Catalogue".
- Aiming a thrown weapon or wand: the target shows as a red attack tag (tap fires); a reticle tag above it cycles the
  visible enemies and brings each into view.
- Fix: starting a game with sound on could fail (the level overlay found no 19 KB block): the sound engine is
  unloaded while the floor is built (snd_hold).

## 0.19 (user: readability; combat tags by the buttons; bugs in 0.18)
- Black body under every window's title (WELL in ui_int.h): item, info, options, diagnostics, menu, hero sheet,
  catalogue, journal, hero-class and erase windows. Unknown catalogue entries get a grey square behind their sprite.
- Combat mode (an enemy in sight, or aiming): three red tags beside the buttons that work them. Up: zoom (cycles,
  magnifier and pips); Select: attack, the tag shows the weapon (or what is aimed); Down: next target, shown only with
  more than one enemy in range (in sight for melee, in the line of fire for throws and wands). The yellow target box
  marks the melee target too. PD's DangerIndicator is gone (the combat tags replace it); loot and resume tags move to
  the left edge during combat. Out of combat, Up / Down zoom as before. game_enemy -> game_target(after, line, &n).
- Fixes: empty journal says "No special places found yet."; menu icons get a black outline (the red book vanished on
  the red buttons); muted Music / Sound: the icon is dimmed under a new outlined red X.

## 0.20 (user: piranhas invisible; hero window blanks the world; music pauses and saves fail: memory pass)
- Measured (emulator, Diagnostics after a new game): 0.19 had 8.5 KB free but the save's 7.9 KB overlay found no
  block (largest 4.6-6 KB): the heap was fragmented by overlays coming and going between pictures.
- Overlay arena (ovl.c): one 8 KB block taken at start-up; overlays that come and go (actions, windows, the save) stack
  in it, resident ones (creatures, region, sound) use ovl_keep() on the heap; what doesn't fit falls back to the heap.
  Windows no longer drop the region's creatures (mreg_spare).
- Save overlay (src/ovl/save = floor.c with SAVE_ONLY): save, save size and peek in 2.1 KB instead of 7.9 KB.
- Sprites mirrored by the blitter (g_blit_flip): hero and monster sheets lose their second row (hero 3.8 -> 2.0 KB).
- Tiles are raw 4-bit cells (tiles_raw.bin) in the item sprite cache: one 48-slot block (6.9 KB, smaller on a tight
  heap) instead of 8.4 KB of tile strips plus 16 small item blocks.
- Hero window's Buffs tab draws the small icons at 2x: the 7.5 KB large-icon sheet is gone (it dropped the tiles).
- icons, buffs and menu icons at 4 bpp; Emery draws without the 912 B frame row table.
- Result: 8.6 KB free on the heap plus the 8 KB arena; the save works with a window open.
- Piranha: its idle frames are a 30% black shadow in PD; drawn solid grey (they were cut as transparent).
- Diagnostics shows the largest free block and the arena's free space.

## 0.21 (user: loot under fire; combat tags by the buttons; bigger menu button)
- The loot tag does PD's LootIndicator: hero.handle(hero.pos), a pick-up even with an enemy beside (it attacked).
- Combat tags all 40x36: zoom and next target mirror each other top and bottom, attack at the screen's centre.
- Emery: the menu button is drawn bigger (27x30) over the pane's own; depth and key counters unchanged.

## 0.22
- Combat with one target in range: Down zooms out as outside combat (it cycled to the same enemy with no tag shown).

## 0.23 (user: play by buttons alone - a target cycle)
- Up always zooms (cycles 3 levels); Down always picks the next target; Select acts on it as a tap on its cell would
  (game_tap_cell: attack, walk, pick up, open, talk, take the stairs). Taps on the map work as before.
- Targets (mobs overlay, hook g_mh.targets; game_targets in game.c): enemies in sight, then one step away from them
  (the free neighbour farthest from them, a doorway on a tie), then all doors in sight (unexplored first: a side never
  seen), things lying about, stairs / wells / alchemy pots / people; with no door in sight, the farthest open spot.
  Nearest first in each group; at most 16. The choice is kept while it's in the list (an enemy by its uid), else the
  first: exploring is Select, Select, Select.
- The middle tag shows what Select does: red with the weapon (attack), blue with the thing (pick up), pale with the
  walk arrow (go). The yellow box marks the target on the map. Aiming a throw or zap: enemies in the line of fire.
- Loot and resume tags always sit at the left edge now; PD's DangerIndicator stays retired.

## 0.24
- The button tags are slim indicators now (22x24, pictures at 1x), no longer finger targets first.
- The farthest open spot in view is always the last target, preferring one at the edge of the unexplored (a cell beside
  one never seen): after the doors, Down reaches the end of the long hall.

## 0.25 (user: the camera zaps; exploring by Select alone loops)
- Camera swoop: a new view offset is reached in 400 ms along a smoothstep (cam_glide); a drag takes over at once.
- Targets measure walks (the pathfinder through known cells), list only what can be reached, and the far spots are
  local maxima of the walk (hall ends, corners), not the one farthest cell. Order: enemies, a step away, unexplored
  doors, things, spots at the edge of the unexplored (plus the nearest such remembered spot out of sight), stairs /
  wells / pots / people, explored doors, other far spots. Locked doors only with a key. Duplicates are merged.
- Select with nothing coming of it moves to the next target; with no target at all it searches (a closed room hides a
  door), or takes the stairs / picks up underfoot.
- The list is rebuilt only when time passes or the hero moves. game_interact / game_select_target / game_attack_target
  left the app (the bot keeps its own copy).

## 0.26 (user: local maxima trap you in rooms; directions instead; long press to go back)
- After people / wells / pots, the targets are the farthest walkable spot in view in each of eight directions (2:1
  slopes split them): those beside the unexplored first, then the nearest remembered spot beside the unexplored (out of
  sight), then the other directions. Stairs moved behind them (down, then explored doors, then up last) so exploring
  by Select never takes the stairs unasked.
- When the chosen target is reached, the default is the best one not among the last six places Select went (no more
  back and forth). A host test pressing only Select now roams the whole floor.
- Long press Down: the target before. The test kit's debug jump moved from long press Down to a tap on the depth counter.
- Chasm question: tapping "No" works in the emulator (tested via the debug tap); not reproduced.

## 0.27
- A newly seen enemy takes the target (the walk was already interrupted, PD's checkVisibleMobs; but the kept target
  sent the next Select walking on into it): Select now attacks; Down / long Down to walk on or step away.

## 0.28 (user: a full backpack grinds the watch; move items; avalanche shows nothing)
- Windows over the game no longer redraw on the animation timer: only on input (the frame was redrawn several times
  a second, and with a full bag over a floor of items the sprite cache churned: no taps, the music starved). The
  backpack also skips drawing the map under it.
- Backpack: press and hold an item (or long Select) to pick it up (green box), then tap / Select another cell to put
  it there, swapping; Back cancels. Backpack and bag slots only (gear isn't moved this way).
- Wand of Avalanche: Camera.shake and rubble squares over the area (FX_QUAKE, the searched-cell effect at a radius).
- game_auto_target rewritten on game_target (app room).

## 0.29 (user: the Tome isn't used up; armour powers need animation; can't move items)
- Tome of Mastery and Weightstone: their window's two choices lived in the hero overlay's memory, gone when the choice
  came back (the overlay is reloaded per action): nothing happened. The choices are worked out again when one is made.
- Backpack moving on the watch: holding a finger on an item for half a second picks it up right away (green box, no
  need to let go); letting go on another cell puts it there (a drag), or let go and tap the cell. Long Select as before.
- Heroic leap: an arc (300 ms) and a camera shake with rubble around the landing; Molten earth: a rumble (2 cells).
- App room: the hero sheet's input moved into its overlay; level_build / level_fallback into the floor overlay,
  draw_arcs into the ui overlay.

## 0.30 (user: the alchemy pot is bugged; test the game more intelligently)
- Alchemy pot: seeds of different kinds now share the pot (they used to spill beside it), so any 3 seeds brew. Only
  seeds go in the pot (Level.drop moves anything else off it). A lone seed waits in the pot, as in PD.
- With a seed pouch the pot's seed picker opens on the pouch (GameScene.selectItem: WndBag.seedPouch).
- Host harness: overlays are .so files dlopen'ed per load (statics reset like on the watch). Six parallel audits
  played scripted scenarios against the real code and compared with the Java: ~75 differences found and fixed,
  e.g. NPCs vanishing once you leave their floor, Scroll of Upgrade wiping wand charges, a held wand acting as an
  enchanted weapon, enchanting class armour setting its armour to 1, frost never breaking on damage, Ankh death
  wiping saved floors / going up a floor on boss levels, thrown things stopping short of doors and high grass,
  items landing on plants not setting them off, immunities ignored (lullaby, terror, psionic, enchantments, wands),
  buffs not stacking, the golden bee, boomerang free throws, equip times, transmuted gear staying broken,
  weak-floor falls missing the pit room, monsters trampling grass, Well of Awareness, lightning trap recharge,
  reforge losing enchantments, holster wands not reforgeable, curse personification, tomb wraiths, mimics and
  fire/frost, shopkeeper fleeing from buffs, sungrass healing lost on save, drops lost on a full floor.
  Audit tests live in /home/claude/audit/<area>/.
- App room: the quest window's text shares the info text buffer (420 B of bss freed).
- Test kit: an alchemy pot in the seeds room, and the hero starts with a seed pouch and 3 kinds of seeds.
- Not done: gas/fire clouds still aren't saved; boomerang can't be enchanted; sungrass for monsters.

## 0.31 (user: "Not enough memory" trying to eat)
- 0.30's fixes grew the "use" overlay to 8240 B and the floor overlay to 8432 B, past the 8192 B overlay arena: each
  then needed 8 KB of free heap, which the watch doesn't have, so eating / drinking / reading (and possibly a floor
  change) failed. The item choices (identify, upgrade, enchant, ring swap, short sword) moved to their own "pick"
  overlay (use.c with PICK_ONLY); the rankings and erasing moved to the save overlay, and the floor overlay no longer
  carries the save code. Now use 7004 B, floor 6648 B, pick 1488 B, save 3112 B.
- tools/build_ovl.py now refuses to build an overlay over 8192 B (level excepted) and warns past 7900 B.

## 0.32 (user: fix all known bugs; gas must not vanish on quit/reload)
- Gas and fire clouds are saved per floor (floor.c gas_store / gas_load, keys 124..175, up to 2 keys a floor), with
  the save and when leaving a floor; a regenerated or forgotten floor drops its clouds too.
- Round 2 of the audit (world / magic / people agents, ~35 fixes): Sungrass for monsters, fire/frost on every pile of a
  cell, transmutation like PD, cooking through Ballistica and on unseen pots, fadeleaf, summoning trap on boss floors,
  honeypot bee, sniper mark, throwing at your own cell, shadows hunger, meat burning message; boomerang enchanting and
  thrown enchantments, Ring of Detection search, Herbalism, Affection, Reach timing, wand-identified text, weakness
  on a held wand, ooze's last tick; shopkeeper not "defeated", full-pack purchases, reforging equipped items, bags
  keeping their contents when dropped/thrown and lost when sold, monk tokens on depth 14, NPCs kicking any heap,
  quest choices kept in resident vars, Ankh clearing buffs, assassin minimum, albino bleeding, a target-cycle overread.
- Left: only 4 items can fall down chasms at once (save format).
- Diagnostics after a new game: 5.6 KB free, largest block 3.6 KB.

## 0.33 (round 3: monsters, bosses, level generation, combat, knowledge/texts, transitions)
- ~80 differences from PD fixed by six audit agents (reports in /home/claude/audit/*3/): creatures see with their own
  eyes (a `sees` hook in the mobs overlay: range 8, 4 for eyes/succubi/scorpios; mind vision / huntress / blindness no
  longer leak), statue weapon enchantments, piranhas, thief gold and stolen bags, fliers' paths, roots on fliers;
  boss arenas can't be walked out of, DM-300's rocks, the Tengu anteroom, bosses' wander targets, Goo's jump, King's
  undead; floors generated like PD (room sizes, random room order, patches, graveyards, mimics, item odds, bones on
  boss floors); damage rolls (NormalIntRange), war hammer, armour equip time by speed, invisibility on attack; many log
  texts, death causes, item/monster info, journal timing, selling score and time; allies kept on their floor,
  lite floors keep quest creatures, quest floors forgotten last, fall details.
- Save format v14: floors are generated differently, so saves from 0.32 and older don't load (new game).
- Kept PD's quirk: depth 21 counts but never drops its strength potion / upgrade scrolls.
- Removed PD-less log lines "You equip / take off / drop the X".

## 0.34 (round 4: interaction, save safety, limits, leftovers, deep playthrough bot)
- Interaction: the target list only offers reachable places / chests and exits you have keys for; a broken-off walk is
  cleared; throw/zap target memory; tapping your own cell works like PD (stairs, pick up, sign).
- Save safety: saves alternate between two key banks and KEY_LEN (written last, with the bank bit) switches them, so a
  save cut short leaves the old one whole; revisited floors rebuild sight; Ankh doesn't reveal the floor; lite floors
  keep the altar; SAVE_BUF 2560; oversized saves refused.
- Limits: thrown/dropped items and bag contents never vanish on a full floor, thieves can't steal into a full floor,
  honeypots, chasm stacks, creature ids, gold capped at 65535; gas/fire evolve in the path finder's scratch (no 3 KB
  malloc per turn).
- Leftovers: succubus blinks, mimic push, cornered thieves, amok paths, King AI, Goo's jump, ooze on allies, Tengu's
  own sight, story chapters after a fall, garden shadowmeld sight, mirror image stats, Ghost journal, missile :STR.
- Kinder than PD: what a creature carried when it vanishes without dying (Ankh floor reset, a fall) lies somewhere on
  its floor (orphans() in floor.c), so a stolen Amulet can't be lost for good. The last shop doesn't use up a strength
  potion / upgrade / enchantment scroll it never drops.
- Camera: one clock per frame (no torn strips while it glides); the map draw marks heap cells in a bitmap first.
- Test kit: depth 1 gear, depth 2 consumables, no creatures (nor respawns) until depth 3 (src/c/testkit.h).
- deepbot (/home/claude/audit/deepbot4/deepbot.c, run from /home/claude/audit/v34d): a host player that plays whole
  games without god mode and checks invariants; with HS=6 about a third of games win; 0 violations on 0.34.

## 0.35 (Gabbro round layout; performance pass 1)
- Gabbro (agent, /home/claude/work/gabbro): round game HUD (status along the top arc, action tags at the rim by the
  buttons, toolbar along the bottom arc, log in the middle), rounded windows with screen text flow and page scrolling,
  a round backpack (gear row, 7/7/5 slots, tabs and Exit at the bottom), hero sheet and catalogue fitted to the curve.
  Round window shapes live in the overlays (ui_tabs.h); keep_syms_gabbro.txt keeps the app symbols only Gabbro's
  overlays call (wscript + build_ovl.py). Emery's code is unchanged by it.
- Performance (agent, /home/claude/work/perf, tools in tools/prof there): the audio refill task (priority 1) only runs
  when the app idles, so frames back to back starved the 128 ms speaker buffer. Now: a rest of >= 8 ms after every
  frame, one redraw per frame while panning, frames timed to when a sprite actually changes (idle redraws 2.5/s ->
  0.75/s), outlined text drawn once with a marker colour (Emery), panel stretching precomputed, bitmap info cached,
  sprite cache remembers slots, one camera per frame; -flto (keeps __pbl_app_info). Frame 24 -> ~14 ms on the busy
  test floor; longest audio refill wait while dragging 177 -> 67 ms. Emery 65205 -> 64406 B, Gabbro 65397 -> 63538 B.

## 0.36
- Floor change: the build loads five overlays in a row (~115 ms) and starved the speaker (180 ms gap); ovl_load now
  sleeps 2 ms first when the app has worked 30 ms without a break (g_idle_at, set after each frame): gap 89 ms.
- Gabbro: options text flows on the round screen, Diagnostics lines centred.
- Tried and reverted (no gain): a newest-first sprite cache, a straight-copy path in blit_nine4. Next big win on hold:
  skipping an unchanged HUD (~6 ms of a ~13 ms idle frame), pending real-watch feedback.

## 0.37 (music rhythm; target cycle on screen)
- Music (agent, /home/claude/work/music, analysis in listen/): PD's tracks sit on a strict 16th grid (theme 77.5 bpm,
  loop 460 16ths; game 90 bpm, loop 432 16ths). tools/audio/requant.py (reads *_raw.json from transcribe.py) snaps
  notes to the grid where the recording has an attack, keeps quieter inner notes (0.8), finds drums per 16th from the
  percussive part (theme kick 1/3 + snare 2/4; game knock 1/3 + hats), per-drum loudness, overtone/ring corrections;
  snd.c drum decays (hat ~22 ms, snare/kick ~80 ms) and a steadier ~80 Hz kick. Notes within 20 ms of the grid:
  theme 62% -> 93%, game 42% -> 96%.
- Targets (agent, /home/claude/work/targets, button-only player targets_host/btnbot.c): the list only offers cells on
  screen (g_view: the visible half-extents per zoom, set by tg_refresh); an off-screen goal is offered as the farthest
  on-screen step on the walk to it; items underfoot and remembered items are targets; piranhas only when adjacent;
  rooted: Select waits. The list builder moved to its own on-demand overlay `tgt` (mobs overlay -1.7 KB).

## 0.38
- Target cycle: a cell counts as on screen when half of it shows (and clear of Emery's right-hand tags): presses per
  floor zoom 2 128 -> 90 (tiny hops 56% -> 14%), zoom 1 77 -> 66, zoom 0 58; only the nearest remembered item is
  offered. Down never moves the camera (checked on the emulator).

## 0.39
- Game music: the lead (held, wavering tone from 10 s) found by requant.py and played on its own sustained voice at full volume, the rest at 0.4.
- Target cycle: ordered by the travel direction (g_from): ahead first, clockwise within a group; targets more than 90 degrees off the heading come after the front-facing groups (enemies / step-away stay first). Default behind the hero 26% -> 8%.

## 0.40
- Game music rebuilt from the Songsterr score (tools/audio/game_tab.txt -> from_tab.py -> game.json -> build_audio.py); the B-C-B figure is a gliding whole-note B (glide marker, drum code 133).
- Accompaniment (chords, bass, drums) raised ~1.3-1.5x; music gain 1280 -> 1700 with a soft 1:3 knee above ~0.7 in the mixer instead of hard clipping.
- Sound effects normalised by loudness (loudest 50 ms ~ music level, SFX_RMS 0.27) instead of by peak: item pickup was about 2x the music, now matched.

## 0.41
- Volume: the Sound button (game menu) and the title speaker cycle Loud / Medium / Quiet / Off (g_sound bits 2-3, SNDC_VOL: gains >> 0/1/2). Music on/off stays separate.
- Target list entry 0 = the deep bot's choice of where to go (tgt.c first()): fight, pick up, quest people, items/wells/locked doors, nearest frontier (sticky: g_goal), a trap that is the only way on, stairs, water frontiers with piranhas, search spots (g_srchd: every searched cell this floor visit). Dropped items (g_drop ring of 4) are not offered back; pickups stop at 2 free slots. Entries 1..n: group, clockwise from the heading, nearer (0.39 "behind last" rule removed). Highlight resets to entry 0 after each step; Select on the hero's own cell searches.
- Bot PRESS=1 mode (audit/deepbot4/deepbot.c): 20 games HS=6: 17.7 floors/game, ~190 presses/floor, 0 stuck, 0 off-screen.

## 0.42
- Buttons only: Back (game) walks a highlight ring: 6 toolbar buttons -> menu -> portrait -> off (long Back is reserved by the OS); Select presses, long Select = long touch; the middle tag shows a half-size copy (blit_shrink). Back no longer saves & exits (menu has it). hero_buffs rewritten as a table (-328 B).
- Backpack (ovl/bag): row mode (wide box; Up/Down rows, Select enters), then Up/Down along the row; Back row->closes; s_sel = 64 + cell for row mode.
- Target list: manual override (Select on entry != 0) sets a direction (g_ovr) that entry 0 follows (cone, route-aware) until done / new floor; one-step moves never override; doorway: Select waits a turn for an approaching enemy 2 away or once after stepping back. Dew only when hurt or vial room; tall grass sweep before the stairs (skipped until XP changes if overridden); quests: ghost, rotberry, phantom fish (search pools), imp tokens, blacksmith gold veins (TK_VEIN walk / TK_MINE mine, pickaxe tag). Off-screen stand-in never the stairs. Huntress-sensed enemies behind long detours skipped.
- Title: thick yellow hero highlight, taller New Game (38) and Continue/Erase (40).
- Bot: PRESS=1 17.3 floors/game, 0 stuck; RT=40 save round trips 0 violations. Open: corpse dust, override turn-back ~9%.

## 0.43
- Music (game track only; theme byte-identical): brighter/louder bass for the small speaker, rhythm chords decay (~half a beat, measured from the original), quieter ending (closing lead chord 35%), lead vibrato ±0.6 st at ~4 Hz delayed 0.15 s, glides up/down of any size (slide into the 63-65-67-70 phrase, added falling 65->61 note after the staccato phrase). snd overlay 3464 B.
- Reticle: far zoom draws it at the medium size (16 px); the target's health bar no longer vanishes at far zoom.
- Target list: ~40% cheaper per build (distances once per build, running unknown counts); not rebuilt on each step of a walk. Grass first (after fights/items) while the dew vial isn't full; hurt without a vial: grass within 3. One-step moves without enemies and map taps count as the player's choice; sticky goal yields to a frontier < half as far; hallway mouths rank with unexplored doors. Quest people always block paths; the Amulet is always offered. With the Amulet: explore when the way up isn't known.
- App icon: lid wider and taller than the base.
- Bot RT=40 PRESS=1: 10 games, 1 won (first middle-button-only win), 0 stuck, 0 violations.

## 0.44
- Music: vibrato only on held notes (>= 720 ticks); the added note after the staccato phrase is plain (no 4-semitone dive).

## 0.45
- Music: the extra note after the staccato phrase removed entirely.

## 0.46
- Target list entry 0: on-screen first. Frontier spots and tall grass on screen form one group (ahead of the heading first, then nearest), before any off-screen plan. Grass with an item on it skipped; enemies no longer suppress grass. Piranha water exploring = rerun of the plan ignoring piranhas.

## Open issues (0.46)
- Middle button loop: on the first floor of an owner test game, entry 0 kept selecting a wall tile (nothing happened), then walked away and back trying to walk into a wall, repeatedly. Not reproduced yet; need the floor/screenshot.
  Lead (stage-1 lab, Oct 11): the game's own path finding can flip-flop between two cells forever when a creature keeps
  stepping in and out of the only route (get_step rebuilds the distance map each step with chars as walls). The bot now
  stops a walk that goes A-B-A-B; the watch has no such guard. Check this when reproducing the wall loop.
- FIXED for 0.47 (was MAJOR): Goo's pump-up warning. PD 1.9.1 GooSprite.pumpUp pours black GooParticles from its centre; the owner asked for Shattered's warning too (a kindness on a watch): the sewers overlay stores a 5x5 bitmask g_goo_warn of the cells its jump reaches (ballistica, at pump time); update_proc draws draw_flames(seed<0: goo green 0xC8) on each one in view. Not done: the 20 fps pump wobble (no room: Emery has 25 B left). A pumped Goo in a loaded save shows no warning until it pumps again (mask not saved).

## Suggester 2.0 (owner's design, Oct 12 00:00) — after stage 1
- The middle button shows plan A = the planner's best OBJECTIVE; up/down cycles plan B, C, ... in the planner's own score
  order (objectives, not tiles: one per frontier cluster / item / locked door / well / NPC / stairs), each with its icon.
- Selecting plan B promotes it (a large score bonus, "sticky") until it is met or impossible (seen, taken, blocked);
  walking towards B by hand promotes it implicitly (replaces the heading/override heuristic g_ovr).
- Danger (an enemy appears) still interrupts as plan A; the chosen objective resumes after.
- Source of the ranking: the stage-1 exploration planner (bot3 plan_next), ported compactly into the tgt overlay.

## 0.47 (night of Oct 11-12)
- Suggester 2.0: plan A = the planner's best objective; Down cycles plan B, C... (max 8): enemies, a step away, items,
  people/wells/locked doors/veins, unexplored regions (one per region), the way down, the way up. Choosing one makes that
  objective plan A until met. Back (into the toolbar) puts the cycle back on plan A.
- Mind reading: walking 2+ steps nearer one of the listed objectives (touch screen, any way) while not nearer plan A
  makes it plan A (tgt mind(); anchor list kept in G.tgm, not saved). TGT_MIND (2) is the step count.
- Engine fixes from the Java comparison: Fire.seed only lights unlit cells; Burning only seeds from its act;
  gases hurt on the grid before spreading; actor ties no longer favour monsters (uid hash).
- Goo pump warning (Shattered kindness). Stale heading from the previous floor/game no longer biases plan A.
- App image: the search bitmap moved into the game state, the FOV shadow lists behind the path finder's block (first tried
  inside its scratch: a fire burning tall grass in view recomputed the view mid-spread and scrambled the flames, caught by
  a step-by-step replay against the old build)
  (Emery 521 B free; the test kit fits again). tgt overlay 8128 B (arena 8192): secret-door scoring and region
  grouping written smaller.
- Bot: burned rooms (embers + fire traps: bare floor is a hidden fire trap 1 in 2) crossed only over embers and known
  floor, searching first; never through grass beside flames; the look-ahead runs when the flames could kill; mystery
  meat eaten in water or when healthy; no choke-point retreats from enemies that shoot (Tengu).
- Bot, food: ~60% of Goo deaths come while starving (no regeneration). Lab: arriving starving wins ~10% vs ~33% fed;
  arrival level 3: 6%, 4: 18%, 5: 36%. Resting is ~25% of all turns. Now: eat only once starving (PD's "hungry" costs
  nothing; eating early wastes up to 100 turns a ration), never with a foe near; with no food in the bag, rest from 40%
  up to 65% instead of 50% to 80%.
- 600-game runs (no head start), start of night -> end: reach Goo 308 -> 327, beat Goo 92 -> 101, reach Tengu 37 -> 53,
  beat Tengu 11 -> 12, burned 34 -> 25, wins 0 -> 0. Tengu lab (49 real arrivals x 3): 18% beat him.
- Open: once in 600 games a crazy thief's stolen item (Potion of Purification) was left carried by nobody (HEAP_ORPHAN,
  game 297 of the o9 run, depth 6): not root-caused.
