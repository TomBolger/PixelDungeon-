#include "pd.h"
#include "game_int.h"
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) { printf("  log: %s\n", m); }
void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) {}
static int s_pick = -1; void ui_pick_item(PickMode m) { s_pick = m; printf("  pick %d\n", m); }
void ui_hero_tier(void) {}
void ui_amulet(int m) { printf("  AMULET %d\n", m); }
void ui_message(const char *t, const char *x) { printf("  msg: %s\n", x); }
void ui_confirm_jump(void) {}
void ui_sound(int s) {}
static int s_btn; void ui_wnd(int m, const char *t, const char *a, const char *b) { s_btn = a != NULL; printf("  wnd %.70s | %s | %s\n", t, a ? a : "", b ? b : ""); }
const char *g_wnd_title;
void cap_first(char *s) {}
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) {}
int main(void) {
  game_init(); g_new_class = 0; game_new();
  for (int s = 0; s < 30; s++) {
    int16_t c[TARGETS_MAX]; uint8_t k[TARGETS_MAX];
    int n = game_targets(c, k);
    printf("hero %d,%d targets %d:", G.hero.pos % MAP_W, G.hero.pos / MAP_W, n);
    for (int i = 0; i < n; i++) printf(" [%d %d,%d t%d]", k[i], c[i] % MAP_W, c[i] / MAP_W, G.map[c[i]]);
    printf("\n");
    if (0) for (int y = G.hero.pos / MAP_W - 4; y <= G.hero.pos / MAP_W + 4; y++) { for (int x = G.hero.pos % MAP_W - 8; x <= G.hero.pos % MAP_W + 8; x++) { int c = y * MAP_W + x; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) { putchar(' '); continue; } putchar(c == G.hero.pos ? '@' : !BIT_GET(G.visited, c) ? ' ' : !PASSABLE(G.map[c]) ? '#' : BIT_GET(G.fov, c) ? '.' : ','); } putchar('\n'); }
    static int16_t rec[6]; static int ri; int pick = 0;
    for (; pick < n; pick++) { bool seen = false; for (int r = 0; r < 6; r++) if (rec[r] == c[pick]) seen = true; if (!seen) break; }
    if (pick == n) pick = 0;
    if (n) c[0] = c[pick], rec[ri++ % 6] = c[pick];
    if (n) { game_tap_cell(c[0]); for (int j = 0; j < 60 && G.action != HA_NONE; j++) game_continue(); }
  }
  return 0;
}
