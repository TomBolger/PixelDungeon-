/*
 * Pixel Dungeon for Pebble - the small in-game windows: game menu, info, options, game over (an overlay, loaded while one is open).
 * 0.14: the in-game windows are three overlays (uiw, bag, sheet) so each fits a fragmented heap.
 * 0.18: the game menu is tabbed pages of big picture buttons, and the options windows have big buttons; their layout and
 * input live here (what frees this overlay is done by the app: CMD in ui_int.h).
 */
#include "pd.h"
#include "ovl.h"
#include "sheets.auto.h"
#include "ui_int.h"
#include "ui_tabs.h"

static const char *OPT_TITLE[7] = { "Chasm", "Harmful potion!", "Beneficial potion", "Unequip one ring", "Quickslot", "", "Missile weapon" };
#ifdef PBL_ROUND
// 0.35 Gabbro: as wide as the screen (the circle trims it), as tall as its text flowed along the curve
static const char *r_desc(void) { return s_text_buf[0] ? s_text_buf : "Nothing special."; }
static int r_head(void) { return s_info_mob >= 0 ? 38 : s_info_title[0] ? 20 : 0; }
static GRect r_info_rect(void) {
  int w = 232, hd = r_head(), h = hd + r_flow_h(r_desc(), GRect((s_w - w) / 2, s_h / 2 - 30, w, 2000)) + 8;
  if (h > s_h - 64) h = s_h - 64;                   // the title needs room in the top of the curve
  return GRect((s_w - w) / 2, (s_h - h) / 2, w, h);
}
#endif
static void draw_info(GContext *ctx) {            // PD WndInfoCell / WndInfoMob / WndMessage
  GRect r = PBL_IF_ROUND_ELSE(r_info_rect(), info_rect());
  int w = r.size.w;
  bool titled = s_info_title[0];
#ifdef PBL_ROUND
  r_text_place(GRect(r.origin.x, r.origin.y + r_head() - 2, w, r.size.h - r_head() + 2), r_desc());
#endif
  blit_begin(ctx); blit_fog(0, 0, s_w > s_h ? s_w : s_h);   // 0.82 (owner): the game behind in the backpack's shadow
  panel(r);
  WELL(r.origin.x, r.origin.y + (s_info_mob >= 0 ? 36 : titled ? 18 : 0), w, r.size.h - (s_info_mob >= 0 ? 36 : titled ? 18 : 0));
  int tx = PBL_IF_ROUND_ELSE(rleft(r), r.origin.x);
#ifdef PBL_ROUND
  r.size.w -= 2 * (tx - r.origin.x); r.origin.x = tx; w = r.size.w;
#endif
  if (s_info_mob >= 0) {                           // mob sprite at 2x, then name and a health bar
    Mob *m = &G.mobs[s_info_mob];
    const CharArt *art = mob_art(m->type);
    blit(mob_sheet(m->type), 0, 0, art->fw, art->fh, r.origin.x, r.origin.y + 2, 2);
    tx += 2 * art->fw + 4;
    int bw = r.origin.x + w - tx, hw = bw * (m->HP > 0 ? m->HP : 0) / m->HT;
    blit_fill(tx, r.origin.y + 22, bw, 4, ARGB(GColorDarkCandyAppleRed));          // PD HealthBar
    blit_fill(tx, r.origin.y + 22, hw, 4, ARGB(GColorRed));
  }
  blit_end(ctx);
  if (titled) text(ctx, s_info_title, s_font_hud, GColorPastelYellow, tx, r.origin.y - 2, r.origin.x + w - tx, PBL_IF_ROUND_ELSE(s_info_mob >= 0 ? GTextAlignmentLeft : GTextAlignmentCenter, GTextAlignmentLeft));
}

// ---------------- PD WndOptions (Chasm.heroJump, the potion warnings, rings, quickslots, quests): big buttons ----------------
#define O_BTN 34
static const char *opt_text(void) { return s_opt == 5 ? s_wnd_text : txt(TX_OPT_CHASM + s_opt); }
static int o_n(void) { return s_opt == 5 && !s_wnd_b[1][0] ? 1 : 2; }   // a quest window may have one button
// the title, the text, then the buttons stacked (side by side when the text is long); *th: the text's height
static GRect o_win(int *th, bool *row) {
  int n = o_n(), stack = n * (O_BTN + 2) - 2, room = s_h - 16 - 18 - 4;
#ifdef PBL_ROUND                                      // 0.35 Gabbro: the text flows along the curve
  *th = flow_h(opt_text(), GRect((s_w - BIG_W) / 2, s_h / 2 - 70, BIG_W, 600)) + 4;
#else
  *th = graphics_text_layout_get_content_size(opt_text(), s_font_log, GRect(0, 0, BIG_W, 600), GTextOverflowModeWordWrap, GTextAlignmentLeft).h + 4;
#endif
  *row = n > 1 && *th + stack > room;
  int bh = *row ? O_BTN : stack;
  if (*th > room - bh) *th = room - bh;
  int h = 18 + *th + 4 + bh;
  return GRect((s_w - BIG_W) / 2, (s_h - h) / 2, BIG_W, h);
}
static GRect o_btn(int i) {
  int th; bool row; GRect r = o_win(&th, &row);
  int y0 = r.origin.y + 18 + th + 4;
  GRect b = row ? GRect(r.origin.x + i * 90, y0, 89, O_BTN) : GRect(r.origin.x, y0 + i * (O_BTN + 2), BIG_W, O_BTN);
  return PBL_IF_ROUND_ELSE(rfit(b), b);
}
static void draw_jump(GContext *ctx) {
  int th; bool row; GRect r = o_win(&th, &row);
  blit_begin(ctx); blit_fog(0, 0, s_w > s_h ? s_w : s_h);   // 0.82 (owner): the game behind in the backpack's shadow
  tab_panel(r);
  WELL(r.origin.x, r.origin.y + 16, r.size.w, r.size.h - 16);
  for (int i = 0; i < o_n(); i++) red_button(o_btn(i), s_sel == i);
  blit_end(ctx);
  text(ctx, s_opt == 5 ? s_wnd_title : OPT_TITLE[s_opt], s_font_hud, GColorIcterine, r.origin.x, r.origin.y - 4, r.size.w, GTextAlignmentCenter);
#ifdef PBL_ROUND
  { GRect tr = GRect(r.origin.x, r.origin.y + 14, r.size.w, th); flow_set(tr);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, opt_text(), s_font_log, tr, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, s_flow); }
#else
  text_wrap(ctx, opt_text(), s_font_log, GColorWhite, GRect(r.origin.x, r.origin.y + 14, r.size.w, th));
#endif
  static const char *opt[2] = { "Yes, I know what I'm doing", "No, I changed my mind" };
  for (int i = 0; i < o_n(); i++) {
    char rn[48];
    const char *label = opt[i];
    if (s_opt == 3) { item_name(&G.hero.ring[i], rn, sizeof rn); cap_first(rn); label = rn; }   // the two rings worn
    if (s_opt == 4) {                              // the quickslots, with what each holds now
      int k = G.hero.quick[i];
      if (k) { Item it = item_make(k, 1); char n[40]; item_name(&it, n, sizeof n); snprintf(rn, sizeof rn, "%d: %s", i + 1, n); }
      else snprintf(rn, sizeof rn, "%d: (empty)", i + 1);
      label = rn;
    }
    if (s_opt == 5) label = s_wnd_b[i];
    big_label(ctx, label, o_btn(i), GColorWhite);
  }
}
static int jump_input(int ev, int x, int y) {
  int n = o_n();
  if (ev == BUTTON_ID_SELECT) return CMD(C_JUMP, s_sel);
  if (ev >= 0 && ev != BUTTON_ID_BACK) { s_sel = (s_sel + 1) % n; redraw(); return 0; }
  for (int i = 0; ev < 0 && i < n; i++) if (hit(o_btn(i), x, y)) return CMD(C_JUMP, i);
  int th; bool row; GRect r = o_win(&th, &row);
  if ((ev < 0 && hit(GRect(r.origin.x - 6, r.origin.y - 6, r.size.w + 12, r.size.h + 12), x, y)) || G.hero.HP <= 0) return 0;   // WndResurrect: only its buttons
  s_scene = SC_GAME; redraw(); return 0;           // Back, or a tap outside: close
}

// ---------------- the game menu (PD WndGame, with WndSettings' switches): tabbed pages of big picture buttons ----------------
#define M_HEAD 16
#define M_BODY 130                                 // two rows of 64 px buttons
#define M_TAB_W 61
#define M_TAB_H 42                                 // room for the pictures at 2x
#define M_AS 20                                    // 0.81 (owner): the settings page: the Assistant's own frame and row, under a title
static const uint8_t PAGE0[4] = { A_RETURN, A_CATALOG, A_MUSIC, A_NEW };   // the pages: game, records, settings
static const uint8_t ICON[11] = { 9, 0, 12, 11, 3, 4, 0, 0, 6, 7, 8 };   // menu_icons.png, by A_* (the Assistant's: drawn below)
static bool m_off(int a) { return a == A_ITEMS && !AS_ON; }   // 0.80: item assist, while the Assist is off: greyed, passed over
static bool dead(void) { return G.hero.HP <= 0; }   // then one page: new game, rankings, main menu
static int m_page(void) { return dead() ? 0 : s_sel >= A_MUSIC ? 2 : s_sel >= A_CATALOG; }
static int m_as(void) { return !dead() && m_page() == 2 ? M_AS : 0; }   // s_sel: the selected button, which picks the page
static int m_first(void) { return dead() ? 0 : PAGE0[m_page()]; }
static int m_count(void) { return dead() ? 3 : PAGE0[m_page() + 1] - PAGE0[m_page()]; }
static int m_act(int i) { return (dead() ? A_NEW : 0) + m_first() + i; }
#ifdef PBL_ROUND                                      // 0.83 (owner: lean into the round screen): the window in the middle, the tabs
static GRect m_win(void) { int h = M_HEAD + M_BODY + m_as(); return GRect((s_w - BIG_W) / 2, (s_h - h) / 2 - 6, BIG_W, h); }   // bubbles at the rim under it
static GRect m_tab(int t) { return GRect(t * 50 + 60, (t & 1) * 14 + 204, 40, 40); }
#else
static GRect m_win(void) { int h = M_HEAD + M_BODY + m_as(), t = dead() ? 0 : M_TAB_H; return GRect((s_w - BIG_W) / 2, (s_h - h - t) / 2 + 2, BIG_W, h); }
static GRect m_tab(int t) { GRect r = m_win(); return GRect(r.origin.x - 4 + t * (M_TAB_W + 1), r.origin.y + r.size.h, M_TAB_W, M_TAB_H); }
#endif
static GRect m_tile(int i) {                       // two buttons: one per row; three: two, then one across
  GRect r = m_win(); int n = m_count(), y = r.origin.y + M_HEAD + (i >= 2 ? m_as() : 0);
#ifdef PBL_ROUND
  bool wide = (n & 1) && i == n - 1;
  return rfit(n == 2 ? GRect(r.origin.x, y + i * 66, BIG_W, 64) : GRect(r.origin.x + (wide ? 0 : (i & 1) * 90), y + (i / 2) * 66, wide ? BIG_W : 89, 64));
#else
  if (n == 2) return GRect(r.origin.x, y + i * 66, BIG_W, 64);
  bool wide = (n & 1) && i == n - 1;
  return GRect(r.origin.x + (wide ? 0 : (i & 1) * 90), y + (i / 2) * 66, wide ? BIG_W : 89, 64);
#endif
}
static void draw_menu(GContext *ctx) {
  GRect r = m_win();
  int n = m_count(), pg = m_page();
  blit_begin(ctx); blit_fog(0, 0, s_w > s_h ? s_w : s_h);   // 0.82 (owner): the game behind in the backpack's shadow
  tab_panel(r);
  int as = m_as();
  WELL(r.origin.x, r.origin.y + M_HEAD - 2, r.size.w, as ? 68 : r.size.h - M_HEAD + 2);
  if (as) { int ay = r.origin.y + M_HEAD + 66; WELL(r.origin.x, ay + as - 2, r.size.w, r.size.h - (ay + as - 2 - r.origin.y));   // the Assistant's frame
    blit_box(r.origin.x - 2, ay, r.size.w + 4, r.origin.y + r.size.h - ay, 1, ARGB(GColorChromeYellow)); }
  if (!dead()) for (int t = 0; t < 3; t++) {        // floppy disk, quill and paper, gear
    GRect tr = m_tab(t); bool sel = t == pg;
#ifdef PBL_ROUND
    bubble(tr, sel ? ARGB(GColorDarkCandyAppleRed) : ARGB(GColorDarkGray), sel ? ARGB(GColorIcterine) : ARGB(GColorLightGray));   // the page's: red, as its buttons
    blit(&s_micons, t * 16, 0, 16, 16, tr.origin.x + 4, tr.origin.y + 4, 2);
#else
    tab_art(tr, sel);
    blit(&s_micons, t * 16, 0, 16, 16, tr.origin.x + (tr.size.w - 32) / 2, tr.origin.y + (sel ? 3 : 5), 2);
#endif
  }
  for (int i = 0; i < n; i++) {
    GRect b = m_tile(i); int a = m_act(i), ix = b.origin.x + (b.size.w - 32) / 2, iy = b.origin.y + 3;
    red_button(b, m_first() + i == s_sel);
    if (a == A_ASSIST) {                           // 0.81 (owner): near: the reticle on the hero (his own sheet, the armour worn); far: on the
      if (AS_OFFCAM) blit(&s_status, 87, 5, 8, 8, ix + 8, iy + 8, 2);   // stairs down (the status pane's); off: crossed out
      else if (AS_ON) blit(&s_hero, 0, 0, 12, 15, ix + 4, iy + 1, 2);
      blit(&s_icons, 67, 0, 16, 16, ix, iy, 2); }
    else if (a == A_ITEMS) blit(&s_icons, 17, 0, 10, 10, ix + 6, iy + 6, 2);   // the backpack (BagTab's)
    else if (a == A_RETURN) {                     // 0.82 (owner: "undersized and too high ... make it punchier"): a big white arrow, outlined,
      int cx = b.origin.x + b.size.w / 2, cy = b.origin.y + 18;   // in the middle of the room over the label
      for (int o = 1; o >= 0; o--) { uint8_t c = o ? ARGB(GColorBlack) : ARGB(GColorWhite); int H = 12 + 3 * o;
        blit_fill(cx - 18 - 2 * o, cy - 4 - 2 * o, 22 + 2 * o, 9 + 4 * o, c);
        for (int k = 0; k <= H; k++) blit_fill(cx + 2 - 2 * o + k, cy - H + k, 1, 2 * (H - k) + 1, c); } }
    else blit(&s_micons, ICON[a] * 16, 0, 16, 16, ix, iy, 2);
    if ((a == A_MUSIC && !(g_sound & SOUND_MUSIC)) || (a == A_SOUND && !(g_sound & SOUND_FX)) || (a == A_ASSIST && !AS_ON) || (a == A_ITEMS && !(g_assist & 2)) || m_off(a)) {
      blit_fog(ix, iy, 32); blit(&s_micons, 10 * 16, 0, 16, 16, ix, iy, 2); }   // switched off: dimmed and crossed out
  }
  blit_end(ctx);
  static const char *const TITLE[3] = { "Pixel Dungeon", "Catalogue & Journal", "Audio" };   // (0.82 (owner): the gear says settings)
  text(ctx, TITLE[pg], s_font_hud, GColorIcterine, r.origin.x, r.origin.y - 2, r.size.w, GTextAlignmentCenter);   // (0.82: centred in their bands)
  if (m_as()) text(ctx, "Assistant", s_font_hud, GColorIcterine, r.origin.x, r.origin.y + M_HEAD + 67, r.size.w, GTextAlignmentCenter);
  static const char *const LABEL[11] = { "Return to Game", "Save & Main Menu", "Catalogue", "Journal", "Music: %s", "Sound: %s", "Assist: %s", "Item assist: %s", "New Game", "Rankings", "Main Menu" };
  for (int i = 0; i < n; i++) {
    GRect b = m_tile(i); int a = m_act(i);
    static const char *const VOL[3] = { "Loud", "Medium", "Quiet" };
    char buf[24]; snprintf(buf, sizeof buf, LABEL[a], a == A_SOUND ? (g_sound & SOUND_FX ? VOL[SOUND_VOL(g_sound) % 3] : "Off") : a == A_MUSIC ? (g_sound & SOUND_MUSIC ? "On" : "Off")
      : a == A_ASSIST ? (!AS_ON ? "Off" : AS_OFFCAM ? "Far" : "Near") : g_assist & 2 ? "On" : "Off");
    big_label(ctx, buf, GRect(b.origin.x, b.origin.y + 35, b.size.w, 28), m_off(a) ? GColorDarkGray : GColorWhite);
  }
}
static int menu_input(int ev, int x, int y) {
  int n = dead() ? 3 : A_NEW;
  if (ev == BUTTON_ID_SELECT) return m_off(m_act(s_sel - m_first())) ? 0 : CMD(C_MENU, m_act(s_sel - m_first()));
  if (ev >= 0 && ev != BUTTON_ID_BACK) { do s_sel = (s_sel + (ev == BUTTON_ID_DOWN ? 1 : -1) + n) % n; while (!dead() && m_off(s_sel)); redraw(); return 0; }   // through every page
  if (ev < 0) {
    for (int i = 0; i < m_count(); i++) if (hit(m_tile(i), x, y)) { s_sel = m_first() + i; return m_off(m_act(i)) ? 0 : CMD(C_MENU, m_act(i)); }
    for (int t = 0; !dead() && t < 3; t++) if (tab_hit(m_tab(t), x, y)) { s_sel = PAGE0[t]; redraw(); return 0; }
    GRect r = m_win();
    if (hit(GRect(r.origin.x - 6, r.origin.y - 6, r.size.w + 12, r.size.h + 12), x, y)) return 0;
  }
  s_scene = dead() ? SC_DEAD : SC_GAME; redraw(); return 0;   // Back, or a tap outside: close
}
static void draw_dead(GContext *ctx) {            // GameScene.gameOver: the GAME OVER banner over the revealed floor
  blit_begin(ctx);
  blit(&s_gameover, 0, 0, 128, 35, (s_w - 128) / 2, map_center_y() - 50, 1);
  blit_end(ctx);
  GRect r = GRect(PBL_IF_ROUND_ELSE(40, 8), map_center_y() - 8, s_w - PBL_IF_ROUND_ELSE(80, 16), 40);
  text_outlined_rect(ctx, G.death, s_font_hud, GColorSunsetOrange, r, GTextAlignmentCenter);
}

static void ui_draw(int sc, GContext *ctx) {
  switch (sc) {
    case SC_MENU: draw_menu(ctx); break;
    case SC_DEAD: draw_dead(ctx); break;
    case SC_INFO: draw_info(ctx); break;
    case SC_OPTIONS: draw_jump(ctx); break;
  }
}
static int ui_input(int ev, int x, int y) { return s_scene == SC_MENU ? menu_input(ev, x, y) : s_scene == SC_OPTIONS ? jump_input(ev, x, y) : 0; }

OVL_ENTRY(uiw, (void *)ui_draw, (void *)ui_input);
