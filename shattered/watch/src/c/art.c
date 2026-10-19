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

void art_init(void) {
  s_res = resource_get_handle(RESOURCE_ID_ART);
  uint8_t b[2];
  resource_load_byte_range(s_res, 0, b, 2);
  s_count = b[0] | (b[1] << 8);
  s_off = malloc(4 * (s_count + 1));
  s_head = calloc(s_count, sizeof(Head));
  if (s_off) resource_load_byte_range(s_res, 2, (uint8_t *)s_off, 4 * (s_count + 1));
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
static int s_stex = -1, s_strip = -1, s_sw;

static const uint8_t *strip(int tex, int k) {
  if (tex == s_stex && k == s_strip) return s_scratch;
  Head *h = head(tex); if (!h || k >= h->strips) return NULL;
  if (!s_scratch) s_scratch = malloc(SCRATCH);
  if (!s_scratch) return NULL;
  uint32_t o[2];
  resource_load_byte_range(s_res, s_off[tex - 1] + 8 + 4 * k, (uint8_t *)o, 8);
  int n = o[1] - o[0];
  uint8_t *z = malloc(n);
  if (!z) return NULL;
  resource_load_byte_range(s_res, s_off[tex - 1] + o[0], z, n);
  int rows = h->h - k * h->strip_h; if (rows > h->strip_h) rows = h->strip_h;
  int got = lz4(z, n, s_scratch, SCRATCH);
  free(z);
  if (got != rows * h->w) { s_stex = -1; return NULL; }
  s_stex = tex; s_strip = k; s_sw = h->w;
  return s_scratch;
}

// ---- cache of rectangles ----
typedef struct CacheEnt { struct CacheEnt *next, *hnext; uint16_t tex, x, y, w, h; uint32_t used; uint8_t px[]; } CacheEnt;
static CacheEnt *s_cache;
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

static void evict_one(void) {
  CacheEnt **best = NULL;
  for (CacheEnt **e = &s_cache; *e; e = &(*e)->next) if (!best || (*e)->used < (*best)->used) best = e;
  if (!best) return;
  CacheEnt *v = *best; *best = v->next;
  hash_remove(v);
  s_cache_bytes -= v->w * v->h; free(v);
}

int art_cache_bytes(void) { return s_cache_bytes; }

void art_forget(int tex) {
  for (CacheEnt **e = &s_cache; *e;) {
    if ((*e)->tex == tex) { CacheEnt *v = *e; *e = v->next; hash_remove(v); s_cache_bytes -= v->w * v->h; free(v); } else e = &(*e)->next;
  }
  if (s_stex == tex) s_stex = -1;
}

void art_forget_dynamic(void) { for (int t = 200; t < 255; t++) art_forget(t); }

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
  // keep the cache within its budget, and give memory back when the screen needs it (big floors, long windows)
  while (s_cache && (s_cache_bytes + w * h > CACHE_MAX || (int)heap_bytes_free() < 12000 + w * h)) evict_one();
  CacheEnt *e = malloc(sizeof(CacheEnt) + w * h);
  while (!e && s_cache) { evict_one(); e = malloc(sizeof(CacheEnt) + w * h); }
  if (!e) return true;
  for (int r = 0; r < h; r++) {
    int yy = y + r;
    if (d) { for (int c = 0; c < w; c++) e->px[r * w + c] = dyn_px(d, yy * d->w + x + c); continue; }
    const uint8_t *st = strip(tex, yy / hd->strip_h);
    if (st) memcpy(e->px + r * w, st + (yy % hd->strip_h) * s_sw + x, w);
    else memset(e->px + r * w, 0, w);
  }
  e->tex = tex; e->x = x; e->y = y; e->w = w; e->h = h; e->used = s_tick;
  e->next = s_cache; s_cache = e; s_cache_bytes += w * h;
  int hk = hkey(tex, x, y); e->hnext = s_hash[hk]; s_hash[hk] = e;
  s->px = e->px;
  return true;
}

static uint8_t *s_rowbuf;

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
