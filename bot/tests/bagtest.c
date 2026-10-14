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

static void dump(const char *w) { printf("%s: bag", w); for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind) printf(" %d:%dx%d", i, G.hero.bag[i].kind, G.hero.bag[i].qty); printf(" | pack"); for (int i = 0; i < 32; i++) if (G.pack[i].kind) printf(" %d:%dx%d", i, G.pack[i].kind, G.pack[i].qty); printf("\n"); }
int main(void) {
  game_init(); g_new_class = 1; game_new();
  bag_add(item_make(IK_SEED_FIREBLOOM, 2)); bag_add(item_make(IK_SCR_IDENTIFY, 1)); bag_add(item_make(IK_WND_SLOW, 1));
  dump("start");
  bag_add(item_make(IK_SEED_POUCH, 1)); dump("pouch");
  bag_add(item_make(IK_SEED_FIREBLOOM, 1)); bag_add(item_make(IK_SEED_ICECAP, 1)); dump("seeds");
  bag_add(item_make(IK_WAND_HOLSTER, 1)); dump("holster");
  printf("find MM %d find icecap %d region scroll %d\n", game_bag_find(IK_WND_MM), game_bag_find(IK_SEED_ICECAP), bag_region(IK_SCR_IDENTIFY));
  G.pack[20].ench = 0; G.pack[21].ench = 0;
  for (int i = 0; i < 100; i++) game_rest(false);
  printf("holster charges %d %d\n", G.pack[20].ench, G.pack[21].ench);
  game_save(); Item p0 = G.pack[0]; memset(G.pack, 0, sizeof G.pack); game_load(); printf("reload pack0 kind %d qty %d (was %d x%d)\n", G.pack[0].kind, G.pack[0].qty, p0.kind, p0.qty);
  int s = game_bag_find(IK_SEED_POUCH); game_item_do(s, ACN_DROP); dump("dropped pouch");
  return 0;
}
