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
int main(void) {
  game_init();
  g_new_class = 0; game_new();
  for (int d = 2; d <= 24; d++) {
    switch_level(d, ARRIVE_ENTRANCE); game_level_commit();
    memset(G.fov, 0xFF, sizeof G.fov);
    if (g_mh.turn) g_mh.turn();
  }
  static const char *N[11] = { "health", "awareness", "transmut", "sacrif", "alchemy", "garden", "statue", "ghost", "wandmaker", "troll", "imp" };
  for (int k = 0; k < J_COUNT; k++) { printf("%-10s", N[k]); for (int d = 1; d <= 26; d++) if (G.journal[k] >> d & 1) printf(" %d", d); printf("\n"); }
  return 0;
}
