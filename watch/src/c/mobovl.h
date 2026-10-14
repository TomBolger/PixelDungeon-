/*
 * Pixel Dungeon for Pebble - helpers shared by the creature overlays (src/ovl/mobs and the regions').
 * Header-only and static: each overlay gets its own copy of what it uses.
 */
#pragma once
#include "pd.h"
#include "game_int.h"
#include "ovl.h"

static inline Mob *M_(int i) { return &G.mobs[i]; }
static inline bool seen(int c) { return BIT_GET(G.fov, c); }
static void yell(int i, const char *s) { LOG(C_BAD, "%s: \"%s\"", mob_name(M_(i)->type), s); }   // Mob.yell
static int free_nb(int c, bool four) {                // a random free passable neighbour, or -1
  int16_t cand[8]; int n = 0;
  for (int k = 0; k < 8; k++) {
    if (four && k >= 4) continue;                     // N8: the four straight neighbours come first
    int p = c + N8[k];
    if (nb_ok(c, p) && PASSABLE(G.map[p]) && !char_at(p)) cand[n++] = p;
  }
  return n ? cand[rnd_int(n)] : -1;
}
// items a creature carries (HEAP_CARRIED, pos = CARRIED_POS(uid))
static bool carry_at(int pos, Item it) {             // false: nothing, or no room (MAX_HEAPS)
  if (!it.kind || G.n_heaps >= MAX_HEAPS) return false;
  G.heaps[G.n_heaps++] = (Heap){ pos, HEAP_CARRIED, it };
  return true;
}
#define carry(i, it) carry_at(CARRIED_POS(M_(i)->uid), it)
static int carried(int i) { for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].pos == CARRIED_POS(M_(i)->uid)) return h; return -1; }
static void drop_carried(int i) {
  int h;
  while ((h = carried(i)) >= 0) { Item it = G.heaps[h].item; heap_remove(h); heap_put(M_(i)->pos, it); }
}
static int find_mob(int type) { for (int i = 0; i < G.n_mobs; i++) if (M_(i)->HP > 0 && M_(i)->type == type) return i; return -1; }
static const char *hero_class_title(void) {           // HeroClass.title
  static const char *const T[4] = { "warrior", "mage", "rogue", "huntress" };
  return T[G.hero.hclass & 3];
}
static const char *hero_class_name(void) {             // Hero.className: the subclass's title once chosen
  static const char *const S[9] = { "", "gladiator", "berserker", "warlock", "battlemage", "assassin", "freerunner", "sniper", "warden" };
  return G.hero.subclass && G.hero.subclass < 9 ? S[G.hero.subclass] : hero_class_title();
}
// NPC.throwItem: an NPC standing on a heap (any kind, a hidden one too) kicks its top item to a random neighbour that is
// passable or to be avoided (a chasm, a trap...), where Level.drop lays it down as a plain heap
static void throw_item(int i) {
  int c = M_(i)->pos, h = G.n_heaps, n, k = 0;
  while (--h >= 0 && G.heaps[h].pos != c) {}
  if (h < 0) return;
  do n = c + N8[rnd_int(8)]; while (!(PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID)) && ++k < 32);
  if (k == 32) return;
  Item it = G.heaps[h].item; heap_remove(h); level_drop(n, it);
}
// Buff.affect(Ooze) on whoever was hit (Goo, the rotting fist)
static void affect_ooze(int who) {
  if (who >= 0) { M_(who)->flags |= MF_OOZE; return; }
  if (!G.hero.ooze) ui_log("Caustic ooze eats your flesh. Wash away it!", C_WARN);   // Hero.add: GLog.w
  G.hero.ooze = 20;
}
