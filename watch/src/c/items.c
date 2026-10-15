/*
 * Pixel Dungeon for Pebble - item definitions.
 * Names, numbers and descriptions from Pixel Dungeon 1.9.1 (GPLv3): Item, MeleeWeapon, MissileWeapon,
 * Armor, Potion, Scroll, Food and their subclasses.
 */
#include "pd.h"
#include "items_int.h"
#include "ovl.h"

const char *ACTION_NAMES[ACN_COUNT] = { "Eat", "Drink", "Read", "Equip", "Unequip", "Throw", "Quickslot", "Drop", "Reforge", "Zap",
  "Plant", "Light", "Apply", "Set", "Return", "End the game", "Mine", "Special", "Disenchant", "Shatter" };

const char *const POTION_COLORS[12] = { "turquoise", "crimson", "azure", "jade", "golden", "magenta",
  "charcoal", "ivory", "amber", "bistre", "indigo", "silver" };
const char *const WAND_WOODS[12] = { "holly", "yew", "ebony", "cherry", "teak", "rowan", "willow", "mahogany", "bamboo",
  "purpleheart", "oak", "birch" };
const char *const RING_GEMS[12] = { "diamond", "opal", "garnet", "ruby", "amethyst", "topaz", "onyx", "tourmaline", "emerald",
  "sapphire", "quartz", "agate" };
const char *const SCROLL_RUNES[12] = { "KAUNAN", "SOWILO", "LAGUZ", "YNGVI", "GYFU", "RAIDO",
  "ISAZ", "MANNAZ", "NAUDIZ", "BERKANAN", "ODAL", "TIWAZ" };

#include "item_sp.h"                              // SP_*: slots in the items sheet
const ItemDef *DEFS;                              // tools/dump_defs.c -> resources/data/item_defs.bin (item_defs.inc)
bool items_init(void) {
  if (DEFS) return true;
  ItemDef *d = malloc(IK_COUNT * sizeof(ItemDef));
  if (d) resource_load_byte_range(resource_get_handle(RESOURCE_ID_ITEM_DEFS), 0, (uint8_t *)d, IK_COUNT * sizeof(ItemDef));
  DEFS = d;
  return d != NULL;
}

ItemCat item_cat(int k) { return DEFS[k].cat; }
bool item_stackable(int k) { return DEFS[k].stack; }
bool item_quickable(int k) {                          // has a defaultAction (WndBag.Mode.QUICKSLOT): eat, drink, read, zap, throw
  int c = DEFS[k].cat;
  return c == CAT_FOOD || c == CAT_POTION || c == CAT_SCROLL || c == CAT_WAND || c == CAT_MISSILE || c == CAT_SEED ||
         k == IK_DEW_VIAL || k == IK_BOMB || k == IK_HONEYPOT || k == IK_TORCH || k == IK_MASTERY;
}
bool item_known(int k) { return (G.known[k >> 3] >> (k & 7)) & 1; }
int item_tier(int k) { return DEFS[k].tier; }
int weapon_acu(int k) { return DEFS[k].acu ? DEFS[k].acu : 100; }
int weapon_dly(int k) { return DEFS[k].dly ? DEFS[k].dly : 100; }
bool item_upgradable(int k) { return k == IK_BOOMERANG ? true : k == IK_PICKAXE || CLASS_ARMOR(k) || k == IK_RNG_HAGGLER || k == IK_RNG_THORNS ? false : DEFS[k].cat == CAT_WEAPON || DEFS[k].cat == CAT_ARMOR || DEFS[k].cat == CAT_WAND || DEFS[k].cat == CAT_RING; }
static bool gear(int k) { return DEFS[k].cat == CAT_WEAPON || DEFS[k].cat == CAT_ARMOR; }   // has a strength requirement
int wand_max(const Item *it) { int m = (it->kind == IK_WND_MM ? 3 : 2) + it->lvl; return m < 0 ? 0 : m > 9 ? 9 : m; }    // Wand.maxCharges

const char *potion_color(int k) { return POTION_COLORS[G.potion_flavor[k - IK_FIRST_POTION]]; }
int item_sprite(int k) {
  if (DEFS[k].cat == CAT_POTION) return SP_POTION0 + G.potion_flavor[k - IK_FIRST_POTION];
  if (DEFS[k].cat == CAT_SCROLL) return SP_SCROLL0 + G.scroll_flavor[k - IK_FIRST_SCROLL];
  if (k == IK_WND_MM) return SP_WAND_MM;
  if (DEFS[k].cat == CAT_WAND) return SP_WAND0 + G.wand_flavor[k - IK_FIRST_WAND];
  if (DEFS[k].cat == CAT_RING) return SP_RING0 + G.ring_flavor[k - IK_FIRST_RING];
  return DEFS[k].sprite;
}

// strength: upgrades lower it (MeleeWeapon / Armor: STR-- per upgrade, ++ per degrade)
int item_str(const Item *it) { return CLASS_ARMOR(it->kind) ? it->hits : DEFS[it->kind].str - (gear(it->kind) ? it->lvl : 0); }
// Item.maxDurability: Weapon 5 x, Armor 6 x (16 - level)
// Wand 6 x; Ring 100 x, kept in tens of turns (255: a ring of level 1 or less never wears out)
int item_max_dur(const Item *it) {
  if (!item_upgradable(it->kind)) return 1;
  int m = it->lvl < 16 ? 16 - it->lvl : 1, c = DEFS[it->kind].cat;
  if (c == CAT_RING) return it->lvl <= 1 ? 255 : 10 * m;
  return (c == CAT_WEAPON ? 5 : c == CAT_MISSILE ? 8 : 6) * m;   // Boomerang: 8 x
}
bool item_broken(const Item *it) { return item_upgradable(it->kind) && it->dur == 0 && !(DEFS[it->kind].cat == CAT_RING && it->lvl <= 1); }
static int eff_level(const Item *it) { return item_broken(it) ? 0 : it->lvl; }
int ring_level(const Item *it) { return item_broken(it) ? 1 : it->lvl; }   // Ring.effectiveLevel

// MeleeWeapon.min0/max0: tier, (tier^2 - tier + 10) / ACU * DLY
int melee_max0(int k) {
  if (k == IK_SHORT_SWORD) return 12;
  int t = DEFS[k].tier;
  return ((t * t - t + 10) * DEFS[k].dly - (DEFS[k].acu > 100)) / DEFS[k].acu;   // (int)(x / ACU * DLY) in floats: 30 / 1.2f is 24.99999 (War hammer: 24)
}
int weapon_min(const Item *it) {
  int l = eff_level(it);
  if (DEFS[it->kind].cat == CAT_WAND) return 1 + l / 3;                       // Wand.min (a mage's melee)
  if (it->kind == IK_BOOMERANG) return 1 + l;
  if (DEFS[it->kind].cat == CAT_MISSILE || DEFS[it->kind].mx) return DEFS[it->kind].mn;   // missiles, the pickaxe
  return DEFS[it->kind].tier + (item_broken(it) ? 0 : it->lvl);
}
int weapon_max(const Item *it) {
  int l = eff_level(it), t = 1 + l / 3;
  if (DEFS[it->kind].cat == CAT_WAND) return (t * t - t + 10) / 2 + l;      // Wand.max
  if (it->kind == IK_BOOMERANG) return 4 + 2 * l;
  if (DEFS[it->kind].cat == CAT_MISSILE || DEFS[it->kind].mx) return DEFS[it->kind].mx;
  return melee_max0(it->kind) + (item_broken(it) ? 0 : it->lvl * DEFS[it->kind].tier);
}
int armor_dr(const Item *it) { if (CLASS_ARMOR(it->kind)) return it->dur; int d = DEFS[it->kind].tier * (2 + eff_level(it) + (it->ench ? 1 : 0)); return d > 0 ? d : 0; }   // a glyph adds a level's worth

bool item_identified(const Item *it) {
  switch (DEFS[it->kind].cat) {
    case CAT_POTION: case CAT_SCROLL: return item_known(it->kind);
    case CAT_WEAPON: case CAT_ARMOR: return (it->flags & IF_LEVEL_KNOWN) && (it->flags & IF_CURSED_KNOWN);
    case CAT_WAND: return (it->flags & IF_LEVEL_KNOWN) && (it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CHARGE_KNOWN) && item_known(it->kind);
    case CAT_RING: return (it->flags & IF_LEVEL_KNOWN) && (it->flags & IF_CURSED_KNOWN) && item_known(it->kind);
    default: return true;
  }
}

Item item_make(int k, int qty) {
  Item it = { .qty = qty, .kind = k };
  if (DEFS[k].cat == CAT_MISSILE || (!item_upgradable(k) && DEFS[k].cat != CAT_RING)) it.flags = IF_LEVEL_KNOWN;   // (Thorns / Haggler: known on pickup)   // MissileWeapon: levelKnown
  if (item_upgradable(k)) { it.hits = DEFS[k].cat == CAT_ARMOR ? 10 : 20; it.dur = item_max_dur(&it); }
  if (DEFS[k].cat == CAT_WAND) { it.ench = k == IK_WND_MM ? 3 : 2; it.hits = 40; }   // Wand: 2 charges, known after 40 zaps
  if (DEFS[k].cat == CAT_RING) it.hits = 200;                                // Ring.TICKS_TO_KNOW: turns worn
  return it;
}

static const char *const AG_WORDS[AG_COUNT] = { "", "bounce", "affection", "anti-entropy", "multiplicity", "potential", "metabolism",
  "stench", "viscosity", "displacement", "entanglement", "auto-repair" };
const char *ench_glyph_name(int g) { return g > 0 && g < AG_COUNT ? AG_WORDS[g] : ""; }
int ench_random(bool armor, int except) {
  static const uint16_t WE_CH[WE_COUNT - 1] = { 10, 10, 1, 2, 1, 2, 6, 3, 2, 2, 3 };   // Weapon.Enchantment.chances
  static const uint16_t AG_CH[AG_COUNT - 1] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
  for (int i = 0; i < 50; i++) {
    int e = 1 + rnd_chances(armor ? AG_CH : WE_CH, armor ? AG_COUNT - 1 : WE_COUNT - 1);
    if (e != except) return e;
  }
  return except == 1 ? 2 : 1;
}
void item_enchant(Item *it) { it->ench = ench_random(DEFS[it->kind].cat == CAT_ARMOR, it->ench); }
// ItemSprite.Glowing colours, snapped to Pebble's palette
uint8_t item_glow(const Item *it) {
  static const uint8_t WE_GLOW[WE_COUNT] = { 0, GColorOrangeARGB8, GColorIndigoARGB8, GColorBlackARGB8, GColorBrassARGB8,
    GColorBulgarianRoseARGB8, GColorBlueMoonARGB8, GColorWhiteARGB8, GColorWhiteARGB8, GColorBlackARGB8, GColorGreenARGB8, GColorRoseValeARGB8 };
  static const uint8_t AG_GLOW[AG_COUNT] = { 0, GColorWhiteARGB8, GColorBrilliantRoseARGB8, GColorBlueARGB8, GColorBrassARGB8,
    GColorPictonBlueARGB8, GColorDarkCandyAppleRedARGB8, GColorJaegerGreenARGB8, GColorLavenderIndigoARGB8, GColorPictonBlueARGB8,
    GColorKellyGreenARGB8, GColorRoseValeARGB8 };
  if (!it->ench) return 0;
  if (DEFS[it->kind].cat == CAT_ARMOR && it->ench < AG_COUNT) return AG_GLOW[it->ench];
  if (DEFS[it->kind].cat != CAT_WAND && it->ench < WE_COUNT) return WE_GLOW[it->ench];   // a weapon or the boomerang (a wand's ench: its charges)
  return 0;
}
// Item.toString (0.75: the "name" overlay, room)
void item_name(const Item *it, char *buf, int len) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_NAME);
  if (!o) { snprintf(buf, len, "%s", txt(DEFS[it->kind].name)); return; }
  ((void (*)(const Item *, char *, int))o->fn[0])(it, buf, len); ovl_free(o);
}

// Item.price > 0 (0.14: the prices themselves live in the hero overlay, with the shops)
bool item_sellable(int k) {
  switch (DEFS[k].cat) {
    case CAT_WEAPON: return k != IK_PICKAXE;
    case CAT_ARMOR: return !CLASS_ARMOR(k);
    case CAT_MISSILE: return k != IK_BOOMERANG;
    case CAT_WAND: case CAT_RING: case CAT_SEED: case CAT_POTION: case CAT_SCROLL: case CAT_FOOD: return true;
  }
  return k == IK_BOMB || k == IK_TORCH || k == IK_HONEYPOT || k == IK_ANKH || k == IK_WEIGHTSTONE || (k >= IK_SEED_POUCH && k <= IK_OVERPRICED) ||
         k == IK_RAT_SKULL || k == IK_DARK_GOLD || k == IK_DWARF_TOKEN || k == IK_WIPE_OUT;
}
