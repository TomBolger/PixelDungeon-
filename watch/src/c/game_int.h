/*
 * Pixel Dungeon for Pebble - game.c internals shared with the overlays (src/ovl/...).
 * Everything declared here must stay a real (non-static) function or variable in game.c.
 */
#pragma once
#include "pd.h"

#ifndef CELLS_SIGNED                                 // 0.61 (0.63: everywhere): cells are never negative: shifts and masks (smaller)
static inline int cx(int c) { return c & (MAP_W - 1); }
static inline int cy(int c) { return c >> 5; }
#else
static inline int cx(int c) { return c % MAP_W; }
static inline int cy(int c) { return c / MAP_W; }
#endif
static inline bool adjacent(int a, int b) { return a != b && abs(cx(a) - cx(b)) <= 1 && abs(cy(a) - cy(b)) <= 1; }
static inline int distance(int a, int b) { int dx = abs(cx(a) - cx(b)), dy = abs(cy(a) - cy(b)); return dx > dy ? dx : dy; }
static inline bool in_map(int c) { return c >= 0 && c < MAP_LEN; }
static inline bool nb_ok(int c, int n) { return in_map(n) && abs(cx(n) - cx(c)) <= 1; }   // no wrap across rows
extern char g_log[120];
#define LOG(color, ...) do { snprintf(g_log, sizeof g_log, __VA_ARGS__); ui_log(g_log, color); } while (0)

// state that outlives an overlay call
extern PickMode g_pick_mode; extern bool g_pick_unknown; extern int g_reforge_slot;
extern bool g_level_changed;

// PathFinder buffers and the 8 neighbours in PD order
extern uint8_t s_pass[MAP_LEN / 8];
extern int16_t *s_dist, *s_queue;
extern const int16_t N8[8];

// game.c functions the overlays call
void affect_burning(int who);
void affect_frost(int who, int turns);
void affect_poison(int who, int turns);
bool ally(int i);
bool bag_add(Item it);
int bag_region(int kind);                      // the owned bag (0 pouch, 1 holder, 2 holster) that takes this kind, or -1
void bags_sync(void);
void bag_take_one(int slot);
void blob_clear(int b, int cell);
void blob_seed(int b, int cell, int amount);
bool char_at(int c);
bool discoverable(int c);
void dispel_invisibility(void);
bool distance_map(int from, int stop);
void mark_chars(bool only_visible);            // s_pass: the creatures (those in sight) and the hero block the way
void earn_exp(int exp);
void eat_satisfy(int energy);
void heap_freeze(int h);
void heap_put(int16_t c, Item it);
int hero_attack_skill(const Item *ranged, int target_pos);
int hero_damage_roll(const Item *ranged);
static inline void mirror_dup(int i) {        // MirrorImage.duplicate: the hero's numbers now (aux: attack skill, aux2: damage)
  int d = hero_damage_roll(NULL); G.mobs[i].aux = hero_attack_skill(NULL, G.hero.pos); G.mobs[i].aux2 = d > 255 ? 255 : d;
}
void hero_press(int c);
void hero_spend(int t);
void mob_damage(int i, int dmg);
void mob_kill(int i);                          // Mob.die: no damage, straight to death (and its loot)
void mob_notice(int i);
void mob_beckon(int i, int cell);
void process(void);
int random_respawn_cell(void);
void set_known(int kind);
void set_terrain(int16_t c, uint8_t t);
void short_name(const Item *it, char *buf, int len);
int spawn_mob(int type, int pos, int state, int32_t delay);
void update_fov(void);
int view_distance(void);                        // the hero's (Level.viewDistance, Light)
void potion_shatter(int k, int cell);                 // ovl.c: into the "use" overlay
int gold_amount(void);                               // Gold.random for this depth
int bestiary(int depth);                             // Bestiary.mobClass
static inline int bestiary_mob(int depth) {          // Bestiary.mob: a new floor's, a summoning trap's (no rare kinds: those are Bestiary.mutable's)
  int t = bestiary(depth);
  return t == MT_ALBINO ? MT_RAT : t == MT_BANDIT ? MT_THIEF : t == MT_SHIELDED ? MT_BRUTE : t == MT_SENIOR ? MT_MONK : t == MT_ACIDIC ? MT_SCORPIO : t;
}
void heap_put_t(int16_t c, Item it, int type);
// ovl.c, the "ench" overlay: Weapon.Enchantment.proc on a monster, Armor.Glyph.proc against one (returns the damage)
void ench_weapon_proc(int def, int dmg, Item *w);   // w: the weapon in hand or the thrown one
int ench_armor_proc(int att, int dmg);
bool hero_search(bool intentional);             // game.c: Hero.search (RingOfDetection.doEquip: search(false))
void mob_press(int i);                               // Level.mobPress
void hero_damage(int dmg, const char *cause_fmt);   // cause_fmt: "Killed by ... on level %d"
// 0.7: wands and rings
#define BOLT_MAX 40
int ballistica_cast(int16_t from, int16_t to, bool magic, bool hit_chars, int16_t *trace);
void level_press_nobody(int c);
void level_destroy(int c);
int ring_sum(int k);
bool ring_worn(int k, int *lvl);
int scale_pow(int v, int num, int den, int n);
int elem_duration(int who, int d);
int wand_power(const Item *w);
void wand_effect(int k, int power, int level, const int16_t *trace, int n);   // ovl.c -> the "wand" overlay
ActResult pick_up_ex(int c, bool reach);
ActResult open_heap(int c);

bool hero_flying(void);
void affect_paralysis(int who, int turns);

// 0.8: the "floor" overlay (saves, floor changes, new game) and what it needs from game.c
enum { ARRIVE_ENTRANCE, ARRIVE_EXIT, ARRIVE_FALL, ARRIVE_CELL = 16 };   // ARRIVE_CELL + cell: Lloyd's beacon
enum { FO_BUILD, FO_LOAD, FO_SAVE, FO_SAVE_SIZE, FO_ERASE, FO_PEEK, FO_GO, FO_RESURRECT, FO_RANK, FO_WHATIF };
int floor_op(int op, int a, void *p);                // ovl.c
void newgame_ovl(void);                              // ovl.c: the "newgame" overlay
extern int s_pend_depth, s_pend_arrive;
uint32_t mix(uint32_t a, uint32_t b, uint32_t salt);
void rnd_reseed_runtime(void);
void blob_free_all(void);
void build_los(void);
void respawn(void);
void check_visible_mobs(void);

// 0.8: creature behaviours. The "mobs" overlay (src/ovl/mobs) stays loaded and fills g_mh at start-up;
// game.c calls a hook when it is set and does PD's plain Mob behaviour otherwise.
enum { DS_PHYS, DS_BURN, DS_POISON, DS_GAS, DS_SHOCK, DS_DEATH, DS_PSI, DS_DISINT, DS_FIREBOLT, DS_LEECH, DS_COUNT };
enum { STAT_ATT, STAT_DEF, STAT_DMG, STAT_DR };
typedef struct {
  bool (*act)(int i);                          // the whole turn (true), else plain Mob.act
  int (*can_attack)(int i, int enemy, int epos);   // 1 / 0, or -1: adjacent and not charmed
  bool (*do_attack)(int i, int enemy);         // true: attacked (and spent its time) its own way
  int (*attack_proc)(int i, int def, int dmg); // Mob.attackProc (def: -1 hero)
  int (*defense_proc)(int i, int att, int dmg);// Mob.defenseProc
  int (*damage)(int i, int dmg, int src);      // Mob.damage extras (DS_* source)
  int (*stats)(int i, int which);               // STAT_*: -1 = the table's value (STAT_DMG: a fresh roll)
  void (*die)(int i);                          // Mob.die extras (before the table's loot)
  Item (*loot)(int i);                         // the loot when the table's chance comes up
  void (*spawned)(int i);
  void (*notice)(int i);                       // yells
  void (*moved)(int i);
  void (*turn)(void);                          // once a turn: level scripts
  ActResult (*interact)(int i);                // NPC.interact
  bool (*heap_open)(int h);                    // Heap.open for mimics and tombs (true: handled, the heap may be gone)
  void (*wnd_choice)(int i);                   // a quest window's button
  void (*press)(int cell, int who);            // Level.press extras (boss arenas); who -1 hero
  void (*buffs)(int who);                      // the buffs' turn (Buff.act), -1 the hero (0.17: moved out of the app)
  bool (*sees)(int i, int enemy);              // Mob.act's enemyInFOV: does creature i see its enemy (-1 the hero)
  int (*closer)(int i, int target);            // Mob.getCloser overridden: the cell to step towards, or CL_MOVED / CL_STAYS (handled)
} MobHooks;
enum { CL_MOVED = -2, CL_STAYS = -3 };
#define SEES_CELL(c) (-0x1000 - (c))           // sees(i, SEES_CELL(c)): is cell c in creature i's field of view (Level.fieldOfView)

extern MobHooks g_mh;
extern int16_t g_move_from;                    // the cell a creature just left (for the moved hook)
// 0.9: each region's creatures live in their own overlay (src/ovl/<region>), loaded for the floor's region.
// Its install(&g_mh, &g_mh_base) chains onto the common hooks kept in g_mh_base.
extern MobHooks g_mh_base;
void mreg_load(int region);                    // ovl.c; region 0..4, -1 unloads

extern int g_dmg_src;                          // set before mob_damage when the source matters
bool hero_side(int i);
bool mob_immune(int i, uint32_t flag);
void mob_act_default(int i);
bool mob_can_attack(int i, int enemy, int epos);
bool attack(int att, int def, Item *thrown);
bool char_hit(int att, int def, bool magic);   // Char.hit
void heap_remove(int h);
void interrupt(void);
int mob_flee_step(int i, int from);              // creature i's step away from 'from' (Dungeon.flee), -1 none
Buffs *buffs_of(int who);
void poison_tick(Buffs *b, int who);           // Poison.act
int16_t pos_of(int who);
bool alive(int who);
void char_damage(int who, int dmg, const char *cause);
int random_destination(void);
bool mob_paralysed(Mob *m);
void mob_move(int i, int step);
bool mob_get_closer(int i, int target);
int mob_move_cost(Mob *m);
void mob_spend(Mob *m, int t);
int mob_enemy(int i);
void mobs_ovl_init(void);                      // ovl.c
void trap_ovl(int cell, int who);              // ovl.c -> the "trap" overlay
int hero_dr(void);
int hero_str(void);                            // STR with Weakness
int elem_resist(int who, int dmg);
bool boss_depth(int d);
int plant_at(int c);
Item random_seed(void);
void trample(int c, int herbalism);
void plant_ovl(int cell, int who);             // ovl.c -> the "trap" overlay: Plant.activate (who: -1 hero, -2 nobody, mob)
void land_ovl(Item it, int cell);
void feature_ovl(int cell, int who);           // ovl.c -> the "trap" overlay: a well or the alchemy pot is pressed
void level_drop(int16_t c, Item it);            // Level.drop: heap_put, then the cell is pressed (traps, wells, the pot)              // ovl.c -> the "trap" overlay: a thrown seed, bomb, honeypot... lands
// the "gen" overlay: Generator.random (cat: GC_*, -1 any)
enum { GC_WEAPON, GC_ARMOR, GC_POTION, GC_SCROLL, GC_WAND, GC_RING, GC_SEED, GC_FOOD, GC_GOLD, GC_MISC, GC_MELEE };
Item gen_item(int cat);
void gen_reset(void);
void gen_hold(bool keep);                      // keep the overlay loaded across many gen_item calls
int random_unequipped(void);                   // Belongings.randomUnequipped: a bag slot or -1
// 0.9: the "hero" overlay (src/ovl/hero): ops for hero_op (ovl.c)
enum { HO_PICKUP, HO_OPEN, HO_ZAP, HO_THROW, HO_ACTIONS, HO_STASH, HO_CONFIRM, HO_DEBUG, HO_SELL, HO_TRADE, HO_COOK, HO_TOME, HO_APPLY, HO_LEAP, HO_ANKH };
extern int8_t g_trade;                         // a trade window is open: 1 buying (g_trade_arg = cell), 2 selling (slot)
extern int16_t g_trade_arg;
extern Item g_wnd_item;                        // the item whose description a window still needs (ovl.c adds it)
void ui_wnd_info(const Item *it);              // main.c: append an item's description to the open window
int hero_op(int op, int a, int b, void *p);
void item_use(Item *it);
int attack_delay(const Item *w);
int hero_move_cost(void);                       // 1 / Hero.speed() in ticks
int16_t ballistica(int16_t from, int16_t to, bool *hit_mob);
void switch_level(int depth, int arrive);
int game_nearest_enemy(void);
