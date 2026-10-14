# Architecture

Pixel Dungeon is about 61,000 lines of Java written for phones. A Pebble Time 2 gives an app 64 KB of code and about 64 KB of heap. This document explains how the port fits the game into that space, and where to find each part of it.

## Design rules

1. **Faithful rules.** Combat, monster behaviour, level generation, items and texts follow the Pixel Dungeon 1.9.1 source. Comments often name the Java class or method a piece of code comes from. Where the port deliberately differs (for example, to make a tiny screen fairer), the comment says so.
2. **Code costs RAM, so code that is not running is not loaded.** Only the core game loop lives in the app image. Everything else is an overlay that is loaded into RAM when needed and dropped afterwards.
3. **Data lives in resources, not in code.** Long texts, monster and item tables, art and music are read from the resource pack on demand.
4. **The save is sacred.** The game saves on every step and survives the watch resetting mid-save. Old saves keep loading after updates.

## The memory budget

| Area | Budget | Used by |
|---|---|---|
| App image (code + constants) | 64 KB | `src/c/*.c`, currently about 63.8 KB on Emery |
| Heap | about 66 KB free at start | game state, the floor, sprite caches, the 8 KB overlay arena, the sound buffer |
| Overlay arena | 8 KB | one or more code overlays at a time |
| Resource pack | 1 MB per platform | art, overlays, text, music, about 560 KB |
| Persistent storage | the watch's key/value store | the save, rankings and settings |

`pebble build` prints the app size and free heap. The Diagnostics screen in the game's menu shows live heap use on the watch.

## Code overlays

An overlay is a piece of C code that is compiled separately, stored as a resource, and copied into RAM only while it is needed. This is how the full game fits in 64 KB.

- Each folder under `watch/src/ovl/` is one overlay. Its source ends with `OVL_ENTRY(name, fn0, fn1, …)`, a table of entry points.
- `tools/build_ovl.py` compiles each overlay against the **final** app image. It links at address 0 with the app's symbols imported, records which words must be moved when the overlay is loaded, and writes `resources/data/ovl_<name>~<platform>.bin`. Overlays call app functions through absolute addresses, so the app image must not change after overlays are built. This is why `tools/build.sh` builds twice and checks.
- `src/c/ovl.c` loads an overlay into the 8 KB arena, applies its relocations, and checks that it was built for this exact app image. Most overlays are **transient**: they are loaded for one job and freed. A few are **resident**, kept on the heap while in use: the sound engine, the `mobs` overlay, and the current region's monster overlay.
- `src/c/ovl.c` also holds the small wrapper functions the rest of the game calls (for example, "generate a floor", "use this item"). Each wrapper loads the right overlay, calls it, and frees it.
- An overlay must fit in 8 KB, and the build stops if one does not. The level generator is the one exception: it is loaded straight onto the heap while a floor is built.
- Overlay `static` variables start from zero on every load. Anything that must last between loads belongs in the app (`game.c`) or the save.
- Some overlays share one source file and are split by a define, so each half fits: `hero`/`hero2`, `tgt`/`tgt2`, `use`/`pick`, `floor`/`save`.

| Overlay | What it does |
|---|---|
| `level`, `content` | Level generator (RegularLevel, SewerLevel and painters), plus each new floor's monsters and items |
| `floor`, `save`, `newgame` | Saving and loading, arriving on a floor, starting a game |
| `gen` | Random item generation (Generator, Weapon/Armor/Wand/Ring.random) |
| `sewers`, `prison`, `caves`, `city`, `halls` | Each region's monsters, bosses, NPCs and quests |
| `mobs` | Monsters found anywhere (mimics, wraiths, statues, piranhas, bees, shopkeepers) |
| `hero`, `hero2` | Hero actions: picking up, zapping, throwing, shops, cooking, the Tome of Mastery |
| `use`, `pick` | Using items (eat, drink, read, equip) and item-choice windows |
| `wand`, `ench`, `trap` | Wand effects, weapon enchantments and armour glyphs, traps and plants |
| `info` | Item and terrain descriptions |
| `tgt`, `tgt2` | The target list for the buttons and the Assist suggestions (shared with the bot) |
| `ui`, `uiw`, `bag`, `sheet` | Title and hero select, in-game windows, backpack, hero and journal windows |
| `snd` | The music and sound synth |

## Code map (`watch/src/c`)

| File | Contents |
|---|---|
| `pd.h` | Shared types: Game, Hero, Mob, Item, Heap, terrain flags, persist keys and limits |
| `game.c` | The always-loaded rules: actor time, attack/hit/damage, hero actions, mob AI, field of view, pathfinding, buffs |
| `game_int.h`, `items_int.h` | Internals of `game.c` and `items.c` that overlays may call (these must stay non-static) |
| `items.c`, `item_defs.inc` | Item kinds, names and numbers (the item table is also exported as `item_defs.bin`) |
| `level_rt.c` | Terrain flags, the random number generator, room helpers (the part of level code that is always loaded) |
| `main.c` | Screens, drawing, input, the target cycle, animation timing and power saving |
| `blit.c` | Drawing into the frame buffer: zoom levels and PD's stretchable window frames |
| `ovl.c`, `ovl.h` | The overlay loader and the wrappers that call into overlays |
| `sfx.c`, `snd.h` | Sound settings and the timer that feeds the speaker |
| `text.c`, `text_ids.h` | Long texts read from `text.bin` by ID |
| `hero_impl.h`, `mobovl.h`, `mob_*.inc` | Code shared by several overlays, included where needed |
| `testkit.h` | The test kit's starting floors (only built with `TEST_BUILD`) |
| `*.auto.h` | Generated by the tools; do not edit by hand |

## Resources (`watch/resources`)

- `images/`: converted sprites, tiles, UI pieces and icons (64-colour Pebble palette; some sheets are split into 4-bit strips).
- `data/text.bin`: every long text, built from `text/en.txt` by `tools/build_text.py`.
- `data/mobs.bin`, `data/item_defs.bin`: the monster and item tables.
- `data/items_raw.bin`, `data/tiles_raw.bin`, `data/mob_art.bin`: raw sprite cells and animation frames.
- `data/music_*.bin`, `data/sfx.bin`: the soundtrack (note events and wavetables) and sound effects.
- `data/ovl_*.bin`: the compiled overlays (build output, not committed).

## Saving

- The game saves at every step through Pebble's persistent storage (`watch/src/ovl/floor/floor.c`).
- **Main record:** the hero, the game, and the current floor.
- **Visited floors:** each visited floor has a small record of changes. Floors are rebuilt from their seed on arrival, and the record is applied on top.
- **Gas and fire:** clouds on each floor are saved in their own keys.
- **Crash-safe:** the main record alternates between two banks of keys, and a length key written last says which bank is valid. A save cut short by a reset leaves the previous save whole.
- **Versioned:** saves carry a version number (`SAVE_MAGIC`), and older versions are converted on load.
- **Permanent death:** a death erases the save, as in the original game.

## Input and Assist

- **Buttons:**
  - Down / long Down: cycle forward / back through the targets (enemies, items, unexplored areas, stairs).
  - Select: act on the current target.
  - Up: zoom. Long Up: open the game menu.
  - Back: step across the toolbar buttons. Long Select: open the backpack, or long-press the highlighted button.
- **The target list:** built by the `tgt` overlay, ranked the way the bot would rank it.
- **Assist** (game menu): sets how much the list suggests.
  - Low: attack, explore and pick-up targets.
  - Medium: adds single steps.
  - High: adds toolbar and item use, and makes Back follow the suggested order.
- **Touch:** where the watch has it, taps work as a backup, and a tap cancels a toolbar highlight.
- **Power saving:** after 10 seconds without input, the game's timers stop until the next button press or tap.

## The host build

The same game code also compiles for Linux (`-DOVL_HOST`). The bot and the host tests use this build:

- `bot/src/pebble.h` stands in for the Pebble SDK: persistent storage is an in-memory table, and resources are read from `watch/resources/data`.
- Each overlay becomes a shared library that is opened and closed like an overlay on the watch. This keeps the zeroed-statics behaviour identical.
- The bot's results are therefore results for the watch's own code.

`tools/javapd` runs the **original Java** game without a screen, so scenarios (a warrior against a rat, fire on a door, gas) can be run in both and compared statistically (`bot/compare.sh`).
