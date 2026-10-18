/*
 * Pixel Dungeon for Pebble - the dwarven city's creatures (depths 16-20), a band overlay (0.9).
 * Fire elementals, dwarf warlocks, monks and seniors, golems, succubi from below, the ambitious imp and his
 * quest, the King of Dwarves with his undead army, and the CityBossLevel hall.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Elemental, Warlock, Monk, Senior, Golem, Imp, WndImp, King, Undead,
 * CityBossLevel, ImpShopkeeper.
 */
#include "mobovl.h"
#include "mob_elemental.inc"
#include "mob_monk.inc"
#include "mob_succubus.inc"

static const MobHooks *B;

// ---------------- Dwarf warlock: Warlock.zap (a shadow bolt) ----------------
static bool warlock_zap(int i, int enemy) {
  Mob *m = M_(i);
  int epos = pos_of(enemy);
  if (seen(m->pos) || seen(epos)) ui_zap(12, m->pos, epos, NULL, 0);   // MagicMissile.shadow
  mob_spend(m, TICK);                                  // TIME_TO_ZAP
  if (char_hit(i, enemy, true)) {
    if (enemy < 0 && rnd_int(2) == 0) {                // Weakness (Belongings.discharge on the first touch)
      if (!G.hero.weakness) {
        for (int s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1)   // Belongings.discharge: the hand too
          if (item_cat(game_slot(s)->kind) == CAT_WAND && game_slot(s)->ench > 0) game_slot(s)->ench--;
        ui_log("You feel weakened!", C_WARN);           // Hero.add(Weakness)
      }
      int d = elem_duration(-1, 40); if (d > G.hero.weakness) G.hero.weakness = d;
    }
    int dmg = rnd_int2(12, 18);
    if (enemy < 0) {
      hero_damage(elem_resist(-1, dmg), "Killed by a dwarf warlock on level %d");
      if (G.hero.HP <= 0) ui_log("Dwarf warlock's shadow bolt killed you...", C_BAD);
    } else mob_damage(enemy, dmg);
  } else if (seen(epos)) ui_fx(FX_TEXT, epos, enemy, enemy < 0 ? "dodged" : "evaded");
  return true;
}

// ---------------- the ambitious imp (Imp.Quest) ----------------
static const char *const IMP_GOLEMS1 = "Are you an adventurer? I love adventurers! You can always rely on them if something needs to be killed. Am I right? For a bounty, of course ;)\nIn my case this is golems who need to be killed. You see, I'm going to start a little business here, but these stupid golems are bad for business! It's very hard to negotiate with wandering lumps of granite, damn them! So please, kill... let's say 6 of them and a reward is yours.";
static const char *const IMP_MONKS1 = "Are you an adventurer? I love adventurers! You can always rely on them if something needs to be killed. Am I right? For a bounty, of course ;)\nIn my case this is monks who need to be killed. You see, I'm going to start a little business here, but these lunatics don't buy anything themselves and will scare away other customers. So please, kill... let's say 8 of them and a reward is yours.";
#define s_wnd g_trade_arg                              // the reward window is open (resident: this overlay may be dropped meanwhile)
enum { WND_IMP = 1 };
static void imp_process(int i) {                       // Imp.Quest.process: a token from each monk (or golem) slain
  Quests *q = &G.q;
  if (q->imp != QS_GIVEN) return;
  int t = M_(i)->type;
  if ((q->imp_alt && (t == MT_MONK || t == MT_SENIOR)) || (!q->imp_alt && t == MT_GOLEM)) heap_put(M_(i)->pos, item_make(IK_DWARF_TOKEN, 1));
}
static ActResult imp_interact(int i) {
  Quests *q = &G.q;
  if (q->imp == QS_GIVEN) {
    int s = game_bag_find(IK_DWARF_TOKEN), n = s >= 0 ? G.hero.bag[s].qty : 0;
    if (n >= 8 || (!q->imp_alt && n >= 6)) {
      s_wnd = WND_IMP;
      ui_wnd(i, "Oh yes! You are my hero!\nRegarding your reward, I don't have cash with me right now, but I have something better for you. This is my family heirloom ring: my granddad took it off a dead paladin's finger.", "Take the ring", NULL);
    } else ui_wnd(i, q->imp_alt ? "Oh, you are still alive! I knew that your kung-fu is stronger ;) Just don't forget to grab these monks' tokens." : "How is your golem safari going?", NULL, NULL);
  } else {
    ui_wnd(i, q->imp_alt ? IMP_MONKS1 : IMP_GOLEMS1, NULL, NULL);
    q->imp = QS_GIVEN;
  }
  return ACT_NONE;
}
static void wnd_choice(int b) {
  if (s_wnd != WND_IMP) { if (B->wnd_choice) B->wnd_choice(b); return; }
  s_wnd = 0;
  if (b != 0) return;                                  // closed without taking it: ask again later
  int i = find_mob(MT_IMP);
  Quests *q = &G.q;
  int s = game_bag_find(IK_DWARF_TOKEN);
  if (s >= 0) G.hero.bag[s] = (Item){ 0 };             // tokens.detachAll
  Item r = q->imp_ring;
  r.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(r.kind);   // reward.identify()
  char n[40]; short_name(&r, n, sizeof n);   // WndSadGhost / WndWandmaker / WndImp: reward.name()
  if (bag_add(r)) LOG(C_INFO, "You now have %s", n);
  else { LOG(C_WARN, "Your pack is too full for the %s", n); heap_put(i >= 0 ? M_(i)->pos : G.hero.pos, r); }   // Level.drop(reward, imp.pos)
  if (i >= 0) {                                        // Imp.flee
    char t[32]; snprintf(t, sizeof t, "See you, %s!", hero_class_name()); yell(i, t);
    M_(i)->HP = 0; ui_fx(FX_DIE, M_(i)->pos, i, NULL);
  }
  q->imp = QS_REWARDED; q->imp_ring = (Item){ 0 };
}

// ---------------- the King of Dwarves (King, King.Undead, CityBossLevel) ----------------
#define CB_TOP 2
#define CB_HW 7
#define CB_HH 15
#define CB_LEFT ((MAP_W - CB_HW) / 2)
#define CB_CENTER (CB_LEFT + CB_HW / 2)
#define MAX_ARMY 5
static int pedestal(int left) { return (CB_TOP + CB_HH / 2) * MAP_W + CB_CENTER + (left ? -2 : 2); }
static int undead_count(void) { int n = 0; for (int i = 0; i < G.n_mobs; i++) if (M_(i)->HP > 0 && M_(i)->type == MT_UNDEAD) n++; return n; }
static int max_army(int i) { return 1 + MAX_ARMY * (M_(i)->HT - M_(i)->HP) / M_(i)->HT; }
static bool can_summon(int i) {                        // King.canTryToSummon (aux2: 0 = the left pedestal next)
  if (undead_count() >= max_army(i)) return false;
  int p = pedestal(!M_(i)->aux2), c = mob_at(p);
  return (c < 0 || c == i) && !(p == G.hero.pos && G.hero.HP > 0);
}
static void king_summon(int i) {                       // King.summon: undead rise round him, nearest cells first
  Mob *m = M_(i);
  m->aux2 ^= 1;
  ui_sound(SND_CHALLENGE);
  int want = max_army(i) - undead_count();
  for (int d = 1; d <= 4 && want > 0; d++)
    for (int c = 0; c < MAP_LEN && want > 0; c++) {
      if (distance(c, m->pos) != d || !PASSABLE(G.map[c]) || char_at(c)) continue;
      int k = spawn_mob(MT_UNDEAD, c, ST_WANDERING, 0);
      if (k < 0) { want = 0; break; }
      ui_fx(FX_SPAWN, c, k, NULL);
      want--;
    }
  yell(i, "Arise, slaves!");
}
#define BOSS_ENTERED 16
static void press(int cell, int who) {                 // CityBossLevel.press: past the door, the King appears and it locks
  if (B->press) B->press(cell, who);
  int door = (CB_TOP + CB_HH) * MAP_W + CB_CENTER;
  if (G.depth != 20 || who != -1 || (G.boss & BOSS_ENTERED) || cell / MAP_W >= door / MAP_W) return;
  G.boss |= BOSS_ENTERED;
  int pos = -1, count = 0;
  for (int n = 0; n < 4000 && pos < 0; n++) {
    int c = rnd_int(MAP_LEN);
    if (!PASSABLE(G.map[c]) || c / MAP_W >= door / MAP_W || char_at(c)) continue;
    if (seen(c) && count++ < 20) continue;
    pos = c;
  }
  if (pos >= 0) { int k = spawn_mob(MT_KING, pos, ST_HUNTING, 0); if (k >= 0) { M_(k)->flags |= MF_SEEN; if (seen(pos)) mob_notice(k); } }
  set_terrain(door, T_LOCKED_DOOR);
  update_fov();
}

// ---------------- the hooks ----------------
static int closer(int i, int target) {                 // Mob.getCloser overridden
  switch (M_(i)->type) {
    case MT_SUCCUBUS: return succubus_closer(i, target);
    case MT_KING: return can_summon(i) ? pedestal(!M_(i)->aux2) : target;   // King.getCloser: to the next pedestal while an army can be raised
  }
  return target;                                       // (the common hook: the plain step)
}
static bool act(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_IMP:                                       // Imp.act: "Psst" when first seen, before the quest
      if (G.q.imp != QS_GIVEN && G.q.imp != QS_REWARDED && seen(m->pos)) { if (!m->aux2) { char t[32]; snprintf(t, sizeof t, "Psst, %s!", hero_class_name()); yell(i, t); } m->aux2 = 1; }
      else m->aux2 = 0;
      throw_item(i);
      break;
  }
  return B->act ? B->act(i) : false;
}
static int can_attack(int i, int enemy, int epos) {
  if (M_(i)->type == MT_KING && can_summon(i)) return M_(i)->pos == pedestal(!M_(i)->aux2);   // King.canAttack: on the pedestal it summons
  if (M_(i)->type == MT_WARLOCK) {
    int16_t tr[BOLT_MAX + 1];
    int n = ballistica_cast(M_(i)->pos, epos, false, true, tr);
    return n > 0 && tr[n - 1] == epos;                // (no charm check: a charmed one zaps all the same)
  }
  return B->can_attack ? B->can_attack(i, enemy, epos) : -1;
}
static bool do_attack(int i, int enemy) {
  Mob *m = M_(i);
  if (m->type == MT_WARLOCK && !adjacent(m->pos, pos_of(enemy))) return warlock_zap(i, enemy);
  if (m->type == MT_KING && can_summon(i) && m->pos == pedestal(!m->aux2)) { king_summon(i); mob_spend(m, MOB_DEFS[m->type].atk_delay); return true; }   // King.attack
  if (m->type == MT_KING && pos_of(enemy) == pedestal(!m->aux2)) m->aux2 ^= 1;   // King.attack: you stand on his pedestal
  return B->do_attack ? B->do_attack(i, enemy) : false;
}
static int attack_proc(int i, int def, int dmg) {
  switch (M_(i)->type) {
    case MT_ELEMENTAL: return elemental_attack_proc(def, dmg);
    case MT_MONK: case MT_SENIOR: return monk_attack_proc(i, def, dmg);
    case MT_SUCCUBUS: return succubus_attack_proc(i, def, dmg);
    case MT_UNDEAD: if (rnd_int(MAX_ARMY) == 0) affect_paralysis(def, 1); return dmg;   // Undead.attackProc
  }
  return B->attack_proc ? B->attack_proc(i, def, dmg) : dmg;
}
static int damage(int i, int dmg, int src) {
  // (0.93 audit: Undead.damage's ToxicGas.clear lands on the frame the swap throws away: the cloud is untouched)
  return B->damage ? B->damage(i, dmg, src) : dmg;
}
static void die(int i) {
  Mob *m = M_(i);
  switch (m->type) {
    case MT_MONK: case MT_SENIOR: case MT_GOLEM: imp_process(i); break;
    case MT_UNDEAD: if (seen(m->pos)) ui_sound(SND_BONES); break;
    case MT_KING: {                                    // King.die: the armor kit and the key; the hall opens
      heap_put(m->pos, item_make(IK_ARMOR_KIT, 1));
      Item k = item_make(IK_SKELETON_KEY, 1); k.lvl = G.depth; heap_put(m->pos, k);
      set_terrain((CB_TOP + CB_HH) * MAP_W + CB_CENTER, T_DOOR);   // CityBossLevel.drop(SkeletonKey)
      if (G.hero.HP > 0) ui_fx(FX_BOSS_SLAIN, -1, -1, NULL);   // GameScene.bossSlain: banner and sound
      char t[64]; snprintf(t, sizeof t, "You cannot kill me, %s... I am... immortal...", hero_class_title()); yell(i, t);
      update_fov();
      break;
    }
  }
  if (B->die) B->die(i);
}
static void spawned(int i) {
  switch (M_(i)->type) {
    case MT_IMP: M_(i)->state = ST_PASSIVE; break;
    case MT_UNDEAD: M_(i)->state = ST_WANDERING; break;
  }
  if (B->spawned) B->spawned(i);
}
static void notice(int i) {
  if (M_(i)->type == MT_KING) { yell(i, "How dare you!"); return; }
  if (B->notice) B->notice(i);
}
static ActResult interact(int i) {
  if (M_(i)->type == MT_IMP) return imp_interact(i);
  return B->interact ? B->interact(i) : ACT_NONE;
}
static void install(MobHooks *h, const MobHooks *base) {
  B = base;
  h->act = act; h->can_attack = can_attack; h->do_attack = do_attack; h->attack_proc = attack_proc;
  h->damage = damage; h->die = die; h->spawned = spawned; h->notice = notice; h->interact = interact; h->wnd_choice = wnd_choice;
  h->press = press; h->closer = closer;
}
OVL_ENTRY(city, (void *)install);
