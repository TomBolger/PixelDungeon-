// ---------------- the watchface (PD_FACE builds; included into main.c) ----------------
// A burst of play (20 s) when the face appears or the wrist is flicked, then everything freezes: no timers, no redraws,
// just the clock layer once a minute. The autoplayer is the Assist's first suggestion (ovl/tgt: the bot's targeting) plus
// face_brain.h; items are used through the real windows, button by button, so a viewer can follow.
#include "face_pace.h"
#define FACE_BURST_MS 20000
#if defined(FACE_HEAPLOG) && defined(FACE_CAMLOG)
#define FL(...) app_log(APP_LOG_LEVEL_INFO, "face", 0, __VA_ARGS__)
#else
#define FL(...) ((void)0)
#endif
#define FACE_WIN_MS 30000
#define KEY_FACE_CLS 210                              // persist: the last hero's class (the next one is random among the other three)
#ifdef FACE_OLDPACE                                   // test build: the pacing before the owner's pacing round (to measure against)
#define STEP_MS 140
#define NAV_MS 330
#else
#define STEP_MS stepms()                                   // a decision after a walk or a hit
#define NAV_MS navms()                                    // one button press in a window: every item-use screen dwells about half a second (movement and fights run at STEP_MS)
#endif
static uint32_t s_f_end, s_f_next, s_f_last, s_f_vic;
static int32_t s_fp_t0; static uint8_t s_fp_chk; static uint8_t s_fp; static int8_t s_fp_slot, s_fp_act; static uint8_t s_fp_n, s_fp_bad;   // the item plan: 0 none, 1 in the backpack, 2 in the item window
static uint32_t s_fc_t, s_fc_hold; static int32_t s_fc_key = -1; static bool s_fc_held;   // the camera (face_cam)
static Layer *s_clock_layer; static int s_f_tlow = 0x7FFFFFFF, s_f_tsc;
#ifdef FACE_LOGS
static int s_f_min = 99999;                           // test builds: the lowest free heap seen while playing
#endif

// ---- the clock: Pixel Dungeon's own 3x5 digits (font1x.png), drawn big with an outline
static const uint8_t FD[10][5] = { {3,5,5,5,6}, {2,6,2,2,2}, {6,1,2,4,7}, {6,1,2,1,6}, {5,5,7,1,1}, {7,4,6,1,6}, {3,4,7,5,7}, {7,1,2,4,4}, {7,5,2,5,7}, {7,5,7,1,6} };
static void fclk_glyph(GContext *g, int d, int x, int y, int k, int grow, GColor c) {   // d: 0-9, 10 colon
  graphics_context_set_fill_color(g, c);
  if (d == 10) { for (int r = 1; r < 5; r += 2) graphics_fill_rect(g, GRect(x - grow, y + r * k - grow, k + 2 * grow, k + 2 * grow), 0, GCornerNone); return; }
  for (int r = 0; r < 5; r++) for (int b = 0; b < 3; b++) if (FD[d][r] >> (2 - b) & 1) graphics_fill_rect(g, GRect(x + b * k - grow, y + r * k - grow, k + 2 * grow, k + 2 * grow), 0, GCornerNone);
}
// ---- settings (Clay, on the phone: pkjs sends them as one byte array; they persist on the watch)
#define KEY_FACE_CFG 211
#define KEY_FACE_WX 212
enum { FV_GAME, FV_TIME, FV_DATE, FV_TEMP, FV_HIGH, FV_LOW, FV_STEPS, FV_HR, FV_BATT, FV_WDAY, FV_MDAY, FV_SEC, FV_AMPM, FV_GOLD, FV_HPP, FV_HILO, FV_BLANK };
typedef struct { uint8_t clk_on, clk_fmt, clk_size, clk_line, res, date_ord, unit_f, slot[FS_N], vibe, god, regen, heal, unbreak, loot, hunger, stage, cls, speed, burst, fps; } FaceCfg;   // clk_fmt 0 system 1 12h 2 24h; clk_size 0 big 1 small; clk_line: shadow and outline; res: the old "clock inside the HUD" (gone; an old save's 1 becomes the time and date slots); speed 0 Fast 1 Normal 2 Relaxed (face_brain.h fb_pace), burst 0 Long 1 Medium 2 Short, fps 0 Full 1 Reduced 2 Low (day-WL); date_ord 0 MM/DD 1 DD/MM; vibe: game events buzz (default off); day-WK, the Cheats and the rest (see face_rules_apply): god mode, healing speed (0 normal 1 x2 2 x3 3 x5), ever-present healing potion, unbreakable gear, better loot, slow hunger, starting stage (0..9: Sewers .. Yog-Dzewa), classes (bit mask: warrior, mage, rogue, huntress; none = all)
#define FACE_CFG_OLD 12                                // the size of a saved FaceCfg before vibe (day-WK: and 13, before the Cheats)
#define FACE_CFG_V13 13
#define FACE_CFG_V21 21
static FaceCfg s_cfg = { 1, 0, 0, 1, 0, 0, 1, { 0 }, 0, 0, 2, 1, 1, 1, 1, 0, 15, 0, 0, 0 };   // (the Cheats' defaults: the game's rules as the face has always had them, but mortal)
static __attribute__((noinline)) uint32_t pace(uint32_t ms, int nav) { return fb_pace(ms, nav, s_cfg.speed & 3); }
static __attribute__((noinline)) uint32_t stepms(void) { return pace(100, 0); }
static __attribute__((noinline)) uint32_t navms(void) { return pace(480, 1); }   // (the Game speed setting)
static uint32_t face_fi(void) { return FB_FRM[s_cfg.fps & 3]; }   // the redraw interval of the Frame rate setting (main.c busy / face_wake)
static uint8_t s_f_cmd;                                // a one-shot command from the settings page, waiting for the next think: 1 a new run, 2 teleport
static int16_t s_wx[3]; static uint8_t s_wx_ok;        // the weather: now, high, low, in tenths of a degree C
static bool face_vibe(void) { return s_cfg.vibe; }
static bool face_hud_dyn(void) { for (int i = 0; i < FS_N; i++) if (s_cfg.slot[i]) return true; return false; }
static bool face_clock_shown(void) { return s_cfg.clk_on; }
static int face_k(void) { int k = PBL_IF_ROUND_ELSE(7, 8); return s_cfg.clk_size ? k * 3 / 4 : k; }
static int face_clk_h(void) { return face_clock_shown() ? 5 * face_k() + 6 : 0; }
static int face_clk_dy(void) { return PBL_IF_ROUND_ELSE(face_clock_shown() ? 12 : 0, 0); }
static void face_clk_changed(void) { if (s_clock_layer) layer_mark_dirty(s_clock_layer); if (s_layer) redraw(); }
static int face_deg(int t10) {                         // tenths of C to whole degrees in the chosen unit
  if (s_cfg.unit_f) t10 = t10 * 9 / 5 + 320;
  return (t10 + (t10 < 0 ? -5 : 5)) / 10;
}
static bool face_h24(void) { return s_cfg.clk_fmt == 0 ? clock_is_24h_style() : s_cfg.clk_fmt == 2; }
#ifdef FACE_FAKE                                       // test build: sensors the emulator lacks
#define FAKE_STEPS 8412
#define FAKE_HR 72
#endif
static bool face_slot(int s, char *b, int n) {         // the slot's text; false: it shows the game's number (b NULL: only asks)
  int k = s_cfg.slot[s];
  if (!k) return false;
  if (!b) return true;
  time_t t = time(NULL); struct tm *tm = localtime(&t); int v = -1; bool wx = false;
  b[0] = 0;
  switch (k) {
    case FV_TIME: { int h = tm->tm_hour; if (!face_h24()) { h %= 12; if (!h) h = 12; } snprintf(b, n, "%d:%02d", h, tm->tm_min); break; }
    case FV_DATE: if (s_cfg.date_ord) snprintf(b, n, "%02d/%02d", tm->tm_mday, tm->tm_mon + 1); else snprintf(b, n, "%02d/%02d", tm->tm_mon + 1, tm->tm_mday); break;
    case FV_TEMP: case FV_HIGH: case FV_LOW: case FV_HILO:
      if (s_wx_ok) { wx = true;
        if (k == FV_HILO) snprintf(b, n, "%d/%d", face_deg(s_wx[1]), face_deg(s_wx[2])); else snprintf(b, n, "%d°", face_deg(s_wx[k == FV_TEMP ? 0 : k == FV_HIGH ? 1 : 2])); }
      break;
    case FV_STEPS:
#ifdef FAKE_STEPS
      v = FAKE_STEPS;
#elif defined(PBL_HEALTH)
      v = (int)health_service_sum_today(HealthMetricStepCount);
#endif
      if (v >= 0) { if (v >= 10000) snprintf(b, n, "%dk", v / 1000); else snprintf(b, n, "%d", v); }
      break;
    case FV_HR:
#ifdef FAKE_HR
      v = FAKE_HR;
#elif defined(PBL_HEALTH)
      { HealthValue hv = health_service_peek_current_value(HealthMetricHeartRateBPM); v = (int)hv; }
#endif
      if (v > 0) snprintf(b, n, "%d", v);
      break;
    case FV_BATT: snprintf(b, n, "%d%%", battery_state_service_peek().charge_percent); break;
    case FV_WDAY: strftime(b, n, "%a", tm); break;
    case FV_MDAY: snprintf(b, n, "%d", tm->tm_mday); break;
    case FV_SEC: snprintf(b, n, "%02d", tm->tm_sec); break;
    case FV_AMPM: snprintf(b, n, "%s", tm->tm_hour < 12 ? "AM" : "PM"); break;
    case FV_GOLD: snprintf(b, n, "%d", G.hero.gold); break;
    case FV_HPP: snprintf(b, n, "%d%%", G.hero.HP > 0 ? G.hero.HP * 100 / G.hero.HT : 0); break;
    case FV_BLANK: snprintf(b, n, " "); break;
  }
  (void)wx;
  if (!b[0]) snprintf(b, n, "--");
  return true;
}
static void face_rules_apply(void) {                   // the settings' rules into the game's (game_int.h FaceRules)
  static const uint8_t RX[4] = { 1, 2, 3, 5 }, SD[10] = { 1, 5, 6, 10, 11, 15, 16, 20, 21, 25 };
  g_fr = (FaceRules){ s_cfg.god, RX[s_cfg.regen & 3], s_cfg.hunger ? 8 : 1, s_cfg.heal, s_cfg.unbreak, s_cfg.loot, SD[s_cfg.stage % 10] };
}
static void face_cfg_fix(void) {                       // after a load: an old save's "clock inside the HUD" is the time and date slots (and no big clock), as it was; unknown choices are the game's number
  if (s_cfg.res) { if (!s_cfg.slot[FS_LVL]) s_cfg.slot[FS_LVL] = FV_TIME; if (!s_cfg.slot[FS_HP]) s_cfg.slot[FS_HP] = FV_DATE; s_cfg.clk_on = 0; s_cfg.res = 0; }
  s_cfg.cls &= 15;                                     // (the other choices come from the phone's page only: 0/1, or an index masked or taken modulo where it is used)
  face_rules_apply();
}
static void face_cfg_load(void) {
  if (persist_exists(KEY_FACE_CFG)) { int n = persist_get_size(KEY_FACE_CFG); if (n == (int)sizeof s_cfg || n == FACE_CFG_OLD || n == FACE_CFG_V13 || n == FACE_CFG_V21) { persist_read_data(KEY_FACE_CFG, &s_cfg, n); face_cfg_fix(); } }
  if (persist_exists(KEY_FACE_WX) && persist_get_size(KEY_FACE_WX) == (int)sizeof s_wx) { persist_read_data(KEY_FACE_WX, s_wx, sizeof s_wx); s_wx_ok = 1; }
#ifdef FACE_FAKE
  s_wx[0] = 215; s_wx[1] = 262; s_wx[2] = 118; s_wx_ok = 1;      // 21.5 C now, 26.2 high, 11.8 low
#endif
}
static void face_burst(void);
static void face_inbox(DictionaryIterator *it, void *ctx) {
  Tuple *t = dict_find(it, MESSAGE_KEY_CFG);
  if (t && (t->length == sizeof s_cfg || t->length == FACE_CFG_OLD || t->length == FACE_CFG_V13 || t->length == FACE_CFG_V21)) { s_cfg.vibe = 0; memcpy(&s_cfg, t->value->data, t->length); face_cfg_fix(); persist_write_data(KEY_FACE_CFG, &s_cfg, sizeof s_cfg); }
  t = dict_find(it, MESSAGE_KEY_WX);
  if (t && t->length == sizeof s_wx) { memcpy(s_wx, t->value->data, sizeof s_wx); s_wx_ok = 1; persist_write_data(KEY_FACE_WX, s_wx, sizeof s_wx); }
  t = dict_find(it, MESSAGE_KEY_CMD);
  if (t) { s_f_cmd = t->length >= 4 ? (uint8_t)t->value->int32 : t->value->uint8; face_burst(); }   // (Controls on the settings page: handled at the next think, when the game is at rest)
  face_clk_changed();
}

// ---- the clock: Pixel Dungeon's own 3x5 digits (font1x.png), drawn big with an outline, between the HUD and the hero
static void clock_proc(Layer *l, GContext *ctx) {
  if (!s_cfg.clk_on) return;
  time_t t = time(NULL); struct tm *tm = localtime(&t);
  int h = tm->tm_hour, m = tm->tm_min;
  if (!face_h24()) { h %= 12; if (!h) h = 12; }
  int dg[5] = { h / 10, h % 10, 10, m / 10, m % 10 };
  bool lead = face_h24() || h >= 10;
  bool hud = s_scene == SC_GAME || s_scene == SC_TARGET || s_scene == SC_INSPECT;   // the HUD is up: the clock sits just under it
  bool win = !hud && s_scene != SC_DEAD && s_scene != SC_AMULET && s_scene != SC_TITLE && s_scene != SC_SELECT;   // a window (the backpack...): the clock small at the top, clear of its numbers
  int k = win ? 3 : face_k(), adv = 4 * k;
  int first = lead ? 0 : 1;
  int total = (first ? 1 : 2) * adv + 2 * k + 2 * adv - k;
  int x = (s_w - total) / 2, y = hud ? PBL_IF_ROUND_ELSE(79, SC(35) + 4) : PBL_IF_ROUND_ELSE(win ? 8 : 22, win ? 4 : 2);
  int xs[5], cx0 = x;
  for (int i = first; i < 5; i++) { xs[i] = cx0; cx0 += i == 2 ? 2 * k : adv; }
  for (int pass = s_cfg.clk_line ? 0 : 2; pass < 3; pass++) {
    GColor c = pass == 2 ? GColorWhite : GColorBlack;
    int grow = pass == 0 ? 3 : pass == 1 ? 2 : 0, ox = pass == 0 ? 2 : 0, oy = pass == 0 ? 2 : 0;   // a drop shadow, an outline, the digits
    for (int i = first; i < 5; i++) fclk_glyph(ctx, dg[i], xs[i] + ox, y + oy, k, grow, c);
  }
}
static bool face_endless(void) { return s_cfg.burst == 3 || battery_state_service_peek().is_plugged; }
static void minute_tick(struct tm *tm, TimeUnits u) { if (s_clock_layer) layer_mark_dirty(s_clock_layer); if (face_hud_dyn() && s_layer) redraw(); if (!s_f_on && face_endless()) face_burst(); }   // (endless: a paused face starts again within a minute)

// ---- bursts
static void face_pause(void) {
  if (!s_f_on) return;
#ifdef FACE_ACTLOG
  app_log(APP_LOG_LEVEL_INFO, "face", 0, "P");
#endif
  s_f_on = false;
  if (g_game && G.hero.HP > 0 && s_scene == SC_GAME && s_fp == 0) { ui_win_free(); game_save(); }   // where it stands, saved (no timer runs from here)
  if (s_timer) app_timer_cancel(s_timer);
  s_timer = NULL;
#if defined(FACE_LOGS) || defined(FACE_HEAPLOG)      // FACE_HEAPLOG: a test build with only this line (FACE_LOGS no longer fits)
  app_log(APP_LOG_LEVEL_INFO, "face", 0, "paused: depth %d heap free %d, lowest this burst %d (loading resource %d; at a frame %d in scene %d)", G.depth, (int)heap_bytes_free(), g_heap_min, g_heap_id, s_f_tlow, s_f_tsc);
#endif
  g_heap_min = s_f_tlow = 0x7FFFFFFF;
  if (s_scene == SC_GAME && (s_fc_held || cam_free() || zoom_now() != s_zuser)) { s_fc_held = false; s_cam_dx = s_cam_dy = 0; s_glide_t0 = 0; s_zto = -1; s_zoom = s_zuser; }   // (frozen zoomed out in the middle of a hold: no)
  redraw();
}
static void face_burst(void) {
  uint32_t n = now_ms();
#ifdef FACE_ACTLOG
  app_log(APP_LOG_LEVEL_INFO, "face", 0, "B %d", s_f_on);
#endif
  s_f_end = n + (FACE_BURST_MS >> (s_cfg.burst & 3)); s_f_last = n; s_f_next = n + 300;
  if (s_f_on) { if (!s_timer) busy(); return; }   // (running: but with no timer it is stuck: kicked)
  s_f_on = true; s_last_in = n; s_timer = NULL; busy();
}
static void face_tap(AccelAxisType axis, int32_t dir) { face_burst(); }
#ifdef FACE_BTNTEST
#define EVL(a, b) app_log(APP_LOG_LEVEL_INFO, "face", 0, "EV %d", (a) + (b))   // codes: 100 appear, 200 focus (+1 in), 300 will_focus (+1), 400 disappear, 500 boot, 600 deinit, 700 click (+button), 800 unob
#endif
static void face_appear(Window *w) {
#ifdef FACE_BTNTEST
  EVL(100, 0);
#endif
  if (g_game) face_burst();
}
static void face_unob(void *c) {                       // the timeline's quick view came or went over the face
#ifdef FACE_BTNTEST
  EVL(800, 0);
#endif
  if (g_game) face_burst();
}
static void face_focus(bool in_focus) {                // the face got the screen back (a notification or menu that covered it has gone)
#ifdef FACE_BTNTEST
  EVL(200, in_focus);
#endif
  if (in_focus && g_game) face_burst();
}
#ifdef FACE_BTNTEST                                    // test build: does anything reach the face when Back (or any button) is pressed?
static void bt_disappear(Window *w) { EVL(400, 0); }
#endif

static void face_new_game(void) {
  bool had = persist_exists(KEY_FACE_CLS); int last = had ? persist_read_int(KEY_FACE_CLS) & 3 : 0;
  uint32_t r = (uint32_t)time(NULL) * 2654435761u ^ now_ms() * 40503u ^ (uint32_t)last * 2246822519u;   // random, from its own mix (the game's RNG is left alone)
  r ^= r >> 15; r *= 2246822519u; r ^= r >> 13;
  int c = fb_pick_class(s_cfg.cls, had, last, r);   // among the classes ticked in the settings, never the last hero's twice in a row (unless it is the only one)
  persist_write_int(KEY_FACE_CLS, c);
  s_fp = 0; s_f_vic = 0; s_hl = -1; s_fp_bad = 0; s_fp_chk = 0; s_fb_depth = -1;
  g_ovr = (Ovr){ 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; memset(g_drop, 0xFF, sizeof g_drop); g_goal = g_from = -1;   // nothing of the last hero's plans carries over
  sheet_free(&s_gameover); sheet_free(&s_arcs); sheet_free(&s_avatars);   // the victory screen's pictures out of the heap before a floor is built (it needs one big block)
  game_art(false); mob_sheets_free(); game_free_fx(); mreg_drop();   // what the last hero's floor held (pictures, creatures' sheets, the region's creatures) out of the heap while the next floor is built: it needs one big block
#ifdef FACE_NGLOG                                      // test build: the heap's low point of the new game alone (the heap log of the pauses, FACE_HEAPLOG, no longer fits beside it)
  { int m0 = g_heap_min; g_heap_min = 0x7FFFFFFF; s_sel = c; start_game(false);
    app_log(APP_LOG_LEVEL_INFO, "face", 0, "NG d%d low %d res %d", G.depth, g_heap_min, g_heap_id); if (m0 < g_heap_min) g_heap_min = m0; }
#else
  s_sel = c; start_game(false);
#endif
  APP_LOG(APP_LOG_LEVEL_INFO, "new game: class %d", c);
}
static void press(ButtonId id, uint32_t wait) {          // a button, as main.c's button() / click_back() would route it (without the real handlers: they cost app size)
  s_f_next = now_ms() + wait;
  if (OWN_INPUT(s_scene)) { ui_input(id, 0, 0); return; }          // the windows
  if (s_scene == SC_GAME && id == BUTTON_ID_SELECT) {
    if (s_tg >= 0) { ActResult r = tg_do(); if (r == ACT_NONE) { g_ovr.ha = 255; tg_refresh(1); s_tgi = 0; } act(r); }
    else act(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? game_tap_cell(G.hero.pos) : game_search());
  }
  else if (s_scene == SC_TARGET && id == BUTTON_ID_SELECT) {
    int t = s_tgk == TK_BAG && s_tg >= 0 && mob_at(s_tg) < 0 ? s_tg   // day-WI: a thing thrown onto a known trap (springs it): the Assist's entry cell (day-WJ: before any creature the aim screen has picked)
      : s_aim_mob >= 0 && G.mobs[s_aim_mob].HP > 0 ? G.mobs[s_aim_mob].pos : game_nearest_enemy();
    if (t >= 0) throw_at(t); else { s_scene = SC_GAME; s_aim_q = s_aim_mob = -1; redraw(); }
  }
  else if (s_scene != SC_GAME) { ui_win_free(); s_scene = G.hero.HP > 0 ? SC_GAME : SC_DEAD; s_aim_q = s_aim_mob = -1; redraw(); }   // info, target...: closed (its window's code out of the arena first: the target overlay needs all of it, else it takes 8 KB of the heap)
}
static void back_out(void) { press(BUTTON_ID_BACK, NAV_MS); }
// A plan through the windows (the backpack, an item, its options) that has not done its thing in FP_MS is dropped: the windows are closed
// and no plan is made for FP_COOL_MS (the game's own way meanwhile). Nothing keeps him in the backpack for a whole burst.
#define FP_MS 18000
#define FP_COOL_MS 40000
static uint32_t s_fp_age, s_fp_cool; static bool s_fp_abort;   // play time (ms) since the plan began / left of the pause from plans (counted in face_tick: a burst ending mid-plan does not use it up)
static bool fp_cool(void) { return s_fp_cool > 0; }
static void fp_begin(void) { s_fp_age = 0; s_fp_abort = false; }

// ---- using an item the way a player does: open the backpack, step to it, open it, pick the action
static bool bag_find(int slot, int *ui, int *tab) {
  int nt = bag_ntabs() ? bag_ntabs() : 1, save = s_bag_tab, found = -1, ft = 0;
  for (int t = 0; t < nt && found < 0; t++) { s_bag_tab = t; for (int i = 0; i < BAG_UI_TAB0 - 1; i++) if (bag_ui_to_slot(i) == slot) { found = i; ft = t; break; } }
  s_bag_tab = save; *ui = found; *tab = ft; return found >= 0;
}
#ifdef PBL_ROUND                                       // Gabbro's backpack has rows of 5, 7, 7, 5 (ovl/bag/bag.c bag_cell), not 5 by 5: the row of a cell as it is drawn
static int bag_row(int c) { return c >= BAG_UI_TAB0 ? 5 : c < 5 ? 0 : c < 12 ? 1 : c < 19 ? 2 : c < 24 ? 3 : 4; }
#else
static int bag_row(int c) { return c >= BAG_UI_TAB0 ? 5 : c / BAG_COLS; }
#endif
static void face_bag(void) {
  if (s_pick >= 0) {                                   // a selector (read an upgrade scroll: which item?): the hand's pick is under the cursor
    if (++s_fp_n > 4) { s_fp_n = 0; back_out(); return; }
    press(BUTTON_ID_SELECT, NAV_MS); return;
  }
  if (s_fp != 1) { back_out(); return; }
  int u, t;
  if (!game_slot(s_fp_slot)->kind || !bag_find(s_fp_slot, &u, &t) || ++s_fp_n > 70) { s_fp = 0; s_fp_bad = 1; back_out(); return; }
  bool row = s_sel >= BAG_ROWM; int c = s_sel - (row ? BAG_ROWM : 0);
  int goal = s_bag_tab != t ? BAG_UI_TAB0 + t : u;     // the tab first, when the item is in another bag
  if (row) { press(bag_row(c) == bag_row(goal) ? BUTTON_ID_SELECT : BUTTON_ID_DOWN, NAV_MS); return; }
  press(c == goal ? BUTTON_ID_SELECT : BUTTON_ID_DOWN, NAV_MS);
}
static void face_item(void) {
  int j = -1;
  for (int i = 0; i < s_n_acts; i++) if (s_acts[i] == s_fp_act) j = i;
  if (s_fp != 2 && s_fp != 1) { s_fp_bad = 1; back_out(); return; }
  if (j < 0 || ++s_fp_n > 20) { s_fp = 0; s_fp_bad = 1; back_out(); return; }
  s_fp = 2;
  if (s_sel == j) { s_fp_chk = s_fp_act != ACN_QUICK; s_fp_t0 = G.hero.time; s_fp = 0; press(BUTTON_ID_SELECT, pace(500, 1)); } else press(BUTTON_ID_DOWN, NAV_MS);
}


// ---- the quickslots: wands and thrown weapons are used from their slot (press it, then aim), never through the backpack.
// The bot keeps the best of them in the three slots, re-assigning through the real windows (item, "Quickslot", the slot).
static int fq_score(int k, const Item *it) {
  switch (k) {
    case IK_DART: return 10; case IK_INCENDIARY_DART: return 12; case IK_CURARE_DART: return 14; case IK_SHURIKEN: return 18;
    case IK_JAVELIN: return 20; case IK_TOMAHAWK: return 25; case IK_BOOMERANG: return 8;
    case IK_WND_FIREBOLT: case IK_WND_LIGHTNING: case IK_WND_DISINTEGRATION: case IK_WND_MM: return 60 + it->ench;
    case IK_WND_AVALANCHE: return 55; case IK_WND_POISON: return 45; case IK_WND_SLOW: return 40;
    default: return 0;
  }
}
static int fq_bag_score(int k) {                       // the best score in the bag for that kind (0: none, or a wand not known / empty)
  int sl = game_bag_find(k); if (sl < 0) return 0;
  const Item *it = game_slot(sl);
  if (item_cat(k) == CAT_WAND && (!item_known(k) || it->ench <= 0)) return 0;
  return fq_score(k, it);
}
static int fq_slot_of(int kind) { for (int q = 0; q < NQ; q++) if (kind && *qkp(&G.hero, q) == kind) return q; return -1; }
// the next re-assignment worth making: sets s_fp_slot (bag slot) and s_fq_q; false: the slots hold the best already
static int8_t s_fq_q; static int16_t s_fq_done = -1;   // (the last plan made: not twice)
static bool fq_plan(void) {
  int best = 0, bk = 0;
  ALL_SLOTS(sl) { const Item *it = game_slot(sl); int k = it->kind, c = item_cat(k); if (c != CAT_MISSILE && c != CAT_WAND) continue;
    int sc = fq_bag_score(k); if (sc <= 0 || fq_slot_of(k) >= 0) continue;
    if (sc > best) { best = sc; bk = k; s_fp_slot = sl; } }
  if (!bk) return false;
  int q = -1, qs = best;                               // an empty slot, else the weakest one that is worth less than the newcomer
  for (int i = 0; i < NQ; i++) { int k = *qkp(&G.hero, i); int sc = k ? fq_bag_score(k) : -1; if (sc < qs) { qs = sc; q = i; } }
  if (q < 0) return false;
  if ((bk << 2 | q) == s_fq_done) return false;
  s_fq_done = bk << 2 | q; s_fq_q = q; return true;
}
// ---- the camera: a zoom that shows what he is up to. Close in for a fight; otherwise the target a few steps away, the zoom
// out as far as it takes to show him and it (main.c cam_to), held for a moment so it can be read, then close in for the run.
#ifdef FACE_OLDPACE
#define FC_HOLD_MS 1100
#define FC_GAP_MS 1200
#else
#define FC_HOLD_MS pace(650, 0)                                 // a glance: the swoop out (300 ms) and a moment to read
#define FC_GAP_MS 3000
#endif
static bool face_cam_ok(void) { return false; }        // (main.c's tg_refresh asks it only with Assist Max; the face is on High: face_cam below does the camera)
static bool face_far(int c) {                          // the target is out of the closest zoom's reach (cam_to would zoom out)
  int h = G.hero.pos, dx = (c & 31) - (h & 31), dy = (c >> 5) - (h >> 5);
  return abs(dx) > FIT[2][0] || abs(dy) > FIT[2][1];
}
static bool face_melee(void) { int e = game_nearest_enemy(); return e >= 0 && distance(e, G.hero.pos) <= 2; }
static bool face_cam_hold(void) {                      // a far place on show (zoomed out): held for a moment, then in for the run. true: this think is used up
  if (!s_fc_held) return false;
  uint32_t n = now_ms();
  if (s_walking || face_melee() || (zoom_now() == 2 && !cam_free())) { s_fc_held = false; return false; }   // a fight or a walk cuts it short
  if ((int32_t)(n - s_fc_hold) < 0) { s_f_next = s_fc_hold; return true; }
#ifdef FACE_OLDPACE
  s_fc_held = false; cam_center(); redraw(); s_f_next = n + 450; return true;
#else
  s_fc_held = false; cam_center(); redraw(); return false;     // (in again while the walk starts: no wait for the camera)
#endif
}
// c: where he is going (the walk's destination or the Assist's target), -1 none. Fight: in at once. A target the closest zoom
// shows: stay in (centred). A far one, new and not too soon after the last change: zoomed out to show both, held ~1.1 s (face_cam_hold).
static bool face_cam(int c) {
  uint32_t n = now_ms();
  if (face_melee()) { s_fc_key = -1; if (zoom_now() != 2 || cam_free()) { s_fc_t = n; cam_center(); redraw(); s_f_next = n + 150; return true; } return false; }
  if (c < 0) return false;
  int32_t key = s_tgk == TK_ENEMY && c == s_tg ? 0x10000 | s_tgu : c;
  if (!face_far(c) || s_cfg.fps > 1) { if (cam_free() || zoom_now() != 2) { cam_center(); redraw(); } return false; }
  if (key == s_fc_key || (int32_t)(n - s_fc_t) < FC_GAP_MS) return false;      // the same place, or a change not long ago: no flicker
  s_fc_key = key; s_fc_t = n; cam_to(c);
  s_fc_held = true; s_fc_hold = n + FC_HOLD_MS; redraw(); s_f_next = s_fc_hold; return true;
}
static void face_game(void) {
  { static int8_t d; if (d != G.depth) { d = G.depth; APP_LOG(APP_LOG_LEVEL_INFO, "depth %d, lvl %d, hp %d/%d, last floor %d presses, %d since news", d, G.hero.lvl, G.hero.HP, G.hero.HT, s_fb_presses, s_fb_since); } }
  if (s_fp_chk) { s_fp_chk = 0; if (G.hero.time == s_fp_t0) s_fp_bad = 1; }   // the windows' way did nothing (a throw with no clear line): the game's own way this time
  if (s_hl >= 0) { s_hl = -1; redraw(); }
  if (face_cam_hold()) return;
  int ov = fb_override();
  if (ov >= 0 && s_scene == SC_GAME && !fb_foe() && face_cam(ov)) return;   // a far place to walk to (stairs, the unexplored): zoomed out to show the way first
  if (ov != -1) APP_LOG(APP_LOG_LEVEL_INFO, "override %d: exit %d entrance %d amulet %d since %d presses %d", ov, G.exit, G.entrance, game_bag_find(IK_AMULET), s_fb_since, s_fb_presses);
  if (ov == FB_DROP) { act(game_item_do(s_fb_slot, ACN_DROP)); s_f_next = now_ms() + STEP_MS * 2; return; }
  if (ov == FB_LEVEL) { act(ACT_LEVEL); s_f_next = now_ms() + 400; return; }
  if (ov >= 0) { ActResult r = game_tap_cell(ov); if (s_scene != SC_GAME) { s_f_next = now_ms() + STEP_MS; return; }   // (the win: the Surface scene is up)
    if (r != ACT_NONE) { act(r); s_f_next = now_ms() + STEP_MS; return; } }
  tg_refresh(0);
  if (s_scene == SC_GAME && face_cam(s_tg >= 0 && s_tgk != TK_BAG ? s_tg : -1)) return;
  if (s_tg >= 0 && s_tgk == TK_BAG && g_ovr.ha == ACN_THROW && !fb_throw(s_tg)) s_fp_bad = 1;   // day-WJ: a throw onto a trap that has not landed twice: not again (the Assist plans the way over the traps)
  if (s_tg >= 0 && s_tgk == TK_BAG && !s_fp_bad && g_ovr.ha != ACN_QUICK && (g_ovr.ha == ACN_THROW || g_ovr.ha == ACN_ZAP)) {   // a throw or a zap from a quickslot: its button, then aim
    int q = fq_slot_of(game_slot(g_ovr.hs)->kind);
    if (q >= 0) { s_fp_n = 0; s_walking = false; APP_LOG(APP_LOG_LEVEL_INFO, "quickslot %d: action %d", q, g_ovr.ha); quickslot(q); s_f_next = now_ms() + NAV_MS; return; }
  }
  if (!s_fp_bad && !fp_cool() && s_scene == SC_GAME && game_nearest_enemy() < 0 && s_fb_presses % 20 == 7 && fq_plan()) {   // rarely, with nothing about: a better wand or missile into a slot
    s_fp = 1; s_fp_n = 0; s_fp_act = ACN_QUICK; s_walking = false; fp_begin();
    FL("qp %d %d", s_fp_slot, s_fq_q); APP_LOG(APP_LOG_LEVEL_INFO, "quickslot plan: bag slot %d into slot %d", s_fp_slot, s_fq_q); open_bag(-1); s_f_next = now_ms() + pace(650, 1); return;
  }
  if (s_tg >= 0 && s_tgk == TK_BAG && !s_fp_bad && !fp_cool() && game_slot(g_ovr.hs)->kind) {   // the bot's hands: through the windows
    s_fp = 1; s_fp_n = 0; s_fp_slot = g_ovr.hs; s_fp_act = g_ovr.ha; fp_begin();
    APP_LOG(APP_LOG_LEVEL_INFO, "hands: slot %d action %d", s_fp_slot, s_fp_act);
    s_walking = false; open_bag(-1); s_f_next = now_ms() + pace(650, 1); return;
  }
  s_fp_bad = 0;
#ifdef FACE_LOGP
  APP_LOG(APP_LOG_LEVEL_INFO, "press d%d: kind %d cell %d hero %d exit %d n %d vis %d", G.depth, s_tgk, s_tg, G.hero.pos, G.exit, s_tgn, fb_seen());
#endif
  press(BUTTON_ID_SELECT, STEP_MS);
}
static bool face_cmd(void) {                           // a command from the settings page (Controls). true: this think is used up
  if (!s_f_cmd) return false;
  if (s_f_cmd == 1) { s_f_cmd = 0; ui_win_free(); face_new_game(); s_f_next = now_ms() + 600; redraw(); return true; }   // a new run: any window closed first
  if (G.hero.HP <= 0) { s_f_cmd = 0; return false; }
  if (s_scene != SC_GAME || s_fp) return false;                            // (a teleport waits for the game's own view: no window open, no item plan under way)
  s_f_cmd = 0; if (fb_teleport()) { cam_center(); redraw(); s_f_next = now_ms() + 300; return true; }
  return false;
}
static void face_think(void) {
  fb_scene(s_scene, &s_fp_n);                                              // (each window counts its own presses)
  if (face_cmd()) return;
  if (s_scene == SC_GAME) { s_fp_abort = false; s_fp_age = 0; }
  else if (!s_fp_abort && (s_fp || s_scene == SC_OPTIONS) && OWN_INPUT(s_scene) && s_fp_age > pace(FP_MS, 0)) {   // a plan taking too long
    FL("dropped %d %d %d", s_scene, s_fp, s_fp_n);
    s_fp = 0; s_fp_bad = 1; s_fp_abort = true; s_fp_cool = FP_COOL_MS; }
  if (s_fp_abort && (s_scene == SC_BAG || s_scene == SC_ITEM || s_scene == SC_OPTIONS || s_scene == SC_TARGET)) { s_pick = -1; back_out(); return; }
  switch (s_scene) {
    case SC_GAME: face_game(); break;
    case SC_BAG: face_bag(); break;
    case SC_ITEM: face_item(); break;
    case SC_OPTIONS: s_sel = s_opt == 4 ? s_fq_q : 0; press(BUTTON_ID_SELECT, NAV_MS); break;          // yes / the first choice
    case SC_TARGET: if (++s_fp_n > 3) { s_fp_n = 0; s_fp_bad = 1; back_out(); } else press(BUTTON_ID_SELECT, NAV_MS); break;
    case SC_AMULET:
      if (s_amulet == 2) { if (s_f_vic >= FACE_WIN_MS) face_new_game(); else s_f_next = now_ms() + 500; }
      else back_out();                                                       // the Amulet's scene: carry on
      break;
    case SC_DEAD: case SC_TITLE: case SC_SELECT: face_new_game(); break;
    default: back_out();
  }
}
#ifdef FACE_OLDPACE
static uint32_t face_wake(uint32_t ms) { return ms; }
#else
static uint32_t face_wake(uint32_t ms) { if (ms < face_fi()) ms = face_fi(); int32_t d = (int32_t)(s_f_next - now_ms()); return d > 0 && (uint32_t)d < ms ? (d < 12 ? 12 : (uint32_t)d) : ms; }
#endif
static void face_tick(bool moving) {
  if (!s_f_on) return;
  { int f = (int)heap_bytes_free(); if (f < g_heap_min) g_heap_min = f;
    if (f < s_f_tlow) { s_f_tlow = f; s_f_tsc = s_scene; } }
  uint32_t n = now_ms(), dt = n - s_f_last; s_f_last = n; if (dt > 1500) dt = 33;
  s_fp_age += dt; s_fp_cool = s_fp_cool > dt ? s_fp_cool - dt : 0;
  s_last_in = n;
#ifdef FACE_LOGS
  { int f = (int)heap_bytes_free(); if (f < s_f_min) { s_f_min = f; } }
#endif
#ifdef FACE_ACTLOG                                     // test build: one line per game turn and per camera/think event, with the ms since the last
  { static int32_t ln = -1; static uint32_t lt; if (G.now != ln) { ln = G.now; app_log(APP_LOG_LEVEL_INFO, "face", 0, "A %u %u %d%d%d", (unsigned)(n - s_f_end + FACE_BURST_MS), (unsigned)(n - lt), s_scene, s_walking, s_fc_held); lt = n; } }
#endif
  if (s_scene == SC_AMULET && s_amulet == 2) s_f_vic += dt;            // the victory screen is counted in play time
#ifndef FACE_OLDPACE
  if ((int32_t)(s_f_next - n) > (int32_t)pace(700, 0)) s_f_next = n;
#endif                   // (no wait is longer: a clock that stepped a second back made them so)
  if (face_endless()) s_f_end = n + 2000;               // endless play (the setting, or on the charger): the burst never runs out
  if ((int32_t)(n - s_f_end) >= 0) { face_pause(); return; }
  if (s_scene == SC_AMULET && s_amulet == 2 && s_f_vic >= FACE_WIN_MS) { face_new_game(); s_f_next = n + 600; return; }   // 30 s of victory: the next hero
  if (moving || s_fade || s_proj.active || s_walking || (int32_t)(n - s_f_next) < 0) return;
  face_think();
}
static void face_boot(void) {
  s_fp_cool = 0; s_fp_abort = false;
  s_fc_t = now_ms() - 5000;                                            // (now_ms() is the epoch's low 32 bits: 0 is not "long ago" as a signed difference: the camera gate never opened)
  g_assist = 3; s_zoom = s_zuser = 2; s_zto = -1;                      // Assist High; the closest zoom (face_game's camera moves out to a far target and back)
  s_has_save = game_peek_save(&s_save_depth, &s_save_lvl);
  face_cfg_load();                                                     // (before any game starts: the rules and the starting stage are the settings')
#ifdef FACE_WINTEST                                   // test build: a new game that already holds the Amulet, on its way up from floor 1
  face_new_game(); bag_add(item_make(IK_AMULET, 1)); G.dflags |= DF_AMULET;
#elif defined(FACE_NEWGAME)
  face_new_game();
#ifdef FACE_QPTEST                                    // test build: javelins, a tomahawk and darts in the pack, to see the quickslot plan through the windows
  { static const int F[] = { IK_MACE, IK_LONGSWORD, IK_GLAIVE, IK_BATTLE_AXE, IK_PLATE_ARMOR, IK_CLOTH_ARMOR, IK_DAGGER, IK_KNUCKLES, IK_POT_FROST, IK_POT_MIND }; for (unsigned i = 0; i < sizeof F / sizeof *F; i++) bag_add(item_make(F[i], 1)); }   // (fillers: the throwables land in the later rows)
  bag_add(item_make(IK_JAVELIN, 3)); bag_add(item_make(IK_TOMAHAWK, 2)); bag_add(item_make(IK_SHURIKEN, 4)); bag_add(item_make(IK_CURARE_DART, 4));
#endif
#else
  if (s_has_save) { s_sel = 0; start_game(true); } else face_new_game();
#endif
  app_message_register_inbox_received(face_inbox); app_message_open(32, 0);      // the settings and the weather, from the phone (pkjs)
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  accel_tap_service_subscribe(face_tap);
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){ .did_change = face_unob }, NULL);
  app_focus_service_subscribe_handlers((AppFocusHandlers){ .did_focus = face_focus
#ifdef FACE_BTNTEST
#endif
  });
#ifdef FACE_BTNTEST
  EVL(500, 0);

#endif
  s_f_on = false;
#ifdef FACE_BAGTEST                                   // test build: the backpack open and still
  open_bag(-1); redraw();
#else
  face_burst();
#endif
}
