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
  int sm = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_IMP && G.mobs[i].HP > 0) sm = i;
  if (sm < 0) return -1;
  int p = G.mobs[sm].pos;
  for (int k = 0; k < 8; k++) { int c = p + N8[k]; if (PASSABLE(G.map[c]) && !char_at(c) && !(TFLAGS(G.map[c]) & F_AVOID)) { G.hero.pos = c; break; } }
  update_fov();
  ActResult r = game_tap_cell(p); int n; for (n = 0; n < 80 && r == ACT_WALK; n++) r = game_continue();
  return sm;
}
int main(void) {
  game_init();
  for (int tries = 0; tries < 40; tries++) {
    game_new(); G.hero.HP = G.hero.HT = 999;
    for (int d = 2; d <= 19 && G.q.imp == QS_NONE; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); G.hero.HP = 999; }
    if (G.q.imp == QS_NONE) continue;
    printf("imp at depth %d alt %d (now %d)\n", G.q.imp_depth, G.q.imp_alt, G.depth);
    if (talk() < 0) { printf("no imp mob!\n"); continue; }
    printf("stage %d\n", G.q.imp);
    // kill monks/golems: spawn one next to the hero and kill it
    for (int k = 0; k < 8; k++) {
      int t = G.q.imp_alt ? MT_MONK : MT_GOLEM; int c = -1;
      for (int j = 0; j < 8; j++) { int cc = G.hero.pos + N8[j]; if (PASSABLE(G.map[cc]) && !char_at(cc)) { c = cc; break; } }
      int mi = spawn_mob(t, c, ST_HUNTING, 0); if (mi < 0) { printf("spawn failed\n"); break; }
      mob_kill(mi);
      int h = heap_at(c); if (h >= 0) { bag_add(G.heaps[h].item); heap_remove(h); }
    }
    int s = game_bag_find(IK_DWARF_TOKEN); printf("tokens %d\n", s >= 0 ? G.hero.bag[s].qty : 0);
    talk(); printf("btn %d\n", s_btn);
    game_wnd_choice(0); printf("stage %d ring slot %d\n", G.q.imp, game_bag_find(G.q.imp_ring.kind));
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_IMP) printf("imp hp %d\n", G.mobs[i].HP);
    switch_level(20, ARRIVE_ENTRANCE); game_level_commit(); switch_level(21, ARRIVE_ENTRANCE); game_level_commit();
    int shop = 0; for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_SHOP) shop = 1;
    int imp = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_IMP_SHOP || G.mobs[i].type == MT_IMP) imp = G.mobs[i].type;
    printf("d21 shop %d imp type %d heaps %d\n", shop, imp, G.n_heaps);
    return 0;
  }
  printf("no imp in 40 games\n");
  return 1;
}
