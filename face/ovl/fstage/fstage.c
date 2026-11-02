/*
 * Pixel Dungeon Face - the "fstage" overlay (the watchface only; the normal app has no such overlay). A new run that starts deeper
 * (settings: "Starting stage") gets, before its first floor is built, what a player arriving there would have: a level, the
 * strength potions and upgrade scrolls the floors above hold (the counters the generator uses, so the floors below drop the
 * rest), a subclass after Tengu, gear a little better than usual, and a backpack of random things with some of the potion and
 * scroll kinds already known. It runs once per run, called by the new game overlay, and is gone again before the floor is built.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"

#ifdef PD_FACE
static const uint8_t STAGES[10] = { 1, 5, 6, 10, 11, 15, 16, 20, 21, 25 };   // = the settings page's list
//                                    stage:  -  Goo Prison Tengu Caves DM-300 City King Halls Yog
static const uint8_t LVL[10]  = { 1, 6, 7, 11, 12, 15, 16, 19, 20, 22 };     // hero level (about what a player has there)
static const uint8_t POS[10]  = { 0, 2, 2, 4, 4, 6, 6, 8, 8, 9 };            // potions of strength found above (Dungeon.posNeeded quota)
static const uint8_t SOU[10]  = { 0, 2, 3, 5, 6, 8, 9, 11, 12, 12 };         // scrolls of upgrade found above, all read into the gear
static const uint8_t SOE[10]  = { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4 };            // scrolls of enchantment found above
static const uint8_t KPOT[10] = { 0, 3, 3, 5, 5, 6, 6, 7, 8, 9 };            // other potion kinds known (besides strength and healing)
static const uint8_t KSCR[10] = { 0, 3, 3, 5, 5, 6, 6, 7, 8, 9 };            // scroll kinds known (besides upgrade and enchantment)
static const uint8_t GT[10]   = { 0, 3, 3, 4, 4, 5, 5, 5, 5, 5 };            // weapon and armour tier (a little above a player's)
// the generator's odds (ovl/gen), in the enum's order: healing experience toxic flame [strength] paralytic levitation mind purity invisibility [might] frost
static const uint16_t PW[12] = { 45, 4, 15, 15, 0, 10, 10, 20, 12, 10, 0, 10 };
// identify mapping recharging remove-curse teleport challenge terror lullaby psionic mirror [upgrade] [enchantment]
static const uint16_t SW[12] = { 30, 15, 10, 15, 10, 12, 8, 8, 4, 6, 0, 0 };

static int free_slots(void) { int n = 0; for (int i = 0; i < BAG_SIZE; i++) n += G.hero.bag[i].kind == IK_NONE; return n; }
static void give(Item it) { if (free_slots() > 3 || bag_region(it.kind) >= 0) bag_add(it); }   // (always leaves three places free for what he picks up)
static void learn(int first, const uint16_t *w0, int count) {            // `count` kinds, by the generator's odds, become known
  uint16_t w[12]; memcpy(w, w0, sizeof w);
  for (int n = 0; n < count; n++) { int i = rnd_chances(w, 12); if (i < 0) break; w[i] = 0; set_known(first + i); }
}
static Item gear(bool armor, int tier, int lvl) {                        // the best tier up to `tier` he can carry (strength need, after the upgrades, within 2 of his)
  for (int t = tier; t >= 1; t--) {
    uint8_t c[11]; int n = 0;
    for (int k = armor ? IK_CLOTH_ARMOR : IK_DAGGER; k <= (armor ? IK_PLATE_ARMOR : IK_GLAIVE); k++) {
      if (item_tier(k) != t) continue;
      Item it = item_make(k, 1); it.lvl = lvl;
      if (item_str(&it) <= G.hero.STR + 2) c[n++] = k;
    }
    if (n) { Item it = item_make(c[rnd_int(n)], 1); it.lvl = lvl; it.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; it.dur = item_max_dur(&it); return it; }
  }
  Item it = item_make(armor ? IK_CLOTH_ARMOR : IK_SHORT_SWORD, 1); it.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; return it;
}
static void stage_setup(int depth) {
  int si = 0; for (int i = 1; i < 10; i++) if (STAGES[i] == depth) si = i;
  if (!si) return;
  Hero *h = &G.hero;
  // the hero: level (+5 health, +1 to hit and to dodge each), strength from the potions, the counters the floors below use
  h->lvl = LVL[si]; h->HT += 5 * (h->lvl - 1); h->HP = h->HT; h->attack_skill += h->lvl - 1; h->defense_skill += h->lvl - 1;
  h->STR += POS[si]; G.pos_count = POS[si]; G.sou_count = SOU[si]; G.soe_count = SOE[si];
  if (POS[si]) set_known(IK_POT_STRENGTH);
  if (SOU[si]) set_known(IK_SCR_UPGRADE);
  if (SOE[si]) set_known(IK_SCR_ENCHANT);
  if (depth > 10) h->subclass = 1 + h->hclass * 2 + rnd_int(2);          // Tengu drops the Tome of Mastery: chosen by now
  h->gold = depth * 70 + rnd_int(depth * 30);
  // gear: the armour takes most of the upgrade scrolls, the weapon some
  h->armor = gear(true, GT[si], (SOU[si] * 55 + 50) / 100 + 1);
  h->weapon = gear(false, GT[si], (SOU[si] * 35 + 50) / 100 + 1);
  for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == IK_WND_MM) { Item *w = &h->bag[i]; w->lvl = 1 + SOU[si] / 3; w->ench = wand_max(w); w->dur = item_max_dur(w); }   // the mage's wand
  // what is known: strength and healing (the face's potion), then kinds drawn by how common they are
  learn(IK_FIRST_POTION, PW, KPOT[si]);
  learn(IK_FIRST_SCROLL, SW, KSCR[si]);
  // the backpack
  gen_reset();
  give(item_make(IK_SEED_POUCH, 1));
  if (si >= 3) give(item_make(IK_SCROLL_HOLDER, 1));
  int nw = 0;
  Item w = { 0 };
  if (rnd_pct(55)) { w = gen_item(GC_WAND); nw = 1; }
  if (nw && si >= 4) give(item_make(IK_WAND_HOLSTER, 1));
  if (nw) { set_known(w.kind); w.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN; give(w); }
  for (int i = 0, n = 3 + si / 3 + rnd_int(3); i < n; i++) give(gen_item(GC_POTION));
  for (int i = 0, n = 3 + si / 3 + rnd_int(3); i < n; i++) give(gen_item(GC_SCROLL));
  for (int i = 0, n = 2 + rnd_int(3); i < n; i++) give(gen_item(GC_SEED));
  give(item_make(IK_RATION, 1));
  if (si >= 3) give(item_make(IK_RATION, 1));
  if (rnd_pct(50)) give(item_make(IK_PASTY, 1));
  if (rnd_pct(55)) give(rnd_pct(50) ? item_make(IK_JAVELIN, rnd_range(5, 8)) : item_make(IK_TOMAHAWK, rnd_range(4, 6)));
  if (rnd_pct(40)) {                                                     // a ring, known and sound, worn in a free hand
    Item r; int g = 0; do r = gen_item(GC_RING); while ((r.flags & IF_CURSED) && ++g < 50);
    if (!(r.flags & IF_CURSED)) {
      r.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(r.kind);
      if (!h->ring[1].kind) h->ring[1] = r; else give(r);
    }
  }
  if (rnd_pct(60)) { give(item_make(IK_DEW_VIAL, 1)); G.dew = rnd_range(3, 10); G.dflags |= DF_VIAL_SPAWNED; }
}
#else
static void stage_setup(int depth) {}
#endif
OVL_ENTRY(fstage, (void *)stage_setup);
