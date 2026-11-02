// Host test of the watchface autoplayer's decisions (face_brain.h + the Assist's first suggestion), cheats on (-DPD_FACE).
//   OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE ; build/facetest [games] ; env: STEPS, START, D=1
//   day-WK, the settings (face/README.md): GOD (default 1: the numbers before the settings existed; GOD=0 is the mortal default of the
//   settings page), STAGE=<depth the run starts at: 1 5 6 10 11 15 16 20 21 25>, CLS=<mask: 1 warrior 2 mage 4 rogue 8 huntress; each game
//   picks its class as the face does, never the last one's twice>, FR_REGEN, FR_HUNGER (the speed-ups: 1..), FR_HEAL, FR_UNBREAK, FR_LOOT (0/1)
#include "pd.h"
#include "game_int.h"
#include "face_brain.h"
#include <signal.h>
#include <unistd.h>
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) { if (getenv("V")) printf("  [%d] %s\n", G.depth, m); }
void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) {}
static int s_pick = -1; void ui_pick_item(PickMode m) { s_pick = m; }
static int s_won, s_amu; void ui_amulet(int m) { if (m == 2) s_won = 1; else s_amu++; }
void ui_hero_tier(void) {}
void ui_message(const char *t, const char *x) {}
static bool s_jump; void ui_confirm_jump(void) { s_jump = true; }
void ui_sound(int s) {}
static int s_wnd; void ui_wnd(int m, const char *t, const char *a, const char *b) { if (a) s_wnd = 1; }
const char *g_wnd_title;
void ui_wnd_info(const Item *it) { char b[300]; item_info(it, b, sizeof b); }
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_free_cache(int m) {}
extern int s_pend_depth, s_pend_arrive;
static volatile int cur_game, cur_step;
static void on_alarm(int s) { printf("HANG game=%d step=%d depth=%d\n", cur_game, cur_step, G.depth); _exit(1); }
static ActResult walk(ActResult r) { for (int n = 0; r == ACT_WALK && n < 400 && G.hero.HP > 0; n++) r = game_continue(); game_level_commit(); return r; }
static long hist[16][2];
extern uint32_t g_host_time; extern uint16_t g_host_ms;
static long hp_hist[11], rest_ticks, all_ticks, drinks, heal_drinks, starve_steps, steps_all, starve_games, broken_steps, eats;   // host-only counters: HP% (tenths) per decision, game time spent resting, healing drinks, starving, broken gear
int main(int argc, char **argv) {
  signal(SIGALRM, on_alarm); setvbuf(stdout, NULL, _IONBF, 0);
  if (!game_init()) return 1;
  int N = argc > 1 ? atoi(argv[1]) : 8, maxsteps = getenv("STEPS") ? atoi(getenv("STEPS")) : 60000;
  static const uint8_t VIEW1[4] = { 6, 4, 4, 4 };
  int drops = 0, stuck_up = 0, floods = 0, wins = 0, deaths = 0, nofood = 0, stuckf = 0, floors = 0; long steps_total = 0;
  g_assist = 3;
  g_fr.god = getenv("GOD") ? atoi(getenv("GOD")) : 1;
  if (getenv("STAGE")) g_fr.stage = atoi(getenv("STAGE"));
  if (getenv("FR_REGEN")) g_fr.regen_x = atoi(getenv("FR_REGEN"));
  if (getenv("FR_HUNGER")) g_fr.hunger_x = atoi(getenv("FR_HUNGER"));
  if (getenv("FR_HEAL")) g_fr.heal_free = atoi(getenv("FR_HEAL"));
  if (getenv("FR_UNBREAK")) g_fr.unbreak = atoi(getenv("FR_UNBREAK"));
  if (getenv("FR_LOOT")) g_fr.loot = atoi(getenv("FR_LOOT"));
  int clsmask = getenv("CLS") ? atoi(getenv("CLS")) : 0, lastcls = 0, cls_n[4] = { 0 }, cls_rep = 0, start_bad = 0, stage_turns = 0, full_bag = 0;
  for (int g = getenv("START") ? atoi(getenv("START")) : 0; g < N; g++) {
    cur_game = g; { extern int g_fb_game; g_fb_game = g; } srand(g + 1); if (getenv("VARY")) { g_host_time = 1000000 + g * 3600 * 5; g_host_ms = (uint16_t)(g * 40503u + 7); }   // VARY=1: a different dungeon each game (else a game per class, repeated) memset(g_keys, 0, sizeof g_keys);
    g_new_class = g % 4;
    if (clsmask) { uint32_t r = (uint32_t)(g + 1) * 2654435761u; r ^= r >> 15; r *= 2246822519u; r ^= r >> 13; g_new_class = fb_pick_class(clsmask, g > 0, lastcls, r); if (g > 0 && g_new_class == lastcls && __builtin_popcount(clsmask & 15) > 1) cls_rep++; lastcls = g_new_class; }
    cls_n[g_new_class & 3]++;
    game_new();
    if (g_fr.stage > 1 && (G.depth != g_fr.stage || G.max_depth != g_fr.stage || G.hero.lvl < 5 || G.hero.HP <= 0)) { start_bad++; printf("BAD START game %d: depth %d (wanted %d) lvl %d HP %d\n", g, G.depth, g_fr.stage, G.hero.lvl, G.hero.HP); }
    { int used = 0; for (int i = 0; i < BAG_SIZE; i++) used += G.hero.bag[i].kind != 0; if (used > BAG_SIZE - 3 && g_fr.stage > 1) full_bag++; if (getenv("START_INFO") && g < 4) { printf("start g%d cls %d depth %d lvl %d HP %d/%d STR %d armor %d+%d weapon %d+%d bag %d items; known potions/scrolls:", g, g_new_class & 3, G.depth, G.hero.lvl, G.hero.HP, G.hero.HT, G.hero.STR, G.hero.armor.kind, G.hero.armor.lvl, G.hero.weapon.kind, G.hero.weapon.lvl, used); int kp = 0, ks = 0; for (int k = IK_FIRST_POTION; k < IK_FIRST_POTION + 12; k++) kp += item_known(k); for (int k = IK_FIRST_SCROLL; k < IK_FIRST_SCROLL + 12; k++) ks += item_known(k); printf(" %d/%d\n", kp, ks); } }
    if (getenv("AMU")) { bag_add(item_make(IK_AMULET, 1)); G.dflags |= DF_AMULET; } s_won = 0; s_amu = 0; s_fb_depth = -1; s_wnd = 0; s_jump = false; s_pick = -1; g_trade = 0;
    int32_t pnow = 0; int steps, maxd = G.depth, starved = 0, lastd = G.depth, fl_steps = 0, fl_max = 0, bagacts = 0;
    for (steps = 0; steps < maxsteps && G.hero.HP > 0 && !s_won; steps++) {
      cur_step = steps; alarm(5);
      game_level_commit();
      if (G.depth != lastd) { if (getenv("FS") && (g < 4 || g == atoi(getenv("FS")))) printf("g%d d%d->%d after %d steps lvl %d\n", g, lastd, G.depth, fl_steps, G.hero.lvl); if (getenv("DS") && steps > atoi(getenv("DS")) && steps < atoi(getenv("DS")) + 400) printf("  depth %d->%d at step %d hero %d,%d exit %d,%d entr %d,%d vis exit %d\n", lastd, G.depth, steps, cx(G.hero.pos), cy(G.hero.pos), cx(G.exit), cy(G.exit), cx(G.entrance), cy(G.entrance), BIT_GET(G.visited, G.exit)); floors++; if (fl_steps > 500) { stuckf++; if (game_bag_find(IK_AMULET) >= 0) stuck_up++; if (getenv("FL")) printf("  slow floor g%d d%d->%d: %d steps, amulet %d\n", g, lastd, G.depth, fl_steps, game_bag_find(IK_AMULET) >= 0); } if (fl_steps > fl_max) fl_max = fl_steps; fl_steps = 0; lastd = G.depth; } fl_steps++;
      if (G.depth > maxd) maxd = G.depth;
      if (game_bag_find(IK_RATION) < 0 && game_bag_find(IK_PASTY) < 0) nofood++;
#ifdef FACE_HEAL_CD
      { static int32_t lht = -1000000; if (g_face_heal_t != lht) { lht = g_face_heal_t; drinks++; } }
#endif
      if (getenv("HG") && g == 0 && steps % 400 == 0) printf("step %d d%d turn %d hunger %d HP %d/%d\n", steps, G.depth, G.now / TICK, G.hero.hunger, G.hero.HP, G.hero.HT);
      { static int lh; if (G.hero.hunger + 40 < lh) eats++; lh = G.hero.hunger; }
      { int32_t dt = G.now - pnow; pnow = G.now; if (dt < 0) dt = 0; hp_hist[G.hero.HP * 10 / G.hero.HT] += dt; steps_all += dt; if (game_starving()) { starve_steps += dt; starved = 1; } }   // (time-weighted: a rest of 300 turns counts 300, not one decision)
      { const Item *w = &G.hero.weapon, *a = &G.hero.armor; if ((w->kind && item_broken(w)) || (a->kind && item_broken(a))) broken_steps++; }
      if (s_pick >= 0) { int p = s_pick; s_pick = -1; int ch = -1000, k = G.tgm.pk - 8;
        if (G.tgm.pk && k >= -4 && k < BAG_SIZE && game_pick_ok((PickMode)p, k)) ch = k; G.tgm.pk = 0;
        if (ch == -1000) for (int sl = -4; sl < BAG_SIZE; sl++) if (game_pick_ok((PickMode)p, sl)) { ch = sl; break; }
        walk(game_pick((PickMode)p, ch)); continue; }
      if (s_jump) { s_jump = false; walk(game_jump()); continue; }
      if (s_wnd) { s_wnd = 0; walk(game_wnd_choice(0)); continue; }
      if (getenv("TR") && g == atoi(getenv("TR")) && G.depth == 26 && steps > 5000) printf("d26 step %d hero %d,%d hp %d/%d entr %d,%d exit %d,%d foes %d heaps %d@%d\n", steps, cx(G.hero.pos), cy(G.hero.pos), G.hero.HP, G.hero.HT, cx(G.entrance), cy(G.entrance), cx(G.exit), cy(G.exit), fb_foe(), G.n_heaps, G.n_heaps ? G.heaps[0].pos : -1);
      int ov = fb_override();
      if (ov == FB_LEVEL) { floods++; continue; }
      if (ov == FB_DROP) { drops++; walk(game_item_do(s_fb_slot, ACN_DROP)); continue; }
      if (ov >= 0) { int32_t t = G.hero.time; ActResult r = game_tap_cell(ov); if (getenv("UPD") && game_bag_find(IK_AMULET) >= 0 && fl_steps < 40) printf("up g%d d%d step %d hero %d,%d -> %d,%d r=%d moved=%d vis=%d foe=%d\n", g, G.depth, fl_steps, cx(G.hero.pos), cy(G.hero.pos), cx(ov), cy(ov), r, G.hero.time != t, BIT_GET(G.visited, ov), fb_foe()); if (r != ACT_NONE || G.hero.time != t) { walk(r); continue; } }
      memcpy(g_view, VIEW1, 4);
      int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind);
      if (getenv("TR") && g == atoi(getenv("TR")) && G.depth == 26 && steps > 5000) printf("   targets n=%d first %d,%d kind %d ha %d\n", n, n>0?cx(cell[0]):-1, n>0?cy(cell[0]):-1, n>0?kind[0]:-1, g_ovr.ha);
      if (n <= 0) { walk(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? game_tap_cell(G.hero.pos) : game_search()); continue; }
      if (getenv("FT") && fl_steps > 400 && fl_steps % 40 == 0) { printf("g%d d%d fl %d hero %d,%d hp %d/%d n %d:", g, G.depth, fl_steps, cx(G.hero.pos), cy(G.hero.pos), G.hero.HP, G.hero.HT, n); for (int i = 0; i < n && i < 4; i++) printf(" %d,%d/k%d", cx(cell[i]), cy(cell[i]), kind[i]); for (int q = 0; q < G.n_mobs; q++) if (G.mobs[q].HP > 0 && BIT_GET(G.fov, G.mobs[q].pos)) printf(" [%s hp%d st%d @%d,%d]", mob_name(G.mobs[q].type), G.mobs[q].HP, G.mobs[q].state, cx(G.mobs[q].pos), cy(G.mobs[q].pos)); printf(" exit %d,%d vis %d since %d act %d hunger %d\n", cx(G.exit), cy(G.exit), BIT_GET(G.visited, G.exit), s_fb_since, g_ovr.ha, G.hero.hunger); }
      bool done = false;
      for (int i = 0; i < n && !done; i++) {
        int c = cell[i]; int32_t t = G.hero.time; ActResult r; int isrest = 0; int32_t tn0 = G.now;
        if (i) { g_ovr.c = g_tgobj[i] | 0x4000; g_ovr.t = G.hero.pos - 65536; }
        if (kind[i] == TK_MINE) r = game_item_do(g_ovr.ms, ACN_MINE);
        else if (kind[i] == TK_BAG) { if (g_ovr.ha == ACN_DRINK && G.hero.bag[g_ovr.hs].kind == IK_POT_HEALING) heal_drinks++; r = game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha); bagacts++; }
        else if (kind[i] == TK_REST) { isrest = 1; r = game_rest(true); }
        else if (!i && (g_ovr.d & 128)) { hero_spend(TICK); process(); r = ACT_TURN; }
        else r = (G.hero.b.roots || c == G.hero.pos) && kind[i] == TK_GO ? game_search() : game_tap_cell(c);
        if (r != ACT_NONE || G.hero.time != t) { hist[kind[i] & 15][i ? 1 : 0]++; walk(r); done = true; if (isrest) rest_ticks += G.now - tn0; }
      }
      if (!done) { int32_t tn0 = G.now; walk(game_rest(false)); rest_ticks += G.now - tn0; }
    }
    steps_total += steps; all_ticks += G.now; starve_games += starved;
    if (G.hero.HP <= 0) deaths++;
    stage_turns += G.now / TICK;
    if (s_won) wins++;
    if (getenv("D")) printf("game %d cls %d: %s depth %d (max %d) lvl %d turns %d steps %d HP %d/%d amulet %s worst floor %d steps\n", g, g % 4, s_won ? "WON" : G.hero.HP <= 0 ? "DIED" : "limit", G.depth, maxd, G.hero.lvl, G.now / TICK, steps, G.hero.HP, G.hero.HT, (G.dflags & DF_AMULET) ? "yes" : "no", fl_max);
  }
  if (getenv("H")) for (int k = 0; k < 16; k++) if (hist[k][0] + hist[k][1]) printf("kind %d: entry0 %ld other %ld\n", k, hist[k][0], hist[k][1]);
  if (all_ticks) { printf("rest share of game time %.1f%%, healing drinks %ld (%.1f per game, %.2f per 1000 turns; %ld tried), starving %.1f%% of game time (in %ld games), broken-gear steps %ld, meals eaten %.1f per game\nHP share of game time by tenths 0..10 (%%):", 100.0 * rest_ticks / all_ticks, drinks, (double)drinks / (N ? N : 1), 1000.0 * drinks / (all_ticks / TICK), heal_drinks, 100.0 * starve_steps / steps_all, starve_games, broken_steps, (double)eats / (N ? N : 1));
    for (int k = 0; k < 11; k++) printf(" %.1f", 100.0 * hp_hist[k] / steps_all); printf("\n"); }
  printf("settings: god %d stage %d regen x%d hunger x%d heal %d unbreak %d loot %d; classes used (warrior mage rogue huntress) %d %d %d %d, same class twice in a row %d; bad starts %d, packs without 3 free places %d, game turns played %d\n", g_fr.god, g_fr.stage, g_fr.regen_x, g_fr.hunger_x, g_fr.heal_free, g_fr.unbreak, g_fr.loot, cls_n[0], cls_n[1], cls_n[2], cls_n[3], cls_rep, start_bad, full_bag, stage_turns);
  printf("slow floors while carrying the Amulet: %d\n", stuck_up);
  printf("amulet-clog drops: %d\n", drops); { extern int g_fb_tele, g_fb_tele2; printf("stuck safeguard teleports: %d by the small-area rule, %d by the no-progress rule, in %d games\n", g_fb_tele, g_fb_tele2, N); } printf("== %d games (hidden stairs used %d): won %d, died %d, steps with no food %d, floors %d, floors over 500 steps %d, avg steps %ld\n", N, floods, wins, deaths, nofood, floors, stuckf, steps_total / (N ? N : 1));
}
