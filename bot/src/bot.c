// Automated games against the real rules (host build, ASan): random play that uses every kind of item.
#include "pd.h"
#include "game_int.h"
#include <signal.h>
#include <unistd.h>
PKey g_keys[256]; int g_persist_fail;
void ui_log(const char *m, MsgColor c) { if (getenv("V")) printf("  [%d] log: %s\n", G.depth, m); }
void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
static int s_zaps, s_bad_zap;
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) { s_zaps++; if (n < 0 || n > BOLT_MAX + 1) s_bad_zap++; for (int i = 0; i < n; i++) if (pts[i] < 0 || pts[i] >= MAP_LEN) s_bad_zap++; }
static int s_pick = -1; void ui_pick_item(PickMode m) { s_pick = m; }
static int s_won; void ui_amulet(int m) { s_won = m + 1; }
void ui_hero_tier(void) { if (hero_tier() < 0 || hero_tier() > 6) { printf("BAD TIER %d\n", hero_tier()); _exit(1); } }
void ui_message(const char *t, const char *x) { if (getenv("V")) printf("  msg: %s\n", x); }
static bool s_jump; void ui_confirm_jump(void) { s_jump = true; }
void ui_sound(int s) {}
static int s_wnd_btn; void ui_wnd(int m, const char *t, const char *a, const char *b) { if (a) s_wnd_btn = 1; if (getenv("V")) printf("  wnd: %s\n", t); }
const char *g_wnd_title;
void ui_wnd_info(const Item *it) { char b[300]; item_info(it, b, sizeof b); }
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_free_cache(int m) {}
static volatile int cur_game, cur_step;
static void on_alarm(int s) { printf("HANG game=%d step=%d depth=%d action=%d\n", cur_game, cur_step, G.depth, G.action); fflush(stdout); _exit(1); }
static int rnd(int n) { return rand() % n; }
extern int s_pend_depth, s_pend_arrive;
static int s_act_hist[16];
static int walk(ActResult r) { int n = 0; while (r == ACT_WALK && n < 200 && G.hero.HP > 0) { r = game_continue(); n++; }
  if (s_pend_depth > LAST_DEPTH) printf("PEND(walk) depth %d arrive %d from depth %d cell %d terrain %d exit %d\n", s_pend_depth, s_pend_arrive, G.depth, G.hero.pos, G.map[G.hero.pos], G.exit);
  game_level_commit(); return r; }
static int shop_taps, rooms_seen[32];
static int uses[ACN_COUNT], kinds_used[IK_COUNT], npc_talks[MT_COUNT], boss_dead[30], boss_alive[30];
static void check(int g) {                         // invariants after every step
  if (G.n_mobs > MAX_MOBS || G.n_heaps > MAX_HEAPS || G.n_diffs > MAX_DIFFS) { printf("OVERFLOW game=%d\n", g); _exit(1); }
  for (int i = 0; i < BAG_CAP; i++) { Item *it = &G.hero.bag[i]; if (it->kind >= IK_COUNT || (it->kind && !it->qty)) { printf("BAD BAG game=%d slot %d kind %d qty %d\n", g, i, it->kind, it->qty); _exit(1); } }
  if (G.hero.HP > 0 && (G.hero.pos < 0 || G.hero.pos >= MAP_LEN)) { printf("BAD POS game=%d\n", g); _exit(1); }
}
static int imp_stage[5], wins;
static int game_attack_target(void) {               // (0.25: gone from the app; the bot keeps PD's AttackIndicator choice)
  int last = G.last_target >= 0 ? mob_at(G.last_target) : -1;
  if (last >= 0 && !ally(last) && BIT_GET(G.fov, G.mobs[last].pos)) return last;
  int c = game_nearest_enemy();
  return c >= 0 ? mob_at(c) : -1;
}
int main(int argc, char **argv) {
  signal(SIGALRM, on_alarm); setvbuf(stdout, NULL, _IONBF, 0);
  if (!game_init()) return 1;
  int N = argc > 1 ? atoi(argv[1]) : 200;
  static int goo_dead, goo_alive, ghost_stage[8], wand_stage[8], smith_stage[8];
  int deaths = 0, maxd[30] = {0}, maxsave = 0, maxpersist = 0, turns = 0;
  char title[40], desc[640], buf[640];
  for (int g = getenv("START") ? atoi(getenv("START")) : 0; g < N; g++) {
    cur_game = g; srand(g + 1);
    memset(g_keys, 0, sizeof g_keys);
    g_new_class = getenv("CLS") ? atoi(getenv("CLS")) : g % 4;
    game_new();
    if (getenv("SD")) { for (int d = 2; d <= atoi(getenv("SD")); d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); } G.hero.lvl = 20; G.hero.HT = G.hero.HP = 120; G.hero.STR = 16;
      if (atoi(getenv("SD")) >= 14) { G.hero.STR = 18; G.hero.weapon = item_make(IK_LONGSWORD, 1); G.hero.weapon.lvl = 3; G.hero.armor = item_make(IK_SCALE_ARMOR, 1); G.hero.armor.lvl = 3; } }
    if (g % 2 || getenv("W")) for (int i = 2; i < BAG_SIZE; i++) {   // every other game: a backpack of random items of every kind
      int k = IK_RATION + rnd(IK_COUNT - IK_RATION);
      if (getenv("W")) k = rnd(2) ? IK_FIRST_WAND + rnd(N_WANDS) : IK_FIRST_RING + rnd(N_RINGS);
      if (k == IK_AMULET) k = IK_RATION;
      G.hero.bag[i] = item_make(k, item_stackable(k) ? 1 + rnd(4) : 1);
      if (rnd(3) == 0) { G.hero.bag[i].lvl = rnd(7) - 3; if (G.hero.bag[i].lvl < 0) G.hero.bag[i].flags |= IF_CURSED; }
      if (rnd(2)) G.hero.bag[i].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
      if ((item_cat(k) == CAT_WEAPON || item_cat(k) == CAT_ARMOR) && rnd(2)) item_enchant(&G.hero.bag[i]);
    }
    if (g % 4 == 1) {                              // and sometimes enchanted gear already on
      G.hero.weapon = item_make(IK_SWORD, 1); item_enchant(&G.hero.weapon);
      G.hero.armor = item_make(IK_LEATHER_ARMOR, 1); item_enchant(&G.hero.armor);
    }
    int steps;
    int maxsteps = getenv("STEPS") ? atoi(getenv("STEPS")) : 6000;
    s_won = 0;
    for (steps = 0; steps < maxsteps && G.hero.HP > 0 && !s_won; steps++) {
      cur_step = steps; alarm(3);
      if (s_pend_depth > LAST_DEPTH) printf("PEND depth %d arrive %d from depth %d cell %d terrain %d exit %d\n", s_pend_depth, s_pend_arrive, G.depth, G.hero.pos, G.map[G.hero.pos], G.exit);
      game_level_commit();                         // the UI commits a floor change after its fade
      check(g);
      if (G.depth > LAST_DEPTH) { printf("TOO DEEP game=%d step=%d depth=%d from action %d\n", g, steps, G.depth, G.action); _exit(1); }
      if (getenv("KT") && G.depth == 20 && steps % 20 == 0) { int k = -1, u = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) { if (G.mobs[i].type == MT_KING) k = i; if (G.mobs[i].type == MT_UNDEAD) u++; }
        printf("s%d hero %d/%d pos %d king %d hp %d pos %d st %d undead %d boss %x para %d\n", steps, G.hero.HP, G.hero.HT, G.hero.pos, k, k >= 0 ? G.mobs[k].HP : 0, k >= 0 ? G.mobs[k].pos : 0, k >= 0 ? G.mobs[k].state : 0, u, G.boss, G.hero.b.paralysis); }
      if (getenv("YT") && G.depth == 25 && steps % 25 == 0) { int y = -1, f = 0, l = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) { if (G.mobs[i].type == MT_YOG) y = i; if (G.mobs[i].type == MT_ROTTING_FIST || G.mobs[i].type == MT_BURNING_FIST) f++; if (G.mobs[i].type == MT_LARVA) l++; }
        printf("s%d hero %d/%d pos %d yog hp %d pos %d fists %d larvae %d boss %x map[entr] %d\n", steps, G.hero.HP, G.hero.HT, G.hero.pos, y >= 0 ? G.mobs[y].HP : 0, y >= 0 ? G.mobs[y].pos : -1, f, l, G.boss, G.map[G.entrance]); }
      if (getenv("KLOW") && G.depth == 20) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].type == MT_KING && G.mobs[i].HP > 20) G.mobs[i].HP = 20;
      if (getenv("GOD")) { G.hero.HP = G.hero.HT; G.hero.hunger = 0; if (G.hero.lvl < 30 && steps % 50 == 0) earn_exp(hero_max_exp()); }
      if (s_pick >= 0) {                           // an item selector is open: choose something it allows, or cancel
        int p = s_pick; s_pick = -1, cur_step = -p;
        int choice = -1000;
        if (rnd(5)) for (int sl = -4; sl < BAG_SIZE; sl++) if (game_pick_ok((PickMode)p, sl) && rnd(2)) { choice = sl; break; }
        walk(game_pick((PickMode)p, choice));
        continue;
      }
      if (s_jump) { s_jump = false; if (rnd(3) == 0) walk(game_jump()); }
      if (s_wnd_btn) { s_wnd_btn = 0; walk(game_wnd_choice(rnd(2))); continue; }
      if (rnd(20) == 0) {                          // inspect a random cell and read an item's info: the info overlay
        game_inspect(rnd(MAP_LEN), title, sizeof title, desc, sizeof desc);
        int sl = rnd(BAG_SIZE + 2) - 2; Item *it = game_slot(sl);
        if (it->kind) item_info(it, buf, sizeof buf);
      }
      if (rnd(25) == 0) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_NPC) && G.mobs[i].type != MT_RAT_KING && BIT_GET(G.fov, G.mobs[i].pos)) { npc_talks[G.mobs[i].type]++; walk(game_tap_cell(G.mobs[i].pos)); break; }   // talk to NPCs
      if (rnd(40) == 0) for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].type == HEAP_FOR_SALE && BIT_GET(G.visited, G.heaps[h].pos)) { shop_taps++; walk(game_tap_cell(G.heaps[h].pos)); break; }
      if (rnd(8) == 0) for (int c = 0; c < MAP_LEN; c++) if ((G.map[c] == T_LOCKED_DOOR || G.map[c] == T_LOCKED_EXIT) && BIT_GET(G.visited, c)) { walk(game_tap_cell(c)); break; }
      if (G.hero.b.roots && rnd(2)) { walk(game_rest(false)); continue; }
      int hs = game_bag_find(IK_POT_HEALING);
      if (G.hero.HP < G.hero.HT / 3 && hs >= 0) { walk(game_item_do(hs, ACN_DRINK)); continue; }
      int fs = game_bag_find(IK_RATION); if (fs < 0) fs = game_bag_find(IK_PASTY);
      if (game_hungry() && fs >= 0) { walk(game_item_do(fs, ACN_EAT)); continue; }
      int at = game_attack_target();
      int r = rnd(100);
      if (at >= 0 && r < 60) { walk(game_tap_cell(G.mobs[at].pos)); continue; }
      if (r < 4) { game_search(); continue; }
      if (r < 6) { walk(game_rest(rnd(4) == 0)); continue; }
      if (r < 22) {                                // use a random item with a random allowed action
        int sl = rnd(BAG_SIZE + 4) - 4;
        if (!game_slot(sl)->kind) continue;
        uint8_t acts[ACN_COUNT]; int n = game_item_actions(sl, acts);
        if (!n) continue;
        int a = acts[rnd(n)];
        if (a == ACN_QUICK || ((a == ACN_UNEQUIP || a == ACN_DROP) && rnd(6))) continue;
        int k = game_slot(sl)->kind;
        uses[a]++; kinds_used[k]++;
        if (a == ACN_ZAP) {
          int t = game_nearest_enemy(); if (t < 0 || rnd(4) == 0) t = rnd(MAP_LEN);
          walk(game_zap(sl, t));
        } else if (a == ACN_THROW) {
          int t = game_nearest_enemy(); if (t < 0) t = rnd(MAP_LEN);
          if (rnd(3) == 0 && game_nearest_enemy() >= 0) walk(game_quick_throw(sl)); else walk(game_throw(sl, t));
        } else walk(game_item_do(sl, (ItemAction)a));
        continue;
      }
      if (r < 25) { game_save(); int sz = game_save_size(); if (sz > maxsave) maxsave = sz; int pt = persist_total(); if (pt > maxpersist) maxpersist = pt;
                    static Game copy; copy = G;
                    if (!game_load()) { printf("LOAD FAIL game=%d\n", g); return 1; }
                    if (G.hero.pos != copy.hero.pos || G.n_heaps != copy.n_heaps || memcmp(G.map, copy.map, MAP_LEN) || memcmp(&G.hero, &copy.hero, sizeof G.hero)) {
                      printf("LOAD MISMATCH game=%d depth=%d heaps %d/%d map %d hero %d\n", g, G.depth, G.n_heaps, copy.n_heaps, memcmp(G.map, copy.map, MAP_LEN), memcmp(&G.hero, &copy.hero, sizeof G.hero));
                      for (int i = 0; i < MAP_LEN; i++) if (G.map[i] != copy.map[i]) { printf(" cell %d: %d vs %d\n", i, G.map[i], copy.map[i]); break; }
                      return 1; }
                    continue; }
      int target = -1;
      static int heap_cool; if (heap_cool) heap_cool--;
      for (int i = 0; i < G.n_heaps && target < 0 && !heap_cool; i++) if (G.heaps[i].pos < MAP_LEN && G.heaps[i].type != HEAP_FOR_SALE && BIT_GET(G.visited, G.heaps[i].pos) && rnd(2)) target = G.heaps[i].pos;
      if (target < 0 && BIT_GET(G.visited, G.exit) && rnd(100) < (getenv("GOD") ? 60 : 25) + steps / 100) target = G.exit;
      if (target < 0 && G.depth > 1 && BIT_GET(G.visited, G.entrance) && rnd(100) < 3) target = G.entrance;
      if (target < 0) {
        for (int k = 0; k < 50; k++) { int c = rnd(MAP_LEN); if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) { target = c; break; } }
        for (int k = 0; k < 400; k++) { int c = rnd(MAP_LEN); if (!BIT_GET(G.visited, c) || !PASSABLE(G.map[c])) continue;
          bool fr = false; for (int d = -1; d <= 1; d += 2) { if (c + d >= 0 && c + d < MAP_LEN && !BIT_GET(G.visited, c + d)) fr = true; if (c + d * MAP_W >= 0 && c + d * MAP_W < MAP_LEN && !BIT_GET(G.visited, c + d * MAP_W)) fr = true; }
          if (fr) { target = c; break; } }
      }
      if (target >= 0) { int r0 = game_tap_cell(target); if (r0 == ACT_NONE && heap_at(target) >= 0) heap_cool = 30; if (getenv("TR") && steps > maxsteps - 30) printf("step %d target %d (exit %d) r=%d pos %d action %d roots %d para %d frost %d map %d\n", steps, target, G.exit, r0, G.hero.pos, G.action, G.hero.b.roots, G.hero.b.paralysis, G.hero.b.frost, G.map[G.hero.pos]); walk(r0); }
    }
    turns += G.now / TICK;
    if (G.depth == 5) { bool goo = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO) goo = true; if (!goo) goo_dead++; else goo_alive++; }
    for (int d = 5; d <= G.max_depth && d <= 25; d += 5) {
      bool alive = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) alive = true;
      bool open = false; for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_UNLOCKED_EXIT || G.map[c] == T_EXIT) open = true;
      if (G.max_depth > d || (G.depth == d && !alive && open)) boss_dead[d]++; else if (G.depth == d) boss_alive[d]++; }
    if (G.q.wand) wand_stage[G.q.wand]++;
    if (G.q.smith) smith_stage[G.q.smith]++;
    if (G.q.imp) imp_stage[G.q.imp]++;
    for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type < 32) rooms_seen[G.rooms[r].type]++;
    if (getenv("B10") && G.depth == 10) { int t = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_TENGU) t = i;
      int ld = 0, le = 0; for (int c = 0; c < MAP_LEN; c++) { if (G.map[c] == T_LOCKED_DOOR) ld++; if (G.map[c] == T_LOCKED_EXIT) le++; }
      int ch = 0; for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == IK_IRON_KEY) ch = G.heaps[h].type + 1;
      printf("d10 game %d: boss %d tengu %d hp %d keys %x lockeddoors %d lockedexit %d keyheap %d\n", g, G.boss, t, t >= 0 ? G.mobs[t].HP : 0, G.keys[10], ld, le, ch); }
    if (G.q.ghost) ghost_stage[G.q.ghost]++;
    if (G.hero.HP <= 0) { deaths++; if (getenv("D")) printf("game %d died depth %d lvl %d: %s\n", g, G.depth, G.hero.lvl, G.death); }
    maxd[G.max_depth]++; if (s_won) { wins++; if (getenv("D")) printf("game %d WON (mode %d) turn %d\n", g, s_won - 1, G.now / TICK); }
    if (getenv("D1") && G.max_depth == 1) { int vis = 0; for (int c = 0; c < MAP_LEN; c++) vis += BIT_GET(G.visited, c);
      printf("game %d stuck d1: exit %d visited %d hero %d map[exit] %d visited cells %d hp %d action %d para %d frost %d roots %d pick %d time %d now %d\n", g, G.exit, BIT_GET(G.visited, G.exit), G.hero.pos, G.map[G.exit], vis, G.hero.HP, G.action, G.hero.b.paralysis, G.hero.b.frost, G.hero.b.roots, s_pick, G.hero.time, G.now);
      for (int y = 0; y < MAP_H; y++) { for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W; putchar(c == G.hero.pos ? '@' : c == G.exit ? '>' : !BIT_GET(G.visited, c) ? ' ' : PASSABLE(G.map[c]) ? '.' : G.map[c] == T_LOCKED_DOOR ? 'L' : '#'); } putchar('\n'); } }
  }
  printf("%d games: deaths %d, avg turns %d, max save %d B, max persist %d B, persist fails %d\nmax depth:", N, deaths, turns / N, maxsave, maxpersist, g_persist_fail);
  for (int d = 1; d <= LAST_DEPTH + 1 && d < 28; d++) if (maxd[d]) printf(" %d:%d", d, maxd[d]); printf("\n");
  printf("actions:"); for (int a = 0; a < ACN_COUNT; a++) printf(" %s %d", ACTION_NAMES[a], uses[a]); printf("\n");
  printf("goo dead %d alive %d; ghost stages: spawned %d given %d done %d rewarded %d\n", goo_dead, goo_alive, ghost_stage[1], ghost_stage[2], ghost_stage[3], ghost_stage[4]);
  printf("wins %d\n", wins);
  printf("imp stages: spawned %d given %d done %d rewarded %d\n", imp_stage[1], imp_stage[2], imp_stage[3], imp_stage[4]);
  printf("smith stages: spawned %d given %d done %d rewarded %d\n", smith_stage[1], smith_stage[2], smith_stage[3], smith_stage[4]);
  printf("wandmaker stages: spawned %d given %d rewarded %d; npc talks:", wand_stage[1], wand_stage[2], wand_stage[4]);
  for (int t = 0; t < MT_COUNT; t++) if (npc_talks[t]) printf(" %s %d", mob_name(t), npc_talks[t]); printf("\n");
  printf("bosses passed/stuck:"); for (int d = 5; d <= 25; d += 5) if (boss_dead[d] || boss_alive[d]) printf(" d%d %d/%d", d, boss_dead[d], boss_alive[d]); printf("\n");
  printf("shop taps %d; room types at game end:", shop_taps); for (int t = 0; t < 32; t++) if (rooms_seen[t]) printf(" %d:%d", t, rooms_seen[t]); printf("\n");
  int unused = 0; for (int k = IK_DAGGER; k < IK_COUNT; k++) if (!kinds_used[k]) { unused++; if (getenv("U")) printf(" unused kind %d\n", k); }
  printf("item kinds never used: %d, zaps %d (bad %d), rings worn at end %d\n", unused, s_zaps, s_bad_zap, 0);
}
