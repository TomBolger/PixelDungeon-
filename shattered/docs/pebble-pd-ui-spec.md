# Pebble PD UI spec (Emery, 200×228): the target look for Shattered on the watch

Taken from the Pixel Dungeon port's source (`watch/src/c/main.c`, `ui_int.h`, `ui_tabs.h`, `watch/src/ovl/{ui,uiw,bag,sheet}/*.c`, Emery branches only, v0.93) and emulator screenshots in `pebble-pd-shots/`. All rectangles are (x, y, w, h) in watch pixels. "2x" means each art pixel is drawn as 2×2.

## Common pieces

**Colours.** The art uses only pure Pebble greys plus PD's reds:
- Greys: black `000000`, DarkGray `555555`, LightGray `AAAAAA` and white.
- Reds: `AA0000` and `550000`.
- Hardly anything else; nothing olive.

The text colours (Pebble names):
- White: normal text.
- Icterine `FFFF55`: titles, the experience bar and zoom pips.
- PastelYellow `FFFFAA`: item and info titles, hints.
- ChromeYellow `FFAA00`: selection boxes, unknown marks, gold messages.
- Green `00FF00`: good things.
- SunsetOrange `FF5555`: bad things, and strength too high.
- Rajah `FFAA55`: warnings.
- LightGray `AAAAAA`: unselected labels, depth and keys.

**Fonts.** Only Pebble system fonts are used:
- `GOTHIC_14_BOLD` (s_font_hud): HUD numbers, titles, slot labels and window titles.
- `GOTHIC_14` (s_font_log): the log and all body text.
- `GOTHIC_18_BOLD`: big button labels, title-screen labels and rankings headings.
- `GOTHIC_18` / `GOTHIC_18_BOLD`: hero-sheet rows.
- `GOTHIC_09` (s_font_tiny): class names on hero select and the second line on Continue/Delete.
- `GOTHIC_28_BOLD` (s_font_big): rare.

Text drawn over the map always has a 1 px black outline (`text_outlined`).

**Window frame (`panel`).** PD's `chrome.png` WINDOW 9-slice is the source rect (0,0,22,22) with margin 7. It is drawn 6 px outside the content rect.
- Under the body there is a black "well" (`WELL`): black fill, 2 px wider on each side.
- The window title sits on the grey frame band above the well. It uses GOTHIC_14_BOLD in Icterine or PastelYellow, at content y−2 to y−4, centred (some windows left-align it).
- Windows with tabs use `tab_panel`: chrome (64,0,22,22) with margin 7.

**Behind every window,** the game shows through a 50 % black checker shadow (`blit_fog`: every other pixel black). Nothing is see-through anywhere else.

**Buttons (`red_button`).** Chrome (58,0,6,6) with margin 2, a 9-slice in red with a light edge.
- Selected: a 2 px ChromeYellow box on its edge.
- Labels (`big_label`): GOTHIC_18_BOLD white, centred. If the label won't fit on one line, it switches to GOTHIC_14_BOLD over two lines.

**Tabs (`tab_art`).** These are drawn with fills, not art:
- Lines: black, LightGray and DarkGray, matching the window's bottom edge.
- The chosen tab opens into the window, 4 px taller.
- An unchosen tab starts 4 px lower.
- Tab text: GOTHIC_14_BOLD, white when chosen, LightGray otherwise.

**Selection (keyboard focus).** Everything uses the same few marks:
- A 2–3 px yellow box around the focused cell or button.
- A white underline on the title screen.
- On the HUD: yellow corner brackets, which are Icons.TARGET's corners at 2x, each 12×12 (`reticle`).

## In-game HUD (`draw_hud_art`, `draw_hud_text`)

Shots: `game-zoom16.png`, `game-combat.png`, `game-back-highlight-*.png`.

**Status pane: (0,0,200,52).** It is PD's `status_pane.png` (128×35) drawn at 1.5x:
- Source (0,0,80,35) goes to (0,0,120,52).
- Source column 80 is stretched across (120,0,8,52).
- Source (80,0,48,35) goes to (128,0,72,52). This part holds the stairs and key icons.

The pieces on the pane:
- **Portrait:** hero sprite frame 0 (12×15) at 2x, drawn at (10,9,24,30).
  - Tap area: (0,0,52,52).
  - Focus mark: yellow corners round (0,0,45,45).
- **Level number:** GOTHIC_14_BOLD white, outlined, centred in (31,31,22). It sits in the small box under the portrait.
- **HP bar:** `hp_bar.png` (64×4) stretched to (45,4, 96×HP/HT, 6).
- **Experience bar:** (45,12, 96×exp/max, 2), Icterine fill.
- **HP text:** "17/20" in GOTHIC_14_BOLD white, outlined, left-aligned at (46,10,60).
- **Depth:** GOTHIC_14_BOLD LightGray, outlined, right-aligned in (113,−1,24), beside the stairs icon.
- **Iron keys:** same style, right-aligned in (137,−1,24), beside the key icon.
- **Buffs:** `buffs.png` 7×7 icons at 2x (14×14), at x = 57 + 16·i, y = 33. At most 4 fit.
- **Menu button:** (173,2,27,30), a red_button with 3 white bars, each 13×3, at (180, 10/16/22).
  - Tap area: (165,−2,39,42).
  - Focus mark: yellow corners.

**Toolbar: y = 190, height 38 (TB_H), 6 buttons of 32 px with an 8 px gap after the third.**

| # | Button | x | Icon (`toolbar.png` source → size at 2x) |
|---|---|---|---|
| 0 | Wait | 0 | (5,8,10,10) → 20×20, centred |
| 1 | Search | 32 | (24,7,12,12) → 24×24 |
| 2 | Inspect "?" | 64 | (46,7,8,12) → 16×24 |
| — | gap | 96–104 | — |
| 3 | Backpack | 104 | (65,5,14,15) → 28×30 |
| 4 | Quickslot 2 | 136 | item sprite 16×16 at 2x, at (bx, 193) |
| 5 | Quickslot 1 | 168 | item sprite 16×16 at 2x, at (bx, 193) |

- **Button face:** each button is the `toolbar.png` 9-slice (83,0,23,25) with margin 4, stretched to 32×38.
- **There are exactly 2 quickslots,** at the right end. Quickslot 1 is the rightmost; a new warrior gets darts there.
- **Quickslot counts:**
  - Quantity: GOTHIC_14_BOLD white, outlined, right-aligned in (bx, 210, 28).
  - Wand charges "n/m": top-left at (bx+3, 189).
  - An item no longer in the bag is greyed with the checker shadow.
- **Aiming:** a red crosshair (Icons 67,0,16,16 at 2x) is drawn over the quickslot doing the aiming.

**Right-edge tags.** Each is `tag_red.png`, `tag_blue` or `tag_white`, a 9-slice (0,0,16,14) with margin 3. Each tag is 22×24 at x = 178 and drawn 26 wide, so 4 px run off the screen. They line up with the physical buttons:
- **Up → zoom tag (178,40):** a magnifier (tag_red 16,1,12,12) at 1x at (181,46), plus one Icterine 3×3 pip per zoom level at (195, 57 − 5·i). Shown while the HUD is in play.
- **Select → action tag (178,102):**
  - Enemy: a red tag with the current weapon's sprite at 1x (or the enemy, when bare-handed).
  - Item on the floor: a blue tag with the item sprite.
  - Door or spot: a white tag with the walk arrow (Icons 83,0,12,11).
  - With a toolbar button highlighted: that button shrunk to half size.
- **Down → next-target tag (178,164):** Icons.TARGET (67,0,16,16) at 1x. Shown only when there is a target.

**Left-edge tags:**
- **Loot (blue):** (−4,54,40,36), the item underfoot at 2x. Tap = pick it up.
- **Resume (white):** (−4,54,40,30), or y = 92 when loot is also shown. Shows the arrow at 2x.

**Map.** It covers the whole screen behind the HUD, with the hero centred on x = 100 and y = 121 (midway between 52 and 190).
- Tiles are drawn 1:1, so 16 px per tile at the default zoom.
- Zoom levels: 8, 16 and 32 px. Up cycles 16 → 32 → 8 → 16.
- Visible cells: across ±6, up 4, down 4 at 16 px. The VIEW table is {12,8,8,9}, {6,4,4,4}, {3,2,2,2}.

**Target mark on the map.** Icons.TARGET (`icons.png` 67,0,16,16), four corner brackets, drawn exactly on the target cell's square:
- At the cell's top-left, sx_of(cx), sy_of(cy).
- Scale 1 at 16 px, 2 at 32 px.
- At 8 px it is drawn at 1x, shifted −4 so it spills 4 px over the neighbours.
- It sits on enemies (melee or aimed), doors, items and spots.
- **It is never offset from the tile.**

**Enemy health bar.** Above the targeted enemy's sprite:
- 1 px black outline, red background, green fill.
- 2, 3 or 5 px thick at the 8, 16 and 32 px zooms.
- As wide as the sprite at that zoom.

**Floating numbers** (damage, "+exp"): GOTHIC_14_BOLD, outlined, centred over the cell, rising 1 px every 33 ms.

**Log / messages (PD GameLog).** No box: up to 2 lines over the map, bottom-left.
- GOTHIC_14, black outline, x = 4, width 170 (clear of the right tags). The newest line is at the bottom, ending 4 px above the toolbar (y ≤ 186).
- If the two lines together are taller than 52 px, only the newest shows.
- All lines vanish 4 s after the newest message.
- Colours: info white, good Green, bad SunsetOrange, warning Rajah, gold ChromeYellow.
- Mode hints use PastelYellow and replace the log while their mode is active: "Tap a tile to inspect it" (`inspect-mode.png`), and the throw/zap/leap hints.

**Off-screen hero.** A small chrome frame (22,0,18,18, margin 5) of 28×28 at the screen edge, with the hero at 1x. Tap it to re-centre.

**Game over.** A GAME OVER banner (128×35) at (36,71). The death cause is GOTHIC_14_BOLD SunsetOrange, outlined, centred in (8,113,184,40). Any button then opens the dead menu: New Game, Rankings, Main Menu.

### Watch buttons in game (`button`, `long_*`, `click_back`)

| Button | Short press | Long press (500 ms) |
|---|---|---|
| Up | Zoom cycle | Game menu |
| Down | Next target (game_targets: enemies, item underfoot, things, doors, unexplored, stairs…). With Assist off and nothing in view, it puts focus on the toolbar instead. | Previous target |
| Select | Act on the target, the same as a tap on its cell (attack, walk, pick up, open). With no target: take the stairs or the thing underfoot, else search. | Open the backpack (with a HUD button focused: a held press on it) |
| Back | Move the focus mark round the HUD buttons (toolbar 0–5, menu button, portrait, then none). Select presses the focused button, Up goes back one, Down drops the focus. | Pebble: leave the app |

While walking a path, any button stops the walk.

### Touch (Emery has a touchscreen: `touch_service_subscribe`)

- **Tap a map cell:** act there (`game_tap_cell`).
- **Drag on the map:** a slide of 24 px or more between the status pane and the toolbar pans the camera. Select or tapping the hero re-centres.
- **Taps vs drags:** a movement of up to 40 px (TAP_SLOP) still counts as a tap. A press of 500 ms or more is a long press.
- **Toolbar:**
  - Wait (long: rest until healed).
  - Search.
  - Inspect, which enters inspect mode: the next tap shows that cell's info, and tapping yourself opens the hero sheet.
  - Backpack (long: Catalogue).
  - Quickslots: a tap uses the item (aims at the auto-target); a long press opens the backpack to choose the item.
- **Tags:** tap the zoom tag, the next-target tag, the action tag (act), loot or resume.
- **Status pane:** tap the portrait for the hero sheet, the menu button for the menu. The HP strip (y < 15) ignores taps.
- **Windows:**
  - Tapping outside a window closes it.
  - Dragging the text scrolls it, and a tap on the text pages it.
  - In the backpack, holding a cell for 500 ms picks the item up, and letting go on another cell swaps them.

## Backpack (WndBag, `bag.c`)

Shots: `bag-row-mode.png`, `bag-cell-mode.png`, `bag-exit-row.png`.

**Size and position.**
- Content is 179 wide (BIG_W) × 189 high: a 15 px head plus 5 rows of 34 px with 1 px gaps.
- With tabs, it sits at x = 10, y = 7.
- The frame (tab_panel) is drawn round the cells only.

**Title.** "Backpack" in GOTHIC_14_BOLD Icterine, sitting on an upside-down tab above the frame. In picker mode it shows "Select an item to …" in GOTHIC_14.

**Grid: 5 × 5 cells of 35×34** at x = 10 + 36·col, y = 22 + 35·row. DarkGray `555555` cells on black 1 px gaps:
- **Row 0:** weapon, armour, ring, ring, gold. Empty gear slots show ghost placeholder sprites; gold shows its count.
- **Rows 1–4:** the 19 backpack slots, plus Exit in the bottom-right cell. Exit is DarkCandyAppleRed with Icons.EXIT (`menu_icons` 128,0,16,16) at 2x.

**Inside a cell:**
- The item sprite at 2x (32×32), centred.
- Top-left: quantity, wand "n/m", or dew "n/10". GOTHIC_14_BOLD white, outlined.
- Top-right: ":STR" (white, or SunsetOrange if too heavy), "N?" in ChromeYellow while unknown, an orange "?" for unknown potions and scrolls, or the known potion/scroll icon (8×8 at 2x).
- Bottom-right: "+N" in Green, ChromeYellow if broken, or "−N" in SunsetOrange.
- Bottom-left: 3 durability pips (4×4: green, then red) on a black 16×6 bar.
- A cell with a known curse is dithered with BulgarianRose.

**Tabs** hang under the window at (6 + 37·t, 196, 36, 28):
- Bag icons from `icons.png` (17 + 10·k, 0, 10, 10) at 2x: backpack, seed pouch, scroll holder, wand holster, keyring.
- Focus mark: a 1 px yellow box.

**Focus and buttons.** The mark is a 2 px ChromeYellow box; in row mode it surrounds the whole row.
- **Row mode:** Up/Down move between rows, wrapping. Select enters the row.
- **Cell mode:** Up/Down move within the row and skip empty cells. Select opens the item; on Exit, it closes.
- **Back:** from cell mode to row mode, and from row mode it closes.
- **Long Select:** pick the item up to move it (green 2 px box); Select puts it down, swapping.

## Item window (WndItem)

Shots: `item-window.png`, `item-window-next-action.png`.

**Size.** Width 179, x = 10, centred vertically. The height is head + text + 2 + rows × 36.

**Head (at least 36 px tall):**
- The item sprite at 2x at the top-left.
- The name in GOTHIC_14_BOLD PastelYellow at x + 36, wrapping onto 2 lines, for example "Wand of Magic Missile (3/3)".

**Description.** GOTHIC_14 white in the black well, in a scrolling text layer. A small white triangle shows there is more.

**Actions.** 2 per row, each 88×34 with a 2 px gap. An odd last one spans the full 179.
- Labels: GOTHIC_18_BOLD white.
- Up/Down cycle the actions, Select does it, Back returns to the backpack on this cell.

## Info windows (WndInfoCell / WndInfoMob / WndMessage / story)

Shots: `mob-info.png`, `story-window.png`.

- **Size:** width 186 at x = 7, centred vertically, at most 212 high.
- **Mob windows:** the sprite at 2x top-left, the name in GOTHIC_14_BOLD PastelYellow, and a health bar under the name: 4 px, DarkCandyAppleRed background with Red fill. The head is 38 px; for a plain titled window it is 20 px.
- **Body:** GOTHIC_14 white in the black well.
- **Buttons:** Up/Down page the text; Select or Back closes.
- **Floor intro story:** this same window with no title.

## Options (WndOptions / WndQuest / jump confirmation)

- **Size:** width 179, title GOTHIC_14_BOLD Icterine, text GOTHIC_14.
- **Buttons:** stacked 179×34 with a 2 px gap, or two of 89×34 side by side when the text leaves no room.
- **Buttons:** Up/Down cycle, Select picks, Back closes. The resurrection window has no way out except its buttons.

## Game menu (long Up, or the menu button: `uiw.c`)

Shots: `menu-game.png`, `menu-catalogue-journal.png`, `menu-settings*.png`.

**Window.** Width 179, x = 10. Height = 16 (head) + 130 (two rows of 64 px buttons with a 2 px gap), plus 20 on the settings page.

**Three tabs underneath,** each 61×42 at x = 6 + 62·t. Their icons are `menu_icons.png` 16×16 at 2x: floppy disk (game), quill and paper (records), gear (settings).

**Pages:**
- **Game,** titled "Pixel Dungeon": 2 full-width buttons of 179×64.
  - Return to Game: a big drawn white arrow, outlined.
  - Save & Main Menu: a floppy disk.
- **Catalogue & Journal:** Catalogue (a potion icon) and Journal (a book icon).
- **Settings,** titled "Audio": 4 buttons of 89×64, two per row.
  - Music: On/Off, and Sound: Loud/Medium/Quiet/Off.
  - Below them, an "Assistant" heading inside a 1 px ChromeYellow frame, then Assist: Near/Far/Off and Item assist: On/Off.

**Each button:** a 2x icon centred at the top (y + 3) and the label in GOTHIC_18_BOLD at y + 35. A switched-off setting is dimmed with the checker shadow and crossed out (`menu_icons` 160,0).

**Buttons:** Up/Down walk every button across all pages, and the page follows the focus. Select presses; Back or a tap outside closes. When the hero is dead the menu has one page: New Game, Rankings, Main Menu.

## Hero sheet (WndHero, `sheet.c`)

Shots: `hero-sheet-stats.png`, `hero-sheet-buffs.png`.

**Window.** 172×150 at (14,26), with tabs "Stats" and "Buffs" (50×26) underneath.

**Stats tab:**
- Title: "LEVEL 1 MAGE" (the subclass replaces the class name) in GOTHIC_14_BOLD Icterine, left-aligned.
- 5 rows at y = 18 + 24·i, with 6 extra px after the third row: Strength, Health, Experience, Gold Collected, Maximum Depth.
- Each row: the label in GOTHIC_18 white, and the value in GOTHIC_18_BOLD white, right-aligned from 64 % of the width.

**Buffs tab:** buff icons at 2x with their names in GOTHIC_14_BOLD, 20 px rows, in two columns when there are more than 5. With none: "No effects".

**Buttons:** Up/Down switch tab; Select or Back closes.

## Catalogue and Journal (`sheet.c`)

Shots: `catalogue-*.png`, `journal.png`.

**Catalogue:**
- 188 wide.
- Title "Catalogue: Potions" or "Catalogue: Scrolls" in PastelYellow.
- 2 columns × 6 rows of 22 px. Each entry: the sprite at 1x, then the name without "Potion of" in GOTHIC_14 (white if known, LightGray if not).
- Tabs Potions/Scrolls, 61×26.
- Buttons: Up/Down switch tab; Select or Back closes.

**Journal:**
- A plain panel, 188 wide.
- 18 px rows: the place name in GOTHIC_14, with its depth and a stairs icon on the right.
- With nothing: "No special places found yet."
- Buttons: Up/Down scroll.

## Title screen (`ui.c`)

Shots: `title*.png`.

**Background:** `arcs.png` 64×64 tiles at 1x, covering the screen (dark arches with red lines).

**Pieces:**
- **Logo:** 128×70 at (36,14).
- **Play and Rankings:** big icons from `play.png` (32×32 each) drawn at 2x. Their rects are (26,104,64,84) and (110,104,64,84).
- **Labels:** GOTHIC_18_BOLD at y + 62, white when focused (LightGray otherwise), with a 2 px white underline at (x+12, 188, 40, 2) when focused.
- **Sound corner:** (2,2,30,30), a chrome 9-slice (22,0,18,18, margin 5) with a drawn speaker. Focus mark: a 1 px white box.
- **Version:** "v0.93" in GOTHIC_14_BOLD LightGray, bottom right.

**Buttons:** Up/Down cycle Play → Rankings → sound; Select picks.

## Hero select (StartScene)

Shots: `heroselect-*.png`.

**Pieces:**
- **Background:** arcs.
- **Banner:** "Select Your Hero", 128×21 at (36,6).
- **No save, a 2×2 grid:** class cells of 56×72 at (32,34), (112,34), (32,110) and (112,110).
  - Each cell: the avatar from `avatars.png` (24×28) at 2x at (x+4, y), and the class name in GOTHIC_09 at y + 56.
  - Focused class: a 3 px Yellow box, and its name in white. A locked class is greyed.
  - A **New Game** button below: 192×38 at (4,186), with its label in GOTHIC_14_BOLD.
- **With a save:** the hero's avatar at 2x in the middle, with the class name under it.
  - Two buttons of 93×40 at (4,184) and (103,184): "Continue" over "Depth 1, level 1", and "Delete" over "Erase this game". The first line is GOTHIC_14_BOLD, the second GOTHIC_09.
  - **Erase confirmation:** a 160×76 panel centred on the screen. Title "Erase this game?" in PastelYellow, the text "Your hero will be lost.", then Erase and Cancel buttons of 70×24.

**Buttons:** Up/Down change the class or button; Select confirms; Back goes to the title. Tapping a class that is already chosen shows its perks window.

## Rankings

Shot: `rankings-empty.png`.

**Background:** arcs, with "Top Rankings" in GOTHIC_18_BOLD.

**Rows:** 30 px each from y = 26, x = 4, width 192:
- A tomb or amulet sprite at 1x.
- The rank number in GOTHIC_14_BOLD.
- The text in GOTHIC_14, or GOTHIC_09 if it is too long.
- The class avatar (24×28) at 1x at the right.

The latest game's row is boxed in LightGray (PastelYellow for a win). Underneath: "Games played: won/total".

**Empty:** "No games have been played yet." in GOTHIC_18_BOLD LightGray, centred.

**Buttons:** any button returns to the title.

## What this means for Shattered on the watch (checklist)

1. **Toolbar:** exactly 6 buttons of 32×38 at y = 190: Wait, Search, Inspect | gap | Backpack, Quickslot 2, Quickslot 1. **2 quickslots,** with icons at 2x.
2. **Status pane:** 52 px tall at the top, PD layout at 1.5x. Portrait at 2x, HP bar 96×6, experience bar 96×2, HP text, depth, keys, red menu button (173,2,27,30), and 14 px buff icons.
3. **Map:** 16 px tiles by default (8/16/32), centred at (100,121). The target mark is the cell's own 16×16 corners, exactly on the tile.
4. **Log:** 2 outlined GOTHIC_14 lines over the map, bottom-left, 170 wide, vanishing 4 s after the newest message.
5. **Right tags** at x = 178 for Up (40), Select (102) and Down (164). Left loot and resume tags at y = 54.
6. **Windows:** a dark grey frame around a black body, a 50 % black shadow behind, and red buttons with a 2 px yellow focus box.
   - Backpack: a 5×5 grid of 35×34 cells with 2x items.
   - Item window: action buttons 88×34, two per row.
   - Menu: 3 tabs of big 64-px-tall buttons.
7. **Fonts:** Pebble system Gothic only (14 / 14 bold / 18 bold, 9 for tiny labels). **Colours:** pure greys and PD reds.
8. **Buttons and touch** as in the two tables above.
