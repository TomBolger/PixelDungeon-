/*
 * Pixel Dungeon for Pebble - the Demon Halls' creatures (depths 22-26), a band overlay (0.12).
 * Succubi, evil eyes and their deathgaze, scorpios and acidic scorpios, and Yog-Dzewa with its fists and larvae
 * in the HallsBossLevel.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Succubus, Eye, Scorpio, Acidic, Yog (RottingFist, BurningFist, Larva),
 * HallsBossLevel.
 */
#include "mobovl.h"
#include "item_sp.h"
#include "mob_succubus.inc"

static const MobHooks *B;

// the creature's current enemy and whether it sees it (Mob.act's enemyInFOV)
static bool enemy_in_fov(int i, int *enemy, int16_t *epos) {
  *enemy = mob_enemy(i);
  if (*enemy < -1) { *epos = -1; return false; }
  *epos = pos_of(*enemy);
  return g_mh.sees(i, *enemy);
}

// ---------------- Evil eye: a deathgaze through everything in the line ----------------
static int eye_ray(int i, int16_t epos, int16_t *tr) {   // Eye.canAttack: the ray's length (0: the enemy isn't on it)
  int n = ballistica_cast(M_(i)->pos, epos, true, false, tr);
  for (int k = 1; k < n; k++) if (tr[k] == epos) return n;
  return 0;
}
static bool eye_attack(int i) {                        // Eye.doAttack / attack
  Mob *m = M_(i);
  int16_t tr[BOLT_MAX + 1];
  int enemy = mob_enemy(i);
  int n = eye_ray(i, pos_of(enemy), tr);
  if (!n) return false;
  mob_spend(m, MOB_DEFS[m->type].atk_delay);
  bool visible = false;
  for (int k = 0; k < n; k++) if (seen(tr[k])) visible = true;
  if (visible) ui_zap(IK_WND_DISINTEGRATION - IK_FIRST_WAND, m->pos, tr[n - 1], NULL, 0);   // DeathRay
  for (int k = 1; k < n; k++) {
    int c = tr[k], who;
    if (c == G.hero.pos && G.hero.HP > 0) who = -1;
    else if ((who = mob_at(c)) < 0 || who == i) continue;
    if (char_hit(i, who, true)) {
      int dmg = rnd_normal(14, 20);
      if (who < 0) {
        hero_damage(elem_resist(-1, dmg), "Killed by an evil eye on level %d");
        if (G.hero.HP <= 0) ui_log("Evil eye's deathgaze killed you...", C_BAD);
      } else mob_damage(who, dmg);
    } else if (seen(c)) ui_fx(FX_TEXT, c, who, who < 0 ? "dodged" : "evaded");
  }
  return true;
}

// ---------------- Scorpio and acidic scorpio: spikes from afar, never close in ----------------
static bool scorpio_act(int i) {                       // Scorpio.getCloser: while hunting, back away from what it sees
  Mob *m = M_(i);
  if (m->state != ST_HUNTING || mob_paralysed(m)) return false;
  int enemy; int16_t epos;
  bool in_fov = enemy_in_fov(i, &enemy, &epos);
  if (in_fov && mob_can_attack(i, enemy, epos)) return false;   // the plain hunt attacks
  m->flags &= ~MF_ALERTED;
  if (in_fov) {
    m->flags |= MF_SEEN; m->target = epos;
    int step = mob_flee_step(i, epos);                 // (getFurther: roots or not)
    if (step >= 0) { mob_move(i, step); mob_spend(m, mob_move_cost(m)); return true; }
  } else m->flags &= ~MF_SEEN;
  mob_spend(m, TICK); m->state = ST_WANDERING; m->target = random_destination();
  return true;
}
static bool scorpio_attack(int i, int enemy) {         // ScorpioSprite: a dart from afar, then a plain blow
  Mob *m = M_(i);
  int epos = pos_of(enemy);
  if (seen(m->pos) || seen(epos)) ui_throw(m->pos, epos, SP_DART);
  attack(i, enemy, NULL);
  mob_spend(m, MOB_DEFS[m->type].atk_delay);
  return true;
}

// ---------------- Yog-Dzewa, its fists and larvae ----------------
static int fists(void) { int n = 0; for (int i = 0; i < G.n_mobs; i++) if (M_(i)->HP > 0 && (M_(i)->type == MT_ROTTING_FIST || M_(i)->type == MT_BURNING_FIST)) n++; return n; }
static void beckon(int i, int cell) {                  // Mob.beckon
  Mob *m = M_(i);
  if (m->state != ST_HUNTING) m->state = ST_WANDERING;
  m->target = cell;
}
static bool burning_fist_attack(int i, int enemy) {    // BurningFist.attack: a shadow bolt when not adjacent
  Mob *m = M_(i);
  int epos = pos_of(enemy);
  if (adjacent(m->pos, epos)) return false;
  mob_spend(m, MOB_DEFS[m->type].atk_delay);
  if (seen(m->pos) || seen(epos)) ui_zap(12, m->pos, epos, NULL, 0);   // MagicMissile.shadow
  if (char_hit(i, enemy, true)) {
    int dmg = rnd_normal(20, 32);
    if (enemy < 0) {
      hero_damage(elem_resist(-1, dmg), "Killed by the burning fist on level %d");
      if (G.hero.HP <= 0) ui_log("Burning fist killed you...", C_BAD);
    } else mob_damage(enemy, dmg);
  } else if (seen(epos)) ui_fx(FX_TEXT, epos, enemy, enemy < 0 ? "dodged" : "evaded");
  return true;
}
static void spawn_fists(int yog) {                     // Yog.spawnFists
  int p = M_(yog)->pos, a = -1, b = -1;
  for (int n = 0; n < 200; n++) {
    a = p + N8[rnd_int(8)]; b = p + N8[rnd_int(8)];
    if (nb_ok(p, a) && nb_ok(p, b) && PASSABLE(G.map[a]) && PASSABLE(G.map[b]) && a != b && !char_at(a) && !char_at(b)) break;
    a = b = -1;
  }
  if (a < 0) return;
  spawn_mob(MT_ROTTING_FIST, a, ST_WANDERING, 0);
  spawn_mob(MT_BURNING_FIST, b, ST_WANDERING, 0);
}
static int yog_defense(int i, int att, int dmg) {      // Yog.defenseProc: a larva crawls out
  int16_t cand[8]; int n = 0, p = M_(i)->pos;
  for (int k = 0; k < 8; k++) {
    int c = p + N8[k];
    if (nb_ok(p, c) && !char_at(c) && (PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID))) cand[n++] = c;
  }
  if (n) { int c = cand[rnd_int(n)], k = spawn_mob(MT_LARVA, c, ST_HUNTING, 0); if (k >= 0 && seen(c)) ui_fx(FX_SPAWN, c, k, NULL); }
  return dmg;
}

// ---------------- HallsBossLevel ----------------
#define HB_L (MAP_W / 2 - 1)
#define HB_R (MAP_W / 2 + 1)
#define HB_T (MAP_H / 2 - 1)
#define HB_B (MAP_H / 2 + 1)
#define BOSS_ENTERED 32
#define BOSS_KEY 64
static void do_magic(int c) { set_terrain(c, T_EMPTY_SP); if (seen(c)) ui_fx(FX_OPERATE, c, -1, NULL); }   // flames as the walls fall
static void press(int cell, int who) {                 // HallsBossLevel.press: off the stairs, the walls fall and Yog wakes
  if (B->press) B->press(cell, who);
  if (G.depth != 25 || who != -1 || (G.boss & BOSS_ENTERED) || cell == G.entrance) return;
  G.boss |= BOSS_ENTERED;
  for (int x = HB_L - 1; x <= HB_R + 1; x++) { do_magic((HB_T - 1) * MAP_W + x); do_magic((HB_B + 1) * MAP_W + x); }
  for (int y = HB_T; y < HB_B + 1; y++) { do_magic(y * MAP_W + HB_L - 1); do_magic(y * MAP_W + HB_R + 1); }
  do_magic(G.entrance);                                // stairs = entrance; entrance = -1 (G.entrance keeps the cell)
  update_fov();
  int pos = -1;
  for (int n = 0; n < 20000 && pos < 0; n++) { int c = rnd_int(MAP_LEN); if (PASSABLE(G.map[c]) && !seen(c) && !char_at(c)) pos = c; }
  if (pos < 0) return;
  int k = spawn_mob(MT_YOG, pos, ST_PASSIVE, 0);
  if (k >= 0) spawn_fists(k);
}

// ---------------- the hooks ----------------
static int closer(int i, int target) {                 // Mob.getCloser overridden
  if (M_(i)->type == MT_SUCCUBUS) return succubus_closer(i, target);
  return target;                                       // (the common hook: the plain step)
}
static bool act(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_SCORPIO: case MT_ACIDIC: if (scorpio_act(i)) return true; break;
    case MT_ROTTING_FIST:                              // RottingFist.act: it mends in the lava
      if ((TFLAGS(G.map[m->pos]) & F_LIQUID) && m->HP < m->HT) m->HP += 4;
      break;
    case MT_BURNING_FIST:                              // BurningFist.act: fire all round
      for (int k = -1; k < 8; k++) { int c = k < 0 ? m->pos : m->pos + N8[k]; if (k < 0 || nb_ok(m->pos, c)) blob_seed(BLOB_FIRE, c, 2); }
      break;
  }
  return B->act ? B->act(i) : false;
}
static int can_attack(int i, int enemy, int epos) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_EYE: { int16_t tr[BOLT_MAX + 1]; return eye_ray(i, epos, tr) ? 1 : 0; }
    case MT_SCORPIO: case MT_ACIDIC: {
      if (adjacent(m->pos, epos)) return 0;
      int16_t tr[BOLT_MAX + 1]; int n = ballistica_cast(m->pos, epos, false, true, tr);
      return n > 0 && tr[n - 1] == epos ? 1 : 0;
    }
    case MT_BURNING_FIST: {
      int16_t tr[BOLT_MAX + 1]; int n = ballistica_cast(m->pos, epos, false, true, tr);
      return n > 0 && tr[n - 1] == epos ? 1 : 0;
    }
  }
  return B->can_attack ? B->can_attack(i, enemy, epos) : -1;
}
static bool do_attack(int i, int enemy) {
  switch (M_(i)->type) {
    case MT_EYE: return eye_attack(i);
    case MT_SCORPIO: case MT_ACIDIC: return scorpio_attack(i, enemy);
    case MT_BURNING_FIST: if (burning_fist_attack(i, enemy)) return true; break;
  }
  return B->do_attack ? B->do_attack(i, enemy) : false;
}
static int attack_proc(int i, int def, int dmg) {
  switch (M_(i)->type) {
    case MT_SUCCUBUS: return succubus_attack_proc(i, def, dmg);
    case MT_SCORPIO: case MT_ACIDIC: {                 // Scorpio.attackProc: crippling spikes
      if (rnd_int(2) == 0) { Buffs *b = buffs_of(def); if (def < 0 && !b->cripple) ui_log("You are crippled!", C_WARN); if (b->cripple < 10) b->cripple = 10; }   // Hero.add
      return dmg;
    }
    case MT_ROTTING_FIST:                              // RottingFist.attackProc: ooze
      if (rnd_int(3) == 0) affect_ooze(def);
      return dmg;
  }
  return B->attack_proc ? B->attack_proc(i, def, dmg) : dmg;
}
static int defense_proc(int i, int att, int dmg) {
  switch (M_(i)->type) {
    case MT_YOG: return yog_defense(i, att, dmg);
    case MT_ACIDIC: {                                  // Acidic.defenseProc: the acid splashes back
      int d = rnd_range(0, dmg);
      if (d > 0 && alive(att)) {
        if (att < 0) { hero_damage(d, "Killed by an acidic scorpio on level %d"); if (G.hero.HP <= 0) ui_log("You were killed by acid...", C_BAD); }
        else mob_damage(att, d);
      }
      return dmg;
    }
  }
  return B->defense_proc ? B->defense_proc(i, att, dmg) : dmg;
}
static int damage(int i, int dmg, int src) {
  if (M_(i)->type == MT_YOG) {                         // Yog.damage: the fists come to help and soak most of it
    int f = fists();
    if (f > 0) {
      for (int k = 0; k < G.n_mobs; k++) if (M_(k)->HP > 0 && (M_(k)->type == MT_ROTTING_FIST || M_(k)->type == MT_BURNING_FIST)) beckon(k, M_(i)->pos);
      dmg >>= f;
    }
  }
  return B->damage ? B->damage(i, dmg, src) : dmg;
}
static void die(int i) {
  Mob *m = M_(i);
  if (m->type == MT_YOG) {                             // Yog.die: the fists go too; the key; the stairs come back
    for (int k = 0; k < G.n_mobs; k++) if (M_(k)->HP > 0 && (M_(k)->type == MT_ROTTING_FIST || M_(k)->type == MT_BURNING_FIST)) mob_kill(k);
    if (G.hero.HP > 0) ui_fx(FX_BOSS_SLAIN, -1, -1, NULL);   // GameScene.bossSlain: banner and sound
    Item key = item_make(IK_SKELETON_KEY, 1); key.lvl = G.depth; heap_put(m->pos, key);
    if (!(G.boss & BOSS_KEY)) { G.boss |= BOSS_KEY; set_terrain(G.entrance, T_ENTRANCE); }   // HallsBossLevel.drop(SkeletonKey)
    yell(i, "...");
    update_fov();
  }
  if (B->die) B->die(i);
}
static Item loot(int i) {
  if (M_(i)->type == MT_SCORPIO || M_(i)->type == MT_ACIDIC) return (Item){ 0 };   // Scorpio.dropLoot is in die below
  return B->loot ? B->loot(i) : (Item){ 0 };
}
static void die_loot(int i) {                          // Scorpio.dropLoot: 1/8 healing, else 1/6 mystery meat
  Mob *m = M_(i);
  if (G.hero.lvl > MOB_DEFS[m->type].max_lvl + 2) return;
  if (rnd_int(8) == 0) level_drop(m->pos, item_make(IK_POT_HEALING, 1));
  else if (rnd_int(6) == 0) level_drop(m->pos, item_make(IK_MYSTERY_MEAT, 1));
}
static void die_all(int i) {
  if (M_(i)->type == MT_SCORPIO || M_(i)->type == MT_ACIDIC) die_loot(i);
  die(i);
}
static void spawned(int i) {
  switch (M_(i)->type) {
    case MT_YOG: M_(i)->state = ST_PASSIVE; break;
    case MT_ROTTING_FIST: case MT_BURNING_FIST: M_(i)->state = ST_WANDERING; break;
    case MT_LARVA: M_(i)->state = ST_HUNTING; break;
  }
  if (B->spawned) B->spawned(i);
}
static void notice(int i) {
  if (M_(i)->type == MT_YOG) { yell(i, "Hope is an illusion..."); return; }
  if (B->notice) B->notice(i);
}
static void install(MobHooks *h, const MobHooks *base) {
  B = base;
  h->act = act; h->can_attack = can_attack; h->do_attack = do_attack; h->attack_proc = attack_proc;
  h->defense_proc = defense_proc; h->damage = damage; h->die = die_all; h->loot = loot;
  h->spawned = spawned; h->notice = notice; h->press = press; h->closer = closer;
}
OVL_ENTRY(halls, (void *)install);
