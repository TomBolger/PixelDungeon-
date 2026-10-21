/*
 * Shattered PD on the watch - particles it makes itself.
 * The phone sends each emitter once (K_EMIT: where, how often, how many; scene.c) and a recipe for its particles
 * (C_RECIPE, measured from Shattered's own particle classes: pebxel/Particles.java). Here they are made at their
 * times, and each one's place, size, colour and see-through-ness worked out from its age when drawn (draw.c), so
 * they cost no radio at all after that: they took most of it before, a picture and its corrections each.
 * A particle: where and when it started, its speed; the recipe says the rest (pull, curves over its life).
 */
#pragma GCC optimize ("Os")   // (the app's code, data and bss must stay under 64 KB: Gabbro's went over)
#include "particles.h"
#include "art.h"

#define MAX_PART 128

typedef struct { uint16_t owner; int16_t x, y, vx, vy; uint16_t born, life; uint8_t mul, mix; } Part;
static Part *s_p;                // (from the heap with the first particle, given back when a floor is left)
static int s_np;
static Recipe *s_rec[MAX_RECIPE];

static uint32_t s_seed = 0x2545F491;
static uint32_t rnd(void) { s_seed ^= s_seed << 13; s_seed ^= s_seed >> 17; s_seed ^= s_seed << 5; return s_seed; }
static int32_t rnd_in(int32_t a, int32_t b) { return b <= a ? a : a + (int32_t)(rnd() % (uint32_t)(b - a + 1)); }

// ---- recipes ----

void recipe_set(int id, const uint8_t *b, int len) {
  if (id < 0 || id >= MAX_RECIPE || len < 1) return;
  Recipe *r = s_rec[id];
  if (!r) { r = art_alloc(sizeof(Recipe)); if (!r) return; s_rec[id] = r; }
  memset(r, 0, sizeof *r);
  const uint8_t *p = b, *end = b + len;
  #define U8() (p < end ? *p++ : 0)
  #define U16() (p += 2, p <= end ? (uint16_t)(p[-2] | p[-1] << 8) : 0)
  r->flags = U8();
  if (r->flags & R_FRAME) { r->tex = U8(); r->tx = U16(); r->ty = U16(); r->tw = U8(); r->th = U8(); }
  r->life0 = U8() * 20; r->life1 = U8() * 20;
  r->colA = U16(); r->colB = U16();
  for (int i = 0; i < 4; i++) r->s[i] = U8();
  r->mul0 = U8(); r->mul1 = U8();
  for (int i = 0; i < 4; i++) r->a[i] = U8();
  for (int i = 0; i < 4; i++) r->v[i] = (int16_t)U16();
  if (r->flags & R_PULL) { r->ax = (int16_t)U16(); r->ay = (int16_t)U16(); }
  r->ox = (int8_t)U8(); r->oy = (int8_t)U8(); r->back = (int8_t)U8();
  #undef U8
  #undef U16
  if (!r->life1) r->life1 = 20;
  if (r->life0 > r->life1) r->life0 = r->life1;
}

const Recipe *recipe_get(int id) { return id >= 0 && id < MAX_RECIPE ? s_rec[id] : NULL; }

void recipes_reset(void) { for (int i = 0; i < MAX_RECIPE; i++) { free(s_rec[i]); s_rec[i] = NULL; } }

void particles_reset(void) { s_np = 0; free(s_p); s_p = NULL; }

void particles_free(uint16_t owner) {
  int k = 0;
  for (int i = 0; i < s_np; i++) if (s_p[i].owner != owner) s_p[k++] = s_p[i];
  s_np = k;
}

// ---- making them ----

static void spawn(uint16_t owner, const Recipe *r, int32_t x, int32_t y, uint32_t at) {
  if (!s_p && !(s_p = art_alloc(MAX_PART * sizeof(Part)))) return;
  if (s_np >= MAX_PART) return;   // (the watch's share is full: this one isn't made)
  Part *p = &s_p[s_np++];
  int32_t vx, vy;
  if (r->flags & R_BOX) { vx = rnd_in(r->v[0], r->v[1]); vy = rnd_in(r->v[2], r->v[3]); }
  else {
    int32_t a = (uint16_t)r->v[0] + rnd_in(0, (uint16_t)r->v[1]), s = rnd_in((uint16_t)r->v[2], (uint16_t)r->v[3]);
    vx = s * cos_lookup(a & 0xFFFF) / TRIG_MAX_RATIO; vy = s * sin_lookup(a & 0xFFFF) / TRIG_MAX_RATIO;
  }
  int32_t life = rnd_in(r->life0, r->life1);
  p->owner = owner; p->vx = vx; p->vy = vy; p->born = at; p->life = life;
  p->mul = rnd_in(r->mul0, r->mul1); p->mix = rnd() & 255;
  // the start: offset, and back along its path (wind and snow come from up their way)
  p->x = x + r->ox + vx * life / 1000 * r->back / 64;
  p->y = y + r->oy + vy * life / 1000 * r->back / 64;
}

// where an emitter's particles start (x8): its area, or its followed picture's
static bool area(Obj *o, Emit *e, uint32_t now, int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
  *x = obj_dyn(o, 0, now); *y = obj_dyn(o, 1, now); *w = e->w; *h = e->h;
  if (e->follow != NONE) {
    Obj *t = e->follow < MAX_ID ? g_obj[e->follow] : NULL;
    if (!t) return false;
    int32_t tx = obj_dyn(t, 0, now), ty = obj_dyn(t, 1, now);
    if (e->flags & E_FILL) { *x = tx; *y = ty; *w = (int32_t)t->w * obj_dyn(t, 3, now) / 256; *h = (int32_t)t->h * obj_dyn(t, 4, now) / 256; }
    else { *x += tx; *y += ty; }
  }
  return true;
}

static void spawn_all(uint16_t id, Obj *o, Emit *e, const Recipe *r, uint32_t at, uint32_t now) {
  int32_t x, y, w, h;
  if (e->flags & E_CELLS) {
    int32_t cs = e->cs * 8;
    for (int j = 0; j < e->ch; j++) for (int i = 0; i < e->cw; i++) {
      int k = j * e->cw + i;
      if (!(e->bits[k >> 3] & (1 << (k & 7)))) continue;
      if ((e->flags & E_SIGHT) && !scene_in_sight(e->cl + i, e->ct + j)) continue;
      int32_t fx = rnd_in(e->bound[0], e->bound[2]), fy = rnd_in(e->bound[1], e->bound[3]);
      spawn(id, r, (e->cl + i) * cs + cs * fx / 64, (e->ct + j) * cs + cs * fy / 64, at);
    }
    return;
  }
  if (!area(o, e, now, &x, &y, &w, &h)) return;
  spawn(id, r, x + rnd_in(0, w > 0 ? w : 0), y + rnd_in(0, h > 0 ? h : 0), at);
}

void emit_update(uint16_t id, Obj *o, uint32_t now) {
  Emit *e = o->ext;
  const Recipe *r = e ? recipe_get(e->recipe) : NULL;
  if (!r) return;
  uint32_t step = e->interval;
  if (e->fresh) {
    e->fresh = 0;
    if (e->quantity) {   // a burst (or a run of so many): what the phone has made of it already, at once
      for (int i = 0; i < e->pre; i++) spawn_all(id, o, e, r, now, now);
      e->count = e->pre;
      e->next = now + step;
    } else if (step) {
      // pouring for a while already (a torch coming into view): as if it had been, older ones part way through
      uint32_t back = r->life1;
      for (uint32_t t = step; t < back && t / step <= 8; t += step) spawn_all(id, o, e, r, now - t, now);
      e->next = now + rnd_in(0, step);
    }
  }
  if (!(e->flags & E_ON) || !step) return;
  if ((e->quantity && e->count >= e->quantity)) return;
  // (far behind, after a pause: start again from now rather than making them all)
  if ((int32_t)(now - e->next) > (int32_t)r->life1) e->next = now - r->life1;
  int made = 0;
  while ((int32_t)(now - e->next) >= 0 && made < 24) {
    spawn_all(id, o, e, r, e->next, now);
    e->next += step; e->count++; made++;
    if (e->quantity && e->count >= e->quantity) break;
  }
}

// ---- their looks, by age ----

static int curve(const uint8_t *c, int u) {   // u: age 0..255
  int k = c[1];
  if (u <= k) return k ? c[0] + (c[2] - c[0]) * u / k : c[2];
  return k < 255 ? c[2] + (c[3] - c[2]) * (u - k) / (255 - k) : c[2];
}

static void colour(uint16_t v, uint8_t *col) {   // 2 bits each: multiply r g b, add r g b
  col[0] = ((v >> 4) & 3) * 85; col[1] = ((v >> 2) & 3) * 85; col[2] = (v & 3) * 85;
  col[3] = ((v >> 10) & 3) * 85; col[4] = ((v >> 8) & 3) * 85; col[5] = ((v >> 6) & 3) * 85;
}

bool emit_next(uint16_t id, uint32_t now, int *iter, PDraw *d) {
  for (int i = *iter; i < s_np; i++) {
    Part *p = &s_p[i];
    if (p->owner != id) continue;
    uint16_t age = (uint16_t)now - p->born;
    if (age >= p->life) {   // gone: the last one takes its place (and is looked at next)
      s_p[i] = s_p[--s_np]; i--; continue;
    }
    if (age > 0x8000) continue;   // (not born yet: backdated ones are made with their own times)
    Obj *o = g_obj[id]; Emit *e = o ? o->ext : NULL;
    const Recipe *r = e ? recipe_get(e->recipe) : NULL;
    if (!r) continue;
    *iter = i + 1;
    int32_t t = age;
    d->x = p->x + (int32_t)p->vx * t / 1000 + (int32_t)r->ax * t / 1000 * t / 2000;
    d->y = p->y + (int32_t)p->vy * t / 1000 + (int32_t)r->ay * t / 1000 * t / 2000;
    int u = t * 255 / p->life;
    int32_t s = curve(r->s, u) * p->mul;   // x64
    if (r->flags & R_FLICKER) s = rnd_in(0, s);
    d->size = r->flags & R_FRAME ? s : s / 64;   // (frame: scale x4096; square: side x8)
    d->alpha = curve(r->a, u);
    uint8_t a[6], b[6];
    colour(r->colA, a); colour(r->colB, b);
    int m = r->flags & R_RANDOM ? p->mix : r->flags & R_FADE ? u : 0;
    for (int c = 0; c < 6; c++) d->col[c] = a[c] + (b[c] - a[c]) * m / 255;
    d->col[6] = 0;
    return true;
  }
  *iter = s_np;
  return false;
}
