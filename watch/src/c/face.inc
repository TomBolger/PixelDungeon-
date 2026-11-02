// ---------------- the watchface (PD_FACE builds; included into main.c) ----------------
// A burst of play (20 s) when the face appears or the wrist is flicked, then everything freezes: no timers, no redraws,
// just the clock layer once a minute. The autoplayer is the Assist's first suggestion (ovl/tgt: the bot's targeting) plus
// face_brain.h; items are used through the real windows, button by button, so a viewer can follow.
#define FACE_BURST_MS 20000
#define FACE_WIN_MS 30000
#define KEY_FACE_CLS 210                              // persist: the class of the next new game (they take turns)
#define STEP_MS 140                                   // a decision after a walk or a hit
#define NAV_MS 330                                    // one button press in a window
static uint32_t s_f_end, s_f_next, s_f_last, s_f_vic;
static int32_t s_fp_t0; static uint8_t s_fp_chk; static uint8_t s_fp; static int8_t s_fp_slot, s_fp_act; static uint8_t s_fp_n, s_fp_bad;   // the item plan: 0 none, 1 in the backpack, 2 in the item window
static Layer *s_clock_layer;

// ---- the clock: Pixel Dungeon's own 3x5 digits (font1x.png), drawn big with an outline
static const uint8_t FD[10][5] = { {3,5,5,5,6}, {2,6,2,2,2}, {6,1,2,4,7}, {6,1,2,1,6}, {5,5,7,1,1}, {7,4,6,1,6}, {3,4,7,5,7}, {7,1,2,4,4}, {7,5,2,5,7}, {7,5,7,1,6} };
static void fclk_glyph(GContext *g, int d, int x, int y, int k, int grow, GColor c) {   // d: 0-9, 10 colon
  graphics_context_set_fill_color(g, c);
  if (d == 10) { for (int r = 1; r < 5; r += 2) graphics_fill_rect(g, GRect(x - grow, y + r * k - grow, k + 2 * grow, k + 2 * grow), 0, GCornerNone); return; }
  for (int r = 0; r < 5; r++) for (int b = 0; b < 3; b++) if (FD[d][r] >> (2 - b) & 1) graphics_fill_rect(g, GRect(x + b * k - grow, y + r * k - grow, k + 2 * grow, k + 2 * grow), 0, GCornerNone);
}
static void clock_proc(Layer *l, GContext *ctx) {
  time_t t = time(NULL); struct tm *tm = localtime(&t);
  int h = tm->tm_hour, m = tm->tm_min;
  if (!clock_is_24h_style()) { h %= 12; if (!h) h = 12; }
  int dg[5] = { h / 10, h % 10, 10, m / 10, m % 10 };
  bool lead = clock_is_24h_style() || h >= 10;
  int k = PBL_IF_ROUND_ELSE(7, 8), adv = 4 * k;
  int n = lead ? 5 : 4, w = (lead ? 4 : 2) * adv + (k + 0) + 4 * k - k;   // two digits, the colon (one cell and a gap), two digits
  int first = lead ? 0 : 1;
  int total = (first ? 1 : 2) * adv + 2 * k + 2 * adv - k;
  (void)n; (void)w;
  int x = (s_w - total) / 2, y = PBL_IF_ROUND_ELSE(22, 2);
  int xs[5], cx0 = x;
  for (int i = first; i < 5; i++) { xs[i] = cx0; cx0 += i == 2 ? 2 * k : adv; }
  for (int pass = 0; pass < 3; pass++) {
    GColor c = pass == 0 ? GColorBlack : pass == 1 ? GColorBlack : GColorWhite;
    int grow = pass == 0 ? 3 : pass == 1 ? 2 : 0, ox = pass == 0 ? 2 : 0, oy = pass == 0 ? 2 : 0;   // a drop shadow, an outline, the digits
    for (int i = first; i < 5; i++) fclk_glyph(ctx, dg[i], xs[i] + ox, y + oy, k, grow, pass == 2 ? GColorWhite : c);
  }
}
static void minute_tick(struct tm *tm, TimeUnits u) { if (s_clock_layer) layer_mark_dirty(s_clock_layer); }

// ---- bursts
static void face_pause(void) {
  if (!s_f_on) return;
  s_f_on = false;
  if (g_game && G.hero.HP > 0 && s_scene == SC_GAME && s_fp == 0) { ui_win_free(); game_save(); }   // where it stands, saved (no timer runs from here)
  if (s_timer) app_timer_cancel(s_timer);
  s_timer = NULL;
  APP_LOG(APP_LOG_LEVEL_INFO, "paused: depth %d heap free %d", G.depth, (int)heap_bytes_free());
  redraw();
}
static void face_burst(void) {
  uint32_t n = now_ms();
  s_f_end = n + FACE_BURST_MS; s_f_last = n; s_f_next = n + 300;
  if (s_f_on) return;
  s_f_on = true; s_last_in = n; s_timer = NULL; busy();
}
static void face_tap(AccelAxisType axis, int32_t dir) { face_burst(); }
static void face_appear(Window *w) { if (g_game) face_burst(); }

static void face_new_game(void) {
  int c = persist_exists(KEY_FACE_CLS) ? persist_read_int(KEY_FACE_CLS) & 3 : 0;
  persist_write_int(KEY_FACE_CLS, (c + 1) & 3);
  s_fp = 0; s_f_vic = 0; s_hl = -1; s_fp_bad = 0; s_fp_chk = 0; s_fb_depth = -1;
  g_ovr = (Ovr){ 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; memset(g_drop, 0xFF, sizeof g_drop); g_goal = g_from = -1;   // nothing of the last hero's plans carries over
  sheet_free(&s_gameover); sheet_free(&s_arcs); sheet_free(&s_avatars);   // the victory screen's pictures out of the heap before a floor is built (it needs one big block)
  s_sel = c; start_game(false);
  APP_LOG(APP_LOG_LEVEL_INFO, "new game: class %d", c);
}
static void press(ButtonId id, uint32_t wait) {          // a button, as main.c's button() / click_back() would route it (without the real handlers: they cost app size)
  s_f_next = now_ms() + wait;
  if (OWN_INPUT(s_scene)) { ui_input(id, 0, 0); return; }          // the windows
  if (s_scene == SC_GAME && id == BUTTON_ID_SELECT) {
    if (s_tg >= 0) { ActResult r = tg_do(); if (r == ACT_NONE) { tg_refresh(1); s_tgi = 0; } act(r); }
    else act(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? game_tap_cell(G.hero.pos) : game_search());
  }
  else if (s_scene == SC_TARGET && id == BUTTON_ID_SELECT) {
    int t = s_aim_mob >= 0 && G.mobs[s_aim_mob].HP > 0 ? G.mobs[s_aim_mob].pos : game_nearest_enemy();
    if (t >= 0) throw_at(t); else { s_scene = SC_GAME; s_aim_q = s_aim_mob = -1; redraw(); }
  }
  else if (s_scene != SC_GAME) { s_scene = G.hero.HP > 0 ? SC_GAME : SC_DEAD; s_aim_q = s_aim_mob = -1; redraw(); }   // info, target...: closed
}
static void back_out(void) { press(BUTTON_ID_BACK, NAV_MS); }

// ---- using an item the way a player does: open the backpack, step to it, open it, pick the action
static bool bag_find(int slot, int *ui, int *tab) {
  int nt = bag_ntabs() ? bag_ntabs() : 1, save = s_bag_tab, found = -1, ft = 0;
  for (int t = 0; t < nt && found < 0; t++) { s_bag_tab = t; for (int i = 0; i < BAG_UI_TAB0 - 1; i++) if (bag_ui_to_slot(i) == slot) { found = i; ft = t; break; } }
  s_bag_tab = save; *ui = found; *tab = ft; return found >= 0;
}
static int bag_row(int c) { return c >= BAG_UI_TAB0 ? 5 : c / BAG_COLS; }
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
  if (s_sel == j) { s_fp_chk = 1; s_fp_t0 = G.hero.time; APP_LOG(APP_LOG_LEVEL_INFO, "do action %d (%d presses)", s_fp_act, s_fp_n); s_fp = 0; press(BUTTON_ID_SELECT, STEP_MS * 3); } else press(BUTTON_ID_DOWN, NAV_MS);
}

static void face_game(void) {
  { static int8_t d; if (d != G.depth) { d = G.depth; APP_LOG(APP_LOG_LEVEL_INFO, "depth %d, lvl %d, hp %d/%d, last floor %d presses, %d since news", d, G.hero.lvl, G.hero.HP, G.hero.HT, s_fb_presses, s_fb_since); } }
  if (s_fp_chk) { s_fp_chk = 0; if (G.hero.time == s_fp_t0) s_fp_bad = 1; }   // the windows' way did nothing (a throw with no clear line): the game's own way this time
  if (s_hl >= 0) { s_hl = -1; redraw(); }
  int ov = fb_override();
  if (ov != -1) APP_LOG(APP_LOG_LEVEL_INFO, "override %d: exit %d entrance %d amulet %d since %d presses %d", ov, G.exit, G.entrance, game_bag_find(IK_AMULET), s_fb_since, s_fb_presses);
  if (ov == FB_DROP) { act(game_item_do(s_fb_slot, ACN_DROP)); s_f_next = now_ms() + STEP_MS * 2; return; }
  if (ov == FB_LEVEL) { act(ACT_LEVEL); s_f_next = now_ms() + 400; return; }
  if (ov >= 0) { ActResult r = game_tap_cell(ov); if (s_scene != SC_GAME) { s_f_next = now_ms() + STEP_MS; return; }   // (the win: the Surface scene is up)
    if (r != ACT_NONE) { act(r); s_f_next = now_ms() + STEP_MS; return; } }
  tg_refresh(0);
  if (s_tg >= 0 && s_tgk == TK_BAG && !s_fp_bad && game_slot(g_ovr.hs)->kind) {   // the bot's hands: through the windows
    s_fp = 1; s_fp_n = 0; s_fp_slot = g_ovr.hs; s_fp_act = g_ovr.ha;
    APP_LOG(APP_LOG_LEVEL_INFO, "hands: slot %d action %d", s_fp_slot, s_fp_act);
    s_walking = false; open_bag(-1); s_f_next = now_ms() + 500; return;
  }
  s_fp_bad = 0;
#ifdef FACE_LOGP
  APP_LOG(APP_LOG_LEVEL_INFO, "press d%d: kind %d cell %d hero %d exit %d n %d vis %d", G.depth, s_tgk, s_tg, G.hero.pos, G.exit, s_tgn, fb_seen());
#endif
  press(BUTTON_ID_SELECT, STEP_MS);
}
static void face_think(void) {
  switch (s_scene) {
    case SC_GAME: face_game(); break;
    case SC_BAG: face_bag(); break;
    case SC_ITEM: face_item(); break;
    case SC_OPTIONS: s_sel = 0; press(BUTTON_ID_SELECT, NAV_MS); break;          // yes / the first choice
    case SC_TARGET: if (++s_fp_n > 3) { s_fp_n = 0; back_out(); } else press(BUTTON_ID_SELECT, NAV_MS); break;
    case SC_AMULET:
      if (s_amulet == 2) { if (s_f_vic >= FACE_WIN_MS) face_new_game(); else s_f_next = now_ms() + 500; }
      else back_out();                                                       // the Amulet's scene: carry on
      break;
    case SC_DEAD: case SC_TITLE: case SC_SELECT: face_new_game(); break;
    default: back_out();
  }
}
static void face_tick(bool moving) {
  if (!s_f_on) return;
  uint32_t n = now_ms(), dt = n - s_f_last; s_f_last = n; if (dt > 1500) dt = 33;
  s_last_in = n;
  if (s_scene == SC_AMULET && s_amulet == 2) s_f_vic += dt;            // the victory screen is counted in play time
  if ((int32_t)(n - s_f_end) >= 0) { face_pause(); return; }
  if (s_scene == SC_AMULET && s_amulet == 2 && s_f_vic >= FACE_WIN_MS) { face_new_game(); s_f_next = n + 600; return; }   // 30 s of victory: the next hero
  if (moving || s_fade || s_proj.active || s_walking || (int32_t)(n - s_f_next) < 0) return;
  face_think();
}
static void face_boot(void) {
  g_assist = 3; s_zoom = s_zuser = 1; s_zto = -1;                      // Assist High, near
  s_has_save = game_peek_save(&s_save_depth, &s_save_lvl);
#ifdef FACE_WINTEST                                   // test build: a new game that already holds the Amulet, on its way up from floor 1
  face_new_game(); bag_add(item_make(IK_AMULET, 1)); G.dflags |= DF_AMULET;
#elif defined(FACE_NEWGAME)
  face_new_game();
#else
  if (s_has_save) { s_sel = 0; start_game(true); } else face_new_game();
#endif
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  accel_tap_service_subscribe(face_tap);
  s_f_on = false; face_burst();
}
