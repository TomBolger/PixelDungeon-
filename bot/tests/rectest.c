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
static int count_markers(int d) { int n = 0; for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_TORCH) n += G.heaps[i].item.qty; return n == 3 * (100 + d) ? 3 : n; }
int main(void) {
  game_init();
  g_new_class = 0; game_new();
  G.hero.HP = G.hero.HT = 999;
  int top = getenv("TOP") ? atoi(getenv("TOP")) : 24;
  for (int d = 1; d <= top; d++) {
    if (d > 1) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
    for (int k = 0; k < 3; k++) { Item it = item_make(IK_TORCH, 1); it.qty = 100 + d; heap_put(G.hero.pos, it); }   // three things left on each floor
    for (int k = 0; k < 6; k++) { Item it = item_make(IK_FIRST_WAND + k, 1); heap_put(G.entrance, it); }
    game_save();
  }
  printf("persist total %d, fails %d\n", persist_total(), g_persist_fail);
  int lost = 0;
  for (int d = top - 1; d >= 1; d--) {
    switch_level(d, ARRIVE_EXIT); game_level_commit();
    int m = count_markers(d); if (m != 3) { lost++; printf("depth %d: %d markers, %d heaps, %d mobs\n", d, m, G.n_heaps, G.n_mobs); }
    game_save();
  }
  printf("floors with lost items: %d; persist total %d, fails %d\n", lost, persist_total(), g_persist_fail);
  // reload check
  Game copy = G; game_load(); printf("reload: depth %d hero %d heaps %d/%d\n", G.depth, memcmp(&G.hero, &copy.hero, sizeof G.hero), G.n_heaps, copy.n_heaps);
  return 0;
}
