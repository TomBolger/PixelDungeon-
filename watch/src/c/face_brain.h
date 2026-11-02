// Watchface autoplayer: the few rules it adds on top of the Assist's target list (ovl/tgt, the bot's targeting).
// Pure game-state code, shared by main.c (face.inc) and bot/tests/facetest.c. Only included in PD_FACE builds.
#pragma once
#include "pd.h"
#include "game_int.h"
static int s_fb_depth = -1, s_fb_presses, s_fb_best, s_fb_since;   // presses on this floor; the most cells seen so far and the presses since that grew
static int fb_seen(void) { int n = 0; for (int i = 0; i < MAP_LEN / 8; i++) n += __builtin_popcount(G.visited[i]); return n; }
static bool fb_foe(void) { return game_nearest_enemy() >= 0; }
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
static int fb_override(void) {
  if (G.depth != s_fb_depth) { s_fb_depth = G.depth; s_fb_presses = 0; s_fb_best = 0; s_fb_since = 0; }
  s_fb_presses++;
  int seen = fb_seen();
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
