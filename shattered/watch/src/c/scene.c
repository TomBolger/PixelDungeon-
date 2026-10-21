// Applies the phone's commands to the watch's copy of the screen (format: server/src/pebxel/Link.java).
#include "scene.h"
#include "snd.h"
#include "predict.h"
#include "art.h"

Obj *g_obj[MAX_ID];
Cam g_cam[MAX_CAM];
uint16_t g_head = NONE;
bool g_dirty;
int g_oom;                       // allocations that failed (reported in main.c's log)
bool g_partial;                  // part of a frame is applied but not yet its end (C_COMMIT): don't show it

#define MAX_ANIM 512
static Anim *s_anim[MAX_ANIM];
#define MAX_DYN 55
static DynTex *s_dyn[MAX_DYN];   // ids 200..254

// Memory for the screen's copy: when it runs short, the picture cache goes first (art_alloc); if there is
// still not enough, the copy is given up and the phone asked for the whole screen again (main.c, g_lost)
// rather than going on with pieces missing.
bool g_lost;
static void *salloc(size_t n) {
  void *m = art_alloc(n);
  if (!m) { if (!g_oom++ || !(g_oom & 15)) APP_LOG(APP_LOG_LEVEL_WARNING, "no room for %d bytes (free %d)", (int)n, (int)heap_bytes_free()); g_lost = true; }
  return m;
}
static void *scalloc(size_t n) { void *m = salloc(n); if (m) memset(m, 0, n); return m; }

Anim *scene_anim(int id) { return id >= 0 && id < MAX_ANIM ? s_anim[id] : NULL; }
DynTex *scene_dyntex(int id) { return id >= 200 && id < 200 + MAX_DYN ? s_dyn[id - 200] : NULL; }

// ---- reading ----

static const uint8_t *p, *end;
static int u8(void) { return p < end ? *p++ : 0; }
static int u16(void) { int v = u8(); return v | (u8() << 8); }
static int s16(void) { return (int16_t)u16(); }

// ---- objects ----

static void free_obj(Obj *o) {
  if (!o) return;
  free(o->ext);
  free(o);
}

int8_t g_map_cam = -1;
int32_t g_pan_x, g_pan_y;

void scene_reset(void) {
  g_map_cam = -1; g_lost = false;
  for (int i = 0; i < MAX_ID; i++) { free_obj(g_obj[i]); g_obj[i] = NULL; }
  for (int i = 0; i < MAX_ANIM; i++) { free(s_anim[i]); s_anim[i] = NULL; }
  for (int i = 0; i < MAX_DYN; i++) { free(s_dyn[i]); s_dyn[i] = NULL; }
  memset(g_cam, 0, sizeof(g_cam));
  g_head = NONE;
  g_partial = false;
  predict_reset();
  art_forget_dynamic();
}

static void link_after(int id, int after) {
  Obj *o = g_obj[id];
  // after a picture this watch never got (memory ran out): first, until the phone sends everything again (g_lost)
  if (after != NONE && (after >= MAX_ID || !g_obj[after])) { after = NONE; g_lost = true; }
  int nx = after == NONE ? g_head : g_obj[after]->next;
  o->prev = after; o->next = nx;
  if (after == NONE) g_head = id; else g_obj[after]->next = id;
  if (nx != NONE) g_obj[nx]->prev = id;
}

static void unlink_obj(int id) {
  Obj *o = g_obj[id];
  if (o->prev == NONE) g_head = o->next; else g_obj[o->prev]->next = o->next;
  if (o->next != NONE) g_obj[o->next]->prev = o->prev;
  o->prev = o->next = NONE;
}

static void read_col(Obj *o) { for (int i = 0; i < 7; i++) o->col[i] = u8(); }

// the unchanging part of a picture
static void read_static(Obj *o) {
  int kind = u8();
  if (kind != o->kind || kind == K_TEXT || kind == K_QUADS) { free(o->ext); o->ext = NULL; }
  o->kind = kind; o->cam = u8();
  switch (kind) {
    case K_IMG: case K_SKIN:
      o->tex = u8(); o->flags = u8();
      o->ox = s16(); o->oy = s16(); o->w = s16(); o->h = s16();
      o->tx = s16(); o->ty = s16(); o->tw = s16(); o->th = s16();
      read_col(o); break;
    case K_RECT:
      o->flags = 0; o->ox = s16(); o->oy = s16(); o->w = s16(); o->h = s16();
      o->tx = u8(); o->ty = u8(); o->tw = u8(); o->th = u8();   // base colour r g b a
      read_col(o); break;
    case K_NINE: {
      o->tex = u8(); o->flags = u8();
      o->ox = s16(); o->oy = s16(); o->w = s16(); o->h = s16();
      o->tx = s16(); o->ty = s16(); o->tw = s16(); o->th = s16();
      if (!o->ext) o->ext = salloc(4);
      uint8_t *m = o->ext; for (int i = 0; i < 4; i++) { int v = u8(); if (m) m[i] = v; }
      read_col(o); break;
    }
    case K_TILES: {
      o->tex = u8();
      int cw = u8(), ch = u8(), fw = u8(), fh = u8(), mw = u8(), mh = u8();
      read_col(o);
      Tiles *t = o->ext;
      bool wide = art_cells(o->tex, fw, fh) > 256;   // (255 means none: SPD's 256-tile sheets leave their last one unused)
      // the cells get their memory with the first tile that isn't empty (C_TILES): a map with none (the grid, often)
      // costs nothing; until then its size waits here
      o->ox = cw; o->oy = ch; o->w = fw; o->h = fh; o->tx = mw; o->ty = mh; o->tw = wide;
      if (t && (t->mw != mw || t->mh != mh || t->wide != wide)) { free(t); o->ext = t = NULL; }
      if (t) { t->cw = cw; t->ch = ch; t->fw = fw; t->fh = fh; }
      break;
    }
    case K_QUADS: {
      // a picture that draws itself its own way, as a list of pieces: n x (texture x, y, w, h; place x, y, w, h)
      o->tex = u8();
      int n = u8();
      int16_t *q = salloc(2 + n * 16);
      if (q) q[0] = n;
      for (int i = 0; i < n * 8; i++) { int v = s16(); if (q) q[1 + i] = v; }
      o->ext = q;
      read_col(o);
      break;
    }
    case K_SHAPE:   // a disc or a band (round screens): kind, Pebble colour, radii, bearings, slant
      o->flags = u8(); o->tex = u8(); o->tx = u8(); o->ty = u8(); o->tw = s16(); o->th = s16(); o->w = s16(); o->h = s16(); o->frame = u8();
      break;
    case K_TEXT: {
      o->w = u8();            // which watch font (0..6)
      read_col(o);
      int n = u8();
      char *s = salloc(n + 1);
      for (int i = 0; i < n; i++) { int c = u8(); if (s) s[i] = c; }
      if (s) s[n] = 0;
      o->ext = s;
      break;
    }
  }
}

int32_t obj_dyn(const Obj *o, int i, uint32_t now) {
  int32_t v;
  if (i < 3 && predict_pos(o, i, now, &v)) return v;
  return obj_dyn_raw(o, i, now);
}

bool obj_moving(const Obj *o, uint32_t now) {
  if (o->until && now - o->at > o->until) return false;
  return o->vel[0] || o->vel[1];
}

int32_t obj_dyn_raw(const Obj *o, int i, uint32_t now) {
  int32_t v = o->dyn[i];
  if (o->vel[i]) {
    uint32_t dt = now - o->at;
    if (i >= 6) {
      // water's drift goes on for good, round its texture (the phone no longer corrects it at each wrap)
      int tw, th; int32_t per = art_size(o->tex, &tw, &th) ? (i == 6 ? tw : th) * 8 : 0;
      int64_t d = (int64_t)o->vel[i] * dt / 1000;
      if (per > 0) d %= per;
      return v + (int32_t)d;
    }
    if (dt > 60000) dt = 60000;
    if (o->until && dt > o->until) dt = o->until;
    v += (int32_t)((int64_t)o->vel[i] * dt / 1000);
  }
  if (i == 2) v = v < 0 ? 0 : v > 255 ? 255 : v;
  return v;
}

// the moving part (see Link.sendDyn)
static void read_dyn(Obj *o, uint32_t now) {
  int m = u8();
  memset(o->vel, 0, sizeof(o->vel));
  o->dyn[0] = s16(); o->dyn[1] = s16();
  o->dyn[2] = (m & 2) ? u8() : 255;
  if (m & 4) { o->dyn[3] = s16(); o->dyn[4] = s16(); } else o->dyn[3] = o->dyn[4] = 256;
  o->dyn[5] = (m & 8) ? s16() : 0;
  if (m & 16) { o->vel[0] = s16(); o->vel[1] = s16(); }
  if (m & 32) o->vel[2] = s16();
  if (m & 64) { o->vel[3] = s16(); o->vel[4] = s16(); }
  if (m & 128) o->vel[5] = s16();
  o->until = (m & 0xF0) ? u8() * 10 : 0;   // the speeds stop then (ms; 0: they don't)
  if (o->kind == K_SKIN) { o->dyn[6] = s16(); o->dyn[7] = s16(); o->vel[6] = s16(); o->vel[7] = s16(); }
  if ((o->kind == K_IMG || o->kind == K_SKIN) && (o->flags & F_ANIM)) { o->frame = u8(); o->timer = u8(); }
  o->at = now;
}

void scene_stats(int *objs, int *tiles, int *anims, int *dyns) {
  *objs = *tiles = *anims = *dyns = 0;
  for (int i = 0; i < MAX_ID; i++) if (g_obj[i]) {
    (*objs)++;
    if (g_obj[i]->kind == K_TILES && g_obj[i]->ext) { Tiles *t = g_obj[i]->ext; *tiles += t->mw * t->mh * (t->wide ? 2 : 1); }
  }
  for (int i = 0; i < MAX_ANIM; i++) if (s_anim[i]) *anims += sizeof(Anim) + s_anim[i]->n * 8;
  for (int i = 0; i < MAX_DYN; i++) if (s_dyn[i]) *dyns += (s_dyn[i]->w * s_dyn[i]->h * s_dyn[i]->bits + 7) / 8;
}

bool scene_moving(uint32_t now) {
  for (int id = g_head; id != NONE; id = g_obj[id]->next) {
    Obj *o = g_obj[id];
    if (o->flags & F_ANIM) return true;
    if (o->until && now - o->at > o->until) continue;   // arrived
    for (int i = 0; i < 8; i++) if (o->vel[i]) return true;
  }
  for (int i = 0; i < MAX_CAM; i++) if (g_cam[i].used && g_cam[i].mode) return true;
  return predict_active();
}

// ---- run-time pictures ----

static void read_dtex(void) {
  int id = u8(), w = u16(), h = u16(), piece = u16();
  int ncol = u8(); uint8_t pal[16] = {0};
  for (int i = 0; i < ncol; i++) { int c = u8(); if (i < 16) pal[i] = c; }
  int len = u16();
  const uint8_t *stop = p + len; if (stop > end) stop = end;
  if (id < 200 || id >= 200 + MAX_DYN) { p = stop; return; }
  if (!w || !h) { free(s_dyn[id - 200]); s_dyn[id - 200] = NULL; art_forget(id); p = stop; return; }   // let go (Link.freeDyn)
  int bits = ncol == 0 || ncol > 16 ? 8 : ncol <= 4 ? 2 : 4;
  DynTex *d = s_dyn[id - 200];
  int bytes = (w * h * bits + 7) / 8;
  if (!d || d->w != w || d->h != h || d->bits != bits) {
    free(d);
    s_dyn[id - 200] = NULL; art_forget(id);
    d = salloc(sizeof(DynTex) + bytes);
    if (d) { d->w = w; d->h = h; d->bits = bits; memset(d->px, 0, bytes); }
    s_dyn[id - 200] = d;
  }
  if (!d) { g_lost = true; p = stop; return; }
  d->ncol = ncol; memcpy(d->pal, pal, 16);
  int k = piece * 1024, n = w * h;
  #define PUT(v) do { if (k < n) { int vv = (v); \
      if (bits == 8) d->px[k] = vv; \
      else if (bits == 4) d->px[k >> 1] = (d->px[k >> 1] & ~(15 << ((k & 1) * 4))) | ((vv & 15) << ((k & 1) * 4)); \
      else d->px[k >> 2] = (d->px[k >> 2] & ~(3 << ((k & 3) * 2))) | ((vv & 3) << ((k & 3) * 2)); } k++; } while (0)
  while (p < stop) {
    int c = *p++;
    if (c > 128) { int v = p < stop ? *p++ : 0; for (int i = 0; i < c - 127; i++) PUT(v); }
    else for (int i = 0; i < c && p < stop; i++) { int v = *p++; PUT(v); }
  }
  #undef PUT
  art_forget(id);
}

// ---- cameras ----

static void read_cam(uint32_t now) {
  int id = u8();
  Cam *k = &g_cam[id < MAX_CAM ? id : 0];
  k->x = s16(); k->y = s16(); k->w = s16(); k->h = s16(); k->zoom = u16();
  k->sx = s16() * 256; k->sy = s16() * 256;
  int m = u8(); k->mode = m & 0x7F;
  if (m & 0x80) g_map_cam = id; else if (g_map_cam == id) g_map_cam = -1;
  if (k->mode == 1) { k->follow = u16(); k->ofx = s16(); k->ofy = s16(); }
  if (k->mode == 2) { k->px = s16(); k->py = s16(); }
  if (k->mode) { k->intensity = u8(); k->dead = u8(); }
  k->used = 1;
  // the phone's scroll follows its own hero; while the watch shows him ahead (predict.c), keep the same lead
  int32_t dx, dy;
  if (k->mode == 1 && predict_offset(k->follow, now, &dx, &dy)) { k->sx += dx * 256; k->sy += dy * 256; }
}

static int32_t dead(int32_t p, int32_t dz) { return p > dz ? p - dz : p < -dz ? p + dz : 0; }

// SPD's camera easing (Camera.update): move a share of the way to the target each step
void cams_follow(uint32_t now, uint32_t dt) {
  for (int i = 0; i < MAX_CAM; i++) {
    Cam *c = &g_cam[i];
    if (!c->used || !c->mode || !c->zoom) continue;
    // camera size in its own units x8
    int32_t cw = (int32_t)c->w * 256 / c->zoom, ch = (int32_t)c->h * 256 / c->zoom;
    int32_t tx = c->px, ty = c->py, dzx = 0, dzy = 0;
    if (c->mode == 1) {
      if (c->follow >= MAX_ID || !g_obj[c->follow]) continue;
      Obj *o = g_obj[c->follow];
      int32_t w = (o->kind == K_IMG || o->kind == K_SKIN) ? o->w : 0, h = (o->kind == K_IMG || o->kind == K_SKIN) ? o->h : 0;
      tx = obj_dyn(o, 0, now) + w * obj_dyn(o, 3, now) / 256 / 2 + c->ofx;
      ty = obj_dyn(o, 1, now) + h * obj_dyn(o, 4, now) / 256 / 2 + c->ofy;
      dzx = cw * c->dead / 255 / 2; dzy = ch * c->dead / 255 / 2;
    }
    // scroll is kept x256 for smooth easing
    int32_t panx = dead(tx * 256 - (c->sx + cw * 128), dzx * 256), pany = dead(ty * 256 - (c->sy + ch * 128), dzy * 256);
    int32_t share = (int32_t)dt * c->intensity / 16;   // per mille
    if (share > 1000) share = 1000;
    c->sx += (int32_t)((int64_t)panx * share / 1000);
    c->sy += (int32_t)((int64_t)pany * share / 1000);
  }
}

// ---- the command stream ----

// a picture the phone says the watch has (NONE: the start of the list)
static bool there(int id) { return id == NONE || (id < MAX_ID && g_obj[id]); }

// a map's cells, made with the first tiles that come for it (see K_TILES); NULL: no memory (g_lost set by salloc)
static Tiles *tiles_of(Obj *o) {
  Tiles *t = o->ext;
  if (t) return t;
  int cells = o->tx * o->ty * (o->tw ? 2 : 1);
  t = salloc(sizeof(Tiles) + cells);
  if (!t) return NULL;
  memset(t->cells, 0xFF, cells);
  t->cw = o->ox; t->ch = o->oy; t->fw = o->w; t->fh = o->h; t->mw = o->tx; t->mh = o->ty; t->wide = o->tw;
  o->ext = t;
  return t;
}

int scene_apply(const uint8_t *data, int len, uint32_t now) {
  p = data; end = data + len;
  while (p < end) {
    if (*p == C_TIME && p > data) break;   // the next frame's changes: they wait for their time
    int c = u8();
    if (c != C_TIME && c != C_LATEST && c != C_COMMIT) g_partial = true;
    switch (c) {
      case C_TIME: p += 4; break;
      case C_LATEST: p += 4; break;
      case C_RESET: scene_reset(); break;
      case C_CAM: read_cam(now); break;
      case C_NEW: {
        int id = u16(), after = u16();
        if (id >= MAX_ID || !there(after)) { g_lost = true; return len; }
        if (g_obj[id]) { unlink_obj(id); free_obj(g_obj[id]); g_obj[id] = NULL; }
        Obj *o = scalloc(sizeof(Obj));
        if (!o) return len;   // out of memory (g_lost: main.c asks the phone for the whole screen again)
        g_obj[id] = o;
        o->prev = o->next = NONE;
        read_static(o); read_dyn(o, now);
        if (g_lost) return len;
        link_after(id, after);
        break;
      }
      // (a picture the watch doesn't have: one it lost; the rest of the message can't be read without it)
      case C_SET: { int id = u16(); if (id >= MAX_ID || !g_obj[id]) { g_lost = true; return len; } read_static(g_obj[id]); read_dyn(g_obj[id], now); break; }
      case C_MOVE: { int id = u16(); if (id >= MAX_ID || !g_obj[id]) { g_lost = true; return len; } read_dyn(g_obj[id], now); break; }
      case C_ANIMTO: {   // an animated sprite's new animation (and flips), then its moving part
        int id = u16(); if (id >= MAX_ID || !g_obj[id]) { g_lost = true; return len; }
        Obj *o = g_obj[id]; o->flags = u8(); o->tx = u16(); read_dyn(o, now); break;
      }
      case C_DEL: { int id = u16(); if (id >= MAX_ID || !g_obj[id]) { g_lost = true; break; } unlink_obj(id); free_obj(g_obj[id]); g_obj[id] = NULL; break; }
      case C_ORDER: { int id = u16(), after = u16(); if (id >= MAX_ID || !g_obj[id]) { g_lost = true; break; } unlink_obj(id); link_after(id, after); break; }
      case C_TILES: {
        int id = u16(), start = u16(), n = u8();
        Obj *o = id < MAX_ID ? g_obj[id] : NULL;
        if (!o || o->kind != K_TILES) { g_lost = true; return len; }
        Tiles *t = tiles_of(o);
        if (!t) return len;   // (g_lost: start over)
        int total = t->mw * t->mh;
        for (int i = 0; i < n; i++) {
          int v = u16();
          if (start + i < total) {
            if (t->wide) ((uint16_t *)t->cells)[start + i] = v;
            else t->cells[start + i] = v > 254 ? 255 : v;
          }
        }
        break;
      }
      case C_TILES2: {   // (Link.tiles: in runs; one byte a cell, 255 empty, unless flagged two)
        int id = u16(), start = u16(), n = u16(), two = u8() & 1;
        Obj *o = id < MAX_ID ? g_obj[id] : NULL;
        if (!o || o->kind != K_TILES) { g_lost = true; return len; }
        Tiles *t = tiles_of(o);   // (it was never given its cells here: every map stayed empty, no walls or floors)
        if (!t) return len;
        int total = t->mw * t->mh, k = 0, v = 0;
        while (k < n && p < end) {
          int h = u8(), cnt = h < 128 ? h + 1 : h - 126;
          for (int i = 0; i < cnt && k < n; i++, k++) {
            if (h < 128 || i == 0) { v = two ? u16() : u8(); if (!two && v == 255) v = 0xFFFF; }
            if (t && start + k < total) {
              if (t->wide) ((uint16_t *)t->cells)[start + k] = v;
              else t->cells[start + k] = v > 254 ? 255 : v;
            }
          }
        }
        break;
      }
      case C_DTEX: read_dtex(); break;
      case C_ANIM: {
        int id = u16(), tex = u8(), n = u8(), delay = u16(), looped = u8();
        Anim *a = n ? salloc(sizeof(Anim) + n * 8) : NULL;
        for (int i = 0; i < n * 4; i++) { int v = u16(); if (a) a->rects[i] = v; }
        if (!n) { free(a); a = NULL; }   // no frames: let go (Link.freeAnims)
        if (id < MAX_ANIM) {
          free(s_anim[id]); s_anim[id] = a;
          if (a) { a->tex = tex; a->n = n; a->delay_ms = delay; a->looped = looped; }
        } else free(a);
        break;
      }
      case C_INPUT: touch_acked(u8()); break;
      case C_SOUND: { int k = u8(), v = u8(); if (k == 0) sound_effect(v); else if (k == 1) sound_music(v == 255 ? -1 : v); else sound_settings(v); break; }
      case C_COMMIT: g_dirty = true; g_partial = false; break;
      case C_PREDICT: {
        static Plan pl;
        memset(&pl, 0, sizeof pl);
        if (u8()) {
          pl.on = true; pl.marker = u16(); pl.hero = u16(); pl.step_ms = u8() * 4; pl.n = u8(); pl.cur = u8();
          for (int i = 0; i < pl.n; i++) {
            PlanTarget t; t.x = s16(); t.y = s16(); t.k = u8();
            for (int j = 0; j < t.k; j++) { int d = u8(); if (j < PLAN_S) t.dir[j] = d; }
            if (t.k > PLAN_S) t.k = PLAN_S;
            if (i < PLAN_T) pl.t[i] = t;
          }
          if (pl.n > PLAN_T) pl.n = PLAN_T;
          if (pl.cur >= pl.n) pl.cur = 0;
        }
        predict_set(&pl);
        break;
      }
      default: APP_LOG(APP_LOG_LEVEL_ERROR, "bad command %d", c); return len;
    }
  }
  return p - data;
}
