/*
 * Pixel Dungeon for Pebble - game rules.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Actor time, Char.attack/hit/damage, Hero (actions, search,
 * awareness, experience, strength/encumbrance), Mob AI (sleeping / wandering / hunting / fleeing),
 * Rat, Gnoll, Crab, MirrorImage, Bestiary, the buffs, Blob / ToxicGas / ParalyticGas / Fire / Freezing,
 * the traps, Chasm, Door, HighGrass, Sign, Heap (chests, remains), PathFinder, ShadowCaster.
 * Items live in items.c; items' effects (potions, scrolls, food, equipment) are here.
 * Time is integer: one PD turn = TICK units.
 */
#include "pd.h"
#include <stddef.h>

Game *g_game;                                        // the whole game state, on the heap (see game_init)

#include "game_int.h"
const int16_t N8[8] = { -1, 1, -MAP_W, MAP_W, -MAP_W - 1, -MAP_W + 1, MAP_W - 1, MAP_W + 1 };  // PathFinder order
char g_log[120];
PickMode g_pick_mode; bool g_pick_unknown; int g_reforge_slot = -1; bool g_level_changed;

uint32_t mix(uint32_t a, uint32_t b, uint32_t salt) {
  uint32_t h = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ salt * 0xC2B2AE3Du;
  h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12;
  return h;
}
void rnd_reseed_runtime(void) { g_rng ^= (uint32_t)time(NULL) * 2654435761u ^ (uint32_t)time_ms(NULL, NULL); }

// ---------------- Monsters (actors/mobs) ----------------
const MobDef *MOB_DEFS;                         // tools/mobs.py -> resources/data/mobs.bin, loaded by game_init
static const char *const VERBS[] = MOB_VERBS;
const char *mob_name(int type) {
  if (G.depth != G.max_depth) switch (type) {          // Goo / Tengu / DM300 / King / Yog: name = depth == deepestFloor ? ...
    case MT_GOO: return "spawn of Goo";
    case MT_TENGU: return "memory of Tengu";
    case MT_DM300: return "DM-350";
    case MT_KING: return "undead King of Dwarves";
    case MT_YOG: return "echo of Yog-Dzewa";
  }
  return txt(MOB_DEFS[type].name);
}
const uint8_t BEST_KIND[] = { MT_RAT, MT_RAT, MT_GNOLL, MT_RAT, MT_GNOLL, MT_CRAB, MT_SWARM, MT_RAT, MT_GNOLL, MT_CRAB, MT_SWARM, MT_SKELETON, MT_THIEF, MT_GOO, MT_SKELETON, MT_THIEF, MT_SWARM, MT_SHAMAN, MT_SKELETON, MT_SHAMAN, MT_THIEF, MT_SWARM, MT_SKELETON, MT_SHAMAN, MT_GNOLL, MT_THIEF, MT_SWARM, MT_BAT, MT_SKELETON, MT_SHAMAN, MT_THIEF, MT_SWARM, MT_BAT, MT_BRUTE, MT_TENGU, MT_BAT, MT_BRUTE, MT_BAT, MT_BRUTE, MT_SPINNER, MT_BAT, MT_BRUTE, MT_SHAMAN, MT_SPINNER, MT_ELEMENTAL, MT_BAT, MT_BRUTE, MT_SHAMAN, MT_SPINNER, MT_ELEMENTAL, MT_MONK, MT_DM300, MT_ELEMENTAL, MT_WARLOCK, MT_MONK, MT_ELEMENTAL, MT_MONK, MT_WARLOCK, MT_ELEMENTAL, MT_MONK, MT_GOLEM, MT_WARLOCK, MT_ELEMENTAL, MT_MONK, MT_GOLEM, MT_WARLOCK, MT_SUCCUBUS, MT_KING, MT_EYE, MT_SUCCUBUS, MT_EYE, MT_SUCCUBUS, MT_EYE, MT_SCORPIO, MT_SUCCUBUS, MT_EYE, MT_SCORPIO, MT_YOG };
const uint16_t BEST_W[] = { 100, 100, 100, 100, 200, 100, 2, 100, 200, 300, 2, 1, 1, 100, 400, 200, 100, 20, 300, 100, 100, 100, 300, 200, 100, 100, 100, 2, 300, 300, 100, 100, 2, 1, 100, 100, 20, 100, 100, 20, 100, 300, 100, 100, 2, 100, 300, 100, 400, 2, 1, 100, 100, 100, 20, 100, 100, 100, 100, 200, 100, 100, 100, 200, 300, 100, 2, 100, 100, 100, 100, 100, 200, 100, 100, 200, 300, 100 };
const uint8_t BEST_OFF[26] = { 0, 1, 3, 7, 13, 14, 18, 22, 28, 34, 35, 37, 40, 45, 51, 52, 55, 58, 62, 67, 68, 69, 71, 74, 77, 78 };
int bestiary(int depth) {                       // Bestiary.mutable: mobClass, and 1 in 30 a rare kind
  int d = depth < 1 ? 1 : depth > 25 ? 21 : depth;
  int o = BEST_OFF[d - 1], n = BEST_OFF[d] - o;
  int t = BEST_KIND[o + rnd_chances(BEST_W + o, n)];
  if (rnd_int(30) == 0) switch (t) {
    case MT_RAT: t = MT_ALBINO; break;
    case MT_THIEF: t = MT_BANDIT; break;
    case MT_BRUTE: t = MT_SHIELDED; break;
    case MT_MONK: t = MT_SENIOR; break;
    case MT_SCORPIO: t = MT_ACIDIC; break;
  }
  return t;
}
int region_of(int d) { return d <= 5 ? 0 : d <= 10 ? 1 : d <= 15 ? 2 : d <= 21 ? 3 : 4; }   // tilesTex (the last shop wears the city's)
bool boss_depth(int d) { return d == 5 || d == 10 || d == 15 || d == 20 || d == 25; }
bool ally(int i) { int t = G.mobs[i].type; return t == MT_MIRROR || t == MT_SHEEP || t == MT_BEE || (MOB_DEFS[t].flags & MD_NPC); }   // not an enemy
bool hero_side(int i) { int t = G.mobs[i].type; return t == MT_MIRROR || t == MT_BEE; }   // fights the hero's enemies
MobHooks g_mh;                                  // the "mobs" overlay's creature behaviours (all NULL: plain Mob)
int g_dmg_src;                                  // what the next mob_damage comes from (DS_*): resistances / immunities
bool mob_immune(int i, uint32_t f) { return i >= 0 && (MOB_DEFS[G.mobs[i].type].flags & f); }
int mob_sprite_type(int i) { return G.mobs[i].type; }

// ---------------- Level state helpers ----------------
int mob_at(int16_t c) {
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].pos == c && G.mobs[i].HP > 0) return i;
  return -1;
}
int heap_at(int16_t c) {
  for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].pos == c && G.heaps[i].type != HEAP_HIDDEN) return i;   // hidden: not there until searched out
  return -1;
}
bool char_at(int c) { return (c == G.hero.pos && G.hero.HP > 0) || mob_at(c) >= 0; }
static bool stacks(const Item *a, const Item *b) { return a->kind == b->kind && item_stackable(a->kind); }

void heap_put_t(int16_t c, Item it, int type) { // Level.drop: stack, else spill to a free neighbour
  for (int tries = 0; tries < 9; tries++) {
    int16_t cell = tries == 0 ? c : c + N8[tries - 1];
    if (!in_map(cell) || (tries && !nb_ok(c, cell)) || !(PASSABLE(G.map[cell]) || (TFLAGS(G.map[cell]) & F_AVOID))) continue;
    if (G.map[cell] == T_ALCHEMY && item_cat(it.kind) != CAT_SEED) continue;   // Level.drop: only seeds go in the pot
    if (TFLAGS(G.map[cell]) & F_PIT) {                 // Dungeon.dropToChasm: it lands on the floor below
      if (!tries && type == HEAP_PLAIN && G.depth < LAST_DEPTH) for (int k = 0; k < 4; k++) if (!G.fall[k].kind) { G.fall[k] = it; G.fall_d[k] = G.depth + 1; return; }
      continue;
    }
    int h = heap_at(cell);
    if (h >= 0 && G.heaps[h].type == HEAP_PLAIN && type == HEAP_PLAIN && stacks(&G.heaps[h].item, &it) && G.heaps[h].item.qty + it.qty < 1000) { G.heaps[h].item.qty += it.qty; return; }
    if (h < 0 && G.n_heaps < MAX_HEAPS) { G.heaps[G.n_heaps++] = (Heap){ cell, type, it }; return; }
  }
  if (G.n_heaps < MAX_HEAPS && in_map(c) && !(TFLAGS(G.map[c]) & F_PIT)) G.heaps[G.n_heaps++] = (Heap){ c, type, it };   // no free cell around: pile it here
}
void heap_put(int16_t c, Item it) { heap_put_t(c, it, HEAP_PLAIN); }
void level_drop(int16_t c, Item it) { heap_put(c, it); level_press_nobody(c); }   // Level.drop: the cell feels it
void heap_remove(int h) { G.heaps[h] = G.heaps[--G.n_heaps]; }
int gold_amount(void) { return rnd_int2(20 + G.depth * 10, 40 + G.depth * 20); }   // Gold.random

void build_los(void) {
  for (int i = 0; i < MAP_LEN; i++)
    if (TFLAGS(G.map[i]) & F_LOS_BLOCKING) BIT_SET(G.los, i); else BIT_CLR(G.los, i);
}
// Level.set: one cell; sight is recomputed only when the change can affect it
void set_terrain(int16_t c, uint8_t t) {
  bool los_was = BIT_GET(G.los, c);
  G.map[c] = t;
  if (TFLAGS(t) & F_LOS_BLOCKING) BIT_SET(G.los, c); else BIT_CLR(G.los, c);
  int i;
  for (i = 0; i < G.n_diffs; i++) if (G.diffs[i].pos == c) break;
  if (i == G.n_diffs && BIT_GET(G.hg, c) && (t == T_GRASS || t == T_HIGH_GRASS || t == T_EMBERS)) {}   // vegetation: the hg bits keep it
  else if (i < G.n_diffs) G.diffs[i].terrain = t;
  else if (G.n_diffs < MAX_DIFFS) G.diffs[G.n_diffs++] = (Diff){ c, t };
  else if (t != T_GRASS && t != T_HIGH_GRASS && t != T_EMBERS)   // full: give up a vegetation record first
    for (int k = 0; k < G.n_diffs; k++) if (G.diffs[k].terrain == T_GRASS || G.diffs[k].terrain == T_HIGH_GRASS || G.diffs[k].terrain == T_EMBERS) { G.diffs[k] = (Diff){ c, t }; break; }
  if (los_was != BIT_GET(G.los, c) && BIT_GET(G.fov, c)) update_fov();
}
static int water_tile(int c) {                         // Level.getWaterTile
  static const int8_t DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };
  int t = T_WATER_TILES;
  for (int j = 0; j < 4; j++) {
    int x = cx(c) + DX[j], y = cy(c) + DY[j];
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || (TFLAGS(G.map[x + y * MAP_W]) & F_UNSTITCHABLE)) t += 1 << j;
  }
  return t;
}
void level_destroy(int c) {                     // Level.destroy: what fire leaves behind
  if (!(TFLAGS(G.map[c]) & F_UNSTITCHABLE)) { set_terrain(c, T_EMBERS); return; }
  bool flood = false;
  for (int k = 0; k < 4; k++) { int n = c + (int16_t[]){ -MAP_W, 1, MAP_W, -1 }[k]; if (nb_ok(c, n) && IS_WATER(G.map[n])) flood = true; }
  set_terrain(c, flood ? water_tile(c) : T_EMBERS);
}

// ---------------- Rings (Ring.RingBuff and where PD reads them) ----------------
int ring_sum(int k) {                                   // levels of every worn ring of a kind (Accuracy, Haste, ...)
  int s = 0;
  for (int i = 0; i < 2; i++) if (G.hero.ring[i].kind == k) s += ring_level(&G.hero.ring[i]);
  return s;
}
bool ring_worn(int k, int *lvl) {                       // Hero.buff(...): the first such ring (Power, Elements, Thorns)
  for (int i = 0; i < 2; i++) if (G.hero.ring[i].kind == k) { if (lvl) *lvl = ring_level(&G.hero.ring[i]); return true; }
  return false;
}
int scale_pow(int v, int num, int den, int n) {        // v * (num / den)^n
  for (; n > 0; n--) v = v * num / den;
  for (; n < 0; n++) v = v * den / num;
  return v;
}
int elem_duration(int who, int d) {                     // RingOfElements.Resistance.durationFactor: (2 + L/2) / (2 + L)
  int l;
  if (who >= 0 || !ring_worn(IK_RNG_ELEMENTS, &l) || l < 0) return d;
  return d * (4 + l) / (4 + 2 * l);
}
int elem_resist(int who, int dmg) {              // Char.damage: a resisted element does 0..dmg
  int l;
  if (who >= 0 || !ring_worn(IK_RNG_ELEMENTS, &l) || rnd_int(l + 3) < 3) return dmg;
  return rnd_range(0, dmg);
}

// ---------------- Hero numbers ----------------
int hero_max_exp(void) { return 5 + G.hero.lvl * 5; }
int hero_tier(void) { return G.hero.armor.kind ? item_tier(G.hero.armor.kind) : 0; }
static bool hero_paralysed(void) { return G.hero.b.paralysis || G.hero.b.frost; }
bool hero_flying(void) { return G.hero.b.levitation > 0; }
static int hero_awareness(void) {                      // Hero.awareness x1000 (updateAwareness): 1 - 0.9 (rogue 0.85) ^ ((1 + lvl) / 2)
  static const uint16_t A[2][10] = { { 100, 100, 146, 190, 232, 271, 308, 344, 377, 410 }, { 150, 150, 216, 278, 334, 386, 434, 478, 519, 556 } };
  return A[G.hero.hclass == 2][G.hero.lvl < 9 ? G.hero.lvl : 9];
}
#define HUNTRESS (G.hero.hclass == 3)
int hero_str(void) { return G.hero.weakness ? G.hero.STR - 2 : G.hero.STR; }   // Hero.STR: Weakness takes 2
static int armor_enc(void) { return G.hero.armor.kind ? item_str(&G.hero.armor) - hero_str() : 0; }
// Weapon.acuracyFactor (x1000): ACU / 1.5^encumbrance; missiles: warriors +3 encumbrance, huntresses -2
static int acu_factor(const Item *w) {
  if (!w || !w->kind) return 1000;
  int enc = item_str(w) - hero_str() + (item_cat(w->kind) == CAT_MISSILE ? (G.hero.hclass == 0 ? 3 : HUNTRESS ? -2 : 0) : 0);
  int f = weapon_acu(w->kind) * 10;
  for (int i = 0; i < enc && i < 8; i++) f = f * 2 / 3;
  return w->flags & IF_ACCURATE ? f * 3 / 2 : f;      // Imbue.ACCURACY
}
int hero_attack_skill(const Item *ranged, int target_pos) {   // Hero.attackSkill
  int s = scale_pow(G.hero.attack_skill, 14, 10, ring_sum(IK_RNG_ACCURACY)) * acu_factor(ranged ? ranged : &G.hero.weapon) / 1000;
  if (ranged && distance(G.hero.pos, target_pos) == 1) s /= 2;
  return s;
}
static int hero_defense_skill(void) {                  // Hero.defenseSkill
  int e = armor_enc(), rogue = G.hero.hclass == 2 && e <= 0, ev = rogue ? -e : 0;   // a rogue dodges better in light armour
  if (rogue && G.hero.subclass == SUB_FREERUNNER && G.action != HA_NONE && G.action != HA_REST && !game_starving()) ev = G.hero.defense_skill + 2 * ev;   // and twice as well running
  int d = scale_pow(G.hero.defense_skill + ev, 12, 10, ring_sum(IK_RNG_EVASION));   // Ring of Evasion: 1.2^level
  if (hero_paralysed()) d /= 2;
  for (int i = 0; i < e && i < 8; i++) d = d * 2 / 3;
  return d;
}
int hero_dr(void) { return (G.hero.armor.kind ? armor_dr(&G.hero.armor) : 0) + G.hero.b.barkskin; }
int hero_damage_roll(const Item *ranged) {      // Hero.damageRoll + Weapon.damageRoll
  const Item *w = ranged ? ranged : (G.hero.weapon.kind ? &G.hero.weapon : NULL);
  if (!w) { int st = hero_str(), d = st > 10 ? rnd_range(1, st - 9) : 1; return G.hero.fury ? d * 3 / 2 : d; }
  int d = rnd_normal(weapon_min(w), weapon_max(w));
  if (!ranged != HUNTRESS && item_cat(w->kind) != CAT_WAND) { int ex = hero_str() - item_str(w); if (ex > 0) d += rnd_range(0, ex); }   // excess STR: melee, or missiles for a huntress
  return G.hero.fury ? d * 3 / 2 : d;                  // Fury
}
// Weapon.speedFactor: DLY * 1.2^encumbrance, in ticks
int attack_delay(const Item *w) {
  if (!w || !w->kind) return TICK;
  int t = TICK * weapon_dly(w->kind) / 100;
  int enc = item_str(w) - hero_str() - (HUNTRESS && item_cat(w->kind) == CAT_MISSILE ? 2 : 0);
  for (int i = 0; i < enc && i < 8; i++) t = t * 6 / 5;
  return w->flags & IF_FAST ? t * 3 / 5 : t;          // Imbue.SPEED
}
int hero_move_cost(void) {                             // 1 / Hero.speed(): armor 1.3^enc, cripple x2 (Armor.time2equip: twice that)
  int t = TICK;
  int e = armor_enc();
  for (int i = 0; i < e && i < 8; i++) t = t * 13 / 10;
  if (e <= 0 && G.hero.subclass == SUB_FREERUNNER && !game_starving()) t = t * 5 / 8;   // Freerunner: 1.6x
  return G.hero.b.cripple ? t * 2 : t;
}
void hero_spend(int t) {                         // Hero.spend (Ring of Haste: 1.1^-level) + Char.spend (Slow)
  t = scale_pow(t, 10, 11, ring_sum(IK_RNG_HASTE));
  if (G.hero.b.slow) t *= 2;
  G.hero.time += t;
  if (G.hero.combo && (G.hero.combo_left -= t * 100 / TICK) < 0) G.hero.combo = 0;   // Combo.act: the next blow came too late
}
bool game_hungry(void) { return G.hero.hunger >= 260; }
bool game_starving(void) { return G.hero.hunger >= 360; }

void interrupt(void) {                                 // Hero.interrupt: what was under way can be resumed
  if (G.hero.HP > 0 && G.action != HA_NONE && G.action != HA_REST && (G.action == HA_ATTACK || G.action == HA_TALK || G.action_cell != G.hero.pos)) {
    G.last_action = G.action; G.last_cell = G.action_cell;
  }
  G.interrupted = true;
}

// ---------------- Inventory ----------------
// slot: 0..18 backpack, -1 weapon, -2 armour, -3 / -4 rings
Item *game_slot(int slot) { return slot == -1 ? &G.hero.weapon : slot == -2 ? &G.hero.armor : slot <= -3 ? &G.hero.ring[-3 - slot] : slot >= PACK0 ? &G.pack[slot - PACK0] : &G.hero.bag[slot]; }
void set_known(int kind) { G.known[kind >> 3] |= (uint8_t)(1 << (kind & 7)); }
// Bags (SeedPouch 8, ScrollHolder 12, WandHolster 12): which bag takes a kind (-1 none, or not owned)
int bag_region(int kind) {
  int c = item_cat(kind), r = c == CAT_SEED ? 0 : c == CAT_SCROLL ? 1 : c == CAT_WAND ? 2 : -1;
  return r >= 0 && game_bag_find(IK_SEED_POUCH + r) >= 0 && game_bag_find(IK_SEED_POUCH + r) < BAG_SIZE ? r : -1;
}
static const uint8_t PACK_OFF[4] = { 0, 8, 20, 32 };
static bool pack_add(int r, Item it) {                  // Bag.grab + Item.collect into it
  Item *p = G.pack + PACK_OFF[r]; int n = PACK_OFF[r + 1] - PACK_OFF[r];
  if (item_stackable(it.kind)) for (int i = 0; i < n; i++) if (stacks(&p[i], &it)) { p[i].qty += it.qty; return true; }
  for (int i = 0; i < n; i++) if (!p[i].kind) { p[i] = it; return true; }
  return false;
}
void bags_sync(void) {                                  // a bag gone (dropped, sold, burnt...): what it held spills into the backpack or underfoot
  for (int r = 0; r < 3; r++) {
    if (game_bag_find(IK_SEED_POUCH + r) >= 0) continue;
    for (int i = PACK_OFF[r]; i < PACK_OFF[r + 1]; i++) if (G.pack[i].kind) { Item it = G.pack[i]; G.pack[i] = (Item){ 0 }; if (!bag_add(it)) heap_put(G.hero.pos, it); }
  }
}
bool bag_add(Item it) {
  int r = bag_region(it.kind);
  if (r >= 0 && pack_add(r, it)) return true;          // Item.collect: the bags first
  if (it.kind >= IK_SEED_POUCH && it.kind <= IK_WAND_HOLSTER) {   // Bag.collect: it takes what it can from the backpack
    bool ok = false;
    for (int i = 0; i < BAG_SIZE && !ok; i++) if (!G.hero.bag[i].kind) { G.hero.bag[i] = it; ok = true; }
    if (!ok) return false;
    for (int i = 0; i < BAG_SIZE; i++) if (bag_region(G.hero.bag[i].kind) == it.kind - IK_SEED_POUCH && pack_add(it.kind - IK_SEED_POUCH, G.hero.bag[i])) G.hero.bag[i] = (Item){ 0 };
    return true;
  }
  if (item_stackable(it.kind))
    for (int i = 0; i < BAG_SIZE; i++)
      if (stacks(&G.hero.bag[i], &it) && G.hero.bag[i].qty + it.qty < 1000) { G.hero.bag[i].qty += it.qty; return true; }
  for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind == IK_NONE) { G.hero.bag[i] = it; return true; }
  return false;
}
void bag_take_one(int slot) {
  Item *it = game_slot(slot);
  if (it->qty > 1) it->qty--; else *it = (Item){ 0 };
}
int8_t g_trade; int16_t g_trade_arg;
ActResult game_wnd_choice(int i) {
  g_level_changed = false;
  if (g_trade) return (ActResult)hero_op(HO_TRADE, i, 0, NULL);   // WndTradeItem, the Tome
  mreg_ensure();
  if (g_mh.wnd_choice) g_mh.wnd_choice(i);
  return g_level_changed ? ACT_LEVEL : ACT_NONE;
}
int game_bag_find(int kind) {                          // the backpack, then the bags
  if (kind != IK_NONE) ALL_SLOTS(i) if (game_slot(i)->kind == kind) return i;
  return -1;
}
int random_unequipped(void) {                   // Belongings.randomUnequipped
  int n = 0, s[BAG_SIZE];
  for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind) s[n++] = i;
  return n ? s[rnd_int(n)] : -1;
}
void short_name(const Item *it, char *buf, int len) {     // Item.name(): no level or STR
  Item t = *it; t.flags &= ~IF_LEVEL_KNOWN;
  item_name(&t, buf, len);
}

// Item.use: wear on weapons and armor with a positive level
void item_use(Item *it) {
  if (it->lvl <= 0 || item_broken(it)) return;
  int threshold = item_max_dur(it) / 6;
  char name[40]; short_name(it, name, sizeof name);
  bool lk = it->flags & IF_LEVEL_KNOWN;
  if (it->dur-- >= threshold && threshold > it->dur && lk) LOG(C_WARN, txt(TX_BECAUSE_OF_FREQUENT_USE), name);
  if (item_broken(it) && lk) { LOG(C_BAD, txt(TX_BECAUSE_OF_FREQUENT_USE_2), name); interrupt(); }
}
static void weapon_learn(Item *w) {                    // Weapon.proc: identify after 20 hits
  if (!(w->flags & IF_LEVEL_KNOWN) && w->hits && --w->hits == 0) {
    w->flags |= IF_LEVEL_KNOWN;
    char n[40], full[48]; short_name(w, n, sizeof n); item_name(w, full, sizeof full);
    LOG(C_INFO, txt(TX_YOU_ARE_NOW_FAMILIAR), n, full);
  }
  item_use(w);
}
static void armor_learn(Item *a) {                     // Armor.proc: identify after 10 hits taken
  if (!(a->flags & IF_LEVEL_KNOWN) && a->hits && --a->hits == 0) {
    a->flags |= IF_LEVEL_KNOWN;
    char n[40], full[48]; short_name(a, n, sizeof n); item_name(a, full, sizeof full);
    LOG(C_WARN, txt(TX_YOU_ARE_NOW_FAMILIAR_2), n, full);
  }
  item_use(a);
}

// ---------------- ShadowCaster (mechanics/ShadowCaster.java) ----------------
// Integer version: PD's float angles are exact fractions (2q +/- 1) / 2p, so they are
// stored scaled by ANG = lcm(2..16). No floating point or libm (it crashed real watches).
#define ANG 720720
static const uint8_t ROUND[VIEW_DISTANCE + 1][VIEW_DISTANCE + 1] = {   // ShadowCaster.rounding[distance][p]
  { 0 }, { 0, 1 }, { 0, 1, 1 }, { 0, 1, 2, 2 }, { 0, 1, 2, 3, 2 }, { 0, 1, 2, 3, 3, 2 }, { 0, 1, 2, 3, 4, 4, 2 },
  { 0, 1, 2, 3, 4, 5, 4, 3 }, { 0, 1, 2, 3, 4, 5, 6, 5, 3 } };
static uint8_t s_vd;                                    // this floor's view distance (Level.viewDistance, Light)
int view_distance(void) {
  int d = G.depth, v = d >= 11 && d <= 15 ? 6 : d >= 22 && d <= 24 ? 25 - d : d == 25 ? 3 : VIEW_DISTANCE;   // Caves, Halls
  if (v < 1) v = 1;
  return G.hero.light && v < 4 ? 4 : v;                 // Light.DISTANCE
}
#define s_obs_a1 ((int32_t *)(s_queue + MAP_LEN))         // 0.47: the shadow lists behind the path finder's block (their own: the view is worked out
#define s_obs_a2 ((int32_t *)(s_queue + MAP_LEN) + 41)     // in the middle of a fire's spread, which uses the queue): 328 B out of the app image
static int s_obs_len, s_obs_limit;
static bool obs_blocked(int32_t a) {
  for (int i = 0; i < s_obs_limit; i++) if (a >= s_obs_a1[i] && a <= s_obs_a2[i]) return true;
  return false;
}
static void obs_add(int32_t o1, int32_t o2) {
  if (s_obs_len > s_obs_limit && o1 <= s_obs_a2[s_obs_len - 1]) s_obs_a2[s_obs_len - 1] = o2;
  else { s_obs_a1[s_obs_len] = o1; s_obs_a2[s_obs_len++] = o2; }
}
static void scan_sector(int x0, int y0, int m1, int m2, int m3, int m4) {
  s_obs_len = s_obs_limit = 0;
  for (int p = 1; p <= s_vd; p++) {
    int32_t unit = ANG / (2 * p);
    int pp = ROUND[s_vd][p];
    for (int q = 0; q <= pp; q++) {
      int x = x0 + q * m1 + p * m3, y = y0 + p * m2 + q * m4;
      if (y >= 0 && y < MAP_H && x >= 0 && x < MAP_W) {
        int32_t a0 = 2 * q * unit, a1 = a0 - unit, a2 = a0 + unit;
        int pos = y * MAP_W + x;
        if (!(obs_blocked(a0) && obs_blocked(a1) && obs_blocked(a2))) BIT_SET(G.fov, pos);
        if (BIT_GET(G.los, pos)) obs_add(a1, a2);
      }
    }
    s_obs_limit = s_obs_len;
  }
}
bool discoverable(int c) {                      // Level.cleanWalls
  for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
    int x = cx(c) + dx, y = cy(c) + dy;
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
    int t = G.map[x + y * MAP_W];
    if (t != T_WALL && t != T_WALL_DECO) return true;
  }
  return false;
}
static void fov_square(int c, int r) {
  for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
    int x = cx(c) + dx, y = cy(c) + dy;
    if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && discoverable(x + y * MAP_W)) BIT_SET(G.fov, x + y * MAP_W);
  }
}
void update_fov(void) {                          // Level.updateFieldOfView + Dungeon.observe
  memset(G.fov, 0, sizeof G.fov);
  if (G.hero.HP <= 0) return;
  int x = cx(G.hero.pos), y = cy(G.hero.pos);
  s_vd = view_distance();
  if (!G.hero.b.blind && !G.hero.H_SHADOWS) {          // Shadows: as blind (Level.updateFieldOfView)
    BIT_SET(G.fov, G.hero.pos);
    scan_sector(x, y, 1, 1, 0, 0); scan_sector(x, y, -1, 1, 0, 0); scan_sector(x, y, 1, -1, 0, 0); scan_sector(x, y, -1, -1, 0, 0);
    scan_sector(x, y, 0, 0, 1, 1); scan_sector(x, y, 0, 0, -1, 1); scan_sector(x, y, 0, 0, 1, -1); scan_sector(x, y, 0, 0, -1, -1);
    if (G.hero.b.mind) fov_square(G.hero.pos, 2);       // MindVision.distance
  } else fov_square(G.hero.pos, 1);                      // blind: only what you can touch
  for (int i = 0; i < G.n_mobs; i++)                    // MindVision: every mind on the floor; a huntress: the ones 2 steps away
    if (G.mobs[i].HP > 0 && (G.hero.b.mind || (HUNTRESS && distance(G.mobs[i].pos, G.hero.pos) == 2))) fov_square(G.mobs[i].pos, 1);
  for (int i = 0; i < MAP_LEN / 8; i++) G.visited[i] |= G.fov[i];
}
bool game_hero_visible_mob(int i) { return G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos); }

// ---------------- PathFinder ----------------
int16_t *s_dist, *s_queue;                      // MAP_LEN each, allocated by game_init (one block: saves pack into it)
bool game_init(void) {                                 // heap state; false if the watch is out of memory
  if (!g_game) g_game = calloc(1, sizeof(Game));
  if (!s_dist) { s_dist = malloc(2 * MAP_LEN * sizeof(int16_t) + 82 * sizeof(int32_t)); s_queue = s_dist + MAP_LEN; }   // one 4 KB block: the save's buffer too (0.14)
  if (!MOB_DEFS) {
    MobDef *d = malloc(MT_COUNT * sizeof(MobDef));
    if (d) resource_load_byte_range(resource_get_handle(RESOURCE_ID_MOB_DEFS), 0, (uint8_t *)d, MT_COUNT * sizeof(MobDef));
    MOB_DEFS = d;
  }
  mobs_ovl_init();
  return g_game && s_dist && s_queue && MOB_DEFS && items_init();
}
uint8_t s_pass[MAP_LEN / 8];
void mark_chars(bool only_visible) {
  for (int i = 0; i < G.n_mobs; i++)
    if (G.mobs[i].HP > 0 && (!only_visible || BIT_GET(G.fov, G.mobs[i].pos))) BIT_CLR(s_pass, G.mobs[i].pos);
  BIT_CLR(s_pass, G.hero.pos);
}
// BFS distances from 'from' through s_pass (from itself always allowed); stops at 'stop' if >= 0
bool distance_map(int from, int stop) {
  for (int i = 0; i < MAP_LEN; i++) s_dist[i] = 0x7FFF;
  int qh = 0, qt = 0;
  s_dist[from] = 0; s_queue[qt++] = from;
  while (qh < qt) {
    int c = s_queue[qh++];
    if (c == stop) return true;
    int nd = s_dist[c] + 1;
    for (int k = 0; k < 8; k++) {
      int n = c + N8[k];
      if (!nb_ok(c, n) || s_dist[n] <= nd) continue;
      if (n == stop || BIT_GET(s_pass, n)) { s_dist[n] = nd; s_queue[qt++] = n; }
    }
  }
  return stop < 0;
}
static int get_step(int from, int to) {                // PathFinder.getStep
  if (from == to || !distance_map(to, from)) return -1;
  int best = -1, min = s_dist[from];
  for (int k = 0; k < 8; k++) { int n = from + N8[k]; if (nb_ok(from, n) && s_dist[n] < min) { min = s_dist[n]; best = n; } }
  return best;
}
// the terrain a creature crosses: Level.passable, for a flier Level.avoid too (chasms, traps), for a piranha Level.water
// (its getCloser / getFurther)
static int pass_need(int i) {
  int f = MOB_DEFS[G.mobs[i].type].flags;
  return f & MD_WATER ? F_LIQUID : f & MD_FLY ? F_PASSABLE | F_AVOID : F_PASSABLE;
}
static void pass_terrain(int need) {
  for (int i = 0; i < MAP_LEN; i++) if (TFLAGS(G.map[i]) & need) BIT_SET(s_pass, i); else BIT_CLR(s_pass, i);
}
static int mob_find_step(int i, int to) {       // Dungeon.findPath for a mob
  int from = G.mobs[i].pos, need = pass_need(i) | (MOB_AMOK(&G.mobs[i]) ? F_AVOID : 0);   // Amok: like a flier (Dungeon.findPath)
  if (adjacent(from, to)) return !char_at(to) && (TFLAGS(G.map[to]) & (need | F_AVOID)) ? to : -1;
  pass_terrain(need);
  mark_chars(false);
  return get_step(from, to);
}
int mob_flee_step(int i, int from) {            // Dungeon.flee / PathFinder.getStepBack
  int cur = G.mobs[i].pos;
  pass_terrain(pass_need(i));
  mark_chars(false);
  BIT_SET(s_pass, cur);
  distance_map(from, -1);
  int best = -1, max = s_dist[cur] == 0x7FFF ? 0 : s_dist[cur];
  for (int k = 0; k < 8; k++) {
    int n = cur + N8[k];
    if (!nb_ok(cur, n) || !BIT_GET(s_pass, n) || s_dist[n] == 0x7FFF) continue;
    if (s_dist[n] > max) { max = s_dist[n]; best = n; }
  }
  return best;
}

// ---------------- Blobs: ToxicGas, ParalyticGas, Fire ----------------
uint16_t *g_blob[BLOB_COUNT];
uint32_t g_goo_warn;
static int s_blob_vol[BLOB_COUNT];
void blob_free_all(void) { for (int b = 0; b < BLOB_COUNT; b++) { if (g_blob[b]) free(g_blob[b]); g_blob[b] = NULL; s_blob_vol[b] = 0; } }
void game_free_fx(void) { blob_free_all(); }
void blob_seed(int b, int cell, int amount) {   // Blob.seed (Fire.seed only lights an unlit cell)
  if (!g_blob[b]) { g_blob[b] = calloc(MAP_LEN, sizeof(uint16_t)); if (!g_blob[b]) return; }
  if (b == BLOB_FIRE) { if (!g_blob[b][cell]) { s_blob_vol[b] += amount; g_blob[b][cell] = amount; } return; }   // Fire.seed: only an unlit cell (0.47: was "raise to": a burning char never let its cell burn out)
  if (b == BLOB_WEB) { if (g_blob[b][cell] < amount) { s_blob_vol[b] += amount - g_blob[b][cell]; g_blob[b][cell] = amount; } return; }   // Web.seed
  int v = g_blob[b][cell] + amount; g_blob[b][cell] = v > 60000 ? 60000 : v; s_blob_vol[b] += amount;
}
void blob_clear(int b, int cell) { if (g_blob[b]) { s_blob_vol[b] -= g_blob[b][cell]; g_blob[b][cell] = 0; } }
static int blob_at(int b, int cell) { return g_blob[b] ? g_blob[b][cell] : 0; }

// ---------------- Damage and death ----------------
static void hero_die(const char *cause) {              // Hero.die (Hero.reallyDie reveals the floor: game_rank, not with an Ankh)
  G.hero.HP = 0;
  ui_sound(SND_DEATH);
  snprintf(G.death, sizeof G.death, "%s", cause);
  G.action = HA_NONE;
}
static void paralysis_break(Buffs *b, int dmg, int hp, int who) {   // Char.damage: pain can snap paralysis
  if (b->paralysis && rnd_int(dmg) >= rnd_int(hp)) {
    b->paralysis = 0;
    if (BIT_GET(G.fov, pos_of(who))) LOG(C_INFO, txt(TX_L_PAIN_SNAP), who < 0 ? "you" : mob_name(G.mobs[who].type));
  }
}
// cause: ResultDescriptions line, "%d" = depth
void hero_damage(int dmg, const char *cause_fmt) {
  if (G.hero.HP <= 0) return;
  Hero *h = &G.hero;
  h->b.frost = 0;                                      // Char.damage: Buff.detach(Frost)
  paralysis_break(&h->b, dmg, h->HP, -1);
  if (h->b.paralysis == 0 && hero_paralysed() == false && dmg && G.action == HA_REST) G.action = HA_NONE;
  h->HP -= dmg;
  char num[12]; snprintf(num, sizeof num, "%d", dmg);
  ui_fx(FX_DAMAGE, h->pos, -1, num);
  if (dmg > 0) ui_fx(FX_HURT, h->pos, -1, NULL);
  if (G.action == HA_REST) interrupt();
  if (h->HP <= 0 && G.dew >= 10 && game_bag_find(IK_DEW_VIAL) >= 0) {   // Hero.die: DewVial.autoDrink
    h->HP += h->HT; G.dew = 0;
    ui_log(txt(TX_L_DEW_AUTO), C_WARN);
    ui_fx(FX_HEAL, h->pos, -1, NULL);
  }
  if (h->HP <= 0) { if ((uintptr_t)cause_fmt < TX_COUNT) cause_fmt = txt((int)(uintptr_t)cause_fmt);   // TXC: a text resource
    char c[64]; snprintf(c, sizeof c, cause_fmt, G.depth); hero_die(c); }
  else if (h->subclass == SUB_BERSERKER && h->HP <= h->HT * 2 / 5 && !h->fury) {   // Hero.damage: Fury
    h->fury = 1; ui_log(txt(TX_L_FURIOUS), C_WARN); ui_fx(FX_TEXT, h->pos, -1, "furious");
  }
}

void earn_exp(int exp) {                        // Hero.earnExp
  Hero *h = &G.hero;
  h->exp += exp;
  bool up = false;
  while (h->exp >= hero_max_exp()) {
    h->exp -= hero_max_exp(); h->lvl++;
    h->HT += 5; h->HP += 5; h->attack_skill++; h->defense_skill++;
    up = true;
  }
  if (up) { LOG(C_GOOD, txt(TX_L_LEVEL_UP), h->lvl); ui_fx(FX_TEXT, h->pos, -1, "level up!"); ui_sound(SND_LEVELUP); }
  if (h->subclass == SUB_WARLOCK) {                    // a warlock feeds on the souls it takes
    int v = 1 + (G.depth - 1) / 5; if (v > h->HT - h->HP) v = h->HT - h->HP;
    h->HP += v; eat_satisfy(10);
  }
}

void mob_kill(int i) {                          // Mob.die / destroy / dropLoot
  Mob *m = &G.mobs[i];
  const MobDef *d = &MOB_DEFS[m->type];
  m->HP = 0;
  bool seen = BIT_GET(G.fov, m->pos);
  ui_fx(FX_DIE, m->pos, i, NULL);
  if (ally(i)) return;
  if (G.hero.HP > 0) {
    int exp = G.hero.lvl <= d->max_lvl ? d->exp : 0;
    if (exp > 0) { char t[12]; snprintf(t, sizeof t, "+%dEXP", exp); ui_fx(FX_HEAL, G.hero.pos, -1, t); earn_exp(exp); }
  }
  if (g_mh.die) g_mh.die(i);                           // Goo unseals, Skeleton bursts, Swarm and Thief drops...
  if (G.hero.lvl <= d->max_lvl + 2 && d->loot && rnd_int(100) < d->loot_pct && g_mh.loot) {   // Mob.dropLoot
    Item it = g_mh.loot(i);
    if (it.kind) level_drop(m->pos, it);
  }
  if (!seen && G.hero.HP > 0) ui_log(txt(TX_L_DIED_FAR), C_INFO);
}
void mob_damage(int i, int dmg) {               // Mob.damage + Char.damage
  Mob *m = &G.mobs[i];
  int src = g_dmg_src; g_dmg_src = DS_PHYS;
  if (m->HP > 0 && (MOB_DEFS[m->type].flags & MD_NPC) && g_mh.damage) { g_mh.damage(i, dmg, src); return; }   // Shopkeeper.damage: flees
  if (m->HP <= 0 || m->type == MT_SHEEP || (MOB_DEFS[m->type].flags & MD_NPC)) return;   // Sheep / NPC.damage: nothing
  uint32_t f = MOB_DEFS[m->type].flags;                        // Char.damage: immunities, then resistances
  static const uint32_t IMM[DS_COUNT] = { 0, MD_I_BURN, MD_I_POISON, MD_I_GAS, 0, MD_I_DEATH, MD_I_PSI, 0, MD_I_FIREBOLT | MD_I_BURN, MD_I_LEECH };
  static const uint32_t RES[DS_COUNT] = { 0, 0, MD_R_POISON, MD_R_GAS, MD_R_SHOCK, MD_R_DEATH, MD_R_PSI, MD_R_DISINT, 0, MD_R_LEECH };
  if (f & IMM[src]) return;
  if (f & RES[src]) dmg = rnd_range(0, dmg);
  if (g_mh.damage) dmg = g_mh.damage(i, dmg, src);
  if (m->HP <= 0) return;
  if (m->b.terror && m->b.terror < 10) { m->b.terror = 0; m->state = ST_HUNTING; }   // Terror.recover
  if (m->state == ST_SLEEPING) m->state = ST_WANDERING;
  m->flags |= MF_ALERTED;
  m->b.frost = 0;                                      // Char.damage: Buff.detach(Frost)
  paralysis_break(&m->b, dmg, m->HP, i);
  m->HP -= dmg;
  if (BIT_GET(G.fov, m->pos)) { char num[12]; snprintf(num, sizeof num, "%d", dmg); ui_fx(FX_DAMAGE, m->pos, i, num); }
  if (m->HP <= 0) mob_kill(i);
}

// ---------------- Buffs ----------------
void dispel_invisibility(void) {                // Invisibility.dispel: only before enemy eyes
  if (!G.hero.b.invis) return;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos)) { G.hero.b.invis = 0; return; }
}
Buffs *buffs_of(int who) { return who < 0 ? &G.hero.b : &G.mobs[who].b; }
int16_t pos_of(int who) { return who < 0 ? G.hero.pos : G.mobs[who].pos; }
bool alive(int who) { return who < 0 ? G.hero.HP > 0 : G.mobs[who].HP > 0; }
void char_damage(int who, int dmg, const char *cause) { if (who < 0) hero_damage(dmg, cause); else mob_damage(who, dmg); }

void poison_tick(Buffs *b, int who) {           // Poison.act
  int dmg = elem_resist(who, b->poison / 3 + 1);
  if (who >= 0) g_dmg_src = DS_POISON;
  char_damage(who, dmg, TXC(TX_C_POISON));
  if (b->poison) b->poison--;
  if (who < 0 && G.hero.HP <= 0) ui_log(txt(TX_L_POISON), C_BAD);
}
void affect_poison(int who, int turns) {
  Buffs *b = buffs_of(who);
  if (mob_immune(who, MD_I_POISON)) return;
  turns = elem_duration(who, turns);
  bool add = !b->poison;                                // Hero.add: only a new buff speaks (Buff.affect finds the old one)
  b->poison = turns > 255 ? 255 : turns;
  if (BIT_GET(G.fov, pos_of(who))) ui_fx(FX_TEXT, pos_of(who), who, "poisoned");
  if (who < 0 && add) { ui_log("You are poisoned!", C_WARN); interrupt(); }
  poison_tick(b, who);                                  // a new buff acts right away
}
void affect_burning(int who) {                  // Buff.affect(Burning).reignite
  Buffs *b = buffs_of(who);
  if (mob_immune(who, MD_I_BURN)) return;              // Yog, Piranha, MirrorImage, the Elemental (Buff.attachTo: immune before Elemental.add's heal); frozen or not (0.47)
  if (!b->burning && who < 0) { ui_log("You catch fire!", C_WARN); interrupt(); }
  if (!b->burning && BIT_GET(G.fov, pos_of(who))) ui_sound(SND_BURNING);
  b->burning = elem_duration(who, 8);
  // (0.47: no seeding here: Burning.reignite only sets the buff; its act seeds the cell each turn, mobs.c burning_tick)
}
void affect_paralysis(int who, int turns) {
  Buffs *b = buffs_of(who);
  if (b->gas_immune || mob_immune(who, MD_I_PARA)) return;   // GasesImmunity; FetidRat, King...
  turns = elem_duration(who, turns);
  bool add = !b->paralysis;                             // Buff.prolong: a new one only
  if (turns > b->paralysis) b->paralysis = turns;
  if (who < 0 && add) { ui_log("You are paralysed!", C_WARN); interrupt(); G.action = HA_NONE; }
  else if (BIT_GET(G.fov, pos_of(who))) ui_fx(FX_TEXT, pos_of(who), who, "paralysed");
}
static void heap_burn(int h);
void affect_frost(int who, int turns) {          // Frost: frozen in place, puts out fire
  Buffs *b = buffs_of(who);
  if (mob_immune(who, MD_I_FROST)) return;
  if (who >= 0 && G.mobs[who].type == MT_ELEMENTAL) { mob_damage(who, rnd_normal(1, G.mobs[who].HT * 2 / 3)); if (G.mobs[who].HP <= 0) return; }   // and frost hurts it
  turns = elem_duration(who, turns);
  if (turns > b->frost) b->frost = turns;
  b->burning = 0;
  if (who < 0) {
    int s = random_unequipped();
    if (s >= 0 && G.hero.bag[s].kind == IK_MYSTERY_MEAT) { bag_take_one(s); Item c = item_make(IK_CARPACCIO, 1); if (!bag_add(c)) heap_put(G.hero.pos, c); }
    interrupt(); G.action = HA_NONE;
  }
}
static void hunger_act(void) {                         // Hunger.act, every 10 turns
  Hero *h = &G.hero;
  if (game_starving()) {
    if (rnd_pct(30) && (h->HP > 1 || !hero_paralysed())) { ui_log("You are starving!", C_BAD); hero_damage(1, TXC(TX_C_STARVED)); interrupt(); if (h->HP <= 0) ui_log(txt(TX_L_STARVED), C_BAD); }
  } else {
    int nl = h->hunger + 10 - ring_sum(IK_RNG_SATIETY);              // Ring of Satiety
    if (nl >= 360) { ui_log("You are starving!", C_BAD); interrupt(); }
    else if (nl >= 260 && h->hunger < 260) ui_log("You are hungry.", C_WARN);
    h->hunger = nl > 360 ? 360 : nl;
  }
}
void eat_satisfy(int energy) { int v = G.hero.hunger - energy; G.hero.hunger = v < 0 ? 0 : v > 360 ? 360 : v; }

// ---------------- Mobs ----------------
int random_destination(void) {                  // RegularLevel.randomDestination
  for (int i = 0; i < 200; i++) {
    int r = rnd_int(G.n_rooms);
    int c = level_room_random(&G.rooms[r], 0);
    if (PASSABLE(G.map[c])) return c;
  }
  return -1;
}
int random_respawn_cell(void) {                 // RegularLevel.randomRespawnCell
  for (int count = 0; count < 10; count++) {
    int c = level_random_room_cell(RT_STANDARD, 10);
    if (c < 0) continue;
    if (!BIT_GET(G.fov, c) && !char_at(c) && PASSABLE(G.map[c])) return c;
  }
  return -1;
}
static int mob_slot(void) {
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP <= 0) return i;
  return G.n_mobs < MAX_MOBS ? G.n_mobs++ : -1;
}
int spawn_mob(int type, int pos, int state, int32_t delay) {
  int i = mob_slot();
  if (i < 0) return -1;
  const MobDef *d = &MOB_DEFS[type];
  G.mobs[i] = (Mob){ .time = G.now + delay, .pos = pos, .target = -1, .HP = d->HT, .HT = d->HT, .type = type,
                     .state = state, .uid = ++G.next_uid };
  if (g_mh.spawned) g_mh.spawned(i);
  return i;
}
bool mob_paralysed(Mob *m) { return m->b.paralysis || m->b.frost; }
static int hero_stealth(void) { return ring_sum(IK_RNG_SHADOWS) + (hero_flying() ? 2 : 0); }

void mob_notice(int i) { if (BIT_GET(G.fov, G.mobs[i].pos)) ui_fx(FX_ALERT, G.mobs[i].pos, i, "!"); if (g_mh.notice) g_mh.notice(i); }
int16_t g_move_from;
void mob_move(int i, int step) {                // Char.move + Mob.move
  Mob *m = &G.mobs[i];
  int16_t old = m->pos;
  if (m->vertigo && adjacent(step, old)) {
    step = old + N8[rnd_int(8)];
    if (!nb_ok(old, step) || !(PASSABLE(G.map[step]) || (TFLAGS(G.map[step]) & F_AVOID)) || char_at(step)) return;
  }
  if (cx(step) != cx(old)) { if (cx(step) < cx(old)) m->flags |= MF_LEFT; else m->flags &= ~MF_LEFT; }
  m->pos = step;
  g_move_from = old;                                    // for the moved hook (Spinner's web, DM-300's quakes)
  if (G.map[old] == T_OPEN_DOOR && heap_at(old) < 0) set_terrain(old, T_DOOR);    // Door.leave
  if (!(MOB_DEFS[m->type].flags & MD_FLY)) mob_press(i); else if (G.map[step] == T_DOOR) set_terrain(step, T_OPEN_DOOR);
  if (g_mh.moved) g_mh.moved(i);
}
bool mob_get_closer(int i, int target) {
  if ((target = g_mh.closer(i, target)) < 0) return target == CL_MOVED;   // Succubus, Tengu, King (the mobs overlay sets it: never NULL)
  if (G.mobs[i].b.roots) return false;
  int step = mob_find_step(i, target);
  if (step < 0) return false;
  mob_move(i, step);
  return true;
}
int mob_move_cost(Mob *m) { int c = MOB_DEFS[m->type].move_cost; return m->b.cripple ? c * 2 : c; }
void mob_spend(Mob *m, int t) { m->time += m->b.slow ? t * 2 : t; }
bool attack(int att, int def, Item *thrown);
static int mstat(int i, int w, int dflt) { int v = g_mh.stats ? g_mh.stats(i, w) : -1; return v >= 0 ? v : dflt; }

// the mob's enemy (Mob.chooseEnemy): the hero, or for a mirror image the nearest visible monster
int mob_enemy(int i) {
  if (MOB_AMOK(&G.mobs[i]) && !ally(i)) {                 // Amok: the nearest other creature in sight, else the hero
    int best = -1, bd = 99;
    for (int j = 0; j < G.n_mobs; j++)
      if (j != i && G.mobs[j].HP > 0 && BIT_GET(G.fov, G.mobs[j].pos)) { int d = distance(G.mobs[i].pos, G.mobs[j].pos); if (d < bd) { bd = d; best = j; } }
    return best;
  }
  if (!hero_side(i)) return -1;
  int best = -2, bd = 99;
  for (int j = 0; j < G.n_mobs; j++)
    if (j != i && G.mobs[j].HP > 0 && !ally(j) && BIT_GET(G.fov, G.mobs[j].pos)) {
      int d = distance(G.mobs[i].pos, G.mobs[j].pos);
      if (d < bd) { bd = d; best = j; }
    }
  return best;                                          // -2: nobody to fight
}
bool mob_can_attack(int i, int enemy, int epos) {        // Mob.canAttack: adjacent and not charmed, unless a creature says otherwise
  if (g_mh.can_attack) { int r = g_mh.can_attack(i, enemy, epos); if (r >= 0) return r; }
  return adjacent(G.mobs[i].pos, epos) && !(enemy == -1 && G.mobs[i].b.charm);
}
static void mob_act(int i) { if (!g_mh.act || !g_mh.act(i)) mob_act_default(i); }
void mob_act_default(int i) {                          // Mob.act and its AI states
  Mob *m = &G.mobs[i];
  bool just_alerted = m->flags & MF_ALERTED;
  m->flags &= ~MF_ALERTED;
  if (m->type == MT_SHEEP) { m->HP = 0; ui_fx(FX_DIE, m->pos, i, NULL); return; }   // its lifespan is up: it fades away
  if (mob_paralysed(m)) { m->flags &= ~MF_SEEN; mob_spend(m, TICK); return; }
  int enemy = mob_enemy(i);
  int16_t epos = enemy == -1 ? G.hero.pos : enemy >= 0 ? G.mobs[enemy].pos : -1;
  bool in_fov = g_mh.sees && g_mh.sees(i, enemy);   // Mob.act's enemyInFOV (the mobs overlay)
  int dist = epos >= 0 ? distance(m->pos, epos) : 99;
  switch (m->state) {
    case ST_SLEEPING:
      if (in_fov && rnd_int(dist + (enemy == -1 ? hero_stealth() : 0)) == 0) {   // Ring of Shadows; a flier is harder to hear
        m->flags |= MF_SEEN; mob_notice(i);
        m->state = ST_HUNTING; m->target = epos;
        mob_spend(m, TICK);
      } else { m->flags &= ~MF_SEEN; mob_spend(m, TICK); }
      return;
    case ST_WANDERING:
      if (in_fov && (just_alerted || rnd_int(dist / 2 + (enemy == -1 ? ring_sum(IK_RNG_SHADOWS) : 0)) == 0)) {
        m->flags |= MF_SEEN; mob_notice(i);
        m->state = ST_HUNTING; m->target = epos;
        return;                                        // no time spent: hunts at once
      }
      m->flags &= ~MF_SEEN;
      if (m->target != -1 && mob_get_closer(i, m->target)) mob_spend(m, mob_move_cost(m));
      else { m->target = random_destination(); mob_spend(m, TICK); }
      return;
    case ST_HUNTING:
      if (in_fov) m->flags |= MF_SEEN; else m->flags &= ~MF_SEEN;
      if (in_fov && mob_can_attack(i, enemy, epos)) {
        if (g_mh.do_attack && g_mh.do_attack(i, enemy)) return;   // ranged and special attacks spend their own time
        attack(i, enemy, NULL);
        if (m->type == MT_MIRROR && m->HP > 0) { m->HP = 0; ui_fx(FX_DIE, m->pos, i, NULL); }   // MirrorImage.attackProc: one strike
        mob_spend(m, MOB_DEFS[m->type].atk_delay);
        return;
      }
      if (in_fov) m->target = epos;
      if (m->target != -1 && mob_get_closer(i, m->target)) { mob_spend(m, mob_move_cost(m)); return; }
      mob_spend(m, TICK); m->state = ST_WANDERING; m->target = random_destination();
      return;
    case ST_FLEEING: {
      if (in_fov) { m->flags |= MF_SEEN; m->target = epos; } else m->flags &= ~MF_SEEN;
      int step = m->target != -1 ? mob_flee_step(i, m->target) : -1;   // (Mob.getFurther: roots or not)
      if (step >= 0) { mob_move(i, step); mob_spend(m, mob_move_cost(m)); }
      else mob_spend(m, TICK);                          // nowhereToRun
      return;
    }
  }
  mob_spend(m, TICK);
}

void respawn(void) {                            // Level.respawner (none on boss floors, the last shop and the last floor)
  if (boss_depth(G.depth) || G.depth == 21 || G.depth >= 26) return;
#ifdef TEST_BUILD
  if (G.depth < 3) return;                     // the test kit: no creatures before depth 3
#endif
  int alive_n = 0;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) alive_n++;   // Level.mobs.size(): allies and NPCs count too
  if (alive_n >= 2 + G.depth % 5 + rnd_int(3)) return;
  int c = random_respawn_cell();
  int i = c >= 0 ? spawn_mob(bestiary(G.depth), c, ST_WANDERING, 0) : -1;
  if (i >= 0 && (G.dflags & DF_AMULET)) mob_beckon(i, G.hero.pos);   // the Amulet calls them all
}
void mob_beckon(int i, int cell) { Mob *m = &G.mobs[i]; mob_notice(i); if (m->state != ST_HUNTING) m->state = ST_WANDERING; m->target = cell; }   // Mob.beckon

// ---------------- Blob evolution (one step per turn) ----------------
static void blob_affect(int b, int who) {
  Buffs *bf = buffs_of(who);
  if (b == BLOB_TOXIC) {                               // ToxicGas: (HT + 5 + 5 depth) / 40, with a remainder chance
    if (bf->gas_immune || mob_immune(who, MD_I_GAS)) return;
    int ht = who < 0 ? G.hero.HT : G.mobs[who].HT, lvl = 5 + G.depth * 5;
    int dmg = (ht + lvl) / 40 + (rnd_int(40) < (ht + lvl) % 40 ? 1 : 0);
    if (who >= 0) g_dmg_src = DS_GAS;
    char_damage(who, elem_resist(who, dmg), TXC(TX_C_GAS));
    if (who < 0 && G.hero.HP <= 0) ui_log(txt(TX_L_GAS), C_BAD);
  } else if (b == BLOB_PARALYTIC) affect_paralysis(who, 10);
  else if (b == BLOB_CONFUSION) {                      // ConfusionGas: Buff.prolong(Vertigo, Vertigo.duration)
    if (bf->gas_immune || mob_immune(who, MD_I_VERTIGO)) return;
    int d = elem_duration(who, 10);
    uint8_t *v = who < 0 ? &G.hero.vertigo : &G.mobs[who].vertigo;
    if (!*v && who < 0) { ui_log(txt(TX_L_SPINNING), C_WARN); interrupt(); }   // Hero.add(Vertigo)
    if (d > *v) *v = d;
  }
}
static void gas_evolve(int b) {                        // Blob.evolve: spread to the 4 neighbours, fade by 1
  uint16_t *cur = g_blob[b];
  uint16_t *off = (uint16_t *)s_queue;                 // the path finder's scratch (idle during the turn's actors): no 2 KB from the heap
  memset(off, 0, MAP_LEN * sizeof(uint16_t));
  int vol = 0;
  for (int y = 1; y < MAP_H - 1; y++)
    for (int x = 1; x < MAP_W - 1; x++) {
      int p = x + y * MAP_W;
      if (TFLAGS(G.map[p]) & F_SOLID) continue;
      int count = 1, sum = cur[p];
      static const int16_t D4[4] = { -1, 1, -MAP_W, MAP_W };
      for (int k = 0; k < 4; k++) if (!(TFLAGS(G.map[p + D4[k]]) & F_SOLID)) { sum += cur[p + D4[k]]; count++; }
      int v = sum >= count ? sum / count - 1 : 0;
      off[p] = v; vol += v;
    }
  memcpy(cur, off, MAP_LEN * sizeof(uint16_t));
  s_blob_vol[b] = vol;
}
static bool mimic_wake(int h, int frost) {             // Heap.burn / freeze: Mimic.spawnAt, burning or frozen
  if (G.heaps[h].type != HEAP_MIMIC) return false;
  int c = G.heaps[h].pos;
  if (g_mh.heap_open(h)) { int m = mob_at(c); if (m >= 0) { if (frost) affect_frost(m, rnd_range(5, 7)); else affect_burning(m); } }
  return true;
}
static void heap_burn(int h) {                          // Heap.burn
  Heap *hp = &G.heaps[h];
  if (mimic_wake(h, 0) || hp->type != HEAP_PLAIN) return;
  int k = hp->item.kind;
  if (item_cat(k) == CAT_SCROLL || k == IK_DEW) { heap_remove(h); ui_sound(SND_BURNING); }
  else if (k == IK_MYSTERY_MEAT) hp->item.kind = IK_CHARGRILLED;
}
void heap_freeze(int h) { if (!mimic_wake(h, 1) && G.heaps[h].type == HEAP_PLAIN && G.heaps[h].item.kind == IK_MYSTERY_MEAT) G.heaps[h].item.kind = IK_CARPACCIO; }
static void fire_burn(int pos) {                       // Fire.burn
  int m = mob_at(pos);
  if (pos == G.hero.pos && G.hero.HP > 0) affect_burning(-1);
  if (m >= 0) affect_burning(m);
  for (int h = G.n_heaps - 1; h >= 0; h--) if (G.heaps[h].pos == pos) heap_burn(h);   // every pile on the cell (PD: one Heap)
}
static void fire_evolve(void) {                        // Fire.evolve
  uint16_t *cur = g_blob[BLOB_FIRE];
  uint8_t *off = (uint8_t *)s_queue;                   // scratch, as above
  memset(off, 0, MAP_LEN);
  int vol = 0;
  bool changed = false;
  for (int pos = MAP_W + 1; pos < MAP_LEN - MAP_W - 1; pos++) {
    int fire;
    bool fl = TFLAGS(G.map[pos]) & F_FLAMABLE;
    if (cur[pos] > 0) {
      fire_burn(pos);
      fire = cur[pos] - 1;
      if (fire <= 0 && fl) { level_destroy(pos); changed = true; }
    } else if (fl && (cur[pos - 1] > 0 || cur[pos + 1] > 0 || cur[pos - MAP_W] > 0 || cur[pos + MAP_W] > 0)) {
      fire = 4; fire_burn(pos);
    } else fire = 0;
    off[pos] = fire > 0 ? fire : 0; vol += off[pos];
  }
  for (int i = 0; i < MAP_LEN; i++) cur[i] = off[i];
  s_blob_vol[BLOB_FIRE] = vol;
  if (changed) update_fov();
}
static void hero_rooted(void) { G.hero.b.roots = 1; ui_log("You can't move!", C_WARN); }   // Hero.add(Roots)
static void regrowth_act(void) {                       // Regrowth.evolve: grass rises where it spreads, roots hold
  uint16_t *cur = g_blob[BLOB_REGROWTH];
  bool changed = false;
  for (int i = 0; i < MAP_LEN; i++) {
    if (!cur[i]) continue;
    int c = G.map[i], c1 = c;
    if (c == T_EMPTY || c == T_EMBERS || c == T_EMPTY_DECO) c1 = cur[i] > 9 ? T_HIGH_GRASS : T_GRASS;
    else if (c == T_GRASS && cur[i] > 9) c1 = T_HIGH_GRASS;
    if (c1 != c) { set_terrain(i, T_HIGH_GRASS); changed = true; }   // (PD sets high grass either way)
    int m = mob_at(i);
    if (m >= 0 && !mob_immune(m, MD_I_ROOTS | MD_FLY) && !G.mobs[m].b.roots) G.mobs[m].b.roots = 1;   // Buff.prolong: immunities, fliers
    if (i == G.hero.pos && !G.hero.b.roots && !hero_flying()) hero_rooted();   // Roots.attachTo: not while flying
  }
  if (changed) update_fov();
}
static void blobs_act(void) {
  if (g_blob[BLOB_FIRE]) fire_evolve();
  if (g_blob[BLOB_WEB]) {                               // Web.evolve: wears away; whoever is caught in it is rooted
    uint16_t *w = g_blob[BLOB_WEB]; int vol = 0;
    for (int c = 0; c < MAP_LEN; c++) {
      if (!w[c]) continue;
      if (--w[c] == 0) continue;
      vol += w[c];
      int m = mob_at(c);
      if (m >= 0 && !mob_immune(m, MD_I_ROOTS | MD_FLY) && !G.mobs[m].b.roots) G.mobs[m].b.roots = 1;   // Roots.attachTo: not a flier
      else if (c == G.hero.pos && G.hero.HP > 0 && !G.hero.b.roots && !hero_flying()) hero_rooted();
    }
    s_blob_vol[BLOB_WEB] = vol;
  }
  if (g_blob[BLOB_REGROWTH]) { gas_evolve(BLOB_REGROWTH); regrowth_act(); }
  static const uint8_t GASES[3] = { BLOB_TOXIC, BLOB_PARALYTIC, BLOB_CONFUSION };
  for (int gi = 0; gi < 3; gi++) {
    int b = GASES[gi];
    if (!g_blob[b]) continue;
    if (G.hero.HP > 0 && g_blob[b][G.hero.pos]) blob_affect(b, -1);   // ToxicGas.evolve: the gas as it was before this turn's spread (0.47)
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && g_blob[b] && g_blob[b][G.mobs[i].pos]) blob_affect(b, i);
    gas_evolve(b);
  }
  if (g_blob[BLOB_TOXIC] && g_blob[BLOB_PARALYTIC]) {    // the two gases cancel out
    uint16_t *t = g_blob[BLOB_TOXIC], *p = g_blob[BLOB_PARALYTIC];
    for (int i = 0; i < MAP_LEN; i++) { if (p[i] >= t[i]) { s_blob_vol[0] -= t[i]; t[i] = 0; } else { s_blob_vol[1] -= p[i]; p[i] = 0; } }
  }
  for (int b = 0; b < BLOB_COUNT; b++) if (g_blob[b] && s_blob_vol[b] <= 0) { free(g_blob[b]); g_blob[b] = NULL; s_blob_vol[b] = 0; }
}

// Ring.RingBuff.act: a worn ring learns itself in 200 turns and wears (Item.use) every turn, here in tens;
// Wand.Charger: each wand in the pack gains a charge every 40 turns (one shared clock for all of them: only the
// phase differs from PD, where each charger starts when its wand is picked up)
static void gear_tick(int t) {
  Hero *h = &G.hero;
  for (int i = 0; i < 2; i++) {
    Item *r = &h->ring[i];
    if (!r->kind) continue;
    if (!item_identified(r) && r->hits && --r->hits == 0) {
      char gem[40], full[48]; short_name(r, gem, sizeof gem);
      r->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(r->kind);
      item_name(r, full, sizeof full);
      LOG(C_WARN, txt(TX_RNG_IDENTIFY), gem, full);
    }
    if (r->lvl > 1 && t % 10 == 0) item_use(r);
  }
  for (int i = -1; i < PACK0 + 32; i = i == BAG_SIZE - 1 ? PACK0 : i + 1) {   // Wand.Charger: 40 turns a charge (a mage: 40 / sqrt(1 + level)); in hand and holster too
    Item *w = game_slot(i);
    if (item_cat(w->kind) != CAT_WAND) continue;
    int p = 40;
    if (h->hclass == 1) { int l = 1 + (item_broken(w) ? 0 : w->lvl), r = 1; while ((r + 1) * (r + 1) <= l * 64) r++; p = 320 / r; }   // 40 / (sqrt(64 l) / 8)
    if (t % p == 0 && w->ench < wand_max(w)) w->ench++;
  }
}

// ---------------- Time (Actor.process) ----------------
static void turn_act(void) {                           // all per-turn actors: buffs, blobs, hunger, regeneration, respawner
  int t = G.turn_due / TICK;
  Hero *h = &G.hero;
  if (h->HP > 0 && t % (h->hclass == 2 ? 12 : 10) == 0) hunger_act();   // Hunger.STEP (a rogue: x1.2)
  if (h->HP > 0 && (G.regen_left -= TICK) <= 0) {                        // Regeneration: every 10 turns / 1.2^Mending
    if (h->HP < h->HT && !game_starving()) h->HP++;
    G.regen_left += scale_pow(10 * TICK, 10, 12, ring_sum(IK_RNG_MENDING));
  }
  if (h->HP > 0) gear_tick(t);
  if (G.health_pos >= 0) {                                                // Sungrass.Health: HT/10 every 5 turns, standing still
    if (h->pos != G.health_pos || h->HP >= h->HT) G.health_pos = -1;
    else if (t % 5 == 0) { h->HP += h->HT / 10; if (h->HP > h->HT) h->HP = h->HT; ui_fx(FX_HEAL, h->pos, -1, NULL); }
  }
  if (h->HP > 0 && g_mh.buffs) g_mh.buffs(-1);
  if (h->fury && h->HP > h->HT * 2 / 5) h->fury = 0;  // Fury.act
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && g_mh.buffs) g_mh.buffs(i);   // (0.17: in the mobs overlay, for app room)
  blobs_act();
  if ((G.turn_due - G.level_t0) % ((G.night || (G.dflags & DF_AMULET) ? 25 : 50) * TICK) == 0) respawn();   // twice as often at night or with the Amulet
}
void check_visible_mobs(void) {                 // Hero.checkVisibleMobs: a new enemy interrupts
  uint16_t seen = 0;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos)) seen |= 1u << i;
  if (seen & ~G.seen_mobs) interrupt();
  G.seen_mobs = seen;
}
// Run everyone else until it is the hero's turn again (or the hero dies). A paralysed hero just waits.
void process(void) {
  for (int guard = 0; guard < 6000 && G.hero.HP > 0; guard++) {
    int best = -1; int32_t bt = G.hero.time;              // a tie with the hero: each creature's own fixed order (0.47; PD: Actor.all, a HashSet)
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && (m->time < bt || (best < 0 && m->time == bt && (m->uid * 0x9E3779B1u) >> 31))) { best = i; bt = m->time; } }
    if (G.turn_due <= bt) { G.now = G.turn_due; turn_act(); if (g_mh.turn) g_mh.turn(); G.turn_due += TICK; continue; }
    if (best < 0) {
      if (hero_paralysed()) { G.now = G.hero.time; G.hero.time += TICK; G.action = HA_NONE; if (G.hero.combo && (G.hero.combo_left -= 100) < 0) G.hero.combo = 0; continue; }
      break;
    }
    G.now = bt;
    mob_act(best);
  }
  if (G.hero.time > G.now) G.now = G.hero.time;
  update_fov();
  check_visible_mobs();
}

// ---------------- Combat (Char.attack / Char.hit) ----------------
bool char_hit(int att, int def, bool magic) {         // Char.hit: magic doubles the attacker's roll
  Mob *am = att >= 0 ? &G.mobs[att] : NULL, *dm = def >= 0 ? &G.mobs[def] : NULL;
  int a = att < 0 ? hero_attack_skill(NULL, pos_of(def)) : mstat(att, STAT_ATT, MOB_DEFS[am->type].att);
  int d = def < 0 ? hero_defense_skill() : ((dm->flags & MF_SEEN) && !mob_paralysed(dm) ? mstat(def, STAT_DEF, MOB_DEFS[dm->type].def) : 0);
  return rnd_scaled(magic ? 2 * a : a) >= rnd_scaled(d);
}
// att / def: -1 hero, else mob index. thrown: the missile the hero threw (or NULL)
bool attack(int att, int def, Item *thrown) {
  Mob *am = att >= 0 ? &G.mobs[att] : NULL, *dm = def >= 0 ? &G.mobs[def] : NULL;
  const MobDef *ad = am ? &MOB_DEFS[am->type] : NULL, *dd = dm ? &MOB_DEFS[dm->type] : NULL;
  int16_t apos = pos_of(att), dpos = pos_of(def);
  bool visible = BIT_GET(G.fov, apos) || BIT_GET(G.fov, dpos);
  if (att < 0) G.last_target = dpos;
  bool left = cx(dpos) < cx(apos);
  if (cx(dpos) != cx(apos)) { if (att < 0) G.hero.facing_left = left; else if (left) am->flags |= MF_LEFT; else am->flags &= ~MF_LEFT; }
  if (!thrown && visible) ui_fx(FX_ATTACK, apos, att, NULL);
  int att_skill = att < 0 ? hero_attack_skill(thrown, dpos) : am->type == MT_MIRROR ? am->aux : mstat(att, STAT_ATT, ad->att);
  int def_skill = def < 0 ? hero_defense_skill() : ((dm->flags & MF_SEEN) && !mob_paralysed(dm) ? mstat(def, STAT_DEF, dd->def) : 0);   // surprise: no defense
  if (rnd_scaled(att_skill) < rnd_scaled(def_skill)) {
    if (visible) {
      ui_sound(SND_MISS);
      const char *verb = def < 0 ? "dodged" : VERBS[dd->verb];
      ui_fx(FX_TEXT, dpos, def, verb);
      if (att < 0) { char n[32]; snprintf(n, sizeof n, "%s", mob_name(dm->type)); cap_first(n); LOG(C_INFO, "%s %s your attack", n, verb); }
    }
    return false;
  }
  if (visible) ui_sound(SND_HIT);
  int dmg = att < 0 ? hero_damage_roll(thrown) : am->type == MT_MIRROR ? am->aux2 : mstat(att, STAT_DMG, rnd_normal(ad->dmg_lo, ad->dmg_hi));
  if (!(thrown && G.hero.subclass == SUB_SNIPER && att < 0)) dmg -= rnd_range(0, def < 0 ? hero_dr() : mstat(def, STAT_DR, dd->dr));   // a sniper's missiles ignore armour
  if (dmg < 0) dmg = 0;
  if (att >= 0 && g_mh.attack_proc) dmg = g_mh.attack_proc(att, def, dmg);   // Mob.attackProc
  if (att < 0) {                                         // Hero.attackProc: the weapon's own effects
    Item *w = thrown ? thrown : (G.hero.weapon.kind ? &G.hero.weapon : NULL);
    if (w && w->ench && item_cat(w->kind) != CAT_WAND) ench_weapon_proc(def, dmg, w);   // Weapon.proc: enchantment first (thrown too: Boomerang)
    if (w && !thrown && item_cat(w->kind) != CAT_WAND) weapon_learn(w);   // Wand: KindOfWeapon.proc does nothing
    if (w && !thrown && G.hero.subclass == SUB_GLADIATOR && item_cat(w->kind) == CAT_WEAPON) {   // Combo.hit
      int n = ++G.hero.combo;
      if (n >= 3) { LOG(C_GOOD, "%d hit combo!", n); G.hero.combo_left = 141 - n * 10; dmg += dmg * (n - 2) / 5; }
      else G.hero.combo_left = 110;
    }
    if (w && !thrown && G.hero.subclass == SUB_BATTLEMAGE && item_cat(w->kind) == CAT_WAND) {   // a battlemage's wand charges as it strikes
      if (w->ench >= wand_max(w)) item_use(w); else if (dmg > 0) w->ench++;
      dmg += w->ench;
    }
    if (def >= 0 && !(dm->flags & MF_SEEN) && G.hero.subclass == SUB_ASSASSIN) dmg += rnd_int2(1, dmg);   // Mob.defenseProc: a surprise attack (Random.Int(1, 0 or 1) is 1)
    Item *mw = thrown ? thrown : item_cat(G.hero.weapon.kind) == CAT_MISSILE ? &G.hero.weapon : NULL;   // MissileWeapon.proc, thrown or held
    if (mw) switch (mw->kind) {
      case IK_CURARE_DART: affect_paralysis(def, 3); break;
      case IK_INCENDIARY_DART: affect_burning(def); break;
      case IK_JAVELIN: buffs_of(def)->cripple = 10; break;
      case IK_TOMAHAWK: buffs_of(def)->bleed = dmg > 255 ? 255 : dmg; break;
    }
    if (mw && !thrown && mw->kind != IK_BOOMERANG && --mw->qty == 0) *mw = (Item){ 0 };   // a held one is used up with each blow
  }
  if (def >= 0 && g_mh.defense_proc) dmg = g_mh.defense_proc(def, att, dmg);  // Mob.defenseProc (Swarm splits...)
  if (def < 0 && att >= 0 && ring_worn(IK_RNG_THORNS, NULL)) { int t = rnd_range(0, dmg); if (t > 0) mob_damage(att, t); }   // Ring of Thorns
  if (def < 0) {                                         // Hero.defenseProc: herbal armour, then the armour's glyph
    if (G.herbal) { if (dmg >= G.herbal) { dmg -= G.herbal; G.herbal = 0; } else { G.herbal -= dmg; dmg = 0; } }
    if (G.hero.armor.kind && G.hero.armor.ench) dmg = ench_armor_proc(att, dmg);
    if (G.hero.armor.kind) armor_learn(&G.hero.armor);
  }
    if (def < 0) {
    const char *nm = mob_name(am->type);                 // ResultDescriptions.BOSS / MOB with Utils.indefinite
    bool boss = (MOB_DEFS[am->type].flags & MD_BOSS) || am->type == MT_ROTTING_FIST || am->type == MT_BURNING_FIST;
    char cause[64]; snprintf(cause, sizeof cause, boss ? "Killed by the %s on level %%d" : VOWEL(nm[0]) ? "Killed by an %s on level %%d" : "Killed by a %s on level %%d", nm);
    if (am->type == MT_WARLOCK || am->type == MT_EYE || am->type == MT_BURNING_FIST) dmg = elem_resist(-1, dmg);   // RingOfElements.FULL
    hero_damage(dmg, cause);
    interrupt();                                         // Char.attack: Dungeon.hero.interrupt()
    if (G.hero.HP <= 0) { char n[32]; snprintf(n, sizeof n, "%s", mob_name(am->type)); cap_first(n); LOG(C_BAD, "%s killed you...", n); }
  } else {
    mob_damage(def, dmg);
    if (dm->HP <= 0 && visible && att < 0 && !(dd->flags & MD_NPC)) LOG(C_INFO, "You defeated %s.", mob_name(dm->type));   // Char.TXT_DEFEAT "%s defeated %s"; a shopkeeper flees alive (Shopkeeper.damage)
  }
  return true;
}

// ---------------- Traps (levels/traps) ----------------
static void trigger_trap(int cell, int who) { trap_ovl(cell, who); }   // the "trap" overlay (src/ovl/trap)
static bool is_trap(int t) {
  int d = terrain_discover(t);
  return d == T_POISON_TRAP || d == T_ALARM_TRAP || d == T_LIGHTNING_TRAP || d == T_GRIPPING_TRAP || d == T_SUMMONING_TRAP ||
         d == T_TOXIC_TRAP || d == T_FIRE_TRAP || d == T_PARALYTIC_TRAP;
}
void mob_press(int i) {                         // Level.mobPress: visible traps only; doors, grass, plants
  int c = G.mobs[i].pos, t = G.map[c];
  if ((TFLAGS(t) & F_PIT) && !(MOB_DEFS[G.mobs[i].type].flags & MD_FLY)) {   // Chasm.mobFall: destroyed
    int h = heap_at(CARRIED_POS(G.mobs[i].uid)), rc; if (h >= 0 && (rc = random_respawn_cell()) >= 0) { G.heaps[h].pos = rc; G.heaps[h].type = HEAP_PLAIN; }   // 0.50: what a thief stole lands somewhere on the floor at once (was: only once you came back)
    G.mobs[i].HP = 0; return; }
  if (t == T_DOOR) { set_terrain(c, T_OPEN_DOOR); if (BIT_GET(G.fov, c)) ui_sound(SND_DOOR_OPEN); }
  else if (is_trap(t) && !(TFLAGS(t) & F_SECRET)) trigger_trap(c, i);
  if (G.mobs[i].HP > 0 && plant_at(c) >= 0) plant_ovl(c, i);
}

// ---------------- Hero: search, moving, levels ----------------
bool hero_search(bool intentional) {                   // Hero.search
  int a = hero_awareness();
  int level = intentional ? (2 * a - a * a / 1000) : a;            // x1000
  bool found = false;
  int pos = 0, neg = 0;                                              // Ring of Detection: a wider (or duller) search
  for (int i = 0; i < 2; i++) if (G.hero.ring[i].kind == IK_RNG_DETECTION) {
    int b = ring_level(&G.hero.ring[i]); if (b > pos) pos = b; else if (b < 0) neg += b;
  }
  int dist = 1 + pos + neg;
  if (dist <= 0) { level /= 2 - dist; dist = 1; }
  int x0 = cx(G.hero.pos), y0 = cy(G.hero.pos);
  for (int y = y0 - dist; y <= y0 + dist; y++)
    for (int x = x0 - dist; x <= x0 + dist; x++) {
      if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
      int p = x + y * MAP_W;
      if (!BIT_GET(G.fov, p)) continue;
      if ((TFLAGS(G.map[p]) & F_SECRET) && (intentional || rnd_int(1000) < level)) {
        set_terrain(p, terrain_discover(G.map[p]));
        BIT_SET(G.visited, p);
        found = true;
      }
      if (intentional) for (int h = 0; h < G.n_heaps; h++)    // Heap.Type.HIDDEN (the phantom fish) shows itself
        if (G.heaps[h].pos == p && G.heaps[h].type == HEAP_HIDDEN) { G.heaps[h].type = HEAP_PLAIN; found = true; }
    }
  if (intentional) {
    ui_fx(FX_SEARCH, G.hero.pos, -1, "search");
    hero_spend(found ? (rnd_int(1000) < level ? 2 * TICK : 4 * TICK) : 2 * TICK);
  }
  if (found) { ui_log("You noticed something", C_WARN); ui_sound(SND_SECRET); interrupt(); }
  return found;
}

void switch_level(int depth, int arrive);

int plant_at(int c) { for (int i = 0; i < G.n_plants; i++) if (G.plants[i].pos == c) return i; return -1; }
Item random_seed(void) { return item_make(IK_FIRST_SEED + rnd_int(N_SEEDS - 1), 1); }   // Generator.Category.SEED (no Rotberry)
void trample(int c, int hl) {                           // HighGrass.trample
  set_terrain(c, T_GRASS);
  if (hl >= 0) {
    if (rnd_int(18) <= rnd_int(hl + 1)) heap_put(c, random_seed());
    if (rnd_int(6) <= rnd_int(hl + 1)) heap_put(c, item_make(IK_DEW, 1));
  }
}
void hero_press(int c) {                        // Level.press
  int t = G.map[c];
  if (hero_flying()) { if (t == T_DOOR) set_terrain(c, T_OPEN_DOOR); return; }   // levitating: nothing underfoot triggers (Char.move: a flier opens doors)
  if (TFLAGS(t) & F_PIT) {                             // Chasm.heroFall
    G.jump_confirmed = false;
    ui_sound(SND_FALLING);
    interrupt();
    switch_level(G.depth + 1, ARRIVE_FALL);
    return;
  }
  if (is_trap(t)) {
    if (TFLAGS(t) & F_SECRET) ui_log(txt(TX_L_PLATE), C_WARN);
    interrupt();
    trigger_trap(c, -1);
    return;
  }
  if (t == T_WELL) feature_ovl(c, -1);                 // WellWater.affectCell
  if (t == T_HIGH_GRASS) {                             // HighGrass.trample with Ring of Herbalism; a warden's barkskin
    int hl = 0; ring_worn(IK_RNG_HERBALISM, &hl);       // Hero.buff(Herbalism): one ring's level, not the sum
    trample(c, hl);
    if (G.hero.subclass == SUB_WARDEN && G.hero.b.barkskin < G.hero.HT / 3) G.hero.b.barkskin = G.hero.HT / 3;
  }
  else if (t == T_DOOR) { set_terrain(c, T_OPEN_DOOR); ui_sound(SND_DOOR_OPEN); }
  if (plant_at(c) >= 0) { interrupt(); plant_ovl(c, -1); }
  if (g_mh.press) g_mh.press(c, -1);                   // boss arenas
}

static void hero_move(int step) {                      // Char.move + Hero.move + onMotionComplete
  Hero *h = &G.hero;
  int16_t old = h->pos;
  if (h->vertigo && adjacent(step, old)) {              // Char.move with Vertigo: a random stumble, or none
    step = old + N8[rnd_int(8)];
    if (!nb_ok(old, step) || !(PASSABLE(G.map[step]) || (TFLAGS(G.map[step]) & F_AVOID)) || mob_at(step) >= 0) { hero_spend(hero_move_cost()); return; }
  }
  if (cx(step) != cx(old)) h->facing_left = cx(step) < cx(old);
  if (G.map[old] == T_OPEN_DOOR && heap_at(old) < 0) set_terrain(old, T_DOOR);
  int mi = mob_at(step);
  if (mi >= 0 && hero_side(mi)) { G.mobs[mi].pos = old; }    // MirrorImage.interact: swap places
  h->pos = step;
  hero_spend(hero_move_cost());
  if (!hero_flying()) ui_sound(IS_WATER(G.map[step]) ? SND_WATER : SND_STEP);   // Hero.move
  g_level_changed = false;
  hero_press(step);
  if (g_level_changed || h->HP <= 0) return;
  update_fov();
  hero_search(false);
}

static int16_t s_jump_cell = -1;
// Hero.getCloser: one step towards target over explored ground. Returns true when it moved.
static bool hero_get_closer(int target) {
  Hero *h = &G.hero;
  if (h->b.roots) return false;                        // (Camera shake only)
  int step = -1;
  bool fly = hero_flying();
  if (adjacent(h->pos, target)) {
    int mi = mob_at(target);
    if (mi < 0 || hero_side(mi)) {
      if ((TFLAGS(G.map[target]) & F_PIT) && !fly && !G.jump_confirmed) { s_jump_cell = target; ui_confirm_jump(); interrupt(); return false; }
      if (PASSABLE(G.map[target]) || (TFLAGS(G.map[target]) & F_AVOID)) step = target;
    }
  } else {
    for (int i = 0; i < MAP_LEN; i++)
      if ((PASSABLE(G.map[i]) || (fly && (TFLAGS(G.map[i]) & F_AVOID))) && BIT_GET(G.visited, i)) BIT_SET(s_pass, i); else BIT_CLR(s_pass, i);
    mark_chars(true);
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && hero_side(i)) BIT_SET(s_pass, G.mobs[i].pos);   // walk through your images
    step = get_step(h->pos, target);
  }
  if (step < 0) return false;
  hero_move(step);
  return true;
}

static void hero_turn_end(void) { if (!g_level_changed) process(); }

static void read_sign(void) {                          // Sign.read: a tip on each of the first 21 floors; deeper, it burns
  if (G.depth <= 21) ui_message("Sign", txt(TX_SIGN_1 + G.depth - 1));
  else { set_terrain(G.hero.pos, T_EMBERS); ui_sound(SND_BURNING); ui_log(txt(TX_L_SIGN_BURN), C_WARN); }
}

// One act of the hero's current action (Hero.act -> actMove / actAttack / actPickUp / ...)
static ActResult hero_act(void) {
  Hero *h = &G.hero;
  int target = G.action_cell;
  g_level_changed = false;
  switch (G.action) {
    case HA_MOVE:
      if (G.map[target] == T_ALCHEMY && target != h->pos && BIT_GET(G.fov, target)) { G.action = HA_NONE; g_trade_arg = target; ui_pick_item(PICK_SEED); return ACT_NONE; }   // Hero.actCook: once it is in sight
      if (hero_get_closer(target)) break;
      if (h->pos == target && G.map[h->pos] == T_SIGN) read_sign();
      G.action = HA_NONE;
      return ACT_NONE;
    case HA_UNLOCK: {                                  // Hero.actUnlock: an iron key for a door, the skeleton key for the exit
      if (!adjacent(h->pos, target)) { if (hero_get_closer(target)) break; G.action = HA_NONE; return ACT_NONE; }
      G.action = HA_NONE;
      bool exit = G.map[target] == T_LOCKED_EXIT;
      uint8_t *k = &G.keys[G.depth];
      if (exit ? !(*k & 64) : !(*k & 15)) { ui_log(txt(TX_L_NO_KEY), C_WARN); return ACT_NONE; }
      if (exit) *k &= ~64; else (*k)--;
      set_terrain(target, exit ? T_UNLOCKED_EXIT : T_DOOR);
      ui_sound(SND_UNLOCK); ui_fx(FX_OPERATE, target, -1, NULL);
      hero_spend(TICK);
      hero_turn_end();
      return ACT_TURN;
    }
    case HA_TALK: {
      int mi = target & 31;
      Mob *m = &G.mobs[mi];
      if (m->HP <= 0 || m->uid != (target >> 5)) { G.action = HA_NONE; return ACT_NONE; }
      if (adjacent(h->pos, m->pos)) {
        G.action = HA_NONE;
        if (m->type == MT_SHEEP) {                       // Sheep.interact
          static const char *const BAA[4] = { "Baa!", "Baa?", "Baa.", "Baa..." };
          ui_fx(FX_TEXT, m->pos, mi, BAA[rnd_int(4)]); return ACT_NONE;
        }
        return g_mh.interact ? g_mh.interact(mi) : ACT_NONE;
      }
      if (BIT_GET(G.fov, m->pos) && hero_get_closer(m->pos)) break;   // actInteract: while it is in sight
      G.action = HA_NONE;
      return ACT_NONE;
    }
    case HA_ATTACK: {
      int mi = target & 31;
      Mob *m = &G.mobs[mi];
      if (m->HP <= 0 || m->uid != (target >> 5)) { G.action = HA_NONE; return ACT_NONE; }
      if (adjacent(h->pos, m->pos) && h->b.charm && m->uid == G.charm_uid) { G.action = HA_NONE; return ACT_NONE; }   // isCharmedBy
      if (adjacent(h->pos, m->pos)) {
        attack(-1, mi, NULL);
        dispel_invisibility();                           // Hero.onAttackComplete: a blow, landed or not (a throw keeps it)
        hero_spend(attack_delay(G.hero.weapon.kind ? &G.hero.weapon : NULL));
        G.action = HA_NONE;
        hero_turn_end();
        return ACT_TURN;
      }
      if (BIT_GET(G.fov, m->pos) && hero_get_closer(m->pos)) break;
      G.action = HA_NONE;
      return ACT_NONE;
    }
    case HA_PICKUP: case HA_OPEN: {
      int hp = heap_at(target);
      bool chest = hp >= 0 && G.heaps[hp].type != HEAP_PLAIN;
      if (h->pos == target || (chest && adjacent(h->pos, target))) {
        G.action = HA_NONE;
        ActResult r = chest ? open_heap(target) : pick_up_ex(target, false);
        if (r == ACT_TURN) hero_turn_end();
        return r;
      }
      if (hero_get_closer(target)) break;
      G.action = HA_NONE;
      return ACT_NONE;
    }
    case HA_DESCEND: case HA_ASCEND:
      if (h->pos == target) {
        bool up = G.action == HA_ASCEND;
        G.action = HA_NONE;
        if (!game_starving() && !(up && G.depth == 1) && !(!up && G.depth >= LAST_DEPTH)) { h->hunger += 36; if (h->hunger > 360) h->hunger = 360; }   // actDescend / actAscend: STARVING / 10
        if (up) {
          if (G.depth == 1) {                          // Hero.actAscend: only with the Amulet; then home
            if (game_bag_find(IK_AMULET) < 0) { ui_message(NULL, txt(TX_ONE_DOES_NOT_SIMPLY)); return ACT_NONE; }
            ui_amulet(2); return ACT_NONE;
          }
          switch_level(G.depth - 1, ARRIVE_EXIT);
          return ACT_LEVEL;
        }
        if (G.depth >= LAST_DEPTH) { ui_message(NULL, txt(TX_THE_STAIRS_LEAD_DOWN)); return ACT_NONE; }
        switch_level(G.depth + 1, ARRIVE_ENTRANCE);
        return ACT_LEVEL;
      }
      if (hero_get_closer(target)) break;
      G.action = HA_NONE;
      return ACT_NONE;
    case HA_REST:                                      // Hero.act with restoreHealth
      if (G.interrupted || h->HP >= h->HT || game_starving()) { G.action = HA_NONE; return ACT_NONE; }
      hero_spend(TICK);
      hero_turn_end();
      if (G.hero.HP <= 0 || G.interrupted) { G.action = HA_NONE; return ACT_TURN; }
      return ACT_WALK;
    default:
      return ACT_NONE;
  }
  // moved one step
  if (g_level_changed) { G.action = HA_NONE; return ACT_LEVEL; }
  hero_turn_end();
  if (G.hero.HP <= 0) { G.action = HA_NONE; return ACT_TURN; }
  return ACT_WALK;
}

// ---------------- Public actions ----------------
ActResult game_tap_cell(int16_t c) {                   // Hero.handle
  g_trade = 0;
  mreg_ensure();
  if (G.hero.HP <= 0 || c < 0 || c >= MAP_LEN) return ACT_NONE;
  G.interrupted = false;
  int mi = mob_at(c);
  int hp = heap_at(c);
  if (G.map[c] == T_ALCHEMY && c != G.hero.pos) { G.action = HA_MOVE; G.action_cell = c; }   // HeroAction.Cook (see HA_MOVE)
  else if (mi >= 0 && BIT_GET(G.fov, c) && (G.mobs[mi].type == MT_SHEEP || (MOB_DEFS[G.mobs[mi].type].flags & MD_NPC))) {
    G.action = HA_TALK; G.action_cell = (G.mobs[mi].uid << 5) | mi;   // HeroAction.Interact: walk up, then NPC.interact
  }
  else if (mi >= 0 && BIT_GET(G.fov, c) && !ally(mi)) { G.action = HA_ATTACK; G.action_cell = (G.mobs[mi].uid << 5) | mi; }
  else if (hp >= 0 && BIT_GET(G.visited, c)) { G.action = G.heaps[hp].type == HEAP_PLAIN ? HA_PICKUP : HA_OPEN; G.action_cell = c; }
  else if (G.map[c] == T_LOCKED_DOOR || G.map[c] == T_LOCKED_EXIT) { G.action = HA_UNLOCK; G.action_cell = c; }   // HeroAction.Unlock
  else if (c == G.exit) { G.action = HA_DESCEND; G.action_cell = c; }
  else if (c == G.entrance && G.map[c] == T_ENTRANCE) { G.action = HA_ASCEND; G.action_cell = c; }   // sealed (Goo, Yog): no way up
  else { G.action = HA_MOVE; G.action_cell = c; G.last_action = HA_NONE; }
  return hero_act();
}
ActResult game_resume(void) {                          // Hero.resume
  mreg_ensure();
  if (!G.last_action || G.hero.HP <= 0) return ACT_NONE;
  G.action = G.last_action; G.action_cell = G.last_cell; G.last_action = HA_NONE; G.interrupted = false;
  return hero_act();
}
int game_target(int after, bool line, int *count) {   // 0.18: the next enemy in range after mob `after` (wrapping), and how many
  int n = 0, first = -1, next = -1;                     // line: in the line of fire (throws, wands); else in sight (melee walks there)
  bool hit;
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (line && ballistica(G.hero.pos, m->pos, &hit) != m->pos)) continue;
    n++;
    if (first < 0) first = i;
    if (i > after && next < 0) next = i;
  }
  if (count) *count = n;
  return next >= 0 ? next : first;
}
ActResult game_continue(void) {
  mreg_ensure();
  if (G.interrupted || G.action == HA_NONE || G.hero.HP <= 0) { G.action = HA_NONE; G.interrupted = false; return ACT_NONE; }
  return hero_act();
}
ActResult game_jump(void) {                            // "Yes, I know what I'm doing"
  mreg_ensure();
  if (s_jump_cell < 0 || !adjacent(G.hero.pos, s_jump_cell)) return ACT_NONE;
  G.jump_confirmed = true;
  G.interrupted = false;
  G.action = HA_MOVE; G.action_cell = s_jump_cell;
  s_jump_cell = -1;
  return hero_act();
}
ActResult game_rest(bool till_healthy) {               // Hero.rest
  mreg_ensure();
  if (G.hero.HP <= 0) return ACT_NONE;
  G.interrupted = false;
  G.action = till_healthy ? HA_REST : HA_NONE;           // (whatever was under way was called off: GameScene.cancel)
  if (till_healthy) {
    hero_spend(TICK);
    process();
    return G.interrupted || G.hero.HP >= G.hero.HT ? ACT_TURN : ACT_WALK;
  }
  ui_fx(FX_TEXT, G.hero.pos, -1, "...");
  hero_spend(TICK);
  process();
  return ACT_TURN;
}
ActResult game_search(void) {
  mreg_ensure();
  if (G.hero.HP <= 0) return ACT_NONE;
  G.action = HA_NONE;
  ui_fx(FX_OPERATE, G.hero.pos, -1, NULL);
  ui_fx(FX_SEARCHED, G.hero.pos, -1, NULL);
  hero_search(true);
  if (g_srch_t != G.level_t0) { memset(g_srchd, 0, sizeof g_srchd); g_srch_t = G.level_t0; }   // 0.40: where this floor was searched
  BIT_SET(g_srchd, G.hero.pos);                        // (the target list's search spots: once each; a search finds all beside)
  process();
  return ACT_TURN;
}

uint8_t g_view[4], g_drop_i; int16_t g_from = -1, g_drop[4], g_goal = -1, g_tgobj[TARGETS_MAX]; uint8_t g_hl_order[8]; Ovr g_ovr = { 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// Ballistica: straight line from the hero, stopping at the first character or wall
int16_t ballistica(int16_t from, int16_t to, bool *hit_mob) {
  int x0 = cx(from), y0 = cy(from), x1 = cx(to), y1 = cy(to);
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  int16_t last = from;
  *hit_mob = false;
  for (;;) {
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
    int16_t c = y0 * MAP_W + x0;
    if (mob_at(c) >= 0) { *hit_mob = true; return c; }
    if (!(PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID))) return last;
    if (BIT_GET(G.los, c)) return c;                     // Ballistica: it stops in the grass / doorway
    last = c;
  }
  return last;
}

// Ballistica.cast(from, to, magic, hitChars): the cells a bolt crosses. A magic bolt flies on past the target until
// something stops it: a wall (the bolt ends before it), sight-blocking terrain or (if hit_chars) a character
// (the bolt ends on it). trace[0] is 'from'; returns the number of cells in trace, the bolt's end is the last.
int ballistica_cast(int16_t from, int16_t to, bool magic, bool hit_chars, int16_t *trace) {
  int x0 = cx(from), x1 = cx(to), y0 = cy(from), y1 = cy(to);
  int dx = x1 - x0, dy = y1 - y0, sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
  dx = abs(dx); dy = abs(dy);
  int sa, sb, da, db;
  if (dx > dy) { sa = sx; sb = sy * MAP_W; da = dx; db = dy; } else { sa = sy * MAP_W; sb = sx; da = dy; db = dx; }
  int n = 1, cell = from, err = da / 2;
  trace[0] = from;
  while ((cell != to || magic) && n < BOLT_MAX) {
    cell += sa; err += db;
    if (err >= da) { err -= da; cell += sb; }
    if (!in_map(cell)) break;
    trace[n++] = cell;
    if (!(PASSABLE(G.map[cell]) || (TFLAGS(G.map[cell]) & F_AVOID))) return n - 1;
    if ((TFLAGS(G.map[cell]) & F_LOS_BLOCKING) || (hit_chars && char_at(cell))) return n;
  }
  return n;
}
// Level.press with nobody on the cell (a Wand of Reach's wave): traps spring, grass is trampled, doors open
void level_press_nobody(int c) {                // Level.press(cell, null)
  int t = G.map[c];
  if (t == T_WELL || t == T_ALCHEMY) { feature_ovl(c, -2); return; }
  if (is_trap(t)) { if (TFLAGS(t) & F_SECRET) ui_log(txt(TX_L_PLATE), C_WARN); trigger_trap(c, -2); }
  else if (t == T_HIGH_GRASS) trample(c, 0);
  else if (t == T_DOOR) set_terrain(c, T_OPEN_DOOR);
  if (plant_at(c) >= 0) plant_ovl(c, -2);               // Plant.activate(null)
}

// ---------------- Wands (Wand.zapper) ----------------
int wand_power(const Item *w) {                         // Wand.power: level, plus a worn Ring of Power
  int l = item_broken(w) ? 0 : w->lvl, p;
  if (ring_worn(IK_RNG_POWER, &p)) { l += p; if (l < 0) l = 0; }
  return l;
}
// ---------------- Potions ----------------
// cells within n steps of cell through anything that doesn't block sight (PathFinder.buildDistanceMap)

// ---------------- Scrolls ----------------

bool game_pick_ok(PickMode mode, int slot) {           // WndBag.Mode filters
  Item *it = game_slot(slot);
  if (!it->kind) return false;
  switch (mode) {
    case PICK_SELL: return item_sellable(it->kind) && !(slot < 0 && (it->flags & IF_CURSED));
    case PICK_SEED: return slot >= 0 && item_cat(it->kind) == CAT_SEED;
    case PICK_IDENTIFY: return !item_identified(it);
    case PICK_UPGRADE: return item_upgradable(it->kind);
    case PICK_REFORGE: return item_cat(it->kind) == CAT_WEAPON && it != &G.hero.weapon;
    case PICK_SMITH: return item_upgradable(it->kind);   // WndBag.Mode.UPGRADEABLE
    case PICK_ARMOR: return item_cat(it->kind) == CAT_ARMOR;
    case PICK_WAND: return item_cat(it->kind) == CAT_WAND && it->kind != IK_WND_MM;
    case PICK_ENCHANT: if (item_cat(it->kind) == CAT_ARMOR) return true;   // MeleeWeapon, Boomerang or Armor
      /* fall through */
    case PICK_WEAPON: return (item_cat(it->kind) == CAT_WEAPON && it->kind != IK_PICKAXE) || it->kind == IK_BOOMERANG;   // MeleeWeapon or Boomerang
    case PICK_RING: return slot <= -3;
    case PICK_QUICK: return slot >= 0 && item_quickable(game_slot(slot)->kind);
  }
  return false;
}

// ---------------- Items: actions ----------------
int game_auto_target(void) {                           // QuickSlot.useTargeting: the last target if in the line of fire, else the first
  int n, i = game_target(-1, true, &n);
  for (int k = 0; k < n; k++, i = game_target(i, true, NULL)) if (G.mobs[i].uid == G.quick_uid) return i;
  i = game_target(-1, true, NULL);
  G.quick_uid = i >= 0 ? G.mobs[i].uid : -1;
  return i;
}
int game_nearest_enemy(void) {
  int best = -1, bd = 999;
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos)) continue;
    int d = distance(m->pos, G.hero.pos);
    if (d < bd) { bd = d; best = m->pos; }
  }
  return best;
}






// ---------------- Floors (the work is in the "floor" overlay: src/ovl/floor) ----------------
// Taking stairs or falling only books the move: the screen fades out on the old floor first, then
// game_level_commit() builds the new one (InterlevelScene).
int s_pend_depth, s_pend_arrive;
void switch_level(int depth, int arrive) {
  s_pend_depth = depth; s_pend_arrive = arrive;
  g_level_changed = true;
}
bool game_level_pending(void) { return s_pend_depth != 0; }
void game_level_commit(void) {
  if (!s_pend_depth) return;
  int d = s_pend_depth; s_pend_depth = 0;
  mreg_load(-1);                                       // level_build brings the new region's creatures, after its floor is made
  floor_op(FO_GO, d, (void *)(intptr_t)s_pend_arrive);
}
uint8_t g_new_class;
void game_new(void) { mreg_load(-1); newgame_ovl(); }
bool game_load(void) { mreg_load(-1); return floor_op(FO_LOAD, 0, NULL) > 0; }
void game_save(void) { snd_poll(); floor_op(FO_SAVE, 0, NULL); snd_poll(); }   // flash writes are slow: the speaker topped up either side
int game_save_size(void) { return floor_op(FO_SAVE_SIZE, 0, NULL); }
void game_rank(int win) { floor_op(FO_RANK, win, NULL); }
void game_erase(void) { persist_delete(1); floor_op(FO_ERASE, 0, NULL); }   // KEY_LEN first: the save is gone even if the overlay can't load
bool game_has_save(uint8_t *depth) { uint8_t b[2]; int r = floor_op(FO_PEEK, 0, b); *depth = b[0]; return r > 0; }
bool game_peek_save(uint8_t *depth, uint8_t *lvl) { uint8_t b[2]; int r = floor_op(FO_PEEK, 0, b); *depth = b[0]; *lvl = b[1]; return r > 0; }

// ---------------- UI helpers ----------------
// PD AttackIndicator: keep the last target while visible, else the nearest visible enemy

// PD WndInfoMob / WndInfoItem / WndInfoCell
