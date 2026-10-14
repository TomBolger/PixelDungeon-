/*
 * Pixel Dungeon for Pebble - what each wand does when its bolt lands (an overlay).
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): WandOfTeleportation, Slowness, Firebolt, Poison, Regrowth,
 * Blink, Lightning, Amok, Reach, Flock, Disintegration, Avalanche (onZap and their fx).
 * trace: Ballistica's cells from the hero (trace[0]) to where the bolt stopped (trace[n - 1]).
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"
#include "items_int.h"

static int s_k;                                       // the wand being zapped (for "Killed by your own ...")
static char s_cause[64];
static void hurt(int who, int dmg, int src) {         // Char.damage from this wand (src: DS_*, for immunities / resistances)
  if (who >= 0) { g_dmg_src = src; mob_damage(who, dmg); return; }
  if (src == DS_SHOCK) dmg = elem_resist(-1, dmg);       // Ring of Elements: Electricity
  hero_damage(dmg, s_cause);
  if (G.hero.HP <= 0) LOG(C_BAD, "You killed yourself with your own %s...", txt(DEFS[s_k == 13 ? IK_WND_MM : IK_FIRST_WAND + s_k].name));
}
static int who_at(int c) { return c == G.hero.pos && G.hero.HP > 0 ? -1 : mob_at(c) >= 0 ? mob_at(c) : -2; }
static int16_t pos_of_w(int who) { return who < 0 ? G.hero.pos : G.mobs[who].pos; }
static void nothing(void) { ui_log("nothing happened", C_INFO); }

static void blink_to(int c) {                         // WandOfBlink.appear: Char.move + Hero.move
  Hero *h = &G.hero;
  if (G.map[h->pos] == T_OPEN_DOOR && heap_at(h->pos) < 0) set_terrain(h->pos, T_DOOR);
  h->pos = c;
  ui_fx(FX_TELEPORT, c, -1, NULL);
  ui_sound(SND_TELEPORT);
  g_level_changed = false;
  if (!hero_flying()) hero_press(c);
  if (!g_level_changed) update_fov();
}

// WandOfLightning.hit: the arc jumps to a random neighbour it hasn't hit, at half to full damage
static uint32_t s_hit;                                // bit per mob, bit 31 the hero
static int16_t s_pts[20]; static int s_npts;
static void arc(int who, int dmg) {
  if (dmg < 1 || s_npts >= 20) return;
  s_hit |= who < 0 ? 1u << 31 : 1u << who;
  int16_t p = pos_of_w(who);
  bool wet = IS_WATER(G.map[p]) && !(who < 0 && hero_flying());
  s_pts[s_npts++] = p;
  if (BIT_GET(G.fov, p)) ui_fx(FX_TEXT, p, who, "zap!");
  hurt(who, wet ? dmg * 2 : dmg, DS_SHOCK);
  int ns[8], nn = 0;
  for (int k = 0; k < 8; k++) {
    int c = p + N8[k];
    if (!nb_ok(p, c)) continue;
    int w = who_at(c);
    if (w == -2 || (w < 0 ? (s_hit >> 31) & 1 : (s_hit >> w) & 1) || (w >= 0 && G.mobs[w].HP <= 0)) continue;
    ns[nn++] = w;
  }
  if (nn) arc(ns[rnd_int(nn)], rnd_int2(dmg / 2, dmg));
}

static void zap_effect(int k, int power, int level, const int16_t *trace, int n) {
  Hero *h = &G.hero;
  int16_t cell = trace[n - 1];
  int who = who_at(cell);
  s_k = k;
  int kind = k == 13 ? IK_WND_MM : IK_FIRST_WAND + k;
  snprintf(s_cause, sizeof s_cause, "Killed by your own %s on level %%d", txt(DEFS[kind].name));
  if (kind != IK_WND_LIGHTNING) ui_zap(k, h->pos, kind == IK_WND_DISINTEGRATION ? trace[(n < level + 4 ? n : level + 4) - 1] : cell, NULL, 0);
  ui_sound(kind == IK_WND_DISINTEGRATION ? SND_RAY : kind == IK_WND_LIGHTNING ? SND_LIGHTNING : SND_ZAP);
  switch (kind) {
    case IK_WND_TELEPORT:
      if (who == -1) {                                // the bolt stopped on the hero: ScrollOfTeleportation.teleportHero
        int p = -1;
        for (int c = 10; c > 0 && p < 0; c--) p = random_respawn_cell();
        if (p < 0) ui_log(txt(TX_STRONG_MAGIC_AURA_OF), C_WARN);
        else { blink_to(p); ui_log(txt(TX_IN_A_BLINK_OF), C_INFO); }
      } else if (who >= 0) {
        int p = -1;
        for (int c = 10; c > 0 && p < 0; c--) p = random_respawn_cell();
        if (p < 0) ui_log(txt(TX_STRONG_MAGIC_AURA_OF), C_WARN);
        else {
          G.mobs[who].pos = p; ui_fx(FX_TELEPORT, p, who, NULL);
          LOG(C_INFO, "you teleported %s to somewhere", mob_name(G.mobs[who].type));
        }
      } else nothing();
      break;
    case IK_WND_SLOW:                                 // Slow.duration / 3 + power, added to what's left
      if (who != -2) { Buffs *b = buffs_of(who); int t = b->slow + elem_duration(who, 10) / 3 + power; b->slow = t > 255 ? 255 : t; ui_fx(FX_TEXT, cell, who, "slowed"); }
      else nothing();
      break;
    case IK_WND_FIREBOLT:
      // PD seeds 1 (a door chars the very next turn); flammable cells burn 4 turns here, like fire that spread to them
      for (int i = 1; i < n - 1; i++) if (TFLAGS(G.map[trace[i]]) & F_FLAMABLE) blob_seed(BLOB_FIRE, trace[i], 4);
      blob_seed(BLOB_FIRE, cell, (TFLAGS(G.map[cell]) & F_FLAMABLE) ? 4 : 1);
      if (who != -2) { if (who < 0 || !(MOB_DEFS[G.mobs[who].type].flags & MD_I_FIREBOLT)) hurt(who, rnd_int2(1, 8 + power * power), DS_PHYS); if (who >= 0 ? G.mobs[who].HP > 0 : h->HP > 0) affect_burning(who); }
      break;
    case IK_WND_POISON:
      if (who != -2) affect_poison(who, 5 + power); else nothing();
      break;
    case IK_WND_REGROWTH: {
      for (int i = 1; i < n - 1; i++) {
        int c = G.map[trace[i]];
        if (c == T_EMPTY || c == T_EMBERS || c == T_EMPTY_DECO) set_terrain(trace[i], T_GRASS);
      }
      int c = G.map[cell];
      if (c == T_EMPTY || c == T_EMBERS || c == T_EMPTY_DECO || c == T_GRASS || c == T_HIGH_GRASS) blob_seed(BLOB_REGROWTH, cell, (power + 2) * 20);
      else nothing();
      break;
    }
    case IK_WND_BLINK: {
      int c = cell;
      if (n > power + 4) c = trace[power + 3];
      else if (who != -2 && n > 1) c = trace[n - 2];
      blink_to(c);
      break;
    }
    case IK_WND_LIGHTNING:
      s_hit = 0; s_npts = 0; s_pts[s_npts++] = h->pos;
      if (who != -2) arc(who, rnd_int2(5 + power / 2, 10 + power));
      else s_pts[s_npts++] = cell;
      ui_zap(k, h->pos, cell, s_pts, s_npts);
      break;
    case IK_WND_AMOK:
      if (who == -1) { int v = G.hero.vertigo + elem_duration(-1, 10); G.hero.vertigo = v > 255 ? 255 : v; }   // Vertigo.duration
      else if (who >= 0 && (MOB_DEFS[G.mobs[who].type].flags & MD_I_AMOK)) ;   // Golem, Monk, Yog: Amok won't attach
      else if (who >= 0) { MOB_SET_AMOK(&G.mobs[who], 3 + power); G.mobs[who].state = ST_HUNTING; ui_fx(FX_TEXT, cell, who, "amok"); }
      else nothing();
      break;
    case IK_WND_REACH: {
      int reach = n < power + 4 ? n : power + 4;
      bool changed = false;
      for (int i = 1; i < reach; i++) {
        int c = trace[i], before = G.map[c], w = who_at(c);
        if (w >= 0) {                                 // Swap: you and the creature trade places
          int16_t hp = h->pos; h->pos = G.mobs[w].pos; G.mobs[w].pos = hp;
          ui_sound(SND_TELEPORT);
          g_level_changed = false;
          if (!hero_flying()) hero_press(h->pos);
          if (!g_level_changed) mob_press(w);
          changed = true;
          break;
        }
        int hp = heap_at(c);
        if (hp >= 0) { int ty = G.heaps[hp].type; if (ty == HEAP_PLAIN) pick_up_ex(c, true); else if (ty == HEAP_CHEST || ty == HEAP_MIMIC || ty == HEAP_TOMB || ty == HEAP_SKELETON) hero_op(HO_OPEN, c, 1, NULL); break; }
        level_press_nobody(c);
        if (before == T_OPEN_DOOR && heap_at(c) < 0) set_terrain(c, T_DOOR);
        if (G.map[c] != before) changed = true;
      }
      if (changed && !g_level_changed) update_fov();
      break;
    }
    case IK_WND_FLOCK: {
      int nsheep = power + 2;
      if (who != -2 && n > 2) cell = trace[n - 2];
      for (int i = 0; i < MAP_LEN; i++) if (PASSABLE(G.map[i]) || (TFLAGS(G.map[i]) & F_AVOID)) BIT_SET(s_pass, i); else BIT_CLR(s_pass, i);
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) BIT_CLR(s_pass, G.mobs[i].pos);
      BIT_CLR(s_pass, h->pos);
      distance_map(cell, -1);
      int d = 0;
      if (who_at(cell) != -2) { s_dist[cell] = 0x7FFF; d = 1; }
      int life = (power + 3) * TICK;
      for (int i = 0; i < nsheep; i++) {
        bool placed = false;
        while (!placed && d < nsheep) {
          for (int j = 0; j < MAP_LEN; j++) if (s_dist[j] == d) {
            int m = spawn_mob(MT_SHEEP, j, ST_PASSIVE, life + rnd_int(2 * TICK));   // NPC: PASSIVE
            if (m >= 0) { ui_fx(FX_SPAWN, j, m, NULL); mob_press(m); }
            s_dist[j] = 0x7FFF; placed = true; break;
          }
          if (!placed) d++;
        }
      }
      break;
    }
    case IK_WND_DISINTEGRATION: {
      int reach = n < level + 4 ? n : level + 4;
      int chars[BOLT_MAX], nc = 0;
      bool changed = false;
      for (int i = 1; i < reach; i++) {
        int c = trace[i], w = who_at(c);
        if (w != -2) chars[nc++] = w;
        int t = G.map[c];
        if (t == T_DOOR || t == T_SIGN) { level_destroy(c); changed = true; }
        else if (t == T_HIGH_GRASS) { set_terrain(c, T_GRASS); changed = true; }
      }
      if (changed) update_fov();
      int l = power + nc;
      for (int i = 0; i < nc; i++) hurt(chars[i], rnd_normal(l, 8 + l * l / 3), DS_DISINT);
      break;
    }
    case IK_WND_MM:                                   // WandOfMagicMissile.onZap
      if (who != -2) hurt(who, rnd_int2(1, 6 + power * 2), DS_PHYS);
      break;
    case IK_WND_AVALANCHE: {
      ui_sound(SND_ROCKS);
      int size = 1 + power / 3;
      ui_fx(FX_QUAKE, cell, size, NULL);         // 0.28: Camera.shake and the falling rocks (rubble squares over the area)
      for (int i = 0; i < MAP_LEN; i++) if (TFLAGS(G.map[i]) & F_SOLID) BIT_CLR(s_pass, i); else BIT_SET(s_pass, i);
      distance_map(cell, -1);
      for (int i = 0; i < MAP_LEN; i++) {
        int d = s_dist[i];
        if (d > size) continue;
        int w = who_at(i);
        if (w != -2) {
          hurt(w, rnd_int2(2, 6 + (size - d) * 2), DS_PHYS);
          if ((w < 0 ? h->HP > 0 : G.mobs[w].HP > 0) && rnd_int(2 + d) == 0) affect_paralysis(w, rnd_range(2, 6));
        }
        if (w == -1 && h->HP > 0) { if (!hero_flying()) hero_press(i); }
        else if (w >= 0 && G.mobs[w].HP > 0) mob_press(w);
        else level_press_nobody(i);
      }
      break;
    }
  }
}

OVL_ENTRY(wand, (void *)zap_effect);
