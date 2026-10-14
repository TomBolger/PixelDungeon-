/*
 * Pixel Dungeon for Pebble - the prison's creatures (depths 6-10), a region overlay (0.9).
 * Gnoll shaman (lightning), the old wandmaker and his quest, Tengu and the PrisonBossLevel arena.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Shaman, Wandmaker, WndWandmaker, Tengu, PrisonBossLevel.
 */
#include "mobovl.h"
#include "item_sp.h"
#include "mob_thief.inc"
#include "mob_skeleton.inc"
#include "mob_swarm.inc"
#include "mob_shaman.inc"
#include "mob_bat.inc"
#include "mob_brute.inc"

static const MobHooks *B;                              // the common hooks underneath (g_mh_base)

// ---------------- Tengu ----------------
#define JUMP_DELAY 5
static void tengu_jump(int i) {                        // Tengu.jump: poison traps wake up, then a leap away
  Mob *m = M_(i);
  m->aux = JUMP_DELAY;
  for (int k = 0; k < 4; k++) {
    int t = -1;
    for (int n = 0; n < 200; n++) { int c = rnd_int(MAP_LEN); if (PASSABLE(G.map[c]) && g_mh.sees(i, SEES_CELL(c))) { t = c; break; } }   // in its own sight
    if (t >= 0 && G.map[t] == T_INACTIVE_TRAP) set_terrain(t, T_POISON_TRAP);
  }
  int c = -1, n = 0;                                   // a random one of all the free cells in sight, not next to the hero
  for (int k = 0; k < MAP_LEN; k++)
    if (PASSABLE(G.map[k]) && !adjacent(k, G.hero.pos) && !char_at(k) && g_mh.sees(i, SEES_CELL(k)) && rnd_int(++n) == 0) c = k;
  if (c >= 0) {
    if (seen(m->pos) || seen(c)) { ui_fx(FX_TELEPORT, m->pos, i, NULL); ui_sound(SND_PUFF); }
    m->pos = c;
    mob_press(i);
    if (seen(c)) ui_fx(FX_TELEPORT, c, i, NULL);
  }
  mob_spend(m, TICK);
}
static int tengu_closer(int i, int target) {           // Tengu.getCloser: a target cell it can see, it leaps to (rooted or not)
  if (!g_mh.sees(i, SEES_CELL(target))) return target;
  tengu_jump(i);
  return CL_MOVED;                                     // and Mob.act spends 1 / speed after getCloser too
}
static bool tengu_attack(int i, int enemy) {
  Mob *m = M_(i);
  int epos = pos_of(enemy);
  if (--m->aux <= 0 && adjacent(m->pos, epos)) { tengu_jump(i); return true; }
  if (adjacent(m->pos, epos)) return false;            // a plain blow
  if (seen(m->pos) || seen(epos)) ui_throw(m->pos, epos, SP_SHURIKEN);   // TenguSprite: a shuriken from afar
  attack(i, enemy, NULL);
  mob_spend(m, MOB_DEFS[m->type].atk_delay);
  return true;
}

// ---------------- PrisonBossLevel: the arena behind the locked door ----------------
#define BOSS_ENTERED 2
static int boss_room(void) { for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_BOSS_EXIT) return r; return -1; }
static int arena_door(void) {                          // the boss room's way in
  int r = boss_room();
  if (r < 0) return -1;
  Room *R = &G.rooms[r];
  for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) {
    if (x != R->left && x != R->right && y != R->top && y != R->bottom) continue;
    int t = G.map[x + y * MAP_W];
    if (t == T_LOCKED_DOOR || t == T_DOOR || t == T_OPEN_DOOR) return x + y * MAP_W;
  }
  return -1;
}
static void press(int cell, int who) {                 // PrisonBossLevel.press: the hero walks in, Tengu appears, the door locks
  if (B->press) B->press(cell, who);
  if (G.depth != 10 || who != -1 || (G.boss & BOSS_ENTERED)) return;
  int r = boss_room();
  if (r < 0) return;
  Room *R = &G.rooms[r];
  if (cx(cell) <= R->left || cx(cell) >= R->right || cy(cell) <= R->top || cy(cell) >= R->bottom) return;
  G.boss |= BOSS_ENTERED;
  int pos = -1;
  for (int n = 0; n < 200 && pos < 0; n++) { int c = level_room_random(R, 0); if (c != cell && !char_at(c) && PASSABLE(G.map[c])) pos = c; }
  if (pos >= 0) {
    int k = spawn_mob(MT_TENGU, pos, ST_HUNTING, 0);
    if (k >= 0) { M_(k)->flags |= MF_SEEN; mob_notice(k); mob_press(k); }
  }
  int d = arena_door();
  if (d >= 0) set_terrain(d, T_LOCKED_DOOR);
  update_fov();
}

// ---------------- the old wandmaker (Wandmaker.Quest) ----------------
static const char *const WM_Q1[3] = {
  "Oh, what a pleasant surprise to meet a decent person in such place! I came here for a rare ingredient - a Rotberry seed. Being a magic user, I'm quite able to defend myself against local monsters, but I'm getting lost in no time, it's very embarrassing. Probably you could help me? I would be happy to pay for your service with one of my best wands.",
  "Oh, what a pleasant surprise to meet a decent person in such place! I came here for a rare ingredient - corpse dust. It can be gathered from skeletal remains and there is an ample number of them in the dungeon. Being a magic user, I'm quite able to defend myself against local monsters, but I'm getting lost in no time, it's very embarrassing. Probably you could help me? I would be happy to pay for your service with one of my best wands.",
  "Oh, what a pleasant surprise to meet a decent person in such place! I came here for a rare ingredient: a phantom fish. You can catch it with your bare hands, but it's very hard to notice in the water. Being a magic user, I'm quite able to defend myself against local monsters, but I'm getting lost in no time, it's very embarrassing. Probably you could help me? I would be happy to pay for your service with one of my best wands." };
static const char *const WM_Q2[3] = {
  "Any luck with a Rotberry seed, %s? No? Don't worry, I'm not in a hurry.",
  "Any luck with corpse dust, %s? Bone piles are the most obvious places to look.",
  "Any luck with a phantom fish, %s? You may want to try searching for it in one of the local pools." };
static const uint8_t WM_ITEM[3] = { IK_SEED_ROTBERRY, IK_CORPSE_DUST, IK_PHANTOM_FISH };
static int free_respawn_cell(void) {                   // randomRespawnCell with no heap on it
  for (int n = 0; n < 200; n++) { int c = random_respawn_cell(); if (c >= 0 && heap_at(c) < 0) return c; }
  return -1;
}
static void wm_place_item(void) {                      // QuestHandler.placeItem
  switch (G.q.wand_alt) {
    case 0: {                                          // a rotberry bush somewhere
      int c = free_respawn_cell();
      if (c >= 0 && G.n_plants < MAX_PLANTS) {
        int p = plant_at(c); if (p >= 0) G.plants[p] = G.plants[--G.n_plants];
        G.plants[G.n_plants++] = (Plant){ (int16_t)c, 7 };
        int t = G.map[c];
        if (t == T_HIGH_GRASS || t == T_EMPTY || t == T_EMBERS || t == T_EMPTY_DECO) set_terrain(c, T_GRASS);
      }
      break;
    }
    case 1: {                                          // in unseen bones, else a new pile
      int16_t cand[MAX_HEAPS]; int n = 0;
      for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].type == HEAP_SKELETON && !seen(G.heaps[h].pos)) cand[n++] = G.heaps[h].pos;
      int c = n ? cand[rnd_int(n)] : free_respawn_cell();
      if (c >= 0) { Item d = item_make(IK_CORPSE_DUST, 1); d.flags |= IF_CURSED | IF_CURSED_KNOWN; heap_put_t(c, d, HEAP_SKELETON); }   // CorpseDust: cursed
      break;
    }
    case 2: {                                          // hidden in a pool
      for (int n = 0; n < 100; n++) {
        int c = rnd_int(MAP_LEN);
        if (IS_WATER(G.map[c])) { heap_put_t(c, item_make(IK_PHANTOM_FISH, 1), HEAP_HIDDEN); return; }
      }
      int c = free_respawn_cell();
      if (c >= 0) heap_put(c, item_make(IK_PHANTOM_FISH, 1));
      break;
    }
  }
}
#define s_wnd g_trade_arg                              // the reward window is open (resident: this overlay may be dropped meanwhile)
static ActResult wm_interact(int i) {
  Quests *q = &G.q;
  if (q->wand == QS_GIVEN) {
    if (game_bag_find(WM_ITEM[q->wand_alt % 3]) >= 0) {
      s_wnd = 1;
      ui_wnd(i, "Oh, I see you have succeeded! I do hope it hasn't troubled you too much. As I promised, you can choose one of my high quality wands.",
             "Battle wand", "Non-battle wand");
    } else {
      char t[140]; snprintf(t, sizeof t, WM_Q2[q->wand_alt % 3], hero_class_name());
      ui_wnd(i, t, NULL, NULL);
    }
  } else {
    ui_wnd(i, WM_Q1[q->wand_alt % 3], NULL, NULL);
    q->wand = QS_GIVEN;
    wm_place_item();
  }
  return ACT_NONE;
}
static void wnd_choice(int b) {
  if (s_wnd != 1) { if (B->wnd_choice) B->wnd_choice(b); return; }
  s_wnd = 0;
  int w = find_mob(MT_WANDMAKER);
  Quests *q = &G.q;
  int s = game_bag_find(WM_ITEM[q->wand_alt % 3]);
  if (s >= 0) bag_take_one(s);
  Item reward = b == 0 ? q->wand1 : q->wand2;
  reward.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN; set_known(reward.kind);   // reward.identify()
  char n[40]; short_name(&reward, n, sizeof n);   // WndSadGhost / WndWandmaker / WndImp: reward.name()
  if (bag_add(reward)) LOG(C_INFO, "You now have %s", n);
  else { LOG(C_WARN, "Your pack is too full for the %s", n); if (w >= 0) heap_put(M_(w)->pos, reward); }   // Item.collect, Level.drop
  if (w >= 0) {
    char t[48]; snprintf(t, sizeof t, "Good luck in your quest, %s!", hero_class_name());
    yell(w, t);
    M_(w)->HP = 0; ui_fx(FX_DIE, M_(w)->pos, w, NULL);
  }
  q->wand = QS_REWARDED;
  q->wand1 = q->wand2 = (Item){ 0 };
}

// ---------------- the hooks ----------------
static int closer(int i, int target) {                 // Mob.getCloser overridden
  if (M_(i)->type == MT_TENGU) return tengu_closer(i, target);
  return target;                                       // (the common hook: the plain step)
}
static bool act(int i) {
  switch (M_(i)->type) {
    case MT_WANDMAKER: throw_item(i); break;           // Wandmaker.act: throwItem, then NPC.act
    case MT_THIEF: case MT_BANDIT: return thief_act(i);
  }
  return B->act ? B->act(i) : false;
}
static int can_attack(int i, int enemy, int epos) {
  switch (M_(i)->type) {
    case MT_SHAMAN: case MT_TENGU: {                   // Ballistica.cast(pos, enemy.pos, false, true) == enemy.pos
      int16_t tr[BOLT_MAX + 1];
      int n = ballistica_cast(M_(i)->pos, epos, false, true, tr);
      return n > 0 && tr[n - 1] == epos;              // (no charm check: a charmed one zaps all the same)
    }
  }
  return B->can_attack ? B->can_attack(i, enemy, epos) : -1;
}
static bool do_attack(int i, int enemy) {
  switch (M_(i)->type) {
    case MT_SHAMAN: if (distance(M_(i)->pos, pos_of(enemy)) > 1) return shaman_zap(i, enemy); return false;
    case MT_TENGU: return tengu_attack(i, enemy);
  }
  return B->do_attack ? B->do_attack(i, enemy) : false;
}
static int stats(int i, int w) {
  if (M_(i)->type == MT_SWARM && w == STAT_ATT) return 12;
  if (M_(i)->type == MT_BRUTE || M_(i)->type == MT_SHIELDED) { int v = brute_stats(i, w); if (v >= 0) return v; }
  return B->stats ? B->stats(i, w) : -1;
}
static int attack_proc(int i, int def, int dmg) {
  if (M_(i)->type == MT_THIEF || M_(i)->type == MT_BANDIT) return thief_attack_proc(i, def, dmg);
  if (M_(i)->type == MT_BAT) return bat_attack_proc(i, dmg);
  return B->attack_proc ? B->attack_proc(i, def, dmg) : dmg;
}
static int defense_proc(int i, int att, int dmg) {
  switch (M_(i)->type) {
    case MT_SWARM: swarm_defense_proc(i, dmg); return dmg;
    case MT_THIEF: case MT_BANDIT: thief_defense_proc(i); return dmg;
  }
  return B->defense_proc ? B->defense_proc(i, att, dmg) : dmg;
}
static int damage(int i, int dmg, int src) {
  if (M_(i)->type == MT_BRUTE || M_(i)->type == MT_SHIELDED) brute_damage(i, dmg);
  return B->damage ? B->damage(i, dmg, src) : dmg;
}
static void die(int i) {
  Mob *m = M_(i);
  if (m->type == MT_SWARM) swarm_die(i);
  if (m->type == MT_SKELETON) skeleton_die(i);
  if (m->type == MT_TENGU) {                           // Tengu.die: the Tome of Mastery and the key; the arena opens
    heap_put(m->pos, item_make(IK_MASTERY, 1));
    Item k = item_make(IK_SKELETON_KEY, 1); k.lvl = G.depth; heap_put(m->pos, k);
    int d = arena_door();
    if (d >= 0) set_terrain(d, T_DOOR);                // PrisonBossLevel.drop(SkeletonKey)
    if (G.hero.HP > 0) ui_fx(FX_BOSS_SLAIN, -1, -1, NULL);   // GameScene.bossSlain: banner and sound
    yell(i, "Free at last...");
    update_fov();
    return;
  }
  if (B->die) B->die(i);
}
static void spawned(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_TENGU: m->aux = JUMP_DELAY; break;
    case MT_WANDMAKER: m->state = ST_PASSIVE; break;
  }
  if (B->spawned) B->spawned(i);
}
static void notice(int i) {
  if (M_(i)->type == MT_TENGU) { char t[32]; snprintf(t, sizeof t, "Gotcha, %s!", hero_class_title()); yell(i, t); return; }
  if (B->notice) B->notice(i);
}
static ActResult interact(int i) {
  if (M_(i)->type == MT_WANDMAKER) return wm_interact(i);
  return B->interact ? B->interact(i) : ACT_NONE;
}

static void install(MobHooks *h, const MobHooks *base) {
  B = base;
  h->act = act; h->closer = closer;
  h->can_attack = can_attack;
  h->do_attack = do_attack;
  h->die = die;
  h->stats = stats;
  h->attack_proc = attack_proc;
  h->defense_proc = defense_proc;
  h->damage = damage;
  h->spawned = spawned;
  h->notice = notice;
  h->interact = interact;
  h->wnd_choice = wnd_choice;
  h->press = press;
}
OVL_ENTRY(prison, (void *)install);
