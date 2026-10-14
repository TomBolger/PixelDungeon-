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
static int s_btn; void ui_wnd(int m, const char *t, const char *a, const char *b) { s_btn = a != NULL; printf("  wnd [%s] %s | %s | %s\n", g_wnd_title ? g_wnd_title : "", t, a ? a : "", b ? b : ""); }
const char *g_wnd_title;
void cap_first(char *s) {}
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) { char b[300]; item_info(it, b, sizeof b); printf("  info: %.60s...\n", b); }
extern int s_pend_depth;
int main(void) {
  game_init(); game_new();
  for (int d = 2; d <= 6; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
  int sk = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_SHOPKEEPER) sk = i;
  printf("depth %d shopkeeper %d at %d hp %d state %d\n", G.depth, sk, sk >= 0 ? G.mobs[sk].pos : -1, sk >= 0 ? G.mobs[sk].HP : 0, sk >= 0 ? G.mobs[sk].state : -1);
  if (sk < 0) return 1;
  // stand two cells away
  int p = G.mobs[sk].pos;
  for (int k = 0; k < 8; k++) { int c = p + 2 * N8[k]; if (PASSABLE(G.map[c]) && !char_at(c)) { G.hero.pos = c; break; } }
  update_fov();
  ActResult r = game_tap_cell(p); printf("tap -> %d action %d\n", r, G.action);
  for (int n = 0; n < 5 && r == ACT_WALK; n++) { r = game_continue(); printf("continue -> %d pos %d\n", r, G.hero.pos); }
  printf("pick %d shopkeeper hp %d\n", s_pick, G.mobs[sk].HP);
  if (s_pick == PICK_SELL) { int sl = -1; for (int i = 0; i < BAG_SIZE; i++) if (game_pick_ok(PICK_SELL, i)) { sl = i; break; }
    s_pick = -1; r = game_pick(PICK_SELL, sl); printf("sell offer slot %d -> %d btn %d\n", sl, r, s_btn);
    r = game_wnd_choice(0); printf("choice -> %d gold %d pick %d\n", r, G.hero.gold, s_pick); }
  return 0;
}
