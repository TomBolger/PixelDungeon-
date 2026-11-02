#include "pd.h"
#include "game_int.h"
#ifdef PD_FACE
#include "face_brain.h"
#endif
#include <unistd.h>
PKey g_keys[256]; int g_persist_fail;
static int TRACE=0;
void ui_log(const char *m, MsgColor c) { if (TRACE) printf("    log: %s\n", m); }
// every hero_damage call (HP > 0 before) shows FX_DAMAGE on the hero with the amount: Sim.SimHero.damage's log

void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) {}
void ui_pick_item(PickMode m) {}
void ui_hero_tier(void) {}
void ui_amulet(int m) {}
void ui_message(const char *t, const char *x) {}
void ui_confirm_jump(void) {}
void ui_sound(int s) {}
void ui_wnd(int m, const char *t, const char *a, const char *b) {}
const char *g_wnd_title;
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) {}


// levtest: a traps room (every floor cell a trap) with a chest in the middle, a known Levitation potion in the bag.
// Does the Assist drink it at the door and then walk over the traps to the chest while the float lasts?  ./levtest [seed]
#define OX 8
#define OY 8
static const char *ART[] = {
  "###########",
  "#TTTTTTTTT#",
  "#TTTTTTTTT#",
  "#TTTTTCTTT#",
  "#TTTTTTTTT#",
  "#TTTTTTTTT#",
  "####+######",
  "###...#####",
  "###.@.#####",
  "###...#####",
  "###########",
};
static const char *ART2[] = {      // modes 2-4: the hero in the room's corner, the float over (every way out crosses a known trap)
  "###########",
  "#@TTTTTTTT#",
  "#TTTTTTTTT#",
  "#TTTTTCTTT#",
  "#TTTTTTTTT#",
  "#TTTTTTTTT#",
  "####+######",
  "###...#####",
  "###...#####",
  "###...#####",
  "###########",
};
static const char *ART3[] = {      // mode 4: outside the door, a full traps room, Levitation known, darts in the pack
  "###########",
  "#TTTTTTTTT#",
  "#TTTTTTTTT#",
  "#TTTTTCTTT#",
  "#TTTTTTTTT#",
  "#TTTTTTTTT#",
  "####+######",
  "###...#####",
  "###.@.#####",
  "###...#####",
  "###########",
};
static int g_trap = T_TOXIC_TRAP, g_throws, g_steps_out;
static int g_fail, g_ord[MAP_LEN], g_maxord, g_refused;                 // modes 5, 6: every throw order fails (as when the windows back out of the aim screen): the hero searches instead
static int run(int mode, uint32_t seed) {   // mode 0: floating already; 1: the potion in the bag (known)
  g_new_class = 0; game_new(); blob_free_all();
  G.depth = 3; G.n_mobs = 0; G.n_heaps = 0; G.n_plants = 0; G.n_diffs = 0;
  memset(G.hg, 0, sizeof G.hg); memset(G.visited, 0, sizeof G.visited);
  for (int i = 0; i < MAP_LEN; i++) G.map[i] = T_WALL;
  int chest = -1, hero = -1;
  const char **A = mode == 4 || mode == 6 ? ART3 : mode >= 2 ? ART2 : ART; g_throws = 0; g_fail = mode == 5 || mode == 6; memset(g_ord, 0, sizeof g_ord); g_maxord = 0; g_refused = 0;
#ifdef PD_FACE
  g_face_nothrow = 0; s_fb_thc = -1;
#endif
  for (int y = 0; y < 11; y++) for (int x = 0; A[y][x]; x++) { char ch = A[y][x]; int c = (OY + y) * MAP_W + OX + x, t;
    switch (ch) { case '#': t = T_WALL; break; case '+': t = T_DOOR; break; case 'T': t = g_trap; break; default: t = T_EMPTY; }
    if (ch == 'C') chest = c; if (ch == '@') hero = c; G.map[c] = t; }
  build_los();
  G.n_rooms = 1; G.rooms[0] = (Room){ OX, OY, OX + 10, OY + 6, RT_TRAPS, chest };
  G.entrance = hero - 2 * MAP_W - 1; G.exit = hero + MAP_W + 1; G.entrance = (OY + 8) * MAP_W + OX + 3; G.exit = (OY + 8) * MAP_W + OX + 5; G.map[G.entrance] = T_ENTRANCE; G.map[G.exit] = T_EXIT; G.well_pos = G.altar_pos = G.health_pos = -1; G.boss = 0; G.dflags = 0; memset(G.lvars, 0, sizeof G.lvars);
  G.now = 0; G.turn_due = 0; G.level_t0 = -TICK; G.hero.time = 0; G.hero.pos = hero; G.hero.HP = G.hero.HT = 40;
  G.action = HA_NONE; G.interrupted = false; g_rng = mix(seed, 1, 0x51u) | 1;
  heap_put_t(chest, item_make(IK_GOLD, 77), HEAP_CHEST);
  for (int y = 0; y < 11; y++) for (int x = 0; x < 11; x++) BIT_SET(G.visited, (OY + y) * MAP_W + OX + x);
  for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind == IK_POT_LEVITATION) memset(&G.hero.bag[i], 0, sizeof G.hero.bag[i]);
  G.known[IK_POT_LEVITATION >> 3] |= 1 << (IK_POT_LEVITATION & 7);
  if (mode >= 2) { G.hero.b.levitation = 0; if (mode == 3 || mode == 4 || mode == 5 || mode == 6) { bag_add(item_make(IK_DART, 6)); } if (mode == 4) bag_add(item_make(IK_POT_LEVITATION, 1)); }
  if (mode == 0) { G.hero.b.levitation = 20; G.hero.pos = OY * MAP_W + OX + 4 + 6 * MAP_W; G.hero.pos = (OY + 6) * MAP_W + OX + 4; }
  else if (mode == 1) bag_add(item_make(IK_POT_LEVITATION, 1));
  update_fov(); G.interrupted = false;
  static const uint8_t VIEW1[4] = { 6, 4, 4, 4 }; memcpy(g_view, VIEW1, 4);
  int drank = 0, took = 0, hp0 = G.hero.HP, g0 = G.hero.gold;
  int after = 0;
  for (int step = 0; step < 400 && after < 25; step++) {
    if ((mode == 2 || mode == 3 || mode == 5) && cy(G.hero.pos) >= OY + 7) break;
    if (took) after++;
    game_level_commit();
    if (G.hero.gold >= g0 + 77) took = 1;
    if (G.depth != 3) break;
    int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind);
    if (n <= 0) break;
    ActResult r; int32_t t0 = G.hero.time;
    if (getenv("V")) for (int q = 0; q < G.n_heaps; q++) printf("   heap %d,%d type %d item %d\n", cx(G.heaps[q].pos), cy(G.heaps[q].pos), G.heaps[q].type, G.heaps[q].item.kind);
    if (getenv("V")) printf("  step %d hero %d,%d lev %d hp %d target %d,%d kind %d ha %d n %d\n", step, cx(G.hero.pos), cy(G.hero.pos), G.hero.b.levitation, G.hero.HP, cx(cell[0]), cy(cell[0]), kind[0], g_ovr.ha, n);
#ifdef PD_FACE
    if (kind[0] == TK_BAG && g_ovr.ha == ACN_THROW && !fb_throw(cell[0])) { g_refused++; r = game_search(); }   // (face.inc: the order is not carried out; the next think plans afresh)
    else
#endif
    if (kind[0] == TK_BAG && g_ovr.ha == ACN_THROW && g_fail) { r = game_search(); g_throws++; if (++g_ord[cell[0]] > g_maxord) g_maxord = g_ord[cell[0]]; }
    else if (kind[0] == TK_BAG && g_ovr.ha == ACN_THROW) { r = game_throw(g_ovr.hs, cell[0]); g_throws++; }
    else if (kind[0] == TK_BAG) { r = game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha); drank += g_ovr.ha == ACN_DRINK; }
    else if (kind[0] == TK_REST) r = game_rest(true);
    else r = (cell[0] == G.hero.pos && kind[0] == TK_GO) ? game_search() : game_tap_cell(cell[0]);
    for (int k = 0; r == ACT_WALK && k < 400 && G.hero.HP > 0; k++) r = game_continue();
    if (G.hero.time == t0 && r == ACT_NONE) break;
  }
  if (mode == 2 || mode == 3 || mode == 5) took = cy(G.hero.pos) >= OY + 7 && G.hero.HP > 0;
  if (mode == 6) took = g_maxord <= 2;                         // the throws that fail are given up after two
  if (mode == 5) took = took && g_maxord <= 2;
  printf("mode %d (trap %d): throws %d, drank %d, chest %s, hero hp %d/%d, levitation left %d, hero at %d,%d\n", mode, g_trap, g_throws, drank, took ? "TAKEN" : "left behind", G.hero.HP, hp0, G.hero.b.levitation, cx(G.hero.pos), cy(G.hero.pos));
  return took;
}
#ifdef PD_FACE
// The window flow of the face (face.inc): the backpack, the item window and the aim screen, each press counted by fb_scene (face_brain.h).
// Walking to an item in the backpack takes presses (a row, then the cells), its Throw option a few more; the aim screen then gets 3.
static int windows(void) {
  uint8_t n = 0; int ok = 1; s_fb_sc = -1;
  struct { int sc, presses; } F[] = { { 1, 1 }, { 2, 7 }, { 3, 4 }, { 4, 1 } };   // game (a decision), backpack (7 presses), item (4), aim screen (1)
  int got = 0, old = 0, oldn = 0;
  for (unsigned i = 0; i < sizeof F / sizeof *F; i++) for (int k = 0; k < F[i].presses; k++) {
    fb_scene(F[i].sc, &n); n++; oldn++;                                      // (oldn: all the presses in one count, as it was)
    if (F[i].sc == 4) { got = n <= 3; old = oldn <= 3; }
  }
  printf("window flow: the aim screen is allowed its press %s (all presses in one count: %s)\n", got ? "yes" : "NO", old ? "yes" : "no, it backed out");
  return ok && got && !old;
}
#endif
int main(int argc, char **argv) {
  if (!game_init()) return 1; g_assist = 3;
  uint32_t seed = argc > 1 ? atoi(argv[1]) : 1;
  int ok = 1;
  ok &= run(0, seed); ok &= run(1, seed);
  static const int KIND[] = { T_TOXIC_TRAP, T_PARALYTIC_TRAP, T_POISON_TRAP, T_LIGHTNING_TRAP, T_GRIPPING_TRAP, T_FIRE_TRAP, T_ALARM_TRAP };
  for (unsigned i = 0; i < sizeof KIND / sizeof *KIND; i++) { g_trap = KIND[i]; ok &= run(2, seed); ok &= run(3, seed); }
  g_trap = T_TOXIC_TRAP; ok &= run(4, seed); g_trap = T_PARALYTIC_TRAP; ok &= run(4, seed);
#ifdef PD_FACE
  g_trap = T_TOXIC_TRAP; ok &= run(5, seed); ok &= run(6, seed); g_trap = T_PARALYTIC_TRAP; ok &= run(5, seed); ok &= run(6, seed);   // throws that never land (the face: the aim screen backed out)
  ok &= windows();
#endif
  printf("%s\n", ok ? "PASS" : "FAIL"); return !ok;
}
