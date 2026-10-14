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
  for (int d = 2; d <= 3; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
  G.hero.weapon = item_make(IK_LONGSWORD, 1); G.hero.weapon.lvl = 5; G.hero.armor = item_make(IK_PLATE_ARMOR, 1); G.hero.ring[0] = item_make(IK_RNG_HASTE, 1); G.hero.ring[1] = item_make(IK_RNG_POWER, 1);
  G.hero.HP = 0; game_erase(); game_rank(0);
  printf("bones key %d\n", persist_exists(KEY_BONES));
  g_new_class = 1; game_new(); G.hero.HP = G.hero.HT = 999;
  // chasm: throw an item into a chasm cell on depth 1 if any
  int pit = -1; for (int c = 0; c < MAP_LEN; c++) if (TFLAGS(G.map[c]) & F_PIT) { pit = c; break; }
  if (pit < 0) { G.map[100] = T_CHASM; pit = 100; }
  heap_put(pit, item_make(IK_TORCH, 1)); printf("fall slot %d->%d\n", G.fall[0].kind, G.fall_d[0]);
  for (int d = 2; d <= 3; d++) {
    switch_level(d, ARRIVE_ENTRANCE); game_level_commit();
    for (int i = 0; i < G.n_heaps; i++) {
      if (G.heaps[i].type == HEAP_SKELETON) { char n[48]; item_name(&G.heaps[i].item, n, sizeof n); printf("d%d skeleton: %s cursed %d lvl %d\n", d, n, !!(G.heaps[i].item.flags & IF_CURSED), G.heaps[i].item.lvl); }
      if (G.heaps[i].item.kind == IK_TORCH) printf("d%d torch at %d\n", d, G.heaps[i].pos);
    }
  }
  printf("bones key after %d\n", persist_exists(KEY_BONES));
  return 0;
}
