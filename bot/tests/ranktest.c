#include "pd.h"
#include "game_int.h"
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) {}
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
void cap_first(char *s) {}
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) {}
static void show(void) {
  Ranks k; memset(&k, 0, sizeof k);
  if (persist_read_data(KEY_RANKS, &k, 256) == 256) persist_read_data(KEY_RANKS + 1, (uint8_t *)&k + 256, sizeof k - 256);
  printf("total %d won %d latest %d n %d\n", k.total, k.won, k.latest, k.n);
  for (int i = 0; i < k.n; i++) printf("  %d. %u cls %d win %d %s\n", i + 1, (unsigned)k.r[i].score, k.r[i].cls, k.r[i].win, k.r[i].info);
}
int main(void) {
  game_init();
  int lv[] = { 3, 1, 9, 5, 2, 7, 4, 1, 12 };
  for (int g = 0; g < 9; g++) {
    g_new_class = g & 3; game_new();
    G.hero.lvl = lv[g]; G.max_depth = lv[g] + 1;
    uint32_t gc = g * 10; memcpy(G.hero.spare + 2, &gc, 4);
    snprintf(G.death, sizeof G.death, "Killed by test %d on level %d", g, G.max_depth);
    game_erase(); game_rank(g == 8 || g == 3);
    printf("game %d:\n", g); show();
  }
  return 0;
}
