/*
 * Pixel Dungeon for Pebble - shared definitions.
 * Rules port Pixel Dungeon (watabou, GPLv3).
 */
#pragma once
#include <pebble.h>
#include "text_ids.h"
#include "snd.h"
#if defined(PERF_PROBE) && defined(PBL_ROUND)
#undef PERF_PROBE                   // (Emery only: Gabbro has no room)
#endif
#ifdef PERF_PROBE                   // profiling builds only (tools/prof): frame / tick / speaker timings read over gdb
extern uint32_t g_perf[12];
uint32_t now_ms(void); void perf_add(int k, uint32_t t0); uint32_t perf_ms(uint32_t d);
#endif

#define MAP_W 32                    // PD Level.WIDTH / HEIGHT
#define MAP_H 32
#define MAP_LEN (MAP_W * MAP_H)
#define TS 16                       // world units per tile (sprite size at 1x)
#define MAX_MOBS 16
#ifdef TEST_BUILD
#define MAX_HEAPS 120               // the test kit's first floor holds one of everything
#else
#define MAX_HEAPS 48                // 0.9: shops lay out ~14 wares
#endif
#define MAX_DIFFS 128
#define MAX_PLANTS 16
#define MAX_ROOMS 60                // split() makes ~33 rooms, at most ~51
#define BAG_SIZE 19                 // Belongings.BACKPACK_SIZE
#define BAG_CAP 20                  // stored slots (0.5 saves had 20)
#define PACK0 32                    // game slot of the first bag (seed pouch) slot
#define ALL_SLOTS(s) for (int s = 0; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1)   // the backpack, then the bags
#define VIEW_DISTANCE 8
#define TICK 60                     // one PD turn (Actor.TICK = 1f) in integer time units
#if (defined(TEST_BUILD) || defined(DEBUG_TOOLS)) && !defined(PERF_PROBE) && !defined(WHATIF)
#define DEBUG_JUMP                  // test kit: long-press Down = next floor / map / teleport to the next place of interest
#endif
int region_of(int depth);   // 0 sewers .. 4 halls
void mreg_drop(void), mreg_ensure(void), mreg_spare(void);   // ovl.c: the depth band's creature overlay out of the way / back
#define LAST_DEPTH 26               // the whole dungeon: the Amulet lies on 26

// ---- Terrain (levels/Terrain.java, every code) ----
enum {
  T_CHASM = 0, T_EMPTY = 1, T_GRASS = 2, T_EMPTY_WELL = 3, T_WALL = 4, T_DOOR = 5, T_OPEN_DOOR = 6,
  T_ENTRANCE = 7, T_EXIT = 8, T_EMBERS = 9, T_LOCKED_DOOR = 10, T_PEDESTAL = 11, T_WALL_DECO = 12,
  T_BARRICADE = 13, T_EMPTY_SP = 14, T_HIGH_GRASS = 15,
  T_SECRET_DOOR = 16, T_TOXIC_TRAP = 17, T_SECRET_TOXIC_TRAP = 18, T_FIRE_TRAP = 19, T_SECRET_FIRE_TRAP = 20,
  T_PARALYTIC_TRAP = 21, T_SECRET_PARALYTIC_TRAP = 22, T_INACTIVE_TRAP = 23, T_EMPTY_DECO = 24,
  T_LOCKED_EXIT = 25, T_UNLOCKED_EXIT = 26, T_POISON_TRAP = 27, T_SECRET_POISON_TRAP = 28, T_SIGN = 29,
  T_ALARM_TRAP = 30, T_SECRET_ALARM_TRAP = 31, T_LIGHTNING_TRAP = 32, T_SECRET_LIGHTNING_TRAP = 33,
  T_WELL = 34, T_STATUE = 35, T_STATUE_SP = 36, T_GRIPPING_TRAP = 37, T_SECRET_GRIPPING_TRAP = 38,
  T_SUMMONING_TRAP = 39, T_SECRET_SUMMONING_TRAP = 40, T_BOOKSHELF = 41, T_ALCHEMY = 42,
  T_CHASM_FLOOR = 43, T_CHASM_FLOOR_SP = 44, T_CHASM_WALL = 45, T_CHASM_WATER = 46,
  T_WATER_TILES = 48, T_WATER = 63
};
enum { F_PASSABLE = 1, F_LOS_BLOCKING = 2, F_FLAMABLE = 4, F_SECRET = 8, F_SOLID = 16, F_AVOID = 32,
       F_LIQUID = 64, F_PIT = 128, F_UNSTITCHABLE = 256 };
extern const uint16_t TERRAIN_FLAGS[64];
#define TFLAGS(t) TERRAIN_FLAGS[(t)]
#define PASSABLE(t) (TERRAIN_FLAGS[(t)] & F_PASSABLE)
#define IS_WATER(t) ((t) >= T_WATER_TILES)
int terrain_discover(int t);                         // Terrain.discover: secret -> visible

// ---- Texts (text.c): long strings live in a resource, read on demand ----
// The pointer stays valid for the next 3 txt() calls (rotating buffers).
const char *txt(int id);
#define TXC(id) ((const char *)(uintptr_t)(id))       // a death cause kept in the text resource (hero_damage reads it only on death)

// ---- Random (mulberry32); PD's Random.* helpers ----
extern uint32_t g_rng;
uint32_t rnd_next(void);
int rnd_int(int n);                     // Random.Int(n): [0, n)
int rnd_int2(int lo, int hi);           // Random.Int(lo, hi): [lo, hi)
int rnd_range(int lo, int hi);          // Random.IntRange: [lo, hi]
int rnd_normal(int lo, int hi);         // Random.NormalIntRange
bool rnd_pct(int pct);                  // Random.Float() < pct / 100
int rnd_chances(const uint16_t *w, int n);   // Random.chances
uint32_t rnd_scaled(int max);           // Random.Float(max), scaled by 2^16

// ---- Rooms (levels/Room.java), kept after generation for spawning ----
enum { RT_NULL, RT_STANDARD, RT_ENTRANCE, RT_EXIT, RT_TUNNEL, RT_PASSAGE, RT_BOSS_EXIT, RT_RAT_KING,   // Room.Type
       RT_SHOP, RT_BLACKSMITH, RT_TREASURY, RT_ARMORY, RT_LIBRARY, RT_LABORATORY, RT_VAULT, RT_TRAPS, RT_STORAGE,
       RT_MAGIC_WELL, RT_GARDEN, RT_CRYPT, RT_STATUE, RT_POOL, RT_WEAK_FLOOR, RT_PIT, RT_ALTAR };
typedef struct { int8_t left, top, right, bottom; uint8_t type; int16_t cell; } Room;   // cell: what the painter left for the content (0.9)
enum { FEEL_NONE, FEEL_CHASM, FEEL_WATER, FEEL_GRASS };

// ---- Items (items.c) ----
typedef enum {
  IK_NONE, IK_GOLD, IK_DEW,
  IK_RATION, IK_PASTY, IK_MYSTERY_MEAT, IK_CHARGRILLED, IK_CARPACCIO,
  IK_DAGGER, IK_KNUCKLES, IK_SHORT_SWORD, IK_QUARTERSTAFF, IK_SPEAR, IK_MACE, IK_SWORD, IK_LONGSWORD,
  IK_BATTLE_AXE, IK_WAR_HAMMER, IK_GLAIVE,
  IK_DART, IK_INCENDIARY_DART, IK_CURARE_DART, IK_SHURIKEN, IK_JAVELIN, IK_TOMAHAWK,
  IK_CLOTH_ARMOR, IK_LEATHER_ARMOR, IK_MAIL_ARMOR, IK_SCALE_ARMOR, IK_PLATE_ARMOR,
  // Potion.potions order (ItemStatusHandler)
  IK_POT_HEALING, IK_POT_EXPERIENCE, IK_POT_TOXIC, IK_POT_FLAME, IK_POT_STRENGTH, IK_POT_PARALYTIC,
  IK_POT_LEVITATION, IK_POT_MIND, IK_POT_PURITY, IK_POT_INVIS, IK_POT_MIGHT, IK_POT_FROST,
  // Scroll.scrolls order
  IK_SCR_IDENTIFY, IK_SCR_MAPPING, IK_SCR_RECHARGING, IK_SCR_REMOVE_CURSE, IK_SCR_TELEPORT, IK_SCR_CHALLENGE,
  IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_PSIONIC, IK_SCR_MIRROR, IK_SCR_UPGRADE, IK_SCR_ENCHANT,
  // Wand.wands order (0.7)
  IK_WND_TELEPORT, IK_WND_SLOW, IK_WND_FIREBOLT, IK_WND_POISON, IK_WND_REGROWTH, IK_WND_BLINK,
  IK_WND_LIGHTNING, IK_WND_AMOK, IK_WND_REACH, IK_WND_FLOCK, IK_WND_DISINTEGRATION, IK_WND_AVALANCHE,
  // Ring.rings order (0.7)
  IK_RNG_MENDING, IK_RNG_DETECTION, IK_RNG_SHADOWS, IK_RNG_POWER, IK_RNG_HERBALISM, IK_RNG_ACCURACY,
  IK_RNG_EVASION, IK_RNG_SATIETY, IK_RNG_HASTE, IK_RNG_HAGGLER, IK_RNG_ELEMENTS, IK_RNG_THORNS,
  // 0.8: Generator.Category.SEED order, then everything else
  IK_SEED_FIREBLOOM, IK_SEED_ICECAP, IK_SEED_SORROWMOSS, IK_SEED_DREAMWEED, IK_SEED_SUNGRASS, IK_SEED_EARTHROOT,
  IK_SEED_FADELEAF, IK_SEED_ROTBERRY,
  IK_DEW_VIAL, IK_IRON_KEY, IK_GOLDEN_KEY, IK_SKELETON_KEY,
  IK_ROSE, IK_RAT_SKULL, IK_PICKAXE, IK_DARK_GOLD, IK_CORPSE_DUST, IK_PHANTOM_FISH, IK_DWARF_TOKEN,
  IK_ANKH, IK_MASTERY, IK_TORCH, IK_BEACON, IK_ARMOR_KIT, IK_AMULET, IK_WEIGHTSTONE, IK_BOMB, IK_HONEYPOT,
  IK_SEED_POUCH, IK_SCROLL_HOLDER, IK_WAND_HOLSTER, IK_OVERPRICED, IK_BOOMERANG,
  IK_WIPE_OUT,                          // 0.9
  IK_ARMOR_WARRIOR, IK_ARMOR_ROGUE, IK_ARMOR_MAGE, IK_ARMOR_HUNTRESS,   // 0.12: ClassArmor (dur = its DR, hits = its STR)
  IK_WND_MM,                            // 0.13: the mage's Wand of Magic Missile (wand effect index 13)
  IK_COUNT
} ItemKind;
#define CLASS_ARMOR(k) ((k) >= IK_ARMOR_WARRIOR && (k) <= IK_ARMOR_HUNTRESS)
#define IK_FIRST_POTION IK_POT_HEALING
#define N_POTIONS 12
#define IK_FIRST_SCROLL IK_SCR_IDENTIFY
#define N_SCROLLS 12
#define IK_FIRST_WAND IK_WND_TELEPORT
#define N_WANDS 12
#define IK_FIRST_RING IK_RNG_MENDING
#define N_RINGS 12
#define IK_FIRST_SEED IK_SEED_FIREBLOOM
#define N_SEEDS 8
_Static_assert(IK_COUNT <= 128, "Game.known is 128 bits");
typedef enum { CAT_GOLD, CAT_FOOD, CAT_DEW, CAT_WEAPON, CAT_ARMOR, CAT_MISSILE, CAT_POTION, CAT_SCROLL, CAT_WAND, CAT_RING,
               CAT_SEED, CAT_KEY, CAT_QUEST, CAT_MISC } ItemCat;
enum { IF_CURSED = 1, IF_CURSED_KNOWN = 2, IF_LEVEL_KNOWN = 4, IF_CHARGE_KNOWN = 8, IF_BLOODY = 16, IF_FAST = 32, IF_ACCURATE = 64 };   // IF_FAST / IF_ACCURATE: Weapon.Imbue (a weightstone)   // IF_BLOODY: the pickaxe has bat blood on it
// PD Item: quantity, level, curse and what the hero knows about them, durability, hits until identified,
// enchantment / glyph. Wands: ench = charges left, hits = zaps until identified. Rings: hits = turns worn
// until identified. 8 bytes: older saves store whole Items, so the layout must not change.
typedef struct { uint16_t qty; uint8_t kind, flags; int8_t lvl; uint8_t dur, hits, ench; } Item;
_Static_assert(sizeof(Item) == 8, "Item is saved raw");
enum { HEAP_PLAIN, HEAP_CHEST, HEAP_SKELETON, HEAP_LOCKED_CHEST, HEAP_CRYSTAL_CHEST, HEAP_TOMB, HEAP_MIMIC, HEAP_CARRIED,
       HEAP_HIDDEN, HEAP_FOR_SALE };   // 0.9: hidden (phantom fish), for sale (shops)
#define CARRIED_POS(uid) ((int16_t)(0x4000 | (uid)))   // a HEAP_CARRIED's pos: the item a creature holds (Thief, Mimic)
#define BAG_POS(r) ((int16_t)(0x4100 | (r)))           // a HEAP_CARRIED's pos: what a bag (0 pouch, 1 holder, 2 holster) holds while it is away from the hero

typedef enum { ACN_EAT, ACN_DRINK, ACN_READ, ACN_EQUIP, ACN_UNEQUIP, ACN_THROW, ACN_QUICK, ACN_DROP, ACN_REFORGE, ACN_ZAP,
               ACN_PLANT, ACN_LIGHT, ACN_APPLY, ACN_SET, ACN_RETURN, ACN_END, ACN_MINE, ACN_SPECIAL, ACN_DISENCHANT, ACN_SHATTER, ACN_COUNT } ItemAction;
extern const char *ACTION_NAMES[ACN_COUNT];

ItemCat item_cat(int kind);
bool item_stackable(int kind);
bool item_quickable(int kind);
int item_sprite(int kind);                     // index into items sheet
enum { SP_PH_WEAPON = 53, SP_PH_ARMOR, SP_PH_RING, SP_CHEST, SP_BONES };   // bag placeholders, heap looks
void item_name(const Item *it, char *buf, int len);       // PD Item.toString (name, +level, :STR)
void item_info(const Item *it, char *buf, int len);       // PD Item.info
const char *potion_color(int kind);            // flavour colour word
bool item_known(int kind);
int item_tier(int kind);                       // weapons and armor
int item_str(const Item *it);                  // strength requirement (upgrades lower it)
int weapon_min(const Item *it), weapon_max(const Item *it);
int weapon_acu(int kind), weapon_dly(int kind);           // x100
int armor_dr(const Item *it);
bool item_upgradable(int kind);
bool item_identified(const Item *it);
bool item_sellable(int kind);                  // Item.price > 0 (the prices: the hero overlay)
int item_max_dur(const Item *it);
int wand_max(const Item *it);                  // Wand.maxCharges
int ring_level(const Item *it);                // Ring.effectiveLevel (a broken ring works at +1)
bool item_broken(const Item *it);
Item item_make(int kind, int qty);
bool items_init(void);                         // loads the item table (item_defs.bin)
// Weapon.Enchantment / Armor.Glyph, kept in Item.ench (0 none). Same order as PD's lists.
enum { WE_NONE, WE_FIRE, WE_POISON, WE_DEATH, WE_PARALYSIS, WE_LEECH, WE_SLOW, WE_SHOCK, WE_INSTABILITY, WE_HORROR, WE_LUCK, WE_TEMPERING, WE_COUNT };
enum { AG_NONE, AG_BOUNCE, AG_AFFECTION, AG_ANTI_ENTROPY, AG_MULTIPLICITY, AG_POTENTIAL, AG_METABOLISM, AG_STENCH, AG_VISCOSITY,
       AG_DISPLACEMENT, AG_ENTANGLEMENT, AG_AUTO_REPAIR, AG_COUNT };
int ench_random(bool armor, int except);         // Enchantment.random / Glyph.random (not 'except')
void item_enchant(Item *it);                      // Weapon.enchant() / Armor.inscribe(): a new random one
const char *ench_glyph_name(int g);               // "multiplicity", for messages
uint8_t item_glow(const Item *it);                // ItemSprite.Glowing colour (ARGB8), 0 if none

// ---- Characters ----
#include "mobs.auto.h"                // MobType MT_*, flags MD_*, LOOT_* (tools/mobs.py)
// one creature kind (resources/data/mobs.bin, loaded by game_init): PD's Mob fields
typedef struct {
  uint32_t flags;                       // MD_*
  uint16_t name, desc, HT;              // text ids
  uint8_t att, def, dmg_lo, dmg_hi, dr, exp, max_lvl, move_cost, atk_delay, verb, loot, loot_pct;   // costs in ticks
  uint8_t pad[2];
} MobDef;
extern const MobDef *MOB_DEFS;
_Static_assert(sizeof(MobDef) == 24, "mobs.bin record");
void cap_first(char *s);               // main.c
const char *mob_name(int type);         // txt(name), with the bosses' "echo" names on revisited floors

enum { ST_SLEEPING, ST_WANDERING, ST_HUNTING, ST_FLEEING, ST_PASSIVE };   // PASSIVE: NPCs that stay put (Wandmaker, shopkeepers)
enum { MF_LEFT = 1, MF_SEEN = 2, MF_ALERTED = 4, MF_OOZE = 8 };   // MF_OOZE: Ooze on a creature (washed off in water)
#define MOB_AMOK(m) ((m)->flags >> 4)              // Amok (turns left, Wand of Amok) in the flags' top bits
#define MOB_SET_AMOK(m, t) ((m)->flags = ((m)->flags & 15) | (uint8_t)((t) > 15 ? 15 : (t)) << 4)
typedef struct {                        // per-character buffs (PD Buff subclasses), turns left unless noted
  uint8_t poison;                       // Poison.left
  uint8_t bleed;                        // Bleeding.level
  uint8_t cripple, burning, paralysis, frost, levitation, mind, invis, terror, blind, gas_immune, roots, slow;
  uint8_t barkskin;                     // Barkskin.level, drops by 1 a turn (a monster: Sungrass.Health, 0x80 | its cell & 0x7F)
  uint8_t charm;                        // Charm (turns): a monster won't attack the hero; the hero won't attack G.charm_uid
} Buffs;
typedef struct {
  int32_t time;
  int16_t pos, target;
  int16_t HP, HT;
  uint8_t type, state, flags, uid;
  Buffs b;
  uint8_t aux2;                         // per-kind state (Swarm generation, Goo pumped up, Thief fleeing...)
  int16_t aux;                          // per-kind state (a cell, a timer, a carried item's uid...)
  uint8_t vertigo;                      // Vertigo (turns): stumbles in a random direction
} Mob;

typedef struct {
  int32_t time;
  int16_t pos;
  int16_t HP, HT;
  uint8_t lvl, exp, attack_skill, defense_skill, facing_left, STR;
  uint16_t gold;
  int16_t hunger;                       // Hunger.level: 260 hungry, 360 starving
  Buffs b;
  Item weapon, armor;
  Item bag[BAG_CAP];
  uint8_t quick[2];                     // quickslot item kinds (PD QuickSlot)
  uint8_t mirror_att, mirror_dmg;       // last attack numbers (MirrorImage.duplicate)
  Item ring[2];                         // Belongings.ring1 / ring2 (0.7; saves before v8 end before this)
  // v9 (0.8): more buffs, class and subclass, room to grow
  uint8_t ooze, weakness, vertigo, light;   // Ooze (turns), Weakness, Vertigo, Light
  uint8_t hclass, subclass, combo, fury;   // subclass: SUB_*; combo: Gladiator's hit count; fury: Berserker's Fury
  int8_t combo_left;                    // hundredths of a turn the combo lasts
  uint8_t spare[7];
} Hero;
#define H_SHADOWS spare[5]                      // Shadows (garden shadowmeld, turns left): the hero sees only the cells around him

typedef struct { int16_t pos; uint8_t type; Item item; } Heap;   // PD calls item piles "heaps"
// the NPC quests (actors/mobs/npcs): stage, floor, which variant, and the rewards made when they appeared
enum { SUB_NONE, SUB_GLADIATOR, SUB_BERSERKER, SUB_WARLOCK, SUB_BATTLEMAGE, SUB_ASSASSIN, SUB_FREERUNNER, SUB_SNIPER, SUB_WARDEN };   // HeroSubClass
enum { QS_NONE, QS_SPAWNED, QS_GIVEN, QS_DONE, QS_REWARDED };
typedef struct {
  uint8_t ghost, ghost_depth, ghost_alt, ghost_left;   // Ghost: FetidRat or DriedRose
  uint8_t wand, wand_depth, wand_alt, wand_spare;      // Wandmaker: CorpseDust or Rotberry seed
  uint8_t smith, smith_depth, smith_alt, smith_count;  // Blacksmith: dark gold count or bat-blood pickaxe
  uint8_t imp, imp_depth, imp_alt, imp_left;           // Imp: dwarf tokens from monks or golems
  Item ghost_weapon, ghost_armor, wand1, wand2, imp_ring;
  uint8_t spare[8];
} Quests;
enum { DF_VIAL_SPAWNED = 1, DF_LASTSHOP_MADE = 2, DF_LASTSHOP_SHOP = 4, DF_AMULET = 8 };   // DF_AMULET: Statistics.amuletObtained
// dflags 16..128 (8 << k): story chapter k (1 prison .. 4 halls) still to show; its first floor was reached by falling (floor.c)
typedef struct { int16_t pos; uint8_t terrain; } Diff;      // terrain change vs. generated map
typedef struct { int16_t pos; uint8_t type; } Plant;        // a planted seed (0..7, Generator.Category.SEED order)

enum { HA_NONE, HA_MOVE, HA_ATTACK, HA_PICKUP, HA_DESCEND, HA_ASCEND, HA_REST, HA_OPEN, HA_TALK, HA_UNLOCK };

typedef struct {
  uint32_t seed;
  int32_t now;                          // PD Actor.now, in ticks
  int32_t turn_due;                     // next whole turn: buffs, hunger, regeneration, respawner
  int32_t level_t0;                     // when the hero arrived (respawner phase)
  uint8_t depth, max_depth, feeling, secret_doors;
  uint8_t pos_count, sou_count;         // Dungeon.potionOfStrength, scrollsOfUpgrade
  Hero hero;
  int16_t last_target;                  // cell of the mob the hero last attacked (AttackIndicator)
  int16_t quick_uid;                    // QuickSlot.lastTarget: uid of the mob last thrown at, or -1
  uint8_t potion_flavor[N_POTIONS], scroll_flavor[N_SCROLLS];   // 0..11 into PD's colors / runes
  uint8_t known[16];                                            // bit per item kind (a uint64 before v8)
  uint8_t wand_flavor[N_WANDS], ring_flavor[N_RINGS];           // woods / gems (v8)
  int16_t regen_left;                                           // Regeneration: ticks until the next HP (v8; Ring of Mending)
  uint8_t map[MAP_LEN];
  uint8_t visited[MAP_LEN / 8];
  uint8_t fov[MAP_LEN / 8];
  uint8_t los[MAP_LEN / 8];
  uint8_t hg[MAP_LEN / 8];              // cells generated as grass or high grass: their state (grass, high, embers) is saved as 2 bits each, not as diffs
  int16_t entrance, exit;
  Room rooms[MAX_ROOMS]; uint8_t n_rooms;
  Mob mobs[MAX_MOBS]; uint8_t n_mobs;
  Heap heaps[MAX_HEAPS]; uint8_t n_heaps;
  Diff diffs[MAX_DIFFS]; uint8_t n_diffs;
  Plant plants[MAX_PLANTS]; uint8_t n_plants;
  // hero's current multi-turn action (PD HeroAction) and what interrupted it
  uint8_t action; int16_t action_cell; bool interrupted, jump_confirmed;
  uint16_t seen_mobs;                   // mobs visible last turn (Hero.visibleEnemies)
  uint8_t next_uid;
  // hero-only buffs from armour glyphs (saved since v7)
  int16_t herbal_pos; uint8_t herbal;   // Earthroot.Armor: absorbs damage until the hero moves
  uint8_t charm_uid;                    // the monster the hero is charmed by
  uint16_t deferred;                    // Viscosity.DeferedDamage: 1 damage a turn
  char death[64];                       // PD ResultDescriptions line for the death screen
  // v9 (0.8)
  uint32_t v9_levels;                   // floors whose saved record is in the v9 layout (older ones are read as v8)
  uint8_t keys[27];                     // per depth: iron keys (bits 0-3), golden keys (4-5), skeleton key (6)
  uint8_t dew;                          // DewVial volume, 0..10 (DF_VIAL in dflags: carried)
  uint8_t dflags;                       // DF_*
  uint8_t boss;                         // the boss floor's script state (sealed, phase...)
  int16_t health_pos;                   // Sungrass.Health: heals while the hero stays here (-1 none)
  uint16_t cat_probs[10];               // Generator's category odds for this floor (not saved)
  Quests q;
  // v10 (0.9)
  uint32_t v10_levels;                  // floors whose saved record carries lvars
  uint32_t weak_floors;                 // floors built with a weak floor room (the next one needs a pit room)
  uint8_t soe_count;                    // Dungeon.scrollsOfEnchantment
  uint8_t lvars[4];                     // this floor's own state, saved with it: [0] sacrificial fire left + 1
  // made with the floor (not saved): its well and altar
  uint8_t well_kind; int16_t well_pos, altar_pos;
  // v11 (0.14): what the hero's bags hold (game slots 32..63): seed pouch 0-7, scroll holder 8-19, wand holster 20-31
  Item pack[32];
  // v12 (0.14): Journal records, a bit per depth for each Journal.Feature (J_*)
  uint32_t journal[11];
  // v13 (0.17): Dungeon.droppedItems, things fallen down a chasm, and the depth they land on
  Item fall[4]; uint8_t fall_d[4];
  // not saved: Hero.lastAction, what an interruption stopped (the ResumeButton)
  uint8_t last_action; int16_t last_cell;
  uint8_t night;                        // Dungeon.nightMode: before 7 am when the floor was entered
  int32_t srch_t; uint8_t srchd[MAP_LEN / 8];   // not saved (0.47: out of the app image): where this floor was searched, for the target list
  struct Tgm { int32_t t0; int16_t hp, c[8]; uint8_t n, d[8]; int16_t tr; int32_t trt; uint8_t srch; } tgm;   // (0.61: tr, trt: the door trick's spot, and till when to wait there)   // not saved (0.47, ovl/tgt): the list's objectives and their walks, from where it was taken (hp)
} Game;
enum { J_WELL_HEALTH, J_WELL_AWARENESS, J_WELL_TRANSMUTATION, J_SACRIFICIAL, J_ALCHEMY, J_GARDEN, J_STATUE, J_GHOST, J_WANDMAKER, J_TROLL, J_IMP, J_COUNT };
// Blobs (gases and fire): per-cell amounts, allocated only while one exists on the floor
enum { BLOB_TOXIC, BLOB_PARALYTIC, BLOB_FIRE, BLOB_REGROWTH, BLOB_CONFUSION, BLOB_WEB, BLOB_COUNT };
extern uint16_t *g_blob[BLOB_COUNT];
extern uint32_t g_goo_warn;                     // the cells (5x5 round it) a pumped-up Goo can jump to: drawn as Shattered's warning

#define BIT_GET(a, i) (((a)[(i) >> 3] >> ((i) & 7)) & 1)
#define BIT_SET(a, i) ((a)[(i) >> 3] |= (uint8_t)(1 << ((i) & 7)))
#define BIT_CLR(a, i) ((a)[(i) >> 3] &= (uint8_t)~(1 << ((i) & 7)))

// ---- level.c: PD RegularLevel + SewerLevel generator ----
// Builds terrain, rooms, entrance/exit, feeling into G; tomb cells for graveyard rooms into *tombs.
// ovl.c, the level overlay: generate the floor; with a content seed also its first monsters and items.
// false if the overlay failed to load.
int level_random_room_cell(int type, int tries);   // RegularLevel.randomRoom(type).random(), or -1
int level_room_random(const Room *r, int m);

// ---- callbacks from rules to the screen layer (main.c) ----
typedef enum { C_INFO, C_GOOD, C_BAD, C_WARN, C_GOLD } MsgColor;
typedef enum { FX_ATTACK, FX_DIE, FX_DAMAGE, FX_TEXT, FX_HEAL, FX_HURT, FX_DESCEND, FX_THROW, FX_TELEPORT,
               FX_ALERT, FX_GOLD, FX_SEARCH, FX_SPAWN, FX_LEAVE, FX_OPERATE, FX_READ, FX_SEARCHED, FX_BOSS_SLAIN, FX_QUAKE, FX_JUMP } FxKind;
void ui_log(const char *msg, MsgColor c);
void ui_fx(FxKind kind, int16_t cell, int who, const char *text);   // who: -1 hero, else mob index
void ui_throw(int16_t from, int16_t to, int sprite);
// a wand's bolt (MagicMissile / Lightning / DeathRay): k = wand (Wand.wands order); pts: the arcs' corners
void ui_zap(int k, int16_t from, int16_t to, const int16_t *pts, int npts);
typedef enum { PICK_IDENTIFY, PICK_UPGRADE, PICK_REFORGE, PICK_ENCHANT, PICK_RING, PICK_QUICK, PICK_SELL, PICK_SEED, PICK_SMITH, PICK_ARMOR, PICK_WEAPON, PICK_WAND } PickMode;   // PICK_RING: which ring comes off
void ui_amulet(int mode);                          // 0 AmuletScene, 1 without its text, 2 SurfaceScene (won)
void ui_pick_item(PickMode mode);                 // InventoryScroll / reforge item selector
void ui_hero_tier(void);                          // armour changed: hero sprite row
void ui_message(const char *title, const char *text);              // PD WndMessage
void ui_confirm_jump(void);                                         // Chasm.heroJump
void ui_wnd(int mob, const char *text, const char *b1, const char *b2);
extern const char *g_wnd_title;                  // ui_wnd's title when mob < 0   // WndQuest (b1 NULL: just the words)
void ui_sound(int sfx);
void ui_free_cache(int more);                     // low memory: drop pictures that reload on demand (more: the floor's tiles too)

// sfx.c: sound settings and music (ui_sound plays PD's effects, SND_* in sfx_ids.auto.h)
enum { SOUND_MUSIC = 1, SOUND_FX = 2 };            // 0.40: SOUND_FX = sound on at all; bits 2-3 the volume (0 loud, 1 medium, 2 quiet)
#define SOUND_VOL(m) (((m) >> 2) & 3)
void snd_cycle(void);                               // Loud -> Medium -> Quiet -> Off -> Loud
extern uint8_t g_sound;
extern uint8_t g_assist;                    // 0.51 (owner): the suggestions' help: 0 low (fight, explore, pick up), 1 medium (where to stand), 2 high (the bot's hands too)
#ifdef TEST_BUILD
#define ASSIST_DEF 2
#else
#define ASSIST_DEF 1
#endif
void snd_init(void), snd_deinit(void);
void snd_set(uint8_t mask);
void snd_poll(void);                             // feed the speaker now (once a frame)
void snd_music(int track);
void snd_hold(bool off);
int snd_stat(int k);                        // MUS_THEME / MUS_GAME (snd.h)

// ---- game.c ----
typedef enum { ACT_NONE, ACT_TURN, ACT_WALK, ACT_LEVEL } ActResult;
ActResult game_wnd_choice(int i);                   // a quest window's button i
extern Game *g_game;                 // allocated by game_init()
#define G (*g_game)
bool game_init(void);
void game_free_fx(void);             // gas and fire clouds: dropped when leaving to the title (they aren't saved)
void game_new(void);                             // a hero of class g_new_class (0 warrior, 1 mage, 2 rogue, 3 huntress)
extern uint8_t g_new_class;
#define VOWEL(c) ((c) >= 'a' && (c) <= 'z' && (0x104111u >> ((c) - 'a') & 1))   // a e i o u (no strchr: it would join the app)
#define KEY_HUNTRESS 201                         // persist: DM-300 has been slain once (the Huntress is unlocked)
bool huntress_unlocked(void);
// Rankings (0.14): persist keys 203-204 hold the top six games, best score first
#define KEY_RANKS 203
#define KEY_INTRO 205                            // IntroScene has been shown
#define KEY_BONES 206                            // Bones: the depth, then the Item a fallen hero left
#define RANK_N 6
typedef struct { uint32_t score; uint8_t cls, win, pad[2]; char info[40]; } Rank;   // cls: HeroClass
typedef struct { uint16_t total, won; uint8_t latest, n, pad[2]; Rank r[RANK_N]; } Ranks;
void game_rank(int win);                         // Rankings.submit
bool game_load(void);
void game_save(void);
int game_save_size(void);
void game_erase(void);
bool game_has_save(uint8_t *depth);
bool game_peek_save(uint8_t *depth, uint8_t *lvl);
ActResult game_resume(void);                     // ResumeButton
int game_target(int after, bool line, int *count); // 0.18 combat tags: the next enemy in range after mob `after`, and how many
#define TARGETS_MAX 16
enum { TK_ENEMY, TK_ITEM, TK_GO, TK_MINE, TK_VEIN, TK_BAG, TK_REST };   // 0.48 test kit: TK_BAG (Select: g_ovr.ha on bag slot g_ovr.hs), TK_REST   // 0.41: a gold vein: beside it (Select mines) / to walk to
extern uint8_t g_view[4], g_drop_i; extern int16_t g_from, g_drop[4], g_goal, g_tgobj[TARGETS_MAX];
#define g_srchd G.srchd
#define g_srch_t G.srch_t   // 0.47 g_tgobj: each list entry's objective (the entry may stand in for it on screen)
typedef struct { int32_t t; int16_t c; uint8_t d, on; int8_t ms; uint8_t gk; int8_t hs; uint8_t ha; uint8_t dk, da; int8_t dd; } Ovr;   /* 0.53: dk/da/dd: a hands suggestion the player passed over (item kind, action, floor) */   // 0.41: the player's own way (ovl/tgt): the target chosen over the first (c),
extern uint8_t g_hl_order[8];                        // 0.54: the bottom bar, the bot's best first (Assist High: Back walks it)
extern Ovr g_ovr;                                  // the floor (t: level_t0; just chosen: the hero's cell - 65536), its heading (d; bit 7: Select waits), on (1: one was on before, 2: entry 0 is grass,
                                                   // 4: grass skipped at kill count gk), ms: the pickaxe's slot (TK_MINE), gk: hero exp + lvl * 101 then   // 0.40: g_drop: where the player last dropped things (not offered back)   // 0.39: g_from: where the hero last moved from (the heading)                     // 0.36: the cells on screen around the hero (across, up, down, radius squared)
int game_targets(int16_t *cell, uint8_t *kind);    // 0.23: the button target cycle (TARGETS_MAX), best first        // mob index for PD's attack indicator, or -1
void game_inspect(int16_t cell, char *title, int tlen, char *desc, int dlen);   // the "info" overlay
ActResult game_tap_cell(int16_t cell);
ActResult game_continue(void);        // next step of a multi-turn action
ActResult game_jump(void);            // confirmed chasm jump
bool game_level_pending(void);        // after ACT_LEVEL: a floor change waits for...
void game_level_commit(void);         // ...this, once the screen has faded out
ActResult game_rest(bool till_healthy);        // long press: Hero.rest(true)
ActResult game_search(void);
int game_item_actions(int slot, uint8_t *out);     // slot: 0..19 bag, -1 weapon, -2 armor
ActResult game_item_do(int slot, ItemAction a);
ActResult game_quick_throw(int slot);            // throw at the nearest enemy
int mob_sprite_type(int i);
ActResult game_throw(int slot, int16_t target);
ActResult game_zap(int slot, int16_t target);    // Wand.zapper
ActResult game_aim(int what, int slot, int16_t target);   // 0 throw, 1 zap, 2 the class armour's leap
void game_ankh(void);                            // WndResurrect
int game_nearest_enemy(void);                       // cell or -1
int game_auto_target(void);                         // QuickSlot.useTargeting: mob index to aim at, or -1
int game_bag_find(int kind);                        // first bag slot holding kind, or -1
typedef enum { SEL_NONE, SEL_ATTACK, SEL_PICKUP, SEL_STAIRS, SEL_UPSTAIRS } SelKind;
bool game_pick_ok(PickMode mode, int slot);       // WndBag.Mode filters
ActResult game_pick(PickMode mode, int slot);     // the chosen item (slot -1000: cancelled)
int game_confirm_needed(int slot, ItemAction a);  // 1: harmful drink, 2: beneficial throw, 0 none
int hero_tier(void);
Item *game_slot(int slot);
int hero_max_exp(void);
int heap_at(int16_t cell);
int plant_at(int c);
int mob_at(int16_t cell);
bool game_hungry(void);
bool game_hero_visible_mob(int i);              // drawn: in sight or felt by Mind Vision
bool game_starving(void);

// ---- blit.c: scaled drawing straight into the frame buffer ----
typedef struct { GBitmap *bmp; } Sheet;
void blit_init(int h);
void blit_forget(void);                    // a bitmap was freed: forget what was learnt about it
void blit_begin(GContext *ctx);
void blit_end(GContext *ctx);
// zoom: 0 = half size, 1 = 1:1, 2 = double
void blit(const Sheet *s, int sx, int sy, int w, int h, int dx, int dy, int zoom);
void blit_spr4(const uint8_t *spr, int dx, int dy, int zoom);   // 16 palette bytes + 16x16 4-bit pixels
void blit_fog(int dx, int dy, int size);
void blit_fill(int dx, int dy, int w, int h, uint8_t argb);
void blit_nine(const Sheet *s, int sx, int sy, int sw, int sh, int b, int dx, int dy, int dw, int dh);
void blit_nine4(const Sheet *s, int sx, int sy, int sw, int sh, int ml, int mt, int mr, int mb, int dx, int dy, int dw, int dh);
void blit_box(int x, int y, int w, int h, int t, uint8_t argb);
void blit_fill2(int dx, int dy, int w, int h, uint8_t a, uint8_t b);
void blit_puff(int cx, int cy, int r, uint8_t argb, int level, int shift);   // a dithered soft disc (gas)
extern bool g_blit_ghost;                  // blit() draws every other pixel
extern bool g_blit_flip;                   // blit() mirrors left to right (0.20)
extern uint8_t g_blit_glow;
extern bool g_blit_opaque;                 // blit(): source is fully opaque, skip the alpha test
extern int g_blit_clip_y;                  // blit_nine4 draws only rows at or below this
void blit_fade(int level);                 // 0..16: dithered black over the whole screen
