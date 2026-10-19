# Shattered on the watch vs Pebble PD: gaps and plan (2026-10-19)

Thomas's goal: Shattered on the watch should look **exactly like Pebble PD** (our Pixel Dungeon port, `watch/`), with the same number of quickslots. Gabbro comes next. This file compares the two screen by screen and sets out how to close each gap within our setup:

- Shattered runs headless on the phone.
- The watch mirrors Shattered's scene graph (`common/src/pebxel/Link.java`).
- Our changes go into `patches/spd.patch` (Shattered itself), `common/src/pebxel` (watch-only Java UI), `tools/convert_art.py` (art), and `watch/src/c` (renderer).

Pictures of the current state come from `tour.sh` (stand-in watch, 2x). The Pebble PD reference is `docs/images/watch-screens.png` plus the code cited below.

---

## 1. Pebble PD's screens (what we are matching), Emery 200x228

All numbers are watch pixels. The source is `watch/src/c/main.c` (layout around lines 255–400; HUD in `draw_hud_art` and `draw_hud_text`), `ui_int.h`, `ui_tabs.h`, and `src/ovl/{ui,uiw,bag,sheet,use,info}`.

| Element | Pebble PD |
|---|---|
| Status pane | PD's own `status_pane.png` at **1.5x** (`SC(v)=v*3/2`). It is 52 px tall: left 80 art px, a stretched column, right 48 px. Portrait: hero frame 0 at **2x**. Under it, the level box (Gothic 14 bold, outlined). The HP bar is `hp_bar.png` scaled to health. The XP line is 2 px yellow. Depth and iron-key counts sit beside stair and key icons (light grey, outlined). Buffs are 7x7 icons at **2x**, spaced 16 px, under the bar. |
| Menu button | Top right, 27x30, red button, three white bars (hamburger). |
| Toolbar | **6 buttons**, each 32x38 (`TB_H 38`). Each uses PD's toolbar frame as a nine-slice (`toolbar.png` 83,0 23x25 m4). The groups are: wait, search, inspect, then an **8 px gap**, then backpack, **quickslot 2**, **quickslot 1**. Icons are at **2x** (wait 5,8 10x10; search 24,7 12x12; inspect 46,7 8x12; bag 65,5 14x15). Item sprites are 2x (32 px). The counts and wand charges are Gothic 14 bold, outlined. **Two quickslots.** |
| Game log | The two newest lines, Gothic 14, outlined in black, no box. They sit bottom-left over the map just above the toolbar, 22 px clear of the right edge. A message may cover the map (Thomas: OK). |
| Button tags (right edge) | Three slim tags, 22x24, at the screen's right edge, each beside the watch button that works it. Up: zoom (a magnifier plus one pip per level). Select: attack, act, or pick up (the weapon at 1x, or the item, or the walk arrow). Down: next target (Icons.TARGET). |
| Loot and resume tags | Left edge under the status pane, 40x30 and 40x36: PD LootIndicator (blue) and ResumeIndicator (white). |
| Target marker | Icons.TARGET corner reticle at 2x around the chosen cell or button (`reticle`). |
| Map | Tiles at 8, 16 or 32 px (zoom 0, 1 or 2; **16 is the default**). Up cycles the zoom. An off-screen hero gets a framed portrait at the edge. |
| Title | PD title: the logo, plus two dashboard items, **Play** and **Rankings** (64x84, art 2x, underline for the selection), a mute button top left, and the version. |
| Hero select | PD StartScene: a 2x2 class grid (portrait and name, the selected one framed), a big red "New Game" banner, and a class info window (`draw_class_info`). |
| Game menu | WndGame: **tabbed pages of big picture buttons** (`menu_icons.png`, tiles about 34 px tall). There are three tabs: game (return, save, …), records (catalog, journal, rankings), and settings (music, sound, Assistant). The tabs hang under the window (`tab_art`). |
| Backpack | WndBag: a **5x5 grid of 35x34 cells** (`BAG_COLS 5`, `SLOT_W 35`, `SLOT_H 34`, `BIG_W 179`). The first row is weapon, armour, rings and gold, then 19 slots, then Exit in the bottom-right cell. The title sits on a tab above the frame. Bag tabs (36x28, icons 2x) hang below. Known potions and scrolls show Shattered's 8x8 item icons. Selection works row first, then cell. |
| Item window | WndItem: the icon and the name (Gothic 14 bold, pale yellow), the description in a scrolling text layer, then **big action buttons, two to a row, 34 px tall**. |
| Character sheet | WndHero: Stats and Buffs tabs; buffs at 2x with names. |
| Info windows | WndInfoCell, WndInfoMob and WndMessage: 186 wide, centred. A mob gets its sprite at 2x and a health bar. Long text scrolls. |
| Options windows | Big buttons, 34 tall, stacked. |
| Rankings | The top six (tomb or amulet, place, result, hero). |
| Death | PD game-over banner, then a one-page game menu (new game, rankings, main menu). |
| Frames | `chrome.png` from PD, converted. The window frame is a light edge, a grey border, and a **black body under the text** (`WELL`). |
| Fonts | HUD: Gothic 14 bold. Log and body: Gothic 14. Big labels: 18 bold, falling back to 14 bold. Tiny: Gothic 09. |
| Input | Buttons, plus **touch** (`touch_service_subscribe`, `touch_handler`): tap, a 500 ms long press, drag to pan the map, drag to scroll text. Back steps through the HUD buttons. |
| Gabbro (260x260 round) | Everything sits at the rim in bubbles. A grey cap over the top arc holds the arcing HP and XP bars, depth and keys. The portrait bubble is left and the menu bubble right. **The six toolbar bubbles follow the bottom-left arc** (`TBX/TBY`, radius 17). The button tags are rim bubbles beside Up, Select and Down. Buffs run down the left rim. Windows are boxes cut by a circle (`rpanel`, R_OUT 127 / R_IN 121), with text flowing along the curve (`flow_set`). |

## 2. Shattered's pieces (what we have), v4.0.2

All paths are under `spd/core/src/main/java/com/shatteredpixel/shatteredpixeldungeon`. The art is under `spd/core/src/main/assets/interfaces/`.

- **GameScene HUD** (`scenes/GameScene.java` about lines 380–565, and `layoutTags()` at line 982):
  - `MenuPane` (`ui/MenuPane.java`, `menu_pane.png` 1,0 31x21; `menu_button.png`): the top right. It holds the journal button (with KeyDisplay for keys), the menu button, the depth icon with a region and challenge count, and DangerIndicator under it.
  - `StatusPane(large)` (`ui/StatusPane.java`, `status_pane.png`): small form 0,0 82x38 nine-slice; HP 0,40 50x4; shield 0,44; XP 0,48 17x4; hpCutout 90,0 12x9. The large form is 0,64 41x39, with HP 128x9. It also holds the hero portrait (HeroIcon), the level, BuffIndicator (`buffs.png` 7x7, or `large_buffs.png`), and the busy indicator.
  - `Toolbar` (`ui/Toolbar.java`, `toolbar.png`):
    - Inventory is Tool 0,0 24x26 (icon 160,0 16x16); wait is 24,0 20x26 (icon 176,0); search is 44,0 20x26 (icon 192,0).
    - Quickslot frames are 86,0 20x24, 88,0 18x24 and 106,0 19x24. The swap tool is 128,0 21x23.
    - **Quickslot count:** `quickslotsToShow = 4`, plus one if `uiCamera.width > 152`, plus one if `> 170`. With `quickSwapper`, it is 3 plus a swap button. `QuickSlotButton.lastVisible` limits where items auto-assign. Storage is `QuickSlot.SIZE = 6`.
    - Search: **tap = examine** (selectCell with an informer), **hold = search**. There is no separate inspect button.
    - Modes: SPLIT, GROUP, CENTER.
  - Tags (`ui/Tag.java`, SIZE 24; `chrome.png` TAG 22,18 16x14): AttackIndicator, LootIndicator, ResumeIndicator, and **ActionIndicator** (class abilities, combo, and similar: Shattered only). They stack up the right edge from the toolbar's top.
  - `GameLog` sits above the toolbar, left, leaving room for the tags.
  - `BossHealthBar` (`boss_hp.png`) shows under the status pane, centred. `TargetHealthIndicator` follows the map. `Compass` points to the exit. `CircleArc` and `KeyDisplay` are not mirrored yet.
  - `InventoryPane` appears only with uiSize 2 (unused).
- **Scenes:**
  - `TitleScene` (logo plus 8 StyledButtons in a grid: Enter, Support, Rankings, Journal, News, Changes, Settings, About).
  - `StartScene` (games in progress, with a New Game button).
  - `HeroSelectScene` (a full-screen splash per class, a strip of **6** class portraits along the bottom, the name with an info button, and a start button).
  - `WelcomeScene`, `RankingsScene`, `JournalScene` (badges, catalog, notes, lore tabs), `AboutScene`, `ChangesScene`, `NewsScene`, `SupporterScene`, `InterlevelScene`, `AmuletScene`, `SurfaceScene`, `AlchemyScene`.
- **Windows** (`windows/`):
  - `WndBag` (`ui/InventorySlot`, `ItemSlot`; SLOT_WIDTH/HEIGHT depend on the interface size; about 5 columns of about 24 px at zoom 2).
  - `WndUseItem` / `WndInfoItem` (RedButtons for actions, laid out to the text width).
  - `WndHero` (tabs: stats, talents, buffs).
  - `WndGame` (stacked RedButtons: settings, challenges, restart, rankings, main menu…).
  - `WndSettings` (tabs: display, UI, input, data, audio, language), `WndJournal`, `WndRanking`, `WndOptions`, `WndInfoMob`, `WndInfoCell`, `WndResurrect`, `WndChallenges`, `WndTextInput` (stays on the phone), `WndQuickBag`, `WndKeyBindings`, and quest windows (`WndBlacksmith`, `WndWandmaker`, `WndSadGhost`, `WndImp`).
  - **Window chrome:** `Chrome.java` maps `chrome.png` regions (WINDOW 0,0 20x20 m6; TOAST 20,0 9x9; RED_BUTTON 38,0 6x6; GREY_BUTTON 38,6; TAG 22,18; TAB_SET 64,0; TAB_SELECTED 65,22 8x13; …).

## 3. What the watch shows now (from `tour.sh`)

- **Interface zoom.** Zoom is 3 in game, which is 1.5x Shattered's art. Every interface pixel is drawn 1 or 2 watch pixels wide, so icons look lumpy.
- **Screens kept at zoom 2.** Title, welcome, about, changes, news and supporter stay at zoom 2. They look small and busy next to the game screen.
- **Map scale.** The map uses its own zoom. The default of 16 px per tile matches Pebble PD, and the map is exact 1:1.
- **HUD.** It is Shattered's own, so it doesn't match Pebble PD:
  - **Status pane:** Shattered's art at 1.5x, with no depth or keys block like PD's (depth lives in MenuPane). The tiny "v4.0.2" and depth figures are unreadable.
  - **Menu and journal:** the two buttons are cramped into the top right.
  - **Toolbar:** at this width it holds wait, search and bag plus **3 to 4 quickslots**. Pebble PD has 6 big buttons with **2 quickslots** and a separate inspect button.
- **Windows** are Shattered's, at 1.5x:
  - The **bag** is about 5 columns of about 40 px with a dithered translucent look (now made solid). It has no Exit cell or PD-style tabs, and its top is cut off unless Controls scrolls it.
  - The **item window** has small RedButtons in a row (DROP/THROW/UNEQUIP), where PD has big two-per-row buttons.
  - **Settings** are full of sliders and options that make no sense on the watch (fullscreen, brightness…).
  - **Challenges** run off the screen.
- **Title** is Shattered's 8-button grid; Pebble PD has 2 dashboard items. **Hero select** is Shattered's splash plus a 6-portrait strip; PD has a 2x2 grid.
- **Welcome and about** overlap at zoom 2, because Gothic text is taller than Shattered's pixel font.
- **Thomas's other reports:**
  - **The target marker is offset from the tile.** The cause hasn't been checked. Controls draws the frame as four 1x1 `ColorBlock`s scaled in world units (`Controls.Frame.inWorld`). The guess is that K_RECT scale and origin, or rounding of the camera scroll (x8 vs the tile map's matrix), differ on the watch. Another possibility is that the rectangles move through the play-out queue a frame apart from the camera. To check, compare `smoke.sh LINK=` side-by-side pictures with the frame up.
  - **Touch does nothing.** The Shattered watch app never subscribes to touch, and the protocol has no touch message.
  - **The art is hard to read.** One cause is the 1.5x uneven scaling of interface art. The other is that Shattered's sprites carry more shading than PD's, which turns into speckle in 64 colours.

## 4. Gap list: each Pebble PD element, Shattered today, and the change

| # | Pebble PD | Shattered on the watch now | Change |
|---|---|---|---|
| 1 | Interface drawn at exact multiples (1x pixels, 1.5x status pane, 2x icons) | Zoom 3 everywhere in game (1.5x) | **Interface zoom 2** everywhere (1 layout px = 1 watch px), with a watch-only HUD and watch-only windows that draw at 2x where PD does. Shattered windows we don't replace keep a **window camera at zoom 3** (patch `Window` to use `pebxelWindowZoom`), so their text stays big. See D1. |
| 2 | PD status pane at 1.5x: portrait, level, HP, XP, depth and keys, buffs at 2x | Shattered StatusPane plus MenuPane | Hide `status`, `menu`, `toolbar` and `boss` from the link (or `visible=false` through a patch hook). Add `pebxel.hud.WatchHud` (a `Group` on `uiCamera`), built from **Pebble PD's converted art** (`watch/resources/images/status_pane.png`, `hp_bar.png`, `toolbar.png`, `tag_*.png`, `icons.png`, `buffs.png`, `menu_icons.png`), copied into `common/assets/pebxel/pd/` and packed by `pack_art.py`. It is laid out with Pebble PD's constants (`SC()`, `TB_*`, `HUD_*`) and reads Shattered's state: `Dungeon.hero.HP/HT/lvl/exp`, `Dungeon.depth`, the iron keys via `Notes.keyCount(new IronKey(depth))`, and buffs by mapping Shattered `BuffIndicator` ids to `buffs.png`; Shattered-only buffs use `interfaces/buffs.png` icons at 2x. |
| 3 | Menu button, red, 27x30, top right | MenuPane: menu plus journal plus depth | The WatchHud menu button opens our game menu (#9). The journal moves into the menu's records tab. |
| 4 | 6 toolbar buttons: wait, search, inspect, gap, bag, Q2, Q1; 2 quickslots | Shattered Toolbar, 3–6 quickslots | In WatchHud, draw 6 buttons exactly as PD does. Actions: wait = `hero.rest(false)` (hold = rest); search = `hero.search(true)`; inspect = `GameScene.selectCell(informer)` (Shattered's examine); bag = `GameScene.show(new WndBag…)` (hold = journal or catalog, as PD); Q1 and Q2 = `QuickSlotButton` logic for slots 0 and 1. Set `QuickSlotButton.lastVisible = 2` (patch the `Toolbar.layout` hook) so auto-assign fills only the visible two. Keep Shattered's Toolbar alive but invisible, because its key actions and `PickedUpItem` animation are used. |
| 5 | Game log: 2 lines, outlined, bottom-left over the map | Shattered GameLog (up to about 4 lines, wraps) | Hide GameLog from the link. WatchHud keeps the last 2 entries (hook `GLog.update` listeners; colours from `GLog` prefixes `++ -- ** @@`) and draws them as outlined text. Outlined text needs a watch text flag: add an `outline` bit to K_TEXT (draw.c draws the 4 offsets in black, as PD's `text_outlined`). The map may be covered. |
| 6 | Right-edge button tags: zoom, act or attack, next target | Shattered Attack, Loot and Action tags stacked from the toolbar | WatchHud draws the three tags at PD's `ctag()` places with `tag_red/white/blue.png`. The Select tag shows what Select will do, using Controls' current target (the weapon, the item, the walk arrow, or **ActionIndicator's icon** when an ability is ready: Shattered's extra goes on Select's tag, with hold Select = ability). Shattered's own tags are hidden from the link. |
| 7 | Loot and resume tags at the left under the status pane | LootIndicator and ResumeIndicator on the right | Move them to PD's `tag_rect()` places in WatchHud, or hide them and let the Select tag cover them (PD does both). |
| 8 | Target reticle: Icons.TARGET corners at 2x | Controls' 1 px ColorBlock frame (offset) | Replace `Controls.Frame` with four 6x6 `Image`s from `interfaces/icons.png` TARGET corners at 2x (PD `reticle`). Map cells use `Camera.main`; the frame goes in `GameScene.effect` with integer cell coordinates. This also removes the K_RECT scale path behind the offset. Check with side-by-side pictures. |
| 9 | Game menu: tabbed pages of big picture buttons | WndGame (stacked RedButtons) plus WndSettings | New `pebxel.ui.WndWatchMenu` (a `Window` at zoom 2): three tabs (Game, Records, Settings), with **big picture buttons using `menu_icons.png`** at PD sizes. Game tab: resume, save and quit, challenges info, new game. Records tab: journal (`JournalScene` or a WndJournal tab), catalog, rankings, badges. Settings tab: only what matters on the watch (music, sound volume, vibration later, quickslot swap off, text size if we keep two). Opened by the menu button and by hold Up. |
| 10 | Backpack: 5x5, 35x34 cells, Exit cell, bag tabs at 2x, title on a tab | WndBag | `pebxel.ui.WndWatchBag`: the same grid and geometry as PD's `bag.c`, filled from `Belongings` (row 1: weapon, armour, artifact, misc, ring; **Shattered has 5 equip slots: weapon, armour, artifact, misc, ring**, so gold moves into the title as PD's top-right gold count). Bags (`VelvetPouch`, `ScrollHolder`, `PotionBandolier`, `MagicalHolster`) become tabs. Item icons come from `ItemSprite` at 2x. Selection is the ItemSelector modes (`WndBag.ItemSelector`) so every caller still works: patch `WndBag.getBag/lastBag` and `GameScene.selectItem` to open ours. |
| 11 | Item window: name plus scrolling text plus big two-per-row buttons | WndUseItem/WndInfoItem with small RedButtons | `pebxel.ui.WndWatchItem`, PD layout, built from `item.actions(hero)` and `item.execute(hero, action)` (as WndUseItem). The description goes in a scrolling text area. Patch `GameScene.show(Window)` (or `WndUseItem` construction sites: `InventorySlot.onClick`, `QuickSlotButton`) to swap it in. |
| 12 | WndHero: Stats and Buffs tabs | WndHero: stats, talents, buffs | Keep Shattered's WndHero (talents are Shattered-only and needed) at window zoom 3, restyled by the chrome (#16). Later: a PD-style stats page. |
| 13 | Info windows (cell, mob, message) | Shattered WndInfo* at zoom 3 | Keep them at window zoom 3. Their text and layout are fine; they only need the frame (#16) and a black body under the text (PD `WELL`). |
| 14 | Options windows with big buttons | WndOptions with RedButtons | Patch `WndOptions` so buttons are 34 px tall at zoom 2 (or 22 at zoom 3), stacked, using PD's red button chrome. |
| 15 | Title: Play and Rankings dashboard, mute, version | Shattered TitleScene grid | `pebxel.ui.WatchTitleScene` (or a patch to TitleScene's layout): logo, **Play** (goes to `StartScene` / `HeroSelectScene` as Enter does) and **Rankings**, using PD's `play.png`/`avatars` dashboard art at 2x, plus mute. Support, News, Changes, About and Settings drop off the watch (phone-only). Keep Journal under Rankings' row only if Thomas wants it. |
| 16 | PD window frame (light edge, grey border, black body) | Repainted grey frames (black edge, light band, dark grey body) | Paint `chrome.png` regions exactly like PD's converted chrome. Copy the pixels for WINDOW, TAB_SET, TOAST, RED_BUTTON, GREY_BUTTON, TAG and TAB_SELECTED/UNSELECTED from `watch/resources/images/chrome.png` (the regions match: Shattered's chrome layout is PD's extended) in `simple_frames()`. Add PD's black **WELL** under window bodies: a patch hook in `Window` that adds a black `ColorBlock` behind the content area of text windows. |
| 17 | Hero select: 2x2 grid, New Game banner | Splash plus strip of 6 portraits | `pebxel.ui.WatchHeroSelect`: a **3x2 grid** (Shattered has 6 classes; locked ones greyed, as PD's huntress), PD `avatars`-style portraits from Shattered's hero sprites at 2x, the red "New Game" banner (`banner_select.png`), and class info on hold Select (`WndHeroInfo` at zoom 3). It keeps `GamesInProgress.selectedClass` and starts as `HeroSelectScene` does (`InterlevelScene.mode = DESCEND`). |
| 18 | Rankings: top 6 rows | RankingsScene (fine but small at zoom 2) | Restyle with PD rows, or keep at zoom 3 (`RankingsScene` is simple). |
| 19 | Death: game-over banner plus one-page menu | Shattered death window (WndResurrect or WndGame) | Show PD's `banner_gameover.png` and our menu's death page. |
| 20 | Touch: tap, hold, drag pan, drag scroll | None | **Watch:** subscribe with `touch_service_subscribe` and send `KEY_TOUCH` (type plus x, y; down, move and up; moves throttled to about 15 per second). **Phone:** `Controls.touch()` turns them into Shattered `PointerEvent`s at device pixels (x2, y2) through `PointerEvent.addPointerEvent`. Shattered's own buttons, CellSelector taps, drag-pan and ScrollPane then work. The watch-only HUD and windows get `PointerArea`s like any noosa UI. |
| 21 | Map art readable at 16 px | Shattered tiles and sprites through PD's tuned `pebbleize` | Map scale is already exact. Improve sprite readability in `convert_art.py`: (a) for `sprites/`, merge near shades first (cluster to at most 6 colours per sprite frame before the Pebble mapping) so shading doesn't turn into speckle; (b) keep the black outlines Shattered's sprites have (do not let alpha-edge pixels dither). Compare pictures against Pebble PD's look. Tiles: SPD's walls are busier than PD's, so trial a stronger mid-tone flattening for `environment/tiles_*.png`. |
| 22 | — | Shattered extras: journal, catalog, notes, lore, talents, challenges, boss bar, ActionIndicator, quickslot swap, alchemy, compass, KeyDisplay, danger count, guidebook hints | **Journal, catalog, lore and badges:** Records tab of the menu (#9). **Talents:** WndHero (#12). **Boss bar:** keep `BossHealthBar` under WatchHud's status (it fits at zoom 2). **ActionIndicator:** the Select tag (#6). **Danger count:** shown as the Down tag's number (enemies in view). **Compass:** keep (map effect). **KeyDisplay:** in the status pane's key count (#2). **Guidebook toasts** that say "tap" need watch wording (patch `Document`/`GLog` hint strings, or hide the guidebook hint GLogs). **Alchemy scene:** keep Shattered's at window zoom 3, check later. **Challenges:** read-only in the menu. |

## 5. Biggest decisions, with recommendations

- **D1. Interface zoom.** Recommendation: zoom 2 for the HUD and our own windows, a window camera at zoom 3 for Shattered windows we keep, and the map at its own zoom. This is the only way to get Pebble PD's exact pixels (1x, plus 2x icons and portrait, plus the 1.5x status art as PD itself does). Zoom 3 everywhere can never match PD, because every 2x PD icon would be 3x and every 1x line 1.5x. The window-camera split is small: `Window` creates its own camera from `PixelScene.defaultZoom`; patch it to use `pebxelWindowZoom` (3) when set.
- **D2. Use Pebble PD's art files for the HUD and chrome, not Shattered's.** Recommendation: yes. "Exactly like Pebble PD" includes PD's status pane, toolbar frame, tags and menu icons. They are already converted to Pebble colours (`watch/resources/images`), so the watch shows them unchanged. Shattered's sprites, items, mobs and tiles stay, because the game content is Shattered's.
- **D3. Build our own watch UI as Java (noosa) components rather than C screens on the watch.** Recommendation: Java on the phone, mirrored by the link. One code path then serves Emery and Gabbro, input (buttons and touch) stays in one place, and the watch stays a thin renderer. The cost is a few link features: outlined text (#5), and possibly a "flow along the circle" text flag for Gabbro.
- **D4. Quickslots: 2, exactly as PD.** Shattered keeps 6 in storage. Show slots 0 and 1, set `lastVisible = 2`, and disable `quickSwapper` on the watch. Items quickslotted on the phone into slots 3–6 stay there (invisible on the watch), which is harmless.
- **D5. Which Shattered windows to replace vs restyle.** Replace the ones a player uses every minute: HUD, bag, item, game menu, options, title, hero select. Restyle the rest through the chrome (#16) at window zoom 3. This keeps the work bounded; Shattered has 50+ windows.

## 6. Order of work

1. **Touch** (#20). Thomas reported it broken. It is small, separate from the rest, and lets Thomas use Shattered's own UI while we rebuild it.
2. **Target reticle fix** (#8). Small, and reported.
3. **Zoom split** (D1): interface zoom 2 plus window zoom 3 (patch `Window`, `PixelScene`).
4. **PD art in the pack**: copy `watch/resources/images/{status_pane,hp_bar,toolbar,tag_red,tag_white,tag_blue,icons,buffs,menu_icons,chrome,banner_*,play,avatars}.png` to `common/assets/pebxel/pd/`. Teach `pack_art.py` to pack them unconverted, and register them in `Assets`.
5. **WatchHud** (#2–#7): status pane, menu button, 6-button toolbar with 2 quickslots, 2-line outlined log (with the K_TEXT outline bit in the link and `draw.c`), and button tags. Hide Shattered's StatusPane, MenuPane, Toolbar, GameLog and tags from the link (add to `Link.skip` by class, or `visible=false` via patch). Update `Controls` so Back walks the new HUD's buttons (PD's order: toolbar 0–5, menu, portrait).
6. **Chrome like PD** (#16, the WELL).
7. **Game menu** (#9) and **options** (#14).
8. **Backpack** (#10) and **item window** (#11).
9. **Title** (#15) and **hero select** (#17); then rankings and death (#18, #19).
10. **Art readability** (#21): sprite shade merging; compare against Pebble PD side by side.
11. **Audit loop:** run `tour.sh` and `buttons.sh` after each step. Put each picture beside the matching Pebble PD screen, using the other fork's spec and emulator shots, until they match.

## 7. Carrying it to Gabbro (260x260 round)

- **Phone side.** The watch's hello already carries its message size. Add **its screen size and shape**. The phone then makes `WatchGraphics` 520x520 for Gabbro (2 device px per watch px). WatchHud picks the **round layout from Pebble PD's `PBL_ROUND` constants**:
  - the cap band with arcing HP and XP bars;
  - portrait and menu bubbles;
  - the six toolbar bubbles on `TBX/TBY`;
  - the tags as rim bubbles beside Up, Select and Down;
  - buffs down the left rim.
- **Arcs and bubbles.** These are not images. Add two link picture kinds: `K_ARC` (centre, radii, angles, colour) and `K_DISC` (bubble: edge, ring, face). The C side can lift them from Pebble PD's `arc()` and `bubble()`.
- **Windows.** Our own windows take PD's round geometry: boxes cut by a circle (`rpanel` R_OUT 127 / R_IN 121), and rows of cells as wide as the circle allows (`bag.c` `rcell`).
  - **Shattered windows we keep:** draw them inside a 180x200 box, centred, through a camera viewport. The link already sends camera viewports. Add a **circle clip flag** on cameras so the watch trims to R_IN.
  - **Text flowing along the curve:** a K_TEXT flag that asks the watch to lay the text out itself with `graphics_text_attributes_enable_screen_text_flow`.
- **Build.** Add `gabbro` to `watch/package.json` `targetPlatforms`. Art is shared, and the heap budget is similar.
