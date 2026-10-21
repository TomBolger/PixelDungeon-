// Picture access: decompresses LZ4 strips from the ART resource and keeps recently used
// rectangles (tiles, sprite frames, window pieces) in a small cache.
#include "art.h"
#include "scene.h"

static ResHandle s_res;
static uint16_t s_count;
static uint32_t *s_off;

typedef struct { uint16_t w, h; uint8_t shrink, strip_h; uint16_t strips; } Head;
static Head *s_head;      // per picture, loaded on first use (w == 0: not loaded)

// ---- LZ4 block decoder ----
static int lz4(const uint8_t *src, int n, uint8_t *dst, int cap) {
  const uint8_t *s = src, *e = src + n;
  uint8_t *d = dst, *de = dst + cap;
  while (s < e) {
    int tok = *s++, lit = tok >> 4;
    if (lit == 15) { int b; do { b = s < e ? *s++ : 0; lit += b; } while (b == 255); }
    if (d + lit > de || s + lit > e) return -1;
    memcpy(d, s, lit); d += lit; s += lit;
    if (s >= e) break;
    int off = s[0] | (s[1] << 8); s += 2;
    int ml = (tok & 15) + 4;
    if ((tok & 15) == 15) { int b; do { b = s < e ? *s++ : 0; ml += b; } while (b == 255); }
    if (off == 0 || d - off < dst || d + ml > de) return -1;
    const uint8_t *m = d - off;
    while (ml--) *d++ = *m++;
  }
  return d - dst;
}

static void s_scratch_get(void);
static bool evict_one(void);
typedef struct CacheEnt CacheEnt;
static CacheEnt *s_cache;
void art_init(void) {
  s_res = resource_get_handle(RESOURCE_ID_ART);
  uint8_t b[2];
  resource_load_byte_range(s_res, 0, b, 2);
  s_count = b[0] | (b[1] << 8);
  s_off = malloc(4 * (s_count + 1));
  s_head = calloc(s_count, sizeof(Head));
  if (s_off) resource_load_byte_range(s_res, 2, (uint8_t *)s_off, 4 * (s_count + 1));
  s_scratch_get();
}

static Head *head(int tex) {
  if (tex < 1 || tex > s_count || !s_head || !s_off) return NULL;
  Head *h = &s_head[tex - 1];
  if (!h->w) {
    uint8_t b[8];
    resource_load_byte_range(s_res, s_off[tex - 1], b, 8);
    h->w = b[0] | (b[1] << 8); h->h = b[2] | (b[3] << 8); h->shrink = b[4]; h->strip_h = b[5]; h->strips = b[6] | (b[7] << 8);
  }
  return h;
}

bool art_size(int tex, int *w, int *h) {
  if (tex >= 200) { DynTex *d = scene_dyntex(tex); if (!d) return false; *w = d->w; *h = d->h; return true; }
  Head *hd = head(tex); if (!hd) return false;
  *w = hd->w * hd->shrink; *h = hd->h * hd->shrink; return true;
}

int art_shrink(int tex) { Head *h = tex < 200 ? head(tex) : NULL; return h ? h->shrink : 1; }

int art_cells(int tex, int fw, int fh) {
  int w, h; if (!art_size(tex, &w, &h) || !fw || !fh) return 0;
  return (w / fw) * (h / fh);
}

// ---- one decoded strip at a time ----
#define SCRATCH 8192
static uint8_t *s_scratch;
static int s_decodes;   // strips decoded (for PROFILE)
int art_decodes(void) { int d = s_decodes; s_decodes = 0; return d; }
static int s_stex = -1, s_strip = -1, s_sw;
// (taken at the start and kept: later, with the heap in pieces, 8 KB in one piece may not be there)
static void s_scratch_get(void) { if (!s_scratch) s_scratch = malloc(SCRATCH); }

static const uint8_t *strip(int tex, int k) {
  if (tex == s_stex && k == s_strip) return s_scratch;
  Head *h = head(tex); if (!h || k >= h->strips) return NULL;
  s_scratch_get();
  if (!s_scratch) return NULL;
  uint32_t o[2];
  resource_load_byte_range(s_res, s_off[tex - 1] + 8 + 4 * k, (uint8_t *)o, 8);
  int n = o[1] - o[0];
  uint8_t *z = malloc(n);
  while (!z && evict_one()) z = malloc(n);   // (short of memory: kept rectangles not on screen go first)
  if (!z) return NULL;
  resource_load_byte_range(s_res, s_off[tex - 1] + o[0], z, n);
  int rows = h->h - k * h->strip_h; if (rows > h->strip_h) rows = h->strip_h;
  int got = lz4(z, n, s_scratch, SCRATCH);
  s_decodes++;
  free(z);
  if (got != rows * h->w) { s_stex = -1; return NULL; }
  s_stex = tex; s_strip = k; s_sw = h->w;
  return s_scratch;
}

// ---- cache of rectangles ----
struct CacheEnt { struct CacheEnt *next, *hnext; uint16_t tex, x, y, w, h; uint32_t used; uint8_t px[]; };
#define HASH 128
static CacheEnt *s_hash[HASH];
static inline int hkey(int tex, int x, int y) { return (tex * 31 + x * 7 + y * 13) & (HASH - 1); }
static void hash_remove(CacheEnt *v) {
  for (CacheEnt **e = &s_hash[hkey(v->tex, v->x, v->y)]; *e; e = &(*e)->hnext) if (*e == v) { *e = v->hnext; return; }
}
static int s_cache_bytes;
static uint32_t s_tick;
#define CACHE_MAX (26 * 1024)
#define CACHE_ONE 4096

static uint32_t s_frame_tick;   // s_tick as this frame's drawing began: rects used since are on screen now
static int s_fails, s_skips;    // rects that couldn't be decoded (memory), rects drawn without the cache (no room)

void art_frame(void) { s_frame_tick = s_tick + 1; }

// the least recently used rect not drawn in this frame (false: every one is on screen now)
static bool evict_one(void) {
  CacheEnt **best = NULL;
  for (CacheEnt **e = &s_cache; *e; e = &(*e)->next) if ((*e)->used < s_frame_tick && (!best || (*e)->used < (*best)->used)) best = e;
  if (!best) return false;
  CacheEnt *v = *best; *best = v->next;
  hash_remove(v);
  s_cache_bytes -= v->w * v->h; free(v);
  return true;
}

int art_cache_bytes(void) { return s_cache_bytes; }

// give memory back until `need` bytes are free (a message arriving when memory is short): the oldest rects first,
// then the strip buffer
void art_release(int need) {
  while ((int)heap_bytes_free() < need && s_cache) {
    CacheEnt **best = &s_cache;
    for (CacheEnt **e = &s_cache; *e; e = &(*e)->next) if ((*e)->used < (*best)->used) best = e;
    CacheEnt *v = *best; *best = v->next; hash_remove(v); s_cache_bytes -= v->w * v->h; free(v);
  }
  if ((int)heap_bytes_free() < need && s_scratch) { free(s_scratch); s_scratch = NULL; s_stex = -1; }
}
// n bytes in one piece for the screen's copy or a message, the cache giving way rect by rect (oldest first) until a
// malloc succeeds, then the strip buffer. (Freeing until enough was free in all, as art_release does, wasn't
// enough: with the heap in pieces among the cache's rects, a 2-8 KB tile map or message still found no room.)
void *art_alloc(int n) {
  void *m = malloc(n);
  while (!m && s_cache) {
    CacheEnt **best = &s_cache;
    for (CacheEnt **e = &s_cache; *e; e = &(*e)->next) if ((*e)->used < (*best)->used) best = e;
    CacheEnt *v = *best; *best = v->next; hash_remove(v); s_cache_bytes -= v->w * v->h; free(v);
    m = malloc(n);
  }
  if (!m && s_scratch) { free(s_scratch); s_scratch = NULL; s_stex = -1; m = malloc(n); }
  return m;
}
void art_stats(int *fails, int *skips) { *fails = s_fails; *skips = s_skips; }

void art_forget(int tex) {
  for (CacheEnt **e = &s_cache; *e;) {
    if ((*e)->tex == tex) { CacheEnt *v = *e; *e = v->next; hash_remove(v); s_cache_bytes -= v->w * v->h; free(v); } else e = &(*e)->next;
  }
  if (s_stex == tex) s_stex = -1;
}

void art_forget_dynamic(void) { for (int t = 200; t < 255; t++) art_forget(t); }

static uint8_t *s_rowbuf;


bool art_open(ArtSrc *s, int tex, int x, int y, int w, int h) {
  s->tex = tex; s->x = x; s->y = y; s->w = w; s->h = h; s->px = NULL; s->stride = w;
  if (w <= 0 || h <= 0) return false;
  DynTex *d = NULL;
  Head *hd = NULL;
  if (tex >= 200) {
    d = scene_dyntex(tex); if (!d) return false;
    if (x < 0 || y < 0 || x + w > d->w || y + h > d->h) return false;
    if (d->bits == 8) { s->px = d->px + y * d->w + x; s->stride = d->w; return true; }
  } else {
    hd = head(tex); if (!hd) return false;
    if (x < 0 || y < 0 || x + w > hd->w || y + h > hd->h) return false;
  }
  s_tick++;
  for (CacheEnt *e = s_hash[hkey(tex, x, y)]; e; e = e->hnext)
    if (e->tex == tex && e->x == x && e->y == y && e->w == w && e->h == h) { e->used = s_tick; s->px = e->px; return true; }
  if (w * h > CACHE_ONE) return true;   // too big to keep: rows are decoded as they are drawn
  // keep the cache within its budget, and give memory back when the screen needs it (big floors, long windows),
  // but only from rects this frame isn't drawing: when memory was short (a menu open over a big floor), giving up
  // the very rects on screen to decode them again, every frame, made each frame many times slower
  #define ROOM() (s_cache_bytes + w * h <= CACHE_MAX && (int)heap_bytes_free() >= 12000 + w * h)
  while (!ROOM() && evict_one()) {}
  if (!ROOM()) { s_skips++; return true; }   // no room: drawn from the picture row by row, uncached
  #undef ROOM
  CacheEnt *e = malloc(sizeof(CacheEnt) + w * h);
  if (!e) { s_skips++; return true; }
  for (int r = 0; r < h; r++) {
    int yy = y + r;
    if (d) { for (int c = 0; c < w; c++) e->px[r * w + c] = dyn_px(d, yy * d->w + x + c); continue; }
    const uint8_t *st = strip(tex, yy / hd->strip_h);
    // couldn't decode (memory short): not kept, the next frame tries again. (A blank rect was kept here, and an
    // icon on screen, drawn every frame, stayed see-through for good: the menu's gear tab.)
    if (!st) { free(e); s_fails++; return true; }
    memcpy(e->px + r * w, st + (yy % hd->strip_h) * s_sw + x, w);
  }
  e->tex = tex; e->x = x; e->y = y; e->w = w; e->h = h; e->used = s_tick;
  e->next = s_cache; s_cache = e; s_cache_bytes += w * h;
  int hk = hkey(tex, x, y); e->hnext = s_hash[hk]; s_hash[hk] = e;
  s->px = e->px;
  return true;
}

const uint8_t *art_row(ArtSrc *s, int row) {
  if (row < 0 || row >= s->h) return NULL;
  if (s->px) return s->px + row * s->stride;
  if (s->tex >= 200) {
    // a packed run-time picture (fog): unpack one row
    DynTex *d = scene_dyntex(s->tex); if (!d || s->w > 512) return NULL;
    if (!s_rowbuf) s_rowbuf = malloc(512);
    if (!s_rowbuf) return NULL;
    for (int c = 0; c < s->w; c++) s_rowbuf[c] = dyn_px(d, (s->y + row) * d->w + s->x + c);
    return s_rowbuf;
  }
  Head *hd = head(s->tex); if (!hd) return NULL;
  int yy = s->y + row;
  const uint8_t *st = strip(s->tex, yy / hd->strip_h);
  return st ? st + (yy % hd->strip_h) * s_sw + s->x : NULL;
}
