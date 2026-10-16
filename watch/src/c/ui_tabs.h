/* Pixel Dungeon for Pebble - PD WndTabbed's frame and tabs, and the big buttons (0.18), shared by the window overlays */
#pragma once
#include "ui_int.h"
#ifdef PBL_ROUND
// 0.35 Gabbro: a window is its box cut by a circle just inside the screen's rim. PD's frame (a light edge, the grey
// border, the body) follows the cut, so a tall window becomes a disc and its text can flow along the curve.
#define R_OUT 127                                    // the frame's outer edge
#define R_IN 121                                     // the body's edge (the frame is 6 px, as PD's)
static int rhalf(int y, int r) {                     // half the chord of a centred circle of radius r at row y (0: missed)
  int d = y - s_h / 2, q = r * r - d * d, h = 0;
  if (q <= 0) return 0;
  for (int b = 128; b; b >>= 1) if ((h + b) * (h + b) <= q) h += b;
  return h;
}
// a row of a box clipped to the circle of radius r
static void rrow(int x0, int x1, int y, int r, uint8_t c) {
  int h = rhalf(y, r), a = s_w / 2 - h > x0 ? s_w / 2 - h : x0, b = s_w / 2 + h < x1 ? s_w / 2 + h : x1;
  if (h && b > a) blit_fill(a, y, b - a, 1, c);
}
static void rfill(int x, int y, int w, int h, uint8_t c) { for (int j = y; j < y + h; j++) rrow(x, x + w, j, R_IN, c); }
static void rpanel(GRect r, uint8_t body) {
  int x0 = r.origin.x - 6, x1 = r.origin.x + r.size.w + 6, y0 = r.origin.y - 6, y1 = r.origin.y + r.size.h + 6;
  for (int y = y0; y < y1; y++) {
    bool edge = y == y0 || y == y1 - 1;
    rrow(x0, x1, y, R_OUT, ARGB(GColorLightGray));
    if (!edge) rrow(x0 + 1, x1 - 1, y, R_OUT - 2, ARGB(GColorDarkGray));
    if (y >= r.origin.y - 1 && y <= r.origin.y + r.size.h) rrow(r.origin.x - 1, r.origin.x + r.size.w + 1, y, R_IN + 1, ARGB(GColorBlack));
    if (y >= r.origin.y && y < r.origin.y + r.size.h) rrow(r.origin.x, r.origin.x + r.size.w, y, R_IN, body);
  }
}
#define panel(r) rpanel(r, ARGB(GColorBlack))
static GRect rfit(GRect b) {                         // a button narrowed to stay inside the window's curve
  int t = b.origin.y, u = t + b.size.h - 1, h = rhalf(abs(t - s_h / 2) > abs(u - s_h / 2) ? t : u, R_IN - 2);
  int a = s_w / 2 - h, c = s_w / 2 + h, x0 = b.origin.x > a ? b.origin.x : a, x1 = b.origin.x + b.size.w < c ? b.origin.x + b.size.w : c;
  return GRect(x0, t, x1 - x0, b.size.h);
}
static int rleft(GRect r) { int a = s_w / 2 - rhalf(r.origin.y + 2, R_IN - 3); return a > r.origin.x ? a : r.origin.x; }   // where a window's top line can start
#define r_flow_h flow_h
static void r_text_place(GRect f, const char *t) {   // the text layer's place and height: redrawn when they change
  int h = r_flow_h(t, f) + 4;
  if (!grect_equal(&f, &s_text_r) || h != s_text_h) { s_text_r = f; s_text_h = h; redraw(); }
}
static void tab_panel(GRect r) { rpanel(r, ARGB(GColorDarkGray)); }
#undef WELL
#define WELL(x, y, w, h) rfill((x) - 2, (y), (w) + 4, (h), ARGB(GColorBlack))
#else
static void tab_panel(GRect r) { blit_nine(&s_chrome, 64, 0, 22, 22, 7, r.origin.x - 6, r.origin.y - 6, r.size.w + 12, r.size.h + 12); }
#endif

#ifdef PBL_ROUND
// 0.83 (owner: "flat against the menu in a row ... their frames merge into the menu frame"): the round frame's bottom is
// black (cb), dark (cb+1..4), light (cb+5, outermost). A tab hangs from it with the same lines, 4 px: the chosen one opens
// into the window (its black and light cut), the others start under the frame, its light line their top
static void tab_art(GRect t, bool selected) {
  int x = t.origin.x, w = t.size.w, cb = t.origin.y, bot = cb + t.size.h, top = selected ? cb : cb + 6;
  uint8_t L = ARGB(GColorLightGray), D = ARGB(GColorDarkGray), K = ARGB(GColorBlack);
  blit_fill(x, top, w, bot - top, D);
  blit_fill(x, cb + 5, 1, bot - cb - 5, L); blit_fill(x + w - 1, cb + 5, 1, bot - cb - 5, L); blit_fill(x, bot - 1, w, 1, L);
  blit_fill(x + 3, top, 1, bot - 4 - top, K); blit_fill(x + w - 4, top, 1, bot - 4 - top, K); blit_fill(x + 3, bot - 4, w - 6, 1, K);
}
#else
static void tab_art(GRect t, bool selected) {
  int x = t.origin.x, w = t.size.w, cb = t.origin.y, bot = cb + t.size.h - (selected ? 0 : 1);
  int top = selected ? cb : cb + 4;                  // the window's bottom edge: cb black, cb+1 light, cb+2..3 dark
  int kt = selected ? cb : top, lt = selected ? cb + 1 : top, dt = selected ? cb + 2 : top;   // where each line starts
  uint8_t L = ARGB(GColorLightGray), D = ARGB(GColorDarkGray), K = ARGB(GColorBlack);
  if (selected) blit_fill(x + 4, cb, w - 8, 4, D);   // open into the window
  blit_fill(x + 4, top, w - 8, bot - 4 - top, D);
  // sides, joining the window's black and light lines at the top
  blit_fill(x, lt, 1, bot - 1 - lt, L); blit_fill(x + 1, dt, 2, bot - dt, D); blit_fill(x + 3, kt, 1, bot - 4 - kt, K);
  blit_fill(x + w - 4, kt, 1, bot - 4 - kt, K); blit_fill(x + w - 3, lt, 1, bot - 2 - lt, L); blit_fill(x + w - 2, dt, 2, bot - dt, D);
  blit_fill(x, bot - 1, 1, 1, D); blit_fill(x + w - 3, bot - 2, 1, 2, D);
  // bottom, like the window's: black, light, dark, dark
  blit_fill(x + 3, bot - 4, w - 6, 1, K); blit_fill(x + 3, bot - 3, w - 5, 1, L); blit_fill(x + 1, bot - 2, w - 4, 2, D);
}

#endif
static bool hit(GRect r, int x, int y) { return x >= r.origin.x && y >= r.origin.y && x < r.origin.x + r.size.w && y < r.origin.y + r.size.h; }
static bool tab_hit(GRect t, int x, int y) { return x >= t.origin.x && x < t.origin.x + t.size.w && y >= t.origin.y && y < t.origin.y + t.size.h + 8; }
// a label that fits its button: 18 bold, else 14 bold over two lines
static void big_label(GContext *ctx, const char *s, GRect b, GColor c) {
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GSize z = graphics_text_layout_get_content_size(s, f, GRect(0, 0, b.size.w - 4, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter);
  if (z.h > 24) { f = s_font_hud; z = graphics_text_layout_get_content_size(s, f, GRect(0, 0, b.size.w - 4, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter); }
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, GRect(b.origin.x + 2, b.origin.y + (b.size.h - z.h) / 2 - 4, b.size.w - 4, z.h + 6), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
