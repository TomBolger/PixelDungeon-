/*
 * Pixel Dungeon for Pebble - the dungeon's little lives (an overlay kept loaded on a floor, let go while the next one is
 * built): the walls' decorations of each region (PD's SewerLevel.Sink, PrisonLevel.Torch, CavesLevel.Vein,
 * CityLevel.Smoke, HallsLevel.Stream), ripples where something steps out of water (GameScene.ripple), blood from a
 * blow (CharSprite.bloodBurstA), leaves from trampled grass (LeafParticle), a potion's splash (Splash.at).
 * Particles are worked out from the time each frame (as the app's flames are), so nothing per particle is stored;
 * the few bursts in flight are kept in a ring of eight.
 * Entry points: fx_under (after the floor, before the creatures), fx_over (over them), fx_add (an event), and the
 * wands' bolts (fx_bolt: moved here from main.c for room).
 */
#include "pd.h"
#include "ovl.h"
#include "ui_int.h"

int map_sx(int wx), map_sy(int wy), map_tpx(void);   // main.c: world units to the screen; a tile's size on it

enum { FXE_RIPPLE, FXE_BLOOD, FXE_LEAVES, FXE_SPLASH };
typedef struct { uint8_t kind, n; int8_t dir; uint8_t col; int16_t cell; uint32_t t0; } Burst;
#define NB 8
static Burst s_b[NB];
static uint8_t s_bn;

static uint32_t hash(uint32_t a, uint32_t b) { uint32_t h = a * 2654435761u ^ b * 40503u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return h; }
static int T(void) { return map_tpx(); }
// a square of `size` world pixels at world (x, y), on the screen (never under a pixel)
static void sq(int wx, int wy, int size, uint8_t c) {
  int s = size * T() / 16; if (s < 1) s = 1;
  blit_fill(map_sx(wx) - s / 2, map_sy(wy) - s / 2, s, s, c);
}
// sine of a/64 of a turn, x256
static int sin64(int a) {
  static const uint16_t Q[17] = { 0, 25, 50, 74, 98, 121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256 };
  a &= 63;
  int q = a & 15, v = (a & 16) ? Q[16 - q] : Q[q];
  return a & 32 ? -v : v;
}
static int cos64(int a) { return sin64(a + 16); }

// GameScene.ripple: Effects.RIPPLE, a ring on the water opening out and fading over 0.5 s
static void ring(int cx, int cy, uint32_t age) {
  if (age >= 500) return;
  int t = T(), rx = (int)(8 * t / 16) * (int)age / 500, ry = rx / 2;
  if (rx < 1) return;
  uint8_t c = age < 250 ? ARGB(GColorWhite) : ARGB(GColorCeleste);
  int sx = map_sx(cx), sy = map_sy(cy), step = age < 300 ? 1 : 2;   // fading: every other point
  for (int a = 0; a < 64; a += step) blit_fill(sx + rx * cos64(a) / 256, sy + ry * sin64(a) / 256, 1, 1, c);
}

static bool s_fast;   // something drew that moves: keep redrawing often

// ---- the walls ----
// SewerLevel.Sink: a drop every 0.05 s from a pipe, falling 0.5 s (acc 50 px/s2), two greys of water; a ripple below
static void sink(int c, uint32_t now) {
  int cx = c % MAP_W * TS + TS / 2, cy = c / MAP_W * TS + TS / 2;
  for (int i = 0; i < 10; i++) {
    uint32_t t = now + (uint32_t)c * 131 + i * 50, a = t % 500, k = t / 500;
    uint32_t h = hash(k * 10 + i, c);
    int y = cy + 1 + (int)(25 * a * a / 1000000);
    sq(cx - 2 + (int)(h & 3), y, 2, h & 16 ? ARGB(GColorCadetBlue) : ARGB(GColorMidnightGreen));
  }
  if (c + MAP_W < MAP_LEN && BIT_GET(G.fov, c + MAP_W)) {
    uint32_t t = now + (uint32_t)c * 977;
    ring(cx, cy + TS - TS / 2, t % 250);
    ring(cx, cy + TS - TS / 2, t % 250 + 250);
  }
}
// PrisonLevel.Torch: a flame every 0.15 s from a 2 px wide spot, rising ever faster and shrinking over 0.6 s
static void torch(int c, uint32_t now) {
  int cx = c % MAP_W * TS + TS / 2, cy = c / MAP_W * TS + TS / 2;
  for (int i = 0; i < 4; i++) {
    uint32_t t = now + (uint32_t)c * 211 + i * 150, a = t % 600, k = t / 600;
    uint32_t h = hash(k * 4 + i, c);
    int size = (4 * (600 - (int)a) + 300) / 600;
    if (size < 1) continue;
    int y = cy + 3 - (int)(a * a / 25000);
    sq(cx - 1 + (int)(h & 1), y, size, ARGB(GColorOrange));
    if (size >= 3) sq(cx - 1 + (int)(h & 1), y, size - 2, ARGB(GColorChromeYellow));
  }
}
// CavesLevel.Vein: about two sparkles a second somewhere on the crystal, each swelling to 2 px and gone in 0.5 s
static void vein(int c, uint32_t now) {
  int x0 = c % MAP_W * TS, y0 = c / MAP_W * TS;
  for (int j = 0; j < 2; j++) {
    uint32_t t = now + (uint32_t)c * 389, k = t / 500 - j;
    uint32_t h = hash(k, c), start = k * 500 + (h >> 20) % 250, a = t - start;
    if (t < start || a >= 500) continue;
    int p = (int)a * 256 / 500, am = p < 128 ? p * 2 : (256 - p) * 2;   // 0..256
    int size = am * 2 / 256;
    if (size < 1) continue;
    sq(x0 + (int)(h & 15), y0 + (int)((h >> 4) & 15), size, ARGB(GColorWhite));
  }
}
// CityLevel.Smoke: a black puff every 0.2 s drifting up and right for 2 s, growing 4 -> 8 px, never more than a
// quarter seen
static void smoke(int c, uint32_t now) {
  int cx = c % MAP_W * TS + TS / 2, cy = c / MAP_W * TS + TS / 2, t16 = T();
  for (int i = 0; i < 10; i++) {
    uint32_t t = now + (uint32_t)c * 157 + i * 200, a = t % 2000, k = t / 2000;
    uint32_t h = hash(k * 10 + i, c);
    int p = 256 - (int)a * 256 / 2000;                       // left / lifespan
    int am = p > 205 ? 256 - p : p / 4;                      // x256
    int vx = (int)(h & 7), vy = (int)((h >> 3) & 7);
    int x = cx - 4 + (int)((h >> 6) & 3) + vx * (int)a / 1000, y = cy - 2 - vy * (int)a / 1000;
    int r = (8 - 4 * p / 256) * t16 / 32;
    blit_puff(map_sx(x), map_sy(y), r < 1 ? 1 : r, ARGB(GColorBlack), am * 16 / 256, (int)(h >> 9) & 15);
  }
}
// HallsLevel.Stream: from the lava, now and then (about once a second a cell) a 4 px flame leaps 10 px and falls back,
// shrinking away over 1 s
static void stream(int c, uint32_t now) {
  int x0 = c % MAP_W * TS, y0 = c / MAP_W * TS;
  for (int j = 0; j < 2; j++) {
    uint32_t t = now + (uint32_t)c * 613, k = t / 1000 - j;
    uint32_t h = hash(k, c + 5000), start = k * 1000 + (h >> 22) % 1000, a = t - start;
    if (t < start || a >= 1000) continue;
    int size = 4 * (1000 - (int)a) / 1000;
    if (size < 1 || a < 60) continue;                        // (fading in)
    int y = y0 + (int)((h >> 4) & 15) - (int)(40 * a / 1000) + (int)(80 * a / 1000 * a / 2000);
    sq(x0 + (int)(h & 15), y, size, ARGB(GColorOrange));
  }
}

static bool fx_under(void) {
  s_fast = false;
  int rg = region_of(G.depth), t = T();
  uint32_t now = now_ms();
  for (int y = 0; y < MAP_H; y++) {
    int py = map_sy(y * TS);
    if (py + 2 * t < 0 || py - t >= s_h) continue;
    for (int x = 0; x < MAP_W; x++) {
      int px = map_sx(x * TS);
      if (px + 2 * t < 0 || px - t >= s_w) continue;
      int c = y * MAP_W + x, m = G.map[c];
      if (!BIT_GET(G.fov, c)) continue;
      if (m == T_WALL_DECO) {
        s_fast = true;
        switch (rg) { case 0: sink(c, now); break; case 1: torch(c, now); break; case 2: vein(c, now); break; case 3: smoke(c, now); break; }
      } else if (rg == 4 && IS_WATER(m)) { s_fast = true; stream(c, now); }
    }
  }
  for (int i = 0; i < NB; i++) {
    Burst *b = &s_b[i];
    uint32_t age = now - b->t0;
    if (b->kind != FXE_RIPPLE || !b->t0 || age >= 500) continue;
    s_fast = true;
    ring(b->cell % MAP_W * TS + TS / 2, b->cell / MAP_W * TS + TS / 2, age);
  }
  return s_fast;
}

// ---- bursts over the creatures ----
// blood (CharSprite.blood per kind), potions (PD's flavours), leaves (each region's two colours: Level.color1/2)
static uint8_t blood_of(int who) {
  if (who < 0 || who >= MAX_MOBS) return ARGB(GColorDarkCandyAppleRed);
  switch (G.mobs[who].type) {
    case MT_ACIDIC: case MT_SCORPIO: return ARGB(GColorGreen);
    case MT_BEE: case MT_MIMIC: return ARGB(GColorChromeYellow);
    case MT_CRAB: case MT_DM300: return ARGB(GColorPastelYellow);
    case MT_ELEMENTAL: return ARGB(GColorOrange);
    case MT_GHOST: return ARGB(GColorWhite);
    case MT_GOLEM: return ARGB(GColorDarkGray);
    case MT_GOO: case MT_WRAITH: return ARGB(GColorBlack);
    case MT_LARVA: return ARGB(GColorBrass);
    case MT_SKELETON: case MT_UNDEAD: case MT_STATUE: return ARGB(GColorLightGray);
    case MT_SPINNER: return ARGB(GColorMintGreen);
    case MT_SWARM: return ARGB(GColorMayGreen);
    default: break;
  }
  return ARGB(GColorDarkCandyAppleRed);
}
static const uint8_t POT[12] = {   // turquoise, crimson, azure, jade, golden, magenta, charcoal, ivory, amber, bistre, indigo, silver
  GColorTiffanyBlueARGB8, GColorRedARGB8, GColorVividCeruleanARGB8, GColorJaegerGreenARGB8, GColorYellowARGB8, GColorMagentaARGB8,
  GColorDarkGrayARGB8, GColorWhiteARGB8, GColorChromeYellowARGB8, GColorWindsorTanARGB8, GColorIndigoARGB8, GColorLightGrayARGB8 };
static const uint8_t LEAF[5][2] = {
  { GColorDarkGreenARGB8, GColorMayGreenARGB8 }, { GColorArmyGreenARGB8, GColorBrassARGB8 }, { GColorArmyGreenARGB8, GColorInchwormARGB8 },
  { GColorDarkGreenARGB8, GColorWhiteARGB8 }, { GColorBulgarianRoseARGB8, GColorWindsorTanARGB8 } };

static bool fx_over(void) {
  uint32_t now = now_ms();
  bool any = false;
  for (int i = 0; i < NB; i++) {
    Burst *b = &s_b[i];
    if (!b->t0 || b->kind == FXE_RIPPLE) continue;
    uint32_t age = now - b->t0;
    int cx = b->cell % MAP_W * TS + TS / 2, cy = b->cell / MAP_W * TS + TS / 2;
    if (b->kind == FXE_LEAVES) {                             // LeafParticle: up at 20 px/s, drifting, falling back (acc 25), 1.2 s
      if (age >= 1200) { b->t0 = 0; continue; }
      any = true;
      for (int k = 0; k < b->n; k++) {
        uint32_t h = hash(b->t0 + k, b->cell);
        int vx = (int)(h % 17) - 8, x = cx - 8 + (int)((h >> 5) & 15) + vx * (int)age / 1000;
        int y = cy - 8 + (int)((h >> 9) & 15) - 20 * (int)age / 1000 + (int)(25 * age / 1000 * age / 2000);
        int size = (2 + (int)((h >> 13) & 1)) * (1200 - (int)age) / 1200;
        if (size >= 1) sq(x, y, size, LEAF[region_of(G.depth)][(h >> 14) & 1]);
      }
      continue;
    }
    // Splash: n drops thrown out within the cone at 40-80 px/s, falling (acc 100), each living 0.5-1 s, shrinking from 4 px
    if (age >= 1000) { b->t0 = 0; continue; }
    any = true;
    int dir = b->dir == -128 ? 48 : b->dir, cone = b->kind == FXE_SPLASH ? 32 : 16;   // in 64ths of a turn; up: 48
    uint8_t col = b->kind == FXE_SPLASH ? POT[b->col % 12] : b->col;
    for (int k = 0; k < b->n; k++) {
      uint32_t h = hash(b->t0 + k * 7, b->cell), life = 500 + (h & 511);
      if (age >= life) continue;
      int ang = dir - cone / 2 + (int)((h >> 9) % (uint32_t)(cone + 1)), v = 40 + (int)((h >> 15) % 41);
      int x = cx + cos64(ang) * v / 256 * (int)age / 1000;
      int y = cy + sin64(ang) * v / 256 * (int)age / 1000 + (int)(100 * age / 1000 * age / 2000);
      int size = 4 * (int)(life - age) / (int)life;
      if (size >= 1) sq(x, y, size, col);
    }
  }
  return any;
}

// MagicMissile, Lightning and DeathRay (moved here from main.c for room)
static void draw_line(int x0, int y0, int x1, int y1, int w, uint8_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int g = 0; g < 800; g++) {
    blit_fill(x0 - w / 2, y0 - w / 2, w, w, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
// MagicMissile colours and sizes per wand (Wand.wands order): cold, slowness, fire, poison, foliage, white,
// (lightning), purple, force, wool, (death ray), earth
static void fx_bolt(int el) {
  static const uint8_t HEAD[14] = { GColorBlueMoonARGB8, GColorWindsorTanARGB8, GColorOrangeARGB8, GColorGreenARGB8,
    GColorKellyGreenARGB8, GColorWhiteARGB8, GColorCelesteARGB8, GColorFollyARGB8, GColorWhiteARGB8, GColorLightGrayARGB8,
    GColorPurpureusARGB8, GColorLightGrayARGB8, GColorPurpleARGB8, GColorCelesteARGB8 };   // 12: MagicMissile.shadow (Warlock), 13: blueLight (magic missile)
  static const uint8_t TAIL[14] = { GColorPictonBlueARGB8, GColorBrassARGB8, GColorChromeYellowARGB8, GColorPurpleARGB8,
    GColorIslamicGreenARGB8, GColorLightGrayARGB8, GColorWhiteARGB8, GColorPurpleARGB8, GColorLightGrayARGB8, GColorWhiteARGB8,
    GColorWhiteARGB8, GColorDarkGrayARGB8, GColorImperialPurpleARGB8, GColorPictonBlueARGB8 };
  static const uint8_t SIZE[14] = { 4, 1, 4, 3, 4, 4, 0, 2, 1, 3, 0, 2, 3, 3 };
  int T = map_tpx();
  int k = -1 - s_proj.sprite, h = TS / 2;
  if (k == IK_WND_LIGHTNING - IK_FIRST_WAND) {               // Lightning: jagged arcs between the creatures it jumped to
    for (int i = 0; i + 1 < s_narc; i++) {
      int ax = map_sx((s_arc[i] % MAP_W) * TS + h), ay = map_sy((s_arc[i] / MAP_W) * TS + h);
      int bx = map_sx((s_arc[i + 1] % MAP_W) * TS + h), by = map_sy((s_arc[i + 1] / MAP_W) * TS + h);
      uint32_t j = (now_ms() / 33 + i * 7) * 2654435761u;      // a new zigzag every frame (not the game's dice)
      int mx = (ax + bx) / 2 + (int)(j >> 28) % 9 - 4, my = (ay + by) / 2 + (int)((j >> 24) & 15) % 9 - 4;
      draw_line(ax, ay, mx, my, 2, GColorCelesteARGB8); draw_line(mx, my, bx, by, 2, GColorCelesteARGB8);
      draw_line(ax, ay, mx, my, 1, GColorWhiteARGB8); draw_line(mx, my, bx, by, 1, GColorWhiteARGB8);
    }
    return;
  }
  int x0 = map_sx(s_proj.x0 + h), y0 = map_sy(s_proj.y0 + h), x1 = map_sx(s_proj.x1 + h), y1 = map_sy(s_proj.y1 + h);
  if (k == IK_WND_DISINTEGRATION - IK_FIRST_WAND) {          // DeathRay: a beam that thins away
    int w = (s_proj.ms - el) * 6 * T / 16 / s_proj.ms + 1;
    draw_line(x0, y0, x1, y1, w, GColorPurpureusARGB8);
    if (w > 2) draw_line(x0, y0, x1, y1, w / 2, GColorWhiteARGB8);
    return;
  }
  int sz = (SIZE[k] + 2) * T / 16; if (sz < 2) sz = 2;
  for (int t = 3; t >= 0; t--) {                             // the trail, then the spark
    int e = el - t * 25; if (e < 0) continue;
    int px = x0 + (x1 - x0) * e / s_proj.ms, py = y0 + (y1 - y0) * e / s_proj.ms, z = t ? (sz + 1) / 2 : sz;
    blit_fill(px - z / 2, py - z / 2, z, z, t ? TAIL[k] : HEAD[k]);
  }
}

// direction in 64ths of a turn from one cell to another (screen y down: 16 is down, 48 up)
static int dir_of(int from, int to) {
  int dx = to % MAP_W - from % MAP_W, dy = to / MAP_W - from / MAP_W;
  static const int8_t D[3][3] = { { 40, 48, 56 }, { 32, 0, 0 }, { 24, 16, 8 } };   // [dy+1][dx+1]
  if (dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy)) return -128;
  return D[dy + 1][dx + 1];
}

// an event: kind, its cell, then: blood: who was hit (-1 the hero) and the damage; splash: the potion's flavour
static void fx_add(int kind, int cell, int a, int b) {
  if (cell < 0 || cell >= MAP_LEN) return;
  Burst *s = &s_b[s_bn++ % NB];
  s->kind = kind; s->cell = cell; s->t0 = now_ms() | 1; s->dir = -128; s->col = 0;
  switch (kind) {
    case FXE_BLOOD: {                                      // bloodBurstA: from the blow's side, more for a bigger share of HT
      int ht = a < 0 ? G.hero.HT : G.mobs[a].HT, n = 0;
      if (ht <= 0 || b <= 0) { s->t0 = 0; return; }
      while (n < 9 && (n + 1) * (n + 1) * ht <= 81 * b) n++;
      if (!n) { s->t0 = 0; return; }
      s->n = n; s->col = blood_of(a);
      if (a >= 0) s->dir = dir_of(G.hero.pos, cell);        // a monster hit: most likely by the hero
      else for (int i = 0; i < G.n_mobs; i++)               // the hero: by a monster next to him
        if (G.mobs[i].HP > 0 && G.mobs[i].state == ST_HUNTING && dir_of(G.mobs[i].pos, cell) != -128) { s->dir = dir_of(G.mobs[i].pos, cell); break; }
      break;
    }
    case FXE_LEAVES: s->n = 4; break;
    case FXE_SPLASH: s->n = 5; s->col = a; break;
  }
}

OVL_ENTRY(fx, (void *)fx_under, (void *)fx_over, (void *)fx_add, (void *)fx_bolt);
