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
static int talk(void) {
  int sm = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_BLACKSMITH && G.mobs[i].HP > 0) sm = i;
  if (sm < 0) return -1;
  int p = G.mobs[sm].pos;
  if (!getenv("FAR")) for (int k = 0; k < 8; k++) { int c = p + N8[k]; if (PASSABLE(G.map[c]) && !char_at(c) && !(TFLAGS(G.map[c]) & F_AVOID)) { G.hero.pos = c; break; } }
  update_fov();
  for (int c = 0; c < MAP_LEN; c++) BIT_SET(G.visited, c);
  printf("hero %d smith %d\n", G.hero.pos, p);
  ActResult r = game_tap_cell(p); printf("tap r=%d action %d\n", r, G.action); int n; for (n = 0; n < 80 && r == ACT_WALK; n++) r = game_continue();
  printf("after %d steps r=%d hero %d\n", n, r, G.hero.pos);
  return sm;
}
int main(void) {
  game_init();
  for (int tries = 0; tries < 40; tries++) {
    game_new(); G.hero.HP = G.hero.HT = 999;
    for (int d = 2; d <= 14 && G.q.smith == QS_NONE; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); G.hero.HP = 999; }
    if (G.q.smith == QS_NONE) continue;
    printf("smith at depth %d alt %d (now %d)\n", G.q.smith_depth, G.q.smith_alt, G.depth);
    if (talk() < 0) { printf("no blacksmith mob!\n"); continue; }
    printf("stage %d\n", G.q.smith);
    // complete: give dark gold or bloody pickaxe
    int ps = game_bag_find(IK_PICKAXE); printf("pickaxe slot %d\n", ps);
    if (G.q.smith_alt) G.hero.bag[ps].flags |= IF_BLOODY; else bag_add(item_make(IK_DARK_GOLD, 15));
    talk(); printf("stage %d\n", G.q.smith);
    talk(); printf("btn %d\n", s_btn);
    game_wnd_choice(0); printf("pick %d\n", s_pick);
    G.hero.bag[10] = item_make(IK_MACE, 1); G.hero.bag[10].lvl = 2; G.hero.bag[10].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
    G.hero.bag[11] = item_make(IK_MACE, 1); G.hero.bag[11].lvl = 1; G.hero.bag[11].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
    s_pick = -1; game_pick(PICK_SMITH, 10); printf("pick again %d\n", s_pick);
    game_pick(PICK_SMITH, 11); printf("stage %d mace lvl %d other %d\n", G.q.smith, G.hero.bag[10].lvl, G.hero.bag[11].kind);
    return 0;
  }
  printf("no smith in 40 games\n");
  return 1;
}
