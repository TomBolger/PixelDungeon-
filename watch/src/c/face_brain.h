// Watchface autoplayer: the few rules it adds on top of the Assist's target list (ovl/tgt, the bot's targeting).
// Pure game-state code, shared by main.c (face.inc) and bot/tests/facetest.c. Only included in PD_FACE builds.
#pragma once
#include "pd.h"
#include "game_int.h"
static int s_fb_depth = -1, s_fb_presses, s_fb_best, s_fb_since;   // presses on this floor; the most cells seen so far and the presses since that grew
static int fb_seen(void) { int n = 0; for (int i = 0; i < MAP_LEN / 8; i++) n += __builtin_popcount(G.visited[i]); return n; }
static bool fb_foe(void) { return game_nearest_enemy() >= 0; }
// day-WJ: the presses made in the window now up (the backpack, the item, the aim screen) are counted for that window alone. They were
// counted together: after the dozen presses it takes to walk to an item in the backpack and its Throw option, the aim screen's own
// limit (3) was already passed and it backed out at once, every time: a thrown thing (a spare weapon onto a trap...) was never thrown.
static int s_fb_sc = -1;
static void fb_scene(int scene, uint8_t *n) { if (scene != s_fb_sc) { s_fb_sc = scene; *n = 0; } }
// day-WJ: a throw ordered onto a known trap (the Assist: shut in by traps, or a traps room's prize) that has been ordered at the same
// cell for the third time in a row did not land twice: no more throws onto traps on this floor (g_face_nothrow: the Assist's tgt4
// then plans the cheapest way over the traps itself). A false return: do not carry the order out. Throws at foes are not counted.
uint8_t g_face_nothrow;
static int s_fb_thc = -1, s_fb_thn;
static bool fb_throw(int cell) {
  if (cell < 0 || mob_at(cell) >= 0) return true;                // (a throw at a foe is not counted: only at a cell, a trap or a barricade)
  if (cell == s_fb_thc) s_fb_thn++; else { s_fb_thc = cell; s_fb_thn = 0; }
  if (s_fb_thn < 2) return true;
  g_face_nothrow = 1; return false;
}
// Called before each decision. Returns a cell to walk to (the way up with the Amulet, or the way down when the floor has
// gone on too long), -1: press the Assist's first suggestion as usual, or FB_LEVEL: the floor was changed here (the stuck-
// breaker's last resort: a hidden stair; the caller treats it as a walk that ended on stairs).
#define FB_LEVEL (-3)
#define FB_DROP (-4)                                               // a full pack with the Amulet lying in sight: drop s_fb_slot (the caller does it: game_item_do(s_fb_slot, ACN_DROP))
static int s_fb_slot;
static bool fb_clog(void) {                                        // the Amulet is on the floor, the pack is full ("too full for the Amulet": it was walked to forever): the least useful item goes
  if (game_bag_find(IK_AMULET) >= 0) return false;
  bool am = false; for (int i = 0; i < G.n_heaps; i++) am |= G.heaps[i].item.kind == IK_AMULET && distance(G.heaps[i].pos, G.hero.pos) > 1;
  if (!am) return false;
  int free_ = 0; for (int i = 0; i < BAG_SIZE; i++) free_ += G.hero.bag[i].kind == 0;
  if (free_) return false;
  static const int8_t ORDER[] = { CAT_WEAPON, CAT_ARMOR, CAT_MISSILE, CAT_SEED, CAT_MISC, CAT_RING, CAT_WAND, CAT_SCROLL, CAT_POTION, CAT_QUEST };
  for (unsigned o = 0; o < sizeof ORDER; o++) for (int i = BAG_SIZE - 1; i >= 0; i--) if (G.hero.bag[i].kind && item_cat(G.hero.bag[i].kind) == ORDER[o]) { s_fb_slot = i; return true; }
  return false;
}
// TEMPORARY entertainment safeguard (day-WI): no progress for FACE_STUCK_TURNS game turns (the hero within 2 cells of where
// the count began, no foe in sight, health not rising) and he is teleported at random like a Scroll of Teleportation (not
// where that has no room to do it: a boss floor). A healing rest keeps the count at zero; a rest or a search at full health does not.
#ifndef FACE_STUCK_TURNS
#define FACE_STUCK_TURNS 60
#endif
#ifdef OVL_HOST
int g_fb_tele;                                                     // host test: how often it fired
#endif
static int16_t s_fb_sa, s_fb_shp; static int s_fb_st;
// day-WJ: the broader rule. FACE_NOPROG_TURNS game turns with none of: a cell seen for the first time, something picked up (gold or
// anything in the pack grew), the floor changed, a creature killed (g_face_kills, game.c mob_kill), health gained while below the
// maximum. Turns with a foe in sight do not count. (The 60-turn rule above only sees him stay put: this one catches pacing about.)
#ifndef FACE_NOPROG_TURNS
#define FACE_NOPROG_TURNS 150
#endif
#ifdef OVL_HOST
int g_fb_tele2, g_fb_game;                                         // host test: how often the broader rule fired; the game being played
#endif
static int s_fb_npt, s_fb_nseen, s_fb_ntot, s_fb_nhp, s_fb_ndep = -1; static uint16_t s_fb_nkill;
static bool fb_noprog(int seen) {
  Hero *h = &G.hero; int tot = h->gold; for (int i = 0; i < BAG_SIZE; i++) tot += h->bag[i].kind ? h->bag[i].qty : 0;
  bool prog = seen > s_fb_nseen || tot > s_fb_ntot || g_face_kills != s_fb_nkill || G.depth != s_fb_ndep || (h->HP > s_fb_nhp && s_fb_nhp < h->HT) || fb_foe();
  s_fb_nseen = seen; s_fb_ntot = tot; s_fb_nkill = g_face_kills; s_fb_ndep = G.depth; s_fb_nhp = h->HP;
  if (prog) s_fb_npt = G.now;
  return G.now - s_fb_npt >= FACE_NOPROG_TURNS * TICK;
}
// day-WK: a random teleport as a Scroll of Teleportation does it (no scroll used, nothing logged). The stuck rules below and the settings
// page's "Teleport the hero" use it. false: no room for it (a boss floor).
static bool fb_teleport(void) {
  int c = random_respawn_cell();                         // (-1: no room for it, a boss floor)
  if (c < 0) return false;
  G.hero.pos = c; hero_press(c); update_fov();
  return true;
}
// day-WK: the class of a new run: among the ticked ones (mask bits: warrior, mage, rogue, huntress; none ticked = all), never the last
// run's unless it is the only one. r: random bits (>> 7). With all four ticked: the choice the face always made.
static int fb_pick_class(int mask, bool had, int last, uint32_t r) {
  mask &= 15; if (!mask) mask = 15;
  int n = __builtin_popcount(mask), c[4], k = 0;
  for (int j = 0; j < 4; j++) { int x = (had ? last + 1 + j : j) & 3; if ((mask >> x & 1) && !(had && n > 1 && x == last)) c[k++] = x; }
  return c[(r >> 7) % k];
}
static void fb_stuck(int seen) {
  Hero *h = &G.hero;
  if (fb_foe() || distance(h->pos, s_fb_sa) > 2 || h->HP > s_fb_shp) { s_fb_sa = h->pos; s_fb_st = G.now; }
  s_fb_shp = h->HP;
  bool np = fb_noprog(seen);
  if (!np && G.now - s_fb_st < FACE_STUCK_TURNS * (h->HP < h->HT ? 3 : 1) * TICK) return;   // (hurt and resting: a third of the rate)
  s_fb_st = s_fb_npt = G.now;
  if (!fb_teleport()) return;
#ifdef OVL_HOST
  if (np) g_fb_tele2++; else g_fb_tele++;
  if (getenv("TELEDBG")) printf("TELE%s g%d d%d pos %d,%d HP %d/%d hunger %d since %d amu %d mobs %d\n", np ? "2" : "", g_fb_game, G.depth, cx(h->pos), cy(h->pos), h->HP, h->HT, h->hunger, s_fb_since, game_bag_find(IK_AMULET) >= 0, G.n_mobs);
#endif
}
static int fb_override(void) {
  if (G.depth != s_fb_depth) { s_fb_depth = G.depth; s_fb_presses = 0; s_fb_best = 0; s_fb_since = 0; g_face_nothrow = 0; s_fb_thc = -1; }
  int seen = fb_seen();
  fb_stuck(seen);
  s_fb_presses++;
  if (seen > s_fb_best) { s_fb_best = seen; s_fb_since = 0; } else s_fb_since++;
  bool up = game_bag_find(IK_AMULET) >= 0;
  if (s_fb_since > 700 || s_fb_presses > 2500) {                   // nothing new for ages: a hidden stair, any fight or not
    s_fb_presses = 0; s_fb_since = 0;
    if (up) { if (G.depth == 1) { ui_amulet(2); return FB_LEVEL; } switch_level(G.depth - 1, ARRIVE_EXIT); return FB_LEVEL; }
    if (G.depth < LAST_DEPTH) { switch_level(G.depth + 1, ARRIVE_ENTRANCE); return FB_LEVEL; }
  }
  if (fb_clog()) return FB_DROP;
  if (up && G.map[G.entrance] == T_ENTRANCE && !BIT_GET(G.visited, G.entrance)) memset(G.visited, 0xFF, sizeof G.visited);   // (a floor remembered from the way down is not kept: the cheat knows the way)
  if (!up && s_fb_since > 120 && G.depth < LAST_DEPTH && G.exit >= 0 && !BIT_GET(G.visited, G.exit)) memset(G.visited, 0xFF, sizeof G.visited);   // lost on a floor: the map shows itself
  if (s_fb_since > 150 && G.exit >= 0 && !up && G.depth < LAST_DEPTH && BIT_GET(G.visited, G.exit)) return G.exit;   // the way down is known and the floor has gone on: go, fight or not
  if (up && G.map[G.entrance] == T_ENTRANCE) return G.entrance;      // the Amulet: straight home, floor by floor (foes or not: he cannot die, and dodges more the more he is hurt)
  if (fb_foe()) return -1;                                           // a fight: the Assist's
  if (G.depth < LAST_DEPTH && (s_fb_since > 60 || s_fb_presses > 500) && G.exit >= 0 && BIT_GET(G.visited, G.exit)) return G.exit;   // nothing new for a while: on down
  return -1;
}
