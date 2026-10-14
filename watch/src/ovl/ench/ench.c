/*
 * Pixel Dungeon for Pebble - weapon enchantments and armour glyphs (an overlay, loaded only when the hero's
 * equipment carries one). Ported from Pixel Dungeon 1.9.1 (GPLv3): the classes in weapon/enchantments and armor/glyphs.
 * Only the hero's equipment is enchanted in the sewers, so the attacker of a weapon proc is always the hero
 * and the defender of a glyph proc is always the hero.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"
#ifdef TEST_BUILD
#define PROC(x) ((void)(x), 1)        // test kit: every enchantment and glyph triggers
#else
#define PROC(x) (x)
#endif

static int eff_level(const Item *it) { int l = item_broken(it) ? 0 : it->lvl; return l > 0 ? l : 0; }   // max(0, effectiveLevel)
static bool mob_alive(int i) { return i >= 0 && G.mobs[i].HP > 0; }
static int free_cell_near(int c, int16_t *out) {        // passable (or avoidable) neighbours with nobody in them
  int n = 0;
  for (int j = 0; j < 8; j++) { int p = c + N8[j]; if (nb_ok(c, p) && !char_at(p) && (PASSABLE(G.map[p]) || (TFLAGS(G.map[p]) & F_AVOID))) out[n++] = p; }
  return n;
}

// ---- Shock: lightning jumps from the target to a random neighbour, then on, each at up to its share ----
static void shock_hit(int i, int dmg, uint32_t *hit_mask) {
  if (dmg < 1 || !mob_alive(i)) return;
  *hit_mask |= 1u << i;
  Mob *m = &G.mobs[i];
  int16_t pos = m->pos;
  bool wet = IS_WATER(G.map[pos]);
  if (BIT_GET(G.fov, pos)) ui_fx(FX_TEXT, pos, i, "zap");
  g_dmg_src = DS_SHOCK; mob_damage(i, wet ? dmg * 2 : dmg);
  int cand[8], n = 0;
  for (int j = 0; j < 8; j++) {
    int p = pos + N8[j]; if (!nb_ok(pos, p)) continue;
    int k = mob_at(p);
    if (k >= 0 && !(*hit_mask & (1u << k))) cand[n++] = k;
  }
  if (n) shock_hit(cand[rnd_int(n)], rnd_int2(dmg / 2, dmg), hit_mask);
}

static Item *s_w;                                       // the weapon: in hand, or thrown (a boomerang)
static void weapon_proc(int e, int def, int dmg, int depth) {
  Item *w = s_w;
  Mob *m = &G.mobs[def];
  int lvl = eff_level(w);
  switch (e) {
    case WE_FIRE:                                        // Fire ("blazing")
      if (PROC(rnd_int(lvl + 3) >= 2)) {
        if (PROC(rnd_int(2) == 0)) affect_burning(def);
        mob_damage(def, rnd_int2(1, lvl + 2));
      }
      break;
    case WE_POISON:                                      // Poison ("venomous")
      if (PROC(rnd_int(lvl + 3) >= 2)) affect_poison(def, lvl + 1);
      break;
    case WE_DEATH:                                       // Death ("grim")
      if (PROC(rnd_int(lvl + 100) >= 92)) { if (BIT_GET(G.fov, m->pos)) ui_fx(FX_TEXT, m->pos, def, "grim!"); g_dmg_src = DS_DEATH; mob_damage(def, m->HP); }
      break;
    case WE_PARALYSIS:                                   // Paralysis ("stunning"): 1 .. 1.5 + level turns
      if (PROC(rnd_int(lvl + 8) >= 7)) affect_paralysis(def, 1 + rnd_int(lvl + 1));   // Buff.prolong: immunities (King, Piranha, FetidRat...)
      break;
    case WE_LEECH: {                                     // Leech ("vampiric")
      int maxv = dmg * (lvl + 2) / (lvl + 6), v = rnd_range(0, maxv), room = G.hero.HT - G.hero.HP;
      if (v > room) v = room;
      if (v > 0) { G.hero.HP += v; char t[8]; snprintf(t, sizeof t, "%d", v); ui_fx(FX_HEAL, G.hero.pos, -1, t); }
      break;
    }
    case WE_SLOW:                                        // Slow ("chilling")
      if (PROC(rnd_int(lvl + 4) >= 3)) { int t = 1 + rnd_int(lvl + 1); if (t > m->b.slow) m->b.slow = t; }
      break;
    case WE_SHOCK:                                       // Shock ("shocking")
      if (PROC(rnd_int(lvl + 4) >= 3)) { uint32_t mask = 0; shock_hit(def, rnd_int2(1, dmg / 2), &mask); }
      break;
    case WE_INSTABILITY:                                 // Instability ("unstable"): any enchantment's effect
      if (depth < 4) weapon_proc(ench_random(false, 0), def, dmg, depth + 1);
      break;
    case WE_HORROR:                                      // Horror ("eldritch"): Terror for 10 turns
      if (PROC(rnd_int(lvl + 5) >= 4) && mob_alive(def) && !(MOB_DEFS[m->type].flags & MD_I_TERROR)) { m->b.terror = 10; m->state = ST_FLEEING; m->target = G.hero.pos; }
      break;
    case WE_LUCK: {                                      // Luck ("lucky"): the best of a few damage rolls
      int best = dmg;
      for (int i = 1; i <= lvl + 1; i++) { int d = hero_damage_roll(w == &G.hero.weapon ? NULL : w) - i; if (d > best) best = d; }
      if (best > dmg) mob_damage(def, best - dmg);
      break;
    }
    case WE_TEMPERING:                                   // Tempering ("tempered"): polish on every hit
      if (w->dur < item_max_dur(w)) w->dur++;
      break;
  }
}
// Statue.attackProc: weapon.proc(this, enemy, damage), the statue's enchantment on the hero (att: the statue)
static void statue_proc(int att, int dmg, Item *w) {
  Mob *m = &G.mobs[att];
  Hero *h = &G.hero;
  static const char CAUSE[] = "Killed by an animated statue on level %d";
  int lvl = eff_level(w), e = w->ench;
  for (int k = 0; k < 4 && e == WE_INSTABILITY; k++) e = ench_random(false, 0);   // Instability: another's effect
  switch (e) {
    case WE_FIRE:
      if (PROC(rnd_int(lvl + 3) >= 2)) { if (PROC(rnd_int(2) == 0)) affect_burning(-1); hero_damage(rnd_int2(1, lvl + 2), CAUSE); }
      break;
    case WE_POISON: if (PROC(rnd_int(lvl + 3) >= 2)) affect_poison(-1, lvl + 1); break;
    case WE_DEATH: if (PROC(rnd_int(lvl + 100) >= 92)) hero_damage(h->HP, CAUSE); break;
    case WE_PARALYSIS: if (PROC(rnd_int(lvl + 8) >= 7)) affect_paralysis(-1, 1 + rnd_int(lvl + 1)); break;
    case WE_LEECH: {                                     // the statue drinks
      int v = rnd_range(0, dmg * (lvl + 2) / (lvl + 6)), room = m->HT - m->HP;
      if (v > room) v = room;
      if (v > 0) { m->HP += v; if (BIT_GET(G.fov, m->pos)) ui_fx(FX_HEAL, m->pos, att, NULL); }
      break;
    }
    case WE_SLOW: if (PROC(rnd_int(lvl + 4) >= 3)) { int t = h->b.slow + 1 + rnd_int(lvl + 1); h->b.slow = t > 255 ? 255 : t; } break;   // Buff.affect: adds
    case WE_SHOCK:                                       // the lightning starts on the hero, then jumps on (not to the statue)
      if (PROC(rnd_int(lvl + 4) >= 3)) {
        int d = rnd_int2(1, dmg / 2);
        hero_damage(elem_resist(-1, IS_WATER(G.map[h->pos]) && !hero_flying() ? d * 2 : d), CAUSE);
        int16_t cells[8]; int n = 0;
        for (int j = 0; j < 8; j++) { int k = mob_at(h->pos + N8[j]); if (k >= 0 && k != att && nb_ok(h->pos, h->pos + N8[j])) cells[n++] = k; }
        uint32_t mask = 1u << att;
        if (n && h->HP > 0) shock_hit(cells[rnd_int(n)], rnd_int2(d / 2, d), &mask);
      }
      break;
    case WE_HORROR:                                      // on the hero: Vertigo
      if (PROC(rnd_int(lvl + 5) >= 4)) {
        if (!h->vertigo) { ui_log(txt(TX_L_SPINNING), C_WARN); interrupt(); }
        int t = h->vertigo + elem_duration(-1, 10); h->vertigo = t > 255 ? 255 : t;
      }
      break;
    case WE_LUCK: {                                      // the best of a few of the statue's rolls
      int best = dmg;
      for (int i = 1; i <= lvl + 1; i++) { int d = rnd_normal(weapon_min(w), weapon_max(w)) - i; if (d > best) best = d; }
      if (best > dmg) hero_damage(best - dmg, CAUSE);
      break;
    }
  }
}
static void ench_weapon(int def, int dmg, Item *w) {     // (a mage's wand never comes here: its Item.ench holds charges)
  if (def <= -2) { statue_proc(-2 - def, dmg, w); return; }   // -2 - i: the hero hit by statue i
  if (def < 0 || !mob_alive(def)) return;
  s_w = w;
  weapon_proc(w->ench, def, dmg, 0);
}

static int ench_armor(int att, int dmg) {
  Item *a = &G.hero.armor;
  Hero *h = &G.hero;
  int lvl = eff_level(a), g = a->ench;
  bool adj = att >= 0 && adjacent(G.mobs[att].pos, h->pos);
  Mob *m = att >= 0 ? &G.mobs[att] : NULL;
  int16_t cells[8];
  switch (g) {
    case AG_BOUNCE:                                      // knock the attacker back a cell
      if (adj && PROC(rnd_int(lvl + 5) >= 4)) {
        int np = m->pos + (m->pos - h->pos);
        if (nb_ok(m->pos, np) && (PASSABLE(G.map[np]) || (TFLAGS(G.map[np]) & F_AVOID)) && !char_at(np)) { m->pos = np; mob_press(att); }
      }
      break;
    case AG_AFFECTION:                                   // both charmed: neither attacks the other for a while
      if (adj) {
        int l = lvl > 6 ? 6 : lvl;                       // GameMath.gate(0, effectiveLevel, 6)
        if (PROC(rnd_int(l / 2 + 5) >= 4)) {
          int d = rnd_range(3, 7);
          if (!mob_immune(att, MD_I_CHARM)) m->b.charm = d;   // Buff.affect: Yog won't take it
          int hd = elem_duration(-1, d * rnd_range(50, 100) / 100);   // Charm.durationFactor
          if (hd > h->b.charm) h->b.charm = hd;
          G.charm_uid = m->uid;
          ui_fx(FX_TEXT, m->pos, att, "charmed"); ui_log("You are charmed!", C_WARN);
        }
      }
      break;
    case AG_ANTI_ENTROPY:                                // freeze the attacker, set yourself alight
      if (adj && PROC(rnd_int(lvl + 6) >= 5)) {
        affect_frost(att, 5 * rnd_range(100, 150) / 100);
        affect_burning(-1);
      }
      break;
    case AG_MULTIPLICITY:                                // a mirror image steps out, at a cost
      if (PROC(rnd_int(lvl / 2 + 6) >= 5)) {
        int n = free_cell_near(h->pos, cells);
        if (n) {
          int c = cells[rnd_int(n)], mi = spawn_mob(MT_MIRROR, c, ST_HUNTING, 0);
          if (mi >= 0) { mirror_dup(mi); ui_fx(FX_SPAWN, c, mi, NULL); }
          hero_damage(rnd_range(1, h->HT / 6 > 1 ? h->HT / 6 : 1), "Killed by the enchantment of multiplicity on level %d");
        }
      }
      break;
    case AG_POTENTIAL:                                   // a discharge hurts both
      if (adj && PROC(rnd_int(lvl + 7) >= 6)) {
        int d1 = rnd_range(1, dmg > 1 ? dmg : 1);
        g_dmg_src = DS_SHOCK; mob_damage(att, d1);
        hero_damage(elem_resist(-1, rnd_range(1, d1)), "Killed by the enchantment of potential on level %d");
        if (h->HP <= 0) ui_log("You were killed by a discharge of your armor...", C_BAD);
      }
      break;
    case AG_METABOLISM:                                  // heal, paid for with food
      if (PROC(rnd_int(lvl / 2 + 5) >= 4)) {
        int hp = rnd_int2(1, h->HT / 5 > 1 ? h->HT / 5 : 2), room = h->HT - h->HP;
        if (hp > room) hp = room;
        if (hp > 0 && !game_starving()) {
          h->hunger += 36; if (h->hunger > 360) h->hunger = 360;   // satisfy(-STARVING / 10)
          h->HP += hp;
          char t[8]; snprintf(t, sizeof t, "%d", hp); ui_fx(FX_HEAL, h->pos, -1, t);
        }
      }
      break;
    case AG_STENCH:                                      // a puff of toxic gas on the attacker
      if (adj && PROC(rnd_int(lvl + 5) >= 4)) blob_seed(BLOB_TOXIC, m->pos, 20);
      break;
    case AG_VISCOSITY:                                   // the blow lands later, one point a turn
      if (dmg > 0 && PROC(rnd_int(lvl + 7) >= 6)) {
        G.deferred += dmg;
        char t[24]; snprintf(t, sizeof t, "deferred %d", dmg); ui_fx(FX_TEXT, h->pos, -1, t);
        return 0;
      }
      break;
    case AG_DISPLACEMENT: {                              // blink to a random visible cell
      if (boss_depth(G.depth)) break;                    // Dungeon.bossLevel()
      int tries = (a->lvl < 0 ? 1 : a->lvl + 1) * 5;
      for (int i = 0; i < tries; i++) {
        int p = rnd_int(MAP_LEN);
        if (BIT_GET(G.fov, p) && PASSABLE(G.map[p]) && !char_at(p)) {
          h->pos = p; ui_fx(FX_TELEPORT, p, -1, NULL);
          hero_press(p); update_fov();
          break;
        }
      }
      break;
    }
    case AG_ENTANGLEMENT:                                // rooted, but shielded by herbal armour
      if (PROC(rnd_int(4) == 0)) {
        int r = 5 - lvl / 5; if (!h->b.roots) ui_log("You can't move!", C_WARN);   // Hero.add(Roots)
        if (r > h->b.roots) h->b.roots = r;
        int lv = 5 * (lvl + 1); if (lv > 255) lv = 255;
        if (lv > G.herbal) G.herbal = lv;
        G.herbal_pos = h->pos;
      }
      break;
    case AG_AUTO_REPAIR:                                 // polish, a few coins at a time
      if (h->gold >= item_tier(a->kind)) { h->gold -= item_tier(a->kind); if (a->dur < item_max_dur(a)) a->dur++; }
      break;
  }
  return dmg;
}

OVL_ENTRY(ench, (void *)ench_weapon, (void *)ench_armor);
