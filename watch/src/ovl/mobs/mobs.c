/*
 * Pixel Dungeon for Pebble - the "mobs" overlay: creatures found anywhere (mimics, wraiths, statues, piranhas,
 * bees, shopkeepers), loot, NPC wandering, and the floor's features (garden light, sacrificial fire).
 * Loaded once at start-up and kept; each depth band adds its own creatures on top (src/ovl/<band>, 0.9).
 */
#include "mobovl.h"
#include <stddef.h>

// ---------------- shops: Shopkeeper / ImpShopkeeper ----------------
static void shop_flee(int i) {                         // Shopkeeper.flee: the wares vanish with him
  for (int h = G.n_heaps - 1; h >= 0; h--)
    if (G.heaps[h].type == HEAP_FOR_SALE) { if (seen(G.heaps[h].pos)) ui_fx(FX_TELEPORT, G.heaps[h].pos, -1, NULL); heap_remove(h); }
  ui_fx(FX_TELEPORT, M_(i)->pos, i, NULL);
  M_(i)->HP = 0;
  ui_fx(FX_DIE, M_(i)->pos, i, NULL);
}
// ---------------- Mob.act's enemyInFOV: what a creature sees (Level.updateFieldOfView for it) ----------------
// Sight is symmetric: a creature sees the hero where the hero's shadowcast reaches it, but out to its own range (8;
// evil eyes, succubi and scorpios: Light.DISTANCE 4) whatever the floor's darkness, and not by what only the hero
// perceives (mind vision, a huntress's sense) nor missing what a blind hero misses: there a straight line decides.
// A blind creature sees only round itself.
// sees(i, SEES_CELL(c)): a cell, by the same reach and a straight line to it (Tengu's leaps, a succubus's blinks).
static bool sees(int i, int enemy) {
  Mob *m = M_(i);
  bool cell = enemy <= SEES_CELL(0);
  if (!cell && (enemy < -1 || (enemy < 0 && (G.hero.HP <= 0 || G.hero.b.invis)))) return false;
  unsigned c = m->pos, e = cell ? SEES_CELL(0) - enemy : enemy < 0 ? G.hero.pos : M_(enemy)->pos;
  int p = abs((int)(c % MAP_W) - (int)(e % MAP_W)), q = abs((int)(c / MAP_W) - (int)(e / MAP_W));
  if (q > p) { int s = p; p = q; q = s; }
  if (m->b.blind) return p <= 1;
  if (enemy >= 0) return seen(c);
  int r = (unsigned)(m->type - MT_SUCCUBUS) <= MT_ACIDIC - MT_SUCCUBUS ? 4 : 8;
  if (p > r || 2 * p + q > 2 * r + 2 + r / 8) return false;   // within ShadowCaster.rounding[r]
  if (!cell && p <= view_distance() && !G.hero.b.blind && !G.hero.b.mind && !(G.hero.hclass == 3 && p <= 3)) return seen(c);
  int16_t tr[BOLT_MAX + 1];
  return tr[ballistica_cast(c, e, false, false, tr) - 1] == (int)e;
}
// ---------------- the floor's features, once a turn: garden light, sacrificial fire ----------------
// Journal.add / remove: a feature in sight goes in for this depth; one that is used up (or gone) comes out
static void journal(int f, int on, int cell) {
  uint32_t bit = 1u << G.depth;
  if (!on) G.journal[f] &= ~bit;
  else if (cell >= 0 && seen(cell)) G.journal[f] |= bit;
}
_Static_assert(offsetof(Quests, wand) == 4 && offsetof(Quests, smith) == 8 && offsetof(Quests, imp) == 12, "Quests stages");
static void journal_turn(void) {
  if (G.well_pos >= 0) for (int k = 0; k < 3; k++)    // WellWater: awareness, health, transmutation (well_kind order)
    journal("\1\0\2"[k], k == G.well_kind && G.map[G.well_pos] == T_WELL, G.well_pos);
  journal(J_SACRIFICIAL, G.altar_pos >= 0 && G.lvars[0] > 1, G.altar_pos);
  int pot = -1, garden = -1;
  for (int c = 0; c < MAP_LEN; c++) if (seen(c)) { if (G.map[c] == T_ALCHEMY) pot = c; }
  for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_GARDEN) {
    Room *R = &G.rooms[r];
    for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) if (seen(x + y * MAP_W)) garden = x + y * MAP_W;
  }
  journal(J_ALCHEMY, 1, pot); journal(J_GARDEN, 1, garden);
  static const uint8_t NPC[5] = { MT_STATUE, MT_GHOST, MT_WANDMAKER, MT_BLACKSMITH, MT_IMP };
  for (int k = 0; k < 5; k++) {
    int at = -1;
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == NPC[k]) at = G.mobs[i].pos;
    int stage = k ? (&G.q.ghost)[4 * (k - 1)] : 0;     // ghost, wand, smith, imp: 4 bytes apart in Quests
    if (k != 1) journal(J_STATUE + k, at >= 0 && (!k || stage == QS_GIVEN || stage == QS_DONE), at);   // NPCs: from the quest given (Journal.add in interact) until the reward is taken (the Ghost: in the sewers' overlay)
  }
}
static void floor_turn(void) {
  Hero *h = &G.hero;
  int sh = h->H_SHADOWS && !G.seen_mobs ? h->H_SHADOWS - 1 : 0;   // Shadows.act: it wears off, or goes once enemies are in sight
  journal_turn();
  for (int r = 0; r < G.n_rooms; r++) {                // Foliage: shafts of light in a garden hide you; embers grow back
    Room *R = &G.rooms[r];
    if (R->type != RT_GARDEN) continue;
    for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++)
      if (G.map[x + y * MAP_W] == T_EMBERS) set_terrain(x + y * MAP_W, T_GRASS);
    int hx = cx(h->pos), hy = cy(h->pos);
    if (h->HP > 0 && hx > R->left && hx < R->right && hy > R->top && hy < R->bottom && !G.seen_mobs) {
      if (!h->H_SHADOWS) ui_sound(SND_MELD);            // Shadows.attachTo
      if (h->b.invis < 2) h->b.invis = 2;              // Shadows.prolong
      sh = 2;
      int n = h->hunger - 10 + ring_sum(IK_RNG_SATIETY);   // Hunger.act x1.5 slower under Shadows: one step in 3 undone (not one that
      if (G.turn_due % ((h->hclass == 2 ? 36 : 30) * TICK) == 0 && n >= 0 && n < h->hunger && h->hunger < 360 && (h->hunger < 260 || n >= 260))
        h->hunger = n;                                 // made you starving or hungry)
    }
  }
  h->H_SHADOWS = sh;                                   // while it lasts the hero sees only the cells around him (update_fov, after this turn)
  if (G.altar_pos >= 0 && G.lvars[0] > 1) {            // SacrificialFire.evolve: whoever stands in it is marked
    int k = mob_at(G.altar_pos);
    if (k >= 0) { if (!M_(k)->b.mind && seen(G.altar_pos)) ui_sound(SND_BURNING); M_(k)->b.mind = 5; }   // Marked (a mob's 'mind' timer)
  }
}
static void sacrifice(int i) {                          // SacrificialFire.sacrifice
  Mob *m = M_(i);
  int e = G.hero.lvl <= MOB_DEFS[m->type].max_lvl ? MOB_DEFS[m->type].exp : 0;
  int exp = e * rnd_range(1, 3);
  if (exp <= 0) { ui_log("\"Your sacrifice is unworthy...\"", C_WARN); return; }
  int vol = G.lvars[0] - 1 - exp;
  if (vol > 0) { G.lvars[0] = vol + 1; ui_log("\"Your sacrifice is worthy...\"", C_WARN); return; }
  G.lvars[0] = 1;
  ui_log("\"Your sacrifice is worthy and so you are!\"", C_WARN);
  heap_put(G.altar_pos, item_make(IK_WIPE_OUT, 1));
  ui_sound(SND_EVOKE);
}

// ---------------- creature turns ----------------
static bool npc_act(int i) {                          // NPC: never hunts; sleeps or wanders (Ghost at half speed)
  Mob *m = M_(i);
  if (m->state == ST_SLEEPING || m->state == ST_PASSIVE) { mob_spend(m, TICK); return true; }
  if (m->target >= 0 && m->target != m->pos && mob_get_closer(i, m->target)) mob_spend(m, mob_move_cost(m));
  else { m->target = random_destination(); mob_spend(m, TICK); }
  return true;
}
static bool act(int i) {
  Mob *m = M_(i);
  if (m->type == MT_SHOPKEEPER || m->type == MT_IMP_SHOP) {
    static const Buffs NONE;                         // Shopkeeper.add(Buff): any buff (slow, amok, terror, sleep, paralysis...) and he is gone
    if (m->state != ST_PASSIVE || MOB_AMOK(m) || memcmp(&m->b, &NONE, sizeof NONE)) { shop_flee(i); return true; }
    if (m->type == MT_IMP_SHOP && !m->aux2 && seen(m->pos)) { yell(i, "Hello, friend!"); m->aux2 = 1; }   // ImpShopkeeper.act
    throw_item(i); mob_spend(m, TICK); return true;
  }
  if (MOB_DEFS[m->type].flags & MD_NPC) return npc_act(i);
  if (m->type == MT_BEE) {                             // Bee.act: a point of life a turn; Wandering: after the hero
    if (--m->HP <= 0) { mob_kill(i); return true; }
    if (m->state == ST_WANDERING && mob_enemy(i) < 0) { if (mob_get_closer(i, G.hero.pos)) mob_spend(m, mob_move_cost(m)); else mob_spend(m, TICK); return true; }
  }
  if (m->type == MT_PIRANHA && !IS_WATER(G.map[m->pos])) { mob_kill(i); return true; }   // Piranha.act: out of water it dies
  return false;
}
static bool do_attack(int i, int enemy) {
  Mob *m = M_(i);
  if (m->type == MT_STATUE) {                          // Statue.attackDelay: its weapon's
    int h = carried(i);
    attack(i, enemy, NULL);
    mob_spend(m, h >= 0 ? TICK * weapon_dly(G.heaps[h].item.kind) / 100 : TICK);
    return true;
  }
  return false;
}
static int stats(int i, int w) {
  Mob *m = M_(i);
  int lvl = m->aux;                                    // Wraith / Mimic / Bee: the depth they were made for
  switch (m->type) {
    case MT_WRAITH:
      if (w == STAT_ATT) return 10 + lvl;
      if (w == STAT_DEF) return (10 + lvl) * 5;
      if (w == STAT_DMG) return rnd_normal(1, 3 + lvl);
      break;
    case MT_MIMIC:
      if (w == STAT_ATT) return 9 + lvl;
      if (w == STAT_DEF) return (9 + lvl) / 2;
      if (w == STAT_DMG) return rnd_normal(m->HT / 10, m->HT / 4);
      break;
    case MT_STATUE: {                                  // Statue: its weapon, the depth
      int h = carried(i);
      if (w == STAT_DEF) return 4 + G.depth;
      if (w == STAT_DR) return G.depth;
      if (h < 0) break;
      if (w == STAT_ATT) return (9 + G.depth) * weapon_acu(G.heaps[h].item.kind) / 100;
      if (w == STAT_DMG) return rnd_normal(weapon_min(&G.heaps[h].item), weapon_max(&G.heaps[h].item));
      break;
    }
    case MT_PIRANHA:
      if (w == STAT_DEF) return 10 + G.depth * 2;
      if (w == STAT_ATT) return 20 + G.depth * 2;
      if (w == STAT_DMG) return rnd_normal(G.depth, 4 + G.depth * 2);
      if (w == STAT_DR) return G.depth;
      break;
    case MT_BEE:
      if (w == STAT_ATT || w == STAT_DEF) return 9 + lvl;
      if (w == STAT_DMG) return rnd_normal(m->HT / 10, m->HT / 4);
      break;
  }
  return -1;
}
static int attack_proc(int i, int def, int dmg) {
  if (M_(i)->type == MT_STATUE && def < 0) {           // Statue.attackProc: weapon.proc (its enchantment)
    int h = carried(i);
    if (h >= 0) { Item w = G.heaps[h].item; if (w.ench) ench_weapon_proc(-2 - i, dmg, &w); item_use(&G.heaps[h].item); }   // 0.92 (audit): Weapon.proc -> use(): its weapon wears too
  }
  if (M_(i)->type == MT_MIMIC && def < 0 && rnd_int(3) == 0) {   // Mimic.attackProc: knocks gold loose
    int g = rnd_int2(G.hero.gold / 10, G.hero.gold / 2);
    if (g > 0) { G.hero.gold -= g; heap_put(G.hero.pos, item_make(IK_GOLD, g)); }
  }
  return dmg;
}
// ---------------- loot by kind (Mob.loot) ----------------
static Item loot(int i) {
  Mob *m = M_(i);
  switch (MOB_DEFS[m->type].loot) {
    case LOOT_GOLD: return item_make(IK_GOLD, gold_amount());
    case LOOT_MEAT: return item_make(IK_MYSTERY_MEAT, 1);
    case LOOT_HEALING: return item_make(IK_POT_HEALING, 1);
    case LOOT_FLAME: return item_make(IK_POT_FLAME, 1);
    case LOOT_DEW: return item_make(IK_DEW, 1);
    case LOOT_FOOD: return item_make(IK_RATION, 1);
    case LOOT_LULLABY: return item_make(IK_SCR_LULLABY, 1);
    case LOOT_SCROLL: return gen_item(GC_SCROLL);
    case LOOT_POTION: return gen_item(GC_POTION);
    case LOOT_BEACON: return item_make(IK_BEACON, 1);
    case LOOT_HAGGLER: { Item r = item_make(IK_RNG_HAGGLER, 1); r.lvl = 1; return r; }
    case LOOT_THORNS: { Item r = item_make(IK_RNG_THORNS, 1); r.lvl = 1; return r; }
    case LOOT_WEAPON: {                                 // Skeleton.dropLoot: the least upgraded of three
      Item w = gen_item(GC_WEAPON);
      for (int k = 0; k < 2; k++) { Item l = gen_item(GC_WEAPON); if (l.lvl < w.lvl) w = l; }
      return w;
    }
  }
  return (Item){ 0 };
}

// ---------------- deaths ----------------
static void die(int i) {
  Mob *m = M_(i);
  if (m->b.mind && G.altar_pos >= 0 && G.lvars[0] > 1) sacrifice(i);   // Marked.detach on a dead creature
  if (m->type == MT_PIRANHA) heap_put(m->pos, item_make(IK_MYSTERY_MEAT, 1));
  if (m->type == MT_MIMIC && G.hero.HP > 0 && G.hero.lvl <= MOB_DEFS[MT_MIMIC].max_lvl) earn_exp(2 * (m->aux - 1) / 5);   // Mimic.adjustStats: EXP grows with depth
  drop_carried(i);                                     // Thief, Mimic, Statue: what they held falls
}
static void spawned(int i) {
  Mob *m = M_(i);
  for (int h = G.n_heaps; h--; ) if (G.heaps[h].pos == CARRIED_POS(m->uid)) { m->uid = ++G.next_uid; h = G.n_heaps; }   // the uid count wrapped: not one a statue / thief / mimic holds its item by
  switch (m->type) {
    case MT_WRAITH: m->aux = G.depth; m->flags |= MF_SEEN; break;   // Wraith.adjustStats(depth); enemySeen
    case MT_MIMIC: m->aux = G.depth; m->HT = m->HP = (3 + G.depth) * 4; m->flags |= MF_SEEN; break;
    case MT_BEE: m->aux = G.depth; m->HT = m->HP = (3 + G.depth) * 5; break;   // Bee.spawn(depth)
    case MT_STATUE: {                                  // Statue(): an enchanted melee weapon, never degraded, identified
      Item w; int g = 0;
      do w = gen_item(GC_MELEE); while (w.lvl < 0 && ++g < 50);
      if (w.lvl < 0) w.lvl = 0;
      w.flags = (w.flags & ~IF_CURSED) | IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(w.kind);
      item_enchant(&w); w.dur = item_max_dur(&w);
      carry(i, w);
      m->HT = m->HP = 15 + G.depth * 5;
      m->state = ST_PASSIVE;
      break;
    }
    case MT_PIRANHA: m->HT = m->HP = 10 + G.depth * 5; break;
    case MT_SHOPKEEPER: case MT_IMP_SHOP: m->state = ST_PASSIVE; break;
  }
}
static ActResult interact(int i) {
  switch (M_(i)->type) {
    case MT_SHOPKEEPER: case MT_IMP_SHOP: ui_pick_item(PICK_SELL); return ACT_NONE;   // Shopkeeper.interact: sell()
  }
  return ACT_NONE;
}
static int damage(int i, int dmg, int src) {
  Mob *m = M_(i);
  if (m->type == MT_SHOPKEEPER || m->type == MT_IMP_SHOP) { shop_flee(i); return 0; }
  if (m->type == MT_STATUE && m->state == ST_PASSIVE) m->state = ST_HUNTING;   // Statue.damage: it wakes
  (void)src;
  return dmg;
}
static bool heap_open(int h) {                        // Heap.open: mimics, tombs, cursed remains
  Heap *hp = &G.heaps[h];
  int c = hp->pos;
  switch (hp->type) {
    case HEAP_MIMIC: {                                 // Mimic.spawnAt: the chest bites, holding what it had
      Item it = hp->item;
      int who = mob_at(c), n = -1, nc = 0;
      if (char_at(c)) {                                // someone stands there: pushed aside (onto a trap too), else no mimic
        for (int k = 0; k < 8; k++) { int p = c + N8[k]; if (nb_ok(c, p) && (PASSABLE(G.map[p]) || (TFLAGS(G.map[p]) & F_AVOID)) && !char_at(p) && rnd_int(++nc) == 0) n = p; }
        if (n < 0) { hp->type = HEAP_CHEST; return false; }
      }
      int k = spawn_mob(MT_MIMIC, c, ST_HUNTING, TICK);   // GameScene.add(m, 1)
      if (k < 0) { hp->type = HEAP_CHEST; return false; }
      heap_remove(h);
      carry(k, it);
      if (n >= 0) {                                    // Pushing: Level.mobPress / press
        if (who < 0) { G.hero.pos = n; hero_press(n); update_fov(); } else { M_(who)->pos = n; mob_press(who); }
      }
      ui_fx(FX_SPAWN, c, k, NULL);
      ui_log("This is a mimic!", C_BAD);
      return true;
    }
    case HEAP_TOMB:                                    // Wraith.spawnAround the hero
      hp->type = HEAP_PLAIN;
      for (int k = 0; k < 4; k++) {                    // the 4 orthogonal neighbours (N8[0..3])
        int n = G.hero.pos + N8[k];
        if (nb_ok(G.hero.pos, n) && PASSABLE(G.map[n]) && !char_at(n)) { int w = spawn_mob(MT_WRAITH, n, ST_HUNTING, 2 * TICK); if (w >= 0) ui_fx(FX_SPAWN, n, w, NULL); }
      }
      ui_sound(SND_TOMB);
      return true;
    case HEAP_SKELETON:
      if (hp->item.flags & IF_CURSED) {                // a cursed item: its owner's wraith rises
        hp->type = HEAP_PLAIN;
        ui_sound(SND_CURSED);
        int w = !char_at(c) ? spawn_mob(MT_WRAITH, c, ST_HUNTING, 2 * TICK) : -1;
        if (w >= 0) ui_fx(FX_SPAWN, c, w, NULL);
        else hero_damage(G.hero.HP / 2, "Killed by a curse on level %d");   // (no log in PD; HP / 2 never kills)
        return true;
      }
      return false;
  }
  return false;
}

// ---------------- Buff.act, once a turn for each living character (0.17: from game.c) ----------------
static void burning_tick(int who) {                    // Burning.act
  Buffs *b = buffs_of(who);
  int16_t pos = pos_of(who);
  if (who >= 0) g_dmg_src = DS_BURN;
  char_damage(who, elem_resist(who, rnd_int2(1, 5)), TXC(TX_C_BURNED));
  if (who < 0 && G.hero.HP > 0) {
    int s = random_unequipped();
    int k = s >= 0 ? G.hero.bag[s].kind : 0;
    if (item_cat(k) == CAT_SCROLL || k == IK_MYSTERY_MEAT) {   // a scroll burns up; meat too, and a steak takes its place
      char n[40]; Item one = G.hero.bag[s]; one.qty = 1; short_name(&one, n, sizeof n);
      bag_take_one(s); cap_first(n); LOG(C_WARN, "%s burns up!", n);
      if (k == IK_MYSTERY_MEAT) { Item c = item_make(IK_CHARGRILLED, 1); if (!bag_add(c)) heap_put(G.hero.pos, c); }
    }
  }
  if (who < 0 && G.hero.HP <= 0) ui_log("You burned to death...", C_BAD);
  if (!alive(who)) return;
  if (TFLAGS(G.map[pos]) & F_FLAMABLE) blob_seed(BLOB_FIRE, pos, 4);
  int hp = who < 0 ? G.hero.HP : G.mobs[who].HP, ht = who < 0 ? G.hero.HT : G.mobs[who].HT;
  bool flying = who < 0 && hero_flying();
  if (--b->burning == 0 || rnd_int(3000) > (2000 + 1000 * hp / ht) || (IS_WATER(G.map[pos]) && !flying)) b->burning = 0;
}
static void buffs_tick(int who) {   // h->buffs
  Buffs *b = buffs_of(who);
  if (b->poison) poison_tick(b, who);
  if (b->bleed && alive(who)) {                          // Bleeding.act
    int lvl = b->bleed / 2 + rnd_int(b->bleed - b->bleed / 2);
    b->bleed = lvl;
    if (lvl > 0) { char_damage(who, lvl, TXC(TX_C_BLED)); if (who < 0 && G.hero.HP <= 0) ui_log(txt(TX_L_BLED), C_BAD); }
  }
  if (b->burning && alive(who)) burning_tick(who);
  if (!alive(who)) return;
  if (b->cripple) b->cripple--;
  if (b->paralysis) b->paralysis--;
  if (b->frost) b->frost--;
  if (b->roots) b->roots--;
  if (b->slow) b->slow--;
  if (b->gas_immune) b->gas_immune--;
  if (b->barkskin) {
    if (who < 0) b->barkskin--;
    else {                                               // a monster's Sungrass.Health (tagged with its cell): HT/10 every 5 turns while it stays
      Mob *m = &G.mobs[who];
      if (b->barkskin != (0x80 | (m->pos & 0x7F)) || m->HP >= m->HT) b->barkskin = 0;
      else if (G.turn_due % (5 * TICK) == 0 && (m->HP += m->HT / 10) > m->HT) m->HP = m->HT;
    }
  }
  if (b->charm) b->charm--;
  if (who < 0) {
    if (G.hero.vertigo) G.hero.vertigo--;
    if (G.hero.ooze) {                                   // Ooze.act: 1 a turn, then washed off in water (a last tick there too)
      hero_damage(1, TXC(TX_C_OOZE)); if (G.hero.HP <= 0) { ui_log(txt(TX_L_OOZE), C_BAD); return; }
      if (IS_WATER(G.map[G.hero.pos])) G.hero.ooze = 0;   // Level.water: even levitating (PD checks no flight here)
    }
    if (G.hero.weakness) G.hero.weakness--;
    if (G.hero.light) G.hero.light--;
  } else {
    Mob *m = &G.mobs[who];
    if (m->vertigo) m->vertigo--;
    if (MOB_AMOK(m)) MOB_SET_AMOK(m, MOB_AMOK(m) - 1);
    if (m->flags & MF_OOZE) {                            // a creature's Ooze (Goo, the rotting fist)
      mob_damage(who, 1);
      if (IS_WATER(G.map[m->pos])) m->flags &= ~MF_OOZE;
      if (m->HP <= 0) return;
    }
  }
  if (who < 0 && G.herbal && G.hero.pos != G.herbal_pos) G.herbal = 0;          // Earthroot.Armor: gone once you move
  if (who < 0 && G.deferred) {                                                     // Viscosity.DeferedDamage
    G.deferred--;
    hero_damage(1, TXC(TX_C_VISCOSITY));
    if (G.hero.HP <= 0) { ui_log(txt(TX_L_VISCOSITY), C_BAD); return; }
  }
  if (b->invis) b->invis--;
  if (b->blind && --b->blind == 0 && who < 0) update_fov();
  if (b->mind && --b->mind == 0 && who < 0) update_fov();
  if (b->terror && --b->terror == 0 && who >= 0) G.mobs[who].state = ST_HUNTING;
  if (b->levitation && --b->levitation == 0 && who < 0) hero_press(G.hero.pos);   // Levitation.detach: land
}

static int closer(int i, int target) { return target; }   // Mob.getCloser: the plain step (the bands override it)
static void install(MobHooks *h) {
  h->act = act;
  h->do_attack = do_attack;
  h->attack_proc = attack_proc;
  h->damage = damage;
  h->stats = stats;
  h->die = die;
  h->loot = loot;
  h->spawned = spawned;
  h->interact = interact;
  h->heap_open = heap_open;
  h->turn = floor_turn;
  h->buffs = buffs_tick;
  h->sees = sees; h->closer = closer;
}
OVL_ENTRY(mobs, (void *)install);
