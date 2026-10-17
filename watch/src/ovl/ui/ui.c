/*
 * Pixel Dungeon for Pebble - the front screens (an overlay): title, hero select, erase confirmation, and the two
 * endings (AmuletScene, SurfaceScene). The in-game windows are the "uiw" overlay (0.14: split so each fits the heap).
 */
#include "pd.h"
#include "ovl.h"
#include "sheets.auto.h"
#include "ui_int.h"
static void draw_arcs(void);
#include "item_sp.h"

// title: PD DashboardItem Play / Rankings; hero select: PD StartScene 2x2 class grid
static GRect mute_rect(void) { return PBL_IF_ROUND_ELSE(GRect(14, s_h / 2 - 15, 30, 30), GRect(2, 2, 30, 30)); }
static GRect title_rect(int i) { return GRect(s_w / 2 - 74 + i * 84, PBL_IF_ROUND_ELSE(108, 104), 64, 84); }   // TitleScene: Play, Rankings
static GRect class_rect(int i) {
  int cw = 56, ch = 72, gx = PBL_IF_ROUND_ELSE(14, 24), x0 = (s_w - 2 * cw - gx) / 2, y0 = PBL_IF_ROUND_ELSE(50, 34);
  return GRect(x0 + (i % 2) * (cw + gx), y0 + (i / 2) * (ch + 4), cw, ch);
}
static GRect select_button(int i, int n) {     // n buttons side by side at the bottom (0.41, user: taller; round: New Game narrower, in the rim)
  int total = PBL_IF_ROUND_ELSE(n == 1 ? 140 : 176, 192), gap = 6, w = (total - gap * (n - 1)) / n, h = n == 1 ? 38 : 40;
  return GRect((s_w - total) / 2 + i * (w + gap), s_h - h - PBL_IF_ROUND_ELSE(n == 1 ? 22 : 36, 4), w, h);
}
static GRect confirm_rect(void) { return GRect((s_w - 160) / 2, (s_h - 76) / 2, 160, 76); }

static const char *CLASS_NAMES[4] = { "WARRIOR", "MAGE", "ROGUE", "HUNTRESS" };

static void draw_mute(GContext *ctx) {
  static const char *ON[9] = { "....#......", "...##...#..", "..###.#..#.", "####...#.#.", "####...#.#.", "####...#.#.",
                               "..###.#..#.", "...##...#..", "....#......" };
  static const char *OFF[9] = { "....#......", "...##......", "..###.#...#", "####...#.#.", "####....#..", "####...#.#.",
                                "..###.#...#", "...##......", "....#......" };
  GRect r = mute_rect();
  blit_begin(ctx);
  blit_nine(&s_chrome, 22, 0, 18, 18, 5, r.origin.x, r.origin.y, r.size.w, r.size.h);
  bool on = g_sound & SOUND_FX; const char **g = on ? ON : OFF; int w = on ? 11 - 3 * SOUND_VOL(g_sound) : 11;   // fewer waves: quieter
  for (int y = 0; y < 9; y++) for (int x = 0; x < w; x++)
    if (g[y][x] == '#') blit_fill(r.origin.x + 4 + x * 2, r.origin.y + 6 + y * 2, 2, 2, on ? ARGB(GColorWhite) : ARGB(GColorLightGray));
  blit_end(ctx);
}

static void draw_title(GContext *ctx) {                // TitleScene: the dashboard's Play and Rankings
  static const char *LABEL[2] = { "Play", "Rankings" };
  blit_begin(ctx);
  draw_arcs();
  blit(&s_logo, 0, 0, 128, 70, s_w / 2 - 64, PBL_IF_ROUND_ELSE(28, 14), 1);
  for (int i = 0; i < 2; i++) { GRect p = title_rect(i); blit(&s_play, i * 32, 0, 32, 32, p.origin.x, p.origin.y, 2); }
  if (s_sel == 2) { GRect m = mute_rect(); blit_box(m.origin.x - 2, m.origin.y - 2, m.size.w + 4, m.size.h + 4, 1, ARGB(GColorWhite)); }
  blit_end(ctx);
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  for (int i = 0; i < 2; i++) {
    GRect p = title_rect(i);
    text(ctx, LABEL[i], f, s_sel == i ? GColorWhite : GColorLightGray, p.origin.x - 10, p.origin.y + 62, p.size.w + 20, GTextAlignmentCenter);
    if (s_sel == i) { graphics_context_set_fill_color(ctx, GColorWhite); graphics_fill_rect(ctx, GRect(p.origin.x + 12, p.origin.y + 84, p.size.w - 24, 2), 0, 0); }
  }
  draw_mute(ctx);
  text(ctx, "v0.89", s_font_hud, GColorLightGray, 0, s_h - PBL_IF_ROUND_ELSE(40, 18), PBL_IF_ROUND_ELSE(s_w / 2 + 20, s_w - 4), GTextAlignmentRight);
}

// RankingsScene: the top six games (Rankings.Record rows: a tomb or the amulet with the place, the result, the hero)
static void draw_arcs(void) {
  for (int y = 0; y < s_h; y += 64) for (int x = 0; x < s_w; x += 64) blit(&s_arcs, 0, 0, 64, 64, x, y, 1);
}
static void draw_rankings(GContext *ctx) {
  Ranks k; memset(&k, 0, sizeof k);
  if (persist_read_data(KEY_RANKS, &k, 256) == 256) persist_read_data(KEY_RANKS + 1, (uint8_t *)&k + 256, sizeof k - 256);
  if (k.n > RANK_N) k.n = RANK_N;
  int x0 = PBL_IF_ROUND_ELSE(34, 4), w = s_w - 2 * x0, rh = PBL_IF_ROUND_ELSE(26, 30), y0 = PBL_IF_ROUND_ELSE(46, 26);
  blit_begin(ctx);
  draw_arcs();
  for (int i = 0; i < k.n; i++) {
    int y = y0 + i * rh;
    if (i == k.latest) { blit_fill(x0, y, w, rh - 2, ARGB(GColorBlack)); blit_box(x0, y, w, rh - 2, 1, ARGB(k.r[i].win ? GColorPastelYellow : GColorLightGray)); }   // the latest (PD: a flare)
    item_spr(k.r[i].win ? SP_AMULET : SP_TOMB, x0 + 2, y + (rh - 18) / 2, 1);
    blit(&s_avatars, (k.r[i].cls & 3) * 24, 0, 24, 28, x0 + w - 25, y + (rh - 30) / 2, 1);
  }
  blit_end(ctx);
  GFont f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (!k.n) {
    text_wrap_c(ctx, "No games have been played yet.", f18, GColorLightGray, GRect(x0 + 10, s_h / 2 - 24, w - 20, 48));
    return;
  }
  text(ctx, "Top Rankings", f18, GColorWhite, 0, y0 - 24, s_w, GTextAlignmentCenter);
  char t[40];
  for (int i = 0; i < k.n; i++) {
    int y = y0 + i * rh;
    GColor c = k.r[i].win ? GColorPastelYellow : GColorLightGray;
    snprintf(t, sizeof t, "%d", i + 1);
    text_outlined(ctx, t, s_font_hud, c, x0 + 2, y + (rh - 18) / 2 - 1, 16, GTextAlignmentCenter);
    k.r[i].info[sizeof k.r[i].info - 1] = 0;
    GRect r = GRect(x0 + 22, y, w - 22 - 28, rh);
    GFont f = s_font_log;
    GSize sz = graphics_text_layout_get_content_size(k.r[i].info, f, r, GTextOverflowModeWordWrap, GTextAlignmentLeft);
    if (sz.h > rh) { f = s_font_tiny; sz = graphics_text_layout_get_content_size(k.r[i].info, f, r, GTextOverflowModeWordWrap, GTextAlignmentLeft); }
    r.origin.y += (rh - sz.h) / 2 - 3; r.size.h = sz.h + 4;
    text_wrap(ctx, k.r[i].info, f, GColorWhite, r);
  }
  snprintf(t, sizeof t, "Games played: %d/%d", k.won, k.total);   // won (yellow) / total
  text(ctx, t, s_font_hud, GColorLightGray, 0, y0 + k.n * rh, s_w, GTextAlignmentCenter);
}

static int s_class_info = -1;                     // WndClass: the class whose perks are shown
static void draw_class_info(GContext *ctx) {        // WndClass, its perks tab
  GFont f = s_font_log; const char *t = txt(s_class_info > 3 ? TX_HUNTRESS_LOCKED : TX_PERKS_0 + s_class_info);
  int w = s_w - PBL_IF_ROUND_ELSE(60, 20), hmax = s_h - PBL_IF_ROUND_ELSE(80, 44);
  GSize sz = graphics_text_layout_get_content_size(t, f, GRect(0, 0, w, 400), GTextOverflowModeWordWrap, GTextAlignmentLeft);
  if (sz.h > hmax) { f = s_font_tiny; sz = graphics_text_layout_get_content_size(t, f, GRect(0, 0, w, 400), GTextOverflowModeWordWrap, GTextAlignmentLeft); }
  GRect r = GRect((s_w - w) / 2, (s_h - sz.h - 22) / 2, w, sz.h + 22);
  blit_begin(ctx); panel(r); WELL(r.origin.x, r.origin.y + 17, w, r.size.h - 17); blit_end(ctx);
  text(ctx, CLASS_NAMES[s_class_info > 3 ? 3 : s_class_info], s_font_hud, GColorPastelYellow, r.origin.x, r.origin.y - 3, w, GTextAlignmentCenter);
  text_wrap(ctx, t, f, GColorWhite, GRect(r.origin.x, r.origin.y + 18, w, sz.h + 4));
}
static void draw_select(GContext *ctx) {
  blit_begin(ctx);
  draw_arcs();
  blit(&s_banner, 0, 0, 128, 21, s_w / 2 - 64, PBL_IF_ROUND_ELSE(24, 6), 1);
  if (s_has_save) {                                  // one save: only that hero, Continue / Delete
    GRect r = class_rect(0);
    blit(&s_avatars, (s_save_lvl >> 6) * 24, 0, 24, 28, s_w / 2 - 24, r.origin.y + 16, 2);
    for (int i = 0; i < 2; i++) red_button(select_button(i, 2), s_sel == i);
  } else {
    for (int i = 0; i < 4; i++) {
      GRect r = class_rect(i);
      blit(&s_avatars, i * 24, i < 3 || huntress_unlocked() ? 0 : 28, 24, 28, r.origin.x + 4, r.origin.y, 2);
      if (i == s_sel) blit_box(r.origin.x - 1, r.origin.y - 4, r.size.w + 2, r.size.h + 2, 3, ARGB(GColorYellow));   // 0.41 (user): thick, the reticle's yellow
    }
    red_button(select_button(0, 1), true);
  }
  blit_end(ctx);
  if (s_has_save) {
    GRect r = class_rect(0);
    text(ctx, CLASS_NAMES[s_save_lvl >> 6], s_font_hud, GColorWhite, 0, r.origin.y + 74, s_w, GTextAlignmentCenter);
    GRect a = select_button(0, 2), b = select_button(1, 2);
    char sub[24]; snprintf(sub, sizeof sub, "Depth %d, level %d", s_save_depth, s_save_lvl & 63);
    text(ctx, "Continue", s_font_hud, GColorWhite, a.origin.x, a.origin.y + 2, a.size.w, GTextAlignmentCenter);
    text(ctx, sub, s_font_tiny, GColorWhite, a.origin.x, a.origin.y + 20, a.size.w, GTextAlignmentCenter);
    text(ctx, "Delete", s_font_hud, GColorWhite, b.origin.x, b.origin.y + 2, b.size.w, GTextAlignmentCenter);
    text(ctx, "Erase this game", s_font_tiny, GColorWhite, b.origin.x, b.origin.y + 20, b.size.w, GTextAlignmentCenter);
  } else {
    for (int i = 0; i < 4; i++) {
      GRect r = class_rect(i);
      text(ctx, CLASS_NAMES[i], s_font_tiny, i == s_sel ? GColorWhite : i < 3 || huntress_unlocked() ? GColorLightGray : GColorDarkGray, r.origin.x - 6, r.origin.y + 56, r.size.w + 12, GTextAlignmentCenter);
    }
    GRect b = select_button(0, 1);
    text(ctx, "New Game", s_font_hud, GColorWhite, b.origin.x, b.origin.y + 9, b.size.w, GTextAlignmentCenter);
    if (s_msg[1][0]) text(ctx, s_msg[1], s_font_log, GColorPastelYellow, 0, b.origin.y - 20, s_w, GTextAlignmentCenter);
  }
  if (s_class_info >= 0 && !s_has_save) draw_class_info(ctx);
}

static void draw_confirm(GContext *ctx) {
  draw_select(ctx);
  GRect r = confirm_rect();
  blit_begin(ctx);
  panel(r);
  WELL(r.origin.x, r.origin.y + 17, r.size.w, r.size.h - 17);
  red_button(GRect(r.origin.x + 6, r.origin.y + 44, 70, 24), s_sel == 0);
  red_button(GRect(r.origin.x + 84, r.origin.y + 44, 70, 24), s_sel == 1);
  blit_end(ctx);
  text(ctx, "Erase this game?", s_font_hud, GColorPastelYellow, r.origin.x, r.origin.y, r.size.w, GTextAlignmentCenter);
  text(ctx, "Your hero will be lost.", s_font_log, GColorWhite, r.origin.x, r.origin.y + 18, r.size.w, GTextAlignmentCenter);
  text(ctx, "Erase", s_font_hud, GColorWhite, r.origin.x + 6, r.origin.y + 46, 70, GTextAlignmentCenter);
  text(ctx, "Cancel", s_font_hud, GColorWhite, r.origin.x + 84, r.origin.y + 46, 70, GTextAlignmentCenter);
}

static void ending_button(GContext *ctx, int b, int x, int y, int w, int h, const char *t) {
  s_btn_rect[b] = GRect(x, y, w, h);
  red_button(s_btn_rect[b], s_amulet != 2 && s_sel == b);
}

static void draw_amulet(GContext *ctx) {               // AmuletScene: the amulet, its text and two choices
  static const char *const TXT = "You finally hold it in your hands, the Amulet of Yendor. Using its power you can take over the world or bring peace and prosperity to people or whatever. Anyway, your life will change forever and this game will end here. Or you can stay a mere mortal a little longer.";
  int w = s_w - PBL_IF_ROUND_ELSE(56, 12), bw = PBL_IF_ROUND_ELSE(140, 160), th = 0, z = 2, gap = 8;
  GFont f = s_font_log;
  if (s_amulet == 0) {                                 // the text, as large as the screen allows
    th = graphics_text_layout_get_content_size(TXT, f, GRect(0, 0, w, 400), GTextOverflowModeWordWrap, GTextAlignmentCenter).h;
    if (32 * 2 + th + 2 * 8 + 48 > s_h - 6) { z = 1; gap = 4; }
    if (32 + th + 2 * 4 + 48 > s_h - 6) { f = s_font_tiny; th = graphics_text_layout_get_content_size(TXT, f, GRect(0, 0, w, 400), GTextOverflowModeWordWrap, GTextAlignmentCenter).h; }
  }
  int h = 32 * z + gap + (th ? th + gap : 0) + 48, y = (s_h - h) / 2, x = (s_w - bw) / 2;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, s_w, s_h), 0, GCornerNone);
  blit_begin(ctx);
  blit(&s_gameover, 0, 0, 32, 32, (s_w - 32 * z) / 2, y, z);
  int ty = y + 32 * z + gap, by = ty + (th ? th + gap : 0);
  ending_button(ctx, 0, x, by, bw, 22, NULL);
  ending_button(ctx, 1, x, by + 26, bw, 22, NULL);
  blit_end(ctx);
  if (th) text_wrap_c(ctx, TXT, f, GColorWhite, GRect((s_w - w) / 2, ty - 2, w, th + 4));
  text(ctx, "Let's call it a day", s_font_hud, GColorWhite, x, by + 2, bw, GTextAlignmentCenter);
  text(ctx, "I'm not done yet", s_font_hud, GColorWhite, x, by + 28, bw, GTextAlignmentCenter);
}

#define SKY_W 80
#define SKY_H 112
static void draw_surface(GContext *ctx) {              // SurfaceScene: home, under the sky, with your pet
  time_t now = time(NULL); struct tm *t = localtime(&now);
  bool day = !(t->tm_hour < 7 || t->tm_hour >= 21);   // Dungeon.nightMode
  int vx = (s_w - SKY_W) / 2, vy = (s_h - SKY_H - 20) / 2;
  blit_begin(ctx);
  draw_arcs();
  static const uint8_t DAY[3] = { GColorPictonBlueARGB8, GColorBabyBlueEyesARGB8, GColorCelesteARGB8 };
  static const uint8_t NIGHT[3] = { GColorOxfordBlueARGB8, GColorDukeBlueARGB8, GColorLibertyARGB8 };
  const uint8_t *c = day ? DAY : NIGHT;
  for (int b = 0; b < 5; b++) {                       // Sky: a gradient, dithered between Pebble's blues
    int y0 = vy + b * SKY_H / 5, y1 = vy + (b + 1) * SKY_H / 5;
    blit_fill2(vx, y0, SKY_W, y1 - y0, c[(b + 1) / 2], c[b / 2]);
  }
  uint32_t r = 0x1234567;
  if (!day) for (int i = 0; i < 60; i++) {            // stars, fainter toward the ground
    r = r * 1103515245 + 12345; int sx = (r >> 8) % SKY_W; r = r * 1103515245 + 12345; int sy = (r >> 8) % SKY_H;
    if ((int)((r >> 20) % SKY_H) > sy) blit_fill(vx + sx, vy + sy, 1, 1, GColorWhiteARGB8);
  }
  static const uint8_t CL[3][4] = { { 0, 0, 49, 20 }, { 0, 20, 49, 22 }, { 0, 42, 50, 18 } };
  static const int8_t CX[3] = { 24, 2, 30 }, CY[3] = { 8, 30, 52 };
  for (int i = 0; i < 3; i++) blit(&s_gameover, 88 + CL[i][0], CL[i][1], CL[i][2], CL[i][3], vx + CX[i], vy + CY[i], 1);
  int cls = G.hero.hclass & 3;
  blit(&s_avatars, cls * 24, 0, 24, 28, vx + (SKY_W - 24) / 2, vy + SKY_H - 28, 1);   // Avatar
  blit(&s_gameover, (now & 3) == 0 ? 16 : 0, 125, 16, 16, vx + SKY_W / 2 + 2, vy + SKY_H - 16, 1);   // Pet
  for (int i = 0; i < 5; i++) blit(&s_gameover, 88 + ((i * 7 + 3) % 4) * 16, 60, 16, 14, vx + i * 16, vy + SKY_H - 14, 1);   // GrassPatch
  blit(&s_gameover, 0, 0, 88, 125, vx - 4, vy - 9, 1);   // the frame
  ending_button(ctx, 0, vx + 4, vy - 9 + 125 + 4, SKY_W - 8, 20, NULL);
  s_btn_rect[1] = GRect(0, 0, 0, 0);
  blit_end(ctx);
  text(ctx, "Game Over", s_font_hud, GColorWhite, vx + 4, vy - 9 + 125 + 5, SKY_W - 8, GTextAlignmentCenter);
}

static void ui_draw(int sc, GContext *ctx) {
  switch (sc) {
    case SC_TITLE: draw_title(ctx); break;
    case SC_SELECT: draw_select(ctx); break;
    case SC_CONFIRM: draw_confirm(ctx); break;
    case SC_AMULET: if (s_amulet == 2) draw_surface(ctx); else draw_amulet(ctx); break;
    case SC_RANKINGS: draw_rankings(ctx); break;
  }
}

// ---------------- input (0.14: moved here from the app) ----------------
static void move_sel(int dir, int n) { s_sel = (s_sel + dir + n) % n; redraw(); }
static void title_pick(int i) {                    // TitleScene: Play, Rankings, or PD's prefs corner (mute all)
  s_sel = i;
  if (i == 2) snd_cycle();
  else { ui_sound(SND_CLICK); if (i) s_scene = SC_RANKINGS; else { open_select(); return; } }
  redraw();
}
static void amulet_choose(int b) {
  if (s_amulet == 2 || b == 0) {                              // Let's call it a day (then the rankings) / Game Over
    if (s_amulet != 2) { game_erase(); game_rank(1); s_has_save = false; }
    to_title(); if (!s_amulet) s_scene = SC_RANKINGS;
    return;
  }
  s_scene = SC_GAME; redraw();                                // I'm not done yet
}
static bool hit(GRect r, int x, int y) { return x >= r.origin.x && y >= r.origin.y && x < r.origin.x + r.size.w && y < r.origin.y + r.size.h; }
static int ui_input(int ev, int x, int y) {          // ev: a ButtonId, or -1 a tap at x, y; returns CMD(C_START, 0 / 1) to start a new / the saved game
  bool tap = ev < 0, sel = ev == BUTTON_ID_SELECT, back = ev == BUTTON_ID_BACK;
  int dir = ev == BUTTON_ID_DOWN ? 1 : -1;
  switch (s_scene) {
    case SC_TITLE:
      if (tap) { for (int i = 0; i < 3; i++) if (hit(i < 2 ? title_rect(i) : mute_rect(), x, y)) title_pick(i); }
      else if (sel) title_pick(s_sel);
      else move_sel(dir, 3);                                     // Play, Rankings, the sound corner
      return 0;
    case SC_RANKINGS: to_title(); return 0;
    case SC_SELECT:
      if (s_class_info >= 0 && !tap) { s_class_info = -1; redraw(); return 0; }
      if (back) to_title();
      else if (s_has_save) {                                     // Continue / New game
        int b = tap ? (hit(select_button(0, 2), x, y) ? 0 : hit(select_button(1, 2), x, y) ? 1 : -1) : sel ? s_sel : -2;
        if (b == -2) move_sel(dir, 2);
        else if (b == 0) return CMD(C_START, 1);
        else if (b == 1) { s_scene = SC_CONFIRM; s_sel = 1; redraw(); }
      } else if (tap) {
        if (s_class_info >= 0) { s_class_info = -1; redraw(); return 0; }   // any tap closes WndClass
        for (int i = 0; i < 4; i++) if (hit(class_rect(i), x, y)) {
          if (i == s_sel) s_class_info = i;                          // StartScene.updateClass: the chosen class again shows WndClass
          else if (i < 3 || huntress_unlocked()) s_sel = i; else s_class_info = 4;   // StartScene: the locked huntress says how to unlock her
          redraw();
          return 0;
        }
        if (hit(select_button(0, 1), x, y)) return CMD(C_START, 0);
      } else if (sel) return CMD(C_START, 0);
      else { do s_sel = (s_sel + dir + 4) % 4; while (s_sel == 3 && !huntress_unlocked()); redraw(); }   // the class
      return 0;
    case SC_CONFIRM: {
      GRect r = confirm_rect();
      int b = tap ? (hit(GRect(r.origin.x + 6, r.origin.y + 44, 70, 24), x, y) ? 0 : hit(GRect(r.origin.x + 84, r.origin.y + 44, 70, 24), x, y) || !hit(r, x, y) ? 1 : -2)
                  : sel ? s_sel : back ? 1 : -1;
      if (b == -2) return 0;
      if (b < 0) { move_sel(dir, 2); return 0; }
      if (b == 0) game_erase();
      open_select();
      return 0;
    }
    case SC_AMULET:
      if (tap) { for (int b = 0; b < 2; b++) if (hit(s_btn_rect[b], x, y)) { amulet_choose(b); return 0; } }
      else if (sel) amulet_choose(s_sel);
      else if (s_amulet != 2) { if (back) { s_scene = SC_GAME; redraw(); } else move_sel(dir, 2); }   // AmuletScene.onBackPressed
      return 0;
    default: return 0;
  }
}

OVL_ENTRY(ui, (void *)ui_draw, (void *)ui_input);
