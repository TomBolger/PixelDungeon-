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

static void hero(const char *w) { char n[48], a[48]; item_name(&G.hero.weapon, n, sizeof n); item_name(&G.hero.armor, a, sizeof a);
  printf("%s: cls %d HP %d/%d STR %d weapon %s armor %s ring0 %d quick %d pos %d\n", w, G.hero.hclass, G.hero.HP, G.hero.HT, G.hero.STR, n, a, G.hero.ring[0].kind, G.hero.quick[0], G.hero.pos); }
static int near_mob(void) { int best = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i)) { if (best < 0 || distance(G.mobs[i].pos, G.hero.pos) < distance(G.mobs[best].pos, G.hero.pos)) best = i; } return best; }
static void put_next_to(int m) { for (int k = 0; k < 8; k++) { int c = G.mobs[m].pos + N8[k]; if (PASSABLE(G.map[c]) && !char_at(c)) { G.hero.pos = c; break; } } update_fov(); }
int main(void) {
  game_init();
  for (int cls = 0; cls < 4; cls++) {
    g_new_class = cls; game_new(); hero("new");
    uint8_t acts[ACN_COUNT]; int n;
    for (int s = 0; s < 3; s++) { if (!G.hero.bag[s].kind) continue; n = game_item_actions(s, acts); char nm[40]; item_name(&G.hero.bag[s], nm, sizeof nm); printf("  %s:", nm); for (int i = 0; i < n; i++) printf(" %s", ACTION_NAMES[acts[i]]); printf("\n"); }
    G.hero.HP = G.hero.HT = 200;
    // class armour special
    G.hero.armor = item_make(IK_ARMOR_WARRIOR + "\0\2\1\3"[cls], 1); G.hero.armor.dur = 10; G.hero.armor.hits = 10;
    for (int d = 2; d <= 3; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
    int m = near_mob(); if (m < 0) { printf("no mob\n"); continue; }
    put_next_to(m);
    int hp0 = G.mobs[m].HP;
    ActResult r = game_aim(2, -2, G.hero.pos + 2);
    printf("  special r=%d mob %d hp %d->%d blind %d roots %d burn %d para %d HP %d\n", r, G.mobs[m].type, hp0, G.mobs[m].HP, G.mobs[m].b.blind, G.mobs[m].b.roots, G.mobs[m].b.burning, G.mobs[m].b.paralysis, G.hero.HP);
    if (cls == 1) {   // disenchant: upgrade MM first, then into another wand
      int mm = game_bag_find(IK_WND_MM); G.hero.bag[mm].lvl = 2;
      G.hero.bag[5] = item_make(IK_WND_FIREBOLT, 1);
      n = game_item_actions(mm, acts); printf("  MM+2:"); for (int i = 0; i < n; i++) printf(" %s", ACTION_NAMES[acts[i]]); printf("\n");
      game_item_do(mm, ACN_DISENCHANT); printf("  pick %d ok(fb)=%d ok(mm)=%d\n", s_pick, game_pick_ok(PICK_WAND, 5), game_pick_ok(PICK_WAND, mm));
      game_pick(PICK_WAND, 5); printf("  firebolt lvl %d mm slot %d\n", G.hero.bag[5].lvl, game_bag_find(IK_WND_MM));
      // equip a wand and hit with it
      game_item_do(5, ACN_EQUIP); printf("  weapon kind %d min %d max %d\n", G.hero.weapon.kind, weapon_min(&G.hero.weapon), weapon_max(&G.hero.weapon));
      G.hero.subclass = SUB_BATTLEMAGE; G.hero.weapon.ench = 0;
      m = near_mob(); if (m >= 0) { put_next_to(m); for (int k = 0; k < 4 && G.mobs[m].HP > 0; k++) attack(-1, m, NULL); printf("  battlemage charges %d\n", G.hero.weapon.ench); }
    }
    if (cls == 3) {   // boomerang returns
      int b = game_bag_find(IK_BOOMERANG);
      m = near_mob(); if (m >= 0) { int c = G.mobs[m].pos; for (int k = 0; k < 8; k++) { int c2 = c + 2 * N8[k]; if (c2 > 0 && c2 < MAP_LEN && PASSABLE(G.map[c2]) && PASSABLE(G.map[c + N8[k]]) && !char_at(c2) && !char_at(c + N8[k])) { G.hero.pos = c2; break; } } update_fov(); }
      if (m >= 0) { int tgt = G.mobs[m].pos; ActResult r2 = game_throw(b, tgt); printf("  boomerang r=%d slot %d heaps at target %d mobhp %d\n", r2, game_bag_find(IK_BOOMERANG), heap_at(tgt), G.mobs[m].HP); for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == IK_BOOMERANG) printf("  boomerang heap at %d (hero %d)\n", G.heaps[h].pos, G.hero.pos); }
    }
  }
  return 0;
}
