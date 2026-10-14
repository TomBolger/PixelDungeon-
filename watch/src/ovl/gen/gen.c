/*
 * Pixel Dungeon for Pebble - the "gen" overlay: Generator (items/Generator.java), Weapon.random, Armor.random,
 * Wand.random, Ring.random. Loaded for level creation, loot, quest rewards and shops.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"

#define CB(p) ((p) * 256)                              // x256: Generator halves a float each draw; 8 halvings stay exact here
static const uint16_t CAT_BASE[10] = { CB(15), CB(10), CB(50), CB(40), CB(4), CB(2), CB(5), CB(0), CB(50), CB(5) };   // Generator.Category
static void random_level(Item *it, bool armor) {      // Weapon.random / Armor.random
  if (rnd_pct(40)) {
    int n = 1;
    if (rnd_int(3) == 0) { n++; if (rnd_int(3) == 0) n++; }
    if (rnd_int(2) == 0) it->lvl += n; else { it->lvl -= n; it->flags |= IF_CURSED; }
    it->dur = item_max_dur(it);
  }
  if (rnd_int(armor ? 10 : 10 + it->lvl) == 0) item_enchant(it);   // MeleeWeapon / Armor.random: enchant() / inscribe()
}
static Item random_weapon(void) {                      // Generator.randomWeapon: two picks, nearer to your STR
  static const uint8_t K[18] = { IK_DAGGER, IK_KNUCKLES, IK_QUARTERSTAFF, IK_SPEAR, IK_MACE, IK_SWORD, IK_LONGSWORD,
    IK_BATTLE_AXE, IK_WAR_HAMMER, IK_GLAIVE, IK_SHORT_SWORD, IK_DART, IK_JAVELIN, IK_INCENDIARY_DART, IK_CURARE_DART,
    IK_SHURIKEN, IK_BOOMERANG, IK_TOMAHAWK };
  static const uint16_t P[18] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 1 };
  int cur = 10 + G.pos_count;
  Item w[2];
  for (int j = 0; j < 2; j++) {
    int k = K[rnd_chances(P, 18)];
    w[j] = item_make(k, 1);
    if (item_cat(k) == CAT_MISSILE) {
      switch (k) {
        case IK_JAVELIN: case IK_SHURIKEN: w[j].qty = rnd_int2(5, 15); break;
        case IK_INCENDIARY_DART: w[j].qty = rnd_int2(3, 6); break;
        case IK_CURARE_DART: w[j].qty = rnd_int2(2, 5); break;
        case IK_TOMAHAWK: w[j].qty = rnd_int2(5, 12); break;
      }
    } else random_level(&w[j], false);
  }
  return abs(cur - item_str(&w[0])) < abs(cur - item_str(&w[1])) ? w[0] : w[1];
}
static Item random_armor(void) {
  int cur = 10 + G.pos_count;
  Item a[2];
  for (int j = 0; j < 2; j++) { a[j] = item_make(IK_CLOTH_ARMOR + rnd_int(5), 1); random_level(&a[j], true); }
  return abs(cur - item_str(&a[0])) < abs(cur - item_str(&a[1])) ? a[0] : a[1];
}
static Item generate_item(int cat) {                  // Generator.random(category); cat < 0: any category
  if (cat < 0) cat = rnd_chances(G.cat_probs, 10);
  if (cat >= 0) G.cat_probs[cat] /= 2;
  switch (cat) {
    case GC_POTION: {                                  // Generator order: healing, experience, toxic, paralytic, flame, levitation, strength, mind, purity, invis, might, frost
      static const uint16_t P[12] = { 45, 4, 15, 10, 15, 10, 0, 20, 12, 10, 0, 10 };
      static const uint8_t K[12] = { IK_POT_HEALING, IK_POT_EXPERIENCE, IK_POT_TOXIC, IK_POT_PARALYTIC, IK_POT_FLAME, IK_POT_LEVITATION,
                                     IK_POT_STRENGTH, IK_POT_MIND, IK_POT_PURITY, IK_POT_INVIS, IK_POT_MIGHT, IK_POT_FROST };
      return item_make(K[rnd_chances(P, 12)], 1);
    }
    case GC_SCROLL: {                                  // identify, teleport, remove curse, recharging, mapping, challenge, terror, lullaby, psionic, mirror, upgrade, enchantment
      static const uint16_t P[12] = { 30, 10, 15, 10, 15, 12, 8, 8, 4, 6, 0, 1 };
      static const uint8_t K[12] = { IK_SCR_IDENTIFY, IK_SCR_TELEPORT, IK_SCR_REMOVE_CURSE, IK_SCR_RECHARGING, IK_SCR_MAPPING, IK_SCR_CHALLENGE,
                                     IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_PSIONIC, IK_SCR_MIRROR, IK_SCR_UPGRADE, IK_SCR_ENCHANT };
      return item_make(K[rnd_chances(P, 12)], 1);
    }
    case GC_WEAPON: return random_weapon();
    case GC_ARMOR: return random_armor();
    case GC_FOOD: { static const uint16_t P[2] = { 4, 1 }; return item_make(rnd_chances(P, 2) ? IK_PASTY : IK_RATION, 1); }
    case GC_WAND: {                                    // teleport, slowness, firebolt, regrowth, poison, blink, lightning, amok, reach, flock, (magic missile), disintegration, avalanche
      static const uint16_t P[12] = { 10, 10, 15, 6, 10, 11, 15, 10, 6, 10, 5, 5 };
      static const uint8_t K[12] = { IK_WND_TELEPORT, IK_WND_SLOW, IK_WND_FIREBOLT, IK_WND_REGROWTH, IK_WND_POISON, IK_WND_BLINK,
                                     IK_WND_LIGHTNING, IK_WND_AMOK, IK_WND_REACH, IK_WND_FLOCK, IK_WND_DISINTEGRATION, IK_WND_AVALANCHE };
      Item w = item_make(K[rnd_chances(P, 12)], 1);
      for (int n = 0; n < 2 && rnd_pct(n ? 15 : 50); n++) { w.lvl++; if (w.ench < wand_max(&w)) w.ench++; }   // Wand.random: upgrade() 50%, again 15%
      w.dur = item_max_dur(&w);
      return w;
    }
    case GC_RING: {                                    // Ring.random: +1..+2, or 30% of the time -1..-2 and cursed (no Haggler, Thorns)
      int i = rnd_int(10);                             // mending .. haste, then elements (equal odds)
      Item r = item_make(i < 9 ? IK_RNG_MENDING + i : IK_RNG_ELEMENTS, 1);
      int l = rnd_int2(1, 3);
      if (rnd_pct(30)) { r.lvl = -l; r.flags |= IF_CURSED; } else r.lvl = l;
      r.dur = item_max_dur(&r);
      return r;
    }
    case GC_SEED: return random_seed();
    case GC_MISC: {                                    // Bomb (1..3), Honeypot: 2 : 1
      if (rnd_int(3) < 2) return item_make(IK_BOMB, rnd_range(1, 3));
      return item_make(IK_HONEYPOT, 1);
    }
    default: break;
  }
  return item_make(IK_GOLD, gold_amount());
}
static void reset(void) { memcpy(G.cat_probs, CAT_BASE, sizeof G.cat_probs); }
static Item gen(int cat) {
  if (cat == GC_MELEE) { Item w; do { G.cat_probs[GC_WEAPON] /= 2; w = random_weapon(); } while (item_cat(w.kind) != CAT_WEAPON); return w; }   // no missiles (Ghost, Statue): each a Generator.random(WEAPON)
  return generate_item(cat);
}
OVL_ENTRY(gen, (void *)gen, (void *)reset);
