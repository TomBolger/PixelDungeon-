/*
 * Pixel Dungeon for Pebble - the caves' creatures (depths 11-15), a band overlay (0.9).
 * Vampire bats, gnoll brutes, cave spinners and their webs, shamans, fire elementals and monks from below,
 * the troll blacksmith and his quest, DM-300 and the CavesBossLevel arena.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Bat, Brute, Shielded, Spinner, Web, Blacksmith, WndBlacksmith,
 * Pickaxe, DM300, CavesBossLevel.
 */
#include "mobovl.h"
#include "mob_shaman.inc"
#include "mob_bat.inc"
#include "mob_brute.inc"
#include "mob_elemental.inc"
#include "mob_monk.inc"

static const MobHooks *B;

// ---------------- Cave spinner ----------------
static bool spinner_act(int i) {                       // Spinner.act: back to the hunt once its prey is poisoned no more
  Mob *m = M_(i);
  int p = m->pos;
  if (m->state != ST_FLEEING || mob_paralysed(m)) return false;
  mob_act_default(i);                                  // Mob.Fleeing.act (after it: Spinner.act's check, nowhereToRun)
  if (m->state == ST_FLEEING && !m->b.terror && m->HP > 0 && (m->pos == p || ((m->flags & MF_SEEN) && !buffs_of(mob_enemy(i))->poison)))
    m->state = ST_HUNTING;
  return true;
}
static int spinner_attack_proc(int i, int def, int dmg) {
  if (rnd_int(2) == 0) { affect_poison(def, rnd_int2(7, 9)); M_(i)->state = ST_FLEEING; }
  return dmg;
}

// ---------------- DM-300 ----------------
static bool dm300_act(int i) {                         // DM300.act: it vents toxic gas as it goes
  blob_seed(BLOB_TOXIC, M_(i)->pos, 30);
  return false;
}
static void dm300_moved(int i) {                       // DM300.move: repairs on old machinery, shakes the cave
  Mob *m = M_(i);
  int step = m->pos;
  if (G.map[step] == T_INACTIVE_TRAP && m->HP < m->HT) {
    m->HP += rnd_int2(1, m->HT - m->HP);
    if (seen(step) && G.hero.HP > 0) { ui_fx(FX_HEAL, step, i, NULL); ui_log("DM-300 repairs itself!", C_BAD); }
  }
  int cell = step + N8[rnd_int(8)];
  if (!nb_ok(step, cell)) return;
  if (seen(cell)) {
    ui_sound(SND_ROCKS);
    ui_fx(FX_TEXT, cell, -1, "*");
    if (G.map[cell] == T_EMPTY) set_terrain(cell, T_EMPTY_DECO);
  }
  int who = cell == G.hero.pos ? -1 : mob_at(cell);    // Actor.findChar: nobody there (-1 from mob_at) is not the hero
  if (who >= 0 ? who != i : cell == G.hero.pos && G.hero.HP > 0) affect_paralysis(who, 2);
}

// ---------------- CavesBossLevel ----------------
#define CB_L (MAP_W / 2 - 2)
#define CB_R (MAP_W / 2 + 2)
#define CB_T (MAP_H / 2 - 2)
#define CB_B (MAP_H / 2 + 2)
#define BOSS_ENTERED 8
static bool outside_entrance_room(int c) { int x = cx(c), y = cy(c); return x < CB_L - 1 || x > CB_R + 1 || y < CB_T - 1 || y > CB_B + 1; }
static void press(int cell, int who) {                 // CavesBossLevel.press: once out of the first room, DM-300 comes and the way back caves in
  if (B->press) B->press(cell, who);
  if (G.depth != 15 || who != -1 || (G.boss & BOSS_ENTERED) || !outside_entrance_room(cell)) return;
  G.boss |= BOSS_ENTERED;
  int pos = -1;
  for (int n = 0; n < 2000 && pos < 0; n++) { int c = rnd_int(MAP_LEN); if (PASSABLE(G.map[c]) && outside_entrance_room(c) && !seen(c) && !char_at(c)) pos = c; }
  if (pos >= 0) { int k = spawn_mob(MT_DM300, pos, ST_HUNTING, 0); if (k >= 0) M_(k)->flags |= MF_SEEN; }
  int door = G.rooms[0].cell;
  if (door >= 0) set_terrain(door, T_WALL);
  ui_sound(SND_ROCKS);
  update_fov();
}

// ---------------- the troll blacksmith (Blacksmith.Quest) ----------------
static const char *const SMITH_GOLD = "Hey human! Wanna be useful, eh? Take dis pickaxe and mine me some dark gold ore, 15 pieces should be enough. What do you mean, how am I gonna pay? You greedy...\nOk, ok, I don't have money to pay, but I can do some smithin' for you. Consider yourself lucky, I'm the only blacksmith around.";
static const char *const SMITH_BLOOD = "Hey human! Wanna be useful, eh? Take dis pickaxe and kill a bat wit' it, I need its blood on the head. What do you mean, how am I gonna pay? You greedy...\nOk, ok, I don't have money to pay, but I can do some smithin' for you. Consider yourself lucky, I'm the only blacksmith around.";
// the reforge flow, kept in resident variables (this overlay may be dropped while a window or the selector is open):
// g_trade_arg: WND_SMITH while WndBlacksmith is open, WND_SECOND while the second item is chosen; g_reforge_slot: the first
enum { WND_SMITH = 1, WND_SECOND };
static int pickaxe_slot(void) {                        // in the pack, or wielded (-1); -100: none
  if (G.hero.weapon.kind == IK_PICKAXE) return -1;
  int s = game_bag_find(IK_PICKAXE);
  return s >= 0 ? s : -100;
}
static void smith_tell(int i, const char *t) { ui_wnd(i, t, NULL, NULL); }
static ActResult smith_interact(int i) {
  Quests *q = &G.q;
  if (q->smith == QS_SPAWNED) {                        // the task, and the pickaxe to do it with
    smith_tell(i, q->smith_alt ? SMITH_BLOOD : SMITH_GOLD);
    q->smith = QS_GIVEN;
    Item p = item_make(IK_PICKAXE, 1); p.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
    if (bag_add(p)) ui_log("You now have pickaxe", C_INFO); else heap_put(G.hero.pos, p);
  } else if (q->smith == QS_GIVEN) {
    int s = pickaxe_slot();
    if (s == -100) { smith_tell(i, "Are you kiddin' me? Where is my pickaxe?!"); return ACT_NONE; }
    Item *pk = game_slot(s);
    if (q->smith_alt) {
      if (!(pk->flags & IF_BLOODY)) { smith_tell(i, "I said I need bat blood on the pickaxe. Chop chop!"); return ACT_NONE; }
    } else {
      int g = game_bag_find(IK_DARK_GOLD);
      if (g < 0 || G.hero.bag[g].qty < 15) { smith_tell(i, "Dark gold ore. 15 pieces. Seriously, is it dat hard?"); return ACT_NONE; }
      G.hero.bag[g] = (Item){ 0 };                     // gold.detachAll
    }
    *pk = (Item){ 0 };
    if (s == -1) ui_hero_tier();
    smith_tell(i, "Oh, you have returned... Better late dan never.");
    q->smith = QS_DONE;
  } else if (q->smith == QS_DONE) {                    // WndBlacksmith
    g_trade_arg = WND_SMITH;
    ui_wnd(i, "Ok, a deal is a deal, dat's what I can do for you: I can reforge 2 items and turn them into one of a better quality.", "Reforge 2 items", "Not now");
  } else smith_tell(i, "I'm busy. Get lost!");
  return ACT_NONE;
}
static const char *smith_verify(Item *a, Item *b, int sa, int sb) {   // Blacksmith.verify
  if (sa == sb) return "Select 2 different items, not the same item twice!";
  if (a->kind != b->kind) return "Select 2 items of the same type!";
  if (!item_identified(a) || !item_identified(b)) return "I need to know what I'm working with, identify them first!";
  if ((a->flags & IF_CURSED) || (b->flags & IF_CURSED)) return "I don't work with cursed items!";
  if (a->lvl < 0 || b->lvl < 0) return "It's a junk, the quality is too poor!";
  if (!item_upgradable(a->kind) || !item_upgradable(b->kind)) return "I can't reforge these items!";
  return NULL;
}
static void smith_reforge(int sa, int sb) {            // Blacksmith.upgrade: the better one gains a level, the other is gone
  Item *a = game_slot(sa), *b = game_slot(sb);
  if (b->lvl > a->lvl) { Item *t = a; a = b; b = t; int ts = sa; sa = sb; sb = ts; }
  ui_sound(SND_EVOKE);
  int c = item_cat(a->kind);
  if (a->ench && (c == CAT_WEAPON || c == CAT_ARMOR || c == CAT_MISSILE) && a->lvl > 0 && rnd_int(a->lvl) > 0) {   // Weapon / Armor.upgrade(false)
    a->ench = 0;
    ui_log(c == CAT_ARMOR ? "Interaction of different types of magic has erased the glyph on this armor!"
                          : "Interaction of different types of magic has negated the enchantment on this weapon!", C_WARN);
  }
  a->lvl++; a->dur = item_max_dur(a);
  if (item_cat(a->kind) == CAT_WAND && a->ench < wand_max(a)) a->ench++;
  char n[48]; short_name(a, n, sizeof n);
  int t = 2 * TICK;                                    // Blacksmith.upgrade: spendAndNext(2), and each doUnequip its time2equip
  if (sa < 0) {                                        // the better one comes off first (doUnequip(hero, true)): into the pack, or at your feet
    Item up = *a; *a = (Item){ 0 };
    if (!bag_add(up)) { LOG(C_WARN, "Your pack is too full for the %s", n); heap_put(G.hero.pos, up); }   // Item.collect fails
    t += sa == -2 ? 2 * TICK : TICK;
  }
  LOG(C_GOOD, "your %s certainly looks better now", n);
  if (sb < 0) t += sb == -2 ? 2 * TICK : TICK;         // the other: doUnequip(hero, false), then detachAll
  *b = (Item){ 0 };
  if (sa < 0 || sb < 0) ui_hero_tier();
  G.q.smith = QS_REWARDED;
  hero_spend(t);
  process();
}
static void wnd_choice(int b) {
  int w = find_mob(MT_BLACKSMITH);
  if (b >= 99 && b < 104 + PACK0 + 32) {                            // an item picked for the reforge (99: cancelled)
    if (b == 99 || w < 0) { g_trade_arg = 0; return; }
    int slot = b - 104;
    if (g_trade_arg != WND_SECOND) { g_trade_arg = WND_SECOND; g_reforge_slot = slot; ui_pick_item(PICK_SMITH); return; }
    int first = g_reforge_slot; g_trade_arg = 0;
    const char *err = smith_verify(game_slot(first), game_slot(slot), first, slot);
    if (err) { smith_tell(w, err); return; }
    smith_reforge(first, slot);
    return;
  }
  if (g_trade_arg == WND_SMITH) { g_trade_arg = 0; if (b == 0) ui_pick_item(PICK_SMITH); return; }
  if (B->wnd_choice) B->wnd_choice(b);
}

// ---------------- the hooks ----------------
static bool act(int i) {
  switch (M_(i)->type) {
    case MT_SPINNER: return spinner_act(i);
    case MT_DM300: return dm300_act(i);
    case MT_BLACKSMITH: throw_item(i); break;          // Blacksmith.act: throwItem, then NPC.act
  }
  return B->act ? B->act(i) : false;
}
static int can_attack(int i, int enemy, int epos) {
  if (M_(i)->type == MT_SHAMAN) {
    int16_t tr[BOLT_MAX + 1];
    int n = ballistica_cast(M_(i)->pos, epos, false, true, tr);
    return n > 0 && tr[n - 1] == epos;                // (no charm check: a charmed one zaps all the same)
  }
  return B->can_attack ? B->can_attack(i, enemy, epos) : -1;
}
static bool do_attack(int i, int enemy) {
  if (M_(i)->type == MT_SHAMAN) { if (distance(M_(i)->pos, pos_of(enemy)) > 1) return shaman_zap(i, enemy); return false; }
  return B->do_attack ? B->do_attack(i, enemy) : false;
}
static int stats(int i, int w) {
  if (M_(i)->type == MT_BRUTE || M_(i)->type == MT_SHIELDED) { int v = brute_stats(i, w); if (v >= 0) return v; }
  return B->stats ? B->stats(i, w) : -1;
}
static int attack_proc(int i, int def, int dmg) {
  switch (M_(i)->type) {
    case MT_BAT: return bat_attack_proc(i, dmg);
    case MT_SPINNER: return spinner_attack_proc(i, def, dmg);
    case MT_ELEMENTAL: return elemental_attack_proc(def, dmg);
    case MT_MONK: case MT_SENIOR: return monk_attack_proc(i, def, dmg);
  }
  return B->attack_proc ? B->attack_proc(i, def, dmg) : dmg;
}
static int damage(int i, int dmg, int src) {
  if (M_(i)->type == MT_BRUTE || M_(i)->type == MT_SHIELDED) brute_damage(i, dmg);
  return B->damage ? B->damage(i, dmg, src) : dmg;
}
static void die(int i) {
  Mob *m = M_(i);
  if ((m->type == MT_MONK || m->type == MT_SENIOR) && G.q.imp == QS_GIVEN && G.q.imp_alt) heap_put(m->pos, item_make(IK_DWARF_TOKEN, 1));   // Imp.Quest.process: any floor (a monk on 14)
  if (m->type == MT_BAT && G.hero.weapon.kind == IK_PICKAXE && adjacent(m->pos, G.hero.pos)) G.hero.weapon.flags |= IF_BLOODY;   // Pickaxe.proc
  if (m->type == MT_DM300) {                           // DM300.die: the skeleton key; the arena opens
    if (!huntress_unlocked()) persist_write_bool(KEY_HUNTRESS, true);   // Badges.validateBossSlain: BOSS_SLAIN_3 unlocks the Huntress
    Item k = item_make(IK_SKELETON_KEY, 1); k.lvl = G.depth; heap_put(m->pos, k);
    int door = G.rooms[0].cell;
    if (door >= 0) set_terrain(door, T_EMPTY_DECO);    // CavesBossLevel.drop(SkeletonKey)
    if (G.hero.HP > 0) ui_fx(FX_BOSS_SLAIN, -1, -1, NULL);   // GameScene.bossSlain: banner and sound
    yell(i, "Mission failed. Shutting down.");
    update_fov();
  }
  if (B->die) B->die(i);
}
static void moved(int i) {
  Mob *m = M_(i);
  if (m->type == MT_SPINNER && m->state == ST_FLEEING) blob_seed(BLOB_WEB, g_move_from, rnd_int2(5, 7));   // Spinner.move
  if (m->type == MT_DM300) dm300_moved(i);
  if (B->moved) B->moved(i);
}
static void spawned(int i) {
  if (M_(i)->type == MT_BLACKSMITH) M_(i)->state = ST_PASSIVE;
  if (B->spawned) B->spawned(i);
}
static void notice(int i) {
  if (M_(i)->type == MT_DM300) { yell(i, "Unauthorised personnel detected."); return; }
  if (B->notice) B->notice(i);
}
static ActResult interact(int i) {
  if (M_(i)->type == MT_BLACKSMITH) return smith_interact(i);
  return B->interact ? B->interact(i) : ACT_NONE;
}
static void install(MobHooks *h, const MobHooks *base) {
  B = base;
  h->act = act; h->can_attack = can_attack; h->do_attack = do_attack; h->stats = stats; h->attack_proc = attack_proc;
  h->damage = damage; h->die = die; h->moved = moved; h->spawned = spawned; h->notice = notice; h->interact = interact;
  h->wnd_choice = wnd_choice; h->press = press;
}
OVL_ENTRY(caves, (void *)install);
