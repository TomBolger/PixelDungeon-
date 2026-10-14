/*
 * Pixel Dungeon for Pebble - the sewers' creatures (depths 1-5), a band overlay (0.9).
 * Sad Ghost and his quest (FetidRat, DriedRose, CursePersonification), Goo, albino rats, swarms, the rat king,
 * and the skeletons and thieves of depth 4. Ported from Pixel Dungeon 1.9.1 (GPLv3).
 */
#include "mobovl.h"
#include "mob_thief.inc"
#include "mob_skeleton.inc"
#include "mob_swarm.inc"

static const MobHooks *B;
// ---------------- the Sad Ghost's quest (Ghost.Quest) ----------------
static void ghost_process_kill(int pos) {             // Ghost.Quest.processSewersKill: rats, gnolls and crabs
  Quests *q = &G.q;
  if (q->ghost != QS_GIVEN || q->ghost_depth != G.depth) return;
  if (q->ghost_alt == 0) {                             // ROSE: one of the kills drops it, ever likelier
    if (rnd_int(q->ghost_left ? q->ghost_left : 1) == 0) { heap_put(pos, item_make(IK_ROSE, 1)); q->ghost = QS_DONE; }
    else if (q->ghost_left) q->ghost_left--;
  } else if (q->ghost_alt == 1) {                      // RAT: the fetid rat appears somewhere
    int c = random_respawn_cell();
    if (c >= 0) { spawn_mob(MT_FETID_RAT, c, ST_WANDERING, 0); q->ghost = QS_DONE; }
  }
}
static int ghost_mob(void) { return find_mob(MT_GHOST); }
static void ghost_relocate(int g) {                    // QuestHandler.relocate
  int c = -1;
  for (int n = 0; n < 10 && c < 0; n++) c = random_respawn_cell();
  if (c >= 0) { ui_fx(FX_TELEPORT, M_(g)->pos, g, NULL); M_(g)->pos = c; }
}
#define s_wnd g_trade_arg                              // which window's buttons come back to wnd_choice (resident: this overlay may be dropped while it is open)
enum { WND_GHOST_REWARD = 1, WND_GHOST_CURSE };
static const char *const ROSE1 = "Hello adventurer... Once I was like you - strong and confident... And now I'm dead... But I can't leave this place... Not until I have my dried rose... It's very important to me... Some monster stole it from my body...";
static const char *const RAT1 = "Hello adventurer... Once I was like you - strong and confident... And now I'm dead... But I can't leave this place... Not until I have my revenge... Slay the fetid rat, that has taken my life...";
static const char *const CURSE1 = "Hello adventurer... Once I was like you - strong and confident... And now I'm dead... But I can't leave this place, as I am bound by a horrid curse... Please... Help me... Destroy the curse...";
static ActResult ghost_interact(int g) {
  Quests *q = &G.q;
  ui_sound(SND_GHOST);
  G.journal[J_GHOST] |= 1u << G.depth;                 // Journal.add(GHOST): on his first words (declined curse too)
  if (q->ghost_alt == 2) {                             // CURSE
    if (q->ghost == QS_DONE) { s_wnd = WND_GHOST_REWARD; char t[180]; snprintf(t, sizeof t, "Thank you, %s! The curse is broken and I can finally rest... Please take one of these items, maybe they will be useful to you in your journey...", hero_class_name()); ui_wnd(g, t, "Ghost's weapon", "Ghost's armor"); }
    else { s_wnd = WND_GHOST_CURSE; ui_wnd(g, CURSE1, "Yes, I will do it for you", "No, I can't help you"); }
    return ACT_NONE;
  }
  if (q->ghost == QS_SPAWNED) { ui_wnd(g, q->ghost_alt ? RAT1 : ROSE1, NULL, NULL); q->ghost = QS_GIVEN; return ACT_NONE; }
  int need = q->ghost_alt ? IK_RAT_SKULL : IK_ROSE;
  if (game_bag_find(need) >= 0) {
    s_wnd = WND_GHOST_REWARD;
    ui_wnd(g, q->ghost_alt ? "Yes! The ugly creature is slain and I can finally rest... Please take one of these items, maybe they will be useful to you in your journey..."
                           : "Yes! Yes!!! This is it! Please give it to me! And you can take one of these items, maybe they will be useful to you in your journey...",
           "Ghost's weapon", "Ghost's armor");
  } else {
    ui_wnd(g, q->ghost_alt ? "Please... Help me... Slay the abomination..." : "Please... Help me... Find the rose...", NULL, NULL);
    ghost_relocate(g);
  }
  return ACT_NONE;
}
static void wnd_choice(int b) {
  if (!s_wnd) { if (B->wnd_choice) B->wnd_choice(b); return; }
  int g = ghost_mob();
  Quests *q = &G.q;
  if (s_wnd == WND_GHOST_REWARD && g >= 0) {           // WndSadGhost.selectReward
    int need = q->ghost_alt == 0 ? IK_ROSE : q->ghost_alt == 1 ? IK_RAT_SKULL : 0;
    if (need) { int s = game_bag_find(need); if (s >= 0) G.hero.bag[s] = (Item){ 0 }; }
    Item reward = b == 0 ? q->ghost_weapon : q->ghost_armor;
    char n[40]; short_name(&reward, n, sizeof n);   // WndSadGhost / WndWandmaker / WndImp: reward.name()
    if (bag_add(reward)) LOG(C_INFO, "You now have %s", n);
    else { LOG(C_WARN, "Your pack is too full for the %s", n); heap_put(M_(g)->pos, reward); }   // Item.collect, Level.drop
    yell(g, "Farewell, adventurer!");
    M_(g)->HP = 0; ui_fx(FX_DIE, M_(g)->pos, g, NULL);
    q->ghost = QS_REWARDED; G.journal[J_GHOST] &= ~(1u << G.depth);   // Quest.complete: Journal.remove(GHOST)
  } else if (s_wnd == WND_GHOST_CURSE && g >= 0) {
    if (b == 0) {                                      // Ghost.replace(ghost, new CursePersonification())
      q->ghost = QS_GIVEN;
      Mob *m = M_(g);
      int c = m->pos; m->HP = 0;
      int k = spawn_mob(MT_WRAITH, c, ST_HUNTING, 0);  // the curse's body (CursePersonification), see spawned()
      if (k >= 0) { M_(k)->aux2 = 1; M_(k)->HT = M_(k)->HP = 10 + G.depth * 3; ui_fx(FX_SPAWN, c, k, NULL); }
      ui_sound(SND_GHOST);
    } else ghost_relocate(g);
  }
  s_wnd = 0;
}


// ---------------- the hooks ----------------
static bool act(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_GOO:                                       // Goo.act: heals in water
      if (IS_WATER(G.map[m->pos]) && m->HP < m->HT) { m->HP++; if (seen(m->pos)) ui_fx(FX_HEAL, m->pos, i, NULL); }
      return false;
    case MT_WRAITH:
      if (m->aux2 == 1 && m->HP > 0 && m->HP < m->HT) m->HP++;   // CursePersonification.act: mends itself
      return false;
    case MT_THIEF: case MT_BANDIT: return thief_act(i);
  }
  return B->act ? B->act(i) : false;
}
static int can_attack(int i, int enemy, int epos) {
  Mob *m = M_(i);
  if (m->type == MT_GOO && m->aux2) return distance(m->pos, epos) <= 2 ? 1 : 0;   // pumped up: reaches 2 cells
  return B->can_attack ? B->can_attack(i, enemy, epos) : -1;
}
static bool do_attack(int i, int enemy) {
  Mob *m = M_(i);
  if (m->type != MT_GOO) return B->do_attack ? B->do_attack(i, enemy) : false;
  int epos = pos_of(enemy);                            // Goo.doAttack
  if (m->aux2) {                                       // pumped up
    if (!adjacent(m->pos, epos)) {                     // jump next to it first
      int16_t tr[BOLT_MAX + 1];
      int n = ballistica_cast(m->pos, epos, false, true, tr);
      if (n >= 2 && tr[n - 1] == epos) { m->aux = 1; mob_move(i, tr[n - 2]); }   // jumped (Goo.move: still pumped up)
      else { m->aux2 = 0; return true; }              // blocked: no longer pumped up, at no cost
    } else m->aux = 0;
    attack(i, enemy, NULL);
    m->aux2 = 0; m->aux = 0;                           // Goo.attack: pumpedUp = false
    mob_spend(m, TICK);
    return true;
  }
  if (rnd_int(3) > 0) { attack(i, enemy, NULL); m->aux2 = 0; mob_spend(m, TICK); return true; }
  m->aux2 = 1;                                         // pump up
  g_goo_warn = 1u << 12;                               // (its own cell: the spray; Shattered's GooSprite warning: every cell its jump can reach)
  for (int k = 0; k < 25; k++) { int x = cx(m->pos) + k % 5 - 2, y = cy(m->pos) + k / 5 - 2, c = x + y * MAP_W; bool hit;
    if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H && ballistica(m->pos, c, &hit) == c) g_goo_warn |= 1u << k; }
  mob_spend(m, 2 * TICK);
  if (seen(m->pos)) { ui_fx(FX_TEXT, m->pos, i, "!!!"); ui_log("Goo is pumping itself up!", C_BAD); }
  return true;
}
static int stats(int i, int w) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_GOO:
      if (w == STAT_DMG) return m->aux2 ? rnd_normal(5, 30) : rnd_normal(2, 12);
      if (w == STAT_ATT) return m->aux2 && !m->aux ? 30 : 15;
      break;
    case MT_WRAITH:
      if (m->aux2 != 1) break;                         // CursePersonification: 10 + depth skills
      if (w == STAT_ATT || w == STAT_DEF) return 10 + G.depth;
      if (w == STAT_DMG) return rnd_normal(3, 5);
      if (w == STAT_DR) return 1;
      break;
    case MT_SWARM: if (w == STAT_ATT) return 12; break;
  }
  return B->stats ? B->stats(i, w) : -1;
}
static int attack_proc(int i, int def, int dmg) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_ALBINO: if (rnd_int(2) == 0) {             // Buff.affect(Bleeding).set(damage): replaces the level; Hero.add logs a new one
      Buffs *b = buffs_of(def);
      if (def < 0 && !b->bleed) ui_log("You are bleeding!", C_WARN);
      b->bleed = dmg > 255 ? 255 : dmg;
    } return dmg;
    case MT_GOO: if (rnd_int(3) == 0) affect_ooze(def); return dmg;
    case MT_THIEF: case MT_BANDIT: return thief_attack_proc(i, def, dmg);
    case MT_WRAITH:
      if (m->aux2 == 1) {                              // CursePersonification.attackProc: shoves the target back
        int dpos = pos_of(def), np = dpos + (dpos - m->pos);
        if (adjacent(m->pos, dpos) && nb_ok(dpos, np) && (PASSABLE(G.map[np]) || (TFLAGS(G.map[np]) & F_AVOID)) && !char_at(np)) {
          if (def < 0) { G.hero.pos = np; hero_press(np); update_fov(); } else { M_(def)->pos = np; mob_press(def); }
        }
        return dmg;
      }
      break;
  }
  return B->attack_proc ? B->attack_proc(i, def, dmg) : dmg;
}
static int defense_proc(int i, int att, int dmg) {
  switch (M_(i)->type) {
    case MT_SWARM: swarm_defense_proc(i, dmg); return dmg;
    case MT_FETID_RAT: blob_seed(BLOB_PARALYTIC, M_(i)->pos, 20); return dmg;   // its foul cloud
    case MT_THIEF: case MT_BANDIT: thief_defense_proc(i); return dmg;
  }
  return B->defense_proc ? B->defense_proc(i, att, dmg) : dmg;
}
static void die(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_RAT: case MT_ALBINO: case MT_GNOLL: case MT_CRAB: ghost_process_kill(m->pos); break;
    case MT_SWARM: swarm_die(i); break;
    case MT_FETID_RAT: heap_put(m->pos, item_make(IK_RAT_SKULL, 1)); break;
    case MT_SKELETON: skeleton_die(i); break;
    case MT_GOO:                                       // Goo.die: the way down opens; the key is left behind
      if (G.boss & 1) { G.boss &= ~1; set_terrain(G.entrance, T_ENTRANCE); }   // SewerBossLevel.unseal
      { Item k = item_make(IK_SKELETON_KEY, 1); k.lvl = G.depth; heap_put(m->pos, k); }
      if (G.hero.HP > 0) ui_fx(FX_BOSS_SLAIN, -1, -1, NULL);   // GameScene.bossSlain: banner and sound
      yell(i, "glurp... glurp...");
      break;
    case MT_WRAITH:
      if (m->aux2 == 1) {                              // CursePersonification.die: the ghost is free again
        G.q.ghost = QS_DONE;
        if (G.hero.HP > 0 && G.hero.lvl <= 5) earn_exp(3);   // CursePersonification: EXP 3, maxLvl 5
        int g = spawn_mob(MT_GHOST, m->pos, ST_WANDERING, 0);
        if (g >= 0) ui_fx(FX_SPAWN, m->pos, g, NULL);
      }
      break;
  }
  if (B->die) B->die(i);                               // carried items, sacrifice
}
static void moved(int i) {
  Mob *m = M_(i);
  if (m->type == MT_GOO && !(G.boss & 1) && G.map[G.entrance] == T_ENTRANCE) {   // Goo.move -> SewerBossLevel.seal
    G.boss |= 1; set_terrain(G.entrance, T_WATER_TILES);
  }
  if (m->type == MT_GOO && !m->aux) m->aux2 = 0;       // Goo.getCloser: pumpedUp = false (not the jump)
  if (B->moved) B->moved(i);
}
static void notice(int i) {
  if (M_(i)->type == MT_GOO) { yell(i, "GLURP-GLURP!"); return; }
  if (B->notice) B->notice(i);
}
static ActResult interact(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_GHOST: return ghost_interact(i);
    case MT_RAT_KING:
      if (m->state == ST_SLEEPING) { mob_notice(i); yell(i, "I'm not sleeping!"); m->state = ST_WANDERING; }
      else yell(i, "What is it? I have no time for this nonsense. My kingdom won't rule itself!");
      return ACT_NONE;
  }
  return B->interact ? B->interact(i) : ACT_NONE;
}
static void install(MobHooks *h, const MobHooks *base) {
  B = base;
  h->act = act; h->can_attack = can_attack; h->do_attack = do_attack; h->stats = stats;
  h->attack_proc = attack_proc; h->defense_proc = defense_proc; h->die = die; h->moved = moved;
  h->notice = notice; h->interact = interact; h->wnd_choice = wnd_choice;
}
OVL_ENTRY(sewers, (void *)install);
