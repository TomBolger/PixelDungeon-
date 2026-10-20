# Pebble PD UI spec: Gabbro (260×260, round)

This is the round counterpart of `pebble-pd-ui-spec.md`, which covers Emery.

**Sources:**
- Pebble PD v0.93 `PBL_ROUND` code: `watch/src/c/main.c` (layout around lines 255–400, HUD around 1395–1650) and `ui_tabs.h`.
- `watch/src/ovl/{ui,uiw,bag,sheet}/*.c`.
- Emulator screenshots in `pebble-pd-shots-gabbro/`.

**Conventions:**
- All numbers are watch pixels. The screen centre is (130,130).
- "Bearing" means the angle from straight up, clockwise positive.
- `arc()` takes the **tangent ×256** of a bearing. `tg(d10)` is tan(d10/10°)×256.
- Fonts, colours, art and the "2x" convention are as on Emery unless stated.

## 1. Round building blocks

### Bubble (`bubble(b, fill, ring)`, main.c:1399)
A bubble is a disc frame round a nominal square `b` (centre c, radius r). It is pushed outward, past the rim, so about ¾ of it shows.

**Growth.** With d = |c − (130,130)| (integer sqrt) and o = r/3:
- If d > 40, the centre moves outward by o·(dx,dy)/d, and r grows by r/3.
- The picture box (b itself) moves outward by (o−2)·(dx,dy)/d.

**Discs drawn:**
- r+1: black edge.
- r: the ring colour.
- r−2: the face.

**Placing pictures:** use `BPIC(b, w, h)`, i.e. centred in the moved box.

**Colours:**
- Plain bubbles: DarkGray face, LightGray ring.
- Red tags: DarkCandyAppleRed face, Orange ring.
- Blue tags: CobaltBlue face, White ring.
- Pale tags: LightGray face, White ring.

### Arc band (`arc(rin, rout, t0, t1, c)`)
For each row y = 1..rout above the centre, it fills the ring between radii rin and rout, cut to x ∈ [y·t0/256, y·t1/256] relative to the centre. In other words, it is the part of the ring between bearings atan(t0/256) and atan(t1/256) on the upper half.

`slant(r0, r1, e0, c)` stacks 1 px arcs from −55° to bearing e0 + 0.5° per px outward. This gives the shelf's slanted end.

### Round window panel (`rpanel(r, body)`, ui_tabs.h)
`rpanel` draws box r cut by circles. With `rrow(x0, x1, y, R, c)` = row y of the box, clipped to the circle of radius R round the centre:
- **Box:** x0 = r.x − 6, x1 = r.x + r.w + 6, y0 = r.y − 6, y1 = r.y + r.h + 6.
- **Rows y0..y1:**
  - LightGray, clipped to **R_OUT = 127**.
  - Not on the first or last row: DarkGray clipped to R_OUT − 2 = 125, on x0+1..x1−1.
  - For y ∈ [r.y − 1, r.y + r.h]: black clipped to R_IN + 1 = 122, on r.x − 1..r.x + r.w + 1.
  - For y ∈ [r.y, r.y + r.h): the body colour clipped to **R_IN = 121**.
- **Variants:**
  - `panel(r)`: body Black.
  - `tab_panel(r)`: body DarkGray.
  - `WELL(x,y,w,h)`: Black, filled with rows clipped to R_IN, on x−2..x+w+2.

**Fitting things inside:**
- `rfit(b)` narrows a button to the circle of radius R_IN − 2 = 119, at whichever of its top and bottom rows is farther from the centre.
- `rleft(r)` is where a window's top line can start: `max(r.x, 130 − half-chord(r.y + 2, R_IN − 3))`.

### Text flowing along the curve
`flow_set(f)` calls:
- `graphics_text_attributes_enable_screen_text_flow(attrs, 12)`, with a 12 px inset from the round edge;
- `graphics_text_attributes_enable_paging(attrs, f.origin, f)`.

Long texts (info, item description, options) are drawn into frame f with these attributes. The system shortens each line to the circle and pages it a whole frame at a time; Up/Down move a page. `flow_h(t, f)` measures the height at width f.w.

### Tabs (`tab_art`, round)
Tabs hang from the window's bottom edge. That edge, from inside out, is: black (cb), dark (cb+1..4), light (cb+5, outermost).
- **Every tab:** a DarkGray fill from `top` to `bot`, where bot = cb + h.
  - Light 1 px sides from cb+5, and a light bottom line.
  - Black 1 px inner sides and inner bottom, 3 px in from the edge.
- **Chosen tab:** top = cb, so it opens into the window.
- **Other tabs:** top = cb + 6, so their light line is their top.

### Behind every window
`blit_fog`: the whole screen gets a 50% black checker.

## 2. In-game HUD (`draw_hud_art` / `draw_hud_text`, `PBL_ROUND`)
Shots: `game-zoom16/32/8.png`, `game-combat.png`, `game-next-target.png`, `game-loot-tag.png`, `game-back-1..8.png`, `game-log.png`.

### Cap band over the top arc
Values are radii; the bearings follow each one.
1. **Strip:** `arc(125,131, −365, 305)` DarkGray, −55° to +50°.
2. **Shelf**, the light slanted end: `slant(115,125, HSE−10)` LightGray. HSE = 60, so it ends at 5.0° at r=115 and leans 0.5°/px outward.
3. Then `slant(115,125, HSE−20)` DarkGray, ending at 4.0°.
4. `arc(115,117, −365, 17)` DarkGray, −55° to 3.8°.
5. `arc(117,118, −365, 24)` LightGray, −55° to 5.4°.

**Health bar:** rows r = 119..124, each from tangent −166 (−33°) to `tg(f + (r−119)·5)`, where f = HB0 + (HBE−HB0)·HP/HT. HB0 = −29.0° and HBE = +5.5°, so each row out ends 0.5° later. Row colours:

| Row | Colour |
|---|---|
| r = 119 | BulgarianRose |
| r = 120..123 | DarkCandyAppleRed |
| r = 124 | RoseVale |

**Experience:** `arc(113,115, −166, tg(HB0 + (HSE−HB0)·exp/max))` in Icterine, under the health bar.

**Icons**, the status pane's at 1.5x:
- Stairs: (87,5,8×8) drawn 12×12 at (158,24).
- Key: (103,5,7×8) drawn 11×12 at (177,32).

**Text:** all GOTHIC_14_BOLD, outlined. The rectangles are text boxes (x, y top, w).

| Text | Box | Colour, alignment |
|---|---|---|
| HP "h/HT" | (86,24,50) | White, left |
| Depth | (133,18,24) | LightGray, right; ends at x=157, just before the stairs |
| Keys | (152,27,24) | LightGray, right |
| Level | (62,60,20) | White, centred, in the level bubble |

### Bubbles

| Item | Nominal BUB(cx,cy,r) | Drawn centre / R | Contents |
|---|---|---|---|
| Portrait (hl 7) | (52,49,26) | (47,44) / 34 | Hero frame 0 at 2x, centred in box (22,19,52) |
| Level | (72,70,9) | (70,68) / 12 | Level number |
| Menu button (hl 6) | (208,49,20) | (212,45) / 26, red | 3 white bars 20×3 at (200, 39+6i) |
| Toolbar, TB_R = 17 | centres TBX = {19,29,52,92,130,168}, TBY = {142,179,211,235,242,235} for wait, search, inspect, backpack, Q2, Q1 | (14,142) (25,181) (49,214) (91,239) (130,247) (169,239) / 22 | Icons at 2x centred; quickslot items 32×32; counts GOTHIC_14_BOLD at the bubble's bottom-right (y = TB_Y + 2·TB_R − 16) |
| Tag zoom (Up) | (238,87,13) | (241,86) / 17 | Red: magnifier from tag_red (16,1,12×12) plus Icterine pips |
| Tag act (Select) | (246,134,13) | (250,134) / 17 | Red with the weapon (attack); blue with the item; pale with the walk arrow (icons 83,0,12×11) |
| Tag next (Down) | (234,181,13) | (237,182) / 17 | Red: Icons.TARGET at 1x |
| Loot | (206,212,14) | (208,214) / 18, blue | Heap sprite at 2x |
| Resume | (206,212,14) alone; (180,184,14) when loot is up | (182,186) / 18, pale | Arrow at 2x |

**The toolbar:**
- Its six bubbles follow the bottom-left arc: wait at about 9 o'clock up to Q1 at about 5 o'clock.
- The toolbar stays visible, shaded, round the backpack.
- `toolbar_y()` = 190; the log ends above it.

**Buffs:**
- 7×7 icons at 2x, down the left rim from the portrait's edge.
- Top-lefts at (BX−7, BY−7), with BX = {29,24,20,45,39,36} and BY = {83,98,113,88,102,117}. The last three are a second, inner column.
- At most 6.

**Back highlight (`hl_rect`):**
- Each bubble's square is marked with `reticle` (Icons.TARGET corners, 12×12 each).
- Order with Assist off: 0..5 (toolbar), 6 (menu), 7 (portrait), then none.
- With Assist on, Back follows `g_hl_order` (the bot's best first). See `game-back-*.png`.

**Log:**
- At most 2 lines of GOTHIC_14, outlined.
- Box x = 68, width 124, centred: `mx = (260 − 124)/2`.
- Bottom at 190 − 4.

**Map:**
- Fills the whole circle, centred at (130,130).
- View radii per zoom (cells at least half on screen), as squared radii: 16 px → 60, 8 px → 240, 32 px → 15.

**Off-screen hero:** a bubble, 28 nominal, clamped to x/y ∈ [58, 260−58] and [58, 190−16].

**Tap zones:** on the top half, taps are ignored in the strip and shelf, where d ≥ 123, or d ≥ 110 with (x−130)·5 < 130−y.

## 3. Backpack (bag overlay, `PBL_ROUND`)
Shots: `bag-row.png`, `bag-cell*.png`, `bag-after-item.png`.

**Overall:** the backpack fills the screen; there is no rectangular window.
- **Frame:** rows from y0 = ROW_Y(0) + 4 (or −4 with a picker title) to WB:
  - LightGray clipped to radius 130;
  - DarkGray to 128 (not on the edge rows);
  - Black to 125 (rows y0+5..WB−5);
  - DarkGray to 124 (y0+6..WB−6).
- **Cells:** 32×32 at a 33 px pitch, in centred rows. `rcell(row, col, n)` = (130 − (33n−1)/2 + 33·col, ROW_Y(row)), where `ROW_Y(r) = 15 + 33r`.

| Row | y | Contents |
|---|---|---|
| 0 | 15 | Title "Backpack", GOTHIC_14_BOLD Icterine, centred in (50, ROW_Y(0)+2+8, 160, 20). Picker titles use GOTHIC_14 over 2 lines. |
| 1 | 48 | 5 cells: weapon, armour, ring, ring, gold |
| 2 | 81 | 7 slots |
| 3 | 114 | 7 slots |
| 4 | 147 | 5 slots (19 slots in all) |

- **Cell backing:** each row has a black backing round it: (x−2, y−2, 33n+3, 36). Cells are DarkGray; a known curse is dithered with BulgarianRose.
- **Window bottom:** WB = ROW_Y(4) + 32 + 9 = 188.
- **Tabs:** round bubbles hanging from WB, drawn with `hang(cx, TAB_CY = 200, r = 17)`.
  - Chosen tab: opens into the window's light edge.
  - Focused tab: ChromeYellow ring.
  - Icons: `icons.png` (17+10k,0,10,10) at 2x at (tab.x+6, tab.y+6).
  - Tab x: `TAB_X0 + 36t − 16`, where TAB_X0 = min(130 − 18(n−1), 184 − 36(n−1)).
- **Exit:** a red bubble at (213,213), r 22, with Icons.EXIT at 2x at (195,195). It sits about a quarter off the rim.
- **Cell labels:** as on Emery: quantity top-left, STR top-right, level bottom-right.
- **Buttons:** as on Emery (rows, then cells).

## 4. Item window (`r_item_rect`)
Shots: `item-window.png`, `item-window-next.png`.

**Size:** w = 216, x = 22. h = head + text + 2 + rows·36, centred vertically.

**Head:**
- At least 36 px; the name is GOTHIC_14_BOLD PastelYellow, wrapped at 124 wide.
- Sprite at 2x at (rleft(r)+4, y+1).
- The name at x+36 inside the curve.

**Description:**
- In the black WELL, flowed along the curve (`flow_set`) in frame (x+5, y+head, w−10, text_h).
- text_h ≤ 200 − head − 2 − rows; it pages.
- A small white triangle marks more.

**Actions:** 2 per row, half = (216−2)/2 = 107, 34 tall with a 2 px gap. An odd last one is full width. Each is `rfit`ted, so rows narrow with the circle. Labels are as on Emery.

**Frame:** `panel` (black body).

## 5. Info / mob window (`r_info_rect`)
Shot: `mob-info.png`.

- **Size:** w = 232, so the circle trims it. Height = head + flowed text + 8, capped at 260 − 64. Centred.
- **Head:** 38 px for a mob, 20 px with a title.
- **Title:** GOTHIC_14_BOLD PastelYellow from `rleft(r)`. Left-aligned for a mob, centred otherwise.
- **Mob:** sprite at 2x at the left. Health bar 4 px at y+22: DarkCandyAppleRed background, Red fill.
- **Body:** text flows along the curve. The well starts at 36 (mob) or 18 (titled).

## 6. Options (WndOptions etc.)
- **Size:** BIG_W 179, centred.
- **Frame:** `tab_panel`, with the title GOTHIC_14_BOLD Icterine, centred at y−4.
- **Text:** flowed in (x, y+14, 179, th), with th sized against y = 130−70.
- **Buttons:** 34 tall, stacked with a 2 px gap, or 2×89 side by side; each `rfit`.

## 7. Game menu (`uiw`)
Shots: `menu-game*.png`, `menu-records*.png`, `menu-settings*.png`.

**Window:**
- Width 179 at x = 40.
- h = 16 + 130, plus 20 for the Assistant row on Settings.
- Centred: y = (260 − h − 42)/2, with no +2 on Gabbro.
- Frame: `tab_panel`.

**Tiles:** 64 tall with a 2 px gap, `rfit`ted.
- 2 tiles: 179 wide each.
- 3–4 tiles: 89 wide in pairs (x+0 and x+90), an odd last one full width.

**Tabs:**
- 3 tabs, each 50×42, centred: x = 130 − 76 + 51t → 54, 105, 156.
- They hang under the window.
- Icons: `menu_icons` t·16 at 2x, y+4 when chosen, y+6 otherwise.

**Titles:** "Pixel Dungeon", "Catalogue & Journal", "Audio"; the Settings page adds an "Assistant" heading in a 1 px ChromeYellow frame. GOTHIC_14_BOLD Icterine.

**Tile contents:** as on Emery: icon at 2x at y+3, label GOTHIC_18_BOLD, the "Return" arrow drawn.

## 8. Hero sheet, catalogue, journal (`sheet`)
Shots: `hero-sheet.png`, `hero-sheet-buffs.png`, `catalogue*.png`, `journal.png`.

**Hero sheet:**
- 166×150, centred: x = 47, y = (260−150−26)/2 = 42.
- Title GOTHIC_14_BOLD Icterine, **centred** (Emery: left).
- Rows as on Emery.
- Tabs "Stats" / "Buffs" (50×26), centred under it at x = 130 − 51 + 51t (79, 130).

**Catalogue:**
- w = 260 − 60 = 200, h = 24 + 6·22. 2 columns of 22 px rows, each `rfit`ted.
- Tabs 61 wide, centred at x = 130 − 62 + 62t.
- Title "Catalogue: Potions/Scrolls" in PastelYellow, centred.

**Journal:**
- w = 200, h = 24 + n·18 (1..7 rows).
- A plain panel with the title "Journal".
- "No special places found yet." when empty.

## 9. Front screens (`ui`)
Shots: `title*.png`, `heroselect*.png`, `rankings.png`, `story.png`.

**Background:** arcs.png tiles, 64 px, over the whole screen.

**Title:**
- Logo 128×70 at (66,28).
- Play and Rankings 64×84 at (56,108) and (140,108), pictures at 2x. Labels GOTHIC_18_BOLD at y+62, in a box 20 px wider. Focus: white label plus a 2 px underline (x+12, y+84, 40×2).
- Sound corner: (14,115,30,30), chrome 9-slice (22,0,18,18,m5) with a drawn speaker. Focus: 1 px white box.
- Version: GOTHIC_14_BOLD LightGray, right-aligned to x = 150, at y = 220.

**Hero select:**
- Banner 128×21 at (66,24).
- Class cells 56×72 in a 2×2 grid: gx 14, x0 = (260−126)/2 = 67, y0 = 50, row pitch 76. Cells at (67,50), (137,50), (67,126), (137,126).
- Avatar 24×28 at 2x at (x+4, y). Class name GOTHIC_09 at y+56. Focus: a 3 px Yellow box at (x−1, y−4, 58, 74).
- New Game: 140×38 at (60, 260−38−22 = 200).
- With a save: Continue/Delete as 2 × 85×40, total 176, at y = 260−40−36 = 184.
- The class-perks window is w = 200; it falls back to GOTHIC_09 when the text is taller than 180.

**Rankings:**
- x0 = 34, w = 192, rows 26 px from y = 46.
- Heading "Top Rankings" GOTHIC_18_BOLD at y = 22.
- Empty: "No games have been played yet." GOTHIC_18_BOLD LightGray, centred at (44, 106, 172, 48).

**Story / floor intro:** the info window (§5) with no title; the text flows along the curve and pages.

**Erase confirmation:** 160×76, centred.

**Amulet:** text width 204, buttons 140 wide.

**Death:** there is no special round layout.
- Game-over or boss banner: `banner_gameover` 128×35 at ((260−128)/2, 130−50).
- Then any button opens the one-page menu: New Game, Rankings, Main Menu as 3 tiles.

## 10. Notes from the emulator run
- Gabbro's `pebble install` failed until the emulator's flash was reset. I restarted only the gabbro QEMU and pypkjs, and moved `~/.local/share/pebble-sdk/4.33.1/gabbro/qemu_spi_flash.bin` to `/tmp/gabbro_flash_old.bin`. It was then re-made from the SDK image, so earlier Gabbro installs are gone.
- After "Save & Main Menu", Play showed "Not enough memory to do that right now." over hero select (PD itself, Gabbro). Rankings were empty.
- Not shot, so taken from code above: options window, death screen, rankings with entries, the Continue/Delete variant, resume tag.
