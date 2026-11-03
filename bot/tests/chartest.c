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


// chartest (day-WM): the hero is charmed by the monster beside him (Affection, a succubus). Hero.actAttack: he cannot strike it and no turn passes
// (ready()), so an Assist that kept suggesting that attack stood still for ever. Now: another foe in sight is attacked, else he waits; and a press
// that spends no turn makes the next suggestion "search here" (the net: main.c button sets g_ovr.ha 255).   ./chartest [seed]
#define OX 8
#define OY 8
static const char *ART[] = {
  "###########",
  "#.........#",
  "#.........#",
  "#.........#",
  "#....@....#",
  "#.........#",
  "#.........#",
  "#.........#",
  "###########",
};
static int s_none;
static ActResult press(int *spent) {      // one Select press as main.c button does it (the Assist: entry 0)
  int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind);
  if (n <= 0) { *spent = 0; return ACT_NONE; }
  ActResult r; int32_t t0 = G.hero.time;
  if (kind[0] == TK_BAG) r = game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha);
  else if (kind[0] == TK_REST) r = game_rest(true);
  else r = (cell[0] == G.hero.pos && kind[0] == TK_GO) ? game_search() : game_tap_cell(cell[0]);
  for (int k = 0; r == ACT_WALK && k < 400 && G.hero.HP > 0; k++) r = game_continue();
  if (r == ACT_NONE) { s_none++; g_ovr.ha = 255; game_targets(cell, kind); }   // main.c: nothing came of it, the list again
  *spent = G.hero.time != t0 || r != ACT_NONE;
  return r;
}
static void setup(uint32_t seed) {
  g_new_class = 0; game_new(); blob_free_all();
  G.depth = 3; G.n_mobs = 0; G.n_heaps = 0; G.n_plants = 0; G.n_diffs = 0;
  memset(G.hg, 0, sizeof G.hg); memset(G.visited, 0, sizeof G.visited);
  for (int i = 0; i < MAP_LEN; i++) G.map[i] = T_WALL;
  int hero = -1;
  for (int y = 0; y < 9; y++) for (int x = 0; ART[y][x]; x++) { char ch = ART[y][x]; int c = (OY + y) * MAP_W + OX + x;
    if (ch == '@') hero = c; if (ch != '#') G.map[c] = T_EMPTY; }
  build_los();
  G.n_rooms = 0; G.entrance = (OY + 1) * MAP_W + OX + 1; G.exit = (OY + 7) * MAP_W + OX + 9; G.map[G.entrance] = T_ENTRANCE; G.map[G.exit] = T_EXIT;
  G.now = 0; G.turn_due = 0; G.level_t0 = -TICK; G.hero.time = 0; G.hero.pos = hero; G.hero.HP = G.hero.HT = 60;
  G.action = HA_NONE; G.interrupted = false; g_rng = mix(seed, 1, 0x51u) | 1;
  for (int y = 0; y < 9; y++) for (int x = 0; x < 11; x++) BIT_SET(G.visited, (OY + y) * MAP_W + OX + x);
  update_fov(); G.interrupted = false;
  static const uint8_t VIEW1[4] = { 6, 4, 4, 4 }; memcpy(g_view, VIEW1, 4);
}
static int run(int mode, uint32_t seed) {   // 0: a fly beside him, charmed by it; 1: also a rat beside him; 2: the net alone (a press that spent no turn)
  setup(seed); s_none = 0;
  int hp = G.hero.pos;
  int fly = spawn_mob(MT_SWARM, hp + 1, ST_HUNTING, 0), rat = -1;
  if (fly < 0) { printf("spawn failed\n"); return 0; }
  G.mobs[fly].b.charm = 20;                                  // (Affection charms the attacker too: it will not strike)
  if (mode == 1) { rat = spawn_mob(MT_RAT, hp - 1, ST_HUNTING, 0); G.mobs[rat].HP = G.mobs[rat].HT = 400; G.mobs[rat].b.charm = 20; }   // (it too, so nothing hurts him; 400 HP: it stays to be hit)
  if (mode < 2) { G.hero.b.charm = 12; G.charm_uid = G.mobs[fly].uid; }
  update_fov(); G.interrupted = false;
  int stuck = 0, fly_hp = G.mobs[fly].HP, rat_hp = rat >= 0 ? G.mobs[rat].HP : 0, hits_rat = 0, charm_gone = -1, fly_hit = 0;
  for (int step = 0; step < 60 && G.hero.HP > 0; step++) {
    game_level_commit();
    if (mode == 2) { int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; g_ovr.ha = 255; int n = game_targets(cell, kind);
      int ok = n == 1 && kind[0] == TK_GO && cell[0] == G.hero.pos; int sp; ActResult r = press(&sp); (void)r;
      printf("mode 2 (net): list of %d, first GO here %s, ha reset %s\n", n, ok ? "yes" : "NO", g_ovr.ha != 255 ? "yes" : "NO"); return ok && g_ovr.ha != 255; }
    int sp; press(&sp);
    if (!sp) { stuck++; if (stuck >= 2) break; } else stuck = 0;
    if (G.mobs[fly].HP > 0 && G.mobs[fly].HP < fly_hp) fly_hit = 1;
    if (rat >= 0 && G.mobs[rat].HP < rat_hp) { hits_rat++; rat_hp = G.mobs[rat].HP; }
    if (charm_gone < 0 && !G.hero.b.charm) charm_gone = step;
    if (mode == 1 && hits_rat >= 3) break;
    if (mode == 0 && (G.mobs[fly].HP <= 0 || fly_hit) && charm_gone >= 0) break;
  }
  int ok;
  if (mode == 0) ok = stuck < 2 && G.hero.HP > 0 && charm_gone >= 0 && fly_hit;
  else ok = stuck < 2 && G.hero.HP > 0 && hits_rat >= 3 && G.mobs[fly].HP == fly_hp && G.hero.b.charm > 0;
  printf("mode %d: %s, stuck %d, ACT_NONE presses %d, charm gone at press %d, fly hit %d, rat hit %d times, hero hp %d/%d -> %s\n", mode, mode == 0 ? "fly only" : "fly and a rat", stuck, s_none, charm_gone, fly_hit, hits_rat, G.hero.HP, G.hero.HT, ok ? "ok" : "FAIL");
  return ok;
}
int main(int argc, char **argv) {
  if (!game_init()) return 1; g_assist = 3;
  uint32_t seed = argc > 1 ? atoi(argv[1]) : 1;
  int ok = 1;
  for (int m = 0; m < 3; m++) ok &= run(m, seed);
  printf("%s\n", ok ? "PASS" : "FAIL"); return !ok;
}
