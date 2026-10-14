/*
 * Pixel Dungeon for Pebble - drawing straight into the frame buffer.
 * Pebble's bitmap drawing can't scale, so zoom (1/2x, 1x, 2x) and PD's stretchable
 * frames (NinePatch) are done here: each source row is decoded once into a small
 * line buffer, then copied nearest-neighbour. One art set serves every zoom.
 */
#include "pd.h"

static GBitmap *s_fb;
static int s_fb_h;

#ifdef PBL_ROUND
// Row pointers are looked up once per frame: asking the system for every row of every sprite was the
// single biggest cost of a frame.
static GBitmapDataRowInfo *s_rows;
static int s_rows_n;
void blit_init(int h) { if (!s_rows) { s_rows = malloc(h * sizeof *s_rows); s_rows_n = s_rows ? h : 0; } }   // 0.14: at start-up, with the other long-lived blocks (a row table made at the first frame split the heap in two)
void blit_begin(GContext *ctx) {
  s_fb = graphics_capture_frame_buffer(ctx);
  s_fb_h = s_fb ? gbitmap_get_bounds(s_fb).size.h : 0;
  if (!s_fb) return;
  if (s_rows_n < s_fb_h) { free(s_rows); s_rows = malloc(s_fb_h * sizeof *s_rows); s_rows_n = s_rows ? s_fb_h : 0; }
  if (!s_rows) { graphics_release_frame_buffer(ctx, s_fb); s_fb = NULL; s_fb_h = 0; return; }
  for (int y = 0; y < s_fb_h; y++) s_rows[y] = gbitmap_get_data_row_info(s_fb, y);
}
#define ROW(y) (s_rows[(y)])
#else
// Emery's frame buffer is a plain rectangle: a row is the start plus y strides (0.20: no 912 B table)
static uint8_t *s_fb_data; static int s_fb_stride, s_fb_w;
void blit_init(int h) {}
void blit_begin(GContext *ctx) {
  s_fb = graphics_capture_frame_buffer(ctx);
  if (!s_fb) { s_fb_h = 0; return; }
  GRect b = gbitmap_get_bounds(s_fb);
  s_fb_h = b.size.h; s_fb_w = b.size.w;
  s_fb_data = gbitmap_get_data(s_fb); s_fb_stride = gbitmap_get_bytes_per_row(s_fb);
}
static inline GBitmapDataRowInfo row_of(int y) { return (GBitmapDataRowInfo){ .data = s_fb_data + y * s_fb_stride, .min_x = 0, .max_x = s_fb_w - 1 }; }
#define ROW(y) row_of(y)
#endif
#define XMAP_MAX PBL_DISPLAY_WIDTH
void blit_end(GContext *ctx) {
  if (s_fb) graphics_release_frame_buffer(ctx, s_fb);
  s_fb = NULL;
}

typedef struct { const uint8_t *data; int stride, bpp; uint8_t pal[16]; } Src;

// 0.35: what a bitmap looks like is asked once, not on every blit (four system calls each; ~60 a frame)
static GBitmap *s_sc_bmp[2]; static Src s_sc[2]; static uint8_t s_sc_next;
void blit_forget(void) { s_sc_bmp[0] = s_sc_bmp[1] = NULL; }
static bool src_of_raw(const Sheet *s, Src *o);
static const Src *src_of(const Sheet *s) {
  if (!s || !s->bmp) return NULL;
  for (int i = 0; i < 2; i++) if (s_sc_bmp[i] == s->bmp) return &s_sc[i];
  int i = s_sc_next; s_sc_next ^= 1;
  if (!src_of_raw(s, &s_sc[i])) return NULL;
  s_sc_bmp[i] = s->bmp; return &s_sc[i];
}
static bool src_of_raw(const Sheet *s, Src *o) {
  o->data = gbitmap_get_data(s->bmp);
  o->stride = gbitmap_get_bytes_per_row(s->bmp);
  switch (gbitmap_get_format(s->bmp)) {
    case GBitmapFormat1BitPalette: o->bpp = 1; break;
    case GBitmapFormat2BitPalette: o->bpp = 2; break;
    case GBitmapFormat4BitPalette: o->bpp = 4; break;
    case GBitmapFormat8Bit: o->bpp = 8; return true;
    default: return false;
  }
  const GColor *pal = gbitmap_get_palette(s->bmp);
  for (int i = 0; i < (1 << o->bpp); i++) o->pal[i] = pal[i].argb;
  return true;
}

// Decode w pixels of one source row into GColor8 bytes (top 2 bits = alpha)
static void decode_row(const Src *s, int sx, int sy, int w, uint8_t *out) {
  const uint8_t *row = s->data + sy * s->stride;
  switch (s->bpp) {
    case 8: memcpy(out, row + sx, w); return;
    case 4: for (int i = 0; i < w; i++) { int x = sx + i; out[i] = s->pal[(row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15]; } return;
    case 2: for (int i = 0; i < w; i++) { int x = sx + i; out[i] = s->pal[(row[x >> 2] >> (6 - 2 * (x & 3))) & 3]; } return;
    case 1: for (int i = 0; i < w; i++) { int x = sx + i; out[i] = s->pal[(row[x >> 3] >> (7 - (x & 7))) & 1]; } return;
  }
}

bool g_blit_ghost;
bool g_blit_flip;                                  // 0.20: mirrored (left-facing characters: their sheets hold one facing)
uint8_t g_blit_glow;
bool g_blit_opaque;                                // the caller knows the sprite has no transparent pixels: plain copies                               // ItemSprite.Glowing: a dither of this colour over the sprite
int g_blit_clip_y = -10000;                        // blit_nine4 skips screen rows above this
#define LINE_MAX 160
static uint8_t s_line[LINE_MAX];

static void blit_core(const Src *srcp, int sx, int sy, int w, int h, int dx, int dy, int zoom);
void blit(const Sheet *s, int sx, int sy, int w, int h, int dx, int dy, int zoom) {
  const Src *src;
  if (!s_fb || w > LINE_MAX || !(src = src_of(s))) return;
  blit_core(src, sx, sy, w, h, dx, dy, zoom);
}
void blit_spr4(const uint8_t *spr, int dx, int dy, int zoom) {   // a 16x16 sprite: 16 palette bytes, then 4-bit pixels
  if (!s_fb) return;
  if (g_blit_glow || g_blit_ghost || g_blit_flip) {
    Src src; src.data = spr + 16; src.stride = 8; src.bpp = 4;
    for (int i = 0; i < 16; i++) src.pal[i] = spr[i];
    blit_core(&src, 0, 0, 16, 16, dx, dy, zoom);
    return;
  }
  // 0.35: map tiles and items straight from their nibbles to the screen, one pass (no row buffer, no unused pixels)
  int n = 8 << zoom;                                   // 8, 16 or 32 px; source pixel of screen pixel x: (x << 1) >> zoom
  for (int y = 0; y < n; y++) {
    int ty = dy + y;
    if (ty < 0) continue;
    if (ty >= s_fb_h) break;
    GBitmapDataRowInfo ri = ROW(ty);
    int x0 = dx < ri.min_x ? ri.min_x - dx : 0, x1 = dx + n - 1 > ri.max_x ? ri.max_x - dx : n - 1;
    const uint8_t *row = spr + 16 + (((y << 1) >> zoom) << 3);
    uint8_t *d = ri.data + dx;
    if (g_blit_opaque) for (int x = x0; x <= x1; x++) { int k = (x << 1) >> zoom; d[x] = spr[(row[k >> 1] >> (k & 1 ? 0 : 4)) & 15]; }
    else for (int x = x0; x <= x1; x++) {
      int k = (x << 1) >> zoom; uint8_t c = spr[(row[k >> 1] >> (k & 1 ? 0 : 4)) & 15];
      if (c & 0xC0) d[x] = c | 0xC0;
    }
  }
}
static void blit_core(const Src *src, int sx, int sy, int w, int h, int dx, int dy, int zoom) {
  int dw = zoom == 2 ? w * 2 : zoom == 0 ? w / 2 : w;
  int dh = zoom == 2 ? h * 2 : zoom == 0 ? h / 2 : h;
  int last_sy = -1;
  for (int y = 0; y < dh; y++) {
    int ty = dy + y;
    if (ty < 0) continue;
    if (ty >= s_fb_h) break;
    int syy = sy + (zoom == 2 ? y >> 1 : zoom == 0 ? y << 1 : y);
    if (syy != last_sy) {
      decode_row(src, sx, syy, w, s_line); last_sy = syy;
      if (g_blit_flip) for (int a = 0, b = w - 1; a < b; a++, b--) { uint8_t t = s_line[a]; s_line[a] = s_line[b]; s_line[b] = t; }
    }
    GBitmapDataRowInfo ri = ROW(ty);
    int x0 = dx < ri.min_x ? ri.min_x - dx : 0, x1 = dx + dw - 1 > ri.max_x ? ri.max_x - dx : dw - 1;
    uint8_t *d = ri.data + dx;
    if (g_blit_opaque && zoom == 1) { if (x1 >= x0) memcpy(d + x0, s_line + x0, x1 - x0 + 1); }
    else if (g_blit_opaque && zoom == 2) { for (int x = x0; x <= x1; x++) d[x] = s_line[x >> 1]; }
    else if (g_blit_glow) {                               // glowing items: about a third of their pixels in the glow colour
      static const uint8_t BY[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
      for (int x = x0; x <= x1; x++) {
        uint8_t c = s_line[zoom == 2 ? x >> 1 : zoom == 0 ? x << 1 : x];
        if (c & 0xC0) d[x] = BY[ty & 3][(dx + x) & 3] < 6 ? g_blit_glow : (c | 0xC0);
      }
    } else if (g_blit_ghost) {                              // Invisibility: every other pixel, like PD's 40% alpha
      for (int x = x0 + ((x0 + dx + ty) & 1); x <= x1; x += 2) {
        uint8_t c = s_line[zoom == 2 ? x >> 1 : zoom == 0 ? x << 1 : x];
        if (c & 0xC0) d[x] = c | 0xC0;
      }
    } else if (zoom == 1) { for (int x = x0; x <= x1; x++) { uint8_t c = s_line[x]; if (c & 0xC0) d[x] = c | 0xC0; } }
    else if (zoom == 2) { for (int x = x0; x <= x1; x++) { uint8_t c = s_line[x >> 1]; if (c & 0xC0) d[x] = c | 0xC0; } }
    else { for (int x = x0; x <= x1; x++) { uint8_t c = s_line[x << 1]; if (c & 0xC0) d[x] = c | 0xC0; } }
  }
}

// PD NinePatch: corners fixed, edges and centre stretched (nearest); margins left, top, right, bottom
void blit_nine4(const Sheet *s, int sx, int sy, int sw, int sh, int ml, int mt, int mr, int mb, int dx, int dy, int dw, int dh) {
  const Src *src;
  if (!s_fb || sw > LINE_MAX || !(src = src_of(s))) return;
  int last_sy = -1, cw = sw - ml - mr, ch = sh - mt - mb, dcw = dw - ml - mr, dch = dh - mt - mb;
  // 0.35: the source column of each screen column, worked out once (a division per pixel made the status pane slow)
  int x0 = dx < 0 ? 0 : dx, x1 = dx + dw > XMAP_MAX ? XMAP_MAX : dx + dw;   // screen columns
  uint8_t xm[XMAP_MAX];
  for (int t = x0; t < x1; t++) { int x = t - dx; xm[t] = x < ml ? x : x >= dw - mr ? sw - (dw - x) : ml + (dcw > 0 ? (x - ml) * cw / dcw : 0); }
  for (int y = 0; y < dh; y++) {
    int ty = dy + y;
    if (ty < 0 || ty < g_blit_clip_y) continue;
    if (ty >= s_fb_h) break;
    int yy = y < mt ? y : y >= dh - mb ? sh - (dh - y) : mt + (dch > 0 ? (y - mt) * ch / dch : 0);
    if (sy + yy != last_sy) { decode_row(src, sx, sy + yy, sw, s_line); last_sy = sy + yy; }
    GBitmapDataRowInfo ri = ROW(ty);
    int a = ri.min_x > x0 ? ri.min_x : x0, b = ri.max_x + 1 < x1 ? ri.max_x + 1 : x1;
    for (int t = a; t < b; t++) { uint8_t c = s_line[xm[t]]; if (c & 0xC0) ri.data[t] = c | 0xC0; }
  }
}
void blit_nine(const Sheet *s, int sx, int sy, int sw, int sh, int b, int dx, int dy, int dw, int dh) {
  blit_nine4(s, sx, sy, sw, sh, b, b, b, b, dx, dy, dw, dh);
}

// remembered-but-unseen cells: black checker dither
void blit_fog(int dx, int dy, int size) {
  if (!s_fb) return;
  for (int y = 0; y < size; y++) {
    int ty = dy + y;
    if (ty < 0) continue;
    if (ty >= s_fb_h) break;
    GBitmapDataRowInfo ri = ROW(ty);
    for (int x = (ty + dx) & 1; x < size; x += 2) {
      int tx = dx + x;
      if (tx >= ri.min_x && tx <= ri.max_x) ri.data[tx] = GColorBlackARGB8;
    }
  }
}

void blit_fill(int dx, int dy, int w, int h, uint8_t argb) {
  if (!s_fb) return;
  for (int y = dy; y < dy + h; y++) {
    if (y < 0) continue;
    if (y >= s_fb_h) break;
    GBitmapDataRowInfo ri = ROW(y);
    int x0 = dx < ri.min_x ? ri.min_x : dx, x1 = dx + w - 1 > ri.max_x ? ri.max_x : dx + w - 1;
    if (x1 >= x0) memset(ri.data + x0, argb, x1 - x0 + 1);
  }
}

__attribute__((noinline)) void blit_shrink(GRect r, int dx, int dy) {   // 0.42: part of the screen at half size (on screen, no overlap)
  if (!s_fb) return;
  for (int y = 0; y < r.size.h / 2; y++) {
    uint8_t *s = ROW(r.origin.y + 2 * y).data + r.origin.x, *t = ROW(dy + y).data + dx;
    for (int x = 0; x < r.size.w / 2; x++) t[x] = s[2 * x];
  }
}
void blit_box(int x, int y, int w, int h, int t, uint8_t argb) {      // outline, t px thick
  blit_fill(x, y, w, t, argb); blit_fill(x, y + h - t, w, t, argb);
  blit_fill(x, y, t, h, argb); blit_fill(x + w - t, y, t, h, argb);
}

// checkerboard of two colours (tinted item slots)
void blit_fill2(int dx, int dy, int w, int h, uint8_t a, uint8_t b) {
  if (!s_fb) return;
  for (int y = dy; y < dy + h; y++) {
    if (y < 0) continue;
    if (y >= s_fb_h) break;
    GBitmapDataRowInfo ri = ROW(y);
    for (int x = dx; x < dx + w; x++) if (x >= ri.min_x && x <= ri.max_x) ri.data[x] = ((x + y) & 1) ? a : b;
  }
}
// gas and flame: scattered pixels in screen space so neighbouring cells blend; phase animates the drift.
// density: 1 = one pixel in 8, 2 = one in 4, 3 = one in 2
// PD's gas specks: a soft round puff, see-through toward its edge. Real blending: each pixel moves toward
// the gas colour channel by channel (2 bits each), with ordered-dither rounding, so clouds are smooth and
// overlapping puffs build up. level: 0..16, the opacity at the centre; shift: this puff's dither phase.
void blit_puff(int cx, int cy, int r, uint8_t argb, int level, int shift) {
  static const uint8_t B[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
  if (!s_fb || r <= 0 || level <= 0) return;
  int r2 = r * r, gr = (argb >> 4) & 3, gg = (argb >> 2) & 3, gb = argb & 3;
  for (int y = cy - r; y <= cy + r; y++) {
    if (y < 0) continue;
    if (y >= s_fb_h) break;
    GBitmapDataRowInfo ri = ROW(y);
    int dy2 = (y - cy) * (y - cy);
    for (int x = cx - r; x <= cx + r; x++) {
      if (x < ri.min_x || x > ri.max_x) continue;
      int d2 = (x - cx) * (x - cx) + dy2;
      if (d2 >= r2) continue;
      int a = level * (r2 - d2) / r2, t = B[(y + shift) & 3][(x + (shift >> 2)) & 3];
      uint8_t o = ri.data[x];
      int orr = (o >> 4) & 3, og = (o >> 2) & 3, ob = o & 3;
      orr += ((gr - orr) * a + t + 64) / 16 - 4; og += ((gg - og) * a + t + 64) / 16 - 4; ob += ((gb - ob) * a + t + 64) / 16 - 4;
      ri.data[x] = 0xC0 | orr << 4 | og << 2 | ob;
    }
  }
}

// screen fade: ordered (Bayer 4x4) black dither, level 0 (none) .. 16 (black)
void blit_fade(int level) {
  static const uint8_t B[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
  if (!s_fb || level <= 0) return;
  for (int y = 0; y < s_fb_h; y++) {
    GBitmapDataRowInfo ri = ROW(y);
    for (int x = ri.min_x; x <= ri.max_x; x++) if (B[y & 3][x & 3] < level) ri.data[x] = GColorBlackARGB8;
  }
}

#ifndef PBL_ROUND
// 0.35: outlined text in one text draw (main.c outline_text): the text is drawn in a marker colour that the area did not
// hold, then every marked pixel takes the text colour and its unmarked 4-neighbours turn black, exactly what the four
// black copies one pixel off and the copy on top used to draw
// mark_scan(r, mark, -1): does the area hold the marker? mark_scan(r, mark, colour): outline the marked pixels, colour them
int mark_scan(GRect r, uint8_t mark, int color) {
  int x0 = r.origin.x < 0 ? 0 : r.origin.x, y0 = r.origin.y < 0 ? 0 : r.origin.y;
  int x1 = r.origin.x + r.size.w > s_fb_w ? s_fb_w : r.origin.x + r.size.w, y1 = r.origin.y + r.size.h > s_fb_h ? s_fb_h : r.origin.y + r.size.h;
  if (!s_fb || x1 <= x0 || y1 <= y0) return 1;
  for (int pass = color < 0 ? 0 : 1; pass < 3; pass++)
    for (int y = y0; y < y1; y++) {
      uint8_t *d = s_fb_data + y * s_fb_stride;
      for (int x = x0; x < x1; x++) {
        if (pass == 0) { if (d[x] == mark) return 1; continue; }
        if ((d[x] == mark) != (pass == 2)) continue;
        if (pass == 2) d[x] = color;
        else if ((x > 0 && d[x - 1] == mark) || (x + 1 < s_fb_w && d[x + 1] == mark) ||
                 (y > 0 && d[x - s_fb_stride] == mark) || (y + 1 < s_fb_h && d[x + s_fb_stride] == mark)) d[x] = GColorBlackARGB8;
      }
      if (pass == 0 && y == y1 - 1) return 0;
    }
  return 0;
}
#endif
