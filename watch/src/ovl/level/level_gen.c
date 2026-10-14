/*
 * Pixel Dungeon for Pebble - the level generator (RegularLevel + SewerLevel), an overlay.
 * Built by tools/build_ovl.py into a resource and loaded only while a floor is generated,
 * so its code and scratch buffers cost no RAM during play.
 */
#include "pd.h"
#include "ovl.h"

#define GAS_TRAPS 1                                    // 0.6: toxic, fire and paralytic traps exist
// Changes from PD: integer random draws; every retry loop is bounded (the real watch froze on an
// unbounded one); special rooms are not generated yet.
// ---------------- Rooms and the room graph (Room.java, Graph.java) ----------------
#define R (G.rooms)
static int width(const Room *r) { return r->right - r->left; }
static int height(const Room *r) { return r->bottom - r->top; }
typedef struct { int l, t, r, b; } Rect;
static Rect intersect(const Room *a, const Room *b) {
  Rect i = { a->left > b->left ? a->left : b->left, a->top > b->top ? a->top : b->top,
             a->right < b->right ? a->right : b->right, a->bottom < b->bottom ? a->bottom : b->bottom };
  return i;
}

typedef uint64_t RMask;
// Room.connected (a HashMap<Room, Door>) as an edge list; door -1 until placeDoors
enum { DOOR_EMPTY, DOOR_TUNNEL, DOOR_REGULAR, DOOR_UNLOCKED, DOOR_HIDDEN, DOOR_BARRICADE, DOOR_LOCKED };
typedef struct { uint8_t a, b; int8_t x, y; uint8_t type; } Edge;
#define MAX_EDGES 160
// 0.12: the generator's scratch lives in the game's path-finding queue (idle while a floor is made), not in the
// overlay's own bss: the overlay has to fit in one free block of the heap
typedef struct {
  RMask nb[MAX_ROOMS];                                 // Room.neigbours
  int16_t dist[MAX_ROOMS], price[MAX_ROOMS];
  Edge e[MAX_EDGES];
  uint8_t pa[MAP_LEN / 8], pb[MAP_LEN / 8];            // Patch.generate's two generations
} Scratch;
extern int16_t *s_queue;                               // game.c: MAP_LEN int16s
_Static_assert(sizeof(Scratch) <= MAP_LEN * sizeof(int16_t), "level scratch outgrew the path-finding queue");
#define SCR ((Scratch *)(void *)s_queue)
#define s_nb (SCR->nb)
#define s_dist (SCR->dist)
#define s_price (SCR->price)
#define s_e (SCR->e)
#define s_pa (SCR->pa)
#define s_pb (SCR->pb)
static int s_ne;
static int s_room_entrance, s_room_exit;

static int edge_find(int a, int b) {
  for (int i = 0; i < s_ne; i++) if ((s_e[i].a == a && s_e[i].b == b) || (s_e[i].a == b && s_e[i].b == a)) return i;
  return -1;
}
static void connect(int a, int b) {
  if (edge_find(a, b) < 0 && s_ne < MAX_EDGES) s_e[s_ne++] = (Edge){ a, b, -1, -1, DOOR_EMPTY };
}
static int n_connected(int r) { int n = 0; for (int i = 0; i < s_ne; i++) if (s_e[i].a == r || s_e[i].b == r) n++; return n; }
static int other(const Edge *e, int r) { return e->a == r ? e->b : e->a; }
static void door_set(Edge *e, int type) { if (type > e->type) e->type = type; }

static int popcount(RMask m) { int n = 0; while (m) { m &= m - 1; n++; } return n; }
static int mask_pick(RMask m) {           // Random.element over a set of rooms
  int n = popcount(m);
  if (!n) return -1;
  int k = rnd_int(n);
  for (int i = 0; i < MAX_ROOMS; i++) if ((m >> i) & 1) { if (k-- == 0) return i; }
  return -1;
}

// Graph.buildDistanceMap: distances to focus, weighted by each node's price
static void build_distance_map(int focus) {
  static uint8_t q[512];
  for (int i = 0; i < G.n_rooms; i++) s_dist[i] = 0x7FFF;
  int qh = 0, qt = 0;
  s_dist[focus] = 0; q[qt++] = focus;
  while (qh < qt) {
    int node = q[qh++], d = s_dist[node], p = s_price[node];
    for (int e = 0; e < G.n_rooms; e++)
      if (((s_nb[node] >> e) & 1) && s_dist[e] > d + p) { s_dist[e] = d + p; if (qt < (int)sizeof q) q[qt++] = e; }
  }
}
// Graph.buildPath: greedy descent along the distance map
static int build_path(int from, int to, uint8_t *path) {
  int n = 0, room = from;
  while (room != to && n < MAX_ROOMS) {
    int min = s_dist[room], mr = -1;
    for (int e = 0; e < G.n_rooms; e++) if (((s_nb[room] >> e) & 1) && s_dist[e] < min) { min = s_dist[e]; mr = e; }
    if (mr < 0) return -1;
    path[n++] = mr; room = mr;
  }
  return room == to ? n : -1;
}

// ---------------- Painter helpers ----------------
static void set(int x, int y, int v) { if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) G.map[x + y * MAP_W] = v; }
static void fill(int x, int y, int w, int h, int v) { for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) set(i, j, v); }
static void fill_room(const Room *r, int m, int v) { fill(r->left + m, r->top + m, width(r) + 1 - 2 * m, height(r) + 1 - 2 * m, v); }
static void room_center(const Room *r, int *x, int *y) {
  *x = (r->left + r->right) / 2 + (((r->right - r->left) & 1) == 1 ? rnd_int(2) : 0);
  *y = (r->top + r->bottom) / 2 + (((r->bottom - r->top) & 1) == 1 ? rnd_int(2) : 0);
}
// Painter.drawInside: n cells from a door inwards
static void draw_inside(const Room *r, int fx, int fy, int n, int v) {
  int sx = 0, sy = 0;
  if (fx == r->left) sx = 1; else if (fx == r->right) sx = -1; else if (fy == r->top) sy = 1; else if (fy == r->bottom) sy = -1;
  int x = fx + sx, y = fy + sy;
  for (int i = 0; i < n; i++) { set(x, y, v); x += sx; y += sy; }
}

static uint8_t s_depth, s_region;                    // region: 0 sewers .. 4 halls
static int16_t *s_tombs; static int *s_ntombs;
static int tunnel_tile(void) { return G.feeling == FEEL_CHASM ? T_EMPTY_SP : T_EMPTY; }
static bool boss_next(void) { return (s_depth + 1) % 5 == 0 && s_depth + 1 <= 25; }   // Dungeon.bossLevel(depth + 1)
static bool boss_here(void) { return s_depth % 5 == 0 && s_depth <= 25; }               // Dungeon.bossLevel()

// ---------------- Painters ----------------
static void paint_stairs(int ri, int tile) {          // EntrancePainter / ExitPainter
  Room *r = &R[ri];
  fill_room(r, 0, T_WALL);
  fill_room(r, 1, T_EMPTY);
  for (int i = 0; i < s_ne; i++) if (s_e[i].a == ri || s_e[i].b == ri) door_set(&s_e[i], DOOR_REGULAR);
  int c = level_room_random(r, 1);
  if (tile == T_ENTRANCE) G.entrance = c; else G.exit = c;
  G.map[c] = tile;
}

static void paint_burned(Room *r) {
  for (int i = r->top + 1; i < r->bottom; i++)
    for (int j = r->left + 1; j < r->right; j++) {
      int t = T_EMBERS;
      switch (rnd_int(5)) {
        case 0: t = T_EMPTY; break;
        case 1: t = GAS_TRAPS ? T_FIRE_TRAP : T_EMBERS; break;
        case 2: t = GAS_TRAPS ? T_SECRET_FIRE_TRAP : T_EMBERS; break;
        case 3: t = T_INACTIVE_TRAP; break;
      }
      G.map[i * MAP_W + j] = t;
    }
}
static void paint_graveyard(Room *r) {
  fill(r->left + 1, r->top + 1, width(r) - 1, height(r) - 1, T_GRASS);
  int w = width(r) - 1, h = height(r) - 1;
  int n = (w > h ? w : h) / 2;
  int index = rnd_int(n), shift = rnd_int(2);
  for (int i = 0; i < n; i++) {
    int pos = w > h ? r->left + 1 + shift + i * 2 + (r->top + 2 + rnd_int(h - 2)) * MAP_W
                    : (r->left + 2 + rnd_int(w - 2)) + (r->top + 1 + shift + i * 2) * MAP_W;
    // tombs: the item is generated with the floor's loot; slot 0x4000 marks the prize grave
    if (*s_ntombs < 8) s_tombs[(*s_ntombs)++] = (int16_t)(pos | (i == index ? 0x4000 : 0));
  }
}
static void paint_striped(Room *r) {
  fill(r->left + 1, r->top + 1, width(r) - 1, height(r) - 1, T_EMPTY_SP);
  if (width(r) > height(r)) { for (int i = r->left + 2; i < r->right; i += 2) fill(i, r->top + 1, 1, height(r) - 1, T_HIGH_GRASS); }
  else for (int i = r->top + 2; i < r->bottom; i += 2) fill(r->left + 1, i, width(r) - 1, 1, T_HIGH_GRASS);
}
static void paint_study(int ri) {
  Room *r = &R[ri];
  fill(r->left + 1, r->top + 1, width(r) - 1, height(r) - 1, T_BOOKSHELF);
  fill(r->left + 2, r->top + 2, width(r) - 3, height(r) - 3, T_EMPTY_SP);
  for (int i = 0; i < s_ne; i++) {
    if (s_e[i].a != ri && s_e[i].b != ri) continue;
    int dx = s_e[i].x, dy = s_e[i].y;
    if (dx == r->left) set(dx + 1, dy, T_EMPTY);
    else if (dx == r->right) set(dx - 1, dy, T_EMPTY);
    else if (dy == r->top) set(dx, dy + 1, T_EMPTY);
    else if (dy == r->bottom) set(dx, dy - 1, T_EMPTY);
  }
  int cx, cy; room_center(r, &cx, &cy);
  set(cx, cy, T_PEDESTAL);
}
static void paint_bridge(int ri) {
  Room *r = &R[ri];
  fill(r->left + 1, r->top + 1, width(r) - 1, height(r) - 1, !boss_here() && !boss_next() && rnd_int(3) == 0 ? T_CHASM : T_WATER);
  int d1x = -1, d1y = -1, d2x = -1, d2y = -1;
  for (int i = 0; i < s_ne; i++) {
    if (s_e[i].a != ri && s_e[i].b != ri) continue;
    if (d1x < 0) { d1x = s_e[i].x; d1y = s_e[i].y; } else { d2x = s_e[i].x; d2y = s_e[i].y; }
  }
  if ((d1x == r->left && d2x == r->right) || (d1x == r->right && d2x == r->left)) {
    int s = width(r) / 2, cx, cy;
    draw_inside(r, d1x, d1y, s, T_EMPTY_SP); draw_inside(r, d2x, d2y, s, T_EMPTY_SP);
    room_center(r, &cx, &cy);
    fill(cx, d1y < d2y ? d1y : d2y, 1, abs(d1y - d2y) + 1, T_EMPTY_SP);
  } else if ((d1y == r->top && d2y == r->bottom) || (d1y == r->bottom && d2y == r->top)) {
    int s = height(r) / 2, cx, cy;
    draw_inside(r, d1x, d1y, s, T_EMPTY_SP); draw_inside(r, d2x, d2y, s, T_EMPTY_SP);
    room_center(r, &cx, &cy);
    fill(d1x < d2x ? d1x : d2x, cy, abs(d1x - d2x) + 1, 1, T_EMPTY_SP);
  } else if (d1x == d2x) {
    fill(d1x == r->left ? r->left + 1 : r->right - 1, d1y < d2y ? d1y : d2y, 1, abs(d1y - d2y) + 1, T_EMPTY_SP);
  } else if (d1y == d2y) {
    fill(d1x < d2x ? d1x : d2x, d1y == r->top ? r->top + 1 : r->bottom - 1, abs(d1x - d2x) + 1, 1, T_EMPTY_SP);
  } else if (d1y == r->top || d1y == r->bottom) {
    draw_inside(r, d1x, d1y, abs(d1y - d2y), T_EMPTY_SP); draw_inside(r, d2x, d2y, abs(d1x - d2x), T_EMPTY_SP);
  } else if (d1x == r->left || d1x == r->right) {
    draw_inside(r, d1x, d1y, abs(d1x - d2x), T_EMPTY_SP); draw_inside(r, d2x, d2y, abs(d1y - d2y), T_EMPTY_SP);
  }
}
static void paint_fissure(Room *r) {
  fill(r->left + 1, r->top + 1, width(r) - 1, height(r) - 1, T_EMPTY);
  for (int i = r->top + 2; i < r->bottom - 1; i++)
    for (int j = r->left + 2; j < r->right - 1; j++) {
      int v = i - r->top < r->bottom - i ? i - r->top : r->bottom - i;
      int h = j - r->left < r->right - j ? j - r->left : r->right - j;
      if ((v < h ? v : h) > 2 || rnd_int(2) == 0) set(j, i, T_CHASM);
    }
}
static void paint_standard(int ri) {                  // StandardPainter, with PD's switch fall-through
  Room *r = &R[ri];
  fill_room(r, 0, T_WALL);
  for (int i = 0; i < s_ne; i++) if (s_e[i].a == ri || s_e[i].b == ri) door_set(&s_e[i], DOOR_REGULAR);
  int w = width(r), h = height(r), mn = w < h ? w : h, mx = w > h ? w : h;
  if (!boss_here() && rnd_int(5) == 0) {
    switch (rnd_int(6)) {
      case 0:
        if (G.feeling != FEEL_GRASS) {
          if (mn >= 4 && mx >= 6) { paint_graveyard(r); return; }
          break;
        } /* fall through */ // burned room
        __attribute__((fallthrough));
      case 1:
        if (s_depth > 1) { paint_burned(r); return; }
        break;
      case 2:
        if (mx >= 4) { paint_striped(r); return; }
        break;
      case 3:
        if (w >= 6 && h >= 6) { paint_study(ri); return; }
        break;
      case 4:
        if (G.feeling != FEEL_WATER) {
          if (n_connected(ri) == 2 && w >= 4 && h >= 4) { paint_bridge(ri); return; }
          break;
        } /* fall through */ // fissure
        __attribute__((fallthrough));
      case 5:
        if (!boss_here() && !boss_next() && mn >= 5) { paint_fissure(r); return; }
        break;
    }
  }
  fill_room(r, 1, T_EMPTY);
}
static void paint_tunnel(int ri) {                    // TunnelPainter
  Room *r = &R[ri];
  int floor = tunnel_tile(), cx, cy;
  room_center(r, &cx, &cy);
  if (width(r) > height(r) || (width(r) == height(r) && rnd_int(2) == 0)) {
    int from = r->right - 1, to = r->left + 1;
    for (int k = 0; k < s_ne; k++) {
      if (s_e[k].a != ri && s_e[k].b != ri) continue;
      int dx = s_e[k].x, dy = s_e[k].y, step = dy < cy ? 1 : -1;
      if (dx == r->left) { from = r->left + 1; for (int i = dy; i != cy; i += step) set(from, i, floor); }
      else if (dx == r->right) { to = r->right - 1; for (int i = dy; i != cy; i += step) set(to, i, floor); }
      else {
        if (dx < from) from = dx;
        if (dx > to) to = dx;
        for (int i = dy + step; i != cy; i += step) set(dx, i, floor);
      }
    }
    for (int i = from; i <= to; i++) set(i, cy, floor);
  } else {
    int from = r->bottom - 1, to = r->top + 1;
    for (int k = 0; k < s_ne; k++) {
      if (s_e[k].a != ri && s_e[k].b != ri) continue;
      int dx = s_e[k].x, dy = s_e[k].y, step = dx < cx ? 1 : -1;
      if (dy == r->top) { from = r->top + 1; for (int i = dx; i != cx; i += step) set(i, from, floor); }
      else if (dy == r->bottom) { to = r->bottom - 1; for (int i = dx; i != cx; i += step) set(i, to, floor); }
      else {
        if (dy < from) from = dy;
        if (dy > to) to = dy;
        for (int i = dx + step; i != cx; i += step) set(i, dy, floor);
      }
    }
    for (int i = from; i <= to; i++) set(cx, i, floor);
  }
  for (int k = 0; k < s_ne; k++) if (s_e[k].a == ri || s_e[k].b == ri) door_set(&s_e[k], DOOR_TUNNEL);
}

// ---------------- Special rooms (painters/*Painter.java): terrain here, their items and creatures in content.c ----------------
static Edge *entrance_of(int ri) { for (int k = 0; k < s_ne; k++) if (s_e[k].a == ri || s_e[k].b == ri) return &s_e[k]; return NULL; }
static int corner_opposite(Room *r, Edge *e) {         // Armory / Laboratory / Pit: a corner across from the door
  if (e->x == r->left) return (r->right - 1) + (rnd_int(2) == 0 ? r->top + 1 : r->bottom - 1) * MAP_W;
  if (e->x == r->right) return (r->left + 1) + (rnd_int(2) == 0 ? r->top + 1 : r->bottom - 1) * MAP_W;
  if (e->y == r->top) return (rnd_int(2) == 0 ? r->left + 1 : r->right - 1) + (r->bottom - 1) * MAP_W;
  return (rnd_int(2) == 0 ? r->left + 1 : r->right - 1) + (r->top + 1) * MAP_W;
}
static void paint_special(int ri) {
  Room *r = &R[ri];
  Edge *e = entrance_of(ri);
  if (!e) return;
  int ex = e->x, ey = e->y, cx, cy;
  R[ri].cell = -1;
  fill_room(r, 0, T_WALL);
  switch (r->type) {
    case RT_ARMORY: {
      fill_room(r, 1, T_EMPTY);
      int c = corner_opposite(r, e); G.map[c] = T_STATUE;
      door_set(e, DOOR_LOCKED);
      break;
    }
    case RT_CRYPT:
      fill_room(r, 1, T_EMPTY);
      room_center(r, &cx, &cy);
      door_set(e, DOOR_LOCKED);
      if (ex == r->left) { set(r->right - 1, r->top + 1, T_STATUE); set(r->right - 1, r->bottom - 1, T_STATUE); cx = r->right - 2; }
      else if (ex == r->right) { set(r->left + 1, r->top + 1, T_STATUE); set(r->left + 1, r->bottom - 1, T_STATUE); cx = r->left + 2; }
      else if (ey == r->top) { set(r->left + 1, r->bottom - 1, T_STATUE); set(r->right - 1, r->bottom - 1, T_STATUE); cy = r->bottom - 2; }
      else { set(r->left + 1, r->top + 1, T_STATUE); set(r->right - 1, r->top + 1, T_STATUE); cy = r->top + 2; }
      R[ri].cell = cx + cy * MAP_W;
      break;
    case RT_GARDEN:
      fill_room(r, 1, T_HIGH_GRASS); fill_room(r, 2, T_GRASS);
      door_set(e, DOOR_REGULAR);
      break;
    case RT_LABORATORY: {
      fill_room(r, 1, T_EMPTY_SP);
      int c = corner_opposite(r, e); G.map[c] = T_ALCHEMY;
      door_set(e, DOOR_LOCKED);
      break;
    }
    case RT_LIBRARY: {
      fill_room(r, 1, T_EMPTY);
      int ax = -1, ay = -1, bx = -1, by = -1;
      if (ex == r->left) { ax = bx = r->left + 1; ay = ey - 1; by = ey + 1; fill(r->right - 1, r->top + 1, 1, height(r) - 1, T_BOOKSHELF); }
      else if (ex == r->right) { ax = bx = r->right - 1; ay = ey - 1; by = ey + 1; fill(r->left + 1, r->top + 1, 1, height(r) - 1, T_BOOKSHELF); }
      else if (ey == r->top) { ax = ex + 1; bx = ex - 1; ay = by = r->top + 1; fill(r->left + 1, r->bottom - 1, width(r) - 1, 1, T_BOOKSHELF); }
      else { ax = ex + 1; bx = ex - 1; ay = by = r->bottom - 1; fill(r->left + 1, r->top + 1, width(r) - 1, 1, T_BOOKSHELF); }
      if (G.map[ax + ay * MAP_W] == T_EMPTY) set(ax, ay, T_STATUE);
      if (G.map[bx + by * MAP_W] == T_EMPTY) set(bx, by, T_STATUE);
      door_set(e, DOOR_LOCKED);
      break;
    }
    case RT_MAGIC_WELL:
      fill_room(r, 1, T_EMPTY);
      room_center(r, &cx, &cy);
      set(cx, cy, T_WELL);
      G.well_kind = rnd_int(3);                        // WATERS: awareness, health, transmutation
      G.well_pos = cx + cy * MAP_W;
      door_set(e, DOOR_REGULAR);
      break;
    case RT_PIT: {
      fill_room(r, 1, T_EMPTY);
      door_set(e, DOOR_LOCKED);
      int c = corner_opposite(r, e); G.map[c] = T_EMPTY_WELL;
      break;
    }
    case RT_POOL: {
      fill_room(r, 1, T_WATER);
      door_set(e, DOOR_REGULAR);
      int x, y;
      if (ex == r->left) { x = r->right - 1; y = r->top + height(r) / 2; }
      else if (ex == r->right) { x = r->left + 1; y = r->top + height(r) / 2; }
      else if (ey == r->top) { x = r->left + width(r) / 2; y = r->bottom - 1; }
      else { x = r->left + width(r) / 2; y = r->top + 1; }
      R[ri].cell = x + y * MAP_W;
      set(x, y, T_PEDESTAL);
      break;
    }
    case RT_STATUE:
      fill_room(r, 1, T_EMPTY);
      room_center(r, &cx, &cy);
      door_set(e, DOOR_LOCKED);
      if (ex == r->left) { fill(r->right - 1, r->top + 1, 1, height(r) - 1, T_STATUE); cx = r->right - 2; }
      else if (ex == r->right) { fill(r->left + 1, r->top + 1, 1, height(r) - 1, T_STATUE); cx = r->left + 2; }
      else if (ey == r->top) { fill(r->left + 1, r->bottom - 1, width(r) - 1, 1, T_STATUE); cy = r->bottom - 2; }
      else { fill(r->left + 1, r->top + 1, width(r) - 1, 1, T_STATUE); cy = r->top + 2; }
      R[ri].cell = cx + cy * MAP_W;
      break;
    case RT_STORAGE:
      fill_room(r, 1, T_EMPTY_SP);
      door_set(e, DOOR_BARRICADE);
      break;
    case RT_TRAPS: {
      static const uint8_t TRAPS[6] = { T_TOXIC_TRAP, T_TOXIC_TRAP, T_TOXIC_TRAP, T_PARALYTIC_TRAP, T_PARALYTIC_TRAP, T_CHASM };
      int t = TRAPS[rnd_int(6)];
      if (t == T_CHASM && boss_next()) t = T_SUMMONING_TRAP;
      fill_room(r, 1, t);
      door_set(e, DOOR_REGULAR);
      int last = G.map[r->left + 1 + (r->top + 1) * MAP_W] == T_CHASM ? T_CHASM : T_EMPTY, x, y;
      if (ex == r->left) { x = r->right - 1; y = r->top + height(r) / 2; fill(x, r->top + 1, 1, height(r) - 1, last); }
      else if (ex == r->right) { x = r->left + 1; y = r->top + height(r) / 2; fill(x, r->top + 1, 1, height(r) - 1, last); }
      else if (ey == r->top) { x = r->left + width(r) / 2; y = r->bottom - 1; fill(r->left + 1, y, width(r) - 1, 1, last); }
      else { x = r->left + width(r) / 2; y = r->top + 1; fill(r->left + 1, y, width(r) - 1, 1, last); }
      int pos = x + y * MAP_W;
      if (rnd_int(3) == 0) { if (last == T_CHASM) G.map[pos] = T_EMPTY; R[ri].cell = pos | 0x4000; }   // a chest
      else { G.map[pos] = T_PEDESTAL; R[ri].cell = pos; }
      break;
    }
    case RT_TREASURY:
      fill_room(r, 1, T_EMPTY);
      room_center(r, &cx, &cy); set(cx, cy, T_STATUE);
      door_set(e, DOOR_LOCKED);
      break;
    case RT_VAULT: {
      fill_room(r, 1, T_EMPTY_SP); fill_room(r, 2, T_EMPTY);
      int c = (r->left + r->right) / 2 + (r->top + r->bottom) / 2 * MAP_W, k = rnd_int(3);
      if (k == 2) G.map[c] = T_PEDESTAL;
      R[ri].cell = c | k << 12;
      door_set(e, DOOR_LOCKED);
      break;
    }
    case RT_WEAK_FLOOR:
      fill_room(r, 1, T_CHASM);
      door_set(e, DOOR_REGULAR);
      if (ex == r->left) for (int i = r->top + 1; i < r->bottom; i++) draw_inside(r, r->left, i, rnd_range(1, width(r) - 2), T_EMPTY_SP);
      else if (ex == r->right) for (int i = r->top + 1; i < r->bottom; i++) draw_inside(r, r->right, i, rnd_range(1, width(r) - 2), T_EMPTY_SP);
      else if (ey == r->top) for (int i = r->left + 1; i < r->right; i++) draw_inside(r, i, r->top, rnd_range(1, height(r) - 2), T_EMPTY_SP);
      else for (int i = r->left + 1; i < r->right; i++) draw_inside(r, i, r->bottom, rnd_range(1, height(r) - 2), T_EMPTY_SP);
      break;
    case RT_ALTAR: {
      fill_room(r, 1, boss_next() ? T_HIGH_GRASS : T_CHASM);
      room_center(r, &cx, &cy);
      if (ex == r->left || ex == r->right) {
        int n = abs(ex - cx) - 2, sx = ex == r->left ? 1 : -1, px = ex + sx * ((n > 0 ? n : 0) + 1), py = ey;   // drawInside returns the cell after
        draw_inside(r, ex, ey, n, T_EMPTY_SP);
        for (; py != cy; py += py < cy ? 1 : -1) set(px, py, T_EMPTY_SP);
      } else {
        int n = abs(ey - cy) - 2, sy = ey == r->top ? 1 : -1, px = ex, py = ey + sy * ((n > 0 ? n : 0) + 1);
        draw_inside(r, ex, ey, n, T_EMPTY_SP);
        for (; px != cx; px += px < cx ? 1 : -1) set(px, py, T_EMPTY_SP);
      }
      fill(cx - 1, cy - 1, 3, 3, T_EMBERS);
      set(cx, cy, T_PEDESTAL);
      G.altar_pos = cx + cy * MAP_W;
      break;                                           // (door.set(EMPTY) never lowers a door: the neighbour's painter decides it)
    }
    case RT_BLACKSMITH:                                // BlacksmithPainter (items and the troll: content.c)
      fill_room(r, 1, T_FIRE_TRAP); fill_room(r, 2, T_EMPTY_SP);
      for (int k = 0; k < s_ne; k++) if (s_e[k].a == ri || s_e[k].b == ri) { door_set(&s_e[k], DOOR_UNLOCKED); draw_inside(r, s_e[k].x, s_e[k].y, 1, T_EMPTY); }
      break;
    case RT_SHOP: {                                    // ShopPainter (the wares and the keeper: content.c)
      fill_room(r, 1, T_EMPTY_SP);
      for (int k = 0; k < s_ne; k++) if (s_e[k].a == ri || s_e[k].b == ri) door_set(&s_e[k], DOOR_REGULAR);
      R[ri].cell = ex + ey * MAP_W;               // the wares are laid out from the door round the walls
      break;
    }
  }
}

// ---------------- RegularLevel ----------------
#define MIN_ROOM (s_region == 4 ? 6 : 7)          // RegularLevel.minRoomSize (HallsLevel: 6)
#define MAX_ROOM 9
static void split(int l, int t, int r, int b) {
  int w = r - l, h = b - t;
  if (w > MAX_ROOM && h < MIN_ROOM) {
    int vw = rnd_int2(l + 3, r - 3); split(l, t, vw, b); split(vw, t, r, b);
  } else if (h > MAX_ROOM && w < MIN_ROOM) {
    int vh = rnd_int2(t + 3, b - 3); split(l, t, r, vh); split(l, vh, r, b);
  } else if ((w * h <= MIN_ROOM * MIN_ROOM && w <= MAX_ROOM && h <= MAX_ROOM) || w < MIN_ROOM || h < MIN_ROOM) {   // PD: Math.random() <= min * min / square(), an int division: 1 or 0
    if (G.n_rooms < MAX_ROOMS) G.rooms[G.n_rooms++] = (Room){ l, t, r, b, RT_NULL, -1 };
  } else if (rnd_int(w + h - 4) < w - 2) {
    int vw = rnd_int2(l + 3, r - 3); split(l, t, vw, b); split(vw, t, r, b);
  } else {
    int vh = rnd_int2(t + 3, b - 3); split(l, t, r, vh); split(l, vh, r, b);
  }
}

static bool init_rooms(void) {
  G.n_rooms = 0;
  split(0, 0, MAP_W - 1, MAP_H - 1);
  if (G.n_rooms < 8) return false;
  for (int i = G.n_rooms - 1; i > 0; i--) { int j = rnd_int(i + 1); Room t = R[i]; R[i] = R[j]; R[j] = t; }   // PD's rooms are a HashSet: every walk over them is in a random order
  for (int i = 0; i < G.n_rooms; i++) { s_nb[i] = 0; s_price[i] = 1; }
  for (int i = 0; i < G.n_rooms - 1; i++)
    for (int j = i + 1; j < G.n_rooms; j++) {               // Room.addNeigbour
      Rect x = intersect(&R[i], &R[j]);
      if ((x.r - x.l == 0 && x.b - x.t >= 3) || (x.b - x.t == 0 && x.r - x.l >= 3)) {
        s_nb[i] |= (RMask)1 << j; s_nb[j] |= (RMask)1 << i;
      }
    }
  return true;
}

static int random_room_of(int type, int tries) {       // RegularLevel.randomRoom
  for (int i = 0; i < tries; i++) { int r = rnd_int(G.n_rooms); if (R[r].type == type) return r; }
  return -1;
}
static const uint8_t SPECIALS[14] = { RT_ARMORY, RT_WEAK_FLOOR, RT_MAGIC_WELL, RT_CRYPT, RT_POOL, RT_GARDEN, RT_LIBRARY,
                                     RT_TREASURY, RT_TRAPS, RT_STORAGE, RT_STATUE, RT_LABORATORY, RT_VAULT, RT_ALTAR };   // Room.SPECIALS
static uint8_t s_spec[14]; static int s_nspec;
static int8_t s_smith;                               // a blacksmith room was made: the quest's variant (-1 none)
static bool is_special(int t) { for (int i = 0; i < 14; i++) if (SPECIALS[i] == t) return true; return false; }   // Room.SPECIALS.contains (not the shop)
static void spec_remove(int t) { for (int i = 0; i < s_nspec; i++) if (s_spec[i] == t) { memmove(s_spec + i, s_spec + i + 1, s_nspec - i - 1); s_nspec--; return; } }
static bool spec_has(int t) { for (int i = 0; i < s_nspec; i++) if (s_spec[i] == t) return true; return false; }
// Changes from PD: each floor shuffles the special rooms on its own (Room.shuffleTypes / useType carry the order from
// floor to floor in PD); a floor must come out the same whenever it is rebuilt from its seed.
static void assign_room_types(void) {                  // RegularLevel.assignRoomType
  memcpy(s_spec, SPECIALS, 14); s_nspec = 14;
  for (int i = 0; i < 13; i++) { int j = rnd_int2(i, 14); uint8_t t = s_spec[i]; s_spec[i] = s_spec[j]; s_spec[j] = t; }
  if (boss_next()) spec_remove(RT_WEAK_FLOOR);
  bool pit_needed = s_depth > 1 && (G.weak_floors >> (s_depth - 1) & 1);
  int special_rooms = 0;
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type != RT_NULL || n_connected(r) != 1) continue;
    if (s_nspec > 0 && width(&R[r]) > 3 && height(&R[r]) > 3 && rnd_int(special_rooms * special_rooms + 2) == 0) {
      if (pit_needed) {
        R[r].type = RT_PIT; pit_needed = false;
        static const uint8_t NOPIT[8] = { RT_ARMORY, RT_CRYPT, RT_LABORATORY, RT_LIBRARY, RT_STATUE, RT_TREASURY, RT_VAULT, RT_WEAK_FLOOR };
        for (int i = 0; i < 8; i++) spec_remove(NOPIT[i]);
      } else if (s_depth % 5 == 2 && spec_has(RT_LABORATORY)) R[r].type = RT_LABORATORY;
      else {
        int n = s_nspec, a = rnd_int(n), b = rnd_int(n);
        R[r].type = s_spec[a < b ? a : b];
      }
      if (R[r].type == RT_WEAK_FLOOR) G.weak_floors |= 1u << s_depth;
      spec_remove(R[r].type);
      special_rooms++;
    } else if (rnd_int(2) == 0) {
      RMask nb = 0;
      for (int n = 0; n < G.n_rooms; n++)
        if (((s_nb[r] >> n) & 1) && edge_find(r, n) < 0 && !is_special(R[n].type) && R[n].type != RT_PIT) nb |= (RMask)1 << n;
      if (popcount(nb) > 1) connect(r, mask_pick(nb));
    }
  }
  int count = 0;
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type != RT_NULL) continue;
    int c = n_connected(r);
    if (c == 0) continue;
    if (rnd_int(c * c) == 0) { R[r].type = RT_STANDARD; count++; }
    else R[r].type = RT_TUNNEL;
  }
  for (int guard = 0; count < 4 && guard < 2000; guard++) {
    int r = random_room_of(RT_TUNNEL, 1);
    if (r >= 0) { R[r].type = RT_STANDARD; count++; }
  }
  if (s_region == 1 || s_region == 3)                  // PrisonLevel / CityLevel.assignRoomType: tunnels become passages
    for (int r = 0; r < G.n_rooms; r++) if (R[r].type == RT_TUNNEL) R[r].type = RT_PASSAGE;
  s_smith = -1;
  if (s_region == 2 && s_depth > 11) {                 // CavesLevel: Blacksmith.Quest.spawn (committed to G.q once the floor is built)
    int roll = rnd_int(15 - s_depth);
    if ((G.q.smith == QS_NONE || G.q.smith_depth == s_depth) && roll == 0)
      for (int r = 0; r < G.n_rooms; r++)
        if (R[r].type == RT_STANDARD && width(&R[r]) > 4 && height(&R[r]) > 4) { R[r].type = RT_BLACKSMITH; s_smith = rnd_int(2); break; }
  }
}
// PassagePainter: a corridor running round the inside of the room, between the doors furthest apart
static int pas_w, pas_h;
static int xy2p(const Room *r, int x, int y) {
  if (y == r->top) return x - r->left - 1;
  if (x == r->right) return y - r->top - 1 + pas_w;
  if (y == r->bottom) return r->right - x - 1 + pas_w + pas_h;
  if (y == r->top + 1) return 0;
  return r->bottom - y - 1 + pas_w * 2 + pas_h;
}
static void p2xy(const Room *r, int p, int *x, int *y) {
  if (p < pas_w) { *x = r->left + 1 + p; *y = r->top + 1; }
  else if (p < pas_w + pas_h) { *x = r->right - 1; *y = r->top + 1 + (p - pas_w); }
  else if (p < pas_w * 2 + pas_h) { *x = r->right - 1 - (p - (pas_w + pas_h)); *y = r->bottom - 1; }
  else { *x = r->left + 1; *y = r->bottom - 1 - (p - (pas_w * 2 + pas_h)); }
}
static void paint_passage(int ri) {
  Room *r = &R[ri];
  pas_w = width(r) - 2; pas_h = height(r) - 2;
  int floor = tunnel_tile();
  int16_t j[16]; int nj = 0;
  for (int k = 0; k < s_ne && nj < 16; k++) if (s_e[k].a == ri || s_e[k].b == ri) j[nj++] = xy2p(r, s_e[k].x, s_e[k].y);
  if (!nj) return;
  for (int a = 1; a < nj; a++) for (int b = a; b > 0 && j[b] < j[b - 1]; b--) { int16_t t = j[b]; j[b] = j[b - 1]; j[b - 1] = t; }
  int perim = pas_w * 2 + pas_h * 2, start = 0, maxd = j[0] + perim - j[nj - 1];
  for (int i = 1; i < nj; i++) { int d = j[i] - j[i - 1]; if (d > maxd) { maxd = d; start = i; } }
  int end = (start + nj - 1) % nj, p = j[start], x, y;
  for (int guard = 0; guard < 200; guard++) { p2xy(r, p, &x, &y); set(x, y, floor); if (p == j[end]) break; p = (p + 1) % perim; }
  for (int k = 0; k < s_ne; k++) if (s_e[k].a == ri || s_e[k].b == ri) door_set(&s_e[k], DOOR_TUNNEL);
}

static void place_doors(int ri) {
  for (int k = 0; k < s_ne; k++) {
    Edge *e = &s_e[k];
    if ((e->a != ri && e->b != ri) || e->x >= 0) continue;
    Rect i = intersect(&R[e->a], &R[e->b]);
    if (i.r - i.l == 0) { e->x = i.l; e->y = rnd_int2(i.t + 1, i.b); }
    else { e->x = rnd_int2(i.l + 1, i.r); e->y = i.t; }
  }
}

static bool join_rooms(int a, int b) {
  Room *r = &R[a], *n = &R[b];
  if (r->type != RT_STANDARD || n->type != RT_STANDARD) return false;
  Rect w = intersect(r, n);
  if (w.l == w.r) {
    if (w.b - w.t < 3) return false;
    int mh = height(r) > height(n) ? height(r) : height(n);
    if (w.b - w.t == mh) return false;
    if (width(r) + width(n) > MAX_ROOM) return false;
    w.t += 1; w.r++;
    fill(w.l, w.t, 1, w.b - w.t, T_EMPTY);
  } else {
    if (w.r - w.l < 3) return false;
    int mw = width(r) > width(n) ? width(r) : width(n);
    if (w.r - w.l == mw) return false;
    if (height(r) + height(n) > MAX_ROOM) return false;
    w.l += 1; w.b++;
    fill(w.l, w.t, w.r - w.l, 1, T_EMPTY);
  }
  return true;
}

static void paint_doors(int ri) {
  for (int k = 0; k < s_ne; k++) {
    Edge *e = &s_e[k];
    if (e->a != ri && e->b != ri) continue;
    if (join_rooms(ri, other(e, ri))) continue;
    int door = e->x + e->y * MAP_W;
    if (e->x < 0) continue;
    switch (e->type) {
      case DOOR_EMPTY: G.map[door] = T_EMPTY; break;
      case DOOR_TUNNEL: G.map[door] = tunnel_tile(); break;
      case DOOR_REGULAR:
        if (s_depth <= 1) G.map[door] = T_DOOR;
        else {
          bool secret = (s_depth < 6 ? rnd_int(12 - s_depth) : rnd_int(6)) == 0;
          G.map[door] = secret ? T_SECRET_DOOR : T_DOOR;
          if (secret) G.secret_doors++;
        }
        break;
      case DOOR_UNLOCKED: G.map[door] = T_DOOR; break;
      case DOOR_HIDDEN: G.map[door] = T_SECRET_DOOR; G.secret_doors++; break;
      case DOOR_BARRICADE: G.map[door] = rnd_int(3) == 0 ? T_BOOKSHELF : T_BARRICADE; break;
      case DOOR_LOCKED: G.map[door] = T_LOCKED_DOOR; break;
    }
  }
}

// Patch.generate: cellular automaton blobs (bit arrays instead of booleans)
static uint8_t *patch(int pct, int gens) {
  uint8_t *off = s_pa, *cur = s_pb;
  memset(off, 0, MAP_LEN / 8);
  for (int i = 0; i < MAP_LEN; i++) if (rnd_pct(pct)) BIT_SET(off, i);
  memcpy(cur, off, MAP_LEN / 8);                       // PD never writes the edges, and its two buffers are static: both edges hold draws
  for (int g = 0; g < gens; g++) {
    for (int y = 1; y < MAP_H - 1; y++)
      for (int x = 1; x < MAP_W - 1; x++) {
        int p = x + y * MAP_W, c = 0;
        c += BIT_GET(off, p - MAP_W - 1) + BIT_GET(off, p - MAP_W) + BIT_GET(off, p - MAP_W + 1);
        c += BIT_GET(off, p - 1) + BIT_GET(off, p + 1);
        c += BIT_GET(off, p + MAP_W - 1) + BIT_GET(off, p + MAP_W) + BIT_GET(off, p + MAP_W + 1);
        if ((!BIT_GET(off, p) && c >= 5) || (BIT_GET(off, p) && c >= 4)) BIT_SET(cur, p); else BIT_CLR(cur, p);
      }
    uint8_t *t = cur; cur = off; off = t;
  }
  return off;
}

static const int16_t N8[8] = { -MAP_W - 1, -MAP_W, -MAP_W + 1, -1, 1, MAP_W - 1, MAP_W, MAP_W + 1 };

static void paint_water(void) {                       // <Region>Level.water(): Patch.generate(odds, generations)
  static const uint8_t W[5][3] = { { 45, 60, 5 }, { 45, 65, 4 }, { 45, 60, 6 }, { 45, 65, 4 }, { 40, 55, 6 } };
  const uint8_t *w = W[s_region];
  uint8_t *lake = patch(s_depth == 5 ? 50 : s_depth == 21 ? 35 : G.feeling == FEEL_WATER ? w[1] : w[0], w[2]);   // SewerBossLevel.water: 0.5; LastShopLevel: 0.35, 4
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && BIT_GET(lake, i)) G.map[i] = T_WATER;
}
static void paint_grass(void) {
  static const uint8_t GR[5][3] = { { 40, 60, 4 }, { 40, 60, 3 }, { 35, 55, 3 }, { 40, 60, 3 }, { 30, 55, 3 } };
  const uint8_t *gp = GR[s_region];
  uint8_t *grass = patch(s_depth == 21 ? 30 : G.feeling == FEEL_GRASS ? gp[1] : gp[0], gp[2]);   // LastShopLevel: 0.30, 3
  if (G.feeling == FEEL_GRASS)
    for (int r = 0; r < G.n_rooms; r++) {
      Room *m = &R[r];
      if (m->type == RT_NULL || m->type == RT_PASSAGE || m->type == RT_TUNNEL) continue;
      BIT_SET(grass, (m->left + 1) + (m->top + 1) * MAP_W); BIT_SET(grass, (m->right - 1) + (m->top + 1) * MAP_W);
      BIT_SET(grass, (m->left + 1) + (m->bottom - 1) * MAP_W); BIT_SET(grass, (m->right - 1) + (m->bottom - 1) * MAP_W);
    }
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (G.map[i] != T_EMPTY || !BIT_GET(grass, i)) continue;
    int count = 1;
    for (int k = 0; k < 8; k++) count += BIT_GET(grass, i + N8[k]);
    G.map[i] = rnd_int(12) < count ? T_HIGH_GRASS : T_GRASS;
  }
}

static void place_traps(void) {
  int n = s_depth <= 1 ? 0 : rnd_int2(1, G.n_rooms + s_depth);
  // RegularLevel.trapChances: all equal; gas traps (toxic, fire, paralytic) wait for gases
  static const uint16_t chances[8] = { GAS_TRAPS, GAS_TRAPS, GAS_TRAPS, 1, 1, 1, 1, 1 };
  static const uint8_t kinds[8] = { T_SECRET_TOXIC_TRAP, T_SECRET_FIRE_TRAP, T_SECRET_PARALYTIC_TRAP, T_SECRET_POISON_TRAP,
                                    T_SECRET_ALARM_TRAP, T_SECRET_LIGHTNING_TRAP, T_SECRET_GRIPPING_TRAP, T_SECRET_SUMMONING_TRAP };
  for (int i = 0; i < n; i++) {
    int p = rnd_int(MAP_LEN);
    if (G.map[p] == T_EMPTY) G.map[p] = kinds[rnd_chances(chances, 8)];
  }
}

// boss floors that are no RegularLevel: Level.randomDestination is any passable cell (one room spanning the map)
static void whole_map(int cell) { G.n_rooms = 1; G.rooms[0] = (Room){ 0, 0, MAP_W - 1, MAP_H - 1, RT_NULL, (int16_t)cell }; }
// PrisonBossLevel / LastShopLevel.randomRespawnCell: -1 (no RT_STANDARD room left for random_respawn_cell to pick)
static void no_respawn(void) { for (int r = 0; r < G.n_rooms; r++) if (R[r].type == RT_STANDARD) R[r].type = RT_TUNNEL; }

// ---------------- SewerBossLevel (depth 5): Goo's room holds the locked exit; the rat king hides beside it ----------------
static void paint_boss_exit(int ri) {                 // BossExitPainter
  Room *r = &R[ri];
  fill_room(r, 0, T_WALL); fill_room(r, 1, T_EMPTY);
  for (int i = 0; i < s_ne; i++) if (s_e[i].a == ri || s_e[i].b == ri) door_set(&s_e[i], DOOR_REGULAR);
  G.exit = r->top * MAP_W + (r->left + r->right) / 2;
  G.map[G.exit] = T_LOCKED_EXIT;
}
static void paint_rat_king(int ri) {                  // RatKingPainter (chests and the king: level_content)
  Room *r = &R[ri];
  fill_room(r, 0, T_WALL); fill_room(r, 1, T_EMPTY_SP);
  for (int i = 0; i < s_ne; i++) if (s_e[i].a == ri || s_e[i].b == ri) s_e[i].type = DOOR_HIDDEN;
}
static bool build_sewer_boss(void) {
  int distance, retry = 0, min_distance = 0;
  while ((min_distance + 1) * (min_distance + 1) <= G.n_rooms) min_distance++;
  do {
    int guard = 0;
    do { if (guard++ > 10) return false; s_room_entrance = rnd_int(G.n_rooms); } while (width(&R[s_room_entrance]) < 4 || height(&R[s_room_entrance]) < 4);
    guard = 0;
    do { if (guard++ > 10) return false; s_room_exit = rnd_int(G.n_rooms); }
    while (s_room_exit == s_room_entrance || width(&R[s_room_exit]) < 6 || height(&R[s_room_exit]) < 6 || R[s_room_exit].top == 0);
    build_distance_map(s_room_exit);
    distance = s_dist[s_room_entrance];
    if (retry++ > 10) return false;
  } while (distance < min_distance);
  R[s_room_entrance].type = RT_ENTRANCE;
  R[s_room_exit].type = RT_BOSS_EXIT;
  uint8_t path[MAX_ROOMS];
  build_distance_map(s_room_exit);
  int n = build_path(s_room_entrance, s_room_exit, path);
  if (n < 0) return false;
  for (int i = 0; i < n; i++) s_price[path[i]] = s_dist[s_room_entrance];      // Graph.setPrice
  build_distance_map(s_room_exit);
  n = build_path(s_room_entrance, s_room_exit, path);
  if (n < 0) return false;
  int room = s_room_entrance;
  for (int i = 0; i < n; i++) { connect(room, path[i]); room = path[i]; }
  for (int k = 0; k < s_ne; k++) {                    // the way into the boss room must not come down from above
    if (s_e[k].a != s_room_exit && s_e[k].b != s_room_exit) continue;
    if (R[s_room_exit].top == R[other(&s_e[k], s_room_exit)].bottom) return false;
    break;
  }
  for (int r = 0; r < G.n_rooms; r++) if (R[r].type == RT_NULL && n_connected(r) > 0) R[r].type = RT_TUNNEL;
  RMask cand = 0; Room *ex = &R[s_room_exit];
  for (int r = 0; r < G.n_rooms; r++)
    if (((s_nb[s_room_exit] >> r) & 1) && edge_find(s_room_exit, r) < 0 && (ex->left == R[r].right || ex->right == R[r].left || ex->bottom == R[r].top))
      cand |= (RMask)1 << r;
  if (cand) { int k = mask_pick(cand); connect(k, s_room_exit); R[k].type = RT_RAT_KING; }
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type == RT_NULL) continue;
    place_doors(r);
    switch (R[r].type) {
      case RT_ENTRANCE: paint_stairs(r, T_ENTRANCE); break;
      case RT_BOSS_EXIT: paint_boss_exit(r); break;
      case RT_RAT_KING: paint_rat_king(r); break;
      case RT_TUNNEL: paint_tunnel(r); break;
    }
  }
  for (int r = 0; r < G.n_rooms; r++) paint_doors(r);
  paint_water();
  paint_grass();
  place_traps();
  return true;
}

// ---------------- PrisonBossLevel (depth 10): Tengu's arena behind a locked door, its key in the anteroom ----------------
static bool build_prison_boss(void) {
  uint8_t path[MAX_ROOMS];
  int n, retry = 0;
  do {
    if (retry++ > 10) return false;
    int guard = 0;
    do { if (guard++ > 10) return false; s_room_entrance = rnd_int(G.n_rooms); } while (width(&R[s_room_entrance]) < 4 || height(&R[s_room_entrance]) < 4);
    guard = 0;
    do { if (guard++ > 10) return false; s_room_exit = rnd_int(G.n_rooms); }
    while (s_room_exit == s_room_entrance || width(&R[s_room_exit]) < 7 || height(&R[s_room_exit]) < 7 || R[s_room_exit].top == 0);
    build_distance_map(s_room_exit);
    n = build_path(s_room_entrance, s_room_exit, path);
  } while (n < 3);
  R[s_room_entrance].type = RT_ENTRANCE;
  R[s_room_exit].type = RT_BOSS_EXIT;
  for (int i = 0; i < n; i++) s_price[path[i]] = s_dist[s_room_entrance];      // Graph.setPrice
  build_distance_map(s_room_exit);
  n = build_path(s_room_entrance, s_room_exit, path);
  if (n < 2) return false;
  R[path[n - 2]].type = RT_STANDARD;                   // the anteroom (its chest holds the arena's key)
  int room = s_room_entrance;
  for (int i = 0; i < n; i++) { connect(room, path[i]); room = path[i]; }
  for (int r = 0; r < G.n_rooms; r++) if (R[r].type == RT_NULL && n_connected(r) > 0) R[r].type = RT_PASSAGE;
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type == RT_NULL) continue;
    place_doors(r);
    switch (R[r].type) {
      case RT_ENTRANCE: paint_stairs(r, T_ENTRANCE); break;
      case RT_BOSS_EXIT: paint_boss_exit(r); break;
      case RT_STANDARD: paint_standard(r); break;
      case RT_PASSAGE: paint_passage(r); break;
    }
  }
  no_respawn();                                        // the anteroom: RT_TUNNEL from now on (its key: level_content)
  for (int k = 0; k < s_ne; k++) {                     // PrisonBossLevel.paintDoors: plain doors, open between passages
    Edge *e = &s_e[k];
    if (e->x < 0) continue;
    G.map[e->x + e->y * MAP_W] = R[e->a].type == RT_PASSAGE && R[e->b].type == RT_PASSAGE ? T_EMPTY : T_DOOR;
  }
  for (int k = 0; k < s_ne; k++)                       // the way into the arena must not come down from above
    if ((s_e[k].a == s_room_exit || s_e[k].b == s_room_exit) && s_e[k].y == R[s_room_exit].top) return false;
  uint8_t *lake = patch(45, 5);
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && BIT_GET(lake, i)) G.map[i] = T_WATER;
  uint8_t *grass = patch(30, 4);
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (G.map[i] != T_EMPTY || !BIT_GET(grass, i)) continue;
    int count = 1;
    for (int k = 0; k < 8; k++) count += BIT_GET(grass, i + N8[k]);
    G.map[i] = rnd_int(12) < count ? T_HIGH_GRASS : T_GRASS;
  }
  int nt = rnd_int2(1, G.n_rooms + s_depth);           // placeTraps: poison only
  for (int i = 0; i < nt; i++) { int p = rnd_int(MAP_LEN); if (G.map[p] == T_EMPTY) G.map[p] = T_POISON_TRAP; }
  return true;
}
static void place_sign(void);
static void decorate_prison_boss(void) {             // PrisonBossLevel.decorate
  uint8_t *m = G.map;
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (m[i] != T_EMPTY) continue;
    int c = 15;
    if (m[i + 1] == T_WALL && m[i + MAP_W] == T_WALL) c += 20;
    if (m[i - 1] == T_WALL && m[i + MAP_W] == T_WALL) c += 20;
    if (m[i + 1] == T_WALL && m[i - MAP_W] == T_WALL) c += 20;
    if (m[i - 1] == T_WALL && m[i - MAP_W] == T_WALL) c += 20;
    if (rnd_int(100) < c) m[i] = T_EMPTY_DECO;
  }
  for (int i = 0; i < MAP_W; i++)
    if (m[i] == T_WALL && (m[i + MAP_W] == T_EMPTY || m[i + MAP_W] == T_EMPTY_SP) && rnd_int(4) == 0) m[i] = T_WALL_DECO;
  for (int i = MAP_W; i < MAP_LEN - MAP_W; i++)
    if (m[i] == T_WALL && m[i - MAP_W] == T_WALL && (m[i + MAP_W] == T_EMPTY || m[i + MAP_W] == T_EMPTY_SP) && rnd_int(2) == 0) m[i] = T_WALL_DECO;
  place_sign();
  for (int k = 0; k < s_ne; k++)                       // the arena door, locked
    if (s_e[k].a == s_room_exit || s_e[k].b == s_room_exit) { m[s_e[k].x + s_e[k].y * MAP_W] = T_LOCKED_DOOR; break; }
  Room *ex = &R[s_room_exit];
  fill(ex->left + 2, ex->top + 2, width(ex) - 3, height(ex) - 3, T_INACTIVE_TRAP);
}

// ---------------- CavesBossLevel (depth 15): open caverns round a walled entrance room; DM-300 waits outside ----------------
#define CB_L (MAP_W / 2 - 2)
#define CB_R (MAP_W / 2 + 2)
#define CB_T (MAP_H / 2 - 2)
#define CB_B (MAP_H / 2 + 2)
static bool build_caves_boss(void) {
  int top_most = 99;
  for (int i = 0; i < 8; i++) {
    int l, r, t, b;
    if (rnd_int(2) == 0) { l = rnd_int2(1, CB_L - 3); r = CB_R + 3; } else { l = CB_L - 3; r = rnd_int2(CB_R + 3, MAP_W - 1); }
    if (rnd_int(2) == 0) { t = rnd_int2(2, CB_T - 3); b = CB_B + 3; } else { t = CB_L - 3; b = rnd_int2(CB_T + 3, MAP_H - 1); }
    fill(l, t, r - l + 1, b - t + 1, T_EMPTY);
    if (t < top_most) { top_most = t; G.exit = rnd_int2(l, r) + (t - 1) * MAP_W; }
  }
  G.map[G.exit] = T_LOCKED_EXIT;
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && rnd_int(6) == 0) G.map[i] = T_INACTIVE_TRAP;
  fill(CB_L - 1, CB_T - 1, CB_R - CB_L + 3, CB_B - CB_T + 3, T_WALL);
  fill(CB_L, CB_T + 1, CB_R - CB_L + 1, CB_B - CB_T, T_EMPTY);
  fill(CB_L, CB_T, CB_R - CB_L + 1, 1, T_TOXIC_TRAP);
  int door = rnd_int2(CB_L, CB_R) + (CB_B + 1) * MAP_W;
  G.map[door] = T_DOOR;
  G.entrance = rnd_int2(CB_L + 1, CB_R - 1) + rnd_int2(CB_T + 1, CB_B - 1) * MAP_W;
  G.map[G.entrance] = T_ENTRANCE;
  uint8_t *lake = patch(45, 6);
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && BIT_GET(lake, i)) G.map[i] = T_WATER;
  whole_map(door);                                     // the arena door, for the press hook
  return true;
}
static void decorate_caves_boss(void) {
  uint8_t *m = G.map;
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W; i++) {
    if (m[i] != T_EMPTY) continue;
    int n = (m[i + 1] == T_WALL) + (m[i - 1] == T_WALL) + (m[i + MAP_W] == T_WALL) + (m[i - MAP_W] == T_WALL);
    if (rnd_int(8) <= n) m[i] = T_EMPTY_DECO;
  }
  for (int i = 0; i < MAP_LEN; i++) if (m[i] == T_WALL && rnd_int(8) == 0) m[i] = T_WALL_DECO;
  for (int g = 0; g < 1000; g++) { int c = rnd_int2(CB_L, CB_R) + rnd_int2(CB_T, CB_B) * MAP_W; if (c != G.entrance) { m[c] = T_SIGN; break; } }
}

// ---------------- CityBossLevel (depth 20): the King's hall; his pedestals left and right ----------------
#define CTB_TOP 2
#define CTB_HW 7
#define CTB_HH 15
#define CTB_CH 3
#define CTB_LEFT ((MAP_W - CTB_HW) / 2)
#define CTB_CENTER (CTB_LEFT + CTB_HW / 2)
static bool build_city_boss(void) {
  fill(CTB_LEFT, CTB_TOP, CTB_HW, CTB_HH, T_EMPTY);
  fill(CTB_CENTER, CTB_TOP, 1, CTB_HH, T_EMPTY_SP);
  for (int y = CTB_TOP + 1; y < CTB_TOP + CTB_HH; y += 2) { G.map[y * MAP_W + CTB_CENTER - 2] = T_STATUE_SP; G.map[y * MAP_W + CTB_CENTER + 2] = T_STATUE_SP; }
  int left = (CTB_TOP + CTB_HH / 2) * MAP_W + CTB_CENTER - 2, right = left + 4;
  G.map[left] = G.map[right] = T_PEDESTAL;
  for (int i = left + 1; i < right; i++) G.map[i] = T_EMPTY_SP;
  G.exit = (CTB_TOP - 1) * MAP_W + CTB_CENTER;
  G.map[G.exit] = T_LOCKED_EXIT;
  int door = (CTB_TOP + CTB_HH) * MAP_W + CTB_CENTER;
  G.map[door] = T_DOOR;
  fill(CTB_LEFT, CTB_TOP + CTB_HH + 1, CTB_HW, CTB_CH, T_EMPTY);
  fill(CTB_LEFT, CTB_TOP + CTB_HH + 1, 1, CTB_CH, T_BOOKSHELF);
  fill(CTB_LEFT + CTB_HW - 1, CTB_TOP + CTB_HH + 1, 1, CTB_CH, T_BOOKSHELF);
  G.entrance = (CTB_TOP + CTB_HH + 2 + rnd_int(CTB_CH - 1)) * MAP_W + CTB_LEFT + rnd_int(CTB_HW - 2);
  G.map[G.entrance] = T_ENTRANCE;
  whole_map(-1);
  return true;
}
static void decorate_city_boss(void) {
  uint8_t *m = G.map;
  for (int i = 0; i < MAP_LEN; i++) {
    if (m[i] == T_EMPTY && rnd_int(10) == 0) m[i] = T_EMPTY_DECO;
    else if (m[i] == T_WALL && rnd_int(8) == 0) m[i] = T_WALL_DECO;
  }
  m[(CTB_TOP + CTB_HH) * MAP_W + CTB_CENTER + MAP_W + 1] = T_SIGN;
}
// ---------------- LastShopLevel (depth 21): the imp's shop, if his quest was done when the floor was first made ----------------
static bool s_lastshop;
static bool build_last_shop(void) {
  if (!init_rooms()) return false;
  s_ne = 0;
  uint8_t path[MAX_ROOMS];
  int n, retry = 0, min_distance = 0;
  while ((min_distance + 1) * (min_distance + 1) <= G.n_rooms) min_distance++;
  do {
    int guard = 0;
    do { if (guard++ > 10) return false; s_room_entrance = rnd_int(G.n_rooms); } while (width(&R[s_room_entrance]) < 4 || height(&R[s_room_entrance]) < 4);
    guard = 0;
    do { if (guard++ > 10) return false; s_room_exit = rnd_int(G.n_rooms); }
    while (s_room_exit == s_room_entrance || width(&R[s_room_exit]) < 6 || height(&R[s_room_exit]) < 6 || R[s_room_exit].top == 0);
    build_distance_map(s_room_exit);
    n = build_path(s_room_entrance, s_room_exit, path);
    if (retry++ > 10) return false;
  } while (n < min_distance);
  R[s_room_entrance].type = RT_ENTRANCE;
  R[s_room_exit].type = RT_EXIT;
  for (int i = 0; i < n; i++) s_price[path[i]] = s_dist[s_room_entrance];
  build_distance_map(s_room_exit);
  n = build_path(s_room_entrance, s_room_exit, path);
  if (n < 0) return false;
  int room = s_room_entrance;
  for (int i = 0; i < n; i++) { connect(room, path[i]); room = path[i]; }
  int shop = -1, best = 0;
  for (int r = 0; r < G.n_rooms; r++)
    if (R[r].type == RT_NULL && n_connected(r) > 0) {
      R[r].type = RT_PASSAGE;
      if (width(&R[r]) * height(&R[r]) > best) { best = width(&R[r]) * height(&R[r]); shop = r; }
    }
  if (shop < 0 || best < 30) return false;
  R[shop].type = s_lastshop ? RT_SHOP : RT_STANDARD;
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type == RT_NULL) { if (G.feeling == FEEL_CHASM && rnd_int(2) == 0) fill_room(&R[r], 0, T_WALL); continue; }   // RegularLevel.paint
    place_doors(r);
    switch (R[r].type) {
      case RT_ENTRANCE: paint_stairs(r, T_ENTRANCE); break;
      case RT_EXIT: paint_stairs(r, T_EXIT); break;
      case RT_STANDARD: paint_standard(r); break;
      case RT_PASSAGE: paint_passage(r); break;
      default: paint_special(r); break;
    }
  }
  for (int r = 0; r < G.n_rooms; r++) paint_doors(r);
  no_respawn();                                        // the last shop: no respawning / teleport cells (LastShopLevel)
  paint_water();                                       // RegularLevel.paintWater / paintGrass with LastShopLevel's patches
  paint_grass();
  return true;
}
static void decorate_last_shop(void) {
  uint8_t *m = G.map;
  for (int i = 0; i < MAP_LEN; i++) {
    if (m[i] == T_EMPTY && rnd_int(10) == 0) m[i] = T_EMPTY_DECO;
    else if (m[i] == T_WALL && rnd_int(8) == 0) m[i] = T_WALL_DECO;
    else if (m[i] == T_SECRET_DOOR) m[i] = T_DOOR;
  }
  if (s_lastshop) place_sign();
}

// ---------------- HallsBossLevel (depth 25): five pillared halls round a sealed room; Yog waits beyond ----------------
#define HB_L (MAP_W / 2 - 1)
#define HB_R (MAP_W / 2 + 1)
#define HB_T (MAP_H / 2 - 1)
#define HB_B (MAP_H / 2 + 1)
static bool build_halls_boss(void) {
  for (int i = 0; i < 5; i++) {
    int top = rnd_range(2, HB_T - 1), bottom = rnd_range(HB_B + 1, 22);
    fill(2 + i * 4, top, 4, bottom - top + 1, T_EMPTY);
    if (i == 2) G.exit = (i * 4 + 3) + (top - 1) * MAP_W;
    for (int j = 0; j < 4; j++)
      if (rnd_int(2) == 0) { int y = rnd_range(top + 1, bottom - 1); G.map[i * 4 + j + y * MAP_W] = T_WALL_DECO; }   // (sic: i*4+j)
  }
  G.map[G.exit] = T_LOCKED_EXIT;
  fill(HB_L - 1, HB_T - 1, HB_R - HB_L + 3, HB_B - HB_T + 3, T_WALL);
  fill(HB_L, HB_T, HB_R - HB_L + 1, HB_B - HB_T + 1, T_EMPTY);
  G.entrance = rnd_int2(HB_L + 1, HB_R - 1) + rnd_int2(HB_T + 1, HB_B - 1) * MAP_W;
  G.map[G.entrance] = T_ENTRANCE;
  uint8_t *p = patch(45, 6);
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && BIT_GET(p, i)) G.map[i] = T_WATER;
  whole_map(-1);
  return true;
}
// ---------------- LastLevel (depth 26): the Amulet of Yendor on its pedestal ----------------
#define LL_SIZE 7
static bool build_last_level(void) {
  memset(G.map, T_WALL, MAP_LEN);
  fill(1, 1, LL_SIZE, LL_SIZE, T_WATER);
  fill(2, 2, LL_SIZE - 2, LL_SIZE - 2, T_EMPTY);
  fill(LL_SIZE / 2, LL_SIZE / 2, 3, 3, T_EMPTY_SP);
  G.entrance = LL_SIZE * MAP_W + LL_SIZE / 2 + 1;
  G.map[G.entrance] = T_ENTRANCE;
  G.exit = G.entrance - MAP_W * LL_SIZE;
  G.map[G.exit] = T_LOCKED_EXIT;
  int ped = (LL_SIZE / 2 + 1) * (MAP_W + 1);
  G.map[ped] = T_PEDESTAL;
  G.map[ped - 1] = G.map[ped + 1] = T_STATUE_SP;
  G.feeling = FEEL_NONE;
  whole_map(-1);
  return true;
}
static void decorate_floor_deco(void) {               // HallsBossLevel / LastLevel.decorate
  for (int i = 0; i < MAP_LEN; i++) if (G.map[i] == T_EMPTY && rnd_int(10) == 0) G.map[i] = T_EMPTY_DECO;
}

static bool build(void) {
  if (s_depth == 25) return build_halls_boss();
  if (s_depth == 26) return build_last_level();
  if (s_depth == 15) return build_caves_boss();
  if (s_depth == 20) return build_city_boss();
  if (s_depth == 21) return build_last_shop();
  if (!init_rooms()) return false;
  s_ne = 0;
  if (s_depth == 5) return build_sewer_boss();
  if (s_depth == 10) return build_prison_boss();
  s_ne = 0;
  int distance, retry = 0, min_distance = 0;
  while ((min_distance + 1) * (min_distance + 1) <= G.n_rooms) min_distance++;     // (int)sqrt(rooms)
  do {
    int guard = 0;
    do { s_room_entrance = rnd_int(G.n_rooms); } while ((width(&R[s_room_entrance]) < 4 || height(&R[s_room_entrance]) < 4) && ++guard < 1000);
    guard = 0;
    do { s_room_exit = rnd_int(G.n_rooms); }
    while ((s_room_exit == s_room_entrance || width(&R[s_room_exit]) < 4 || height(&R[s_room_exit]) < 4) && ++guard < 1000);
    if (guard >= 1000) return false;
    build_distance_map(s_room_exit);
    distance = s_dist[s_room_entrance];
    if (retry++ > 10) return false;
  } while (distance < min_distance);
  R[s_room_entrance].type = RT_ENTRANCE;
  R[s_room_exit].type = RT_EXIT;

  RMask connected = (RMask)1 << s_room_entrance;
  uint8_t path[MAX_ROOMS];
  for (int pass = 0; pass < 2; pass++) {               // main path, then a second one around it
    build_distance_map(s_room_exit);
    int n = build_path(s_room_entrance, s_room_exit, path);
    if (n < 0) return false;
    int room = s_room_entrance;
    for (int i = 0; i < n; i++) { connect(room, path[i]); room = path[i]; connected |= (RMask)1 << room; }
    if (pass == 0) for (int i = 0; i < n; i++) s_price[path[i]] = s_dist[s_room_entrance];   // Graph.setPrice
  }
  int n_connected_target = G.n_rooms * (50 + rnd_int(20)) / 100;
  for (int guard = 0; popcount(connected) < n_connected_target && guard < 10000; guard++) {
    int cr = mask_pick(connected), orr = mask_pick(s_nb[cr]);
    if (orr >= 0 && !((connected >> orr) & 1)) { connect(cr, orr); connected |= (RMask)1 << orr; }
  }
  if (s_depth == 6 || s_depth == 11 || s_depth == 16) {   // Dungeon.shopOnLevel: a dead end off the entrance room
    RMask cand = 0;                                    // roomEntrance.connected is a HashMap: the first fit is any of them
    for (int k = 0; k < s_ne; k++) {
      if (s_e[k].a != s_room_entrance && s_e[k].b != s_room_entrance) continue;
      int o = other(&s_e[k], s_room_entrance);
      if (n_connected(o) == 1 && width(&R[o]) >= 5 && height(&R[o]) >= 5) cand |= (RMask)1 << o;
    }
    int shop = mask_pick(cand);
    if (shop < 0) return false;
    R[shop].type = RT_SHOP;
  }
  G.weak_floors &= ~(1u << s_depth);
  assign_room_types();

  for (int r = 0; r < G.n_rooms; r++) {                // paint()
    if (R[r].type != RT_NULL) {
      place_doors(r);
      switch (R[r].type) {
        case RT_ENTRANCE: paint_stairs(r, T_ENTRANCE); break;
        case RT_EXIT: paint_stairs(r, T_EXIT); break;
        case RT_STANDARD: paint_standard(r); break;
        case RT_TUNNEL: paint_tunnel(r); break;
        case RT_PASSAGE: paint_passage(r); break;
        default: paint_special(r); break;
      }
    } else if (G.feeling == FEEL_CHASM && rnd_int(2) == 0) fill_room(&R[r], 0, T_WALL);
  }
  for (int r = 0; r < G.n_rooms; r++) paint_doors(r);
  paint_water();
  paint_grass();
  place_traps();
  return true;
}

static void place_sign(void) {
  for (int guard = 0; guard < 1000; guard++) { int p = level_room_random(&R[s_room_entrance], 0); if (p != G.entrance) { G.map[p] = T_SIGN; break; } }
}
static void decorate_prison(void) {                   // PrisonLevel.decorate: blood in the corners, torches on the walls
  uint8_t *m = G.map;
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (m[i] != T_EMPTY) continue;
    int c = 5;                                         // 0.05 + 0.2 per wall corner, in hundredths
    if (m[i + 1] == T_WALL && m[i + MAP_W] == T_WALL) c += 20;
    if (m[i - 1] == T_WALL && m[i + MAP_W] == T_WALL) c += 20;
    if (m[i + 1] == T_WALL && m[i - MAP_W] == T_WALL) c += 20;
    if (m[i - 1] == T_WALL && m[i - MAP_W] == T_WALL) c += 20;
    if (rnd_int(100) < c) m[i] = T_EMPTY_DECO;
  }
  for (int i = 0; i < MAP_W; i++)
    if (m[i] == T_WALL && (m[i + MAP_W] == T_EMPTY || m[i + MAP_W] == T_EMPTY_SP) && rnd_int(6) == 0) m[i] = T_WALL_DECO;
  for (int i = MAP_W; i < MAP_LEN - MAP_W; i++)
    if (m[i] == T_WALL && m[i - MAP_W] == T_WALL && (m[i + MAP_W] == T_EMPTY || m[i + MAP_W] == T_EMPTY_SP) && rnd_int(3) == 0) m[i] = T_WALL_DECO;
  place_sign();
}
static void decorate_caves(void) {                    // CavesLevel.decorate: rounded rooms, moss, ore veins, chasm clefts
  uint8_t *m = G.map;
  for (int ri = 0; ri < G.n_rooms; ri++) {
    Room *r = &R[ri];
    if (r->type != RT_STANDARD || width(r) <= 3 || height(r) <= 3) continue;
    int s = width(r) * height(r);                      // Rect.square
    int c = (r->left + 1) + (r->top + 1) * MAP_W;
    if (rnd_int(s) > 8 && m[c - 1] == T_WALL && m[c - MAP_W] == T_WALL) m[c] = T_WALL;
    c = (r->right - 1) + (r->top + 1) * MAP_W;
    if (rnd_int(s) > 8 && m[c + 1] == T_WALL && m[c - MAP_W] == T_WALL) m[c] = T_WALL;
    c = (r->left + 1) + (r->bottom - 1) * MAP_W;
    if (rnd_int(s) > 8 && m[c - 1] == T_WALL && m[c + MAP_W] == T_WALL) m[c] = T_WALL;
    c = (r->right - 1) + (r->bottom - 1) * MAP_W;
    if (rnd_int(s) > 8 && m[c + 1] == T_WALL && m[c + MAP_W] == T_WALL) m[c] = T_WALL;
    for (int k = 0; k < s_ne; k++) {
      if (s_e[k].a != ri && s_e[k].b != ri) continue;
      int n = other(&s_e[k], ri);
      if ((R[n].type == RT_STANDARD || R[n].type == RT_TUNNEL) && rnd_int(3) == 0) set(s_e[k].x, s_e[k].y, T_EMPTY_DECO);
    }
  }
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W; i++) {
    if (m[i] != T_EMPTY) continue;
    int n = (m[i + 1] == T_WALL) + (m[i - 1] == T_WALL) + (m[i + MAP_W] == T_WALL) + (m[i - MAP_W] == T_WALL);
    if (rnd_int(6) <= n) m[i] = T_EMPTY_DECO;
  }
  for (int i = 0; i < MAP_LEN; i++) if (m[i] == T_WALL && rnd_int(12) == 0) m[i] = T_WALL_DECO;
  place_sign();
  if (boss_next()) return;
  for (int r = 0; r < G.n_rooms; r++) {
    if (R[r].type != RT_STANDARD) continue;
    for (int n = 0; n < G.n_rooms; n++) {
      if (!((s_nb[r] >> n) & 1) || R[n].type != RT_STANDARD || edge_find(r, n) >= 0) continue;
      Rect w = intersect(&R[r], &R[n]);
      if (w.l == w.r && w.b - w.t >= 5) fill(w.l, w.t + 2, 1, w.b - w.t - 3, T_CHASM);   // top += 2, bottom -= 1, then height()
      else if (w.t == w.b && w.r - w.l >= 5) fill(w.l + 2, w.t, w.r - w.l - 3, 1, T_CHASM);
    }
  }
}
static void decorate_city(void) {                     // CityLevel.decorate
  uint8_t *m = G.map;
  for (int i = 0; i < MAP_LEN; i++) {
    if (m[i] == T_EMPTY && rnd_int(10) == 0) m[i] = T_EMPTY_DECO;
    else if (m[i] == T_WALL && rnd_int(8) == 0) m[i] = T_WALL_DECO;
  }
  place_sign();
}
static void decorate_halls(void) {                    // HallsLevel.decorate
  uint8_t *m = G.map;
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (m[i] == T_EMPTY) {
      int count = 0;
      for (int j = 0; j < 8; j++) if (PASSABLE(m[i + N8[j]])) count++;
      if (rnd_int(80) < count) m[i] = T_EMPTY_DECO;
    } else if (m[i] == T_WALL && m[i - 1] != T_WALL_DECO && m[i - MAP_W] != T_WALL_DECO && rnd_int(20) == 0) m[i] = T_WALL_DECO;
  }
  place_sign();
}
static void decorate(void) {                           // SewerLevel.decorate
  uint8_t *m = G.map;
  if (s_depth == 10) { decorate_prison_boss(); return; }
  if (s_depth == 15) { decorate_caves_boss(); return; }
  if (s_depth == 20) { decorate_city_boss(); return; }
  if (s_depth == 21) { decorate_last_shop(); return; }
  if (s_depth >= 25) { decorate_floor_deco(); return; }
  switch (s_depth == 5 ? 0 : s_region) {
    case 1: decorate_prison(); return;
    case 2: decorate_caves(); return;
    case 3: decorate_city(); return;
    case 4: decorate_halls(); return;
  }
  if (s_depth == 5) {                                  // SewerBossLevel.decorate: a mossy wall and a moat under the exit's wall
    Room *ex = &R[s_room_exit];
    int start = ex->top * MAP_W + ex->left + 1, end = start + width(ex) - 1;
    for (int i = start; i < end; i++) {
      if (i != G.exit) { m[i] = T_WALL_DECO; m[i + MAP_W] = T_WATER; } else m[i + MAP_W] = T_EMPTY;
    }
    for (int guard = 0; guard < 1000; guard++) { int p = level_room_random(&R[s_room_entrance], 0); if (p != G.entrance) { m[p] = T_SIGN; break; } }
    return;
  }
  for (int i = 0; i < MAP_W; i++)
    if (m[i] == T_WALL && m[i + MAP_W] == T_WATER && rnd_int(4) == 0) m[i] = T_WALL_DECO;
  for (int i = MAP_W; i < MAP_LEN - MAP_W; i++)
    if (m[i] == T_WALL && m[i - MAP_W] == T_WALL && m[i + MAP_W] == T_WATER && rnd_int(2) == 0) m[i] = T_WALL_DECO;
  for (int i = MAP_W + 1; i < MAP_LEN - MAP_W - 1; i++) {
    if (m[i] != T_EMPTY) continue;
    int count = (m[i + 1] == T_WALL) + (m[i - 1] == T_WALL) + (m[i + MAP_W] == T_WALL) + (m[i - MAP_W] == T_WALL);
    if (rnd_int(16) < count * count) m[i] = T_EMPTY_DECO;
  }
  for (int guard = 0; guard < 1000; guard++) {
    int p = level_room_random(&R[s_room_entrance], 0);
    if (p != G.entrance) { m[p] = T_SIGN; break; }
  }
}

static const int8_t N4X[4] = { 0, 1, 0, -1 }, N4Y[4] = { -1, 0, 1, 0 };
static void stitch(void) {                             // Level.buildFlagMaps: water edges and chasm walls
  uint8_t *m = G.map;
  for (int i = MAP_W; i < MAP_LEN - MAP_W; i++) {
    if (TFLAGS(m[i]) & F_LIQUID) {
      int t = T_WATER_TILES, x = i % MAP_W;
      for (int j = 0; j < 4; j++) {
        int nx = x + N4X[j];
        int n = (nx < 0 || nx >= MAP_W) ? -1 : i + N4X[j] + N4Y[j] * MAP_W;
        if (n < 0 || (TFLAGS(m[n]) & F_UNSTITCHABLE)) t += 1 << j;
      }
      m[i] = t;
    }
    if ((TFLAGS(m[i]) & F_PIT) && !(TFLAGS(m[i - MAP_W]) & F_PIT)) {
      int c = m[i - MAP_W];
      if (c == T_EMPTY_SP || c == T_STATUE_SP) m[i] = T_CHASM_FLOOR_SP;
      else if (TFLAGS(c) & F_LIQUID) m[i] = T_CHASM_WATER;
      else if (TFLAGS(c) & F_UNSTITCHABLE) m[i] = T_CHASM_WALL;
      else m[i] = T_CHASM_FLOOR;
    }
  }
}

static void level_generate(uint8_t depth, int16_t *tombs, int *n_tombs) {
  memset(SCR, 0, sizeof(Scratch));                     // as the overlay's own bss was: zero at every load
  s_depth = depth; s_tombs = tombs; s_ntombs = n_tombs; s_smith = -1;
  if (depth == 21) {                                   // the imp's shop: decided the first time the floor is made, then kept
    if (!(G.dflags & DF_LASTSHOP_MADE)) { G.dflags |= DF_LASTSHOP_MADE; if (G.q.imp == QS_REWARDED) G.dflags |= DF_LASTSHOP_SHOP; }
    s_lastshop = G.dflags & DF_LASTSHOP_SHOP;
  }
  s_region = depth <= 5 ? 0 : depth <= 10 ? 1 : depth <= 15 ? 2 : depth <= 21 ? 3 : 4;
  G.feeling = FEEL_NONE;
  if (depth > 1 && depth % 5 != 0) {                  // Level.create: the floor's feeling (not on boss floors)
    switch (rnd_int(10)) {
      case 0: if (!boss_next()) G.feeling = FEEL_CHASM; break;
      case 1: G.feeling = FEEL_WATER; break;
      case 2: G.feeling = FEEL_GRASS; break;
    }
  }
  for (int tries = 0; ; tries++) {
    memset(G.map, G.feeling == FEEL_CHASM ? T_CHASM : T_WALL, MAP_LEN);
    G.secret_doors = 0; *n_tombs = 0; G.entrance = G.exit = -1; G.well_pos = G.altar_pos = -1;
    if (build() && G.entrance >= 0 && G.exit >= 0) break;
    s_smith = -1;
    if (tries >= 1000) {                               // keeps the watch alive regardless (PrisonBossLevel needs a 7x7 room: ~1 try in 30 succeeds)
      memset(G.map, T_WALL, MAP_LEN);
      G.n_rooms = 1; G.rooms[0] = (Room){ 4, 4, 12, 12, RT_STANDARD, -1 };
      fill(5, 5, 7, 7, T_EMPTY);
      G.entrance = 5 * MAP_W + 5; G.exit = 11 * MAP_W + 11; G.map[G.entrance] = T_ENTRANCE; G.map[G.exit] = T_EXIT;
      s_room_entrance = 0;
      break;
    }
  }
  if (s_smith >= 0 && G.q.smith == QS_NONE) { G.q.smith = QS_SPAWNED; G.q.smith_depth = s_depth; G.q.smith_alt = s_smith; }
  decorate();
  stitch();
}

// the overlay's entry table (first thing in the image)
OVL_ENTRY(level, (void *)level_generate);
