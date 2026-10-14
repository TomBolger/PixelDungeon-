/*
 * Pixel Dungeon for Pebble - the "floor" overlay: saving and loading, building floors on arrival
 * (InterlevelScene), new games. Moved out of game.c to make room in the app image (0.8).
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"
#include <stddef.h>

// ---------------- Saves ----------------
// Main record (keys 2..15): hero, game, and the current floor. Each other visited floor has its
// own record (keys 40 + 3 * depth, up to 3 keys); floors are rebuilt from the seed and these apply on top.
// 0.32: gas and fire clouds (Blob) of a floor go in their own keys, KEY_GAS(d), written with the save and on leaving.
#define SAVE_MAGIC 0x5044000Eu     // "PD" v14 (0.33): floors are generated as in PD (older saves' floors would not match)
#define SAVE_MAGIC_V13 0x5044000Du // v13 (0.17): things fallen down a chasm
#define SAVE_MAGIC_V12 0x5044000Cu // v12 (0.14): the journal
#define SAVE_MAGIC_V11 0x5044000Bu // v11 (0.14 test builds): what the bags hold
#define SAVE_MAGIC_V10 0x5044000Au // v10 (0.9): floor vars, weak floors, enchantment scrolls
#define SAVE_MAGIC_V9 0x50440009u  // v9 (0.8): creatures' state, hero extras, keys, quests, dew
#define SAVE_MAGIC_V8 0x50440008u
#define HERO_V8_SIZE ((offsetof(Hero, ooze) + 3) & ~3u)    // the Hero record before v9
#define SAVE_MAGIC_V7 0x50440007u
#define HERO_V7_SIZE ((offsetof(Hero, ring) + 3) & ~3u)   // the Hero record before v8 (it ended at the rings)
#define SAVE_MAGIC_V6 0x50440006u
#define KEY_LEN 1
#define KEY_DATA 2
// persist4: the save takes turns between two sets of keys (2..11 and 20..29) and KEY_LEN, written last, says which holds it
// (SAVE_BANK in its length): a save cut short (the watch reset, the battery out) leaves the previous one whole
#define KEY_DATA_B 20
#define SAVE_BANK 0x8000
#define KEY_LEVEL(d) (40 + 3 * (d))
// ---------------- Gas and fire clouds (0.32) ----------------
// Level.blobs are saved with their floor in PD. Here a floor's clouds take up to 2 keys of their own (KEY_GAS): per cloud
// kind its id + 1, then runs of cells (length, start cell, the amounts), a 0 length closing the kind and a 0 closing all.
// Amounts above 127 are kept to 64ths (about 1.5% off), so a fresh potion's cloud comes back as big as it was.
#define KEY_GAS(d) (122 + 2 * (d))                    // 124 .. 175 (the floor records end at 120)
#define GAS_MAX (2 * PERSIST_DATA_MAX_LENGTH)
static void gas_forget(int d) { persist_delete(KEY_GAS(d)); persist_delete(KEY_GAS(d) + 1); }
static void forget_floor(int d) { for (int k = 0; k < 3; k++) persist_delete(KEY_LEVEL(d) + k); gas_forget(d); }
#define SAVE_BUF (10 * PERSIST_DATA_MAX_LENGTH)        // persist4: the ten keys (was 2400: a 2401..2560 B save that would not pack was written, then refused)
#define SAVE_SPACE (2 * MAP_LEN * 2)                  // the scratch block: 4 KB
// 0.14: saves pack into the path finder's 4 KB scratch block (idle between turns): saving needs no free heap but this code's
static uint8_t *s_buf;
static bool buf_alloc(void) { s_buf = (uint8_t *)s_dist; return s_buf != NULL; }
static void buf_release(void) { s_buf = NULL; }
#define PUT(p, v) do { memcpy(p, &(v), sizeof(v)); p += sizeof(v); } while (0)
#define GET(p, v) do { memcpy(&(v), p, sizeof(v)); p += sizeof(v); } while (0)

// visited cells: run lengths (alternating unseen / seen), else the raw bitset
static uint8_t *pack_visited(uint8_t *p) {
  uint8_t *start = p++;
  int bit = 0, i = 0;
  while (i < MAP_LEN && p - start < 140) {             // a 255-long run is followed by a 0-long one
    int run = 0;
    while (i < MAP_LEN && (int)BIT_GET(G.visited, i) == bit && run < 255) { run++; i++; }
    *p++ = run;
    bit ^= 1;
  }
  if (i < MAP_LEN) { *start = 0; memcpy(start + 1, G.visited, sizeof G.visited); return start + 1 + sizeof G.visited; }
  *start = (uint8_t)(p - start - 1);
  return p;
}
static const uint8_t *unpack_visited(const uint8_t *p) {
  int n = *p++;
  if (n == 0) { memcpy(G.visited, p, sizeof G.visited); return p + sizeof G.visited; }
  memset(G.visited, 0, sizeof G.visited);
  int i = 0, bit = 0;
  for (int k = 0; k < n; k++) {
    int run = p[k];
    for (int j = 0; j < run && i < MAP_LEN; j++, i++) if (bit) BIT_SET(G.visited, i);
    bit ^= 1;
  }
  return p + n;
}
typedef struct { int16_t pos, target, HP, dt; uint8_t type, state, flags, uid; Buffs b; } MobRec8;   // before v9
typedef struct { int16_t pos, target, HP, dt, HT, aux; uint8_t type, state, flags, uid, aux2, vertigo; Buffs b; } MobRec;
static bool s_old_rec, s_v10_rec;                      // unpack_level reads a v8 record / a record with lvars
static uint8_t *pack_level(uint8_t *p) {
  p = pack_visited(p);
  uint8_t n = 0;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) n++;
  PUT(p, n);
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0) continue;
    int32_t dt = m->time - G.now; if (dt > 30000) dt = 30000; if (dt < -30000) dt = -30000;
    MobRec r = { m->pos, m->target, m->HP, dt, m->HT, m->aux, m->type, m->state, m->flags, m->uid, m->aux2, m->vertigo, m->b };
    PUT(p, r);
  }
  PUT(p, G.n_heaps); memcpy(p, G.heaps, G.n_heaps * sizeof(Heap)); p += G.n_heaps * sizeof(Heap);
  PUT(p, G.n_diffs);
  for (int i = 0; i < G.n_diffs; i++) { PUT(p, G.diffs[i].pos); PUT(p, G.diffs[i].terrain); }
  PUT(p, G.n_plants); memcpy(p, G.plants, G.n_plants * sizeof(Plant)); p += G.n_plants * sizeof(Plant);   // v9
  uint8_t acc = 0; int nb = 0;                         // v9: 2 bits per generated grass cell: 1 grass, 2 embers, 3 high grass, 0 other
  for (int c = 0; c < MAP_LEN; c++) {
    if (!BIT_GET(G.hg, c)) continue;
    int t = G.map[c], v = t == T_GRASS ? 1 : t == T_EMBERS ? 2 : t == T_HIGH_GRASS ? 3 : 0;
    acc |= v << (nb & 6);
    if (((nb += 2) & 7) == 0) { *p++ = acc; acc = 0; }
  }
  if (nb & 7) *p++ = acc;
  PUT(p, G.lvars);                                     // v10
  return p;
}
static const uint8_t *unpack_level(const uint8_t *p) {
  p = unpack_visited(p);
  uint8_t n; GET(p, n);
  G.n_mobs = 0;
  for (int i = 0; i < n && i < MAX_MOBS; i++) {
    MobRec r;
    if (s_old_rec) { MobRec8 o; GET(p, o); r = (MobRec){ o.pos, o.target, o.HP, o.dt, MOB_DEFS[o.type].HT, 0, o.type, o.state, o.flags, o.uid, 0, 0, o.b }; }
    else GET(p, r);
    if (r.type >= MT_COUNT) continue;
    G.mobs[G.n_mobs++] = (Mob){ .time = G.now + r.dt, .pos = r.pos, .target = r.target, .HP = r.HP, .HT = r.HT,
                                .type = r.type, .state = r.state, .flags = r.flags, .uid = r.uid, .b = r.b, .aux = r.aux, .aux2 = r.aux2, .vertigo = r.vertigo };
  }
  GET(p, G.n_heaps); if (G.n_heaps > MAX_HEAPS) G.n_heaps = MAX_HEAPS;
  memcpy(G.heaps, p, G.n_heaps * sizeof(Heap)); p += G.n_heaps * sizeof(Heap);
  GET(p, G.n_diffs); if (G.n_diffs > MAX_DIFFS) G.n_diffs = MAX_DIFFS;
  for (int i = 0; i < G.n_diffs; i++) { GET(p, G.diffs[i].pos); GET(p, G.diffs[i].terrain); }
  for (int i = 0; i < G.n_diffs; i++) G.map[G.diffs[i].pos] = G.diffs[i].terrain;
  G.n_plants = 0;
  if (!s_old_rec) {
    GET(p, G.n_plants); if (G.n_plants > MAX_PLANTS) G.n_plants = MAX_PLANTS;
    memcpy(G.plants, p, G.n_plants * sizeof(Plant)); p += G.n_plants * sizeof(Plant);
    int nb = 0;
    static const uint8_t VEG[4] = { 0, T_GRASS, T_EMBERS, T_HIGH_GRASS };
    for (int c = 0; c < MAP_LEN; c++) {
      if (!BIT_GET(G.hg, c)) continue;
      int v = (p[nb >> 3] >> (nb & 6)) & 3;
      if (v) G.map[c] = VEG[v];
      nb += 2;
    }
    p += (nb + 7) >> 3;
  }
  if (s_v10_rec) GET(p, G.lvars); else memset(G.lvars, 0, sizeof G.lvars);
  return p;
}
static bool write_keys(uint32_t key, const uint8_t *buf, int n, int max_keys) {
  bool ok = true;
  for (int k = 0, off = 0; off < n && k < max_keys; k++, off += PERSIST_DATA_MAX_LENGTH) {
    int len = n - off < PERSIST_DATA_MAX_LENGTH ? n - off : PERSIST_DATA_MAX_LENGTH;
    if (persist_write_data(key + k, buf + off, len) != len) ok = false;
  }
  return ok;
}
// 0.15: records are packed: for each 8 bytes a mask of the non-zero ones, then those bytes. Creatures' buffs, items and
// unused fields are mostly zeros, so a floor takes about 60% of the room. Unpacking gives whole groups of 8 (a few
// zeros at the end, which nothing reads).
static int zpack(const uint8_t *s, int n, uint8_t *d, int max) {
  int o = 0;
  for (int i = 0; i < n; i += 8) {
    if (o + 9 > max) return -1;
    int m = o++, b = 0;
    for (int k = 0; k < 8 && i + k < n; k++) if (s[i + k]) { b |= 1 << k; d[o++] = s[i + k]; }
    d[m] = b;
  }
  return o;
}
static int zunpack(const uint8_t *s, int n, uint8_t *d, int max) {
  int o = 0;
  for (int i = 0; i < n; ) {
    int b = s[i++];
    if (o + 8 > max) return -1;
    for (int k = 0; k < 8; k++) d[o++] = b >> k & 1 ? (i < n ? s[i++] : 0) : 0;
  }
  return o;
}
static bool read_keys(uint32_t key, uint8_t *buf, int n) {
  for (int k = 0, off = 0; off < n; k++, off += PERSIST_DATA_MAX_LENGTH) {
    int len = n - off < PERSIST_DATA_MAX_LENGTH ? n - off : PERSIST_DATA_MAX_LENGTH;
    if (persist_read_data(key + k, buf + off, len) != len) return false;
  }
  return true;
}
#define LEVEL_MAX (3 * PERSIST_DATA_MAX_LENGTH)
// other floors: a 2-byte head (stored length | RF_* flags) + the level record, at most three keys.
// Pebble keeps 4 KB per app: floors' records share LEVEL_BUDGET. 0.15: when they don't fit, the farthest floors are made
// "lite" first (their map memory and ordinary monsters go; what lies there, shops, NPCs, bosses and statues stay);
// only when that is not enough is the farthest floor forgotten (it comes back regenerated), and the hero is told if
// anything had been left there.
#define LEVEL_BUDGET 2500                              // (if storage still fills up, writes that fail make the far floors give way)
#define RF_Z 0x8000                                    // the body is zero-run packed
#define RF_LITE 0x4000                                 // already made lite
#define RF_LEN 0x3FFF
#define ZOFF 2048                                      // the scratch block: raw record at s_buf + 2, packed at s_buf + ZOFF
static int rec_head(int d) {
  uint8_t head[2]; uint16_t n;
  if (!persist_exists(KEY_LEVEL(d)) || persist_read_data(KEY_LEVEL(d), head, 2) < 2) return 0;
  memcpy(&n, head, 2); return n;
}
static int rec_read(int d) {                           // the record's raw body at s_buf + 2: its length, or -1
  int h = rec_head(d), n = h & RF_LEN;
  if (n < 3 || n > LEVEL_MAX || !buf_alloc() || !read_keys(KEY_LEVEL(d), s_buf + ZOFF, n)) return -1;
  if (!(h & RF_Z)) { memcpy(s_buf + 2, s_buf + ZOFF + 2, n - 2); return n - 2; }
  return zunpack(s_buf + ZOFF + 2, n - 2, s_buf + 2, ZOFF - 2);
}
static bool rec_write(int d, int raw, int flags) {     // the raw body at s_buf + 2, packed and stored
  int z = zpack(s_buf + 2, raw, s_buf + ZOFF + 2, LEVEL_MAX - 2);
  for (int k = 0; k < 3; k++) persist_delete(KEY_LEVEL(d) + k);
  if (z < 0) return false;
  uint16_t h = (uint16_t)((z + 2) | RF_Z | flags); memcpy(s_buf + ZOFF, &h, 2);
  if (write_keys(KEY_LEVEL(d), s_buf + ZOFF, z + 2, 3)) return true;
  for (int k = 0; k < 3; k++) persist_delete(KEY_LEVEL(d) + k);   // the storage is full
  return false;
}
static bool lite_keeps(int t) {                        // what a lite floor keeps: the floor's people, bosses and statues,
  static const uint8_t K[] = { MT_SHOPKEEPER, MT_IMP_SHOP, MT_GHOST, MT_WANDMAKER, MT_BLACKSMITH, MT_IMP, MT_STATUE,   // and what
                               MT_GOO, MT_TENGU, MT_DM300, MT_KING, MT_YOG, MT_ROTTING_FIST, MT_BURNING_FIST,           // never comes
                               MT_FETID_RAT, MT_WRAITH, MT_RAT_KING, MT_PIRANHA, MT_MIMIC };                            // back (quests)
  for (unsigned i = 0; i < sizeof K; i++) if (K[i] == t) return true;
  return false;
}
static int lite(uint8_t *b, int n) {                   // in place, a v9+ body: no map memory, only the kept creatures
  int vn = b[0], vlen = vn ? 1 + vn : 1 + MAP_LEN / 8;
  uint8_t *w = b; const uint8_t *r = b + vlen;
  *w++ = 1; *w++ = 0;                                  // visited: one empty run
  int nm = *r++, keep = 0; uint8_t *cnt = w++;
  const uint8_t *hs = r + nm * sizeof(MobRec);         // the heaps: a creature carrying something (a thief's loot) stays with it
  for (int i = 0; i < nm; i++) {
    MobRec m; memcpy(&m, r, sizeof m); r += sizeof m;
    bool k = lite_keeps(m.type);
    for (int h = 0; h < hs[0] && !k; h++) { int16_t p; memcpy(&p, hs + 1 + h * sizeof(Heap) + offsetof(Heap, pos), 2); k = p == CARRIED_POS(m.uid); }
    if (k) { memcpy(w, &m, sizeof m); w += sizeof m; keep++; }
  }
  *cnt = keep;
  int rest = n - (int)(r - b);
  memmove(w, r, rest);
  uint8_t *g = w, *end = w + rest;                     // trampled grass grows back: its 2-bit states (before the 4 lvars) zeroed
  if (g < end) g += 1 + g[0] * sizeof(Heap);
  if (g < end) g += 1 + g[0] * 3;
  if (g < end) g += 1 + g[0] * sizeof(Plant);
  if (g < end - 11) memset(g, 0, end - 11 - g);       // persist4: n may be rounded up to 8 (an unpacked record): the lvars end within 7 of it
  return (int)(w - b) + rest;
}
static int rec_heaps(int d, int raw) {                 // things lying on a floor whose raw body is at s_buf + 2
  const uint8_t *b = s_buf + 2;
  int vn = b[0], at = vn ? 1 + vn : 1 + MAP_LEN / 8;
  if (at >= raw) return 0;
  at += 1 + b[at] * ((G.v9_levels >> d & 1) ? (int)sizeof(MobRec) : (int)sizeof(MobRec8));
  return at < raw ? b[at] : 0;
}
// which floor is forgotten first: the farthest, but floors with someone to come back to (a boss, a shop, a quest) last
static uint32_t precious(void) {
  const Quests *q = &G.q;
  return 0x2318C60u | 1u << q->ghost_depth | 1u << q->wand_depth | 1u << q->smith_depth | 1u << q->imp_depth;   // 5, 6, 10, 11, 15, 16, 20, 21, 25
}
static int make_room(int keep, int budget) {           // returns what the floors take
  for (;;) {
    int total = 0, far = -1, fd = -99, full = -1, ffd = -1;
    uint32_t pm = precious();
    for (int d = 1; d <= 26; d++) {
      int h = rec_head(d);
      if (!h) continue;
      total += h & RF_LEN;
      if (d == keep) continue;
      int dist = d > keep ? d - keep : keep - d, fr = dist - (int)(pm >> d & 1) * 32;
      if (fr > fd) { fd = fr; far = d; }
      if (!(h & RF_LITE) && (G.v9_levels >> d & 1) && dist > ffd) { ffd = dist; full = d; }
    }
    if (total <= budget || far < 0) return total;
    if (full >= 0) {                                   // make the farthest full floor lite
      int raw = rec_read(full);
      if (raw > 0 && rec_write(full, lite(s_buf + 2, raw), RF_LITE)) continue;
      forget_floor(full);                              // (unreadable: gone)
      continue;
    }
    int raw = rec_read(far), things = raw > 0 ? rec_heaps(far, raw) : 0;
    forget_floor(far);
    if (things) LOG(C_WARN, "The dungeon has swallowed what was left on depth %d.", far);
  }
}
static bool store_floor(void) {                        // this floor's record, else its lite form
  int raw = pack_level(s_buf + 2) - (s_buf + 2);
#ifdef OVL_HOST
  if (getenv("RECSZ")) { int nm = 0; for (int i = 0; i < G.n_mobs; i++) nm += G.mobs[i].HP > 0; int z = zpack(s_buf + 2, raw, s_buf + ZOFF, 2000);
    printf("REC d%d raw=%d z=%d lite=%d mobs=%d heaps=%d diffs=%d\n", G.depth, raw, z, zpack(s_buf + 2, lite(s_buf + 2, raw), s_buf + ZOFF, 2000), nm, G.n_heaps, G.n_diffs);
    raw = pack_level(s_buf + 2) - (s_buf + 2); }
#endif
  return rec_write(G.depth, raw, 0) || rec_write(G.depth, lite(s_buf + 2, raw), RF_LITE);
}
static void save_level_record(void) {
  if (!buf_alloc()) return;
  bool ok = store_floor();
  G.v9_levels |= 1u << G.depth; G.v10_levels |= 1u << G.depth;
  int total = make_room(G.depth, LEVEL_BUDGET);
  for (int b = total - 200; !ok && b > 0; b -= 200) { make_room(G.depth, b); ok = store_floor(); }   // storage full: the far floors give way
  if (!ok && G.n_heaps) LOG(C_WARN, "The dungeon has swallowed what was left on depth %d.", G.depth);
}
static bool load_level_record(int depth) {
  int raw = rec_read(depth);
  if (raw < 3) return false;
  s_old_rec = !(G.v9_levels & (1u << depth)); s_v10_rec = (G.v10_levels >> depth) & 1;
  unpack_level(s_buf + 2);
  s_old_rec = s_v10_rec = false;
  return true;
}
static void gas_store(int d) {
  bool any = false;
  for (int b = 0; b < BLOB_COUNT; b++) any |= g_blob[b] != NULL;
  if (!any) { if (persist_exists(KEY_GAS(d))) gas_forget(d); return; }
  if (!buf_alloc()) return;
  uint8_t *p = s_buf, *end = s_buf + GAS_MAX - 2;       // room kept for the two closing zeros
  for (int b = 0; b < BLOB_COUNT; b++) {
    const uint16_t *v = g_blob[b];
    if (!v || p + 1 >= end) continue;
    *p++ = b + 1;
    for (int c = 0; c < MAP_LEN; ) {
      if (!v[c]) { c++; continue; }
      int s = c;
      while (c < MAP_LEN && v[c] && c - s < 255) c++;
      int n = c - s;
      if (p + 4 + n >= end) { n = end - p - 4; if (n <= 0) break; }   // full: the rest is lost
      *p++ = n; *p++ = s & 0xFF; *p++ = s >> 8;
      for (int i = s; i < s + n; i++) { int a = v[i]; *p++ = a < 128 ? a : 128 + ((a - 96) / 64 > 127 ? 127 : (a - 96) / 64); }   // (to the nearest)
    }
    *p++ = 0;
  }
  *p++ = 0;
  int n = p - s_buf;
  if (!write_keys(KEY_GAS(d), s_buf, n, 2)) gas_forget(d);
  else if (n <= PERSIST_DATA_MAX_LENGTH) persist_delete(KEY_GAS(d) + 1);
  buf_release();
}
static void gas_load(int d) {
  int n = persist_exists(KEY_GAS(d)) ? persist_get_size(KEY_GAS(d)) : 0;
  if (n <= 0 || !buf_alloc()) return;
  persist_read_data(KEY_GAS(d), s_buf, n);
  if (n == PERSIST_DATA_MAX_LENGTH && persist_exists(KEY_GAS(d) + 1)) { int m = persist_get_size(KEY_GAS(d) + 1); if (m > 0) n += persist_read_data(KEY_GAS(d) + 1, s_buf + n, m); }
  const uint8_t *p = s_buf, *end = s_buf + n;
  while (p < end && *p && *p <= BLOB_COUNT) {
    int b = *p++ - 1;
    while (p + 3 <= end && *p) {
      int len = p[0], s = p[1] | p[2] << 8; p += 3;
      for (int i = 0; i < len && p < end; i++, p++) if (s + i < MAP_LEN && *p) blob_seed(b, s + i, *p < 128 ? *p : 128 + (*p - 128) * 64);
    }
    p++;
  }
  buf_release();
}
static void erase_level_records(void) { for (int d = 1; d <= 26; d++) forget_floor(d); }

static int pack(void) {
  if (!buf_alloc()) return 0;
  uint8_t *p = s_buf;
  uint32_t magic = SAVE_MAGIC;
  G.hero.spare[6] = G.health_pos >= 0 && G.health_pos == G.hero.pos;   // Sungrass.Health (it ends once the hero moves)
  PUT(p, magic); PUT(p, G.seed); PUT(p, G.depth); PUT(p, G.hero);
  PUT(p, G.now); PUT(p, G.turn_due); PUT(p, G.level_t0); PUT(p, G.max_depth); PUT(p, G.pos_count); PUT(p, G.sou_count);
  PUT(p, G.potion_flavor); PUT(p, G.scroll_flavor); PUT(p, G.known); PUT(p, G.next_uid);
  PUT(p, G.herbal_pos); PUT(p, G.herbal); PUT(p, G.charm_uid); PUT(p, G.deferred);
  PUT(p, G.wand_flavor); PUT(p, G.ring_flavor); PUT(p, G.regen_left);
  PUT(p, G.v9_levels); PUT(p, G.keys); PUT(p, G.dew); PUT(p, G.dflags); PUT(p, G.boss); PUT(p, G.q);
  PUT(p, G.v10_levels); PUT(p, G.weak_floors); PUT(p, G.soe_count);
  { uint8_t n = 0; for (int i = 0; i < 32; i++) n += G.pack[i].kind != 0;   // v11: the bags' items, (slot, item) pairs
    PUT(p, n); for (int i = 0; i < 32; i++) if (G.pack[i].kind) { uint8_t b = i; PUT(p, b); PUT(p, G.pack[i]); } }
  PUT(p, G.journal);                                     // v12
  PUT(p, G.fall); PUT(p, G.fall_d);                      // v13
  p = pack_level(p);
  return p - s_buf;
}
static int f_save_size(void) { int n = pack(), z = n > 0 ? zpack(s_buf, n, s_buf + n, SAVE_SPACE - n - 8) : 0; buf_release(); return z > 0 ? z : n; }   // as stored
// the save: KEY_LEN holds its length, negative when it is zero-run packed (0.15); the floors give way if storage is full
static void drop_keys(uint32_t key, int from) { for (int k = from; k < 10; k++) persist_delete(key + k); }
static void f_save(void) {
  if (G.hero.HP <= 0) return;
  int v = persist_exists(KEY_LEN) ? persist_read_int(KEY_LEN) : 0;
  int bank = (v < 0 ? -v : v) & SAVE_BANK ? 0 : SAVE_BANK;   // persist4: into the keys the last save is not in
  uint32_t key = bank ? KEY_DATA_B : KEY_DATA;
  bool ok = false;
  for (int tries = 0; tries < 26 && !ok; tries++) {
    int n = pack();
    if (n <= 0) break;
    int z = zpack(s_buf, n, s_buf + n, SAVE_SPACE - n - 8), len = z > 0 ? z : n;
    ok = len <= SAVE_BUF && write_keys(key, z > 0 ? s_buf + n : s_buf, len, 10) && persist_write_int(KEY_LEN, z > 0 ? -(len + bank) : len + bank) == 4;   // (more than ten keys: never)
    if (ok) { drop_keys(key, (len + PERSIST_DATA_MAX_LENGTH - 1) / PERSIST_DATA_MAX_LENGTH); drop_keys(bank ? KEY_DATA : KEY_DATA_B, 0); break; }   // the old keys
    int far = -1, fd = -99; uint32_t pm = precious();  // no room: forget the farthest other floor (precious() last), then try again
    for (int d = 1; d <= 26; d++) if (d != G.depth && (rec_head(d) || persist_exists(KEY_GAS(d)))) { int fr = (d > G.depth ? d - G.depth : G.depth - d) - (int)(pm >> d & 1) * 32; if (fr > fd) { fd = fr; far = d; } }
    if (far < 0) break;
    int raw = rec_read(far), things = raw > 0 ? rec_heaps(far, raw) : 0;
    forget_floor(far);
    if (things) LOG(C_WARN, "The dungeon has swallowed what was left on depth %d.", far);
  }
  if (!ok) drop_keys(key, 0);                          // no room even so: the last save stays, this half of one goes
  buf_release();
  gas_store(G.depth);                                  // 0.32: this floor's clouds
}
static void f_erase(void) { persist_delete(KEY_LEN); erase_level_records(); drop_keys(KEY_DATA, 0); drop_keys(KEY_DATA_B, 0); }
static bool read_save(void) {
  if (!persist_exists(KEY_LEN)) return false;
  int n = persist_read_int(KEY_LEN), m = n < 0 ? -n : n;
  uint32_t key = m & SAVE_BANK ? KEY_DATA_B : KEY_DATA;   // persist4: which keys hold it
  n = n < 0 ? -(m & ~SAVE_BANK) : m & ~SAVE_BANK;
  if (!buf_alloc()) return false;
  if (n < 0) {                                         // packed: read to the block's end, unpacked from its start
    n = -n;
    if (n > SAVE_SPACE || !read_keys(key, s_buf + SAVE_SPACE - n, n)) return false;
    n = zunpack(s_buf + SAVE_SPACE - n, n, s_buf, SAVE_SPACE - n);
    if (n < 9) return false;
  } else if (n == 0 || n > SAVE_BUF || !read_keys(key, s_buf, n)) return false;
  uint32_t magic; memcpy(&magic, s_buf, 4);
  return magic == SAVE_MAGIC;   // 0.9 builds floors with special rooms: saves from older builds would not match their floors
}
static bool f_has_save(uint8_t *depth) {
  if (!read_save()) { buf_release(); return false; }
  *depth = s_buf[8];
  buf_release();
  return true;
}
static bool f_peek_save(uint8_t *depth, uint8_t *lvl) {
  if (!read_save()) { buf_release(); return false; }
  Hero h;
  *depth = s_buf[8];
  memcpy(&h, s_buf + 9, sizeof h);
  *lvl = h.lvl | (h.hclass & 3) << 6;                  // the class in the top bits (the select screen's avatar)
  buf_release();
  return true;
}

// ---------------- Floors ----------------
// 0.29: out of the app (only this overlay builds floors)
static bool level_build(uint8_t depth, int16_t *tombs, int *n_tombs, uint32_t content_seed) {
  int reg = depth <= 5 ? 0 : depth <= 10 ? 1 : depth <= 15 ? 2 : depth <= 20 ? 3 : 4;   // the band's creatures (Bestiary)
  mreg_load(-1);                                       // room for the generator: the region's creatures come back for the content
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_LEVEL);
  if (!o) { mreg_load(reg); return false; }
  ((void (*)(uint8_t, int16_t *, int *))o->fn[0])(depth, tombs, n_tombs);
  ovl_free(o);
  mreg_load(reg);
  if (!content_seed) return true;
  o = ovl_load(RESOURCE_ID_OVL_CONTENT);
  if (!o) return true;                                 // an empty floor rather than none
  ((void (*)(const int16_t *, int, uint32_t))o->fn[0])(tombs, *n_tombs, content_seed);
  ovl_free(o);
  return true;
}
static void level_fallback(void) {
  memset(G.map, T_WALL, MAP_LEN);
  G.feeling = FEEL_NONE; G.secret_doors = 0;
  G.n_rooms = 1; G.rooms[0] = (Room){ 4, 4, 12, 12, RT_STANDARD, -1 };
  for (int y = 5; y <= 11; y++) for (int x = 5; x <= 11; x++) G.map[y * MAP_W + x] = T_EMPTY;
  G.entrance = 5 * MAP_W + 5; G.exit = 11 * MAP_W + 11;
  G.map[G.entrance] = T_ENTRANCE; G.map[G.exit] = T_EXIT;
}
static void build_floor(int depth, bool fresh) {
  static int16_t tombs[8]; int n_tombs = 0;
  G.depth = depth;
  blob_free_all();
  memset(G.visited, 0, sizeof G.visited);
  G.n_diffs = 0; G.n_mobs = 0; G.n_heaps = 0; G.n_plants = 0; G.health_pos = -1; memset(G.lvars, 0, sizeof G.lvars);
  g_rng = mix(G.seed, depth, 0x51);                    // the floor's shape: same every visit; its first content too
  if (!level_build(depth, tombs, &n_tombs, fresh ? mix(G.seed, depth, 0xA7) : 0)) {
    extern int g_ovl_need, g_ovl_big;
    n_tombs = 0; level_fallback(); LOG(C_BAD, "The floor could not be generated: out of memory (%d/%d).", g_ovl_big, g_ovl_need);
  }
  build_los();
  for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_HIGH_GRASS || G.map[c] == T_GRASS) BIT_SET(G.hg, c); else BIT_CLR(G.hg, c);
}
static void arrive_messages(bool back) {
  if (back) LOG(C_INFO, txt(TX_WELCOME_BACK_TO_THE), G.depth);
  else LOG(C_INFO, txt(TX_WELCOME_TO_THE_LEVEL), G.depth);
  switch (G.feeling) {
    case FEEL_CHASM: ui_log("Your steps echo across the dungeon.", C_WARN); break;
    case FEEL_WATER: ui_log(txt(TX_YOU_HEAR_THE_WATER), C_WARN); break;
    case FEEL_GRASS: ui_log(txt(TX_THE_SMELL_OF_VEGETATION), C_WARN); break;
  }
  if (G.secret_doors > rnd_range(3, 4)) ui_log(txt(TX_THE_ATMOSPHERE_HINTS_THAT), C_WARN);
}
// GameScene.create / Dungeon.switchLevel: night mode (before 7 am), and with the Amulet every creature comes for you
static void scene_create(bool arrived) {
  time_t now = time(NULL); struct tm *t = localtime(&now);
  G.night = t->tm_hour < 7;
  if (G.dflags & DF_AMULET) for (int i = 0; i < G.n_mobs; i++)   // (NPC.beckon, Statue.beckon: nothing)
    if (G.mobs[i].HP > 0 && !ally(i) && !(MOB_DEFS[G.mobs[i].type].flags & MD_PASSIVE)) mob_beckon(i, G.hero.pos);
  if (arrived && G.night && !boss_depth(G.depth)) ui_log(txt(TX_L_NIGHT), C_WARN);
}
// 0.34 (kinder than PD): what a creature held when it went for good without dying (an Ankh's floor reset, a fall into a
// chasm...) is left lying somewhere on its floor, so a stolen Amulet can't be lost (PD loses it with the thief)
static void orphans(void) {
  for (int h = G.n_heaps - 1; h >= 0; h--) {
    int p = G.heaps[h].pos, i = 0;
    if (G.heaps[h].type != HEAP_CARRIED || (p & 0xFF00) != 0x4000) continue;   // CARRIED_POS(uid) (BAG_POS is 0x41xx)
    while (i < G.n_mobs && !(G.mobs[i].HP > 0 && G.mobs[i].uid == (p & 0xFF))) i++;
    if (i < G.n_mobs) continue;
    Item it = G.heaps[h].item; heap_remove(h);
    int c = random_respawn_cell();
    heap_put(c >= 0 ? c : G.entrance, it);
  }
}
static void level_enter(int depth, int arrive);
static bool s_into_pit;                                // InterlevelScene.fallIntoPit: fell through a weak floor room
static void level_go(int depth, int arrive) {          // InterlevelScene descend / ascend / fall
  s_into_pit = false;
  if (arrive == ARRIVE_FALL) for (int r = 0; r < G.n_rooms; r++) {
    const Room *R = &G.rooms[r]; int x = G.hero.pos % MAP_W, y = G.hero.pos / MAP_W;
    if (R->type == RT_WEAK_FLOOR && x > R->left && x < R->right && y > R->top && y < R->bottom) s_into_pit = true;
  }
  ui_fx(FX_LEAVE, 0, -1, NULL);
  save_level_record(); buf_release();
  gas_store(G.depth);                                  // 0.32: the clouds stay with the floor (Level.blobs)
  level_enter(depth, arrive);
}
static void level_enter(int depth, int arrive) {
  bool known = depth <= G.max_depth;
  build_floor(depth, false);
  bool loaded = known && load_level_record(depth);
  buf_release();
  if (!loaded) { build_floor(depth, true); gas_forget(depth); }
  else { gas_load(depth); build_los(); }                // persist4: the record's doors, grass, holes block sight as they are now
  rnd_reseed_runtime();
  orphans();
  if (depth > G.max_depth) G.max_depth = depth;
  Hero *h = &G.hero;
  h->pos = arrive == ARRIVE_ENTRANCE ? G.entrance : arrive == ARRIVE_EXIT ? G.exit : arrive >= ARRIVE_CELL ? arrive - ARRIVE_CELL : -1;
  memset(G.fov, 0, sizeof G.fov);
  if (arrive == ARRIVE_FALL) {                         // Chasm.heroLand
    int c = -1;
    if (s_into_pit) for (int r = 0; r < G.n_rooms && c < 0; r++) if (G.rooms[r].type == RT_PIT) c = level_room_random(&G.rooms[r], 0);   // Level.pitCell
    if (c < 0) c = random_respawn_cell();
    h->pos = c >= 0 ? c : G.exit;                      // Dungeon.switchLevel: pos -1 -> level.exit
  }
  G.action = HA_NONE; G.last_action = HA_NONE; G.last_target = -1; G.quick_uid = -1; G.seen_mobs = 0;
  h->time = G.now; G.level_t0 = G.turn_due - TICK;   // respawner acts on arrival, then every 50 turns
  update_fov();
  arrive_messages(depth < G.max_depth);                // GameScene.create: back only above the deepest floor
  if (arrive == ARRIVE_FALL) {
    if (!h->b.cripple) ui_log("You are crippled!", C_WARN);   // Hero.add(Cripple)
    if (h->b.cripple < 10) h->b.cripple = 10;          // Buff.prolong(Cripple.DURATION)
    hero_damage(rnd_range(h->HT / 3, h->HT / 2), "Fell to death on level %d");
    if (h->HP <= 0) ui_log("You fell to death...", C_BAD);
  }
  if (depth >= G.max_depth) ui_sound(SND_DESCEND);     // GameScene: with "Welcome to the level"
  for (int k = 1; k < 5; k++) if (depth == "\1\6\13\20\26"[k]) {   // WndStory.showChapter: on the first arrival by the stairs down
    int bit = 8 << k;                                  // DF_CHAPTER(k): fallen in first, the chapter still to come
    if (arrive == ARRIVE_FALL) { if (!known) G.dflags |= bit; }
    else if (arrive == ARRIVE_ENTRANCE && (!known || (G.dflags & bit))) { G.dflags &= ~bit; ui_message(NULL, txt(TX_STORY_0 + k)); }
  }
  ui_fx(FX_DESCEND, 0, -1, NULL);
  respawn(); update_fov();                             // (sight takes in what the respawner just placed, as a reload's GameScene.create would)
  for (int k = 0; k < 4; k++) if (G.fall[k].kind && G.fall_d[k] == depth) {   // InterlevelScene: what fell from above lands somewhere
    int c = random_respawn_cell(); Item it = G.fall[k]; G.fall[k] = (Item){ 0 };
    if (c < 0) c = G.entrance;
    if (item_cat(it.kind) == CAT_POTION) potion_shatter(it.kind, c); else if (item_cat(it.kind) == CAT_SEED) land_ovl(it, c); else heap_put(c, it);   // potions shatter, seeds take root
  }
  scene_create(true);
  G.seen_mobs = 0; check_visible_mobs(); G.interrupted = false;
  g_level_changed = true;
}

// ---------------- Ankh: InterlevelScene.resurrect, Hero.resurrect, Belongings.resurrect (0.12) ----------------
static bool unique_kind(int k) {                       // Item.unique: kept through a resurrection
  return k == IK_DWARF_TOKEN || k == IK_ROSE || k == IK_PHANTOM_FISH || k == IK_PICKAXE || k == IK_DARK_GOLD || k == IK_RAT_SKULL ||
         k == IK_CORPSE_DUST || k == IK_MASTERY || k == IK_AMULET || k == IK_DEW_VIAL || k == IK_BEACON || k == IK_ARMOR_KIT;
}
static void f_resurrect(void) {
  Hero *h = &G.hero;
  int d = G.depth;
  bool boss = boss_depth(d);
  h->HP = h->HT; h->gold = 0; h->exp = 0;
  for (int i = 0; i < BAG_SIZE; i++) if (!unique_kind(h->bag[i].kind)) h->bag[i] = (Item){ 0 };
  memset(G.pack, 0, sizeof G.pack);                    // the bags went too, with all they held
  if (boss) G.keys[d] = 0;                             // this floor's keys
  h->weapon.flags &= ~IF_CURSED; h->armor.flags &= ~IF_CURSED; h->ring[0].flags &= ~IF_CURSED; h->ring[1].flags &= ~IF_CURSED;
  memset(&h->b, 0, sizeof h->b);                       // Char.die (Char.onRemove) detached every buff; Hero.live: a new
  h->ooze = h->weakness = h->vertigo = h->light = h->combo = h->fury = 0; h->spare[0] = 0;   // Regeneration and Hunger (level 0)
  h->hunger = 0; G.herbal = 0; G.deferred = 0; G.health_pos = -1;   // Light, Combo, SnipersMark, Earthroot, Viscosity, Sungrass
  G.action = HA_NONE;
  ui_fx(FX_LEAVE, 0, -1, NULL);
  if (boss) {                                          // a boss floor: a new floor above, the boss's floor made anew
    static const uint8_t BITS[6] = { 0, 1, 6, 8, 16, 96 };
    G.boss &= ~BITS[d / 5];
    forget_floor(d);                                   // depth--, Dungeon.newLevel() (depth++): the boss floor made anew
    level_enter(d, ARRIVE_ENTRANCE);
    return;
  }
  // Dungeon.resetLevel: Level.reset keeps what resets (NPCs, piranhas, statues, mimics, wraiths, the rat king), then createMobs
  for (int i = 0; i < G.n_mobs; i++) {
    int t = G.mobs[i].type;
    if (G.mobs[i].HP > 0 && !(MOB_DEFS[t].flags & MD_NPC) && t != MT_PIRANHA && t != MT_STATUE && t != MT_MIMIC && t != MT_WRAITH && t != MT_RAT_KING) {
      G.mobs[i].HP = 0;                                // (removed, not killed: PD loses what it carried; 0.34: orphans() keeps it)
    }
  }
  orphans();
  blob_free_all();
  h->pos = G.entrance;
  memset(G.fov, 0, sizeof G.fov);
  int n = 2 + d % 5 + rnd_int(3);                      // RegularLevel.nMobs
  for (int i = 0; i < n; i++) { int c = random_respawn_cell(); if (c >= 0) spawn_mob(bestiary_mob(d), c, ST_SLEEPING, 0); }
  h->time = G.now;
  update_fov();
  ui_fx(FX_DESCEND, 0, -1, NULL);
  G.seen_mobs = 0; check_visible_mobs(); G.interrupted = false;
  g_level_changed = true;
}


static bool f_load(void) {
  s_pend_depth = 0;
  if (!read_save()) { buf_release(); return false; }
  blob_free_all();
  memset(&G, 0, sizeof G);
  const uint8_t *p = s_buf + 4;
  uint8_t depth;
  uint32_t magic; memcpy(&magic, s_buf, 4);
  bool v13 = magic == SAVE_MAGIC || magic == SAVE_MAGIC_V13, v12 = v13 || magic == SAVE_MAGIC_V12, v11 = v12 || magic == SAVE_MAGIC_V11, v10 = v11 || magic == SAVE_MAGIC_V10, v9 = v10 || magic == SAVE_MAGIC_V9, v8 = v9 || magic == SAVE_MAGIC_V8;
  GET(p, G.seed); GET(p, depth);
  if (v9) GET(p, G.hero);
  else if (v8) { memcpy(&G.hero, p, offsetof(Hero, ooze)); p += HERO_V8_SIZE; }      // v8 heroes end before the v9 extras
  else { memcpy(&G.hero, p, offsetof(Hero, ring)); p += HERO_V7_SIZE; }   // older heroes end before the rings
  GET(p, G.now); GET(p, G.turn_due); GET(p, G.level_t0); GET(p, G.max_depth); GET(p, G.pos_count); GET(p, G.sou_count);
  GET(p, G.potion_flavor); GET(p, G.scroll_flavor);
  if (v8) GET(p, G.known); else { memcpy(G.known, p, 8); p += 8; }
  GET(p, G.next_uid);
  if (magic != SAVE_MAGIC_V6) { GET(p, G.herbal_pos); GET(p, G.herbal); GET(p, G.charm_uid); GET(p, G.deferred); }
  if (v8) { GET(p, G.wand_flavor); GET(p, G.ring_flavor); GET(p, G.regen_left); }
  if (v9) { GET(p, G.v9_levels); GET(p, G.keys); GET(p, G.dew); GET(p, G.dflags); GET(p, G.boss); GET(p, G.q); }
  if (v10) { GET(p, G.v10_levels); GET(p, G.weak_floors); GET(p, G.soe_count); }
  if (v11) { uint8_t n; GET(p, n); for (int i = 0; i < n; i++) { uint8_t b; GET(p, b); GET(p, G.pack[b & 31]); } }
  if (v12) GET(p, G.journal);
  if (v13) { GET(p, G.fall); GET(p, G.fall_d); }
  int off = p - s_buf;
  buf_release();                                // the generator needs the room: the save is read again after
  build_floor(depth, false);                    // regenerate the floor from the seed...
  if (!read_save()) { buf_release(); return false; }
  s_old_rec = !v9; s_v10_rec = v10;
  unpack_level(s_buf + off);                    // ...then apply what happened on it
  s_old_rec = s_v10_rec = false;
  buf_release();
  gas_load(depth);                                     // 0.32: the clouds as they were saved
  build_los();
  rnd_reseed_runtime();
  orphans();                                           // (an older save: a thief lost on a floor reset)
  G.health_pos = G.hero.spare[6] ? G.hero.pos : -1;
  G.last_target = -1; G.quick_uid = -1;
  if (G.hero.time < G.now) G.hero.time = G.now;
  for (int i = BAG_SIZE; i < BAG_CAP; i++) G.hero.bag[i] = (Item){ 0 };
  update_fov();
  scene_create(false);
  check_visible_mobs(); G.interrupted = false;
  return true;
}


// ---------------- Rankings.submit ----------------
static void bones_leave(void) {                        // Bones.leave: one of the hero's things (or some gold) for the next one
  Item *g[4] = { &G.hero.weapon, &G.hero.armor, &G.hero.ring[0], &G.hero.ring[1] };
  Item it = *g[rnd_int(4)];
  if (!it.kind) { it = item_make(IK_GOLD, 1); it.qty = G.hero.gold > 0 ? rnd_range(1, G.hero.gold) : 1; }
  uint8_t b[1 + sizeof(Item)]; b[0] = G.depth; memcpy(b + 1, &it, sizeof it);
  persist_write_data(KEY_BONES, b, sizeof b);
}
static void f_rank(bool win) {
  if (!win) {                                          // Hero.reallyDie: the whole floor is revealed (persist4: not when an Ankh
    for (int c = 0; c < MAP_LEN; c++) {                // saves the hero: that floor is played on, and its secrets came back on a load)
      if (!discoverable(c)) continue;
      BIT_SET(G.visited, c);
      if (TFLAGS(G.map[c]) & F_SECRET) G.map[c] = terrain_discover(G.map[c]);
    }
    build_los(); update_fov();
    bones_leave();
  }
  Item *gear[4] = { &G.hero.weapon, &G.hero.armor, &G.hero.ring[0], &G.hero.ring[1] };   // Belongings.identify
  for (int i = 0; i < 4; i++) gear[i]->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN;
  ALL_SLOTS(s) { Item *it = game_slot(s); if (it->kind) { it->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN; set_known(it->kind); } }
  for (int i = 0; i < 4; i++) if (gear[i]->kind) set_known(gear[i]->kind);
  if (!buf_alloc()) return;
  Ranks *k = (Ranks *)s_buf;
  memset(k, 0, sizeof *k);
  if (persist_exists(KEY_RANKS)) read_keys(KEY_RANKS, s_buf, sizeof *k);
  Rank rec = { 0 };
  uint32_t gold; memcpy(&gold, G.hero.spare + 2, 4);   // Statistics.goldCollected
  rec.score = (gold + G.hero.lvl * G.max_depth * 100) * (win ? 2 : 1);
  rec.cls = G.hero.hclass & 3; rec.win = win;
  const char *d = win ? "Obtained the Amulet of Yendor" : G.death;
  if (strlen(d) >= sizeof rec.info && !strncmp(d, "Killed by ", 10)) d += 10;   // too long: the depth must stay readable
  snprintf(rec.info, sizeof rec.info, "%s", d);
  int at = k->n;
  while (at > 0 && k->r[at - 1].score < rec.score) at--;   // scoreComparator: best first, ties keep the older above
  if (at >= RANK_N) at = RANK_N - 1;                    // a new record that doesn't make the table replaces the last
  int last = k->n < RANK_N ? k->n : RANK_N - 1;
  memmove(&k->r[at + 1], &k->r[at], (last - at) * sizeof(Rank));
  k->r[at] = rec;
  if (k->n < RANK_N) k->n++;
  k->latest = at; k->total++; if (win) k->won++;
  write_keys(KEY_RANKS, s_buf, sizeof *k, 2);
  buf_release();
}

#ifdef SAVE_ONLY
// 0.20: the "save" overlay (src/ovl/save) is this file with only the save: a much smaller block to find than the whole floor code
static int save_dispatch(int op, int a, void *p) {
  if (op == FO_SAVE) { f_save(); return 1; }
  if (op == FO_PEEK) { uint8_t *b = p, d = 0, l = 0; bool ok = f_peek_save(&d, &l); b[0] = d; b[1] = l; return ok; }
  if (op == FO_RANK) { f_rank(a); return 1; }          // 0.30: the rankings and erasing live here too (the floor overlay is full)
  if (op == FO_ERASE) { f_erase(); return 1; }
  return f_save_size();
}
OVL_ENTRY(save, (void *)save_dispatch);
#else
static int floor_dispatch(int op, int a, void *p) {
  switch (op) {
    case FO_BUILD: build_floor(a, p != NULL); return 1;   // the new game overlay's first floor
    case FO_LOAD: return f_load();
    case FO_GO: level_go(a, (int)(intptr_t)p); return 1;   // FO_SAVE, FO_SAVE_SIZE, FO_PEEK, FO_RANK, FO_ERASE: the save overlay
    case FO_RESURRECT: f_resurrect(); return 1;
  }
  return 0;
}
OVL_ENTRY(floor, (void *)floor_dispatch);
#endif
