/*
 * Pixel Dungeon for Pebble - the hero's windows: WndHero (stats, buffs), WndCatalogus, WndJournal (an overlay, loaded while one is open).
 * 0.14: the in-game windows are three overlays (uiw, bag, sheet) so each fits a fragmented heap.
 * Their layout helpers and most input handling stay in main.c (shared through ui_int.h).
 */
#include "pd.h"
#include "ovl.h"
#include "sheets.auto.h"
#include "ui_int.h"
#include "ui_tabs.h"
#include "items_int.h"
#include "item_sp.h"

static const char *BUFF_NAMES[BI_COUNT] = { "Poisoned", "Hungry", "Starving", "Crippled", "Bleeding", "Burning", "Paralysed",
  "Levitating", "Mind vision", "Invisible", "Frozen", "Blinded", "Rooted", "Slowed", "Barkskin", "Immune to gases",
  "Charmed", "Herbal armor", "Defered damage", "Caustic ooze", "Vertigo", "Weakened", "Herbal healing", "Amok", "Terror",
  "Illuminated", "Combo", "Fury", "Shadowmelded", "Enraged" };   // Buff.toString (PD spelling)
static const char *CLASS_NAMES[4] = { "WARRIOR", "MAGE", "ROGUE", "HUNTRESS" };

static GRect sheet_rect(void) { int w = PBL_IF_ROUND_ELSE(166, 172), h = 150; return GRect((s_w - w) / 2, (s_h - h - 26) / 2, w, h); }
#ifdef PBL_ROUND                                      // 0.35 Gabbro: buttons inside the curve, the tabs centred under it
static GRect sheet_button(int b) { GRect r = sheet_rect(); int w = r.size.w / 2 - 2; return rfit(GRect(r.origin.x + b * (w + 4), r.origin.y + 18, w, 28)); }
static GRect sheet_tab_rect(int t) { GRect r = sheet_rect(); return GRect(s_w / 2 - SHEET_TAB_W - 1 + t * (SHEET_TAB_W + 1), r.origin.y + r.size.h, SHEET_TAB_W, SHEET_TAB_H); }
#else
static GRect sheet_button(int b) { GRect r = sheet_rect(); int w = r.size.w / 2 - 2; return GRect(r.origin.x + b * (w + 4), r.origin.y + 18, w, 28); }   // WndHero: Catalogue, Journal
static GRect sheet_tab_rect(int t) { GRect r = sheet_rect(); return GRect(r.origin.x - 4 + t * (SHEET_TAB_W + 1), r.origin.y + r.size.h, SHEET_TAB_W, SHEET_TAB_H); }
#endif
static void draw_sheet(GContext *ctx) {           // PD WndHero: Stats and Buffs tabs
  Hero *h = &G.hero;
  GRect r = sheet_rect();
  static const char *TABS[2] = { "Stats", "Buffs" };
  uint8_t b[BI_COUNT]; int nb = hero_buffs(b);
  int col_w = r.size.w / 2;                       // more than five: two columns
  #define BUFF_X(i) (r.origin.x + 2 + (nb > 5 ? ((i) / 5) * col_w : 0))
  #define BUFF_Y(i) (r.origin.y + 4 + ((i) % 5) * 20)
  if (nb > 10) nb = 10;
  blit_begin(ctx);
  tab_panel(r);
  WELL(r.origin.x, r.origin.y + (s_sheet_tab ? 0 : 14), r.size.w, r.size.h - (s_sheet_tab ? 0 : 14));   // stats: under the title
  for (int t = 0; t < 2; t++) tab_art(sheet_tab_rect(t), t == s_sheet_tab);
  if (!s_sheet_tab) for (int i = 0; i < 2; i++) red_button(sheet_button(i), false);   // WndHero: Catalogus, Journal
  if (s_sheet_tab == 1)
    for (int i = 0; i < nb; i++) blit(&s_buffs, b[i] * 7, 0, 7, 7, BUFF_X(i) + 1, BUFF_Y(i) + 1, 2);   // 0.20: the status bar's icons at 2x (the 16 px sheet took 7.5 KB)
  blit_end(ctx);
  for (int t = 0; t < 2; t++) {
    GRect tr = sheet_tab_rect(t);
    text(ctx, TABS[t], s_font_hud, t == s_sheet_tab ? GColorWhite : GColorLightGray, tr.origin.x, tr.origin.y + (t == s_sheet_tab ? 0 : 3), tr.size.w, GTextAlignmentCenter);
  }
  if (s_sheet_tab == 1) {
    char dname[28];
    for (int i = 0; i < nb; i++)
      text(ctx, b[i] == BI_DEFERRED ? (snprintf(dname, sizeof dname, "Defered damage (%d)", G.deferred), dname) : BUFF_NAMES[b[i]], nb > 5 ? s_font_log : s_font_hud, GColorWhite, BUFF_X(i) + 20, BUFF_Y(i) - 1, (nb > 5 ? col_w : r.size.w) - 22, GTextAlignmentLeft);
    if (!nb) text(ctx, "No effects", s_font_log, GColorLightGray, r.origin.x, r.origin.y + 4, r.size.w, GTextAlignmentCenter);
    return;
  }
  char buf[32];
  static const char *const SUB[9] = { NULL, "GLADIATOR", "BERSERKER", "WARLOCK", "BATTLEMAGE", "ASSASSIN", "FREERUNNER", "SNIPER", "WARDEN" };
  snprintf(buf, sizeof buf, "LEVEL %d %s", h->lvl, h->subclass && h->subclass < 9 ? SUB[h->subclass] : CLASS_NAMES[h->hclass & 3]);   // WndHero: hero.className()
  text(ctx, buf, s_font_hud, GColorIcterine, r.origin.x, r.origin.y - 3, r.size.w, PBL_IF_ROUND_ELSE(GTextAlignmentCenter, GTextAlignmentLeft));
  for (int i = 0; i < 2; i++) { GRect bb = sheet_button(i); text(ctx, i ? "Journal" : "Catalogue", s_font_hud, GColorWhite, bb.origin.x, bb.origin.y + 4, bb.size.w, GTextAlignmentCenter); }
  const char *labels[5] = { "Strength", "Health", "Experience", "Gold Collected", "Maximum Depth" };
  char vals[5][16];
  snprintf(vals[0], 16, "%d", h->weakness ? h->STR - 2 : h->STR);   // hero.STR(): Weakness takes 2
  snprintf(vals[1], 16, "%d/%d", h->HP > 0 ? h->HP : 0, h->HT);
  snprintf(vals[2], 16, "%d/%d", h->exp, hero_max_exp());
  uint32_t gc; memcpy(&gc, h->spare + 2, 4);       // Statistics.goldCollected
  snprintf(vals[3], 16, "%d", (int)gc);
  snprintf(vals[4], 16, "%d", G.max_depth);
  for (int i = 0; i < 5; i++) {
    int yy = r.origin.y + 52 + i * 18 + (i >= 3 ? 6 : 0);
    text(ctx, labels[i], s_font_log, GColorWhite, r.origin.x, yy, r.size.w * 65 / 100, GTextAlignmentLeft);
    text(ctx, vals[i], s_font_log, GColorWhite, r.origin.x + r.size.w * 65 / 100, yy, r.size.w * 35 / 100, GTextAlignmentLeft);
  }
}

// ---------------- WndCatalogus (tabs: Potions, Scrolls) and WndJournal (s_sheet_tab 2) ----------------
static const char *JOURNAL_NAMES[J_COUNT] = { "Well of Health", "Well of Awareness", "Well of Transmutation", "Sacrificial chamber",
  "Alchemy pot", "Garden", "Animated statue", "Sad ghost", "Old wandmaker", "Troll blacksmith", "Ambitious imp" };   // Journal.Feature
#define CAT_ROWS 6
#define J_ROWS 7
static GRect cat_rect(void);
#ifdef PBL_ROUND                                      // 0.35 Gabbro: the rows narrow with the curve, the tabs centred under it
static GRect cat_tab_rect(int t) { GRect r = cat_rect(); return GRect(s_w / 2 - SHEET_TAB_W - 12 + t * (SHEET_TAB_W + 12), r.origin.y + r.size.h, SHEET_TAB_W + 11, SHEET_TAB_H); }
static GRect cat_cell(int i) { GRect r = cat_rect(); int w = r.size.w / 2; return rfit(GRect(r.origin.x + (i / CAT_ROWS) * w, r.origin.y + 22 + (i % CAT_ROWS) * 22, w, 22)); }
#else
static GRect cat_tab_rect(int t) { GRect r = cat_rect(); return GRect(r.origin.x - 4 + t * (SHEET_TAB_W + 12), r.origin.y + r.size.h, SHEET_TAB_W + 11, SHEET_TAB_H); }
static GRect cat_cell(int i) { GRect r = cat_rect(); int w = r.size.w / 2; return GRect(r.origin.x + (i / CAT_ROWS) * w, r.origin.y + 22 + (i % CAT_ROWS) * 22, w, 22); }
#endif
static int cat_kinds(int *out) {                   // Potion / Scroll .getKnown(), then .getUnknown()
  int first = s_sheet_tab ? IK_FIRST_SCROLL : IK_FIRST_POTION, n = 0;
  for (int pass = 1; pass >= 0; pass--) for (int k = first; k < first + 12; k++) if (item_known(k) == pass) out[n++] = k;
  return n;
}
static int journal_rows(int8_t *f, int8_t *d) {    // Journal.records, deepest first
  int n = 0;
  for (int dd = 26; dd >= 1; dd--) for (int k = 0; k < J_COUNT; k++) if (G.journal[k] >> dd & 1) { f[n] = k; d[n] = dd; if (++n == 32) return n; }
  return n;
}
static GRect cat_rect(void) {                      // WndJournal grows with its records (up to J_ROWS, then it scrolls)
  int w = s_w - PBL_IF_ROUND_ELSE(60, 12), h = 24 + CAT_ROWS * 22;
  if (s_sheet_tab == 2) { int8_t f[32], d[32]; int n = journal_rows(f, d); h = 24 + (n < 1 ? 1 : n > J_ROWS ? J_ROWS : n) * 18; }
  return GRect((s_w - w) / 2, (s_h - h - (s_sheet_tab == 2 ? 0 : SHEET_TAB_H)) / 2, w, h);
}
static void draw_catalog(GContext *ctx) {
  GRect r = cat_rect();
  bool jr = s_sheet_tab == 2;
  int kinds[12]; int8_t jf[32], jd[32]; int n = jr ? journal_rows(jf, jd) : cat_kinds(kinds);
  if (s_sel > n - J_ROWS) s_sel = n > J_ROWS ? n - J_ROWS : 0;
  blit_begin(ctx);
  if (jr) panel(r); else tab_panel(r);
  WELL(r.origin.x, r.origin.y + 18, r.size.w, r.size.h - 18);
  if (!jr) {
    for (int t = 0; t < 2; t++) tab_art(cat_tab_rect(t), t == s_sheet_tab);
    for (int i = 0; i < 12; i++) {                 // unknown: the dark "something" sprite, on a grey square so it shows on the black
      GRect c = cat_cell(i); bool k = item_known(kinds[i]);
      if (!k) blit_fill(c.origin.x, c.origin.y + 3, 14, 15, ARGB(GColorDarkGray));
      item_spr(k ? item_sprite(kinds[i]) : SP_SMTH, c.origin.x, c.origin.y + 3, 1);
    }
  }
  if (jr) for (int i = 0; i < J_ROWS && s_sel + i < n; i++) blit(&s_status, 87, 5, 9, 8, r.origin.x + r.size.w - 10, r.origin.y + 28 + i * 18, 1);
  blit_end(ctx);
  char t[32];
  snprintf(t, sizeof t, jr ? "Journal" : "Catalogue: %s", s_sheet_tab ? "Scrolls" : "Potions");
  text(ctx, t, s_font_hud, GColorPastelYellow, r.origin.x, r.origin.y - 2, r.size.w, GTextAlignmentCenter);
  if (jr) {
    if (!n) text(ctx, "No special places found yet.", s_font_log, GColorLightGray, r.origin.x, r.origin.y + 22, r.size.w, GTextAlignmentCenter);
    for (int i = 0; i < J_ROWS && s_sel + i < n; i++) {
      int y = r.origin.y + 22 + i * 18;
      text(ctx, JOURNAL_NAMES[jf[s_sel + i]], s_font_log, GColorWhite, r.origin.x + 2, y, r.size.w - 34, GTextAlignmentLeft);
      snprintf(t, sizeof t, "%d", jd[s_sel + i]);
      text(ctx, t, s_font_hud, GColorWhite, r.origin.x + r.size.w - 40, y - 1, 28, GTextAlignmentRight);
    }
    return;
  }
  for (int t2 = 0; t2 < 2; t2++) {
    GRect tr = cat_tab_rect(t2);
    text(ctx, t2 ? "Scrolls" : "Potions", s_font_hud, t2 == s_sheet_tab ? GColorWhite : GColorLightGray, tr.origin.x, tr.origin.y + (t2 == s_sheet_tab ? 0 : 3), tr.size.w, GTextAlignmentCenter);
  }
  for (int i = 0; i < 12; i++) {                  // the name without "Potion of" / "Scroll of": the title says which
    GRect c = cat_cell(i);
    const char *nm = txt(DEFS[kinds[i]].name), *of = nm;
    while (*of && (of[0] != ' ' || of[1] != 'o' || of[2] != 'f' || of[3] != ' ')) of++;   // (no strstr: it would join the app)
    text_wrap(ctx, *of ? of + 4 : nm, s_font_log, item_known(kinds[i]) ? GColorWhite : GColorLightGray, GRect(c.origin.x + 15, c.origin.y + 1, c.size.w - 15, 18));
  }
}
static int sheet_input(int ev, int x, int y) {      // WndHero (0.29: its input out of the app)
  if (ev < 0) {
    for (int t = 0; t < 2; t++) if (tab_hit(sheet_tab_rect(t), x, y)) { s_sheet_tab = t; redraw(); return 0; }
    for (int b = 0; b < 2 && !s_sheet_tab; b++) if (hit(sheet_button(b), x, y)) { s_scene = SC_CATALOG; s_sheet_tab = b * 2; s_sel = 0; redraw(); return 0; }
  } else if (ev != BUTTON_ID_SELECT && ev != BUTTON_ID_BACK) { s_sheet_tab ^= 1; redraw(); return 0; }
  s_scene = SC_GAME; redraw(); return 0;
}
static int ui_input(int ev, int x, int y) {         // the catalogue's taps (ev -1) and buttons
  if (s_scene == SC_SHEET) return sheet_input(ev, x, y);
  bool jr = s_sheet_tab == 2;
  if (ev == BUTTON_ID_SELECT || ev == BUTTON_ID_BACK) { s_scene = SC_GAME; redraw(); return 0; }
  if (ev >= 0) {                                   // Up / Down: the other tab; the journal scrolls
    if (jr) s_sel += ev == BUTTON_ID_DOWN ? 1 : -1; else s_sheet_tab ^= 1;
    if (s_sel < 0) s_sel = 0;
    redraw(); return 0;
  }
  if (!jr) {
    for (int t = 0; t < 2; t++) { GRect tr = cat_tab_rect(t); tr.size.h += 8; if (hit(tr, x, y)) { s_sheet_tab = t; redraw(); return 0; } }
    int kinds[12]; cat_kinds(kinds);
    for (int i = 0; i < 12; i++) if (hit(cat_cell(i), x, y)) {   // ListItem.onClick: what an identified one does
      if (!item_known(kinds[i])) return 0;
      Item it = item_make(kinds[i], 1);
      ui_message(txt(DEFS[it.kind].name), ""); item_info(&it, s_text_buf, TEXT_BUF);
      redraw(); return 0;
    }
  }
  if (!hit(cat_rect(), x, y)) { s_scene = SC_GAME; redraw(); }
  return 0;
}

static void ui_draw(int sc, GContext *ctx) { if (sc == SC_SHEET) draw_sheet(ctx); else draw_catalog(ctx); }

OVL_ENTRY(sheet, (void *)ui_draw, (void *)ui_input);
