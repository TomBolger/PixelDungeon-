/*
 * Pixel Dungeon for Pebble - the hero overlays (0.9): hero actions that need not live in the app image.
 * 0.17: one source, two overlays so each fits a fragmented heap: "hero" (HERO_PART 0: picking up and opening heaps,
 * zapping, throwing, the item menu's actions) and "hero2" (HERO_PART 1: shops, cooking, the tome, applied items,
 * class armour specials, the ankh, the debug tools). Included by src/ovl/hero/hero.c and src/ovl/hero2/hero2.c.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"
#include "item_sp.h"
#include "items_int.h"

// reach: a Wand of Reach brought it (PD's own message; a full pack drops it at your feet, no time spent)
// Item.price (0.9 shops): what the shopkeeper pays; shops sell at 5 x (depth / 5 + 1) that
static int consider_state(const Item *it, int p) {     // Item.considerState
  if ((it->flags & IF_CURSED) && (it->flags & IF_CURSED_KNOWN)) p /= 2;
  if (it->flags & IF_LEVEL_KNOWN) {
    if (it->lvl > 0) { p *= it->lvl + 1; if (item_broken(it)) p /= 2; }
    else if (it->lvl < 0) p /= 1 - it->lvl;
  }
  return p < 1 ? 1 : p;
}
static int item_price(const Item *it) {
  int k = it->kind, q = it->qty;
  switch (DEFS[k].cat) {
    case CAT_WEAPON: return k == IK_PICKAXE ? 0 : consider_state(it, 20 * (1 << (DEFS[k].tier - 1)) * (it->ench ? 3 : 2) / 2);
    case CAT_ARMOR: return CLASS_ARMOR(k) ? 0 : consider_state(it, 10 * (1 << (DEFS[k].tier - 1)) * (it->ench ? 3 : 2) / 2);
    case CAT_WAND: return consider_state(it, 50);
    case CAT_RING: return consider_state(it, 80);
    case CAT_SEED: return 10 * q;
    case CAT_POTION: {
      static const uint8_t P[N_POTIONS] = { 30, 80, 40, 40, 100, 40, 35, 35, 50, 40, 200, 50 };   // IK_POT_* order
      return (item_known(k) ? P[k - IK_FIRST_POTION] : 20) * q;
    }
    case CAT_SCROLL: {
      static const uint8_t P[N_SCROLLS] = { 30, 25, 40, 30, 40, 15, 50, 50, 80, 15, 15, 15 };
      return (item_known(k) ? P[k - IK_FIRST_SCROLL] : 15) * q;
    }
    default: break;
  }
  switch (k) {
    case IK_DART: return 2 * q;
    case IK_INCENDIARY_DART: case IK_BOMB: case IK_TORCH: case IK_RATION: case IK_CARPACCIO: return 10 * q;
    case IK_CURARE_DART: return 12 * q;
    case IK_SHURIKEN: case IK_JAVELIN: return 15 * q;
    case IK_TOMAHAWK: case IK_PASTY: case IK_OVERPRICED: return 20 * q;
    case IK_MYSTERY_MEAT: case IK_CHARGRILLED: return 5 * q;
    case IK_HONEYPOT: case IK_ANKH: return 50 * q;
    case IK_WEIGHTSTONE: return 40 * q;
    case IK_SEED_POUCH: case IK_SCROLL_HOLDER: case IK_WAND_HOLSTER: return 50;
    case IK_RAT_SKULL: return 100;
    case IK_DARK_GOLD: return q;
    case IK_DWARF_TOKEN: return 100 * q;
    case IK_WIPE_OUT: return 100 * q;
  }
  return 0;
}

static void gold_collect(int n) {                      // Gold.doPickUp: Dungeon.gold and Statistics.goldCollected (the rankings' score)
  G.hero.gold = G.hero.gold + n > 65535 ? 65535 : G.hero.gold + n;   // (a uint16: full, not wrapped to nothing)
  uint32_t gc; memcpy(&gc, G.hero.spare + 2, 4); gc += n; memcpy(G.hero.spare + 2, &gc, 4);
  char t[12]; snprintf(t, sizeof t, "%+d", n); ui_fx(FX_GOLD, G.hero.pos, -1, t);   // showStatus(TXT_VALUE "%+d")
  ui_sound(SND_GOLD);
}
static ActResult h_pick_up(int c, bool reach) {
  int i = heap_at(c);
  if (i < 0) return ACT_NONE;
  Item it = G.heaps[i].item;
  Hero *h = &G.hero;
  if (it.kind == IK_GOLD) {
    gold_collect(it.qty);
    LOG(C_INFO, reach ? "You have magically transported %s into your backpack" : "You now have %s", "gold");   // Hero.actPickUp / WandOfReach.transport
  } else if (it.kind == IK_DEW) {                      // Dewdrop.doPickUp: heal, or into a dew vial that isn't full
    ui_sound(SND_DEWDROP);
    bool vial = game_bag_find(IK_DEW_VIAL) >= 0;
    if (h->HP < h->HT || !vial || G.dew >= 10) {
      int value = 1 + (G.depth - 1) / 5 + ((h->hclass & 3) == 3), effect = h->HT - h->HP < value * it.qty ? h->HT - h->HP : value * it.qty;
      if (effect > 0) { h->HP += effect; char t[12]; snprintf(t, sizeof t, "+%dHP", effect); ui_fx(FX_HEAL, h->pos, -1, t); }
    } else {                                             // DewVial.collectDew
      ui_log("You collected a dewdrop into your dew vial.", C_INFO);
      G.dew = G.dew + it.qty >= 10 ? 10 : G.dew + it.qty;
      if (G.dew >= 10) ui_log("Your dew vial is full!", C_GOOD);
    }
  } else if (item_cat(it.kind) == CAT_KEY) {           // Key.doPickUp: onto the keyring (Item.lvl = its floor)
    int d = it.lvl > 0 && it.lvl <= 26 ? it.lvl : G.depth, k = G.keys[d];
    for (int n = it.qty; n > 0; n--) {                 // keys stack (Key.isSimilar: same kind, same floor): each one counts
      if (it.kind == IK_IRON_KEY && (k & 15) < 15) k++;
      else if (it.kind == IK_GOLDEN_KEY && ((k >> 4) & 3) < 3) k += 16;
      else if (it.kind == IK_SKELETON_KEY) k |= 64;
    }
    G.keys[d] = k;
    char name[40]; short_name(&it, name, sizeof name); LOG(C_INFO, reach ? "You have magically transported %s into your backpack" : "You now have %s", name);
    ui_sound(SND_ITEM);
  } else {
    char name[40]; short_name(&it, name, sizeof name);
    if (!bag_add(it)) {
      LOG(C_WARN, "Your pack is too full for the %s", name);
      if (!reach) return ACT_NONE;
      heap_remove(i); heap_put(h->pos, it); return ACT_TURN;
    }
    if (it.kind == IK_RNG_THORNS || it.kind == IK_RNG_HAGGLER) {   // RingOfThorns / RingOfHaggler.doPickUp: identify()
      int s = game_bag_find(it.kind); if (s >= 0) game_slot(s)->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
      set_known(it.kind); short_name(&it, name, sizeof name);
    }
    int k = it.kind;                                     // Hero.actPickUp: "important" (GLog.p) once known
    LOG((k == IK_SCR_UPGRADE || k == IK_SCR_ENCHANT || k == IK_POT_STRENGTH || k == IK_POT_MIGHT) && item_known(k) ? C_GOOD : C_INFO,
        reach ? "You have magically transported %s into your backpack" : "You now have %s", name);
    ui_sound(SND_ITEM);
    if (it.kind == IK_AMULET && !(G.dflags & DF_AMULET)) { G.dflags |= DF_AMULET; ui_amulet(0); }   // Amulet.doPickUp
    if (it.kind == IK_SEED_ROTBERRY) {                 // Rotberry.Seed.collect: everything on the floor comes for you
      for (int k = 0; k < G.n_mobs; k++) if (G.mobs[k].HP > 0 && !ally(k) && !(MOB_DEFS[G.mobs[k].type].flags & MD_PASSIVE)) mob_beckon(k, h->pos);   // (Statue.beckon: nothing)
      ui_log(txt(TX_L_ROAR), C_WARN); ui_sound(SND_CHALLENGE);
    }
  }
  heap_remove(i);
  if (!reach && heap_at(c) >= 0) ui_log("There is something else here", C_INFO);   // Hero.actPickUp: !heap.isEmpty()
  if (it.kind >= IK_SEED_POUCH && it.kind <= IK_WAND_HOLSTER)   // a bag back in hand: what it held when it left (bags_sync)
    for (int k = G.n_heaps - 1; k >= 0; k--) if (G.heaps[k].pos == BAG_POS(it.kind - IK_SEED_POUCH)) { Item x = G.heaps[k].item; heap_remove(k); if (!bag_add(x)) heap_put(h->pos, x); }
  hero_spend(TICK);                                    // Item.doPickUp: TIME_TO_PICK_UP (by a Wand of Reach too)
  return ACT_TURN;
}
static ActResult buy_offer(int cell);
static ActResult h_open(int c, bool reach) {        // Hero.actOpenChest + Heap.open (reach: WandOfReach, no time of its own)
  int h = heap_at(c);
  if (h < 0 || G.heaps[h].type == HEAP_PLAIN) return ACT_NONE;
  int ty = G.heaps[h].type;
  if (ty == HEAP_FOR_SALE) return buy_offer(c);
  if (ty == HEAP_LOCKED_CHEST || ty == HEAP_CRYSTAL_CHEST) {   // GoldenKey
    if (!(G.keys[G.depth] & 0x30)) { ui_log("This chest is locked and you don't have matching key", C_WARN); return ACT_NONE; }
    G.keys[G.depth] -= 16;
  }
  if (g_mh.heap_open && g_mh.heap_open(h)) { if (!reach) hero_spend(TICK); return ACT_TURN; }   // Mimic, tomb: the creatures' overlay
  ui_fx(FX_OPERATE, c, -1, NULL);
  ui_sound(G.heaps[h].type == HEAP_SKELETON ? SND_BONES : SND_UNLOCK);
  if (G.heaps[h].type == HEAP_SKELETON && (G.heaps[h].item.flags & IF_CURSED)) {
    hero_damage(G.hero.HP / 2, txt(TX_KILLED_BY_A_SKELETON));
  }
  for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].pos == c && G.heaps[k].type == ty) G.heaps[k].type = HEAP_PLAIN;   // one heap, many items
  G.heaps[h].type = HEAP_PLAIN;
  if (!reach) hero_spend(TICK);                        // Key.TIME_TO_UNLOCK
  return ACT_TURN;
}

static ActResult h_zap(int slot, int16_t target) {
  Item *w = game_slot(slot);
  Hero *h = &G.hero;
  if (item_cat(w->kind) != CAT_WAND || h->HP <= 0) return ACT_NONE;
  if (target == h->pos) { ui_log("You can't target yourself", C_INFO); return ACT_NONE; }
  set_known(w->kind);                                  // zapping shows what it is
  g_level_changed = false;
  int k = w->kind == IK_WND_MM ? 13 : w->kind - IK_FIRST_WAND;
  bool hit_chars = !(w->kind == IK_WND_REACH || w->kind == IK_WND_DISINTEGRATION || w->kind == IK_WND_AVALANCHE);
  int16_t trace[BOLT_MAX];
  int n = ballistica_cast(h->pos, target, true, hit_chars, trace);
  int16_t cell = trace[n - 1];
  if (cx(target) != cx(h->pos)) h->facing_left = cx(target) < cx(h->pos);
  ui_fx(FX_ATTACK, h->pos, -1, NULL);                  // HeroSprite.zap
  int mi = mob_at(cell);
  G.quick_uid = mi >= 0 ? G.mobs[mi].uid : -1;        // QuickSlot.target (nobody there: no target)
  if (w->ench > 0) {
    wand_effect(k, wand_power(w), w->lvl, trace, n);   // the "wand" overlay: MagicMissile + onZap
    dispel_invisibility();
    w = game_slot(slot);                                // (Reach may have changed the pack)
    if (item_cat(w->kind) == CAT_WAND) {                // Wand.wandUsed
      w->ench--;
      if (!item_identified(w) && w->hits && --w->hits == 0) {
        w->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN;
        char n2[48]; short_name(w, n2, sizeof n2); LOG(C_WARN, "You are now familiar enough with your %s.", n2);   // Wand.wandUsed: name()
      }
      item_use(w);
    }
  } else {
    ui_log("Your wand fizzles; it must be out of charges for now", C_WARN);
    w->flags |= IF_LEVEL_KNOWN;
  }
  hero_spend(TICK);                                    // Wand.TIME_TO_ZAP
  if (g_level_changed) return ACT_LEVEL;
  process();
  return ACT_TURN;
}

static bool harmful(int k) { return k == IK_POT_FLAME || k == IK_POT_TOXIC || k == IK_POT_PARALYTIC; }
static bool beneficial(int k) {
  return k == IK_POT_EXPERIENCE || k == IK_POT_HEALING || k == IK_POT_LEVITATION || k == IK_POT_MIND ||
         k == IK_POT_STRENGTH || k == IK_POT_INVIS || k == IK_POT_MIGHT;
}
static int h_confirm(int slot, ItemAction a) {      // Potion.execute / doThrow warnings
  int k = game_slot(slot)->kind;
  if (a == ACN_EQUIP && item_cat(k) == CAT_MISSILE) return 6;   // MissileWeapon.doEquip: as a melee weapon?
  if (item_cat(k) != CAT_POTION || !item_known(k)) return 0;
  if (a == ACN_DRINK && harmful(k)) return 1;
  if (a == ACN_THROW && beneficial(k)) return 2;
  return 0;
}

// Bag: a bag that left the hero (dropped, thrown) takes what it holds along: the floor keeps that as heaps the bag carries
// (BAG_POS) until the bag is picked up again (h_pick_up); with no room on the floor, what is left spills (bags_sync)
static void bag_stash(void) {
  for (int r = 0; r < 3; r++) if (game_bag_find(IK_SEED_POUCH + r) < 0)
    for (Item *p = G.pack + 12 * r - (r ? 4 : 0); p < G.pack + 12 * r + 8; p++)   // game slots PACK0 + 0..7, 8..19, 20..31
      if (p->kind && G.n_heaps < MAX_HEAPS) { Heap *h = &G.heaps[G.n_heaps++]; h->pos = BAG_POS(r); h->type = HEAP_CARRIED; h->item = *p; *p = (Item){ 0 }; }
  bags_sync();
}
static void throw_drop(int16_t c, Item it) {             // Level.drop of a thrown thing that lies where it fell
  bool pit = TFLAGS(G.map[c]) & F_PIT;
  if (pit && item_stackable(it.kind))                  // Dungeon.dropToChasm: onto a stack going down too (only 4 slots)
    for (int k = 0; k < 4; k++) if (G.fall[k].kind == it.kind && G.fall_d[k] == G.depth + 1) { G.fall[k].qty += it.qty; return; }
  if (!pit && G.n_heaps > MAX_HEAPS - (it.kind >= IK_SEED_POUCH && it.kind <= IK_WAND_HOLSTER ? 13 : 1) && bag_add(it)) {   // the floor's item table full
    ui_log("There is no room to drop it here.", C_WARN); return;                                                               // (MAX_HEAPS; a bag: room for what it holds too): it comes back
  }
  level_drop(c, it);
}
static ActResult throw_item(int slot, int16_t target, bool quick) {
  Item *it = game_slot(slot);
  if (it->kind == IK_NONE || G.hero.HP <= 0) return ACT_NONE;
  if (slot < 0 && (it->flags & IF_CURSED)) { char n[40]; short_name(it, n, sizeof n); LOG(C_WARN, "You can't remove cursed %s!", n); return ACT_NONE; }
  (void)quick;
  if (target == G.hero.pos && item_cat(it->kind) == CAT_POTION) return game_item_do(slot, ACN_DRINK);   // Potion.onThrow at your feet: apply(hero)
  int unequip = slot < 0 && it->qty == 1 ? (slot == -2 ? 2 * hero_move_cost() : TICK) : 0;   // EquipableItem.cast: doUnequip first (time2equip; armour 2 / speed)
  Item one = *it; one.qty = 1;
  bool hit_mob;
  int16_t land = ballistica(G.hero.pos, target, &hit_mob);
  G.hero.facing_left = cx(target) < cx(G.hero.pos) ? 1 : cx(target) > cx(G.hero.pos) ? 0 : G.hero.facing_left;
  bag_take_one(slot);
  ui_throw(G.hero.pos, land, item_sprite(one.kind));
  int mi = hit_mob ? mob_at(land) : -1;
  G.quick_uid = mi >= 0 ? G.mobs[mi].uid : -1;         // QuickSlot.target (nobody there: no target)
  int delay = TICK; bool drop = false;
  if (item_cat(one.kind) == CAT_MISSILE) {              // MissileWeapon.onThrow
    delay = attack_delay(&one);
    Hero *h = &G.hero;
    if (mi >= 0 && h->spare[0] && h->spare[0] == G.mobs[mi].uid + 1 && (uint8_t)(h->spare[1] - G.now / TICK) <= 2) delay /= 2;   // SnipersMark: about a turn
    if (mi >= 0 || land == h->pos) h->spare[0] = 0;   // Item.cast: removed only when there is a target
    if (mi >= 0) {
      bool hit = attack(-1, mi, &one);
      if (hit && h->subclass == SUB_SNIPER && G.mobs[mi].HP > 0) { h->spare[0] = G.mobs[mi].uid + 1; h->spare[1] = (uint8_t)(G.now / TICK + 2); }
      if (one.kind == IK_BOOMERANG) {                    // Boomerang.circleBack: home to the thrower's hand or pack
        if (hit) item_use(&one);                         // Weapon.proc: use()
        if (slot == -1 && !h->weapon.kind) { h->weapon = one; delay -= TICK; }
        else if (!bag_add(one)) heap_put(h->pos, one);
      } else drop = !hit;
    }
    else if (one.kind == IK_INCENDIARY_DART && (TFLAGS(G.map[land]) & F_FLAMABLE)) blob_seed(BLOB_FIRE, land, 4);
    else drop = true;
  } else if (item_cat(one.kind) == CAT_POTION) {        // Potion.onThrow
    if (G.map[land] == T_WELL || (TFLAGS(G.map[land]) & F_PIT)) level_drop(land, one);
    else potion_shatter(one.kind, land);
  } else if (item_cat(one.kind) == CAT_SEED || one.kind == IK_BOMB || one.kind == IK_HONEYPOT) land_ovl(one, land);   // Seed / Bomb / Honeypot.onThrow
  else drop = true;
  if (drop) throw_drop(land, one);
  if (!(TFLAGS(G.map[land]) & F_PIT)) bag_stash();     // a thrown bag: what it holds goes with it (one that fell: that stays put till it is back)
  if (slot < 0) ui_hero_tier();
  hero_spend(delay + unequip);
  process();
  return ACT_TURN;
}

static int h_actions(int slot, uint8_t *out) {
  Item *it = game_slot(slot);
  int n = 0;
  switch (item_cat(it->kind)) {
    case CAT_FOOD: out[n++] = ACN_EAT; break;
    case CAT_POTION: out[n++] = ACN_DRINK; break;
    case CAT_SCROLL: out[n++] = ACN_READ; break;
    case CAT_WEAPON: case CAT_ARMOR: case CAT_RING: out[n++] = slot < 0 ? ACN_UNEQUIP : ACN_EQUIP; break;
    case CAT_WAND:                                      // only a mage can hold a wand as a weapon
      if ((G.hero.hclass & 3) == 1) out[n++] = slot < 0 ? ACN_UNEQUIP : ACN_EQUIP;
      if (it->ench > 0 || !(it->flags & IF_CHARGE_KNOWN)) out[n++] = ACN_ZAP;
      if (it->kind == IK_WND_MM && it->lvl > 0) out[n++] = ACN_DISENCHANT;
      break;
    case CAT_MISSILE: if ((G.hero.hclass & 3) >= 2) out[n++] = slot < 0 ? ACN_UNEQUIP : ACN_EQUIP; break;   // rogues and huntresses wield missiles
    case CAT_SEED: out[n++] = ACN_PLANT; break;
    default:                                            // missiles: warriors can't equip them
      switch (it->kind) {
        case IK_DEW_VIAL: if (G.dew > 0) out[n++] = ACN_DRINK; break;
        case IK_PHANTOM_FISH: out[n++] = ACN_EAT; break;
        case IK_MASTERY: out[n++] = ACN_READ; break;
        case IK_TORCH: out[n++] = ACN_LIGHT; break;
        case IK_HONEYPOT: out[n++] = ACN_SHATTER; break;   // Honeypot.AC_SHATTER
        case IK_ARMOR_KIT: case IK_WEIGHTSTONE: out[n++] = ACN_APPLY; break;
        case IK_BEACON: out[n++] = ACN_SET; if (it->lvl > 0) out[n++] = ACN_RETURN; break;   // LloydsBeacon: lvl = the depth set, dur | hits << 8 = the cell
        case IK_AMULET: out[n++] = ACN_END; break;
        case IK_WIPE_OUT: out[n++] = ACN_READ; break;
      }
  }
  if (CLASS_ARMOR(it->kind) && slot == -2 && G.hero.HP >= 3) out[n++] = ACN_SPECIAL;   // ClassArmor.actions
  if (it->kind == IK_SHORT_SWORD && it->lvl > 0) out[n++] = ACN_REFORGE;
  if (it->kind == IK_PICKAXE) out[n++] = ACN_MINE;
  out[n++] = ACN_THROW;
  if (slot >= 0 && item_quickable(it->kind)) out[n++] = ACN_QUICK;
  out[n++] = ACN_DROP;
  return n;
}
#ifdef DEBUG_JUMP
static bool interesting(int c) {                        // debug: places worth a look
  int t = G.map[c], m = mob_at(c), h = heap_at(c);
  if (m >= 0 && ((MOB_DEFS[G.mobs[m].type].flags & (MD_NPC | MD_BOSS)) || G.mobs[m].type == MT_STATUE || G.mobs[m].type == MT_PIRANHA)) return true;
  if (t == T_WELL || t == T_ALCHEMY || t == T_LOCKED_DOOR || t == T_LOCKED_EXIT || c == G.altar_pos || plant_at(c) >= 0) return true;
  return h >= 0 && G.heaps[h].type != HEAP_PLAIN && G.heaps[h].type != HEAP_CARRIED;
}
static void h_debug(void) {
  Hero *h = &G.hero;
  if (game_bag_find(IK_AMULET) >= 0) { ui_amulet(2); return; }   // with the Amulet: straight home
  if (h->pos == G.entrance && G.depth < LAST_DEPTH) { switch_level(G.depth + 1, ARRIVE_ENTRANCE); return; }   // on the up stairs: next floor
  if (h->pos == G.exit) {                                // on the down stairs: a creature of this depth
    for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c) && PASSABLE(G.map[c]) && !char_at(c)) {
      int t = bestiary(G.depth), i = spawn_mob(t, c, ST_SLEEPING, 0); if (i >= 0) ui_fx(FX_SPAWN, c, i, NULL);
      LOG(C_INFO, "debug: %s", mob_name(t)); break; } }
    return;
  }
  if (!BIT_GET(G.visited, G.exit)) {                     // first press: the map, and a few things to try
    for (int c = 0; c < MAP_LEN; c++) if (discoverable(c)) BIT_SET(G.visited, c);
    static const uint8_t K[] = { IK_POT_TOXIC, IK_POT_FLAME, IK_SCR_IDENTIFY, IK_SCR_UPGRADE, IK_MASTERY, IK_JAVELIN };
    for (unsigned i = 0; i < sizeof K; i++) { Item it = item_make(K[i], K[i] == IK_JAVELIN ? 5 : 2); set_known(K[i]); bag_add(it); }
    h->gold += 2000;
    update_fov();
    return;
  }
  for (int n = 1; n <= MAP_LEN; n++) {                   // later presses: the next interesting place, by cell order
    int c = (h->pos + n) % MAP_LEN;
    if (!interesting(c)) continue;
    for (int k = 0; k < 8; k++) {
      int d = c + N8[k];
      if (!nb_ok(c, d) || !PASSABLE(G.map[d]) || char_at(d) || d == h->pos) continue;
      h->pos = d; update_fov();
      LOG(C_INFO, "debug: to %d,%d", d % MAP_W, d / MAP_W);
      APP_LOG(APP_LOG_LEVEL_DEBUG, "debug: to %d,%d next to %d (terrain %d, mob %d, heap %d) rooms %d", d % MAP_W, d / MAP_W, c, G.map[c], mob_at(c), heap_at(c), G.n_rooms);
      return;
    }
  }
}
#endif

// ---------------- shops: WndTradeItem ----------------
static int shop_price(const Item *it) {                // WndTradeItem.price
  int p = item_price(it) * 5 * (G.depth / 5 + 1);
  if (ring_worn(IK_RNG_HAGGLER, NULL) && p >= 2) p /= 2;   // RingOfHaggler.Haggling
  return p;
}
static char s_title[48], s_text[400], s_b1[32], s_b2[32];
static void trade_wnd(const Item *it, const char *head) {
  item_name(it, s_title, sizeof s_title); cap_first(s_title);
  g_wnd_title = s_title;
  ui_wnd(-1, head, s_b1, s_b2);
  g_wnd_title = NULL;
  g_wnd_item = *it;                                    // its description is added once this overlay is out of memory (ovl.c)
}
static ActResult buy_offer(int cell) {                 // Hero.actBuy -> WndTradeItem(heap, true)
  int h = heap_at(cell);
  if (h < 0) return ACT_NONE;
  int price = shop_price(&G.heaps[h].item);
  char head[40]; snprintf(head, sizeof head, "For sale: %d gold.\n\n", price);
  if (price <= G.hero.gold) snprintf(s_b1, sizeof s_b1, "Buy for %dg", price); else snprintf(s_b1, sizeof s_b1, "Not enough gold");
  snprintf(s_b2, sizeof s_b2, "Never mind");
  g_trade = 1; g_trade_arg = cell;
  trade_wnd(&G.heaps[h].item, head);
  return ACT_NONE;
}
static ActResult sell_offer(int slot) {                // Shopkeeper.sell -> WndTradeItem(item, owner)
  Item *it = game_slot(slot);
  if (!it->kind) return ACT_NONE;
  int all = item_price(it);
  if (it->qty <= 1) { snprintf(s_b1, sizeof s_b1, "Sell for %dg", all); snprintf(s_b2, sizeof s_b2, "Never mind"); }
  else { snprintf(s_b1, sizeof s_b1, "Sell 1 for %dg", all / it->qty); snprintf(s_b2, sizeof s_b2, "Sell all for %dg", all); }
  g_trade = 2; g_trade_arg = slot;
  trade_wnd(it, "");
  return ACT_NONE;
}
static ActResult tome_choose(int b);
static ActResult balance(int slot, int b);
static ActResult resurrect(int b);
static ActResult trade_choice(int b) {
  int mode = g_trade, arg = g_trade_arg;
  g_trade = 0;
  if (mode == 3) return tome_choose(b);
  if (mode == 4) return balance(arg, b);
  if (mode == 5) return resurrect(b);
  if (mode == 6) {                                     // InventoryScroll.confirmCancelation: read anyway, or choose again
    if (b == 0) { hero_spend(TICK); process(); return ACT_TURN; }
    ui_pick_item((PickMode)arg); return ACT_NONE;
  }
  char n[48];
  if (mode == 1) {                                     // WndTradeItem.buy
    int h = heap_at(arg);
    if (b != 0 || h < 0 || G.heaps[h].type != HEAP_FOR_SALE) return ACT_NONE;
    Item it = G.heaps[h].item;
    int price = shop_price(&it);
    if (price > G.hero.gold) return ACT_NONE;
    G.hero.gold -= price;
    heap_remove(h);
    short_name(&it, n, sizeof n);                      // item.name()
    LOG(C_INFO, "You've bought %s for %dg", n, price);
    ui_sound(SND_GOLD);
    if (!bag_add(it)) { LOG(C_WARN, "Your pack is too full for the %s", n); heap_put(arg, it); return ACT_NONE; }   // Item.collect fails: Level.drop at heap.pos
    hero_spend(TICK); process(); return ACT_TURN;      // Item.doPickUp: TIME_TO_PICK_UP
  }
  if (mode == 2) {                                     // WndTradeItem.sell / sellOne, then the selector again
    Item *it = game_slot(arg);
    bool stack = it->qty > 1, sell = it->kind && (stack || b == 0);
    if (sell) {
      Item sold = *it;
      if (stack && b == 0) { sold.qty = 1; bag_take_one(arg); }
      else { *it = (Item){ 0 }; if (arg < 0) ui_hero_tier(); }
      int price = item_price(&sold);
      gold_collect(price);                             // new Gold(price).doPickUp(hero): it is collected gold, and a turn
      short_name(&sold, n, sizeof n);
      LOG(C_INFO, "You've sold your %s for %dg", n, price);
      int r = sold.kind - IK_SEED_POUCH;               // a bag sold (detachAll): what it holds goes with it
      if (r >= 0 && r <= 2) memset(G.pack + (r ? 12 * r - 4 : 0), 0, (r ? 12 : 8) * sizeof(Item));
      hero_spend(TICK); process();
    }
    ui_pick_item(PICK_SELL);
    return sell ? ACT_TURN : ACT_NONE;
  }
  return ACT_NONE;
}
// ---------------- the alchemy pot: AlchemyPot.operate -> Item.cast onto the pot ----------------
static ActResult cook(int slot) {
  int pot = g_trade_arg;
  Item *it = game_slot(slot);
  if (item_cat(it->kind) != CAT_SEED) return ACT_NONE;
  Item one = *it; one.qty = 1;
  bag_take_one(slot);
  bool hit;
  pot = ballistica(G.hero.pos, pot, &hit);             // Item.cast: whoever is in the way stops it
  ui_throw(G.hero.pos, pot, item_sprite(one.kind));
  land_ovl(one, pot);                                  // Seed.onThrow: into the pot, or it takes root where it fell
  hero_spend(TICK);
  process();
  return ACT_TURN;
}

// ---------------- Tome of Mastery: WndChooseWay ----------------
static const char *const SUB_TITLE[9] = { "", "gladiator", "berserker", "warlock", "battlemage", "assassin", "freerunner", "sniper", "warden" };
static const char *const SUB_DESC[9] = { "",
  "A successful attack with a melee weapon allows the Gladiator to start a combo, in which every next successful hit inflicts more damage.",
  "When severely wounded, the Berserker enters a state of wild fury significantly increasing his damage output.",
  "After killing an enemy the Warlock consumes its soul. It heals his wounds and satisfies his hunger.",
  "When fighting with a wand in his hands, the Battlemage inflicts additional damage depending on the current number of charges. Every successful hit restores 1 charge to this wand.",
  "When performing a surprise attack, the Assassin inflicts additional damage to his target.",
  "The Freerunner can move almost twice faster, than most of the monsters. When he is running, the Freerunner is much harder to hit. For that he must be unencumbered and not starving.",
  "Snipers are able to detect weak points in an enemy's armor, effectively ignoring it when using a missile weapon.",
  "Having a strong connection with forces of nature gives Wardens an ability to gather dewdrops and seeds from plants. Also trampling a high grass grants them a temporary armor buff." };
static uint8_t s_way[2];
static bool tome_ways(void) {                          // the window's two ways (0.29: worked out again when one is chosen: the
  Hero *h = &G.hero;                                   // overlay that showed the window is gone by then); true: a respec
  int a = 1 + 2 * (h->hclass & 3), b = a + 1;          // warrior: gladiator / berserker; mage: battlemage / warlock (PD's order below)
  if ((h->hclass & 3) == 1) { a = SUB_BATTLEMAGE; b = SUB_WARLOCK; }
  bool re = h->subclass == a || h->subclass == b;
  s_way[0] = re ? (h->subclass == a ? b : a) : a; s_way[1] = re ? 0 : b;
  return re;
}
static ActResult tome_read(int slot) {                 // TomeOfMastery.execute(AC_READ)
  Hero *h = &G.hero;
  if (h->b.blind) { ui_log("You can't read while blinded", C_WARN); return ACT_NONE; }
  int a = s_way[0], b = s_way[1];
  snprintf(s_title, sizeof s_title, h->subclass ? "Tome of Remastery" : "Tome of Mastery");
  if (tome_ways()) {                                   // the respec window
    snprintf(s_text, sizeof s_text, "%s\n\nDo you want to respec into %s %s?", SUB_DESC[s_way[0]], VOWEL(SUB_TITLE[s_way[0]][0]) ? "an" : "a", SUB_TITLE[s_way[0]]);
    snprintf(s_b1, sizeof s_b1, "Yes, I want to respec"); snprintf(s_b2, sizeof s_b2, "Maybe later");
  } else {
    a = s_way[0]; b = s_way[1];
    snprintf(s_text, sizeof s_text, "%s\n\n%s\n\nWhich way will you follow?", SUB_DESC[a], SUB_DESC[b]);
    snprintf(s_b1, sizeof s_b1, "%s", SUB_TITLE[a]); snprintf(s_b2, sizeof s_b2, "%s", SUB_TITLE[b]);
    cap_first(s_b1); cap_first(s_b2);
  }
  g_trade = 3; g_trade_arg = slot;
  g_wnd_title = s_title;
  ui_wnd(-1, s_text, s_b1, s_b2);
  g_wnd_title = NULL;
  return ACT_NONE;
}
static ActResult tome_choose(int b) {                  // TomeOfMastery.choose
  Hero *h = &G.hero;
  tome_ways();
  int way = s_way[b];
  if (!way || game_slot(g_trade_arg)->kind != IK_MASTERY) return ACT_NONE;
  bag_take_one(g_trade_arg);
  h->subclass = way;                                   // (Combo and Fury stay: TomeOfMastery.choose only sets subClass)
  ui_fx(FX_OPERATE, h->pos, -1, NULL);
  ui_sound(SND_MASTERY);
  char t[24]; snprintf(t, sizeof t, "%s", SUB_TITLE[way]); cap_first(t);
  LOG(C_WARN, "You have chosen the way of the %s!", t);
  if (way == SUB_BERSERKER && h->HP <= h->HT * 2 / 5) h->fury = 1;
  hero_spend(10 * TICK);                               // TIME_TO_READ
  process();
  return ACT_TURN;
}

// ---------------- ArmorKit, Weightstone (0.12) ----------------
static uint8_t s_bal[2];                               // WndBalance's buttons: 1 speed, 2 accuracy, 0 never mind
static int bal_ways(const Item *it) {                  // (0.29: worked out again at the choice: the overlay was reloaded)
  int nb = 0;
  if (!(it->flags & IF_FAST)) s_bal[nb++] = 1;
  if (!(it->flags & IF_ACCURATE)) s_bal[nb++] = 2;
  if (nb < 2) s_bal[nb] = 0;
  return nb;
}
static ActResult h_apply(int slot, int mode) {
  Hero *h = &G.hero;
  Item *it = game_slot(slot);
  char n[48]; short_name(it, n, sizeof n);
  if (mode == PICK_ARMOR) {                            // ArmorKit.upgrade -> ClassArmor.upgrade
    int kit = game_bag_find(IK_ARMOR_KIT);
    if (kit < 0 || item_cat(it->kind) != CAT_ARMOR) return ACT_NONE;
    bag_take_one(kit);
    LOG(C_WARN, "you applied the armor kit to upgrade your %s", n);
    Item ca = { .qty = 1, .kind = IK_ARMOR_WARRIOR + "\0\2\1\3"[h->hclass & 3], .flags = IF_LEVEL_KNOWN | IF_CURSED_KNOWN };
    ca.hits = item_str(it); ca.dur = armor_dr(it); ca.ench = it->ench;   // STR, DR() and the glyph carry over
    *it = ca;
    ui_fx(FX_OPERATE, h->pos, -1, NULL); ui_sound(SND_EVOKE);
    if (slot == -2) ui_hero_tier();
    hero_spend(2 * TICK); process();
    return ACT_TURN;
  }
  if (mode == PICK_WAND) {                             // WandOfMagicMissile: disenchanted to upgrade another wand
    Item *mm = h->weapon.kind == IK_WND_MM ? &h->weapon : game_bag_find(IK_WND_MM) >= 0 ? game_slot(game_bag_find(IK_WND_MM)) : NULL;
    if (!mm || item_cat(it->kind) != CAT_WAND || it == mm) return ACT_NONE;
    *mm = (Item){ 0 };
    ui_sound(SND_EVOKE); ui_fx(FX_READ, h->pos, -1, NULL);
    LOG(C_WARN, "you disenchanted the Wand of Magic Missile and used its essence to upgrade your %s", n);
    it->flags &= ~IF_CURSED; it->lvl++; it->dur = item_max_dur(it);   // item.upgrade()
    if (it->ench < wand_max(it)) it->ench++;
    hero_spend(2 * TICK); process();
    return ACT_TURN;
  }
  if (!it->kind) return ACT_NONE;                      // Weightstone: WndBalance
  bal_ways(it);
  for (int i = 0; i < 2; i++) snprintf(i ? s_b2 : s_b1, 32, "%s", s_bal[i] == 1 ? "For speed" : s_bal[i] ? "For accuracy" : "Never mind");
  item_name(it, s_title, sizeof s_title); cap_first(s_title);
  snprintf(s_text, sizeof s_text, "How would you like to balance your %s?", n);
  g_trade = 4; g_trade_arg = slot;
  g_wnd_title = s_title;
  ui_wnd(-1, s_text, s_b1, s_b2);
  g_wnd_title = NULL;
  return ACT_NONE;
}
static ActResult balance(int slot, int b) {            // Weightstone.apply
  int ws = game_bag_find(IK_WEIGHTSTONE);
  Item *it = game_slot(slot);
  bal_ways(it);
  if (b < 0 || b > 1 || !s_bal[b] || ws < 0 || !it->kind) return ACT_NONE;
  bag_take_one(ws);
  it->dur = item_max_dur(it);                          // weapon.fix()
  it->flags = (it->flags & ~(IF_FAST | IF_ACCURATE)) | (s_bal[b] == 1 ? IF_FAST : IF_ACCURATE);
  char n[48]; short_name(it, n, sizeof n);
  LOG(C_GOOD, s_bal[b] == 1 ? "you balanced your %s to make it faster" : "you balanced your %s to make it more accurate", n);
  ui_fx(FX_OPERATE, G.hero.pos, -1, NULL); ui_sound(SND_MISS);
  hero_spend(2 * TICK); process();
  return ACT_TURN;
}
// ---------------- ClassArmor specials: heroic leap, smoke bomb, molten earth, spectral blades ----------------
static ActResult h_leap(int target) {
  Hero *h = &G.hero;
  int k = h->armor.kind;
  if (h->HP < 3) { ui_log("Your health is too low!", C_WARN); return ACT_NONE; }
  if (k == IK_ARMOR_MAGE) {                            // MageArmor: every enemy in sight burns, held fast
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) {
      affect_burning(i);
      if (G.mobs[i].HP > 0 && !mob_immune(i, MD_I_ROOTS | MD_FLY) && G.mobs[i].b.roots < 3) G.mobs[i].b.roots = 3;   // Roots.attachTo: not a flier
    }
    h->HP -= h->HP / 3;
    ui_fx(FX_OPERATE, h->pos, -1, NULL); ui_sound(SND_READ); ui_fx(FX_QUAKE, h->pos, 2, NULL);   // 0.29: the molten earth rumbles
    hero_spend(TICK); process();
    return ACT_TURN;
  }
  if (k == IK_ARMOR_HUNTRESS) {                        // HuntressArmor: a spectral blade at every enemy in sight
    int n = 0;
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) n++;
    if (!n) { ui_log("No enemies in sight", C_WARN); return ACT_NONE; }
    h->HP -= h->HP / 3;
    ui_fx(FX_ATTACK, h->pos, -1, NULL);
    bool shown = false;
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) {
      if (!shown) { ui_throw(h->pos, G.mobs[i].pos, SP_SHURIKEN); shown = true; }
      attack(-1, i, NULL);
    }
    hero_spend(attack_delay(h->weapon.kind ? &h->weapon : NULL)); process();
    return ACT_TURN;
  }
  if (target == h->pos || target < 0) return ACT_NONE;
  if (k == IK_ARMOR_ROGUE) {                           // RogueArmor: blind whoever sees you and vanish to where you look
    if (!BIT_GET(G.fov, target) || !(PASSABLE(G.map[target]) || (TFLAGS(G.map[target]) & F_AVOID)) || char_at(target)) {
      ui_log("You can only jump to an empty location in your field of view", C_WARN); return ACT_NONE;
    }
    h->HP -= h->HP / 3;
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) {
      if (G.mobs[i].b.blind < 2) G.mobs[i].b.blind = 2;
      G.mobs[i].state = ST_WANDERING;
    }
    ui_fx(FX_TELEPORT, h->pos, -1, NULL);
    h->pos = target; ui_fx(FX_TELEPORT, target, -1, NULL);   // WandOfBlink.appear
    ui_sound(SND_PUFF);
    g_level_changed = false;
    hero_press(target); update_fov();
    hero_spend(TICK);
    if (g_level_changed) return ACT_LEVEL;              // Chasm.heroFall
    process();
    return ACT_TURN;
  }
  int16_t tr[BOLT_MAX + 1];                            // WarriorArmor: heroic leap
  int n = ballistica_cast(h->pos, target, false, true, tr), cell = tr[n - 1];
  if (char_at(cell) && cell != h->pos && n >= 2) cell = tr[n - 2];
  if (cell == h->pos) return ACT_NONE;
  h->HP -= h->HP / 3;
  if (h->subclass == SUB_BERSERKER && h->HP <= h->HT * 2 / 5) h->fury = 1;
  dispel_invisibility();
  ui_fx(FX_JUMP, cell, -1, NULL);                      // 0.29: HeroSprite.jump (an arc), then the slam shakes the floor
  h->pos = cell;                                       // sprite.jump, then move / press / observe
  g_level_changed = false;
  hero_press(cell);
  if (g_level_changed) { hero_spend(TICK); return ACT_LEVEL; }   // Chasm.heroFall
  update_fov();
  for (int k2 = 0; k2 < 8; k2++) {                     // the slam stuns every neighbour
    int c = cell + N8[k2], m = mob_at(c);
    if (m >= 0 && nb_ok(cell, c)) affect_paralysis(m, 3);
  }
  ui_sound(SND_ROCKS); ui_fx(FX_QUAKE, cell, 1, NULL);
  hero_spend(TICK); process();
  return ACT_TURN;
}
// ---------------- Ankh: WndResurrect ----------------
static ActResult ankh_offer(void) {                    // Hero.die with an Ankh: WndResurrect
  g_trade = 5;
  g_wnd_title = "Ankh";
  ui_wnd(-1, "You died, but you were given another chance to win this dungeon. Will you take it?", "Yes, I will fight!", "No, I give up");
  g_wnd_title = NULL;
  return ACT_NONE;
}
static ActResult resurrect(int b) {
  if (b != 0) {                                        // "No, I give up": Hero.reallyDie
    for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind == IK_ANKH) G.hero.bag[i] = (Item){ 0 };   // (ankhs never go in a bag)
    return ACT_TURN;
  }
  floor_op(FO_RESURRECT, 0, NULL);                     // InterlevelScene.Mode.RESURRECT
  return ACT_LEVEL;
}

static int hero_dispatch(int op, int a, int b, void *p) {
  G.action = HA_NONE;                                  // a walk the UI broke off is over (GameScene.cancel)
  switch (op) {
#if HERO_PART == 0
    case HO_PICKUP: return h_pick_up(a, b);
    case HO_OPEN: return h_open(a, b);
    case HO_ZAP: return h_zap(a, b);
    case HO_THROW: return throw_item(a, b, false);
    case HO_ACTIONS: return h_actions(a, p);
    case HO_CONFIRM: return h_confirm(a, (ItemAction)b);
    case HO_STASH: bag_stash(); return 0;
#else
    case HO_SELL: return sell_offer(a);
    case HO_TRADE: return trade_choice(a);
    case HO_COOK: return cook(a);
    case HO_TOME: return tome_read(a);
    case HO_APPLY: return h_apply(a, b);
    case HO_LEAP: return h_leap(a);
    case HO_ANKH: return ankh_offer();
#ifdef DEBUG_JUMP
    case HO_DEBUG: h_debug(); return 0;
#endif
#endif
  }
  return 0;
}
#if HERO_PART == 0
OVL_ENTRY(hero, (void *)hero_dispatch);
#else
OVL_ENTRY(hero2, (void *)hero_dispatch);
#endif
