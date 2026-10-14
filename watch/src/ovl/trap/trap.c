/*
 * Pixel Dungeon for Pebble - the "trap" overlay: what a trap does when it springs (levels/traps),
 * and what a plant does when it is trampled (plants). Loaded for the moment it is needed.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"

static void trigger_trap(int cell, int who) {          // Level.press on a trap; who: -1 hero, -2 nobody, else mob index          // who: -1 hero, -2 nobody, else mob index
  int t = terrain_discover(G.map[cell]);
  bool vis = BIT_GET(G.fov, cell);
  if (vis) ui_sound(t == T_ALARM_TRAP ? SND_ALERT : t == T_LIGHTNING_TRAP ? SND_LIGHTNING : SND_TRAP);   // Level.press
  switch (t) {
    case T_TOXIC_TRAP: blob_seed(BLOB_TOXIC, cell, 300 + 20 * G.depth); break;
    case T_FIRE_TRAP: blob_seed(BLOB_FIRE, cell, 2); break;
    case T_PARALYTIC_TRAP: blob_seed(BLOB_PARALYTIC, cell, 80 + 5 * G.depth); break;
    case T_POISON_TRAP: if (who != -2) affect_poison(who, 4 + G.depth / 2); break;
    case T_ALARM_TRAP:
      for (int i = 0; i < G.n_mobs; i++) if (i != who && G.mobs[i].HP > 0 && !ally(i) && !(MOB_DEFS[G.mobs[i].type].flags & MD_PASSIVE)) {   // Mob.beckon (Statue: not)
        Mob *m = &G.mobs[i];
        mob_notice(i);
        if (m->state != ST_HUNTING) m->state = ST_WANDERING;
        m->target = cell;
      }
      if (vis) ui_log(txt(TX_THE_TRAP_EMITS_A), C_WARN);
      break;
    case T_LIGHTNING_TRAP: {
      if (who == -2) break;
      int hp = who < 0 ? G.hero.HP : G.mobs[who].HP;
      int dmg = rnd_int2(hp / 3, 2 * hp / 3); if (dmg < 1) dmg = 1;
      ui_fx(FX_TEXT, cell, who, "zap!");
      if (who < 0) {
        hero_damage(elem_resist(who, dmg), "Killed by discharge of lightning trap on level %d");   // ResultDescriptions.TRAP
        if (G.hero.HP <= 0) ui_log(txt(TX_YOU_WERE_KILLED_BY), C_BAD);
        else for (int s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) {   // Belongings.charge(false): +1 to every wand
          Item *w = game_slot(s); if (item_cat(w->kind) == CAT_WAND && w->ench < wand_max(w)) w->ench++;
        }
      }
      else mob_damage(who, dmg);
      break;
    }
    case T_GRIPPING_TRAP: {
      if (who == -2) break;
      Buffs *b = buffs_of(who);
      int dr = who < 0 ? hero_dr() : MOB_DEFS[G.mobs[who].type].dr;
      int dmg = (G.depth + 3) - rnd_range(0, dr / 2); if (dmg < 0) dmg = 0;
      b->bleed = dmg; b->cripple = 10;
      if (who < 0) { ui_log("You are bleeding!", C_WARN); ui_log("You are crippled!", C_WARN); }
      if (vis) ui_fx(FX_TEXT, cell, who, "crippled");
      break;
    }
    case T_SUMMONING_TRAP: {
      if (boss_depth(G.depth)) break;                  // SummoningTrap.trigger: Dungeon.bossLevel()
      int n = 1;
      if (rnd_int(2) == 0) { n++; if (rnd_int(2) == 0) n++; }
      int16_t cand[8]; int nc = 0;
      for (int k = 0; k < 8; k++) { int p = cell + N8[k]; if (nb_ok(cell, p) && !char_at(p) && (PASSABLE(G.map[p]) || (TFLAGS(G.map[p]) & F_AVOID))) cand[nc++] = p; }
      while (n > 0 && nc > 0) {
        int k = rnd_int(nc);
        int mi = spawn_mob(bestiary_mob(G.depth), cand[k], ST_WANDERING, 2 * TICK);   // SummoningTrap: Bestiary.mob
        if (mi >= 0) ui_fx(FX_SPAWN, cand[k], mi, NULL);
        cand[k] = cand[--nc]; n--;
      }
      break;
    }
    default: break;
  }
  set_terrain(cell, T_INACTIVE_TRAP);
}


// ---------------- Plants (plants/*.java): Plant.activate, then it withers ----------------
static void teleport_hero(void) {                      // ScrollOfTeleportation.teleportHero
  Hero *h = &G.hero;
  int c = -1;
  for (int count = 10; count > 0 && c < 0; count--) c = random_respawn_cell();
  if (c < 0) { ui_log("Strong magic aura of this place prevents you from teleporting!", C_WARN); return; }
  if (G.map[h->pos] == T_OPEN_DOOR && heap_at(h->pos) < 0) set_terrain(h->pos, T_DOOR);
  h->pos = c; G.action = HA_NONE;
  ui_fx(FX_TELEPORT, c, -1, NULL);
  hero_press(c);
  update_fov();
  ui_log("In a blink of an eye you were teleported to another location of the level.", C_INFO);
}
static void freezing(int c) {                          // Freezing.affect
  int m = mob_at(c);
  int turns = 5 * rnd_range(100, 150) / 100;
  if (m >= 0) affect_frost(m, turns);
  if (c == G.hero.pos && G.hero.HP > 0) affect_frost(-1, turns);
  blob_clear(BLOB_FIRE, c);
  for (int h = G.n_heaps - 1; h >= 0; h--) if (G.heaps[h].pos == c) heap_freeze(h);   // every pile on the cell (PD: one Heap)
}
static void plant_activate(int cell, int who) {        // who: -1 hero, -2 nobody, else mob index
  int i = plant_at(cell);
  if (i < 0) return;
  int type = G.plants[i].type;
  G.plants[i] = G.plants[--G.n_plants];                // wither: Level.uproot
  if (BIT_GET(G.fov, cell)) ui_fx(FX_SEARCHED, cell, -1, NULL);   // leaves
  if (G.hero.subclass == SUB_WARDEN) {                 // a warden: barkskin from the plant, and it may leave a seed or dew as it withers
    if (who == -1 && G.hero.b.barkskin < G.hero.HT / 3) G.hero.b.barkskin = G.hero.HT / 3;
    if (rnd_int(5) == 0) level_drop(cell, random_seed());
    if (rnd_int(5) == 0) level_drop(cell, item_make(IK_DEW, 1));
  }
  switch (type) {
    case 0: blob_seed(BLOB_FIRE, cell, 2); break;                             // Firebloom
    case 1:                                                                   // Icecap: everything within 1 that sight reaches
      freezing(cell);
      for (int k = 0; k < 8; k++) { int c = cell + N8[k]; if (nb_ok(cell, c) && !(TFLAGS(G.map[c]) & F_LOS_BLOCKING)) freezing(c); }
      break;
    case 2: if (who != -2) affect_poison(who, 4 + G.depth / 2); break;      // Sorrowmoss
    case 3: if (who != -2) blob_seed(BLOB_CONFUSION, cell, 400); break;     // Dreamweed
    case 4:                                                                   // Sungrass.Health
      if (who == -1) G.health_pos = G.hero.pos;      // (no log in PD)
      else if (who >= 0) {                             // a monster: kept in its (unused) barkskin byte, tagged with its cell; acts at once
        Mob *m = &G.mobs[who]; m->b.barkskin = 0x80 | (m->pos & 0x7F);
        m->HP += m->HT / 10; if (m->HP > m->HT) m->HP = m->HT;
      }
      break;
    case 5: if (who == -1) { G.herbal = G.hero.HT > 255 ? 255 : G.hero.HT; G.herbal_pos = G.hero.pos; } break;   // Earthroot.Armor
    case 6:                                                                   // Fadeleaf
      if (who == -1) teleport_hero();
      else if (who >= 0) {
        int c = -1;
        for (int n = 10; n > 0 && c < 0; n--) c = random_respawn_cell();
        if (c >= 0) { G.mobs[who].pos = c; ui_fx(FX_TELEPORT, c, who, NULL); }   // (its state is left alone)
      }
      break;
    case 7:                                                                   // Rotberry
      blob_seed(BLOB_TOXIC, cell, 100);
      heap_put(cell, item_make(IK_SEED_ROTBERRY, 1));
      if (who != -2) { Buffs *b = buffs_of(who); if (b->roots < 3) b->roots = 3; }
      break;
  }
}

// ---------------- Thrown things that do more than land (Item.onThrow) ----------------
static void transmute(int cell);
static void land(Item *it, int cell) {
  int k = it->kind;
  if (item_cat(k) == CAT_SEED) {                      // Seed.onThrow: grows where it lands (not in a pot or a chasm)
    if (G.map[cell] == T_ALCHEMY) {                   // Level.drop -> press: Alchemy.transmute. The pot's heap holds mixed seeds
      int h = 0;
      while (h < G.n_heaps && !(G.heaps[h].pos == cell && G.heaps[h].item.kind == k)) h++;
      if (h < G.n_heaps) G.heaps[h].item.qty++;
      else if (G.n_heaps < MAX_HEAPS) G.heaps[G.n_heaps++] = (Heap){ cell, HEAP_PLAIN, *it };
      transmute(cell); return;
    }
    if (TFLAGS(G.map[cell]) & F_PIT) { heap_put(cell, *it); return; }
    int i = plant_at(cell);
    if (i >= 0) G.plants[i] = G.plants[--G.n_plants];   // the old plant withers
    if (G.n_plants < MAX_PLANTS) {
      G.plants[G.n_plants++] = (Plant){ cell, (uint8_t)(k - IK_FIRST_SEED) };
      if (BIT_GET(G.fov, cell)) ui_sound(SND_PLANT);
    } else heap_put(cell, *it);                       // no room for another plant: it lies there
    return;
  }
  if (k == IK_BOMB) {                                 // Bomb.onThrow
    if (TFLAGS(G.map[cell]) & F_PIT) { heap_put(cell, *it); return; }
    ui_sound(SND_BLAST);
    if (BIT_GET(G.fov, cell)) ui_fx(FX_TEXT, cell, -1, "BOOM!");
    for (int n = -1; n < 8; n++) {
      int c = n < 0 ? cell : cell + N8[n];
      if (n >= 0 && !nb_ok(cell, c)) continue;
      if (TFLAGS(G.map[c]) & F_FLAMABLE) level_destroy(c);
      int m = mob_at(c);
      bool hero = c == G.hero.pos && G.hero.HP > 0;
      if (m < 0 && !hero) continue;
      int dr = hero ? hero_dr() : MOB_DEFS[G.mobs[m].type].dr;
      int dmg = rnd_int2(1 + G.depth, 10 + G.depth * 2) - rnd_int(dr);
      if (dmg <= 0) continue;
      if (hero) { hero_damage(dmg, "Killed by an explosion of a bomb on level %d"); if (G.hero.HP > 0) affect_paralysis(-1, 2); else ui_log("You killed yourself with a bomb...", C_BAD); }
      else { mob_damage(m, dmg); if (G.mobs[m].HP > 0) affect_paralysis(m, 2); }
    }
    update_fov();
    return;
  }
  if (k == IK_HONEYPOT) {                             // Honeypot.shatter: a golden bee comes out
    if (TFLAGS(G.map[cell]) & F_PIT) { heap_put(cell, *it); return; }
    ui_sound(SND_SHATTER);
    int c = cell;
    if (char_at(cell)) {                               // someone there: a random free one of the 4 straight neighbours
      int16_t cand[4]; int nc = 0;
      for (int n = 0; n < 4; n++) { int p = cell + (n & 1 ? (n & 2 ? MAP_W : -MAP_W) : (n & 2 ? 1 : -1)); if (PASSABLE(G.map[p]) && !char_at(p)) cand[nc++] = p; }
      c = nc ? cand[rnd_int(nc)] : -1;
    }
    if (c >= 0) { int b = spawn_mob(MT_BEE, c, ST_WANDERING, 0); if (b >= 0) ui_fx(FX_SPAWN, c, b, NULL); else heap_put(cell, *it); }   // no room for one more creature (MAX_MOBS): the pot holds
    return;
  }
  heap_put(cell, *it);
}

// ---------------- the alchemy pot: Heap.transmute (3 seeds or more make a potion) ----------------
static void transmute(int cell) {
  int count = 0, n = 0; uint16_t ch[MAX_HEAPS]; int8_t idx[MAX_HEAPS];
  for (int h = 0; h < G.n_heaps; h++) {
    if (G.heaps[h].pos != cell) continue;
    if (item_cat(G.heaps[h].item.kind) != CAT_SEED) return;
    count += G.heaps[h].item.qty; ch[n] = G.heaps[h].item.qty; idx[n++] = h;
  }
  if (count < 3) return;                               // SEEDS_TO_POTION
  ui_sound(SND_PUFF);
  static const uint8_t POT[8] = { IK_POT_FLAME, IK_POT_FROST, IK_POT_TOXIC, IK_POT_INVIS, IK_POT_HEALING, IK_POT_PARALYTIC, IK_POT_MIND, IK_POT_STRENGTH };
  Item result;
  if (rnd_int(count) == 0) result = gen_item(GC_POTION);
  else result = item_make(POT[G.heaps[idx[rnd_chances(ch, n)]].item.kind - IK_FIRST_SEED], 1);
  for (int h = G.n_heaps - 1; h >= 0; h--) if (G.heaps[h].pos == cell) heap_remove(h);
  int to = -1;                                         // Level.drop: no potion stays in the pot
  for (int g = 0; g < 50 && to < 0; g++) { int c = cell + N8[rnd_int(8)]; if (nb_ok(cell, c) && G.map[c] == T_EMPTY_SP) to = c; }
  heap_put(to >= 0 ? to : G.hero.pos, result);
  if (BIT_GET(G.fov, cell)) ui_fx(FX_TEXT, cell, -1, "!");
}
// ---------------- wells: WaterOfHealth / Awareness / Transmutation (WellWater.affectCell) ----------------
static bool well_hero(void) {
  Hero *h = &G.hero;
  if (G.well_kind == 1) {                              // WaterOfHealth.affectHero
    ui_sound(SND_DRINK);
    h->HP = h->HT; h->b.poison = h->b.cripple = h->b.bleed = 0; h->weakness = 0;   // PotionOfHealing.heal
    Item *eq[4] = { &h->weapon, &h->armor, &h->ring[0], &h->ring[1] };
    for (int i = 0; i < 4; i++) if (eq[i]->kind) { eq[i]->flags &= ~IF_CURSED; }   // uncurseEquipped
    h->hunger = 0;                                     // satisfy(STARVING)
    ui_fx(FX_HEAL, h->pos, -1, NULL);
    interrupt();
    ui_log("As you take a sip, you feel your wounds heal completely.", C_GOOD);
    return true;
  }
  if (G.well_kind == 0) {                              // WaterOfAwareness.affectHero
    ui_sound(SND_DRINK);
    Item *eq[4] = { &h->weapon, &h->armor, &h->ring[0], &h->ring[1] };
    for (int i = 0; i < 4; i++) if (eq[i]->kind) { eq[i]->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(eq[i]->kind); }   // belongings.observe
    for (int c = 0; c < MAP_LEN; c++) if (TFLAGS(G.map[c]) & F_SECRET) set_terrain(c, terrain_discover(G.map[c]));
    ALL_SLOTS(s) if (game_slot(s)->kind) game_slot(s)->flags |= IF_CURSED_KNOWN;      // observe: the backpack's curses
    for (int k = 0; k < G.n_heaps; k++) { int p = G.heaps[k].pos; if (!in_map(p)) continue;   // Awareness: every heap and its 8 neighbours seen
      for (int n = -1; n < 8; n++) { int c = n < 0 ? p : p + N8[n]; if (nb_ok(p, c)) BIT_SET(G.visited, c); } }
    update_fov();
    interrupt();
    ui_log("As you take a sip, you feel the knowledge pours into your mind. Now you know everything about your equipped items. Also you sense all items on the level and know all its secrets.", C_GOOD);
    return true;
  }
  return false;                                        // transmutation does nothing to the drinker
}
static const uint8_t SWAP[10][2] = { { IK_KNUCKLES, IK_DAGGER }, { IK_DAGGER, IK_KNUCKLES }, { IK_SPEAR, IK_QUARTERSTAFF }, { IK_QUARTERSTAFF, IK_SPEAR },
  { IK_SWORD, IK_MACE }, { IK_MACE, IK_SWORD }, { IK_LONGSWORD, IK_BATTLE_AXE }, { IK_BATTLE_AXE, IK_LONGSWORD }, { IK_GLAIVE, IK_WAR_HAMMER }, { IK_WAR_HAMMER, IK_GLAIVE } };
static bool well_item(Item *it) {                      // affectItem: true if the water took it
  int c = item_cat(it->kind);
  if (G.well_kind == 1) {
    if (it->kind == IK_DEW_VIAL && G.dew < 10) { G.dew = 10; return true; }   // DewVial.fill
    return false;
  }
  if (G.well_kind == 0) {
    if (item_identified(it)) return false;
    it->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | (c == CAT_WAND ? IF_CHARGE_KNOWN : 0); set_known(it->kind);
    return true;
  }
  // WaterOfTransmutation: a new item (fresh durability, no imbue, a new count to know it by)
  int nk = 0;
  if (c == CAT_WEAPON) {
    for (int i = 0; i < 10; i++) if (SWAP[i][0] == it->kind) nk = SWAP[i][1];
    if (!nk) return false;
  } else if (it->kind == IK_SCR_UPGRADE) nk = IK_SCR_ENCHANT;
  else if (it->kind == IK_SCR_ENCHANT) nk = IK_SCR_UPGRADE;
  else if (it->kind == IK_POT_STRENGTH) nk = IK_POT_MIGHT;
  else if (it->kind == IK_POT_MIGHT) nk = IK_POT_STRENGTH;
  else if (c == CAT_SCROLL || c == CAT_POTION || c == CAT_RING || c == CAT_WAND || c == CAT_SEED) {
    int g = 0;
    do nk = gen_item(c == CAT_SCROLL ? GC_SCROLL : c == CAT_POTION ? GC_POTION : c == CAT_RING ? GC_RING : c == CAT_WAND ? GC_WAND : GC_SEED).kind;
    while (nk == it->kind && ++g < 50);
  } else return false;
  Item n = item_make(nk, 1);
  if (c == CAT_WEAPON || c == CAT_RING || c == CAT_WAND) {   // changeWeapon / changeRing / changeWand: level and curse (and what is known of them)
    n.lvl = it->lvl; n.flags = it->flags & (IF_CURSED | IF_CURSED_KNOWN | IF_LEVEL_KNOWN);
    if (c == CAT_WEAPON && it->ench) n.ench = ench_random(false, 0);   // isEnchanted: n.enchant(), a random one
    if (c == CAT_WAND) n.ench = wand_max(&n);          // upgrade(level): fully charged
    n.dur = item_max_dur(&n);
  }
  if (it->qty > 1) {                                   // one of a stack changes; the rest stay
    it->qty--;
    heap_put(G.well_pos, n);
    return true;
  }
  *it = n;
  return true;
}
static void feature(int cell, int who) {               // Level.press for a well (who -1: the hero; -2: an item fell in) or the pot
  if (G.map[cell] == T_ALCHEMY) { if (who == -2) transmute(cell); return; }
  if (G.map[cell] != T_WELL || cell != G.well_pos) return;
  bool used = false;
  if (who == -1 && cell == G.hero.pos) used = well_hero();
  else {
    int h = heap_at(cell);
    if (h < 0) return;
    used = well_item(&G.heaps[h].item);
    if (!used) {                                       // the water won't take it: it is washed out beside the well
      int to = -1;
      for (int g = 0; g < 50 && to < 0; g++) { int c = cell + N8[rnd_int(8)]; if (nb_ok(cell, c) && (PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID))) to = c; }
      if (to >= 0) { Item it = G.heaps[h].item; heap_remove(h); heap_put(to, it); }
    } else if (BIT_GET(G.fov, cell)) ui_fx(FX_TEXT, cell, -1, "!");
  }
  if (used) set_terrain(cell, T_EMPTY_WELL);
}
OVL_ENTRY(trap, (void *)trigger_trap, (void *)plant_activate, (void *)land, (void *)feature);
