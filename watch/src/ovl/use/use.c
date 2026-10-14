/*
 * Pixel Dungeon for Pebble - using items (an overlay): eating, drinking and shattering potions, reading
 * scrolls and choosing their target, equipping, dropping, reforging.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Food and the meats, Potion and its subclasses, Scroll and its
 * subclasses, InventoryScroll, ShortSword, Item.execute / doDrop, KindOfWeapon / Armor doEquip.
 */
#include "pd.h"
#include "game_int.h"
#include "items_int.h"
#include "ovl.h"
#ifndef PICK_ONLY                                     // 0.30: the item choices (use_pick) are their own overlay, src/ovl/pick

static void area(int cell, int n) {
  for (int i = 0; i < MAP_LEN; i++) if (TFLAGS(G.map[i]) & F_LOS_BLOCKING) BIT_CLR(s_pass, i); else BIT_SET(s_pass, i);
  distance_map(cell, -1);
  for (int i = 0; i < MAP_LEN; i++) if (s_dist[i] > n) s_dist[i] = 0x7FFF;
}

static void use_shatter(int k, int cell) {          // Potion.shatter and the gas / flame / frost potions
  bool vis = BIT_GET(G.fov, cell);
  if (vis) ui_sound(SND_SHATTER);
  switch (k) {
    case IK_POT_TOXIC: if (vis) set_known(k); blob_seed(BLOB_TOXIC, cell, 1000); return;
    case IK_POT_PARALYTIC: if (vis) set_known(k); blob_seed(BLOB_PARALYTIC, cell, 1000); return;
    case IK_POT_FLAME: if (vis) set_known(k); blob_seed(BLOB_FIRE, cell, 2); return;
    case IK_POT_FROST: {
      area(cell, 2);
      bool seen = false;
      for (int i = 0; i < MAP_LEN; i++) {
        if (s_dist[i] == 0x7FFF) continue;
        int m = mob_at(i);
        int turns = 5 * rnd_range(100, 150) / 100;
        if (m >= 0) affect_frost(m, turns);
        if (i == G.hero.pos && G.hero.HP > 0) affect_frost(-1, turns);
        blob_clear(BLOB_FIRE, i);
        for (int h = G.n_heaps - 1; h >= 0; h--) if (G.heaps[h].pos == i) heap_freeze(h);   // Heap.freeze, every pile (a mimic wakes up frozen)
        if (BIT_GET(G.fov, i)) seen = true;
      }
      if (seen) set_known(k);
      return;
    }
    case IK_POT_PURITY: {
      area(cell, 2);
      bool procd = false;
      for (int b = BLOB_TOXIC; b <= BLOB_PARALYTIC; b++) if (g_blob[b])
        for (int i = 0; i < MAP_LEN; i++) if (s_dist[i] != 0x7FFF && g_blob[b][i]) { blob_clear(b, i); procd = true; }
      bool hero_in = s_dist[G.hero.pos] != 0x7FFF;
      if (procd) { set_known(k); if (hero_in) ui_log("You feel uncommon freshness in the air.", C_GOOD); }
      else {
        if (vis) LOG(C_INFO, txt(TX_THE_FLASK_SHATTERS_AND), potion_color(k));
        if (hero_in) { ui_log("You feel uncommon freshness in the air.", C_INFO); set_known(k); }
      }
      return;
    }
    default: {
      if (vis) {
        LOG(C_INFO, txt(TX_THE_FLASK_SHATTERS_AND), potion_color(k));   // Potion.shatter
      }
    }
  }
}

static void potion_apply(int k) {                      // Potion.apply when drunk
  Hero *h = &G.hero;
  ui_fx(FX_OPERATE, h->pos, -1, NULL);                 // Potion.drink: sprite.operate
  ui_sound(k == IK_POT_INVIS ? SND_MELD : SND_DRINK);
  switch (k) {
    case IK_POT_HEALING:
      set_known(k);
      h->HP = h->HT; h->b.poison = 0; h->b.cripple = 0; h->b.bleed = 0; h->weakness = 0;
      ui_log("Your wounds heal completely.", C_GOOD); ui_fx(FX_HEAL, h->pos, -1, "healed");
      break;
    case IK_POT_EXPERIENCE: set_known(k); earn_exp(hero_max_exp() - h->exp); break;
    case IK_POT_STRENGTH:
      set_known(k); h->STR++;
      ui_fx(FX_HEAL, h->pos, -1, "+1 str"); ui_log(txt(TX_NEWFOUND_STRENGTH_SURGES_THROUGH), C_GOOD);
      break;
    case IK_POT_MIGHT:
      set_known(k); h->STR++; h->HT += 5; h->HP += 5;
      ui_fx(FX_HEAL, h->pos, -1, "+1 str, +5 ht"); ui_log(txt(TX_NEWFOUND_STRENGTH_SURGES_THROUGH), C_GOOD);
      break;
    case IK_POT_LEVITATION: set_known(k); h->b.levitation = h->b.levitation + 20 > 255 ? 255 : h->b.levitation + 20; h->b.roots = 0; ui_log("You float into the air!", C_INFO); break;
    case IK_POT_MIND: {
      set_known(k); h->b.mind = h->b.mind + 20 > 255 ? 255 : h->b.mind + 20; update_fov();
      bool any = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) any = true;
      ui_log(any ? txt(TX_YOU_CAN_SOMEHOW_FEEL) : txt(TX_YOU_CAN_SOMEHOW_TELL), C_INFO);
      break;
    }
    case IK_POT_INVIS: set_known(k); h->b.invis = h->b.invis + 15 > 255 ? 255 : h->b.invis + 15; ui_log("You see your hands turn invisible!", C_INFO); break;
    case IK_POT_PURITY: ui_log("You've stopped sensing any smells!", C_WARN); if (h->b.gas_immune < 5) h->b.gas_immune = 5; set_known(k); break;
    default: use_shatter(k, h->pos); break;          // gases, flame, frost: right where you stand
  }
}

static int count_seen_mobs(int *last) {
  int n = 0;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos)) { n++; *last = i; }
  return n;
}

static bool uncurse(Item *it) { if (it->kind && (it->flags & IF_CURSED)) { it->flags &= ~IF_CURSED; return true; } return false; }

static void shop_flee(void) {                         // Shopkeeper.add(Buff): any buff makes him flee
  for (int i = 0; i < G.n_mobs; i++) if ((G.mobs[i].type == MT_SHOPKEEPER || G.mobs[i].type == MT_IMP_SHOP) && G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) mob_damage(i, 0);
}
static ActResult scroll_read(int k) {                  // returns ACT_NONE while waiting for an item choice
  Hero *h = &G.hero;
  ui_fx(FX_READ, h->pos, -1, NULL);                    // HeroSprite.read
  ui_sound(k == IK_SCR_CHALLENGE ? SND_CHALLENGE : k == IK_SCR_LULLABY ? SND_LULLABY : k == IK_SCR_PSIONIC ? SND_BLAST : SND_READ);
  if (k == IK_SCR_IDENTIFY || (unsigned)(k - IK_SCR_UPGRADE) < 2) {   // InventoryScroll: choose an item (invisibility only goes once one is chosen: use_pick)
    g_pick_unknown = !item_known(k); set_known(k);
    g_pick_mode = k == IK_SCR_IDENTIFY ? PICK_IDENTIFY : k == IK_SCR_UPGRADE ? PICK_UPGRADE : PICK_ENCHANT;
    ui_pick_item(g_pick_mode);
    return ACT_NONE;
  }
  dispel_invisibility();
  switch (k) {
    case IK_SCR_MAPPING: {
      bool noticed = false;
      for (int c = 0; c < MAP_LEN; c++) {
        if (!discoverable(c)) continue;
        BIT_SET(G.visited, c);
        if (TFLAGS(G.map[c]) & F_SECRET) { set_terrain(c, terrain_discover(G.map[c])); noticed = true; }
      }
      if (noticed) ui_sound(SND_SECRET);
      update_fov();
      ui_log("You are now aware of the level layout.", C_INFO);
      break;
    }
    case IK_SCR_RECHARGING: {                          // Belongings.charge(true): every wand to full
      int n = 0;
      for (int i = -1; i < PACK0 + 32; i = i == BAG_SIZE - 1 ? PACK0 : i + 1) {
        Item *w = game_slot(i);
        if (item_cat(w->kind) == CAT_WAND && w->ench < wand_max(w)) { w->ench = wand_max(w); n++; }
      }
      if (n) { LOG(C_INFO, txt(TX_SCR_RECHARGED), n > 1 ? "s" : ""); ui_fx(FX_HEAL, h->pos, -1, "charged"); }
      else ui_log(txt(TX_A_SURGE_OF_ENERGY), C_INFO);
      break;
    }
    case IK_SCR_REMOVE_CURSE: {
      bool p = uncurse(&h->weapon) | uncurse(&h->armor) | uncurse(&h->ring[0]) | uncurse(&h->ring[1]);
      ALL_SLOTS(i) p |= uncurse(game_slot(i));
      h->weakness = 0;                                   // Weakness.detach
      ui_log(p ? txt(TX_YOUR_PACK_GLOWS_WITH) : txt(TX_YOUR_PACK_GLOWS_WITH_2), p ? C_GOOD : C_INFO);
      break;
    }
    case IK_SCR_TELEPORT: {                            // ScrollOfTeleportation.teleportHero
      int c = -1;
      for (int count = 10; count > 0 && c < 0; count--) c = random_respawn_cell();
      if (c < 0) ui_log(txt(TX_STRONG_MAGIC_AURA_OF), C_WARN);
      else {
        if (G.map[h->pos] == T_OPEN_DOOR && heap_at(h->pos) < 0) set_terrain(h->pos, T_DOOR);
        h->pos = c;
        ui_fx(FX_TELEPORT, c, -1, NULL);
        g_level_changed = false;
        hero_press(c);
        update_fov();
        ui_log(txt(TX_IN_A_BLINK_OF), C_INFO);
      }
      break;
    }
    case IK_SCR_CHALLENGE:
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && !(MOB_DEFS[G.mobs[i].type].flags & MD_PASSIVE)) {
        Mob *m = &G.mobs[i];
        mob_notice(i);
        if (m->state != ST_HUNTING) m->state = ST_WANDERING;
        m->target = h->pos;
      }
      for (int i = G.n_heaps - 1; i >= 0; i--) if (i < G.n_heaps && G.heaps[i].type == HEAP_MIMIC && g_mh.heap_open) {   // Mimic.spawnAt, beckoned
        int c = G.heaps[i].pos;
        if (g_mh.heap_open(i)) { int m = mob_at(c); if (m >= 0) mob_beckon(m, h->pos); }
      }
      ui_log(txt(TX_THE_SCROLL_EMITS_A), C_WARN);
      break;
    case IK_SCR_TERROR: {
      int last = -1, n = count_seen_mobs(&last);
      shop_flee();
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && !mob_immune(i, MD_I_TERROR)) {
        G.mobs[i].b.terror = G.mobs[i].b.terror + 10 > 255 ? 255 : G.mobs[i].b.terror + 10; G.mobs[i].state = ST_FLEEING; G.mobs[i].target = h->pos;
      }
      if (n == 0) ui_log(txt(TX_THE_SCROLL_EMITS_A_2), C_INFO);
      else if (n == 1) LOG(C_INFO, txt(TX_THE_SCROLL_EMITS_A_3), mob_name(G.mobs[last].type));
      else ui_log(txt(TX_THE_SCROLL_EMITS_A_4), C_INFO);
      break;
    }
    case IK_SCR_LULLABY: {
      int last = -1, n = 0;
      shop_flee();
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && !mob_immune(i, MD_I_SLEEP)) {
        Mob *m = &G.mobs[i]; m->state = ST_SLEEPING; n++; last = i; m->flags &= ~MF_SEEN;
        if (m->time < G.now + TICK * 3 / 2) m->time = G.now + TICK * 3 / 2;     // Sleep.SWS
      }
      if (n == 0) ui_log("The scroll utters a soothing melody.", C_INFO);
      else if (n == 1) LOG(C_INFO, txt(TX_THE_SCROLL_UTTERS_A), mob_name(G.mobs[last].type));
      else ui_log(txt(TX_THE_SCROLL_UTTERS_A_2), C_INFO);
      break;
    }
    case IK_SCR_PSIONIC:
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) {
        int t = rnd_int2(3, 6); if (t > G.mobs[i].b.blind) G.mobs[i].b.blind = t;
        g_dmg_src = DS_PSI; mob_damage(i, rnd_range(1, G.mobs[i].HT * 2 / 3));
      }
      { int t = rnd_int2(3, 6); if (t > h->b.blind) h->b.blind = t; ui_log(txt(TX_L_BLINDED), C_WARN); }
      update_fov();
      break;
    case IK_SCR_MIRROR: {
      int16_t cand[8]; int nc = 0;
      for (int j = 0; j < 8; j++) { int p = h->pos + N8[j]; if (nb_ok(h->pos, p) && !char_at(p) && (PASSABLE(G.map[p]) || (TFLAGS(G.map[p]) & F_AVOID))) cand[nc++] = p; }
      int n = 3;
      while (n > 0 && nc > 0) {
        int j = rnd_int(nc);
        int mi = spawn_mob(MT_MIRROR, cand[j], ST_HUNTING, 0);
        if (mi >= 0) { mirror_dup(mi); ui_fx(FX_SPAWN, cand[j], mi, NULL); }
        cand[j] = cand[--nc]; n--;
      }
      if (n == 3) return ACT_TURN;                        // nowhere to put them: still unknown
      break;
    }
  }
  set_known(k);
  return ACT_TURN;
}

#endif
static void ring_put_on(Item *r) {
  r->flags |= IF_CURSED_KNOWN;
  if (G.hero.hclass == 2 && !item_known(r->kind)) {    // Ring.RingBuff.attachTo: a rogue knows a ring at once
    set_known(r->kind); char n[40]; short_name(r, n, sizeof n); LOG(C_INFO, "This is a %s", n);
  }
  if (r->flags & IF_CURSED) { char f[48]; item_name(r, f, sizeof f); ui_sound(SND_CURSED); LOG(C_BAD, "your %s tightens around your finger painfully", f); }   // (PD logs nothing for a plain equip)
  if (r->kind == IK_RNG_DETECTION) hero_search(false);   // RingOfDetection.doEquip
}
#ifdef PICK_ONLY
static ActResult use_pick(PickMode mode, int slot) {
  Hero *h = &G.hero;
  if (slot == -1000) {                                  // cancelled
    if (mode == PICK_REFORGE || mode == PICK_RING) return ACT_NONE;
    if (g_pick_unknown) {                               // InventoryScroll.confirmCancelation: identified by use, consumed anyway
      g_trade = 6; g_trade_arg = mode;
      g_wnd_title = txt(DEFS[mode == PICK_IDENTIFY ? IK_SCR_IDENTIFY : mode == PICK_ENCHANT ? IK_SCR_ENCHANT : IK_SCR_UPGRADE].name);
      ui_wnd(-1, txt(TX_W_SCROLL_CANCEL), "Yes, I'm positive", "No, I changed my mind");
      g_wnd_title = NULL;
      return ACT_NONE;
    }
    Item back = item_make(mode == PICK_IDENTIFY ? IK_SCR_IDENTIFY : mode == PICK_ENCHANT ? IK_SCR_ENCHANT : IK_SCR_UPGRADE, 1);
    if (!bag_add(back)) heap_put(h->pos, back);
    return ACT_NONE;
  }
  Item *it = game_slot(slot);
  char n[48];
  switch (mode) {
    case PICK_IDENTIFY:
      it->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | (item_cat(it->kind) == CAT_WAND ? IF_CHARGE_KNOWN : 0);
      if (item_cat(it->kind) != CAT_WEAPON && item_cat(it->kind) != CAT_ARMOR) set_known(it->kind);
      item_name(it, n, sizeof n); LOG(C_INFO, "It is %s", n);
      break;
    case PICK_ENCHANT:                                  // ScrollOfEnchantment: uncurse, enchant / inscribe anew, fix
      it->flags &= ~IF_CURSED; it->flags |= IF_CURSED_KNOWN;
      item_enchant(it);
      if (!CLASS_ARMOR(it->kind)) it->dur = item_max_dur(it);   // item.fix() (a class armour keeps its DR in dur)
      short_name(it, n, sizeof n); LOG(C_WARN, "your %s glows in the dark", n);
      break;
    case PICK_UPGRADE:
      if (!item_broken(it) && (item_cat(it->kind) == CAT_WEAPON || item_cat(it->kind) == CAT_ARMOR || it->kind == IK_BOOMERANG) && it->ench && it->lvl > 0 && rnd_int(it->lvl) > 0) {   // Weapon / Armor.upgrade(false): magic may clash
        it->ench = 0;
        ui_log(item_cat(it->kind) == CAT_ARMOR ? "Interaction of different types of magic has erased the glyph on this armor!"
                                               : "Interaction of different types of magic has negated the enchantment on this weapon!", C_WARN);
      }
      it->flags &= ~IF_CURSED; it->flags |= IF_CURSED_KNOWN;
      if (item_broken(it)) it->dur = item_max_dur(it);   // item.fix()
      else {
        it->lvl++; it->dur = item_max_dur(it);
        if (item_cat(it->kind) == CAT_WAND && it->ench < wand_max(it)) it->ench++;   // Wand.upgrade: one more charge
      }
      short_name(it, n, sizeof n); LOG(C_GOOD, "your %s certainly looks better now", n);
      ui_hero_tier();
      break;
    case PICK_QUICK: case PICK_SELL: case PICK_SEED: case PICK_SMITH: case PICK_ARMOR: case PICK_WEAPON: case PICK_WAND: break;   // the UI fills quickslots itself; the rest: the hero overlay
    case PICK_RING: {                                  // Ring.doEquip with both hands full: the chosen ring comes off
      Item *old = &h->ring[slot == -3 ? 0 : 1];
      if (old->flags & IF_CURSED) { short_name(old, n, sizeof n); LOG(C_WARN, "You can't remove cursed %s!", n); return ACT_NONE; }
      Item nw = h->bag[g_reforge_slot];
      if (item_cat(nw.kind) != CAT_RING) return ACT_NONE;
      h->bag[g_reforge_slot] = *old;                    // they trade places
      *old = nw;
      ring_put_on(old);
      hero_spend(2 * TICK); process();                  // the old ring's doUnequip, then Ring.TIME_TO_EQUIP
      return ACT_TURN;
    }
    case PICK_REFORGE: {                               // ShortSword: sacrifice it to upgrade another weapon
      Item *sw = g_reforge_slot == -1 ? &h->weapon : game_slot(g_reforge_slot);
      if (sw->kind != IK_SHORT_SWORD) return ACT_NONE;
      *sw = (Item){ 0 };
      it->flags &= ~IF_CURSED; it->flags |= IF_CURSED_KNOWN;
      it->lvl++; it->dur = item_max_dur(it);
      short_name(it, n, sizeof n); LOG(C_WARN, txt(TX_YOU_REFORGED_THE_SHORT), n);
      ui_sound(SND_EVOKE);
      hero_spend(2 * TICK); process();
      return ACT_TURN;
    }
  }
  dispel_invisibility();                               // InventoryScroll.itemSelector (identify, upgrade, enchant)
  hero_spend(TICK);
  process();
  return ACT_TURN;
}

#else
static void eat(int k) {                               // Food.execute and the meats
  Hero *h = &G.hero;
  ui_fx(FX_OPERATE, h->pos, -1, NULL);
  ui_sound(SND_EAT);
  if (k == IK_PHANTOM_FISH) {                          // PhantomFish.execute: no food, but invisible for a while
    ui_sound(SND_MELD); ui_log("You see your hands turn invisible!", C_INFO); h->b.invis = h->b.invis > 240 ? 255 : h->b.invis + 15; return;
  }
  int energy = k == IK_RATION ? 260 : k == IK_PASTY ? 360 : 100;
  eat_satisfy(energy);
  ui_log(k == IK_MYSTERY_MEAT ? "That food tasted... strange." : k == IK_OVERPRICED ? "That food tasted ok." : "That food tasted delicious!", C_INFO);
  if (h->hclass == 0 && h->HP < h->HT) h->HP = h->HP + 5 > h->HT ? h->HT : h->HP + 5;   // Food.execute: a warrior heals 5
  if (h->hclass == 1)                                // a mage: every wand gets a charge back (Belongings.charge)
    for (int i = -1; i < PACK0 + 32; i = i == BAG_SIZE - 1 ? PACK0 : i + 1) { Item *w = game_slot(i); if (item_cat(w->kind) == CAT_WAND && w->ench < wand_max(w)) w->ench++; }
  hero_spend(3 * TICK);                              // Food.execute spends before MysteryMeat / FrozenCarpaccio add their effects
  if (k == IK_MYSTERY_MEAT) {
    switch (rnd_int(5)) {
      case 0: ui_log("Oh it's hot!", C_WARN); affect_burning(-1); break;
      case 1: ui_log("You can't feel your legs!", C_WARN); if (!hero_flying()) { if (!h->b.roots) ui_log("You can't move!", C_WARN); h->b.roots = 10; } break;   // Roots: not while flying; Hero.add
      case 2: ui_log("You are not feeling well.", C_WARN); affect_poison(-1, h->HT / 5); break;
      case 3: ui_log("You are stuffed.", C_WARN); h->b.slow = 10; break;
    }
  } else if (k == IK_CARPACCIO) {
    switch (rnd_int(5)) {
      case 0: ui_log("You see your hands turn invisible!", C_INFO); h->b.invis = h->b.invis > 240 ? 255 : h->b.invis + 15; break;
      case 1: ui_log("You feel your skin hardens!", C_INFO); if (h->b.barkskin < h->HT / 4) h->b.barkskin = h->HT / 4; break;
      case 2: ui_log("Refreshing!", C_INFO); h->b.poison = h->b.cripple = h->b.bleed = 0; h->weakness = 0; break;
      case 3: ui_log("You feel better!", C_INFO); if (h->HP < h->HT) h->HP = h->HP + h->HT / 4 > h->HT ? h->HT : h->HP + h->HT / 4; break;
    }
  }
}

static ActResult use_item(int slot, ItemAction a) {
  Item *it = game_slot(slot);
  Hero *h = &G.hero;
  int k = it->kind;
  if (h->HP <= 0 || !k) return ACT_NONE;
  g_level_changed = false; G.action = HA_NONE;          // a walk the UI broke off is over (GameScene.cancel)
  char name[48]; short_name(it, name, sizeof name);
  int cost = TICK;
  switch (a) {
    case ACN_EAT:
      bag_take_one(slot);
      eat(k);
      cost = k == IK_PHANTOM_FISH ? 3 * TICK : 0;     // (eat spent it)
      break;
    case ACN_DRINK:
      if (k == IK_DEW_VIAL) {                            // DewVial.execute: volume^log10(20) / 20 of HT
        static const uint8_t PCT[11] = { 0, 5, 13, 21, 31, 41, 52, 63, 75, 88, 100 };   // v^log10(20) / 20, in %
        if (!G.dew) { ui_log("Your dew vial is empty!", C_WARN); return ACT_NONE; }
        int value = (h->HT * PCT[G.dew] + 99) / 100, effect = h->HT - h->HP < value ? h->HT - h->HP : value;
        if (effect > 0) { h->HP += effect; char t[12]; snprintf(t, sizeof t, "+%dHP", effect); ui_fx(FX_HEAL, h->pos, -1, t); }
        G.dew = 0;
        ui_sound(SND_DRINK); ui_fx(FX_OPERATE, h->pos, -1, NULL);
        break;
      }
      bag_take_one(slot);
      potion_apply(k);
      break;
    case ACN_PLANT: {                                    // Seed.execute(AC_PLANT): on the hero's own cell
      Item one = *it; one.qty = 1;
      bag_take_one(slot);
      ui_fx(FX_OPERATE, h->pos, -1, NULL);
      land_ovl(one, h->pos);
      break;
    }
    case ACN_READ: {
      if (k == IK_MASTERY) return (ActResult)hero_op(HO_TOME, slot, 0, NULL);   // TomeOfMastery.execute: its own "You can't read while blinded"
      if (h->b.blind) { ui_log("You can't read a scroll while blinded", C_WARN); return ACT_NONE; }
      if (k == IK_WIPE_OUT) {                          // ScrollOfWipeOut.doRead: every lesser creature dies; shops and mimics give up
        bag_take_one(slot);
        ui_fx(FX_READ, h->pos, -1, NULL); ui_sound(SND_CURSED);
        h->b.invis = 0;
        for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !(MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) mob_kill(i);
        for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE || G.heaps[i].type == HEAP_MIMIC) G.heaps[i].type = HEAP_PLAIN;
        break;
      }
      bag_take_one(slot);
      ActResult r = scroll_read(k);
      if (r == ACT_NONE) return ACT_NONE;               // waiting for the item choice
      break;
    }
    case ACN_EQUIP: {
      if (item_cat(k) == CAT_RING) {                     // Ring.doEquip
        int r = !h->ring[0].kind ? 0 : !h->ring[1].kind ? 1 : -1;
        if (r < 0) { g_reforge_slot = slot; g_pick_mode = PICK_RING; ui_pick_item(PICK_RING); return ACT_NONE; }   // which one comes off?
        h->ring[r] = *it; *it = (Item){ 0 };
        ring_put_on(&h->ring[r]);
        break;
      }
      if (CLASS_ARMOR(k) && k != IK_ARMOR_WARRIOR + (int)"\0\2\1\3"[h->hclass & 3]) {   // WarriorArmor.doEquip...
        static const char *const C[4] = { "warriors", "mages", "rogues", "huntresses" };
        LOG(C_WARN, "Only %s can use this armor!", C[k - IK_ARMOR_WARRIOR == 0 ? 0 : k - IK_ARMOR_WARRIOR == 1 ? 2 : k - IK_ARMOR_WARRIOR == 2 ? 1 : 3]);
        return ACT_NONE;
      }
      bool weapon = item_cat(k) != CAT_ARMOR;            // a weapon, a mage's wand, a rogue's or huntress's missiles
      Item *cur = weapon ? &h->weapon : &h->armor;
      if (cur->kind && (cur->flags & IF_CURSED)) { char cn[40]; short_name(cur, cn, sizeof cn); LOG(C_WARN, "You can't remove cursed %s!", cn); return ACT_NONE; }
      Item take = *it, old = *cur;
      int c0 = old.kind ? 2 * hero_move_cost() : 0;     // the old armour's doUnequip: its time2equip while still worn
      *it = slot >= PACK0 ? (Item){ 0 } : old;           // what was worn goes where this was (from a bag: into the pack anew)
      if (slot >= PACK0 && old.kind && !bag_add(old)) heap_put(h->pos, old);
      *cur = take;
      cur->flags |= IF_CURSED_KNOWN;
      if (cur->flags & IF_CURSED) {
        ui_sound(SND_CURSED);
        char full[48]; item_name(cur, full, sizeof full);
        if (weapon) LOG(C_BAD, txt(TX_YOU_WINCE_AS_YOUR), name);
        else LOG(C_BAD, "your %s constricts around you painfully", full);
      }
      if (!weapon) { cost = 2 * hero_move_cost() + c0; ui_hero_tier(); }   // Armor.time2equip: 2 / hero.speed(), the new one on
      else if (old.kind) cost *= 2;                     // the old one's doUnequip spends its time2equip too
      break;
    }
    case ACN_UNEQUIP:
      if (it->flags & IF_CURSED) { LOG(C_WARN, "You can't remove cursed %s!", name); return ACT_NONE; }
      if (slot == -2) cost = 2 * hero_move_cost();      // Armor.time2equip, while it is still worn
      if (!bag_add(*it)) heap_put(h->pos, *it);
      *it = (Item){ 0 };
      if (slot == -2) ui_hero_tier();
      break;
    case ACN_DROP: {
      if (slot < 0 && (it->flags & IF_CURSED)) { LOG(C_WARN, "You can't remove cursed %s!", name); return ACT_NONE; }
      if (G.n_heaps > MAX_HEAPS - (it->kind >= IK_SEED_POUCH && it->kind <= IK_WAND_HOLSTER ? 13 : 1)) { ui_log("There is no room to drop it here.", C_WARN); return ACT_NONE; }   // the floor's item table is full
      Item all = *it;                                   // Item.doDrop: the whole stack
      cost = TICK / 2 + (slot == -2 ? 2 * hero_move_cost() : slot < 0 ? TICK : 0);   // EquipableItem.doDrop: doUnequip first (armour: 2 / speed)
      *it = (Item){ 0 };
      level_drop(h->pos, all); g_drop[g_drop_i++ & 3] = h->pos;   // 0.40: the target list leaves it there
      if (all.kind >= IK_SEED_POUCH && all.kind <= IK_WAND_HOLSTER) hero_op(HO_STASH, 0, 0, NULL);   // Item.doDrop of a bag: what it holds goes with it
      if (slot == -2) ui_hero_tier();
      break;
    }
    case ACN_MINE: {                                   // Pickaxe.execute(AC_MINE): a dark gold vein beside you
      int pos = -1;
      if (G.depth >= 11 && G.depth <= 15) for (int k = 0; k < 8 && pos < 0; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c) && G.map[c] == T_WALL_DECO) pos = c; }
      if (pos < 0) { ui_log("There is no dark gold vein near you to mine", C_WARN); return ACT_NONE; }
      ui_fx(FX_ATTACK, h->pos, -1, NULL); ui_sound(SND_EVOKE);
      set_terrain(pos, T_WALL);
      Item g = item_make(IK_DARK_GOLD, 1);
      if (bag_add(g)) ui_log("You now have dark gold ore", C_INFO); else heap_put(h->pos, g);
      if (!game_starving()) eat_satisfy(-36);             // satisfy(-STARVING / 10): never past starving (Hunger.satisfy clamps)
      cost = 2 * TICK;                                    // TIME_TO_MINE
      break;
    }
    case ACN_END: ui_amulet(1); return ACT_NONE;        // Amulet.execute(AC_END): the scene without its text
    case ACN_SHATTER: {                                // Honeypot.execute(AC_SHATTER): broken where you stand, the bee comes out
      Item one = *it; one.qty = 1; bag_take_one(slot);
      ui_fx(FX_OPERATE, h->pos, -1, NULL);
      land_ovl(one, h->pos);
      break;
    }
    case ACN_LIGHT:                                    // Torch.execute(AC_LIGHT): Light, Light.DURATION more turns
      bag_take_one(slot);
      h->light = h->light + 250 > 255 ? 255 : h->light + 250;
      ui_fx(FX_OPERATE, h->pos, -1, NULL);
      update_fov();
      break;
    case ACN_APPLY:                                    // ArmorKit / Weightstone: choose what to work on
      ui_pick_item(k == IK_ARMOR_KIT ? PICK_ARMOR : PICK_WEAPON);
      return ACT_NONE;
    case ACN_SET: case ACN_RETURN: {                   // LloydsBeacon.execute (lvl: the depth it was set on, dur | hits << 8: the cell)
      if (boss_depth(G.depth)) { ui_log("Strong magic aura of this place prevents you from using the lloyd's beacon!", C_WARN); break; }
      for (int n = 0; n < 8; n++) if (char_at(h->pos + N8[n])) { ui_log("Psychic aura of neighbouring creatures doesn't allow you to use the lloyd's beacon at this moment.", C_WARN); return ACT_NONE; }
      if (a == ACN_SET) {
        it->lvl = G.depth; it->dur = h->pos & 255; it->hits = h->pos >> 8;
        ui_fx(FX_OPERATE, h->pos, -1, NULL); ui_sound(SND_BEACON);
        ui_log("The lloyd's beacon is successfully set at your current location, now you can return here anytime.", C_INFO);
        break;
      }
      int depth = it->lvl, cell = it->dur | it->hits << 8;
      it->lvl = 0;                                     // reset
      if (depth == G.depth) {                          // WandOfBlink.appear
        ui_fx(FX_TELEPORT, h->pos, -1, NULL); h->pos = cell; ui_fx(FX_TELEPORT, cell, -1, NULL);
        hero_press(cell); update_fov();
        return ACT_TURN;
      }
      switch_level(depth, ARRIVE_CELL + cell);          // InterlevelScene.Mode.RETURN
      return ACT_LEVEL;
    }
    case ACN_DISENCHANT: ui_pick_item(PICK_WAND); return ACT_NONE;   // WandOfMagicMissile.AC_DISENCHANT
    case ACN_SPECIAL: return ACT_NONE;                 // the class armour's power aims first (main.c)
    case ACN_THROW: case ACN_ZAP: {                    // 0.50 (the test kit's hands): at the nearest creature hunting you in a clear line
      int t = -1, td = 99; bool hit; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; int d = distance(m->pos, G.hero.pos); if (m->HP > 0 && !ally(i) && m->state == ST_HUNTING && BIT_GET(G.fov, m->pos) && d < td && ballistica(G.hero.pos, m->pos, &hit) == m->pos) { td = d; t = m->pos; } }
      return t < 0 ? ACT_NONE : (ActResult)hero_op(a == ACN_ZAP ? HO_ZAP : HO_THROW, slot, t, NULL); }
    case ACN_REFORGE:
      g_reforge_slot = slot;
      ui_pick_item(PICK_REFORGE);
      return ACT_NONE;
    default: return ACT_NONE;
  }
  hero_spend(cost);
  if (g_level_changed) return ACT_LEVEL;
  process();
  return ACT_TURN;
}

OVL_ENTRY(use, (void *)use_item, NULL, (void *)use_shatter);
#endif
#ifdef PICK_ONLY
OVL_ENTRY(pick, (void *)use_pick);
#endif
