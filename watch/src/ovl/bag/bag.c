/*
 * Pixel Dungeon for Pebble - the backpack and item windows (WndBag, WndItem) (an overlay, loaded while one is open).
 * 0.18: both fill the screen with big cells and buttons (the watch is tiny): the backpack is a 5x5 grid (weapon,
 * armour, rings and gold on top; 19 slots; Exit in the corner) over its 5 tabs; the item window's actions are big
 * buttons two to a row. Layout and input live here; what frees this overlay is done by the app (CMD in ui_int.h).
 */
#include "pd.h"
#include "ovl.h"
#include "sheets.auto.h"
#include "ui_int.h"
#include "ui_tabs.h"
#include "item_sp.h"

// Keyring: each key held, floor by floor (iron keys, golden keys, the skeleton key); its sprite and depth
static int key_at(int k, int *depth) {
  for (int d = 1; d <= 26; d++) {
    int v = G.keys[d], n[3] = { v & 15, (v >> 4) & 3, (v >> 6) & 1 };
    for (int t = 0; t < 3; t++) for (int j = 0; j < n[t]; j++) if (k-- == 0) { *depth = d; return SP_IRON_KEY + t; }
  }
  return -1;
}

// ---------------- WndBag ----------------
#define HEAD 15
#ifdef PBL_ROUND
// 0.35 Gabbro: the backpack fills the round screen. Its cells (32 px) stand in rows as wide as the circle allows: the
// title on top, the gear and gold (5), the backpack's 19 slots (7, 7, 5), the bags' tabs (up to 5), Exit at the bottom
#define CW 32
#define CH 32
#define ROW_Y(r) (s_h / 2 - 115 + (r) * 33)
static GRect rcell(int row, int col, int n) { return GRect(s_w / 2 - (n * 33 - 1) / 2 + col * 33, ROW_Y(row), CW, CH); }
static GRect bag_cell(int i) {
  if (i < 5) return rcell(1, i, 5);
  if (i < 19) return rcell(i < 12 ? 2 : 3, (i - 5) % 7, 7);
  return i < 24 ? rcell(4, i - 19, 5) : rcell(6, 0, 1);
}
static GRect bag_tab(int t) { return rcell(5, t, bag_ntabs()); }
#define tab_hit hit
#else
#define CW SLOT_W
#define CH SLOT_H
static GRect bag_win(void) {                        // the content (the frame is drawn 6 px around it); the tabs hang below
  int h = HEAD + BAG_ROWS * (SLOT_H + 1) - 1, tabs = bag_ntabs() ? TAB_H : 0;
  return GRect((s_w - BIG_W) / 2, (s_h - h - tabs) / 2 + 2, BIG_W, h);
}
static GRect bag_cell(int i) { GRect r = bag_win(); return GRect(r.origin.x + (i % BAG_COLS) * (SLOT_W + 1), r.origin.y + HEAD + (i / BAG_COLS) * (SLOT_H + 1), SLOT_W, SLOT_H); }
static GRect bag_tab(int t) { GRect r = bag_win(); return GRect(r.origin.x - 4 + t * (TAB_W + 1), r.origin.y + r.size.h, TAB_W, TAB_H); }
#endif
// 0.42 gloves on: the buttons pick a row first (s_sel = BAG_ROWM + a cell in it: a wide box), then a cell in it
static GRect ucell(int i) { return i < BAG_UI_TAB0 ? bag_cell(i) : bag_tab(i - BAG_UI_TAB0); }   // a cell or a tab
static int s_move;                                   // 0.28: the cell whose item is being moved, + 1 (0: none)
static bool nav_ok(int i) {                        // 0.42: the buttons skip empty cells (unless an item is being moved there)
  Item *it;
  return bag_selectable(i) && (s_move || i >= BAG_EXIT || ((it = bag_item(i)) && it->kind));
}
// 0.70 (owner: "borrow from Shattered PD"): a known potion or scroll shows Shattered PD's own icon for it (item_icons.png,
// 8x8, potions then scrolls in IK order, each against its cell's right edge, built by tools/convert_art.py), bare, in the top
// right corner beside the bottle's neck; an unknown one an orange "?" there. (The top left is always the count.)
_Static_assert(IK_POT_FROST - IK_POT_HEALING == 11 && IK_SCR_ENCHANT - IK_SCR_IDENTIFY == 11, "potion / scroll kinds in order");
static int known_icon(int k) {
  if (k >= IK_POT_HEALING && k <= IK_POT_FROST) return k - IK_POT_HEALING;
  if (k >= IK_SCR_IDENTIFY && k <= IK_SCR_ENCHANT) return 12 + k - IK_SCR_IDENTIFY;
  return -1;
}
#ifndef PBL_ROUND
static void tab_up(int x, int w, int ct, int h) {     // 0.69 (owner): the title's tab on top of the window: tab_art (a chosen tab) upside down,
  uint8_t L = ARGB(GColorLightGray), D = ARGB(GColorDarkGray), K = ARGB(GColorBlack);   // its foot open into the frame (ct: the content's top)
  #define F(xx, yy, ww, hh, c) blit_fill(xx, ct - (yy) - (hh), ww, hh, c)
  F(x + 4, 0, w - 8, 4, D); F(x + 4, 0, w - 8, h - 4, D);
  F(x, 1, 1, h - 2, L); F(x + 1, 2, 2, h - 2, D); F(x + 3, 0, 1, h - 4, K);
  F(x + w - 4, 0, 1, h - 4, K); F(x + w - 3, 1, 1, h - 3, L); F(x + w - 2, 2, 2, h - 2, D);
  F(x, h - 1, 1, 1, D); F(x + w - 3, h - 2, 1, 2, D);
  F(x + 3, h - 4, w - 6, 1, K); F(x + 3, h - 3, w - 5, 1, L); F(x + 1, h - 2, w - 4, 2, D);
  #undef F
}
#endif
static void sprite2(const Item *it, int x, int y) { g_blit_glow = item_glow(it); item_spr(item_sprite(it->kind), x, y, 2); g_blit_glow = 0; }   // ItemSprite at 2x
static void draw_bag(GContext *ctx) {
  GRect r = PBL_IF_ROUND_ELSE(GRect(s_w / 2 - 80, ROW_Y(0) + 2, 160, 30), bag_win());   // Gabbro: the title's place
  int ntabs = bag_ntabs(), kind = bag_tab_kind(s_bag_tab);   // a selector shows tabs only once there are bags
  static const char *PICK_TITLES[12] = { "Select an item to identify", "Select an item to upgrade", "Select a weapon to upgrade", "Select an enchantable item", "", "Select an item for the quickslot", "Select an item to sell", "Select a seed to throw", "Select an item to reforge", "Select an armor to upgrade", "Select a weapon to balance", "Select a wand to upgrade" };
  static const char *const TAB_TITLES[5] = { "Backpack", "Seed pouch", "Scroll holder", "Wand holster", "Keyring" };
  const char *title = s_pick >= 0 ? PICK_TITLES[s_pick] : TAB_TITLES[kind];
  Sheet ii = { gbitmap_create_with_resource(RESOURCE_ID_IMAGE_ITEM_ICONS) };   // 1.5 KB, only while the bag draws
  blit_begin(ctx);
#ifdef PBL_ROUND
  blit_fog(0, 0, s_w);                             // 0.85 (owner: "the same look" as Emery): the game behind in the shadow
  {                                                  // 0.86 (owner: "follow the overall shape of the display ... the same vibe as the settings
    int y0 = ROW_Y(0) + (s_pick >= 0 ? -4 : 4), y1 = ROW_Y(6) + CH + 7;   // screens"): their round window (rpanel) out to the rim, so
    for (int y = y0; y < y1; y++) {                  // the widest rows' corners stay inside: light edge, grey, a black line, the grey body
      bool edge = y == y0 || y == y1 - 1;
      rrow(0, s_w, y, 130, ARGB(GColorLightGray));
      if (!edge) rrow(1, s_w - 1, y, 128, ARGB(GColorDarkGray));
      if (y >= y0 + 5 && y < y1 - 5) rrow(0, s_w, y, 125, ARGB(GColorBlack));
      if (y >= y0 + 6 && y < y1 - 6) rrow(0, s_w, y, 124, ARGB(GColorDarkGray));
    }
    for (int row = 1; row <= 6; row++) { int n = row == 5 ? ntabs : row == 6 ? 1 : row == 2 || row == 3 ? 7 : 5; GRect q = rcell(row, 0, n);
      if (n) blit_fill(q.origin.x - 2, q.origin.y - 2, n * 33 + 3, CH + 4, ARGB(GColorBlack)); }
  }
  for (int t = 0; t < ntabs; t++) { GRect q = bag_tab(t); bool sel = t == s_bag_tab; blit_fill(q.origin.x, q.origin.y, CW, CH, sel ? ARGB(GColorLightGray) : ARGB(GColorDarkGray)); if (sel) blit_box(q.origin.x + 2, q.origin.y + 2, CW - 4, CH - 4, 1, ARGB(GColorDarkGray)); }
#else
  blit_fog(0, 0, s_w > s_h ? s_w : s_h);           // 0.70 (owner): the game behind the window in the unexplored-cells shadow
  tab_panel(GRect(r.origin.x, r.origin.y + HEAD, r.size.w, r.size.h - HEAD));   // 0.69 (owner): the frame round the cells only, the title on a tab above it
  for (int t = 0; t < ntabs; t++) tab_art(bag_tab(t), t == s_bag_tab);
  { int tw = s_pick >= 0 ? r.size.w - 12 : graphics_text_layout_get_content_size(title, s_font_hud, GRect(0, 0, r.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter).w + 16;
    tab_up(r.origin.x + (r.size.w - tw) / 2, tw, r.origin.y + HEAD, 22); }
#endif
  for (int t = 0; t < ntabs; t++) {                // BagTab icons, at 2x
    GRect tr = bag_tab(t); bool sel = t == s_bag_tab;
    static const uint8_t ICON[5] = { 0, 2, 3, 4, 1 };   // backpack, pouch, holder, holster, keyring in icons.png
#ifdef PBL_ROUND
    blit(&s_icons, 17 + ICON[bag_tab_kind(t)] * 10, 0, 10, 10, tr.origin.x + 6, tr.origin.y + 6, 2);
    if (s_sel == BAG_UI_TAB0 + t) blit_box(tr.origin.x, tr.origin.y, CW, CH, 2, ARGB(GColorChromeYellow));
    (void)sel;
  }
#else
    blit(&s_icons, 17 + ICON[bag_tab_kind(t)] * 10, 0, 10, 10, tr.origin.x + (tr.size.w - 20) / 2, tr.origin.y + (sel ? 3 : 5), 2);
    if (s_sel == BAG_UI_TAB0 + t) blit_box(tr.origin.x + 4, tr.origin.y + (sel ? 1 : 5), tr.size.w - 8, tr.size.h - (sel ? 5 : 9), 1, ARGB(GColorChromeYellow));
  }
  blit_fill(r.origin.x, r.origin.y + HEAD, r.size.w, BAG_ROWS * (SLOT_H + 1) - 1, ARGB(GColorBlack));   // 1 px gaps between the cells
#endif
  for (int i = 0; i < BAG_UI_TAB0; i++) {
    int sl = bag_ui_to_slot(i);
    GRect q = bag_cell(i);
    if (sl == SL_NONE) { blit_fill(q.origin.x, q.origin.y, q.size.w, q.size.h, ARGB(GColorDarkGray)); blit_fog(q.origin.x, q.origin.y, CW); continue; }
    Item *it = bag_item(i);
    bool has = it && it->kind != IK_NONE;
    if (has && (it->flags & IF_CURSED) && (it->flags & IF_CURSED_KNOWN))       // ItemButton: known curse tints the slot red
      blit_fill2(q.origin.x, q.origin.y, q.size.w, q.size.h, ARGB(GColorDarkGray), ARGB(GColorBulgarianRose));
    else blit_fill(q.origin.x, q.origin.y, q.size.w, q.size.h, ARGB(sl == SL_EXIT ? GColorDarkCandyAppleRed : GColorDarkGray));
    int cx = q.origin.x + (CW - 32) / 2, cy = q.origin.y + (CH - 32) / 2;
    if (has) {
      sprite2(it, cx, cy);
      if (item_upgradable(it->kind) && (it->flags & IF_LEVEL_KNOWN)) {          // durability: 3 pips, green then red (0.69: bigger, outlined)
        int mx = item_max_dur(it), nb = (3 * it->dur + mx / 2) / mx;
        blit_fill(q.origin.x + 1, q.origin.y + CH - 7, 16, 6, ARGB(GColorBlack));
        for (int k = 0; k < 3; k++) blit_fill(q.origin.x + 2 + k * 5, q.origin.y + CH - 6, 4, 4, k < nb ? ARGB(GColorGreen) : ARGB(GColorRed));
      }
      int ic = item_known(it->kind) ? known_icon(it->kind) : -1;
      if (ic >= 0 && ii.bmp) blit(&ii, ic * 8, 0, 8, 8, q.origin.x + CW - 18, q.origin.y + 2, 2);
    }
    else if (sl <= -1 && sl >= -4) ITEM_SPR(SP_PH_WEAPON + (i < 2 ? i : 2), cx, cy, 2);   // ghost placeholders for empty gear slots
    else if (sl == SL_GOLD) { Item gold = item_make(IK_GOLD, 1); sprite2(&gold, cx, cy); }
    else if (sl == SL_KEY) { int d, sp = key_at(i, &d); if (sp >= 0) ITEM_SPR(sp, cx, cy, 2); }
    else if (sl == SL_EXIT) blit(&s_micons, 8 * 16, 0, 16, 16, cx, cy, 2);       // Icons.EXIT
    if (s_pick >= 0 && !bag_selectable(i)) blit_fog(q.origin.x, q.origin.y, CW);
    if (i == s_sel) blit_box(q.origin.x, q.origin.y, q.size.w, q.size.h, 2, ARGB(GColorChromeYellow));
    if (i == s_move - 1) blit_box(q.origin.x + 2, q.origin.y + 2, q.size.w - 4, q.size.h - 4, 2, ARGB(GColorGreen));   // being moved
  }
  if (s_sel >= BAG_ROWM) {                         // 0.42: the chosen row, all of it
    int y = ucell(s_sel - BAG_ROWM).origin.y, x0 = 999, x1 = 0;
    for (int i = 0; i < BAG_UI_TAB0 + ntabs; i++) {
      GRect q = ucell(i);
      if (q.origin.y != y) continue;
      if (q.origin.x < x0) x0 = q.origin.x;
      if (q.origin.x + q.size.w > x1) x1 = q.origin.x + q.size.w;
    }
    blit_box(x0 - 1, y - 1, x1 - x0 + 2, ucell(s_sel - BAG_ROWM).size.h + 2, 2, ARGB(GColorChromeYellow));
  }
  blit_end(ctx);
  if (ii.bmp) { gbitmap_destroy(ii.bmp); blit_forget(); }
#ifdef PBL_ROUND
  graphics_context_set_text_color(ctx, GColorIcterine);   // two lines for the selectors' longer titles
  graphics_draw_text(ctx, title, s_pick >= 0 ? s_font_log : s_font_hud, s_pick >= 0 ? r : GRect(r.origin.x, r.origin.y + 8, r.size.w, 20), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
#else
  text(ctx, title, s_pick >= 0 ? s_font_log : s_font_hud, GColorIcterine, r.origin.x, r.origin.y - 3, r.size.w, GTextAlignmentCenter);
#endif
  char buf[12];
  for (int i = 0; i < BAG_UI_TAB0; i++) {          // ItemSlot labels: quantity / status top left, STR top right, level bottom right
    int sl = bag_ui_to_slot(i);
    Item *it = bag_item(i);
    GRect q = bag_cell(i);
    #define TL(...) do { snprintf(buf, sizeof buf, __VA_ARGS__); text_outlined(ctx, buf, s_font_hud, GColorWhite, q.origin.x + 2, q.origin.y - 4, CW - 2, GTextAlignmentLeft); } while (0)   // (0.69: Gothic 14 bold, was 9)
    int kd;
    if (sl == SL_GOLD) TL("%d", G.hero.gold);
    if (sl == SL_KEY && key_at(i, &kd) >= 0) TL("%d", kd);                       // Key.status: the depth it opens
    if (!it || !it->kind) continue;
    ItemCat cat = item_cat(it->kind);
    if (it->qty > 1) TL("%d", it->qty);
    if ((cat == CAT_POTION || cat == CAT_SCROLL) && !item_known(it->kind))     // 0.69: not yet known: an orange "?"
      text_outlined(ctx, "?", s_font_hud, GColorChromeYellow, q.origin.x, q.origin.y - 4, CW - 3, GTextAlignmentRight);
    if (it->kind == IK_DEW_VIAL) TL("%d/10", G.dew);                              // DewVial.status
    if (cat == CAT_WAND && (it->flags & IF_LEVEL_KNOWN)) {                          // Wand.status: charges / max
      if (it->flags & IF_CHARGE_KNOWN) TL("%d/%d", it->ench, wand_max(it)); else TL("?/%d", wand_max(it));
    }
    #undef TL
    if (cat == CAT_WEAPON || cat == CAT_ARMOR || cat == CAT_MISSILE) {          // top right: ":STR", or "N?" typical while unknown
      bool lk = (it->flags & IF_LEVEL_KNOWN) || cat == CAT_MISSILE;
      Item typ = *it; typ.lvl = 0;
      int str = lk ? item_str(it) : item_str(&typ);
      snprintf(buf, sizeof buf, lk ? ":%d" : "%d?", str);
      text_outlined(ctx, buf, s_font_hud, !lk ? GColorChromeYellow : str > G.hero.STR ? GColorSunsetOrange : GColorWhite, q.origin.x, q.origin.y - 4, CW - 1, GTextAlignmentRight);
    }
    int lvl = (it->flags & IF_LEVEL_KNOWN) ? it->lvl : 0;                    // bottom right: "+N" / "-N"
    if ((lvl || (it->flags & (IF_LEVEL_KNOWN | IF_CURSED | IF_CURSED_KNOWN)) == (IF_LEVEL_KNOWN | IF_CURSED | IF_CURSED_KNOWN)) && item_upgradable(it->kind)) {   // ItemSlot: a known curse shows "+0" too
      snprintf(buf, sizeof buf, "%+d", lvl);
      text_outlined(ctx, buf, s_font_hud, lvl > 0 ? (item_broken(it) ? GColorChromeYellow : GColorGreen) : GColorSunsetOrange, q.origin.x, q.origin.y + CH - 18, CW - 1, GTextAlignmentRight);
    }
  }
}
static bool bag_move(int i, bool hold) {            // press and hold picks an item up, the next press puts it there (swapping)
  bool ok = i < BAG_UI_TAB0 && bag_ui_to_slot(i) >= 0 && s_pick < 0;
  if (s_move) {
    if (ok && i != s_move - 1) { Item *a = bag_item(s_move - 1), *b = bag_item(i), t = *a; *a = *b; *b = t; s_sel = i; }
    s_move = 0; redraw(); return true;
  }
  if (!hold || !ok || !bag_item(i)->kind) return false;
  s_move = i + 1; s_sel = i; redraw(); return true;
}
static int bag_input(int ev, int x, int y) {
  int ntabs = bag_ntabs();
  if (ev == -3) { if (s_sel < BAG_ROWM) bag_move(s_sel, true); return 0; }   // long Select
  if (ev < 0) {                                    // a tap: a cell, a tab, or outside (closes, like Back)
    for (int i = 0; i < BAG_UI_TAB0; i++) if (bag_ui_to_slot(i) != SL_NONE && hit(bag_cell(i), x, y)) {
      if (bag_move(i, ev == -2)) return 0;
      s_sel = i; return i == BAG_EXIT ? CMD(C_BAG_CLOSE, 0) : CMD(C_ITEM, i);
    }
    for (int t = 0; t < ntabs; t++) if (tab_hit(bag_tab(t), x, y)) { s_bag_tab = t; s_move = 0; redraw(); return 0; }
#ifndef PBL_ROUND                                    // (Gabbro: the backpack is the whole screen)
    GRect r = bag_win();
    if (!hit(GRect(r.origin.x - 6, r.origin.y - 6, r.size.w + 12, r.size.h + 12), x, y)) return CMD(C_BAG_CLOSE, 0);
#endif
    return 0;
  }
  int n = BAG_UI_TAB0 + ntabs, dir = ev == BUTTON_ID_DOWN ? 1 : -1, row = s_sel >= BAG_ROWM, c = s_sel - row * BAG_ROWM;
  int ry = ucell(c).origin.y, best = -1, bd = 0, cnt = 0;
  if (ev == BUTTON_ID_BACK) {                      // a cell: back to its row (an item being moved goes along); a row: drop
    if (!row) s_sel += BAG_ROWM;                   // the move, else close (as before)
    else if (s_move) s_move = 0;
    else return CMD(C_BAG_CLOSE, 0);
    redraw(); return 0;
  }
  if (row) {                                       // rows: Up / Down the row above / below (round the window), Select goes in
    for (int i = 0; i < n; i++) {
      if (!nav_ok(i)) continue;
      int d = (ucell(i).origin.y - ry) * dir;
      if (ev == BUTTON_ID_SELECT) { if (!d) { if (!cnt++) best = i; } continue; }
      if (d <= 0) d += 1000;
      if (best < 0 || d < bd) { best = i; bd = d; }
    }
    if (best < 0) return 0;
    if (ev != BUTTON_ID_SELECT) { s_sel = BAG_ROWM + best; redraw(); return 0; }
    if (nav_ok(c)) best = c;               // back into a row: where it was left
    s_sel = best;
    if (cnt > 1) { redraw(); return 0; }           // a row of one (Exit): pressed at once
  }
  if (ev == BUTTON_ID_SELECT) {
    if (s_move) { bag_move(s_sel, false); return 0; }
    if (s_sel >= BAG_UI_TAB0) { s_bag_tab = s_sel - BAG_UI_TAB0; redraw(); return 0; }
    return s_sel == BAG_EXIT ? CMD(C_BAG_CLOSE, 0) : CMD(C_ITEM, s_sel);
  }
  for (int k = 1; k < n; k++) {                    // a cell: Up / Down the one before / after in its row (round it)
    int j = (c + dir * k + n) % n;
    if (nav_ok(j) && ucell(j).origin.y == ry) { s_sel = j; break; }
  }
  redraw(); return 0;
}

// ---------------- WndItem: the icon and name, the description (the app's scrolling text layer), big action buttons ----------------
#ifdef PBL_ROUND
// 0.35 Gabbro: laid out here (the app only shows the text): wider, the text flowed along the curve, the buttons' rows
// narrowing with the circle
static GRect r_item_rect(void) {
  int w = 216, rows = item_rows() * (SLOT_H + 2);
  char name[40]; item_name(game_slot(s_item_slot), name, sizeof name); cap_first(name);
  int nh = graphics_text_layout_get_content_size(name, s_font_hud, GRect(0, 0, 124, 60), GTextOverflowModeWordWrap, GTextAlignmentLeft).h + 6;
  s_item_head = nh > 36 ? nh : 36;
  int th = r_flow_h(s_text_buf, GRect((s_w - w) / 2 + 5, s_h / 2 - 50, w - 10, 2000)) + 4, hmax = 200 - s_item_head - 2 - rows;
  s_item_text_h = th < hmax ? th : hmax;
  int h = s_item_head + s_item_text_h + 2 + rows;
  return GRect((s_w - w) / 2, (s_h - h) / 2, w, h);
}
#define item_rect r_item_rect
#endif
static GRect act_rect(int i) {
  GRect r = item_rect();
  int y0 = r.origin.y + r.size.h - item_rows() * (SLOT_H + 2), half = (r.size.w - 2) / 2;
  bool wide = (s_n_acts & 1) && i == s_n_acts - 1;   // an odd last one spans the row
  GRect b = GRect(r.origin.x + (wide ? 0 : (i & 1) * (half + 2)), y0 + (i / 2) * (SLOT_H + 2), wide ? r.size.w : half, SLOT_H);
  return PBL_IF_ROUND_ELSE(rfit(b), b);
}
static void draw_item(GContext *ctx) {
  GRect r = item_rect();
  Item *it = game_slot(s_item_slot);
#ifdef PBL_ROUND
  r_text_place(GRect(r.origin.x + 5, r.origin.y + s_item_head, r.size.w - 10, s_item_text_h), s_text_buf);
#endif
  blit_begin(ctx); blit_fog(0, 0, s_w > s_h ? s_w : s_h);   // 0.82 (owner): the game behind in the backpack's shadow
  panel(r);
  WELL(r.origin.x, r.origin.y + s_item_head - 2, r.size.w, r.size.h - s_item_head + 2);   // the text and the buttons
#ifdef PBL_ROUND
  { int lx = rleft(r) + 4; r.size.w -= lx - r.origin.x; r.origin.x = lx; }   // the icon and name: inside the curve
#endif
  sprite2(it, r.origin.x, r.origin.y + 1);
  for (int i = 0; i < s_n_acts; i++) red_button(act_rect(i), i == s_sel);
  blit_end(ctx);
  char name[40]; item_name(it, name, sizeof name);
  cap_first(name);                                   // WndItem: Utils.capitalize
  text_wrap(ctx, name, s_font_hud, GColorPastelYellow, GRect(r.origin.x + 36, r.origin.y + 1, r.size.w - 36 - PBL_IF_ROUND_ELSE(r.origin.x - (s_w - 216) / 2, 0), s_item_head));
  for (int i = 0; i < s_n_acts; i++) {
    static const char *const SPECIAL[4] = { "Heroic leap", "Smoke bomb", "Molten earth", "Spectral blades" };   // ClassArmor.special()
    const char *an = s_acts[i] == ACN_SPECIAL && CLASS_ARMOR(G.hero.armor.kind) ? SPECIAL[G.hero.armor.kind - IK_ARMOR_WARRIOR] : ACTION_NAMES[s_acts[i]];
    big_label(ctx, an, act_rect(i), GColorWhite);
  }
}
static int item_input(int ev, int x, int y) {
  if (ev == BUTTON_ID_BACK) {                      // back to the backpack, on this item's cell (0.42)
    for (int i = 0; i < BAG_UI_TAB0; i++) if (bag_ui_to_slot(i) == s_item_slot) s_sel = i;
    s_scene = SC_BAG; redraw(); return 0;
  }
  if (ev == BUTTON_ID_SELECT) return s_n_acts ? CMD(C_DO, s_sel) : 0;
  if (ev >= 0) { if (s_n_acts) { s_sel = (s_sel + (ev == BUTTON_ID_DOWN ? 1 : -1) + s_n_acts) % s_n_acts; redraw(); } return 0; }
  GRect r = item_rect();
  for (int i = 0; i < s_n_acts; i++) if (hit(act_rect(i), x, y)) return CMD(C_DO, i);
  if (hit(GRect(r.origin.x, r.origin.y + s_item_head, r.size.w, s_item_text_h), x, y)) return CMD(C_PAGE, 0);   // the text: next page
  if (!hit(r, x, y)) { s_scene = SC_BAG; redraw(); }                               // outside: back to the backpack
  return 0;
}

static void ui_draw(int sc, GContext *ctx) { if (sc == SC_BAG) draw_bag(ctx); else draw_item(ctx); }
static int ui_input(int ev, int x, int y) { return s_scene == SC_BAG ? bag_input(ev, x, y) : item_input(ev, x, y); }

OVL_ENTRY(bag, (void *)ui_draw, (void *)ui_input);
