// Draws the watch's copy of Shattered PD's screen, the way SPD's renderer would
// (textured rectangles x colour multiply + add, then alpha), in Pebble's 64 colours.
#include "draw.h"
#include "scene.h"
#include "art.h"

static uint8_t *s_fb;
static int s_stride, s_W, s_H;
// the frame buffer's rows: on a round screen each row holds only the pixels inside the circle
#ifdef PBL_ROUND
static uint8_t *s_row[PBL_DISPLAY_HEIGHT];
static int16_t s_rmin[PBL_DISPLAY_HEIGHT], s_rmax[PBL_DISPLAY_HEIGHT];
#define ROWPTR(y) s_row[y]
#define ROWCLIP(y, a, b) do { if ((a) < s_rmin[y]) (a) = s_rmin[y]; if ((b) > s_rmax[y] + 1) (b) = s_rmax[y] + 1; } while (0)
#define INROW(y, x) ((x) >= s_rmin[y] && (x) <= s_rmax[y])
#else
#define ROWPTR(y) (s_fb + (y) * s_stride)
#define ROWCLIP(y, a, b) ((void)0)
#define INROW(y, x) true
#endif
static int s_cx0, s_cy0, s_cx1, s_cy1;     // clip, px
static uint32_t s_now;

// ---- per-picture colour: a lookup from source pixel to (colour, alpha 0-255) ----
static uint8_t s_lc[256], s_la[256];
static bool s_plain;   // colour untouched and fully opaque: copy pixels straight

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static int q4(int v) { return (v + 42) / 85 > 3 ? 3 : (v + 42) / 85; }

static void colour_setup(const uint8_t *col, int alpha) {
  s_plain = alpha >= 240 && col[0] == 255 && col[1] == 255 && col[2] == 255 && !col[3] && !col[4] && !col[5] && !col[6];
  if (s_plain) return;
  for (int c = 0; c < 256; c++) {
    int a = (c >> 6) * 85, r = ((c >> 4) & 3) * 85, g = ((c >> 2) & 3) * 85, b = (c & 3) * 85;
    r = clamp255(r * col[0] / 255 + col[3]);
    g = clamp255(g * col[1] / 255 + col[4]);
    b = clamp255(b * col[2] / 255 + col[5]);
    a = clamp255(a * alpha / 255 + col[6]);
    s_lc[c] = 0xC0 | (q4(r) << 4) | (q4(g) << 2) | q4(b);
    s_la[c] = a;
  }
}

// see-through pixels are dithered (a 4x4 ordered pattern) rather than blended: with only four levels
// per colour, blending would turn dimmed areas (fog of war over remembered rooms) to plain black
static const uint8_t BAYER[16] = {8, 136, 40, 168, 200, 72, 232, 104, 56, 184, 24, 152, 248, 120, 216, 88};
static inline void put(uint8_t *d, uint8_t c, int x, int y) {
  int A;
  if (s_plain) {
    A = c >> 6;
    if (A == 3) { *d = c; return; }
    if (!A) return;
    A *= 85;
  } else {
    A = s_la[c]; c = s_lc[c];
    if (A > 247) { *d = c; return; }
  }
  if (A > BAYER[((y & 3) << 2) | (x & 3)]) *d = c | 0xC0;
}

// ---- camera ----
static Cam *s_cam;
// camera units x8 -> screen px x8
static inline int32_t scx(int32_t x8) { return s_cam->x + (int32_t)((int64_t)(x8 * 256 - s_cam->sx) * s_cam->zoom / 65536); }
static inline int32_t scy(int32_t y8) { return s_cam->y + (int32_t)((int64_t)(y8 * 256 - s_cam->sy) * s_cam->zoom / 65536); }

// ---- an upright (unturned) picture: source texels [tx, tx+tw) to screen edges L..R, T..B (px x8; reversed = mirrored) ----
static void blit(int tex, int tx, int ty, int tw, int th, int32_t L, int32_t T, int32_t R, int32_t B, bool fx, bool fy) {
  if (tw <= 0 || th <= 0 || L == R || T == B) return;
  int k = art_shrink(tex);
  if (k > 1) { tx /= k; ty /= k; tw = (tw + k - 1) / k; th = (th + k - 1) / k; }
  if (R < L) { int32_t t = L; L = R; R = t; fx = !fx; }
  if (B < T) { int32_t t = T; T = B; B = t; fy = !fy; }
  int x0 = (L + 4) >> 3, x1 = (R + 4) >> 3, y0 = (T + 4) >> 3, y1 = (B + 4) >> 3;   // pixels whose centres are inside
  if (x0 < s_cx0) x0 = s_cx0; if (x1 > s_cx1) x1 = s_cx1;
  if (y0 < s_cy0) y0 = s_cy0; if (y1 > s_cy1) y1 = s_cy1;
  if (x0 >= x1 || y0 >= y1) return;
  ArtSrc src;
  if (!art_open(&src, tex, tx, ty, tw, th)) return;
  // 16.16 steps through the source
  int32_t du = (int32_t)(((int64_t)tw << 19) / (R - L)), dv = (int32_t)(((int64_t)th << 19) / (B - T));
  int32_t u0 = (int32_t)(((int64_t)(x0 * 8 + 4 - L) * tw << 16) / (R - L));
  int32_t v = (int32_t)(((int64_t)(y0 * 8 + 4 - T) * th << 16) / (B - T));
  for (int y = y0; y < y1; y++, v += dv) {
    int sv = v >> 16; if (sv >= th) sv = th - 1;
    const uint8_t *row = art_row(&src, fy ? th - 1 - sv : sv);
    if (!row) continue;
    int xa = x0, xb = x1; ROWCLIP(y, xa, xb); if (xa >= xb) continue;
    uint8_t *d = ROWPTR(y) + xa;
    int32_t u = u0 + (xa - x0) * du;
    if (s_plain && du == 65536 && !fx) {
      // the common case: no scaling, no colour change
      const uint8_t *sp = row + (u >> 16);
      for (int x = xa; x < xb; x++, d++) { uint8_t c = *sp++; if ((c >> 6) == 3) *d = c; else if (c >> 6) put(d, c, x, y); }
      continue;
    }
    for (int x = xa; x < xb; x++, u += du, d++) {
      int su = u >> 16; if (su >= tw) su = tw - 1;
      put(d, row[fx ? tw - 1 - su : su], x, y);
    }
  }
}

// ---- a turned or scaled picture, the general way: map each covered pixel back into the picture ----
static int32_t s_sin, s_cos;   // x 4096
static int32_t isin(int32_t deg64) {
  // sine x4096 of an angle in 1/64 degrees, by a small table and interpolation
  static const int16_t T[] = {0, 357, 711, 1060, 1401, 1731, 2048, 2349, 2633, 2896, 3138, 3355, 3547, 3712, 3849, 3956, 4034, 4080, 4096};
  int32_t d = deg64 % (360 * 64); if (d < 0) d += 360 * 64;
  int sign = 1; if (d >= 180 * 64) { d -= 180 * 64; sign = -1; }
  if (d > 90 * 64) d = 180 * 64 - d;
  int i = d / 320, f = d % 320;
  int32_t v = T[i] + (i < 18 ? (T[i + 1] - T[i]) * f / 320 : 0);
  return sign * v;
}

typedef struct { int tex, tx, ty, tw, th; bool fx, fy; uint8_t solid; const uint8_t *nine; } Face;

static void turned(Obj *o, const Face *f, int32_t X, int32_t Y, int32_t sx, int32_t sy) {
  int32_t ox = o->ox, oy = o->oy, w = o->w, h = o->h;   // x8 units
  // the corners on screen, to find the box to fill
  int32_t minx = INT32_MAX, miny = INT32_MAX, maxx = INT32_MIN, maxy = INT32_MIN;
  for (int k = 0; k < 4; k++) {
    int32_t u = (k & 1) * w - ox, v = (k >> 1) * h - oy;
    int32_t dx = u * sx / 256, dy = v * sy / 256;
    int32_t cx = X + ox + (s_cos * dx - s_sin * dy) / 4096, cy = Y + oy + (s_sin * dx + s_cos * dy) / 4096;
    int32_t px = scx(cx), py = scy(cy);
    if (px < minx) minx = px; if (px > maxx) maxx = px; if (py < miny) miny = py; if (py > maxy) maxy = py;
  }
  int x0 = minx >> 3, x1 = (maxx + 7) >> 3, y0 = miny >> 3, y1 = (maxy + 7) >> 3;
  if (x0 < s_cx0) x0 = s_cx0; if (x1 > s_cx1) x1 = s_cx1;
  if (y0 < s_cy0) y0 = s_cy0; if (y1 > s_cy1) y1 = s_cy1;
  if (x0 >= x1 || y0 >= y1 || !sx || !sy) return;
  int k = f->solid ? 1 : art_shrink(f->tex);
  ArtSrc src;
  if (!f->solid && !art_open(&src, f->tex, f->tx / k, f->ty / k, (f->tw + k - 1) / k, (f->th + k - 1) / k)) return;
  int32_t z = s_cam->zoom;
  for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
    // screen px -> camera units x8 -> picture units x8
    int32_t cx = (int32_t)((int64_t)(px * 8 + 4 - s_cam->x) * 65536 / z / 256) + s_cam->sx / 256;
    int32_t cy = (int32_t)((int64_t)(py * 8 + 4 - s_cam->y) * 65536 / z / 256) + s_cam->sy / 256;
    int32_t dx = cx - X - ox, dy = cy - Y - oy;
    int32_t rx = (s_cos * dx + s_sin * dy) / 4096, ry = (-s_sin * dx + s_cos * dy) / 4096;
    int32_t u = rx * 256 / sx + ox, v = ry * 256 / sy + oy;
    if (u < 0 || v < 0 || u >= w || v >= h || !INROW(py, px)) continue;
    uint8_t *d = ROWPTR(py) + px;
    if (f->solid) { put(d, f->solid, px, py); continue; }
    int su = (int)((int64_t)u * f->tw / w), sv = (int)((int64_t)v * f->th / h);
    if (f->fx) su = f->tw - 1 - su;
    if (f->fy) sv = f->th - 1 - sv;
    su /= k; sv /= k;
    const uint8_t *row = art_row(&src, sv);
    if (row) put(d, row[su], px, py);
  }
}

// ---- solid rectangles ----
static void fill(int32_t L, int32_t T, int32_t R, int32_t B, uint8_t c) {
  if (R < L) { int32_t t = L; L = R; R = t; }
  if (B < T) { int32_t t = T; T = B; B = t; }
  int x0 = (L + 4) >> 3, x1 = (R + 4) >> 3, y0 = (T + 4) >> 3, y1 = (B + 4) >> 3;
  if (x0 < s_cx0) x0 = s_cx0; if (x1 > s_cx1) x1 = s_cx1;
  if (y0 < s_cy0) y0 = s_cy0; if (y1 > s_cy1) y1 = s_cy1;
  for (int y = y0; y < y1; y++) {
    int xa = x0, xb = x1; ROWCLIP(y, xa, xb);
    uint8_t *d = ROWPTR(y) + xa;
    for (int x = xa; x < xb; x++) put(d++, c, x, y);
  }
}

// ---- kinds ----

static void draw_tiles(Obj *o) {
  Tiles *t = o->ext; if (!t) return;
  int tw, th; if (!art_size(o->tex, &tw, &th) || !t->fw) return;
  int cols = tw / t->fw;
  int32_t X = obj_dyn(o, 0, s_now), Y = obj_dyn(o, 1, s_now);
  int32_t cw8 = t->cw * 8, ch8 = t->ch * 8;
  // only the cells the camera can see
  int32_t vx0 = (int32_t)((int64_t)(s_cx0 * 8 - s_cam->x) * 65536 / s_cam->zoom / 256) + s_cam->sx / 256;
  int32_t vy0 = (int32_t)((int64_t)(s_cy0 * 8 - s_cam->y) * 65536 / s_cam->zoom / 256) + s_cam->sy / 256;
  int32_t vx1 = (int32_t)((int64_t)(s_cx1 * 8 - s_cam->x) * 65536 / s_cam->zoom / 256) + s_cam->sx / 256;
  int32_t vy1 = (int32_t)((int64_t)(s_cy1 * 8 - s_cam->y) * 65536 / s_cam->zoom / 256) + s_cam->sy / 256;
  int c0 = (vx0 - X) / cw8 - 1, c1 = (vx1 - X) / cw8 + 1, r0 = (vy0 - Y) / ch8 - 1, r1 = (vy1 - Y) / ch8 + 1;
  if (c0 < 0) c0 = 0; if (r0 < 0) r0 = 0; if (c1 > t->mw) c1 = t->mw; if (r1 > t->mh) r1 = t->mh;
  for (int r = r0; r < r1; r++) for (int c = c0; c < c1; c++) {
    int i = r * t->mw + c;
    int v = t->wide ? ((uint16_t *)t->cells)[i] : t->cells[i];
    if (v == (t->wide ? 0xFFFF : 0xFF)) continue;
    int32_t x = X + c * cw8, y = Y + r * ch8;
    blit(o->tex, (v % cols) * t->fw, (v / cols) * t->fh, t->fw, t->fh, scx(x), scy(y), scx(x + cw8), scy(y + ch8), false, false);
  }
}

static void draw_skin(Obj *o) {
  // water: the texture repeats, shifted by an offset that drifts
  int tw, th; if (!art_size(o->tex, &tw, &th)) return;
  int32_t X = obj_dyn(o, 0, s_now), Y = obj_dyn(o, 1, s_now);
  int32_t ox = obj_dyn(o, 6, s_now), oy = obj_dyn(o, 7, s_now);
  int32_t L = scx(X), T = scy(Y), R = scx(X + o->w), B = scy(Y + o->h);
  int x0 = (L + 4) >> 3, x1 = (R + 4) >> 3, y0 = (T + 4) >> 3, y1 = (B + 4) >> 3;
  if (x0 < s_cx0) x0 = s_cx0; if (x1 > s_cx1) x1 = s_cx1;
  if (y0 < s_cy0) y0 = s_cy0; if (y1 > s_cy1) y1 = s_cy1;
  if (x0 >= x1 || y0 >= y1) return;
  ArtSrc src; if (!art_open(&src, o->tex, 0, 0, tw, th)) return;
  int32_t z = s_cam->zoom;
  // source x in 1/256 texel steps: one screen pixel is 256*256/zoom of them
  int32_t step = 65536 / z;                          // texels x256 per screen pixel
  for (int y = y0; y < y1; y++) {
    int32_t cy = (int32_t)((int64_t)(y * 8 + 4 - s_cam->y) * 65536 / z / 256) + s_cam->sy / 256;
    int sv = ((cy - Y + oy) / 8) % th; if (sv < 0) sv += th;
    const uint8_t *row = art_row(&src, sv); if (!row) continue;
    int xa = x0, xb = x1; ROWCLIP(y, xa, xb); if (xa >= xb) continue;
    uint8_t *d = ROWPTR(y) + xa;
    int32_t cx = (int32_t)((int64_t)(xa * 8 + 4 - s_cam->x) * 65536 / z / 256) + s_cam->sx / 256;
    int32_t u = (cx - X + ox) * 32;                    // texels x256
    int32_t span = tw * 256;
    u %= span; if (u < 0) u += span;
    for (int x = xa; x < xb; x++, d++) {
      uint8_t c = row[u >> 8];
      if (s_plain && (c >> 6) == 3) *d = c; else put(d, c, x, y);
      u += step; if (u >= span) u -= span;
    }
  }
}

static void draw_nine(Obj *o, int32_t X, int32_t Y, int32_t sx, int32_t sy) {
  const uint8_t *m = o->ext; if (!m) return;
  int32_t w = o->w, h = o->h;
  bool fx = o->flags & F_FLIPH, fy = o->flags & F_FLIPV;
  // the picture's edges in its own units x8, scaled about its origin
  int32_t ex[4] = {0, m[0] * 8, w - m[2] * 8, w}, ey[4] = {0, m[1] * 8, h - m[3] * 8, h};
  int ux[4] = {0, m[0], o->tw - m[2], o->tw}, uy[4] = {0, m[1], o->th - m[3], o->th};
  for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
    int si = fx ? 2 - i : i, sj = fy ? 2 - j : j;
    int32_t l = X + o->ox + (ex[i] - o->ox) * sx / 256, r = X + o->ox + (ex[i + 1] - o->ox) * sx / 256;
    int32_t t = Y + o->oy + (ey[j] - o->oy) * sy / 256, b = Y + o->oy + (ey[j + 1] - o->oy) * sy / 256;
    blit(o->tex, o->tx + ux[si], o->ty + uy[sj], ux[si + 1] - ux[si], uy[sj + 1] - uy[sj], scx(l), scy(t), scx(r), scy(b), fx, fy);
  }
}

static GContext *s_ctx;
static GBitmap *s_bmp;

static void fb_take(void) {
  s_bmp = graphics_capture_frame_buffer(s_ctx); s_fb = gbitmap_get_data(s_bmp); s_stride = gbitmap_get_bytes_per_row(s_bmp);
#ifdef PBL_ROUND
  if (s_bmp) for (int y = 0; y < s_H && y < PBL_DISPLAY_HEIGHT; y++) {
    GBitmapDataRowInfo r = gbitmap_get_data_row_info(s_bmp, y);
    s_row[y] = r.data; s_rmin[y] = r.min_x; s_rmax[y] = r.max_x;
  }
#endif
}
static void fb_give(void) { graphics_release_frame_buffer(s_ctx, s_bmp); s_bmp = NULL; }

// the phone picks the font (server WatchFonts) and lays the text out with its exact letter widths
static const char *FONTS[] = {FONT_KEY_GOTHIC_09, FONT_KEY_GOTHIC_14, FONT_KEY_GOTHIC_14_BOLD, FONT_KEY_GOTHIC_18, FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24, FONT_KEY_GOTHIC_24_BOLD};
static const int8_t CAP_TOP[] = {3, 5, 5, 7, 7, 10, 10};   // pixels above the capitals in each font

static void draw_text(Obj *o) {
  const char *s = o->ext; if (!s || !*s) return;
  int alpha = obj_dyn(o, 2, s_now);
  if (alpha < 64) return;
  int32_t x = scx(obj_dyn(o, 0, s_now)) >> 3, y = scy(obj_dyn(o, 1, s_now)) >> 3;
  // SPD marks highlighted words with _underscores_; draw the text without them
  char buf[64]; int n = 0;
  for (const char *c = s; *c && n < 63; c++) if (*c != '_') buf[n++] = *c;
  buf[n] = 0;
  int r = clamp255(o->col[0] + o->col[3]), g = clamp255(o->col[1] + o->col[4]), b = clamp255(o->col[2] + o->col[5]);
  fb_give();
  graphics_context_set_text_color(s_ctx, GColorFromRGB(r, g, b));
  int fi = (o->w & 0x7F) < 7 ? (o->w & 0x7F) : 1;
  GFont font = fonts_get_system_font(FONTS[fi]);
  if (o->w & 0x80) {
    // outlined (Pebble PD's HUD and log): black copies around it first
    graphics_context_set_text_color(s_ctx, GColorBlack);
    static const int8_t OX[4] = {-1, 1, 0, 0}, OY[4] = {0, 0, -1, 1};
    for (int k = 0; k < 4; k++)
      graphics_draw_text(s_ctx, buf, font, GRect(x + OX[k], y - CAP_TOP[fi] + OY[k], s_W, 30), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(s_ctx, GColorFromRGB(r, g, b));
  }
  graphics_draw_text(s_ctx, buf, font, GRect(x, y - CAP_TOP[fi], s_W, 30), GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  fb_take();
}

// ---- round-screen pieces (Pebble PD's disc and arc) ----
static int isq(int v) { int r = 0, b = 1 << 14; while (b > v) b >>= 2; while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return r; }
static void hline(int x0, int x1, int y, uint8_t c) {   // pixels x0..x1 of row y
  if (y < s_cy0 || y >= s_cy1) return;
  if (x0 < s_cx0) x0 = s_cx0; if (x1 >= s_cx1) x1 = s_cx1 - 1;
  int a = x0, b = x1 + 1; ROWCLIP(y, a, b);
  if (b > a) memset(ROWPTR(y) + a, c, b - a);
}
static int tg(int d10) { int32_t a = (int32_t)TRIG_MAX_ANGLE * d10 / 3600; int32_t c = cos_lookup(a); return c ? sin_lookup(a) * 256 / c : 0; }
static void draw_shape(Obj *o) {
  int cx = scx(obj_dyn(o, 0, s_now)) >> 3, cy = scy(obj_dyn(o, 1, s_now)) >> 3;
  uint8_t c = o->tex;
  if (o->flags == 1) {
    int r = o->tx;
    for (int dy = -r; dy <= r; dy++) { int w = isq(r * r - dy * dy); hline(cx - w, cx + w, cy + dy, c); }
    return;
  }
  // a band above the centre between radii r0..r1 and two bearings (the end one leaning out by slant per px)
  int r0 = o->tx, r1 = o->ty, b0 = o->tw, b1 = o->th, sl = (int8_t)o->frame;
  for (int rr = r0; rr < r1; rr++) {
    int t0 = tg(b0), t1 = tg(b1 + (rr - r0) * sl), rin = rr, rout = rr + 1;
    if (!sl) { rin = r0; rout = r1; rr = r1; }
    for (int y = 1; y <= rout; y++) {
      int xo = isq(rout * rout - y * y), xi = y < rin ? isq(rin * rin - y * y) : 0, a = y * t0 >> 8, b = y * t1 >> 8;
      for (int sd = 0; sd < 2; sd++) {
        int lo = sd ? xi : -xo, hi = sd ? xo : -xi;
        if (lo < a) lo = a; if (hi > b) hi = b;
        if (hi >= lo) hline(cx + lo, cx + hi, cy - y, c);
      }
    }
  }
}

static void draw_obj(Obj *o) {
  if (o->cam >= MAX_CAM || !g_cam[o->cam].used) return;
  s_cam = &g_cam[o->cam];
  static Cam s_panned;   // the map, moved by a finger ahead of the phone
  if (o->cam == g_map_cam && (g_pan_x || g_pan_y)) { s_panned = *s_cam; s_panned.sx += g_pan_x; s_panned.sy += g_pan_y; s_cam = &s_panned; }
  s_cx0 = s_cam->x >> 3; s_cy0 = s_cam->y >> 3; s_cx1 = (s_cam->x + s_cam->w) >> 3; s_cy1 = (s_cam->y + s_cam->h) >> 3;
  if (s_cx0 < 0) s_cx0 = 0; if (s_cy0 < 0) s_cy0 = 0; if (s_cx1 > s_W) s_cx1 = s_W; if (s_cy1 > s_H) s_cy1 = s_H;
  int alpha = obj_dyn(o, 2, s_now);
  if (o->kind == K_TEXT) { draw_text(o); return; }
  if (o->kind == K_SHAPE) { draw_shape(o); return; }
  if (alpha < 30 && !o->col[6]) return;
  colour_setup(o->col, alpha);
  if (o->kind == K_TILES) { draw_tiles(o); return; }
  if (o->kind == K_SKIN) { draw_skin(o); return; }
  int32_t X = obj_dyn(o, 0, s_now), Y = obj_dyn(o, 1, s_now), sx = obj_dyn(o, 3, s_now), sy = obj_dyn(o, 4, s_now), ang = obj_dyn(o, 5, s_now);
  if (o->kind == K_NINE) { draw_nine(o, X, Y, sx, sy); return; }
  if (o->kind == K_QUADS) {
    int16_t *q = o->ext; if (!q) return;
    for (int i = 0; i < q[0]; i++) {
      int16_t *e = q + 1 + i * 8;
      blit(o->tex, e[0], e[1], e[2], e[3], scx(e[4]), scy(e[5]), scx(e[4] + e[6]), scy(e[5] + e[7]), false, false);
    }
    return;
  }
  Face f = {0};
  if (o->kind == K_RECT) {
    int r = o->tx, g = o->ty, b = o->tw, a = o->th;   // base colour
    uint8_t c = (q4(a) << 6) | (q4(r) << 4) | (q4(g) << 2) | q4(b);
    if (!(c >> 6)) c |= 0xC0;   // colour blocks carry their alpha in the multiply; keep the pixel itself solid
    f.solid = c;
  } else {
    f.tex = o->tex; f.tx = o->tx; f.ty = o->ty; f.tw = o->tw; f.th = o->th;
    f.fx = o->flags & F_FLIPH; f.fy = o->flags & F_FLIPV;
    if (o->flags & F_ANIM) {
      Anim *a = scene_anim(o->tx); if (!a || !a->n) return;
      int fr = o->frame;
      if (a->delay_ms) {
        uint32_t t = (uint32_t)o->timer * a->delay_ms / 256 + (s_now - o->at);
        int steps = t / a->delay_ms;
        fr = a->looped ? (o->frame + steps) % a->n : (o->frame + steps < a->n - 1 ? o->frame + steps : a->n - 1);
      }
      f.tex = a->tex; f.tx = a->rects[fr * 4]; f.ty = a->rects[fr * 4 + 1]; f.tw = a->rects[fr * 4 + 2]; f.th = a->rects[fr * 4 + 3];
    }
  }
  if (ang % (360 * 64)) {
    s_sin = isin(ang); s_cos = isin(ang + 90 * 64);
    turned(o, &f, X, Y, sx, sy);
    return;
  }
  // upright: just the two corners, scaled about the origin
  int32_t l = X + o->ox - o->ox * sx / 256, t = Y + o->oy - o->oy * sy / 256;
  int32_t r = l + o->w * sx / 256, b = t + o->h * sy / 256;
  if (f.solid) { fill(scx(l), scy(t), scx(r), scy(b), f.solid); return; }
  blit(f.tex, f.tx, f.ty, f.tw, f.th, scx(l), scy(t), scx(r), scy(b), f.fx, f.fy);
}

#ifdef PROFILE
static uint32_t ms(void) { time_t s; uint16_t m; time_ms(&s, &m); return (uint32_t)s * 1000 + m; }
uint32_t g_prof[8];
#endif

void draw_scene(GContext *ctx, GRect bounds, uint32_t now) {
  s_ctx = ctx; s_now = now; s_W = bounds.size.w; s_H = bounds.size.h;
  fb_take();
  if (!s_fb) return;
  for (int y = 0; y < s_H; y++) { int a = 0, b = s_W; ROWCLIP(y, a, b); if (b > a) memset(ROWPTR(y) + a, GColorBlackARGB8, b - a); }
  for (int id = g_head; id != NONE; id = g_obj[id]->next) {
#ifdef PROFILE
    uint32_t t0 = ms(); draw_obj(g_obj[id]); g_prof[g_obj[id]->kind & 7] += ms() - t0;
#else
    draw_obj(g_obj[id]);
#endif
  }
  fb_give();
}

// ---- touch: which camera's picture is under a point (the same maths as drawing) ----
int scene_hit(int x, int y, uint32_t now) {
  int cam = -1;
  for (int id = g_head; id != NONE; id = g_obj[id]->next) {   // in drawing order: the last hit is on top
    Obj *o = g_obj[id];
    if (o->cam >= MAX_CAM || !g_cam[o->cam].used) continue;
    Cam *c = &g_cam[o->cam];
    if (o->cam == g_map_cam) {   // the map: anywhere in its view
      if (x >= c->x >> 3 && y >= c->y >> 3 && x < (c->x + c->w) >> 3 && y < (c->y + c->h) >> 3) cam = o->cam;
      continue;
    }
    if (o->kind == K_TILES || o->kind == K_TEXT || o->kind == K_QUADS || o->kind == K_SHAPE) continue;
    if (obj_dyn(o, 2, now) < 30) continue;
    s_cam = c;
    int32_t X = obj_dyn(o, 0, now), Y = obj_dyn(o, 1, now), sx = obj_dyn(o, 3, now), sy = obj_dyn(o, 4, now);
    int32_t l = X + o->ox - o->ox * sx / 256, t = Y + o->oy - o->oy * sy / 256;
    int32_t L = scx(l) >> 3, T = scy(t) >> 3, R = scx(l + o->w * sx / 256) >> 3, B = scy(t + o->h * sy / 256) >> 3;
    if (L > R) { int32_t k = L; L = R; R = k; }
    if (T > B) { int32_t k = T; T = B; B = k; }
    if (x >= L && x < R && y >= T && y < B) cam = o->cam;
  }
  return cam;
}
