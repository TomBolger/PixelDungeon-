#include "pd.h"
#include "game_int.h"
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) { }
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
static long vol(int b, int *cells) { long t = 0; *cells = 0; if (!g_blob[b]) return 0; for (int i = 0; i < MAP_LEN; i++) if (g_blob[b][i]) { t += g_blob[b][i]; (*cells)++; } return t; }
static void show(const char *w) { printf("%-28s", w); for (int b = 0; b < BLOB_COUNT; b++) { int c; long v = vol(b, &c); printf(" b%d:%ld/%d", b, v, c); } printf("  HP %d depth %d gaskey %d\n", G.hero.HP, G.depth, g_keys[122 + 2 * G.depth].used ? g_keys[122 + 2 * G.depth].len : -1); }
int main(void) {
  game_init(); g_new_class = 0; game_new();
  G.hero.HT = G.hero.HP = 500;
  int c = G.hero.pos;
  // a toxic potion beside the hero, a paralytic one further away, fire on the floor
  potion_shatter(IK_POT_TOXIC, c);
  for (int i = 0; i < 3; i++) game_rest(false);
  show("after toxic + 3 turns");
  game_save();
  uint16_t snap[MAP_LEN]; memcpy(snap, g_blob[BLOB_TOXIC], sizeof snap);
  int hp = G.hero.HP;
  game_load();
  show("after save + load");
  int diff = 0, maxd = 0; for (int i = 0; i < MAP_LEN; i++) { int d = abs((g_blob[BLOB_TOXIC] ? g_blob[BLOB_TOXIC][i] : 0) - snap[i]); if (d) diff++; if (d > maxd) maxd = d; }
  printf("cells differing %d, max diff %d, HP saved %d now %d\n", diff, maxd, hp, G.hero.HP);
  game_rest(false); show("one turn later (hurts?)");
  // leave the floor and come back
  switch_level(2, ARRIVE_ENTRANCE); game_level_commit(); show("on depth 2");
  switch_level(1, ARRIVE_EXIT); game_level_commit(); show("back on depth 1");
  // gas fades: keys go
  for (int i = 0; i < 80; i++) game_rest(false);
  game_save(); show("80 turns later, saved");
  // new game: all gas keys gone
  game_new(); int left = 0; for (int k = 124; k < 176; k++) left += g_keys[k].used; printf("gas keys after new game: %d\n", left);
  return 0;
}
