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

static void show(const char *what) { char n[64]; item_name(&G.hero.armor, n, sizeof n); printf("%s: armor %s (kind %d dr %d str %d) weapon flags %x tier %d HP %d/%d pos %d depth %d gold %d\n", what, n, G.hero.armor.kind, armor_dr(&G.hero.armor), item_str(&G.hero.armor), G.hero.weapon.flags, hero_tier(), G.hero.HP, G.hero.HT, G.hero.pos, G.depth, G.hero.gold); }
int main(void) {
  game_init();
  game_new();
  for (int d = 2; d <= 3; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
  // armor kit on worn plate +3 with a glyph
  G.hero.armor = item_make(IK_PLATE_ARMOR, 1); G.hero.armor.lvl = 3; G.hero.armor.ench = 2; G.hero.armor.flags |= IF_LEVEL_KNOWN;
  G.hero.bag[5] = item_make(IK_ARMOR_KIT, 1);
  show("before kit");
  uint8_t acts[ACN_COUNT]; int n = game_item_actions(5, acts); printf("kit actions:"); for (int i = 0; i < n; i++) printf(" %s", ACTION_NAMES[acts[i]]); printf("\n");
  game_item_do(5, ACN_APPLY); printf("pick %d ok(armor)=%d ok(kit)=%d\n", s_pick, game_pick_ok(PICK_ARMOR, -2), game_pick_ok(PICK_ARMOR, 5));
  ActResult r = game_pick(PICK_ARMOR, -2); printf("r=%d kit slot %d\n", r, game_bag_find(IK_ARMOR_KIT));
  show("after kit");
  n = game_item_actions(-2, acts); printf("armor actions:"); for (int i = 0; i < n; i++) printf(" %s", ACTION_NAMES[acts[i]]); printf("\n");
  // leap 3 cells away
  int t = -1; for (int c = 0; c < MAP_LEN; c++) if (PASSABLE(G.map[c]) && !char_at(c) && abs(c % MAP_W - G.hero.pos % MAP_W) == 3 && c / MAP_W == G.hero.pos / MAP_W) { t = c; break; }
  printf("leap to %d\n", t);
  r = game_aim(2, -2, t); show("after leap");
  // weightstone on the weapon
  G.hero.weapon = item_make(IK_SWORD, 1); G.hero.bag[6] = item_make(IK_WEIGHTSTONE, 2);
  int d0 = attack_delay(&G.hero.weapon);
  s_pick = -1; game_item_do(6, ACN_APPLY); printf("pick %d\n", s_pick);
  game_pick(PICK_WEAPON, -1); game_wnd_choice(0); printf("delay %d -> %d, stones %d\n", d0, attack_delay(&G.hero.weapon), G.hero.bag[6].qty);
  game_pick(PICK_WEAPON, -1); game_wnd_choice(0); printf("flags now %x stones %d\n", G.hero.weapon.flags, G.hero.bag[6].qty);
  // torch
  G.hero.bag[7] = item_make(IK_TORCH, 1); game_item_do(7, ACN_LIGHT); printf("light %d torch %d\n", G.hero.light, G.hero.bag[7].kind);
  // beacon: set here, go down, return
  G.hero.bag[8] = item_make(IK_BEACON, 1);
  int here = G.hero.pos; game_item_do(8, ACN_SET); printf("beacon lvl %d cell %d (pos %d)\n", G.hero.bag[8].lvl, G.hero.bag[8].dur | G.hero.bag[8].hits << 8, here);
  switch_level(4, ARRIVE_ENTRANCE); game_level_commit(); show("at 4");
  r = game_item_do(8, ACN_RETURN); game_level_commit(); show("returned");
  // ankh: die, resurrect
  G.hero.bag[9] = item_make(IK_ANKH, 1); G.hero.bag[10] = item_make(IK_RATION, 2);
  G.hero.gold = 500;
  hero_damage(9999, "Killed by testing on level %d"); show("dead");
  game_ankh(); r = game_wnd_choice(0); printf("resurrect r=%d\n", r); show("alive"); printf("ankh %d ration %d kit... beacon %d mobs %d\n", game_bag_find(IK_ANKH), game_bag_find(IK_RATION), game_bag_find(IK_BEACON), G.n_mobs);
  // ankh on a boss floor
  for (int d = 4; d <= 5; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
  G.hero.bag[9] = item_make(IK_ANKH, 1);
  hero_damage(9999, "Killed by testing on level %d"); game_ankh(); r = game_wnd_choice(0); show("boss resurrect");
  return 0;
}
