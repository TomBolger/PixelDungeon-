/*
 * Pixel Dungeon for Pebble - the "tgt" overlay (0.36, from the mobs overlay): the button target cycle, game_targets.
 * Loaded for each new list (main.c tg_refresh: once per step or turn) and freed again.
 */
#ifndef TGT_PART
#define TGT_PART 0                                     // 0.48: overlays from one source: 0 "tgt" (plan A), 1 "tgt2" (plans B, C...), (0.63) 2 "tgt3" (the hands)
#endif
#include "pd.h"
#define CELLS_POS
#include "game_int.h"
#include "ovl.h"
#ifdef OVL_HOST                                        // 0.64: the bot's A/B switches for the Assist's rules (fixed on the watch)
#include <stdlib.h>
#define XENV(n, d) (getenv(n) ? atoi(getenv(n)) : (d))
#else
#define XENV(n, d) (d)
#endif
// 0.43 (perf): the neighbour test without divisions (MAP_W 32), not inlined (smaller)
static __attribute__((noinline)) bool nbk(int c, int n) { return (unsigned)n < MAP_LEN && (unsigned)((n & 31) - (c & 31) + 1) <= 2; }
#define nb_ok nbk

// 0.23 target cycle (the watch buttons): what Down picks and Select acts on (by tapping its cell), in this order:
// enemies, one step away from them, unexplored doors, things lying about, wells / pots / people, (0.26) the farthest
// open spot in view in each of eight directions (those beside the unexplored first, then the nearest remembered such
// spot out of sight, then the rest), the way down, explored doors, the way up; nearest first in each group, the
// directions farthest first. Distances are walks. kind: TK_*
#define TGT_CAP 8
#ifndef TGT_MIND
#define TGT_MIND 2                                     // 0.47: steps towards another objective that make it plan A
#endif
static int s_tn; static int16_t *s_tc; static uint8_t *s_tk; static uint16_t s_tkey[TGT_CAP];
// 0.39 (user: "it keeps turning me around"): within a group, the target ahead (the way the hero last went) first, then
// clockwise round; ties: the nearer. key: group << 8 | tier << 6 (the far spots' three tiers) | 0..63 clockwise
static int pang(int dx, int dy) {                     // a pseudo-angle 0..63, clockwise on screen from the east
  int s = abs(dx) + abs(dy); if (!s) return 0;
  return dx > 0 && dy >= 0 ? 16 * dy / s : dx <= 0 && dy > 0 ? 16 + 16 * -dx / s : dx < 0 ? 32 + 16 * -dy / s : 48 + 16 * dx / s;
}
static int s_head;                                     // the heading (pang), -1: none
static bool s_zero;                                    // 0.40: adding the first entry (what a good player would do next)
static int t_key(int c, int key) {                     // 0.47 (owner: "plan B, C..."): by group, then the walk (key: group << 10 | walk)
  (void)c; return s_zero ? 0 : key;
}
static int16_t s_obj;                                  // 0.47: the objective the entry being added stands for
#if TGT_PART >= 1
static int s_h0;                                        // 0.51 (owner): entries pinned at the front (the hands, where to stand): the rest go on after them
#else
#define s_h0 0
#endif
#if TGT_PART == 1
__attribute__((noinline))                              // (tgt2: shared by t_add and t_front, room)
#endif
static void t_del(int j) {                             // 0.61: entry j out
  while (++j < s_tn) { s_tc[j - 1] = s_tc[j]; s_tk[j - 1] = s_tk[j]; s_tkey[j - 1] = s_tkey[j]; g_tgobj[j - 1] = g_tgobj[j]; } s_tn--; }
static void t_add(int c, int kind, int key) {
  key = t_key(c, key);
  for (int j = s_h0; j < s_tn; j++) if (s_tc[j] == c) { if (key >= s_tkey[j]) return; t_del(j); break; }   // once, at its best
  int i = s_tn;
  if (i == TGT_CAP) { if (key >= s_tkey[--i]) return; } else s_tn++;   // full: the last one makes way (0.47: 8 plans at most)
  for (; i > s_h0 && s_tkey[i - 1] > key; i--) { s_tc[i] = s_tc[i - 1]; s_tk[i] = s_tk[i - 1]; s_tkey[i] = s_tkey[i - 1]; g_tgobj[i] = g_tgobj[i - 1]; }
  s_tc[i] = c; s_tk[i] = kind; s_tkey[i] = key; g_tgobj[i] = s_obj;
}
static bool open_cell(int c) { int t = G.map[c]; return PASSABLE(t) && !(TFLAGS(t) & F_AVOID); }
// 0.43 (perf): steps to reach the cell, or to stand beside it (255: no way, or too far to matter), worked out once per list
// into the path finder's queue (free once distance_map is done): the list took ~4x as long asking each cell's neighbours
#define s_w ((uint8_t *)s_queue)
static int walk(int c) { return s_w[c] == 255 ? 0x7FFF : s_w[c]; }
static void walk_map(void) {
  memset(s_w, 255, MAP_LEN);
  for (int c = 0; c < MAP_LEN; c++) {
    int d = s_dist[c]; if (d >= 255) continue;
    if (d < s_w[c]) s_w[c] = d;
    if (++d < 255) for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && d < s_w[n]) s_w[n] = d; }
  }
}
// 0.36: only cells on screen (g_view: the cells shown around the hero at rest, at least half of each: to the left, up,
// down, then (round) the radius squared / (Emery) to the right, short of the buttons' tags; all 0: no limit). A place
// off screen is offered as the farthest step on screen along the walk there.
static bool on_screen(int c) {
  int dx = (c & 31) - (G.hero.pos & 31), dy = (c >> 5) - (G.hero.pos >> 5);
  const uint8_t *v = g_view;
  return !v[0] || (dx >= -v[0] && dy >= -v[1] && dy <= v[2]
#ifdef PBL_ROUND
                   && dx <= v[0] && dx * dx + dy * dy <= v[3]
#else
                   && dx <= v[3]
#endif
                  );
}
static void t_put(int c, int kind, int key) {
  s_obj = c;
  for (int c0 = c; !on_screen(c) || (c != c0 && (c == G.entrance || c == G.exit)); ) {   // back along the walk until on screen
    int b = c;                                         // (0.41: not onto the stairs on the way, Select would take them: a
    if (on_screen(c))                                  // spot beside them as far on, else back)
      for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && on_screen(n) && open_cell(n) && n != G.entrance && n != G.exit && s_dist[n] >= s_dist[c] && s_dist[n] < 255) { b = n; break; } }
    if (b != c) { c = b; kind = TK_GO; break; }
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && s_dist[n] < s_dist[b]) b = n; }
    if (b == c) return;
    c = b; kind = TK_GO;
  }
  if (c != G.hero.pos || kind == TK_ITEM || s_zero) t_add(c, kind, key);   // (the first entry on the hero: search here)
}
// 0.40: the first entry: what a good player would do next (the deep test bot's choice of where to go, from what the
// player knows): fight, pick up, see the quest people, explore the nearest unknown, the stairs, else search for secrets
static bool s_fish; __attribute__((unused)) static bool s_foe;                           // a piranha in sight: keep out of the water; (0.45) an enemy in sight
static bool s_pick;                                    // 0.41: a pickaxe, and dark gold veins about (depths 11-15)
static bool has(int k) { return game_bag_find(k) >= 0; }
static int qty(int k) { int s = game_bag_find(k); return s < 0 ? 0 : G.hero.bag[s].qty; }
static bool unseen_by(int c) {                         // c is beside the unknown (not just beside the hero: that stays unseen)
  if (!BIT_GET(G.visited, c) || !open_cell(c) || G.map[c] == T_ALCHEMY || c == G.entrance || c == G.exit || (s_fish && IS_WATER(G.map[c]))) return false;
  for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && !BIT_GET(G.visited, n) && distance(n, G.hero.pos) > 1) return true; }
  return false;
}
static bool opening(int c) {                           // 0.47: beside the unknown straight on (a door, a hallway going on): a region to explore
  if (!unseen_by(c)) return false;                      // (not a room's corner peeking past a wall: those join every region into one)
  for (int k = 0; k < 4; k++) { int n = c + N8[k]; if (nb_ok(c, n) && !BIT_GET(G.visited, n)) return true; }
  return false;
}
static bool npc_wanted(int t) {                        // a quest person worth talking to now
  Quests *q = &G.q;
  if (t == MT_GHOST) return q->ghost == QS_SPAWNED || (q->ghost_alt == 2 && q->ghost != QS_GIVEN) || ((q->ghost == QS_GIVEN || q->ghost == QS_DONE) && has(q->ghost_alt ? IK_RAT_SKULL : IK_ROSE)) || (q->ghost == QS_DONE && q->ghost_alt == 2);
  if (t == MT_WANDMAKER) return q->wand == QS_SPAWNED || (q->wand == QS_GIVEN && has((const uint8_t[]){ IK_SEED_ROTBERRY, IK_CORPSE_DUST, IK_PHANTOM_FISH }[q->wand_alt % 3]));
  if (t == MT_BLACKSMITH) return q->smith == QS_SPAWNED || (q->smith == QS_GIVEN && (q->smith_alt ? (has(IK_PICKAXE) ? (G.hero.bag[game_bag_find(IK_PICKAXE)].flags & IF_BLOODY) : (G.hero.weapon.kind == IK_PICKAXE && (G.hero.weapon.flags & IF_BLOODY))) : qty(IK_DARK_GOLD) >= 15));
  if (t == MT_IMP) return q->imp == QS_SPAWNED || (q->imp == QS_GIVEN && qty(IK_DWARF_TOKEN) >= (q->imp_alt ? 8 : 6));
  return false;
}
static bool searched(int w) {                          // searched from beside it on this visit to the floor
  if (g_srch_t != G.level_t0) return false;
  for (int k = 0; k < 8; k++) { int n = w + N8[k]; if (nb_ok(w, n) && BIT_GET(g_srchd, n)) return true; }
  return false;
}
static bool precious(int k) { return (k == IK_SCR_UPGRADE || k == IK_POT_STRENGTH) && item_known(k); }
static bool food_k(int k) { return (unsigned)(k - IK_RATION) <= IK_CARPACCIO - IK_RATION || k == IK_OVERPRICED; }
static bool room_for(const Item *it) {                 // (two slots kept free: a full bag is the player's to sort out; 0.43: not for the Amulet)
  int n = 0; for (int i = 0; i < BAG_SIZE; i++) n += G.hero.bag[i].kind != 0;
  int k = it->kind;                                    // 0.63 (owner: a known Scroll of Upgrade left lying): what a bag of its own takes; the
  return n < BAG_SIZE - 2 || bag_region(k) >= 0 || (n < BAG_SIZE && (food_k(k) || precious(k))) ||   // last two slots: food, upgrades and strength
         it->kind == IK_GOLD || it->kind == IK_DEW || it->kind == IK_AMULET || item_cat(it->kind) == CAT_KEY || (item_stackable(it->kind) && has(it->kind));
}
static bool dropped(int c) { return c == g_drop[0] || c == g_drop[1] || c == g_drop[2] || c == g_drop[3]; }   // put down on purpose
static bool junk(const Item *it) {                     // 0.48 (owner): known to be bad: cursed, or a level below 0 (a ring's: 0 too)
  return ((it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CURSED)) || ((it->flags & IF_LEVEL_KNOWN) && (it->lvl < 0 || (item_cat(it->kind) == CAT_RING && !it->lvl)));
}
static bool dead_end(int c) {                          // 0.48 (owner): a corridor that just stops: PD's tunnels only end at doors,
  if (!BIT_GET(G.visited, c) || !open_cell(c) || c == G.entrance || c == G.exit || (g_srch_t == G.level_t0 && BIT_GET(g_srchd, c))) return false;
  int open = 0, oo = 0;                                // so a hidden one; a search there (2 turns) always finds what's beside you
  for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n)) continue; if (!BIT_GET(G.visited, n)) return false;
    int t = G.map[n]; if (t != T_WALL && t != T_WALL_DECO && t != T_SECRET_DOOR) { open++; oo += k < 4; } }
  return open == 1 && oo == 1;
}
static bool risky(const Heap *h) {                     // 0.64 (the Assist alone died to wraiths and piranhas): a tomb (wraiths) till
  Hero *hr = &G.hero; int c = h->pos;                  // well ahead of the depth; a pool room's prize (piranhas) unless levitating or
  if (h->type == HEAP_TOMB) return hr->lvl < G.depth + 3;   // invisible; a skeleton (a cursed one takes half your health) while hurt
  if (h->type == HEAP_SKELETON && hr->HP * 3 < hr->HT * 2) return true;
  for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r];
    if (R->type == RT_POOL && cx(c) >= R->left && cx(c) <= R->right && cy(c) >= R->top && cy(c) <= R->bottom) return !hr->b.levitation && !hr->b.invis; }
  return false;
}
static bool heap_ok(const Heap *h) {                   // worth picking up: not put down on purpose, not known junk, room for it
  int t = h->type;
  if (XENV("RISKY", 1) && risky(h)) return false;
  if (t >= HEAP_CARRIED || t == HEAP_FOR_SALE || !BIT_GET(G.visited, h->pos) || (TFLAGS(G.map[h->pos]) & F_AVOID) || dropped(h->pos) || ((h->pos == G.entrance || h->pos == G.exit) && !food_k(h->item.kind))) return false;   // (0.50: on the stairs a tap takes them; 0.61: food there all the same: a tap picks up first, Hero.handle)
  if (((unsigned)(t - HEAP_LOCKED_CHEST) < 2 && !(G.keys[G.depth] & 0x30)) || (t == HEAP_PLAIN && (!room_for(&h->item) || junk(&h->item)))) return false;
  return true;
}
// 0.61 (owner: "leaving food on the ground next to me while I'm starving"): hungry, none in the bag: the nearest food
// lying about (12 steps at most), whichever way the player was going
#define HUNGRY (G.hero.hunger >= 260)                  // (game.c game_hungry / game_starving, without the calls)
#define STARVING (G.hero.hunger >= 360)
static int food_near(void) {
  if (!HUNGRY) return -1;
  for (int i = 0; i < BAG_SIZE; i++) if (food_k(G.hero.bag[i].kind)) return -1;
  int best = -1, bd = 13;
  for (int i = 0; i < G.n_heaps; i++) { Heap *h = &G.heaps[i]; int c = h->pos;
    if (h->type == HEAP_PLAIN && food_k(h->item.kind) && heap_ok(h) && walk(c) < bd && (c == G.hero.pos || !char_at(c))) { bd = walk(c); best = c; } }
  return best;
}
// 0.49 (owner: "a search along a flat wall does all three nearby tiles"): a search finds whatever is hidden in the 8
// cells around you, so it's a covering problem. A wall can hide a door only with known floor on one side and the
// unknown straight across (PD's doors are orthogonal); the more unknown behind it, the likelier (a dead end's most of
// all). A spot is worth the walls it newly covers per step to get there and search: along a wall three at a time
static int wall_worth(int w) {
  if ((G.map[w] != T_WALL && G.map[w] != T_SECRET_DOOR) || searched(w)) return 0;
  int best = 0;
  for (int k = 0; k < 4; k++) {
    int a = w - N8[k], b = w + N8[k]; if (!nb_ok(w, a) || !nb_ok(w, b)) continue;
    int bx = cx(b), by = cy(b); if (bx <= 0 || by <= 0 || bx >= MAP_W - 1 || by >= MAP_H - 1) continue;
    if (!BIT_GET(G.visited, a) || BIT_GET(G.visited, b) || !open_cell(a)) continue;
    int x0 = 2 * bx - cx(w), y0 = 2 * by - cy(w), u = 0;   // (the 3x3 one further on)
    for (int y = y0 - 1; y <= y0 + 1; y++) for (int x = x0 - 1; x <= x0 + 1; x++) u += (unsigned)x < MAP_W && (unsigned)y < MAP_H && !BIT_GET(G.visited, x + y * MAP_W);
    int v = 1 + u / 3 + (dead_end(a) ? 12 : 0); if (v > best) best = v;
  }
  return best;
}
static int srch_spot(int x1, int x2) {                 // x1, x2: spots listed already
  int hp = G.hero.pos, best = -1, fb = -1; int32_t bk = 0;
  for (int c = 0; c < MAP_LEN; c++) {
    if (s_dist[c] >= 255 || !open_cell(c) || G.map[c] == T_ALCHEMY || c == G.entrance || c == G.exit || (s_fish && IS_WATER(G.map[c])) || (c != hp && char_at(c))) continue;
    if ((x1 >= 0 && distance(c, x1) <= 2) || (x2 >= 0 && distance(c, x2) <= 2)) continue;
    { int h = heap_at(c); if (h >= 0 && G.heaps[h].type == HEAP_FOR_SALE) continue; }   // (a shop's wares: Select would buy)
    int walls = 0, g = 0;
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (G.map[n] == T_WALL || G.map[n] == T_SECRET_DOOR) && !searched(n)) { walls++; g += wall_worth(n); } }
    if (!walls) continue;
    if (g) { int32_t sc = (int32_t)g * 1024 / (s_dist[c] + 2); if (sc > bk) { bk = sc; best = c; } }
    if (fb < 0 || s_dist[c] < s_dist[fb]) fb = c;     // (none with the unknown behind: the nearest wall not yet searched)
  }
  return best < 0 ? fb : best;
}
static bool floor_left(void) {                         // anything left to explore or pick up here
  for (int c = 0; c < MAP_LEN; c++) if (s_dist[c] < 255 && s_dist[c] > 0 && unseen_by(c)) return true;
  for (int i = 0; i < G.n_heaps; i++) if (heap_ok(&G.heaps[i]) && walk(G.heaps[i].pos) < 255) return true;
  return false;
}
#ifdef OVL_HOST
#include <stdio.h>
#include <stdlib.h>
#define DBG_RET(x) do { int _r = (x); if (getenv("TDBG")) printf("  first: line %d -> %d kind %d\n", __LINE__, _r, *kind); return _r; } while (0)
#else
#define DBG_RET(x) return (x)
#endif
// 0.41 (user: "make it take my previous override into account"): after the player chose another target over the first
// (main.c tg_do: g_ovr.c, from the hero's cell g_ovr.t + 65536), the first follows that way: fights and things underfoot still
// first, then the chosen cell till reached, then only what lies that way (within ~70 degrees of it, seen from the hero:
// never back); when nothing is left that way (or on another floor) the player's way ends and the bot's plan is back.
static int s_od;                                       // the player's way (pang), -1: none
#ifndef CONE
#define CONE 12
#endif
static int off(int c, int a) {                         // how far off the way a (pang) c lies, seen from the hero: 0..32
  int hp = G.hero.pos; return abs(((pang((c & 31) - (hp & 31), (c >> 5) - (hp >> 5)) - a + 32) & 63) - 32);
}
static bool ahead(int c) { return s_od < 0 || c == G.hero.pos || off(c, s_od) <= CONE; }
static bool direct(int c) {                            // no long way round, and shown (t_put: on screen) the player's way
  if (s_od < 0) return true;
  if (s_dist[c] > 2 * distance(c, G.hero.pos) + 2) return false;
  while (!on_screen(c)) { int b = c; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && s_dist[n] < s_dist[b]) b = n; } if (b == c) break; c = b; }
  return ahead(c);
}
// 0.45 (user: "humans are more prone to explore the unexplored part they can see"): what's on screen first: the spots
// beside the unknown and the tall grass, nearest first, those ahead (the heading) before the rest; grass
// and spots alike, so the grass on the way is trampled, not left behind. all (0.43: room in the dew vial; 0.41: before the
// stairs): all the grass seen. Grass skipped by the player (another target chosen over it): not again until a kill.
static int look(bool all) {
  int best = -1, bk = 0x7FFF, gb = 0; bool gr = !(g_ovr.on & 4);   // (an enemy to fight came first; one asleep or in the water doesn't stop it)
  for (int c = 0; c < MAP_LEN; c++) {
    int d = s_dist[c];
    if (d >= 255 || !d || !ahead(c)) continue;
    bool k = c == g_goal;                              // 0.61 (owner: "constantly doubling back and forth"): the one chosen before, even
    bool g = gr && G.map[c] == T_HIGH_GRASS && (all || on_screen(c) || k) && !char_at(c) && heap_at(c) < 0;   // (a tap on a heap opens / picks it up)
    if (!g && !((on_screen(c) || k) && unseen_by(c))) continue;   // once the way there takes it off screen (else the plan out of sight, back, and again)
    if (k) d = 0;
    else { if (s_od < 0 && s_head >= 0 && off(c, s_head) > 16) d += 256;   // (behind: after those ahead; off screen: after all on it)
      if (!on_screen(c)) d += 512; }
    if (d < bk) { bk = d; best = c; gb = g; }
  }
  g_ovr.on |= gb << 1;                                 // (entry 0 is grass: skipped if another is chosen)
  if (best >= 0) g_goal = best;
  return best;
}
static bool foul(int c) {                               // 0.64: toxic gas, fire or confusion gas in this cell
  for (int b = BLOB_TOXIC; b <= BLOB_CONFUSION; b++) if (b != BLOB_REGROWTH && g_blob[b] && g_blob[b][c]) return true;
  return false;
}
static int first(int *kind) {                          // the cell (on the map, maybe off screen) and its kind; -1: none
  int hp = G.hero.pos, best = -1, bk = 0x7FFF, bi = 0;
  g_ovr.on &= ~2;
  if ((uint8_t)(G.hero.exp + G.hero.lvl * 101) != g_ovr.gk) g_ovr.on &= ~4;   // (a kill: more experience)
  for (int i = 0; i < G.n_mobs; i++) {                 // fight: a boss beside you, the weakest beside you, else the nearest
    Mob *m = &G.mobs[i]; int fl = MOB_DEFS[m->type].flags;
    if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos)) continue;
    bool adj = adjacent(m->pos, hp);
    if (!adj && G.tgm.vs == 1 && G.tgm.vu == m->uid && G.now < G.tgm.vt && XENV("VETO", 1)) continue;   // 0.66: the player went another way than at it: not plan A a while
    if (adj) g_ovr.d &= ~64;                           // (beside one: a step back may hold again)
    if ((m->state == ST_PASSIVE && (!adj || m->type == MT_STATUE)) || ((fl & MD_WATER) && !adj)) continue;
    if (!adj && walk(m->pos) > 2 * distance(m->pos, hp) + 4) continue;   // 0.41: sensed through a wall (the Huntress): a long way round
    if (m->state == ST_SLEEPING && !adj && !(fl & MD_BOSS) && floor_left() && distance(m->pos, hp) > 2) continue;   // 0.48 (owner): sleepers once the floor is explored (0.50: one close by: the sure first hit)
    int k = adj ? ((fl & MD_BOSS) ? 0 : 1 + m->HP) : walk(m->pos) < 255 ? 1000 + walk(m->pos) * 64 + m->HP : 0x7FFF;
    if (k < bk) { bk = k; best = m->pos; bi = i; }
  }
  int fd = food_near();
  if (fd >= 0 && bk >= 1000 && (best < 0 || walk(fd) <= 3) && ahead(fd)) { *kind = TK_ITEM; DBG_RET(fd); }   // 0.61: hungry: food first (a few steps off: before an enemy not yet beside you)
  if (best >= 0) {
    if (!g_assist) { *kind = TK_ENEMY; DBG_RET(best); }   // 0.51 Assist Low: just the fight
    // 0.41 (user: doorway fighting): Select waits a turn (main.c tg_do; entry 0 stays the enemy) for one a step short of
    // you that is coming at you; and once just after the hero stepped back from beside it (whatever it does: then on to it)
    Mob *m = &G.mobs[bi]; int t = m->type, w = walk(best);
    { int adj = 0, os = 0, bc = -1, bo = 9;               // 0.50 (owner): two or more beside you in the open: back into a narrow spot none touches
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && adjacent(G.mobs[i].pos, hp)) adj++;
      for (int k = 0; k < 4; k++) os += nb_ok(hp, hp + N8[k]) && open_cell(hp + N8[k]);
      if (adj >= 2 && os > 2) for (int k = 0; k < 8; k++) { int c = hp + N8[k], o = 0; if (!nb_ok(hp, c) || !open_cell(c) || char_at(c) || IS_WATER(G.map[c])) continue;
        bool touch = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && adjacent(G.mobs[i].pos, c)) touch = true;
        for (int j = 0; j < 4; j++) o += nb_ok(c, c + N8[j]) && open_cell(c + N8[j]);
        if (!touch && o <= 2 && o < bo) { bo = o; bc = c; } }
      if (bc >= 0) { *kind = TK_GO; DBG_RET(bc); } }
    bool fast = MOB_DEFS[t].move_cost * 2 <= hero_move_cost() && !G.hero.b.roots;   // 0.49 (owner): twice your pace (crabs, bats)
    if (bk >= 1000 && (w == 2 || (fast && w == 3)) && m->state == ST_HUNTING && !m->b.paralysis && !m->b.roots && !m->b.frost && !m->b.terror &&
        t != MT_SHAMAN && t != MT_WARLOCK && t != MT_EYE && t != MT_SCORPIO && t != MT_TENGU && t != MT_BURNING_FIST &&
        !(MOB_DEFS[t].flags & MD_IMMOVABLE)) {
      if (fast && w == 2) {                            // waiting, it closes and strikes in one turn; a step back, it spends both
        int bc = -1;                                   // moves closing and you strike first (no other one near there)
        for (int k = 0; k < 8; k++) { int c = hp + N8[k]; if (!nb_ok(hp, c) || !open_cell(c) || char_at(c) || IS_WATER(G.map[c]) || distance(c, best) < 3) continue;
          bool near = false; for (int j = 0; j < G.n_mobs; j++) if (j != bi && G.mobs[j].HP > 0 && !ally(j) && G.mobs[j].state == ST_HUNTING && distance(G.mobs[j].pos, c) <= 2) near = true;
          if (!near) { bc = c; break; } }
        if (bc >= 0) { *kind = TK_GO; DBG_RET(bc); }
      } else g_ovr.d |= 128;
    }
    else if (bk >= 1000 && w <= 3 && g_from >= 0 && distance(g_from, best) <= 1 && !(g_ovr.d & 64)) g_ovr.d |= 128 | 64;
    if (bk >= 1000 && m->state == ST_HUNTING && XENV("FOULWAIT", 1)) for (int k = 0; k < 8; k++) { int n = hp + N8[k]; if (nb_ok(hp, n) && foul(n)) g_ovr.d |= 128; }   // 0.64: fire or gas between: let it come through it
    if (bk < 1000 && (MOB_DEFS[t].flags & MD_WATER) && (XENV("FISH", 1) || IS_WATER(G.map[hp]))) {   // a piranha beside you: out of its reach, the shortest way (0.64: from the
      int c0 = -1, d0 = 0x7FFF;                        // shore too: the Assist alone lost 1 game in 7 to them on the first floors)
      for (int c = 0; c < MAP_LEN; c++) if (s_dist[c] < d0 && s_dist[c] > 0 && open_cell(c) && c != G.entrance && c != G.exit) {
        bool wet = false; for (int k = -1; k < 8; k++) { int n = k < 0 ? c : c + N8[k]; if (k < 0 || nb_ok(c, n)) wet |= IS_WATER(G.map[n]); }
        if (!wet) { d0 = s_dist[c]; c0 = c; } }
      if (c0 >= 0) { *kind = TK_GO; DBG_RET(c0); }
    }
    *kind = TK_ENEMY; DBG_RET(best);
  }
  bool amulet = has(IK_AMULET);
again:
  *kind = TK_ITEM; bk = 0x7FFF;                        // things worth picking up (underfoot first), the nearest
  for (int i = 0; i < G.n_heaps; i++) {
    Heap *h = &G.heaps[i]; int c = h->pos;
    if (!heap_ok(h) || G.map[c] == T_ALCHEMY || (c != hp && char_at(c)) || (s_fish && IS_WATER(G.map[c]))) continue;   // (0.48: not junk you know of)
    if (h->item.kind == IK_DEW && G.hero.HP >= G.hero.HT && (G.dew >= 10 || !has(IK_DEW_VIAL))) continue;   // 0.41 (user): Dewdrop.doPickUp would do nothing
    int d = c == hp ? 0 : walk(c);
    if (d < bk && (ahead(c) || precious(h->item.kind) || (!XENV("OBEY", 1) && d <= 4))) { bk = d; best = c; }   // (0.63: the player's way: still upgrades; 0.65 (owner: "if I want to explore a hallway but left a dewdrop behind, it shouldn't turn me around"): nothing else behind)
  }
  if (best >= 0) DBG_RET(best);
  *kind = TK_GO;
  if (g_ovr.c >= 0 && g_ovr.c < MAP_LEN) {             // the cell the player chose
    int c = g_ovr.c, t = G.map[c];                      // 0.47: an objective from the list: plan A till met, then the plan again
    if ((g_ovr.on & 8) && !unseen_by(c) && heap_at(c) < 0 && mob_at(c) < 0 && c != G.exit && c != G.entrance && t != T_WELL && t != T_ALCHEMY && t != T_LOCKED_DOOR && t != T_WALL_DECO) { g_ovr.c = -1; g_ovr.on &= ~8; s_od = -1; goto again; }
    if (walk(c) < 255) DBG_RET(c);
    g_ovr.c = MAP_LEN; }

  for (int i = 0; i < G.n_mobs; i++) {                 // the quest people
    Mob *m = &G.mobs[i];
    if (m->HP > 0 && BIT_GET(G.fov, m->pos) && npc_wanted(m->type) && walk(m->pos) < bk && ahead(m->pos)) { bk = walk(m->pos); best = m->pos; }
  }
  if (best >= 0) DBG_RET(best);
  int keys = G.keys[G.depth];
  for (int c = 0; c < MAP_LEN; c++) {                  // a well worth a drink, a door to unlock, the rotberry to tread on
    if (!BIT_GET(G.visited, c) || walk(c) >= bk || !ahead(c)) continue;
    int t = G.map[c];
    if ((t == T_WELL && c == G.well_pos && (G.well_kind == 0 || (G.well_kind == 1 && G.hero.HP < G.hero.HT * 2 / 3))) || (t == T_LOCKED_DOOR && (keys & 15))) { bk = walk(c); best = c; }
  }
  if (G.q.wand_depth == G.depth && G.q.wand == QS_GIVEN && G.q.wand_alt % 3 == 0 && !has(IK_SEED_ROTBERRY))
    for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 7 && BIT_GET(G.visited, G.plants[i].pos) && walk(G.plants[i].pos) < bk && ahead(G.plants[i].pos)) { bk = walk(G.plants[i].pos); best = G.plants[i].pos; }
  // 0.41 (user: "does it know the quests?"): the blacksmith's dark gold: the nearest known vein (TK_VEIN: Select walks
  // beside it; TK_MINE, beside it: mines, main.c tg_do, with the pickaxe g_ovr.ms); the wandmaker's phantom fish: search from each pool
  // cell in turn (hidden in the water: a search finds it beside you)
  Quests *q = &G.q; int mk = TK_GO;
  bool gold = q->smith_depth == G.depth && q->smith == QS_GIVEN && !q->smith_alt && s_pick && qty(IK_DARK_GOLD) < 15;
  bool fish = q->wand_depth == G.depth && q->wand == QS_GIVEN && q->wand_alt % 3 == 2 && !has(IK_PHANTOM_FISH) && !s_fish;
  for (int c = 0; c < MAP_LEN && (gold || fish); c++) {
    if (!ahead(c) || !BIT_GET(G.visited, c)) continue;
    if (fish && s_dist[c] < bk && IS_WATER(G.map[c]) && !searched(c) && !(g_srch_t == G.level_t0 && BIT_GET(g_srchd, c)) && (c == hp || !char_at(c))) { bk = s_dist[c]; best = c; mk = TK_GO; }
    if (gold && G.map[c] == T_WALL_DECO && walk(c) < bk) { bk = walk(c); best = c; mk = bk == 1 ? TK_MINE : TK_VEIN; }
  }
  *kind = mk;
  if (best >= 0) DBG_RET(best);
  int st = amulet ? G.entrance : G.exit;
  bool way = BIT_GET(G.visited, st) && walk(st) < 255 && (G.map[st] != T_LOCKED_EXIT || (keys & 64));   // 0.61: the way on is known
  if (!way) { int d0 = 9; for (int c = 0; c < MAP_LEN; c++) if (walk(c) < d0 && dead_end(c) && ahead(c)) { d0 = walk(c); best = c; }   // 0.48 (owner): a dead end near: search it on the way (0.61 (owner): not once the way on is known)
    if (best >= 0) { *kind = TK_GO; DBG_RET(best); } }
  if ((best = look(has(IK_DEW_VIAL) && G.dew < 10)) >= 0) DBG_RET(best);   // 0.45: on screen first, then the plan out of sight
  if (!amulet || !BIT_GET(G.visited, st) || walk(st) >= 255) {   // (with the Amulet: only to find the way up)
  //                                    // explore: the nearest spot beside the unknown; the one chosen before
    int g = g_goal;                                    // while it still is one and can be reached (no dithering between two;
    if (g >= 0 && g < MAP_LEN && s_dist[g] < 255 && s_dist[g] > 0 && unseen_by(g) && ahead(g) && direct(g)) best = g;   // 0.45: those on
    else for (int p = 0; p < 2 && best < 0; p++)      // screen came first, look); the player's way: first those the walk doesn't double back to
      for (int c = 0; c < MAP_LEN; c++) if (s_dist[c] < bk && s_dist[c] > 0 && unseen_by(c) && ahead(c) && (p || direct(c))) { bk = s_dist[c]; best = c; }
    if (best >= 0) { g_goal = best; DBG_RET(best); }
  }
  for (int c = 0; c < MAP_LEN; c++) {                  // a known trap the only way on (a corridor): over it, the nearest
    int t = G.map[c];
    if (!BIT_GET(G.visited, c) || (TFLAGS(t) & (F_AVOID | F_PIT | F_SOLID)) != F_AVOID || walk(c) >= bk || !ahead(c)) continue;   // (traps aren't passable: avoid)
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (!BIT_GET(G.visited, n) || (open_cell(n) && s_dist[n] >= 255))) { bk = walk(c); best = c; break; } }
  }
  if (best >= 0) DBG_RET(best);
  if (s_od >= 0) { g_ovr.c = -1; s_od = -1; goto again; }   // nothing left the player's way: the plan again
  if ((best = look(true)) >= 0) DBG_RET(best);        // the rest of the grass before the stairs (after exploring: what lies on is worth more)
  if (st == hp) *kind = TK_ITEM;                       // (on the stairs: Select takes them, not a search)
  if (way) DBG_RET(st);                                // the stairs (0.61 (owner): no more "the quest: look around a while longer first": it searched walls)
  if (G.map[st] == T_LOCKED_EXIT && !(keys & 64)) {   // the boss's key: the boss, the iron key to its arena, the key itself
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[i].pos)) DBG_RET(G.mobs[i].pos);
    for (int i = 0; i < G.n_heaps; i++) { int c = G.heaps[i].pos, k = G.heaps[i].item.kind; if ((k == IK_SKELETON_KEY || (k == IK_IRON_KEY && !(keys & 15))) && c < MAP_LEN && BIT_GET(G.visited, c) && walk(c) < 255) { *kind = TK_ITEM; DBG_RET(c); } }   // (0.41: a pick-up, not a search there)
  }
  if (s_fish) { s_fish = false; goto again; }          // only water left, piranhas about: into it all the same (0.45: the plan again)
  G.tgm.srch = 1;                                      // search for secret doors: where, tgt2 works out (0.63: srch_spot moved there, room)
  DBG_RET(hp);                                         // (nothing at all: search here)
}
// 0.47 (owner: "read my mind"): two steps or more nearer one of the list's objectives (on the touch screen, say), and
// not one nearer plan A's: that objective is plan A now, as if chosen from the list (a single step isn't enough). The
// steps count from where the list was last taken (G.tgm): taken again when plan A comes nearer or nothing does
static __attribute__((noinline)) int mind(int hp) {
#ifdef OVL_HOST
  if (!getenv("MIND")) return -1;                     // (the test bot walks its own way: only when asked)
#endif
  struct Tgm *M = &G.tgm; int pr = -1, up = 0;
  if (M->t0 == G.level_t0 && M->hp != hp)
    for (int j = 0; j < M->n; j++) {
      int c = M->c[j], g = M->d[j] - ((unsigned)c < MAP_LEN ? walk(c) : 255);
      if (g > 0) { if (!j) break; up = 1; if (g >= TGT_MIND) { pr = c; up = 0; break; } }
    }
  if (!up) {                                           // the list as it is now
    M->t0 = G.level_t0; M->hp = hp; M->n = s_tn;
    for (int i = 0; i < s_tn; i++) { int c = s_tk[i] == TK_ENEMY ? -1 : g_tgobj[i];   // (an enemy moves: never one)
      M->c[i] = c; M->d[i] = (unsigned)c < MAP_LEN ? walk(c) : 255; }
  }
  return pr;
}
#if TGT_PART == 1
static int escape(void) {                              // 0.64 (moved from first(): room): the hero in gas or fire, or eaten by Goo's ooze
  int hp = G.hero.pos, best = -1, bk = 0x7FFF;
  if (XENV("FOUL", 1) && (foul(hp) || G.hero.b.burning)) {   // 0.64 (the Assist alone died in the gas of a potion it had drunk to learn it):
    bool w = G.hero.b.burning;                         // out of the gas / fire, the nearest clean cell (alight: water, if it's near)
    for (int p = 0; p < 2 && best < 0; p++, w = false)
      for (int c = 0; c < MAP_LEN; c++) if (s_dist[c] < bk && s_dist[c] > 0 && (!w || s_dist[c] <= 4) && open_cell(c) && !foul(c) && !char_at(c) && (!w || IS_WATER(G.map[c]))) { bk = s_dist[c]; best = c; }
    if (best >= 0) { return best; }
    bk = 0x7FFF; }
  if (G.hero.ooze && !IS_WATER(G.map[hp]) && XENV("OOZE", 1)) {   // 0.64: Goo's caustic ooze eats 20 turns at you: water 2 steps off washes it
    for (int c = 0; c < MAP_LEN; c++) if (s_dist[c] <= 2 && s_dist[c] > 0 && IS_WATER(G.map[c]) && !char_at(c) && s_dist[c] < bk) { bk = s_dist[c]; best = c; }
    if (best >= 0) { return best; }
    bk = 0x7FFF; }
  return -1;
}
#endif
#if TGT_PART == 1
// 0.50 (the night of 10-13: the solver bot's lessons, for the feet): where to stand. -1: nothing special.
// The rotting fist mends 4 a turn in water: meet it on dry ground. The burning fist sets all round it alight: fight it
// from the water beside it. Evil eyes and scorpios shoot along a line (Tengu's arena has no corner: no use): out of it, round a corner, and they come
// to you blind. DM-300 repairs itself on every dead trap it steps on: wait for it where there's none on or beside you.
// (owner) the door trick: a creature coming for you from afar, a door between: through it and wait beside it, out of
// its sight; it comes through blind and the first blow is yours.
static bool t_near(int c, int t) {                     // on or beside c: water (t -1), else that terrain (0.61: one helper, room)
  for (int k = -1; k < 8; k++) { int n = k < 0 ? c : c + N8[k]; if (k >= 0 && !nb_ok(c, n)) continue; int m = G.map[n]; if (t < 0 ? IS_WATER(m) : m == t) return true; }
  return false; }
#define wet_near(c) t_near(c, -1)
#define dead_trap_near(c) t_near(c, T_INACTIVE_TRAP)
static bool stand_ok(int c, int hp) { return c == hp || (open_cell(c) && !char_at(c) && walk(c) <= 6); }
static int tactics(void) {
  Hero *h = &G.hero; int hp = h->pos; bool hit;
  if (h->b.roots) return -1;
  Mob *bf = 0, *rf = 0, *dm = 0, *sh = 0, *ki = 0; int kd = 99;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->type == MT_GOO && m->HP > 0 && m->aux2 && BIT_GET(G.fov, m->pos) && XENV("GOOPUMP", 1)) {
      // 0.64 (the Assist alone's first killer once past floor 4): Goo pumped up hits 3x, and reaches 2 cells: out of its reach
      // (farther than 2, or round a corner) and let it waste it; if there's no such spot near, fight (Goo.attack)
      if (distance(m->pos, hp) > 2 || ballistica(m->pos, hp, &hit) != hp) return hp;
      int best = -1, bk = 99;
      for (int c = 0; c < MAP_LEN; c++) if (walk(c) <= 2 && walk(c) < bk && stand_ok(c, hp) && (distance(m->pos, c) > 2 || ballistica(m->pos, c, &hit) != c)) { bk = walk(c); best = c; }
      return best; } }
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; int t = m->type; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos)) continue;
    int d = distance(m->pos, hp); if (d < 2 && t != MT_ROTTING_FIST && t != MT_BURNING_FIST) return -1;   // (one beside you: fight it)
    if (t == MT_BURNING_FIST) bf = m; else if (t == MT_ROTTING_FIST) rf = m; else if (t == MT_DM300) dm = m;
    else if ((t == MT_EYE || t == MT_SCORPIO || t == MT_ACIDIC || t == MT_SHAMAN || t == MT_WARLOCK) && m->state == ST_HUNTING) { if (!sh || d < distance(sh->pos, hp)) sh = m; }   // (0.63 (owner): shamans and warlocks too)
    else if (m->state == ST_HUNTING && !(MOB_DEFS[t].flags & (MD_BOSS | MD_IMMOVABLE | MD_WATER | MD_NPC)) && MOB_DEFS[t].move_cost * 2 > hero_move_cost() && t != MT_SHAMAN && t != MT_WARLOCK && t != MT_MIMIC && t != MT_STATUE && d < kd) { kd = d; ki = m; }
    else if (m->state != ST_HUNTING && m->state != ST_PASSIVE && !(MOB_DEFS[t].flags & MD_NPC)) kd = -1;   // (another one about: no tricks)
  }
  int best = -1, bk = 0x7FFF;
#ifdef OVL_HOST
  if (getenv("TACT_NOKITE")) ki = 0; if (getenv("TACT_NODM")) dm = 0; if (getenv("TACT_NOSH")) sh = 0;   // (A/B parts)
#endif
  if (rf && distance(rf->pos, hp) <= 3 && wet_near(hp)) {
    for (int c = 0; c < MAP_LEN; c++) if (c != hp && stand_ok(c, hp) && !wet_near(c) && walk(c) < bk) { bk = walk(c); best = c; }
    return best; }
  if (bf && !(rf && distance(rf->pos, hp) <= 3) && !IS_WATER(G.map[hp])) {
    for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && stand_ok(c, hp) && distance(c, bf->pos) <= 1 && walk(c) < bk) { bk = walk(c); best = c; }
    if (best >= 0) return best; }
  if (sh && ballistica(sh->pos, hp, &hit) == hp) {    // (0.63: not into water: a shaman's bolt does half again there; and wait there, below)
    for (int c = 0; c < MAP_LEN; c++) if (c != hp && walk(c) <= 5 && stand_ok(c, hp) && !IS_WATER(G.map[c]) && walk(c) < bk && ballistica(sh->pos, c, &hit) != c) { bk = walk(c); best = c; }
    if (best >= 0) { G.tgm.tr = best; G.tgm.trt = G.now + (distance(sh->pos, best) + 4) * TICK; }
    return best; }
  if (dm) {
    if (!dead_trap_near(hp)) return hp;                // (hold: let it come)
    for (int c = 0; c < MAP_LEN; c++) if (c != hp && stand_ok(c, hp) && !dead_trap_near(c) && walk(c) < bk) { bk = walk(c); best = c; }
    return best; }
  if (ki && kd >= 3) {                                 // the door trick
    struct Tgm *M = &G.tgm;
    for (int D = 0; D < MAP_LEN; D++) { int t = G.map[D]; if ((t != T_DOOR && t != T_OPEN_DOOR) || walk(D) > 5 || walk(D) >= distance(ki->pos, D)) continue;
      for (int k = 0; k < 8; k++) { int A = D + N8[k]; if (!nb_ok(D, A) || !stand_ok(A, hp) || G.map[A] == T_DOOR || G.map[A] == T_OPEN_DOOR || IS_WATER(G.map[A]) || distance(A, ki->pos) <= distance(D, ki->pos) || ballistica(ki->pos, A, &hit) == A) continue;
        bool known = true; for (int j = 0; j < 8; j++) { int n = A + N8[j]; if (nb_ok(A, n) && !BIT_GET(G.visited, n)) known = false; } if (!known) continue;
        int w = walk(A) - (A == M->tr) * 3; if (w < bk) { bk = w; best = A; } } }   // (0.61: the spot chosen before, unless another is 3 nearer)
    if (best >= 0) { M->tr = best; M->trt = G.now + (kd + 4) * TICK; }
    return best; }
  // 0.61 (owner: "constantly doubling back and forth"): there, it's out of sight (that's the trick): wait for it a while,
  // not back out into its view and in again; and a wait, not a search (main.c tg_do: bit 7)
  if (G.tgm.tr == hp && G.now < G.tgm.trt && kd > 0) return hp;
  return -1;
}
#endif
#if TGT_PART == 2
// 0.48 test kit (0.51: Assist High in either build) (0.49: in tgt2, 0.63: tgt3, room) (owner: judge the bot by mashing Select): the bot's hands as plan A: eat once starving, a known Potion of
// Healing when a fight has you under a quarter, a better identified weapon or armour, rest when hurt with nothing about
static int16_t s_aim;                                 // 0.50: the cell an aimed hand (zap, throw) is for
#define DECL(k, a) (g_ovr.dd == G.depth && g_ovr.dk == (k) && g_ovr.da == (a))   // 0.53 (owner): passed over: not again till the stairs are plan A or the next floor
static int hands(void) {
  Hero *h = &G.hero; int hp = h->pos; bool near = false, seen = false, adj = false; int hard = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & MD_NPC)) continue; seen = true;
    int d = distance(m->pos, hp); if (d <= 3) near = true; if (d <= 1) adj = true;
    hard += (MOB_DEFS[m->type].flags & MD_BOSS) ? 2 : m->state == ST_HUNTING; }   // (a boss, or two coming for you: a hard fight)
  g_ovr.hs = -1;                                       // (G.tgm.pk stays till a selector opens: reading may refresh the list first)
  if (XENV("FOUL", 1) && (foul(hp) || h->b.burning)) return -1;   // 0.64: out of the gas / fire first (first())
  // 0.61 (owner): hunger stops rising at Starving, so the turns spent there cost no food (just ~1 HP a 33 turns, and no
  // regeneration): eat only once starving and under 80% health (EATHP), or to go into a hard fight fed; never with one within 3
  // 0.64: the PC tested it (food2, 1,152 games each): eating only under 80% cost half a floor (-0.51 +/- 0.09: starving,
  // there's no regeneration, and the fights go worse); eating as soon as Starving is as good as eating Hungry (+0.02)
  bool eat = STARVING && !near && (XENV("EATHP", 101) * h->HT > 100 * h->HP || hard >= 2);
  // (owner) a warrior near death in a fight, no potion of healing known and none beside him: a meal is 5 HP (3 turns)
  int s = game_bag_find(IK_POT_HEALING); bool pot = s >= 0 && item_known(IK_POT_HEALING);
  bool war = seen && !adj && (h->hclass & 3) == 0 && h->HP * 4 < h->HT && !pot;
  // 0.64 (owner: "I don't want to starve to death because I ignored food suggestion once"): a meal passed over comes
  // back once health falls under half, and again under a quarter (two nudges; past that the player has a reason)
  int band = h->HP * 4 < h->HT ? 2 : h->HP * 2 < h->HT;
  bool passed = g_ovr.dd == G.depth && g_ovr.da == ACN_EAT;
  if (!passed) G.tgm.eb = 0;
  else if (!G.tgm.eb) G.tgm.eb = band + 1;              // (the band it was passed over in)
  else if (band + 1 > G.tgm.eb) { passed = false; g_ovr.dd = -1; G.tgm.eb = 0; }
  for (int i = 0; i < BAG_SIZE; i++) {
    int k = h->bag[i].kind;
    if (food_k(k) && (eat || war) && (k != IK_MYSTERY_MEAT || (g_ovr.hs < 0 && !war)) && !passed) { g_ovr.hs = i; g_ovr.ha = ACN_EAT; }   // (0.63: passed over: not again this floor, 0.64: but for those two)
  }
  if (g_ovr.hs >= 0) return TK_BAG;
  if (seen && h->HP * 4 < h->HT && pot) { g_ovr.hs = s; g_ovr.ha = ACN_DRINK; return TK_BAG; }
  // 0.64 (the Assist alone died carrying 2.4 unknown potions, a healing one among them every other time): beside one,
  // under a quarter, no known healing: drink an unknown potion (healing is the likeliest), else read an unknown scroll
  if (XENV("ESCAPE", 1) && adj && h->HP * 4 < h->HT && !pot) {   // 0.64: dying beside one, a known way out first (before the gamble)
    static const uint8_t ESC[6] = { IK_SCR_TELEPORT, IK_POT_INVIS, IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_MIRROR, IK_SCR_PSIONIC };
    for (int j = 0; j < 6; j++) { int k = ESC[j]; if ((s = game_bag_find(k)) >= 0 && item_known(k) && (item_cat(k) == CAT_POTION || !h->b.blind)) { g_ovr.hs = s; g_ovr.ha = item_cat(k) == CAT_POTION ? ACN_DRINK : ACN_READ; return TK_BAG; } } }
  if (XENV("GAMBLE", 1) && adj && h->HP * 4 < h->HT && !pot) for (int c = CAT_POTION; c <= CAT_SCROLL; c++) for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind;
    if (item_cat(k) == c && !item_known(k) && (c == CAT_POTION || !h->b.blind)) { g_ovr.hs = i; g_ovr.ha = c == CAT_POTION ? ACN_DRINK : ACN_READ; return TK_BAG; } }
  if (seen) {                                          // 0.50 (the solver bot): from afar: a full wand, or any charge when hurt; the Huntress's boomerang
    Mob *e = 0; int ed = 99; bool hit;               // every turn (it comes straight back); darts and such at one 3+ away
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state != ST_HUNTING || (MOB_DEFS[m->type].flags & MD_NPC)) continue; int d = distance(m->pos, hp); if (d < ed && ballistica(hp, m->pos, &hit) == m->pos) { ed = d; e = m; } }
    int fs = -1; if (XENV("FLOCK", 1)) for (int sl = 0; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl);
      if (it->kind == IK_WND_FLOCK && item_known(IK_WND_FLOCK) && (!(it->flags & (IF_LEVEL_KNOWN | IF_CHARGE_KNOWN)) || it->ench > 0)) fs = sl; }
    if (fs >= 0) {                                     // 0.66 (owner): a Wand of Flock's sheep for cover: beside one, under 40%; or a shaman / warlock
      int a = -1; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && m->state == ST_HUNTING && adjacent(m->pos, hp)) a = m->pos; }   // shooting at you
      if (a >= 0 && h->HP * 5 < h->HT * 2) { g_ovr.hs = fs; g_ovr.ha = ACN_ZAP; s_aim = a; return TK_BAG; }
      if (e && ed >= 2 && (e->type == MT_SHAMAN || e->type == MT_WARLOCK)) { g_ovr.hs = fs; g_ovr.ha = ACN_ZAP; s_aim = e->pos; return TK_BAG; } }
    if (!e || ed < 2) return -1;
    int t = e->type; bool shooter = t == MT_TENGU || t == MT_EYE || t == MT_SCORPIO || t == MT_ACIDIC;
    if (t == MT_SHAMAN || t == MT_WARLOCK) return -1;   // 0.63 (owner): those want melee, from round a corner (tactics), not a wand duel
    for (int q = 0; q < 2; q++) { int k = h->quick[q], sl = k ? game_bag_find(k) : -1; if (sl < 0) continue; Item *it = game_slot(sl); int c = item_cat(k);   // 0.54 (owner): what's in a quickslot first, and more readily
      if (c == CAT_MISSILE && !shooter && k != IK_BOOMERANG) { g_ovr.hs = sl; g_ovr.ha = ACN_THROW; s_aim = e->pos; return TK_BAG; }
      if (c == CAT_WAND && it->ench > 0 && item_known(k) && k != IK_WND_TELEPORT && k != IK_WND_REGROWTH && k != IK_WND_BLINK && k != IK_WND_REACH && k != IK_WND_FLOCK && k != IK_WND_AMOK
          && !((k == IK_WND_LIGHTNING || k == IK_WND_AVALANCHE) && ed < 3) && !(k == IK_WND_FIREBOLT && (TFLAGS(G.map[hp]) & F_FLAMABLE))) { g_ovr.hs = sl; g_ovr.ha = ACN_ZAP; s_aim = e->pos; return TK_BAG; } }
    for (int sl = 0; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); int k = it->kind;
      if (k < IK_FIRST_WAND || k > IK_WND_AVALANCHE + 0 || !item_known(k) || it->ench <= 0) { if (k != IK_WND_MM || it->ench <= 0) continue; }
      if (k == IK_WND_TELEPORT || k == IK_WND_REGROWTH || k == IK_WND_BLINK || k == IK_WND_REACH || k == IK_WND_FLOCK || k == IK_WND_AMOK) continue;
      if ((k == IK_WND_LIGHTNING || k == IK_WND_AVALANCHE) && ed < 3) continue;   // (owner: never point blank)
      if (k == IK_WND_FIREBOLT && (TFLAGS(G.map[hp]) & F_FLAMABLE)) continue;
      if (it->ench >= wand_max(it) || h->HP * 2 < h->HT) { g_ovr.hs = sl; g_ovr.ha = ACN_ZAP; s_aim = e->pos; return TK_BAG; } }
    if (XENV("UNKWAND", 1)) for (int sl = 0; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl);   // 0.64: an unknown wand
      if (item_cat(it->kind) == CAT_WAND && !item_known(it->kind) && (!(it->flags & (IF_LEVEL_KNOWN | IF_CHARGE_KNOWN)) || it->ench > 0)) { g_ovr.hs = sl; g_ovr.ha = ACN_ZAP; s_aim = e->pos; return TK_BAG; } }   // at it: that's how it's learnt
    for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind;   // 0.64: a known harmful potion at one coming from afar (gas: 4+ away)
      if (!XENV("HARMTHROW", 1)) break;
      if ((k == IK_POT_TOXIC || k == IK_POT_PARALYTIC) ? ed >= 4 : (k == IK_POT_FLAME || k == IK_POT_FROST) && ed >= 3 && !(TFLAGS(G.map[hp]) & F_FLAMABLE))
        if (item_known(k)) { g_ovr.hs = i; g_ovr.ha = ACN_THROW; s_aim = e->pos; return TK_BAG; } }
    if (ed >= 3 && XENV("SEEDTHROW", 1)) for (int sl = 0; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { int k = game_slot(sl)->kind;   // 0.64: Sorrowmoss
      if (k == IK_SEED_SORROWMOSS || k == IK_BOMB || (k == IK_SEED_FIREBLOOM && !(TFLAGS(G.map[hp]) & F_FLAMABLE))) { g_ovr.hs = sl; g_ovr.ha = ACN_THROW; s_aim = e->pos; return TK_BAG; } }   // (poison), Firebloom, a bomb at one coming
    if ((h->hclass & 3) == 3 && !shooter && (s = game_bag_find(IK_BOOMERANG)) >= 0) { g_ovr.hs = s; g_ovr.ha = ACN_THROW; s_aim = e->pos; return TK_BAG; }
    if (ed >= 3 && !shooter) for (int sl = 0; sl < BAG_SIZE; sl++) { int k = h->bag[sl].kind; if (k == IK_DART || k == IK_JAVELIN || k == IK_SHURIKEN || k == IK_TOMAHAWK) { g_ovr.hs = sl; g_ovr.ha = ACN_THROW; s_aim = e->pos; return TK_BAG; } }
    return -1; }
  for (int k = IK_POT_STRENGTH; k; k = k == IK_POT_STRENGTH ? IK_POT_MIGHT : k == IK_POT_MIGHT ? IK_POT_EXPERIENCE : 0)   // 0.50 (0.64: Might and Experience too)
    if ((s = game_bag_find(k)) >= 0 && item_known(k) && !DECL(k, ACN_DRINK)) { g_ovr.hs = s; g_ovr.ha = ACN_DRINK; return TK_BAG; }
  if (!h->b.blind && (s = game_bag_find(IK_SCR_UPGRADE)) >= 0 && item_known(IK_SCR_UPGRADE) && !DECL(IK_SCR_UPGRADE, ACN_READ)) { G.tgm.pk = h->armor.kind ? 6 : 7; g_ovr.hs = s; g_ovr.ha = ACN_READ; return TK_BAG; }   // (0.64: the selector opens on the armour: upgrades there first)   // 0.50: an upgrade (the selector asks what for)
  if (!h->b.blind && XENV("UTILSCR", 1)) {               // 0.64: the known scrolls that sat unread till death: Remove Curse on a cursed piece worn,
    bool cw = false; int unid = 0;                     // Identify with something to learn, Enchantment, Magic Mapping a while into a floor with no way down yet
    for (int sl = -4; sl < BAG_SIZE; sl++) { Item *it = game_slot(sl); int c = item_cat(it->kind); if (c != CAT_WEAPON && c != CAT_ARMOR && c != CAT_RING && c != CAT_WAND) continue;
      if (sl < 0 && (it->flags & (IF_CURSED | IF_CURSED_KNOWN)) == (IF_CURSED | IF_CURSED_KNOWN)) cw = true;
      bool u = (it->flags & (IF_LEVEL_KNOWN | IF_CURSED_KNOWN)) != (IF_LEVEL_KNOWN | IF_CURSED_KNOWN) || (c >= CAT_WAND && !item_known(it->kind));
      if (u && !unid) unid = 1;                        // 0.65 (owner): Identify opens on a better weapon / armour a point of STR short (an
      if (u && sl >= 0 && c <= CAT_ARMOR && !(it->flags & IF_LEVEL_KNOWN) && item_str(it) + it->lvl <= hero_str() + 1 &&   // upgrade on it would make it wearable),
          item_tier(it->kind) > item_tier((c == CAT_WEAPON ? &h->weapon : &h->armor)->kind)) unid = sl + 8; }   // else on nothing: the player chooses
    for (int k = IK_SCR_REMOVE_CURSE; k; k = k == IK_SCR_REMOVE_CURSE ? IK_SCR_IDENTIFY : k == IK_SCR_IDENTIFY ? IK_SCR_ENCHANT : k == IK_SCR_ENCHANT ? IK_SCR_MAPPING : 0) {
      if ((s = game_bag_find(k)) < 0 || !item_known(k) || DECL(k, ACN_READ)) continue;
      if ((k == IK_SCR_REMOVE_CURSE && cw) || (k == IK_SCR_IDENTIFY && unid) || k == IK_SCR_ENCHANT || (k == IK_SCR_MAPPING && !BIT_GET(G.visited, G.exit) && G.now - G.level_t0 > 250 * TICK)) {
        G.tgm.pk = k == IK_SCR_IDENTIFY ? (unid > 1 ? unid : 0) : 6; g_ovr.hs = s; g_ovr.ha = ACN_READ; return TK_BAG; } } }
  if (!h->b.blind && h->HP * 5 >= h->HT * 4 && !game_starving()) for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) == CAT_SCROLL && !item_known(k) && !DECL(k, ACN_READ)) { G.tgm.pk = 6; g_ovr.hs = i; g_ovr.ha = ACN_READ; return TK_BAG; } }   // 0.50: read to learn it, nothing about (0.64: an Upgrade: the armour)
  if (h->HP * 5 < h->HT * 3 && G.health_pos != hp && plant_at(hp) < 0 && heap_at(hp) < 0 && hp != G.entrance && hp != G.exit && !DECL(IK_SEED_SUNGRASS, ACN_PLANT) && XENV("SUNGRASS", 1))
    for (int sl = 0; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) if (game_slot(sl)->kind == IK_SEED_SUNGRASS) { g_ovr.hs = sl; g_ovr.ha = ACN_PLANT; return TK_BAG; }   // 0.64: hurt, nothing about: Sungrass underfoot, then rest on it
  // 0.64: drink unknown potions to learn them while it's safe (PD players do it early: then healing is known when it
  // matters): healthy, nothing in sight, not on what burns; Liquid Flame not yet known: only in water or with no scrolls to lose
  if (XENV("TESTDRINK", 1) && h->HP * 10 >= h->HT * 9 && !STARVING) {
    bool scr = false, burn = false; for (int i = 0; i < BAG_SIZE; i++) scr |= item_cat(h->bag[i].kind) == CAT_SCROLL;
    for (int k = -1; k < 8; k++) { int n = k < 0 ? hp : hp + N8[k]; if (k < 0 || nb_ok(hp, n)) burn |= (TFLAGS(G.map[n]) & F_FLAMABLE) != 0; }   // (owner: doors burn too; Liquid Flame catches what's beside you)
    if (!XENV("FLAMEAROUND", 1)) burn = TFLAGS(G.map[hp]) & F_FLAMABLE;
    if (item_known(IK_POT_FLAME) || ((IS_WATER(G.map[hp]) || !scr) && !burn)) for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind;
      if (item_cat(k) == CAT_POTION && !item_known(k) && !DECL(k, ACN_DRINK)) { g_ovr.hs = i; g_ovr.ha = ACN_DRINK; return TK_BAG; } } }
  for (int i = 0; i < BAG_SIZE; i++) {
    Item *it = &h->bag[i]; int c = item_cat(it->kind); Item *cur = c == CAT_WEAPON ? &h->weapon : &h->armor;
    if ((c != CAT_WEAPON && c != CAT_ARMOR) || it->kind == IK_PICKAXE || CLASS_ARMOR(it->kind) || (cur->flags & IF_CURSED)) continue;
    // 0.64 (the Assist alone died in cloth armour: found gear was never worn unidentified): an unknown piece a tier or more
    // better is worth wearing (PD's odds: mostly +0, a cursed one now and then; wearing it is how you learn it)
    bool lk = it->flags & IF_LEVEL_KNOWN, ck = it->flags & IF_CURSED_KNOWN;
    if ((!(lk && ck) && !XENV("UNKGEAR", 1)) || (ck && (it->flags & IF_CURSED)) || (lk && it->lvl < 0) || item_str(it) + (lk || !XENV("STRHONEST", 1) ? 0 : it->lvl) > hero_str()) continue;   // (0.65: an unknown level's STR: the plain one, no peeking)
    int a = 2 * item_tier(it->kind) + (lk ? it->lvl : -1), b = cur->kind ? 2 * (CLASS_ARMOR(cur->kind) ? 6 : item_tier(cur->kind)) + ((cur->flags & IF_LEVEL_KNOWN) ? cur->lvl : 0) : -1;
    if (a > b && !DECL(it->kind, ACN_EQUIP)) { g_ovr.hs = i; g_ovr.ha = ACN_EQUIP; return TK_BAG; }
  }
  if (XENV("DROPJUNK", 1)) {                           // 0.64: a nearly full pack: put down what's known to be bad, or a weapon / armour
    int n = 0; for (int i = 0; i < BAG_SIZE; i++) n += h->bag[i].kind != 0;   // known no better than what's worn
    if (n >= BAG_SIZE - 2) for (int i = 0; i < BAG_SIZE; i++) { Item *it = &h->bag[i]; int c = item_cat(it->kind); Item *cur = c == CAT_WEAPON ? &h->weapon : &h->armor;
      bool worse = (c == CAT_WEAPON || c == CAT_ARMOR) && it->kind != IK_PICKAXE && (it->flags & IF_LEVEL_KNOWN) && cur->kind && !CLASS_ARMOR(it->kind) &&
                   2 * item_tier(it->kind) + it->lvl <= 2 * (CLASS_ARMOR(cur->kind) ? 6 : item_tier(cur->kind)) + ((cur->flags & IF_LEVEL_KNOWN) ? cur->lvl : 0);
      if ((junk(it) || worse) && !DECL(it->kind, ACN_DROP)) { g_ovr.hs = i; g_ovr.ha = ACN_DROP; return TK_BAG; } } }
  if ((h->HP * 10 < h->HT * XENV("RESTP", 5) || (G.health_pos == hp && h->HP < h->HT)) && !game_starving() && !DECL(0, 255)) return TK_REST;   // (0.64: on Sungrass: to full)
  return -1;
}
#endif
#if TGT_PART == 0
static int targets(int16_t *cell, uint8_t *kind) {
  int hp = G.hero.pos;
  s_tn = 0; s_tc = cell; s_tk = kind;
  { int f = g_from, dx = (hp & 31) - (f & 31), dy = (hp >> 5) - (f >> 5); s_head = f >= 0 && (dx || dy) && abs(dx) <= 8 && abs(dy) <= 8 ? pang(dx, dy) : -1; }
  for (int c = 0; c < MAP_LEN; c++)                    // 0.25: distances are walks through what's known (the pathfinder's)
    if (open_cell(c) && (BIT_GET(G.visited, c) || BIT_GET(G.fov, c))) BIT_SET(s_pass, c); else BIT_CLR(s_pass, c);
  mark_chars(true);                                    // Hero.getCloser: those in sight block the way
  for (int i = 0; i < G.n_mobs; i++) {                 // 0.43: and the quest people seen before (they stay put: a way only open
    Mob *m = &G.mobs[i];                               // while one was out of sight sent the hero to and fro)
    if (m->HP <= 0) continue;
    if (MOB_DEFS[m->type].flags & MD_NPC) BIT_CLR(s_pass, m->pos);
    if (BIT_GET(G.fov, m->pos)) { if (m->type == MT_PIRANHA) { s_fish = true; G.tgm.fish = G.level_t0; } if (!ally(i)) s_foe = true; }   // a piranha in sight: keep out of the water
  }
  if (G.tgm.fish == G.level_t0 && XENV("FISHMEM", 1)) s_fish = true;   // 0.64: (seen on this floor: they're still in there; the Assist alone went back in, out, in...)
  distance_map(hp, -1); walk_map();
  if (g_ovr.c >= 0 && (g_ovr.c & 0x4000)) { g_ovr.c &= 0x3FF; g_ovr.on |= 8; }   // 0.47: chosen from the list: an objective (main.c tg_do)
  else if (g_ovr.c >= 0 && g_ovr.t < -9999) g_ovr.on &= ~8;   // (a tap on the screen: a place)
  if (g_ovr.c >= 0 && g_ovr.t < -9999) {                 // just chosen: the way from where it was chosen; on this floor
    int o = g_ovr.t + 65536, dx = (g_ovr.c & 31) - (o & 31), dy = (g_ovr.c >> 5) - (o >> 5);
    g_ovr.t = G.level_t0;
    if (g_ovr.on & 2) { g_ovr.on |= 4; g_ovr.gk = G.hero.exp + G.hero.lvl * 101; }   // the grass was the first: skipped
    g_goal = -1;                                       // 0.43: a step is the player's way too, unless it's away from an enemy
    if ((s_foe || !(dx | dy)) && abs(dx) < 2 && abs(dy) < 2) g_ovr.c = g_ovr.on ? MAP_LEN : -1;   // (a step back: no way; the one before, if any, goes on)
    else g_ovr.d = (g_ovr.d & 64) | pang(dx, dy);
  }
  if (g_ovr.t != G.level_t0) g_ovr.c = -1; else if (g_ovr.c == hp) g_ovr.c = (g_ovr.on & 8) ? -1 : MAP_LEN;   // reached: on that way (an objective: done)
  g_ovr.on = (g_ovr.on & 12) | (g_ovr.c >= 0); if (g_ovr.c < 0) g_ovr.on &= ~8; g_ovr.d &= 127; s_od = g_ovr.c >= 0 ? g_ovr.d & 63 : -1;   // (bit 7: entry 0 is an enemy to wait for; 6: waited after a step back)
  { int pk = G.hero.weapon.kind == IK_PICKAXE ? -1 : game_bag_find(IK_PICKAXE); g_ovr.ms = pk;
    s_pick = (pk >= 0 || G.hero.weapon.kind == IK_PICKAXE) && G.depth >= 11 && G.depth <= 15; }
  G.tgm.src0 = 1;
  { int k0, c0 = first(&k0); s_zero = true; t_put(c0, k0, 0); s_zero = false;
#ifdef OVL_HOST
    void tgt_first(int c, int k) __attribute__((weak)); if (tgt_first) tgt_first(c0, k0);   // (botrun deepbot: entry 0 before the on-screen stand-in)
#endif
  }
  return s_tn;
}
#else
static void t_front(int c, int kind) {               // 0.51: a pinned entry, after those pinned before it (the one at that cell goes)
  for (int j = s_h0; j < s_tn; j++) if (s_tc[j] == c) { t_del(j); break; }
  int i = s_tn < TGT_CAP ? s_tn++ : TGT_CAP - 1;
  for (; i > s_h0; i--) { s_tc[i] = s_tc[i - 1]; s_tk[i] = s_tk[i - 1]; s_tkey[i] = s_tkey[i - 1]; g_tgobj[i] = g_tgobj[i - 1]; }
  s_tc[i] = c; s_tk[i] = kind; s_tkey[i] = 0; g_tgobj[i] = c; s_h0++;
}
#if TGT_PART == 2
enum { HB_WAIT, HB_SEARCH, HB_INFO, HB_BAG, HB_Q2, HB_Q1 };   // (ui_int.h TB_*: the bottom bar, then 6 the menu, 7 the portrait)
static void hl_rank(int hk) {                          // 0.54 (owner): Back walks the bottom bar in the bot's order, best first
  Hero *h = &G.hero; int hp = h->pos, sc[8]; bool seen = false, foe2 = false;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & MD_NPC)) continue; seen = true;
    if (m->state == ST_HUNTING && distance(m->pos, hp) == 2 && MOB_DEFS[m->type].move_cost >= TICK) foe2 = true; }   // (a slow one a step off: wait, it walks into the first blow)
  int k = hk == TK_BAG && g_ovr.hs >= 0 ? game_slot(g_ovr.hs)->kind : 0, hb = hk != TK_BAG ? -1 : k && k == h->quick[0] ? HB_Q1 : k && k == h->quick[1] ? HB_Q2 : HB_BAG;
  sc[HB_WAIT] = hk == TK_REST ? 92 : foe2 ? 90 : !seen && h->HP < h->HT ? 50 : 25;
  sc[HB_SEARCH] = s_tn > 0 && s_tc[0] == hp && s_tk[0] == TK_GO ? 80 : seen ? 10 : 35;
  sc[HB_INFO] = 15; sc[HB_BAG] = 40; sc[6] = 2; sc[7] = 3;
  for (int q = 0; q < 2; q++) { int qk = h->quick[q], c = item_cat(qk); sc[q ? HB_Q2 : HB_Q1] = !qk || game_bag_find(qk) < 0 ? 5 : seen && (c == CAT_MISSILE || c == CAT_WAND) ? 60 : 20; }
  if (hb >= 0) sc[hb] = 100;
  for (int i = 0; i < 8; i++) { int b = 0; for (int j = 1; j < 8; j++) if (sc[j] > sc[b]) b = j; g_hl_order[i] = b; sc[b] = -1; }
}
static int targets(int16_t *cell, uint8_t *kind, int n0) {   // 0.63: tgt3: the hands, pinned in front of tgt2's list (main.c game_targets)
  s_tn = n0; s_tc = cell; s_tk = kind; s_h0 = 0;
  if (g_assist >= 2) { s_aim = -1; int hk = hands(); if (hk >= 0) { t_front(hk == TK_BAG && s_aim >= 0 ? s_aim : G.hero.pos, hk); G.tgm.src0 = 3; } hl_rank(hk); }   // 0.51 (owner): plan A, and Down still offers the rest
  return s_tn;
}
#else
static int targets(int16_t *cell, uint8_t *kind, int n0) {   // tgt2: the distances and entry 0 are tgt's (s_dist, s_queue)
  int hp = G.hero.pos, ne = 0;
  s_tn = n0; s_tc = cell; s_tk = kind; s_tkey[0] = 0;
  s_h0 = 0;
  if (n0 > 0 && cell[0] == G.exit) g_ovr.dd = -1;   // 0.53: the stairs are plan A: passed-over hands come back
  if (G.tgm.srch) { G.tgm.srch = 0; int a = srch_spot(-1, -1);   // 0.63: tgt's last resort, the best spot to search for secret doors from, as entry 0
    if (a >= 0 && s_tn > 0 && s_tc[0] == hp) { t_del(0); int n1 = s_tn; s_zero = true; t_put(a, TK_GO, 0); s_zero = false;
      if (s_tn > n1) { int c = s_tc[n1], k = s_tk[n1], o = g_tgobj[n1]; for (int j = n1; j > 0; j--) { s_tc[j] = s_tc[j - 1]; s_tk[j] = s_tk[j - 1]; g_tgobj[j] = g_tgobj[j - 1]; } s_tc[0] = c; s_tk[0] = k; g_tgobj[0] = o; }
      else t_front(hp, TK_GO); } }                      // (nowhere on screen: search here, as before)
  bool vo = G.now < G.tgm.vt && XENV("VETO", 1), veto = vo && G.tgm.vs == 2;   // 0.66: the player overrode where we'd have them stand (or get out
  int esc = vo && G.tgm.vs == 4 && G.now + 10 * TICK < G.tgm.vt ? -1 : escape();   // of gas: only 5 turns): not again a while
  if (esc >= 0) { t_front(esc, TK_GO); G.tgm.src0 = 4; }   // 0.64: out of gas / fire / ooze first (Assist Low too)
  else if (!veto)
#ifdef OVL_HOST
  if (getenv("PRESS") && !getenv("NOTACT"))           // (the solver bot keeps its own: only when it plays by the buttons)
#endif
  if (g_assist >= 1) { int tc = tactics(); if (tc >= 0) { if (tc == hp) g_ovr.d |= 128; t_front(tc, TK_GO); G.tgm.src0 = 2; } }   // 0.50: where to stand (0.51: then the rest; 0.61: there: wait)
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && BIT_GET(G.fov, m->pos) && m->type == MT_PIRANHA) s_fish = true; }
  { int pk = G.hero.weapon.kind == IK_PICKAXE ? -1 : game_bag_find(IK_PICKAXE); s_pick = (pk >= 0 || G.hero.weapon.kind == IK_PICKAXE) && G.depth >= 11 && G.depth <= 15; }
  // 0.47 (owner: "cycle through its plan B, C..."): the alternatives are objectives, best first by kind then walk:
  // enemies in sight, a step away from them, things to pick up, people / wells / locked doors / veins, the unexplored
  // (one entry per region: the nearest spot of it), the way down, the way up. Off screen: shown at the farthest step on
  // screen towards it (t_put); choosing one makes the objective itself plan A till it is met (g_tgobj, first())
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0 || !BIT_GET(G.fov, m->pos)) continue;
    int d = distance(hp, m->pos);
    if (!ally(i)) { ne++; if (on_screen(m->pos) && (d < 2 || !(MOB_DEFS[m->type].flags & MD_WATER))) { s_obj = m->pos; t_add(m->pos, TK_ENEMY, 1 << 10 | d); } }   // a piranha: only beside you
    else if (npc_wanted(m->type) && walk(m->pos) < 255) t_put(m->pos, TK_GO, 4 << 10 | walk(m->pos));
  }
  for (int i = 0; i < G.n_heaps; i++) {
    Heap *h = &G.heaps[i];                              // (a carried heap's pos is off the map: not walked to)
    if (!heap_ok(h)) continue;                          // (0.48: not what you put down, not junk you know of)
    int d = h->pos == hp ? 0 : walk(h->pos);
    if (d < 255) t_put(h->pos, TK_ITEM, 3 << 10 | d);
  }
  int keys = G.keys[G.depth];
  uint8_t *cov = (uint8_t *)s_queue + MAP_LEN;           // (the path finder's queue, free: the regions taken)
  memset(cov, 0, MAP_LEN);
  for (int c = 0; c < MAP_LEN; c++) {
    int t = G.map[c], d = walk(c);
    if (d >= 255 || c == hp) continue;
    if ((t == T_WELL && c == G.well_pos) || t == T_ALCHEMY || (t == T_LOCKED_DOOR && (keys & 15))) t_put(c, TK_GO, 4 << 10 | d);
    else if (t == T_WALL_DECO && s_pick && on_screen(c)) { s_obj = c; t_add(c, d == 1 ? TK_MINE : TK_VEIN, 4 << 10 | d); }   // 0.41: a gold vein
    else if (dead_end(c)) t_put(c, TK_GO, 4 << 10 | d);   // 0.48: a dead end: search there
    else if (c == G.exit && (t != T_LOCKED_EXIT || (keys & 64))) t_put(c, TK_GO, 6 << 10 | d);
    else if (c == G.entrance && t == T_ENTRANCE) t_put(c, TK_GO, 7 << 10 | d);
  }
#ifdef OVL_HOST
  if (getenv("TDBG2")) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR || opening(c)) printf("   cell %d (%d,%d) t%d vis%d dist%d unseen%d opening%d\n", c, c & 31, c >> 5, G.map[c], BIT_GET(G.visited, c), s_dist[c], unseen_by(c), opening(c));
#endif
  int nreg = 0;
  for (int n = 0; n < TGT_CAP; n++, nreg++) {          // the unexplored regions: the nearest spot not yet taken, then its whole region is
    int best = -1, bd = 255;
    for (int c = 0; c < MAP_LEN; c++) if (!cov[c] && s_dist[c] < bd && s_dist[c] > 0 && opening(c)) { bd = s_dist[c]; best = c; }
    if (best < 0) break;
    cov[best] = 1;                                      // its region: the spots beside the unknown near it
    for (int c = 0; c < MAP_LEN; c++)                 // (0.47: within 3, and as far to walk give or take 3: no chain needed)
      if (s_dist[c] < 255 && abs(s_dist[c] - bd) <= 3 && opening(c) && G.map[c] != T_DOOR && G.map[best] != T_DOOR && distance(c, best) <= 3) cov[c] = 1;   // (a door: one of its own)
#ifdef OVL_HOST
    if (getenv("TDBG")) printf("   region %d: spot %d walk %d\n", n, best, bd);
#endif
    t_put(best, TK_GO, 5 << 10 | bd);
  }
  if (!nreg && (!BIT_GET(G.visited, G.exit) || walk(G.exit) >= 255)) {   // 0.48 (owner): all explored, the way on hidden: where to search
    int a = srch_spot(-1, -1), b = a >= 0 ? srch_spot(a, -1) : -1, c = b >= 0 ? srch_spot(a, b) : -1;
    if (a >= 0) t_put(a, TK_GO, 5 << 10 | walk(a));
    if (b >= 0) t_put(b, TK_GO, 5 << 10 | walk(b));
    if (c >= 0) t_put(c, TK_GO, 5 << 10 | walk(c));
  }
  if (ne) {                                             // a step away: the free neighbour farthest from every enemy in sight
    int best = -1, bs = 0;
    for (int k = 0; k < 8; k++) {
      int n = hp + N8[k], s = 99;
      if (!nb_ok(hp, n) || !open_cell(n) || char_at(n)) continue;
      for (int i = 0; i < s_tn; i++) if (s_tk[i] == TK_ENEMY) { int e = distance(n, s_tc[i]); if (e < s) s = e; }
      s = s * 2 + (G.map[n] == T_DOOR || G.map[n] == T_OPEN_DOOR);   // a doorway wins a tie
      if (s > bs) { bs = s; best = n; }
    }
    if (best >= 0) { s_obj = best; t_add(best, TK_GO, 2 << 10); }
  }
  int pr = mind(hp);
  if (pr >= 0) { g_ovr.c = pr | 0x4000; g_ovr.t = hp - 65536; return -100; }   // (game_targets: both parts again)
  return s_tn;
}
#endif
#endif
#ifdef OVL_HOST
#include <time.h>
void tgt_time(long ns) __attribute__((weak));        // host timing (botrun deepbot TT=1)
#if TGT_PART == 0
static int targets_t(int16_t *cell, uint8_t *kind) {
  struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a); int n = targets(cell, kind); clock_gettime(CLOCK_MONOTONIC, &b);
#else
static int targets_t(int16_t *cell, uint8_t *kind, int n0) {
  struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a); int n = targets(cell, kind, n0); clock_gettime(CLOCK_MONOTONIC, &b);
#endif
  if (tgt_time) tgt_time((b.tv_sec - a.tv_sec) * 1000000000L + b.tv_nsec - a.tv_nsec);
  return n;
}
#if TGT_PART == 0
OVL_ENTRY(tgt, (void *)targets_t);
#elif TGT_PART == 1
OVL_ENTRY(tgt2, (void *)targets_t);
#else
OVL_ENTRY(tgt3, (void *)targets_t);
#endif
#elif TGT_PART == 0
OVL_ENTRY(tgt, (void *)targets);
#elif TGT_PART == 1
OVL_ENTRY(tgt2, (void *)targets);
#else
OVL_ENTRY(tgt3, (void *)targets);
#endif
