#include "pd.h"
#include "game_int.h"
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
static int run(int mode, uint32_t seed) {   // mode 0: floating already; 1: the potion in the bag (known)
  g_new_class = 0; game_new(); blob_free_all();
  G.depth = 3; G.n_mobs = 0; G.n_heaps = 0; G.n_plants = 0; G.n_diffs = 0;
  memset(G.hg, 0, sizeof G.hg); memset(G.visited, 0, sizeof G.visited);
  for (int i = 0; i < MAP_LEN; i++) G.map[i] = T_WALL;
  int chest = -1, hero = -1;
  for (int y = 0; y < 11; y++) for (int x = 0; ART[y][x]; x++) { char ch = ART[y][x]; int c = (OY + y) * MAP_W + OX + x, t;
    switch (ch) { case '#': t = T_WALL; break; case '+': t = T_DOOR; break; case 'T': t = T_TOXIC_TRAP; break; default: t = T_EMPTY; }
    if (ch == 'C') chest = c; if (ch == '@') hero = c; G.map[c] = t; }
  build_los();
  G.n_rooms = 1; G.rooms[0] = (Room){ OX, OY, OX + 10, OY + 6, RT_TRAPS, chest };
  G.entrance = hero - 2 * MAP_W - 1; G.exit = hero + MAP_W + 1; G.map[G.entrance] = T_ENTRANCE; G.map[G.exit] = T_EXIT; G.well_pos = G.altar_pos = G.health_pos = -1; G.boss = 0; G.dflags = 0; memset(G.lvars, 0, sizeof G.lvars);
  G.now = 0; G.turn_due = 0; G.level_t0 = -TICK; G.hero.time = 0; G.hero.pos = hero; G.hero.HP = G.hero.HT = 40;
  G.action = HA_NONE; G.interrupted = false; g_rng = mix(seed, 1, 0x51u) | 1;
  heap_put_t(chest, item_make(IK_GOLD, 77), HEAP_CHEST);
  for (int y = 0; y < 11; y++) for (int x = 0; x < 11; x++) BIT_SET(G.visited, (OY + y) * MAP_W + OX + x);
  for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind == IK_POT_LEVITATION) memset(&G.hero.bag[i], 0, sizeof G.hero.bag[i]);
  G.known[IK_POT_LEVITATION >> 3] |= 1 << (IK_POT_LEVITATION & 7);
  if (mode == 0) { G.hero.b.levitation = 20; G.hero.pos = OY * MAP_W + OX + 4 + 6 * MAP_W; G.hero.pos = (OY + 6) * MAP_W + OX + 4; }
  else bag_add(item_make(IK_POT_LEVITATION, 1));
  update_fov(); G.interrupted = false;
  static const uint8_t VIEW1[4] = { 6, 4, 4, 4 }; memcpy(g_view, VIEW1, 4);
  int drank = 0, took = 0, hp0 = G.hero.HP, g0 = G.hero.gold;
  int after = 0;
  for (int step = 0; step < 300 && after < 25; step++) {
    if (took) after++;
    game_level_commit();
    if (G.hero.gold >= g0 + 77) took = 1;
    if (G.depth != 3) break;
    int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind);
    if (n <= 0) break;
    ActResult r; int32_t t0 = G.hero.time;
    if (getenv("V")) for (int q = 0; q < G.n_heaps; q++) printf("   heap %d,%d type %d item %d\n", cx(G.heaps[q].pos), cy(G.heaps[q].pos), G.heaps[q].type, G.heaps[q].item.kind);
    if (getenv("V")) printf("  step %d hero %d,%d lev %d target %d,%d kind %d\n", step, cx(G.hero.pos), cy(G.hero.pos), G.hero.b.levitation, cx(cell[0]), cy(cell[0]), kind[0]);
    if (kind[0] == TK_BAG) { r = game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha); drank += g_ovr.ha == ACN_DRINK; }
    else if (kind[0] == TK_REST) r = game_rest(true);
    else r = (cell[0] == G.hero.pos && kind[0] == TK_GO) ? game_search() : game_tap_cell(cell[0]);
    for (int k = 0; r == ACT_WALK && k < 400 && G.hero.HP > 0; k++) r = game_continue();
    if (G.hero.time == t0 && r == ACT_NONE) break;
  }
  printf("mode %d: drank %d, chest %s, hero hp %d/%d, levitation left %d, hero at %d,%d\n", mode, drank, took ? "TAKEN" : "left behind", G.hero.HP, hp0, G.hero.b.levitation, cx(G.hero.pos), cy(G.hero.pos));
  return took;
}
int main(int argc, char **argv) {
  if (!game_init()) return 1; g_assist = 3;
  uint32_t seed = argc > 1 ? atoi(argv[1]) : 1;
  int a = run(0, seed), b = run(1, seed);
  printf("%s\n", a && b ? "PASS" : "FAIL"); return !(a && b);
}
