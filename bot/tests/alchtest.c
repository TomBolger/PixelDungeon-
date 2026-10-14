#include "pd.h"
#include "game_int.h"
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) { printf("  log: %s\n", m); }
void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) {}
static int s_pick = -1; void ui_pick_item(PickMode m) { s_pick = m; printf("  pick %d\n", m); }
void ui_hero_tier(void) {}
void ui_amulet(int m) {}
void ui_message(const char *t, const char *x) { printf("  msg: %s\n", x); }
void ui_confirm_jump(void) {}
void ui_sound(int s) {}
void ui_wnd(int m, const char *t, const char *a, const char *b) { printf("  wnd %.70s\n", t); }
const char *g_wnd_title;
void cap_first(char *s) {}
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) {}
static int pot;
static void dump(const char *w) {
  printf("%s: heaps near pot:", w);
  for (int h = 0; h < G.n_heaps; h++) { int d = G.heaps[h].pos - pot; if (d >= -2*MAP_W-2 && d <= 2*MAP_W+2) printf(" [%+d %s kind %d x%d t%d]", d, item_cat(G.heaps[h].item.kind)==CAT_SEED?"seed":item_cat(G.heaps[h].item.kind)==CAT_POTION?"potion":"?", G.heaps[h].item.kind, G.heaps[h].item.qty, G.heaps[h].type); }
  printf("\n  hero %+d fov(pot)=%d plants:", G.hero.pos-pot, BIT_GET(G.fov,pot)); for (int i=0;i<G.n_plants;i++) printf(" %+d", G.plants[i].pos-pot); printf(" sung=%d\n", game_bag_find(IK_SEED_SUNGRASS));
}
int main(void) {
  game_init(); g_new_class = 0; game_new();
  G.n_mobs = 0;
  int h = 16*MAP_W + 16; G.hero.pos = h;
  // build a small clean room around the hero
  for (int y = -4; y <= 4; y++) for (int x = -4; x <= 4; x++) { int c = h + y*MAP_W + x; G.map[c] = (abs(x)==4||abs(y)==4) ? T_WALL : T_EMPTY_SP; }
  for (int i = G.n_heaps-1; i >= 0; i--) { int d = G.heaps[i].pos - h; (void)d; }
  G.n_heaps = 0; G.n_plants = 0;
  pot = h + 2; G.map[pot] = T_ALCHEMY;
  build_los(); update_fov();
  bag_add(item_make(IK_SEED_SUNGRASS, 5)); bag_add(item_make(IK_SEED_FIREBLOOM, 3)); bag_add(item_make(IK_SEED_ICECAP, 2));
  int s;
  printf("== 3 sungrass through the pot\n");
  for (int i = 0; i < 3; i++) { ActResult r = game_tap_cell(pot); s = game_bag_find(IK_SEED_SUNGRASS); ActResult r2 = game_pick(PICK_SEED, s); printf("  tap r=%d slot %d pick r=%d\n", r, s, r2); dump("."); }
  dump("after 3 cooked");
  printf("== throw 1 sungrass at the pot\n");
  s = game_bag_find(IK_SEED_SUNGRASS); game_throw(s, pot); dump("after manual throw");
  printf("== throw 1 firebloom at the pot\n");
  s = game_bag_find(IK_SEED_FIREBLOOM); game_throw(s, pot); dump("after firebloom");
  printf("== cook 1 icecap\n");
  game_tap_cell(pot); s = game_bag_find(IK_SEED_ICECAP); game_pick(PICK_SEED, s); dump("after icecap");
  printf("== cook 1 more sungrass\n");
  game_tap_cell(pot); s = game_bag_find(IK_SEED_SUNGRASS); game_pick(PICK_SEED, s); dump("after sungrass");
  printf("plants %d\n", G.n_plants);
  return 0;
}
