// Host test of the watchface autoplayer's decisions (face_brain.h + the Assist's first suggestion), cheats on (-DPD_FACE).
//   OUT=facetest MAIN=tests/facetest.c ./build.sh -DPD_FACE ; build/facetest [games] ; env: STEPS, START, D=1
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
int main(int argc, char **argv) {
  signal(SIGALRM, on_alarm); setvbuf(stdout, NULL, _IONBF, 0);
  if (!game_init()) return 1;
  int N = argc > 1 ? atoi(argv[1]) : 8, maxsteps = getenv("STEPS") ? atoi(getenv("STEPS")) : 60000;
  static const uint8_t VIEW1[4] = { 6, 4, 4, 4 };
  int floods = 0, wins = 0, deaths = 0, nofood = 0, stuckf = 0, floors = 0; long steps_total = 0;
  g_assist = 3;
  for (int g = getenv("START") ? atoi(getenv("START")) : 0; g < N; g++) {
    cur_game = g; srand(g + 1); memset(g_keys, 0, sizeof g_keys);
    g_new_class = g % 4; game_new(); s_won = 0; s_amu = 0; s_fb_depth = -1;
    int steps, maxd = 1, lastd = 1, fl_steps = 0, fl_max = 0, bagacts = 0;
    for (steps = 0; steps < maxsteps && G.hero.HP > 0 && !s_won; steps++) {
      cur_step = steps; alarm(5);
      game_level_commit();
      if (G.depth != lastd) { floors++; if (fl_steps > 500) stuckf++; if (fl_steps > fl_max) fl_max = fl_steps; fl_steps = 0; lastd = G.depth; } fl_steps++;
      if (G.depth > maxd) maxd = G.depth;
      if (game_bag_find(IK_RATION) < 0 && game_bag_find(IK_PASTY) < 0) nofood++;
      if (s_pick >= 0) { int p = s_pick; s_pick = -1; int ch = -1000, k = G.tgm.pk - 8;
        if (G.tgm.pk && k >= -4 && k < BAG_SIZE && game_pick_ok((PickMode)p, k)) ch = k; G.tgm.pk = 0;
        if (ch == -1000) for (int sl = -4; sl < BAG_SIZE; sl++) if (game_pick_ok((PickMode)p, sl)) { ch = sl; break; }
        walk(game_pick((PickMode)p, ch)); continue; }
      if (s_jump) { s_jump = false; walk(game_jump()); continue; }
      if (s_wnd) { s_wnd = 0; walk(game_wnd_choice(0)); continue; }
      int ov = fb_override();
      if (ov == FB_LEVEL) { floods++; continue; }
      if (ov >= 0) { int32_t t = G.hero.time; ActResult r = game_tap_cell(ov); if (r != ACT_NONE || G.hero.time != t) { walk(r); continue; } }
      memcpy(g_view, VIEW1, 4);
      int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind);
      if (n <= 0) { walk(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? game_tap_cell(G.hero.pos) : game_search()); continue; }
      if (getenv("FT") && fl_steps > 400 && fl_steps % 40 == 0) { printf("g%d d%d fl %d hero %d,%d hp %d/%d n %d:", g, G.depth, fl_steps, cx(G.hero.pos), cy(G.hero.pos), G.hero.HP, G.hero.HT, n); for (int i = 0; i < n && i < 4; i++) printf(" %d,%d/k%d", cx(cell[i]), cy(cell[i]), kind[i]); for (int q = 0; q < G.n_mobs; q++) if (G.mobs[q].HP > 0 && BIT_GET(G.fov, G.mobs[q].pos)) printf(" [%s hp%d st%d @%d,%d]", mob_name(G.mobs[q].type), G.mobs[q].HP, G.mobs[q].state, cx(G.mobs[q].pos), cy(G.mobs[q].pos)); printf(" exit %d,%d vis %d since %d act %d hunger %d\n", cx(G.exit), cy(G.exit), BIT_GET(G.visited, G.exit), s_fb_since, g_ovr.ha, G.hero.hunger); }
      bool done = false;
      for (int i = 0; i < n && !done; i++) {
        int c = cell[i]; int32_t t = G.hero.time; ActResult r;
        if (i) { g_ovr.c = g_tgobj[i] | 0x4000; g_ovr.t = G.hero.pos - 65536; }
        if (kind[i] == TK_MINE) r = game_item_do(g_ovr.ms, ACN_MINE);
        else if (kind[i] == TK_BAG) { r = game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha); bagacts++; }
        else if (kind[i] == TK_REST) r = game_rest(true);
        else if (!i && (g_ovr.d & 128)) { hero_spend(TICK); process(); r = ACT_TURN; }
        else r = (G.hero.b.roots || c == G.hero.pos) && kind[i] == TK_GO ? game_search() : game_tap_cell(c);
        if (r != ACT_NONE || G.hero.time != t) { hist[kind[i] & 15][i ? 1 : 0]++; walk(r); done = true; }
      }
      if (!done) walk(game_rest(false));
    }
    steps_total += steps;
    if (G.hero.HP <= 0) deaths++;
    if (s_won) wins++;
    if (getenv("D")) printf("game %d cls %d: %s depth %d (max %d) lvl %d turns %d steps %d HP %d/%d amulet %s worst floor %d steps\n", g, g % 4, s_won ? "WON" : G.hero.HP <= 0 ? "DIED" : "limit", G.depth, maxd, G.hero.lvl, G.now / TICK, steps, G.hero.HP, G.hero.HT, (G.dflags & DF_AMULET) ? "yes" : "no", fl_max);
  }
  if (getenv("H")) for (int k = 0; k < 16; k++) if (hist[k][0] + hist[k][1]) printf("kind %d: entry0 %ld other %ld\n", k, hist[k][0], hist[k][1]);
  printf("== %d games (hidden stairs used %d): won %d, died %d, steps with no food %d, floors %d, floors over 500 steps %d, avg steps %ld\n", N, floods, wins, deaths, nofood, floors, stuckf, steps_total / (N ? N : 1));
}
