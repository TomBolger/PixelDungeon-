/*
 * Pixel Dungeon for Pebble - terrain, random numbers and rooms: the always-loaded part of the level code.
 * The generator itself (RegularLevel, SewerLevel, painters) is the overlay in src/ovl/level.
 */
#include "pd.h"

// ---------------- Terrain (Terrain.java) ----------------
#define F_TRAP_HIDDEN (F_PASSABLE | F_SECRET)
const uint16_t TERRAIN_FLAGS[64] = {
  [T_CHASM] = F_AVOID | F_PIT | F_UNSTITCHABLE,
  [T_EMPTY] = F_PASSABLE, [T_GRASS] = F_PASSABLE | F_FLAMABLE, [T_EMPTY_WELL] = F_PASSABLE,
  [T_WALL] = F_LOS_BLOCKING | F_SOLID | F_UNSTITCHABLE,
  [T_DOOR] = F_PASSABLE | F_LOS_BLOCKING | F_FLAMABLE | F_SOLID | F_UNSTITCHABLE,
  [T_OPEN_DOOR] = F_PASSABLE | F_FLAMABLE | F_UNSTITCHABLE,
  [T_ENTRANCE] = F_PASSABLE, [T_EXIT] = F_PASSABLE, [T_EMBERS] = F_PASSABLE,
  [T_LOCKED_DOOR] = F_LOS_BLOCKING | F_SOLID | F_UNSTITCHABLE,
  [T_PEDESTAL] = F_PASSABLE | F_UNSTITCHABLE,
  [T_WALL_DECO] = F_LOS_BLOCKING | F_SOLID | F_UNSTITCHABLE,
  [T_BARRICADE] = F_FLAMABLE | F_SOLID | F_LOS_BLOCKING,
  [T_EMPTY_SP] = F_PASSABLE | F_UNSTITCHABLE,
  [T_HIGH_GRASS] = F_PASSABLE | F_LOS_BLOCKING | F_FLAMABLE,
  [T_SECRET_DOOR] = F_LOS_BLOCKING | F_SOLID | F_SECRET | F_UNSTITCHABLE,
  [T_TOXIC_TRAP] = F_AVOID, [T_SECRET_TOXIC_TRAP] = F_TRAP_HIDDEN,
  [T_FIRE_TRAP] = F_AVOID, [T_SECRET_FIRE_TRAP] = F_TRAP_HIDDEN,
  [T_PARALYTIC_TRAP] = F_AVOID, [T_SECRET_PARALYTIC_TRAP] = F_TRAP_HIDDEN,
  [T_INACTIVE_TRAP] = F_PASSABLE, [T_EMPTY_DECO] = F_PASSABLE,
  [T_LOCKED_EXIT] = F_SOLID, [T_UNLOCKED_EXIT] = F_PASSABLE,
  [T_POISON_TRAP] = F_AVOID, [T_SECRET_POISON_TRAP] = F_TRAP_HIDDEN,
  [T_SIGN] = F_PASSABLE | F_FLAMABLE,
  [T_ALARM_TRAP] = F_AVOID, [T_SECRET_ALARM_TRAP] = F_TRAP_HIDDEN,
  [T_LIGHTNING_TRAP] = F_AVOID, [T_SECRET_LIGHTNING_TRAP] = F_TRAP_HIDDEN,
  [T_WELL] = F_AVOID, [T_STATUE] = F_SOLID, [T_STATUE_SP] = F_SOLID | F_UNSTITCHABLE,
  [T_GRIPPING_TRAP] = F_AVOID, [T_SECRET_GRIPPING_TRAP] = F_TRAP_HIDDEN,
  [T_SUMMONING_TRAP] = F_AVOID, [T_SECRET_SUMMONING_TRAP] = F_TRAP_HIDDEN,
  [T_BOOKSHELF] = F_FLAMABLE | F_SOLID | F_LOS_BLOCKING | F_UNSTITCHABLE,
  [T_ALCHEMY] = F_PASSABLE,
  [T_CHASM_FLOOR] = F_AVOID | F_PIT | F_UNSTITCHABLE, [T_CHASM_FLOOR_SP] = F_AVOID | F_PIT | F_UNSTITCHABLE,
  [T_CHASM_WALL] = F_AVOID | F_PIT | F_UNSTITCHABLE, [T_CHASM_WATER] = F_AVOID | F_PIT | F_UNSTITCHABLE,
  [48 ... 63] = F_PASSABLE | F_LIQUID | F_UNSTITCHABLE,
};

int terrain_discover(int t) {
  switch (t) {
    case T_SECRET_DOOR: return T_DOOR;
    case T_SECRET_FIRE_TRAP: return T_FIRE_TRAP;
    case T_SECRET_PARALYTIC_TRAP: return T_PARALYTIC_TRAP;
    case T_SECRET_TOXIC_TRAP: return T_TOXIC_TRAP;
    case T_SECRET_POISON_TRAP: return T_POISON_TRAP;
    case T_SECRET_ALARM_TRAP: return T_ALARM_TRAP;
    case T_SECRET_LIGHTNING_TRAP: return T_LIGHTNING_TRAP;
    case T_SECRET_GRIPPING_TRAP: return T_GRIPPING_TRAP;
    case T_SECRET_SUMMONING_TRAP: return T_SUMMONING_TRAP;
    default: return t;
  }
}

// Level.tileName with SewerLevel's override

// ---------------- Random ----------------
uint32_t g_rng;
uint32_t rnd_next(void) {
  uint32_t t = (g_rng += 0x6D2B79F5u);
  t = (t ^ (t >> 15)) * (t | 1);
  t ^= t + (t ^ (t >> 7)) * (t | 61);
  return t ^ (t >> 14);
}
int rnd_int(int n) { return n <= 0 ? 0 : (int)(rnd_next() % (uint32_t)n); }
int rnd_int2(int lo, int hi) { return lo + rnd_int(hi - lo); }
int rnd_range(int lo, int hi) { return lo + rnd_int(hi - lo + 1); }
int rnd_normal(int lo, int hi) { uint32_t s = rnd_next() >> 16; s += rnd_next() >> 16; return lo + (int)(s * (uint32_t)(hi - lo + 1) >> 17); }   // NormalIntRange: min + (int)((Float() + Float()) * (max - min + 1) / 2)
bool rnd_pct(int pct) { return rnd_int(100) < pct; }
uint32_t rnd_scaled(int max) { return (rnd_next() >> 16) * (uint32_t)max; }
int rnd_chances(const uint16_t *w, int n) {
  int sum = 0;
  for (int i = 0; i < n; i++) sum += w[i];
  if (sum <= 0) return -1;
  int v = rnd_int(sum);
  for (int i = 0; i < n; i++) { if (v < w[i]) return i; v -= w[i]; }
  return n - 1;
}

// ---------------- Rooms (Room.java): what play needs after generation ----------------
#define R (G.rooms)
int level_room_random(const Room *r, int m) {           // Room.random(m)
  int x = rnd_int2(r->left + 1 + m, r->right - m), y = rnd_int2(r->top + 1 + m, r->bottom - m);
  return x + y * MAP_W;
}
int level_random_room_cell(int type, int tries) {        // RegularLevel.randomRoom(type).random()
  for (int i = 0; i < tries; i++) { int r = rnd_int(G.n_rooms); if (R[r].type == type) return level_room_random(&R[r], 0); }
  return -1;
}

