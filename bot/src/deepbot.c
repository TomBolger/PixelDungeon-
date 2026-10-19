#include <sys/time.h>
// deepbot: a host-side player that plays whole games through the public API the way a person would (round 4).
// It explores with the target list (game_targets) and taps, fights (bosses first when beside them), picks up and uses
// items sensibly (eats, drinks healing when low, reads identify / upgrade, equips better gear, throws and zaps at enemies,
// keys, wells, the alchemy pot, sungrass, honeypots, Lloyd's beacon), talks to the quest NPCs and finishes their quests,
// shops, descends, fights the bosses, takes the Amulet on 26 and climbs back out. No god mode: the hero only gets a fair
// head start (HS: more HP, STR and skill, a level a floor, a ring of mending and satiety, a potion of healing a floor).
// Every step it checks invariants and reports each violation as VIOL[kind] game=G step=S (rerun: START=G ./deepbot G+1,
// STOP=G:S for a verbose trace from step S). Checks: creatures and heaps on cells they may be on, no two creatures on a
// cell, counts within limits, items with possible kind / level / quantity / charges / durability, bag and bag-contents
// consistency, one stack per stackable, HP <= HT, sight bits matching the terrain, time never going back, every
// creature acted up to the hero's turn, no tap walking forever, no action that should pass time passing none, no 300
// decisions without time passing (a window that never resolves...), each new floor solvable (exit reachable from the
// entrance with secret doors searched, barricades burnt and locked doors opened with the keys in reach, every locked
// door's key on its floor), bosses that can be worn down, quests whose items still exist, save/load round trips that
// give back the same state, and a fork replay: a child process saves, loads and plays the same decisions as its parent
// for 300 decisions, and the two must end in the same state.
//
// usage: ./deepbot [N]   env: START=g first game, CLS=c class (else g % 4), HS=n head start (default 3; 6 wins games),
//   STEPS=n decisions per game (default 150000), RT=n save/load round trip every ~n decisions (400), FK=n fork replay
//   every ~n decisions (3000, 0 off), SD=n start on depth n, D=1 a line per game, V=1 everything, STOP=g:s / STOPD=d
//   verbose from a step / a depth, MAP=1 maps at structural violations and stuck floors, FR=1 a line per floor left,
//   Q=1 quests left unfinished, MOBS=1 creatures each decision (with V), WPN=1 weapon changes, MEM=1 memory use,
//   FT=g FTDIR=dir (FTBIN=1) fork traces (and Game dumps) of game g, RNGLOG=s random draws of step s (needs the
//   deepbot_dbg build: level_rt_rnglog.c.txt as level_rt.c, -DRNGLOG).
// Run in chunks of <= 40 games per process (bg.sh): the host's dlopen per overlay load leaks under ASan.
#include "pd.h"
#include "game_int.h"
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <fcntl.h>

PKey g_keys[256]; int g_persist_fail;
static void dump_map(int mark);
static bool transmutes(int k) { return k == IK_KNUCKLES || k == IK_DAGGER || k == IK_SPEAR || k == IK_QUARTERSTAFF || k == IK_SWORD || k == IK_MACE || k == IK_LONGSWORD || k == IK_BATTLE_AXE || k == IK_GLAIVE || k == IK_WAR_HAMMER; }
static int V, cur_game, cur_step, stop_game = -1, stop_step = -1;
static char s_last_log[8][120]; static int s_nlog;
static char s_did[200];
#define DID(...) do { if (!s_did[0]) snprintf(s_did, sizeof s_did, __VA_ARGS__); } while (0)
void ui_log(const char *m, MsgColor c) { snprintf(s_last_log[s_nlog++ & 7], 120, "%s", m); if (V) printf("  [d%d s%d] log: %s\n", G.depth, cur_step, m); }
void ui_fx(FxKind k, int16_t c, int w, const char *t) {}
void ui_throw(int16_t a, int16_t b, int s) {}
static int s_bad_zap;
void ui_zap(int k, int16_t a, int16_t b, const int16_t *pts, int n) { if (n < 0 || n > BOLT_MAX + 1) s_bad_zap++; for (int i = 0; i < n; i++) if (pts[i] < 0 || pts[i] >= MAP_LEN) s_bad_zap++; }
static int s_pick = -1; void ui_pick_item(PickMode m) { s_pick = m; }
static int s_won, s_amulet_scene; void ui_amulet(int m) { if (m == 2) s_won = 1; else s_amulet_scene++; }
void ui_hero_tier(void) {}
void ui_message(const char *t, const char *x) { if (V) printf("  msg: %.100s\n", x); }
static bool s_jump, s_want_jump; void ui_confirm_jump(void) { s_jump = true; }
void ui_sound(int s) {}
static int s_wnd_btn, s_wnd_mob = -1, s_wnd_type; static char s_wnd_b1[40], s_wnd_b2[40];
void ui_wnd(int m, const char *t, const char *a, const char *b) {
  if (a) { s_wnd_btn = 1; s_wnd_mob = m; s_wnd_type = m >= 0 ? G.mobs[m].type : -1; snprintf(s_wnd_b1, 40, "%s", a); snprintf(s_wnd_b2, 40, "%s", b ? b : ""); }
  if (V) printf("  wnd(%d): %.80s [%s|%s]\n", m, t, a ? a : "", b ? b : "");
}
const char *g_wnd_title;
void ui_wnd_info(const Item *it) { char b[300]; item_info(it, b, sizeof b); }
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_free_cache(int m) {}
extern int s_pend_depth, s_pend_arrive;
extern int8_t g_trade; extern int16_t g_trade_arg;

// ---------------- violations ----------------
#define NK 64
static const char *vk_name[NK]; static int vk_count[NK], vk_games[NK], vk_lastgame[NK]; static int n_vk;
static int total_viol;
static int s_is_child;
static int s_str_plus; static bool s_dring_on; static double s_arm_speedk = 1.0; static bool s_explab; static long st_upg_holds, st_stash, st_stash_lost; static int upg_plan(bool *hold); static int unknown_potion(void);                                   // bot3: planning: strength potions still to come
static bool lookahead(void);
// bot3: tunable judgment calls (PRM=v0,v1,... overrides; tools/tune.py searches them)
enum { P_REST_LO, P_REST_HI, P_REST_LO_B, P_REST_HI_B, P_ENGAGE, P_EMERG, P_SEEDHP, P_WAITN, P_FRONT_BIAS, P_TESTHP, P_CHOKE, P_BOSSLAST, P_GEARMODEL, P_HUNT, P_HUNGRYDOWN, P_QSTAY, P_COUNT };
static int PRM[P_COUNT] = { 30, 60, 25, 40, 0, 33, 60, 3, 1, 90, 3, 1, 1, 0, 1, 2500 };
static double gear_cost(const Item *w, const Item *a); static bool have_rc(void); static Item believed(const Item *it, int unk); static double gear_cost_b(const Item *w, const Item *a, int unk); static bool s_oldgear; static bool underpowered(int c); static double ev_cost(const Item *it, int cat, double base); static int s_zone_start; static double s_riskp; static bool s_nounk; static double unk_cost(const Item *it, int cat, double base); static bool floor_left(void); static bool puzzle_pending(void); static bool trap_puzzle_pending(void); static bool pool_puzzle_pending(void); static bool trap_door_testable(void); static bool room_skipped(int r); static int s_trap_throws_room[64], s_trap_door[64]; static int s_arrive_step = -1000, s_prev_arrive = -1000, s_camp_steps, s_camp_reroll; static uint8_t s_trap_done[64]; static int s_bounce_n[32]; static uint8_t s_room_skip[64]; static int room_door(int r); static bool want_buy(int h); static double idb_p(int label, int kind); static int32_t s_sale_gold[MAP_LEN];   /* 0.93: the gold when a ware was passed over: more gold since, look again */ static bool attack_wand_ready(void); static bool room_seen(int r);
static bool curse_food_ok(void); static bool food_pressed(void); static long curse_kit_sig(void); static bool curse_go(void); static bool curse_go_full(void); static int s_curse_force; static bool curse_sim_ok(void); static int s_curse_trip; static long curse_key(void); static long s_curse_no_key = -1;
static void prm_load(void) { const char *ps = getenv("PSET"); while (ps && *ps) { int i = atoi(ps); const char *q = strchr(ps, ':'); if (!q) break; if (i >= 0 && i < P_COUNT) PRM[i] = atoi(q + 1); ps = strchr(q, ','); if (ps) ps++; }
  const char *e = getenv("PRM"); if (!e) return; for (int i = 0; i < P_COUNT && *e; i++) { PRM[i] = atoi(e); while (*e && *e != ',') e++; if (*e) e++; } }
static double s_hp_cut; static bool s_toe; static int toe_upgrade(int scroll); static int toe_choose(bool *hold); static bool toe_keep(int slot); static bool toe_buy(int hh);
static int s_boom_plus; static bool hunt2(void); static bool dm_hold(void); static int boom_slot(void); static double boom_dpt_vs(int def, int dr, int extra, bool near);
static int s_rollout; static int32_t s_last_up = -1000000; static int s_avoid_n; static int s_upg_child, s_upgcf_n; static int s_ev_sou0 = -1, s_ev_str0, s_ev_d = -1; static int food_budget(void); static uint8_t s_stepped[MAP_LEN / 8 + 1]; static uint8_t s_stepcnt[MAP_LEN]; static int s_wk[5]; static int s_floor_moves, s_floor_lastpos = -1; static int32_t s_floor_t0; static int storage_barr(void); static bool pot_new(int sl); static int s_barr_tries;   // (0.58: declared early: STAIRFLEE)                                   // bot3: inside a look-ahead copy of the game
static char s_ring[64][96]; static int s_ring_n;      // 0.52g: the last 64 decisions, shown at a lockout
static void ring_dump(const char *kind) { if (!(strcmp(kind, "PINGPONG") == 0 || strcmp(kind, "FLOOR_STUCK") == 0 || strcmp(kind, "NO_PROGRESS") == 0)) return;
  printf("  LAST DECISIONS (%s):\n", kind); for (int i = 0; i < 64; i++) { int j = (s_ring_n + i) & 63; if (s_ring[j][0]) printf("    %s\n", s_ring[j]); } }
static void viol(const char *kind, const char *fmt, ...) {
  if (s_rollout) return;
  if (!s_is_child) ring_dump(kind);
  if (s_is_child && strcmp(kind, "WALK_FOREVER")) return;   // (the fork replay's child only plays; its parent reports)
  int k = 0;
  for (; k < n_vk; k++) if (!strcmp(vk_name[k], kind)) break;
  if (k == n_vk && n_vk < NK) { vk_name[n_vk++] = kind; vk_lastgame[k] = -1; }
  vk_count[k]++; total_viol++;
  if (vk_lastgame[k] != cur_game) { vk_lastgame[k] = cur_game; vk_games[k]++; }
  if (vk_count[k] > 6 && !V) return;
  char b[400]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  printf("VIOL[%s] game=%d cls=%d step=%d depth=%d turn=%d: %s\n", kind, cur_game, G.hero.hclass & 3, cur_step, G.depth, G.now / TICK, b);
  if (vk_count[k] <= 2) for (int i = 0; i < 4; i++) { const char *l = s_last_log[(s_nlog - 4 + i) & 7]; if (l[0]) printf("     log: %s\n", l); }
}

// ---------------- invariants ----------------
static bool cell_ok_for(int c, bool fly) {             // can a creature stand here
  int t = G.map[c];
  if (PASSABLE(t)) return true;
  if ((TFLAGS(t) & F_AVOID) && !(TFLAGS(t) & F_PIT)) return true;   // traps, wells
  return fly && (TFLAGS(t) & F_PIT);
}
static const char *item_bad(const Item *it, bool equipped) {
  if (!it->kind) return NULL;
  if (it->kind >= IK_COUNT) return "kind out of range";
  if (it->qty == 0) return "qty 0";
  if (it->kind == IK_GOLD) return NULL;
  if (!item_stackable(it->kind) && it->qty != 1) return "unstackable qty != 1";
  if (it->qty > 500) return "qty > 500";
  int c = item_cat(it->kind);
  if (it->flags & 0x80) return "unknown flag";
  if (item_upgradable(it->kind)) {
    if (it->lvl < -10 || it->lvl > 40) return "level out of range";
    if (it->dur > item_max_dur(it)) return "durability above max";
    if (c == CAT_WAND && it->ench > wand_max(it)) return "charges above max";
    if (c == CAT_WEAPON && it->ench >= WE_COUNT) return "bad enchantment";
    if (c == CAT_ARMOR && it->ench >= AG_COUNT) return "bad glyph";
  } else if (c == CAT_KEY) { if (it->lvl < 1 || it->lvl > 26) return "key depth"; }
  else if (it->kind == IK_BEACON) { if (it->lvl < 0 || it->lvl > 26) return "beacon depth"; }
  else if (c == CAT_POTION || c == CAT_SCROLL || c == CAT_SEED || c == CAT_FOOD) { if (it->lvl) return "consumable with a level"; }
  return NULL;
}
static int st_overlap, s_overlap_uid = -1; static bool s_arrival;
static void check_state(const char *where) {
  if (s_rollout) return;
  Hero *h = &G.hero;
  if (G.n_mobs > MAX_MOBS || G.n_heaps > MAX_HEAPS || G.n_diffs > MAX_DIFFS || G.n_plants > MAX_PLANTS)
    viol("COUNT", "%s: mobs %d heaps %d diffs %d plants %d", where, G.n_mobs, G.n_heaps, G.n_diffs, G.n_plants);
  if (h->HP > h->HT) viol("HERO_HP>HT", "%s: %d/%d", where, h->HP, h->HT);
  if (h->HP <= 0 || s_pend_depth) return;
  if (h->pos < 0 || h->pos >= MAP_LEN) { viol("HERO_POS", "%s: pos %d", where, h->pos); return; }
  if (!cell_ok_for(h->pos, hero_flying())) viol("HERO_CELL", "%s: hero on terrain %d at %d", where, G.map[h->pos], h->pos);
  if (h->STR < 5 || h->STR > 40 || h->lvl < 1 || h->lvl > 30 || h->exp > hero_max_exp() || h->hunger < 0 || h->hunger > 360)
    viol("HERO_STATS", "%s: STR %d lvl %d exp %d/%d hunger %d", where, h->STR, h->lvl, h->exp, hero_max_exp(), h->hunger);
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0) continue;
    if (m->type >= MT_COUNT) { viol("MOB_TYPE", "%s: mob %d type %d", where, i, m->type); continue; }
    if (m->pos < 0 || m->pos >= MAP_LEN) { viol("MOB_POS", "%s: %s at %d", where, mob_name(m->type), m->pos); continue; }
    uint32_t f = MOB_DEFS[m->type].flags;
    if (!cell_ok_for(m->pos, true)) viol("MOB_CELL", "%s: %s (state %d) on terrain %d at %d", where, mob_name(m->type), m->state, G.map[m->pos], m->pos);
    if ((f & MD_WATER) && !IS_WATER(G.map[m->pos])) viol("MOB_WATER", "%s: %s out of water at %d (t %d)", where, mob_name(m->type), m->pos, G.map[m->pos]);
    if (m->pos == h->pos) {                           // PD 1.9.1 too: Dungeon.switchLevel and LloydsBeacon put the hero down whoever stands there
      if (s_arrival) s_overlap_uid = m->uid;
      if (m->uid == s_overlap_uid) st_overlap++; else viol("STACKED", "%s: %s on the hero's cell %d", where, mob_name(m->type), m->pos); }
    for (int j = i + 1; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && G.mobs[j].pos == m->pos)
      viol("STACKED", "%s: %s and %s both on %d", where, mob_name(m->type), mob_name(G.mobs[j].type), m->pos);
    if (m->HP > m->HT && m->HT > 0 && !(m->type == MT_ROTTING_FIST && m->HP < m->HT + 4)) viol("MOB_HP>HT", "%s: %s %d/%d", where, mob_name(m->type), m->HP, m->HT);
  }
  for (int i = 0; i < G.n_heaps; i++) {
    Heap *hp = &G.heaps[i];
    const char *b = item_bad(&hp->item, false);
    if (!hp->item.kind) b = "empty heap";
    if (b) viol("HEAP_ITEM", "%s: heap %d at %d type %d kind %d qty %d lvl %d dur %d ench %d: %s", where, i, hp->pos, hp->type, hp->item.kind, hp->item.qty, hp->item.lvl, hp->item.dur, hp->item.ench, b);
    if (hp->type > HEAP_FOR_SALE) viol("HEAP_TYPE", "%s: heap type %d", where, hp->type);
    if (hp->pos >= MAP_LEN) {
      if ((hp->pos & 0xFF00) == 0x4000) { int uid = hp->pos & 0xFF; bool own = false; for (int k = 0; k < G.n_mobs; k++) if (G.mobs[k].HP > 0 && G.mobs[k].uid == uid) own = true;
        if (!own) viol("HEAP_ORPHAN", "%s: carried heap (kind %d) of uid %d, nobody carries it", where, hp->item.kind, uid); }
      else if ((hp->pos & 0xFF00) != 0x4100 || (hp->pos & 0xFF) > 2) viol("HEAP_POS", "%s: heap pos %x", where, hp->pos);
      continue;
    }
    if (hp->pos < 0) { viol("HEAP_POS", "%s: heap pos %d", where, hp->pos); continue; }
    int t = G.map[hp->pos];
    if (TFLAGS(t) & F_PIT) viol("HEAP_PIT", "%s: kind %d lying over a chasm at %d", where, hp->item.kind, hp->pos);
    else if (t == T_ALCHEMY && item_cat(hp->item.kind) != CAT_SEED) viol("HEAP_ON_POT", "%s: kind %d (heap type %d) lies on the alchemy pot at %d", where, hp->item.kind, hp->type, hp->pos);
    else if (!PASSABLE(t) && !(TFLAGS(t) & F_AVOID) && t != T_ALCHEMY) { viol("HEAP_WALL", "%s: kind %d (heap type %d) in terrain %d at %d", where, hp->item.kind, hp->type, t, hp->pos); static int dumped; if (getenv("MAP") && !dumped++) dump_map(hp->pos); }
  }
  for (int i = 0; i < G.n_plants; i++) {
    if (G.plants[i].pos < 0 || G.plants[i].pos >= MAP_LEN || G.plants[i].type >= 8) { viol("PLANT", "%s: plant %d pos %d type %d", where, i, G.plants[i].pos, G.plants[i].type); continue; }
    if (!cell_ok_for(G.plants[i].pos, false)) viol("PLANT_CELL", "%s: plant on terrain %d", where, G.map[G.plants[i].pos]);
    for (int j = i + 1; j < G.n_plants; j++) if (G.plants[j].pos == G.plants[i].pos) viol("PLANT_STACK", "%s: two plants at %d", where, G.plants[i].pos);
  }
  // the hero's things
  for (int s = -4; s < BAG_CAP; s++) { const char *b = item_bad(game_slot(s), s < 0); if (b) viol("BAG_ITEM", "%s: slot %d kind %d qty %d lvl %d dur %d ench %d: %s", where, s, game_slot(s)->kind, game_slot(s)->qty, game_slot(s)->lvl, game_slot(s)->dur, game_slot(s)->ench, b); }
  for (int s = 0; s < 32; s++) { const char *b = item_bad(&G.pack[s], false); if (b) viol("PACK_ITEM", "%s: pack %d kind %d: %s", where, s, G.pack[s].kind, b); }
  if (G.hero.bag[BAG_SIZE].kind) viol("BAG_SPARE", "%s: slot 19 holds kind %d", where, G.hero.bag[BAG_SIZE].kind);
  if (h->weapon.kind && item_cat(h->weapon.kind) != CAT_WEAPON && item_cat(h->weapon.kind) != CAT_MISSILE && item_cat(h->weapon.kind) != CAT_WAND) viol("EQUIP", "%s: weapon slot kind %d", where, h->weapon.kind);
  if (h->armor.kind && item_cat(h->armor.kind) != CAT_ARMOR) viol("EQUIP", "%s: armor slot kind %d", where, h->armor.kind);
  for (int r = 0; r < 2; r++) if (h->ring[r].kind && item_cat(h->ring[r].kind) != CAT_RING) viol("EQUIP", "%s: ring slot kind %d", where, h->ring[r].kind);
  for (int s = 0; s < 32; s++) {
    int k = G.pack[s].kind; if (!k) continue;
    int r = s < 8 ? 0 : s < 20 ? 1 : 2, want = r == 0 ? CAT_SEED : r == 1 ? CAT_SCROLL : CAT_WAND;
    if (item_cat(k) != want) viol("PACK_KIND", "%s: pack slot %d (bag %d) holds kind %d", where, s, r, k);
    bool owned = false; for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == IK_SEED_POUCH + r) owned = true;
    if (!owned) viol("PACK_NO_BAG", "%s: pack slot %d holds kind %d but bag %d is not carried", where, s, k, r);
  }
  for (int a = 0; a < PACK0 + 32; a = a == BAG_SIZE - 1 ? PACK0 : a + 1) {   // stackables are one stack
    Item *x = game_slot(a); if (!x->kind || !item_stackable(x->kind)) continue;
    for (int b = a + 1; b < PACK0 + 32; b = b == BAG_SIZE - 1 ? PACK0 : b + 1) { if (b >= BAG_SIZE && b < PACK0) continue; Item *y = game_slot(b);
      if (y->kind == x->kind && (x->kind != IK_DART || 1)) viol("TWO_STACKS", "%s: kind %d in slots %d and %d", where, x->kind, a, b); }
  }
  for (int i = 0; i < G.n_diffs; i++) for (int j = i + 1; j < G.n_diffs; j++) if (G.diffs[i].pos == G.diffs[j].pos) viol("DIFF_DUP", "%s: two diffs at %d", where, G.diffs[i].pos);
  for (int c = 0; c < MAP_LEN; c++) if (G.map[c] > 63 || G.map[c] == 47) { viol("TERRAIN", "%s: cell %d terrain %d", where, c, G.map[c]); break; }
  for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.los, c) != ((TFLAGS(G.map[c]) & F_LOS_BLOCKING) != 0)) { viol("LOS_STALE", "%s: cell %d terrain %d, sight-blocking bit %d", where, c, G.map[c], BIT_GET(G.los, c)); break; }
}
// after a whole action: everyone else has acted up to the hero's turn (Actor.process ran to completion)
static int32_t s_prev_now;
static void check_time(const char *where) {
  if (s_rollout) return;
  if (G.now < s_prev_now) viol("TIME_BACK", "%s: now went from %d back to %d", where, s_prev_now, G.now);
  s_prev_now = G.now;
  if (G.hero.HP <= 0 || s_pend_depth) return;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].time <= G.hero.time - TICK * 20)
    { viol("MOB_TIME", "%s: %s time %d far behind hero %d (process guard ran out?)", where, mob_name(G.mobs[i].type), G.mobs[i].time, G.hero.time); break; }
  if (G.turn_due < G.hero.time - TICK * 2 && !s_arrival && G.hero.HP > 0) viol("TURN_DUE", "%s: turn_due %d behind hero %d", where, G.turn_due, G.hero.time);
}

// ---------------- driving the game ----------------
static int st_thief_hunt, st_thief_back;
static int s_fallback_search;   // a fully seen floor and still no way on (teleported into a closed room): search every wall
static int s_walk_max, s_bot_steps, s_idle, s_am_depth;   // s_am_depth: the floor a thief took the Amulet on (-1: lost for good)   // s_idle: rests in a row with nothing to do on this floor
static int s_dbg_kind;
static bool s_walk_osc;                                  // bot3: the last walk went back and forth (the game's path flip-flops): given up
static ActResult walk(ActResult r) {
  int n = 0, p1 = -1, p2 = -1, osc = 0; s_walk_osc = false;
  int32_t t0 = G.hero.time;
  while (r == ACT_WALK && G.hero.HP > 0) {
    if (G.hero.pos == p2 && ++osc >= 3 && !getenv("NOOSC")) { G.action = HA_NONE; s_walk_osc = true; r = ACT_TURN; break; }   // A-B-A-B: stop
    p2 = p1; p1 = G.hero.pos;
    check_state("walk");
    if (++n > 2500) { viol("WALK_FOREVER", "action %d cell %d: ACT_WALK %d times; hero at %d, time %d -> %d", G.action, G.action_cell, n, G.hero.pos, t0, G.hero.time); G.action = HA_NONE; break; }
    if (getenv("WALKSTOP") && n > 0 && !s_rollout) { bool seen_new = false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC) && distance(m->pos, G.hero.pos) >= 2) seen_new = true; }
      if (seen_new) { G.action = HA_NONE; r = ACT_TURN; break; } }   // 0.58: one hunting me comes into view: stop and think (avoid it, door, throw) instead of walking into it
    { int p = G.hero.pos; r = game_continue(); if (G.hero.pos != p) { s_floor_moves++; BIT_SET(s_stepped, G.hero.pos); if (s_stepcnt[G.hero.pos] < 255) s_stepcnt[G.hero.pos]++; } if (G.hero.pos != p && !getenv("NOHEAD")) g_from = p; if (V > 1) printf("     step %d,%d -> %d,%d r %d action %d\n", cx(p), cy(p), cx(G.hero.pos), cy(G.hero.pos), r, G.action); }   // (main.c tg_refresh: the heading)
  }
  if (n > s_walk_max) s_walk_max = n;
  if (s_pend_depth > LAST_DEPTH) viol("TOO_DEEP", "pending depth %d", s_pend_depth);
  check_state("act");
  check_time("act");
  return r;
}
static ActResult tap_safe(int c);
static ActResult do_tap(int c) { int32_t t = G.hero.time; int hb = heap_at(c), tb = hb >= 0 ? G.heaps[hb].type : -1; ActResult r = tap_safe(c); if (V) printf("   tap %d: heap type %d -> %d, action %d\n", c, tb, heap_at(c) >= 0 ? G.heaps[heap_at(c)].type : -1, G.action); if ((r == ACT_TURN || r == ACT_WALK) && G.hero.time <= t && G.hero.HP > 0 && !s_pend_depth) viol("NO_TIME", "tap %d (terrain %d) returned %d without time passing", c, G.map[c], r); return walk(r); }
static int s_line;
static long st_eat_energy, st_eat_waste; static int st_warreat;   // 0.61: food eaten (energy) and wasted (below 0 hunger)
#define do_item(s, a) (s_line = __LINE__, do_item_(s, a))
static int s_iddrop_k = -1;
static ActResult do_item_(int slot, ItemAction a) { if (a == ACN_DROP) s_iddrop_k = game_slot(slot)->kind; if (a == ACN_EAT && !s_rollout && !s_is_child) { int k = game_slot(slot)->kind, e = k == IK_RATION ? 260 : k == IK_PASTY ? 360 : k == IK_PHANTOM_FISH ? 0 : 100; st_eat_energy += e; if (e > G.hero.hunger) st_eat_waste += e - G.hero.hunger; } { char nm[64]; item_name(game_slot(slot), nm, sizeof nm); DID("%s: %s", ACTION_NAMES[a], nm); } int32_t t = G.hero.time; if (V) printf("  item slot %d kind %d %s (line %d)\n", slot, game_slot(slot)->kind, ACTION_NAMES[a], s_line); ActResult r = game_item_do(slot, a); if (V) printf("   -> %d\n", r); if (r == ACT_TURN && G.hero.time <= t && G.hero.HP > 0 && !s_pend_depth && a != ACN_DROP && a != ACN_RETURN) viol("NO_TIME", "item %d action %s took no time", game_slot(slot)->kind, ACTION_NAMES[a]); return walk(r); }

// ---------------- knowledge ----------------
static int s_ign_uid[MAX_MOBS], s_ign_until[MAX_MOBS];
static uint16_t s_black[MAP_LEN]; static uint8_t s_sale[MAP_LEN / 8 + 1]; static bool s_sale_any;                       // cell: skipped as a target until this decision
static uint8_t s_searched[MAP_LEN / 8], s_dropped[MAP_LEN / 8], s_used_feature[MAP_LEN / 8];
static bool sell_extra(const Item *it);
static int s_floor_steps, s_floor_turn0, s_last_depth, s_goal = 99, s_floor_searches, s_shop_sold, s_cooked, s_fallback, s_tgt_explore;
static uint8_t s_pwater[MAP_LEN / 8];                    // bot3: the piranhas' water (what joins the pool room's), not every puddle
#define PWATER(c) BIT_GET(s_pwater, (c))
static uint8_t s_preach[MAP_LEN / 8];                   // bot3: within a piranha's bite (its water and the cells beside it)
#define PREACH(c) BIT_GET(s_preach, (c))
static uint8_t s_burnt[MAP_LEN / 8], s_verified[MAP_LEN / 8]; static bool s_burnt_any, s_noburnt;   // bot3 0.47n: a burned room's plain-looking floor (1 in 2 a hidden fire trap)
#define BURNT(c) BIT_GET(s_burnt, (c))
static bool fire_next(int c) {                          // 0.47n: flammable and a flame beside it (Fire spreads to the four sides each turn)
  if (!g_blob[BLOB_FIRE] || !(TFLAGS(G.map[c]) & F_FLAMABLE)) return false;
  static const int8_t O4[4] = { -1, 1, -MAP_W, MAP_W }; for (int k = 0; k < 4; k++) { int n = c + O4[k]; if (nb_ok(c, n) && g_blob[BLOB_FIRE][n]) return true; } return false;
}
static bool s_oldfire, s_oldchoke, s_oldeat;
static bool s_firedoom; static uint8_t s_doom[MAP_LEN / 8]; static uint32_t s_doom_key;   // 0.92 (s1 g18 burned): grass joined to a fire burns in time, every cell of it
static bool fire_doomed(int c) {                        // flammable and joined (four ways, through flammable cells) to a flame: the fire gets here
  if (!s_firedoom || !g_blob[BLOB_FIRE] || !(TFLAGS(G.map[c]) & F_FLAMABLE)) return false;
  uint32_t key = (uint32_t)G.hero.time * 7919u + (uint32_t)G.depth * 131u + 1u; if (key != s_doom_key) { s_doom_key = key; memset(s_doom, 0, sizeof s_doom);
    static int16_t q[MAP_LEN]; int qh = 0, qt = 0; static const int8_t O4[4] = { -1, 1, -MAP_W, MAP_W };
    for (int x = 0; x < MAP_LEN; x++) if (g_blob[BLOB_FIRE][x]) for (int k = 0; k < 4; k++) { int n = x + O4[k]; if (nb_ok(x, n) && (TFLAGS(G.map[n]) & F_FLAMABLE) && !BIT_GET(s_doom, n)) { BIT_SET(s_doom, n); q[qt++] = n; } }
    while (qh < qt) { int x = q[qh++]; for (int k = 0; k < 4; k++) { int n = x + O4[k]; if (nb_ok(x, n) && (TFLAGS(G.map[n]) & F_FLAMABLE) && !BIT_GET(s_doom, n)) { BIT_SET(s_doom, n); q[qt++] = n; } } } }
  return BIT_GET(s_doom, c);
}
static bool looks_floor(int t) { return t == T_EMPTY || ((TFLAGS(t) & F_SECRET) && PASSABLE(t)); }   // what a hidden trap looks like
static void burnt_scan(void) {                          // StandardPainter.paintBurned: embers, fire traps (half hidden), spent traps, bare floor
  memset(s_burnt, 0, sizeof s_burnt); s_burnt_any = false;
  if (s_noburnt) return;
  BIT_SET(s_verified, G.hero.pos);                      // where you stand holds no trap
  for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; int emb = 0, trap = 0;
    for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int c = x + y * MAP_W; if (!BIT_GET(G.visited, c)) continue;
      int t = G.map[c]; if (t == T_EMBERS) emb++; else if (t == T_FIRE_TRAP || t == T_INACTIVE_TRAP) trap++; }
    if (!trap || emb < 2 || emb + trap < 3 || boss_depth(G.depth)) continue;   // (not Tengu's arena: its spent traps are no burned room)
    for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int c = x + y * MAP_W;
      if (BIT_GET(G.visited, c) && looks_floor(G.map[c]) && !BIT_GET(s_verified, c)) { BIT_SET(s_burnt, c); s_burnt_any = true; } }
  }
}
static ActResult bot_search(void) {                     // an intentional search shows every hidden trap beside you
  for (int k = 0; k < 8; k++) { int c = G.hero.pos + N8[k]; if (nb_ok(G.hero.pos, c)) BIT_SET(s_verified, c); }
  return game_search();
}
static bool pool_floor(void);
static void pwater_map(void) {
  memset(s_pwater, 0, sizeof s_pwater); int16_t q[MAP_LEN]; int qh = 0, qt = 0;
  for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_POOL) { Room *R = &G.rooms[r];
    for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int c = x + y * MAP_W; if (IS_WATER(G.map[c]) && !BIT_GET(s_pwater, c)) { BIT_SET(s_pwater, c); q[qt++] = c; } } }
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_PIRANHA && !BIT_GET(s_pwater, G.mobs[i].pos)) { BIT_SET(s_pwater, G.mobs[i].pos); q[qt++] = G.mobs[i].pos; }
  while (qh < qt) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && IS_WATER(G.map[n]) && !BIT_GET(s_pwater, n)) { BIT_SET(s_pwater, n); q[qt++] = n; } } }
  memcpy(s_preach, s_pwater, sizeof s_preach);
  for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(s_pwater, c)) for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n)) BIT_SET(s_preach, n); }
}
static int16_t b_dist[MAP_LEN], b_q[MAP_LEN];
static bool s_dry, s_dry_reach; static int s_dry_goal = -1, s_pass_bite = -1;                                      // bot2: bfs keeps out of the water (a piranha floor)
static int bfs(int from, bool through_unknown) {        // walking distances over what the hero knows
  for (int c = 0; c < MAP_LEN; c++) b_dist[c] = 0x7FFF;
  int qh = 0, qt = 0; b_dist[from] = 0; b_q[qt++] = from;
  while (qh < qt) {
    int c = b_q[qh++];
    for (int k = 0; k < 8; k++) {
      int n = c + N8[k];
      if (!nb_ok(c, n) || b_dist[n] != 0x7FFF) continue;
      if (!BIT_GET(G.visited, n) && !through_unknown) continue;
      int t = G.map[n];
      if (!PASSABLE(t) || (TFLAGS(t) & F_AVOID) || (s_dry && (s_dry_reach ? PREACH(n) && n != s_dry_goal : PWATER(n)))) continue;
      if ((g_blob[BLOB_FIRE] && g_blob[BLOB_FIRE][n]) || (g_blob[BLOB_TOXIC] && g_blob[BLOB_TOXIC][n])) continue;   // never path through flames or toxic gas
      if (!s_oldfire && fire_next(n)) continue;           // (0.47n: nor grass about to catch)
      if (s_firedoom) { int pi = plant_at(n); if (pi >= 0 && G.plants[pi].type <= 3) continue; }   // 0.92 (s1 g112): not over a Firebloom, Icecap, Sorrowmoss or Dreamweed
      b_dist[n] = b_dist[c] + 1; if (!BURNT(n)) b_q[qt++] = n;   /* (0.52f: a ware: a place to go, never a way through: stepping on it opens the trade window) */   // (0.47n: a burned room's bare floor: a place to go, never a way through)
    }
  }
  return qt;
}
static ActResult tap_safe(int c) {                       // 0.47n: every tap: across a burned room only over embers and floor you know
  int hp = G.hero.pos;
  if (!getenv("OLDTOMB") && (unsigned)c < MAP_LEN && distance(hp, c) > 1) { int hi = heap_at(c); if (hi >= 0 && G.heaps[hi].type == HEAP_TOMB) {   // 0.52 (game 7): a walk that ends on a grave opens it (3 wraiths on floor 1): stop beside it
      int best = -1, bd = 999; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && PASSABLE(G.map[n]) && !(TFLAGS(G.map[n]) & F_AVOID) && !char_at(n) && heap_at(n) < 0) { int dd = distance(n, hp); if (dd < bd) { bd = dd; best = n; } } }
      if (best >= 0) c = best; } }
  if (!s_burnt_any || c == hp || (unsigned)c >= MAP_LEN) {
    int32_t t0 = G.hero.time; ActResult r = game_tap_cell(c);
    if (r != ACT_NONE || G.hero.time != t0 || getenv("OLDTAPSTEP") || c == hp || (unsigned)c >= MAP_LEN || distance(hp, c) <= 1) return r;
    // 0.52g (game 806 starved on floor 2): the game finds no way (something unseen in a doorway, say): one step along my own map's way, to see
    static int16_t keep2[MAP_LEN]; memcpy(keep2, b_dist, sizeof keep2); bfs(c, false); int st = -1, sd = b_dist[hp];
    if (sd != 0x7FFF) for (int k = 0; k < 8; k++) { int n = hp + N8[k]; if (nb_ok(hp, n) && b_dist[n] < sd && !char_at(n) && (n == c || !BIT_GET(s_sale, n))) { sd = b_dist[n]; st = n; } }
    memcpy(b_dist, keep2, sizeof keep2);
    return st >= 0 ? game_tap_cell(st) : r; }
  if (distance(hp, c) == 1) return mob_at(c) < 0 && BURNT(c) ? bot_search() : game_tap_cell(c);
  static int16_t keep[MAP_LEN]; memcpy(keep, b_dist, sizeof keep);
  bfs(c, false); int st = -1, sd = b_dist[hp];
  if (sd != 0x7FFF) for (int k = 0; k < 8; k++) { int n = hp + N8[k]; if (nb_ok(hp, n) && b_dist[n] < sd && mob_at(n) < 0 && (n == c || !BIT_GET(s_sale, n))) { sd = b_dist[n]; st = n; } }
  memcpy(b_dist, keep, sizeof keep);
  if (st < 0) return game_tap_cell(c);
  return BURNT(st) ? bot_search() : game_tap_cell(st);
}
static bool s_piranhas, s_brave; static bool s_front_ortho, s_front_wall; static int s_exw_far; static int s_exw_diag, s_exw_unk, s_exw_item, s_exw_inear = 99; static bool s_exp_tsp; static int s_srch_dead; static int s_detour, s_grassw, st_detour; static bool s_kite2, s_pztest; static int s_scrhold; static int s_pocket, s_plandbg = -1; static int s_headb; static int s_expgain, s_expcommit, s_exp_tgt = -1;
static bool known_wall(int c) { return (unsigned)c < MAP_LEN && BIT_GET(G.visited, c) && (G.map[c] == T_WALL || G.map[c] == T_WALL_DECO); }
static bool likely_wall(int u) {                         // bot3: an unseen cell in line with known walls (left and right, or above and below): part of a wall
  return (known_wall(u - 1) && known_wall(u + 1) && cx(u) > 0 && cx(u) < MAP_W - 1) || (known_wall(u - MAP_W) && known_wall(u + MAP_W));
}
static bool frontier(int c) {                           // a known open cell beside the unknown
  if (s_piranhas && PREACH(c)) return false;
  if (!BIT_GET(G.visited, c) || !PASSABLE(G.map[c]) || G.map[c] == T_ALCHEMY || c == G.entrance || c == G.exit) return false;
  for (int k = 0; k < (s_front_ortho ? 4 : 8); k++) { int n = c + N8[k]; if (nb_ok(c, n) && !BIT_GET(G.visited, n) && !(s_front_wall && likely_wall(n))) return true; }   // (bot3 experiment: only straight-on unknowns)
  return false;
}
static int threat_count(void) { int n = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && G.mobs[i].state == ST_HUNTING) n++; return n; }   // bot2: those coming for you
static int enemy_count(void) { int n = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && G.mobs[i].state != ST_PASSIVE) n++; return n; }
static bool ignored(int i) { for (int k = 0; k < MAX_MOBS; k++) if (s_ign_uid[k] == G.mobs[i].uid && s_ign_until[k] > s_bot_steps) return true; return false; }
static void ignore_mob(int i, int n) { int k = 0; for (int j = 0; j < MAX_MOBS; j++) if (s_ign_until[j] < s_ign_until[k]) k = j; s_ign_uid[k] = G.mobs[i].uid; s_ign_until[k] = s_bot_steps + n; }
static int bag_used(void) { int n = 0; for (int i = 0; i < BAG_SIZE; i++) n += G.hero.bag[i].kind != 0; return n; }
static int find_kind(int k) { return game_bag_find(k); }
static bool cursed_known(const Item *it) { return (it->flags & IF_CURSED) && (it->flags & IF_CURSED_KNOWN); }
static int known_lvl(const Item *it) { return (it->flags & IF_LEVEL_KNOWN) ? it->lvl : 0; }
static int armor_score(const Item *it) {
  if (!it->kind) return -100;
  if (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0)) return -50;
  int s = (CLASS_ARMOR(it->kind) ? 5 : item_tier(it->kind)) * 3 + known_lvl(it) * 2 + (it->ench ? 1 : 0);
  int enc = item_str(it) - hero_str(); if (enc > 0) s -= enc * 4;
  return s;
}
static int weapon_score(const Item *it) {
  if (!it->kind) return -1000;
  if (item_cat(it->kind) != CAT_WEAPON || it->kind == IK_PICKAXE) return -100;
  if (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0)) return -50;
  Item t = *it; if (!(t.flags & IF_LEVEL_KNOWN)) t.lvl = 0;
  int s = (weapon_min(&t) + weapon_max(&t)) * 100 / weapon_dly(t.kind) * weapon_acu(t.kind) / 100;
  int enc = item_str(it) - hero_str(); if (enc > 0) s -= enc * 6;
  return s;
}
static bool attack_wand(int k) { return k == IK_WND_SLOW || k == IK_WND_FIREBOLT || k == IK_WND_POISON || k == IK_WND_LIGHTNING || k == IK_WND_AMOK ||
                                        k == IK_WND_DISINTEGRATION || k == IK_WND_AVALANCHE || k == IK_WND_MM; }
static bool kn(int k) { return (item_cat(k) == CAT_SEED && !getenv("OLDSEED")) || item_known(k); }   // 0.52 (audit): seeds are always named in the port (no set_known for them)
static bool harmful_potion(int k) { return k == IK_POT_TOXIC || k == IK_POT_FLAME || k == IK_POT_PARALYTIC || k == IK_POT_FROST; }
static bool is_food(int k) { return k == IK_RATION || k == IK_PASTY || k == IK_CHARGRILLED || k == IK_CARPACCIO || k == IK_OVERPRICED; }
// 0.92 (owner: "one weighted system for the bag"): what an item is worth to keep, in rough points (a Healing potion ~100,
// an Upgrade ~300). Used for: what to drop when full, whether a full bag takes something, what the alchemy pot is worth.
static int s_pot_guess[IK_COUNT];                       // alchemy: a potion brewed from three seeds of one kind is that seed's potion 2 times in 3
static int seed_potion(int k) { switch (k) { case IK_SEED_FIREBLOOM: return IK_POT_FLAME; case IK_SEED_ICECAP: return IK_POT_FROST; case IK_SEED_SORROWMOSS: return IK_POT_TOXIC; case IK_SEED_DREAMWEED: return IK_POT_INVIS; case IK_SEED_SUNGRASS: return IK_POT_HEALING; case IK_SEED_EARTHROOT: return IK_POT_PARALYTIC; case IK_SEED_FADELEAF: return IK_POT_MIND; case IK_SEED_ROTBERRY: return IK_POT_STRENGTH; } return 0; }
static int s_brew_kind = -1, s_brew_pot = -1;           // the last brew's seeds, if all of one kind, and the pot
static bool smith_reachable(void) { Quests *q = &G.q; return q->smith && (q->smith == QS_DONE || q->smith == QS_GIVEN || q->smith == QS_SPAWNED) && G.depth <= q->smith_depth + 2; }
static int item_value(const Item *it) {
  int k = it->kind, c = item_cat(k), q = it->qty > 0 ? it->qty : 1; Hero *h = &G.hero; if (!k) return -1000;
  if (k == IK_AMULET || c == CAT_QUEST || c == CAT_KEY || k == IK_ANKH || (k >= IK_SEED_POUCH && k <= IK_WAND_HOLSTER) || k == IK_SEED_ROTBERRY || k == IK_DEW_VIAL) return 10000;
  if (k == IK_PICKAXE) return G.q.smith == QS_GIVEN ? 10000 : -20;
  if (k == IK_DARK_GOLD) return G.q.smith == QS_GIVEN ? 10000 : 0;
  if (c == CAT_FOOD || is_food(k)) return (k == IK_PASTY ? 110 : k == IK_MYSTERY_MEAT ? 30 : 90) * q;
  if (c == CAT_POTION) { int v;
    if (!item_known(k)) v = s_pot_guess[k] == IK_POT_HEALING ? 90 : 45;
    else switch (k) { case IK_POT_HEALING: v = 100; break; case IK_POT_STRENGTH: case IK_POT_MIGHT: v = 400; break; case IK_POT_EXPERIENCE: v = 250; break;
      case IK_POT_INVIS: v = 35; break; case IK_POT_LEVITATION: v = 30; break; case IK_POT_PARALYTIC: v = 35; break; case IK_POT_FLAME: v = 35; break;
      case IK_POT_FROST: v = 20; break; case IK_POT_TOXIC: v = 25; break; case IK_POT_PURITY: v = 15; break; case IK_POT_MIND: v = 8; break; default: v = 20; }
    return v * q; }
  if (c == CAT_SCROLL) { int v;
    if (!item_known(k)) v = 50;
    else switch (k) { case IK_SCR_UPGRADE: v = 300; break; case IK_SCR_ENCHANT: v = 150; break; case IK_SCR_TELEPORT: v = 30; break; case IK_SCR_MAPPING: v = 25; break;
      case IK_SCR_TERROR: v = 30; break; case IK_SCR_MIRROR: v = 25; break; case IK_SCR_REMOVE_CURSE: v = 20; break; case IK_SCR_RECHARGING: v = 20; break;
      case IK_SCR_PSIONIC: v = 20; break; case IK_SCR_LULLABY: v = 15; break; case IK_SCR_IDENTIFY: v = 15; break; case IK_SCR_CHALLENGE: v = 0; break; default: v = 15; }
    return v * q; }
  if (c == CAT_SEED) { int v; switch (k) { case IK_SEED_SUNGRASS: v = s_pztest && !getenv("OLDSUNVAL") ? 85 : 22; break;   /* 0.93 (audit): planted, one heals a tenth of max HP every 5 turns to full: worth about a Healing; brewed, three make 2/3 of one */ case IK_SEED_EARTHROOT: v = 15; break; case IK_SEED_DREAMWEED: v = 12; break; case IK_SEED_FIREBLOOM: v = 12; break; default: v = 7; } return v * q; }
  if (c == CAT_WAND) return item_known(k) ? (attack_wand(k) ? 70 : k == IK_WND_TELEPORT || k == IK_WND_BLINK ? 50 : k == IK_WND_SLOW ? 45 : 20) + 5 * it->ench : 45;
  if (c == CAT_RING) return cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl <= 0) ? -10 : 60 + 20 * known_lvl(it);
  if (c == CAT_MISSILE) return k == IK_BOOMERANG ? 200 : (k == IK_CURARE_DART ? 8 : k == IK_INCENDIARY_DART ? 6 : 3) * q;
  if (k == IK_BOMB) return 40 * q;
  if (k == IK_WEIGHTSTONE || k == IK_ARMOR_KIT || k == IK_MASTERY) return 150;
  if (c == CAT_WEAPON || c == CAT_ARMOR) {
    const Item *w = c == CAT_WEAPON ? &h->weapon : &h->armor;
    if (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0)) return -10;
    if (w->kind == k && smith_reachable() && it != w) return 200;   // (owner: by the blacksmith a twin of what you wear is a scroll of upgrade, or better)
    int sc = c == CAT_WEAPON ? weapon_score(it) - weapon_score(w) : armor_score(it) - armor_score(w);
    if (k == IK_SHORT_SWORD && it->lvl > 0 && c == CAT_WEAPON && h->weapon.kind != IK_SHORT_SWORD) return 250;   // (its reforge: +1 for the weapon in hand)
    return sc > 0 ? 100 + sc : 5; }
  return 20 * q;
}

// ---------------- stats ----------------
static int st_games, st_wins, st_deaths, st_maxd[30], st_turns, st_boss_kill[30], st_boss_seen[30], st_qs[4][5], st_floor_turns_max;
static int st_uses[ACN_COUNT], st_kinds[IK_COUNT], st_ankh, st_wells, st_pot, st_buys, st_sells, st_talks[MT_COUNT], st_rt, st_fork, st_amulet, st_stuck;
static int st_unused, st_unused_c[5], st_unused0, st_wipe; static int s_resp_n, st_resp, st_resp_d[30]; static char s_resp_log[3000]; static int foes_near(int r); static int st_srch_w, st_fastback, st_surr, st_bounce; static int st_traps, st_deaths_d[30], st_explore_tgt, st_explore_bfs, st_search;
static char st_deathmsg[30][64];

// ---------------- structural checks on a new floor ----------------
static void dump_map(int mark) {
  for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c]; char ch = PASSABLE(t) ? '.' : t == T_WALL || t == T_WALL_DECO ? '#' : t == T_SECRET_DOOR ? 'S' : t == T_LOCKED_DOOR ? 'L' : (TFLAGS(t) & F_PIT) ? ':' : t == T_BARRICADE ? 'B' : t == T_LOCKED_EXIT ? 'X' : '?';
    if (t == T_DOOR) ch = '+'; if (IS_WATER(t)) ch = '~';
    if (heap_at(c) >= 0) ch = '*'; if (c == G.entrance) ch = '<'; if (c == G.exit) ch = '>'; if (c == mark) ch = '@'; putchar(ch); } putchar('\n'); }
  printf("   odd cells:"); for (int c = 0; c < MAP_LEN; c++) { int t = G.map[c]; if (!(PASSABLE(t) || t == T_WALL || t == T_WALL_DECO)) printf(" %d(%d,%d)=%d%s", c, cx(c), cy(c), t, BIT_GET(G.visited, c) ? "v" : ""); } printf("\n");
  for (int r = 0; r < G.n_rooms; r++) printf("   room %d: %d,%d-%d,%d type %d\n", r, G.rooms[r].left, G.rooms[r].top, G.rooms[r].right, G.rooms[r].bottom, G.rooms[r].type);
}
static bool in_room(int c, int type) { for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; if (R->type == type && cx(c) > R->left && cx(c) < R->right && cy(c) > R->top && cy(c) < R->bottom) return true; } return false; }
static void floor_checks(void) {
  // the floor must be solvable the way a person would: from the entrance (and, when a weak floor above makes one, the pit
  // room you fall into) through open ground, traps, secret doors (search) and barricades (burn), picking up the keys in
  // reach and opening a locked door with each, until the exit is reached and every locked door can be opened
  int d = G.depth;
  int locked = 0, lexit = 0, lchest = 0, crystal = 0;
  for (int c = 0; c < MAP_LEN; c++) { if (G.map[c] == T_LOCKED_DOOR) locked++; if (G.map[c] == T_LOCKED_EXIT) lexit++; }
  for (int h = 0; h < G.n_heaps; h++) {
    Item *it = &G.heaps[h].item;
    if (G.heaps[h].type == HEAP_LOCKED_CHEST) lchest++;
    if (G.heaps[h].type == HEAP_CRYSTAL_CHEST) crystal++;
    if ((it->kind == IK_IRON_KEY || it->kind == IK_GOLDEN_KEY) && it->lvl != d) viol("KEY_DEPTH", "key kind %d for depth %d lies on depth %d", it->kind, it->lvl, d);
  }
  bool boss = boss_depth(d);
  if (lexit && !boss && d != LAST_DEPTH) viol("LOCKED_EXIT", "a locked exit on a non-boss floor");
  if (d < LAST_DEPTH && G.map[G.exit] != T_EXIT && G.map[G.exit] != T_LOCKED_EXIT) viol("EXIT_TERRAIN", "exit cell %d has terrain %d", G.exit, G.map[G.exit]);
  if (d > 1 && G.map[G.entrance] != T_ENTRANCE) viol("ENTRANCE_TERRAIN", "entrance cell %d terrain %d", G.entrance, G.map[G.entrance]);
  static int16_t dist[MAP_LEN]; static uint8_t opened[MAP_LEN / 8];
  memset(opened, 0, sizeof opened);
  int iron = 0, gold = 0, used = 0;
  for (int round = 0; round < 40; round++) {
    for (int c = 0; c < MAP_LEN; c++) dist[c] = -1;
    int qh = 0, qt = 0;
    dist[G.entrance] = 0; b_q[qt++] = G.entrance;
    for (int c = 0; c < MAP_LEN; c++) if (in_room(c, RT_PIT) && PASSABLE(G.map[c]) && dist[c] < 0) { dist[c] = 0; b_q[qt++] = c; }
    while (qh < qt) {
      int c = b_q[qh++];
      for (int k = 0; k < 8; k++) {
        int n = c + N8[k];
        if (!nb_ok(c, n) || dist[n] >= 0) continue;
        int t = G.map[n], f = TFLAGS(t);
        bool ok = (f & F_PASSABLE) || ((f & F_AVOID) && !(f & F_PIT)) || t == T_SECRET_DOOR || t == T_BARRICADE || t == T_BOOKSHELF || BIT_GET(opened, n) || (t == T_LOCKED_EXIT && boss)
                  || ((f & F_PIT) && in_room(n, RT_TRAPS));   // a chasm trap room: its potion of levitation
        if (!ok) continue;
        dist[n] = dist[c] + 1; b_q[qt++] = n;
      }
    }
    iron = G.keys[d] & 15; gold = (G.keys[d] >> 4) & 3;
    for (int h = 0; h < G.n_heaps; h++) {
      Item *it = &G.heaps[h].item; int p = G.heaps[h].pos;
      bool reach = p >= MAP_LEN || dist[p] >= 0;          // (a creature carrying one: it comes to you)
      if (it->kind == IK_IRON_KEY && it->lvl == d && reach) iron += it->qty;   // (a stack of keys: each opens a door)
      if (it->kind == IK_GOLDEN_KEY && it->lvl == d && reach) gold += it->qty;
    }
    if (iron <= used) break;
    int door = -1;                                       // a locked door at the edge of what's reached
    for (int c = 0; c < MAP_LEN && door < 0; c++) if (G.map[c] == T_LOCKED_DOOR && !BIT_GET(opened, c))
      for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && dist[n] >= 0) { door = c; break; } }
    if (door < 0) break;
    BIT_SET(opened, door); used++;
  }
  if (used < locked) { viol("KEYS_UNSOLVABLE", "%d locked doors, only %d can be opened with the keys in reach (%d keys)", locked, used, iron); if (getenv("MAP")) dump_map(-1);
    for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == IK_IRON_KEY) printf("     iron key heap qty %d at %d (%d,%d) type %d lvl %d reached %d\n", G.heaps[h].item.qty, G.heaps[h].pos, cx(G.heaps[h].pos), cy(G.heaps[h].pos), G.heaps[h].type, G.heaps[h].item.lvl, G.heaps[h].pos < MAP_LEN ? dist[G.heaps[h].pos] : -9); }
  if (gold < lchest + (crystal + 1) / 2 && !getenv("NOGOLD")) viol("GOLDKEYS_MISSING", "%d locked + %d crystal chests, %d golden keys in reach", lchest, crystal, gold);
  if (d < LAST_DEPTH && d != 25 && dist[G.exit] < 0) {   // (Yog's floor: the hall's walls burn away once you step off the stairs)
    bool nb = false; for (int k = 0; k < 8; k++) { int n = G.exit + N8[k]; if (nb_ok(G.exit, n) && dist[n] >= 0) nb = true; }
    if (!nb) { viol("EXIT_UNREACHABLE", "exit %d not reachable from entrance %d", G.exit, G.entrance); if (getenv("MAP")) dump_map(-1); }
  }
  if (d == LAST_DEPTH) { bool am = false; for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == IK_AMULET) { am = true; if (G.heaps[h].pos >= MAP_LEN || dist[G.heaps[h].pos] < 0) viol("AMULET_UNREACHABLE", "amulet at %d", G.heaps[h].pos); }
    if (!am && !(G.dflags & DF_AMULET)) viol("AMULET_MISSING", "no amulet on depth 26"); }
}

// ---------------- save / load round trips ----------------
static Game s_copy;
static bool mobs_eq(const Mob *a, const Mob *b, bool amulet) {
  return a->pos == b->pos && a->HP == b->HP && a->HT == b->HT && a->type == b->type && a->flags == b->flags && a->uid == b->uid &&
         !memcmp(&a->b, &b->b, sizeof a->b) && a->aux == b->aux && a->aux2 == b->aux2 && a->vertigo == b->vertigo &&
         (amulet || (a->state == b->state && a->target == b->target)) && a->time == b->time;
}
static void roundtrip(void) {
  st_rt++;
  game_save();
  s_copy = G;
  if (!game_load()) { viol("LOAD_FAIL", "game_load failed after game_save"); return; }
  const Game *c = &s_copy;
  Hero h1 = c->hero, h2 = G.hero;
  if (h1.time < c->now) h1.time = c->now;
  if (memcmp(&h1, &h2, sizeof h1)) {
    const uint8_t *a = (const uint8_t *)&h1, *b = (const uint8_t *)&h2; int off = 0; while (a[off] == b[off]) off++;
    viol("RT_HERO", "hero differs at byte %d (%d vs %d)", off, a[off], b[off]);
  }
  if (memcmp(c->map, G.map, MAP_LEN)) { int i = 0; while (c->map[i] == G.map[i]) i++; viol("RT_MAP", "cell %d terrain %d -> %d", i, c->map[i], G.map[i]);
    int nd = 0; for (int k = 0; k < c->n_diffs; k++) if (c->diffs[k].pos == i) { nd++; printf("     diff before: %d -> %d\n", i, c->diffs[k].terrain); }
    for (int k = 0; k < G.n_diffs; k++) if (G.diffs[k].pos == i) printf("     diff after: %d -> %d\n", i, G.diffs[k].terrain);
    printf("     n_diffs %d -> %d, hg %d, cells differing:", c->n_diffs, G.n_diffs, BIT_GET(c->hg, i)); for (int k = 0; k < MAP_LEN; k++) if (c->map[k] != G.map[k]) printf(" %d:%d>%d", k, c->map[k], G.map[k]); printf("\n"); }
  if (memcmp(c->visited, G.visited, sizeof G.visited)) { int n1 = 0, n2 = 0, first = -1; for (int i = 0; i < MAP_LEN; i++) if (BIT_GET(c->visited, i) != BIT_GET(G.visited, i)) { if (first < 0) first = i; if (BIT_GET(c->visited, i)) n1++; else n2++; }
    viol("RT_VISITED", "visited cells differ: %d lost, %d gained, first %d (terrain %d, fov %d/%d), hero %d blind %d mind %d", n1, n2, first, G.map[first], BIT_GET(c->fov, first), BIT_GET(G.fov, first), G.hero.pos, G.hero.b.blind, G.hero.b.mind);
    printf("     gained:"); for (int i = 0; i < MAP_LEN; i++) if (BIT_GET(c->visited, i) != BIT_GET(G.visited, i)) printf(" %d(%d,%d)", i, cx(i), cy(i)); printf("\n     mobs:"); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf(" %s@%d/%d", mob_name(G.mobs[i].type), c->mobs[i].pos, G.mobs[i].pos); printf("\n"); }
  if (c->now != G.now || c->turn_due != G.turn_due || c->level_t0 != G.level_t0 || c->depth != G.depth || c->max_depth != G.max_depth)
    viol("RT_TIME", "now %d/%d turn_due %d/%d t0 %d/%d", c->now, G.now, c->turn_due, G.turn_due, c->level_t0, G.level_t0);
  if (memcmp(c->keys, G.keys, sizeof G.keys) || c->dew != G.dew || c->dflags != G.dflags || c->boss != G.boss || memcmp(&c->q, &G.q, sizeof G.q) ||
      memcmp(c->known, G.known, sizeof G.known) || c->pos_count != G.pos_count || c->sou_count != G.sou_count || c->soe_count != G.soe_count ||
      memcmp(c->pack, G.pack, sizeof G.pack) || memcmp(c->journal, G.journal, sizeof G.journal) || memcmp(c->fall, G.fall, sizeof G.fall) ||
      c->regen_left != G.regen_left || c->herbal != G.herbal || c->deferred != G.deferred || c->charm_uid != G.charm_uid || c->next_uid != G.next_uid ||
      memcmp(c->lvars, G.lvars, sizeof G.lvars) || c->health_pos != G.health_pos || c->weak_floors != G.weak_floors)
    viol("RT_GAME", "game fields differ (keys %d q %d known %d pack %d lvars %d boss %d/%d health %d/%d)", memcmp(c->keys, G.keys, sizeof G.keys) != 0, memcmp(&c->q, &G.q, sizeof G.q) != 0,
         memcmp(c->known, G.known, sizeof G.known) != 0, memcmp(c->pack, G.pack, sizeof G.pack) != 0, memcmp(c->lvars, G.lvars, sizeof G.lvars) != 0, c->boss, G.boss, c->health_pos, G.health_pos);
  int n = 0; bool am = G.dflags & DF_AMULET;
  for (int i = 0; i < c->n_mobs; i++) {
    if (c->mobs[i].HP <= 0) continue;
    while (n < G.n_mobs && G.mobs[n].HP <= 0) n++;
    if (n >= G.n_mobs) { viol("RT_MOBS", "mob %s lost", mob_name(c->mobs[i].type)); break; }
    Mob a = c->mobs[i]; int32_t dt = a.time - c->now; if (dt > 30000) a.time = c->now + 30000; if (dt < -30000) a.time = c->now - 30000;
    if (!mobs_eq(&a, &G.mobs[n], am)) { Mob *b = &G.mobs[n]; viol("RT_MOBS", "%s at %d: pos %d HP %d/%d st %d/%d tgt %d/%d time %d/%d aux %d/%d aux2 %d/%d fl %d/%d", mob_name(a.type), a.pos, b->pos, a.HP, b->HP, a.state, b->state, a.target, b->target, a.time - c->now, b->time - G.now, a.aux, b->aux, a.aux2, b->aux2, a.flags, b->flags); }
    n++;
  }
  if (c->n_heaps != G.n_heaps || memcmp(c->heaps, G.heaps, c->n_heaps * sizeof(Heap))) viol("RT_HEAPS", "heaps %d -> %d", c->n_heaps, G.n_heaps);
  if (c->n_plants != G.n_plants || memcmp(c->plants, G.plants, c->n_plants * sizeof(Plant))) viol("RT_PLANTS", "plants %d -> %d", c->n_plants, G.n_plants);
  if (c->entrance != G.entrance || c->exit != G.exit || c->n_rooms != G.n_rooms || memcmp(c->rooms, G.rooms, sizeof(Room) * c->n_rooms) || (c->well_pos >= 0 && c->well_kind != G.well_kind) || c->well_pos != G.well_pos || c->altar_pos != G.altar_pos)
    viol("RT_FLOOR", "floor layout differs after load: entr %d/%d exit %d/%d rooms %d/%d (%d) well %d/%d %d/%d altar %d/%d", c->entrance, G.entrance, c->exit, G.exit, c->n_rooms, G.n_rooms, memcmp(c->rooms, G.rooms, sizeof(Room) * c->n_rooms), c->well_kind, G.well_kind, c->well_pos, G.well_pos, c->altar_pos, G.altar_pos);
  for (int b = 0; b < BLOB_COUNT; b++) if ((g_blob[b] != NULL) != (s_copy.n_mobs >= 0 && 0)) { /* gas amounts: approximate by design */ }
}
// fork replay: the parent plays on; the child saves, loads and plays the same decisions; the states must agree after
static FILE *s_trace;
static int s_fork_until = -1, s_fork_fd = -1; static pid_t s_fork_pid;
static uint32_t state_hash(void) {
  uint32_t h = 2166136261u;
  #define HB(p, n) do { const uint8_t *q_ = (const uint8_t *)(p); for (int i_ = 0; i_ < (int)(n); i_++) h = (h ^ q_[i_]) * 16777619u; } while (0)
  Hero hh = G.hero; HB(&hh, sizeof hh); HB(G.map, MAP_LEN); HB(&G.now, 4); HB(&G.depth, 1);
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) { HB(&G.mobs[i].pos, 2); HB(&G.mobs[i].HP, 2); HB(&G.mobs[i].type, 1); }
  HB(G.heaps, G.n_heaps * sizeof(Heap));
  return h;
}
// ---------------- decisions ----------------
static int s_last_choice_cell;
static int upg_plan(bool *hold) {                         // bot3: which item the scroll of upgrade goes to; *hold: keep it for now (owner)
  Hero *h = &G.hero; PickMode p = PICK_UPGRADE; if (hold) *hold = false;
  int n = 0; for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == IK_SCR_UPGRADE) n += h->bag[i].qty; if (n < 1) n = 1;   // the scrolls in hand all go the same way
  double base = gear_cost(&h->weapon, &h->armor), bg = 0; int bs = -1000;
  // owner: plan the scrolls still to come before the next boss (3 per 5 floors) and the strength potions (2 per 5):
  // a tier-3 mace that +2 makes wearable beats a short sword +2 for the whole next region
  int B = ((G.depth - 1) / 5 + 1) * 5, r = B / 5 - 1; if (r > 4) r = 4;
  static const uint8_t SQ[5] = { 3, 6, 9, 12, 13 }, PQ[5] = { 2, 4, 6, 8, 9 };
  int F = SQ[r] - G.sou_count, Sg = PQ[r] - G.pos_count; if (F < 0) F = 0; if (Sg < 0) Sg = 0; if (G.depth == B) F = Sg = 0;
  bool plan = !getenv("UPG_GREEDY");
  double bsc = 1e18, bnow = 0;
  for (int sl = -2; sl < BAG_SIZE; sl++) { Item *it = game_slot(sl); int c = item_cat(it->kind); if (!it->kind || !game_pick_ok(p, sl)) continue;
    if (it->kind == IK_BOOMERANG && hunt2()) {         // 0.54: her boomerang (never heavier, every throw comes back)
      s_boom_plus = n; double now = gear_cost(&h->weapon, &h->armor); s_boom_plus = n + F; s_str_plus = Sg; double cp = gear_cost(&h->weapon, &h->armor); s_boom_plus = 0; s_str_plus = 0;
      if (!plan) { if (base - now > bg) { bg = base - now; bs = sl; } continue; }
      double sc = 0.4 * (now < base ? now : base) + 0.6 * cp;
      if (getenv("UPGDBG") && !s_rollout) printf("  upgrade plan (%d in hand): boomerang+%d now %.1f -> %.1f, at the boss %.1f: score %.1f\n", n, it->lvl, base, now, cp, sc);
      if (sc < bsc) { bsc = sc; bs = sl; bnow = now; } continue; }
    if ((c != CAT_WEAPON && c != CAT_ARMOR) || it->kind == IK_PICKAXE || cursed_known(it)) continue;
    if (hunt2() && c == CAT_WEAPON && item_tier(it->kind) <= 1) continue;   // (her dagger: the worst use of a scroll)
    Item t = *it; if (!(t.flags & IF_LEVEL_KNOWN)) t.lvl = getenv("OLDGEARFIX") ? (t.lvl > 0 ? t.lvl : 0) : 0;   // (0.52 audit: no peeking at an unknown level)
    if (item_broken(it) && !getenv("OLDGEARFIX")) { t.dur = item_max_dur(it); t.lvl += (plan ? n : 1) - 1; } else   // a broken piece: the first scroll only mends it
    t.lvl += plan ? n : 1;
    double c1 = c == CAT_WEAPON ? gear_cost(&t, &h->armor) : gear_cost(&h->weapon, &t), now = c1 < base ? c1 : base;   // (unworn and worse: the old piece stays on for now)
    if (!getenv("OLDGLYPH") && it->ench && it->lvl >= 2) { Item t0 = t; t0.ench = 0; double keep = 1.0 / it->lvl, c0 = c == CAT_WEAPON ? gear_cost(&t0, &h->armor) : gear_cost(&h->weapon, &t0);   // 0.52 (audit): an upgrade may wash the enchantment off ((L-1)/L at +L)
      c1 = keep * c1 + (1 - keep) * c0; now = c1 < base ? c1 : base; }
    if (!plan) { if (base - c1 > bg) { bg = base - c1; bs = sl; } continue; }
    Item tp = t; tp.lvl += F; s_str_plus = Sg;
    double cp = c == CAT_WEAPON ? gear_cost(&tp, &h->armor) : gear_cost(&h->weapon, &tp), bp = gear_cost(&h->weapon, &h->armor); s_str_plus = 0;
    double sc = 0.4 * now + 0.6 * (cp < bp ? cp : bp);
    if (getenv("UPGDBG") && !s_rollout) { char nm[64]; item_name(it, nm, sizeof nm); printf("  upgrade plan (%d in hand): %-30s now %.1f -> %.1f, at the boss (+%d more, STR+%d) %.1f: score %.1f\n", n, nm, base, now, F, Sg, cp, sc); }
    if (sc < bsc) { bsc = sc; bs = sl; bnow = now; } }
  if (hold && plan && bs > -1000 && bnow >= base * 0.98 && G.depth != B && !getenv("UPG_NOHOLD")) *hold = true;   // owner: no help yet: keep it in the bag (flexibility)
  return bs;
}
static int s_force_pick = -999;                          // bot3: the strategic look-ahead chose the upgrade's target
static uint32_t s_salt;   // 0.93: BOTSALT=n: the same seed, another hand of the bot's own dice (a ticket on a good seed)
static long st_sou_read, st_sou_pos; static long st_brkw, st_brka, st_brkn;
// 0.92 (owner: "alchemy through the same bag valuation"): Heap.transmute with n seeds: 1 in n a random potion, else the
// potion of a seed picked by count. A brew is worth it when the potion beats the three seeds it eats plus the bag slot it
// needs (nothing, with a free slot or a stack to join; else the least useful thing in the bag). Three of one kind whose
// potion is unknown also names that potion (2 times in 3): worth a bit more.
static int s_brew_plan[3], s_brew_i = 3;
static int brew_value(const int kinds[3]) {
  int ev = 0; for (int j = 0; j < 3; j++) { int pk = seed_potion(kinds[j]); Item t = (Item){ .kind = (uint8_t)pk, .qty = 1 }; int v = pk ? item_value(&t) : 0; if (pk && !item_known(pk)) { int kv = v; Item u = t; (void)u; v = kv; } ev += v * 2 / 9; }   // (each seed: picked 1 in 3 of the 2-in-3 share)
  ev += 45 / 3;                                         // (1 in 3: a random potion, ~45)
  if (kinds[0] == kinds[1] && kinds[1] == kinds[2] && !item_known(seed_potion(kinds[0]))) ev += 15;
  return ev;
}
static int cook_pick(PickMode p) {
  Hero *h = &G.hero;
  if (s_brew_i < 3) { for (; s_brew_i < 3; s_brew_i++) ALL_SLOTS(s) if (game_pick_ok(p, s) && game_slot(s)->kind == s_brew_plan[s_brew_i]) { s_brew_i++; s_cooked++; return s; } return -1000; }
  if (s_cooked >= 12) return -1000;
  int cnt[IK_COUNT]; memset(cnt, 0, sizeof cnt); ALL_SLOTS(s) if (game_pick_ok(p, s) && game_slot(s)->kind != IK_SEED_ROTBERRY) cnt[game_slot(s)->kind] += game_slot(s)->qty;
  int best[3] = { 0, 0, 0 }, bnet = -1000;
  for (int k = 0; k < IK_COUNT; k++) if (cnt[k] >= 3) { int t[3] = { k, k, k }; Item sd = (Item){ .kind = (uint8_t)k, .qty = 3 }; int net = brew_value(t) - item_value(&sd); if (net > bnet) { bnet = net; memcpy(best, t, sizeof t); } }
  { int t[3] = { 0, 0, 0 }, n = 0; for (int r = 0; r < 3; r++) { int bk = 0, bv = -1; for (int k = 0; k < IK_COUNT; k++) { int used = 0; for (int j = 0; j < n; j++) used += t[j] == k; if (cnt[k] - used <= 0) continue; int pk = seed_potion(k); Item pt = (Item){ .kind = (uint8_t)pk, .qty = 1 }, sd = (Item){ .kind = (uint8_t)k, .qty = 1 }; int v = (pk ? item_value(&pt) : 0) - item_value(&sd); if (v > bv) { bv = v; bk = k; } } if (bk) t[n++] = bk; }
    if (n == 3) { int sv = 0; for (int j = 0; j < 3; j++) { Item sd = (Item){ .kind = (uint8_t)t[j], .qty = 1 }; sv += item_value(&sd); } int net = brew_value(t) - sv; if (net > bnet) { bnet = net; memcpy(best, t, sizeof t); } } }
  if (!best[0]) return -1000;
  int slotcost = 0; if (bag_used() >= BAG_SIZE - 1) { int mn = 100000; for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind) { int v = item_value(&h->bag[i]); if (v < mn) mn = v; } slotcost = mn; }
  if (getenv("COOKDBG") && !s_rollout) printf("  COOK step %d plan %d %d %d net %d slotcost %d free %d\n", s_bot_steps, best[0], best[1], best[2], bnet, slotcost, BAG_SIZE - 1 - bag_used());
  if (bnet - slotcost <= 0) return -1000;
  memcpy(s_brew_plan, best, sizeof best); s_brew_i = 0; s_brew_kind = best[0] == best[1] && best[1] == best[2] ? best[0] : -1; s_brew_pot = g_trade_arg;
  return cook_pick(p);
}
static int pick_for(PickMode p) {                        // an item selector is open: what a person would choose
  Hero *h = &G.hero;
  if (p == PICK_UPGRADE && !s_rollout) st_sou_read++;
  if (p == PICK_UPGRADE && getenv("ARMLATE") && G.depth >= atoi(getenv("ARMLATE")) && h->armor.kind && !CLASS_ARMOR(h->armor.kind) && item_tier(h->armor.kind) >= 3 && game_pick_ok(p, -2) && s_force_pick == -999) return -2;   // 0.93 test: from floor N on, upgrades into the armour (DR is tier x (2 + level): what the bosses there test)
  if (p == PICK_UPGRADE && getenv("ARMFIRST") && h->armor.kind && !CLASS_ARMOR(h->armor.kind) && item_tier(h->armor.kind) >= atoi(getenv("ARMFIRST")) && game_pick_ok(p, -2)) { s_force_pick = -999; return -2; }   // 0.54 test: the winners wore heavy armour with every upgrade in it
  if ((p == PICK_UPGRADE || p == PICK_ENCHANT) && s_force_pick != -999) { int f = s_force_pick; s_force_pick = -999; if (game_pick_ok(p, f)) return f; }
  switch (p) {
    case PICK_IDENTIFY: {
      if (s_toe) for (int sl = 0; sl < BAG_SIZE; sl++) if (game_pick_ok(p, sl) && toe_keep(sl)) return sl;   // 0.73 TOE (owner: "identify the armor we're 1 point away from wearing"): the piece the plan grows into
      if (!getenv("OLDHERO")) {                          // 0.52 (audit): what wearing won't tell: potions while Healing/Strength are unknown (the commonest colour), wands, gear in the bag; worn gear last (it tells itself)
        int bp = -1, bq = 0; if (!item_known(IK_POT_HEALING) || !item_known(IK_POT_STRENGTH)) ALL_SLOTS(s) { Item *it = game_slot(s); if (game_pick_ok(p, s) && item_cat(it->kind) == CAT_POTION && it->qty > bq) { bq = it->qty; bp = s; } }
        if (bp >= 0) return bp;
        ALL_SLOTS(s) if (game_pick_ok(p, s) && item_cat(game_slot(s)->kind) == CAT_WAND) return s;
        ALL_SLOTS(s) if (game_pick_ok(p, s) && (item_cat(game_slot(s)->kind) == CAT_ARMOR || item_cat(game_slot(s)->kind) == CAT_WEAPON || item_cat(game_slot(s)->kind) == CAT_RING)) return s; }
      int order[] = { -1, -2, -3, -4 };
      for (int i = 0; i < 4; i++) if (game_pick_ok(p, order[i])) return order[i];
      ALL_SLOTS(s) if (game_pick_ok(p, s) && (item_cat(game_slot(s)->kind) == CAT_RING || item_cat(game_slot(s)->kind) == CAT_WAND)) return s;
      ALL_SLOTS(s) if (game_pick_ok(p, s)) return s;
      return -1000;
    }
    case PICK_UPGRADE: case PICK_ENCHANT: {
      if (p == PICK_UPGRADE && PRM[P_GEARMODEL]) { int bs = s_toe && !s_rollout && !getenv("TOENOUP") ? toe_choose(NULL) : upg_plan(NULL); if (bs > -1000) return bs; }
      bool arm = h->armor.kind && game_pick_ok(p, -2), wpn = h->weapon.kind && game_pick_ok(p, -1);
      if (p == PICK_ENCHANT && !getenv("OLDGEARFIX")) {   // 0.52 (audit): cursed or broken first, then armour with no glyph (+tier DR for good), a plain weapon; never re-roll a good one
        if (arm && (cursed_known(&h->armor) || item_broken(&h->armor))) return -2; if (wpn && (cursed_known(&h->weapon) || item_broken(&h->weapon))) return -1;
        if (arm && !h->armor.ench) return -2; if (wpn && !h->weapon.ench) return -1;
        if (arm && (h->armor.ench == AG_ANTI_ENTROPY || h->armor.ench == AG_POTENTIAL || h->armor.ench == AG_MULTIPLICITY)) return -2;
        if (wpn && h->weapon.ench == WE_INSTABILITY) return -1;
        return arm ? -2 : wpn ? -1 : -1000; }
      if (p == PICK_ENCHANT) return wpn ? -1 : arm ? -2 : -1000;
      if (arm && wpn) return h->armor.lvl <= h->weapon.lvl ? -2 : -1;   // (owner: what you can wear now, the lower of the two)
      if (arm) return -2; if (wpn) return -1;
      ALL_SLOTS(s) if (game_pick_ok(p, s)) return s;
      return -1000;
    }
    case PICK_RING: return -3;
    case PICK_REFORGE: return h->weapon.kind && game_pick_ok(p, -1) ? -1 : -1000;
    case PICK_ARMOR: return h->armor.kind ? -2 : -1000;
    case PICK_WEAPON: return h->weapon.kind && game_pick_ok(p, -1) ? -1 : -1000;
    case PICK_SEED: {                                    // the alchemy pot: any seed but the quest's rotberry
      if (s_pztest && !getenv("OLDCOOK") && !getenv("COOK1")) { return cook_pick(p); }
      if (s_pztest && !getenv("OLDCOOK")) {             // 0.92 (owner: potions beat seeds): Heap.transmute: 1 in n a random potion, else the potion of a seed picked by its count: Sungrass gives Healing, so the best seeds go in first (3 Sungrass: Healing 2 times in 3)
        static const uint8_t PRI[] = { IK_SEED_SUNGRASS, IK_SEED_DREAMWEED, IK_SEED_EARTHROOT, IK_SEED_FIREBLOOM, IK_SEED_ICECAP, IK_SEED_SORROWMOSS, IK_SEED_FADELEAF };
        int nseed = 0; ALL_SLOTS(s) if (game_pick_ok(p, s) && game_slot(s)->kind != IK_SEED_ROTBERRY) nseed += game_slot(s)->qty;
        if (s_cooked < 9 && (s_cooked % 3 || nseed >= 3)) for (unsigned j = 0; j < sizeof PRI; j++) ALL_SLOTS(s) if (game_pick_ok(p, s) && game_slot(s)->kind == PRI[j]) { s_cooked++; return s; }
        return -1000; }
      ALL_SLOTS(s) if (game_pick_ok(p, s) && game_slot(s)->kind != IK_SEED_ROTBERRY && s_cooked < 3) { s_cooked++; return s; }
      return -1000;
    }
    case PICK_SELL: {                                    // the unwanted: worse weapons and armour, extra rings and wands that are known junk
      if (s_shop_sold > 6) return -1000;
      for (int s = 0; s < BAG_SIZE; s++) {
        Item *it = &h->bag[s]; int c = item_cat(it->kind);
        if (!it->kind || !game_pick_ok(p, s)) continue;
        if (s_toe && (c == CAT_ARMOR || c == CAT_WEAPON) && toe_keep(s)) continue;   // 0.73 TOE
        if (sell_extra(it) || (c == CAT_ARMOR && armor_score(it) <= armor_score(&h->armor)) || (c == CAT_WEAPON && it->kind != IK_PICKAXE && weapon_score(it) <= weapon_score(&h->weapon))) { s_shop_sold++; st_sells++; return s; }
      }
      return -1000;
    }
    case PICK_SMITH: {                                   // two identified items of a kind, neither cursed
      static int first = -1000;
      if (g_trade_arg != 2) first = -1000;
      for (int a = -2; a < BAG_SIZE; a++) for (int b = a + 1; b < BAG_SIZE; b++) {
        Item *x = game_slot(a), *y = game_slot(b);
        if (!x->kind || x->kind != y->kind || !item_upgradable(x->kind) || !item_identified(x) || !item_identified(y) || (x->flags & IF_CURSED) || (y->flags & IF_CURSED) || x->lvl < 0 || y->lvl < 0) continue;
        if (first == -1000) { first = a; return a; }
        if (a == first) { first = -1000; return b; }
      }
      first = -1000; return -1000;
    }
    default: return -1000;
  }
}
static int wnd_for(void) {                              // a window with buttons: which one
  if (g_trade == 1) { int hh = heap_at(g_trade_arg); if (hh >= 0 && strncmp(s_wnd_b1, "Buy", 3) == 0 && (getenv("OLDBUY") || want_buy(hh))) { st_buys++; return 0; } /* 0.50 (review g6): only what it meant to buy (walking over the wares once cost 1200g for a sword it sold back for 80) */ s_black[g_trade_arg] = s_bot_steps + 3000; s_sale_gold[g_trade_arg] = G.hero.gold; return 1; }
  if (g_trade == 3 && !getenv("OLDGEARFIX")) {   // 0.52 (audit): the Tome: the way that suits this bot (no wand-in-hand melee, so not Battlemage)
    if (strncmp(s_wnd_b1, "Yes", 3) == 0) return 1;      // (a respec: no)
    static const char *const PREF[] = { "Gladiator", "Warlock", "Freerunner", "Sniper" };
    return strncmp(s_wnd_b1, PREF[G.hero.hclass & 3], 5) == 0 ? 0 : 1; }
  if (g_trade) return 0;                                 // sell one, the tome's first way, the weightstone, the ankh, "read anyway"
  if (s_wnd_type == MT_BLACKSMITH) { int pa = 0; for (int a = -2; a < BAG_SIZE; a++) for (int b = a + 1; b < BAG_SIZE; b++) { Item *x = game_slot(a), *y = game_slot(b);
      if (x->kind && x->kind == y->kind && item_upgradable(x->kind) && item_identified(x) && item_identified(y) && !(x->flags & IF_CURSED) && !(y->flags & IF_CURSED) && x->lvl >= 0 && y->lvl >= 0) pa = 1; }
    return pa ? 0 : 1; }
  if (s_wnd_type == MT_GHOST && !getenv("OLDQBUG")) {   // 0.52 (audit): "Yes, I will do it" to the curse; the reward by the model, on the two items he shows
    if (strncmp(s_wnd_b1, "Yes", 3) == 0) return (attack_wand_ready() || curse_go()) && G.hero.HP >= G.hero.HT * 9 / 10 ? 0 : 1;   // (review: the curse dodges 65 and regenerates: melee can't win it; a wand never misses)   // (the curse rises as a wraith of 10+3*depth HP beside you, hunting: only when ready)
    Item bw = believed(&G.hero.weapon, 0), ba = believed(&G.hero.armor, 0);
    return gear_cost(&G.q.ghost_weapon, &ba) <= gear_cost(&bw, &G.q.ghost_armor) ? 0 : 1; }
  if (s_wnd_type == MT_GHOST) return G.hero.weapon.lvl > G.hero.armor.lvl ? 1 : 0;
  return 0;
}
static int s_slot_gen;
static bool fire_safe(int c) {                          // bot2: no flammable ground within 2 (a fire potion's flames spread on grass, doors)
  for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int x = cx(c) + dx, y = cy(c) + dy; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue; if (TFLAGS(G.map[x + y * MAP_W]) & F_FLAMABLE) return false; }
  return true;
}
static bool water_near(int c, int r) { bfs(c, false); for (int i = 0; i < MAP_LEN; i++) if (IS_WATER(G.map[i]) && b_dist[i] <= r) return true; return false; }   // bot2: a fire potion: water to jump into
static void on_new_floor(bool first); static int strategic_upgrade(int scroll); static int32_t s_upg_hold_until; static bool la_step(void);
static bool s_resting; static int s_test_walk = -1; static int room_at(int c);
static int s_stash = -1, s_stash_t, s_stash_drink = -1, s_nw_n, s_nw_last; static uint8_t s_stashbit[MAP_LEN / 8 + 1];   // (0.91: every cell the stash's scrolls lie on: a second drop rolls to a cell beside the first)
static bool in_stash(int c) { return s_stash >= 0 && (unsigned)c < MAP_LEN && BIT_GET(s_stashbit, c); }                         // bot3 (owner): scrolls put down while a potion that might be liquid flame is tested
static bool scrolls_loose(void) { for (int i = 0; i < BAG_SIZE; i++) if (item_cat(G.hero.bag[i].kind) == CAT_SCROLL) return true; return false; }   // (the scroll holder keeps its own safe)
static bool stash_for(int drink) {                       // true: acted (dropped a scroll stack, or walked to where they go)
  Hero *h = &G.hero; if (item_known(IK_POT_FLAME) || !scrolls_loose() || getenv("NOSTASH") || s_stash_drink == -2) return false;
  if (h->pos == s_stash || (distance(h->pos, drink) >= 3 && !(TFLAGS(G.map[h->pos]) & F_FLAMABLE) && heap_at(h->pos) < 0 && h->pos != G.exit && h->pos != G.entrance && G.map[h->pos] != T_WELL && G.map[h->pos] != T_ALCHEMY)) {
    for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_SCROLL) { if (s_stash < 0) memset(s_stashbit, 0, sizeof s_stashbit); s_stash = h->pos; s_stash_t = s_bot_steps; BIT_SET(s_dropped, h->pos); if (!s_rollout) st_stash++; s_did[0] = 0; DID("put the scrolls down before testing a potion (liquid flame?)"); do_item(i, ACN_DROP); for (int k = 0; k < G.n_heaps; k++) if ((unsigned)G.heaps[k].pos < MAP_LEN && distance(G.heaps[k].pos, h->pos) <= 1 && item_cat(G.heaps[k].item.kind) == CAT_SCROLL) { BIT_SET(s_stashbit, G.heaps[k].pos); BIT_SET(s_dropped, G.heaps[k].pos); } return true; } }
  bfs(s_pztest ? drink : h->pos, false); int best = -1, bd = 20;   // (0.91: beside where I'll drink, not where I am: it walked 37 turns between them)
  for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < bd && distance(c, drink) >= 3 && !(TFLAGS(G.map[c]) & F_FLAMABLE) && heap_at(c) < 0 && c != G.exit && c != G.entrance && G.map[c] != T_WELL && G.map[c] != T_ALCHEMY && !(TFLAGS(G.map[c]) & F_AVOID)) { bd = b_dist[c]; best = c; }
  if (best < 0) return false;
  int16_t p0 = h->pos; s_did[0] = 0; DID("to a safe spot for the scrolls"); walk(tap_safe(best)); if (h->pos == p0) { s_stash_drink = -2; return false; } return true;
}
static bool stash_ready(void) { return s_stash >= 0 && (item_known(IK_POT_FLAME) || unknown_potion() < 0 || s_bot_steps > s_stash_t + 80); }
static int trap_room_kind_any(int r) { Room *R = &G.rooms[r]; for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) if (TFLAGS(G.map[x + y * MAP_W]) & F_AVOID) return 1; return 0; }
static bool gear_worth_upgrading(void) {                // 0.91: something better than the starting kit to put an Upgrade into
  Hero *h = &G.hero;
  if ((h->armor.kind && item_str(&h->armor) >= 13) || (h->weapon.kind && item_str(&h->weapon) + h->weapon.lvl >= 14)) return true;
  for (int sl = -4; sl < BAG_SIZE; sl++) { Item *it = game_slot(sl); int c = item_cat(it->kind); if (c == CAT_WAND || c == CAT_RING) return true;
    if (sl >= 0 && !cursed_known(it) && ((c == CAT_ARMOR && item_str(it) >= 13) || (c == CAT_WEAPON && it->kind != IK_PICKAXE && item_str(it) + it->lvl >= 14))) return true; }
  return false;
}
static int room_at_door_in(int door) {                  // the trap room a door opens into (the room beside it that's RT_TRAPS), or -2
  for (int k = 0; k < 8; k++) { int n = door + N8[k]; if (!nb_ok(door, n)) continue; int r = room_at(n); if (r >= 0 && G.rooms[r].type == RT_TRAPS) return r; }
  return -2;
}
static int test_cell(void) {                             // bot2: drink-test standing in water (a fire potion goes out at once): the cell, or -1
  Hero *h = &G.hero;
  if (!getenv("OLDTESTSPOT")) {                          // 0.49 (owner): in water while Liquid Flame is unknown, beside a door while the gas
    bool wet = !item_known(IK_POT_FLAME), atpz = false, dooq = !item_known(IK_POT_TOXIC) || !item_known(IK_POT_PARALYTIC);   // potions are (out of it and shut
    int pz = -1;                                         // the door), and near a puzzle room still to do (levitation doesn't last)
    for (int i = 0; i < G.n_heaps && pz < 0; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos)) continue; int r = room_at(hp->pos);
      if (r >= 0 && !room_skipped(r) && G.rooms[r].type == RT_TRAPS && trap_room_kind_any(r)) pz = room_door(r); }
    if (s_pztest && pz >= 0 && !item_known(IK_POT_LEVITATION)) { atpz = true; wet = !item_known(IK_POT_FLAME); }   // 0.91 (owner: "wait to test by the puzzle"): a trap room still to do: test at its door (levitation flies straight in), not in far water   // 0.92: but in water while liquid flame is unknown (a dry door test took 17 of 25 HP)
    static int16_t dpz[MAP_LEN]; if (atpz) { bfs(pz, false); memcpy(dpz, b_dist, sizeof dpz); bool any = false; for (int c = 0; c < MAP_LEN && !any; c++) if (IS_WATER(G.map[c]) && !PWATER(c) && dpz[c] <= 6 && room_at(c) != room_at_door_in(pz)) any = true; if (wet && !any) atpz = false; }   // (no water near the door: an ordinary water test; a known levitation comes back)
    bfs(h->pos, false); int best = -1, bs = 1 << 30;
    for (int c = 0; c < MAP_LEN; c++) {
      if (b_dist[c] > 30 || s_black[c] > s_bot_steps || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || heap_at(c) >= 0 || (wet && !IS_WATER(G.map[c])) || (room_at(c) >= 0 && G.rooms[room_at(c)].type == RT_POOL) || (s_stash >= 0 && distance(c, s_stash) < 3) || c == G.entrance || c == G.exit) continue;
      if (!wet && (TFLAGS(G.map[c]) & F_FLAMABLE)) continue;
      int dd = 99; if (dooq) for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (G.map[n] == T_DOOR || G.map[n] == T_OPEN_DOOR)) dd = 0; }
      if (dooq && dd) for (int y = cy(c) - 3; y <= cy(c) + 3; y++) for (int x = cx(c) - 3; x <= cx(c) + 3; x++) if ((unsigned)x < MAP_W && (unsigned)y < MAP_H && (G.map[x + y * MAP_W] == T_DOOR || G.map[x + y * MAP_W] == T_OPEN_DOOR)) { int d2 = distance(c, x + y * MAP_W); if (d2 < dd) dd = d2; }
      if (atpz && ((wet ? dpz[c] > 6 : distance(c, pz) > 3) || room_at(c) == room_at_door_in(pz))) continue;
      int sc = b_dist[c] + (dooq && !atpz ? 8 * (dd > 3 ? 4 : dd) : 0) + (pz >= 0 ? (atpz ? 10 : 3) * distance(c, pz) : 0) - (atpz && IS_WATER(G.map[c]) ? 6 : 0);
      if (sc < bs) { bs = sc; best = c; } }
    return best;
  }
  if (IS_WATER(G.map[h->pos]) && !(room_at(h->pos) >= 0 && G.rooms[room_at(h->pos)].type == RT_POOL) && (s_stash < 0 || distance(h->pos, s_stash) >= 3)) return h->pos;
  bfs(h->pos, false); int best = -1, bd = 12;
  for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && b_dist[c] < bd && s_black[c] <= s_bot_steps && heap_at(c) < 0 && !(room_at(c) >= 0 && G.rooms[room_at(c)].type == RT_POOL) && (s_stash < 0 || distance(c, s_stash) >= 3)) { bd = b_dist[c]; best = c; }
  return best;
}
static bool test_spot(void) { int c = test_cell(); if (c >= 0 && c != G.hero.pos) s_test_walk = c; return c >= 0 && c == G.hero.pos; }
// 0.52 (audit): Sungrass heals HT/10 every 5 turns while you stand where it fired, and it fires only when trodden on
// (planting doesn't press the cell): plant, step off, step back on, rest there. A sungrass bush in sight (a garden) too.
static int s_sun = -1, s_sun_ph;
static bool sun_dance(void) {
  Hero *h = &G.hero; int s;
  if (enemy_count()) { s_sun = -1; return false; }
  if (s_sun >= 0) {
    if (s_sun_ph == 1) { s_sun_ph = 2; int best = -1; for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c) && PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & F_AVOID) && !char_at(c) && plant_at(c) < 0) { best = c; break; } }
      if (best >= 0) { DID("sungrass: step off it..."); walk(game_tap_cell(best)); return true; } s_sun = -1; return false; }
    if (s_sun_ph == 2) { s_sun_ph = 3; if (plant_at(s_sun) >= 0 && h->pos != s_sun) { DID("sungrass: ...and back on: it heals while I stand here"); walk(tap_safe(s_sun)); return true; } }
    if (G.health_pos == h->pos && h->HP < h->HT && (!game_starving() || s_pztest)) { DID("rest on the sungrass"); walk(game_rest(false)); return true; }
    s_sun = -1; return false;
  }
  if (h->HP >= h->HT * 3 / 4 || (game_starving() && !s_pztest)) return false;   // (0.92: Sungrass mends HT/10 a 5 turns starving or not: n60 died at the Goo, starving, with one in the bag)
  if (G.health_pos == h->pos && h->HP < h->HT) { DID("rest on the sungrass"); walk(game_rest(false)); return true; }
  bfs(h->pos, false); int bush = -1; for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 4 && BIT_GET(G.visited, G.plants[i].pos) && b_dist[G.plants[i].pos] <= 15 && !char_at(G.plants[i].pos)) bush = G.plants[i].pos;
  if (bush >= 0) { s_sun = bush; s_sun_ph = 3; DID("hurt: to the sungrass bush to heal"); walk(tap_safe(bush)); return true; }
  if ((s = find_kind(IK_SEED_SUNGRASS)) >= 0) for (int k = 0; k < 8; k++) { int c = h->pos + N8[k];   // (owner) thrown beside me, then a step onto it: one action less than planting
    if (!nb_ok(h->pos, c) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & (F_AVOID | F_PIT)) || char_at(c) || plant_at(c) >= 0 || heap_at(c) >= 0 || IS_WATER(G.map[c]) || c == G.entrance || c == G.exit) continue;
    st_uses[ACN_THROW]++; s_sun = c; s_sun_ph = 2; DID("hurt: throw sungrass beside me"); walk(game_throw(s, c)); return true; }
  return false;
}
static bool soures(void) { return s_pztest && !getenv("NOSOURES"); }
static bool use_items_when_safe(void) {                 // housekeeping with no enemy in sight; true: did something
  Hero *h = &G.hero;
  // known good potions and scrolls
  int s;
  if ((s = find_kind(IK_POT_STRENGTH)) >= 0 && item_known(IK_POT_STRENGTH) && (getenv("OLDWELL") || !(G.well_pos >= 0 && G.well_kind == 2 && G.map[G.well_pos] == T_WELL && BIT_GET(G.visited, G.well_pos)))) { st_uses[ACN_DRINK]++; do_item(s, ACN_DRINK); return true; }
  if ((s = find_kind(IK_POT_MIGHT)) >= 0 && item_known(IK_POT_MIGHT)) { do_item(s, ACN_DRINK); return true; }
  if (s_pztest && !getenv("NOTORCH") && G.depth >= 22 && G.depth <= 25 && !h->light && (s = find_kind(IK_TORCH)) >= 0 && s < BAG_SIZE) { if (!s_rollout) DID("light a torch (sight here is %d; the eyes, succubi and scorpios see 4)", 25 - G.depth); do_item(s, ACN_LIGHT); return true; }   // 0.93 (audit): HallsLevel.viewDistance = 25 - depth
  if ((s = find_kind(IK_POT_EXPERIENCE)) >= 0 && item_known(IK_POT_EXPERIENCE)) { do_item(s, ACN_DRINK); return true; }
  if (!getenv("OLDGEARFIX") && (s = G.hero.b.blind ? -1 : find_kind(IK_SCR_UPGRADE)) >= 0 && item_known(IK_SCR_UPGRADE)) {   // 0.52 (audit): worn gear about to break (1/6 left, its level known): upgrade it now: +1 and mended (broken, it would only be mended)
    for (int w = -2; w <= -1; w++) { Item *it = game_slot(w); if (it->kind && it->lvl > 0 && (it->flags & IF_LEVEL_KNOWN) && !item_broken(it) && it->dur * (soures() ? (getenv("MEND4") ? 4 : 3) : 6) <= item_max_dur(it) && game_pick_ok(PICK_UPGRADE, w)) {
      s_force_pick = w; DID("upgrade the %s before it breaks", w == -1 ? "weapon" : "armour"); do_item(s, ACN_READ); return true; } } }
  if (s_pztest && boss_depth(G.depth) && enemy_count() == 0 && (s = G.hero.b.blind ? -1 : find_kind(IK_SCR_UPGRADE)) >= 0 && item_known(IK_SCR_UPGRADE) && (game_pick_ok(PICK_UPGRADE, -2) || game_pick_ok(PICK_UPGRADE, -1))) {   // 0.92 (seed 84: held from floor 4 into the Goo fight and beyond): the boss floor: no more holding
    if (!s_rollout) DID("the boss floor: the held upgrade goes in now"); do_item(s, ACN_READ); return true; }
  if (s_pztest && !getenv("NOREFORGE") && enemy_count() == 0 && h->weapon.kind && h->weapon.kind != IK_SHORT_SWORD && h->weapon.kind != IK_PICKAXE && !cursed_known(&h->weapon))   // 0.92 (audit): the Short Sword's reforge: a +1 one gives its +1 (and a full repair, never losing the enchantment) to the weapon in hand
    for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == IK_SHORT_SWORD && h->bag[i].lvl > 0 && !cursed_known(&h->bag[i])) { if (!s_rollout) DID("reforge the old short sword into the weapon in hand"); do_item(i, ACN_REFORGE); return true; }
  if (soures() && (s = find_kind(IK_SCR_UPGRADE)) >= 0 && item_known(IK_SCR_UPGRADE) && game_slot(s)->qty == 1 && !boss_depth(G.depth) && ((h->armor.lvl >= 3 && (h->armor.flags & IF_LEVEL_KNOWN)) || (h->weapon.lvl >= 3 && (h->weapon.flags & IF_LEVEL_KNOWN)))) {   // 0.93 (zone run: plate+6 broke, then monks: an upgrade mends too): one scroll kept back to mend the piece about to break (it lasts 6x / 5x (16 - level) hits); more go in at once
    if (!s_rollout) st_upg_holds++; }
  else if ((s = G.hero.b.blind ? -1 : find_kind(IK_SCR_UPGRADE)) >= 0 && item_known(IK_SCR_UPGRADE) && G.now >= s_upg_hold_until && !(getenv("UPGHOLDBOSS") && !boss_depth(G.depth) && !boss_depth(G.depth + 1)) &&   /* 0.58 test (owner: hold for better gear): read only on or just before a boss floor */ (game_pick_ok(PICK_UPGRADE, -2) || game_pick_ok(PICK_UPGRADE, -1))) {
    if (getenv("ARMLATE") && G.depth >= atoi(getenv("ARMLATE")) && h->armor.kind && !CLASS_ARMOR(h->armor.kind) && item_tier(h->armor.kind) >= 3) { do_item(s, ACN_READ); return true; }   /* (ARMLATE test: no playouts: into the armour) */
    if (!s_rollout && !getenv("NOSTRAT")) { int r = s_toe && !getenv("TOENOUP") ? toe_upgrade(s) : strategic_upgrade(s); if (r == 1) return true; if (r == 0) { s_upg_hold_until = G.now + 400 * TICK; } }
    else { bool hold = false; if (PRM[P_GEARMODEL]) upg_plan(&hold); if (!hold) { do_item(s, ACN_READ); return true; } if (!s_rollout) st_upg_holds++; }
  }
  if ((s = G.hero.b.blind ? -1 : find_kind(IK_SCR_ENCHANT)) >= 0 && item_known(IK_SCR_ENCHANT) && (game_pick_ok(PICK_ENCHANT, -1) || game_pick_ok(PICK_ENCHANT, -2)) &&
      (getenv("OLDGEARFIX") || ({ Item *a = &h->armor, *w = &h->weapon; (a->kind && (cursed_known(a) || item_broken(a) || !a->ench || a->ench == AG_ANTI_ENTROPY || a->ench == AG_POTENTIAL || a->ench == AG_MULTIPLICITY)) || (w->kind && (cursed_known(w) || item_broken(w) || !w->ench || w->ench == WE_INSTABILITY)); }))) {   /* (0.52: both already good: hold it) */ do_item(s, ACN_READ); return true; }
  if ((s = G.hero.b.blind ? -1 : find_kind(IK_MASTERY)) >= 0 && h->lvl >= 6) { do_item(s, ACN_READ); return true; }
  if ((s = find_kind(IK_ARMOR_KIT)) >= 0 && h->armor.kind && !CLASS_ARMOR(h->armor.kind) && (getenv("OLDGEARFIX") || (!item_broken(&h->armor) && find_kind(IK_SCR_UPGRADE) < 0 && find_kind(IK_SCR_ENCHANT) < 0))) {   /* 0.52 (audit): the kit freezes the armour's DR as it is now (broken: +0 for good) */ do_item(s, ACN_APPLY); return true; }
  if (soures() && !G.hero.b.blind && find_kind(IK_SCR_UPGRADE) < 0 && (s = find_kind(IK_SCR_ENCHANT)) >= 0 && item_known(IK_SCR_ENCHANT))   // 0.93: no upgrade to mend with: an enchantment mends too (Item.fix), its old enchantment the price
    for (int w = -2; w <= -1; w++) { Item *it = game_slot(w); if (it->kind && it->lvl >= 3 && (it->flags & IF_LEVEL_KNOWN) && it->dur * 4 <= item_max_dur(it) && game_pick_ok(PICK_ENCHANT, w)) { s_force_pick = w; if (!s_rollout) DID("mend the %s with the enchantment", w == -1 ? "weapon" : "armour"); do_item(s, ACN_READ); return true; } }
  if ((s = find_kind(IK_WEIGHTSTONE)) >= 0 && h->weapon.kind && item_cat(h->weapon.kind) == CAT_WEAPON && !(h->weapon.flags & (IF_FAST | IF_ACCURATE)) && !(soures() && h->weapon.lvl >= 3 && (h->weapon.flags & IF_LEVEL_KNOWN) && h->weapon.dur * 4 > item_max_dur(&h->weapon))) {   /* 0.93: a weightstone mends the blade too (Weightstone.apply: fix): an upgraded one waits for that */ do_item(s, ACN_APPLY); return true; }
  if (!getenv("OLDHERO") && (s = G.hero.b.blind ? -1 : find_kind(IK_SCR_RECHARGING)) >= 0 && item_known(IK_SCR_RECHARGING)) {   // 0.52 (audit): wands low: recharge them
    int miss = 0; for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); if (item_cat(it->kind) == CAT_WAND && item_known(it->kind) && attack_wand(it->kind)) miss += wand_max(it) - it->ench; }
    if (miss >= 3 || (miss >= 1 && boss_depth(G.depth + 1))) { DID("read Recharging (wands low)"); do_item(s, ACN_READ); return true; } }
  if ((s = G.hero.b.blind ? -1 : find_kind(IK_SCR_REMOVE_CURSE)) >= 0 && item_known(IK_SCR_REMOVE_CURSE)) {
    bool cur = false; for (int k = -4; k < 0; k++) if (cursed_known(game_slot(k))) cur = true;
    if (cur) { do_item(s, ACN_READ); return true; }
  }
  if ((s = G.hero.b.blind ? -1 : find_kind(IK_SCR_IDENTIFY)) >= 0 && item_known(IK_SCR_IDENTIFY)) {
    bool any = false; for (int k = -4; k < BAG_SIZE; k++) { Item *it = game_slot(k); int c = item_cat(it->kind); if (it->kind && (c == CAT_RING || c == CAT_WAND || c == CAT_ARMOR || c == CAT_WEAPON) && !item_identified(it)) any = true; }
    if (any) { do_item(s, ACN_READ); return true; }
  }
  // bot3: better armour and weapons by the power model (5% better at least: switching costs turns)
  if (PRM[P_GEARMODEL] && (getenv("OLDGEARFIX") || !h->weakness)) {   // 0.52 (audit): weakened (-2 STR for a while): no swaps on that
    int unk = have_rc() ? 0 : -2;                       // 0.48: an unidentified piece counts as -2 without a Remove Curse at hand
    double base = gear_cost_b(&h->weapon, &h->armor, 0); int ba = -1, bw = -1; double ca = base, cw = base;
    for (int i = 0; i < BAG_SIZE; i++) { Item *it = &h->bag[i]; int c = item_cat(it->kind); if (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0)) continue;
      double need = item_str(it) > hero_str() && !getenv("ENC_EASY") ? (c == CAT_WEAPON && item_str(it) == hero_str() + 1 && !getenv("OLDWPN1") ? 0.9 : 0.7) : 0.95;   // 0.50 (review g6): a weapon one STR short costs little (a bit slower, a bit less sure): worth it for a real gain   // owner: almost never wear what you're too weak for: only for a massive gain
      bool unid = !(it->flags & IF_LEVEL_KNOWN) && !(it->flags & IF_CURSED_KNOWN) && !s_oldgear;   // 0.48: never tried on: the expected cost at the game's own odds (once its curse is known it counts as +0, as when worn: no swapping to and fro)
      if (unid && s_nounk && !(it->flags & IF_CURSED_KNOWN) && !have_rc()) continue;
      if (unid && getenv("GEARDBG2") && !s_rollout && (c == CAT_ARMOR || (c == CAT_WEAPON && it->kind != IK_PICKAXE))) { double ev = ev_cost(it, c, base), ch = c == CAT_ARMOR ? ({ Item w0 = believed(&h->weapon, 0); gear_cost(&w0, it); }) : ({ Item a0 = believed(&h->armor, 0); gear_cost(it, &a0); }); static int last[64]; int key = (i * 131 + it->kind) & 63; if (last[key] != it->kind * 100 + it->lvl + 50) { last[key] = it->kind * 100 + it->lvl + 50; printf("  GEAR d%d kind %d true %+d str %d/%d base %.1f ev %.1f (%s) cheat %.1f (%s)\n", G.depth, it->kind, it->lvl, item_str(it), hero_str(), base, ev, ev < base * 0.95 ? "EQUIP" : "no", ch, ch < base * 0.95 ? "EQUIP" : "no"); } }
      if (c == CAT_ARMOR && !cursed_known(&h->armor)) { Item bw2 = believed(&h->weapon, 0), bi = believed(it, unk); double v = unid ? unk_cost(it, c, base) : gear_cost(&bw2, &bi); if (!unid && getenv("ARMLOOK")) { int U = atoi(getenv("ARMLOOK")); Item wa = believed(&h->armor, 0), bu = bi; wa.lvl += U; bu.lvl += U; bu.flags |= IF_LEVEL_KNOWN; wa.flags |= IF_LEVEL_KNOWN; double bl = gear_cost(&bw2, &wa); if (bl > 0) v = gear_cost(&bw2, &bu) / bl * base; }   /* 0.93 test (DR is tier x (2 + level): the scrolls still to come are worth more on heavier armour): compare as if U more upgrades went into either */ if (getenv("NEEDDBG") && !s_rollout) { static int lk2 = -1; if (lk2 != it->kind * 64 + it->lvl + G.depth * 4096) { lk2 = it->kind * 64 + it->lvl + G.depth * 4096; { char n1[64], n2[64]; item_name(it, n1, sizeof n1); item_name(&h->armor, n2, sizeof n2); printf("NEEDDBG ARM [%s] vs worn [%s] ", n1, n2); } printf("d%d kind %d +%d str %d/%d unid %d ratio %.3f need %.2f | worn kind %d +%d str %d dr %d vs dr %d | base %.1f ca %.1f v %.1f\n", G.depth, it->kind, it->lvl, item_str(it), hero_str(), unid, v / ca, need, h->armor.kind, h->armor.lvl, item_str(&h->armor), armor_dr(&h->armor), armor_dr(&bi), base, ca, v); } } if (v < ca * need) { ca = v; ba = i; } }
      if (c == CAT_WEAPON && it->kind != IK_PICKAXE && !cursed_known(&h->weapon) && !(G.q.smith == QS_GIVEN && G.q.smith_alt && h->weapon.kind == IK_PICKAXE)) { Item bi = believed(it, unk), ba2 = believed(&h->armor, 0); double v = unid ? unk_cost(it, c, base) : gear_cost(&bi, &ba2); if (getenv("NEEDDBG") && !s_rollout) { static int lk = -1; if (lk != it->kind * 64 + it->lvl) { lk = it->kind * 64 + it->lvl; printf("NEEDDBG d%d kind %d +%d str %d/%d ratio %.3f need %.2f\n", G.depth, it->kind, it->lvl, item_str(it), hero_str(), v / cw, need); } } if (v < cw * need) { cw = v; bw = i; } } }
    if (ba >= 0) { st_uses[ACN_EQUIP]++; DID("wear the better armour (model)"); do_item(ba, ACN_EQUIP); return true; }
    if (bw >= 0 && ((h->hclass & 3) != 1 || !getenv("OLDGEARFIX"))) {   /* 0.52 (audit): the mage too (knuckles all game) */ st_uses[ACN_EQUIP]++; DID("wield the better weapon (model)"); do_item(bw, ACN_EQUIP); return true; }
  }
  if (PRM[P_GEARMODEL] && !getenv("OLDQBUG")) {   // 0.52 (audit): the blood quest's pickaxe in hand (it was inside the old model only: never ran)
    int pk = G.q.smith == QS_GIVEN && G.q.smith_alt && G.depth == G.q.smith_depth ? find_kind(IK_PICKAXE) : -1;
    if (pk >= 0 && pk < BAG_SIZE && !(G.hero.bag[pk].flags & IF_BLOODY) && !(G.hero.weapon.flags & IF_CURSED)) { DID("the pickaxe in hand, for the bat's blood"); do_item(pk, ACN_EQUIP); return true; }
    if (h->weapon.kind == IK_PICKAXE && ((h->weapon.flags & IF_BLOODY) || G.q.smith != QS_GIVEN || G.depth != G.q.smith_depth)) {
      int bw = -1; double bc = 1e18; Item ba = believed(&h->armor, 0);
      for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_WEAPON && h->bag[i].kind != IK_PICKAXE && !cursed_known(&h->bag[i])) { Item b = believed(&h->bag[i], 0); double c = gear_cost(&b, &ba); if (c < bc) { bc = c; bw = i; } }
      if (bw >= 0) { DID("the weapon back in hand"); do_item(bw, ACN_EQUIP); return true; } }
  }
  if (!PRM[P_GEARMODEL]) {
  int best = -1, bs = armor_score(&h->armor) + 1;
  for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_ARMOR && armor_score(&h->bag[i]) > bs) { bs = armor_score(&h->bag[i]); best = i; }
  if (best >= 0 && !cursed_known(&h->armor)) { st_uses[ACN_EQUIP]++; do_item(best, ACN_EQUIP); return true; }
  int pk = G.q.smith == QS_GIVEN && G.q.smith_alt && G.depth == G.q.smith_depth ? find_kind(IK_PICKAXE) : -1;   // the blood quest: the pickaxe in hand
  if (pk >= 0 && pk < BAG_SIZE && !(G.hero.bag[pk].flags & IF_BLOODY) && !(G.hero.weapon.flags & IF_CURSED)) { do_item(pk, ACN_EQUIP); return true; }
  if (h->weapon.kind == IK_PICKAXE && ((h->weapon.flags & IF_BLOODY) || G.q.smith != QS_GIVEN || G.depth != G.q.smith_depth)) {
    for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_WEAPON && h->bag[i].kind != IK_PICKAXE) { do_item(i, ACN_EQUIP); return true; }
  } else if (h->weapon.kind != IK_PICKAXE) {
    best = -1; bs = h->weapon.kind ? weapon_score(&h->weapon) + 2 : 0;
    if ((h->hclass & 3) == 1 && item_cat(h->weapon.kind) == CAT_WAND) bs += 4;
    for (int i = 0; i < BAG_SIZE; i++) if (weapon_score(&h->bag[i]) > bs) { bs = weapon_score(&h->bag[i]); best = i; }
    if (best >= 0 && !cursed_known(&h->weapon)) { st_uses[ACN_EQUIP]++; do_item(best, ACN_EQUIP); return true; }
  }
  }
  // rings: wear what isn't known to be bad
  for (int i = 0; i < BAG_SIZE; i++) {
    Item *r = &h->bag[i];
    if (item_cat(r->kind) != CAT_RING || cursed_known(r) || ((r->flags & IF_LEVEL_KNOWN) && r->lvl <= 0)) continue;
    if (!(r->flags & IF_CURSED_KNOWN) && !have_rc() && !s_oldgear && item_known(r->kind) && (r->kind == IK_RNG_HAGGLER || r->kind == IK_RNG_DETECTION || r->kind == IK_RNG_HERBALISM)) continue;
    // 0.48 (owner): an unknown ring (Ring.random): +1/+2 35% each, cursed -1/-2 15% each: on average +0.6 levels, so worth a
    // free hand; only kinds that hardly matter either way (haggling, detection, herbalism) aren't worth a stuck hand
    if (h->ring[0].kind && h->ring[1].kind) {
      int worse = (h->ring[0].flags & IF_LEVEL_KNOWN) && h->ring[0].lvl < known_lvl(r) && !(h->ring[0].flags & IF_CURSED) ? 0 : (h->ring[1].flags & IF_LEVEL_KNOWN) && h->ring[1].lvl < known_lvl(r) && !(h->ring[1].flags & IF_CURSED) ? 1 : -1;
      if (worse < 0) continue;
      { static int sw_floor = -1, sw_n; if (sw_floor != G.depth) { sw_floor = G.depth; sw_n = 0; } if (sw_n++ >= 2 && !getenv("OLDRINGSWAP")) continue; }   /* (0.53: a ring swapped to and fro for 20000 moves, d13) */
      do_item(-3 - worse, ACN_UNEQUIP); return true;
    }
    st_uses[ACN_EQUIP]++; do_item(i, ACN_EQUIP); return true;
  }
  // identify by use, when it's safe: full health, nothing in sight
  if ((h->HP * 100 >= h->HT * PRM[P_TESTHP] || (s_pztest && !getenv("NOHALFTEST") && h->HP * 2 >= h->HT)) && !G.hero.b.blind) {   // 0.92 (seed 84 d3: the ivory one was Healing, drunk at 27/30): a potion test from half health up (a Healing then isn't wasted; fire goes out in the water)
    for (int i = 0; i < BAG_SIZE + 32; i++) {
      int sl = i < BAG_SIZE ? i : PACK0 + i - BAG_SIZE; Item *it = game_slot(sl);
      if (!it->kind || item_known(it->kind)) continue;
      int c = item_cat(it->kind);
      if (c != CAT_POTION && h->HP * 100 < h->HT * PRM[P_TESTHP]) continue;   // (scrolls and the rest: at full health as before)
      if (c == CAT_POTION && s_pztest && !getenv("NOHALFTEST") && h->HP * 100 > h->HT * 85 && floor_left() && !pool_puzzle_pending() && !trap_puzzle_pending()) continue;   // (0.92: nearly whole with the floor still to do: the test can wait for a scratch)
      if (c == CAT_SCROLL && getenv("SCRDBG") && !s_rollout) { static int ls = -1; if (ls != s_bot_steps) { ls = s_bot_steps; printf("SCR step %d d%d fsteps %d bag %d left %d gw %d\n", s_bot_steps, G.depth, s_floor_steps, bag_used(), floor_left(), gear_worth_upgrading()); } }
      if (c == CAT_SCROLL && !getenv("OLDHERO") && s_floor_steps > 30 && bag_used() < BAG_SIZE - 2 && !cursed_known(&h->armor) && !cursed_known(&h->weapon) && (floor_left() || s_pztest) && !boss_depth(G.depth) && !(s_pztest && G.q.ghost_depth == G.depth && G.q.ghost_alt == 2 && G.q.ghost < QS_GIVEN && gear_worth_upgrading())) continue;   // (0.92: not with the ghost's curse to fight here: an Upgrade helps now)   // 0.52 (audit): an unknown scroll waits for the next floor's start (Mapping then saves the walking)
      if (c == CAT_SCROLL && s_scrhold && !gear_worth_upgrading() && bag_used() < BAG_SIZE - 3 && G.depth < (s_scrhold > 1 ? s_scrhold : 4) && !cursed_known(&h->armor) && !cursed_known(&h->weapon)) continue;   // 0.91 (owner): an Upgrade read now goes into the starting gear: hold them till something's worth it (or the Goo's near)
      if (c == CAT_SCROLL) { st_uses[ACN_READ]++; st_kinds[it->kind]++; do_item(sl, ACN_READ); return true; }
      if (c == CAT_POTION && getenv("HOLDPOT") && floor_left() && G.depth <= 2) continue;
      if (c == CAT_POTION && getenv("IDB") && h->HP * 100 > h->HT * 60 && idb_p(it->kind, IK_POT_HEALING) >= (getenv("IDBKEEP") ? atof(getenv("IDBKEEP")) : 0.35)) continue;   // 0.93 IDB (owner): probably Healing: not tested at full health (wasted), kept for when it's needed (and cheaper in the shop unknown)
      if (c == CAT_POTION && !getenv("OLDBOOKS") && pot_new(sl) && storage_barr() >= 0 && s_barr_tries < 3) continue;   // 0.58 (owner: "test potions of flame against books"): one found on a storage room's floor is thrown at its door, not drunk   /* 0.51: off by default: ab50 +0.107, ab50b +0.097 without the hold */
      if (c == CAT_POTION && G.hero.b.levitation > 0 && puzzle_pending()) continue;
      if (c == CAT_POTION && s_pztest && ((!item_known(IK_POT_LEVITATION) && trap_puzzle_pending() && trap_door_testable()) || (!item_known(IK_POT_INVIS) && pool_puzzle_pending()))) continue;   // 0.92 (seed 84 d2: the bistre one was invisibility, drunk across the floor from the pool with the vault's key in it): pools too   // 0.91 (owner): tested at the trap room's door (puzzle_task), where levitation flies straight in   // (levitating: to the prize first)   // (puzzles: tested in water once the floor is done; levitating then, it flies to the prize)   // 0.49 (owner): no tests till the floor is done and its puzzles tried (a levitation potion opens a trap room)
      bool tdep = (G.depth <= 2 && !s_pztest) || (!getenv("OLDHERO") && !floor_left() && enemy_count() == 0);   // (0.91 owner: "never drink unknown potions until the end of a stage")
      if (getenv("X_NOTEST") && boss_depth(G.depth) && ({ bool b = false; for (int j = 0; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && (MOB_DEFS[G.mobs[j].type].flags & MD_BOSS)) b = true; b; })) tdep = false;   // X: no drink-tests with the boss alive on its floor   // 0.52 (audit): Healing unknown at d3 in 38% of games: test on any floor once it's done
      if (c == CAT_POTION && tdep) { if (s_stash_drink != -2) { int w = s_stash_drink >= 0 && scrolls_loose() ? s_stash_drink : test_cell(); s_stash_drink = w; if (w >= 0 && stash_for(w)) return true; } }   // (the drinking spot stays put while the scrolls go down)
      if (c == CAT_POTION && tdep && test_spot()) { st_uses[ACN_DRINK]++; st_kinds[it->kind]++; do_item(sl, ACN_DRINK); return true; }   // bot2: drink-test on floors 1-2 only, nothing flammable near
    }
  }
  if (s_test_walk >= 0) { int w = s_test_walk; s_test_walk = -1; if (w != h->pos) { int32_t t0 = h->time; ActResult rr = tap_safe(w); if (rr != ACT_NONE || h->time != t0) { DID("to water, to test a potion"); walk(rr); return true; } s_black[w] = s_bot_steps + 300; } }   /* 0.56 (night lockout): water it couldn't walk to: aside */
  // a well of transmutation in sight: in goes an unwanted weapon or armour
  if (G.well_pos >= 0 && G.well_kind == 2 && G.map[G.well_pos] == T_WELL && BIT_GET(G.fov, G.well_pos) && distance(G.well_pos, h->pos) <= 4) {
    bool hit; if (ballistica(h->pos, G.well_pos, &hit) == G.well_pos && !hit)
      { int bi = -1; if (!getenv("OLDWELL")) for (int i = 0; i < BAG_SIZE && bi < 0; i++) { int k = h->bag[i].kind; if (k == IK_POT_STRENGTH && item_known(k)) bi = i; }   // 0.54 (audit): one use: strength becomes Might (+1 STR and +5 HP), enchantment becomes an upgrade
        if (!getenv("OLDWELL")) for (int i = 0; i < BAG_SIZE && bi < 0; i++) { int k = h->bag[i].kind; if (k == IK_SCR_ENCHANT && item_known(k)) bi = i; }
        if (bi >= 0) { st_wells++; st_uses[ACN_THROW]++; DID("into the well of transmutation: a %s", h->bag[bi].kind == IK_POT_STRENGTH ? "strength potion (Might back)" : "scroll of enchantment (an upgrade back)"); walk(game_throw(bi, G.well_pos)); return true; } }
      for (int i = 0; i < BAG_SIZE; i++) { Item *it = &h->bag[i]; int c = item_cat(it->kind);
        if (c == CAT_WEAPON && transmutes(it->kind) && weapon_score(it) < weapon_score(&h->weapon) && (getenv("OLDWELL") || !(it->flags & IF_CURSED))) {   // (WaterOfTransmutation changes only these weapons: the rest, armour too, the well spits back out)
          st_wells++; st_uses[ACN_THROW]++; walk(game_throw(i, G.well_pos)); return true; } }
  }
  // sungrass when hurt: plant it and stand on it; a honeypot's bee; Lloyd's beacon now and then
  if (!getenv("OLDSEED")) { if (sun_dance()) return true; } else
  if (h->HP < h->HT * 3 / 4 && (s = find_kind(IK_SEED_SUNGRASS)) >= 0 && plant_at(h->pos) < 0 && heap_at(h->pos) < 0 && PASSABLE(G.map[h->pos]) && h->pos != G.entrance && h->pos != G.exit) { st_uses[ACN_PLANT]++; do_item(s, ACN_PLANT); return true; }
  if (!getenv("OLDDUNG") && (s = find_kind(IK_BEACON)) >= 0 && !({ bool nb = false; for (int k = 0; k < 8; k++) if (char_at(h->pos + N8[k])) nb = true; nb; })) {   // 0.52 (audit): set it once, early (the way home); use it only with the Amulet
    Item *b = game_slot(s);
    if (b->lvl == 0 && !boss_depth(G.depth) && G.depth <= 6 && h->pos == G.entrance) { st_uses[ACN_SET]++; DID("set Lloyd's beacon here, for the way home"); do_item(s, ACN_SET); return true; }
    static int bt_depth = -1, bt_n; if (bt_depth != G.depth) { bt_depth = G.depth; bt_n = 0; }
    if (b->lvl > 0 && find_kind(IK_AMULET) >= 0 && !boss_depth(G.depth) && b->lvl < G.depth && bt_n++ < 2) {   /* (0.53: a boss floor's aura refuses it: 27000 tries on floor 5) */ st_uses[ACN_RETURN]++; s_arrival = true; DID("the Amulet: Lloyd's beacon home"); do_item(s, ACN_RETURN); s_arrival = false; return true; }
  } else
  if (getenv("OLDDUNG") && (s = find_kind(IK_BEACON)) >= 0 && rand() % 50 == 0 && !boss_depth(G.depth) && !({ bool nb = false; for (int k = 0; k < 8; k++) if (char_at(h->pos + N8[k])) nb = true; nb; })) {   // (not with a creature beside: the beacon refuses)
    Item *b = game_slot(s);
    if (b->lvl > 0 && rand() % 2) { st_uses[ACN_RETURN]++; s_arrival = true; do_item(s, ACN_RETURN); s_arrival = false; return true; }
    st_uses[ACN_SET]++; do_item(s, ACN_SET); return true;
  }
  // the dew vial when hurt
  if (h->HP < h->HT / 2 && G.dew >= 5 && (s = find_kind(IK_DEW_VIAL)) >= 0 && (getenv("OLDDEW") || (G.dew < 10 && (game_starving() || (s_pztest && G.depth >= 6))))) { do_item(s, ACN_DRINK); return true; }   // (0.92: deep, the vial's drops are worth more drunk than hoarded: it fills only at full health)   // 0.52 (audit): a full vial is a second life (it revives you at death): rest instead, unless starving
  // 0.48 (owner): leave junk behind: known cursed or negative pieces not worn, rings known bad, and weapons and armour
  // that even at +1 would be worse than what's worn (dropped here, never picked up again)
  if (!s_oldgear && !getenv("NOJUNK") && enemy_count() == 0 && (getenv("OLDGEARFIX") || !h->weakness) && (getenv("OLDJUNK2") || ({ int nb = 0; for (int j = 0; j < BAG_SIZE; j++) nb += h->bag[j].kind != 0; nb; }) >= BAG_SIZE - 4)) for (int i = 0; i < BAG_SIZE; i++) {   // 0.49 (owner): only when the bag fills (spares are things to throw onto traps; heavy gear may fit later)
    Item *it = &h->bag[i]; int c = item_cat(it->kind); bool junk = false;
    if (c == CAT_RING) junk = cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl <= 0);
    else if ((c == CAT_WEAPON && it->kind != IK_PICKAXE) || (c == CAT_ARMOR && !CLASS_ARMOR(it->kind))) {
      if (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0)) junk = true;
      else { Item b = believed(it, 1); double now = gear_cost_b(&h->weapon, &h->armor, 0), with = c == CAT_WEAPON ? ({ Item ba = believed(&h->armor, 0); gear_cost(&b, &ba); }) : ({ Item bw = believed(&h->weapon, 0); gear_cost(&bw, &b); });
        junk = with > now * 1.05; }
      if (junk && s_toe && toe_keep(i)) junk = false;   // 0.73 TOE: the plan grows into it, or the blacksmith wants its twin
    }
    if (junk && heap_at(h->pos) < 0 && h->pos != G.entrance && h->pos != G.exit) { BIT_SET(s_dropped, h->pos); st_uses[ACN_DROP]++; DID("leave the junk behind: %s", ({ static char jn[96]; item_name(it, jn, sizeof jn); jn; })); do_item(i, ACN_DROP); return true; }
  }
  // too full: drop the worst
  if (bag_used() >= BAG_SIZE - 1) {
    int worst = -1, ws = 1000;
    for (int i = 0; i < BAG_SIZE; i++) {
      Item *it = &h->bag[i]; int c = item_cat(it->kind), sc;
      if (!it->kind || it->kind == IK_AMULET || c == CAT_QUEST || it->kind == IK_ANKH || (it->kind >= IK_SEED_POUCH && it->kind <= IK_WAND_HOLSTER) || it->kind == IK_SEED_ROTBERRY) continue;
      sc = c == CAT_ARMOR ? armor_score(it) : c == CAT_WEAPON ? weapon_score(it) / 3 : c == CAT_MISSILE ? 20 : c == CAT_SEED ? 5 : c == CAT_MISC ? 30 : c == CAT_FOOD ? 60 : 40;
      if (c == CAT_ARMOR && armor_score(it) <= armor_score(&h->armor)) sc = -10;
      if (c == CAT_WEAPON && weapon_score(it) <= weapon_score(&h->weapon)) sc = -10;
      if (s_toe && (c == CAT_ARMOR || c == CAT_WEAPON) && toe_keep(i)) sc = 50;
      if (it->kind == IK_PICKAXE) sc = G.q.smith == QS_GIVEN ? 1000 : -20;   // (the blacksmith wants it back)
      if (it->kind == IK_DARK_GOLD && G.q.smith == QS_GIVEN) sc = 1000;
      if (s_pztest && !getenv("OLDBAGVAL")) sc = item_value(it);
      if (sc < ws) { ws = sc; worst = i; }
    }
    if (worst >= 0) { BIT_SET(s_dropped, h->pos); st_uses[ACN_DROP]++; if (s_pztest && !s_rollout) DID("bag full: leave the least useful behind: %s", ({ static char jn[96]; item_name(&h->bag[worst], jn, sizeof jn); jn; })); do_item(worst, ACN_DROP); return true; }
  }
  return false;
}
static bool goo_alive(void) { return G.max_depth <= 5; }
static bool s_losing;
static bool boss_reserve(int k) {                       // bot3 (owner): early fire, poison and paralysis are saved for Goo
  if (!goo_alive() || s_losing) return false;   /* 0.51: not in a losing fight (g107 died with 14 curare darts) */
  return k == IK_POT_FLAME || k == IK_POT_PARALYTIC || k == IK_INCENDIARY_DART || k == IK_CURARE_DART || k == IK_BOMB || k == IK_SEED_ICECAP || k == IK_SEED_SORROWMOSS;
}
static int ranged_try(int mi) {                         // something to throw or zap at mob mi from afar; 1 done
  Mob *m = &G.mobs[mi]; Hero *h = &G.hero;
  bool hit; int16_t end = ballistica(h->pos, m->pos, &hit);
  if (end != m->pos) return 0;
  int d = distance(h->pos, m->pos);
  for (int s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) {   // wands (the holster too), the mage's in hand
    Item *it = game_slot(s);
    if (item_cat(it->kind) != CAT_WAND || it->ench == 0) continue;   // bot2: no charges, no zap (the bot may read the charge)
    if (item_known(it->kind) && !attack_wand(it->kind) && !(it->kind == IK_WND_TELEPORT && h->HP < h->HT / 3)) continue;
    if (s == -1 && it->ench == 0) continue;
    if (s_firedoom && it->kind == IK_WND_FIREBOLT && item_known(it->kind) && ((MOB_DEFS[m->type].flags & MD_I_FIREBOLT) || ({ bool gr = TFLAGS(G.map[h->pos]) & F_FLAMABLE; for (int k = 0; k < 8 && !gr; k++) { int q = h->pos + N8[k]; if (nb_ok(h->pos, q) && (TFLAGS(G.map[q]) & F_FLAMABLE)) gr = true; } gr; }))) continue;   // 0.92 (s1 g18 burned): its bolt lights the grass on the way, with me in it
    st_uses[ACN_ZAP]++; st_kinds[it->kind]++;
    int32_t t = h->time; if (V) printf("  zap slot %d kind %d at %d\n", s, it->kind, m->pos); ActResult r = game_zap(s, m->pos); if (r == ACT_TURN && h->time <= t && h->HP > 0) viol("NO_TIME", "zap of kind %d took no time", it->kind);
    walk(r); return 1;
  }
  if (d < 2) return 0;
  if (m->type == MT_TENGU && !getenv("OLDTENGU2")) return 0;   // 0.54 (audit): a throw at Tengu is a step not taken: every idle turn is a blow lost before it leaps again
  for (int s = 0; s < BAG_SIZE; s++) {
    Item *it = &h->bag[s]; int c = item_cat(it->kind);
    bool ok = c == CAT_MISSILE || (c == CAT_POTION && item_known(it->kind) && harmful_potion(it->kind) && d >= 3) || (it->kind == IK_BOMB && d >= 3) || (it->kind == IK_SEED_FIREBLOOM && d >= 2 && item_known(it->kind));
    if (!ok) continue;
    if (boss_reserve(it->kind) && !(MOB_DEFS[m->type].flags & MD_BOSS) && h->HP * 10 > h->HT * 3) continue;   // bot3: saved for Goo
    st_uses[ACN_THROW]++; st_kinds[it->kind]++;
    if (V) printf("  throw slot %d kind %d at %d\n", s, it->kind, m->pos); walk(game_throw(s, m->pos)); return 1;
  }
  return 0;
}
// 0.52e (class audit): the class armour's special (game_aim 2): costs a third of current HP (cheap when low).
// Rogue: smoke bomb (blinds all in sight, teleports you to the cell): the best reusable escape. Warrior: heroic leap
// (paralyses the 8 round where you land). Mage: molten earth (all in sight burn, rooted). Huntress: spectral blades (a
// free blow on everything in sight). They hit NPCs and allies too: not with one in sight.
static int st_special;
static bool special_ok(void) {
  Hero *h = &G.hero; if (getenv("OLDSPECIAL") || !CLASS_ARMOR(h->armor.kind) || h->HP < 3) return false;
  for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && BIT_GET(G.fov, o->pos) && (ally(i) || (MOB_DEFS[o->type].flags & MD_NPC) || o->state == ST_PASSIVE || o->type == MT_YOG)) return false; }
  return true;
}
static bool class_special(bool escape) {
  Hero *h = &G.hero; if (!special_ok()) return false; int cls = h->hclass & 3, n = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && o->state == ST_HUNTING && BIT_GET(G.fov, o->pos)) n++; }
  if (!n) return false;
  if (escape && (cls == 2 || cls == 0)) {               // a cell far from them, in my line of sight
    int best = -1, bs = -1;
    for (int c = 0; c < MAP_LEN; c++) { if (!BIT_GET(G.fov, c) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || char_at(c)) continue;
      bool hit; if (cls == 0 && ballistica(h->pos, c, &hit) != c) continue;
      int md = 99; bool seen = false; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP <= 0 || ally(i) || o->state != ST_HUNTING) continue; int d = distance(o->pos, c); if (d < md) md = d; if (ballistica(o->pos, c, &hit) == c) seen = true; }
      int sc = md * 10 + (seen ? 0 : 30); if (cls == 0 && md < 3) continue; if (sc > bs) { bs = sc; best = c; } }
    if (best >= 0) { st_special++; s_did[0] = 0; DID("%s to escape", cls == 2 ? "smoke bomb" : "heroic leap"); walk(game_aim(2, 0, best)); return true; }
    return false; }
  if (!escape && n >= 2 && h->HP * 10 >= h->HT * 4) {   // a crowd
    if (cls == 1 && fire_safe(h->pos)) { st_special++; s_did[0] = 0; DID("molten earth on the crowd"); walk(game_aim(2, 0, h->pos)); return true; }
    if (cls == 3 && n >= 3) { st_special++; s_did[0] = 0; DID("spectral blades on the crowd"); walk(game_aim(2, 0, h->pos)); return true; } }
  return false;
}
static bool magic_shooter(int t); static bool losing_fight(int mi);
static bool calc_on(void); static bool fair_on(void); static int foe_sees(int i); static void round_dmg(double *E, double *V);
static bool emergency(void) {                           // badly hurt
  Hero *h = &G.hero; int s;
  { bool boss = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[i].pos)) boss = true;
    int dot = 0; for (int k = 1; k <= h->b.poison; k++) dot += k / 3 + 1; dot += 3 * h->b.bleed;
    if (!getenv("OLDHERO") && dot >= h->HP && (s = find_kind(IK_POT_HEALING)) >= 0 && item_known(IK_POT_HEALING)) { st_uses[ACN_DRINK]++; do_item(s, ACN_DRINK); return true; }   // 0.52 (audit): the poison still to come would kill
  if (calc_on() && getenv("EMERG4") && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) { double E, V; round_dmg(&E, &V); double k = getenv("EMERGK") ? atof(getenv("EMERGK")) : 2.5;   // 0.93 review: drink when the next round could kill (Healing fills me up: as late as is safe)
    if ((h->HP - G.deferred) > E + k * sqrt(V) + 1 && h->HP * 100 >= h->HT * 12) return false; } else
  if ((h->HP - (getenv("OLDGLYPH") ? 0 : G.deferred)) * 100 >= h->HT * (boss && getenv("BOSS_EMERG") ? atoi(getenv("BOSS_EMERG")) : PRM[P_EMERG])) return false; }   /* (0.52: Viscosity's deferred damage is still coming) */
  if (s_pztest && !(calc_on() && getenv("EMERG4") && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) && !getenv("OLDEMERG2") && h->HP * 5 >= h->HT && !h->b.poison && !h->b.bleed && !({ bool t = false; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && o->state == ST_HUNTING && BIT_GET(G.fov, o->pos) && (distance(o->pos, h->pos) <= 2 || magic_shooter(o->type) || o->type == MT_SCORPIO)) t = true; } t; })) return false;   // 0.93 (DM lab: Healings are what the bosses take): under a third but nothing on me: no potion yet (rest, walk on)
  if (s_pztest && !(calc_on() && getenv("EMERG4") && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) && !getenv("OLDEMERG3") && (h->hclass & 3) != 1 && h->HP * 100 >= h->HT * 15 && !h->b.poison && !h->b.bleed && !({ bool bs = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[i].pos)) bs = true; bs; })) { int mi = -1, md = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && o->state == ST_HUNTING && BIT_GET(G.fov, o->pos) && distance(o->pos, h->pos) < md) { md = distance(o->pos, h->pos); mi = i; } } if (mi >= 0 && !losing_fight(mi) && h->HP > 2 * MOB_DEFS[G.mobs[mi].type].dmg_hi) return false; }   /* (a margin: two of its best blows) */   // 0.93: the fight's sum says I win it with HP to spare: no Healing for it (the bosses need them)
  if ((s = find_kind(IK_POT_HEALING)) >= 0 && item_known(IK_POT_HEALING)) { st_uses[ACN_DRINK]++; do_item(s, ACN_DRINK); return true; }
  if (s_pztest && !item_known(IK_POT_HEALING)) for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_POTION && !item_known(h->bag[i].kind) && s_pot_guess[h->bag[i].kind] == IK_POT_HEALING) { st_uses[ACN_DRINK]++; if (!s_rollout) DID("the potion brewed from Sungrass: Healing, 2 times in 3"); do_item(i, ACN_DRINK); return true; }
  if (getenv("IDB") && !item_known(IK_POT_HEALING)) { int bi = -1; double bp = getenv("IDBEMERG") ? atof(getenv("IDBEMERG")) : 0.45;   // 0.93 IDB (owner): the colour that's most likely Healing (and not likely a gas or a fire on myself)
    for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) != CAT_POTION || item_known(k)) continue; double ph = idb_p(k, IK_POT_HEALING), bad = idb_p(k, IK_POT_PARALYTIC) + idb_p(k, IK_POT_TOXIC) + idb_p(k, IK_POT_FLAME);
      if (ph >= bp && bad <= 0.25) { bp = ph; bi = i; } }
    if (bi >= 0) { st_uses[ACN_DRINK]++; if (!s_rollout) DID("probably Healing (%.0f%%): drink it", bp * 100); do_item(bi, ACN_DRINK); return true; } }
  if (G.dew >= 3 && (s = find_kind(IK_DEW_VIAL)) >= 0 && (getenv("OLDDEW") || G.dew < 10)) { do_item(s, ACN_DRINK); return true; }   // (0.52: at 10 it fires by itself at death)
  int adj = 0, adjm = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && adjacent(G.mobs[i].pos, h->pos) && (!fair_on() || foe_sees(i)) && G.mobs[i].state != ST_PASSIVE && (getenv("OLDEMERG") || G.mobs[i].state == ST_HUNTING)) { adj++; adjm = i; }   // 0.52 (audit): lulled, terrified, asleep: not a threat now (it spent the next escape item)
  if (!adj) return false;
  if (dm_hold()) return false;   // 0.54 (audit): no leap, smoke, terror, invisibility: they all make DM-300 walk over the dead traps
  if (class_special(true)) return true;
  if (!getenv("OLDEMERG")) for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl);   // a Wand of Teleportation at the one beside you: reusable, sure
    if (it->kind == IK_WND_TELEPORT && item_known(IK_WND_TELEPORT) && it->ench > 0 && !(MOB_DEFS[G.mobs[adjm].type].flags & MD_BOSS)) { walk(game_zap(sl, G.mobs[adjm].pos)); return true; } }
  if ((s = find_kind(IK_WIPE_OUT)) >= 0 && !G.hero.b.blind && (getenv("OLDALTAR2") || G.depth >= 22 || G.hero.HP * 4 < G.hero.HT)) { st_wipe++; do_item(s, ACN_READ); return true; }   // 0.51: the altar's gift: every lesser creature here dies
  static const uint8_t ESC[] = { IK_SCR_TELEPORT, IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_MIRROR, IK_POT_INVIS };
  for (unsigned k = 0; k < sizeof ESC; k++) if ((s = find_kind(ESC[k])) >= 0 && item_known(ESC[k]) && !(G.hero.b.blind && item_cat(ESC[k]) == CAT_SCROLL) && !(calc_on() && ((ESC[k] == IK_SCR_TERROR && (MOB_DEFS[G.mobs[adjm].type].flags & MD_I_TERROR)) || (ESC[k] == IK_SCR_LULLABY && (MOB_DEFS[G.mobs[adjm].type].flags & MD_I_SLEEP))))) {   /* (0.93 review: not on a monk or golem: immune) */ do_item(s, item_cat(ESC[k]) == CAT_SCROLL ? ACN_READ : ACN_DRINK); return true; }
  if (getenv("LASTSTAND")) {                            // 0.73 X: dying with the kit unspent: every charge, scroll and missile at it first
    Mob *am = &G.mobs[adjm]; bool boss = MOB_DEFS[am->type].flags & MD_BOSS;
    for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); int k = it->kind;
      if (item_cat(k) != CAT_WAND || it->ench <= 0) continue;
      if (item_known(k) && (k == IK_WND_BLINK || k == IK_WND_REGROWTH || k == IK_WND_REACH || (boss && (k == IK_WND_TELEPORT || k == IK_WND_AMOK)))) continue;
      s_did[0] = 0; DID("last stand: zap it"); walk(game_zap(sl, am->pos)); return true; }
    if (!h->b.blind) for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) == CAT_SCROLL && !item_known(k)) { s_did[0] = 0; DID("last stand: an unknown scroll"); do_item(i, ACN_READ); return true; } }
    for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) == CAT_MISSILE || k == IK_BOMB) { s_did[0] = 0; DID("last stand: throw"); walk(game_throw(i, am->pos)); return true; } }
  }
  { int bi = -1, bq = 0;                               // a gamble: an unknown potion, the likeliest healing: 0.73 (GAMBLE2=1): the
    for (int i = 0; i < BAG_SIZE; i++) {               // biggest stack (Healing is the commonest colour by far, 45 of 201)
      int k = h->bag[i].kind; if (item_cat(k) == CAT_POTION && !item_known(k)) { int q = getenv("GAMBLE2") ? h->bag[i].qty : 1; if (q > bq) { bq = q; bi = i; } } }
    if (bi >= 0) { do_item(bi, ACN_DRINK); return true; } }
  return false;
}
// ---- 0.51 (owner): "something other than whack, whack, dead": the duel sum, and the kit when it's a losing one
static double phit(double a, double d) { if (d <= 0) return 1; if (a <= 0) return 0; return a >= d ? 1 - d / (2 * a) : a / (2 * d); }
static double hero_def_est(void) {                   // Hero.defenseSkill (game.c: static there): armour weight, the rogue's light armour, Ring of Evasion
  if (getenv("OLDDUEL2")) { double d = G.hero.defense_skill; if (G.hero.armor.kind) { int e = item_str(&G.hero.armor) - hero_str(); for (int i = 0; i < e && i < 8; i++) d = d * 2 / 3; } return d; }
  int e = G.hero.armor.kind ? item_str(&G.hero.armor) - hero_str() : 0, ev = (G.hero.hclass == 2 && e <= 0) ? -e : 0;
  double d = scale_pow(G.hero.defense_skill + ev, 12, 10, ring_sum(IK_RNG_EVASION));
  if (G.hero.b.paralysis) d /= 2; for (int i = 0; i < e && i < 8; i++) d = d * 2 / 3; return d; }
static double dr_left(double dm, int dr) { if (getenv("OLDDUEL2")) { dm -= dr / 2.0; return dm < 0.3 ? 0.3 : dm; } double t = 0; for (int r = 0; r <= dr; r++) t += dm > r ? dm - r : 0; t /= dr + 1; return t < 0.3 ? 0.3 : t; }   // E[max(0, dmg - U(0..DR))]
static int s_fair = -1; static bool fair_on(void) { if (s_fair < 0) s_fair = getenv("FAIR") != NULL; return s_fair; }   // 0.93 review (owner: no hidden information): every peek closed
static int s_calc = -1; static bool calc_on(void) { if (s_calc < 0) s_calc = getenv("CALC") != NULL; return s_calc; }   // 0.93 review: the guesses replaced by the source's numbers
static double dmg_vs_dr(int lo, int hi, int dr) {          // E[max(0, D - A)]: D = NormalIntRange(lo, hi) (two uniforms), A = U(0..dr) (Char.damage, game.c)
  if (hi < lo) hi = lo; if (dr < 0) dr = 0; int n = hi - lo + 1; double t = 0;
  for (int j = 0; j < n; j++) { double a = 2.0 * j / n, b = 2.0 * (j + 1) / n, Fa = a <= 1 ? a * a / 2 : 1 - (2 - a) * (2 - a) / 2, Fb = b <= 1 ? b * b / 2 : 1 - (2 - b) * (2 - b) / 2, pj = Fb - Fa, e = 0;
    for (int r = 0; r <= dr; r++) e += lo + j > r ? lo + j - r : 0; t += pj * e / (dr + 1); }
  return t; }
static void dmg_moments(int lo, int hi, int dr, double *m1, double *m2) {   // E[X], E[X^2] of max(0, D - A) (dr < 0: no armour)
  if (hi < lo) hi = lo; int n = hi - lo + 1, R = dr < 0 ? 0 : dr; double t1 = 0, t2 = 0;
  for (int j = 0; j < n; j++) { double a = 2.0 * j / n, b = 2.0 * (j + 1) / n, Fa = a <= 1 ? a * a / 2 : 1 - (2 - a) * (2 - a) / 2, Fb = b <= 1 ? b * b / 2 : 1 - (2 - b) * (2 - b) / 2, pj = Fb - Fa;
    for (int r = 0; r <= R; r++) { double x = lo + j - (dr < 0 ? 0 : r); if (x < 0) x = 0; t1 += pj * x / (R + 1); t2 += pj * x * x / (R + 1); } }
  *m1 = t1; *m2 = t2; }
static bool magic_shooter(int t) { return t == MT_SHAMAN || t == MT_WARLOCK || t == MT_EYE || t == MT_BURNING_FIST; }   // char_hit(.., magic): their roll doubled, armour ignored
static int mst(Mob *m, int w, int d) { int v = g_mh.stats ? g_mh.stats((int)(m - G.mobs), w) : -1; return v >= 0 ? v : d; }   // the creature overlay's own numbers (bats, Goo...)
static double hero_dpt(Mob *m) {                       // the hero's mean damage to m per turn, melee
  const Item *w = G.hero.weapon.kind ? &G.hero.weapon : NULL; double dm;
  if (!w) { int st = hero_str(); dm = st > 10 ? (1 + st - 9) / 2.0 : 1; }
  else { dm = (weapon_min(w) + weapon_max(w)) / 2.0; int ex = hero_str() - item_str(w); if (ex > 0 && G.hero.hclass != 3) dm += ex / 2.0; }   // (a huntress's spare STR goes to her missiles, not her blade)
  if (calc_on() && !getenv("CALCNOFOE") && w && !g_mh.stats) { int ex = hero_str() - item_str(w); dm = dmg_vs_dr(weapon_min(w), weapon_max(w), mst(m, STAT_DR, MOB_DEFS[m->type].dr)) + (ex > 0 && G.hero.hclass != 3 ? ex / 2.0 : 0); } else
  dm = dr_left(dm, mst(m, STAT_DR, MOB_DEFS[m->type].dr));
  double p = (m->b.paralysis || m->b.frost) ? 1 : phit(hero_attack_skill(NULL, m->pos), mst(m, STAT_DEF, MOB_DEFS[m->type].def));
  double r = p * dm * TICK / (double)attack_delay(w);
  if (calc_on() && !getenv("CALCNOFOE") && (m->type == MT_MONK || m->type == MT_SENIOR) && w && w->kind != IK_KNUCKLES && !(w->flags & IF_CURSED)) {   // mob_monk.inc: 1 in 6 of its hits knocks the weapon away: 2 turns to pick it up and wield it
    const MobDef *md = &MOB_DEFS[m->type]; double lam = phit(md->att, hero_def_est()) * TICK / (double)(md->atk_delay ? md->atk_delay : TICK) / 6; r /= 1 + 2 * lam; }
  return r;
}
// 0.54 (owner: "the Huntress needs massively different tactics: her boomerang"; audit): it comes back the same turn,
// her spare STR adds 0..STR-10 to it, a Sniper's throws ignore armour and a hit marks for a half-turn follow-up
static int st_boom;
static bool dm_plan(void) { return s_pztest && !getenv("NODMPLAN"); }
static bool dm_hold(void) { if ((!getenv("DMHOLD2") && !(dm_plan() && getenv("DMHOLDPLAN"))) || G.depth != 15) return false;   /* (r17: more gas deaths, no fewer DM deaths: opt-in) */ for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_DM300 && G.mobs[i].HP < G.mobs[i].HT) return true; return false; }   // 0.54 (audit): DM-300 hurt: it mends only by stepping onto a dead trap: never make it walk
static bool hunt2(void) { return (G.hero.hclass & 3) == 3 && !getenv("OLDHUNT2"); }
static int boom_slot(void) { if (G.hero.weapon.kind == IK_BOOMERANG) return -1; for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind == IK_BOOMERANG) return i; return -100; }
static double boom_dpt_vs(int def, int dr, int extra, bool near) {   // her boomerang's mean damage a turn against def / dr
  int bs = boom_slot(); if (bs == -100) return 0; Item *b = game_slot(bs); int L = (item_broken(b) ? 0 : (b->lvl > 0 ? b->lvl : 0)) + extra, st = hero_str();
  double bd = 2.5 + 1.5 * L + (st > 10 ? (st - 10) / 2.0 : 0); bool sn = G.hero.subclass == SUB_SNIPER;
  double e = sn ? bd : bd - dr / 2.0; if (e < 0.5) e = 0.5;
  double acc = G.hero.attack_skill * (near ? 0.5 : 1.0), p = acc <= 0 ? 0 : def <= 0 ? 1 : acc >= def ? 1 - def / (2 * acc) : acc / (2 * def);
  return p * e / (sn ? 1 - 0.5 * p : 1);
}
static int foe_sees(int i) { return BIT_GET(G.fov, G.mobs[i].pos); }
static void round_dmg(double *E, double *V) {             // 0.93 review: what could land before my next turn: each foe that can strike now, each of its attacks a hit-or-miss draw
  Hero *h = &G.hero; double e = 0, v = 0, def = hero_def_est();
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || !foe_sees(i) || m->b.paralysis || m->b.frost) continue;
    const MobDef *d = &MOB_DEFS[m->type]; bool adj = adjacent(m->pos, h->pos), mag = magic_shooter(m->type) && !adj;
    if (!adj && !mag && m->type != MT_SCORPIO) continue;
    int lo = d->dmg_lo, hi = d->dmg_hi; if (mag && m->type == MT_WARLOCK) { lo = 12; hi = 17; }
    double m1, m2; dmg_moments(lo, hi, mag ? -1 : hero_dr(), &m1, &m2);
    double p = phit(mst(m, STAT_ATT, d->att) * (mag ? 2 : 1), def); int k = d->atk_delay && d->atk_delay < TICK ? TICK / d->atk_delay : 1;
    for (int j = 0; j < k; j++) { e += p * m1; v += p * m2 - p * p * m1 * m1; } }
  if (h->b.burning) { e += 2.5; v += 1.25; }
  for (int k = 1; k <= h->b.poison && k <= 1; k++) e += h->b.poison / 3 + 1;
  if (h->b.bleed) e += h->b.bleed;
  *E = e; *V = v; }
static double foe_dpt(Mob *m) {                        // m's mean damage to the hero per turn
  if (m->b.paralysis || m->b.frost) return 0;
  const MobDef *d = &MOB_DEFS[m->type]; double dm = (d->dmg_lo + d->dmg_hi) / 2.0;
  if (!getenv("OLDDUEL2") && g_mh.stats) { uint32_t r0 = g_rng; double t = 0; int n = 0; for (int k = 0; k < 16; k++) { int v = g_mh.stats((int)(m - G.mobs), STAT_DMG); if (v < 0) break; t += v; n++; } g_rng = r0; if (n) dm = t / n; }   // the overlay's own roll (brute, Goo pumped, mimic, piranha...), averaged; the dice put back
  bool mag = !getenv("OLDDUEL2") && magic_shooter(m->type) && distance(m->pos, G.hero.pos) > 1;
  dm = mag ? (dm < 0.3 ? 0.3 : dm) : dr_left(dm, hero_dr());
  if (calc_on() && !getenv("CALCNOFOE")) {
    if (!mag && !g_mh.stats) dm = dmg_vs_dr(d->dmg_lo, d->dmg_hi, hero_dr());
    if (mag && m->type == MT_WARLOCK) dm = 14.5;   // city.c warlock_zap: 12..17, armour ignored
    if (dm < 0.3) dm = 0.3; }
  if (m->type == MT_SPINNER && s_pztest && getenv("SPINNER2")) dm += 6;   // 0.93 (poison: 11% of good-seed deaths, floors 12-14): every other bite poisons for 7-9 turns (~17 HP; a new one resets it): ~6 a hit on top, armour or not
  double a = mst(m, STAT_ATT, d->att) * (mag ? 2 : 1);
  double hp = phit(a, hero_def_est()), rate = TICK / (double)(d->atk_delay ? d->atk_delay : TICK), r = hp * dm * rate;
  if (calc_on() && !getenv("CALCNOFOE") && m->type == MT_ELEMENTAL && !IS_WATER(G.map[G.hero.pos])) r += 2.5 * (1 - exp(-hp * 0.5 * rate * 6));   // city.c: half its hits set you alight (1-4 a turn, ~6 turns; water puts it out)
  return r;
}
static double s_lose_cost;                              // the HP the fight's sum says it costs (vs the HP you have)
static bool losing_fight(int mi) {                      // the sum: its HP over my damage, times what all of them that hunt me deal
  if (getenv("OLDKIT")) return false;
  Mob *m = &G.mobs[mi]; Hero *h = &G.hero;
  #define IN_FIGHT(i, o) ((o)->HP > 0 && !ally(i) && (o)->state == ST_HUNTING && (getenv("OLDDUEL2") ? distance((o)->pos, h->pos) <= 3 : ((BIT_GET(G.fov, (o)->pos) || adjacent((o)->pos, h->pos)) && distance((o)->pos, h->pos) <= (magic_shooter((o)->type) || (o)->type == MT_SCORPIO ? 8 : 3))))
  double in = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (IN_FIGHT(i, o)) in += foe_dpt(o); }
  if (in <= 0) in = foe_dpt(m);
  double hd = hero_dpt(m); if (m->type == MT_BAT && !getenv("OLDDUEL2")) { hd -= foe_dpt(m); if (hd < 0.3) hd = 0.3; }   // (a vampire bat heals what it bites)
  if (hunt2() && boom_slot() >= 0) { double nb = boom_dpt_vs(mst(m, STAT_DEF, MOB_DEFS[m->type].def), mst(m, STAT_DR, MOB_DEFS[m->type].dr), 0, adjacent(m->pos, h->pos)); if (nb > hd) hd = nb; }   // 0.54 (audit): her boomerang counts
  double hp_left = m->HP; if (hunt2() && boom_slot() >= 0 && !adjacent(m->pos, h->pos)) { int d = distance(m->pos, h->pos) - 1; hp_left -= d * boom_dpt_vs(mst(m, STAT_DEF, MOB_DEFS[m->type].def), mst(m, STAT_DR, MOB_DEFS[m->type].dr), 0, false); if (hp_left < 0) hp_left = 0; }   // (free throws while it comes)
  double turns = hp_left / hd;                   // (the others after it: counted once more each)
  for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (i != mi && IN_FIGHT(i, o)) turns += o->HP / hero_dpt(o); }
  s_lose_cost = turns * in;
  if (s_pztest && !getenv("NOBRUTEEST") && (m->type == MT_BRUTE || m->type == MT_SHIELDED) && m->HP >= m->HT / 4) { int q = m->HT / 4; s_lose_cost += (q < m->HP ? q : m->HP) / hd * foe_dpt(m) * 0.9; }   // 0.92 (gnoll brutes: the top killer on floors 11-14): its last quarter it fights enraged, 10-40 a blow
  if (getenv("KITDBG")) printf("  duel s%d: %s hp %d: my dpt %.2f (att %d vs def %d, dly %d), its dpt %.2f (att %d vs my def %.1f, my dr %d, its dr %d, wpn %d-%d), turns %.1f cost %.1f of %d\n", s_bot_steps, mob_name(m->type), m->HP, hero_dpt(m), hero_attack_skill(NULL, m->pos), mst(m, STAT_DEF, MOB_DEFS[m->type].def), attack_delay(G.hero.weapon.kind ? &G.hero.weapon : NULL), in, mst(m, STAT_ATT, MOB_DEFS[m->type].att), hero_def_est(), hero_dr(), mst(m, STAT_DR, MOB_DEFS[m->type].dr), G.hero.weapon.kind ? weapon_min(&G.hero.weapon) : 0, G.hero.weapon.kind ? weapon_max(&G.hero.weapon) : 0, turns, s_lose_cost, h->HP);
  return s_lose_cost >= (h->HP - (getenv("OLDGLYPH") ? 0 : G.deferred)) * 0.6;                    // it would leave you under 40% of what you have now (dice run hot: margin)
}
// 0.51 (owner): the sacrificial altar. A creature standing in its fire is marked (5 turns); one killed marked feeds the
// fire its exp (x1-3, only if you're not above its level: mobs.c sacrifice). Fed 5+5*depth, it gives a Scroll of Wipe Out:
// every lesser creature on the floor dies. So: stand beside the altar on the far side from the one coming, so its way to
// you is over the altar; once it's in the fire (or marked), kill it.
static int s_altar_uid = -1, s_altar_moves; static int st_altar_moves, st_altar_done;
static bool altar_live(void) { return G.altar_pos >= 0 && G.lvars[0] > 1 && BIT_GET(G.visited, G.altar_pos); }
static bool altar_lure(int mi) {
  if (getenv("OLDALTAR") || !altar_live()) return false;
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; int a = G.altar_pos;
  if (m->state != ST_HUNTING || (MOB_DEFS[m->type].flags & (MD_BOSS | MD_WATER)) || h->lvl > MOB_DEFS[m->type].max_lvl || !MOB_DEFS[m->type].exp) return false;
  if (!getenv("OLDALTAR3") && (magic_shooter(m->type) || m->type == MT_SCORPIO || m->type == MT_ACIDIC || m->type == MT_TENGU)) return false;   // 0.93 (zone run: waited by the altar while a warlock shot from across the room, two Healings drunk): a shooter never comes over it
  if (m->pos == a || m->b.mind > 0) return false;      // in the fire or marked: kill it now
  bool far = !getenv("OLDALTAR2");                    // 0.54 (owner: "it can take an entire stage 24 out at once; careful kiting of non-fast mobs; attempt it early in a stage with lots of mobs left")
  int live = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && !(MOB_DEFS[o->type].flags & (MD_BOSS | MD_NPC | MD_WATER)) && o->state != ST_PASSIVE && MOB_DEFS[o->type].exp) live++; }
  bool fast = MOB_DEFS[m->type].move_cost * 2 <= hero_move_cost();
  int reach = far && !fast && live >= 3 && G.depth >= 4 && h->HP * 10 >= h->HT * 7 ? 24 : 6;   // (not on the first floors: food is too tight there)
  if (h->HP * 2 < h->HT || h->b.roots) return false;
  if (distance(h->pos, a) > reach) return false;
  if (reach > 6) { bfs(h->pos, false); if (b_dist[a] > reach) return false; }
  if (m->uid != s_altar_uid) { s_altar_uid = m->uid; s_altar_moves = 0; }
  if (s_altar_moves >= (reach > 6 ? 30 : 8) || (char_at(a) && a != h->pos)) return false;
  bool adj = adjacent(m->pos, h->pos); int best = -1, bs = -1000;
  if (far && !adj) {                                   // (audit) the creature steps straight before diagonal: stand right across the altar from where its way comes in
    int e = -1, ed = 99; for (int k = 0; k < 8; k++) { int c = a + N8[k]; if (nb_ok(a, c) && PASSABLE(G.map[c]) && distance(c, m->pos) < ed) { ed = distance(c, m->pos); e = c; } }
    if (e >= 0) { int o = a + (a - e); if (nb_ok(a, o) && PASSABLE(G.map[o]) && !(TFLAGS(G.map[o]) & F_AVOID) && (!char_at(o) || o == h->pos)) { best = o; bs = 0; } } }
  if (best < 0)
  for (int k = 0; k < 8; k++) { int c = a + N8[k]; if (!nb_ok(a, c) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || (char_at(c) && c != h->pos)) continue;
    int sc = distance(c, m->pos) * 10 - distance(c, h->pos);
    if (adj && (adjacent(c, m->pos) || c == m->pos || !adjacent(a, m->pos))) sc -= 100;   // (beside me already: a cell it can't reach but over the altar)
    if (sc > bs) { bs = sc; best = c; } }
  if (best < 0 || bs < 0) return false;
  if (best == h->pos) { if (adj) return false; s_did[0] = 0; DID("altar: beside it, the far side: wait for the %s to come over it", mob_name(m->type)); walk(game_rest(false)); return true; }
  int32_t t0 = h->time; ActResult r = tap_safe(best); if (r == ACT_NONE && h->time == t0) return false;
  s_altar_moves++; st_altar_moves++; s_did[0] = 0; DID("altar: round to its far side, so the %s comes over it", mob_name(m->type)); walk(r); return true;
}
static int st_kit[8];                                   // kit used: wand, curare, missile, unknown wand, scroll gamble, seed, harmful potion
static bool kit_fight(int mi) {                         // a losing duel: spend the kit before the blows, not after
  Mob *m = &G.mobs[mi]; Hero *h = &G.hero; int s;
  bool lose = losing_fight(mi); if (getenv("OLDKIT")) return false;
  int d = distance(m->pos, h->pos); bool hit; bool line = ballistica(h->pos, m->pos, &hit) == m->pos;
  if (line) for (s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) {   // 1. a charged attack wand (it never misses, beside you too)
    Item *it = game_slot(s); if (item_cat(it->kind) != CAT_WAND || it->ench == 0 || !item_known(it->kind) || !attack_wand(it->kind)) continue;
    if (!(it->ench >= wand_max(it) && m->state != ST_SLEEPING) && !(lose && s_lose_cost >= h->HP * 0.25)) continue;
    if (!getenv("OLDMOBS") && ((d <= 2 && (it->kind == IK_WND_LIGHTNING || it->kind == IK_WND_AVALANCHE)) || (it->kind == IK_WND_AVALANCHE && d <= 1 + it->lvl / 3) || (it->kind == IK_WND_FIREBOLT && ((TFLAGS(G.map[h->pos]) & F_FLAMABLE) || (MOB_DEFS[m->type].flags & MD_I_FIREBOLT))) || (it->kind == IK_WND_POISON && (MOB_DEFS[m->type].flags & MD_I_POISON)))) continue;   // 0.52 (audit): lightning arcs back to you, an avalanche buries you too, firebolt lights your grass   // (owner: a full wand isn't recharging: use it on anything; below full, only when it's needed)
    st_kit[0]++; s_did[0] = 0; DID("%s (~%d of my %d HP): zap the wand", it->ench >= wand_max(it) ? "a full wand isn't recharging" : "losing duel", (int)s_lose_cost, h->HP); walk(game_zap(s, m->pos)); return true; }
  if (getenv("THROWFIRST") && line && d >= 2 && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_BOSS)) for (s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind;   // 0.58 (owner: "addicted to melee"): every free throw while it comes, not only in a losing duel
    if (item_cat(k) != CAT_MISSILE || k == IK_CURARE_DART || k == IK_BOOMERANG || (k == IK_INCENDIARY_DART && (d < 3 || (TFLAGS(G.map[m->pos]) & F_FLAMABLE)))) continue;
    st_kit[7]++; s_did[0] = 0; DID("throw while it comes"); walk(game_throw(s, m->pos)); return true; }
  if (!lose) return false;                              // (the rest: a losing one)
  if (class_special(false)) return true;
  if (!getenv("OLDMOBS") && m->HP <= (G.hero.weapon.kind ? weapon_min(&G.hero.weapon) : 1) - mst(m, STAT_DR, MOB_DEFS[m->type].dr)) return false;   // (owner: a 1-HP wraith: it's only about landing one blow; no darts, no potions, no scrolls on it)
  if (line && !m->b.paralysis && (s = find_kind(IK_CURARE_DART)) >= 0 && s < BAG_SIZE && (getenv("OLDMOBS") || (!(MOB_DEFS[m->type].flags & MD_I_PARA) && phit(hero_attack_skill(&h->bag[s], m->pos), mst(m, STAT_DEF, MOB_DEFS[m->type].def)) >= ((m->type == MT_MONK || m->type == MT_GOLEM || m->type == MT_SENIOR) && getenv("CURMONK") ? 0.25 : 0.45)))) {   /* (0.52: point blank halves the aim: not at a dodger like a wraith, game 7) */   // 2. curare: paralysis, free blows (beside you: half the aim, still)
    st_kit[1]++; s_did[0] = 0; DID("losing duel (costs ~%d of my %d HP): curare dart", (int)s_lose_cost, h->HP); walk(game_throw(s, m->pos)); return true; }
  if (line && d >= 3) for (s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind;   // 3. a known harmful potion or a bomb, while it's far enough off
    if (!((item_cat(k) == CAT_POTION && item_known(k) && harmful_potion(k)) || k == IK_BOMB)) continue;
    if (!getenv("OLDMOBS") && (((k == IK_POT_PARALYTIC) && (MOB_DEFS[m->type].flags & MD_I_PARA)) || (k == IK_POT_FLAME && (MOB_DEFS[m->type].flags & MD_I_BURN)) || (k == IK_POT_TOXIC && (MOB_DEFS[m->type].flags & MD_I_GAS)) || (k == IK_POT_FROST && (MOB_DEFS[m->type].flags & MD_I_FROST)) || ((k == IK_POT_PARALYTIC || k == IK_POT_TOXIC) && d < 4))) continue;   // 0.52 (audit): immune; a gas cloud close enough to reach you
    st_kit[6]++; s_did[0] = 0; DID("losing duel: throw the %s", k == IK_BOMB ? "bomb" : "potion"); walk(game_throw(s, m->pos)); return true; }
  if (line && d >= 2 && !(s_pztest && !getenv("NOTENGUCLOSE") && m->type == MT_TENGU)) for (s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind;   // 4. darts and the rest while it comes (0.92: not at Tengu: it dodges nearly all; close in)
    if (item_cat(k) != CAT_MISSILE || (k == IK_INCENDIARY_DART && d < 3)) continue;
    st_kit[2]++; s_did[0] = 0; DID("losing duel: throw while it comes"); walk(game_throw(s, m->pos)); return true; }
  if (line) for (s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) {   // 5. an unknown wand: a gamble (most of them hurt it or hold it)
    Item *it = game_slot(s); if (item_cat(it->kind) != CAT_WAND || it->ench == 0 || item_known(it->kind)) continue;
    st_kit[3]++; s_did[0] = 0; DID("losing duel: try the unknown wand on it"); walk(game_zap(s, m->pos)); return true; }
  if (!getenv("OLDHERO") && !h->b.blind && s_lose_cost >= h->HP * 0.6) {   // 0.52 (audit): known fight scrolls when the sum says losing (not only at a third of my health)
    if ((s = find_kind(IK_SCR_PSIONIC)) >= 0 && item_known(IK_SCR_PSIONIC) && foes_near(4) >= 2 && h->HP * 3 > h->HT) { s_did[0] = 0; DID("losing duel: Psionic Blast"); do_item(s, ACN_READ); return true; }
    if ((s = find_kind(IK_SCR_MIRROR)) >= 0 && item_known(IK_SCR_MIRROR)) { s_did[0] = 0; DID("losing duel: Mirror Image"); do_item(s, ACN_READ); return true; }
    if ((s = find_kind(IK_SCR_LULLABY)) >= 0 && item_known(IK_SCR_LULLABY) && !(MOB_DEFS[m->type].flags & MD_I_SLEEP)) { s_did[0] = 0; DID("losing duel: Lullaby"); do_item(s, ACN_READ); return true; }
    if ((s = find_kind(IK_SCR_TERROR)) >= 0 && item_known(IK_SCR_TERROR) && !(MOB_DEFS[m->type].flags & MD_I_TERROR) && m->type != MT_DM300) { s_did[0] = 0; DID("losing duel: Terror"); do_item(s, ACN_READ); return true; } }
  if (h->HP * 2 < h->HT && !h->b.blind && (foes_near(3) >= 2 || h->HP * 4 < h->HT) && (s = find_kind(IK_WIPE_OUT)) >= 0) { st_wipe++; s_did[0] = 0; DID("losing duel, low: read Wipe Out"); do_item(s, ACN_READ); return true; }   // the altar's gift
  if (adjacent(m->pos, h->pos) && h->HP * 2 < h->HT && !h->b.blind) for (s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind;   // 6. an unknown scroll: teleport, terror, lullaby, mirror images, psionic blast...
    if (item_cat(k) != CAT_SCROLL || item_known(k)) continue;
    if (getenv("NOSCRGAMBLE")) { double esc = idb_p(k, IK_SCR_TELEPORT) + idb_p(k, IK_SCR_TERROR) * !(MOB_DEFS[m->type].flags & MD_I_TERROR) + idb_p(k, IK_SCR_LULLABY) * !(MOB_DEFS[m->type].flags & MD_I_SLEEP) + idb_p(k, IK_SCR_MIRROR) + idb_p(k, IK_SCR_PSIONIC), bad = idb_p(k, IK_SCR_CHALLENGE);   // 0.93 (teacher: the look-ahead threw them away rather than read them): only when the odds say it's probably an escape
      if (esc < 0.45 || bad > 0.2) continue; }
    st_kit[4]++; s_did[0] = 0; DID("losing duel, half HP: read an unknown scroll (could be an escape)"); do_item(s, ACN_READ); return true; }
  return false;
}
static bool pool_prize(int c) {                        // bot2: a pedestal in a pool (PD's PoolPainter: piranhas guard it)
  if (G.map[c] != T_PEDESTAL) return false;
  int w = 0; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && IS_WATER(G.map[n])) w++; }
  return w >= 4;
}

static bool attack_wand_ready(void); static int dry_step(int target); static bool pool_floor(void);
// ---- bot2: special rooms (STRATEGY.md): puzzle rooms and graves wait until the floor is explored
static int room_at(int c) { for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; if (cx(c) > R->left && cx(c) < R->right && cy(c) > R->top && cy(c) < R->bottom) return r; } return -1; }
static uint8_t s_room_skip[64];                        // rooms given up on this floor (cleared on a new floor)
static uint8_t s_room_sig[64];                         // 0.92: 2 = given up for now: no potion that could open it (back on when the potions change)
static int pz_sig(void) { int n = 0; for (int i = 0; i < BAG_SIZE; i++) { int k = G.hero.bag[i].kind; if (item_cat(k) == CAT_POTION && (!item_known(k) || k == IK_POT_INVIS || k == IK_POT_LEVITATION)) n += k == IK_POT_INVIS ? 16 : k == IK_POT_LEVITATION ? 32 : 1; } return n & 255; }
static bool room_skipped(int r) { if (s_room_skip[r] == 2 && pz_sig() != s_room_sig[r]) s_room_skip[r] = 0; return s_room_skip[r] != 0; }
static bool deferred_heap(int hi) {                    // left alone until the puzzle phase
  Heap *hp = &G.heaps[hi]; int r = room_at(hp->pos);
  if (hp->type == HEAP_SKELETON && s_pztest && !getenv("OLDREMAINS") && G.hero.HP * 10 >= G.hero.HT * 7 && !food_pressed() && (r < 0 || G.rooms[r].type != RT_PIT)) return false;   // 0.92 (seed 84 d2: a 30-turn trip back for the remains at the floor's end): remains raise one weak wraith at most: opened in passing when sound
  if (hp->type == HEAP_TOMB || hp->type == HEAP_SKELETON) return true;
  return r >= 0 && (G.rooms[r].type == RT_POOL || G.rooms[r].type == RT_TRAPS);
}
static bool trap_puzzle_pending(void) {                // 0.91: a trap room's prize in sight, not given up on
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos) || hp->type >= HEAP_CARRIED) continue;
    int r = room_at(hp->pos); if (r >= 0 && !room_skipped(r) && G.rooms[r].type == RT_TRAPS) return true; }
  return false;
}
static bool pool_puzzle_pending(void) {                // 0.92: a pool room's prize in sight, not given up on
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos) || hp->type >= HEAP_CARRIED) continue;
    int r = room_at(hp->pos); if (r >= 0 && !room_skipped(r) && G.rooms[r].type == RT_POOL) return true; }
  return false;
}
static bool puzzle_pending(void) {                     // a puzzle room's prize in sight, not given up on
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos) || hp->type >= HEAP_CARRIED) continue;
    int r = room_at(hp->pos); if (r >= 0 && !room_skipped(r) && (G.rooms[r].type == RT_POOL || G.rooms[r].type == RT_TRAPS)) return true; }
  return false;
}
static int room_door(int r) {                          // a known door of room r that can be reached
  Room *R = &G.rooms[r]; int best = -1;
  for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) {
    if (x != R->left && x != R->right && y != R->top && y != R->bottom) continue;
    int c = x + y * MAP_W, t = G.map[c];
    if ((t == T_DOOR || t == T_OPEN_DOOR) && BIT_GET(G.visited, c) && b_dist[c] < 0x7FFF && (best < 0 || b_dist[c] < b_dist[best])) best = c;
  }
  return best;
}
static bool trap_door_testable(void) {                 // 0.92: the pending trap room's door has a dry stand (flame known) or water within 10 (flame unknown)
  if (item_known(IK_POT_FLAME)) return true;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos) || hp->type >= HEAP_CARRIED) continue;
    int r = room_at(hp->pos); if (r < 0 || room_skipped(r) || G.rooms[r].type != RT_TRAPS) continue;
    int door = room_door(r); if (door < 0) door = s_trap_door[r] - 1; if (door < 0) continue;
    static int16_t keep[MAP_LEN]; memcpy(keep, b_dist, sizeof keep); bfs(door, false); bool ok = false;
    for (int c = 0; c < MAP_LEN && !ok; c++) if (IS_WATER(G.map[c]) && BIT_GET(G.visited, c) && room_at(c) != r && !PWATER(c) && b_dist[c] <= 6) ok = true;
    memcpy(b_dist, keep, sizeof keep); if (ok) return true; }
  return false;
}
static int pending_trap_door_dist(void) {               // 0.92: how far the pending trap room's door is (99: none)
  int best = 99; for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos) || hp->type >= HEAP_CARRIED) continue;
    int r = room_at(hp->pos); if (r < 0 || room_skipped(r) || G.rooms[r].type != RT_TRAPS) continue; int door = room_door(r); if (door < 0) door = s_trap_door[r] - 1; if (door < 0) continue;
    int d = distance(G.hero.pos, door); if (d < best) best = d; }
  return best;
}
static int unknown_potion(void) { if (getenv("GOO_POTREV")) { for (int i = BAG_SIZE - 1; i >= 0; i--) { int k = G.hero.bag[i].kind; if (item_cat(k) == CAT_POTION && !item_known(k)) return i; } return -1; } for (int i = 0; i < BAG_SIZE; i++) { int k = G.hero.bag[i].kind; if (item_cat(k) == CAT_POTION && !item_known(k)) return i; } return -1; }
static int open_sides(int c) { int n = 0; for (int k = 0; k < 4; k++) { int m = c + N8[k]; if (nb_ok(c, m) && PASSABLE(G.map[m]) && mob_at(m) < 0) n++; } return n; }
// 0.49 (owner): a gas-trap room (toxic or paralytic) cleared without a potion: anything that lands on a trap sets it off
// (Level.drop presses the cell) and a sprung trap stays dead. From the doorway, throw spare things onto the traps along
// the way to the prize, farthest first; step well away while the gas spreads, wait for it to clear, walk in on the dead
// traps, and pick it all up again
static bool s_oldtrap; static int st_trap_throws, st_trap_rooms;
static int trap_room_gas(int r) {                       // the trap kind filling room r (toxic / paralytic), else -1
  Room *R = &G.rooms[r]; int tox = 0, par = 0, other = 0;
  for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int t = G.map[x + y * MAP_W]; if (fair_on() && !BIT_GET(G.visited, x + y * MAP_W)) continue;
    if (t == T_TOXIC_TRAP) tox++; else if (t == T_PARALYTIC_TRAP) par++; else if (t == T_CHASM || t == T_SUMMONING_TRAP) other++; }
  if (other) return -1;
  return tox ? T_TOXIC_TRAP : par ? T_PARALYTIC_TRAP : 0;   // (0: none left: all sprung)
}
static bool gas_at(int c) { return (g_blob[BLOB_TOXIC] && g_blob[BLOB_TOXIC][c]) || (g_blob[BLOB_PARALYTIC] && g_blob[BLOB_PARALYTIC][c]); }
static bool gas_in_room(int r) { Room *R = &G.rooms[r];
  for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) if (gas_at(x + y * MAP_W)) return true; return false; }
static int throwable(void) {                            // the least useful thing that lands whole: missiles, spare gear, food, scrolls, rings
  int best = -1, bs = 99;
  for (int i = 0; i < BAG_SIZE; i++) { int k = G.hero.bag[i].kind, c = item_cat(k), sc;
    if (!k || k == IK_BOOMERANG || k == IK_INCENDIARY_DART || k == IK_BOMB || k == IK_HONEYPOT || k == IK_PICKAXE) continue;
    if (c == CAT_MISSILE) sc = 1; else if (c == CAT_WEAPON || c == CAT_ARMOR) sc = 2; else if (c == CAT_RING) sc = 3; else if (c == CAT_SCROLL) sc = 4; else if (c == CAT_FOOD && !game_hungry() && ({ int nf = 0; for (int j = 0; j < BAG_SIZE; j++) if (item_cat(G.hero.bag[j].kind) == CAT_FOOD) nf += G.hero.bag[j].qty; nf; }) >= 2) sc = 5; else continue;   // (food: only a spare one, not hungry)
    if (sc < bs) { bs = sc; best = i; } }
  return best;
}
static int throwables(void) {                           // how many throws the bag has in it (a stack of darts: each one)
  int n = 0, nf = 0; for (int j = 0; j < BAG_SIZE; j++) if (item_cat(G.hero.bag[j].kind) == CAT_FOOD) nf += G.hero.bag[j].qty;
  for (int i = 0; i < BAG_SIZE; i++) { int k = G.hero.bag[i].kind, c = item_cat(k);
    if (!k || k == IK_BOOMERANG || k == IK_INCENDIARY_DART || k == IK_BOMB || k == IK_HONEYPOT || k == IK_PICKAXE) continue;
    if (c == CAT_MISSILE) n += G.hero.bag[i].qty; else if (c == CAT_WEAPON || c == CAT_ARMOR || c == CAT_RING || c == CAT_SCROLL) n += c == CAT_SCROLL ? G.hero.bag[i].qty : 1; }
  if (!game_hungry() && nf >= 2) n += nf - 1;
  return n;
}
static int trap_path(int r, int door, int prize, int16_t *path) {   // the cells from the prize back to the door, fewest live traps; n
  static int16_t d[MAP_LEN], from[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) d[c] = 0x7FFF;
  d[door] = 0; from[door] = -1; bool chg = true;
  while (chg) { chg = false;                            // (small rooms: relax till settled)
    for (int c = 0; c < MAP_LEN; c++) { if (d[c] == 0x7FFF) continue;
      for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || (room_at(n) != r && n != prize)) continue; int t = G.map[n];
        if (!PASSABLE(t) && !(TFLAGS(t) & F_AVOID)) continue;
        int w = d[c] + 1 + ((TFLAGS(t) & F_AVOID) ? 20 : 0); if (w < d[n]) { d[n] = w; from[n] = c; chg = true; } } } }
  if (d[prize] == 0x7FFF) return 0;
  int n = 0; for (int c = prize; c >= 0 && n < 64; c = from[c]) path[n++] = c;
  return n;
}
static int trap_task(int r, int prize, const char **why) {   // -1: not this way; else as puzzle_task
  Hero *h = &G.hero; int door = room_door(r), gas = trap_room_gas(r);
  if (door >= 0) s_trap_door[r] = door + 1; else door = s_trap_door[r] - 1;   // (gas about: the door may not count as reachable)
  if (getenv("TRAPDBG") && (door < 0 || gas < 0)) { static int lr = -1, ld = -1; if (r != lr || G.depth != ld) { lr = r; ld = G.depth; printf("TRAPDBG game %d d%d room %d skip: door %d kind %d\n", cur_game, G.depth, r, door, gas); } }
  if (s_oldtrap || door < 0 || gas < 0 || s_trap_throws_room[r] > 40 || game_starving()) { if (getenv("TRAPDBG") && !s_rollout) printf("TRAPDBG ret -1 at 1 hero %d,%d\n", cx(G.hero.pos), cy(G.hero.pos)); return -1; }
  int16_t path[64]; int n = trap_path(r, door, prize, path), tgt = -1;
  if (getenv("TRAPDBG") && !s_rollout) { static int lastr = -1, lastd = -1; if (r != lastr || G.depth != lastd) { lastr = r; lastd = G.depth; printf("TRAPDBG game %d d%d room %d gas %d path %d throwable %d\n", cur_game, G.depth, r, gas, n, throwable()); } }
  if (!n) { if (getenv("TRAPDBG") && !s_rollout) printf("TRAPDBG ret -1 at 2 hero %d,%d\n", cx(G.hero.pos), cy(G.hero.pos)); return -1; }
  for (int i = 0; i < n && tgt < 0; i++) if (TFLAGS(G.map[path[i]]) & F_AVOID) tgt = path[i];   // (from the prize end: the farthest live trap)
  { int live = 0; for (int i = 0; i < n; i++) if (TFLAGS(G.map[path[i]]) & F_AVOID) live++;   // (not enough to throw for the whole way: not started this way)
    if (live && !s_trap_throws_room[r] && throwables() < live) return -1; }
  bool gassy = gas_in_room(r) || gas_at(door), near = false;
  for (int y = cy(h->pos) - 2; y <= cy(h->pos) + 2; y++) for (int x = cx(h->pos) - 2; x <= cx(h->pos) + 2; x++) if ((unsigned)x < MAP_W && (unsigned)y < MAP_H && gas_at(x + y * MAP_W)) near = true;
  bool volley = !getenv("OLDVOLLEY") && tgt >= 0 && !near && h->pos == door && throwable() >= 0;   // 0.50 (owner): gas spreads a tile a turn: throw the lot while it's 2+ away, then leave once
  if (!volley && (gassy || (tgt >= 0 && gas_at(h->pos)))) {         // wait well away from it
    if (gas_at(h->pos) || distance(h->pos, door) < 4 || room_at(h->pos) == r) {
      int best = -1; for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < 0x7FFF && room_at(c) != r && distance(c, door) >= 4 && !gas_at(c) && PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & F_AVOID) && mob_at(c) < 0 && (best < 0 || b_dist[c] < b_dist[best])) best = c;
      if (best >= 0) { *why = "traps: away from the gas"; return best; } }
    walk(game_rest(false)); *why = "traps: wait for the gas to clear"; return -2;
  }
  if (tgt < 0) { if (getenv("TRAPDBG") && !s_rollout) printf("TRAPDBG ret -1 at 3 hero %d,%d\n", cx(G.hero.pos), cy(G.hero.pos)); return -1; }                              // the way is clear: picked up like anything else (below)
  int s = throwable(); if (s < 0) { if (getenv("TRAPDBG") && !s_rollout) { printf("TRAPDBG ret -1 at 4 hero %d,%d door %d,%d prize %d,%d n %d:", cx(G.hero.pos), cy(G.hero.pos), cx(door), cy(door), cx(prize), cy(prize), n); for (int i = 0; i < n; i++) printf(" %d,%d(%d)", cx(path[i]), cy(path[i]), G.map[path[i]]); printf("\n"); } return -1; }
  bool hit; if (h->pos != door || ballistica(h->pos, tgt, &hit) != tgt) {
    if (h->pos != door) { *why = "traps: into the doorway to throw"; return door; }
    for (int i = 0; i < n; i++) if ((TFLAGS(G.map[path[i]]) & F_AVOID) && ballistica(h->pos, path[i], &hit) == path[i]) { tgt = path[i]; break; }
    if (ballistica(h->pos, tgt, &hit) != tgt) { if (getenv("TRAPDBG") && !s_rollout) printf("TRAPDBG ret -1 at 5 hero %d,%d\n", cx(G.hero.pos), cy(G.hero.pos)); return -1; }
  }
  if (h->HP < h->HT * 7 / 10) { walk(game_rest(true)); *why = "traps: heal first"; return -2; }
  char nm[64]; item_name(&h->bag[s], nm, sizeof nm); static char wb[120]; snprintf(wb, sizeof wb, "traps: throw %s onto the trap at %d,%d", nm, cx(tgt), cy(tgt)); *why = wb;
  s_trap_throws_room[r]++; st_trap_throws++; st_uses[ACN_THROW]++; walk(game_throw(s, tgt)); return -2;
}
#define PZSKIP(r, tag) do { if (getenv("PZDBG") && !s_rollout) printf("PZDBG d%d step %d room %d skip: %s\n", G.depth, s_bot_steps, (r), (tag)); s_room_skip[r] = 1; } while (0)
static int s_pz_only_traps;
static int puzzle_task(const char **why) {             // after exploring: a cell to go to, -2 acted, -1 nothing left
  Hero *h = &G.hero; bfs(h->pos, false);
  for (int hi = 0; hi < G.n_heaps; hi++) {
    Heap *hp = &G.heaps[hi]; int c = hp->pos;
    if ((unsigned)c >= MAP_LEN || !BIT_GET(G.visited, c) || hp->type >= HEAP_CARRIED || !deferred_heap(hi) || s_black[c] > s_bot_steps) continue;
    int r = room_at(c); if (r >= 0 && room_skipped(r)) continue;
    if (s_pz_only_traps && !(r >= 0 && G.rooms[r].type == RT_TRAPS)) continue;
    if (hp->type == HEAP_TOMB || hp->type == HEAP_SKELETON) {     // a grave / remains: open it from the side with the fewest open cells (wraiths rise there)
      if (r >= 0 && room_skipped(r)) continue;
      bool skel = hp->type == HEAP_SKELETON && !getenv("OLDDUNG");   // 0.52 (audit): remains raise one weak wraith at most (and only if cursed): not a crypt
      if (!skel && !attack_wand_ready() && h->lvl < G.depth + 3) { if (r >= 0 && getenv("OLDDUNG")) s_room_skip[r] = 1; else s_black[c] = s_bot_steps + 100000; continue; }   // wraiths: wands, or much stronger
      bool pitr = skel && r >= 0 && G.rooms[r].type == RT_PIT && !getenv("OLDDUNG2");   // 0.54 (audit): in the pit room the remains are the way out (its key): never skip them, 50% health will do
      if (skel && !pitr && h->HP < h->HT * 8 / 10 && food_pressed()) { s_black[c] = s_bot_steps + 100000; continue; }
      if (h->HP < h->HT * (pitr ? 5 : 9) / 10) { if (pitr && !game_starving()) { walk(game_rest(true)); *why = "remains: heal first"; return -2; } if (food_pressed()) { if (r >= 0) s_room_skip[r] = 1; else s_black[c] = s_bot_steps + 100000; continue; } if (s_pztest) { static int gk = -1, gt; if (gk != G.depth * MAP_LEN + c) { gk = G.depth * MAP_LEN + c; gt = G.now; } if (G.now - gt > 50 * TICK) { s_black[c] = s_bot_steps + 100000; continue; } }   /* 0.92 (seed 84 d6: 191 turns of resting for one grave) */ walk(game_rest(true)); *why = "grave: heal first"; return -2; }   // (hungry: no time for it)
      int best = -1, bo = 9;
      for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && PASSABLE(G.map[n]) && b_dist[n] < 0x7FFF && mob_at(n) < 0 && !(TFLAGS(G.map[n]) & F_AVOID)) { int o = open_sides(n); if (o < bo || (o == bo && b_dist[n] < b_dist[best])) { bo = o; best = n; } } }
      if (best < 0) { if (r >= 0) s_room_skip[r] = 1; else s_black[c] = s_bot_steps + 100000; continue; }
      if (h->pos != best) { *why = "grave: take position"; return best; }
      *why = "grave: open"; return c;                  // (tapped from beside it: opened from here)
    }
    int ty = r >= 0 ? G.rooms[r].type : 0, buff = ty == RT_POOL ? h->b.invis : h->b.levitation, need = ty == RT_POOL ? IK_POT_INVIS : IK_POT_LEVITATION;
    if (buff > 2) { *why = ty == RT_POOL ? "pool: invisible, grab it" : "traps: levitating, grab it"; return c; }
    if (ty == RT_TRAPS && !s_oldtrap) {                // 0.49: sprung traps all the way: walk in; else spring them from the doorway
      int16_t pth[64]; int dr = room_door(r); if (dr < 0) dr = s_trap_door[r] - 1; int np = dr >= 0 ? trap_path(r, dr, c, pth) : 0; bool live = false; for (int i = 0; i < np; i++) if (TFLAGS(G.map[pth[i]]) & F_AVOID) live = true;
      if (getenv("TRAPDBG") && !s_rollout && np) { printf("TRAPDBG clear? np %d gas %d bdist %d prize %d,%d map %d path:", np, gas_in_room(r), b_dist[c], cx(c), cy(c), G.map[c]); for (int i = 0; i < np; i++) printf(" %d,%d(%d)", cx(pth[i]), cy(pth[i]), G.map[pth[i]]); printf("\n"); }
      if (np && !live && !gas_in_room(r) && b_dist[c] < 0x7FFF) { if (!s_trap_done[r]) { s_trap_done[r] = 1; st_trap_rooms++; } *why = "traps: the way is clear, grab it"; return c; }
      int tt = s_pztest && unknown_potion() >= 0 && !item_known(IK_POT_LEVITATION) ? -1 : trap_task(r, c, why); if (tt != -1) return tt; }   // (0.91 owner: test the unknown potions at its door first: levitation flies straight in)
    int door = room_door(r), stand = -1;
    if (ty == RT_TRAPS) { if (door >= 0) s_trap_door[r] = door + 1; else if (s_trap_door[r]) door = s_trap_door[r] - 1; }   // 0.91: fire took the door (seed 84): its cell is still the way in
    if (door < 0) { PZSKIP(r, "no door"); continue; }
    bool wetneed = false;
    if (ty == RT_TRAPS && s_pztest) {                  // 0.91 (owner: "find a safe spot to test very nearby"): 2 steps from the door is near enough for
      int bs = 1 << 30; wetneed = !item_known(IK_POT_FLAME) && unknown_potion() >= 0 && !(find_kind(need) >= 0 && item_known(need));   // levitation to fly in, and a step clear of the wooden door and the grass when it's liquid flame
      int R = wetneed ? 10 : 3;                        // 0.92 (seed 84: burned to death testing at the door, no water near): liquid flame unknown: in water a short walk from the door (levitation lasts 20 turns: 6 to the door, the room's own path inside), or not here
      static int16_t dd[MAP_LEN]; if (wetneed) { memcpy(dd, b_dist, sizeof dd); bfs(door, false); }
      for (int y = cy(door) - R; y <= cy(door) + R; y++) for (int x = cx(door) - R; x <= cx(door) + R; x++) { if ((unsigned)x >= MAP_W || (unsigned)y >= MAP_H) continue; int c = x + y * MAP_W;
        if (c == door || room_at(c) == r || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || PWATER(c) || PREACH(c) || (wetneed ? dd[c] : b_dist[c]) >= 0x7FFF || c == G.entrance || c == G.exit || heap_at(c) >= 0) continue;
        if (wetneed && (!IS_WATER(G.map[c]) || b_dist[c] > 6)) continue;
        int d = distance(c, door), sc = d * 4 + (d == 1 ? 12 : 0) - (IS_WATER(G.map[c]) ? 6 : 0) + ((TFLAGS(G.map[c]) & F_FLAMABLE) ? 8 : 0);
        for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (TFLAGS(G.map[n]) & F_FLAMABLE)) sc += 3; }
        if (sc < bs) { bs = sc; stand = c; } }
      if (wetneed) memcpy(b_dist, dd, sizeof dd);
      if (wetneed && stand < 0) continue; }           // (no water near the door: the potions get tested in water elsewhere, and a known levitation comes back)
    if (stand < 0) for (int k = 0; k < 8; k++) { int n = door + N8[k]; if (nb_ok(door, n) && room_at(n) != r && PASSABLE(G.map[n]) && !IS_WATER(G.map[n]) && !PREACH(n) && !(TFLAGS(G.map[n]) & (F_FLAMABLE | F_AVOID)) && b_dist[n] < 0x7FFF && (stand < 0 || fire_safe(n) > fire_safe(stand))) stand = n; }
    if (stand < 0) for (int k = 0; k < 8; k++) { int n = door + N8[k]; if (nb_ok(door, n) && room_at(n) != r && PASSABLE(G.map[n]) && !PWATER(n) && !(TFLAGS(G.map[n]) & F_AVOID) && b_dist[n] < 0x7FFF) { bool wet = false; for (int j = 0; j < 8; j++) { int m2 = n + N8[j]; if (nb_ok(n, m2) && PWATER(m2)) wet = true; } if (!wet) stand = n; } }   // bot3: (flammable or not) out of the piranhas' reach
    if (stand < 0) { bool wet = false; for (int j = 0; j < 8; j++) { int m2 = door + N8[j]; if (nb_ok(door, m2) && PWATER(m2)) wet = true; } if (wet) { PZSKIP(r, "wet door"); continue; } stand = door; }
    bool doortest = getenv("OLDDOORTEST") || (ty == RT_POOL && !getenv("OLDPOOLTEST")) || (ty == RT_TRAPS && s_pztest);   // 0.54 (owner): the pool's painter leaves a Potion of Invisibility on the floor: guess the unknown ones at the door
    if (doortest && !(find_kind(need) >= 0 && item_known(need)) && unknown_potion() >= 0 && h->HP >= h->HT * (s_pztest ? 7 : 9) / 10 && stash_for(stand)) { *why = "puzzle: scrolls out of harm's way first"; return -2; }
    if (!getenv("OLDQBUG") && (s_black[stand] > s_bot_steps || (game_starving() && h->HP < h->HT / 2))) { PZSKIP(r, "stand blacked / starving"); continue; }   // 0.54 (r10 lockout): can't get there, or no time
    if (h->pos != stand) { *why = "puzzle: beside the door"; return stand; }
    if (h->HP < h->HT * (s_pztest ? 6 : 9) / 10) { if (food_pressed()) { PZSKIP(r, "hungry, hurt"); continue; } walk(game_rest(true)); *why = "puzzle: heal first"; return -2; }   // (0.92 owner: less resting; invisible or floating, the room can't touch me)   // (hungry: no time for it)
    int s = find_kind(need);
    if (s >= 0 && item_known(need) && s_pztest && ({ bool foe = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && G.mobs[i].state != ST_PASSIVE && !(MOB_DEFS[G.mobs[i].type].flags & (MD_WATER | MD_NPC)) && distance(G.mobs[i].pos, h->pos) <= 8) foe = true; foe; })) { static int wq = 0; if (wq++ % 20 < 15) { walk(game_rest(false)); *why = "puzzle: a creature about: not yet"; return -2; } }   // 0.92: not with one about to break it
    if (s >= 0 && item_known(need)) { st_uses[ACN_DRINK]++; do_item(s, ACN_DRINK); *why = "puzzle: drink the known potion"; return -2; }
    if (unknown_potion() >= 0 && !doortest) continue;   // 0.49: not tested at the door (liquid flame, toxic gas: the deaths it caused): tested the usual way, in water; levitating, it flies in (above)
    if ((s = unknown_potion()) >= 0) { st_uses[ACN_DRINK]++; st_kinds[h->bag[s].kind]++; do_item(s, ACN_DRINK); *why = "puzzle: test an unknown potion"; return -2; }
    PZSKIP(r, "no way in"); if (s_pztest) { s_room_skip[r] = 2; s_room_sig[r] = pz_sig(); }   // no way in: leave it (0.92 seed 84 d2: given up at step 172, before the bistre potion turned up: till the potions change)
  }
  return -1;
}

static bool room_seen(int r);
static bool pool_floor(void) { for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_POOL && (!fair_on() || room_seen(r))) return true; return false; }
static int dry_step(int target) {                      // bot2: the first step of a walk that keeps out of the water, -1 none
  s_dry = true; s_dry_reach = !getenv("DRYWATER"); s_dry_goal = target; bfs(target, false);   // bot3: out of the piranhas' bite, not just their water
  if (s_dry_reach && b_dist[G.hero.pos] == 0x7FFF) { s_dry_reach = false; bfs(target, false); if (!s_rollout) s_pass_bite = s_bot_steps + 8; }   // (no way round: their water only; past their teeth quickly)
  s_dry = false; s_dry_reach = false;
  int best = -1, bd = b_dist[G.hero.pos];
  for (int k = 0; k < 8; k++) { int n = G.hero.pos + N8[k]; if (nb_ok(G.hero.pos, n) && b_dist[n] < bd && mob_at(n) < 0 && ((n != G.exit && n != G.entrance) || n == target) && (n == target || heap_at(n) < 0 || G.heaps[heap_at(n)].type == HEAP_PLAIN) && G.map[n] != T_ALCHEMY && G.map[n] != T_WELL) { bd = b_dist[n]; best = n; } }   // (a tap on a shop item buys, on a chest opens it)
  return best;
}
static bool attack_wand_ready(void) {
  for (int s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) { Item *it = game_slot(s); if (item_cat(it->kind) == CAT_WAND && item_known(it->kind) && attack_wand(it->kind) && it->ench > 0) return true; }
  return false;
}
static int blob_at(int b, int c) { return g_blob[b] ? g_blob[b][c] : 0; }
static bool bad_air(int c) { return blob_at(BLOB_TOXIC, c) || blob_at(BLOB_FIRE, c) || blob_at(BLOB_PARALYTIC, c) || blob_at(BLOB_CONFUSION, c) || (!s_oldfire && fire_next(c)) || fire_doomed(c); }
static bool escape_air(void) {                          // bot2: in gas or fire: walk to the nearest clean air (two steps clear)
  Hero *h = &G.hero; if (!bad_air(h->pos)) return false;
  if (dm_hold() && !blob_at(BLOB_FIRE, h->pos) && !blob_at(BLOB_PARALYTIC, h->pos)) return false;   // 0.54: its gas follows it anyway; stepping out makes it walk (and mend)
  if (!getenv("OLDGASTANK") && blob_at(BLOB_TOXIC, h->pos) && !blob_at(BLOB_FIRE, h->pos) && !blob_at(BLOB_PARALYTIC, h->pos) && !blob_at(BLOB_CONFUSION, h->pos) && !(!s_oldfire && fire_next(h->pos)) && h->HP * 10 >= h->HT * ((!getenv("OLDDM") && ({ bool dm = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_DM300 && adjacent(G.mobs[i].pos, h->pos)) dm = true; dm; })) ? (dm_plan() && getenv("DMHOLDPLAN") ? 0 : 2.5) : 4)
      && ({ bool adj = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && adjacent(G.mobs[i].pos, h->pos)) adj = true; adj; })) return false;   // 0.50 (review g6): toxic gas with a foe at your side (DM-300 vents it): fight on above 40%, fleeing only gives it free turns
  bfs(h->pos, false); int best = -1, bd = 0x7FFF;
  for (int c = 0; c < MAP_LEN; c++) {
    if (b_dist[c] >= bd || b_dist[c] == 0 || bad_air(c) || PWATER(c)) continue;
    bool clear = true; for (int k = 0; k < 8 && clear; k++) { int n = c + N8[k]; if (nb_ok(c, n) && bad_air(n)) clear = false; }
    if (clear) { bd = b_dist[c]; best = c; }
  }
  if (best < 0) return false;
  bfs(best, false); int st = -1, sd = b_dist[h->pos];   // one step toward it, round whoever is in the way
  for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && b_dist[n] < sd && !char_at(n)) { sd = b_dist[n]; st = n; } }
  if (st < 0) return false;
  int32_t t = h->time; ActResult r = tap_safe(st);
  if (r == ACT_NONE && h->time == t) return false;
  DID(fire_doomed(h->pos) && !blob_at(BLOB_FIRE, h->pos) ? "off the grass: the fire will get here" : "out of the gas / fire"); walk(r); return true;
}

// ---- bot2: seed tactics (STRATEGY.md: plant a seed between you and the enemy, step back)
static int s_step_back = -1, s_step_back2, s_onto = -1;                           // a seed planted here: step away from it next
static int foes_near(int r) { int n = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && (!fair_on() || foe_sees(i)) && distance(G.mobs[i].pos, G.hero.pos) <= r) n++; return n; }
// 0.52 (owner): seeds are thrown, not planted: a thrown seed grows where it lands (trap.c land), and fires when trodden
// on. Onto the foe's way in (Icecap 2+ cells from me: it freezes all within 1); Earthroot or Sungrass beside me, then
// step onto it. Planting under yourself is throwing it at yourself: only for the one beside you (step back, it follows).
static int st_seedthrow;
static int path_cell(Mob *m, int mind) {                // a free cell between it and me, mind+ from me, in my throw line, on its way
  Hero *h = &G.hero; int best = -1, bs = 99;
  for (int c = 0; c < MAP_LEN; c++) { int dh = distance(c, h->pos), dm = distance(c, m->pos); if (dh < mind || dh > 3 || dm >= distance(h->pos, m->pos) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & (F_AVOID | F_PIT)) || char_at(c) || plant_at(c) >= 0 || heap_at(c) >= 0 || IS_WATER(G.map[c])) continue;
    bool hit; if (ballistica(h->pos, c, &hit) != c) continue;
    int sc = dm * 2 + dh; if (sc < bs) { bs = sc; best = c; } }
  return best;
}
static bool seed_throw(int mi) {
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; int s, d = distance(m->pos, h->pos);
  if (m->state != ST_HUNTING || h->b.blind) return false;
  bool hurt = h->HP * 100 < h->HT * PRM[P_SEEDHP] || s_losing, many = foes_near(3) >= 2;
  if (!hurt && !many) return false;
  if (d >= 3 && (s = find_kind(IK_SEED_EARTHROOT)) >= 0 && G.herbal_pos != h->pos) {   // armour while I stand on it: beside me, then onto it, before it comes
    for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (!nb_ok(h->pos, c) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || char_at(c) || plant_at(c) >= 0 || heap_at(c) >= 0 || IS_WATER(G.map[c]) || distance(c, m->pos) < 2) continue;
      st_seedthrow++; s_onto = c; DID("throw Earthroot beside me, then stand on it (armour)"); walk(game_throw(s, c)); return true; } }
  static const uint8_t T[] = { IK_SEED_FADELEAF, IK_SEED_ICECAP, IK_SEED_SORROWMOSS, IK_SEED_FIREBLOOM };
  for (unsigned k = 0; k < sizeof T; k++) { if ((s = find_kind(T[k])) < 0) continue;
    int mind = T[k] == IK_SEED_ICECAP || T[k] == IK_SEED_FIREBLOOM ? 2 : 1;
    if (T[k] == IK_SEED_FIREBLOOM && (!fire_safe(h->pos) || d < 4)) continue;
    if (d < mind + 1) continue;
    int c = path_cell(m, mind); if (c < 0) continue;
    st_seedthrow++; DID("throw a seed of %s onto its way in", T[k] == IK_SEED_FADELEAF ? "Fadeleaf" : T[k] == IK_SEED_ICECAP ? "Icecap" : T[k] == IK_SEED_SORROWMOSS ? "Sorrowmoss" : "Firebloom"); walk(game_throw(s, c)); return true; }
  return false;
}
static bool seed_tactic(int mi) {
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; int s;
  if (!getenv("OLDSEED") && seed_throw(mi)) return true;
  if (plant_at(h->pos) >= 0 || heap_at(h->pos) >= 0 || !PASSABLE(G.map[h->pos]) || h->pos == G.entrance || h->pos == G.exit || IS_WATER(G.map[h->pos])) return false;
  bool hurt = h->HP * 100 < h->HT * PRM[P_SEEDHP] || s_losing, many = foes_near(3) >= 2;
  if (!hurt && !many) return false;
  if (getenv("OLDSEED") && (s = find_kind(IK_SEED_EARTHROOT)) >= 0 && item_known(IK_SEED_EARTHROOT) && adjacent(m->pos, h->pos)) { DID("plant Earthroot under me (armour)"); do_item(s, ACN_PLANT); return true; }   // (0.52 audit: a plant under you never fires: off)
  static const uint8_t TRAP0[] = { IK_SEED_ICECAP, IK_SEED_SORROWMOSS, IK_SEED_DREAMWEED, IK_SEED_FADELEAF }, TRAP1[] = { IK_SEED_FADELEAF, IK_SEED_SORROWMOSS, IK_SEED_FADELEAF, IK_SEED_SORROWMOSS };   // 0.52 (audit): Dreamweed's gas spreads to you; Icecap freezes all within 1 (you too: two steps back)
  const uint8_t *TRAP = getenv("OLDSEED") ? TRAP0 : TRAP1; if (!getenv("OLDSEED") && !adjacent(m->pos, h->pos)) return false;   // (not Firebloom: it burned the hero more than the foe)
  if (!adjacent(m->pos, h->pos) && distance(m->pos, h->pos) > 2) return false;
  for (unsigned k = 0; k < 4; k++) {
    if ((s = find_kind(TRAP[k])) < 0 || !kn(TRAP[k])) continue;
    if (boss_reserve(TRAP[k]) && !(MOB_DEFS[m->type].flags & MD_BOSS) && h->HP * 10 > h->HT * 3) continue;
    if (TRAP[k] == IK_SEED_FIREBLOOM && !fire_safe(h->pos)) continue;
    DID("plant a seed, then step back"); s_step_back = h->pos; s_step_back2 = TRAP[k] == IK_SEED_ICECAP && !getenv("OLDSEED"); do_item(s, ACN_PLANT); return true;
  }
  if (h->HP < h->HT * 4 / 10) for (int i = 0; i < BAG_SIZE; i++) if (getenv("OLDSEED") && item_cat(h->bag[i].kind) == CAT_SEED && !item_known(h->bag[i].kind) && h->bag[i].kind != IK_SEED_ROTBERRY) {   // a gamble (0.52: seeds are known: no gamble)
    DID("plant an unknown seed, then step back"); s_step_back = h->pos; do_item(i, ACN_PLANT); return true; }
  return false;
}
static bool step_back(void) {                          // off the planted seed, away from the enemies, so they step on it
  Hero *h = &G.hero; int c0 = s_step_back; s_step_back = -1;
  if (c0 != h->pos) return false;
  int best = -1, bs = -1;
  for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (!nb_ok(h->pos, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID) || char_at(n)) continue;
    int d = 99; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos)) { int e = distance(n, G.mobs[i].pos); if (e < d) d = e; }
    if (d > bs) { bs = d; best = n; } }
  if (best < 0) return false;
  int32_t t = h->time; ActResult r = tap_safe(best); if (r == ACT_NONE && h->time == t) return false;
  DID("step back off the seed"); if (s_step_back2) { s_step_back = h->pos; s_step_back2 = 0; } walk(r); return true;
}

static bool npc_wanted(int i); static bool ignored(int i);
// ---- bot3: the route planner (calm play): always the nearest useful thing, so a floor costs as few turns as possible
static int s_boss_room = -1;                             // bot3: a sleeping boss's room: the rest of the floor first
static bool boss_zone(int c) {
  if (!PRM[P_BOSSLAST]) return false;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && (MOB_DEFS[m->type].flags & MD_BOSS) && m->state != ST_HUNTING && BIT_GET(G.visited, m->pos)) { int r = room_at(m->pos); if ((r >= 0 && room_at(c) == r) || distance(c, m->pos) <= 4) return true; } }
  return false;
}
// 0.91 (owner: "ATE DEW AT FULL HP"): a dewdrop heals first (Dewdrop.doPickUp), so take it only when it does something: under
// half health; into a vial that isn't full at full health; or hurt with the vial full. Else leave it for later (the Assist's rule)
static bool dew_take(void) {
  Hero *h = &G.hero; bool vial = find_kind(IK_DEW_VIAL) >= 0;
  if (s_pztest && getenv("DEWBANK") && vial && G.dew < 10) return h->HP >= h->HT || h->HP * 10 < h->HT * 3;   // 0.93 test (audit): a full vial heals all of max HP (volume^1.3/20), ten drops eaten hurt ~3 HP each: bank them
  if (s_pztest && h->HP < h->HT && (G.depth >= 6 || game_starving())) return true;   // 0.92 (seed 84: 52 drops left lying while it limped, starving, from floor 8 on; mending is 1 HP a 10 turns): deep, or with no mending, a drop is 2-4 HP now
  return h->HP * 2 < h->HT || (vial && (G.dew < 10 ? h->HP >= h->HT : h->HP < h->HT));
}
static bool far_missile(int hp) {                       // 0.92 (seed 84 d2: 53 turns there and back for two shuriken): plain missiles aren't worth a long walk
  if (!s_pztest || getenv("NOFARMISS")) return false; int k = G.heaps[hp].item.kind; if (item_cat(k) != CAT_MISSILE || k == IK_CURARE_DART || k == IK_INCENDIARY_DART || k == IK_BOOMERANG) return false;
  int c = G.heaps[hp].pos; return (unsigned)c < MAP_LEN && b_dist[c] > 12; }
static bool heap_wanted(int hp) {
  Hero *h = &G.hero; int c = G.heaps[hp].pos, ty = G.heaps[hp].type;
  if (getenv("KEYDBG") && !s_rollout && item_cat(G.heaps[hp].item.kind) == CAT_KEY && (unsigned)c < MAP_LEN && BIT_GET(G.visited, c)) { static int ls = -1; if (ls != s_bot_steps) { ls = s_bot_steps; printf("KEYDBG step %d key at %d,%d ty %d black %d hero %d mob %d defer %d dropped %d stash %d piran %d entr %d exit %d\n", s_bot_steps, cx(c), cy(c), ty, s_black[c] > s_bot_steps, c == h->pos, mob_at(c), deferred_heap(hp), BIT_GET(s_dropped, c) ? 1 : 0, in_stash(c), s_piranhas && PREACH(c), c == G.entrance, c == G.exit); } }
  if ((unsigned)c >= MAP_LEN || !BIT_GET(G.visited, c) || c == h->pos || s_black[c] > s_bot_steps || ty >= HEAP_CARRIED || ty == HEAP_FOR_SALE) return false;
  if (G.map[c] == T_ALCHEMY || mob_at(c) >= 0 || (s_piranhas && PREACH(c))) return false;
  if ((c == G.entrance || c == G.exit) && !getenv("OLDQBUG")) return false;   // 0.54 (night ping-pong): a tap on the stairs takes them
  if ((ty == HEAP_LOCKED_CHEST || ty == HEAP_CRYSTAL_CHEST) && !(G.keys[G.depth] & 0x30)) return false;
  if (deferred_heap(hp)) return false;
  if (s_pztest && !getenv("OLDMIMIC") && (ty == HEAP_CHEST || ty == HEAP_MIMIC) && G.depth > 1 && h->HP < (3 + G.depth) * 2 + 4) return false;   // 0.93 (1356:1 d17: opened a chest at 26/85, a mimic: (3+depth)x4 HP, hits to a quarter of that): a chest is a mimic 1 time in 5: not when hurt
  if (!getenv("OLDDEW2") && G.heaps[hp].item.kind == IK_DEW && !dew_take()) return false;
  if (getenv("DEWLEAVE") && G.heaps[hp].item.kind == IK_DEW && find_kind(IK_DEW_VIAL) < 0 && h->HP * 3 > h->HT) return false;   // 0.78 (owner): no vial yet: leave the dew for it
  if (!getenv("OLDDEW") && G.heaps[hp].item.kind == IK_DEW && find_kind(IK_DEW_VIAL) >= 0 && G.dew < 10 && h->HP < h->HT && h->HP > h->HT / 3) return false;   // 0.52 (audit): hurt, a drop heals a point; at full health it fills the vial (a second life at 10)
  if ((c == s_stash || in_stash(c)) && stash_ready() && (bag_used() < BAG_SIZE || item_cat(G.heaps[hp].item.kind) == CAT_SCROLL)) return true;
  if (ty == HEAP_PLAIN && BIT_GET(s_dropped, c) && G.heaps[hp].item.kind != IK_GOLD) return false;
  if (ty == HEAP_PLAIN && bag_used() >= BAG_SIZE && item_cat(G.heaps[hp].item.kind) != CAT_KEY && G.heaps[hp].item.kind != IK_GOLD && G.heaps[hp].item.kind != IK_DEW && !(item_stackable(G.heaps[hp].item.kind) && find_kind(G.heaps[hp].item.kind) >= 0)) return false;   // (bot3: a full bag takes only what stacks onto something there)
  return true;
}

// bot3 stage 1: a TOUR instead of "nearest first": every known goal (frontier clusters, wanted items, locked doors
// with a key) gets a visiting order that is short overall (nearest-neighbour start, then 2-opt and or-opt moves), ending
// at the exit; the first goal of that tour is where to go now.
static int tour_first(int16_t *goal, int ng, int endc) {
  if (ng <= 1) return ng ? goal[0] : -1;
  if (ng > 40) ng = 40;
  static int16_t D[42][42]; static int16_t dist[MAP_LEN]; int pts[42]; int np = 0; pts[np++] = G.hero.pos; for (int i = 0; i < ng; i++) pts[np++] = goal[i]; int hasend = endc >= 0; if (hasend) pts[np++] = endc;
  for (int a = 0; a < np; a++) {                      // walking distances from each point (known cells, no traps)
    bfs(pts[a], false); for (int b = 0; b < np; b++) { int d = b_dist[pts[b]]; if (d >= 0x7FFF) { int m = 0x7FFF; for (int k = 0; k < 8; k++) { int n = pts[b] + N8[k]; if (nb_ok(pts[b], n) && b_dist[n] + 1 < m) m = b_dist[n] + 1; } d = m; } D[a][b] = d > 9999 ? 9999 : d; } }
  (void)dist;
  int ord[42], n = ng; bool used[42] = { 0 }; int cur = 0;   // goals are 1..ng; start 0; end ng+1 (if any)
  for (int i = 0; i < n; i++) { int best = -1; for (int j = 1; j <= ng; j++) if (!used[j] && (best < 0 || D[cur][j] < D[cur][best])) best = j; ord[i] = best; used[best] = true; cur = best; }
  #define TCOST(o) ({ int _c = D[0][o[0]]; for (int _i = 0; _i + 1 < n; _i++) _c += D[o[_i]][o[_i + 1]]; if (hasend) _c += D[o[n - 1]][ng + 1]; _c; })
  for (int it = 0, improved = 1; it < 30 && improved; it++) { improved = 0;
    for (int i = 0; i < n - 1; i++) for (int j = i + 1; j < n; j++) {   // 2-opt: reverse ord[i..j]
      int a = i ? ord[i - 1] : 0, b = ord[i], c = ord[j], d2 = j + 1 < n ? ord[j + 1] : (hasend ? ng + 1 : -1);
      int before = D[a][b] + (d2 >= 0 ? D[c][d2] : 0), after = D[a][c] + (d2 >= 0 ? D[b][d2] : 0);
      if (after < before) { for (int x = i, y = j; x < y; x++, y--) { int t = ord[x]; ord[x] = ord[y]; ord[y] = t; } improved = 1; } }
    for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) { if (i == j) continue;   // or-opt: move one goal elsewhere
      int tmp[42]; int k = 0; for (int x = 0; x < n; x++) if (x != i) tmp[k++] = ord[x]; int ins = j > i ? j - 1 : j; for (int x = n - 1; x > ins; x--) tmp[x] = tmp[x - 1]; tmp[ins] = ord[i];
      if (TCOST(tmp) < TCOST(ord)) { memcpy(ord, tmp, sizeof(int) * n); improved = 1; } } }
  #undef TCOST
  return pts[ord[0]];
}
// 0.48 (owner): a corridor that just stops hides a door (PD's tunnels only end at doors): one search there (2 turns, and
// a search always finds what's hidden right beside you) on the way, not only once the floor has run out
static bool s_olddead;
static bool dead_end(int c) {
  if (!BIT_GET(G.visited, c) || !PASSABLE(G.map[c]) || c == G.entrance || c == G.exit || G.map[c] == T_ALCHEMY || BIT_GET(s_searched, c)) return false;
  int open = 0, oo = 0;
  for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n)) continue; if (!BIT_GET(G.visited, n) || BIT_GET(s_searched, n)) return false;
    int t = G.map[n]; if (t != T_WALL && t != T_WALL_DECO && t != T_SECRET_DOOR) { open++; if (k < 4) oo++; } }
  return open == 1 && oo == 1;
}
static int plan_next(bool going, const char **why) {
  Hero *h = &G.hero; bfs(h->pos, false);
  int f = g_from, hx = cx(h->pos), hy = cy(h->pos), fx = f >= 0 ? hx - cx(f) : 0, fy = f >= 0 ? hy - cy(f) : 0;
  int best = -1, bc = 1 << 30; const char *bw = NULL;
#define CONSIDER(cell, cost, w) do { int _c = (cell), _k = (cost); if (b_dist[_c] < 0x7FFF) { int dx = cx(_c) - hx, dy = cy(_c) - hy; _k = _k * 4 - ((dx * fx + dy * fy) > 0 ? 2 + s_headb : (dx * fx + dy * fy) < 0 ? -s_headb : 0); if (s_plandbg == s_bot_steps) printf("   PLAN %s %d,%d bd %d k %d\n", (w), cx(_c), cy(_c), b_dist[_c], _k); if (_k < bc) { bc = _k; best = _c; bw = (w); } } } while (0)
  for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i)) { int c = G.heaps[i].pos; if (going && b_dist[c] > 4) continue; CONSIDER(c, b_dist[c] + (b_dist[c] > s_exw_inear ? s_exw_item : 0), "item"); }
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && BIT_GET(G.fov, m->pos) && npc_wanted(i) && s_black[m->pos] <= s_bot_steps) CONSIDER(m->pos, b_dist[m->pos] > 0 ? b_dist[m->pos] : 1, "npc"); }
  int hunt = PRM[P_HUNT] ? PRM[P_HUNT] : (getenv("OLDHUNT") || getenv("THRIFT")) ? 0 : 70;   /* (0.58 THRIFT, owner: "under-levelled but at full HP and well fed") */   // 0.52 (audit): under-levelled at Goo (3-5 vs 6-7): respawns are the XP; hunt those that still give it and that I'd beat
  if (hunt && !going && h->HP * 100 >= h->HT * hunt && !game_hungry())   // bot3: XP: go after the monsters in sight (not bosses, not piranhas)
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & (MD_BOSS | MD_WATER | MD_NPC)) || m->type == MT_WRAITH || ignored(i)) continue;
      if (!PRM[P_HUNT] && (h->lvl > MOB_DEFS[m->type].max_lvl || !MOB_DEFS[m->type].exp || losing_fight(i))) continue;
      int d = 0x7FFF; for (int k = 0; k < 8; k++) { int q = m->pos + N8[k]; if (nb_ok(m->pos, q) && b_dist[q] < d) d = b_dist[q]; } if (d < 0x7FFF) CONSIDER(m->pos, d, "hunt"); }
  if (G.keys[G.depth] & 15) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) {   // bot3: a locked door is reached from beside it (bfs never enters it)
    int d = 0x7FFF; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] + 1 < d) d = b_dist[n] + 1; } if (d < 0x7FFF) b_dist[c] = d; }
  if (!getenv("OLDDUNG") && G.well_pos >= 0 && G.map[G.well_pos] == T_WELL && BIT_GET(G.visited, G.well_pos) && G.well_kind != 2) {   // 0.52 (audit): a well is reached from beside it (bfs never enters it: the target was dead)
    int c = G.well_pos, d = 0x7FFF; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] + 1 < d) d = b_dist[n] + 1; } if (d < 0x7FFF) b_dist[c] = d; }
  if (!going) for (int c = 0; c < MAP_LEN; c++) {
    if (b_dist[c] == 0x7FFF || s_black[c] > s_bot_steps) continue;
    int t = G.map[c];
    if (t == T_LOCKED_DOOR && (G.keys[G.depth] & 15)) CONSIDER(c, b_dist[c], "locked door");
    else if (t == T_WELL && !BIT_GET(s_used_feature, c) && c == G.well_pos && (getenv("OLDDUNG") || G.well_kind == 0 || (G.well_kind == 1 && (h->HP * 2 < h->HT || cursed_known(&h->armor) || cursed_known(&h->weapon) || !floor_left() || (s_pztest && !getenv("OLDWELL2") && h->hunger >= 260))))) CONSIDER(c, b_dist[c] + 2, "well");   /* 0.52: Awareness at once; Health when hurt, cursed, or on leaving */
    else if (!s_olddead && dead_end(c)) CONSIDER(c, b_dist[c] + 2, "dead end");
    else if (b_dist[c] > 0 && frontier(c) && !(s_piranhas && PREACH(c)) && !boss_zone(c)) {
      int adj = 0;
      if (s_exw_diag || s_exw_unk || s_exw_far) { int uo = 0, unk = 0; for (int k = 0; k < 4; k++) { int u = c + N8[k]; if (nb_ok(c, u) && !BIT_GET(G.visited, u)) uo++; }
        for (int y = cy(c) - 2; y <= cy(c) + 2; y++) for (int x = cx(c) - 2; x <= cx(c) + 2; x++) if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && !BIT_GET(G.visited, x + y * MAP_W)) unk++;
        adj = (uo ? 0 : s_exw_diag) - (unk * s_exw_unk) / 4;
        if (s_exw_far && !uo && unk <= 3) adj += b_dist[c] * s_exw_far / 10; }   // bot3 experiment: a far trip just to peek at a corner
      if (s_expgain) { int unk = 0; for (int y = cy(c) - 5; y <= cy(c) + 5; y++) for (int x = cx(c) - 5; x <= cx(c) + 5; x++) if (x >= 1 && y >= 1 && x < MAP_W - 1 && y < MAP_H - 1 && !BIT_GET(G.visited, x + y * MAP_W)) unk++; adj -= unk * s_expgain / 100; }   // 0.58 (owner: the floor fills its rectangle): how much unknown lies that way
      if (s_expcommit && c == s_exp_tgt) adj -= s_expcommit;   // 0.58: keep to the edge already headed for
      CONSIDER(c, b_dist[c] + PRM[P_FRONT_BIAS] + adj, "explore"); }
  }
#undef CONSIDER
  if (bw && !strcmp(bw, "explore")) s_exp_tgt = best;
  if (s_exp_tsp && !going && best >= 0) {             // bot3: the tour's first goal instead of the nearest
    int16_t goal[64]; const char *gw[64]; int ng = 0; bfs(h->pos, false);
    if (G.keys[G.depth] & 15) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) { int d = 0x7FFF; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] + 1 < d) d = b_dist[n] + 1; } if (d < 0x7FFF) b_dist[c] = d; }
    for (int i = 0; i < G.n_heaps && ng < 60; i++) if (heap_wanted(i) && !far_missile(i) && b_dist[G.heaps[i].pos] < 0x7FFF) { goal[ng] = G.heaps[i].pos; gw[ng++] = "item"; }
    static uint8_t seen[MAP_LEN]; memset(seen, 0, sizeof seen);
    for (int c = 0; c < MAP_LEN && ng < 60; c++) { if (seen[c] || b_dist[c] == 0x7FFF || b_dist[c] == 0 || s_black[c] > s_bot_steps || !frontier(c) || (s_piranhas && PREACH(c)) || boss_zone(c)) continue;
      int q[MAP_LEN], qh = 0, qt = 0, rep = c; q[qt++] = c; seen[c] = 1;   // a frontier cluster: the touching frontier cells; its nearest cell stands for it
      while (qh < qt) { int x = q[qh++]; if (b_dist[x] < b_dist[rep]) rep = x; for (int k = 0; k < 8; k++) { int y = x + N8[k]; if (nb_ok(x, y) && !seen[y] && b_dist[y] < 0x7FFF && b_dist[y] > 0 && s_black[y] <= s_bot_steps && frontier(y)) { seen[y] = 1; q[qt++] = y; } } }
      goal[ng] = rep; gw[ng++] = "explore"; }
    if (G.keys[G.depth] & 15) for (int c = 0; c < MAP_LEN && ng < 60; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c) && b_dist[c] < 0x7FFF && s_black[c] <= s_bot_steps) { goal[ng] = c; gw[ng++] = "locked door"; }
    bool npcw = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos) && npc_wanted(i) && G.mobs[i].pos == best) npcw = true;
    if (ng >= 2 && !npcw && strcmp(bw, "well")) { int endc = BIT_GET(G.visited, G.exit) ? G.exit : -1; int f = tour_first(goal, ng, endc);
      if (f >= 0) for (int i = 0; i < ng; i++) if (goal[i] == f) { best = f; bw = gw[i]; break; } }
  }
  if (best >= 0 && !going && s_detour && bw && (!strcmp(bw, "explore") || !strcmp(bw, "item") || !strcmp(bw, "locked door"))) {
    // 0.91 (owner: "the probability-weighted cost of NOT doing the things that are not #1 ... and reevaluate once"): each
    // thing passed over (a wanted item, a key, tall grass hiding dew and seeds) costs a trip back later: about there and back
    // from where #1 leaves me, times how likely I'd make it. Doing it now costs only the detour. Take the best such
    // detour first when it's cheaper than its weighted trip back.
    static int s_dt = -1, s_dt_d = -1; static const char *s_dtw; if (s_dt_d != G.depth) { s_dt = -1; s_dt_d = G.depth; }
    if (s_dt >= 0 && (s_dt == h->pos || b_dist[s_dt] >= 0x7FFF || b_dist[s_dt] > (G.map[s_dt] == T_LOCKED_DOOR ? 26 : 10) || (s_pztest && b_dist[best] < b_dist[s_dt]) || !(heap_at(s_dt) >= 0 ? heap_wanted(heap_at(s_dt)) : G.map[s_dt] == T_HIGH_GRASS || (G.map[s_dt] == T_LOCKED_DOOR && (G.keys[G.depth] & 15)) || (s_pocket && frontier(s_dt))))) s_dt = -1;
    if (s_dt >= 0) { best = s_dt; bw = s_dtw; goto detour_done; }   // (a detour, once chosen, is kept till done: two things each behind the other made it walk to and fro)
    static int16_t dh[MAP_LEN]; memcpy(dh, b_dist, sizeof dh); bfs(best, false);
    if (s_pztest && (G.keys[G.depth] & 15)) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) { int d = 0x7FFF; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] + 1 < d) d = b_dist[n] + 1; } if (d < 0x7FFF) b_dist[c] = d; }
    int pick = -1, pg = 0; const char *pw = NULL;
    for (int c = 0; c < MAP_LEN; c++) {
      bool ld = s_pztest && G.map[c] == T_LOCKED_DOOR && (G.keys[G.depth] & 15) && BIT_GET(G.visited, c) && c != best;   // 0.92 (seed 84 d1: the library 12 cells from the key, left for a 60-turn trip at the floor's end): a sure prize room
      if (c == best || dh[c] == 0 || dh[c] > (ld ? 30 : 8) || b_dist[c] >= 0x7FFF || s_black[c] > s_bot_steps) continue;
      int wt = 0; const char *w = NULL; int hpi = heap_at(c);
      if (ld) { wt = 12; w = "locked door"; }
      else if (hpi >= 0 && heap_wanted(hpi)) { wt = 10; w = "item"; }
      else if (G.map[c] == T_HIGH_GRASS && BIT_GET(G.visited, c) && !boss_zone(c) && mob_at(c) < 0 && s_grassw) { wt = s_grassw; w = "grass"; }
      else if (s_pocket && frontier(c) && !(s_piranhas && PREACH(c)) && !boss_zone(c) && (cx(c) <= 5 || cx(c) >= MAP_W - 6 || cy(c) <= 5 || cy(c) >= MAP_H - 6)) { wt = s_pocket; w = "explore"; }   // 0.92 (seed 84 d1: the west door by the stairs, left at turn 0, cost a 30-turn trip back): a corner pocket beside me is cheap now, dear later
      if (!w) continue;
      int detour = dh[c] + b_dist[c] - dh[best], back = 2 * b_dist[c] + 2, gain; if (ld && back < 30) back = 30; gain = back * wt - detour * 10;   // (tenths; a vault's a dead end: the trip back later is from wherever the floor ends, far on average)
      if (detour <= (ld ? 40 : s_detour) && gain > pg) { pg = gain; pick = c; pw = w; } }
    memcpy(b_dist, dh, sizeof dh);
    if (pick >= 0) { best = pick; bw = pw; st_detour++; s_dt = pick; s_dtw = pw; }
    detour_done:; }
  if (best >= 0 && getenv("FEAT") && bw && !strcmp(bw, "explore")) {   // bot3: features of the frontier chosen (stage 1 learning)
    int uo = 0, ud = 0, maxw = 0, maxf = 0, minw = 9; for (int k = 0; k < 8; k++) { int u = best + N8[k]; if (!nb_ok(best, u) || BIT_GET(G.visited, u)) continue; if (k < 4) uo++; else ud++;
      int w = 0, f = 0; for (int j = 0; j < 8; j++) { int v = u + N8[j]; if (!nb_ok(u, v) || !BIT_GET(G.visited, v)) continue; if (G.map[v] == T_WALL || G.map[v] == T_WALL_DECO) w++; else if (PASSABLE(G.map[v]) || G.map[v] == T_DOOR) f++; }
      if (w > maxw) maxw = w; if (w < minw) minw = w; if (f > maxf) maxf = f; }
    int unk5 = 0; for (int y = cy(best) - 2; y <= cy(best) + 2; y++) for (int x = cx(best) - 2; x <= cx(best) + 2; x++) if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && !BIT_GET(G.visited, x + y * MAP_W)) unk5++;
    printf("F %d %d %d %d %d %d %d %d\n", uo, ud, minw, maxw, maxf, unk5, b_dist[best], G.map[best]); }
  if (best >= 0) { *why = bw; if (G.map[best] == T_WELL) { BIT_SET(s_used_feature, best); st_wells++; } int mi = mob_at(best); if (mi >= 0) st_talks[G.mobs[mi].type]++; }
  if (getenv("DBGITEM") && best >= 0 && heap_at(best) >= 0) { int hp = heap_at(best); printf("   ITEM %d,%d kind %d bag %d/%d stash %d dropped %d stack %d have %d ready %d\n", cx(best), cy(best), G.heaps[hp].item.kind, bag_used(), BAG_SIZE, s_stash, BIT_GET(s_dropped, best), item_stackable(G.heaps[hp].item.kind), find_kind(G.heaps[hp].item.kind), stash_ready()); }
  if (getenv("DBGPLAN") && best >= 0) printf("   plan_next -> %d (%s) black %d steps %d bdist %d\n", best, bw, s_black[best], s_bot_steps, b_dist[best]);
  return best;
}

// ==== bot3: a gear power model from the game's own formulas (Char.hit, Weapon accuracy/speed with encumbrance,
// armour DR and evasion): the expected health lost killing the floor's typical monsters with this weapon and armour.

static double hitp(double a, double d) { if (a <= 0) return 0; if (d <= 0) return 1; return a >= d ? 1 - d / (2 * a) : a / (2 * d); }
static double pw2(double b, int e) { double r = 1; for (int i = 0; i < e && i < 8; i++) r *= b; return r; }
static double hp_cost_vs(int mt, const Item *w, const Item *a) {
  const MobDef *md = &MOB_DEFS[mt]; Hero *h = &G.hero; int st = hero_str() + s_str_plus;
  double acc = h->attack_skill, dmg, dly = 1;
  bool ng = !getenv("OLDGLYPH");                         // 0.52 (owner + audit): imbues, enchantments, glyphs, the huntress, the rogue
  if (w && w->kind) { int enc = item_str(w) - st; acc *= weapon_acu(w->kind) / 100.0 * pw2(2.0 / 3, enc); dly = weapon_dly(w->kind) / 100.0 * pw2(1.2, enc);
    dmg = (weapon_min(w) + weapon_max(w)) / 2.0 + (st > item_str(w) && !(ng && h->hclass == 3) ? (st - item_str(w)) / 2.0 : 0);
    if (ng && (w->flags & IF_FAST)) dly *= 0.6; if (ng && (w->flags & IF_ACCURATE)) acc *= 1.5; }
  else dmg = st > 10 ? (st - 9 + 1) / 2.0 : 1;
  dmg -= md->dr / 2.0; if (dmg < 0.5) dmg = 0.5;
  int wl = w && w->kind && !item_broken(w) ? (w->lvl > 0 ? w->lvl : 0) : 0, we = ng && w && w->kind ? w->ench : 0; double mk = 1;   // mk: what's left of its damage to me
  switch (we) {                                         // ench.c weapon_proc, as the chance of a proc per blow times what it does
    case WE_FIRE: dmg += (wl + 1.0) / (wl + 3) * ((wl + 3) / 2.0 + 5); mk *= 1.05; break;   // (owner: strong, but its flames can catch you)
    case WE_POISON: dmg += (wl + 1.0) / (wl + 3) * 0.5 * ((wl + 1) + (wl + 1) * (wl + 2) / 6.0); break;
    case WE_DEATH: dmg += 8.0 / (wl + 100) * md->HT; break;
    case WE_PARALYSIS: mk *= 1 - (wl + 1.0) / (wl + 8) * (wl / 2.0 + 1) * 0.8; break;
    case WE_SLOW: mk *= 1 - (wl + 1.0) / (wl + 4) * 0.5; break;
    case WE_SHOCK: dmg *= 1 + (wl + 1.0) / (wl + 4) * 0.4; break;
    case WE_INSTABILITY: dmg *= 1.15; break;
    case WE_HORROR: mk *= 1 - (wl + 1.0) / (wl + 5) * 0.4; break;
    case WE_LUCK: dmg *= 1.1; break; }
  double dps = hitp(acc, md->def) * dmg / dly;
  int ae = a && a->kind ? item_str(a) - st : 0; double def = h->defense_skill * pw2(2.0 / 3, ae);
  if (ng && h->hclass == 2 && ae <= 0) def = (h->defense_skill - ae);   // the rogue: light armour, better dodging
  double md_dmg = (md->dmg_lo + md->dmg_hi) / 2.0 - (a && a->kind ? armor_dr(a) / 2.0 : 0); if (md_dmg < 0.3) md_dmg = 0.3;
  double hr = hitp(md->att, def), mdps = hr * md_dmg / (md->atk_delay ? md->atk_delay / (double)TICK : 1);
  int al = a && a->kind && !item_broken(a) ? (a->lvl > 0 ? a->lvl : 0) : 0, ag = ng && a && a->kind ? a->ench : 0;
  switch (ag) {                                         // ench.c ench_armor
    case AG_VISCOSITY: mk *= 1 - (al + 1.0) / (al + 7) * 0.6; break;   // (owner: the best) the blow comes later, a point a turn: rest and regeneration outrun it
    case AG_BOUNCE: mk *= 1 - (al + 1.0) / (al + 5) * 0.3; break;
    case AG_METABOLISM: mk *= 1 - (al / 2 + 1.0) / (al / 2 + 5) * 0.5; break;
    case AG_DISPLACEMENT: mk *= 0.9; break;
    case AG_ENTANGLEMENT: mk *= 0.95; break;
    case AG_STENCH: mk *= 0.95; break;
    case AG_ANTI_ENTROPY: mdps += hr * (al + 1.0) / (al + 6) * 8; mk *= 1 - (al + 1.0) / (al + 6) * 0.3; break;   // (owner: the worst) it freezes the foe but sets you alight: burns, and your scrolls
    case AG_POTENTIAL: mdps += hr * (al + 1.0) / (al + 7) * md_dmg / 4; break;
    case AG_MULTIPLICITY: mdps += (al / 2 + 1.0) / (al / 2 + 6) * h->HT / 12.0 * hr; mk *= 0.95; break; }
  mdps *= mk;
  if (ng && we == WE_LEECH) { double heal = dps * 0.5 * (wl + 2.0) / (wl + 6); mdps = mdps - heal > mdps * 0.1 ? mdps - heal : mdps * 0.1; }
  if (hunt2() && boom_slot() != -100) {                // 0.54: a couple of free boomerang throws while it comes, then the better of her blade and a point-blank throw
    double far = boom_dpt_vs(md->def, md->dr, s_boom_plus, false), near = boom_dpt_vs(md->def, md->dr, s_boom_plus, true);
    bool shoots = mt == MT_TENGU || mt == MT_EYE || mt == MT_SCORPIO || mt == MT_SHAMAN || mt == MT_DM300;
    double left = (md->HT - s_hp_cut > 1 ? md->HT - s_hp_cut : 1) - (shoots ? 0 : 2.0) * far; if (left < 0) left = 0; double d2 = dps > near ? dps : near;
    return left / (d2 > 0.01 ? d2 : 0.01) * mdps; }
  if (calc_on() && !getenv("CALCNOFOE")) {                                      // 0.93 review: what the plain stats leave out
    double rate = md->atk_delay ? TICK / (double)md->atk_delay : 1;
    if (mt == MT_ELEMENTAL) mdps += 2.5 * (1 - exp(-hr * 0.5 * rate * 6));
    if (mt == MT_BRUTE || mt == MT_SHIELDED) mdps *= 0.75 + 0.25 * 25.0 / 13;   // mob_brute.inc: enraged under a quarter, 10-40 for 8-18
    if ((mt == MT_MONK || mt == MT_SENIOR) && w && w->kind && w->kind != IK_KNUCKLES && !(w->flags & IF_CURSED)) dps /= 1 + 2 * hr * rate / 6;
    double extra = 0;
    if (mt == MT_WARLOCK) {                                  // two bolts on the way in (doubled roll, armour ignored), each 1 in 2 weakening: -2 STR
      double pb = hitp(md->att * 2.0, def), pw = 1 - (1 - 0.5 * pb) * (1 - 0.5 * pb); extra = 2 * pb * 14.5;
      int ew = w && w->kind ? item_str(w) - st : 0; double fa = pw2(2.0 / 3, ew + 2) / pw2(2.0 / 3, ew), fd = pw2(2.0 / 3, ae + 2) / pw2(2.0 / 3, ae);
      dps *= 1 - pw * (1 - fa); mdps *= 1 + pw * (1 / fd - 1) * 0.5; }
    return extra + (md->HT - s_hp_cut > 1 ? md->HT - s_hp_cut : 1) / (dps > 0.01 ? dps : 0.01) * mdps; }
  return (md->HT - s_hp_cut > 1 ? md->HT - s_hp_cut : 1) / (dps > 0.01 ? dps : 0.01) * mdps;
}
extern const uint8_t BEST_KIND[], BEST_OFF[26]; extern const uint16_t BEST_W[];
static double mix_cost(int d, const Item *w, const Item *a, double *worst) {   // 0.93: the expected cost over the floor's real spawn odds (Bestiary, game.c)
  if (d < 1) d = 1; if (d > 25) d = 25; int o = BEST_OFF[d - 1], n = BEST_OFF[d] - o; double t = 0, ws = 0; if (worst) *worst = 0;
  for (int i = 0; i < n; i++) { int mt = BEST_KIND[o + i]; if (MOB_DEFS[mt].flags & MD_BOSS) continue; double c = hp_cost_vs(mt, w, a); t += BEST_W[o + i] * c; ws += BEST_W[o + i]; if (worst && BEST_W[o + i] * 5 >= 100 && c > *worst) *worst = c; }
  return ws > 0 ? t / ws : 0; }
static double gear_cost(const Item *w, const Item *a) {   // lower: better (the region's monsters, and its boss)
  static const uint8_t REF[5][3] = { { MT_GNOLL, MT_CRAB, MT_GOO }, { MT_SKELETON, MT_THIEF, MT_TENGU }, { MT_BAT, MT_BRUTE, MT_DM300 }, { MT_MONK, MT_GOLEM, MT_KING }, { MT_SUCCUBUS, MT_SCORPIO, MT_EYE } };
  int r = (G.depth - 1) / 5; if (r > 4) r = 4; double c = 0;
  if (calc_on() && !getenv("CALCNOMIX")) { for (int d = G.depth; d < G.depth + 3; d++) c += mix_cost(d, w, a, NULL); c = c / 3 * 2 + hp_cost_vs(REF[r][2], w, a) * 0.5; } else
  for (int i = 0; i < 3; i++) c += hp_cost_vs(REF[r][i], w, a) * (i == 2 ? 0.5 : 1);
  if (a && a->kind) { int enc = item_str(a) - (hero_str() + s_str_plus); if (enc > 0) c *= 1 + s_arm_speedk * (pw2(1.3, enc) - 1); }   // bot3: Hero.speed: heavy armour walks 1.3x slower per missing STR (turns, hunger, exposure)
  return c;
}
// 0.58 (owner: "rest only up to a calculated amount based on expected crab encounter damage"; THRIFT=1): the health to
// keep: a fight with this depth's worst common creature, with a margin; between a quarter and 60% of the most
static int thrift_need(void) {
  static const uint8_t R2[5][2] = { { MT_GNOLL, MT_CRAB }, { MT_SKELETON, MT_THIEF }, { MT_BAT, MT_BRUTE }, { MT_MONK, MT_GOLEM }, { MT_SUCCUBUS, MT_SCORPIO } };
  Hero *h = &G.hero; int r = (G.depth - 1) / 5; if (r > 4) r = 4; double c = 0;
  if (calc_on() && !getenv("CALCNOMIX")) mix_cost(G.depth, h->weapon.kind ? &h->weapon : NULL, h->armor.kind ? &h->armor : NULL, &c); else
  for (int i = 0; i < 2; i++) { double v = hp_cost_vs(R2[r][i], h->weapon.kind ? &h->weapon : NULL, h->armor.kind ? &h->armor : NULL); if (v > c) c = v; }
  int need = (int)(c * 1.3) + 2, lo = h->HT / 4, hi = h->HT * 6 / 10; return need < lo ? lo : need > hi ? hi : need;
}
// 0.48 (owner): what an unidentified piece is believed to be. PD (Weapon/Armor.random): 1 in 5 cursed at -1..-3 (stuck
// on you), 1 in 5 upgraded, the rest +0; so without a known Remove Curse in the bag it counts as -2 (worth it only if
// still better then); with one, or known not cursed, as +0. Known levels are what they are.
static bool s_oldgear;
static bool have_rc(void) { return find_kind(IK_SCR_REMOVE_CURSE) >= 0 && item_known(IK_SCR_REMOVE_CURSE); }
static Item believed(const Item *it, int unk) {
  Item t = *it; if (!t.kind || (t.flags & IF_LEVEL_KNOWN) || s_oldgear) return t;
  bool clean = (t.flags & IF_CURSED_KNOWN) && !(t.flags & IF_CURSED);
  t.lvl = clean ? (unk > 0 ? unk : 0) : unk; return t;
}
static double gear_cost_b(const Item *w, const Item *a, int unk) { Item bw = believed(w, unk), ba = believed(a, unk); return gear_cost(&bw, &ba); }
static bool underpowered(int c) {                       // still in the starting gear, unupgraded, with the bosses near: the risk is worth taking
  Hero *h = &G.hero;
  if (c == CAT_WEAPON) return item_tier(h->weapon.kind) <= 2 && known_lvl(&h->weapon) <= 0 && G.depth >= 4;
  if (c == CAT_ARMOR) return item_tier(h->armor.kind) <= 1 && known_lvl(&h->armor) <= 0 && G.depth >= 3;
  return false;
}
// 0.48 (owner: "take all factors into consideration"): an unidentified weapon or armour at the odds of Weapon/Armor.random:
// +0 60%; +1/+2/+3 13.3/4.4/2.2%; cursed -1/-2/-3 13.3/4.4/2.2%. A good outcome can be swapped back if it disappoints; a
// cursed one is worn till it's lifted (a known Remove Curse in the bag: as good as not stuck; a Scroll of Upgrade lifts
// it too, at +1). Known clean or known cursed narrows it. The outcomes' costs (gear_cost: health lost to the region's
// monsters and boss) are averaged as a root mean square, so a bad tail weighs more than a plain average would.
static double s_riskp = 2; static bool s_nounk;   // 0.48: the risk exponent (1: plain average; higher: the bad tail weighs more); never an unknown piece
static double rt2(double x) { if (x <= 0) return 0; double r = x > 1 ? x : 1; for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r); return r; }
static double ev_cost(const Item *it, int cat, double base) {
  static const int8_t L[7] = { 0, 1, 2, 3, -1, -2, -3 }; static const int16_t P[7] = { 540, 120, 40, 20, 120, 40, 20 };
  bool clean = (it->flags & IF_CURSED_KNOWN) && !(it->flags & IF_CURSED), cur = (it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CURSED);
  bool rc = have_rc(), up = getenv("OLDUPFREE") ? find_kind(IK_SCR_UPGRADE) >= 0 && (getenv("OLDGEARFIX") || (item_known(IK_SCR_UPGRADE) && 0)) : item_known(IK_SCR_UPGRADE) && (find_kind(IK_SCR_UPGRADE) >= 0 || G.depth >= 3);   // 0.54: an upgrade lifts the curse AND adds a level: on the piece you mean to keep it's no scroll lost (and one comes every floor or two)   // 0.52 (audit, a death): an unknown scroll isn't an uncurse; and a known Upgrade spent on lifting a curse is a scroll lost: not "free"
  Item ow = believed(&G.hero.weapon, 0), oa = believed(&G.hero.armor, 0); double s = 0, w = 0;
  for (int k = 0; k < 7; k++) {
    if ((clean && L[k] < 0) || (cur && L[k] >= 0)) continue;
    Item t = *it; t.lvl = L[k] < 0 && up && !rc ? L[k] + 1 : L[k];
    double c = cat == CAT_WEAPON ? gear_cost(&t, &oa) : gear_cost(&ow, &t);
    if ((L[k] >= 0 || rc || up) && c > base) c = base;   // (not stuck: back to what you wore)
    s += P[k] * pow(c, s_riskp); w += P[k];
  }
  return w > 0 ? pow(s / w, 1.0 / s_riskp) : base;
}
static double unk_cost(const Item *it, int cat, double base) {   // 0.48 experiments: UNKAS=n treats every unknown piece as +n (no odds)
  if (getenv("UNKAS")) { Item t = *it; t.lvl = atoi(getenv("UNKAS")); Item ow = believed(&G.hero.weapon, 0), oa = believed(&G.hero.armor, 0); return cat == CAT_WEAPON ? gear_cost(&t, &oa) : gear_cost(&ow, &t); }
  return ev_cost(it, cat, base);
}
#include "toe.inc"
static int32_t s_goo_calm = -1;                          // bot3: just dodged a pumped Goo: hold still a moment (don't walk back into it)
static int s_goo_spot = -1, s_goo_hold;                  // bot3: the owner's spot outside Goo's door
static bool goo_open(int mi) {                           // bot3 (owner): bombs, darts, missiles, known harmful potions, attack wands at a Goo 2+ away in line
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; bool hit; if (ballistica(h->pos, m->pos, &hit) != m->pos) return false; int d = distance(m->pos, h->pos);
  if (d < 2 || !BIT_GET(G.fov, m->pos)) return false;
  if (getenv("X_OPENFIX") && m->aux2 && d <= 2) return false;   // X: pumped and within its jump: dodge first
  static const uint8_t KIT[] = { IK_BOMB, IK_POT_PARALYTIC, IK_POT_FLAME, IK_CURARE_DART, IK_INCENDIARY_DART, IK_JAVELIN, IK_TOMAHAWK, IK_SHURIKEN, IK_DART, IK_BOOMERANG };
  for (unsigned k = 0; k < sizeof KIT; k++) { int sl = find_kind(KIT[k]); if (sl < 0 || sl >= BAG_SIZE) continue; if (item_cat(KIT[k]) == CAT_POTION && (!item_known(KIT[k]) || d < 3)) continue;
    s_did[0] = 0; DID("Goo: open with the saved %s", KIT[k] == IK_BOMB ? "bomb" : item_cat(KIT[k]) == CAT_POTION ? "potion" : "missile"); walk(game_throw(sl, m->pos)); return true; }
  for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { Item *it = game_slot(s2); if (item_cat(it->kind) == CAT_WAND && it->ench > 0 && item_known(it->kind) && attack_wand(it->kind)) { s_did[0] = 0; DID("Goo: zap it"); walk(game_zap(s2, m->pos)); return true; } }
  return false;
}
static bool dm_open(int mi) {                            // 0.93 (DM-300 deaths with 2-5 missiles and bombs unused): while it comes, everything that flies: bombs first (20-40 here), then the heavy missiles, then the wands
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; bool hit; if (ballistica(h->pos, m->pos, &hit) != m->pos) return false; int d = distance(m->pos, h->pos);
  if (d < 2 || !BIT_GET(G.fov, m->pos) || m->state != ST_HUNTING) return false;
  if (!getenv("NODMPARA") && !m->b.paralysis && d >= 5 && item_known(IK_POT_PARALYTIC) && find_kind(IK_POT_PARALYTIC) >= 0 && find_kind(IK_POT_PARALYTIC) < BAG_SIZE && !blob_at(BLOB_PARALYTIC, h->pos)) {   // (audit: not immune; a potion's cloud holds it ~12 turns, the buff ~10 more; damage breaks it ~1 time in 20)
    s_did[0] = 0; DID("DM-300: paralytic gas on it from 5 away"); walk(game_throw(find_kind(IK_POT_PARALYTIC), m->pos)); return true; }
  static const uint8_t KIT[] = { IK_BOMB, IK_POT_FLAME, IK_CURARE_DART, IK_JAVELIN, IK_TOMAHAWK, IK_INCENDIARY_DART, IK_SHURIKEN, IK_BOOMERANG };   // (no plain darts: its armour eats them; no gas: the arena fills)
  for (unsigned k = 0; k < sizeof KIT; k++) { int sl = find_kind(KIT[k]); if (sl < 0 || sl >= BAG_SIZE) continue; if (item_cat(KIT[k]) == CAT_POTION && !item_known(KIT[k])) continue; if ((KIT[k] == IK_BOMB && d < 2) || (KIT[k] == IK_INCENDIARY_DART && d < 3)) continue;
    s_did[0] = 0; DID("DM-300: while it comes, the %s", KIT[k] == IK_BOMB ? "bomb" : item_cat(KIT[k]) == CAT_POTION ? "potion" : "missile"); walk(game_throw(sl, m->pos)); return true; }
  for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { Item *it = game_slot(s2); if (item_cat(it->kind) == CAT_WAND && it->ench > 0 && item_known(it->kind) && attack_wand(it->kind) && !(it->kind == IK_WND_LIGHTNING && d <= 2) && !(it->kind == IK_WND_AVALANCHE && d <= 1 + it->lvl / 3)) { s_did[0] = 0; DID("DM-300: zap it while it comes"); walk(game_zap(s2, m->pos)); return true; } }
  return false;
}
static int s_wait_mob, s_wait_n, s_boss_uid = -1, s_boss_min, s_boss_hits;
static bool s_oldsleep;
static bool floor_left(void) {                          // anything left to explore or pick up on this floor (b_dist kept as it was)
  static int16_t keep[MAP_LEN]; memcpy(keep, b_dist, sizeof keep); bool r = false;
  bfs(G.hero.pos, false);
  for (int c = 0; c < MAP_LEN && !r; c++) if (b_dist[c] < 0x7FFF && b_dist[c] > 0 && frontier(c) && s_black[c] <= s_bot_steps && !(s_piranhas && PREACH(c)) && !boss_zone(c)) r = true;
  for (int i = 0; i < G.n_heaps && !r; i++) if (heap_wanted(i) && b_dist[G.heaps[i].pos] < 0x7FFF) r = true;
  memcpy(b_dist, keep, sizeof keep); return r;
}
// 0.51 (review: crab deaths, g107): hurt, and a wanderer in sight I'd lose to (the sum): out of its sight and rest
// first, not on exploring toward it. (Hunting me already: no use, a crab runs twice as fast: the kit then.)
static int s_prep_floor = -1, s_prep_steps; static int st_prep;
static bool prep_retreat(void) {
  Hero *h = &G.hero; if (getenv("OLDPREP") || h->HP * 10 >= h->HT * 9 || game_starving() || h->b.roots) return false;
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && adjacent(G.mobs[i].pos, h->pos)) return false;   // (review: not while one beside me is hitting me)
  if (s_prep_floor != G.depth) { s_prep_floor = G.depth; s_prep_steps = 0; }
  if (s_prep_steps > 500 || h->hunger >= 250) return false;   // (a floor's worth, and not hungry: then on as before)
  int w = -1; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; int d = distance(m->pos, h->pos);
    if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (m->state != ST_WANDERING && m->state != ST_SLEEPING) || d < 2 || d > 8 || (MOB_DEFS[m->type].flags & (MD_BOSS | MD_WATER))) continue;
    if (s_pztest && m->type == MT_WRAITH && m->aux2 == 1) continue;   // (0.92: not from the ghost's curse: it mends faster than I rest)
    if (losing_fight(i)) { w = i; break; } }
  if (w < 0) return false;
  Mob *m = &G.mobs[w];
  if (m->state == ST_SLEEPING && distance(m->pos, h->pos) >= 4) {   // asleep, a way off: rest here till you'd win (exploring on past it wakes it: g107)
    int32_t t0 = h->time; ActResult r = game_rest(false); if (h->time == t0 && r == ACT_NONE) return false;
    s_prep_steps++; st_prep++; s_did[0] = 0; DID("hurt, a %s asleep I'd lose to (~%d of my %d HP): rest before going near it", mob_name(m->type), (int)s_lose_cost, h->HP); walk(r); return true; }
  bfs(h->pos, false); int best = -1, bd = 26;
  for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] < 1 || b_dist[c] >= bd || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || PWATER(c) || distance(c, m->pos) < 4) continue;
    bool hit; if (m->state == ST_WANDERING && ballistica(m->pos, c, &hit) == c) continue;   // (it would still see you there)
    bd = b_dist[c]; best = c; }
  if (best < 0) return false;
  int32_t t0 = h->time; ActResult r = tap_safe(best); if (r == ACT_NONE && h->time == t0) return false;
  s_prep_steps++; st_prep++; s_resting = true; s_did[0] = 0; DID("hurt, and a %s I'd lose to (~%d of my %d HP): out of its sight to rest first", mob_name(m->type), (int)s_lose_cost, h->HP); walk(r); return true;
}
// 0.52 (audit): a storage room's door is a barricade: fire opens it (its own Liquid Flame lies on the floor somewhere:
// content.c), and behind it are 3-4 items, often the floor's Strength or Upgrade. From 2-3 cells away, nothing that burns
// beside me: a known Liquid Flame, an incendiary dart, or a Wand of Firebolt.
static int st_barr, st_dmcell; static int s_kite_uid = -1, s_kite_cell = -1, s_kite_t, st_kite; static int s_thief_uid = -1, s_thief_cell = -1, s_lurk_until; static int st_thief_chase, st_lurk;
static bool carries(Mob *m) { int cp = CARRIED_POS(m->uid); for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].pos == cp) return true; return false; }
static uint8_t s_arr_qty[IK_COUNT];
static bool pot_new(int sl) { Item *it = game_slot(sl); return it->kind < IK_COUNT && item_cat(it->kind) == CAT_POTION && !item_known(it->kind) && it->qty > s_arr_qty[it->kind]; }   // 0.58: an unknown potion picked up on this floor
static int storage_barr(void) {                         // 0.58: the storage room's way in (a barricade, or a bookshelf one time in three), seen
  for (int c = 0; c < MAP_LEN; c++) { if (!BIT_GET(G.visited, c)) continue; if (G.map[c] == T_BARRICADE) return c;
    if (G.map[c] == T_BOOKSHELF && !getenv("OLDBOOKS") && !fair_on()) for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; if (R->type == RT_STORAGE && cx(c) >= R->left && cx(c) <= R->right && cy(c) >= R->top && cy(c) <= R->bottom) return c; } }
  return -1; }
static bool barricade_task(void) {
  if (getenv("OLDDUNG") || enemy_count() || G.hero.b.blind) return false;
  Hero *h = &G.hero; int src = -1; bool zap = false;
  for (int sl = 0; sl < BAG_SIZE && src < 0; sl++) { int k = h->bag[sl].kind; if ((k == IK_POT_FLAME && item_known(k)) || k == IK_INCENDIARY_DART) src = sl; }
  for (int sl = -1; sl < PACK0 + 32 && src < 0; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); if (it->kind == IK_WND_FIREBOLT && item_known(it->kind) && it->ench > 0) { src = sl; zap = true; } }
  if (src < 0 && !getenv("OLDBARR") && s_barr_tries < (getenv("OLDBOOKS") ? 2 : 3)) {   // 0.54 (13 upgrade scrolls in 8 games behind barricades): the storage room's painter leaves a Potion of Liquid Flame on the floor: an unknown potion picked up here is likely it
    int bq = 99; for (int sl = 0; sl < BAG_SIZE; sl++) { int k = h->bag[sl].kind; if (item_cat(k) != CAT_POTION || item_known(k) || k >= IK_COUNT) continue; int q = h->bag[sl].qty; if (q <= s_arr_qty[k]) continue; if (q < bq) { bq = q; src = sl; } } }
  if (getenv("BARRDBG")) { static int last = -1; if (last != s_bot_steps / 50) { last = s_bot_steps / 50; int b = -1; for (int c = 0; c < MAP_LEN && b < 0; c++) if (G.map[c] == T_BARRICADE) b = c; int np = 0; for (int sl = 0; sl < BAG_SIZE; sl++) if (item_cat(h->bag[sl].kind) == CAT_POTION) np++; printf("  BARR s%d d%d src %d barricade %d seen %d potions %d tries %d\n", s_bot_steps, G.depth, src, b, b >= 0 ? BIT_GET(G.visited, b) : -1, np, s_barr_tries); } }
  if (src < 0) return false;
  if (g_blob[BLOB_FIRE]) for (int c = 0; c < MAP_LEN; c++) if (g_blob[BLOB_FIRE][c]) return false;   // (burning already: let it)
  int b = storage_barr();   // 0.58 (owner: "Pixel Dungeon 101"): PD's barricade door is a bookshelf one time in three (Painter.paintDoors)
  if (b < 0) return false;
  bfs(h->pos, false); int best = -1, bd = 40;
  static uint8_t burn[MAP_LEN]; bool nb = !getenv("OLDBOOKS");   // 0.58: where the fire can run from the door (flammable cells joined to it); stand clear of that, even right beside the door
  if (nb) { memset(burn, 0, sizeof burn); static int16_t q[MAP_LEN]; int qh = 0, qt = 0; burn[b] = 1; q[qt++] = b;
    while (qh < qt) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && !burn[n] && (TFLAGS(G.map[n]) & F_FLAMABLE)) { burn[n] = 1; q[qt++] = n; } } } }
  for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] >= bd || (char_at(c) && c != h->pos)) continue; int dd = distance(c, b); if (dd < (nb ? 1 : 2) || dd > 3) continue;
    if (TFLAGS(G.map[c]) & F_FLAMABLE) continue; bool fl = false; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && !nb && (TFLAGS(G.map[n]) & F_FLAMABLE)) fl = true; } if (fl) continue;   /* (0.58: PD fire burns only who stands in it: beside it is safe; the spot itself just mustn't burn) */
    bool hit; int land = ballistica(c, b, &hit); if (!getenv("OLDBARR") ? ((hit && !(nb && land == b)) || !(land == b || (adjacent(land, b) && (cx(land) == cx(b) || cy(land) == cy(b))))) : land != b) continue; bd = b_dist[c]; best = c; }   /* 0.54 (audit): a throw stops a cell short of anything solid: land beside it, in line */
  if (getenv("BARRDBG")) printf("  BARR2 s%d b %d,%d best %d src %d\n", s_bot_steps, cx(b), cy(b), best, src); if (getenv("BARRDBG") && best < 0) for (int dy = -3; dy <= 3; dy++) { printf("    "); for (int dx = -4; dx <= 4; dx++) { int x = cx(b) + dx, y = cy(b) + dy; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) { printf(" .."); continue; } int c = x + y * MAP_W; bool hh; int land = ballistica(c, b, &hh); printf(" %2d%c%c", G.map[c], b_dist[c] < 0x7FFF ? 'r' : '-', land == b ? 'B' : hh ? 'h' : adjacent(land, b) ? 'a' : 'x'); } printf("\n"); }
  if (best < 0) return false;
  if (best != h->pos) { int32_t t0 = h->time; ActResult r = tap_safe(best); if (r == ACT_NONE && h->time == t0) return false; DID("to a spot to set the barricade alight"); walk(r); return true; }
  { static int bk = -1, bn; int key = cur_game * 64 + G.depth; if (bk != key) { bk = key; bn = 0; } if (++bn > 6) return false; }   /* 0.92 (game 1000: 3300 throws of an incendiary dart that never left the hand): six tries a floor */
  st_barr++; if (!zap && !item_known(h->bag[src].kind)) s_barr_tries++; DID("burn the barricade (a storage room behind it)"); walk(zap ? game_zap(src, b) : game_throw(src, b)); return true;
}
// 0.52 (audit): a weak-floor room: jump in and you land in the locked pit room below (Level.pitCell): its key, a ring or
// a piece of gear and a prize or two. The fall costs HT/3..HT/2 and a cripple: only at full health, the floor done.
static int st_pitjump;
static bool pit_jump(void) {
  Hero *h = &G.hero; if (getenv("OLDDUNG") || enemy_count() || h->HP * 100 < h->HT * 90 || game_hungry() || boss_depth(G.depth + 1)) return false;
  int wr = -1; for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_WEAK_FLOOR && room_seen(r)) wr = r;
  if (wr < 0) return false;
  Room *R = &G.rooms[wr]; bfs(h->pos, false); int best = -1, bd = 60, pit = -1;
  for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int c = x + y * MAP_W; if (!(TFLAGS(G.map[c]) & F_PIT)) continue;
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] < bd && (!char_at(n) || (n == h->pos && !getenv("OLDDUNG2")))) { bd = b_dist[n]; best = n; pit = c; } } }   /* 0.54 (audit): the hero's own cell counts (it walked to and fro) */
  if (best < 0) return false;
  if (h->pos != best) { int32_t t0 = h->time; ActResult r = tap_safe(best); if (r == ACT_NONE && h->time == t0) return false; DID("to the weak floor: down into the pit room below"); walk(r); return true; }
  st_pitjump++; s_want_jump = true; DID("jump into the chasm (the pit room's below)"); walk(game_tap_cell(pit)); return true;
}
static bool statue_worth(int i) {                     // 0.52 (audit): a statue holds an enchanted, uncursed weapon (its description names it): worth a fight if better and the duel's won
  Mob *m = &G.mobs[i]; Hero *h = &G.hero; if (h->HP * 10 < h->HT * 9) return false;
  int cp = CARRIED_POS(m->uid); for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].pos == cp && item_cat(G.heaps[k].item.kind) == CAT_WEAPON) {
    Item w = G.heaps[k].item, ba = believed(&h->armor, 0), bw = believed(&h->weapon, 0); if (fair_on() && !(w.flags & IF_LEVEL_KNOWN)) w.lvl = 0;   /* (fair: its name and enchantment show, not its level) */
    if (gear_cost(&w, &ba) < gear_cost(&bw, &ba) * 0.9 && !losing_fight(i) && !(s_pztest && (s_lose_cost * 2.5 > h->HP || h->lvl < G.depth + 3))) return true; }   // (0.92 seed 84 d4: a statue judged winnable at 40/40, lvl 5, took 35 HP in 4 blows: its weapon hits far harder than the estimate)
  return false;
}

// 0.58 (owner: "a mobility/kiting puzzle: get the Goo to stand on dry land while I stand in water, over and over, while
// dodging pump hits"; tank the last one only when it'll likely die first). GOO2=1. 1: acted, 0: hit it (the caller)
static int s_goo2_lure; static int32_t s_goo2_last = -1000000;
static bool goo2_safe(int r, Mob *m) { bool hh; return distance(r, m->pos) > 2 || ballistica(m->pos, r, &hh) != r; }   // a pumped Goo can't reach it
static bool goo2_ok(int q) { return PASSABLE(G.map[q]) && !(TFLAGS(G.map[q]) & F_AVOID) && !char_at(q) && plant_at(q) < 0; }
static bool goo2_far(int r, int g) { bool hh; return distance(r, g) > 2 || ballistica(g, r, &hh) != r; }
static bool goo2_esc(int q, int g) {                    // standing at q with Goo pumping at g: two steps take me out of its reach
  if (g < 0) return true;
  for (int a = 0; a < 8; a++) { int q1 = q + N8[a]; if (!nb_ok(q, q1) || q1 == g || !PASSABLE(G.map[q1]) || (TFLAGS(G.map[q1]) & F_AVOID) || (char_at(q1) && q1 != G.hero.pos)) continue;
    if (goo2_far(q1, g)) return true;
    for (int b = 0; b < 8; b++) { int q2 = q1 + N8[b]; if (nb_ok(q1, q2) && q2 != g && q2 != q && PASSABLE(G.map[q2]) && !(TFLAGS(G.map[q2]) & F_AVOID) && !(char_at(q2) && q2 != G.hero.pos) && goo2_far(q2, g)) return true; } }
  return false;
}
static int goo2(Mob *m) {
  Hero *h = &G.hero; int hp = h->pos, d = distance(m->pos, hp); bool gw = IS_WATER(G.map[m->pos]), hw = IS_WATER(G.map[hp]);
  if (getenv("GOODBG")) { printf("   GOO2 t%d hero %d,%d (w%d esc%d) goo %d,%d (w%d) hp %d/%d aux2 %d d %d\n", G.now / TICK, cx(hp), cy(hp), hw, goo2_esc(hp, m->pos), cx(m->pos), cy(m->pos), gw, h->HP, m->HP, m->aux2, d);
    for (int dy = -3; dy <= 3; dy++) { printf("     "); for (int dx = -5; dx <= 5; dx++) { int x = cx(hp) + dx, y = cy(hp) + dy, c = x + y * MAP_W; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) { printf(" "); continue; } printf("%c", c == hp ? '@' : c == m->pos ? 'G' : char_at(c) ? 'm' : !PASSABLE(G.map[c]) ? '#' : (TFLAGS(G.map[c]) & F_AVOID) ? '^' : IS_WATER(G.map[c]) ? '~' : '.'); } printf("\n"); } }
  s_goo2_last = G.now;
  if (m->aux2 && goo2_safe(hp, m) && m->state == ST_HUNTING) { s_did[0] = 0; DID("Goo2: pumped, I'm out of its reach: let it waste it"); walk(game_rest(false)); return 1; }
  double my = hero_dpt(m); int WW = getenv("GOO2W") ? atoi(getenv("GOO2W")) : 4;
  if (m->aux2 && d <= 2) {                                // pumped: two moves before it lands
    bool finish = m->HP <= my * 2.2 && h->HP > 30;      // (owner: tank the last one if it'll likely die first)
    if (finish && d == 1) { s_did[0] = 0; DID("Goo2: pumped, but two blows should finish it"); return 0; }
    if (h->HP * 4 < h->HT && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) { s_did[0] = 0; DID("Goo2: drink healing"); do_item(find_kind(IK_POT_HEALING), ACN_DRINK); return 1; }
    int best = -1, bs = -1;
    for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (!nb_ok(hp, q) || !goo2_ok(q)) continue;
      int sc = -1;
      if (goo2_safe(q, m)) sc = 100 + IS_WATER(G.map[q]) * 5;
      else for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && goo2_ok(r) && r != hp && goo2_safe(r, m)) { int v = 50 + IS_WATER(G.map[r]) * 5; if (v > sc) sc = v; } }
      if (sc < 0) continue; sc += distance(q, m->pos);
      if (sc > bs) { bs = sc; best = q; } }
    if (best >= 0) { s_goo2_lure = 0; s_did[0] = 0; DID("Goo2: pumped: out of its reach"); walk(tap_safe(best)); return 1; }
    if (d == 1) { s_did[0] = 0; DID("Goo2: pumped, nowhere to go: two free blows"); return 0; }
    s_did[0] = 0; DID("Goo2: pumped, nowhere to go: wait"); walk(game_rest(false)); return 1;
  }
  if (h->HP * 10 < h->HT * 4 && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) { s_did[0] = 0; DID("Goo2: drink healing"); do_item(find_kind(IK_POT_HEALING), ACN_DRINK); return 1; }
  // where it would stand to hit me at q: the cell beside q nearest to it (it walks the short way)
  #define GOO2_APPROACH(q, out) do { int _b = -1, _bd = 99; for (int _k = 0; _k < 8; _k++) { int _c = (q) + N8[_k]; if (!nb_ok((q), _c) || !PASSABLE(G.map[_c]) || (TFLAGS(G.map[_c]) & F_AVOID) || (char_at(_c) && _c != m->pos && _c != G.hero.pos)) continue; int _dd = distance(_c, m->pos); if (_dd < _bd) { _bd = _dd; _b = _c; } } (out) = _b; } while (0)
  if (d == 1) {
    bool esc_here = goo2_esc(hp, m->pos);
    int LCAP = getenv("GOO2CAP") ? atoi(getenv("GOO2CAP")) : 6; if (!gw && getenv("GOO2CAP")) s_goo2_lure = 0;   // (GOO2CAP: lures per spell in the water, counted afresh once it's out)
    if (h->ooze && !hw && getenv("GOO2WASH") && s_goo2_lure < 8) for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (nb_ok(hp, q) && goo2_ok(q) && IS_WATER(G.map[q]) && goo2_esc(q, m->pos)) { s_goo2_lure++; s_did[0] = 0; DID("Goo2: into the water: wash the ooze off"); walk(tap_safe(q)); return 1; } }
    if ((!gw || s_goo2_lure >= LCAP) && (esc_here || s_goo2_lure >= LCAP)) { s_did[0] = 0; DID("Goo2: it's on dry ground: hit it"); return 0; }
    // it stands in water (mending): step back so it has to come out onto dry ground (prefer water under me)
    int best = -1, bs = -1;
    for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (!nb_ok(hp, q) || !goo2_ok(q) || adjacent(q, m->pos)) continue;
      int a; GOO2_APPROACH(q, a); if (a < 0) continue; int sc = (IS_WATER(G.map[a]) ? 0 : 10) + IS_WATER(G.map[q]) * WW + (goo2_esc(q, a) ? 20 : 0); if (sc > bs) { bs = sc; best = q; } }
    if (best >= 0 && bs >= (esc_here ? 30 : 20)) { s_goo2_lure++; s_did[0] = 0; DID("Goo2: step back: dry ground for it, a way out for me"); walk(tap_safe(best)); return 1; }
    if (getenv("GOO2FAR") && s_goo2_lure < LCAP) {        // 0.58: no such cell beside me: a dry-approach spot a few steps off; it follows (no blows while I walk)
      bfs(hp, false); int tgt = -1, ts = -1;
      for (int q = 0; q < MAP_LEN; q++) { if (b_dist[q] < 2 || b_dist[q] > 6 || !goo2_ok(q)) continue; int a; GOO2_APPROACH(q, a); if (a < 0 || IS_WATER(G.map[a]) || !goo2_esc(q, a)) continue;
        int sc = 100 + IS_WATER(G.map[q]) * 8 - b_dist[q] * 3; if (sc > ts) { ts = sc; tgt = q; } }
      if (tgt >= 0) { bfs(tgt, false); int st = -1, sd = 0x7FFF; for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (!nb_ok(hp, q) || !goo2_ok(q) || adjacent(q, m->pos) || b_dist[q] >= sd) continue; sd = b_dist[q]; st = q; }
        if (st >= 0 && sd < 0x7FFF) { s_goo2_lure++; s_did[0] = 0; DID("Goo2: lead it out of the water"); walk(tap_safe(st)); return 1; } } }
    s_did[0] = 0; DID("Goo2: hit it (no dry ground to draw it onto)"); return 0;
  }
  // not beside me: wait where I stand in water and it must come over dry ground, or go to such a cell nearby
  { int a; GOO2_APPROACH(hp, a); int here = (hw ? WW : 0) + (a >= 0 && !IS_WATER(G.map[a]) ? 10 : 0) + (goo2_esc(hp, a) ? 20 : 0);
    int best = -1, bs = here; bfs(hp, false);
    for (int q = 0; q < MAP_LEN; q++) { if (b_dist[q] < 1 || b_dist[q] > 3 || !goo2_ok(q) || distance(q, m->pos) < 2 || distance(q, m->pos) < b_dist[q] + 1) continue;
      int aa; GOO2_APPROACH(q, aa); int sc = (IS_WATER(G.map[q]) ? WW : 0) + (aa >= 0 && !IS_WATER(G.map[aa]) ? 10 : 0) + (goo2_esc(q, aa) ? 20 : 0) - b_dist[q]; if (sc > bs) { bs = sc; best = q; } }
    if (best >= 0 && !(m->state != ST_HUNTING)) { s_did[0] = 0; DID("Goo2: to a wet spot it must reach over dry ground"); walk(tap_safe(best)); return 1; }
    if (m->state == ST_HUNTING) { if (ranged_try((int)(m - G.mobs))) return 1; s_did[0] = 0; DID("Goo2: wait for it here"); walk(game_rest(false)); return 1; }
  }
  return -1;                                             // (asleep or wandering: the usual)
}
// 0.78 (owner: "if a sleeping mob is near grass, sneak up on it to get a free surprise attack. Don't just run up"):
// Mob.Sleeping wakes with 1/(distance + stealth) a turn while it can see you; tall grass, walls and doors hide you. The
// way to a cell beside it that it sees least of (each step it could see costs SNEAKW/distance); returns the first step
static int s_sneak_expo; static int st_sneak, st_door, st_dewrest;
static int sneak_step(const Mob *m) {
  static int cost[MAP_LEN]; static int16_t from[MAP_LEN]; static uint8_t done[MAP_LEN];
  int hp = G.hero.pos, W = getenv("SNEAKW") ? atoi(getenv("SNEAKW")) : 40, tgt = -1; bool hit;
  for (int c = 0; c < MAP_LEN; c++) { cost[c] = 1 << 28; done[c] = 0; }
  cost[hp] = 0; from[hp] = -1;
  for (;;) {
    int c = -1; for (int q = 0; q < MAP_LEN; q++) if (!done[q] && cost[q] < (1 << 28) && (c < 0 || cost[q] < cost[c])) c = q;
    if (c < 0 || cost[c] > 400) break;
    done[c] = 1;
    if (c != hp && adjacent(c, m->pos)) { tgt = c; break; }
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || done[n] || !BIT_GET(G.visited, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID) || char_at(n)) continue;
      int d = distance(n, m->pos), st = 10 + (!adjacent(n, m->pos) && d <= 8 && ballistica(m->pos, n, &hit) == n ? W * 10 / d : 0);
      if (cost[c] + st < cost[n]) { cost[n] = cost[c] + st; from[n] = c; } }
  }
  if (tgt < 0) return -1;
  int steps = 0, c = tgt; while (from[c] != hp && from[c] >= 0) { c = from[c]; steps++; }
  s_sneak_expo = cost[tgt] - (steps + 1) * 10;
  return c;
}
// 0.92 (owner: "a human player simply kills the wraith"): the ghost's curse drifts at half speed, mends a point a turn,
// and shoves on a hit. So: never a standing trade. Set it alight (liquid flame, a firebloom seed, an incendiary dart) and
// walk away while it burns: it can't catch up, and each cell it closes costs it two turns, which are two throws for me.
// Bombs at two cells (the blast reaches one). Melee only while it's burning or nearly dead, and with a wall at my back.
static int s_curse_retreat_t = -1000;
static bool curse_fight(int mi) {
  Hero *h = &G.hero; Mob *m = &G.mobs[mi]; if (!s_pztest || m->type != MT_WRAITH || m->aux2 != 1 || getenv("OLDCURSE")) return false;
  int d = distance(m->pos, h->pos); bool hit; bool line = ballistica(h->pos, m->pos, &hit) == m->pos; int s;
  bool myfire = (TFLAGS(G.map[h->pos]) & F_FLAMABLE) != 0;   // (fire lands on its cell and spreads over grass only: my own cell is what matters)
  int bombs = 0, missiles = 0; for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (k == IK_BOMB) bombs += h->bag[i].qty; if (item_cat(k) == CAT_MISSILE && k != IK_BOOMERANG && k != IK_CURARE_DART && k != IK_INCENDIARY_DART) missiles += h->bag[i].qty; }
  int wmax = h->weapon.kind ? weapon_max(&h->weapon) : 3;
  // 1. beside me and not burning: set it alight (the fire bites on the next tick: it must still be on the cell, i.e. attacking, not walking)
  if (!m->b.burning && d <= 1 && !myfire) {
    if ((s = find_kind(IK_POT_FLAME)) >= 0 && item_known(IK_POT_FLAME)) { s_did[0] = 0; DID("the curse: liquid flame on it, then keep away while it burns"); walk(game_throw(s, m->pos)); return true; }
    if ((s = find_kind(IK_SEED_FIREBLOOM)) >= 0 && kn(IK_SEED_FIREBLOOM) && !(TFLAGS(G.map[m->pos]) & F_FLAMABLE)) { s_did[0] = 0; DID("the curse: a firebloom seed under it"); walk(game_throw(s, m->pos)); return true; } }
  if (!m->b.burning && d >= 2 && line && (s = find_kind(IK_INCENDIARY_DART)) >= 0) { s_did[0] = 0; DID("the curse: an incendiary dart"); walk(game_throw(s, m->pos)); return true; }
  // 2. bombs from two cells away (the blast reaches one); the last one is the Goo's unless it still has most of its health
  if (line && d >= 2 && d <= 4 && bombs && (bombs >= 2 || m->HP > 10) && (s = find_kind(IK_BOMB)) >= 0) { s_did[0] = 0; DID("the curse: a bomb from two cells"); walk(game_throw(s, m->pos)); return true; }
  // 3. beside me: trade blows only when a blow or two will finish it, or it's burning anyway and I'm sound; else a step away (it's slow)
  bool burst_left = (find_kind(IK_POT_FLAME) >= 0 && item_known(IK_POT_FLAME)) || bombs;
  bool trade = (m->HP <= wmax) || (m->b.burning && h->HP * 10 >= h->HT * 6) || (!burst_left && missiles == 0 && h->HP * 10 >= h->HT * 6);
  if (d <= 1 && !trade) {
    int best = -1, bs = -1; for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (!nb_ok(h->pos, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID) || char_at(n) || gas_at(n) || (g_blob[BLOB_FIRE] && g_blob[BLOB_FIRE][n])) continue;
      int dn = distance(n, m->pos); if (dn < 2) continue; int sc = dn * 10 + open_sides(n) * 2 - ((TFLAGS(G.map[n]) & F_FLAMABLE) ? 3 : 0); if (sc > bs) { bs = sc; best = n; } }
    if (best >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(best); if (rr != ACT_NONE || h->time != t0) { s_curse_retreat_t = s_bot_steps; s_did[0] = 0; DID("the curse: a step away (it's slow; %s)", m->b.burning ? "it burns meanwhile" : "no standing trade"); walk(rr); return true; } } }
  // 4. at two or more: throw while it closes (two turns a cell for it); burning: just let it
  if (d >= 2 && line && missiles && !(m->b.burning && m->HP <= 4)) for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) != CAT_MISSILE || k == IK_BOOMERANG || k == IK_CURARE_DART || k == IK_INCENDIARY_DART) continue;
    s_did[0] = 0; DID("the curse: throw while it closes"); walk(game_throw(i, m->pos)); return true; }
  if (d >= 2 && (m->b.burning || !trade || !line)) { s_did[0] = 0; DID(m->b.burning ? "the curse: let it burn" : "the curse: let it come (no standing trade; it's slow)"); walk(game_rest(false)); return true; }
  return false;                                         // (melee, as usual)
}
// 0.93 STEP2 (teacher: the look-ahead's chosen step beats a random one by ~0.5 floors, and no single feature explains it):
// a deterministic two-turn evaluation from what's visible, cheap enough for the watch. For staying to fight and for each
// step: the hero acts, then each foe in sight acts on its real speed (attack if beside me, else a step toward me; a fast
// one gets two actions), twice over; damage taken against damage dealt (a kill worth the blows it saves).
static double step2_eval(int hp, bool stay) {
  Hero *h = &G.hero; int ix[24], ps[24], n = 0; double mh[24];
  for (int i = 0; i < G.n_mobs && n < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state != ST_HUNTING || distance(m->pos, h->pos) > 6) continue; ix[n] = i; ps[n] = m->pos; mh[n] = m->HP; n++; }
  double taken = 0, value = 0;
  for (int turn = 0; turn < 2; turn++) {
    if (turn > 0 || stay) { int t = -1; for (int k = 0; k < n; k++) if (mh[k] > 0 && distance(ps[k], hp) <= 1 && (t < 0 || mh[k] < mh[t])) t = k;   // the hero strikes the weakest beside him
      if (t >= 0) { Mob *m = &G.mobs[ix[t]]; double d = hero_dpt(m); if (d > mh[t]) d = mh[t]; mh[t] -= d; value += d / (m->HT ? m->HT : 1) * foe_dpt(m) * 6; if (mh[t] <= 0.5) value += foe_dpt(m) * 3; } }
    for (int k = 0; k < n; k++) { if (mh[k] <= 0) continue; Mob *m = &G.mobs[ix[k]]; int acts = mob_move_cost(m) * 2 <= hero_move_cost() ? 2 : 1; double fd = foe_dpt(m);
      if (magic_shooter(m->type) && distance(ps[k], hp) > 1) { taken += fd; continue; }
      for (int a = 0; a < acts; a++) {
        if (distance(ps[k], hp) <= 1) { taken += fd / acts; continue; }
        int bq = ps[k], bd = distance(ps[k], hp); for (int j = 0; j < 8; j++) { int q = ps[k] + N8[j]; if (!nb_ok(ps[k], q) || !PASSABLE(G.map[q]) || q == hp) continue; bool occ = false; for (int o = 0; o < n; o++) if (o != k && ps[o] == q && mh[o] > 0) occ = true; if (occ) continue; int dd = distance(q, hp); if (dd < bd) { bd = dd; bq = q; } }
        ps[k] = bq; } } }
  double hpv = h->HP, risk = taken > hpv * 0.5 ? (taken - hpv * 0.5) * (getenv("STEP2K") ? atof(getenv("STEP2K")) : 6.0) : 0;   // (near the edge, a blow costs far more than its points: the look-ahead's score is mostly deaths)
  return value - taken - risk;
}
static bool fight(void) {
  Hero *h = &G.hero;
  if (soures() && !getenv("NOFIGHTMEND") && !h->b.blind && !s_rollout) { int s = find_kind(IK_SCR_UPGRADE); if (s >= 0 && item_known(IK_SCR_UPGRADE))   // 0.93 (owner: a broken piece is only mended, the level is lost): a piece a few uses from breaking mid-fight: read it now
    for (int w = -2; w <= -1; w++) { Item *it = game_slot(w); if (it->kind && it->lvl >= 2 && (it->flags & IF_LEVEL_KNOWN) && !item_broken(it) && it->dur <= 3 && game_pick_ok(PICK_UPGRADE, w)) { s_force_pick = w; s_did[0] = 0; DID("the %s is about to break: upgrade it now, before it does", w == -1 ? "weapon" : "armour"); do_item(s, ACN_READ); return true; } } }
  if (getenv("STEP2") && !s_rollout && !h->b.roots && !h->b.paralysis) {
    bool near = false, boss = false; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && BIT_GET(G.fov, o->pos) && o->state == ST_HUNTING) { if (distance(o->pos, h->pos) <= 2) near = true; if (MOB_DEFS[o->type].flags & MD_BOSS) boss = true; } }
    if (near && !boss) { double s0 = step2_eval(h->pos, true), bsv = s0 + (getenv("STEP2M") ? atof(getenv("STEP2M")) : 1.0); int bq = -1;
      for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || PWATER(q)) continue; double v = step2_eval(q, false); if (v > bsv) { bsv = v; bq = q; } }
      if (getenv("STEP2DBG")) printf("STEP2 step %d hp %d/%d stay %.1f best %.1f at %d\n", s_bot_steps, h->HP, h->HT, s0, bsv, bq);
      if (bq >= 0) { s_did[0] = 0; DID("step2: a step scores %.1f against %.1f for standing", bsv, s0); walk(tap_safe(bq)); return true; } } }
  if (getenv("CHOKE") && !s_rollout && !h->b.roots && !h->b.paralysis) {   // 0.93 (teacher: a second foe coming and the one beside me still healthy: the look-ahead steps first, 77%): to where fewer can reach me at once
    int na = 0, nh = 0, ai = -1; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP <= 0 || ally(i) || !BIT_GET(G.fov, o->pos) || o->state != ST_HUNTING || (MOB_DEFS[o->type].flags & MD_BOSS)) continue; int d = distance(o->pos, h->pos); if (d <= 1) { na++; ai = i; } if (d <= 4) nh++; }
    if (na >= 1 && nh >= 2 && G.mobs[ai].HP * 4 > G.mobs[ai].HT * 3) {
      int slots0 = 0; for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (nb_ok(h->pos, q) && PASSABLE(G.map[q]) && !(TFLAGS(G.map[q]) & F_AVOID)) slots0++; }
      int bq = -1, bsl = slots0 - 2;   // (worth a step only if it shuts at least two ways in)
      for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || PWATER(q)) continue;
        int adjf = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && o->state == ST_HUNTING && BIT_GET(G.fov, o->pos) && distance(o->pos, q) <= 1) adjf++; }
        if (adjf > na) continue;
        int sl = 0; for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && PASSABLE(G.map[r]) && !(TFLAGS(G.map[r]) & F_AVOID)) sl++; }
        if (sl <= bsl) { bsl = sl; bq = q; } }
      if (bq >= 0) { s_did[0] = 0; DID("choke: a step to where only %d can reach me (was %d)", bsl, slots0); walk(tap_safe(bq)); return true; } } }
  if ((getenv("DANCE") || getenv("TELEADJ")) && !s_rollout && !h->b.roots && !h->b.paralysis) {   // 0.93 (teacher: the look-ahead's commonest pick was a step away from the one beside me)
    int adj = -1, nadj = 0, oth = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP <= 0 || ally(i) || !BIT_GET(G.fov, o->pos) || o->state == ST_PASSIVE) continue; int d = distance(o->pos, h->pos); if (d <= 1 && o->state == ST_HUNTING) { nadj++; adj = i; } else if (d <= 6 && o->state == ST_HUNTING) oth++; }
    if (nadj == 1) { Mob *m = &G.mobs[adj]; bool boss = MOB_DEFS[m->type].flags & MD_BOSS;
      if (getenv("TELEADJ") && !boss && h->HP * 3 < h->HT * 2) { int ws = -1; for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); if (it->kind == IK_WND_TELEPORT && item_known(IK_WND_TELEPORT) && it->ench > 0) ws = sl; }
        double hd = hero_dpt(m), cost = m->HP / (hd > 0.1 ? hd : 0.1) * foe_dpt(m);
        if (ws >= 0 && cost > h->HP * 0.5) { s_did[0] = 0; DID("teleport the %s away (finishing it would cost ~%.0f of my %d)", mob_name(m->type), cost, h->HP); walk(game_zap(ws, m->pos)); return true; } }
      static int s_dance_on, s_dance_n, s_dance_g = -1; if (s_dance_g != cur_game) { s_dance_g = cur_game; s_dance_on = 0; s_dance_n = 0; }
      int lo = getenv("DANCELO") ? atoi(getenv("DANCELO")) : 50, hi = getenv("DANCEHI") ? atoi(getenv("DANCEHI")) : 75;
      bool want = h->HP * 100 < h->HT * (s_dance_on ? hi : lo);
      if (getenv("DANCEDBG")) printf("DANCE? step %d hp %d/%d want %d oth %d starving %d mag %d mc %d/%d n %d\n", s_bot_steps, h->HP, h->HT, want, oth, game_starving(), magic_shooter(m->type), mob_move_cost(m), hero_move_cost(), s_dance_n);
      if (getenv("DANCE") && want && !boss && oth == 0 && !game_starving() && !magic_shooter(m->type) && m->type != MT_SCORPIO && mob_move_cost(m) >= hero_move_cost() && s_dance_n < 300) {
        int bq = -1, bo = 0; for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || PWATER(q) || distance(q, m->pos) < 2) continue;
          int open = 0; for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && PASSABLE(G.map[r]) && !(TFLAGS(G.map[r]) & F_AVOID) && distance(r, m->pos) >= 2) open++; } if (open > bo) { bo = open; bq = q; } }
        if (bq >= 0 && bo >= 2) { s_dance_on = 1; s_dance_n++; s_did[0] = 0; DID("dance: a step away from the %s (it follows instead of striking; I mend meanwhile)", mob_name(m->type)); walk(tap_safe(bq)); return true; } }
      if (!want) s_dance_on = 0; } }
  int best = -1, bk = 1 << 30;
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i];
    if (m->HP <= 0 || ally(i) || ((!BIT_GET(G.fov, m->pos) || ignored(i)) && !(adjacent(m->pos, h->pos) && m->state == ST_HUNTING) && !(m->type == MT_GOO && getenv("GOO2") && m->state == ST_HUNTING && G.now - s_goo2_last <= 4 * TICK && distance(m->pos, h->pos) <= 5))) continue;   // (bot2: one beside you hunting you is fought, seen or not) (0.58: a Goo just dodged round a corner too)
    if (m->state == ST_PASSIVE && !adjacent(m->pos, h->pos) && !(m->type == MT_STATUE && !getenv("OLDMOBS") && statue_worth(i))) continue;   // a statue minding its own business
    if (m->state == ST_PASSIVE && m->type == MT_STATUE && (getenv("OLDMOBS") || !statue_worth(i))) continue;
    int d = distance(m->pos, h->pos), key = adjacent(m->pos, h->pos) ? ((MOB_DEFS[m->type].flags & MD_BOSS) ? -1 : m->HP) : 1000 + d * 100 + m->HP;   // a boss beside you first
    if (!getenv("OLDMOBS")) {                           // 0.52 (audit)
      bool fists = false; for (int j = 0; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && (G.mobs[j].type == MT_ROTTING_FIST || G.mobs[j].type == MT_BURNING_FIST)) fists = true;
      if (m->type == MT_YOG && fists) key = 5000 + d;      // Yog takes a quarter while the fists live, and every blow spawns a larva: the fists first
      if (m->type == MT_BURNING_FIST) key = -3 + d; else if (m->type == MT_ROTTING_FIST) key = -2 + d;
      if ((m->type == MT_THIEF || m->type == MT_BANDIT) && d <= 4) key = adjacent(m->pos, h->pos) ? -2 : 500 + d;   // a thief takes a bag, the Ankh, the healing: first
    }
    if ((MOB_DEFS[m->type].flags & MD_WATER) && !adjacent(m->pos, h->pos)) continue;   // piranhas: only when they come close
    if (m->type == MT_SPINNER && m->state == ST_FLEEING && !adjacent(m->pos, h->pos) && s_pztest && getenv("SPINNER2")) continue;   // 0.93 (source): a spinner that bit runs off laying webs until your poison is gone, then comes back: no chasing it into its webs
    if ((MOB_DEFS[m->type].flags & MD_WATER) && h->b.invis && !getenv("OLDINVIS")) continue;   // bot3: invisible in the pool: they can't see you; a blow would show you
    if (m->state != ST_HUNTING && !adjacent(m->pos, h->pos) && h->HP * 100 < h->HT * (getenv("OLDENGAGE") ? PRM[P_ENGAGE] : getenv("ENGAGEHP") ? atoi(getenv("ENGAGEHP")) : 60)) continue;   // (0.50 review g6: below 60%, let a wanderer pass; was tuned to 0)   // bot3: hurt: let it wander by
    if (m->state == ST_SLEEPING && !adjacent(m->pos, h->pos) && !(MOB_DEFS[m->type].flags & MD_BOSS) && !s_oldsleep && floor_left() && (getenv("OLDSLEEP2") || distance(m->pos, h->pos) > (getenv("SNEAKR") ? atoi(getenv("SNEAKR")) : 2))) continue;   // 0.49 (owner): one close by gets the sure first hit (asleep, it can't dodge) before walking past wakes it   // 0.48 (owner): sleepers after exploring (what's in the next room may be worth the health)
    if ((MOB_DEFS[m->type].flags & MD_BOSS) && m->state != ST_HUNTING && PRM[P_BOSSLAST] && (h->HP < h->HT || ({ bool fr = false; for (int q = 0; q < MAP_LEN && !fr; q++) if (frontier(q) && !boss_zone(q)) fr = true; fr; }))) continue;   // bot3: a sleeping boss: last, at full health
    if (key < bk) { bk = key; best = i; }
  }
  if (best < 0) return false;
  Hero *hh0 = &G.hero; (void)hh0;
  // 0.50 (review g6): fighting in toxic gas bleeds you every turn: to a clean cell beside you (beside the enemy if it can be)
  if (!getenv("OLDGASFIGHT") && !dm_hold() && g_blob[BLOB_TOXIC] && g_blob[BLOB_TOXIC][hh0->pos] && !hh0->b.roots && !(adjacent(G.mobs[best].pos, hh0->pos) && hh0->HP * 10 >= hh0->HT * (G.mobs[best].type == MT_DM300 && !getenv("OLDDM") ? 2.5 : 4))) {   // (a foe at your side: fight on above 40%)
    int bc = -1, bs = 99;
    for (int k = 0; k < 8; k++) { int c = hh0->pos + N8[k]; if (!nb_ok(hh0->pos, c) || !PASSABLE(G.map[c]) || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || gas_at(c)) continue;
      int sc = adjacent(c, G.mobs[best].pos) ? 0 : distance(c, G.mobs[best].pos); if (sc < bs) { bs = sc; bc = c; } }
    if (bc >= 0) { int32_t t0 = hh0->time; ActResult rr = game_tap_cell(bc); if (rr != ACT_NONE || hh0->time != t0) { s_did[0] = 0; DID("out of the gas, keep fighting"); walk(rr); return true; } }
  }
  Mob *m = &G.mobs[best];
  if (V) printf("  fight: %s at %d (st %d, hp %d) dist %d\n", mob_name(m->type), m->pos, m->state, m->HP, distance(m->pos, h->pos));
  DID("fight %s (its HP %d/%d, %s) %d away", mob_name(m->type), m->HP, m->HT, m->state == ST_HUNTING ? "hunting me" : m->state == ST_SLEEPING ? "asleep" : "wandering", distance(m->pos, h->pos));
  if (emergency()) return true;
  if (getenv("SNEAK") && m->state == ST_SLEEPING && !adjacent(m->pos, h->pos) && !(MOB_DEFS[m->type].flags & MD_BOSS) && !h->b.roots) {
    int c = sneak_step(m);
    if (c >= 0) { int32_t t0 = h->time; ActResult rr = game_tap_cell(c); if (rr != ACT_NONE || h->time != t0) { st_sneak++; s_did[0] = 0; DID("sneak up on the sleeping %s (seen %d)", mob_name(m->type), s_sneak_expo); walk(rr); return true; } } }
  if (getenv("TACTBOT") && m->state == ST_HUNTING && !adjacent(m->pos, h->pos) && !(MOB_DEFS[m->type].flags & MD_BOSS) && G.depth <= (getenv("TACTD") ? atoi(getenv("TACTD")) : 99)) {   // 0.78 (owner): the door trick (ovl/tgt tactics())
    int16_t tc[TARGETS_MAX]; uint8_t tk[TARGETS_MAX]; int tn = game_targets(tc, tk);
    if (tn > 0 && G.tgm.src0 == 2 && tk[0] == TK_GO && !(getenv("TACTNOWAIT") && tc[0] == h->pos)) { int32_t t0 = h->time; ActResult rr = tc[0] == h->pos ? game_rest(false) : game_tap_cell(tc[0]);
      if (rr != ACT_NONE || h->time != t0) { st_door++; s_did[0] = 0; DID(tc[0] == h->pos ? "door trick: wait out of its sight" : "door trick: through the door, out of its sight"); walk(rr); return true; } } }
  if (!getenv("OLDYOG") && G.depth == 25 && !h->b.roots) {   // 0.54 (hs3: 105-137 deaths a round to the fists): the rotting fist mends 4 a turn in water: fight it dry; the burning fist's bolt needs a line: keep out of it
    int bf = -1, rf = -1; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP <= 0) continue; if (o->type == MT_BURNING_FIST) bf = i; if (o->type == MT_ROTTING_FIST) rf = i; }
    bool hit; bool bf_line = getenv("YOGLINE") && bf >= 0 && BIT_GET(G.fov, G.mobs[bf].pos) && !adjacent(G.mobs[bf].pos, h->pos) && ballistica(G.mobs[bf].pos, h->pos, &hit) == h->pos;
    bool wet_here = IS_WATER(G.map[h->pos]); if (!wet_here) for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && IS_WATER(G.map[n])) wet_here = true; }
    bool rf_near = rf >= 0 && distance(G.mobs[rf].pos, h->pos) <= 3;
    if ((rf_near && wet_here) || bf_line) {
      bfs(h->pos, false); int bc = -1, bsc = 1 << 30;
      for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] > 6 || b_dist[c] == 0 || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || !PASSABLE(G.map[c])) continue;
        bool wet = IS_WATER(G.map[c]); for (int k = 0; k < 8 && !wet; k++) { int n = c + N8[k]; if (nb_ok(c, n) && IS_WATER(G.map[n])) wet = true; }
        bool line = bf >= 0 && ballistica(G.mobs[bf].pos, c, &hit) == c && !adjacent(G.mobs[bf].pos, c);
        if (rf_near && wet) continue; if (bf >= 0 && line && getenv("YOGLINE")) continue;
        int sc = b_dist[c] * 2 + (g_blob[BLOB_FIRE] && g_blob[BLOB_FIRE][c] ? 20 : 0); if (sc < bsc) { bsc = sc; bc = c; } }
      if (bc >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(bc); if (rr != ACT_NONE || h->time != t0) { st_lurk++; s_did[0] = 0; DID(bf_line ? "fists: out of the burning fist's line" : "fists: the rotting fist on dry ground"); walk(rr); return true; } } } }
  if ((m->type == MT_BURNING_FIST || (m->type == MT_ELEMENTAL && getenv("ELEMWATER"))) && !getenv("OLDFIST") && !IS_WATER(G.map[h->pos]) && !h->b.roots && ({ bool rn = false; if (!getenv("OLDYOG")) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_ROTTING_FIST && distance(G.mobs[i].pos, h->pos) <= 3) rn = true; !rn; })) {   // 0.53 (126 deaths): it sets all round it alight every turn: fight it standing in water (no fire there, and you're put out)
    bfs(h->pos, false); int bc = -1, bd = 6;
    for (int c = 0; c < MAP_LEN; c++) { if (!IS_WATER(G.map[c]) || b_dist[c] >= bd || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || distance(c, m->pos) > 1) continue; bd = b_dist[c]; bc = c; }
    if (bc >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(bc); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("burning fist: into the water beside it"); walk(rr); return true; } } }
  if (G.depth == 20 && !getenv("OLDKING") && !h->b.roots) {   // 0.54 (audit): the King can't raise his army while you stand on the pedestal he's going to next: take it, keep it
    int ki = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_KING) ki = i;
    if (ki >= 0 && BIT_GET(G.fov, G.mobs[ki].pos)) {
      int pl = -1, pr = -1; for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_PEDESTAL) { if (pl < 0 || cx(c) < cx(pl)) pl = c; if (pr < 0 || cx(c) > cx(pr)) pr = c; }
      static int kped = -1, kg = -1; if (kg != cur_game * 64 + G.depth) { kg = cur_game * 64 + G.depth; kped = -1; }   // (agent: once on it, never leave: his next one flips when he strikes you there)
      int P = kped >= 0 && !getenv("OLDKING2") ? kped : G.mobs[ki].aux2 ? pr : pl; if (h->pos == P) kped = P;
      if (P >= 0 && pl != pr) {
        if (h->pos != P) { bfs(h->pos, false); if (b_dist[P] <= 12 && !char_at(P)) { int32_t t0 = h->time; ActResult rr = tap_safe(P); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("King: onto the pedestal he wants next (no army from there)"); walk(rr); return true; } } }
        else if (!adjacent(m->pos, h->pos)) { if (!ranged_try(best)) { s_did[0] = 0; DID("King: hold the pedestal"); walk(game_rest(false)); } return true; } } } }
  if (m->type == MT_DM300 && s_pztest && !getenv("NODMOPEN") && dm_open((int)(m - G.mobs))) return true;
  if (m->type == MT_DM300 && s_pztest && !getenv("NODMPARA") && m->b.paralysis && !adjacent(m->pos, h->pos) && ({ bool gas = blob_at(BLOB_PARALYTIC, m->pos); for (int k = 0; k < 8 && !gas; k++) { int c = m->pos + N8[k]; if (nb_ok(m->pos, c) && blob_at(BLOB_PARALYTIC, c)) gas = true; } gas; })) {   // 0.93 (audit): held in the cloud: wait outside it (its own gas: 3+ straight steps off); in once the cloud round it is gone
    if (blob_at(BLOB_PARALYTIC, h->pos) || blob_at(BLOB_TOXIC, h->pos)) return false;   // (the gas code moves me out)
    if (!ranged_try(best)) { s_did[0] = 0; DID("DM-300 held in the paralytic cloud: wait for the cloud to thin"); walk(game_rest(false)); } return true; }
  if (m->type == MT_DM300 && !getenv("OLDDM")) {     // 0.53 (boss audit D1/D2): it repairs on dead traps and vents gas: meet it on a cell with no trap on it or beside it; hold it
    if (adjacent(m->pos, h->pos) && dm_plan() && getenv("DMP_DRINK") && h->HP <= 30) { int s = find_kind(IK_POT_HEALING); if (s >= 0 && item_known(IK_POT_HEALING)) { s_did[0] = 0; DID("DM-300: drink before its next blow (up to 29)"); do_item(s, ACN_DRINK); return true; }
      for (int i = 0; i < BAG_SIZE; i++) if (item_cat(h->bag[i].kind) == CAT_POTION && !item_known(h->bag[i].kind) && s_pot_guess[h->bag[i].kind] == IK_POT_HEALING) { s_did[0] = 0; DID("DM-300: the Sungrass brew (Healing, likely)"); do_item(i, ACN_DRINK); return true; } }
    if (!adjacent(m->pos, h->pos) && dm_plan() && getenv("DMP_ROOT") && distance(m->pos, h->pos) <= 4 && !G.herbal && m->state == ST_HUNTING) { int s = find_kind(IK_SEED_EARTHROOT); if (s >= 0) {   // 0.92 (audit): Earthroot soaks up the hero's HT in blows while you stand still: throw it beside me, step on it
        int bn = -1; for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || plant_at(q) >= 0 || heap_at(q) >= 0 || G.map[q] == T_INACTIVE_TRAP) continue; bool tr = false; for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && G.map[r] == T_INACTIVE_TRAP) tr = true; } if (tr) continue; bn = q; break; }
        if (bn >= 0) { s_onto = bn; s_did[0] = 0; DID("DM-300: Earthroot beside me, then onto it"); walk(game_throw(s, bn)); return true; } } }
    if (adjacent(m->pos, h->pos)) { int s; if (blob_at(BLOB_TOXIC, h->pos) && (s = find_kind(IK_POT_PURITY)) >= 0 && item_known(IK_POT_PURITY) && !h->b.gas_immune) { s_did[0] = 0; DID("DM-300: Purity against its gas"); do_item(s, ACN_DRINK); return true; }
      if (!getenv("OLDDMHOLD2") && m->b.paralysis <= 1 && (s = find_kind(IK_CURARE_DART)) >= 0 && s < BAG_SIZE) { st_uses[ACN_THROW]++; s_did[0] = 0; DID("DM-300: a curare dart (paralysed, every blow lands)"); walk(game_throw(s, m->pos)); return true; }   // 0.54 (audit)
      if (!getenv("OLDDMHOLD2") && !m->b.slow) for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); if (it->kind == IK_WND_SLOW && item_known(it->kind) && it->ench > 0) { s_did[0] = 0; DID("DM-300: slow it"); walk(game_zap(sl, m->pos)); return true; } } }
    else if (dm_hold()) { if (dm_plan() ? (distance(m->pos, h->pos) <= 2 && attack_wand_ready() && ranged_try(best)) : ranged_try(best)) {} else { s_did[0] = 0; DID("DM-300 is hurt: hold still, let it come (no walking over dead traps)"); walk(game_rest(false)); } return true; }
    else if (distance(m->pos, h->pos) >= 2) {
      bfs(h->pos, false); int bc = -1, bd = 9;
      for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] >= bd || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || G.map[c] == T_INACTIVE_TRAP || (char_at(c) && c != h->pos) || blob_at(BLOB_TOXIC, c)) continue;
        bool tr = false; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && G.map[n] == T_INACTIVE_TRAP) tr = true; } if (tr) continue; bd = b_dist[c]; bc = c; }
      if (bc >= 0 && bc != h->pos) { int32_t t0 = h->time; ActResult rr = tap_safe(bc); if (rr != ACT_NONE || h->time != t0) { st_dmcell++; s_did[0] = 0; DID("DM-300: to a cell with no dead trap near (it repairs on them)"); walk(rr); return true; } }
      if (bc == h->pos && !ranged_try(best)) { s_did[0] = 0; DID("DM-300: hold the trap-free cell, let it come"); walk(game_rest(false)); return true; } } }
  if (curse_fight(best)) return true;
  s_losing = m->type != MT_GOO && losing_fight(best);
  if (!(MOB_DEFS[m->type].flags & MD_WATER) && seed_tactic(best)) { s_losing = false; return true; }
  if (altar_lure(best)) { s_losing = false; return true; }
  if (s_pztest && !(getenv("NOTENGUCLOSE") && !getenv("TENGUCORNER")) && m->type == MT_TENGU && distance(m->pos, h->pos) >= 2 && !h->b.roots && !m->b.paralysis && ({ bool hit; ballistica(m->pos, h->pos, &hit) == h->pos; })) {   // 0.92 (random-seed Tengu deaths: 15 darts thrown, all dodged, while its shuriken took 40 HP): in its sight it out-shoots you and leaps away; out of its sight it must walk to you, and up close the blows land
    bfs(h->pos, false); int bc = -1, bd = getenv("TCD") ? atoi(getenv("TCD")) : 16;   // (0.92 t1: hiding first 7.33, closing in only 7.42, neither 7.36: noise; hiding is opt-in, TENGUCORNER=1)
    if (getenv("TENGUCORNER")) for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] >= bd || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || !BIT_GET(G.visited, c)) continue; bool hit; if (ballistica(m->pos, c, &hit) == c) continue; bd = b_dist[c]; bc = c; }
    if (getenv("TDBG") && !s_rollout) printf("  TDBG step %d hero %d,%d tengu %d,%d corner %d dist %d\n", s_bot_steps, cx(h->pos), cy(h->pos), cx(m->pos), cy(m->pos), bc, bd);
    if (bc >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(bc); if (rr != ACT_NONE || h->time != t0) { st_lurk++; s_lurk_until = s_bot_steps + 10; s_losing = false; s_did[0] = 0; DID("Tengu: out of its sight first, then let it come"); walk(rr); return true; } }
    if (bc < 0 && !getenv("NOTENGUCLOSE") && !(find_kind(IK_CURARE_DART) >= 0 && find_kind(IK_CURARE_DART) < BAG_SIZE && m->b.paralysis <= 0 && distance(m->pos, h->pos) <= 4)) {   // (no hiding in its arena: it stands and shoots while you come, and only leaps after a few blows up close: close in rather than trade darts it dodges)
      bfs(m->pos, false); int st = -1, sd = b_dist[h->pos];
      for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (nb_ok(h->pos, q) && b_dist[q] < sd && !char_at(q) && !(TFLAGS(G.map[q]) & F_AVOID) && G.map[q] != T_INACTIVE_TRAP && !bad_air(q)) { sd = b_dist[q]; st = q; } }
      if (st >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(st); if (rr != ACT_NONE || h->time != t0) { s_losing = false; s_did[0] = 0; DID("Tengu: close in (it dodges darts; up close the blows land)"); walk(rr); return true; } } } }
  if (m->type != MT_GOO && !(MOB_DEFS[m->type].flags & MD_WATER) && kit_fight(best)) { s_losing = false; return true; }
  s_losing = false;
  if ((MOB_DEFS[m->type].flags & MD_WATER) && s_bot_steps < s_pass_bite && h->HP > h->HT / 3 && !getenv("OLDPIRANHA")) return false;   // bot3: the only way on goes past them: keep walking
  if ((MOB_DEFS[m->type].flags & MD_WATER) && !h->b.roots && !getenv("OLDPIRANHA")) {   // bot3: piranhas: to the nearest cell out of their reach (no piranha water beside it)
    s_dry = true; bfs(h->pos, false); s_dry = false; int best = -1, bd = 30;
    for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] >= bd || PWATER(c) || char_at(c)) continue; bool near = false; for (int k = 0; k < 8 && !near; k++) { int n = c + N8[k]; if (nb_ok(c, n) && PWATER(n)) near = true; } if (!near) { bd = b_dist[c]; best = c; } }
    if (best >= 0 && best != h->pos) { int st = dry_step(best); if (st >= 0) { s_did[0] = 0; DID("out of the piranhas' reach"); walk(tap_safe(st)); return true; } }
  }
  if ((MOB_DEFS[m->type].flags & MD_WATER) && !h->b.roots) {   // piranhas: away from the water
    int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int n = game_targets(cell, kind), away = -1;
    for (int i = 0; i < n; i++) if (kind[i] == TK_GO && adjacent(cell[i], h->pos) && !IS_WATER(G.map[cell[i]])) { away = cell[i]; break; }
    if (away >= 0) { int water = 0; for (int k = 0; k < 8; k++) { int c = away + N8[k]; if (nb_ok(away, c) && mob_at(c) == best) water = 1; }
      if (!water || IS_WATER(G.map[h->pos])) { walk(tap_safe(away)); return true; } }
  }
  if (m->type == MT_GOO && !h->b.roots && !getenv("GOO_OLD")) {   // bot3: the owner's Goo script (his 90% on the challenge save)
    bool hit; bool line = ballistica(h->pos, m->pos, &hit) == m->pos; int d = distance(m->pos, h->pos);
    if (getenv("GOODBG")) printf("   GOO t%d hero %d,%d hp %d goo %d,%d hp %d aux2 %d aux %d d %d herbal %d\n", G.now / TICK, cx(h->pos), cy(h->pos), h->HP, cx(m->pos), cy(m->pos), m->HP, m->aux2, m->aux, d, G.herbal); if (getenv("GOODBG") && m->aux2) for (int dy = -3; dy <= 3; dy++) { printf("     "); for (int dx = -4; dx <= 4; dx++) { int x = cx(h->pos) + dx, y = cy(h->pos) + dy, c = x + y * MAP_W; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) { printf(" "); continue; } printf("%c", c == h->pos ? '@' : c == m->pos ? 'G' : char_at(c) ? 'm' : !PASSABLE(G.map[c]) ? '#' : (TFLAGS(G.map[c]) & F_AVOID) ? '^' : IS_WATER(G.map[c]) ? '~' : G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR ? '+' : '.'); } printf("\n"); }
    int gr = room_at(m->pos);
    // 1. open while it sleeps / comes: bombs, then darts and other missiles, then known harmful potions
    if (goo_open(best)) return true;
    if (getenv("GOO2")) { int r = goo2(m); if (r == 1) return true; if (r == 0) { walk(game_tap_cell(m->pos)); return true; } }
    // 2. it's awake: back out through the door of its room, and fight it there from the water if there is any
    int spot = -1, door = -1;
    if (gr >= 0 && m->state == ST_HUNTING && !getenv("GOO_NODOOR")) {
      bfs(h->pos, false); int bsc = 1 << 30; Room *R = &G.rooms[gr];
      for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) {
        if (x != R->left && x != R->right && y != R->top && y != R->bottom) continue;
        int c = x + y * MAP_W; if (G.map[c] != T_DOOR && G.map[c] != T_OPEN_DOOR) continue;
        for (int k = 0; k < 8; k++) { int q = c + N8[k]; if (!nb_ok(c, q) || room_at(q) == gr || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || b_dist[q] == 0x7FFF || (char_at(q) && q != h->pos)) continue;
          if (getenv("GOO_ESCSPOT") || s_pztest) { bool esc = false; for (int a = 0; a < 8 && !esc; a++) { int q1 = q + N8[a]; if (!nb_ok(q, q1) || !PASSABLE(G.map[q1]) || (TFLAGS(G.map[q1]) & F_AVOID) || room_at(q1) == gr) continue;   // 0.58: a spot with a way out of a pumped Goo's reach (two steps to 3+ from the door)
              for (int b2 = 0; b2 < 8 && !esc; b2++) { int q2 = q1 + N8[b2]; if (nb_ok(q1, q2) && PASSABLE(G.map[q2]) && !(TFLAGS(G.map[q2]) & F_AVOID) && room_at(q2) != gr && distance(q2, c) >= 3) esc = true; } } if (!esc) continue; }
          int sc = b_dist[q] * 4 - (IS_WATER(G.map[q]) ? 6 : 0) + (k >= 4 ? 2 : 0); if (sc < bsc) { bsc = sc; spot = q; door = c; } }
      }
    }
    if (spot >= 0) { if (spot != s_goo_spot) s_goo_hold = 0; s_goo_spot = spot; }
    // 3. Icecap on the doorway just before it steps there
    if (door >= 0 && h->pos == spot && distance(m->pos, door) <= 2 && m->pos != door && !char_at(door) && plant_at(door) < 0) { int sl = find_kind(IK_SEED_ICECAP); if (sl >= 0) { s_did[0] = 0; DID("Goo: Icecap on the doorway"); walk(game_throw(sl, door)); return true; } }
    // 4. pumped: no healing known and unknown potions: drink them (owner: find the healing, then take the hit); full health: take it
    if (m->aux2 && d <= 2) {
      bool kh = find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING); int up = unknown_potion();
      if (kh && h->HP < h->HT * 6 / 10) { s_did[0] = 0; DID("Goo pumps: drink healing"); do_item(find_kind(IK_POT_HEALING), ACN_DRINK); return true; }
      if (!kh && up >= 0 && (getenv("GOO_DRINK80") ? h->HP < h->HT * 8 / 10 && (IS_WATER(G.map[h->pos]) || h->HP < h->HT / 2 || getenv("GOO_DRINKANY")) : h->HP < h->HT * 35 / 100)) { s_did[0] = 0; DID("Goo pumps: try an unknown potion"); do_item(up, ACN_DRINK); return true; }
      if (!getenv("X_DODGE") && (h->HP > 25 || h->HP > h->HT * 7 / 10)) { /* take it, keep swinging */ }
      else goto goo_dodge;
    }
    if (spot >= 0 && h->pos != spot && (!adjacent(m->pos, h->pos) || (!m->aux2 && room_at(h->pos) == gr && getenv("GOO_RUNADJ")))) { int32_t t0 = h->time; ActResult rr = tap_safe(spot); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("Goo: back out through the door"); walk(rr); return true; } }
    if (spot >= 0 && h->pos == spot && !adjacent(m->pos, h->pos) && m->state == ST_HUNTING) { s_did[0] = 0; DID("Goo: wait for it at the door"); walk(game_rest(false)); return true; }
    if (h->HP < h->HT * 4 / 10 && find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) { s_did[0] = 0; DID("Goo: drink healing"); do_item(find_kind(IK_POT_HEALING), ACN_DRINK); return true; }
    if (getenv("X_DRYGOO") && adjacent(m->pos, h->pos) && !m->aux2 && IS_WATER(G.map[m->pos])) {   // X: it mends in water: step back so it has to come out onto dry ground
      static int s_dry_n, s_dry_uid = -1; if (s_dry_uid != m->uid) { s_dry_uid = m->uid; s_dry_n = 0; }
      int bq = -1, bs = -1;
      if (s_dry_n < 8) for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || adjacent(q, m->pos) || plant_at(q) >= 0) continue;
        int dry = 0, wet = 0; for (int j = 0; j < 8; j++) { int c = q + N8[j]; if (!nb_ok(q, c) || !PASSABLE(G.map[c]) || !adjacent(c, m->pos)) continue; if (IS_WATER(G.map[c])) wet++; else dry++; }
        if (wet || !dry) continue; int sc = 10 + (IS_WATER(G.map[q]) ? 3 : 0); if (sc > bs) { bs = sc; bq = q; } }
      if (bq >= 0) { s_dry_n++; s_did[0] = 0; DID("Goo: step back, out of its water"); walk(tap_safe(bq)); return true; } }
    goto goo_melee;
  }
  goo_dodge:
  if (m->type == MT_GOO && !h->b.roots && G.hero.ooze && !IS_WATER(G.map[h->pos]) && !adjacent(m->pos, h->pos) && !(m->aux2 && distance(m->pos, h->pos) <= 2)) {   // bot3: ooze: one step into water washes it off
    for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (nb_ok(h->pos, q) && IS_WATER(G.map[q]) && !char_at(q) && !adjacent(q, m->pos)) { s_did[0] = 0; DID("wash the ooze off in the water"); walk(tap_safe(q)); return true; } }
  }
  if (m->type == MT_GOO && getenv("GOODBG") && !s_rollout) printf("  GOODBG step %d hero %d,%d hp %d/%d goo %d,%d hp %d aux2 %d aux %d state %d d %d ooze %d calm %d spot %d\n", s_bot_steps, cx(h->pos), cy(h->pos), h->HP, h->HT, cx(m->pos), cy(m->pos), m->HP, m->aux2, m->aux, m->state, distance(m->pos, h->pos), h->ooze, G.now < s_goo_calm, s_goo_spot);
  if (m->type == MT_GOO && !h->b.roots) {             // bot3: the Goo plan (STRATEGY.md)
    // a pumped Goo jumps 2 cells along a straight line: step where its line to you is blocked (round a corner, behind
    // the door frame: the jump fails and the pump is wasted), else as far as you can
    if (s_pztest && !getenv("NOGOOT") && m->aux2 && distance(m->pos, h->pos) <= 2 && !G.herbal) {   // 0.92 (Goo deaths at full health: the "2-move escape" had one move: the pump began mid-step; 16-22 HP a jump): count the moves before it acts
      #define GSAFE2(c) (distance((c), m->pos) > 2 || ({ bool hh; ballistica(m->pos, (c), &hh) != (c); }))
      int mc = hero_move_cost(); int nm = m->time > h->time ? (m->time - h->time + mc - 1) / mc : 0;
      int best = -1, bs = -1;
      if (nm >= 1) for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || plant_at(q) >= 0) continue;
        int sc = GSAFE2(q) ? 100 : 0;
        if (!sc && nm >= 2) for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && PASSABLE(G.map[r]) && !(TFLAGS(G.map[r]) & F_AVOID) && !char_at(r) && GSAFE2(r)) { sc = 50; break; } }
        if (!sc) continue; sc += distance(q, m->pos) * 2 + (IS_WATER(G.map[q]) ? 1 : 0);
        if (sc > bs) { bs = sc; best = q; } }
      if (getenv("GOODBG") && !s_rollout) printf("  GOOT step %d moves %d best %d adj %d\n", s_bot_steps, nm, best, adjacent(m->pos, h->pos));
      if (best >= 0) { s_goo_calm = G.now + 3 * TICK; s_did[0] = 0; DID("Goo pumps up: out of its reach in the %d move%s I have", nm, nm == 1 ? "" : "s"); walk(game_tap_cell(best)); return true; }
      if (adjacent(m->pos, h->pos)) { s_did[0] = 0; DID("Goo pumps up: no way out in time: the free blows"); goto goo_melee; }
    }
    if (getenv("X_DODGE2") && m->aux2 && distance(m->pos, h->pos) <= 2 && !G.herbal) {   // X: a 2-move escape (out of reach, or round a corner), else take the free blows
      #define GSAFE(c) (distance((c), m->pos) > 2 || ({ bool hh; ballistica(m->pos, (c), &hh) != (c); }))
      int best = -1, bs = -1;
      for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || plant_at(q) >= 0) continue;
        int sc = GSAFE(q) ? 100 : 0;
        if (!sc) for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && PASSABLE(G.map[r]) && !(TFLAGS(G.map[r]) & F_AVOID) && !char_at(r) && GSAFE(r)) { sc = 50; break; } }
        if (!sc) continue; sc += distance(q, m->pos) * 2 + (IS_WATER(G.map[q]) ? 1 : 0);
        if (sc > bs) { bs = sc; best = q; } }
      if (best >= 0) { s_goo_calm = G.now + 3 * TICK; s_did[0] = 0; DID("Goo pumps up: 2-move escape"); walk(game_tap_cell(best)); return true; }
      if (adjacent(m->pos, h->pos)) goto goo_melee;   // no way out: two free blows before it lands
    }
    if (m->aux2 && distance(m->pos, h->pos) <= 2 && !G.herbal && !getenv("GOO_NODODGE")) {
      int best = -1, bs = -1;
      for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !PASSABLE(G.map[q]) || (TFLAGS(G.map[q]) & F_AVOID) || char_at(q) || plant_at(q) >= 0) continue;   // (not onto our own seed)
        bool hit; int16_t e = ballistica(m->pos, q, &hit); int sc = (e != q ? 20 : 0) + distance(q, m->pos) * 4 + !IS_WATER(G.map[q]);
        if (distance(q, m->pos) > 2) sc += 20;
        { int q2 = -1; for (int j = 0; j < 8; j++) { int r2 = q + N8[j]; if (nb_ok(q, r2) && PASSABLE(G.map[r2]) && !(TFLAGS(G.map[r2]) & F_AVOID) && !char_at(r2) && distance(r2, m->pos) > 2) q2 = r2; } if (q2 >= 0) sc += 10; }   // (a second step reaches 3: out of its reach)
        if (sc > bs) { bs = sc; best = q; } }
      if (best >= 0) { int p0 = h->pos; ActResult rr = tap_safe(best); if (getenv("GOODBG")) printf("   pump-dodge from %d to %d (terrain %d): r=%d now at %d\n", p0, best, G.map[best], rr, h->pos); s_goo_calm = G.now + 3 * TICK; s_did[0] = 0; DID("Goo pumps up: out of its line"); walk(rr); return true; }
    }
    // owner's plan: pull it out of its pool to a puddle in the hallway, plant Earthroot in the water under you and hold
    // that spot (herbal armour; the water washes the ooze off; Goo stands on dry ground and can't heal)
    int er = find_kind(IK_SEED_EARTHROOT);
    if ((er >= 0 || G.herbal) && getenv("GOO_ER")) {
      int groom = room_at(m->pos);
      if (G.herbal && IS_WATER(G.map[h->pos])) { if (adjacent(m->pos, h->pos)) { /* hold: fight below */ } else { if (ranged_try(best)) return true; s_did[0] = 0; DID("Goo: hold the Earthroot puddle"); walk(game_rest(false)); return true; } }
      else {
        bfs(h->pos, false); int spot = -1, sd = 99;
        for (int q = 0; q < MAP_LEN; q++) { if (b_dist[q] > 14 || !IS_WATER(G.map[q]) || char_at(q) || plant_at(q) >= 0 || heap_at(q) >= 0 || (groom >= 0 && room_at(q) == groom)) continue;
          int dry = 0, wet = 0; for (int k = 0; k < 8; k++) { int r = q + N8[k]; if (!nb_ok(q, r) || !PASSABLE(G.map[r])) continue; if (IS_WATER(G.map[r])) wet++; else dry++; }
          if (!dry) continue; int d = b_dist[q] + wet * 2; if (d < sd) { sd = d; spot = q; } }
        if (spot >= 0 && spot != h->pos) { int32_t t0 = h->time; ActResult rr = tap_safe(spot); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("Goo: to a puddle in the hallway"); walk(rr); return true; } }
        if (spot == h->pos && er >= 0 && distance(m->pos, h->pos) <= 3 && plant_at(h->pos) < 0) { s_did[0] = 0; DID("Goo: Earthroot in the puddle under me"); do_item(er, ACN_PLANT); return true; }
      }
    }
    // owner: you in the water (it washes the ooze off), Goo on dry ground (it can't heal), room to step back
    {
      bfs(h->pos, false); int spot = -1, bsc = -1000;
      for (int q = 0; q < MAP_LEN; q++) { if (b_dist[q] > 12 || char_at(q) || (TFLAGS(G.map[q]) & F_AVOID) || !PASSABLE(G.map[q]) || (distance(q, m->pos) <= 1 && q != h->pos)) continue;
        int dry = 0, wet = 0, back = 0; for (int k = 0; k < 8; k++) { int r = q + N8[k]; if (!nb_ok(q, r) || !PASSABLE(G.map[r])) continue; if (IS_WATER(G.map[r])) wet++; else dry++; if (distance(r, m->pos) > distance(q, m->pos)) back++; }
        int sc = getenv("GOO_DRY") ? (IS_WATER(G.map[q]) ? -20 : 0) - wet * 4 + (open_sides(q) <= 2 ? 6 : 0) + (back ? 4 : -6) - b_dist[q] / 3 : (IS_WATER(G.map[q]) ? 12 : 0) + dry * 2 - wet * 3 + (back ? 4 : -6) - b_dist[q] / 3;
        if (sc > bsc) { bsc = sc; spot = q; } }
      int here = -1000; { int q = h->pos, dry = 0, wet = 0, back = 0; for (int k = 0; k < 8; k++) { int r = q + N8[k]; if (!nb_ok(q, r) || !PASSABLE(G.map[r])) continue; if (IS_WATER(G.map[r])) wet++; else dry++; if (distance(r, m->pos) > distance(q, m->pos)) back++; } here = getenv("GOO_DRY") ? (IS_WATER(G.map[q]) ? -20 : 0) - wet * 4 + (open_sides(q) <= 2 ? 6 : 0) + (back ? 4 : -6) : (IS_WATER(G.map[q]) ? 12 : 0) + dry * 2 - wet * 3 + (back ? 4 : -6); }
      if (spot >= 0 && spot != h->pos && bsc > here + 3 && !adjacent(m->pos, h->pos) && (getenv("GOO_PUDDLE") || getenv("GOO_DRY"))) { int32_t t0 = h->time; ActResult rr = tap_safe(spot); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("Goo: to a puddle with dry ground for it"); walk(rr); return true; } }
    }
    if (m->state == ST_HUNTING && distance(m->pos, h->pos) >= 2 && !getenv("GOO_NOKIT")) {   // the kit, best first (owner: damage beyond hits)
      bool hit; if (ballistica(h->pos, m->pos, &hit) == m->pos) {
        static const uint8_t KIT[] = { IK_POT_PARALYTIC, IK_POT_FLAME, IK_BOMB, IK_CURARE_DART, IK_INCENDIARY_DART };
        for (unsigned k = 0; k < sizeof KIT; k++) { int sl = find_kind(KIT[k]); if (sl < 0 || sl >= BAG_SIZE) continue; if (item_cat(KIT[k]) == CAT_POTION && (!item_known(KIT[k]) || distance(m->pos, h->pos) < 3)) continue;
          s_did[0] = 0; DID("Goo: throw the saved %s", KIT[k] == IK_POT_PARALYTIC ? "paralytic gas" : KIT[k] == IK_POT_FLAME ? "liquid flame" : KIT[k] == IK_BOMB ? "bomb" : "dart"); walk(game_throw(sl, m->pos)); return true; }
      }
      if (distance(m->pos, h->pos) <= 3 && plant_at(h->pos) < 0 && heap_at(h->pos) < 0 && !IS_WATER(G.map[h->pos])) {   // a seed where it will step, then back off
        static const uint8_t SD[] = { IK_SEED_ICECAP, IK_SEED_SORROWMOSS };   // (Icecap: frozen, free hits; not Firebloom: its fire spread to the hero)
        for (unsigned k = 0; k < sizeof SD; k++) { int sl = find_kind(SD[k]); if (sl < 0) continue; s_did[0] = 0; DID("Goo: plant a seed in its path"); s_step_back = h->pos; do_item(sl, ACN_PLANT); return true; }
      }
    }
    static int s_goo_wait; if (adjacent(m->pos, h->pos)) s_goo_wait = 0;
    if (m->state == ST_HUNTING && !adjacent(m->pos, h->pos) && s_goo_wait++ < 15) { if (ranged_try(best)) return true; s_did[0] = 0; DID("Goo: wait for it at the doorway"); walk(game_rest(false)); return true; }
  }
  goo_melee:
  if (m->type == MT_WRAITH && (m->HT <= 1 || !getenv("OLDMOBS")) && attack_wand_ready() && ranged_try(best)) return true;   // bot2: wands never miss; wraiths dodge melee
  // 0.52 (owner: the door kite, "by far the most consistent wraith strategy"): a hunter that loses sight of you forgets it
  // saw you (game.c mob_act: MF_SEEN off when you're out of its view as it acts), and an unseeing creature doesn't dodge
  // (attack: def 0). So: through a door, wait beside it; the door shuts behind you; it steps into the doorway blind and
  // your blow can't miss. A 1-HP wraith dies to it. (Not for the fast ones: they step in and strike in one turn.)
  bool kfast = MOB_DEFS[m->type].move_cost < hero_move_cost();   // (owner: almost every fight goes through a door: the fast ones too: in the doorway they come one at a time)
  if (hunt2() && distance(m->pos, h->pos) >= 2 && boom_slot() >= 0 && !(m->type == MT_TENGU || m->type == MT_EYE || m->type == MT_SCORPIO || m->type == MT_ACIDIC) && !h->b.blind) {   // 0.54: the boomerang every turn while it comes (it's back in the bag at once)
    bool hit; if (ballistica(h->pos, m->pos, &hit) == m->pos) { st_boom++; st_uses[ACN_THROW]++; s_did[0] = 0; DID("boomerang at the %s, %d away", mob_name(m->type), distance(m->pos, h->pos)); walk(game_throw(boom_slot(), m->pos)); return true; } }
  if (s_pztest && !getenv("NOCLOSEIN") && (m->type == MT_SHAMAN || m->type == MT_WARLOCK) && m->state == ST_HUNTING && distance(m->pos, h->pos) >= 2 && !h->b.roots) {   // 0.92 (seed 84 d7: ten turns of dodged darts while its lightning took 28 HP): it shoots and stands; close in, it dies in two blows
    bfs(m->pos, false); int st = -1, sd = b_dist[h->pos];
    for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (nb_ok(h->pos, q) && b_dist[q] < sd && !char_at(q) && !(TFLAGS(G.map[q]) & F_AVOID) && !bad_air(q)) { sd = b_dist[q]; st = q; } }
    if (st >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(st); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("close in on the %s (it shoots from where it stands)", mob_name(m->type)); walk(rr); return true; } } }
  if (!getenv("OLDKITE") && distance(m->pos, h->pos) >= 3 && ranged_try(best)) return true;   // (review: missiles first while it's far: the huntress's boomerang, darts)
  if (getenv("KITEDBG") && !s_rollout) printf("KITE s%d m%d st%d kfast %d adj %d seen %d roots %d\n", s_bot_steps, m->type, m->state, kfast, adjacent(m->pos, h->pos), (m->flags & MF_SEEN) != 0, h->b.roots);
  if (!getenv("OLDKITE") && !kfast && m->state == ST_HUNTING && !h->b.roots && !(s_pztest && m->type == MT_WRAITH && m->aux2 == 1) &&   /* 0.92: not the ghost's curse: it mends a point a turn while it hunts for you */ !(adjacent(m->pos, h->pos) && !(m->flags & MF_SEEN)) && !(MOB_DEFS[m->type].flags & (MD_BOSS | MD_WATER)) &&
      (getenv("KITE_ALL") ? 1 : getenv("KITE_HARD") ? (m->type == MT_WRAITH || phit(hero_attack_skill(NULL, m->pos), mst(m, STAT_DEF, MOB_DEFS[m->type].def)) < 0.5) : 1) && !magic_shooter(m->type) && m->type != MT_SCORPIO && m->type != MT_ACIDIC && m->type != MT_MIMIC && m->type != MT_STATUE) {
    if (s_kite_uid == m->uid && h->pos == s_kite_cell && s_bot_steps - s_kite_t < 25 && !adjacent(m->pos, h->pos)) { s_did[0] = 0; DID("door kite: beside the door, out of its sight; it comes through blind"); walk(game_rest(false)); return true; }
    if (s_kite_uid != m->uid || s_bot_steps - s_kite_t >= 25) {
      bfs(h->pos, false); int bd = -1, ba = -1, bs = 99;
      for (int D = 0; D < MAP_LEN; D++) { if ((G.map[D] != T_DOOR && G.map[D] != T_OPEN_DOOR) || !BIT_GET(G.visited, D) || b_dist[D] > 6 || char_at(D)) continue;
        int dm = distance(m->pos, D); if (getenv("KITEDBG") && !s_rollout) printf("  door %d,%d bd %d dm %d\n", cx(D), cy(D), b_dist[D], dm); if ((kfast ? 2 * b_dist[D] : b_dist[D]) > dm || (b_dist[D] == dm && !adjacent(m->pos, h->pos))) continue;   // (I'm through first: one at my heels only follows, it can't strike while it keeps up)
        for (int k = 0; k < 8; k++) { int A = D + N8[k]; if (!nb_ok(D, A) || !PASSABLE(G.map[A]) || G.map[A] == T_DOOR || G.map[A] == T_OPEN_DOOR || (TFLAGS(G.map[A]) & F_AVOID) || char_at(A) || IS_WATER(G.map[A]) || b_dist[A] > 10) continue;
          if (distance(A, m->pos) <= dm) continue;          // the far side of the door from it
          { bool unk = false, foe = false; int ur = s_kite2 ? 1 : 2; for (int y = -ur; y <= ur; y++) for (int x = -ur; x <= ur; x++) { int q = A + x + y * MAP_W; if (cx(A) + x < 0 || cx(A) + x >= MAP_W || q < 0 || q >= MAP_LEN) continue; if (!BIT_GET(G.visited, q)) unk = true; }   // (owner: not into the unexplored, nor where danger waits; 0.91 owner, "the safe door right there": only the cells round my spot)
            for (int j = 0; j < G.n_mobs; j++) { Mob *o = &G.mobs[j]; if (o->HP > 0 && !ally(j) && o->state != ST_PASSIVE && o != m && o->state != ST_HUNTING && distance(o->pos, A) <= 6 && (!s_kite2 || distance(o->pos, A) < distance(o->pos, D) || distance(o->pos, A) <= 2)) { foe = true; if (getenv("KITEDBG") && !s_rollout) printf("      foe type %d st %d at %d,%d dA %d dD %d\n", o->type, o->state, cx(o->pos), cy(o->pos), distance(o->pos, A), distance(o->pos, D)); } }   // (0.91: only one on my side of the door, or right by my spot)
            if (getenv("KITEDBG") && !s_rollout) printf("    A %d,%d unk %d foe %d\n", cx(A), cy(A), unk, foe); if (unk || foe || (TFLAGS(G.map[A]) & F_AVOID)) continue; }
          bool hit; if (ballistica(m->pos, A, &hit) == A) continue;   // (it mustn't see me there past the door frame)
          if (b_dist[A] < bs) { bs = b_dist[A]; bd = D; ba = A; } } }
      if (ba >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(ba); if (rr != ACT_NONE || h->time != t0) { s_kite_uid = m->uid; s_kite_cell = ba; s_kite_t = s_bot_steps; st_kite++; s_did[0] = 0; DID("door kite: through the door, to wait beside it"); walk(rr); return true; } } (void)bd; }
    else if (h->pos != s_kite_cell) { int32_t t0 = h->time; ActResult rr = tap_safe(s_kite_cell); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("door kite: to my spot beside the door"); walk(rr); return true; } }
  }
  if (adjacent(m->pos, h->pos)) {
    if (h->b.charm && m->uid == G.charm_uid) {
      if (!getenv("OLDMOBS")) {                         // 0.52 (audit): charm only stops the blow: zap, throw, or step away (it keeps hitting you while you rest)
        if (ranged_try(best)) return true;
        int bc = -1; for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c) && PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & F_AVOID) && !char_at(c) && !adjacent(c, m->pos)) { bc = c; break; } }
        if (bc >= 0) { s_did[0] = 0; DID("charmed: away from the charmer"); walk(game_tap_cell(bc)); return true; } }
      walk(game_rest(false)); return true; }
    if (!getenv("OLDMOBS") && m->type == MT_FETID_RAT && !h->b.roots) {   // each blow on it lets out paralytic gas: out of the cloud before the next
      bool gas = false; for (int k = 0; k < 9; k++) { int c = k < 8 ? h->pos + N8[k] : h->pos; if ((k == 8 || nb_ok(h->pos, c)) && blob_at(BLOB_PARALYTIC, c)) gas = true; }
      if (gas) { int bc = -1, bd = -1; for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (!nb_ok(h->pos, c) || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID) || char_at(c) || blob_at(BLOB_PARALYTIC, c)) continue; int dd = distance(c, m->pos); if (dd > bd) { bd = dd; bc = c; } }
        if (bc >= 0) { s_did[0] = 0; DID("fetid rat: out of its gas"); walk(game_tap_cell(bc)); return true; } } }
    if (MOB_DEFS[m->type].flags & MD_BOSS) {          // can the boss be worn down?
      if (m->uid != s_boss_uid || m->HP < s_boss_min) { s_boss_uid = m->uid; s_boss_min = m->HP; s_boss_hits = 0; }
      else if (++s_boss_hits == 250) viol("BOSS_UNKILLABLE", "%s: 250 blows in a row and its HP stays at %d/%d or more", mob_name(m->type), s_boss_min, m->HT);
    }
    if ((m->type == MT_TENGU || ((m->type == MT_ROTTING_FIST || m->type == MT_BURNING_FIST) && !getenv("OLDYOG"))) && !getenv("OLDTENGU2") && m->b.paralysis <= 1) { int cs = find_kind(IK_CURARE_DART); if (cs >= 0 && cs < BAG_SIZE) { st_uses[ACN_THROW]++; s_did[0] = 0; DID("Tengu: a curare dart (paralysed it can't leap, every blow lands)"); walk(game_throw(cs, m->pos)); return true; } }
    if (hunt2() && boom_slot() >= 0 && !h->b.blind && boom_dpt_vs(mst(m, STAT_DEF, MOB_DEFS[m->type].def), mst(m, STAT_DR, MOB_DEFS[m->type].dr), 0, true) > hero_dpt(m) * 1.15) { st_boom++; st_uses[ACN_THROW]++; s_did[0] = 0; DID("boomerang point-blank at the %s (beats my blade)", mob_name(m->type)); walk(game_throw(boom_slot(), m->pos)); return true; }
    int32_t t = h->time; ActResult r = tap_safe(m->pos);
    if (r == ACT_NONE && h->time == t) { ignore_mob(best, 5); walk(game_rest(false)); return true; }
    walk(r); return true;
  }
  if (h->b.roots) { if (!ranged_try(best)) walk(game_rest(false)); return true; }
  if (ranged_try(best)) return true;                    // bot2: darts and wands every time it comes, not 2 in 3
  if (!getenv("OLDMOBS") && (((m->type == MT_SCORPIO || m->type == MT_ACIDIC) && !getenv("SCORPCHASE")) || m->type == MT_EYE || (m->type == MT_TENGU && !getenv("OLDTENGU")) || (m->type == MT_WARLOCK && getenv("WARLURK"))) && distance(m->pos, h->pos) >= 2) {   /* 0.93 test (audit): a warlock's bolt needs a clear line: round a corner it has to come to melee */   /* (0.53: Tengu shoots whenever it sees you and leaps when it can see where it's going; out of its sight it walks round to you, blind: a sure blow) */   // 0.52 (audit): it shoots in line and backs off when close (a scorpio cripples): out of its line, round a corner; it comes, you strike
    bfs(h->pos, false); int bc = -1, bd = 6;
    for (int c = 0; c < MAP_LEN; c++) { if (b_dist[c] >= bd || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID)) continue; bool hit; if (ballistica(m->pos, c, &hit) == c) continue; bd = b_dist[c]; bc = c; }
    if (bc >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(bc); if (rr != ACT_NONE || h->time != t0) { st_lurk++; s_lurk_until = s_bot_steps + 10; s_did[0] = 0; DID("out of the %s's line: wait round the corner", mob_name(m->type)); walk(rr); return true; } } }
  { int s = find_kind(IK_HONEYPOT); if (s >= 0 && s < BAG_SIZE && distance(m->pos, h->pos) <= 3 && rand() % 2) { st_uses[ACN_THROW]++; walk(game_throw(s, m->pos)); return true; } }
  // bot3: a hunting enemy is coming and I stand in the open: first to a choke point (a door, a corridor) close by that
  // isn't toward it, so they come one at a time (the look-ahead kept finding this)
  bool ranged = m->type == MT_SHAMAN || m->type == MT_WARLOCK || m->type == MT_EYE || m->type == MT_SCORPIO || m->type == MT_ACIDIC || m->type == MT_GOO || m->type == MT_YOG || m->type == MT_BURNING_FIST || m->type == MT_KING || m->type == MT_TENGU;
  if (m->state == ST_HUNTING && distance(m->pos, h->pos) >= 2 && open_sides(h->pos) > 2 && PRM[P_CHOKE] && (!ranged || s_oldchoke)) {   // (0.47n: not from one that shoots: Tengu had the hero to and fro under its shuriken)
    bfs(h->pos, false); int spot = -1, sd = 99;
    for (int q = 0; q < MAP_LEN; q++) { if (b_dist[q] < 1 || b_dist[q] > PRM[P_CHOKE] || char_at(q) || (TFLAGS(G.map[q]) & F_AVOID) || (PWATER(q))) continue;
      if (open_sides(q) > 2 || distance(q, m->pos) < distance(h->pos, m->pos)) continue;
      if (b_dist[q] < sd) { sd = b_dist[q]; spot = q; } }
    if (spot >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(spot); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("to a choke point before it arrives"); walk(rr); return true; } }
  }
  if (!getenv("OLDSTAIRREST") && h->pos == G.entrance && G.depth > 1 && h->HP * 10 < h->HT * 5 && !adjacent(m->pos, h->pos) && !boss_depth(G.depth - 1) && (getenv("OLDQBUG") || !game_starving())) {   /* 0.54: starving, rest heals nothing (r10 lockout) */   // 0.50: low, on the stairs up, it isn't beside you yet: up and away, rest there
    int32_t t0 = h->time; ActResult rr = game_tap_cell(h->pos); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("up the stairs, away from it, to rest"); walk(rr); return true; } }
  // 0.49 (owner): one that runs twice as fast as you walk, a cell short of you: a step back. Waiting, it closes and
  // strikes in the same turn; a step back, it spends both its moves closing and you strike first (not when another
  // one would be beside you there, nor onto a trap or water)
  if (!ranged && !getenv("OLDFAST") && m->state == ST_HUNTING && distance(m->pos, h->pos) == 2 && !m->b.paralysis && !m->b.roots && !m->b.frost &&
      MOB_DEFS[m->type].move_cost * 2 <= hero_move_cost() && !h->b.roots) {
    int bc = -1, bo = -1;
    for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (!nb_ok(h->pos, c) || !PASSABLE(G.map[c]) || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || IS_WATER(G.map[c]) || distance(c, m->pos) < 3) continue;
      bool near = false; for (int j = 0; j < G.n_mobs; j++) { Mob *o = &G.mobs[j]; if (j != best && o->HP > 0 && !ally(j) && o->state == ST_HUNTING && distance(o->pos, c) <= 2) near = true; }
      if (near) continue;
      int os = open_sides(c); if (bc < 0 || os < bo) { bc = c; bo = os; } }   // (the narrowest such cell: a doorway's better)
    if (bc >= 0) { int32_t t0 = h->time; ActResult rr = game_tap_cell(bc); if (rr != ACT_NONE || h->time != t0) { st_fastback++; s_did[0] = 0; DID("step back from a fast one: it closes, you strike first"); walk(rr); return true; } }
  }
  // 0.49 (owner): two or more beside you: back into a narrow spot none of them touches (they spend the turn following,
  // and there only one at a time can reach you)
  if (!getenv("OLDSURR") && !h->b.roots) {
    int adj = 0; for (int j = 0; j < G.n_mobs; j++) { Mob *o = &G.mobs[j]; if (o->HP > 0 && !ally(j) && o->state == ST_HUNTING && adjacent(o->pos, h->pos)) adj++; }
    if (adj >= 2 && open_sides(h->pos) > 2) {
      int bc = -1, bo = 9;
      for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (!nb_ok(h->pos, c) || !PASSABLE(G.map[c]) || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || IS_WATER(G.map[c])) continue;
        bool touch = false; for (int j = 0; j < G.n_mobs; j++) { Mob *o = &G.mobs[j]; if (o->HP > 0 && !ally(j) && o->state == ST_HUNTING && adjacent(o->pos, c)) touch = true; }
        int os = open_sides(c); if (!touch && os <= 2 && os < bo) { bo = os; bc = c; } }
      if (bc >= 0) { int32_t t0 = h->time; ActResult rr = game_tap_cell(bc); if (rr != ACT_NONE || h->time != t0) { st_surr++; s_did[0] = 0; DID("%d on me: back into the narrow spot", adj); walk(rr); return true; } }
    }
  }
  // let it come to you, unless it hits from afar or won't come
  if (!ranged && m->state == ST_HUNTING && distance(m->pos, h->pos) <= 3 && s_wait_n < PRM[P_WAITN]) { s_wait_n++; walk(game_rest(false)); return true; }
  s_wait_n = 0;
  int16_t p0 = h->pos; int32_t t0 = h->time;
  ActResult r = tap_safe(m->pos);
  if (r == ACT_NONE && h->pos == p0 && h->time == t0) { ignore_mob(best, 40); return false; }   // no way there
  walk(r);
  if (!getenv("OLDMOBS") && (m->type == MT_THIEF || m->type == MT_BANDIT) && m->HP > 0 && carries(m)) { s_thief_uid = m->uid; s_thief_cell = m->pos; }   // 0.52 (audit): what it took, it keeps: after it
  else
  if ((h->pos == p0 && h->time == t0) || (m->HP > 0 && !BIT_GET(G.fov, m->pos))) ignore_mob(best, 40);   // no way there, or the way round loses it
  return true;
}
// a quest NPC worth talking to now
static bool npc_wanted(int i) {
  Quests *q = &G.q; int t = G.mobs[i].type;
  switch (t) {
    case MT_GHOST: if (s_pztest && !getenv("NOCURSE2") && q->ghost_alt == 2 && q->ghost == QS_SPAWNED && (G.journal[J_GHOST] & (1u << G.depth)) && !curse_go()) return false;   // 0.92 (seed 84: ten talks, ten "no"s): heard him once; back only when the plan says the curse is on
      return q->ghost == QS_SPAWNED || (q->ghost_alt == 2 && q->ghost != QS_GIVEN && (getenv("OLDQBUG") || q->ghost == QS_DONE || ((attack_wand_ready() || curse_go()) && G.hero.HP >= G.hero.HT * 9 / 10))) || (q->ghost == QS_GIVEN && find_kind(q->ghost_alt ? IK_RAT_SKULL : IK_ROSE) >= 0) || (q->ghost == QS_DONE && (q->ghost_alt == 2 || find_kind(q->ghost_alt ? IK_RAT_SKULL : IK_ROSE) >= 0));
    case MT_WANDMAKER: { static const uint8_t W[3] = { IK_SEED_ROTBERRY, IK_CORPSE_DUST, IK_PHANTOM_FISH }; return q->wand == QS_SPAWNED || (q->wand == QS_GIVEN && find_kind(W[q->wand_alt % 3]) >= 0); }
    case MT_BLACKSMITH: {
      if (q->smith == QS_SPAWNED) return true;
      if (q->smith == QS_GIVEN) { int s = G.hero.weapon.kind == IK_PICKAXE ? -1 : find_kind(IK_PICKAXE); if (s == -100 || (s < 0 && s != -1)) return false;
        if (q->smith_alt) return (game_slot(s)->flags & IF_BLOODY) != 0; s = find_kind(IK_DARK_GOLD); return s >= 0 && game_slot(s)->qty >= 15; }
      if (q->smith == QS_DONE) { for (int a = -2; a < BAG_SIZE; a++) for (int b = a + 1; b < BAG_SIZE; b++) { Item *x = game_slot(a), *y = game_slot(b);
          if (x->kind && x->kind == y->kind && item_upgradable(x->kind) && item_identified(x) && item_identified(y) && !(x->flags & IF_CURSED) && !(y->flags & IF_CURSED) && x->lvl >= 0 && y->lvl >= 0) return true; } }
      return false; }
    case MT_IMP: { int s = find_kind(IK_DWARF_TOKEN), n = s >= 0 ? game_slot(s)->qty : 0; return q->imp == QS_SPAWNED || (q->imp == QS_GIVEN && (n >= 8 || (!q->imp_alt && n >= 6))); }
    case MT_SHOPKEEPER: case MT_IMP_SHOP: {
      if (s_shop_sold > 6) return false;
      for (int k = 0; k < BAG_SIZE; k++) { Item *it = &G.hero.bag[k]; int c = item_cat(it->kind);
        if (it->kind && game_pick_ok(PICK_SELL, k) && !(s_toe && toe_keep(k)) && (sell_extra(it) || (c == CAT_ARMOR && armor_score(it) <= armor_score(&G.hero.armor)) || (c == CAT_WEAPON && it->kind != IK_PICKAXE && weapon_score(it) <= weapon_score(&G.hero.weapon)))) return true; }
      return false; }
  }
  return false;
}
static int food_budget(void);
static bool sell_extra(const Item *it) {               // 0.93 SHOP2: more to sell: known-bad rings and wands, a known Scroll of Challenge
  int k = it->kind, c = item_cat(k); if (!getenv("SHOP2") || !s_pztest) return false;
  if ((c == CAT_RING || c == CAT_WAND) && (cursed_known(it) || ((it->flags & IF_LEVEL_KNOWN) && it->lvl < 0))) return true;
  if (k == IK_SCR_CHALLENGE && item_known(k)) return true;
  if (k == IK_PICKAXE && G.q.smith != QS_GIVEN) return false;
  return false;
}
static int shop_price(const Item *it) {                 // 0.93 SHOP2: what the shopkeeper asks (Item.price x 5 x (depth / 5 + 1)), from what the player sees
  int k = it->kind, c = item_cat(k), p = 20;
  static const uint8_t PP[12] = { 30, 80, 40, 40, 100, 40, 35, 35, 50, 40, 200, 50 }, SP[12] = { 30, 25, 40, 30, 40, 15, 50, 50, 80, 15, 15, 15 };
  if (c == CAT_POTION) p = item_known(k) ? PP[k - IK_FIRST_POTION] : 20; else if (c == CAT_SCROLL) p = item_known(k) ? SP[k - IK_FIRST_SCROLL] : 15;
  else if (c == CAT_WEAPON) p = 20 * (1 << (item_tier(k) - 1)); else if (c == CAT_ARMOR) p = 10 * (1 << (item_tier(k) - 1));
  else if (k == IK_ANKH || k == IK_SEED_POUCH || k == IK_SCROLL_HOLDER || k == IK_WAND_HOLSTER) p = 50; else if (k == IK_WEIGHTSTONE) p = 40; else if (k == IK_TORCH) p = 10; else if (k == IK_OVERPRICED) p = 20;
  return p * 5 * (G.depth / 5 + 1);
}
static int shop_value(const Item *it) {                 // 0.93 SHOP2 (owner: shops replenish the valuable reserves): what a ware is worth to this hero now
  int k = it->kind, c = item_cat(k); Hero *h = &G.hero;
  if (k == IK_POT_HEALING) return 1000;
  if ((k == IK_POT_STRENGTH || k == IK_SCR_UPGRADE || k == IK_POT_MIGHT) && item_known(k)) return 2000;
  if (is_food(k)) return food_budget() < 700 ? 600 : 150;
  if (k == IK_ANKH) return find_kind(IK_ANKH) >= 0 ? 0 : G.depth >= 11 ? 500 : 250;
  if (k == IK_WEIGHTSTONE) return h->weapon.kind && item_cat(h->weapon.kind) == CAT_WEAPON ? (h->weapon.lvl >= 3 ? 300 : 120) : 0;
  if (c == CAT_POTION && !item_known(k)) return item_known(IK_POT_HEALING) ? 60 : 250;   // (Healing unknown: 3 in 10 unknown potions are it)
  if (k == IK_SEED_POUCH || k == IK_SCROLL_HOLDER || k == IK_WAND_HOLSTER) return 120;
  if (k == IK_TORCH) return G.depth >= 21 && find_kind(IK_TORCH) < 0 ? 200 : 0;
  if (c == CAT_ARMOR && armor_score(it) > armor_score(&h->armor) + 2) return 400;
  return 0;
}
static bool want_buy2(int hsel) {                       // greedy by worth: the wares worth buying that the gold covers, best first
  int idx[32], n = 0; for (int i = 0; i < G.n_heaps && n < 32; i++) if (G.heaps[i].type == HEAP_FOR_SALE && shop_value(&G.heaps[i].item) >= 100) idx[n++] = i;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (shop_value(&G.heaps[idx[b]].item) > shop_value(&G.heaps[idx[a]].item)) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
  int gold = G.hero.gold;
  for (int j = 0; j < n; j++) { int pr = shop_price(&G.heaps[idx[j]].item); if (pr <= gold) { if (idx[j] == hsel) return true; gold -= pr; } else if (idx[j] == hsel) return false; }
  return false;
}
static bool want_buy(int h) {
  Item *it = &G.heaps[h].item; int k = it->kind;
  if (s_pztest && getenv("SHOP2")) return want_buy2(h);
  if (G.hero.gold < 60) return false;
  if (s_pztest && !getenv("OLDAFFORD") && G.hero.gold < shop_price(it)) return false;   // 0.93: the price is on the tag
  if (getenv("IDB") && item_cat(k) == CAT_POTION && !item_known(k) && idb_p(k, IK_POT_HEALING) >= 0.5) return true;   // 0.93 IDB (owner): probably Healing, at the unknown price
  if (getenv("IDB") && item_cat(k) == CAT_SCROLL && !item_known(k) && idb_p(k, IK_SCR_UPGRADE) >= 0.5) return true;
  if (((k == IK_POT_HEALING || k == IK_SCR_UPGRADE || k == IK_POT_STRENGTH) && (!fair_on() || item_known(k))) || is_food(k) || k == IK_ANKH || k == IK_SEED_POUCH || k == IK_SCROLL_HOLDER || k == IK_WAND_HOLSTER) return true;   /* (fair: an unknown one by its odds, IDB) */
  if (s_toe && (item_cat(k) == CAT_ARMOR || item_cat(k) == CAT_WEAPON)) return toe_buy(h);   // 0.73 TOE: by the plan
  if (item_cat(k) == CAT_ARMOR && armor_score(it) > armor_score(&G.hero.armor) + 2) return true;
  return false;
}
static uint32_t s_npc_seen; static int s_npc_d = -1, s_npc_g = -1;   // 0.93 FAIR: the quest-givers seen on this floor (a player can't know one's there before)
static bool npc_known(int mt, int st) { if (!fair_on() || st >= QS_GIVEN) return true; if (s_npc_d != G.depth || s_npc_g != cur_game) { s_npc_d = G.depth; s_npc_g = cur_game; s_npc_seen = 0; }
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos) && G.mobs[i].type < 32) s_npc_seen |= 1u << G.mobs[i].type; return (s_npc_seen >> mt) & 1; }
static bool quest_on_floor(void) {                      // work on this floor's quest before leaving
  Quests *q = &G.q; int d = G.depth; if (s_explab) return false;   // (stage 1: no quests)
  if (q->ghost_depth == d && q->ghost && q->ghost < QS_REWARDED && npc_known(MT_GHOST, q->ghost)) return true;
  if (q->wand_depth == d && q->wand && q->wand < QS_REWARDED && npc_known(MT_WANDMAKER, q->wand)) return true;
  if (q->smith_depth == d && q->smith && q->smith < QS_DONE && npc_known(MT_BLACKSMITH, q->smith)) return true;
  if (q->imp_depth == d && q->imp && q->imp < QS_REWARDED && npc_known(MT_IMP, q->imp)) return true;
  return false;
}
static bool wait_quest(void) {                          // 0.50 (review g6): quests that need monsters to come: the ghost's (rose, fetid rat), the smith's bat blood
  Quests *q = &G.q; int d = G.depth; if (s_explab) return false;
  if (q->ghost_depth == d && q->ghost && q->ghost < QS_REWARDED && npc_known(MT_GHOST, q->ghost) && !(s_pztest && !getenv("NOCURSE2") && q->ghost_alt == 2 && q->ghost >= QS_GIVEN)) return true;   // (0.92: the curse needs no monsters to come: the curse plan runs it)
  if (q->smith_depth == d && q->smith == QS_GIVEN && q->smith_alt) return true;
  return false;
}
static int quest_task(void) {                           // a cell to go to for the quest, -1 none; may act itself (returns -2)
  Quests *q = &G.q; Hero *h = &G.hero; int d = G.depth;
  if (q->smith_depth == d && q->smith == QS_GIVEN && !q->smith_alt) {   // dark gold: mine the veins
    int pk = h->weapon.kind == IK_PICKAXE ? -1 : find_kind(IK_PICKAXE), g = find_kind(IK_DARK_GOLD);
    if (h->weapon.kind != IK_PICKAXE && pk < 0) return -1;
    if (g >= 0 && game_slot(g)->qty >= 15) return -1;
    for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c) && G.map[c] == T_WALL_DECO) { st_uses[ACN_MINE]++; do_item(pk, ACN_MINE); return -2; } }
    bfs(h->pos, false); int best = -1, bd = 9999;
    for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < bd && s_black[c] <= s_bot_steps && G.map[c] != T_ALCHEMY && c != G.entrance && c != G.exit) for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && G.map[n] == T_WALL_DECO && BIT_GET(G.visited, n)) { best = c; bd = b_dist[c]; break; } }
    return best;
  }
  if (q->wand_depth == d && q->wand == QS_GIVEN && q->wand_alt % 3 == 2 && find_kind(IK_PHANTOM_FISH) < 0) {   // the phantom fish: search by the pools
    if (IS_WATER(G.map[h->pos]) && !BIT_GET(s_searched, h->pos)) { for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); } BIT_SET(s_searched, h->pos); st_search++; walk(bot_search()); return -2; }
    bfs(h->pos, false); int best = -1, bd = 9999;
    for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && !BIT_GET(s_searched, c) && b_dist[c] < bd && mob_at(c) < 0) { bd = b_dist[c]; best = c; }
    return best;
  }
  if (q->wand_depth == d && q->wand == QS_GIVEN && q->wand_alt % 3 == 0 && find_kind(IK_SEED_ROTBERRY) < 0) {   // the rotberry: tread on it
    bfs(h->pos, false);                                // (only when there is a way to it: else explore / search on)
    for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 7 && BIT_GET(G.visited, G.plants[i].pos) && b_dist[G.plants[i].pos] < 0x7FFF) return G.plants[i].pos;
  }
  return -1;
}
static int st_trapgrab; static bool s_rush; static int st_rush; static int s_starve_leave = 30; static int trap_danger(int t);
static int trap_prize(void) {                            // 0.54 (an upgrade scroll on an alarm trap, a strength potion on a fire trap, left behind): -1 none, -2 acted, else a cell to walk to
  Hero *h = &G.hero; if (getenv("OLDTRAPGRAB") || enemy_count() > 0 || h->HP < h->HT * 6 / 10 || h->b.roots) return -1;
  int s = throwable(); if (s >= 0 && item_cat(h->bag[s].kind) == CAT_SCROLL) { int s2 = -1; for (int i = 0; i < BAG_SIZE; i++) { int c = item_cat(h->bag[i].kind); if (c == CAT_MISSILE || c == CAT_WEAPON || c == CAT_ARMOR || c == CAT_RING) { if (h->bag[i].kind != IK_BOOMERANG && h->bag[i].kind != IK_PICKAXE && h->bag[i].kind != IK_INCENDIARY_DART) { s2 = i; break; } } } if (s2 >= 0) s = s2; }
  bool fl = s >= 0 && item_cat(h->bag[s].kind) == CAT_SCROLL;
  int best = -1, bd = 0x7FFF;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; int c = hp->pos, k = hp->item.kind, cat = item_cat(k);
    if ((unsigned)c >= MAP_LEN || hp->type != HEAP_PLAIN || !BIT_GET(G.visited, c) || s_black[c] > s_bot_steps) continue;
    if (!(((k == IK_SCR_UPGRADE || k == IK_POT_STRENGTH || k == IK_SCR_ENCHANT) && (!fair_on() || item_known(k))) || (fair_on() && (cat == CAT_SCROLL || cat == CAT_POTION) && !item_known(k)) || cat == CAT_WAND || cat == CAT_RING || cat == CAT_ARMOR || cat == CAT_WEAPON || (is_food(k) && !getenv("OLDTRAPFOOD") && !({ bool f = false; for (int j = 0; j < BAG_SIZE; j++) if (is_food(h->bag[j].kind)) f = true; f; })))) continue;   // 0.58: food too, with none in the bag (a ration on a trap was left while starving)
    int t = G.map[c]; if (!(TFLAGS(t) & F_AVOID) || (TFLAGS(t) & F_PIT) || in_room(c, RT_TRAPS)) continue;
    int dg = trap_danger(t);
    if (s >= 0) {                                      // (owner) throw something onto it: the trap goes off on that, then both lie there safe
      if (dg >= 9 && (fl || cat == CAT_SCROLL)) continue;   // (fire: nothing that burns)
      static int16_t tries[MAP_LEN]; static int tdep = -1, tgame = -1; if (tdep != G.depth || tgame != cur_game) { memset(tries, 0, sizeof tries); tdep = G.depth; tgame = cur_game; } if (tries[c] >= 3) continue;
      bool hit; if (distance(h->pos, c) <= 5 && ballistica(h->pos, c, &hit) == c) { tries[c]++; st_trapgrab++; s_did[0] = 0; DID("throw something onto the trap with the prize on it"); st_uses[ACN_THROW]++; walk(game_throw(s, c)); return -2; }
      for (int n = 0; n < MAP_LEN; n++) { if (b_dist[n] >= bd || distance(n, c) > 4 || n == c || char_at(n) || (TFLAGS(G.map[n]) & F_AVOID) || !PASSABLE(G.map[n]) || ballistica(n, c, &hit) != c) continue; bd = b_dist[n]; best = n; }
      continue; }
    if (dg >= 9) continue; if (dg >= 5 && h->HP < h->HT * 8 / 10) continue;
    if (adjacent(c, h->pos)) { int32_t t0 = h->time; ActResult rr = game_tap_cell(c); if (rr != ACT_NONE || h->time != t0) { st_trapgrab++; s_did[0] = 0; DID("onto the trap for the prize"); walk(rr); return -2; } s_black[c] = s_bot_steps + 200; continue; }
    for (int q = 0; q < 8; q++) { int n = c + N8[q]; if (!nb_ok(c, n) || b_dist[n] >= bd || char_at(n) || (TFLAGS(G.map[n]) & F_AVOID)) continue; bd = b_dist[n]; best = n; } }
  return best;
}
static int s_spot = -1;   // a piranha has been seen on this floor: keep out of the water
static void search_dbg(void) { if (!getenv("SRCHDBG")) return; for (int k = 0; k < 8; k++) { int n = G.hero.pos + N8[k]; if (nb_ok(G.hero.pos, n) && G.map[n] == T_SECRET_DOOR) printf("   SEARCH at %d,%d next to secret %d,%d fov %d\n", cx(G.hero.pos), cy(G.hero.pos), cx(n), cy(n), BIT_GET(G.fov, n)); } }
static int trap_danger(int t) {                           // bot3: how bad stepping on a known trap is (fire burns for ~8 turns: deadly early)
  switch (t) { case T_ALARM_TRAP: return 1; case T_GRIPPING_TRAP: return 2; case T_POISON_TRAP: return 3; case T_PARALYTIC_TRAP: return 4;
    case T_SUMMONING_TRAP: return 5; case T_TOXIC_TRAP: return 5; case T_LIGHTNING_TRAP: return 6; case T_FIRE_TRAP: return 9; default: return 4; } }
static bool trap_ok(int c) {                             // bot3: worth stepping on now, if it's the only way on
  Hero *h = &G.hero; int d = trap_danger(G.map[c]);
  bool desperate = s_floor_searches > 12;              // (searched round and found nothing: the trap is the way)
  if (d >= 9) { if (h->HP < h->HT * 9 / 10) return false; if (!desperate) for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (TFLAGS(G.map[n]) & F_FLAMABLE)) return false; } }   // fire: full health, nothing around to catch
  if (d >= 5 && h->HP < h->HT * 6 / 10) return false;
  return true;
}
// 0.53 (game 806 starved on floor 2): walled into a part of the floor the stairs aren't in (a teleport, a fall): the way
// out is a secret door in the walls between here and the seen floor beyond them. Search beside those walls (the usual
// search spots want the unknown behind a wall, and here the far side has been seen).
static int enclosed_search(void) {
  Hero *h = &G.hero; bfs(h->pos, false); int best = -1; long bs = 0;
  for (int r = 0; r < MAP_LEN; r++) { if (b_dist[r] >= 0x7FFF || char_at(r) && r != h->pos || (TFLAGS(G.map[r]) & F_AVOID)) continue;
    int g = 0; for (int k = 0; k < 8; k++) { int w = r + N8[k]; if (!nb_ok(r, w) || BIT_GET(s_searched, w) || (G.map[w] != T_WALL && G.map[w] != T_WALL_DECO && G.map[w] != T_SECRET_DOOR)) continue;
      for (int j = 0; j < 4; j++) { int b = w + N8[j]; if (!nb_ok(w, b)) continue; if ((PASSABLE(G.map[b]) || G.map[b] == T_DOOR) && b_dist[b] >= 0x7FFF && BIT_GET(G.visited, b)) { g++; break; } } }
    if (!g) continue; long sc = g * 1000L / (b_dist[r] + 2); if (sc > bs) { bs = sc; best = r; } }
  return best;
}
static int trap_step(int target) {                     // bot3: the next step towards target when traps (not chasms) may be crossed: fewest traps first
  static int32_t d[MAP_LEN]; static int16_t q[4096]; static uint8_t inq[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) { d[c] = 0x7FFFFFF; inq[c] = 0; }
  unsigned qh = 0, qt = 0; d[target] = 0; q[qt++ & 4095] = target; inq[target] = 1;   // 0.53: a queue that relaxes until settled (the 0-1 deque overflowed: game 806 starved beside a paralytic trap)
  while (qh != qt) { int c = q[qh++ & 4095]; inq[c] = 0;
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || !BIT_GET(G.visited, n)) continue; int t = G.map[n];
      bool trap = (TFLAGS(t) & F_AVOID) && !(TFLAGS(t) & F_PIT) && t != T_WELL; if (!PASSABLE(t) && !trap) continue;
      int nd = d[c] + (trap ? 100 * trap_danger(t) : 1); if (nd < d[n]) { d[n] = nd; if (!inq[n] && qt - qh < 4000) { q[qt++ & 4095] = n; inq[n] = 1; } } } }
  int best = -1; int32_t bd = d[G.hero.pos]; for (int k = 0; k < 8; k++) { int n = G.hero.pos + N8[k]; if (nb_ok(G.hero.pos, n) && d[n] < bd && !char_at(n) && (n == target || !BIT_GET(s_sale, n))) { bd = d[n]; best = n; } }   /* (a ware as the step's end opens the trade window) */
  return best;
}
// 0.49 (owner: "a search along a flat wall does all three nearby tiles"): a search always finds whatever is hidden in the
// 8 cells around you, so it's a covering problem. A wall can hide a door only with known floor on one side and the
// unknown straight across on the other (PD's doors are orthogonal); the more unknown behind it, the likelier (a dead
// end's wall most of all). Each spot is worth the walls it would newly cover, per step to get there and search
// (2 turns): sweeping along a wall three cells at a time beats crossing the map for a slightly better spot.
static uint8_t s_ww[MAP_LEN]; static int s_srch_win = 1, s_srch_k = 2; static bool s_shophunt;   // 0.93: hunting this floor's shop (evidence_hidden): the entrance room's walls first
static int wall_worth(int w) {
  if ((G.map[w] != T_WALL && G.map[w] != T_SECRET_DOOR) || BIT_GET(s_searched, w)) return 0;
  int best = 0;
  for (int k = 0; k < 4; k++) {
    int a = w - N8[k], b = w + N8[k]; if (!nb_ok(w, a) || !nb_ok(w, b)) continue;
    int bx = cx(b), by = cy(b); if (bx <= 0 || by <= 0 || bx >= MAP_W - 1 || by >= MAP_H - 1) continue;
    if (!BIT_GET(G.visited, a) || BIT_GET(G.visited, b) || !(PASSABLE(G.map[a]) || G.map[a] == T_DOOR || G.map[a] == T_OPEN_DOOR)) continue;
    if (s_shophunt && !getenv("SHOPANY") && G.entrance >= 0 && room_at(a) != room_at(G.entrance)) continue;   // 0.93 (owner): the shop's only door is in the entrance room's walls (Dungeon.shopOnLevel: a dead end off roomEntrance)
    int dx = bx - cx(w), dy = by - cy(w), R = s_srch_win, x0 = bx + R * dx, y0 = by + R * dy, u = 0;   // (the box beyond, R each way)
    for (int y = y0 - R; y <= y0 + R; y++) for (int x = x0 - R; x <= x0 + R; x++) u += x > 0 && y > 0 && x < MAP_W - 1 && y < MAP_H - 1 && !BIT_GET(G.visited, x + y * MAP_W);
    int v = 1 + u * 10 / ((2 * R + 1) * (2 * R + 1)) + (dead_end(a) ? 12 : 0) + (s_shophunt && G.entrance >= 0 && room_at(a) >= 0 && room_at(a) == room_at(G.entrance) ? 30 : 0); if (v > best) best = v;   // (0.93: the shop's a dead end off the entrance room)
  }
  return best;
}
static int search_spot2(void) {
  Hero *h = &G.hero; bfs(h->pos, false); bool any = false;
  for (int c = 0; c < MAP_LEN; c++) { s_ww[c] = wall_worth(c); any |= s_ww[c] > 0; }
  if (!any) return -2;
  int best = -1; long bs = 0;
  for (int c = 0; c < MAP_LEN; c++) {
    if (b_dist[c] >= 0x7FFF || G.map[c] == T_ALCHEMY || c == G.entrance || c == G.exit || (s_piranhas && PREACH(c)) || s_black[c] > s_bot_steps || (heap_at(c) >= 0 && G.heaps[heap_at(c)].type != HEAP_PLAIN) || mob_at(c) >= 0) continue;
    int g = 0; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n)) g += s_ww[n]; }
    if (!g) continue;
    long sc = g * 10000L / (b_dist[c] + s_srch_k); if (sc > bs) { bs = sc; best = c; }
  }
  if (V && best >= 0) printf("   search spot %d,%d dist %d score %ld (hero %d,%d)\n", cx(best), cy(best), b_dist[best], bs, cx(h->pos), cy(h->pos));
  return best;
}
static int search_spot(void) {                          // where to look for secret doors: beside walls with much unknown behind
  Hero *h = &G.hero;
  if (!getenv("OLDSRCH2")) { int r = search_spot2(); if (r != -2) return r; }   // (-2: no wall with the unknown behind it: the old way)
  bfs(h->pos, false);
  int best = -1, bs = getenv("OLDSRCH") ? 0 : -1000000, fb = -1;   // (bot3: a far spot with much unknown behind had a score below 0: never chosen, and not a fallback either)
  for (int c = 0; c < MAP_LEN; c++) {
    if (b_dist[c] >= 0x7FFF || G.map[c] == T_ALCHEMY || c == G.entrance || c == G.exit || (s_piranhas && PREACH(c)) || s_black[c] > s_bot_steps || (heap_at(c) >= 0 && G.heaps[heap_at(c)].type != HEAP_PLAIN) || mob_at(c) >= 0) continue;
    int walls = 0; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && (G.map[n] == T_WALL || G.map[n] == T_SECRET_DOOR) && !BIT_GET(s_searched, n)) walls++; }
    if (!walls) continue;
    int unk = 0, x0 = cx(c), y0 = cy(c);
    for (int y = y0 - 4; y <= y0 + 4; y++) for (int x = x0 - 4; x <= x0 + 4; x++) if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && !BIT_GET(G.visited, x + y * MAP_W)) unk++;
    int sc = unk * 4 - b_dist[c];
    if (s_srch_dead) { int open = 0; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && BIT_GET(G.visited, n) && (PASSABLE(G.map[n]) || G.map[n] == T_DOOR || G.map[n] == T_OPEN_DOOR)) open++; } if (open <= 1) sc += s_srch_dead; }   // bot3: a dead end: where hidden doors hide
    if (unk > 3 && sc > bs) { bs = sc; best = c; }
    if (unk <= 3 && (fb < 0 || b_dist[c] < b_dist[fb])) fb = c;                    // (the nearest wall not yet searched, whatever lies behind)
  }
  return best >= 0 ? best : s_fallback_search ? fb : -1;
}
// 0.51 (owner: "use the information we have"): evidence of a room not found yet. A special room's painter leaves its
// key or potion in an ordinary room somewhere on the floor (content.c add_spawn): a locked door and no key seen, a
// locked chest and no golden key, a trap room and no levitation, a pool and no invisibility, a storage room and no flame:
// that room is behind a secret door. (Unknown potions: no potion seen at all in the ordinary rooms is the evidence.)
static int s_ev_food, st_xpb, st_pace; static uint8_t s_ev_seen[MAP_LEN / 8 + 1]; static int s_ev_floor = -1, s_std_pots, s_ev_k[5], s_ev_searches, s_ev_on; static int st_ev_search, st_ev_floors, st_ev_found;
static bool std_room(int c) { int r = room_at(c); if (r < 0) return true; int t = G.rooms[r].type; return t == RT_STANDARD || t == RT_ENTRANCE || t == RT_EXIT || t == RT_TUNNEL || t == RT_PASSAGE; }
static bool room_seen(int r) { Room *R = &G.rooms[r]; int i = fair_on() ? 1 : 0; for (int y = R->top + i; y <= R->bottom - i; y++) for (int x = R->left + i; x <= R->right - i; x++) if (BIT_GET(G.visited, x + y * MAP_W)) return true; return false; }   /* (fair: its inside seen, not a wall) */
static bool room_prize(int r) { for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if (hp->pos < MAP_LEN && room_at(hp->pos) == r && hp->type != HEAP_CARRIED && (!fair_on() || BIT_GET(G.visited, hp->pos))) return true; } return false; }
static const char *evidence_hidden(void);
static void evidence_track(void) {
  if (s_ev_floor != G.depth) {                        // 0.92 (seed 84: back up to floor 3 for the stairs bounce, it hunted for the ration it had eaten there): what each floor showed is kept
    static struct { int game, food, pots, k[5], searches; uint8_t seen[MAP_LEN / 8 + 1]; } m[32];
    if (s_ev_floor >= 0 && s_pztest) { int d = s_ev_floor & 31; m[d].game = cur_game + 1; m[d].food = s_ev_food; m[d].pots = s_std_pots; memcpy(m[d].k, s_ev_k, sizeof s_ev_k); m[d].searches = s_ev_searches; memcpy(m[d].seen, s_ev_seen, sizeof s_ev_seen); }
    s_ev_floor = G.depth; memset(s_ev_seen, 0, sizeof s_ev_seen); s_std_pots = 0; memset(s_ev_k, 0, sizeof s_ev_k); s_ev_searches = 0; s_ev_on = 0; s_ev_food = 0;
    { int d = G.depth & 31; if (s_pztest && m[d].game == cur_game + 1) { s_ev_food = m[d].food; s_std_pots = m[d].pots; memcpy(s_ev_k, m[d].k, sizeof s_ev_k); s_ev_searches = m[d].searches; memcpy(s_ev_seen, m[d].seen, sizeof s_ev_seen); } } }
  for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].pos < MAP_LEN && BIT_GET(G.fov, G.heaps[i].pos) && is_food(G.heaps[i].item.kind)) s_ev_food = 1;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; int c = hp->pos;
    if (c >= MAP_LEN || !BIT_GET(G.fov, c) || BIT_GET(s_ev_seen, c) || (!std_room(c) && !s_pztest)) continue;   // 0.92 (seed 84 d2): the key was the pool's prize (spawn_prize takes from the same list): seen anywhere counts
    BIT_SET(s_ev_seen, c); int k = hp->item.kind;
    if (item_cat(k) == CAT_POTION && std_room(c)) s_std_pots++;
    if (k == IK_POT_LEVITATION) s_ev_k[0]++; if (k == IK_POT_INVIS) s_ev_k[1]++; if (k == IK_POT_FLAME) s_ev_k[2]++; if (k == IK_IRON_KEY) s_ev_k[3]++; if (k == IK_GOLDEN_KEY) s_ev_k[4]++; }
  if (s_ev_on && !evidence_hidden()) { s_ev_on = 0; st_ev_found++; if (getenv("EVDBG")) printf("  EV game %d d%d step %d: resolved after %d\n", cur_game, G.depth, s_bot_steps, s_ev_searches); }
}
static bool rest_cap(void) {                             // 0.92 (seed 84 d4: 238 turns of "rest before going down", a respawn biting each time, into starving): 120 turns of it a floor at most
  static int d = -1, g = -1; static int32_t t0; if (d != G.depth || g != cur_game) { d = G.depth; g = cur_game; t0 = G.now; }
  return s_pztest && G.now - t0 > 120 * TICK && G.hero.HP * 10 >= G.hero.HT * 5;
}
static bool food_ev_ok(void) {                           // 0.92: hungry with no food, this floor's ration unseen: search for it here, not on down
  Hero *h = &G.hero; if (!s_pztest || getenv("NOEV") || s_ev_food || boss_depth(G.depth) || G.depth == 1 || G.depth == 21 || s_ev_searches >= 20 || h->HP * 2 < h->HT || threat_count() || floor_left()) return false;
  for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) return false;
  return game_hungry();
}
static bool shop_seen(void) { for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE && (unsigned)G.heaps[i].pos < MAP_LEN && BIT_GET(G.visited, G.heaps[i].pos)) return true; return false; }
static bool shop_seen(void);
static bool shop_hunt_ok(void) {                         // 0.93: a shop floor, its shop not found, gold to spend: search for it, hungry or not (it sells food)
  Hero *h = &G.hero; if (getenv("SHOPMAP") && !s_rollout && !s_is_child && (G.depth == 6 || G.depth == 11 || G.depth == 16) && !shop_seen()) { static int dd = -1, gg = -1; if (dd != G.depth || gg != cur_game) { dd = G.depth; gg = cur_game; printf("SHOPMAP d%d entrance %d,%d room %d hp %d/%d gold %d\n", G.depth, cx(G.entrance), cy(G.entrance), room_at(G.entrance), h->HP, h->HT, h->gold); dump_map(h->pos); } } return s_pztest && !getenv("NOSHOPHUNT") && !getenv("NOEV") && (G.depth == 6 || G.depth == 11 || G.depth == 16) && h->gold >= 150 * (G.depth / 5 + 1) && s_ev_searches < 48 && h->HP * 4 >= h->HT && threat_count() == 0 && !shop_seen();
}
static const char *evidence_hidden(void) {
  s_shophunt = false;
  if (getenv("OLDEVID") || boss_depth(G.depth) || G.depth == 1) return NULL;
  if (s_pztest && !getenv("NOSHOPHUNT") && (G.depth == 6 || G.depth == 11 || G.depth == 16) && G.hero.gold >= 150 * (G.depth / 5 + 1) && !shop_seen()) { s_shophunt = true; return "a shop floor and no shop seen: it's behind a secret door off the entrance room"; }   // 0.93 (n8g5: a quarter of shop visits bought nothing, the shop never found: PD hides a shop's door like any other, 1 in 6)
  if (s_pztest && !s_ev_food && G.depth != 21) return "this floor's ration not seen: it's behind a secret door";   // 0.92 (seed 84 d3: went down starving beside the dead end that hid it): every floor but the bosses' has one
  bool ld = false, lc = false; for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) ld = true;
  for (int i = 0; i < G.n_heaps; i++) if ((G.heaps[i].type == HEAP_LOCKED_CHEST || G.heaps[i].type == HEAP_CRYSTAL_CHEST) && G.heaps[i].pos < MAP_LEN && BIT_GET(G.visited, G.heaps[i].pos)) lc = true;
  if (ld && !(G.keys[G.depth] & 15) && !s_ev_k[3]) return "a locked door and no key seen: the key's in a room I haven't found";
  if (lc && !(G.keys[G.depth] & 48) && !s_ev_k[4]) return "a locked chest and no golden key seen: it's in a room I haven't found";
  for (int r = 0; r < G.n_rooms; r++) { int t = G.rooms[r].type, w = t == RT_TRAPS ? 0 : t == RT_POOL ? 1 : t == RT_STORAGE ? 2 : -1;
    if (w < 0 || !room_seen(r) || !room_prize(r)) continue;
    static const int PK[3] = { IK_POT_LEVITATION, IK_POT_INVIS, IK_POT_FLAME };
    bool missing = item_known(PK[w]) ? !s_ev_k[w] : s_std_pots == 0;
    if (missing) return w == 0 ? "a trap room, and its levitation potion not seen: a room I haven't found" : w == 1 ? "a pool room, and its invisibility potion not seen: a room I haven't found" : "a storage room, and its flame potion not seen: a room I haven't found"; }
  return NULL;
}
// PRESS=1: "just press the middle button": every move is the game's target list entry 0 (the next one if nothing came of it)
static int st_near_n, st_near_skip, st_near_any, st_far_n, st_far, st_eatst_n, st_eatst, st_flip, st_explore_n;
static int st_dither, st_turnback, st_turnback_foe, st_srch, st_srch_foe, st_srch_exit, st_food_near, st_food_left;
static bool s_pure;                                       // 0.64 PURE=1: the Assist alone
static int P, st_press, st_press_off, st_osc, st_press_n, st_floors, s_pp[3];
static const uint8_t PVIEW[3][4] = { { 12, 8, 8, 9 }, { 6, 4, 4, 4 }, { 3, 2, 2, 2 } };   // Emery's g_view per zoom (main.c VIEW)
static bool p_onscreen(int c) { if (!g_view[0]) return true; int dx = cx(c) - cx(G.hero.pos), dy = cy(c) - cy(G.hero.pos); return dx >= -g_view[0] && dy >= -g_view[1] && dy <= g_view[2] && dx <= g_view[3]; }
// PRESS=2: now and then the player overrides like a human (at a fork, a target the other way from entry 0), then just
// presses Select again; counted: the overrides, and entry 0 pointing back (> 90 degrees off the chosen way) in the next
// presses while that way is still on (g_ovr.c) and in the next 8 presses regardless
static int st_mine, st_mine_walk, st_mine_none, st_pend_pr, st_turn_pend, s_back, st_backs, st_back_fight, st_back_hold, st_back_adv, st_back_other, st_hold, st_hold_run, st_hold_max, st_fb, st_ovr, st_ovr_set, st_turn_on, st_turn8, st_watch8, st_on_pr, s_od = -1, s_left, s_odepth;
static int ppang(int dx, int dy) { int s = abs(dx) + abs(dy); if (!s) return 0;
  return dx > 0 && dy >= 0 ? 16 * dy / s : dx <= 0 && dy > 0 ? 16 + 16 * -dx / s : dx < 0 ? 32 + 16 * -dy / s : 48 + 16 * dx / s; }
static int cdir(int c) { return ppang(cx(c) - cx(G.hero.pos), cy(c) - cy(G.hero.pos)); }
static int adiff(int a, int b) { return abs(((a - b + 32) & 63) - 32); }
static int s_f0 = -1, s_k0, st_offf, st_offf_n, st_tramp, st_grass_left, s_hg_depth = -1, s_hg_last;
void tgt_first(int c, int k) { s_f0 = c; s_k0 = k; }
static int s_tline; void tgt_line(int l) { s_tline = l; }
static int hg_count(void) { int n = 0; for (int c = 0; c < MAP_LEN; c++) n += G.map[c] == T_HIGH_GRASS && BIT_GET(G.visited, c); return n; }
static bool onscreen_frontier(void) {                   // a reachable known open cell on screen beside the unknown (not just beside the hero)
  bfs(G.hero.pos, false);
  for (int c = 0; c < MAP_LEN; c++) {
    if (c == G.hero.pos || b_dist[c] >= 0x7FFF || !BIT_GET(G.visited, c) || !p_onscreen(c) || !PASSABLE(G.map[c]) || c == G.entrance || c == G.exit || G.map[c] == T_ALCHEMY || (s_piranhas && PREACH(c))) continue;
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && !BIT_GET(G.visited, n) && distance(n, G.hero.pos) > 1) return true; }
  }
  return false;
}
static void aud_dump(int a, int b) {                   // 0.61: the map round the hero (AUDMAP=1)
  int hx = cx(G.hero.pos), hy = cy(G.hero.pos);
  for (int y = hy - 7; y <= hy + 7; y++) { if (y < 0 || y >= MAP_H) continue; printf("   %2d ", y);
    for (int x = hx - 14; x <= hx + 14; x++) { if (x < 0 || x >= MAP_W) { putchar(' '); continue; } int c = x + y * MAP_W, t = G.map[c];
      int mi = mob_at(c);
      putchar(c == G.hero.pos ? '@' : c == a ? 'A' : c == b ? 'B' : mi >= 0 && BIT_GET(G.fov, c) ? 'm' : !BIT_GET(G.visited, c) ? ' ' : t == T_WALL || t == T_WALL_DECO ? '#' : t == T_DOOR || t == T_OPEN_DOOR ? '+' : IS_WATER(t) ? '~' : (TFLAGS(t) & F_AVOID) ? '^' : PASSABLE(t) ? '.' : '?'); }
    putchar('\n'); }
}
// 0.72 (owner: "go to the opposite corner of the map to a fully explored area for no reason. The assist should assist
// with THAT"): CONTRA=1: once a floor, at a random moment, the player sets off for the farthest explored cell, tapping
// hops of 2-6 cells along the way (as on the watch's screen). Before each hop, entry 0 is judged: with the player's way,
// beside it, or against it (a turn round), a trigger (hands, an enemy, gas), or a stand-still. Enemies: the Assist fights.
static int s_ct_dest = -1, s_ct_depth = -1, s_ct_at, s_ct_fp, s_ct_hops, s_ct_lastk = -1;
static long ct_n[12], ct_mn[4][12]; static int ct_trips, ct_arrived, ct_abort, s_ct_mode, ct_mtrips[4];
static const char *CT_NAME[12] = { "with", "beside", "AGAINST", "item with", "item beside", "ITEM AGAINST", "stand still", "hands", "enemy/tactics", "escape", "hold (wait for it)", "nothing" };
static bool contra(int n, int16_t *cell, uint8_t *kind) {
  Hero *h = &G.hero; int hp = h->pos;
  if (G.depth != s_ct_depth) { s_ct_depth = G.depth; s_ct_dest = -1; s_ct_fp = 0; s_ct_at = 30 + rand() % 220; if (boss_depth(G.depth)) s_ct_at = 1 << 30; }
  s_ct_fp++;
  if (s_ct_dest < 0) {
    if (s_ct_fp != s_ct_at || enemy_count()) { if (s_ct_fp == s_ct_at) s_ct_at++; return false; }
    bfs(hp, false); int best = -1, bd = 7;
    s_ct_mode = getenv("CTMODE") ? atoi(getenv("CTMODE")) : (cur_game + G.depth) % 4;   // 0 the farthest explored cell, 1 back to the way up,
    if (s_ct_mode == 1 && b_dist[G.entrance] < 0x7FFF && b_dist[G.entrance] > bd) best = G.entrance;   // 2 the way down (unexplored left behind),
    if (s_ct_mode == 2 && BIT_GET(G.visited, G.exit) && b_dist[G.exit] < 0x7FFF && b_dist[G.exit] > bd) best = G.exit;   // 3 a part left unexplored, not plan A's
    if (s_ct_mode == 3) { int o = n > 0 ? g_tgobj[0] : -1;
      for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < 0x7FFF && b_dist[c] > bd && frontier(c) && (o < 0 || distance(c, o) > 6)) { bd = b_dist[c]; best = c; } }
    if (best < 0) { s_ct_mode = 0; for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < 0x7FFF && b_dist[c] > bd && c != G.exit && c != G.entrance && heap_at(c) < 0) { bd = b_dist[c]; best = c; } }
    if (best < 0) return false;
    s_ct_dest = best; s_ct_hops = 0; ct_trips++; ct_mtrips[s_ct_mode]++;
    if (getenv("CTV")) printf("  CONTRA g%d d%d step %d mode %d: off to %d,%d (%d steps) from %d,%d\n", cur_game, G.depth, s_bot_steps, s_ct_mode, cx(best), cy(best), b_dist[best], cx(hp), cy(hp));
  }
  if (s_ct_dest == MAP_LEN) return false;              // (this floor's trip is over)
  if (enemy_count()) return false;                     // a fight: the Assist's (entry 0 pressed as usual)
  bfs(s_ct_dest, false); int dh = b_dist[hp];
  if (dh == 0x7FFF || ++s_ct_hops > 120) { ct_abort++; s_ct_dest = MAP_LEN; return false; }
  static int16_t dd[MAP_LEN]; memcpy(dd, b_dist, sizeof dd);
  // judge entry 0
  int cat = 11;
  if (n > 0) {
    int c0 = cell[0], k0 = kind[0];
    if (k0 == TK_BAG || k0 == TK_REST) cat = 7;
    else if (G.tgm.src0 == 4) cat = 9;
    else if (k0 == TK_ENEMY || G.tgm.src0 == 2) cat = 8;
    else if (c0 == hp) cat = (g_ovr.d & 128) ? 10 : 6;
    else {
      bfs(c0, false); int st = -1, sd = 0x7FFF;
      for (int k = 0; k < 8; k++) { int m = hp + N8[k]; if (nb_ok(hp, m) && b_dist[m] < sd) { sd = b_dist[m]; st = m; } }
      int v = st < 0 || dd[st] == 0x7FFF ? 1 : dd[st] < dh ? 0 : dd[st] == dh ? 1 : 2;
      cat = (k0 == TK_ITEM ? 3 : 0) + v;
      if (v == 2 && s_ct_hops > 1 && getenv("CTV")) {
        int o = g_tgobj[0];
        printf("  CT-AGAINST g%d d%d hop %d: hero %d,%d dest %d,%d (%d away) entry0 %d,%d kind %d src0 %d line %d objective %d,%d tile %d visited %d (list %d) ovr c %d on %d d %d\n",
          cur_game, G.depth, s_ct_hops, cx(hp), cy(hp), cx(s_ct_dest), cy(s_ct_dest), dh, cx(c0), cy(c0), k0, G.tgm.src0, s_tline, o >= 0 ? cx(o) : -1, o >= 0 ? cy(o) : -1, o >= 0 ? G.map[o] : -1, o >= 0 ? BIT_GET(G.visited, o) : -1, n, g_ovr.c, g_ovr.on, g_ovr.d);
        if (getenv("CTMAP")) aud_dump(c0, hp);
      }
    }
    if (cat == 7 && getenv("CTV")) { char nm[48] = "rest"; if (k0 == TK_BAG) item_name(game_slot(g_ovr.hs), nm, sizeof nm); printf("  CT-HANDS g%d d%d hop %d: %s %s HP %d/%d hunger %d\n", cur_game, G.depth, s_ct_hops, k0 == TK_BAG ? ACTION_NAMES[g_ovr.ha] : "", nm, h->HP, h->HT, h->hunger); }
  }
  if (s_ct_hops > 1) { ct_n[cat]++; ct_mn[s_ct_mode][cat]++; }                      // (the first: no way tapped yet)
  // the player's hop: 2-6 cells along the way, on screen (passing over the hands: main.c on_tap's pass_hands)
  int want = 2 + rand() % 5, c = hp;
  for (int s = 0; s < want; s++) { int b = -1, bdd = dd[c]; for (int k = 0; k < 8; k++) { int m = c + N8[k]; if (nb_ok(c, m) && dd[m] < bdd && p_onscreen(m) && m != G.exit && m != G.entrance) { bdd = dd[m]; b = m; } } if (b < 0) break; c = b; }
  if (c == hp) { ct_abort++; s_ct_dest = MAP_LEN; return false; }
  if (c == hp && dh <= 2) { ct_arrived++; s_ct_dest = MAP_LEN; return false; }
  if (n > 0 && (kind[0] == TK_BAG || kind[0] == TK_REST)) { int j = G.tgm.dn++ & 7; G.tgm.dk[j] = kind[0] == TK_REST ? 0 : game_slot(g_ovr.hs)->kind; G.tgm.da[j] = kind[0] == TK_REST ? 255 : g_ovr.ha; G.tgm.dd = G.depth; }
  if (n > 0 && G.tgm.src0 >= 2 && G.tgm.src0 != 3) { int m = kind[0] == TK_ENEMY ? mob_at(cell[0]) : -1; G.tgm.vs = G.tgm.src0; G.tgm.vu = m >= 0 ? G.mobs[m].uid : 255; G.tgm.vt = G.now + 15 * TICK; }
  g_ovr.c = c; g_ovr.t = hp - 65536;
  int32_t t = h->time; ActResult r = tap_safe(c); walk(r);
  if (G.hero.pos == s_ct_dest || dd[G.hero.pos] <= 1) { ct_arrived++; s_ct_dest = MAP_LEN; if (getenv("CTV")) printf("  CONTRA g%d d%d arrived after %d hops\n", cur_game, G.depth, s_ct_hops); }
  if (h->time == t && G.hero.pos == hp) { ct_abort++; s_ct_dest = MAP_LEN; }
  return true;
}
static void ph_save(const char *name, int g, int steps);
static bool press(void) {
  int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX];
  { int h = hg_count(); if (G.depth != s_hg_depth) { if (s_hg_depth >= 0) st_grass_left += s_hg_last; s_hg_depth = G.depth; } else if (h < s_hg_last) st_tramp += s_hg_last - h; s_hg_last = h; }
  s_f0 = -1;
  int n = game_targets(cell, kind), p0 = G.hero.pos, i0 = 0;
  st_press_n++;
  if (getenv("CONTRA") && !s_rollout && !s_is_child && contra(n, cell, kind)) return true;   // 0.72: the contrary player
  if (s_f0 >= 0 && s_k0 != TK_ENEMY && s_k0 != TK_ITEM && g_ovr.c < 0 && !enemy_count() && !p_onscreen(s_f0)) { st_offf_n++; if (onscreen_frontier()) { st_offf++; if (V) printf("  OFFSCREEN-FIRST %d while an on-screen frontier exists\n", s_f0); } }
  if (P == 2 && n > 0) {
    if (s_odepth != G.depth) s_left = 0;
    if (s_left > 0 && kind[0] != TK_ENEMY && cell[0] != p0) {
      bool back = adiff(cdir(cell[0]), s_od) > 16;
      st_watch8++; st_turn8 += back; s_left--;
      if (g_ovr.c >= 0 && g_ovr.c < MAP_LEN) { st_pend_pr++; st_turn_pend += back; }
      if (g_ovr.c >= 0) { st_on_pr++; st_turn_on += back; if (back && V) printf("  TURNAROUND entry0 %d hero %d way %d ovr %d\n", cell[0], p0, s_od, g_ovr.c); }
    }
    if (s_back) {                                       // the press after a step back from an enemy: fight (or wait for it), not advance
      s_back = 0;
      if (kind[0] != TK_ENEMY) { st_back_other++; if (V) printf("  BACK-OTHER entry0 %d/%d hero %d\n", cell[0], kind[0], p0); }
      else if (adjacent(cell[0], p0)) st_back_fight++;
      else if (g_ovr.d & 128) st_back_hold++;
      else { st_back_adv++; if (V) printf("  BACK-ADVANCE entry0 %d hero %d\n", cell[0], p0); }
    }
    if (kind[0] == TK_ENEMY && adjacent(cell[0], p0) && rand() % 4 == 0) {   // doorway fighting: the step away (any single step)
      for (int j = 1; j < n; j++) if (kind[j] == TK_GO && adjacent(cell[j], p0)) { i0 = j; s_back = 1; st_backs++; break; }
    }
    else if (kind[0] != TK_ENEMY && enemy_count() == 0 && n > 1 && rand() % 12 == 0) {   // a human's own idea: the most opposite target
      int bj = -1, bd = 16;
      for (int j = 1; j < n; j++) if (kind[j] != TK_ENEMY && cell[j] != p0 && adiff(cdir(cell[j]), cdir(cell[0])) > bd) { bd = adiff(cdir(cell[j]), cdir(cell[0])); bj = j; }
      if (bj > 0 && cell[0] != p0) { i0 = bj; st_ovr++; s_od = cdir(cell[bj]); s_left = 8; s_odepth = G.depth; if (V) printf("  OVERRIDE to %d (entry %d) way %d; entry 0 was %d\n", cell[bj], bj, s_od, cell[0]); }
    }
  }
  if (!s_rollout && !s_is_child) { bool foe = false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC)) foe = true; }   // 0.61 (owner): the Assist audit
    if (getenv("DOORCHK")) {                             // 0.78 (owner: "it doesn't step back into doors on its own, it's always the second suggestion")
      static long dn, dt; Mob *ki = 0; int kd = 99;
      for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; int d = distance(m->pos, p0); if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & (MD_BOSS | MD_NPC | MD_IMMOVABLE)) && MOB_DEFS[m->type].move_cost * 2 > hero_move_cost() && d >= 2 && d <= 6 && d < kd) { kd = d; ki = m; } }
      if (ki) { bfs(p0, false); int D = -1; for (int c = 0; c < MAP_LEN; c++) if ((G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR) && b_dist[c] <= 3 && distance(c, ki->pos) > distance(c, p0)) D = c;
        if (D >= 0) { dn++; bool tr = n > 0 && kind[0] == TK_GO && G.tgm.src0 == 2; dt += tr;
          if (!tr && getenv("AUDV")) { printf("  DOOR g%d d%d hero %d,%d mob %d (%d,%d) d%d door %d,%d entry0 %d/%d src %d n %d\n", cur_game, G.depth, cx(p0), cy(p0), ki->type, cx(ki->pos), cy(ki->pos), kd, cx(D), cy(D), n > 0 ? cell[0] : -1, n > 0 ? kind[0] : -1, G.tgm.src0, n); if (getenv("AUDMAP")) aud_dump(ki->pos, D); } }
        if (dn && dn % 200 == 0) printf("DOORCHK %ld chances (a slow one coming 2-6 off, a door within 3 on my side), door trick plan A %ld\n", dn, dt); } }
    if (getenv("ADJCHK")) { static long ao, ad, ax, nn; nn++;   // 0.77: cells beside the hero left unknown (blind excluded)
      if (!G.hero.b.blind) for (int k = 0; k < 8; k++) { int q = p0 + N8[k]; if (!nb_ok(p0, q) || BIT_GET(G.visited, q)) continue;
        if (k < 4) ao++; else { int a1 = (q & 31) | (p0 & ~31), b1 = (p0 & 31) | (q & ~31); if (!PASSABLE(G.map[a1]) && !PASSABLE(G.map[b1])) ad++; else { ax++; if (getenv("AUDV")) { printf("  ADJ diag open g%d d%d hero %d,%d cell %d,%d t%d\n", cur_game, G.depth, cx(p0), cy(p0), cx(q), cy(q), G.map[q]); aud_dump(q, q); } } } }
      if (nn % 2000 == 0) printf("ADJCHK presses %ld: unknown beside the hero: straight %ld, diagonal between walls %ld, diagonal other %ld\n", nn, ao, ad, ax); }
    if (n > 0 && !foe && getenv("NEAR")) {             // 0.77 audit: FAR: entry 0 a place much farther than the nearest unexplored spot (any distance);
      bfs(p0, false); int f = -1, nv = 0; for (int c = 0; c < MAP_LEN; c++) { nv += BIT_GET(G.visited, c); if (b_dist[c] > 0 && b_dist[c] < 0x7FFF && frontier(c) && (f < 0 || b_dist[c] < b_dist[f])) f = c; }
      int o0 = g_tgobj[0], d0 = (unsigned)o0 < MAP_LEN ? b_dist[o0] : 0x7FFF;   // (the objective, not its on-screen stand-in)
      if (f >= 0 && kind[0] == TK_GO && cell[0] != p0) { st_far_n++; if (d0 < 0x7FFF && d0 > 2 * b_dist[f] + 6 && distance(o0, f) > 3) { st_far++; if (getenv("AUDV")) printf("  FAR g%d d%d hero %d,%d unexplored %d,%d (%d) obj %d,%d (%d) t%d src %d line %d way %d\n", cur_game, G.depth, cx(p0), cy(p0), cx(f), cy(f), b_dist[f], cx(o0), cy(o0), d0, G.map[o0], G.tgm.src0, s_tline, g_ovr.c >= 0 ? g_ovr.d & 63 : -1); if (getenv("AUDMAP")) aud_dump(o0, f); } }
      if (f >= 0) st_explore_n++;
      // EATST: a meal suggested while starving, the floor done (entry 0 would be the stairs), health at half or more: the stairs first are free (PD: no hunger on stairs while starving)
      if (kind[0] == TK_BAG && g_ovr.ha == ACN_EAT && game_starving()) { st_eatst_n++; if (f < 0 && G.exit >= 0 && BIT_GET(G.visited, G.exit) && b_dist[G.exit] < 0x7FFF && G.hero.HP * 2 >= G.hero.HT) { st_eatst++; if (getenv("AUDV")) printf("  EATST g%d d%d hp %d/%d\n", cur_game, G.depth, G.hero.HP, G.hero.HT); } }
      // FLIP: entry 0's objective back to one it had two changes ago, nothing new seen since (the A, B, A loop)
      { static int16_t ob[3] = { -1, -1, -1 }; static int nvb[3]; static int dd = -1; if (dd != G.depth) { ob[0] = ob[1] = ob[2] = -1; dd = G.depth; }
        if (kind[0] == TK_GO && o0 != ob[0]) { if (o0 == ob[1] && nvb[1] == nv) { st_flip++; if (getenv("AUDV")) printf("  FLIP g%d d%d hero %d,%d obj %d,%d <-> %d,%d src %d line %d\n", cur_game, G.depth, cx(p0), cy(p0), cx(o0), cy(o0), cx(ob[0]), cy(ob[0]), G.tgm.src0, s_tline); if (getenv("AUDMAP")) aud_dump(o0, ob[0]); }
          ob[2] = ob[1]; nvb[2] = nvb[1]; ob[1] = ob[0]; nvb[1] = nvb[0]; ob[0] = o0; nvb[0] = nv; } } }
    if (n > 0 && !foe && kind[0] == TK_GO && getenv("NEAR")) {   // 0.76 (owner: "won't suggest an unexplored door or area right next to me"): an unexplored spot within 3, entry 0 far off
      bfs(p0, false); int f = -1; for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] > 0 && b_dist[c] <= 3 && frontier(c) && (f < 0 || b_dist[c] < b_dist[f])) f = c;
      if (f >= 0) { st_near_n++; int d0 = b_dist[cell[0]]; bool any = false; for (int j = 0; j < n; j++) any |= distance(cell[j], f) <= 2;
        if (d0 > b_dist[f] + 5 && distance(cell[0], f) > 3) { st_near_skip++; st_near_any += !any; if (getenv("AUDV")) printf("  NEAR g%d d%d hero %d,%d unexplored %d,%d (%d) entry0 %d,%d (%d) src %d line %d way %d listed %d n %d\n", cur_game, G.depth, cx(p0), cy(p0), cx(f), cy(f), b_dist[f], cx(cell[0]), cy(cell[0]), d0, G.tgm.src0, s_tline, g_ovr.c >= 0 ? g_ovr.d & 63 : -1, any, n); if (getenv("AUDMAP")) aud_dump(cell[0], f); } } }
    if (n > 0 && cell[0] == p0 && kind[0] == TK_GO && !(g_ovr.d & 128)) { st_srch++; if (foe) { st_srch_foe++; if (getenv("AUDV") && !s_rollout && !s_is_child) printf("  AUDIT search with a foe g%d d%d hero %d\n", cur_game, G.depth, p0); }
      if (G.exit >= 0 && BIT_GET(G.visited, G.exit)) { bfs(p0, false); if (b_dist[G.exit] < 0x7FFF) { st_srch_exit++; if (getenv("AUDV") && !s_rollout && !s_is_child) printf("  AUDIT search, exit known g%d d%d hero %d\n", cur_game, G.depth, p0); } } }
    if (game_hungry()) { bool inbag = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(G.hero.bag[i].kind) || G.hero.bag[i].kind == IK_MYSTERY_MEAT) inbag = true;
      if (!inbag) for (int i = 0; i < G.n_heaps; i++) { Heap *hh = &G.heaps[i]; if (hh->type != HEAP_PLAIN || hh->pos >= MAP_LEN || !BIT_GET(G.visited, hh->pos) || distance(hh->pos, p0) > 3 || !(is_food(hh->item.kind) || hh->item.kind == IK_MYSTERY_MEAT)) continue;
        st_food_near++; if (!(n > 0 && cell[0] == hh->pos)) { st_food_left++; if (getenv("AUDV") && !s_rollout && !s_is_child) printf("  AUDIT food left g%d d%d hero %d food %d entry0 %d/%d starving %d bag %d drop %d\n", cur_game, G.depth, p0, hh->pos, n > 0 ? cell[0] : -1, n > 0 ? kind[0] : -1, game_starving(), bag_used(), hh->pos == g_drop[0] || hh->pos == g_drop[1] || hh->pos == g_drop[2] || hh->pos == g_drop[3]); } break; } } }
  if (getenv("HANDSHOT") && n > 0 && kind[0] == TK_BAG && enemy_count() == 0 && !s_rollout && !s_is_child) { char nm[40]; snprintf(nm, sizeof nm, "hand_g%d_d%d_a%d", cur_game, G.depth, g_ovr.ha); ph_save(nm, cur_game, st_press_n); }   // 0.61: the hands' flying item, for the emulator
  if (n <= 0) { st_press++; walk(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? tap_safe(G.hero.pos) : bot_search()); return true; }
  if (!p_onscreen(cell[0])) { st_press_off++; if (V) printf("  OFFSCREEN entry 0 %d hero %d\n", cell[0], G.hero.pos); }
  if (V) { printf("  press: hero %d (t0 %d) list", G.hero.pos, G.map[cell[0]]); for (int i = 0; i < n; i++) printf(" %d/%d", cell[i], kind[i]); printf("\n"); }
  if (V) for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && BIT_GET(G.fov, m->pos)) printf("   mob %d type %d state %d hp %d ally %d\n", m->pos, m->type, m->state, m->HP, ally(i)); }
  for (int i = i0; i < n; i++) {
    st_press++;
    int c = cell[i]; int32_t t = G.hero.time;
    if (i && i == i0 && !(kind[0] == TK_ENEMY && adjacent(cell[i], G.hero.pos))) { int m = kind[0] == TK_ENEMY ? mob_at(cell[0]) : -1; G.tgm.vs = G.tgm.src0; G.tgm.vu = m >= 0 ? G.mobs[m].uid : 255; G.tgm.vt = G.now + 15 * TICK; }   // 0.66: main.c pass_hands: the override vetoes plan A's part
    if (i && i == i0 && kind[i] != TK_ENEMY && !getenv("NOOVR")) { g_ovr.c = getenv("OLDOVR") ? c : g_tgobj[i] | 0x4000; g_ovr.t = G.hero.pos - 65536; st_ovr_set++; }   // main.c tg_do: not the first (0.47: the objective)
    bool hold = !i && (g_ovr.d & 128);                  // main.c tg_do: wait for the enemy a step short
    if (hold) { st_hold++; if (++st_hold_run > st_hold_max) st_hold_max = st_hold_run; } else st_hold_run = 0;
    bool mine = kind[i] == TK_MINE;
    if (mine) st_mine++; if (kind[i] == TK_VEIN) st_mine_walk++;
    if (kind[i] == TK_BAG && !s_pure && g_ovr.ha != ACN_EAT && g_ovr.ha != ACN_DRINK && g_ovr.ha != ACN_THROW && g_ovr.ha != ACN_ZAP) continue;
    if (i && (kind[0] == TK_BAG || kind[0] == TK_REST)) { int j = G.tgm.dn++ & 7; G.tgm.dk[j] = kind[0] == TK_REST ? 0 : game_slot(g_ovr.hs)->kind; G.tgm.da[j] = kind[0] == TK_REST ? 255 : g_ovr.ha; G.tgm.dd = G.depth; }   // (main.c tg_do: the hands passed over)   // 0.61: the hands (main.c tg_do), those with no aiming or choosing (gear: the bot's own)
    ActResult r = mine ? game_item_do(g_ovr.ms, ACN_MINE) : kind[i] == TK_BAG ? do_item(g_ovr.hs, (ItemAction)g_ovr.ha) : kind[i] == TK_REST ? game_rest(true) : hold ? game_rest(false) : (G.hero.b.roots || c == G.hero.pos) && kind[i] == TK_GO ? bot_search() : getenv("RAWTAP") ? game_tap_cell(c) : tap_safe(c);   // (0.77: RAWTAP: the watch's Select, no bot route-around)
    if (G.hero.pos != p0 && !getenv("NOHEAD")) g_from = p0;
    if (kind[i] == TK_VEIN && r == ACT_NONE && G.hero.time == t) { st_mine_none++; if (V) printf("  MINE-WALK did nothing: vein %d hero %d\n", c, G.hero.pos); }
    if (r != ACT_NONE || G.hero.time != t) {
      if (i > i0) { st_fb++; if (getenv("FBV")) printf("  FB g%d d%d entry0 %d t%d k%d hero %d heap %d\n", cur_game, G.depth, cell[0], G.map[cell[0]], kind[0], p0, heap_at(cell[0])); }
      walk(r);
      if (G.hero.pos != p0 && !s_rollout && !s_is_child) {   // 0.61: dithering: back on a cell visited twice in the last 24 moves, nothing new seen meanwhile, no enemy in sight
        static int16_t win[24]; static int wn, wd = -1; static int vis[24]; int nv = 0;
        for (int c = 0; c < MAP_LEN; c++) nv += BIT_GET(G.visited, c);
        if (wd != G.depth) { wn = 0; wd = G.depth; }
        int k = 0; for (int j = 0; j < 24 && j < wn; j++) if (win[j] == G.hero.pos) k++;
        int old = wn >= 24 ? vis[wn % 24] : -1;
        if (k >= 2 && old == nv && !enemy_count()) { st_dither++; if (getenv("AUDV")) printf("  AUDIT dither g%d d%d %d->%d entry %d/%d i%d\n", cur_game, G.depth, p0, G.hero.pos, c, kind[i], i); if (getenv("AUDMAP")) aud_dump(c, p0); }
        win[wn % 24] = G.hero.pos; vis[wn % 24] = nv; wn++; }
      if (G.hero.pos != p0 && !s_rollout && !s_is_child && G.hero.pos == s_pp[1] && s_pp[0] == p0) { st_turnback++; if (enemy_count()) st_turnback_foe++; if (getenv("AUDMAP")) aud_dump(c, p0); if (getenv("AUDV")) printf("  AUDIT back g%d d%d %d->%d entry %d/%d i%d foes %d\n", cur_game, G.depth, p0, G.hero.pos, c, kind[i], i, enemy_count()); }   // 0.61: A, B, back to A
      if (G.hero.pos != p0) { if (G.hero.pos == s_pp[1] && s_pp[0] == p0 && s_pp[2] == G.hero.pos) { st_osc++; if (getenv("AUDV") && !s_rollout && !s_is_child) printf("  AUDIT osc g%d d%d %d<->%d entry %d/%d i%d foes %d\n", cur_game, G.depth, p0, G.hero.pos, c, kind[i], i, enemy_count()); } s_pp[2] = s_pp[1]; s_pp[1] = s_pp[0]; s_pp[0] = G.hero.pos; }
      return true;
    }
  }
  walk(game_rest(false)); return true;                  // (nothing on the list did anything: wait)
}
// one decision; returns false if the bot has nothing at all to do
static bool foe_near(int r) { for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE) continue; int d = distance(m->pos, G.hero.pos); if ((MOB_DEFS[m->type].flags & MD_WATER) ? d <= 1 : d <= r) return true; } return false; }
static int s_ambush; static bool s_olddark; static int16_t s_trail[8]; static int s_trail_n;
static bool amb_retreat(void) {                          // shot from the dark: back the way you came, out of its line
  Hero *h = &G.hero; int back = -1;
  for (int k = 4; k >= 2 && back < 0; k--) { int c = s_trail[(s_trail_n - k) & 7]; if (s_trail_n >= k && c != h->pos && c >= 0 && c < MAP_LEN && c != G.entrance && c != G.exit) back = c; }
  if (back < 0) return false;
  int32_t t = h->time; ActResult r = tap_safe(back); if (r == ACT_NONE && h->time == t) return false;
  s_did[0] = 0; DID("hurt by something unseen: back out of its sight"); walk(r); return true;
}
// 0.49 stat: the search phase: turns from "everything reachable seen, the way down not found" till it's found (or the floor's left)
static long st_sp_turns; static int st_sp_n, s_sp_depth = -1; static int32_t s_sp_t0 = -1;
static void sp_stat(void) {
  Hero *h = &G.hero;
  if (s_sp_t0 >= 0 && h->time < s_sp_t0) s_sp_t0 = -1;   // (a new game)
  if (G.depth != s_sp_depth) { if (s_sp_t0 >= 0) { st_sp_turns += (h->time - s_sp_t0) / TICK; st_sp_n++; } s_sp_t0 = -1; s_sp_depth = G.depth; }
  bool stuck = G.exit >= 0 && !BIT_GET(G.visited, G.exit) && !boss_depth(G.depth);
  if (stuck) { static int16_t keep[MAP_LEN]; memcpy(keep, b_dist, sizeof keep); bfs(h->pos, false);
    for (int c = 0; c < MAP_LEN && stuck; c++) if (b_dist[c] < 0x7FFF && frontier(c)) stuck = false;
    memcpy(b_dist, keep, sizeof keep); }
  if (stuck && s_sp_t0 < 0) { s_sp_t0 = h->time;
    if (getenv("SPDUMP")) { printf("SEARCH PHASE game %d d%d hero %d,%d\n", cur_game, G.depth, cx(h->pos), cy(h->pos));
      for (int y = 0; y < MAP_H; y++) { printf("  %2d ", y); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c];
        putchar(c == h->pos ? '@' : t == T_SECRET_DOOR ? (BIT_GET(G.visited, c) ? 'S' : 's') : !BIT_GET(G.visited, c) ? (PASSABLE(t) ? ',' : ' ') : t == T_WALL || t == T_WALL_DECO ? '#' : t == T_DOOR || t == T_OPEN_DOOR ? '+' : PASSABLE(t) ? '.' : '?'); }
        putchar('\n'); } } }
  if (!stuck && s_sp_t0 >= 0 && BIT_GET(G.visited, G.exit)) { st_sp_turns += (h->time - s_sp_t0) / TICK; st_sp_n++; if (getenv("SPDUMP") || getenv("SPEND")) printf("SEARCH PHASE END game %d d%d turns %d\n", cur_game, G.depth, (h->time - s_sp_t0) / TICK); s_sp_t0 = -1; }
}
// 0.61 (owner: "eating before starving is a waste"): hunger stops rising at Starving, so every turn spent there is a
// turn no food pays for; it costs ~1 HP a 33 turns and the regeneration (1 a 10 turns when hurt). EATLATE=p: eat only once
// starving and under p% of full health, or with a hard fight coming (a boss, or two hunting you, none near yet)
static int s_eatlate = -1, s_warreat;
static bool eat_late(void) {
  Hero *h = &G.hero; if (!game_starving()) return false;
  if (h->HP * 100 < h->HT * s_eatlate) return true;
  int hunt = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & MD_NPC)) continue;
    if (MOB_DEFS[m->type].flags & MD_BOSS) return true; if (m->state == ST_HUNTING) hunt++; }
  return hunt >= 2;
}
// WARREAT=1: a warrior near death in a fight eats for Food.execute's 5 HP (3 turns), none beside him; WARREAT=2: even with one beside him
static bool warrior_eat(void) {
  Hero *h = &G.hero;
  if (!s_warreat || (h->hclass & 3) != 0 || h->HP * 4 >= h->HT || !enemy_count() || (s_warreat == 1 && foe_near(1))) return false;
  if (find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) return false;   // (the potion first: emergency())
  static const uint8_t PREF[5] = { IK_OVERPRICED, IK_CHARGRILLED, IK_CARPACCIO, IK_RATION, IK_PASTY };   // (the least food first)
  for (int j = 0; j < 5; j++) { int s = find_kind(PREF[j]); if (s >= 0 && s < BAG_SIZE) { st_warreat++; DID("a warrior's last 5 HP: eat"); do_item(s, ACN_EAT); return true; } }
  return false;
}
// 0.64 oracles: upper bounds on what item management could gain (not fair play: research only)
//   ORACLE_ID=1: every potion, scroll, wand and ring kind known from the start; ORACLE_GEAR=1: every carried weapon,
//   armour, ring and wand fully identified (level, curse, charges); ORACLE_FOOD=1: never hungry
static void oracles(void) {
  static int8_t o_id = -1, o_gear, o_food; if (o_id < 0) { o_id = getenv("ORACLE_ID") != NULL; o_gear = getenv("ORACLE_GEAR") != NULL; o_food = getenv("ORACLE_FOOD") != NULL; }
  if (o_food) G.hero.hunger = 0;
  if (o_id) for (int k = 1; k < IK_COUNT; k++) { int c = item_cat(k); if (c == CAT_POTION || c == CAT_SCROLL || c == CAT_WAND || c == CAT_RING) set_known(k); }
  if (o_gear) for (int s = -4; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) { Item *it = game_slot(s); int c = item_cat(it->kind);
    if (c == CAT_WEAPON || c == CAT_ARMOR || c == CAT_RING || c == CAT_WAND) it->flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN; }
}
// 0.93 (owner: "calculate the odds of what might be on a floor and leave early"): Dungeon.posNeeded / souNeeded, from the
// source: Strength potions 2 by floor 4, 4 by 9, 6 by 14, 8 by 19, 9 by 24; Upgrades 3 by 5, 6 by 10, 9 by 15, 12 by 20,
// 13 by 25; on floor d the chance is (still owed) / (floors to the deadline). The count spawned isn't known (a floor left
// half seen may have held one), so it's kept as odds over 0..15, updated on leaving each floor with what was found there
// and how much of it went unseen. Unknown scrolls / potions picked up there count as half a find (no peeking at kinds).
static const uint8_t Q_POS[10] = { 4, 2, 9, 4, 14, 6, 19, 8, 24, 9 }, Q_SOU[10] = { 5, 3, 10, 6, 15, 9, 20, 12, 25, 13 };
static int s_q_lab0[IK_COUNT]; static double s_q_sou[16], s_q_pos[16], s_q_u = 1, s_q_fs, s_q_fp; static int s_q_d = -1, s_q_game = -1, s_q_us0, s_q_up0, s_q_sou0, s_q_str0;
static double q_spawn(const uint8_t *Q, int n, int d) { if (d == 21 || boss_depth(d)) return 0;   /* (review: level_content rolls no quota on a boss floor) */ for (int i = 0; i < 10; i += 2) if (d <= Q[i]) { double p = (double)(Q[i + 1] - n) / (Q[i] - d + 1); return p < 0 ? 0 : p > 1 ? 1 : p; } return 0; }
static double q_unseen(const double *P, const uint8_t *Q, int d, double f, double u) {   // the chance one is still in the unseen part here
  double r = 0; for (int n = 0; n < 15; n++) if (P[n] > 0) { double sp = q_spawn(Q, n, d), den = 1 - sp * (1 - u); r += P[n] * (den > 1e-9 ? sp * u / den : 0); } return (1 - f) * r; }
static void q_leave(double *P, const uint8_t *Q, int d, double f, double u) {   // the odds over the count spawned, after floor d
  double N[16] = { 0 }; for (int n = 0; n < 15; n++) if (P[n] > 0) { double sp = q_spawn(Q, n, d), den = 1 - sp * (1 - u), ps = f + (1 - f) * (den > 1e-9 ? sp * u / den : 0); N[n + 1] += P[n] * ps; N[n] += P[n] * (1 - ps); }
  double t = 0; for (int n = 0; n < 16; n++) t += N[n]; for (int n = 0; n < 16; n++) P[n] = t > 0 ? N[n] / t : (n == 0); }
static int q_unknown(int cat) { int q = 0; for (int i = 0; i < BAG_SIZE; i++) if (item_cat(G.hero.bag[i].kind) == cat && !item_known(G.hero.bag[i].kind)) q += G.hero.bag[i].qty; return q; }
static double floor_unseen(void) { int seen = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) seen++; double est = seen + 40 > 300 ? seen + 40 : 300, un = 1 - seen / est; return un < 0 ? 0 : un; }
// 0.93 IDB (owner: "worth the average of the yet-unidentified, weighted by their drop rates"): what an unknown potion or
// scroll probably is. Each colour / label is an opaque name (the bot never reads its meaning). Evidence: how often each
// label has turned up against each kind's rate (Generator's weights; Strength and Upgrade come by the quota instead,
// about 0.38 and 0.52 a floor), the flies' drop (only ever Healing), and the shop's shelf (one Healing among its
// potions; Identify, Remove Curse and Magic Mapping among its scrolls). Each kind has exactly one label: the odds are
// balanced both ways (rows and columns) until they agree.
static const uint16_t IDB_PW[12] = { 45, 4, 15, 10, 15, 10, 0, 20, 12, 10, 0, 10 }, IDB_SW[12] = { 30, 10, 15, 10, 15, 12, 8, 8, 4, 6, 0, 1 };
static const uint8_t IDB_PK[12] = { IK_POT_HEALING, IK_POT_EXPERIENCE, IK_POT_TOXIC, IK_POT_PARALYTIC, IK_POT_FLAME, IK_POT_LEVITATION, IK_POT_STRENGTH, IK_POT_MIND, IK_POT_PURITY, IK_POT_INVIS, IK_POT_MIGHT, IK_POT_FROST };
static const uint8_t IDB_SK[12] = { IK_SCR_IDENTIFY, IK_SCR_TELEPORT, IK_SCR_REMOVE_CURSE, IK_SCR_RECHARGING, IK_SCR_MAPPING, IK_SCR_CHALLENGE, IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_PSIONIC, IK_SCR_MIRROR, IK_SCR_UPGRADE, IK_SCR_ENCHANT };
static double s_idn[IK_COUNT]; static uint8_t s_idfly[IK_COUNT]; static uint16_t s_idshop_p[8], s_idshop_s[8]; static int s_nidshop, s_idgame = -1, s_idmaxd, s_idq[IK_COUNT], s_idshop_d[8];
static int16_t s_idfpos[32]; static int s_nidf; static uint8_t s_idheap[MAP_LEN / 8 + 1]; static int s_iddepth = -1; static int32_t s_idgold; static int s_iddrop = -1;
static double s_idP[2][12][12]; static int s_idstep = -1;   // [potions/scrolls][label][kind index]
static double idb_rate(int pot, int i) { double r = pot ? 1.0 * IDB_PW[i] / 151.0 : 0.8 * IDB_SW[i] / 119.0; int k = pot ? IDB_PK[i] : IDB_SK[i];
  if (k == IK_POT_STRENGTH) r += 0.38; if (k == IK_SCR_UPGRADE) r += 0.52; if (k == IK_SCR_ENCHANT) r += 0.12; if (k == IK_POT_MIGHT) r += 0.01; if (k == IK_POT_LEVITATION || k == IK_POT_INVIS || k == IK_POT_FLAME) r += 0.05; return r; }
static void idb_solve(void) {
  if (s_idstep == s_bot_steps) return; s_idstep = s_bot_steps;
  for (int pot = 0; pot < 2; pot++) { const uint8_t *K = pot ? IDB_PK : IDB_SK; int base = pot ? IK_FIRST_POTION : IK_FIRST_SCROLL; double (*P)[12] = s_idP[pot];
    double M[12][12]; bool lu[12], ku[12];   // label unknown, kind unknown (a known kind's label is itself)
    for (int c = 0; c < 12; c++) lu[c] = !item_known(base + c); for (int i = 0; i < 12; i++) ku[i] = !item_known(K[i]);
    int F = s_idmaxd > 0 ? s_idmaxd : 1;
    for (int c = 0; c < 12; c++) for (int i = 0; i < 12; i++) { if (!lu[c] || !ku[i]) { M[c][i] = 0; continue; } double r = idb_rate(pot, i); M[c][i] = s_idn[base + c] * log(r) - r * F; }
    for (int c = 0; c < 12; c++) { if (!lu[c]) continue; double mx = -1e300; for (int i = 0; i < 12; i++) if (ku[i] && M[c][i] > mx) mx = M[c][i]; for (int i = 0; i < 12; i++) M[c][i] = ku[i] ? exp(M[c][i] - mx) : 0; }
    for (int c = 0; c < 12; c++) if (lu[c]) {   // the hard clues
      int L = base + c; if (pot && s_idfly[L]) for (int i = 0; i < 12; i++) if (K[i] != IK_POT_HEALING) M[c][i] = 0;
      if (pot) for (int j = 0; j < 12; j++) if (j != c && s_idfly[base + j] && !item_known(base + j)) for (int i = 0; i < 12; i++) if (K[i] == IK_POT_HEALING) M[c][i] = 0;
      for (int sh = 0; sh < s_nidshop; sh++) { uint16_t set = pot ? s_idshop_p[sh] : s_idshop_s[sh]; if (set & (1 << c)) continue;
        for (int i = 0; i < 12; i++) if (pot ? K[i] == IK_POT_HEALING : (K[i] == IK_SCR_IDENTIFY || K[i] == IK_SCR_REMOVE_CURSE || K[i] == IK_SCR_MAPPING)) M[c][i] = 0; } }
    for (int it = 0; it < 80; it++) {
      for (int c = 0; c < 12; c++) { double t = 0; for (int i = 0; i < 12; i++) t += M[c][i]; if (t > 0) for (int i = 0; i < 12; i++) M[c][i] /= t; }
      for (int i = 0; i < 12; i++) { double t = 0; for (int c = 0; c < 12; c++) t += M[c][i]; if (t > 0) for (int c = 0; c < 12; c++) M[c][i] /= t; } }
    for (int c = 0; c < 12; c++) { double t = 0; for (int i = 0; i < 12; i++) t += M[c][i]; for (int i = 0; i < 12; i++) P[c][i] = t > 0 ? M[c][i] / t : 0; } }
}
static double idb_p(int label, int kind) {              // the chance the unknown label is that kind (a known label: 1 or 0)
  if (item_known(label)) return label == kind; int pot = item_cat(label) == CAT_POTION; if (!pot && item_cat(label) != CAT_SCROLL) return 0;
  idb_solve(); const uint8_t *K = pot ? IDB_PK : IDB_SK; int c = label - (pot ? IK_FIRST_POTION : IK_FIRST_SCROLL);
  for (int i = 0; i < 12; i++) if (K[i] == kind) return s_idP[pot][c][i]; return 0; }
static void idb_track(void) {                           // each step (main game): finds by label, the flies' drops, the shops' shelves
  Hero *h = &G.hero; if (s_rollout || s_is_child) return;
  if (s_idgame != cur_game) { s_idgame = cur_game; memset(s_idn, 0, sizeof s_idn); memset(s_idfly, 0, sizeof s_idfly); s_nidshop = 0; s_idmaxd = 0; memset(s_idq, 0, sizeof s_idq); s_nidf = 0; s_iddepth = -1; s_idgold = h->gold; s_iddrop = -1; }
  if (G.depth > s_idmaxd) s_idmaxd = G.depth;
  if (s_iddepth != G.depth) { s_iddepth = G.depth; memset(s_idheap, 0, sizeof s_idheap); s_nidf = 0; }
  int q[IK_COUNT] = { 0 }; for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (item_cat(k) == CAT_POTION || item_cat(k) == CAT_SCROLL) q[k] += h->bag[i].qty; }
  for (int k = 0; k < IK_COUNT; k++) if (q[k] != s_idq[k]) { int d = q[k] - s_idq[k];
    if (d > 0 && h->gold >= s_idgold) s_idn[k] += d;        // found (not bought: the gold went down then)
    if (d < 0 && s_iddrop_k == k) s_idn[k] += d;              // dropped (it'll be picked up again)
    s_idq[k] = q[k]; }
  s_idgold = h->gold; s_iddrop = -1; s_iddrop_k = -1;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; int c = hp->pos, k = hp->item.kind; if ((unsigned)c >= MAP_LEN || !BIT_GET(G.fov, c) || BIT_GET(s_idheap, c)) continue;
    BIT_SET(s_idheap, c); if (hp->type == HEAP_PLAIN && item_cat(k) == CAT_POTION && !item_known(k)) for (int f = 0; f < s_nidf; f++) if (s_idfpos[f] == c) { s_idfly[k] = 1; if (getenv("IDBDBG")) printf("IDB g%d d%d: a potion where flies died: label %d is Healing\n", cur_game, G.depth, k); } }
  s_nidf = 0; for (int i = 0; i < G.n_mobs && s_nidf < 32; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_SWARM && BIT_GET(G.fov, G.mobs[i].pos)) s_idfpos[s_nidf++] = G.mobs[i].pos;
  if (G.depth == 6 || G.depth == 11 || G.depth == 16) { int ns = 0; uint16_t sp = 0, ss = 0; for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if (hp->type != HEAP_FOR_SALE || (unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos)) continue; ns++; int k = hp->item.kind;
      if (item_cat(k) == CAT_POTION) sp |= 1 << (k - IK_FIRST_POTION); if (item_cat(k) == CAT_SCROLL) ss |= 1 << (k - IK_FIRST_SCROLL); }
    if (ns >= 10) { int j = 0; while (j < s_nidshop && s_idshop_d[j] != G.depth) j++; if (j < 8) { if (j == s_nidshop) s_nidshop++; s_idshop_d[j] = G.depth; s_idshop_p[j] |= sp; s_idshop_s[j] |= ss; } } }
  if (getenv("IDBDBG") && !(s_bot_steps % atoi(getenv("IDBDBG")))) { printf("IDB g%d d%d step %d potions:", cur_game, G.depth, s_bot_steps); for (int c = 0; c < 12; c++) if (!item_known(IK_FIRST_POTION + c) && s_idn[IK_FIRST_POTION + c] > 0) printf(" L%d n%.0f heal %.2f str %.2f", c, s_idn[IK_FIRST_POTION + c], idb_p(IK_FIRST_POTION + c, IK_POT_HEALING), idb_p(IK_FIRST_POTION + c, IK_POT_STRENGTH));
    printf(" | scrolls:"); for (int c = 0; c < 12; c++) if (!item_known(IK_FIRST_SCROLL + c) && s_idn[IK_FIRST_SCROLL + c] > 0) printf(" L%d n%.0f upg %.2f", c, s_idn[IK_FIRST_SCROLL + c], idb_p(IK_FIRST_SCROLL + c, IK_SCR_UPGRADE)); printf("\n"); }
}
static void q_track(void) {                              // each step: the floor's finds so far; a new deepest floor: fold the last one in
  Hero *h = &G.hero; if (s_rollout || s_is_child) return;
  if (getenv("IDB") || getenv("IDBDBG") || fair_on()) idb_track();
  if (s_q_game != cur_game) { s_q_game = cur_game; memset(s_q_sou, 0, sizeof s_q_sou); memset(s_q_pos, 0, sizeof s_q_pos); s_q_sou[0] = s_q_pos[0] = 1; s_q_d = -1; }
  int sou = (int)st_sou_read + (find_kind(IK_SCR_UPGRADE) >= 0 && item_known(IK_SCR_UPGRADE) ? game_slot(find_kind(IK_SCR_UPGRADE))->qty : 0), str = h->STR + (find_kind(IK_POT_STRENGTH) >= 0 && item_known(IK_POT_STRENGTH) ? game_slot(find_kind(IK_POT_STRENGTH))->qty : 0);
  if (G.depth > s_q_d) {
    if (s_q_d >= 1) { q_leave(s_q_sou, Q_SOU, s_q_d, s_q_fs, s_q_u); q_leave(s_q_pos, Q_POS, s_q_d, s_q_fp, s_q_u); }
    memcpy(s_q_lab0, s_idq, sizeof s_q_lab0);
    s_q_d = G.depth; s_q_us0 = q_unknown(CAT_SCROLL); s_q_up0 = q_unknown(CAT_POTION); s_q_sou0 = sou; s_q_str0 = str; s_q_fs = s_q_fp = 0; s_q_u = 1; }
  if (G.depth != s_q_d) return;
  s_q_u = floor_unseen();
  s_q_fs = sou > s_q_sou0 ? 1 : q_unknown(CAT_SCROLL) > s_q_us0 ? 0.5 : 0; s_q_fp = str > s_q_str0 ? 1 : q_unknown(CAT_POTION) > s_q_up0 ? 0.5 : 0;
  if (getenv("IDB")) {   // 0.93: an unknown one found here counts as the chance it's the one (not a flat half)
    double ns = 1, np = 1; for (int c = 0; c < 12; c++) { int ks = IK_FIRST_SCROLL + c, kp = IK_FIRST_POTION + c, ds = s_idq[ks] - s_q_lab0[ks], dp = s_idq[kp] - s_q_lab0[kp];
      if (ds > 0 && !item_known(ks)) ns *= pow(1 - idb_p(ks, IK_SCR_UPGRADE), ds); if (dp > 0 && !item_known(kp)) np *= pow(1 - idb_p(kp, IK_POT_STRENGTH), dp); }
    if (s_q_fs < 1) s_q_fs = 1 - ns; if (s_q_fp < 1) s_q_fp = 1 - np; }
  if (sou > s_q_sou0) s_q_fs = 1; if (s_q_fs < 1 && q_unknown(CAT_SCROLL) < s_q_us0) s_q_us0 = q_unknown(CAT_SCROLL);   // (read or dropped: the baseline follows)
  if (s_q_fp < 1 && q_unknown(CAT_POTION) < s_q_up0) s_q_up0 = q_unknown(CAT_POTION);
}
static bool decide(void) {
  oracles();
  if (P && s_pure && !s_rollout) return press();   // 0.64: Assist alone: every move is Select on the watch's suggestion (windows still the bot's)
  pwater_map(); burnt_scan();
  memset(s_sale, 0, sizeof s_sale); s_sale_any = false; if (!getenv("OLDSALE")) for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE && G.heaps[i].pos < MAP_LEN) { BIT_SET(s_sale, G.heaps[i].pos); s_sale_any = true; }
  evidence_track();
  { static int s_alt_last = -1, s_alt_depth = -1; if (s_alt_depth != G.depth) { s_alt_depth = G.depth; s_alt_last = G.lvars[0]; } if (G.altar_pos >= 0 && s_alt_last > 1 && G.lvars[0] == 1 && !s_rollout && !s_is_child) st_altar_done++; s_alt_last = G.lvars[0]; }
  { static int s_hp_last = -1, s_seen_last = 1; static int32_t s_now_last; Hero *hh = &G.hero;   // 0.48: hurt by something out of sight (an evil eye in the dark): no resting
    bool dot = hh->b.burning || hh->b.poison || hh->b.bleed || game_starving() || bad_air(hh->pos) || hh->ooze;   // (not a foe seen then or now: one just killed hurt you first)
    int seen = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && distance(G.mobs[i].pos, hh->pos) <= 8 && (BIT_GET(G.fov, G.mobs[i].pos) || adjacent(G.mobs[i].pos, hh->pos))) seen = 1;
    if (!s_olddark && s_hp_last >= 0 && hh->HP < s_hp_last && G.now > s_now_last && !dot && !seen && !s_seen_last && view_distance() < 8) s_ambush = s_bot_steps + 12;
    s_hp_last = hh->HP; s_now_last = G.now; s_seen_last = seen || enemy_count() > 0; }
  { int s = find_kind(IK_TORCH); Hero *hh = &G.hero;  // 0.48: the halls are dark (you see 1-3, an evil eye 4): a torch
    if (!s_olddark && s >= 0 && s < BAG_SIZE && !hh->light && view_distance() < 4 && enemy_count() == 0) { DID("light a torch"); do_item(s, ACN_LIGHT); return true; } }                                        // bot3: fire can turn a burnt door beside the pool into water
  Hero *h = &G.hero; const char *why = "?";
  if (s_brew_kind >= 0 && s_brew_i >= 3 && (unsigned)s_brew_pot < MAP_LEN) { int hb = heap_at(s_brew_pot); if (hb >= 0 && item_cat(G.heaps[hb].item.kind) == CAT_POTION) { int pk = G.heaps[hb].item.kind; if (!item_known(pk) && !s_pot_guess[pk]) s_pot_guess[pk] = seed_potion(s_brew_kind); s_brew_kind = -1; } }   // (the brew's colour: probably that seed's potion)
  if (V) printf("  decide: hero %d hp %d/%d hunger %d blind %d light %d vd %d\n", h->pos, h->HP, h->HT, h->hunger, h->b.blind, h->light, view_distance());
  // eat
  bool buffrush = s_pztest && ((h->b.invis > 0 && pool_puzzle_pending()) || (h->b.levitation > 0 && trap_puzzle_pending()));   // 0.92 (seed 84: ate while floating, the ration left behind): the prize first
  if (!(buffrush && !game_starving()) && (s_eatlate >= 0 ? eat_late() : s_oldeat ? game_hungry() : (game_starving() || (((!getenv("OLDEAT2")) || getenv("THRIFT")) && game_hungry() && ({ bool r = false; for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == IK_RATION || h->bag[i].kind == IK_CHARGRILLED || h->bag[i].kind == IK_CARPACCIO || h->bag[i].kind == IK_OVERPRICED) r = true; r; }))) && !foe_near(3))) {   // 0.58 EATEARLY: a ration (260) at hungry (260+) wastes nothing, and a fight while starving has no healing   // 0.47n: only once starving (PD: hungry costs nothing, a ration eaten early wastes up to 100 turns), never with a foe at hand
    int s = -1; for (int i = 0; i < BAG_SIZE && s < 0; i++) if (is_food(h->bag[i].kind) && !(((!getenv("OLDEAT2")) || getenv("THRIFT")) && !game_starving() && h->bag[i].kind == IK_PASTY)) s = i;
    if (s < 0) s = find_kind(IK_MYSTERY_MEAT);
    if (s < 0 && game_starving() && (!s_pztest || getenv("OLDFISH"))) s = find_kind(IK_PHANTOM_FISH) >= 0 && G.q.wand != QS_GIVEN ? find_kind(IK_PHANTOM_FISH) : -1;   /* (0.93 audit: the phantom fish feeds nothing) */
    if (s >= 0 && h->bag[s].kind == IK_MYSTERY_MEAT && !IS_WATER(G.map[h->pos]) && !h->b.levitation && !getenv("OLDMEAT")) {   // 0.47n: 1 in 5 it sets you alight:
      bfs(h->pos, false); int w = -1, wd = 13;           // eat it standing in water (that puts the flames out), else only when healthy
      for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && !PWATER(c) && b_dist[c] < wd && heap_at(c) < 0 && c != G.entrance && c != G.exit) { wd = b_dist[c]; w = c; }
      if (w >= 0 && enemy_count() == 0) { DID("to the water to eat the meat"); walk(tap_safe(w)); return true; }
      if (h->HP < 20 && h->HP < h->HT * 2 / 3 && !(game_starving() && h->HP > 8)) s = -1;
    }
    if (s >= 0) { st_uses[ACN_EAT]++; do_item(s, ACN_EAT); return true; }
  }
  if (warrior_eat()) return true;
  { bool any = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_PIRANHA) { any = true; if (BIT_GET(G.fov, G.mobs[i].pos)) s_piranhas = true; }
    if (!any || s_brave) s_piranhas = false; }   // (seen once, and still about; unless there's no other way)
  if (h->b.paralysis || h->b.frost) { walk(game_rest(false)); return true; }
  if (getenv("GOODBG") && !s_rollout && G.depth == 5 && s_goo_calm > 0 && G.now < s_goo_calm + 8 * TICK) { int gi = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO) gi = i; printf("  CALMDBG step %d now %d calm %d goo %d state %d fov %d threat %d\n", s_bot_steps, G.now, s_goo_calm, gi, gi >= 0 ? G.mobs[gi].state : -1, gi >= 0 ? BIT_GET(G.fov, G.mobs[gi].pos) : -1, threat_count()); }
  if (s_pztest && !h->ooze && G.now >= s_goo_calm && G.now < s_goo_calm + 6 * TICK && G.now >= 0 && ({ bool unseen = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && G.mobs[i].state != ST_SLEEPING && !BIT_GET(G.fov, G.mobs[i].pos)) unseen = true; unseen; }) && threat_count() == 0) {
    DID("Goo: out of sight after its pump: let it come to me"); walk(game_rest(false)); return true; }   // 0.92 (seed 84: walked back round the corner into the pumped jump, 16 HP): it comes to me
  if (G.now < s_goo_calm && G.now >= 0) { bool adj = false; for (int i = 0; i < G.n_mobs; i++) { Mob *gm = &G.mobs[i]; if (gm->HP > 0 && gm->type == MT_GOO) { bool hit; if (adjacent(gm->pos, h->pos) || (gm->aux2 && distance(gm->pos, h->pos) <= 2 && ballistica(gm->pos, h->pos, &hit) == h->pos)) adj = true; } }   // (still in its jump: keep moving)
    if (!adj && !getenv("GOO_NOCALM")) {
      if (G.hero.ooze && !IS_WATER(G.map[h->pos])) for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !IS_WATER(G.map[q]) || char_at(q)) continue;   // the pause: wash the ooze off, out of its reach
        bool ok = true; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && distance(G.mobs[i].pos, q) <= 2) ok = false;
        if (ok) { DID("Goo: wash the ooze off during its pump"); walk(tap_safe(q)); return true; } }
      DID("Goo: wait out its pump"); walk(game_rest(false)); return true; } }
  if (!getenv("OLDQBUG") && !h->weapon.kind && !h->b.paralysis) {   // 0.54: a monk knocked it out of my hand (it lies at my feet): pick it up, back in hand
    int hi = heap_at(h->pos); if (hi >= 0 && G.heaps[hi].type == HEAP_PLAIN && item_cat(G.heaps[hi].item.kind) == CAT_WEAPON && bag_used() < BAG_SIZE) { int32_t t0 = h->time; ActResult rr = game_tap_cell(h->pos); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("pick my weapon back up"); walk(rr); return true; } }
    int bw = -1; double bs = 0; for (int i = 0; i < BAG_SIZE; i++) { double sc = weapon_score(&h->bag[i]); if (sc > bs) { bs = sc; bw = i; } }
    if (bw >= 0) { s_did[0] = 0; DID("back in hand: a weapon"); do_item(bw, ACN_EQUIP); return true; } }
  if (!(s_pztest && ({ bool cu = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_WRAITH && G.mobs[i].aux2 == 1 && BIT_GET(G.fov, G.mobs[i].pos)) cu = true; cu; })) && lookahead()) return true;   // 0.92 (seed 84: the curse fight was judged winnable played straight, then the look-ahead fled up the stairs and it healed): play it as judged
  if (P) {                                              // the player's own decisions stay: eat (above), heal, items, rest
    if (!h->weapon.kind && enemy_count()) for (int i = 0; i < BAG_SIZE; i++) if (weapon_score(&h->bag[i]) > 0) { do_item(i, ACN_EQUIP); return true; }
    if (emergency()) return true;
    if ((h->HP < h->HT / 2 || (s_resting && h->HP < h->HT)) && !game_starving() && enemy_count() == 0) { DID("rest to heal"); s_resting = true; int32_t t = h->time; walk(game_rest(false)); if (h->time > t) return true; }   // bot2: rest to (nearly) full
    if (enemy_count() == 0 && use_items_when_safe()) return true;
    return press();
  }
  bool fish_near = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_PIRANHA && distance(G.mobs[i].pos, h->pos) <= 1) fish_near = true;
  if (fish_near && IS_WATER(G.map[h->pos]) && !h->b.roots) {   // out of the water, the shortest way
    bfs(h->pos, false); int best = -1, bd = 0x7FFF;
    for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < bd && !IS_WATER(G.map[c]) && PASSABLE(G.map[c]) && G.map[c] != T_ALCHEMY && c != G.entrance && c != G.exit && heap_at(c) < 0) { bd = b_dist[c]; best = c; }
    if (best >= 0) { int32_t t = h->time; ActResult r = tap_safe(best); if (r != ACT_NONE || h->time != t) { walk(r); return true; } }
  }
  if (h->b.roots && enemy_count() == 0) { walk(game_rest(false)); return true; }
  if (h->b.burning && !IS_WATER(G.map[h->pos])) {      // on fire: into the water if it's close (bot2: up to 8 steps, piranha water too)
    bfs(h->pos, false); int best = -1, bd = 9;
    for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && b_dist[c] < bd && c != G.entrance && c != G.exit && heap_at(c) < 0) { bd = b_dist[c]; best = c; }
    if (best >= 0) { ActResult r = tap_safe(best); if (V) printf("  to water %d from %d: %d (dist %d) action %d\n", best, h->pos, r, bd, G.action); if (r != ACT_NONE) { walk(r); return true; } }
  }
  if ((getenv("X_OOZE") || s_pztest) && h->ooze && !IS_WATER(G.map[h->pos]) && !h->b.roots) {   // X (0.92: on under PZTEST; seed 84: the Goo fled in terror, the ooze ate the bot at rest): ooze never wears off: wash it in the nearest water
    bool gc = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos) && (G.mobs[i].type == MT_GOO || (G.mobs[i].type == MT_ROTTING_FIST && !getenv("OLDYOG"))) && distance(G.mobs[i].pos, h->pos) <= (G.mobs[i].type == MT_GOO ? 2 : 4)) gc = true;   // (0.92: one I can see)
    if (s_pztest && h->HP * 10 < h->HT * 4) gc = false;   // 0.92 (seed 84: 6 HP, oozing, the Goo fled in terror: it rested and died): under 40% the water comes first whatever's near   // (0.54: not with the rotting fist near: in water it mends)
    if (!gc) { bfs(h->pos, false); int best = -1, bd = 40;
      for (int c = 0; c < MAP_LEN; c++) if (IS_WATER(G.map[c]) && b_dist[c] < bd && !PWATER(c) && !char_at(c)) { bool nearg = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && distance(G.mobs[i].pos, c) <= 1) nearg = true; if (!nearg) { bd = b_dist[c]; best = c; } }
      if (best >= 0) { int32_t t0 = h->time; ActResult r = tap_safe(best); if (r != ACT_NONE || h->time != t0) { DID("ooze: to the water to wash it off"); walk(r); return true; } } } }
  if (getenv("X_OPENFIX") && s_step_back >= 0 && ({ bool p = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && G.mobs[i].aux2 && distance(G.mobs[i].pos, h->pos) <= 2) p = true; p; })) s_step_back = -1;   // X: the pump dodge decides
  if (s_step_back >= 0 && step_back()) return true;
  if (s_onto >= 0) { int c = s_onto; s_onto = -1; if (c != h->pos && adjacent(c, h->pos) && plant_at(c) >= 0 && !char_at(c)) { DID("onto the plant"); walk(game_tap_cell(c)); return true; } }
  if (escape_air()) return true;
  if (h->b.burning && !IS_WATER(G.map[h->pos])) {      // bot2: on fire, no water near: off flammable ground (grass keeps relighting you)
    int best = -1, bs = -1;
    for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (!nb_ok(h->pos, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & (F_FLAMABLE | F_AVOID)) || char_at(n) || (!s_oldfire && (bad_air(n) || BURNT(n)))) continue;
      int sc = 0; for (int j = 0; j < 8; j++) { int m = n + N8[j]; if (nb_ok(n, m) && !(TFLAGS(G.map[m]) & F_FLAMABLE)) sc++; } if (sc > bs) { bs = sc; best = n; } }
    if (best >= 0 && (TFLAGS(G.map[h->pos]) & F_FLAMABLE || enemy_count() == 0)) { DID("on fire: step off the grass"); walk(tap_safe(best)); return true; }
  }
  { int hp0 = heap_at(h->pos);                         // something at your feet (knocked from your hands...): pick it up
    if (hp0 >= 0 && G.heaps[hp0].type == HEAP_PLAIN && (!BIT_GET(s_dropped, h->pos) || ((h->pos == s_stash || in_stash(h->pos)) && stash_ready())) && s_black[h->pos] <= s_bot_steps && G.map[h->pos] != T_ALCHEMY && (bag_used() < BAG_SIZE || (item_stackable(G.heaps[hp0].item.kind) && find_kind(G.heaps[hp0].item.kind) >= 0) || item_cat(G.heaps[hp0].item.kind) == CAT_KEY || G.heaps[hp0].item.kind == IK_GOLD || G.heaps[hp0].item.kind == IK_DEW)
        && (G.heaps[hp0].item.kind != IK_DEW || getenv("OLDDEW2") || dew_take())
        && (enemy_count() == 0 || item_cat(G.heaps[hp0].item.kind) == CAT_WEAPON || h->b.invis || !foe_near(3))) {   // (bot3: a piranha in its pool or a far foe: pick it up all the same)   // (bot3: invisible: take it, nobody sees)
      int32_t t = h->time; ActResult r = tap_safe(h->pos); if (r == ACT_NONE && h->time == t) s_black[h->pos] = s_bot_steps + 100; else { walk(r); return true; } } }
  if (!h->weapon.kind && enemy_count()) for (int i = 0; i < BAG_SIZE; i++) if (weapon_score(&h->bag[i]) > 0) { do_item(i, ACN_EQUIP); return true; }   // back in hand
  if (prep_retreat()) return true;
  if (getenv("AVOID") && (!getenv("AVOIDCLS") || (h->hclass & 3) == atoi(getenv("AVOIDCLS"))) && !h->b.roots && !boss_depth(G.depth) && floor_left() && s_avoid_n < (getenv("AVOIDN") ? atoi(getenv("AVOIDN")) : 150)) {   // 0.58 (owner: "avoid combat until the level's power is gathered; if you can run and escape, do so: pop behind a door")
    int foe = -1, fd = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *fm = &G.mobs[i]; if (fm->HP <= 0 || ally(i) || fm->state != ST_HUNTING || !BIT_GET(G.fov, fm->pos) || (MOB_DEFS[fm->type].flags & (MD_BOSS | MD_NPC | MD_WATER))) continue;
      if (MOB_DEFS[fm->type].move_cost < hero_move_cost()) continue;   // (a faster one can't be outrun; Haste changes that)
      int dd = distance(fm->pos, h->pos); if (dd < fd) { fd = dd; foe = i; } }
    if (foe >= 0 && fd >= 2 && !(getenv("AVOIDFAV") && hp_cost_vs(G.mobs[foe].type, h->weapon.kind ? &h->weapon : NULL, h->armor.kind ? &h->armor : NULL) * 3 < h->HP)) {
      static int16_t md[MAP_LEN]; int fp = G.mobs[foe].pos; bfs(fp, false); for (int c = 0; c < MAP_LEN; c++) md[c] = b_dist[c];
      bfs(h->pos, false); int best = -1, bs = 1 << 30;
      for (int c = 0; c < MAP_LEN; c++) { int hd = b_dist[c]; if (hd < 1 || hd > 12 || char_at(c) || (TFLAGS(G.map[c]) & F_AVOID) || PWATER(c)) continue; int m2 = md[c] < 0x7FFF ? md[c] : 99;
        if (m2 - hd < 2) continue; bool hh; if (ballistica(fp, c, &hh) == c) continue;   // (it gets there first, or still sees me there)
        int sc = hd * 4 - (m2 - hd) * 2 + ((G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR) ? -6 : 0); if (sc < bs) { bs = sc; best = c; } }
      if (best >= 0) { int32_t t0 = h->time; ActResult rr = tap_safe(best); if (rr != ACT_NONE || h->time != t0) { s_avoid_n++; s_did[0] = 0; DID("avoid the %s: out of its sight (fight later)", mob_name(G.mobs[foe].type)); walk(rr); return true; } } } }
  if ((getenv("STAIRFLEE") || getenv("THRIFT")) && G.depth > 1 && G.entrance >= 0 && (getenv("THRIFT") ? h->HP < thrift_need() : h->HP * 2 < h->HT) && !h->b.roots && !boss_depth(G.depth) && !(s_zone_start && boss_depth(G.depth - 1)) && G.now - s_last_up > 100 * TICK) {   // 0.58: hurt and one hunting me: up the stairs (they don't follow), rest, come back
    int foe = -1, fd = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *fm = &G.mobs[i]; if (fm->HP <= 0 || ally(i) || fm->state != ST_HUNTING || !BIT_GET(G.fov, fm->pos) || (MOB_DEFS[fm->type].flags & (MD_BOSS | MD_NPC | MD_WATER))) continue; int dd = distance(fm->pos, h->pos); if (dd < fd) { fd = dd; foe = i; } }
    if (foe >= 0 && fd >= 2) { bfs(h->pos, false); if (b_dist[G.entrance] <= 8 && distance(G.mobs[foe].pos, G.entrance) > b_dist[G.entrance]) {
      if (h->pos == G.entrance) { s_last_up = G.now; s_did[0] = 0; DID("hurt, one hunting me: up the stairs"); walk(game_tap_cell(h->pos)); return true; }
      int32_t t0 = h->time; ActResult rr = tap_safe(G.entrance); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; st_kit[5]++; DID("hurt, one hunting me: to the stairs up"); walk(rr); return true; } } } }
  if (getenv("GOO2") && !h->b.roots) for (int i = 0; i < G.n_mobs; i++) { Mob *gm = &G.mobs[i];   // 0.58: in the middle of the Goo dance (it may be round a corner): the dance, nothing else
    if (gm->HP <= 0 || gm->type != MT_GOO || gm->state != ST_HUNTING || distance(gm->pos, h->pos) > 5 || !(BIT_GET(G.fov, gm->pos) || G.now - s_goo2_last <= 4 * TICK)) continue;
    int r = goo2(gm); if (r == 1) return true; if (r == 0) { walk(game_tap_cell(gm->pos)); return true; } break; }
  if (s_pztest && ((h->b.invis > 2 && pool_puzzle_pending()) || (h->b.levitation > 2 && trap_puzzle_pending())) && !({ bool adjh = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && G.mobs[i].state == ST_HUNTING && adjacent(G.mobs[i].pos, h->pos) && !(MOB_DEFS[G.mobs[i].type].flags & MD_WATER)) adjh = true; adjh; })) {   // 0.92 (seed 84 d3: turned invisible at the pool, then swung at a passing crab and lost it): the prize first, the buff won't wait
    const char *pw = NULL; int pt = puzzle_task(&pw); if (pt == -2) { DID("%s", pw); return true; }
    if (pt >= 0 && pw && (!strncmp(pw, "traps: levitating", 17) || !strncmp(pw, "pool: invisible", 15))) { int32_t t0 = h->time; ActResult rr = tap_safe(pt); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("%s (before anything else)", pw); walk(rr); return true; } } }
  if (fight()) return true;
  if (getenv("DEWREST") && find_kind(IK_DEW_VIAL) >= 0 && G.dew < 10 && h->HP < h->HT && h->HP * 5 >= h->HT * 4 && h->hunger < 260 && enemy_count() == 0 && !gas_at(h->pos)) {   // 0.78 (owner): fed, nearly full,
    bool dew = false; for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_DEW && G.heaps[i].type == HEAP_PLAIN && BIT_GET(G.visited, G.heaps[i].pos)) dew = true;   // dew about: rest to full first, so it fills the vial
    if (dew) { st_dewrest++; DID("rest to full: the dew then fills the vial"); walk(game_rest(false)); return true; } }
  if (!getenv("OLDMOBS") && s_thief_uid >= 0) {        // the thief out of sight with my item: to where it was last seen
    int ti = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].uid == s_thief_uid) ti = i;
    if (ti < 0 || h->pos == s_thief_cell || s_thief_cell < 0) s_thief_uid = -1;
    else { int32_t t0 = h->time; ActResult r = tap_safe(s_thief_cell); if (r != ACT_NONE || h->time != t0) { st_thief_chase++; DID("after the thief (it has my %s)", "item"); walk(r); return true; } s_thief_uid = -1; } }
  if (s_kite2 && s_kite_uid >= 0 && h->pos != s_kite_cell && s_bot_steps - s_kite_t < 25 && distance(h->pos, s_kite_cell) <= 6 && enemy_count() == 0 && ({ bool al = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].uid == s_kite_uid && G.mobs[i].state == ST_HUNTING) al = true; al; })) {   // 0.91 (owner: "door trick was not done properly, this costs like 10 turns"): out of its sight it's still coming: back to the spot, not off to the item it guards
    int32_t t0 = h->time; ActResult rr = tap_safe(s_kite_cell); if (rr != ACT_NONE || h->time != t0) { DID("door kite: back to my spot, it's still coming"); walk(rr); return true; } }
  if (!getenv("OLDKITE") && s_kite_uid >= 0 && h->pos == s_kite_cell && s_bot_steps - s_kite_t < 25 && ({ bool al = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].uid == s_kite_uid && G.mobs[i].state == ST_HUNTING) al = true; al; })) { DID("door kite: the door's shut, it's coming: wait"); walk(game_rest(false)); return true; }   // (review: it waited in 6 of 68 kites: once the door shut, the bot wandered off)
  if (!getenv("OLDMOBS") && s_lurk_until > s_bot_steps && enemy_count() == 0 && h->HP > h->HT / 3) { DID("wait round the corner for it"); walk(game_rest(false)); return true; }
  if (s_goo_spot == h->pos && !getenv("GOO_NOHOLD")) {   // bot3 (owner): at the spot by Goo's door, it's awake and coming: wait for it there
    for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && G.mobs[i].state == ST_HUNTING && s_goo_hold++ < 12) { DID("Goo: hold the spot by its door"); walk(game_rest(false)); return true; } }
  { bool sheep = false; for (int k = 0; k < 8; k++) { int mi = mob_at(h->pos + N8[k]); if (mi >= 0 && G.mobs[mi].type == MT_SHEEP) sheep = true; }   // magic sheep in a corridor: they go away in time
    if (sheep && rand() % 2) { walk(game_rest(false)); return true; } }
  if (h->b.roots) { walk(game_rest(false)); return true; }   // rooted with no one to fight: wait it out (a tap would pass no time)
  if (emergency()) return true;
  { static int s_fire_wait; bool hot = false;           // bot3: flames next to you and nobody around: let them burn out first
    if (g_blob[BLOB_FIRE] && !h->b.burning && enemy_count() == 0) for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && g_blob[BLOB_FIRE][n]) hot = true; }
    if (hot && s_fire_wait++ < 12) { DID("wait for the flames to die down"); walk(game_rest(false)); return true; }
    if (!hot) s_fire_wait = 0; }
  // rest when hurt and nothing around
  bool backup = (find_kind(IK_POT_HEALING) >= 0 && item_known(IK_POT_HEALING)) || (G.dew >= 3 && find_kind(IK_DEW_VIAL) >= 0) || (find_kind(IK_SCR_TELEPORT) >= 0 && item_known(IK_SCR_TELEPORT));
  bool lean = !getenv("OLDREST") && !({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; });   // 0.47n: no food in the bag: every turn counts, rest less
  int rhi = backup ? PRM[P_REST_HI_B] : lean ? 65 : PRM[P_REST_HI], rlo = backup ? PRM[P_REST_LO_B] : lean ? 40 : PRM[P_REST_LO];
  if (!getenv("OLDFOOD") && !backup) {                  // 0.52 (audit): a food budget: each HP rested back costs ~10 turns of food (a ration ~26 HP)
    int bud = 360 - h->hunger; for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (is_food(k)) bud += (k == IK_RATION ? 260 : k == IK_PASTY ? 360 : 100) * h->bag[i].qty; }
    if (!s_ev_food && !boss_depth(G.depth)) bud += 260;   // (this floor's ration, not found yet)
    int need = 300 + (boss_depth(G.depth + 1) ? 300 : 0), sur = bud - need;
    if (sur < 0) { if (rhi > 55) rhi = 55; if (rlo > 35) rlo = 35; if (getenv("RESTFOOD2")) { rhi = 35; rlo = 25; } } else if (sur < 200) { if (rhi > 65) rhi = 65; if (rlo > 40) rlo = 40; } }   /* (0.93 RESTFOOD2 test (audit): food-short, regen is food-limited: resting only moves it earlier and walking regenerates too) */
  if (getenv("RESTFULL")) { rhi = 100; rlo = 80; }
  if (getenv("THRIFT")) { int pc = thrift_need() * 100 / (h->HT ? h->HT : 1); rhi = pc + 1; rlo = pc; }   // 0.58 (owner): rest only to what the next fight needs; the rest comes back while walking   // 0.57 counterfactual: rest to full whatever the food
  s_resting = s_resting && h->HP * 100 < h->HT * rhi;   // bot3 (owner): heal while walking; with a backup, rest only when low
  if (s_ambush > s_bot_steps && amb_retreat()) return true;
  int near_foes = getenv("OLDRESTGATE") ? enemy_count() : ({ int n = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP <= 0 || ally(i) || !BIT_GET(G.fov, o->pos) || o->state == ST_PASSIVE) continue; int dd = distance(o->pos, h->pos); if (dd <= 3 || (o->state == ST_HUNTING && dd <= 8)) n++; } n; });   // 0.50 (review g6): what Mind Vision shows far off, asleep or wandering, doesn't stop a rest
  if (!getenv("OLDSTAIRREST") && (h->HP * 100 < h->HT * rlo || s_resting) && !game_starving() && near_foes == 0 && G.depth > 1 && G.entrance >= 0 && h->pos != G.entrance && !boss_depth(G.depth)) {
    bfs(h->pos, false);                                  // 0.50 (review: crab deaths): rest on the stairs up: a crab or a pack turning up while you're low, you step up and away
    if (b_dist[G.entrance] < 20 && mob_at(G.entrance) < 0) { s_resting = true; DID("to the stairs up, to rest there"); walk(tap_safe(G.entrance)); return true; } }
  if (s_pztest && !getenv("OLDSTAIRUP") && h->pos == G.entrance && G.depth > 1 && h->HP * 2 < h->HT && !boss_depth(G.depth - 1) && !game_starving() && ({ bool w = false; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && BIT_GET(G.fov, o->pos) && o->state != ST_PASSIVE && o->state != ST_SLEEPING && !(MOB_DEFS[o->type].flags & MD_NPC)) w = true; } w; })) {   // 0.93 (zone run: resting on the stairs at 8 HP, a wandering warlock in sight shot it dead): one awake in sight: up now
    int32_t t0 = h->time; ActResult rr = game_tap_cell(h->pos); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("low on the stairs up, one awake in sight: up and away"); walk(rr); return true; } }
  if ((h->HP * 100 < h->HT * rlo || s_resting) && !game_starving() && near_foes == 0 && s_ambush <= s_bot_steps && !(s_pztest && h->ooze && !IS_WATER(G.map[h->pos]))) {   // bot2: rest when below half, up to 4/5, a turn at a time   // (0.92: never oozing on dry ground: it eats a point a turn)
    if (!getenv("OLDHERO") && room_at(h->pos) >= 0 && G.rooms[room_at(h->pos)].type != RT_GARDEN) {   // 0.52 (audit): a garden: unseen there, and hunger runs at 2/3: rest in it
      bfs(h->pos, false); int gc = -1, gd = 16;
      for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_GARDEN && room_seen(r)) { Room *R = &G.rooms[r]; for (int y = R->top + 1; y < R->bottom; y++) for (int x = R->left + 1; x < R->right; x++) { int c = x + y * MAP_W; if (b_dist[c] < gd && !char_at(c) && PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & F_AVOID)) { gd = b_dist[c]; gc = c; } } }
      if (gc >= 0) { int32_t t0 = h->time; ActResult r = tap_safe(gc); if (r != ACT_NONE || h->time != t0) { DID("to the garden, to rest there"); s_resting = true; walk(r); return true; } } }
    if (!getenv("OLDHERO")) for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && G.map[n] == T_WALL && !BIT_GET(s_searched, n) && wall_worth(n) > 0) {   // 0.52 (audit): a search heals and costs as much as resting, and may find a door
        for (int q = 0; q < 8; q++) { int c = h->pos + N8[q]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); } BIT_SET(s_searched, h->pos); s_floor_searches++; st_search++;
        DID("rest to heal (searching the walls while at it)"); s_resting = true; walk(bot_search()); return true; } }
    DID("rest to heal"); s_resting = true; int32_t t = h->time; walk(game_rest(false));
    if (h->time > t) return true;
  }
  int in_puzzle = -1; for (int i = 0; i < G.n_rooms; i++) if (G.rooms[i].type == RT_POOL || G.rooms[i].type == RT_TRAPS) { Room *R = &G.rooms[i]; if (cx(h->pos) > R->left && cx(h->pos) < R->right && cy(h->pos) > R->top && cy(h->pos) < R->bottom) in_puzzle = i; }
  if ((PREACH(h->pos) || in_puzzle >= 0) && !getenv("NOPOOLEXIT")) {   // bot3: in a puzzle room (the prize taken, or the potion wearing off): out by the shortest way, water or traps or not
    int r = in_puzzle; if (r < 0) for (int i = 0; i < G.n_rooms; i++) if (G.rooms[i].type == RT_POOL) { Room *R = &G.rooms[i]; if (cx(h->pos) >= R->left && cx(h->pos) <= R->right && cy(h->pos) >= R->top && cy(h->pos) <= R->bottom) r = i; }
    bool has_prize = false; for (int i = 0; i < G.n_heaps; i++) if (r >= 0 && room_at(G.heaps[i].pos) == r && G.heaps[i].type == HEAP_PLAIN) has_prize = true;
    bool buff = G.rooms[r >= 0 ? r : 0].type == RT_POOL ? h->b.invis > 0 : h->b.levitation > 0;
    if (r >= 0 && (!has_prize || !buff)) { bfs(h->pos, false); int door = room_door(r);
      if (door < 0) for (int y = G.rooms[r].top; y <= G.rooms[r].bottom; y++) for (int x = G.rooms[r].left; x <= G.rooms[r].right; x++) { int c = x + y * MAP_W; if ((G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR) && door < 0) door = c; }
      if (door >= 0) { int best = -1, bd = 99; for (int k = 0; k < 8; k++) { int n = door + N8[k]; if (nb_ok(door, n) && room_at(n) != r && PASSABLE(G.map[n]) && !PREACH(n) && b_dist[n] < bd) { bd = b_dist[n]; best = n; } }
        if (best < 0) best = door;
        if (best != h->pos) { int st = (h->b.levitation || h->b.invis || b_dist[best] < 0x7FFF) ? best : trap_step(best);   // (walled in by traps once the potion is gone: cross the fewest)
          if (st >= 0) { s_did[0] = 0; DID(G.rooms[r].type == RT_POOL ? "out of the pool room, quick" : "out of the trap room, quick"); walk(tap_safe(st)); return true; } } } } }
  if (s_pztest && s_stash >= 0 && stash_ready() && enemy_count() == 0) {   // 0.92 (seed 84: a scroll left at the stash, twice): the stash is picked up as soon as it's safe to, before anything else
    bfs(h->pos, false); int sc = -1; for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos < MAP_LEN && hp->type == HEAP_PLAIN && (hp->pos == s_stash || in_stash(hp->pos)) && item_cat(hp->item.kind) == CAT_SCROLL && b_dist[hp->pos] < 0x7FFF && b_dist[hp->pos] <= 12 && (sc < 0 || b_dist[hp->pos] < b_dist[sc])) sc = hp->pos; }
    if (sc >= 0 && sc != h->pos) { int32_t t0 = h->time; ActResult rr = tap_safe(sc); if (rr != ACT_NONE || h->time != t0) { s_did[0] = 0; DID("the scrolls at the stash, first"); walk(rr); return true; } } }
  if (!PREACH(h->pos) && in_puzzle < 0 && use_items_when_safe()) return true;
  // the target list
  int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX];
  int n = game_targets(cell, kind);
  int choice = -1;
  if (s_am_depth > 0 && s_am_depth == G.depth) {       // the Amulet was stolen on this floor: hunt the thief, then pick it up
    for (int k = 0; k < G.n_heaps && choice < 0; k++) if (G.heaps[k].item.kind == IK_AMULET) {
      int p = G.heaps[k].pos;
      if (p < MAP_LEN) { choice = p; why = "amulet"; }
      else for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && CARRIED_POS(G.mobs[i].uid) == p) { choice = G.mobs[i].pos; why = "thief"; st_thief_hunt++; }
    }
    if (choice < 0) s_am_depth = -1;                   // gone with its carrier (an Ankh's floor reset): no way to win now
  }
  bool going = s_goal < G.depth && BIT_GET(G.visited, G.entrance);   // on the way up with the way known: no detours but items
  if (choice < 0 && s_pztest && getenv("PACE") && G.depth >= atoi(getenv("PACE")) && !boss_depth(G.depth) && !boss_depth(G.depth + 1) && s_ev_food && G.exit >= 0 && BIT_GET(G.visited, G.exit) && threat_count() == 0 && !quest_on_floor() && !shop_hunt_ok()
      && food_budget() < (getenv("PACEB") ? atoi(getenv("PACEB")) : 300) && ({ bfs(h->pos, false); b_dist[G.exit] < 0x7FFF; })) {   // 0.93 test (deep runs arrive starving: ~346 turns a floor against ~260 of food): the floor's food found, nothing seen left to take: down, the rest unexplored
    bool want = false; for (int i = 0; i < G.n_heaps && !want; i++) if (heap_wanted(i) && G.heaps[i].item.kind != IK_DEW && b_dist[G.heaps[i].pos] < 0x7FFF) want = true;
    if (!want) { choice = G.exit; why = "pace: food short, this floor's food found: down"; st_pace++; } }
  bool famished = game_hungry() && !({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; });
  if (choice < 0 && famished && PRM[P_HUNGRYDOWN] && !food_ev_ok() && !shop_hunt_ok() && BIT_GET(G.visited, G.exit) && s_goal > G.depth && ({ bfs(h->pos, false); b_dist[G.exit] < 0x7FFF; }) &&   /* (0.52: a way there: seen across a chasm or a trap field isn't one) */ (getenv("OLDHERO") || (s_ev_food && (getenv("OLDHUNGRY") || game_starving())) || !floor_left() || boss_depth(G.depth) || G.depth == 21 || (game_starving() && h->HP * 100 < h->HT * s_starve_leave && !(s_pztest && boss_depth(G.depth + 1) && floor_left() && !s_ev_food))) &&   /* (0.92 seed 84: down to the Goo at 12/40 starving, the floor's ration unfound: before a boss, find it) */   /* (owner: look for it even starving; under 30% health, just go) */   /* 0.52 (audit): every floor but the bosses' has a ration somewhere: find it first */ (G.map[G.exit] != T_LOCKED_EXIT || (G.keys[G.depth] & 64))) {   // bot3: no food, hungry: on down
    bool food_here = false; for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && is_food(G.heaps[i].item.kind)) food_here = true;
    if (!game_starving()) for (int i = 0; i < G.n_heaps; i++) { int r = (unsigned)G.heaps[i].pos < MAP_LEN ? room_at(G.heaps[i].pos) : -1; if (r >= 0 && !room_skipped(r) && G.rooms[r].type == RT_TRAPS && is_food(G.heaps[i].item.kind) && BIT_GET(G.visited, G.heaps[i].pos)) food_here = true; }   // 0.91: food in the trap room: solve it, don't leave it lying (seed 84 floor 1)
    bool grab = false; if (!getenv("OLDHUNGRY") && !(game_starving() && h->HP * 100 < h->HT * s_starve_leave)) for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i) && G.heaps[i].item.kind != IK_GOLD && G.heaps[i].item.kind != IK_DEW && b_dist[G.heaps[i].pos] < 0x7FFF) grab = true;
    if (s_pztest && !grab) for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && G.heaps[i].item.kind == IK_POT_HEALING && b_dist[G.heaps[i].pos] <= 20) grab = true;   // (0.92: a Healing is never left for being starved and hurt)   // 0.54 (SoU left lying on 1 floor in 3): what's in sight comes along first
    if (!food_here && !grab) { if (!getenv("OLDDOWNREST") && !rest_cap() && (getenv("THRIFT") ? h->HP < thrift_need() : h->HP * 10 < h->HT * 7) && !game_starving() && threat_count() == 0 && !gas_at(h->pos)) { DID("rest before going down"); walk(game_rest(false)); return true; } choice = G.exit; why = "hungry: down"; if (getenv("HUNGDBG") && !s_rollout) printf("HUNGDBG d%d step %d evfood %d starving %d floor_left %d hp %d/%d\n", G.depth, s_bot_steps, s_ev_food, game_starving(), floor_left(), h->HP, h->HT); } }
  q_track();
  if (s_ev_d != G.depth) { s_ev_d = G.depth; s_ev_sou0 = (int)st_sou_read + (find_kind(IK_SCR_UPGRADE) >= 0 ? game_slot(find_kind(IK_SCR_UPGRADE))->qty : 0); s_ev_str0 = h->STR + (find_kind(IK_POT_STRENGTH) >= 0 ? game_slot(find_kind(IK_POT_STRENGTH))->qty : 0); }
  if ((choice < 0 || (getenv("EVLEAVE2") && why && (!strcmp(why, "explore") || !strcmp(why, "frontier")))) && !going && (getenv("EVLEAVE") || getenv("EV2")) && (!getenv("EVDEEP") || G.depth >= atoi(getenv("EVDEEP"))) && s_ev_food && G.depth % 5 && !shop_hunt_ok() && !quest_on_floor() && BIT_GET(G.visited, G.exit) && s_goal > G.depth && (G.map[G.exit] != T_LOCKED_EXIT || (G.keys[G.depth] & 64)) && ({ bfs(h->pos, false); b_dist[G.exit] < 0x7FFF; })) {
    // 0.58 (owner: "use the game's odds: have I probably looted most of the valuables?"): what's likely still out there against
    // what finding it costs in food. PD per floor: ~3.7 random items (3, +1 at 40% repeatedly), 3 Upgrade scrolls in each
    // region's 4 floors, ~2 Strength potions per region; spread over the rooms, so what's left scales with what's unseen
    int seen = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) seen++;
    double est = seen + 40 > 300 ? seen + 40 : 300, un = 1 - seen / est; if (un < 0) un = 0;
    bool sou_here = (int)st_sou_read + (find_kind(IK_SCR_UPGRADE) >= 0 && (!fair_on() || item_known(IK_SCR_UPGRADE)) ? game_slot(find_kind(IK_SCR_UPGRADE))->qty : 0) > s_ev_sou0;
    bool pos_here = h->STR + (find_kind(IK_POT_STRENGTH) >= 0 && (!fair_on() || item_known(IK_POT_STRENGTH)) ? game_slot(find_kind(IK_POT_STRENGTH))->qty : 0) > s_ev_str0;
    double v = un * ((sou_here ? 0 : 0.6) * 300 + (pos_here ? 0 : 0.4) * 300 + 2.7 * 40);
    if (getenv("EV2")) { un = floor_unseen(); double ps = q_unseen(s_q_sou, Q_SOU, G.depth, s_q_fs, un), pp = q_unseen(s_q_pos, Q_POS, G.depth, s_q_fp, un);
      v = ps * (getenv("EVSOU") ? atoi(getenv("EVSOU")) : 300) + pp * (getenv("EVPOS") ? atoi(getenv("EVPOS")) : 300) + un * 2.7 * 40;
      if (getenv("EVDBG2") && !s_rollout && !s_is_child) { static int ls = -1; if (ls != s_bot_steps / 25) { ls = s_bot_steps / 25; printf("EV2 d%d step %d unseen %.2f sou %.2f (found %.1f, E[n] %.2f) str %.2f (found %.1f) value %.0f food %d\n", G.depth, s_bot_steps, un, ps, s_q_fs, ({ double e = 0; for (int n = 0; n < 16; n++) e += n * s_q_sou[n]; e; }), pp, s_q_fp, v, food_budget()); } } }
    double w = getenv("EVFOODW") ? (900 - food_budget()) / 600.0 : 1.0; if (w < 0.2) w = 0.2; if (w > 1.5) w = 1.5;   /* (owner: "exploring is never free": a turn now is a starving turn later) */
    double cost = un * 220 * w * (getenv("EVLEAVE") ? atoi(getenv("EVLEAVE")) : 100) / 100.0;
    if (v < cost) { bool grab = false; for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i) && G.heaps[i].item.kind != IK_GOLD && G.heaps[i].item.kind != IK_DEW && b_dist[G.heaps[i].pos] < 0x7FFF) grab = true;
      if (!grab) { if ((getenv("THRIFT") ? h->HP < thrift_need() : h->HP * 10 < h->HT * 7) && !game_starving() && threat_count() == 0 && !gas_at(h->pos)) { DID("rest before going down"); walk(game_rest(false)); return true; } choice = G.exit; why = "the rest isn't worth the food: down"; } } }
  if (choice < 0 && !going && getenv("FOODLEAVE") && s_ev_food && food_budget() < atoi(getenv("FOODLEAVE")) && BIT_GET(G.visited, G.exit) && s_goal > G.depth && (G.map[G.exit] != T_LOCKED_EXIT || (G.keys[G.depth] & 64)) && ({ bfs(h->pos, false); b_dist[G.exit] < 0x7FFF; })) {   // 0.58: food running short, this floor's food found: leave the far corners
    bool grab = false; for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i) && G.heaps[i].item.kind != IK_GOLD && G.heaps[i].item.kind != IK_DEW && b_dist[G.heaps[i].pos] < 0x7FFF) grab = true;
    if (!grab) { if ((getenv("THRIFT") ? h->HP < thrift_need() : h->HP * 10 < h->HT * 7) && !game_starving() && threat_count() == 0 && !gas_at(h->pos)) { DID("rest before going down"); walk(game_rest(false)); return true; } choice = G.exit; why = "food low: down"; } }
  if (choice < 0 && !going && s_rush && G.depth >= 22 && G.depth <= 24 && BIT_GET(G.visited, G.exit) && (G.map[G.exit] != T_LOCKED_EXIT || (G.keys[G.depth] & 64))) {   // 0.54 (owner): rush the bottom floors
    const char *pw = NULL; int c = plan_next(true, &pw); if (c >= 0 && pw && !strcmp(pw, "item")) { choice = c; why = "rush: an item on the way"; }
    else { bfs(h->pos, false); if (b_dist[G.exit] < 0x7FFF) { choice = G.exit; why = "rush: straight to the stairs"; st_rush++; } } }
  if (choice < 0 && !going && (h->b.levitation > 2 || h->b.invis > 2) && s_pztest) { const char *pw = NULL; int pt = puzzle_task(&pw); if (pt == -2) { DID("%s", pw); return true; } if (pt >= 0 && pw && (!strncmp(pw, "traps: levitating", 17) || !strncmp(pw, "pool: invisible", 15))) { choice = pt; why = pw; } }   // 0.91: floating: the trap room's prize before anything else (levitation doesn't last)
  if (choice < 0 && !going) { bfs(h->pos, false); int tp = trap_prize(); if (tp == -2) return true; if (tp >= 0 && tp != h->pos) { choice = tp; why = "to throw onto / step onto a trap with a prize"; } }
  if (choice < 0 && !going && ((dm_plan() && !getenv("DMP_NOREST") && G.depth == 15 && !(G.boss & 8)) || (s_pztest && !getenv("NOBOSSREST") && ((G.depth == 10 && !(G.boss & 2)) || (G.depth == 20 && !(G.boss & 16)) || (G.depth == 5 && !(G.boss & 1) && !getenv("NOREST5"))))) && h->HP * 20 < h->HT * 19 && !game_starving() && threat_count() == 0 && ({ static int rk = -1, rn; int key = cur_game * 64 + G.depth; if (rk != key) { rk = key; rn = 0; } rn++ < 400; })) { DID("floor %d: rest to full before the boss (the floor is empty)", G.depth); walk(game_rest(false)); return true; }   // 0.92 (audit): no respawns on the boss floor
  if (choice < 0 && !going && s_pztest && !getenv("NOHEALFETCH") && h->HP * 10 < h->HT * 4 && threat_count() == 0 && item_known(IK_POT_HEALING)) {   // 0.92 (seed 84 d4: went down to the Goo at 4 HP, starving, with a Potion of Healing lying two cells away): a known Healing within reach comes first
    bfs(h->pos, false); int best = -1, bd = 20;
    for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_POT_HEALING && heap_wanted(i) && b_dist[G.heaps[i].pos] < bd) { bd = b_dist[G.heaps[i].pos]; best = G.heaps[i].pos; }
    if (best >= 0) { choice = best; why = "a Healing potion lies there: fetch it first"; } }
  if (choice < 0 && !going && s_pztest && game_hungry() && !s_ev_food && storage_barr() >= 0 && !({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; }) && barricade_task()) return true;   // 0.92 (seed 84 d4: starved with the flame potion in the bag and the storage room unopened): hungry, this floor's ration unseen: the storage room now
  if (choice < 0 && !going && s_pztest && trap_puzzle_pending() && trap_door_testable() && unknown_potion() >= 0 && !item_known(IK_POT_LEVITATION) && h->HP * 10 >= h->HT * 7 && enemy_count() == 0 && !game_starving() &&
      !({ bool pf = false; for (int i = 0; i < G.n_heaps; i++) if (item_cat(G.heaps[i].item.kind) == CAT_POTION && heap_wanted(i)) pf = true; pf; })) {   // 0.92 (owner: "it's still skipping over things and doubling back"): passing the trap room's door with the floor's potions in hand: test them now, not from the far side of the floor later
    const char *pw = NULL; s_pz_only_traps = 1; int pt = puzzle_task(&pw); s_pz_only_traps = 0;
    if (pt == -2 && pw && !strncmp(pw, "puzzle", 6)) { DID("%s (passing the door)", pw); return true; }
    if (pt >= 0 && pw && !strncmp(pw, "puzzle: beside the door", 23)) { bfs(h->pos, false); if (b_dist[pt] <= 5) { choice = pt; why = "puzzle: passing its door: the potions now"; }
      else if (IS_WATER(G.map[h->pos]) && !PWATER(h->pos) && pending_trap_door_dist() <= 6 && (s_stash < 0 || !scrolls_loose() || distance(h->pos, s_stash) >= 3)) { int sl = unknown_potion(); if (sl >= 0) { st_uses[ACN_DRINK]++; st_kinds[h->bag[sl].kind]++; DID("puzzle: in the water by its door already: the next potion here"); do_item(sl, ACN_DRINK); return true; } } } }
  if (choice < 0 && !getenv("NOPLAN")) { const char *pw = NULL; choice = plan_next(going, &pw); if (choice >= 0) why = pw; }
  if (getenv("SHOPDBG") && !s_rollout) { static int ls = -1; if (ls != s_bot_steps / 20) { ls = s_bot_steps / 20; int ns = 0, nw = 0; for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE) { ns++; nw += want_buy(i); } if (ns) { printf("SHOPDBG d%d gold %d sale %d want %d choice %d enemies %d\n", G.depth, G.hero.gold, ns, nw, choice, enemy_count()); bfs(h->pos, false); for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE && want_buy(i)) { int c = G.heaps[i].pos; printf("   ware %d at %d vis %d dist %d black %d\n", G.heaps[i].item.kind, c, c < MAP_LEN && BIT_GET(G.visited, c), b_dist[c], s_black[c] - s_bot_steps); } } } }
  if (s_pztest && !getenv("NOSHOPDOOR") && (G.depth == 6 || G.depth == 11 || G.depth == 16) && h->gold >= 150 * (G.depth / 5 + 1) && enemy_count() == 0 && G.entrance >= 0 && room_at(G.entrance) >= 0 && !shop_seen()) {   // 0.93 (owner; n8g5 g2613: the shop's door two steps from the stairs, explored last, gone down starving): the shop hangs off the entrance room: its unexplored doors first
    Room *er = &G.rooms[room_at(G.entrance)]; bfs(h->pos, false); int bd = -1;
    for (int y = er->top; y <= er->bottom; y++) for (int x = er->left; x <= er->right; x++) { if (x != er->left && x != er->right && y != er->top && y != er->bottom) continue;
      int c = x + y * MAP_W, t = G.map[c]; if (!(t == T_DOOR || t == T_OPEN_DOOR || PASSABLE(t)) || b_dist[c] >= 0x7FFF || s_black[c] > s_bot_steps) continue;
      int ox = x == er->left ? x - 1 : x == er->right ? x + 1 : x, oy = y == er->top ? y - 1 : y == er->bottom ? y + 1 : y; if (ox < 0 || oy < 0 || ox >= MAP_W || oy >= MAP_H) continue;
      if (BIT_GET(G.visited, ox + oy * MAP_W)) continue; if (bd < 0 || b_dist[c] < b_dist[bd]) bd = c; }
    if (bd >= 0) { choice = bd; why = "shop floor: the entrance room's unexplored way out first (the shop's off it)"; }
    else if (!getenv("NOSHOPSRCH") && h->HP * 4 >= h->HT && 1) {   // every way out seen and no shop: its door's a secret one in these walls: search them now (they're near)
      static int sd = -1, sg = -1, sn; if (sd != G.depth || sg != cur_game) { sd = G.depth; sg = cur_game; sn = 0; }
      s_shophunt = true; int sp = sn < 16 ? search_spot2() : -1; s_shophunt = false;
      if (sp >= 0 && sp == h->pos) { sn++; for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); } BIT_SET(s_searched, h->pos); s_floor_searches++; st_search++; DID("search (the shop's hidden door)"); search_dbg(); walk(bot_search()); return true; }
      if (sp >= 0) { sn++; choice = sp; why = "shop floor: search the entrance room's walls (the shop's door is hidden)"; } } }
  if (!getenv("OLDSHOP") && enemy_count() == 0) {   // 0.50 (review g6): the shop's wares never made the target list (the suggester leaves them out): go and buy what's wanted
    bfs(h->pos, false); int bc = -1, bp = 99;
    for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; int c = hp->pos; if (hp->type != HEAP_FOR_SALE || (unsigned)c >= MAP_LEN || !BIT_GET(G.visited, c) || b_dist[c] >= 0x7FFF || (s_black[c] > s_bot_steps && (getenv("OLDAFFORD") || G.hero.gold <= s_sale_gold[c])) || !want_buy(i)) continue;   /* (0.93 n8g5 g3001: in at 62 gold, the wares blacklisted 3000 steps: back with 491, two rations left, down starving) */
      int k = hp->item.kind, pr = (s_pztest && getenv("SHOP2")) ? (3000 - shop_value(&hp->item)) / 40 : (s_pztest && !getenv("OLDSHOPPRI")) ? (getenv("SHOPANKH") && k == IK_ANKH ? 0 : ((k == IK_POT_HEALING && (!fair_on() || item_known(k))) || (getenv("IDB") && !item_known(k) && item_cat(k) == CAT_POTION && idb_p(k, IK_POT_HEALING) >= 0.5)) ? 0 : is_food(k) ? 1 : (k == IK_POT_STRENGTH || k == IK_SCR_UPGRADE) && (!fair_on() || item_known(k)) ? 2 : (k == IK_SEED_POUCH || k == IK_SCROLL_HOLDER || k == IK_WAND_HOLSTER) ? 3 : k == IK_ANKH ? 4 : 5) : (k == IK_SEED_POUCH || k == IK_SCROLL_HOLDER || k == IK_WAND_HOLSTER) ? 0 : is_food(k) ? 1 : k == IK_POT_HEALING ? 2 : k == IK_ANKH ? 3 : 4;   /* (0.92 seed 84 d6: 800 gold on a pouch and a ration, two Healings left on the shelf: a Healing is 60-odd HP, 600 turns of mending) */ /* (owner: the bag first: more room, more tools all game) */ if (pr < bp || (pr == bp && b_dist[c] < b_dist[bc])) { bp = pr; bc = c; } }
    if (bc >= 0) { static int lc = -1, ln, lg; if (bc == lc && G.hero.gold == lg) ln++; else { lc = bc; ln = 0; lg = G.hero.gold; }
      if (getenv("WNDDBG")) printf("  SHOP bc %d ln %d black %d steps %d\n", bc, ln, s_black[bc], s_bot_steps); if (ln > 6) { s_black[bc] = s_bot_steps + 3000; s_sale_gold[bc] = G.hero.gold; ln = 0; } else { choice = bc; why = "shop"; } } }   /* (0.52f: can't afford it: the trade window does nothing: 300 steps stuck, games 4/41) */
  for (int i = 0; i < n && choice < 0; i++) {
    int c = cell[i];
    if (s_black[c] > s_bot_steps) continue;
    if (kind[i] == TK_ITEM && c == h->pos) continue;   // 0.37: the list offers what lies underfoot (the bot picks up on its own)
    if (kind[i] == TK_ITEM) {
      int hp = heap_at(c);
      if (hp < 0) continue;
      int ty = G.heaps[hp].type;
      if (ty == HEAP_FOR_SALE) { if (want_buy(hp)) choice = c; continue; }
      if (G.map[c] == T_ALCHEMY || mob_at(c) >= 0) continue;   // the pot's brew: tapping it cooks; someone standing on it
      if (s_piranhas && PREACH(c)) continue;
      if ((ty == HEAP_LOCKED_CHEST || ty == HEAP_CRYSTAL_CHEST) && !(G.keys[G.depth] & 0x30)) continue;
      if (deferred_heap(hp)) continue;                 // bot2: graves, remains, puzzle-room prizes: after exploring (puzzle_task)
      if (G.heaps[hp].item.kind == IK_DEW && !getenv("OLDDEW2") && !dew_take()) continue;   // 0.91: the dew rule here too (it walked 60 turns across floor 1 for a drop at full health)
      if (ty == HEAP_PLAIN && BIT_GET(s_dropped, c) && G.heaps[hp].item.kind != IK_GOLD) continue;
      if (ty == HEAP_PLAIN && bag_used() >= BAG_SIZE && item_cat(G.heaps[hp].item.kind) != CAT_KEY && G.heaps[hp].item.kind != IK_GOLD && G.heaps[hp].item.kind != IK_DEW && !(item_stackable(G.heaps[hp].item.kind) && find_kind(G.heaps[hp].item.kind) >= 0)) continue;
      choice = c;
    } else if (kind[i] == TK_GO) {
      int mi = mob_at(c);
      if (mi >= 0) { if (BIT_GET(G.fov, c) && npc_wanted(mi)) { st_talks[G.mobs[mi].type]++; choice = c; } continue; }
      int t = G.map[c];
      if (t == T_WELL && !BIT_GET(s_used_feature, c)) { BIT_SET(s_used_feature, c); st_wells++; choice = c; continue; }
      if (t == T_ALCHEMY) { int seeds = 0; ALL_SLOTS(s) if (item_cat(game_slot(s)->kind) == CAT_SEED && game_slot(s)->kind != IK_SEED_ROTBERRY) seeds += game_slot(s)->qty;
        if (seeds >= 3 && !BIT_GET(s_used_feature, c)) { BIT_SET(s_used_feature, c); s_cooked = 0; s_brew_i = 3; st_pot++; choice = c; } continue; }
      if (t == T_LOCKED_DOOR) { choice = c; continue; }
      if (going || (s_piranhas && PREACH(c))) continue;
      if (frontier(c)) { choice = c; st_explore_tgt++; continue; }
    }
  }
  if (choice >= 0 && !strcmp(why, "?")) why = "target";
  if (choice < 0 && !s_explab) { int q = quest_task(); if (q == -2) return true; if (q >= 0 && s_black[q] <= s_bot_steps) { choice = q; why = "quest"; } else if (q >= 0 && IS_WATER(G.map[q])) BIT_SET(s_searched, q); }   // (a fish pool cell it cannot get to: skip it)
  {  }
  if (choice < 0 && !going) {                          // explore on: the nearest known spot beside the unknown
    bfs(h->pos, false);
    int bd = 0x7FFF;
    for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < bd && b_dist[c] > 0 && frontier(c) && s_black[c] <= s_bot_steps) { bd = b_dist[c]; choice = c; }
    if (choice >= 0) { st_explore_bfs++; why = "frontier"; }
  }
  int stairs_c = s_goal < G.depth ? G.entrance : G.exit;
  if (choice < 0) bfs(h->pos, false);
  bool stairs_ok = choice < 0 && BIT_GET(G.visited, stairs_c) && b_dist[stairs_c] < 0x7FFF && !(quest_on_floor() && (s_floor_steps < PRM[P_QSTAY] || getenv("OLDQSTAIRS")) && (getenv("OLDQSTAY") || (wait_quest() && s_floor_steps <= 600 && !game_hungry())));   // bot3: the quest time spent: the stairs are fine (it used to send the bot onto traps)
  if (choice < 0 && !stairs_ok && !(getenv("X_NOBOSSTRAP") && boss_depth(G.depth) && ({ bool b = false; for (int j = 0; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && (MOB_DEFS[G.mobs[j].type].flags & MD_BOSS)) b = true; b; }))) {                      // a known trap in the way: step on it
    int tbest = 1 << 30;                                // bot3: the least dangerous trap, then the nearest (it took the first one found)
    for (int t = 0; t < MAP_LEN; t++) {
      int tt = G.map[t];
      if (!BIT_GET(G.visited, t) || !(TFLAGS(tt) & F_AVOID) || (TFLAGS(tt) & F_PIT) || tt == T_WELL || s_black[t] > s_bot_steps || mob_at(t) >= 0 || PREACH(t) || (room_at(t) >= 0 && G.rooms[room_at(t)].type == RT_TRAPS) || (!trap_ok(t) && !getenv("OLDTRAP"))) continue;   // (bot3: not a fire trap when hurt)   // (bot3: not into the trap room: that's the puzzle)   // (not one a creature stands on: the tap would talk / fight)
      int near = -1; bool beyond = false;
      for (int k = 0; k < 8; k++) { int n = t + N8[k]; if (!nb_ok(t, n)) continue;
        if (b_dist[n] < 0x7FFF && s_black[n] <= s_bot_steps && n != G.entrance && n != G.exit && G.map[n] != T_ALCHEMY && mob_at(n) < 0 && !PREACH(n)) { if (near < 0 || b_dist[n] < b_dist[near]) near = n; }
        else if (PASSABLE(G.map[n]) || !BIT_GET(G.visited, n) || ((TFLAGS(G.map[n]) & F_AVOID) && !(TFLAGS(G.map[n]) & F_PIT)) || (s_fallback_search && (G.map[n] == T_WALL || G.map[n] == T_SECRET_DOOR) && !BIT_GET(s_searched, n))) beyond = true; }   // (when searching every wall: one only a trap touches too)   // (a second trap beyond counts: two in a row)
      if (near < 0 || !beyond) continue;
      int tsc = trap_danger(tt) * 40 + b_dist[near]; if (tsc >= tbest) continue; tbest = tsc;
      choice = adjacent(h->pos, t) ? t : near; why = "trap"; if (V) printf("   trap rule: t %d,%d near %d,%d preach t %d near %d\n", cx(t), cy(t), cx(near), cy(near), PREACH(t), PREACH(near));
    }
    if (choice >= 0) st_traps++;
    if (choice >= 0 && getenv("SLEEPDBG")) { static int16_t keep[MAP_LEN]; memcpy(keep, b_dist, sizeof keep); bfs(h->pos, false); for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] < 0x7FFF && b_dist[c] > 0 && frontier(c)) printf("   SLEEPDBG frontier %d,%d d %d black %d preach %d boss %d\n", cx(c), cy(c), b_dist[c], s_black[c] > s_bot_steps, PREACH(c), boss_zone(c)); for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i)) printf("   SLEEPDBG heap %d,%d d %d\n", cx(G.heaps[i].pos), cy(G.heaps[i].pos), b_dist[G.heaps[i].pos]); memcpy(b_dist, keep, sizeof keep); }
  }
  if (choice < 0 && !going && (!floor_left() || (!getenv("OLDBOOKS") && storage_barr() >= 0 && ({ bool pn = false; for (int sl = 0; sl < BAG_SIZE; sl++) if (pot_new(sl)) pn = true; pn; }))) && barricade_task()) return true;
  if (choice < 0 && !going) { const char *pw = NULL; int pt = puzzle_task(&pw); if (pt == -2) { DID("%s", pw); return true; } if (pt >= 0) { choice = pt; why = pw; } }
  if (getenv("CURSEDBG") && !s_rollout && G.q.ghost_depth == G.depth && G.q.ghost < QS_GIVEN) { static int ls = -1; if (ls != s_bot_steps / 5) { ls = s_bot_steps / 5; int gi = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GHOST) gi = i; printf("CURSECHK step %d choice %d going %d left %d ghost %d alt %d hungry %d gi %d fov %d\n", s_bot_steps, choice, going, floor_left(), G.q.ghost, G.q.ghost_alt, game_hungry(), gi, gi >= 0 ? BIT_GET(G.fov, G.mobs[gi].pos) : -1); } }
  static int gd = -1, gp = -1, gi = -1, gsw = 0, gsw_t = -1, gpd[32], gpg = -1; if (gpg != cur_game) { gpg = cur_game; for (int i = 0; i < 32; i++) gpd[i] = -1; }
  if (gd != G.depth) { if (gd >= 0) gpd[gd & 31] = gp; gd = G.depth; gp = gpd[G.depth & 31]; gsw = 0; gsw_t = -1; }   // (where he was last seen, kept per floor)
  if (s_curse_trip && (G.q.ghost >= QS_REWARDED || !curse_food_ok() || (G.depth == G.q.ghost_depth && !floor_left() && G.q.ghost < QS_GIVEN && gp < 0 && gsw >= 5))) { s_curse_trip = 0; s_goal = 99; }   // (done, or no luck: on down again)
  if (choice < 0 && !going && s_pztest && !s_curse_trip && G.depth == G.q.ghost_depth + 1 && G.q.ghost_alt == 2 && G.q.ghost < QS_GIVEN && !floor_left() && curse_food_ok() && threat_count() == 0 && BIT_GET(G.visited, G.entrance) && curse_sim_ok()) {
    s_curse_trip = 1; s_goal = G.depth - 1; DID("back up for the ghost's curse: I'd win it now"); walk(tap_safe(G.entrance)); return true; }
  gi = -1; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GHOST) gi = i;   // (alive: the quest says so; where: only by sight)
  bool gvis = gi >= 0 && BIT_GET(G.fov, G.mobs[gi].pos);
  if (gvis) gp = G.mobs[gi].pos; else if (gp >= 0 && BIT_GET(G.fov, gp)) gp = -1;   // (where he was last seen; there and gone: lost)
  if (choice < 0 && !going && s_pztest && !floor_left() && G.q.ghost_depth == G.depth && G.q.ghost_alt == 2 && G.q.ghost < QS_GIVEN && gi >= 0 && curse_food_ok()) {   // 0.92: the ghost's curse, at the floor's end (the pool's Strength first, the mace in hand): back to him if it's winnable now
    if (h->HP < h->HT && threat_count() == 0) { if (curse_go_full()) { if (game_starving()) { for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind) && h->bag[i].kind != IK_MYSTERY_MEAT) { DID("the ghost's curse next: eat, then rest"); do_item(i, ACN_EAT); return true; } } DID("the ghost's curse next: rest first"); walk(game_rest(false)); return true; } else if (s_curse_trip) { s_curse_trip = 0; s_goal = 99; s_curse_no_key = curse_key(); } }
    else if (curse_go_full()) { bfs(h->pos, false);
      if (gp >= 0 && b_dist[gp] < 0x7FFF && gp != h->pos) { choice = gp; why = gvis ? "the ghost: take on his curse" : "the ghost: where I last saw him"; }
      else if (gsw < 5) {                               // (he drifts about: look round the rooms for him, a few at most)
        if (gsw_t >= 0 && (h->pos == gsw_t || b_dist[gsw_t] >= 0x7FFF)) { gsw++; gsw_t = -1; }
        if (gsw_t < 0 && gsw < 5) { int best = -1, bs = -1; for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; int c = (R->left + R->right) / 2 + (R->top + R->bottom) / 2 * MAP_W;
            if (!BIT_GET(G.visited, c) || b_dist[c] >= 0x7FFF || BIT_GET(G.fov, c)) continue; int sc = b_dist[c] < 40 ? b_dist[c] : 0; if (sc > bs) { bs = sc; best = c; } }
          gsw_t = best; if (best < 0) gsw = 5; }
        if (gsw_t >= 0) { choice = gsw_t; why = "the ghost: looking round for him"; } } }
    else if (s_curse_trip && threat_count() == 0) { s_curse_trip = 0; s_goal = 99; s_curse_no_key = curse_key(); } }   // (here, the real odds say no after all: on down)
  if (choice < 0 && !going && !floor_left() && s_goal > G.depth && pit_jump()) return true;
  if (choice < 0) {
    // explored: stairs, or the goal
    bool up = s_goal < G.depth;
    int stairs = up ? G.entrance : G.exit;
    bool stay = !up && quest_on_floor() && s_floor_steps < PRM[P_QSTAY] && !game_starving();   // bot2: starving: food lies deeper
    if (stay && !getenv("OLDQSTAY") && (!wait_quest() || s_floor_steps > 600 || game_hungry())) stay = false;
    if (stay && s_pztest && (G.now - s_floor_turn0) / TICK > (getenv("QSTAYT") ? atoi(getenv("QSTAYT")) : 350)) stay = false;   // 0.92 (s1 g112: 700 turns and two rations on floor 3 for a quest that never came): a floor's worth of turns, no more
    if (stay && s_pztest && !({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; })) stay = false;   // 0.92 (seed 84 d3: 223 turns wandering for the ghost on its last pasty, then down hungry to the Goo): only on food in the bag   // 0.50 (review g6): wandering about only helps a quest that needs monsters to come, and not for long (food)
    if (stay && !BIT_GET(G.visited, stairs) && !getenv("OLDSTAY")) stay = false;   // 0.49: the way down not found yet: search while waiting (what it waits for may be behind that door)
    static int s_bounce_wait;
    bool xpb = !getenv("OLDXPB") && !getenv("THRIFT") && !up && boss_depth(G.depth + 1) && h->lvl < G.depth + (getenv("XPBLVL") ? atoi(getenv("XPBLVL")) : 1) && !game_starving() && BIT_GET(G.visited, stairs) && h->HP * 10 >= h->HT * 7 &&
      ((h->hunger < 150 && !s_pztest) || ({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind) && !(s_pztest && (h->bag[i].kind == IK_MYSTERY_MEAT || h->bag[i].kind == IK_OVERPRICED))) f = true; f; }));   // (0.92 seed 84: up and back on a lump of meat: 72 food for one monster)   // 0.52 (audit): under-levelled before a boss (Goo kills most at level 4): every arrival brings a monster: bounce for XP while the food lasts
    if (getenv("XPDBG") && !s_rollout && !s_is_child && boss_depth(G.depth + 1)) { static int ls = -1; if (ls != s_bot_steps / 10) { ls = s_bot_steps / 10; printf("  XPB d%d lvl %d up %d hungry %d hunger %d stairs %d hp %d/%d food %d stay %d xpb %d n %d\n", G.depth, h->lvl, up, game_hungry(), h->hunger, BIT_GET(G.visited, stairs), h->HP, h->HT, ({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; }), stay, xpb, s_bounce_n[G.depth & 31]); } }
    if (xpb && s_bounce_n[G.depth & 31] >= (getenv("XPBN") ? atoi(getenv("XPBN")) : 4)) xpb = false;   // (review: each stair costs 36 food: 4 bounces at most)
    if (xpb && s_bounce_n[G.depth & 31] < 12) { stay = true; }
    if (stay && ((wait_quest() && (getenv("QBOUNCE") || (s_pztest && !getenv("NOQBOUNCE") && !(G.q.ghost_depth == G.depth && G.q.ghost_alt == 2)))) || xpb) && !getenv("OLDBOUNCE") && G.depth > 1 && G.entrance >= 0 && ({ bfs(h->pos, false); b_dist[G.entrance] < 0x7FFF; })) {   /* (0.53: quest bouncing off: kit1 -0.064 with it; and only with a way to the stairs: game 41 locked out) */   // 0.50 (owner, from the source): each arrival on a floor spawns a monster at once (the respawner starts afresh): bounce on the stairs for the quest's kills
      if (s_bounce_n[G.depth & 31] < 12) {
        if (h->pos != G.entrance) { choice = G.entrance; why = "quest: to the stairs, to bounce for monsters"; }
        else if (s_bounce_wait++ < 15) { DID("quest: wait on the stairs for the new monster"); walk(game_rest(false)); return true; }
        else { s_bounce_wait = 0; s_bounce_n[G.depth & 31]++; st_bounce++; if (xpb) st_xpb++; DID("quest: up and straight back (each arrival brings a monster)"); walk(game_tap_cell(h->pos)); return true; }
      } else stay = false;
    }
    if (stay && choice >= 0) {}
    else if (stay) {                                        // hunt for the quest: wander
      why = "stay"; for (int k = 0; k < 40; k++) { int c = rand() % MAP_LEN; if (BIT_GET(G.visited, c) && PASSABLE(G.map[c]) && G.map[c] != T_ALCHEMY && heap_at(c) < 0 && c != G.entrance && c != G.exit) { choice = c; break; } }
      if (choice < 0) { walk(game_rest(false)); return true; }
    } else if (BIT_GET(G.visited, stairs) && (G.map[stairs] != T_LOCKED_EXIT || (G.keys[G.depth] & 64)) && s_black[stairs] <= s_bot_steps) {
      const char *ev = !up && !getenv("NOEV") && s_ev_searches < (s_pztest ? 24 : 60) && (h->hunger < 200 || ({ bool f = false; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) f = true; f; })) && !game_starving() && threat_count() == 0 ? evidence_hidden() : NULL;
      if (!ev && food_ev_ok()) ev = evidence_hidden();   // 0.92: the ration's the one worth looking for hungry (2 turns a search: a dead end beside me is a sure find)   /* (food in the bag: the hunger is paid for) */
      if (!ev && !up && shop_hunt_ok()) { const char *e = evidence_hidden(); if (s_shophunt) ev = e; }   // 0.93: the shop sells food: hungry is no reason to pass it by (48: walking steps count too)
      if (ev) {                                            // 0.51: the evidence says a room is hidden: search for it before going on
        if (!s_ev_on) { s_ev_on = 1; st_ev_floors++; if (getenv("EVDBG")) printf("  EV game %d d%d step %d: %s (std pots %d, keys %x, hunger %d)\n", cur_game, G.depth, s_bot_steps, ev, s_std_pots, G.keys[G.depth], h->hunger); for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_IRON_KEY || G.heaps[i].item.kind == IK_POT_LEVITATION) printf("    (truth: kind %d at %d,%d heap type %d visited %d room type %d)\n", G.heaps[i].item.kind, cx(G.heaps[i].pos), cy(G.heaps[i].pos), G.heaps[i].type, G.heaps[i].pos < MAP_LEN ? BIT_GET(G.visited, G.heaps[i].pos) : -1, G.heaps[i].pos < MAP_LEN && room_at(G.heaps[i].pos) >= 0 ? G.rooms[room_at(G.heaps[i].pos)].type : -1); fflush(stdout); }
        if (s_spot >= 0 && (s_black[s_spot] > s_bot_steps || mob_at(s_spot) >= 0)) s_spot = -1;
        int sp = s_spot >= 0 ? s_spot : search_spot();
        if (sp < 0) { if (getenv("EVDBG")) printf("  EV game %d d%d: no spot left to search after %d\n", cur_game, G.depth, s_ev_searches); s_ev_searches = 99; }                    // (nowhere left worth a look)
        else { if (getenv("EVDBG") && !s_rollout && !s_is_child && s_ev_searches == 59) { printf("  EV map (v seen, ' ' unseen, S secret door, s searched wall, K key, @ hero):\n"); for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c]; bool v = BIT_GET(G.visited, c); char ch = t == T_SECRET_DOOR ? 'S' : !v ? (PASSABLE(t) ? ',' : ' ') : PASSABLE(t) ? '.' : t == T_DOOR || t == T_OPEN_DOOR ? '+' : t == T_LOCKED_DOOR ? 'L' : BIT_GET(s_searched, c) ? 's' : '#'; for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].pos == c && G.heaps[i].item.kind == IK_IRON_KEY) ch = 'K'; if (c == h->pos) ch = '@'; putchar(ch); } putchar('\n'); } fflush(stdout); }
          if (getenv("EVDBG") && !s_rollout && !s_is_child) printf("  EVS d%d step %d n%d hero %d,%d spot %d,%d%s\n", G.depth, s_bot_steps, s_ev_searches, cx(h->pos), cy(h->pos), cx(sp), cy(sp), s_shophunt ? " shop" : "");
          s_ev_searches++; st_ev_search++; if (sp != h->pos) { s_spot = sp; choice = sp; why = ev; } else { s_spot = -1; choice = sp; } }
      }
    }
    if (choice >= 0) {}
    else if (stay && 0) {}
    else if (BIT_GET(G.visited, stairs) && (G.map[stairs] != T_LOCKED_EXIT || (G.keys[G.depth] & 64)) && s_black[stairs] <= s_bot_steps) {
      bfs(h->pos, false);
      bool brcap = rest_cap();
      if (!up && !brcap && !getenv("OLDDOWNREST") && (getenv("THRIFT") && !boss_depth(G.depth + 1) ? h->HP < thrift_need() : h->HP * 10 < h->HT * (boss_depth(G.depth + 1) && !getenv("OLDBOSSPREP") ? 10 : (getenv("EARLYREST") && G.depth <= 4) ? 9 : s_pztest ? 5 : 7)) && !game_starving() && threat_count() == 0 && !gas_at(h->pos)) { DID("rest before going down%s", boss_depth(G.depth + 1) ? " (a boss below: full health)" : ""); walk(game_rest(false)); return true; }
      if (!up && !getenv("OLDBOSSPREP") && boss_depth(G.depth + 1) && game_hungry() && threat_count() == 0) { int fs = -1; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) fs = i; if (fs >= 0) { DID("eat before the boss"); do_item(fs, ACN_EAT); return true; } }
      if (getenv("STAIRDBG")) { printf("  STAIRS from %d,%d to %d,%d bdist %d trapstep %d\n", cx(h->pos), cy(h->pos), cx(stairs), cy(stairs), b_dist[stairs], trap_step(stairs)); if (b_dist[stairs] < 0x7FFF) { int c = stairs; printf("   path:"); for (int g = 0; g < 80 && c != h->pos; g++) { int nb = -1; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && b_dist[n] == b_dist[c] - 1) nb = n; } if (nb < 0) break; printf(" %d,%d:t%d%s", cx(nb), cy(nb), G.map[nb], char_at(nb) ? "M" : ""); c = nb; } printf("\n"); } }
      if (!up && s_pztest && !game_starving() && threat_count() == 0 && unknown_potion() >= 0 && h->HP * 2 < h->HT && !rest_cap() && (trap_puzzle_pending() || s_stash >= 0)) { DID("rest a little before testing the potions (a trap room's prize, or the scrolls lying out)"); walk(game_rest(false)); return true; }   // 0.92 (seed 84 d1: left with the potions untested, the ration in the trap room and a scroll at the stash)
      if (!up && s_stash >= 0 && ({ bool sl = false; for (int i = 0; i < G.n_heaps; i++) if ((unsigned)G.heaps[i].pos < MAP_LEN && G.heaps[i].type == HEAP_PLAIN && (G.heaps[i].pos == s_stash || in_stash(G.heaps[i].pos)) && item_cat(G.heaps[i].item.kind) == CAT_SCROLL) sl = true; sl; }) && b_dist[s_stash] < 0x7FFF) { choice = s_stash; why = "the scrolls at the stash, before the stairs"; s_stash_t = -1000; }   // (the stash counts as ready once I'm leaving)
      else if (b_dist[stairs] < 0x7FFF || getenv("OLDSTAIRS")) { choice = stairs; why = "stairs";
        if (getenv("FOODHUNT") && !s_ev_food && !boss_depth(G.depth) && G.depth != 21 && s_floor_searches < atoi(getenv("FOODHUNT")) && h->hunger < 330 && enemy_count() == 0) {   // 0.73 X: every floor but the bosses' holds a ration: not seen yet, it's behind a secret door
          int sp = search_spot();
          if (sp == h->pos) { for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); } BIT_SET(s_searched, h->pos); s_floor_searches++; st_search++; DID("this floor's ration is hidden: search"); walk(bot_search()); return true; }
          if (sp >= 0) { choice = sp; why = "this floor's ration: search there"; } } }
      else { int st = trap_step(stairs); if (st >= 0) { choice = st; why = "over a trap: the only way to the stairs"; }
        else if (!getenv("OLDENCL") && (st = enclosed_search()) >= 0) {
          if (st == h->pos) { for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); } s_floor_searches++; st_search++; DID("walled in: search the walls toward the stairs"); walk(bot_search()); return true; }
          choice = st; why = "walled in: to the wall toward the stairs, to search it"; }
        else { choice = stairs; why = "stairs"; } } }   // bot3: walled in by traps: cross the fewest
    else if (G.map[stairs] == T_LOCKED_EXIT && BIT_GET(G.visited, stairs) && !(G.keys[G.depth] & 64)) {   // the boss's key: find the boss
      { int kc = -1; bfs(h->pos, false); for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos < MAP_LEN && hp->item.kind == IK_SKELETON_KEY && hp->type < HEAP_CARRIED && BIT_GET(G.visited, hp->pos) && b_dist[hp->pos] < 0x7FFF && (kc < 0 || b_dist[hp->pos] < b_dist[kc])) kc = hp->pos; }
        if (kc >= 0) { if (!s_rollout && getenv("KEYDBG")) printf("KEYDBG step %d: the boss's key lies at %d,%d (wanted %d): fetch it\n", s_bot_steps, cx(kc), cy(kc), heap_wanted(heap_at(kc))); choice = kc; why = "the boss's key"; } }   // 0.92 (seed 84 d5: the Goo's key lay a cell away for 400 turns while it rested and searched): the key first, whatever else
      if (choice < 0 && h->HP * 10 < h->HT * 9 && !game_starving() && threat_count() == 0 && ({ bool b = false; for (int j = 0; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && (MOB_DEFS[G.mobs[j].type].flags & MD_BOSS)) b = true; b; })) { DID("rest before the boss"); walk(game_rest(false)); return true; }   // bot3: the boss at full health only
      for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) { if (G.mobs[i].type == MT_GOO && goo_open(i)) return true;
        if (!getenv("OLDBOSSWALK") && ({ bfs(h->pos, false); int d = 0x7FFF; for (int k = 0; k < 8; k++) { int q = G.mobs[i].pos + N8[k]; if (nb_ok(G.mobs[i].pos, q) && b_dist[q] < d) d = b_dist[q]; } d >= 0x7FFF; })) { DID("boss: no way to it from here: wait for it"); walk(game_rest(false)); return true; }   /* (0.53: walking at an unreachable Tengu locked two games) */
        choice = G.mobs[i].pos; why = "boss"; break; }
      if (choice < 0 && !(G.keys[G.depth] & 15)) { bool ld = false; for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) ld = true;   // the arena's locked door: its iron key
        if (ld) for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].item.kind == IK_IRON_KEY && G.heaps[k].pos < MAP_LEN && BIT_GET(G.visited, G.heaps[k].pos)) { choice = G.heaps[k].pos; why = "iron key"; break; } }
      if (choice < 0) for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].item.kind == IK_SKELETON_KEY && G.heaps[k].pos < MAP_LEN && BIT_GET(G.visited, G.heaps[k].pos) && s_black[G.heaps[k].pos] <= s_bot_steps) { choice = G.heaps[k].pos; why = "key"; break; }
      if (choice < 0) { int sp = search_spot(); if (sp >= 0) choice = sp; }
      if (choice < 0) { walk(game_rest(false)); return true; }
    } else {
      if (V) printf("   (stairs %d: visited %d map %d black %d steps %d goal %d)\n", stairs, BIT_GET(G.visited, stairs), G.map[stairs], s_black[stairs], s_bot_steps, s_goal);
      if (s_spot >= 0 && (s_black[s_spot] > s_bot_steps || mob_at(s_spot) >= 0)) s_spot = -1;
      if (!getenv("OLDRUSH") && s_floor_searches > 20 && !h->b.blind && enemy_count() == 0) { int ms = find_kind(IK_SCR_MAPPING); if (ms >= 0 && item_known(IK_SCR_MAPPING)) { DID("searched round, the way on still hidden: read the saved Magic Mapping"); do_item(ms, ACN_READ); return true; } }   // 0.56 (night lockouts: mapping now saved for the bottom floors)
      if (!getenv("OLDTRAPFRONT") && s_floor_searches > 40) {   // 0.53 (game 16 starved on the Goo floor): searched round and found nothing: the way on is over a known trap, to the unexplored floor past it
        bfs(h->pos, false); int bf = -1, bd = 999;
        for (int c = 0; c < MAP_LEN; c++) if (frontier(c) && b_dist[c] >= 0x7FFF && !in_room(c, RT_TRAPS)) { int d = distance(c, h->pos); if (d < bd) { bd = d; bf = c; } }
        if (bf >= 0) { int st = trap_step(bf); if (st >= 0 && (!(TFLAGS(G.map[st]) & F_AVOID) || trap_ok(st)) && enemy_count() == 0) { choice = st; why = "over a trap: the only way on"; } } }
      int sp = choice >= 0 ? -1 : s_spot >= 0 ? s_spot : search_spot();      // keep to the spot chosen until it's searched
      if (sp >= 0 && sp != h->pos) s_spot = sp;
      if (sp == h->pos) { s_spot = -1; choice = sp; }      // (searched below)
      else if (sp >= 0) { choice = sp; why = "search"; st_srch_w++; }
      if (choice < 0 && sp < 0 && !BIT_GET(s_searched, h->pos) && s_floor_searches < 400) {
        for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); }
        BIT_SET(s_searched, h->pos); s_floor_searches++; st_search++; DID("search (secret doors)"); search_dbg(); walk(bot_search()); return true;
      }
      if (choice >= 0) {}
      else if (sp < 0 && s_piranhas && !s_brave) { s_brave = true; walk(game_rest(false)); return true; }   // the only way on is through their water
      else if (sp < 0) { s_fallback_search = 1; sp = search_spot(); if (sp >= 0) { choice = sp; why = "search all"; st_srch_w++; } }
      if (choice < 0 && sp < 0) { if (++s_idle < 4 || !getenv("GIVEUP")) { memset(s_black, 0, sizeof s_black); if (s_idle >= 3) memset(s_searched, 0, sizeof s_searched);   // bot3: never give up: search again (each search can still find it)
          if (s_idle >= 3) { DID("search again (secret doors)"); search_dbg(); walk(bot_search()); return true; } walk(game_rest(false)); return true; } s_idle = 0; return false; }
    }
  }
  if (choice == h->pos) {                              // standing on it: a search spot, or the stairs
    if (choice != G.exit && choice != G.entrance) {
      for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; if (nb_ok(h->pos, c)) BIT_SET(s_searched, c); }
      BIT_SET(s_searched, h->pos); s_floor_searches++; st_search++;
      if (V) { printf("  search at %d:", h->pos); for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; printf(" %d:t%d/f%d", c, G.map[c], BIT_GET(G.fov, c)); } printf("\n"); }
      DID("search (secret doors)"); search_dbg(); walk(bot_search());
      if (V) { printf("  after:"); for (int k = 0; k < 8; k++) { int c = h->pos + N8[k]; printf(" %d:t%d", c, G.map[c]); } printf("\n"); }
      return true;
    }
  }
  if (pool_floor() && !h->b.invis && choice != h->pos && !IS_WATER(G.map[choice])) { int st = dry_step(choice); if (st >= 0) choice = st; }   // bot2: keep out of the pool
  if (s_burnt_any && choice != h->pos) {                // 0.47n: through a burned room only over embers and floor you know
    bfs(choice, false); int st = -1, sd = b_dist[h->pos];
    for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && b_dist[n] < sd && mob_at(n) < 0 && (n == choice || !BIT_GET(s_sale, n))) { sd = b_dist[n]; st = n; } }   /* (0.53: never a step onto a ware: its trade window: games 4/41) */
    if (st >= 0 && BURNT(st)) { DID("search before the burned floor"); walk(bot_search()); return true; }
    if (st >= 0 && st != choice && b_dist[h->pos] != 0x7FFF) choice = st;
  }
  s_last_choice_cell = choice;
  if (!getenv("OLDTRAPTHROW") && !h->b.levitation && why && strstr(why, "trap") && adjacent(h->pos, choice) && (TFLAGS(G.map[choice]) & F_AVOID) && !(TFLAGS(G.map[choice]) & F_PIT) && G.map[choice] != T_SUMMONING_TRAP && enemy_count() == 0) {
    int s = throwable(); bool hit;                       // 0.50 (review g6, owner's trap trick): a trap in the only way: spring it with a spare thing, not your feet
    if (s >= 0 && G.map[choice] == T_FIRE_TRAP && item_cat(G.hero.bag[s].kind) == CAT_SCROLL) s = -1;   // (not a scroll into fire)
    if (s >= 0 && ballistica(h->pos, choice, &hit) == choice) { char nm[64]; item_name(&h->bag[s], nm, sizeof nm); s_did[0] = 0; DID("throw %s onto the trap at %d,%d (spring it)", nm, cx(choice), cy(choice)); st_uses[ACN_THROW]++; st_trap_throws++; walk(game_throw(s, choice)); return true; }
  }
  if (!getenv("OLDGASWAIT") && enemy_count() == 0 && gas_at(h->pos)) {   // 0.50 (review g6): standing in gas with nothing about: out of it first, the nearest clean cell
    bfs(h->pos, false); int bc = -1; for (int c = 0; c < MAP_LEN; c++) if (b_dist[c] > 0 && b_dist[c] < 0x7FFF && !gas_at(c) && (bc < 0 || b_dist[c] < b_dist[bc])) bc = c;
    if (bc >= 0 && bc != choice) { s_did[0] = 0; DID("out of the gas"); walk(tap_safe(bc)); return true; } }
  if (!getenv("OLDGASWAIT") && enemy_count() == 0 && !gas_at(h->pos)) {   // 0.50 (review g6): never walk into gas with nothing chasing you: it clears
    int nx = -1; for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && (n == choice || (distance(n, choice) < distance(h->pos, choice) && PASSABLE(G.map[n])))) { if (nx < 0 || n == choice) nx = n; } }
    if (nx >= 0 && gas_at(nx) && (distance(h->pos, choice) <= 1 || ({ bool all = true; for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; if (nb_ok(h->pos, n) && PASSABLE(G.map[n]) && distance(n, choice) < distance(h->pos, choice) && !gas_at(n)) all = false; } all; }))) {
      s_did[0] = 0; DID("wait for the gas ahead to clear"); walk(game_rest(false)); return true; } }
  if (!getenv("OLDDOWNREST") && choice == G.exit && s_goal > G.depth && !rest_cap() && h->HP * 10 < h->HT * (s_pztest ? 5 : 7) && !game_starving() && threat_count() == 0 && !gas_at(h->pos)) {   // 0.50 (review g6): not down the stairs hurt (a new floor can start with a fight); hungry is not starving
    s_did[0] = 0; DID("rest before going down"); walk(game_rest(false)); return true; }
  if (!getenv("OLDYOGREST") && G.depth == 25 && !(G.boss & 32) && h->pos == G.entrance && threat_count() == 0) {   // 0.54: off the stairs Yog wakes: fed and at full health first
    if (game_hungry()) for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind) && h->bag[i].kind != IK_MYSTERY_MEAT) { s_did[0] = 0; DID("eat before Yog wakes"); do_item(i, ACN_EAT); return true; }
    if (h->HP * 100 < h->HT * 95 && !game_starving()) { s_did[0] = 0; DID("rest on the stairs before Yog wakes"); walk(game_rest(false)); return true; } }
  if (!getenv("OLDYOGREST") && why && !strcmp(why, "locked door") && boss_depth(G.depth) && game_hungry() && threat_count() == 0) for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) { s_did[0] = 0; DID("eat before the boss's door"); do_item(i, ACN_EAT); return true; }   // 0.54: fed, the rest heals
  if (!getenv("OLDBOSSREST") && why && !strcmp(why, "locked door") && boss_depth(G.depth) && h->HP * 10 < h->HT * 9 && !game_starving() && threat_count() == 0) {   // 0.50 (review g6): the boss is behind it: at full health first
    s_did[0] = 0; DID("rest before the boss's door"); walk(game_rest(false)); return true; }
  DID("walk to %d,%d [%s]", choice % MAP_W, choice / MAP_W, why);
  int16_t p0 = h->pos; int32_t t0 = h->time; int tc0 = G.map[choice];
  if (getenv("DBGD") && atoi(getenv("DBGD")) == choice) { bfs(choice, false); printf("   DBGD from %d: hero %d,%d d=%d;", choice, cx(h->pos), cy(h->pos), b_dist[h->pos]); for (int k = 0; k < 8; k++) { int n = h->pos + N8[k]; printf(" %d,%d:%d(t%d,v%d)", cx(n), cy(n), b_dist[n], G.map[n], BIT_GET(G.visited, n)); } printf("\n"); }
  ActResult r = do_tap(choice);
  if (V) printf("  (d%d entr %d exit %d maxd %d r=%d action %d pend %d) mob@choice %d heaps:", G.depth, G.entrance, G.exit, G.max_depth, r, G.action, s_pend_depth, mob_at(choice) >= 0 ? G.mobs[mob_at(choice)].type : -1);
  if (V) { for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].pos == choice) printf(" [type %d kind %d]", G.heaps[k].type, G.heaps[k].item.kind); printf("\n"); }
  if (V) printf("  choice %d [%s] (heap %d, black %d, step %d) from %d\n", choice, why, heap_at(choice), s_black[choice], s_bot_steps, h->pos);
  if (h->pos == p0 && choice != p0 && r != ACT_LEVEL && s_pick < 0 && !s_wnd_btn && !s_pend_depth && G.map[choice] == tc0 && (h->time == t0 || getenv("OLDBLACK"))) {   // no step taken towards it (bot3: and no time spent: a chest opened from beside it is no failure)
    bool npc = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (ally(i) || (MOB_DEFS[G.mobs[i].type].flags & MD_NPC)) && distance(G.mobs[i].pos, h->pos) <= 3) npc = true;
    if (s_bot_steps - s_nw_last > 30) s_nw_n = 0; s_nw_last = s_bot_steps;
    if (npc && !getenv("NONPCWAIT") && s_nw_n++ < 4) { s_black[choice] = s_bot_steps + 6; walk(game_rest(false)); }   // bot3: someone friendly stands in the doorway: wait a moment (a few times: a blacksmith never moves)
    else s_black[choice] = s_bot_steps + ((choice == G.exit || choice == G.entrance) && !getenv("OLDSTAIRBLACK") ? 8 : enemy_count() ? 20 : 200);
    { static int fc = -1, fn; if (choice == fc) fn++; else { fc = choice; fn = 1; }   // 0.53 (r5 lockout): the same target failing again and again: over a trap if that's the way, else any step, to see anew
      if (fn >= 3 && !getenv("OLDFAILSTEP") && !npc) { int st = trap_step(choice); if (st < 0) { int o = s_bot_steps % 8; for (int k = 0; k < 8 && st < 0; k++) { int n = h->pos + N8[(k + o) & 7]; if (nb_ok(h->pos, n) && PASSABLE(G.map[n]) && !(TFLAGS(G.map[n]) & F_AVOID) && !char_at(n) && !BIT_GET(s_sale, n)) st = n; } }
        if (st >= 0) { fn = 0; s_did[0] = 0; DID("the way there fails: a step aside to see anew"); walk(game_tap_cell(st)); } } } }   // (an enemy in the way: try again soon; bot3: never give up on the stairs for long)
  if (s_walk_osc) s_black[choice] = s_bot_steps + 60;   // (a path that flip-flops: something moves in the way; later)
  if (h->pos == choice && frontier(choice)) s_black[choice] = s_bot_steps + 2000;   // there, and what's beside it stays unseen
  if (choice != G.exit && choice != G.entrance && h->pos == choice && heap_at(choice) < 0 && G.map[choice] != T_WELL) {   // arrived at a search spot
    if (!frontier(choice) && s_floor_steps > 50 && !BIT_GET(s_searched, choice)) {}
  }
  return true;
}


// ---- bot2: snapshots for the photo tool (tools/snap.py): the state before a decision, and what was done
static char *s_snapbuf[16]; static int s_snap_n;
static void jclean(char *s) { for (; *s; s++) if (*s == '"' || *s == '\\') *s = '\''; }
// 0.49: REC extras: what's carried (icons, state, the game's own description once per name), every heap
// on the floor (seen or not), plants, gas and fire
static void rec_item(FILE *f, const Item *it, bool *first) {
  if (!it->kind) return; char nm[96]; item_name(it, nm, sizeof nm); jclean(nm);
  int fl = ((it->flags & IF_LEVEL_KNOWN) ? 1 : 0) | ((it->flags & IF_CURSED_KNOWN) ? 2 : 0) | ((it->flags & IF_CURSED) ? 4 : 0) | (item_identified(it) ? 8 : 0) | (item_known(it->kind) ? 16 : 0);
  fprintf(f, "%s[%d,%d,\"%s\",%d]", *first ? "" : ",", item_sprite(it->kind), it->qty, nm, fl); *first = false;
}
static void rec_extra(FILE *f, int g) {
  static char seen[600][96]; static int nseen, lastg = -1; if (g != lastg) { nseen = 0; lastg = g; }
  Hero *h = &G.hero; bool fst = true;
  fprintf(f, ",\"eq\":["); rec_item(f, &h->weapon, &fst); if (fst) { fprintf(f, "null"); fst = false; }
  fprintf(f, ","); fst = true; rec_item(f, &h->armor, &fst); if (fst) fprintf(f, "null");
  for (int i = 0; i < 2; i++) { fprintf(f, ","); fst = true; rec_item(f, &h->ring[i], &fst); if (fst) fprintf(f, "null"); }
  fprintf(f, "],\"bagi\":["); fst = true; ALL_SLOTS(s) rec_item(f, game_slot(s), &fst);
  fprintf(f, "],\"info\":{"); bool fi = true;
  for (int s = -4; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) { Item *it = game_slot(s); if (!it->kind) continue;
    char nm[96]; item_name(it, nm, sizeof nm); jclean(nm); bool have = false; for (int k = 0; k < nseen && !have; k++) have = !strcmp(seen[k], nm);
    if (have || nseen >= 600) continue; strcpy(seen[nseen++], nm);
    char inf[1200]; item_info(it, inf, sizeof inf); fprintf(f, "%s\"%s\":\"", fi ? "" : ",", nm); fi = false;
    for (const char *q = inf; *q; q++) { if (*q == '"' || *q == '\\') fputc('\\', f); if (*q == '\n') fputs("\\n", f); else if ((unsigned char)*q >= 32) fputc(*q, f); } fputc('"', f); }
  fprintf(f, "},\"allheaps\":["); fst = true;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN) continue; char nm[96]; item_name(&hp->item, nm, sizeof nm); jclean(nm);
    fprintf(f, "%s[%d,%d,%d,\"%s\",%d,%d]", fst ? "" : ",", hp->pos, item_sprite(hp->item.kind), hp->type, nm, BIT_GET(G.visited, hp->pos), hp->item.qty); fst = false; }
  fprintf(f, "],\"plants\":["); for (int i = 0; i < G.n_plants; i++) fprintf(f, "%s[%d,%d]", i ? "," : "", G.plants[i].pos, G.plants[i].type);
  fprintf(f, "],\"gas\":["); fst = true;
  for (int b = 0; b < BLOB_COUNT; b++) if (g_blob[b]) for (int c = 0; c < MAP_LEN; c++) if (g_blob[b][c]) { fprintf(f, "%s[%d,%d]", fst ? "" : ",", c, b); fst = false; }
  fprintf(f, "]");
}
// PHOTODIR=dir: save the game at moments worth a screenshot (one file per kind, first found wins), for the What-If build
// (bot/tools/whatif_blob.py, scripts/whatif.sh): each region, special rooms, quest givers, boss fights, the depth-25 backpack
static const char *const PH_ROOM[] = { [RT_SHOP] = "room_shop", [RT_BLACKSMITH] = "room_blacksmith", [RT_TREASURY] = "room_treasury",
  [RT_ARMORY] = "room_armory", [RT_LIBRARY] = "room_library", [RT_LABORATORY] = "room_laboratory", [RT_VAULT] = "room_vault",
  [RT_TRAPS] = "room_traps", [RT_STORAGE] = "room_storage", [RT_MAGIC_WELL] = "room_magic_well", [RT_GARDEN] = "room_garden",
  [RT_CRYPT] = "room_crypt", [RT_STATUE] = "room_statue", [RT_POOL] = "room_pool", [RT_PIT] = "room_pit", [RT_ALTAR] = "room_altar",
  [RT_RAT_KING] = "room_rat_king" };
static int ph_dist(int a, int b) { int dx = abs(a % 32 - b % 32), dy = abs(a / 32 - b / 32); return dx > dy ? dx : dy; }
static int ph_best(int first, int last, int str, int lvl) {   // the highest tier the hero's strength allows at this level
  int best = first; for (int k = first; k <= last; k++) { Item t = item_make(k, 1); t.lvl = lvl; if (item_str(&t) <= str && item_tier(k) >= item_tier(best)) best = k; }
  return best;
}
static void ph_stage(void) {                    // PHOTOSTAGE: undo the head start for the picture: a level, HP, strength and gear typical of this depth
  Hero *h = &G.hero; int d = G.depth, z = (d - 1) / 5, p = (d - 1) % 5;
  int L = 1 + d * 9 / 10 + (d >= 2); if (L > 30) L = 30; if (h->lvl > L) h->lvl = L;
  int HT = 20 + 5 * (h->lvl - 1); h->HP = h->HT > 0 ? (int)((long)h->HP * HT / h->HT) : HT; if (h->HP < 1) h->HP = 1; h->HT = HT;
  h->STR = 10 + 2 * z + (p >= 2);
  int al = 2 * z + (p >= 2), wl = z + (p >= 3);
  Item a = item_make(ph_best(IK_CLOTH_ARMOR, IK_PLATE_ARMOR, h->STR, al), 1); a.lvl = al; a.flags = IF_LEVEL_KNOWN | IF_CURSED_KNOWN; a.dur = item_max_dur(&a); h->armor = a;
  Item w = item_make(ph_best(IK_DAGGER, IK_GLAIVE, h->STR, wl), 1); w.lvl = wl; w.flags = IF_LEVEL_KNOWN | IF_CURSED_KNOWN; w.dur = item_max_dur(&w); h->weapon = w;
  if (d < 25) { h->ring[0] = (Item){ 0 }; h->ring[1] = (Item){ 0 }; }
}
static void ph_save(const char *name, int g, int steps) {
  char fn[300]; snprintf(fn, sizeof fn, "%s/%s.bin", getenv("PHOTODIR"), name);
  if (access(fn, F_OK) == 0) return;
  static Game keep; memcpy(&keep, &G, sizeof G);
  if (getenv("PHOTOSTAGE")) ph_stage();
  game_save(); FILE *f = fopen(fn, "wb");
  if (f) { fwrite(g_keys, sizeof g_keys, 1, f); fclose(f);
    Hero *h = &G.hero; printf("PHOTO %s game %d step %d depth %d lvl %d HP %d/%d STR %d armor %d%+d weapon %d%+d\n", name, g, steps, G.depth, h->lvl, h->HP, h->HT, h->STR, h->armor.kind, h->armor.lvl, h->weapon.kind, h->weapon.lvl); }
  memcpy(&G, &keep, sizeof G);
}
static void ph_loot(void) {                     // the depth-25 backpack: every bag present and every slot filled with good, known things
  Hero *h = &G.hero;
  static const uint8_t BAGS[3] = { IK_SEED_POUCH, IK_SCROLL_HOLDER, IK_WAND_HOLSTER };
  for (int s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind; if (k != IK_SEED_POUCH && k != IK_SCROLL_HOLDER && k != IK_WAND_HOLSTER && k != IK_DEW_VIAL) h->bag[s] = (Item){ 0 }; }   // a clean slate
  for (int s = 0; s < BAG_SIZE; s++) for (int j = s + 1; j < BAG_SIZE; j++) if (!h->bag[s].kind && h->bag[j].kind) { h->bag[s] = h->bag[j]; h->bag[j] = (Item){ 0 }; }
  for (int i = 0; i < 3; i++) if (game_bag_find(BAGS[i]) < 0) bag_add(item_make(BAGS[i], 1));
  if (game_bag_find(IK_DEW_VIAL) < 0) bag_add(item_make(IK_DEW_VIAL, 1));
  G.keys[21] |= 1; G.keys[22] |= 2; G.keys[23] |= 1 | 1 << 4; G.keys[24] |= 1 << 4;   // iron and golden keys for the keyring
  for (int i = 0; i < N_SEEDS; i++) { G.pack[i] = item_make(IK_FIRST_SEED + i, 1 + i % 3); set_known(IK_FIRST_SEED + i); }
  for (int i = 0; i < N_SCROLLS; i++) { G.pack[8 + i] = item_make(IK_FIRST_SCROLL + i, i == IK_SCR_UPGRADE - IK_FIRST_SCROLL ? 2 : 1 + i % 2); set_known(IK_FIRST_SCROLL + i); }
  static const int8_t WL[N_WANDS] = { 3, 2, 5, 2, 3, 4, 6, 2, 3, 2, 5, 4 };
  for (int i = 0; i < N_WANDS; i++) { Item w = item_make(IK_FIRST_WAND + i, 1); w.lvl = WL[i]; w.flags = IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN; w.ench = wand_max(&w); w.dur = item_max_dur(&w); G.pack[20 + i] = w; set_known(IK_FIRST_WAND + i); }
  struct { uint8_t k; uint8_t q; int8_t l; } L[] = { { IK_POT_HEALING, 4, 0 }, { IK_POT_STRENGTH, 1, 0 }, { IK_POT_MIGHT, 1, 0 }, { IK_POT_EXPERIENCE, 2, 0 },
    { IK_POT_FROST, 2, 0 }, { IK_POT_INVIS, 1, 0 }, { IK_RNG_HASTE, 1, 3 }, { IK_RNG_EVASION, 1, 2 }, { IK_GLAIVE, 1, 3 }, { IK_PLATE_ARMOR, 1, 4 },
    { IK_RATION, 3, 0 }, { IK_ANKH, 1, 0 }, { IK_JAVELIN, 6, 0 }, { IK_INCENDIARY_DART, 9, 0 }, { IK_BOMB, 2, 0 }, { IK_HONEYPOT, 1, 0 }, { IK_PASTY, 1, 0 } };
  for (int i = 0; i < (int)(sizeof L / sizeof L[0]); i++) {
    int used = 0; for (int s = 0; s < BAG_SIZE; s++) used += h->bag[s].kind != IK_NONE;
    if (used >= BAG_SIZE) break;
    Item it = item_make(L[i].k, L[i].q); it.lvl = L[i].l; it.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; if (item_upgradable(it.kind)) it.dur = item_max_dur(&it);
    set_known(it.kind); bag_add(it);
  }
}
static void photo_step(int g, int steps) {
  if (!getenv("PHOTODIR") || s_rollout || s_is_child) return;
  Hero *h = &G.hero; int hp = h->pos, d = G.depth;
  if (h->HP <= 0) return;
  bool calm = true;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !(MOB_DEFS[m->type].flags & MD_NPC) && m->state == ST_HUNTING && ph_dist(m->pos, hp) <= 4) calm = false; }
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i]; if (m->HP <= 0 || !BIT_GET(G.fov, m->pos)) continue;
    int dist = ph_dist(m->pos, hp);
    const char *boss = m->type == MT_GOO ? "boss_goo" : m->type == MT_TENGU ? "boss_tengu" : m->type == MT_DM300 ? "boss_dm300" : m->type == MT_KING ? "boss_king" : m->type == MT_YOG ? "boss_yog" : NULL;
    if (boss && dist <= 2 && m->HP * 100 <= m->HT * 85 && h->HP * 10 >= h->HT * 4) ph_save(boss, g, steps);
    if ((m->type == MT_ROTTING_FIST || m->type == MT_BURNING_FIST) && dist <= 2 && h->HP * 10 >= h->HT * 4) ph_save("boss_yog_fist", g, steps);
    const char *npc = m->type == MT_GHOST ? "npc_ghost" : m->type == MT_WANDMAKER ? "npc_wandmaker" : m->type == MT_BLACKSMITH ? "npc_blacksmith" : m->type == MT_IMP ? "npc_imp" : NULL;
    if (npc && dist <= 3 && dist >= 1 && calm) ph_save(npc, g, steps);
  }
  for (int r = 0; r < G.n_rooms; r++) {
    Room *rm = &G.rooms[r]; int x = hp % 32, y = hp / 32;
    if (rm->type >= sizeof PH_ROOM / sizeof PH_ROOM[0] || !PH_ROOM[rm->type]) continue;
    if (x > rm->left && x < rm->right && y > rm->top && y < rm->bottom && calm) ph_save(PH_ROOM[rm->type], g, steps);
  }
  if (d % 5 != 0 && d % 5 >= 2 && s_floor_steps >= 60 && calm) {
    int seen = 0; for (int i = 0; i < G.n_mobs; i++) seen += G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos);
    for (int i = 0; i < G.n_heaps; i++) seen += BIT_GET(G.fov, G.heaps[i].pos);
    static const char *const REG[5] = { "region_sewers", "region_prison", "region_caves", "region_city", "region_halls" };
    if (seen >= 2 && d <= 24) ph_save(REG[(d - 1) / 5], g, steps);
  }
  if (d == 25 && s_floor_steps >= 3 && calm) {
    char fn[300]; snprintf(fn, sizeof fn, "%s/bag_d25.bin", getenv("PHOTODIR"));
    if (access(fn, F_OK) != 0) { static Game keep2; memcpy(&keep2, &G, sizeof G); ph_loot(); ph_save("bag_d25", g, steps); memcpy(&G, &keep2, sizeof G); }
  }
  if (d == 1 && getenv("PHOTORAT") && (h->hclass & 3) == 0 && h->HP == h->HT) {   // PHOTORAT=1: a warrior about to meet a rat, loot in sight
    int rats = 0, others = 0, rd = 0, loot = 0;
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || !BIT_GET(G.fov, m->pos)) continue; if (m->type == MT_RAT) { rats++; rd = ph_dist(m->pos, hp); } else others++; }
    for (int i = 0; i < G.n_heaps; i++) if (BIT_GET(G.fov, G.heaps[i].pos) && ph_dist(G.heaps[i].pos, hp) <= 3 && G.heaps[i].item.kind != IK_GOLD) loot++;
    if (rats == 1 && !others && rd >= 2 && rd <= 3 && loot) { char nm[40]; snprintf(nm, sizeof nm, "rat_g%03d", g); ph_save(nm, g, steps); }
  }
  if (d == 25 && getenv("PHOTOYOG")) {           // PHOTOYOG=1: the best Yog shot of each game (Yog in the open and near, fists in view)
    static int s_yog_game = -1, s_yog_best; if (s_yog_game != g) { s_yog_game = g; s_yog_best = 0; }
    for (int i = 0; i < G.n_mobs; i++) {
      Mob *m = &G.mobs[i]; if (m->type != MT_YOG || m->HP <= 0 || !BIT_GET(G.fov, m->pos)) continue;
      int dist = ph_dist(m->pos, hp); if (dist != 2 || h->HP * 10 < h->HT * 4) continue;
      int open = 0; for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) { int x = m->pos % 32 + dx, y = m->pos / 32 + dy; if (x >= 0 && x < 32 && y >= 0 && y < 32 && (TFLAGS(G.map[y * 32 + x]) & F_PASSABLE)) open++; }
      int fists = 0; for (int j = 0; j < G.n_mobs; j++) if ((G.mobs[j].type == MT_ROTTING_FIST || G.mobs[j].type == MT_BURNING_FIST) && G.mobs[j].HP > 0 && BIT_GET(G.fov, G.mobs[j].pos) && ph_dist(G.mobs[j].pos, hp) <= 4) fists++;
      int score = open + 25 * (fists > 0) + 10 * (fists > 1) + (dist == 3 ? 5 : 0);
      if (score > s_yog_best && fists > 0 && open >= 30) {
        s_yog_best = score; char nm[40]; snprintf(nm, sizeof nm, "yog_g%03d", g);
        char fn[300]; snprintf(fn, sizeof fn, "%s/%s.bin", getenv("PHOTODIR"), nm); remove(fn);
        printf("YOG score %d (open %d fists %d dist %d) ", score, open, fists, dist); ph_save(nm, g, steps);
      }
    }
  }
  if (d == 26 && calm) for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_AMULET && BIT_GET(G.fov, G.heaps[i].pos)) ph_save("amulet", g, steps);
}

static char *snap_state(int g, int step) {
  size_t cap = 20000; char *b = malloc(cap); int n = 0; Hero *h = &G.hero; char nm[64];
#define P(...) n += snprintf(b + n, cap - n, __VA_ARGS__)
  P("{\"game\":%d,\"step\":%d,\"depth\":%d,\"turn\":%d,\"hero\":{\"pos\":%d,\"hp\":%d,\"ht\":%d,\"lvl\":%d,\"str\":%d,\"cls\":%d,\"hunger\":%d,\"left\":%d", g, step, G.depth, G.now / TICK, h->pos, h->HP, h->HT, h->lvl, h->STR, h->hclass & 3, h->hunger, h->facing_left);
  item_name(&h->weapon, nm, sizeof nm); jclean(nm); P(",\"weapon\":\"%s\"", h->weapon.kind ? nm : "");
  item_name(&h->armor, nm, sizeof nm); jclean(nm); P(",\"armor\":\"%s\"},\"map\":[", h->armor.kind ? nm : "");
  for (int c = 0; c < MAP_LEN; c++) P("%s%d", c ? "," : "", G.map[c]);
  P("],\"vis\":\""); for (int c = 0; c < MAP_LEN; c++) P("%c", BIT_GET(G.fov, c) ? '2' : BIT_GET(G.visited, c) ? '1' : '0');
  P("\",\"heaps\":["); int k = 0;
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !BIT_GET(G.visited, hp->pos)) continue; item_name(&hp->item, nm, sizeof nm); jclean(nm);
    P("%s{\"pos\":%d,\"type\":%d,\"spr\":%d,\"name\":\"%s\"}", k++ ? "," : "", hp->pos, hp->type, item_sprite(hp->item.kind), nm); }
  P("],\"mobs\":["); k = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || !game_hero_visible_mob(i)) continue;
    P("%s{\"pos\":%d,\"type\":%d,\"name\":\"%s\",\"hp\":%d,\"ht\":%d,\"state\":%d,\"ally\":%d,\"aux2\":%d}", k++ ? "," : "", m->pos, m->type, mob_name(m->type), m->HP, m->HT, m->state, ally(i), m->aux2); }
  int16_t cell[TARGETS_MAX]; uint8_t kind[TARGETS_MAX]; int nt = game_targets(cell, kind);
  P("],\"targets\":["); for (int i = 0; i < nt; i++) P("%s[%d,%d]", i ? "," : "", cell[i], kind[i]);
  P("],\"bag\":["); k = 0;
  for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind) { item_name(&h->bag[i], nm, sizeof nm); jclean(nm); P("%s\"%s\"", k++ ? "," : "", nm); }
  P("],\"log\":["); for (int i = 0; i < 4; i++) { const char *l = s_last_log[(s_nlog - 4 + i) & 7]; P("%s\"", i ? "," : ""); for (const char *q = l; *q; q++) if (*q != '"' && *q != '\\') P("%c", *q); P("\""); }
  P("]");
#undef P
  return b;
}


// 0.57 (owner: "use the look-ahead to find the strategies we need"): COUNTERFACTUALS, an analysis tool. CF=1: on each new
// floor a frozen copy of the game waits; when the hero dies, that copy replays the floor under each variant in CFV
// ("name:ENV=v,ENV2=v;name2:..."), CFR times with fresh luck each, and prints how often each got down the stairs alive.
// The real game is untouched (it dies as before); only the copies try things.
static int s_cf_fd = -1, s_cf_child, s_cf_d0, s_cf_s0, s_cf_out = -1; static pid_t s_cf_pid = -1;
static int s_cf_fd2 = -1; static pid_t s_cf_pid2 = -1;   // (CFBACK: the floor before's copy too)
static void cf_kill1(int *fd, pid_t *pid) { if (*pid > 0) { close(*fd); kill(*pid, SIGKILL); int st; waitpid(*pid, &st, 0); } *pid = -1; *fd = -1; }
static void cf_kill(void) { cf_kill1(&s_cf_fd, &s_cf_pid); cf_kill1(&s_cf_fd2, &s_cf_pid2); }
static int food_budget(void) { int bud = 360 - G.hero.hunger; for (int i = 0; i < BAG_SIZE; i++) { int k = G.hero.bag[i].kind; if (k == IK_RATION) bud += 260 * G.hero.bag[i].qty; else if (k == IK_PASTY) bud += 360 * G.hero.bag[i].qty; else if (is_food(k) || k == IK_MYSTERY_MEAT) bud += 100 * G.hero.bag[i].qty; } return bud; }
static void cf_report(int ok) { int r[6] = { ok, G.now / TICK, G.hero.lvl, G.hero.HP, G.hero.HT, food_budget() }; if (write(s_cf_out, r, sizeof r) != sizeof r) {} _exit(0); }
static int LA_K = 40;
static void cf_checkpoint(int g, int steps) {
  if (getenv("CFBACK")) { cf_kill1(&s_cf_fd2, &s_cf_pid2); s_cf_fd2 = s_cf_fd; s_cf_pid2 = s_cf_pid; s_cf_fd = -1; s_cf_pid = -1; } else cf_kill1(&s_cf_fd, &s_cf_pid);
  fflush(stdout); int fds[2]; if (pipe(fds)) return;
  pid_t pid = fork(); if (pid < 0) { close(fds[0]); close(fds[1]); return; }
  if (pid > 0) { close(fds[0]); s_cf_fd = fds[1]; s_cf_pid = pid; return; }
  close(fds[1]); if (s_cf_fd2 >= 0) close(s_cf_fd2); s_cf_fd2 = -1; s_cf_pid2 = -1;
  char c[2] = { 0, 0 }; if (read(fds[0], c, 2) != 2 || c[0] != 'g') _exit(0);
  int target = c[1];                                    // (the depth the hero died on: a replay passes when it gets below it)
  const char *spec = getenv("CFV") ? getenv("CFV") : "base:;nola:NOLA=1;la3:LAMULT=3,LAK=80;rfull:RESTFULL=1;erest:EARLYREST=1;goola:GOO_LA=1";
  int R = getenv("CFR") ? atoi(getenv("CFR")) : 6, d = G.depth;
  char out[3000]; int on = snprintf(out, sizeof out, "CF game %d d%d from d%d cls %d lvl %d hp %d:", g, target, d, g_new_class, G.hero.lvl, G.hero.HT);
  char buf[1500]; snprintf(buf, sizeof buf, "%s", spec);
  for (char *sv = NULL, *v = strtok_r(buf, ";/", &sv); v; v = strtok_r(NULL, ";/", &sv)) {   // (the PC runner allows no ";" or "=": "a/b:ENV-1,ENV2-x" too)
    char *colon = strchr(v, ':'); if (colon) *colon = 0; const char *env = colon ? colon + 1 : "";
    int ok = 0, die = 0, tt = 0;
    for (int r = 0; r < R; r++) {
      int p[2]; if (pipe(p)) break; fflush(stdout); pid_t k = fork();
      if (k == 0) { close(p[0]); s_cf_out = p[1]; s_cf_child = 1; s_cf_d0 = target; s_cf_s0 = steps; s_cf_pid = -1;
        char eb[400]; snprintf(eb, sizeof eb, "%s", env);
        for (char *s2 = NULL, *e = strtok_r(eb, ",", &s2); e; e = strtok_r(NULL, ",", &s2)) { char *q = strchr(e, '='); if (!q) q = strchr(e, '-'); if (q) { *q = 0; if (q[1]) setenv(e, q + 1, 1); else unsetenv(e); } else if (*e) setenv(e, "1", 1); }
        if (getenv("LAK")) LA_K = atoi(getenv("LAK")); if (getenv("CFDBG")) fprintf(stderr, "  child %s: GOO2=%s NOLA=%s\n", v, getenv("GOO2") ? getenv("GOO2") : "(unset)", getenv("NOLA") ? getenv("NOLA") : "(unset)");
        int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } V = 0;
        g_rng ^= (uint32_t)(r + 1) * 2654435761u; srand(r + 1);
        return; }                                       // (back into the game loop: plays the floor again)
      close(p[1]); int res[6] = { -1, 0, 0, 0, 0, 0 }; if (read(p[0], res, sizeof res) != sizeof res) res[0] = -1; close(p[0]); int st; waitpid(k, &st, 0);
      ok += res[0] == 1; die += res[0] == 0; tt += res[1]; if (getenv("CFDBG")) fprintf(stderr, "  replay %s r%d -> %d at turn %d lvl %d\n", v, r, res[0], res[1], res[2]);
    }
    on += snprintf(out + on, sizeof out - on, " %s %d/%d", v, ok, R);
    if (on > (int)sizeof out - 60) break;
  }
  printf("%s\n", out); fflush(stdout); _exit(0);
}

// 0.58 (owner: "focus on one game at a time: a MASSIVE look-ahead, the super-bot trying lots of behaviour switches in the
// same game, record good results"): FLA=1: on each new floor, the floor is played ahead under each style in FLAV, FLAR
// times with fresh luck; the style that came out best (alive below, with the most health, food and XP left) is the one
// the real game plays this floor with. The look-ahead sees the floor's layout (a learning tool, not a fair player).
static const char *s_fla_keys[] = { "THRIFT", "EATEARLY", "THROWFIRST", "ENGAGEHP", "EARLYREST", "STAIRFLEE", "OLDHUNT", "XPBLVL", "XPBN", "LAMULT", "LAK", "RESTFULL", "GOO2", NULL };
static void fla_apply(const char *env) {
  for (int i = 0; s_fla_keys[i]; i++) unsetenv(s_fla_keys[i]);
  char eb[400]; snprintf(eb, sizeof eb, "%s", env);
  for (char *s2 = NULL, *e = strtok_r(eb, ",", &s2); e; e = strtok_r(NULL, ",", &s2)) { char *q = strchr(e, '='); if (!q) q = strchr(e, '-'); if (q) { *q = 0; if (q[1]) setenv(e, q + 1, 1); } else if (*e) setenv(e, "1", 1); }
  LA_K = getenv("LAK") ? atoi(getenv("LAK")) : 40;
}
static int fla_score(const int *r) { if (r[0] == 1) return 1000 + 400 * r[3] / (r[4] ? r[4] : 1) + r[5] / 3 + 60 * r[2]; if (r[0] == 2) return 300 + r[5] / 3; return -2000 + r[1] / 10; }
static void fla_choose(int g, int steps) {
  const char *spec = getenv("FLAV") ? getenv("FLAV") : "base:/careful:ENGAGEHP-90,EARLYREST-1,STAIRFLEE-1/bold:ENGAGEHP-0/xp:XPBLVL-2,XPBN-8/nohunt:OLDHUNT-1/deep:LAMULT-3,LAK-80/goo2:GOO2-1";
  int R = getenv("FLAR") ? atoi(getenv("FLAR")) : 4, d = G.depth;
  char buf[1500]; snprintf(buf, sizeof buf, "%s", spec); char names[16][32], envs[16][200]; int nv = 0;
  for (char *sv = NULL, *v = strtok_r(buf, ";/", &sv); v && nv < 16; v = strtok_r(NULL, ";/", &sv)) { char *colon = strchr(v, ':'); if (colon) *colon = 0; snprintf(names[nv], 32, "%s", v); snprintf(envs[nv], 200, "%s", colon ? colon + 1 : ""); nv++; }
  int bestv = 0; double bests = -1e9; char out[2000]; int on = snprintf(out, sizeof out, "FLA game %d d%d lvl %d hp %d/%d food %d:", g, d, G.hero.lvl, G.hero.HP, G.hero.HT, food_budget());
  for (int v = 0; v < nv; v++) {
    double tot = 0; int ok = 0;
    for (int r = 0; r < R; r++) {
      int p[2]; if (pipe(p)) break; fflush(stdout); pid_t k = fork();
      if (k == 0) { close(p[0]); s_cf_out = p[1]; s_cf_child = 1; s_cf_d0 = G.max_depth; s_cf_s0 = steps; fla_apply(envs[v]);
        int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } V = 0;
        g_rng ^= (uint32_t)(r + 1 + d * 101) * 2654435761u; srand(r + 1 + d * 101); return; }
      close(p[1]); int res[6] = { 0, 0, 0, 0, 0, 0 }; if (read(p[0], res, sizeof res) != sizeof res) res[0] = 0; close(p[0]); int st; waitpid(k, &st, 0);
      tot += fla_score(res); ok += res[0] == 1;
    }
    double avg = R ? tot / R : 0; on += snprintf(out + on, sizeof out - on, " %s %d/%d (%.0f)", names[v], ok, R, avg);
    if (avg > bests + 1e-9) { bests = avg; bestv = v; }
  }
  fla_apply(envs[bestv]);
  printf("%s -> %s\n", out, names[bestv]); fflush(stdout);
}
// ==== bot3: LOOK-AHEAD. At dangerous moments, copy the game (fork) for each candidate action, play it forward with the
// ordinary rules for a few dozen decisions, several times with different luck, and do what turned out best.
enum { C_DEFAULT, C_TAP, C_REST, C_ITEM, C_ZAP, C_THROW };
typedef struct { uint8_t t, act; int16_t a, b; } Cand;
static const char *CNAME[] = { "default", "tap", "rest", "item", "zap", "throw" };
static int LA_R = 3;                         // decisions per playout, playouts per candidate
static long st_la_points, st_la_changed, st_la_forks;
static bool decide(void); static int wnd_for(void);
static bool la_step(void) {                             // one decision as the main loop makes it (windows first)
  if (s_pick >= 0) { int p = s_pick; s_pick = -1; int ch = pick_for((PickMode)p); walk(game_pick((PickMode)p, ch)); return true; }
  if (s_wnd_btn) { s_wnd_btn = 0; int b = wnd_for(); walk(game_wnd_choice(b)); return true; }
  return decide();
}
static int hostile_near(int r, int *adj) { int n = 0; if (adj) *adj = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE) continue; int d = distance(m->pos, G.hero.pos); if (d <= r && (m->state == ST_HUNTING || d <= 1)) { n++; if (adj && d <= 1) (*adj)++; } } return n; }
static bool danger(void) {
  Hero *h = &G.hero; int adj, n = hostile_near(5, &adj);
  if (h->b.burning && !IS_WATER(G.map[h->pos]) && h->HP < 3 * h->b.burning + 3 && !getenv("OLDFIRE")) return true;   // 0.47n: the flames may finish you: what can save you?
  if (!n) { if (h->HP >= h->HT / 2) return false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && BIT_GET(G.fov, G.mobs[i].pos) && G.mobs[i].state != ST_PASSIVE) return true; return false; }
  return h->HP < h->HT * 3 / 4 || n >= 2;
}
static int nearest_hostile(void) { int best = -1, bd = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE) continue; int d = distance(m->pos, G.hero.pos); if (d < bd) { bd = d; best = i; } } return best; }
static int cands(Cand *c) {
  Hero *h = &G.hero; int n = 0, hp = h->pos;
  c[n++] = (Cand){ C_DEFAULT, 0, 0, 0 };
  c[n++] = (Cand){ C_REST, 0, 0, 0 };
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && adjacent(m->pos, hp) && m->state != ST_PASSIVE && (!fair_on() || BIT_GET(G.fov, m->pos))) c[n++] = (Cand){ C_TAP, 0, m->pos, 0 }; }
  for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (nb_ok(hp, q) && PASSABLE(G.map[q]) && !(TFLAGS(G.map[q]) & F_AVOID) && !char_at(q)) c[n++] = (Cand){ C_TAP, 0, q, 0 }; }
  if (hp == G.exit || (hp == G.entrance && !(s_zone_start && boss_depth(G.depth - 1)))) c[n++] = (Cand){ C_TAP, 0, hp, 0 };   // (zone starts: the boss above was never fought: no escape up there)
  int foe = nearest_hostile(), fp = foe >= 0 ? G.mobs[foe].pos : -1;
  {                                                    // retreat: the nearest choke points (a door, a corridor) within 8 steps, away from the foes
    bfs(hp, false); int best[3] = { -1, -1, -1 }, bd[3] = { 99, 99, 99 };
    for (int q = 0; q < MAP_LEN; q++) {
      if (b_dist[q] < 2 || b_dist[q] > 8 || char_at(q) || PWATER(q)) continue;
      int open = 0; for (int k = 0; k < 4; k++) { int m = q + N8[k]; if (nb_ok(q, m) && PASSABLE(G.map[m])) open++; }
      if (open > 2) continue;
      if (fp >= 0 && distance(q, fp) <= distance(hp, fp)) continue;
      int d = b_dist[q];
      for (int z = 0; z < 3; z++) if (d < bd[z]) { for (int y = 2; y > z; y--) { bd[y] = bd[y - 1]; best[y] = best[y - 1]; } bd[z] = d; best[z] = q; break; }
    }
    bool fastfoe = foe >= 0 && getenv("NOFASTRET") && MOB_DEFS[G.mobs[foe].type].move_cost * 2 <= hero_move_cost() && distance(fp, hp) <= 2;   // 0.73 X: a crab or a bat outruns you: a retreat is free hits
    if (!fastfoe) for (int z = 0; z < 3; z++) if (best[z] >= 0) c[n++] = (Cand){ C_TAP, 1, best[z], 0 };
    if (G.entrance >= 0 && BIT_GET(G.visited, G.entrance) && b_dist[G.entrance] <= 10 && b_dist[G.entrance] > 0 && G.depth > 1 && G.now - s_last_up > 300 * TICK) c[n++] = (Cand){ C_TAP, 2, G.entrance, 0 };   // up the stairs: they don't follow
  }
  if (getenv("NOFASTRET") && foe >= 0 && MOB_DEFS[G.mobs[foe].type].move_cost * 2 <= hero_move_cost() && adjacent(fp, hp)) { int m = 0; for (int i = 0; i < n; i++) if (!(c[i].t == C_TAP && c[i].a != hp && mob_at(c[i].a) < 0 && distance(c[i].a, fp) > 1)) c[m++] = c[i]; n = m; }   // (nor a step away from it)
  if (!getenv("OLDDMHOLD") && (G.hero.HP * 4 >= G.hero.HT || dm_hold()) && ({ bool dm = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_DM300 && adjacent(G.mobs[i].pos, G.hero.pos)) dm = true; dm; })) { int m = 0; for (int i = 0; i < n; i++) if (c[i].t != C_TAP) c[m++] = c[i]; n = m; }   // 0.50 (review g6): toe to toe with DM-300, no stepping about: it follows over the dead traps and repairs itself
  if (!getenv("OLDPING") && s_bot_steps - s_prev_arrive < 30) { int m = 0; for (int i = 0; i < n; i++) if (!(c[i].t == C_TAP && (c[i].a == G.entrance || c[i].a == G.hero.pos))) c[m++] = c[i]; n = m; }   // 0.49: two floor changes in 30 steps: not straight back up again (a ping-pong where no time passed)
  int seen[IK_COUNT / 8 + 1]; memset(seen, 0, sizeof seen);
  for (int s = -1; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) {
    Item *it = game_slot(s); int k = it->kind, cat = item_cat(k);
    if (!k || (seen[k >> 3] >> (k & 7) & 1)) continue;
    seen[k >> 3] |= 1 << (k & 7);
    if (s < 0 && cat != CAT_WAND) continue;
    if (cat == CAT_POTION && s_pztest && item_known(k) && ((k == IK_POT_INVIS && pool_puzzle_pending()) || (k == IK_POT_LEVITATION && trap_puzzle_pending())) && h->HP * 4 >= h->HT) continue;   // 0.92 (seed 84 d3: the pool's invisibility spent getting away from a crab at 2/3 health): kept for its room unless near death
    if (cat == CAT_POTION) { if (getenv("OLDLADRINK") || !item_known(k) || !(k == IK_POT_LEVITATION || k == IK_POT_MIND || harmful_potion(k) || (k == IK_POT_PURITY && !bad_air(G.hero.pos)))) c[n++] = (Cand){ C_ITEM, ACN_DRINK, s, 0 };   /* (0.53: no Levitation or Mind Vision mid-fight: the look-ahead drank three at Tengu) */ if (fp >= 0 && ((!item_known(k) && !(MOB_DEFS[G.mobs[foe].type].flags & MD_BOSS)) || harmful_potion(k)) && (getenv("OLDLAGAS") || distance(fp, G.hero.pos) >= ((k == IK_POT_TOXIC || k == IK_POT_PARALYTIC || !item_known(k)) ? 4 : 3))) c[n++] = (Cand){ C_THROW, 0, s, fp }; }   /* 0.54: a gas thrown 2 away comes back over you (a Tengu death) */
    else if (cat == CAT_SCROLL && !h->b.blind && !(s_pztest && item_known(k) && k == IK_SCR_CHALLENGE) && !(k == IK_SCR_TERROR && item_known(k) && !getenv("OLDDMTERROR") && ({ bool dm = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_DM300) dm = true; dm; }))) c[n++] = (Cand){ C_ITEM, ACN_READ, s, 0 };   // (0.50 review g6: not Terror at DM-300: it flees over the dead traps and repairs itself to full)
    else if (cat == CAT_SEED && k != IK_SEED_ROTBERRY && !(s_firedoom && (k == IK_SEED_FIREBLOOM || (!getenv("LASEEDS") && (k == IK_SEED_ICECAP || k == IK_SEED_SORROWMOSS || k == IK_SEED_DREAMWEED || k == IK_SEED_FADELEAF)))) && plant_at(hp) < 0 && heap_at(hp) < 0 && hp != G.exit && hp != G.entrance) c[n++] = (Cand){ C_ITEM, ACN_PLANT, s, 0 };
    else if (cat == CAT_WAND && it->ench > 0 && fp >= 0 && !(s_pztest && !getenv("OLDDMTELE") && k == IK_WND_TELEPORT && ({ int mi = mob_at(fp); mi >= 0 && G.mobs[mi].type == MT_DM300; }))) c[n++] = (Cand){ C_ZAP, 0, s, fp };   // 0.93: not a teleport at DM-300: it walks back over the dead traps and mends half of what it lost on each
    else if (cat == CAT_MISSILE && fp >= 0) c[n++] = (Cand){ C_THROW, 0, s, fp };
    else if (is_food(k) && (s_eatlate < 0 ? h->hunger >= 150 : game_starving() || (s_warreat && (h->hclass & 3) == 0 && h->HP * 4 < h->HT))) c[n++] = (Cand){ C_ITEM, ACN_EAT, s, 0 };
    if (n > 60) break;
  }
  return n;
}
static bool apply_cand(Cand c) {                        // true: it did something (time passed or the floor changed)
  Hero *h = &G.hero; int32_t t = h->time; int d0 = G.depth;
  switch (c.t) {
    case C_DEFAULT: return decide();
    case C_REST: walk(game_rest(false)); break;
    case C_TAP: walk(tap_safe(c.a)); if (c.act == 2 && !s_rollout) s_last_up = G.now; break;
    case C_ITEM: do_item(c.a, (ItemAction)c.act); break;
    case C_ZAP: walk(game_zap(c.a, c.b)); break;
    case C_THROW: walk(game_throw(c.a, c.b)); break;
  }
  return h->time != t || G.depth != d0 || s_pend_depth;
}
static int boss_hp(void) { int t = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) t += G.mobs[i].HP; return t; }
static int s_la_boss0;
static int score_now(int hp0, int ht0, int exp0, int lvl0, int32_t now0) {
  Hero *h = &G.hero;
  if (h->HP <= 0 && s_la_boss0 > 0 && !getenv("LA_OLDDEATH")) return -100000 + (s_la_boss0 - boss_hp()) * 40;   // a boss fight lost: how much of it was done (stalling doesn't count)
  if (h->HP > 0 && s_la_boss0 > 0) { int b = boss_hp(); int bonus = (s_la_boss0 - b) * 15 + (b == 0 ? 1500 : 0); if (h->HP > 0) return 2000 + h->HP * 1000 / h->HT + bonus - (G.now - now0) / TICK; }
  if (h->HP <= 0) return -100000 + (G.now - now0) / TICK * 20;
  int adj, n = hostile_near(4, &adj);
  int sc = 2000 + h->HP * 1000 / h->HT - adj * 120 - n * 60 + (h->lvl - lvl0) * 300 + (h->exp - exp0) * 15 - (G.now - now0) / TICK;
  if (s_pend_depth) sc += s_pend_depth < G.depth ? -150 : 100;   // (up the stairs: food and time spent)
  return sc;
}
// bot3: honest look-ahead: in a playout, the unknown potions and scrolls in the bag are re-rolled from the game's own
// odds among the kinds not yet identified (the same roll for every option at one playout index), so the search
// weighs "it might be healing, it might be liquid flame" instead of peeking at what they really are
static void resample_unknowns(uint32_t seed) {
  if (getenv("LA_PEEK")) return;
  static const uint8_t PK[12] = { IK_POT_HEALING, IK_POT_EXPERIENCE, IK_POT_TOXIC, IK_POT_PARALYTIC, IK_POT_FLAME, IK_POT_LEVITATION, IK_POT_STRENGTH, IK_POT_MIND, IK_POT_PURITY, IK_POT_INVIS, IK_POT_MIGHT, IK_POT_FROST };
  static const uint8_t PW[12] = { 45, 4, 15, 10, 15, 10, 8, 20, 12, 10, 2, 10 };
  static const uint8_t SK[12] = { IK_SCR_IDENTIFY, IK_SCR_TELEPORT, IK_SCR_REMOVE_CURSE, IK_SCR_RECHARGING, IK_SCR_MAPPING, IK_SCR_CHALLENGE, IK_SCR_TERROR, IK_SCR_LULLABY, IK_SCR_PSIONIC, IK_SCR_MIRROR, IK_SCR_UPGRADE, IK_SCR_ENCHANT };
  static const uint8_t SW[12] = { 30, 10, 15, 10, 15, 12, 8, 8, 4, 6, 10, 1 };
  uint32_t r = seed * 2246822519u + 374761393u;
  for (int pass = 0; pass < 2; pass++) {
    const uint8_t *K = pass ? SK : PK, *W = pass ? SW : PW;
    int held[12], nh = 0; bool used[12] = { 0 };
    for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { int k = game_slot(s2)->kind; if (item_cat(k) != (pass ? CAT_SCROLL : CAT_POTION) || item_known(k)) continue;
      bool dup = false; for (int j = 0; j < nh; j++) dup |= held[j] == k; if (!dup && nh < 12) held[nh++] = k; }
    if (s_pztest) for (int i = 0; i < G.n_heaps; i++) { int k = G.heaps[i].item.kind; if (item_cat(k) != (pass ? CAT_SCROLL : CAT_POTION) || item_known(k)) continue;   // 0.92: the floor's unknown ones too (same flavours)
      bool dup = false; for (int j = 0; j < nh; j++) dup |= held[j] == k; if (!dup && nh < 12) held[nh++] = k; }
    int map[12];
    for (int j = 0; j < nh; j++) { int tot = 0; for (int i = 0; i < 12; i++) if (!used[i] && !item_known(K[i])) tot += W[i];
      map[j] = held[j]; if (tot <= 0) continue; r = r * 1664525u + 1013904223u; int x = (r >> 8) % tot;
      for (int i = 0; i < 12; i++) if (!used[i] && !item_known(K[i])) { if (x < W[i]) { used[i] = true; map[j] = K[i]; break; } x -= W[i]; } }
    for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { Item *it = game_slot(s2); for (int j = 0; j < nh; j++) if (it->kind == held[j]) { it->kind = map[j]; break; } }
    if (s_pztest) for (int i = 0; i < G.n_heaps; i++) { Item *it = &G.heaps[i].item; for (int j = 0; j < nh; j++) if (it->kind == held[j]) { it->kind = map[j]; break; } }
  }
}
// 0.93 (owner: progress with hidden information doesn't count): before a playout, the copy is cut down to what a player
// knows: only the creatures in sight, hidden traps as plain floor and hidden doors as wall, nothing lying where it hasn't
// been seen, unknown potions and scrolls drawn from the odds (IDB), unknown wands from the Generator's weights.
static void la_scrub(uint32_t seed) {
  uint32_t r = seed * 747796405u + 2891336453u;
  int k = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && (ally(i) || BIT_GET(G.fov, m->pos))) G.mobs[k++] = *m; } G.n_mobs = k;
  for (int c = 0; c < MAP_LEN; c++) { int t = G.map[c]; if (t == T_SECRET_DOOR) G.map[c] = T_WALL;
    else if (t == T_SECRET_TOXIC_TRAP || t == T_SECRET_FIRE_TRAP || t == T_SECRET_PARALYTIC_TRAP || t == T_SECRET_POISON_TRAP || t == T_SECRET_ALARM_TRAP || t == T_SECRET_LIGHTNING_TRAP || t == T_SECRET_GRIPPING_TRAP || t == T_SECRET_SUMMONING_TRAP) G.map[c] = T_EMPTY; }
  k = 0; for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if (hp->type == HEAP_CARRIED || ((unsigned)hp->pos < MAP_LEN && BIT_GET(G.visited, hp->pos))) G.heaps[k++] = *hp; } G.n_heaps = k;
  for (int pot = 0; pot < 2; pot++) {                  // potions and scrolls: a consistent draw from the odds
    int base = pot ? IK_FIRST_POTION : IK_FIRST_SCROLL; const uint8_t *K = pot ? IDB_PK : IDB_SK; int map[12]; bool used[12] = { 0 };
    for (int i = 0; i < 12; i++) if (item_known(K[i])) used[i] = true;
    for (int c = 0; c < 12; c++) { map[c] = base + c; if (item_known(base + c)) continue; double tot = 0, w[12];
      for (int i = 0; i < 12; i++) { w[i] = used[i] ? 0 : idb_p(base + c, K[i]); tot += w[i]; }
      if (tot <= 0) { for (int i = 0; i < 12; i++) { w[i] = used[i] ? 0 : (pot ? IDB_PW[i] + 1 : IDB_SW[i] + 1); tot += w[i]; } }
      r = r * 1664525u + 1013904223u; double x = (r >> 8) / 16777216.0 * tot;
      for (int i = 0; i < 12; i++) { if (w[i] <= 0) continue; if (x < w[i]) { map[c] = K[i]; used[i] = true; break; } x -= w[i]; } }
    for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { Item *it = game_slot(s2); int c = it->kind - base; if (c >= 0 && c < 12 && !item_known(it->kind)) it->kind = map[c]; }
    for (int i = 0; i < G.n_heaps; i++) { Item *it = &G.heaps[i].item; int c = it->kind - base; if (c >= 0 && c < 12 && !item_known(it->kind)) it->kind = map[c]; } }
  { static const uint16_t WW[12] = { 10, 10, 15, 6, 10, 11, 15, 10, 6, 10, 5, 5 };   // gen.c Generator wands (teleport .. avalanche)
    static const uint8_t WK[12] = { IK_WND_TELEPORT, IK_WND_SLOW, IK_WND_FIREBOLT, IK_WND_REGROWTH, IK_WND_POISON, IK_WND_BLINK, IK_WND_LIGHTNING, IK_WND_AMOK, IK_WND_REACH, IK_WND_FLOCK, IK_WND_DISINTEGRATION, IK_WND_AVALANCHE };
    bool used[12] = { 0 }; for (int i = 0; i < 12; i++) if (item_known(WK[i])) used[i] = true;
    for (int s2 = -1; s2 < PACK0 + 32; s2 = s2 == BAG_SIZE - 1 ? PACK0 : s2 + 1) { Item *it = game_slot(s2); if (item_cat(it->kind) != CAT_WAND || item_known(it->kind) || it->kind == IK_WND_MM) continue;
      int tot = 0; for (int i = 0; i < 12; i++) if (!used[i]) tot += WW[i]; if (tot <= 0) continue; r = r * 1664525u + 1013904223u; int x = (r >> 8) % tot, nk = it->kind;
      for (int i = 0; i < 12; i++) { if (used[i]) continue; if (x < WW[i]) { nk = WK[i]; used[i] = true; break; } x -= WW[i]; }
      int old = it->kind; for (int s3 = -1; s3 < PACK0 + 32; s3 = s3 == BAG_SIZE - 1 ? PACK0 : s3 + 1) if (game_slot(s3)->kind == old) game_slot(s3)->kind = nk; } }
}
static int playout(Cand c, uint32_t seed) {
  int fds[2]; if (pipe(fds)) return -1000000;
  st_la_forks++;
  pid_t pid = fork();
  if (pid == 0) { signal(SIGALRM, SIG_DFL); alarm(900);
    close(fds[0]); s_rollout = 1; V = 0;
    g_rng ^= seed * 2654435761u; srand(seed); if (fair_on() && !getenv("LACHEAT")) la_scrub(seed); else resample_unknowns(seed);
    Hero *h = &G.hero; int hp0 = h->HP, ht0 = h->HT, exp0 = h->exp, lvl0 = h->lvl; int32_t now0 = G.now; s_la_boss0 = boss_hp();
    int sc;
    if (!apply_cand(c)) sc = -1000000;
    else {
      for (int k = 0, calm = 0; k < LA_K && h->HP > 0 && !s_pend_depth && calm < 3; k++) { if (!la_step()) break; calm = hostile_near(8, NULL) ? 0 : calm + 1; }   // (until the fight is over)
      sc = score_now(hp0, ht0, exp0, lvl0, now0);
    }
    if (write(fds[1], &sc, sizeof sc) != sizeof sc) {}
    _exit(0);
  }
  close(fds[1]); int sc = -1000000; if (read(fds[0], &sc, sizeof sc) != sizeof sc) sc = -1000000; close(fds[0]); int stt; waitpid(pid, &stt, 0);
  return sc;
}

// 0.58 (owner: "use the look-ahead to find the strategies"): BLAME=1: a frozen copy of the game every BLK decisions (the
// last BLN kept); when the hero dies, each copy, newest first, tries every move it had then (the look-ahead's list:
// steps, retreats to a doorway, the stairs up, rest, each item), BLR times with fresh luck, the plain rules after it,
// and prints which first move would have lived to 150 turns past the death (or got below). An analysis tool only.
#define BLN 6
static char s_bl_sname[12][24]; static int s_bl_fd[BLN], s_bl_n; static pid_t s_bl_pid[BLN]; static int32_t s_bl_t[BLN]; static int s_bl_hp[BLN];
static void bl_kill_all(void) { for (int i = 0; i < BLN; i++) if (s_bl_pid[i] > 0) { close(s_bl_fd[i]); kill(s_bl_pid[i], SIGKILL); int st; waitpid(s_bl_pid[i], &st, 0); s_bl_pid[i] = 0; } s_bl_n = 0; }
static void bl_desc(Cand c, char *b, int n) {
  if (c.t == C_TAP) { const char *w = c.act == 1 ? "retreat to" : c.act == 2 ? "stairs up at" : c.a == G.hero.pos ? "stairs here" : mob_at(c.a) >= 0 ? "hit" : "step to"; int mi = mob_at(c.a); if (mi >= 0 && c.act == 0) snprintf(b, n, "hit %s", mob_name(G.mobs[mi].type)); else snprintf(b, n, "%s %d,%d", w, cx(c.a), cy(c.a)); return; }
  if (c.t == C_ITEM || c.t == C_ZAP || c.t == C_THROW) { char nm[40]; Item *it = game_slot(c.a); item_name(it, nm, sizeof nm); snprintf(b, n, "%s %s", c.t == C_ZAP ? "zap" : c.t == C_THROW ? "throw" : c.act == ACN_DRINK ? "drink" : c.act == ACN_READ ? "read" : c.act == ACN_EAT ? "eat" : "use", nm); return; }
  if (c.t == C_DEFAULT && c.act) { snprintf(b, n, "style %s", s_bl_sname[c.act - 1]); return; }
  snprintf(b, n, "%s", CNAME[c.t]);
}
static void bl_eval(int g, int ddepth, int32_t dnow, int age) {
  Cand c[80]; int n = cands(c); int R = getenv("BLR") ? atoi(getenv("BLR")) : 4; Hero *h = &G.hero; int cdep = G.depth;
  // the styles (behaviour switches for the whole playout, first move the default): after the moves
  const char *spec = getenv("BLV") ? getenv("BLV") : "throw:THROWFIRST-1/flee:STAIRFLEE-1/careful:ENGAGEHP-90/restfull:RESTFULL-1/erest:EARLYREST-1/nohunt:OLDHUNT-1";
  char sbuf[600]; snprintf(sbuf, sizeof sbuf, "%s", spec); char sname[12][24], senv[12][120]; int ns = 0;
  for (char *sv = NULL, *v = strtok_r(sbuf, "/", &sv); v && ns < 12; v = strtok_r(NULL, "/", &sv)) { char *colon = strchr(v, ':'); if (colon) *colon = 0; snprintf(sname[ns], 24, "%s", v); snprintf(senv[ns], 120, "%s", colon ? colon + 1 : ""); ns++; }
  int n0 = n; (void)n0; for (int z = 0; z < ns && n < 80; z++) c[n++] = (Cand){ C_DEFAULT, (uint8_t)(1 + z), 0, 0 };   // (act: the style, 1-based)
  for (int z = 0; z < ns; z++) snprintf(s_bl_sname[z], 24, "%s", sname[z]);
  int ok[80] = { 0 }; for (int i = 0; i < n; i++) { ok[i] = 0;
    for (int r = 0; r < R; r++) { int p[2]; if (pipe(p)) break; fflush(stdout); pid_t k = fork();
      if (k == 0) { close(p[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); }
        uint32_t seed = (uint32_t)(r + 1) * 7919u + s_salt * 104729u; g_rng ^= seed * 2654435761u; srand(seed); resample_unknowns(seed);   // (the same luck for every option at one r)
        if (c[i].t == C_DEFAULT && c[i].act) fla_apply(senv[c[i].act - 1]);
        int res = 0;
        if (apply_cand(c[i])) for (int q = 0; q < 4000; q++) { if (h->HP <= 0) { res = 0; break; } if (G.max_depth > ddepth) { res = 1; break; } if (G.now >= dnow + (getenv("BLNEXT") ? 300 : 150) * TICK) { res = getenv("BLNEXT") ? G.depth >= cdep : 1; break; } if (!la_step()) walk(game_rest(false)); }   /* (BLNEXT: alive 300 turns past the death and not hiding upstairs) */   /* (BLNEXT: only getting below counts: stalling upstairs doesn't) */

        if (getenv("BLDBG")) fprintf(stderr, "  bl cand %d r%d: res %d hp %d depth %d now %d dnow %d\n", i, r, res, h->HP, G.depth, G.now / TICK, dnow / TICK);
        if (write(p[1], &res, sizeof res) != sizeof res) {} _exit(0); }
      close(p[1]); int res = 0; if (read(p[0], &res, sizeof res) != sizeof res) res = 0; close(p[0]); int st; waitpid(k, &st, 0); ok[i] += res; } }
  char foes[200]; int fn = 0; foes[0] = 0;
  for (int i = 0; i < G.n_mobs && fn < 150; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE) continue; fn += snprintf(foes + fn, sizeof foes - fn, " %s(%d/%d,%s,d%d)", mob_name(m->type), m->HP, m->HT, m->state == ST_HUNTING ? "hunt" : m->state == ST_SLEEPING ? "sleep" : "wand", distance(m->pos, h->pos)); }
  int ord[80]; for (int i = 0; i < n; i++) ord[i] = i; for (int i = 1; i < n; i++) for (int j = i; j > 0 && ok[ord[j]] > ok[ord[j - 1]]; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
  printf("BLAME game %d cls %d d%d %d turns before death: hp %d/%d lvl %d hunger %d | foes:%s | default %d/%d | best:", g, g_new_class, G.depth, age, h->HP, h->HT, h->lvl, h->hunger, fn ? foes : " none", ok[0], R);
  for (int z = 0; z < n && z < 3; z++) { char b[80]; bl_desc(c[ord[z]], b, sizeof b); printf(" [%s %d/%d]", b, ok[ord[z]], R); }
  printf(" | styles:"); for (int i = 0; i < n; i++) if (c[i].t == C_DEFAULT && c[i].act) printf(" %s %d", s_bl_sname[c[i].act - 1], ok[i]);
  printf("\n"); fflush(stdout);
}
static void bl_checkpoint(int g) {
  int slot = s_bl_n % BLN; if (s_bl_pid[slot] > 0) { close(s_bl_fd[slot]); kill(s_bl_pid[slot], SIGKILL); int st; waitpid(s_bl_pid[slot], &st, 0); s_bl_pid[slot] = 0; }
  fflush(stdout); int fds[2]; if (pipe(fds)) return; pid_t pid = fork(); if (pid < 0) { close(fds[0]); close(fds[1]); return; }
  if (pid > 0) { close(fds[0]); s_bl_fd[slot] = fds[1]; s_bl_pid[slot] = pid; s_bl_t[slot] = G.now; s_bl_hp[slot] = G.hero.HP; s_bl_n++; return; }
  close(fds[1]); for (int i = 0; i < BLN; i++) if (i != slot && s_bl_pid[i] > 0) close(s_bl_fd[i]);
  int32_t msg[3]; if (read(fds[0], msg, sizeof msg) != sizeof msg) _exit(0);
  bl_eval(g, msg[0], msg[1], (msg[1] - G.now) / TICK); _exit(0);
}
static void bl_on_death(void) {
  int32_t msg[3] = { G.depth, G.now, 0 };
  for (int k = 0; k < BLN; k++) { int slot = ((s_bl_n - 1 - k) % BLN + BLN) % BLN; if (s_bl_n - 1 - k < 0 || s_bl_pid[slot] <= 0) break;
    fflush(stdout); if (write(s_bl_fd[slot], msg, sizeof msg) != sizeof msg) {} close(s_bl_fd[slot]); int st; waitpid(s_bl_pid[slot], &st, 0); s_bl_pid[slot] = 0; }
  bl_kill_all();
}
static bool lookahead(void) {
  if (s_rollout || s_is_child || getenv("NOLA") || !danger()) return false;
  if (!getenv("GOO_LA")) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && G.mobs[i].type == MT_GOO && G.mobs[i].state == ST_HUNTING && BIT_GET(G.fov, G.mobs[i].pos)) return false;   // (the Goo plan decides)
  Cand c[80]; int n = cands(c); if (n <= 1) return false;
  static long tot[80]; static int cnt[80];
  uint32_t base = (uint32_t)(cur_game * 1000003 + s_bot_steps * 7919);
  // 1: are the ordinary rules safe here? (6 playouts: nobody dies, half the health or more kept: no search)
  tot[0] = 0; cnt[0] = 0; int deaths = 0;
  int LM = getenv("LAMULT") ? atoi(getenv("LAMULT")) : 1; if (LM < 1) LM = 1;   // 0.57: heavier look-ahead (the PC is fast)
  for (int r = 0; r < 6 * LM; r++) { int sc = playout(c[0], base + r * 104729); tot[0] += sc; cnt[0]++; if (sc < -50000) deaths++; }
  if (!deaths && tot[0] / cnt[0] >= 2000 + 500 - 300) return false;   // (2000 + health*1000/HT ... : about half the health or more)
  st_la_points++;
  if (getenv("LADBG")) { printf("  LA game %d step %d hero %d hp %d/%d deaths %d/6 foes:", cur_game, s_bot_steps, G.hero.pos, G.hero.HP, G.hero.HT, deaths); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && BIT_GET(G.fov, G.mobs[i].pos)) printf(" %s@%d(hp%d,st%d)", mob_name(G.mobs[i].type), G.mobs[i].pos, G.mobs[i].HP, G.mobs[i].state); printf("\n"); }
  // 2: every option, 3 times
  for (int i = 1; i < n; i++) { tot[i] = 0; cnt[i] = 0; for (int r = 0; r < 3 * LM; r++) { tot[i] += playout(c[i], base + r * 104729); cnt[i]++; } }
  // 3: the 4 best (and the rules' choice) 9 more times each
  int ord[80]; for (int i = 0; i < n; i++) ord[i] = i;
  for (int a = 1; a < n; a++) for (int b = a + 1; b < n; b++) if (tot[ord[b]] * cnt[ord[a]] > tot[ord[a]] * cnt[ord[b]]) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
  for (int q = 0; q < 5 && q < n; q++) { int i = q == 0 ? 0 : ord[q]; if (q && i == 0) continue; int r0 = cnt[i]; for (int r = r0; r < 12 * LM; r++) { tot[i] += playout(c[i], base + r * 104729); cnt[i]++; } }
  if (s_pztest && !getenv("NOHEALVAL") && !s_rollout) for (int i = 1; i < n; i++) if (c[i].t == C_ITEM && c[i].act == ACN_DRINK && game_slot(c[i].a)->kind == IK_POT_HEALING && item_known(IK_POT_HEALING)) {   // 0.93 (DM lab: 0 Healings at floor 15: 11% pass; 3+: 85-95%): a Healing drunk now is one the boss fight won't have
      int hv = getenv("HEALVAL") ? atoi(getenv("HEALVAL")) : 600; bool bossnow = false; for (int j = 0; j < G.n_mobs; j++) if (G.mobs[j].HP > 0 && (MOB_DEFS[G.mobs[j].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[j].pos)) bossnow = true;
      if (!bossnow) tot[i] -= (long)hv * cnt[i]; }
  int bi = 0; long bm = tot[0] / cnt[0];
  for (int i = 1; i < n; i++) if (cnt[i] >= 12 * LM && tot[i] / cnt[i] > bm) { bm = tot[i] / cnt[i]; bi = i; }
  if (getenv("LADBG")) for (int i = 0; i < n; i++) { char nm[64] = ""; if (c[i].t >= C_ITEM) item_name(game_slot(c[i].a), nm, sizeof nm); printf("    cand %-7s %-24s %4d -> %ld (%d)%s\n", CNAME[c[i].t], nm, c[i].t == C_TAP ? c[i].a : c[i].b, tot[i] / cnt[i], cnt[i], i == bi ? " <==" : ""); }
  if (getenv("LAFEAT") && !s_rollout && !s_is_child) {   // 0.93 (teacher, every decision in a plain fight): the numbers a watch could work out, and what the look-ahead chose
    Hero *h = &G.hero; int ai = -1, na = 0, no = 0; double odpt = 0, adpt = 0; bool boss = false;
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE) continue; if (MOB_DEFS[m->type].flags & MD_BOSS) boss = true; int d = distance(m->pos, h->pos);
      if (d <= 1 && m->state == ST_HUNTING) { na++; adpt += foe_dpt(m); if (ai < 0 || m->HP < G.mobs[ai].HP) ai = i; } else if (d <= 4 && m->state == ST_HUNTING) { no++; odpt += foe_dpt(m); } }
    if (ai >= 0 && !boss) { Mob *m = &G.mobs[ai]; double hd = hero_dpt(m), fd = foe_dpt(m); int bs = -1; long bsv = -1000000000L;
      for (int i = 1; i < n; i++) if (c[i].t == C_TAP && mob_at(c[i].a) < 0 && c[i].a != h->pos && tot[i] / cnt[i] > bsv) { bsv = tot[i] / cnt[i]; bs = i; }
      int o1 = 0; if (bs >= 0) for (int j = 0; j < 8; j++) { int q = c[bs].a + N8[j]; if (nb_ok(c[bs].a, q) && PASSABLE(G.map[q])) o1++; }
      int o0 = 0; for (int j = 0; j < 8; j++) { int q = h->pos + N8[j]; if (nb_ok(h->pos, q) && PASSABLE(G.map[q])) o0++; }
      const char *pick = bi == 0 || bm < tot[0] / cnt[0] + 150 ? "rules" : (c[bi].t == C_TAP && mob_at(c[bi].a) < 0) ? "step" : "other";
      printf("LAF d%d c%d hp%d ht%d lvl%d na%d no%d foe=%s fhp%d fht%d fast%d hd%.2f fd%.2f ad%.2f od%.2f tkill%.1f tdie%.1f o0 %d o1 %d starv%d pick=%s stepgain=%ld\n", G.depth, h->hclass & 3, h->HP, h->HT, h->lvl, na, no, mob_name(m->type), m->HP, m->HT, mob_move_cost(m) < hero_move_cost(), hd, fd, adpt, odpt,
        m->HP / (hd > 0.01 ? hd : 0.01), h->HP / (adpt + odpt > 0.01 ? adpt + odpt : 0.01), o0, o1, game_starving(), pick, bs >= 0 ? bsv - tot[0] / cnt[0] : -999999L); } }
  if (getenv("LACELL") && !s_rollout && !s_is_child) {   // 0.93 (teacher: where it steps): every step candidate's features and score, in one decision, beside staying (the rules)
    Hero *h = &G.hero; int nh = 0, adj = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *o = &G.mobs[i]; if (o->HP > 0 && !ally(i) && BIT_GET(G.fov, o->pos) && o->state == ST_HUNTING && !(MOB_DEFS[o->type].flags & MD_BOSS)) { nh++; if (distance(o->pos, h->pos) <= 1) adj++; } }
    static int dec; if (adj) { dec++; printf("LCD %d d%d hp%d%% nh%d adj%d stay %ld\n", dec, G.depth, h->HP * 100 / (h->HT ? h->HT : 1), nh, adj, tot[0] / cnt[0]);
      for (int i = 1; i < n; i++) { if (c[i].t != C_TAP || mob_at(c[i].a) >= 0 || c[i].a == h->pos) continue; int q = c[i].a, open = 0, reach = 0, near = 0, mind = 99, sumd = 0;
        for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && PASSABLE(G.map[r]) && !(TFLAGS(G.map[r]) & F_AVOID)) open++; }
        for (int k = 0; k < G.n_mobs; k++) { Mob *o = &G.mobs[k]; if (o->HP <= 0 || ally(k) || !BIT_GET(G.fov, o->pos) || o->state != ST_HUNTING) continue; int d = distance(o->pos, q); if (d <= 1) near++; if (d <= 2) reach++; if (d < mind) mind = d; sumd += d; }
        printf("LCC %d step=%d dist=%d open=%d near=%d reach=%d mind=%d sumd=%d water=%d door=%d stairs=%d score=%ld\n", dec, distance(q, h->pos), b_dist[q], open, near, reach, mind, sumd, IS_WATER(G.map[q]) ? 1 : 0, (G.map[q] == T_DOOR || G.map[q] == T_OPEN_DOOR) ? 1 : 0, (q == G.exit || q == G.entrance) ? 1 : 0, tot[i] / cnt[i]); } } }
  if (bi == 0 || bm < tot[0] / cnt[0] + 150) return false;   // not clearly better: the rules
  if (getenv("LAONLY")) {   // 0.93 diagnostic (teacher): let only one kind of override through, to find which kind carries the gain
    const char *o = getenv("LAONLY"); int t = c[bi].t; bool mob = t == C_TAP && mob_at(c[bi].a) >= 0 && !ally(mob_at(c[bi].a));
    const char *k = t == C_REST ? "rest" : t == C_TAP ? (c[bi].a == G.hero.pos ? "stairs" : mob ? "attack" : "move") : t == C_ITEM ? "item" : (t == C_ZAP || t == C_THROW) ? "ranged" : "other";
    if (!strstr(o, k)) return false; }
  if (getenv("LACTX")) {   // 0.93 diagnostic: only in one kind of situation (boss in sight / the rules' plain fight / anything else)
    bool boss = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[i].pos)) boss = true;
    char dflt[96] = "?"; if (!boss) { int fd[2]; if (!pipe(fd)) { fflush(stdout); pid_t kk = fork(); if (kk == 0) { close(fd[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } s_did[0] = 0; decide(); char b[96]; snprintf(b, sizeof b, "%s", s_did[0] ? s_did : "-"); if (write(fd[1], b, sizeof b) != sizeof b) {} _exit(0); }
        close(fd[1]); if (read(fd[0], dflt, sizeof dflt) != sizeof dflt) dflt[0] = 0; close(fd[0]); int st2; waitpid(kk, &st2, 0); } }
    const char *ctx = boss ? "boss" : !strncmp(dflt, "fight ", 6) ? "fight" : "other";
    if (!strstr(getenv("LACTX"), ctx)) return false; }
  st_la_changed++;
  if (getenv("LARANDSTEP") && c[bi].t == C_TAP && mob_at(c[bi].a) < 0 && c[bi].a != G.hero.pos) {   // 0.93 diagnostic: is it the cell, or just not doing the rules' move? a random step instead
    int m[16], nm = 0; for (int i = 1; i < n && nm < 16; i++) if (c[i].t == C_TAP && mob_at(c[i].a) < 0 && c[i].a != G.hero.pos && distance(c[i].a, G.hero.pos) == 1) m[nm++] = i; if (nm) bi = m[rand() % nm]; }
  if (getenv("LATEACH") && !s_rollout && !s_is_child) {   // 0.93 (owner: the look-ahead as a teacher): what it chose over the rules, and in what situation
    Hero *h = &G.hero; int nf = nearest_hostile(), fp = nf >= 0 ? G.mobs[nf].pos : -1; char what[96] = "?", nm[64] = "";
    if (c[bi].t >= C_ITEM) { Item *it = game_slot(c[bi].a); if (item_known(it->kind) || item_cat(it->kind) > CAT_SCROLL) item_name(it, nm, sizeof nm); else snprintf(nm, sizeof nm, "unknown %s", item_cat(it->kind) == CAT_POTION ? "potion" : "scroll"); }
    if (c[bi].t == C_REST) snprintf(what, sizeof what, "rest");
    else if (c[bi].t == C_TAP) { int q = c[bi].a, mi = mob_at(q);
      if (q == h->pos) snprintf(what, sizeof what, "stairs");
      else if (mi >= 0 && !ally(mi)) snprintf(what, sizeof what, "attack %s%s", mob_name(G.mobs[mi].type), mi == nf ? "" : " (not the nearest)");
      else { int d0 = fp >= 0 ? distance(h->pos, fp) : 0, d1 = fp >= 0 ? distance(q, fp) : 0; snprintf(what, sizeof what, "step %s%s", d1 > d0 ? "away" : d1 < d0 ? "toward" : "aside", q == G.exit || q == G.entrance ? " onto stairs" : IS_WATER(G.map[q]) ? " into water" : G.map[q] == T_DOOR || G.map[q] == T_OPEN_DOOR ? " into a doorway" : ""); } }
    else { int mi = c[bi].b >= 0 && c[bi].b < MAP_LEN ? mob_at(c[bi].b) : -1; snprintf(what, sizeof what, "%s %s%s%s", CNAME[c[bi].t], nm, mi >= 0 ? " at " : "", mi >= 0 ? mob_name(G.mobs[mi].type) : ""); }
    printf("LAT d%d c%d hp%d%% lvl%d foes", G.depth, h->hclass & 3, h->HP * 100 / (h->HT ? h->HT : 1), h->lvl);
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && m->state != ST_PASSIVE) printf(" %s:%d:%d%%", mob_name(m->type), distance(m->pos, h->pos), m->HP * 100 / (m->HT ? m->HT : 1)); }
    { char dflt[96] = "?"; int fd[2]; if (!pipe(fd)) { fflush(stdout); pid_t k = fork(); if (k == 0) { close(fd[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } s_did[0] = 0; decide(); char b[96]; snprintf(b, sizeof b, "%s", s_did[0] ? s_did : "-"); if (write(fd[1], b, sizeof b) != sizeof b) {} _exit(0); }
        close(fd[1]); if (read(fd[0], dflt, sizeof dflt) != sizeof dflt) snprintf(dflt, sizeof dflt, "?"); close(fd[0]); int st2; waitpid(k, &st2, 0); }
      for (char *q = dflt; *q; q++) if (*q == '|') *q = '/';
      int o0 = 0, o1 = 0, r1 = 0; if (c[bi].t == C_TAP && mob_at(c[bi].a) < 0) { int q1 = c[bi].a; for (int j = 0; j < 8; j++) { int a0 = h->pos + N8[j], a1 = q1 + N8[j]; if (nb_ok(h->pos, a0) && PASSABLE(G.map[a0])) o0++; if (nb_ok(q1, a1) && PASSABLE(G.map[a1])) o1++; }
        for (int i = 0; i < G.n_mobs; i++) { Mob *mm = &G.mobs[i]; if (mm->HP > 0 && !ally(i) && BIT_GET(G.fov, mm->pos) && mm->state == ST_HUNTING && distance(mm->pos, q1) <= 2) r1++; } }
      printf(" | pick %s | gain %ld | rules: %s | open %d->%d reach %d\n", what, bm - tot[0] / cnt[0], dflt, o0, o1, r1); } }
  { char nm[64] = ""; if (c[bi].t >= C_ITEM) item_name(game_slot(c[bi].a), nm, sizeof nm); DID("look-ahead: %s %s%s%d (%ld vs %ld)", CNAME[c[bi].t], nm, nm[0] ? " " : "", c[bi].t == C_TAP ? c[bi].a : c[bi].b, bm, tot[0] / cnt[0]); }
  apply_cand(c[bi]);
  return true;
}

// ==== bot3: STRATEGIC LOOK-AHEAD for rare, big decisions (a scroll of upgrade: which item, or hold it). Each option is
// played forward a long way (through the next floors and boss) many times with different luck; the best average wins.
static long st_sla_points;
static int strat_score(int d0) {
  Hero *h = &G.hero; int dd = G.depth - d0; if (s_pend_depth > G.depth) dd++;
  return dd * 1000 + (h->HP > 0 ? 2500 + h->HP * 300 / h->HT : 0);
}
static int strat_playout(int scroll, int target, uint32_t seed) {
  int fds[2]; if (pipe(fds)) return -1;
  pid_t pid = fork();
  if (pid == 0) { signal(SIGALRM, SIG_DFL); alarm(900);
    close(fds[0]); s_rollout = 2; V = 0; g_rng ^= seed * 2654435761u; srand(seed); if (s_pztest) resample_unknowns(seed);   /* 0.92 (owner: no peeking): unknown potions and scrolls are any of the unknown kinds */
    int d0 = G.depth, sc;
    if (target == -1000) s_upg_hold_until = G.now + 400 * TICK; else { s_force_pick = target; do_item(scroll, ACN_READ); if (s_pick >= 0) la_step(); }
    int steps = 0;
    while (G.hero.HP > 0 && steps < 2500 && G.depth < d0 + 3) {
      if (s_pend_depth) { s_arrival = true; game_level_commit(); s_arrival = false; if (G.depth != s_last_depth) { s_last_depth = G.depth; on_new_floor(G.depth > G.max_depth - 1); } }
      if (!la_step()) break; steps++;
    }
    sc = strat_score(d0);
    if (write(fds[1], &sc, sizeof sc) != sizeof sc) {}
    _exit(0);
  }
  close(fds[1]); int sc = -1; if (read(fds[0], &sc, sizeof sc) != sizeof sc) sc = -1; close(fds[0]); int stt; waitpid(pid, &stt, 0);
  return sc;
}
// 0.92 (owner, seed 84: beat the ghost's curse with the warrior, took his scale armour): take it on when the bot's own play wins it,
// played forward from here (fight and all) a few times; the wand rule was a guess

static int curse_playout(uint32_t seed) {
  int fds[2]; if (pipe(fds)) return 0;
  pid_t pid = fork();
  if (pid == 0) { signal(SIGALRM, SIG_DFL); alarm(300);
    close(fds[0]); s_rollout = 2; s_curse_force = 1; V = 0; g_rng ^= seed * 2654435761u; srand(seed); if (s_pztest) resample_unknowns(seed);   /* 0.92 (owner: no peeking): unknown potions and scrolls are any of the unknown kinds */
    int d0 = G.depth, steps = 0, ok = 0;
    while (G.hero.HP > 0 && steps < 400 && G.depth == d0 && !s_pend_depth) { if (G.q.ghost >= QS_DONE) { ok = 1; break; } if (!la_step()) break; if (getenv("CURSEDBG") && steps < 4 && seed % 7 == 0) fprintf(stderr, "   child %d: %s\n", steps, s_did); steps++; }
    if (G.hero.HP <= 0) ok = 0; else if (G.q.ghost >= QS_DONE) ok = 1 + G.hero.HP * 100 / G.hero.HT; else ok = G.depth != d0 || s_pend_depth ? -2 : steps >= 400 ? -3 : -4;
    if (write(fds[1], &ok, sizeof ok) != sizeof ok) {}
    _exit(0);
  }
  close(fds[1]); int ok = 0; if (read(fds[0], &ok, sizeof ok) != sizeof ok) ok = 0; close(fds[0]); int stt; waitpid(pid, &stt, 0);
  return ok;
}
static bool curse_go(void);
static bool curse_go_full(void) { Hero *h = &G.hero; int hp = h->HP; h->HP = h->HT; bool r = curse_go(); h->HP = hp; return r; }   // (would it be, rested?)
static int curse_odds(void) {
  if (getenv("CURSEGO") || s_curse_force) return 100;
  if (!s_pztest || getenv("NOCURSESIM") || s_rollout || s_is_child) return 0;
  static long key = -1; static int ans; static long go_key = -1;   // 0.92 (seed 84: 33/40 go, then 28/40 no after a shuriken was thrown and picked up): a go sticks while level, gear and the flame potion stay
  { Hero *hh = &G.hero; long gk = ((long)cur_game * 64 + G.depth) * 4096 + hh->lvl * 128 + hh->STR * 4; gk = gk * 256 + hh->weapon.kind; gk = gk * 256 + hh->armor.kind; gk = gk * 8 + (find_kind(IK_POT_FLAME) >= 0 && item_known(IK_POT_FLAME) ? 1 : 0) + (find_kind(IK_SEED_FIREBLOOM) >= 0 ? 2 : 0) + (find_kind(IK_BOMB) >= 0 ? 4 : 0);
    if (gk == go_key && hh->HP * 10 >= hh->HT * 9) return 100; }
  Hero *h = &G.hero; long k = ((long)cur_game * 64 + G.depth) * 4096 + h->lvl * 128 + h->STR * 4 + (h->HP * 4 / (h->HT ? h->HT : 1));
  k = k * 256 + h->weapon.kind; k = k * 256 + h->armor.kind; k = k * 1048576 + curse_kit_sig();
  if (k == key) return ans;
  key = k; int wins = 0, hp = 0, n = getenv("CURSEN") ? atoi(getenv("CURSEN")) : 40; uint32_t base = (uint32_t)(cur_game * 9173 + s_bot_steps * 37);
  char rs[2400] = ""; for (int r = 0; r < n; r++) { int o = curse_playout(base + r * 104729); if (o > 0) { wins++; hp += o - 1; } char t[8]; snprintf(t, sizeof t, "%d ", o); strcat(rs, t); }
  if (getenv("CURSEDBG")) printf("  playouts: %s\n", rs);
  ans = wins * 100 / n; if (ans >= 80) { Hero *hh = &G.hero; long gk = ((long)cur_game * 64 + G.depth) * 4096 + hh->lvl * 128 + hh->STR * 4; gk = gk * 256 + hh->weapon.kind; gk = gk * 256 + hh->armor.kind; gk = gk * 8 + (find_kind(IK_POT_FLAME) >= 0 && item_known(IK_POT_FLAME) ? 1 : 0) + (find_kind(IK_SEED_FIREBLOOM) >= 0 ? 2 : 0) + (find_kind(IK_BOMB) >= 0 ? 4 : 0); go_key = gk; }   // (0.92: the armour it gives is the difference on floor 4 (crabs): 4 honest runs without it all died there; 1 in 5 risk is worth it)
  if (getenv("CURSEDBG")) printf("CURSE d%d step %d lvl %d hp %d/%d wpn %d: %d/%d won (avg hp left %d%%) -> %s\n", G.depth, s_bot_steps, h->lvl, h->HP, h->HT, h->weapon.kind, wins, n, wins ? hp / wins : 0, ans >= 80 ? "80%+" : "low");
  return ans;
}
static bool curse_go(void) {   // 0.92: met mid-floor, only a near-sure thing (the floor may still hold Strength, a better weapon); at the floor's end, 4 in 5
  int o = curse_odds(); return o >= (floor_left() ? 95 : (getenv("CURSEPCT") ? atoi(getenv("CURSEPCT")) : 80)); }
// 0.92: from a later floor, "would I beat the ghost's curse now?": the same fight played here (a curse of his floor's
// strength beside me), my own play, a few times; then back up for it if so
static int curse_sim1(uint32_t seed) {
  int fds[2]; if (pipe(fds)) return 0;
  pid_t pid = fork();
  if (pid == 0) { signal(SIGALRM, SIG_DFL); alarm(300);
    close(fds[0]); s_rollout = 2; V = 0; g_rng ^= seed * 2654435761u; srand(seed); resample_unknowns(seed);
    int ok = 0, c = -1, d0 = G.depth; for (int k = 0; k < 8 && c < 0; k++) { int n = G.hero.pos + N8[k]; if (nb_ok(G.hero.pos, n) && PASSABLE(G.map[n]) && !char_at(n)) c = n; }
    int mi = c >= 0 ? spawn_mob(MT_WRAITH, c, ST_HUNTING, 0) : -1;
    if (mi >= 0) { int uid = G.mobs[mi].uid; G.mobs[mi].aux2 = 1; G.mobs[mi].HT = G.mobs[mi].HP = 10 + G.q.ghost_depth * 3;
      for (int steps = 0; steps < 250 && G.hero.HP > 0 && G.depth == d0 && !s_pend_depth; steps++) { bool alive = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].uid == uid && G.mobs[i].HP > 0) alive = true; if (!alive) { ok = 1; break; } if (!la_step()) break; }
      if (G.hero.HP <= 0) ok = 0; }
    if (write(fds[1], &ok, sizeof ok) != sizeof ok) {}
    _exit(0);
  }
  close(fds[1]); int ok = 0; if (read(fds[0], &ok, sizeof ok) != sizeof ok) ok = 0; close(fds[0]); int stt; waitpid(pid, &stt, 0);
  return ok;
}
// 0.92 (seed 84: up and down three times between the cheap odds and the real ones): s_curse_no_key: the real odds said no in this state: no more trips till it changes
static long curse_key(void) { Hero *h = &G.hero; long k = ((long)cur_game * 64 + G.q.ghost_depth) * 4096 + h->lvl * 128 + h->STR * 4; k = k * 256 + h->weapon.kind; k = k * 256 + h->armor.kind; k = k * 1048576 + curse_kit_sig(); return k; }
static bool curse_food_ok(void) {                      // 0.92 (eat-late keeps the hero Hungry most of the time; the curse plan was gated on not being hungry and never ran): only Starving with nothing to eat rules it out
  if (!game_starving()) return true; for (int i = 0; i < BAG_SIZE; i++) if (is_food(G.hero.bag[i].kind) && G.hero.bag[i].kind != IK_MYSTERY_MEAT) return true; return false; }
static bool food_pressed(void) { return s_pztest && !getenv("NOFOODP") ? !curse_food_ok() : game_hungry(); }
static long curse_kit_sig(void) {                       // 0.92 (seed 84: the odds were cached at 30% before the flame potion was known, and never looked at again): the kit is part of the state
  Hero *h = &G.hero; long sig = h->weapon.lvl * 7 + h->armor.lvl * 13;
  for (int i = 0; i < BAG_SIZE; i++) { int k = h->bag[i].kind; if (!k) continue; int c = item_cat(k);
    if ((c == CAT_POTION && item_known(k) && k == IK_POT_FLAME) || k == IK_BOMB || k == IK_SEED_FIREBLOOM || c == CAT_MISSILE || (c == CAT_WAND && item_known(k))) sig = sig * 31 + k * 4 + (h->bag[i].qty > 3 ? 3 : h->bag[i].qty); }
  return sig & 0xFFFFF; }   // (0.92: "no time, food matters" under eat-late: Starving with nothing to eat)
static bool curse_sim_ok(void) {
  static long key = -1; static bool ans; Hero *h = &G.hero;
  long k = curse_key();
  if (k == s_curse_no_key) return false;
  if (k == key) return ans;
  key = k; int n = getenv("CURSEN") ? atoi(getenv("CURSEN")) : 20, w = 0; uint32_t base = (uint32_t)(cur_game * 5153 + s_bot_steps * 41);
  int hp = h->HP; h->HP = h->HT; for (int r = 0; r < n; r++) w += curse_sim1(base + r * 104729); h->HP = hp;   /* (rested first, as it would be) */
  ans = w >= n * 9 / 10;
  if (getenv("CURSEDBG")) printf("CURSESIM d%d step %d lvl %d wpn %d arm %d: %d/%d -> %s\n", G.depth, s_bot_steps, h->lvl, h->weapon.kind, h->armor.kind, w, n, ans ? "back up for it" : "no");
  return ans;
}
static int strategic_upgrade(int scroll) {              // 1: read on the best target, 0: hold it, -1: no choice to make
  int tg[24], nt = 0;
  for (int sl = -2; sl < BAG_SIZE && nt < 23; sl++) { if (sl == 0 && 0) continue; Item *it = game_slot(sl); int c = item_cat(it->kind);
    if (!it->kind || ((c != CAT_WEAPON && c != CAT_ARMOR && c != CAT_WAND) && !(it->kind == IK_BOOMERANG && hunt2())) || !game_pick_ok(PICK_UPGRADE, sl)) continue; if (hunt2() && c == CAT_WEAPON && item_tier(it->kind) <= 1) continue;
    if (c == CAT_WEAPON && it->kind == IK_PICKAXE) continue;
    if (s_pztest && sl >= 0 && (c == CAT_WEAPON || c == CAT_ARMOR) && ({ Item *eq = c == CAT_WEAPON ? &G.hero.weapon : &G.hero.armor; c == CAT_WEAPON ? weapon_score(it) <= weapon_score(eq) : armor_score(it) <= armor_score(eq); })) continue;   // 0.92 (seed 84: the spare cloth armour upgraded, scale armour on): a spare that's worse than what's on isn't a target
    tg[nt++] = sl; }
  if (!nt) return -1;
  if (!s_pztest) tg[nt++] = -1000;                     // hold (0.92 seed 84 d4: held at 2 HP among the crabs: a known Upgrade goes in now)
  st_sla_points++;
  long best = -1; int bi = 0; uint32_t base = (uint32_t)(cur_game * 7777 + s_bot_steps * 31 + s_salt * 15485863u);
  for (int i = 0; i < nt; i++) {
    long tot = 0; for (int r = 0; r < 8; r++) tot += strat_playout(scroll, tg[i], base + r * 104729);
    if (getenv("SLADBG")) { char nm[64] = "hold"; if (tg[i] != -1000) item_name(game_slot(tg[i]), nm, sizeof nm); printf("   upgrade? %-34s -> %ld\n", nm, tot / 8); }
    if (tot > best) { best = tot; bi = i; }
  }
  if (tg[bi] == -1000) { DID("strategy: hold the scroll of upgrade"); return 0; }
  { char nm[64]; item_name(game_slot(tg[bi]), nm, sizeof nm); DID("strategy: upgrade %s", nm); }
  s_force_pick = tg[bi]; do_item(scroll, ACN_READ); return 1;
}
static int s_hs;
static void head_start(int hs) {
  if (hs <= 0) return;                                 // bot2: no head start: nothing (it used to add empty stacks)
  Hero *h = &G.hero;
  h->HT += 30 * hs; h->HP = h->HT; h->STR += hs; h->attack_skill += 3 * hs; h->defense_skill += 3 * hs; s_hs = hs;
  if (hs >= 3) { Item r = item_make(IK_RNG_MENDING, 1); r.lvl = hs; r.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; r.dur = item_max_dur(&r); h->ring[0].kind ? bag_add(r) : (void)(h->ring[0] = r); set_known(IK_RNG_MENDING); }
  for (int i = 0; i < hs; i++) { Item u = item_make(IK_SCR_UPGRADE, 1); bag_add(u); }
  Item p = item_make(IK_POT_HEALING, hs); bag_add(p);
  if (hs >= 2) { Item r = item_make(IK_RNG_SATIETY, 1); r.lvl = hs; r.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; r.dur = item_max_dur(&r); h->ring[1] = r; set_known(IK_RNG_SATIETY); }
  bag_add(item_make(IK_RATION, hs));
}
// ==== 0.48 (owner): ZONES=1: games start in equal numbers at depth 1, 6, 11, 16, 21 (just past each boss) with a
// typical bag for that point, so each zone gets its fair share of testing. A game ends when its zone's boss is beaten
// (the floor after it is reached). Typical, by the boss just beaten (z = 1..4): level 6-7 / 10-12 / 14-16 / 18-20, STR
// 10 + 2z (a potion missed now and then), 3z upgrades (give or take one) split between weapon and armour (now and then
// one in a wand), the best weapon and armour that STR allows (sometimes a tier lower), a few potions of healing (known),
// random potions, scrolls and seeds with more of them identified the deeper it is, food, darts, the dew vial, a subclass
// after Tengu.
static int s_zone_start, st_zone_n[6], st_zone_clear[6], st_zone_depth[6];
static Item kit_item(int kind, int lvl) { Item it = item_make(kind, 1); it.lvl = lvl; it.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; it.dur = item_max_dur(&it); return it; }
static int kit_best(int first, int last, int str, int lvl) {   // the highest tier that STR allows at this level (now and then one lower)
  int best = first; for (int k = first; k <= last; k++) { Item t = kit_item(k, lvl); if (item_str(&t) <= str && item_tier(k) >= item_tier(best)) best = k; }
  if (rand() % 4 == 0) { int lo = -1; for (int k = first; k <= last; k++) { Item t = kit_item(k, lvl); if (item_str(&t) <= str && item_tier(k) == item_tier(best) - 1) lo = k; } if (lo >= 0) best = lo; }
  if (first == IK_DAGGER) { int c[16], n = 0; for (int k = first; k <= last; k++) if (item_tier(k) == item_tier(best)) { Item t = kit_item(k, lvl); if (item_str(&t) <= str) c[n++] = k; } if (n) best = c[rand() % n]; }
  return best;
}
static void zone_kit(int d) {
  int z = (d - 1) / 5; Hero *h = &G.hero;
  for (int x = 2; x <= d; x++) { switch_level(x, ARRIVE_ENTRANCE); game_level_commit();
    if (boss_depth(x)) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) mob_kill(i); }   // (the bosses behind you are dead: a retreat upstairs must not meet one)
  int lv = z == 1 ? 6 + rand() % 2 : z == 2 ? 10 + rand() % 3 : z == 3 ? 14 + rand() % 3 : 18 + rand() % 3;
  while (h->lvl < lv) earn_exp(hero_max_exp() - h->exp);
  h->HP = h->HT; h->STR = 10 + 2 * z - (rand() % 4 == 0); h->hunger = 0;
  if (z >= 2) h->subclass = 1 + 2 * (h->hclass & 3) + rand() % 2;
  int up = 3 * z + rand() % 3 - 1, wu = 0, au = 0, nu = 0;
  bool wand_up = rand() % 4 == 0;
  for (int i = 0; i < up; i++) { int r = rand() % 10; if (wand_up && r == 0) nu++; else if (r < (getenv("KITW") ? atoi(getenv("KITW")) : 6)) wu++; else au++; }   // (0.93: KITW=n of 10 to the weapon: how much do armour-heavy heroes gain deep?)
  if (h->weapon.kind && item_cat(h->weapon.kind) == CAT_WAND) { bag_add(h->weapon); h->weapon = (Item){ 0 }; }
  h->weapon = kit_item(kit_best(IK_DAGGER, IK_GLAIVE, h->STR, wu), wu);
  h->armor = kit_item(kit_best(IK_CLOTH_ARMOR, IK_PLATE_ARMOR, h->STR, au), au);
  set_known(IK_POT_HEALING); bag_add(item_make(IK_POT_HEALING, 1 + rand() % 3));
  bag_add(item_make(IK_RATION, 1 + rand() % 2));
  bag_add(item_make(IK_DART, 5 + rand() % 10));
  { Item v = item_make(IK_DEW_VIAL, 1); bag_add(v); G.dew = rand() % 8; }
  if (nu || rand() % 2) { Item w = gen_item(GC_WAND); w.lvl += nu; w.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(w.kind); bag_add(w); }
  if (rand() % 3 == 0) { Item r = gen_item(GC_RING); if (r.lvl > 0 && !(r.flags & IF_CURSED)) { r.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; set_known(r.kind); bag_add(r); } }
  for (int i = 0; i < 2 + z; i++) { Item p = gen_item(GC_POTION); if (p.kind != IK_POT_STRENGTH && p.kind != IK_POT_MIGHT && p.kind != IK_POT_EXPERIENCE) bag_add(p); }
  for (int i = 0; i < 2 + z; i++) { Item q = gen_item(GC_SCROLL); if (q.kind != IK_SCR_UPGRADE && q.kind != IK_SCR_ENCHANT) bag_add(q); }
  for (int i = 0; i < 2; i++) bag_add(gen_item(GC_SEED));
  int nk = 2 + 2 * z;                                  // kinds identified besides healing
  for (int i = 0; i < nk; i++) { set_known(IK_FIRST_POTION + rand() % N_POTIONS); set_known(IK_FIRST_SCROLL + rand() % N_SCROLLS); }
  h->gold += 100 * z * (1 + rand() % 3);
  if (z >= 4) for (int i = 0; i < 2 + rand() % 2; i++) bag_add(item_make(IK_TORCH, 1));   // (the halls are dark: the last shop sells torches)
}
static int st_quest_left[4][3];
static void quest_leave_check(void) {                  // leaving a quest floor with the quest under way: can it still be done?
  Quests *q = &G.q; int d = G.depth;
  if (q->wand_depth == d && q->wand == QS_GIVEN) {
    static const uint8_t W[3] = { IK_SEED_ROTBERRY, IK_CORPSE_DUST, IK_PHANTOM_FISH };
    int k = W[q->wand_alt % 3]; bool there = find_kind(k) >= 0;
    for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == k) there = true;
    if (k == IK_SEED_ROTBERRY) for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 7) there = true;
    st_quest_left[1][q->wand_alt % 3]++;
    if (getenv("Q")) { printf("  QLEFT game %d wandmaker alt %d floor steps %d:", cur_game, q->wand_alt, s_floor_steps); bfs(G.hero.pos, false);
      for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].item.kind == k) printf(" heap at %d type %d visited %d reach %d black %d", G.heaps[h].pos, G.heaps[h].type, G.heaps[h].pos < MAP_LEN ? BIT_GET(G.visited, G.heaps[h].pos) : -1, G.heaps[h].pos < MAP_LEN ? b_dist[G.heaps[h].pos] : -1, G.heaps[h].pos < MAP_LEN ? s_black[G.heaps[h].pos] : -1);
      for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 7) printf(" rotberry at %d visited %d", G.plants[i].pos, BIT_GET(G.visited, G.plants[i].pos)); printf("\n"); }
    if (!there) viol("QUEST_UNSOLVABLE", "wandmaker quest %d: no %d left on the floor or in the pack", q->wand_alt, k);
    else if (find_kind(k) < 0) {                       // where it lies: can anyone get there (secret doors found, locked ones opened)?
      static int16_t dd[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) dd[c] = -1;
      int qh = 0, qt = 0; dd[G.hero.pos] = 0; b_q[qt++] = G.hero.pos;
      while (qh < qt) { int c = b_q[qh++]; for (int j = 0; j < 8; j++) { int n = c + N8[j]; if (!nb_ok(c, n) || dd[n] >= 0) continue; int t = G.map[n], f = TFLAGS(t);
          if (!((f & F_PASSABLE) || ((f & F_AVOID) && !(f & F_PIT)) || t == T_SECRET_DOOR || t == T_LOCKED_DOOR || t == T_BARRICADE || t == T_BOOKSHELF)) continue; dd[n] = dd[c] + 1; b_q[qt++] = n; } }
      bool reach = false;
      for (int hh = 0; hh < G.n_heaps; hh++) if (G.heaps[hh].item.kind == k && (G.heaps[hh].pos >= MAP_LEN || dd[G.heaps[hh].pos] >= 0)) reach = true;
      for (int i = 0; i < G.n_plants; i++) if (G.plants[i].type == 7 && k == IK_SEED_ROTBERRY && dd[G.plants[i].pos] >= 0) reach = true;
      if (!reach) viol("QUEST_UNSOLVABLE", "wandmaker quest %d: the %d lies where nobody can walk", q->wand_alt, k);
    }
  }
  if (q->smith_depth == d && q->smith == QS_GIVEN) {
    st_quest_left[2][q->smith_alt ? 1 : 0]++;
    if (!q->smith_alt) { int veins = 0; for (int c = 0; c < MAP_LEN; c++) veins += G.map[c] == T_WALL_DECO; int g = find_kind(IK_DARK_GOLD); int have = g >= 0 ? game_slot(g)->qty : 0;
      if (veins + have < 15 && getenv("Q")) printf("  smith gold: %d veins + %d ore on depth %d\n", veins, have, d); }
    if (find_kind(IK_PICKAXE) < 0 && G.hero.weapon.kind != IK_PICKAXE) viol("QUEST_UNSOLVABLE", "blacksmith quest: the pickaxe is gone");
  }
  if (q->ghost_depth == d && q->ghost >= QS_GIVEN && q->ghost < QS_REWARDED) st_quest_left[0][q->ghost_alt % 3]++;
  if (q->imp_depth == d && q->imp == QS_GIVEN) st_quest_left[3][q->imp_alt ? 1 : 0]++;
}
// 0.63 item ledger (owner: "all effort on managing gear, items and the backpack"): what was left lying on each floor
// when leaving it downwards the first time (seen / never seen), and what was still carried at death
static const char *LG_NAME[] = { "gold", "food", "dew", "weapon", "armour", "missile", "potion", "scroll", "wand", "ring", "seed", "key", "quest", "misc", "SoU", "PoS", "healing" };
static long lg_left[17][2], lg_dead[17][2], lg_floors, lg_deaths; static int lg_maxleft;
static int lg_cat(int k) { return k == IK_SCR_UPGRADE ? 14 : k == IK_POT_STRENGTH ? 15 : k == IK_POT_HEALING ? 16 : k == IK_MYSTERY_MEAT ? CAT_FOOD : item_cat(k); }
static void ledger_leave(void) {
  if (s_rollout || s_is_child || s_pend_depth <= G.depth || G.depth <= lg_maxleft) return;
  lg_maxleft = G.depth; lg_floors++;
  for (int i = 0; i < G.n_heaps; i++) { Heap *h = &G.heaps[i]; if (h->pos >= MAP_LEN || h->type == HEAP_FOR_SALE || !h->item.kind) continue;
    lg_left[lg_cat(h->item.kind)][BIT_GET(G.visited, h->pos) ? 0 : 1]++; }
}
static void ledger_death(void) {
  if (s_rollout || s_is_child) return; lg_deaths++;
  if (getenv("LGDUMP")) { printf("  BAG at death (STR %d):", hero_str()); for (int s = 0; s < BAG_SIZE; s++) { Item *it = &G.hero.bag[s]; if (it->kind) printf(" %d%s%+d/f%d", it->kind, item_known(it->kind) ? "k" : "", it->lvl, it->flags); } printf(" | worn a%d w%d\n", G.hero.armor.kind, G.hero.weapon.kind); }
  for (int s = 0; s < PACK0 + 32; s = s == BAG_SIZE - 1 ? PACK0 : s + 1) { Item *it = game_slot(s); if (!it->kind) continue; int k = it->kind, c = lg_cat(k);
    lg_dead[c][(c == CAT_POTION || c == CAT_SCROLL || c == CAT_WAND || c == CAT_RING || c >= 14) && item_known(k) ? 0 : 1] += (c == CAT_MISSILE || c == CAT_GOLD) ? 1 : it->qty; }
}
static void ledger_print(void) {
  printf("ledger: %ld floors left behind; lying there (seen/unseen):", lg_floors); for (int c = 0; c < 17; c++) if (lg_left[c][0] || lg_left[c][1]) printf(" %s %ld/%ld", LG_NAME[c], lg_left[c][0], lg_left[c][1]); printf("\n");
  printf("ledger: %ld deaths; carried (known/unknown):", lg_deaths); for (int c = 0; c < 17; c++) if (lg_dead[c][0] || lg_dead[c][1]) printf(" %s %ld/%ld", LG_NAME[c], lg_dead[c][0], lg_dead[c][1]); printf("\n");
}
static void floor_report(void) {
  quest_leave_check(); ledger_leave();
  if (getenv("FOODLOG") && !s_rollout && !s_is_child && !s_cf_child) { int fb = 0; for (int i = 0; i < BAG_SIZE; i++) if (is_food(G.hero.bag[i].kind)) fb += G.hero.bag[i].qty;
    { int pass = 0, vis = 0, st = 0; for (int c = 0; c < MAP_LEN; c++) { if (PASSABLE(G.map[c]) || G.map[c] == T_DOOR) pass++; if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) vis++; if (BIT_GET(s_stepped, c)) st++; }
      if (getenv("PATHMAP")) { for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, n = s_stepcnt[c]; putchar(c == G.entrance ? '<' : c == G.exit ? '>' : n ? (n > 9 ? '+' : '0' + n) : PASSABLE(G.map[c]) || G.map[c] == T_DOOR ? (BIT_GET(G.visited, c) ? '.' : '?') : ' '); } printf("\n"); } }
      memset(s_stepcnt, 0, sizeof s_stepcnt);
      printf("  WALK d%d moves %d stepped %d passable %d seen %d turns %d | moving %d, standing: other %d rest %d fight %d search %d\n", G.depth, s_floor_moves, st, pass, vis, (G.now - s_floor_t0) / TICK, s_wk[0], s_wk[1], s_wk[2], s_wk[3], s_wk[4]); memset(s_wk, 0, sizeof s_wk); }
    printf("  FOOD leave d%d t%d hunger %d bag %d lvl %d hp %d/%d\n", G.depth, G.now / TICK, G.hero.hunger, fb, G.hero.lvl, G.hero.HP, G.hero.HT);
    for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if (hp->pos < MAP_LEN && item_cat(hp->item.kind) == CAT_POTION) printf("  POTION left d%d kind %d (flame %d) at %d,%d seen %d heaptype %d room %d\n", G.depth, hp->item.kind, hp->item.kind == IK_POT_FLAME, cx(hp->pos), cy(hp->pos), BIT_GET(G.visited, hp->pos), hp->type, room_at(hp->pos) >= 0 ? G.rooms[room_at(hp->pos)].type : -1); }
    for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == RT_STORAGE) { Room *R = &G.rooms[r]; printf("  STORAGE room %d,%d-%d,%d border:", R->left, R->top, R->right, R->bottom); for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) { if (x != R->left && x != R->right && y != R->top && y != R->bottom) continue; int c = x + y * MAP_W; if (G.map[c] != T_WALL) printf(" %d,%d=t%d(v%d)", x, y, G.map[c], BIT_GET(G.visited, c)); } printf("\n"); }
    for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if (hp->pos >= MAP_LEN || !(is_food(hp->item.kind) || hp->item.kind == IK_MYSTERY_MEAT)) continue; bfs(G.hero.pos, false); printf("  FOOD left d%d kind %d at %d,%d seen %d heaptype %d room %d dist %d wanted %d black %d terrain %d\n", G.depth, hp->item.kind, cx(hp->pos), cy(hp->pos), BIT_GET(G.visited, hp->pos), hp->type, room_at(hp->pos) >= 0 ? G.rooms[room_at(hp->pos)].type : -1, b_dist[hp->pos], heap_wanted(i), s_black[hp->pos] > s_bot_steps, G.map[hp->pos]); } }
  static uint8_t reach_kind[MAP_LEN];   // 0.54: how an unseen prize could be reached: 1 open ways only, 2 needs a secret door, 3 needs a locked door / barricade, 0 not at all
  if (getenv("GEARLOG") && !s_rollout && !s_is_child) { memset(reach_kind, 0, sizeof reach_kind); for (int pass = 1; pass <= 3; pass++) { int qh = 0, qt = 0; static int16_t q[MAP_LEN]; static uint8_t seenq[MAP_LEN]; memset(seenq, 0, sizeof seenq); seenq[G.hero.pos] = 1; q[qt++] = G.hero.pos;
      while (qh < qt) { int c = q[qh++]; if (!reach_kind[c]) reach_kind[c] = pass; for (int j = 0; j < 8; j++) { int n = c + N8[j]; if (!nb_ok(c, n) || seenq[n]) continue; int t = G.map[n], f = TFLAGS(t);
          bool ok = (f & F_PASSABLE) || ((f & F_AVOID) && !(f & F_PIT)); if (pass >= 2 && t == T_SECRET_DOOR) ok = true; if (pass >= 3 && (t == T_LOCKED_DOOR || t == T_BARRICADE || t == T_BOOKSHELF)) ok = true; if (!ok) continue; seenq[n] = 1; q[qt++] = n; } } } }
  if (getenv("GEARLOG") && !s_rollout && !s_is_child) for (int k = 0; k < G.n_heaps; k++) { Heap *hp = &G.heaps[k]; int kk = hp->item.kind; if (kk != IK_SCR_UPGRADE && kk != IK_POT_STRENGTH && item_cat(kk) != CAT_ARMOR) continue; if (hp->pos >= MAP_LEN) continue;
      int rt = -1; for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; if (cx(hp->pos) >= R->left && cx(hp->pos) <= R->right && cy(hp->pos) >= R->top && cy(hp->pos) <= R->bottom) { rt = R->type; break; } }
      printf("  LEFT d%d kind %d%+d at %d,%d heap type %d visited %d room type %d terrain %d reach %d [bag %d pir %d preach %d black %d dropped %d defer %d mob %d]\n", G.depth, kk, hp->item.lvl, cx(hp->pos), cy(hp->pos), hp->type, BIT_GET(G.visited, hp->pos) ? 1 : 0, rt, G.map[hp->pos], reach_kind[hp->pos], bag_used(), s_piranhas, PREACH(hp->pos) ? 1 : 0, s_black[hp->pos] > s_bot_steps, BIT_GET(s_dropped, hp->pos) ? 1 : 0, deferred_heap(k), mob_at(hp->pos));
      if (kk != IK_SCR_UPGRADE && kk != IK_POT_STRENGTH) continue; if (!BIT_GET(G.visited, hp->pos)) continue; printf("    wanted %d; going to d%d; last decisions:\n", heap_wanted(k), s_pend_depth); for (int i = 56; i < 64; i++) { int j = (s_ring_n + i) & 63; if (s_ring[j][0]) printf("      %s\n", s_ring[j]); } }
  if (!getenv("FR")) return;
  int fr = 0, hp = 0, vis = 0; for (int c = 0; c < MAP_LEN; c++) { fr += frontier(c); vis += BIT_GET(G.visited, c); }
  for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].pos < MAP_LEN && BIT_GET(G.visited, G.heaps[h].pos) && G.heaps[h].type != HEAP_FOR_SALE) hp++;
  int fl = 0, fls = 0; for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].pos < MAP_LEN && (is_food(G.heaps[k].item.kind) || G.heaps[k].item.kind == IK_MYSTERY_MEAT)) { fl++; if (BIT_GET(G.visited, G.heaps[k].pos)) fls++; }
  printf("  leave d%d: %d decisions, %d turns, visited %d, frontier %d, heaps left %d, hunger %d, food %d, food left %d (seen %d)\n", G.depth, s_floor_steps, (G.now - s_floor_turn0) / TICK, vis, fr, hp, G.hero.hunger, find_kind(IK_RATION), fl, fls);
  for (int i = 61; i < 64; i++) { int j = (s_ring_n + i) & 63; if (s_ring[j][0]) printf("      %s\n", s_ring[j]); }
}
static void toe_cal_log(void);
static void on_new_floor(bool first) {
  if (getenv("ROOMDBG") && !s_rollout && !s_is_child) for (int r = 0; r < G.n_rooms; r++) printf("ROOM d%d %d type %d %d,%d-%d,%d\n", G.depth, r, G.rooms[r].type, G.rooms[r].left, G.rooms[r].top, G.rooms[r].right, G.rooms[r].bottom);
  if (getenv("ROOMDBG") && !s_rollout && !s_is_child && G.q.ghost_depth == G.depth) { char a[64], b[64]; item_name(&G.q.ghost_weapon, a, sizeof a); item_name(&G.q.ghost_armor, b, sizeof b); printf("GHOST d%d state %d alt %d weapon %s armor %s\n", G.depth, G.q.ghost, G.q.ghost_alt, a, b); }
  if (first && getenv("TOECAL")) toe_cal_log();
  s_avoid_n = 0;
  pwater_map(); g_from = -1;                            // (0.47: the heading starts fresh on each floor, as on the watch)
  s_trail_n = 0; s_ambush = 0;
  s_stash_drink = -1;
  if (s_stash >= 0) { if (!s_rollout) st_stash_lost++; if (getenv("STASHDBG")) printf("  STASH LOST game %d depth %d\n", cur_game, G.depth); s_stash = -1; }
  bool arr_at = false; { const char *ad = getenv("ARRDEPTH"); if (!ad) arr_at = G.depth == 5; else for (const char *q = ad; *q; ) { if (atoi(q) == G.depth) arr_at = true; while (*q && *q != ',') q++; if (*q) q++; } }   // (0.47n: "6,10": several depths, each its own folder)
  if (first) { memset(s_arr_qty, 0, sizeof s_arr_qty); s_barr_tries = 0; for (int i = 0; i < BAG_SIZE; i++) if (G.hero.bag[i].kind && G.hero.bag[i].kind < IK_COUNT) s_arr_qty[G.hero.bag[i].kind] = G.hero.bag[i].qty > 255 ? 255 : G.hero.bag[i].qty; }
  if (getenv("GEARLOG") && first && !s_rollout && !s_is_child) { Hero *h = &G.hero; printf("  gear d%d lvl %d HP %d STR %d arm %d%+d%s wpn %d%+d%s SoU %d/%ld bag %d PoS %d | bag:", G.depth, h->lvl, h->HT, h->STR, h->armor.kind, h->armor.lvl, (h->armor.flags & IF_LEVEL_KNOWN) ? "" : "?", h->weapon.kind, h->weapon.lvl, (h->weapon.flags & IF_LEVEL_KNOWN) ? "" : "?", G.sou_count, st_sou_read, find_kind(IK_SCR_UPGRADE) >= 0 ? game_slot(find_kind(IK_SCR_UPGRADE))->qty : 0, G.pos_count);
    for (int i = 0; i < BAG_SIZE; i++) { Item *it = &h->bag[i]; int c = item_cat(it->kind); if (c == CAT_ARMOR || c == CAT_WEAPON || c == CAT_WAND || c == CAT_RING) printf(" %d%+d%s%s", it->kind, it->lvl, (it->flags & IF_LEVEL_KNOWN) ? "" : "?", (it->flags & IF_CURSED) ? "c" : ""); else if (c == CAT_POTION || c == CAT_SCROLL) printf(" %s%d", item_known(it->kind) ? "" : "u", it->kind); }
    printf("\n"); }
  if (getenv("CAMPDIR") && first && !s_rollout && !s_is_child) {   // 0.50 campaign: a checkpoint at each new floor (the save, the dice, and C's rand from a fixed seed on)
    srand(G.depth * 7919 + cur_game * 104729); game_save(); char fn[300]; snprintf(fn, sizeof fn, "%s/g%04d_d%02d.bin", getenv("CAMPDIR"), cur_game, G.depth);
    FILE *f = fopen(fn, "wb"); if (f) { fwrite(g_keys, sizeof g_keys, 1, f); fwrite(&g_rng, sizeof g_rng, 1, f); fwrite(&s_bot_steps, sizeof s_bot_steps, 1, f); fclose(f); }
    { static Game before; memcpy(&before, &G, sizeof G); game_load(); if (s_camp_reroll) { g_rng ^= (uint32_t)s_camp_reroll * 2654435761u; printf("CAMP reroll %d at depth %d\n", s_camp_reroll, G.depth); s_camp_reroll = 0; } if (getenv("CAMPDIFF")) { const uint8_t *a = (const uint8_t *)&before, *b = (const uint8_t *)&G; for (size_t i = 0; i < sizeof G; i++) if (a[i] != b[i]) { size_t j = i; while (j < sizeof G && a[j] != b[j]) j++; printf("CAMPDIFF d%d off %zu len %zu\n", G.depth, i, j - i); i = j; } } } }   // (the original goes on from the loaded state too: whatever a save leaves out, both runs leave out)
  if (first && (G.depth == 5 || G.depth == 10 || G.depth == 15) && !s_rollout && !s_is_child && !s_cf_child && !s_upg_child) { int pot = 0, scr = 0, fd = 0, ph = 0, pu = 0; for (int i = 0; i < BAG_SIZE; i++) { int c = item_cat(G.hero.bag[i].kind); if (c == CAT_POTION) pot += G.hero.bag[i].qty; if (c == CAT_SCROLL) scr += G.hero.bag[i].qty; if (c == CAT_FOOD) fd += G.hero.bag[i].qty; if (c == CAT_POTION && G.hero.bag[i].kind == IK_POT_HEALING && item_known(IK_POT_HEALING)) ph += G.hero.bag[i].qty; if (c == CAT_POTION && !item_known(G.hero.bag[i].kind)) pu += G.hero.bag[i].qty; }   // 0.91: how the hero meets the Goo (owner: "I get to the goo well fed and strong")
    printf("ARRIVE%d g%d lvl %d hp %d/%d hunger %d str %d arm %d+%d wpn %d+%d pot %d scr %d turn %d food %d heal %d unk %d\n", G.depth, cur_game, G.hero.lvl, G.hero.HP, G.hero.HT, G.hero.hunger, hero_str(), G.hero.armor.kind, G.hero.armor.lvl, G.hero.weapon.kind, G.hero.weapon.lvl, pot, scr, G.now / TICK, fd, ph, pu); }
  if (getenv("ARRDUMP") && first && arr_at && !s_rollout && !s_is_child) {   // bot3: a real hero arriving at Goo's floor: keep it for the lab
    game_save(); char fn[300]; if (getenv("ARRDEPTH")) snprintf(fn, sizeof fn, "%s/d%d/arr_g%04d_s%u.bin", getenv("ARRDUMP"), G.depth, cur_game, s_salt); else snprintf(fn, sizeof fn, "%s/arr_g%04d.bin", getenv("ARRDUMP"), cur_game); FILE *f = fopen(fn, "wb"); if (f) { fwrite(g_keys, sizeof g_keys, 1, f); fclose(f); } printf("ARRDUMP %s %s\n", fn, f ? "ok" : "FAILED"); }
  s_idle = 0; s_fallback_search = 0; memset(s_black, 0, sizeof s_black); memset(s_searched, 0, sizeof s_searched); memset(s_dropped, 0, sizeof s_dropped); memset(s_used_feature, 0, sizeof s_used_feature); memset(s_verified, 0, sizeof s_verified);
  s_prev_arrive = s_arrive_step; s_arrive_step = s_bot_steps; memset(s_room_skip, 0, sizeof s_room_skip); memset(s_trap_throws_room, 0, sizeof s_trap_throws_room); memset(s_trap_door, 0, sizeof s_trap_door); memset(s_trap_done, 0, sizeof s_trap_done);   s_floor_steps = 0; s_floor_turn0 = G.now; s_spot = -1; s_piranhas = false; s_brave = false; s_floor_searches = 0; s_shop_sold = 0;
  if (first) floor_checks();
  if (first && getenv("GEARX") && !s_rollout) {          // 0.48 experiment (owner): a floor strewn with extra gear at the game's own odds
    int n = atoi(getenv("GEARX"));
    for (int i = 0, g = 0; i < n && g < 400; g++) { int c = rand() % MAP_LEN; if (!PASSABLE(G.map[c]) || c == G.entrance || c == G.exit || heap_at(c) >= 0 || (TFLAGS(G.map[c]) & F_AVOID)) continue;
      int r = rand() % 10; heap_put(c, gen_item(r < 4 ? GC_WEAPON : r < 8 ? GC_ARMOR : GC_RING)); i++; } }
  if (first && s_hs >= 3) { bag_add(item_make(IK_POT_HEALING, 1)); set_known(IK_POT_HEALING); if (G.depth % 3 == 0) bag_add(item_make(IK_SCR_UPGRADE, 1)); }   // head start: supplies on each new floor
  if (first && s_hs) while (G.hero.lvl < G.depth + s_hs / 2 && G.hero.lvl < 30) earn_exp(hero_max_exp() - G.hero.exp);   // head start: a level a floor
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) st_boss_seen[G.depth]++;
  bool deep = G.depth >= 22 && G.depth <= 24;           // 0.54 (owner): magic mapping and mind vision saved for the bottom floors: see it all, rush the stairs past the eyes
  if (first) { int s = find_kind(IK_SCR_MAPPING); if (s >= 0 && item_known(IK_SCR_MAPPING) && !boss_depth(G.depth) && (getenv("OLDRUSH") || deep)) do_item(s, ACN_READ); }
  if (first && deep && !getenv("OLDRUSH")) { int s = find_kind(IK_POT_MIND); if (s >= 0 && item_known(IK_POT_MIND)) do_item(s, ACN_DRINK); }
  if (first) s_rush = deep && !getenv("OLDRUSH") && G.hero.lvl >= 20 && find_kind(IK_AMULET) < 0;
}

static volatile int s_alarm_game;
static void on_alarm(int s) { printf("HANG game=%d step=%d depth=%d action=%d cell=%d pick=%d wnd=%d\n", cur_game, cur_step, G.depth, G.action, G.action_cell, s_pick, s_wnd_btn); fflush(stdout); _exit(1); }

static long tt_n, tt_sum, tt_max, tt_h[6];   // host timing of the target list build (tgt.c, TT=1)
static FILE *tt_f; void tgt_time(long ns) { if (getenv("TTV") && ns > atoi(getenv("TTV"))) printf("  SLOW %ld us\n", ns / 1000); if (getenv("TTF") && !s_is_child) { if (!tt_f) tt_f = fopen(getenv("TTF"), "wb"); int v = ns; fwrite(&v, 4, 1, tt_f); } tt_n++; tt_sum += ns; if (ns > tt_max) tt_max = ns; for (int i = 0; i < 6; i++) if (ns > (25000L << i)) tt_h[i]++; }

// ==== bot3: the "real arrivals" lab: each saved hero (ARRLAB=dir) fights floor 5 R times with different luck
static char *snap_state(int g, int step);
static long s_lab_dh[32]; static int s_lab_b = 1 << 30;
static int lab_trial(const char *fn, uint32_t seed) {
  int fds[2]; if (pipe(fds)) return -1;
  pid_t pid = fork();
  if (pid == 0) { signal(SIGALRM, SIG_DFL); alarm(900);
    close(fds[0]); FILE *f = fopen(fn, "rb"); if (!f || fread(g_keys, sizeof g_keys, 1, f) != 1) _exit(1); fclose(f);
    if (!game_load()) _exit(1);
    if (getenv("LABSWAP")) { int k = atoi(getenv("LABSWAP")); Hero *h = &G.hero;   // what-if: the weapon's upgrades went into this bag weapon instead
      for (int i = 0; i < BAG_SIZE; i++) if (h->bag[i].kind == k) { Item w = h->weapon, b = h->bag[i]; b.lvl += w.lvl; w.lvl = 0; b.flags |= IF_LEVEL_KNOWN; h->weapon = b; h->bag[i] = w; break; } }
    if (getenv("GEARDBG")) { Hero *h = &G.hero; Item a = h->armor; printf("hero STR %d lvl %d weapon kind %d +%d str %d\n", hero_str(), h->lvl, h->weapon.kind, h->weapon.lvl, item_str(&h->weapon));
      for (int i = -1; i < BAG_SIZE; i++) { Item w = i < 0 ? h->weapon : h->bag[i]; if (item_cat(w.kind) != CAT_WEAPON) continue; for (int up = 0; up <= 3; up++) { Item x = w; x.lvl = up; printf("  weapon %2d +%d (STR %d): gear_cost %.1f\n", x.kind, up, item_str(&x), gear_cost(&x, &a)); } } _exit(0); }
    g_rng ^= seed * 2654435761u; srand(seed); V = 0; s_pend_depth = 0;
    s_goal = 99; s_curse_trip = 0; s_last_depth = G.depth; s_goo_calm = -1; s_goo_spot = -1; s_goo_hold = 0; s_stash = -1; s_stash_drink = -1; s_last_up = -1000000; s_upg_hold_until = 0; s_test_walk = -1; s_resting = false; s_step_back = -1;
    memset(s_ign_until, 0, sizeof s_ign_until); on_new_floor(false);
    int res = 0, steps = 0, goal = getenv("LABGOAL") ? atoi(getenv("LABGOAL")) : 5, cap = getenv("LABSTEPS") ? atoi(getenv("LABSTEPS")) : 4000;   // (0.47n: any zone's boss: LABGOAL 10, ...)
    while (steps++ < cap) {
      if (G.hero.HP <= 0) { res = 0; break; }
      if (s_pend_depth) { if (s_pend_depth > goal) { res = 1; break; } s_arrival = true; game_level_commit(); s_arrival = false; if (G.depth != s_last_depth) { s_last_depth = G.depth; on_new_floor(false); } }
      if (G.depth > goal) { res = 1; break; }
      cur_step = steps; s_bot_steps = steps;
      char *sb = getenv("LABSNAP") ? snap_state((int)(seed % 1000), steps) : NULL; s_did[0] = 0;
      if (!la_step()) { if (getenv("LABDBG")) fprintf(stderr, "lab: no step at %d: depth %d hp %d\n", steps, G.depth, G.hero.HP); break; }
      if (sb) { char fn[300]; snprintf(fn, sizeof fn, "%s/t%03u_s%04d.json", getenv("LABSNAP"), seed % 1000, steps); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%s,\"hp_after\":%d,\"did\":\"", sb, G.hero.HP); for (const char *q = s_did; *q; q++) if (*q != '"') fputc(*q, f); fprintf(f, "\"}\n"); fclose(f); } free(sb); }
    }
    if (steps >= cap) res = 2;
    if (write(fds[1], &res, sizeof res) != sizeof res) {}
    if (getenv("LABDEPTH")) { int d = G.depth; if (write(fds[1], &d, sizeof d) != sizeof d) {} }
    _exit(0);
  }
  close(fds[1]); int r = -1; if (read(fds[0], &r, sizeof r) != sizeof r) r = -1;
  { int dd = -1; if (getenv("LABDEPTH") && read(fds[0], &dd, sizeof dd) == sizeof dd && dd >= 0 && dd < 32) s_lab_dh[dd]++; }
  close(fds[0]); int st; waitpid(pid, &st, 0);
  return r;
}
static int run_lab(const char *dir) {
  char cmd[400]; snprintf(cmd, sizeof cmd, "ls %s/arr_g*.bin", dir); FILE *ls = popen(cmd, "r"); char fn[300]; int R = getenv("LABR") ? atoi(getenv("LABR")) : 5;
  int a = getenv("LABA") ? atoi(getenv("LABA")) : getenv("START") ? atoi(getenv("START")) : 0, b = getenv("LABB") ? atoi(getenv("LABB")) : s_lab_b, idx = 0;   // (0.93: START..N: one runner chunk each)
  long wins = 0, tot = 0, stuck = 0;
  while (ls && fgets(fn, sizeof fn, ls)) { fn[strcspn(fn, "\n")] = 0; if (idx < a || idx >= b) { idx++; continue; } idx++;
    int w = 0; for (int r = 0; r < R; r++) { int res = lab_trial(fn, 1000 + r * 7919); if (res == 1) w++; if (res == 2) stuck++; tot++; }
    wins += w;
    if (getenv("LABV")) { FILE *f = fopen(fn, "rb"); if (f && fread(g_keys, sizeof g_keys, 1, f) == 1 && game_load()) { Hero *h = &G.hero; int pot = 0, heal = 0; for (int i = 0; i < BAG_SIZE; i++) { if (item_cat(h->bag[i].kind) == CAT_POTION) pot += h->bag[i].qty; if (h->bag[i].kind == IK_POT_HEALING) heal += h->bag[i].qty; }
        printf("  arr %s w %d/%d cls %d lvl %d HT %d STR %d arm %d+%d wpn %d+%d pot %d heal %d hunger %d hp %d food %d\n", fn, w, R, h->hclass & 3, h->lvl, h->HT, h->STR, h->armor.kind, h->armor.lvl, h->weapon.kind, h->weapon.lvl, pot, heal, h->hunger, h->HP, ({ int fd = 0; for (int i = 0; i < BAG_SIZE; i++) if (is_food(h->bag[i].kind)) fd += h->bag[i].qty; fd; })); } if (f) fclose(f); } }
  if (ls) pclose(ls);
  printf("lab: %ld trials, %ld beat the boss (%.1f%%), %ld stuck\n", tot, wins, 100.0 * wins / (tot ? tot : 1), stuck);
  if (getenv("LABDEPTH")) { printf("lab depths:"); for (int d = 0; d < 32; d++) if (s_lab_dh[d]) printf(" d%d:%ld", d, s_lab_dh[d]); printf("\n"); }
  return 0;
}
// ==== bot3 STAGE 1 (owner's dependency tree): EXPLORATION ONLY. Real floors (every depth but the bosses'), no
// monsters (piranhas and NPCs stay), no hunger, no damage: how many turns to see every reachable tile, pick up every
// item a person would, and stand on the exit? Secret doors and locked doors count as walls for "reachable";
// puzzle-room prizes, graves, shops and locked chests don't count as items to take.
static bool s_exp_secret, s_exp_locked;  static uint8_t s_vis0[MAP_LEN / 8];
static void exp_clear_mobs(void) { int k = 0; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && ((MOB_DEFS[m->type].flags & MD_NPC) || m->type == MT_PIRANHA)) G.mobs[k++] = *m; } G.n_mobs = k; }
static void exp_target(uint8_t *tg, int *ncell) {         // reachable cells from the entrance (no secret / locked doors)
  memset(tg, 0, MAP_LEN); int q[MAP_LEN], qh = 0, qt = 0; q[qt++] = G.entrance; tg[G.entrance] = 1; *ncell = 0;
  while (qh < qt) { int c = q[qh++]; (*ncell)++;
    for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || tg[n]) continue; int t = G.map[n];
      if (!PASSABLE(t) && !(TFLAGS(t) & F_AVOID) && t != T_DOOR && !(t == T_LOCKED_DOOR && s_exp_locked) && !(s_exp_secret && t == T_SECRET_DOOR)) continue; if (t == T_SECRET_DOOR && !s_exp_secret) continue; tg[n] = 1; q[qt++] = n; } }
  if (!s_exp_locked && !s_exp_secret) {                   // an iron key to be had: the locked rooms count too
    bool key = find_kind(IK_IRON_KEY) >= 0; for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].item.kind == IK_IRON_KEY && (unsigned)G.heaps[i].pos < MAP_LEN && tg[G.heaps[i].pos]) key = true;
    if (key) { s_exp_locked = true; exp_target(tg, ncell); s_exp_locked = false; return; } }
  if (!tg[G.exit] && !s_exp_secret) { tg[G.exit] = 1; (*ncell)++; }   // the way on is behind a secret door: it must be found (the exit counts as a cell to see)
}
static int exp_items(const uint8_t *tg) {                // the pick-ups a person would make
  int n = 0; for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !tg[hp->pos] || (hp->type != HEAP_PLAIN && hp->type != HEAP_CHEST)) continue;
    int r = room_at(hp->pos), ty = r >= 0 ? G.rooms[r].type : -1; if (ty == RT_POOL || ty == RT_TRAPS || ty == RT_SHOP || ty == RT_VAULT || ty == RT_STORAGE) continue; n++; }
  return n;
}
static int exp_oracle(const uint8_t *tg0) {
  static uint8_t tg[MAP_LEN]; int nc; s_exp_secret = true; exp_target(tg, &nc); s_exp_secret = false;   // (it knows where the secret doors are)
  for (int c = 0; c < MAP_LEN; c++) if (!tg0[c] && tg[c] && room_at(c) >= 0 && !tg0[c]) {}                 // omniscient estimate: someone who already knows the map visits every room
  // (a cell inside or its doorway), every item, then the exit, always heading for the nearest unvisited one
  static int16_t dist[MAP_LEN], par[MAP_LEN]; static int16_t grp[MAP_LEN]; int ng = 0; static uint8_t cov[MAX_ROOMS + 64];
  for (int c = 0; c < MAP_LEN; c++) grp[c] = -1;
  int gr[MAX_ROOMS]; for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r]; gr[r] = -1;
    if (R->type == RT_NULL || R->type == RT_TUNNEL || R->type == RT_PASSAGE) continue; bool any = false;
    for (int y = R->top; y <= R->bottom; y++) for (int x = R->left; x <= R->right; x++) { int c = x + y * MAP_W; if (!tg[c]) continue; bool in = x > R->left && x < R->right && y > R->top && y < R->bottom; int t = G.map[c];
      if (in || t == T_DOOR || t == T_OPEN_DOOR) any = true; }
    if (any) gr[r] = ng++; }
  int item_g0 = ng, nitem = 0, icell[64];
  for (int i = 0; i < G.n_heaps && nitem < 64; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !tg[hp->pos] || (hp->type != HEAP_PLAIN && hp->type != HEAP_CHEST)) continue;
    int r = room_at(hp->pos), ty = r >= 0 ? G.rooms[r].type : -1; if (ty == RT_POOL || ty == RT_TRAPS || ty == RT_SHOP || ty == RT_VAULT || ty == RT_STORAGE) continue; icell[nitem++] = hp->pos; }
  ng += nitem; memset(cov, 0, sizeof cov);
  #define COVER(c) do { int _c = (c); for (int r = 0; r < G.n_rooms; r++) if (gr[r] >= 0) { Room *R = &G.rooms[r]; int x = cx(_c), y = cy(_c); if (x >= R->left && x <= R->right && y >= R->top && y <= R->bottom && (G.map[_c] == T_DOOR || G.map[_c] == T_OPEN_DOOR || (x > R->left && x < R->right && y > R->top && y < R->bottom))) cov[gr[r]] = 1; } for (int k = 0; k < nitem; k++) if (icell[k] == _c) cov[item_g0 + k] = 1; } while (0)
  int cur = G.entrance, cost = 0; COVER(cur);
  for (;;) {
    for (int c = 0; c < MAP_LEN; c++) dist[c] = 0x7FFF; int q[MAP_LEN], qh = 0, qt = 0; dist[cur] = 0; par[cur] = -1; q[qt++] = cur; int best = -1;
    while (qh < qt && best < 0) { int c = q[qh++];
      // is c in an uncovered group?
      { int x = cx(c), y = cy(c); for (int r = 0; r < G.n_rooms && best < 0; r++) if (gr[r] >= 0 && !cov[gr[r]]) { Room *R = &G.rooms[r]; if (x >= R->left && x <= R->right && y >= R->top && y <= R->bottom && (G.map[c] == T_DOOR || G.map[c] == T_OPEN_DOOR || (x > R->left && x < R->right && y > R->top && y < R->bottom))) best = c; }
        for (int k = 0; k < nitem && best < 0; k++) if (!cov[item_g0 + k] && icell[k] == c) best = c; }
      if (best >= 0) break;
      for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || !tg[n] || dist[n] != 0x7FFF) continue; dist[n] = dist[c] + 1; par[n] = c; q[qt++] = n; } }
    if (best < 0) break;
    cost += dist[best]; for (int c = best; c >= 0; c = par[c]) COVER(c); cur = best;
  }
  for (int c = 0; c < MAP_LEN; c++) dist[c] = 0x7FFF; { int q[MAP_LEN], qh = 0, qt = 0; dist[cur] = 0; q[qt++] = cur;
    while (qh < qt) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || !tg[n] || dist[n] != 0x7FFF) continue; dist[n] = dist[c] + 1; q[qt++] = n; } } }
  #undef COVER
  return cost + (dist[G.exit] < 0x7FFF ? dist[G.exit] : 0);
}
static int exp_seer(const uint8_t *tg) {                  // a second yardstick: someone who knows the map but must still SEE every tile:
  // from each spot what it shows (the game's own field of view), then greedily the spot with the most new tiles per step
  // (items must be stood on), every tile seen on the way counting, ending at the exit
  static uint8_t fovs[MAP_LEN][MAP_LEN / 8]; static int16_t idx[MAP_LEN]; int nc = 0;
  uint8_t vis0[MAP_LEN / 8], fov0[MAP_LEN / 8]; memcpy(vis0, G.visited, sizeof vis0); memcpy(fov0, G.fov, sizeof fov0); int p0 = G.hero.pos;
  for (int c = 0; c < MAP_LEN; c++) { idx[c] = -1; if (!tg[c] || !PASSABLE(G.map[c]) || (TFLAGS(G.map[c]) & F_AVOID)) continue; G.hero.pos = c; update_fov(); memcpy(fovs[nc], G.fov, MAP_LEN / 8); idx[c] = nc++; }
  memcpy(G.visited, vis0, sizeof vis0); memcpy(G.fov, fov0, sizeof fov0); G.hero.pos = p0;
  static uint8_t seen[MAP_LEN]; memset(seen, 0, sizeof seen); bool needi[MAP_LEN]; memset(needi, 0, sizeof needi);
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !tg[hp->pos] || (hp->type != HEAP_PLAIN && hp->type != HEAP_CHEST)) continue;
    int r = room_at(hp->pos), ty = r >= 0 ? G.rooms[r].type : -1; if (ty == RT_POOL || ty == RT_TRAPS || ty == RT_SHOP || ty == RT_VAULT || ty == RT_STORAGE) continue; needi[hp->pos] = true; }
  #define SEE(c) do { int _i = idx[c]; if (_i >= 0) for (int _q = 0; _q < MAP_LEN; _q++) if (tg[_q] && BIT_GET(fovs[_i], _q)) seen[_q] = 1; needi[c] = false; } while (0)
  int cur = G.entrance, cost = 0; SEE(cur);
  static int16_t d[MAP_LEN], par[MAP_LEN];
  for (int guard = 0; guard < 400; guard++) {
    for (int c = 0; c < MAP_LEN; c++) d[c] = 0x7FFF; int q[MAP_LEN], qh = 0, qt = 0; d[cur] = 0; par[cur] = -1; q[qt++] = cur;
    while (qh < qt) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || d[n] != 0x7FFF || idx[n] < 0) continue; d[n] = d[c] + 1; par[n] = c; q[qt++] = n; } }
    int best = -1; double bs = 0;
    for (int c = 0; c < MAP_LEN; c++) { if (idx[c] < 0 || d[c] == 0x7FFF || c == cur) continue; int nw = 0; for (int q2 = 0; q2 < MAP_LEN; q2++) if (tg[q2] && !seen[q2] && BIT_GET(fovs[idx[c]], q2)) nw++;
      double sc = (nw + (needi[c] ? 40 : 0)) / (double)(d[c] + 2); if (sc > bs) { bs = sc; best = c; } }
    if (best < 0) break;
    cost += d[best]; for (int c = best; c >= 0; c = par[c]) SEE(c); cur = best;
  }
  #undef SEE
  for (int c = 0; c < MAP_LEN; c++) d[c] = 0x7FFF; { int q[MAP_LEN], qh = 0, qt = 0; d[cur] = 0; q[qt++] = cur;
    while (qh < qt) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (!nb_ok(c, n) || d[n] != 0x7FFF || !tg[n] || !PASSABLE(G.map[n])) continue; d[n] = d[c] + 1; q[qt++] = n; } } }
  return cost + (d[G.exit] < 0x7FFF ? d[G.exit] : 0);
}
static int run_explab(void) {
  int N = getenv("EXPN") ? atoi(getenv("EXPN")) : 20, D = getenv("EXPD") ? atoi(getenv("EXPD")) : 24, g0 = getenv("START") ? atoi(getenv("START")) : 0;
  PRM[P_QSTAY] = 0; PRM[P_HUNT] = 0; s_explab = !getenv("EXPQUEST");   // (stage 1: no quests)
  static int scs[4096]; int nsc = 0; long tt = 0, nf = 0, fin = 0, tdone = 0, odone = 0, nsrch = 0, sc_tot = 0, nseen = 0, tseen = 0, tafter = 0, miss_c = 0, tot_c = 0, miss_i = 0, tot_i = 0;
  for (int g = g0; g < N; g++) {
    cur_game = g; srand(g + 1);
    extern uint32_t g_host_time; extern uint16_t g_host_ms; g_host_time = 1000000 + g * 3600 * 5; g_host_ms = (uint16_t)(g * 40503u + 7);
    memset(g_keys, 0, sizeof g_keys); g_new_class = g % 4; g_trade = 0; g_trade_arg = 0; s_prev_now = 0;
    game_new();
    for (int d = 1; d <= D; d++) {
      if (d > 1) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
      if (d % 5 == 0 || d > 20) continue;              // boss floors and the last ones: not exploration
      int fds[2]; if (pipe(fds)) return 1;
      pid_t pid = fork();
      if (pid == 0) {
        close(fds[0]); V = 0; s_is_child = 1;
        memset(s_ign_until, 0, sizeof s_ign_until); s_goal = 99; s_curse_trip = 0; s_last_depth = G.depth; s_goo_calm = -1; s_goo_spot = -1; s_goo_hold = 0; s_stash = -1; s_stash_drink = -1; s_last_up = -1000000; s_upg_hold_until = 0; s_test_walk = -1; s_resting = false; s_step_back = -1; s_pick = -1; s_wnd_btn = 0; s_pend_depth = 0;
        exp_clear_mobs(); on_new_floor(true);
        if (getenv("EXPH") && atoi(getenv("EXPH")) == g * 100 + d) for (int i = 0; i < G.n_heaps; i++) { char nm[64]; item_name(&G.heaps[i].item, nm, sizeof nm); printf("    HEAP %d at %d,%d type %d: %s\n", i, cx(G.heaps[i].pos), cy(G.heaps[i].pos), G.heaps[i].type, nm); }
        uint8_t tg[MAP_LEN]; int ncell; exp_target(tg, &ncell); int nit = exp_items(tg), orc = getenv("SEER") ? exp_seer(tg) : exp_oracle(tg);
        int32_t now0 = G.now; int steps = 0, done = 0, srch0 = st_search, t_seen = -1; memcpy(s_vis0, G.visited, sizeof s_vis0);
        while (steps++ < 6000 && (G.now - now0) / TICK < 4000) {
          G.hero.HP = G.hero.HT; G.hero.hunger = 0; exp_clear_mobs(); s_bot_steps = steps; cur_step = steps;
          if (s_pend_depth) { done = s_pend_depth > G.depth && G.hero.pos == G.exit; break; }   // (a fall through a trapdoor or chasm: not done)
          if (t_seen < 0) { bool all = true; for (int c = 0; c < MAP_LEN && all; c++) if (tg[c] && !BIT_GET(G.visited, c)) all = false; if (all && exp_items(tg) == 0) t_seen = (G.now - now0) / TICK; }
          if (getenv("EXPVT") && (G.now - now0) / TICK >= atoi(getenv("EXPVT")) && getenv("EXPT") && atoi(getenv("EXPT")) == g * 100 + d) V = getenv("EXPVV") ? 2 : 1;
          s_did[0] = 0; int32_t tw0 = G.now; if (!la_step()) break;
          if (getenv("EXPW")) { char cat[40] = "other"; const char *b = strchr(s_did, '['); if (b) { int k = 0; b++; while (*b && *b != ']' && *b != ':' && k < 39) cat[k++] = *b++; cat[k] = 0; } else if (s_did[0]) { int k = 0; const char *q = s_did; while (*q && *q != ' ' && *q != ':' && k < 39) cat[k++] = *q++; cat[k] = 0; }
            int gain = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && !BIT_GET(s_vis0, c) && (PASSABLE(G.map[c]) || G.map[c] == T_DOOR)) gain++; memcpy(s_vis0, G.visited, sizeof s_vis0);
            printf("W %s %d %d\n", cat, (G.now - tw0) / TICK, gain); }
          if (getenv("EXPT") && atoi(getenv("EXPT")) == g * 100 + d) printf("    t%d %d,%d %s\n", (G.now - now0) / TICK, cx(G.hero.pos), cy(G.hero.pos), s_did);
        }
        int mc = 0; for (int c = 0; c < MAP_LEN; c++) if (tg[c] && !BIT_GET(G.visited, c)) mc++;
        if (getenv("EXPT")) { for (int c = 0; c < MAP_LEN; c++) if (tg[c] && !BIT_GET(G.visited, c)) { int r = room_at(c); printf("    unseen %d,%d terrain %d room %d type %d\n", cx(c), cy(c), G.map[c], r, r >= 0 ? G.rooms[r].type : -1); } }
        if (getenv("EXPT")) for (int y = 0; y < MAP_H; y++) { printf("    %2d ", y); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c]; char ch = c == G.hero.pos ? '@' : mob_at(c) >= 0 ? 'M' : c == G.exit ? '>' : PWATER(c) ? 'P' : IS_WATER(t) ? '~' : t == T_WALL ? '#' : (t == T_DOOR || t == T_OPEN_DOOR) ? '+' : t == T_SECRET_DOOR ? 'S' : PASSABLE(t) ? '.' : (TFLAGS(t) & F_AVOID) ? '^' : t == T_CHASM ? ' ' : '?'; if (!BIT_GET(G.visited, c) && ch != '#') ch = ch == '.' ? ',' : ch; putchar(ch); } putchar('\n'); }
        if (getenv("EXPT")) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_SECRET_DOOR) { printf("    SECRET %d,%d searched %d visited %d nbrs:", cx(c), cy(c), BIT_GET(s_searched, c), BIT_GET(G.visited, c)); for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n)) printf(" %d,%d:t%d v%d", cx(n), cy(n), G.map[n], BIT_GET(G.visited, n)); } printf("\n"); }
        if (getenv("EXPT")) printf("    pw(14,10)=%d preach(15,9)=%d terrain(14,10)=%d piranhas %d\n", PWATER(334), PREACH(303), G.map[334], s_piranhas);
        if (getenv("EXPT")) printf("    end: hero %d,%d exit %d,%d (terrain %d) pend %d depth %d\n", cx(G.hero.pos), cy(G.hero.pos), cx(G.exit), cy(G.exit), G.map[G.exit], s_pend_depth, G.depth);
        int left = exp_items(tg);
        if (getenv("EXPI")) for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; if ((unsigned)hp->pos >= MAP_LEN || !tg[hp->pos] || (hp->type != HEAP_PLAIN && hp->type != HEAP_CHEST)) continue; int r = room_at(hp->pos), ty = r >= 0 ? G.rooms[r].type : -1; if (ty == RT_POOL || ty == RT_TRAPS || ty == RT_SHOP || ty == RT_VAULT || ty == RT_STORAGE) continue;
          char nm[64]; item_name(&hp->item, nm, sizeof nm); printf("    LEFT g%d d%d: %s (kind %d) at %d,%d type %d room type %d seen %d dropped %d black %d terrain %d\n", g, d, nm, hp->item.kind, cx(hp->pos), cy(hp->pos), hp->type, ty, BIT_GET(G.visited, hp->pos), BIT_GET(s_dropped, hp->pos), s_black[hp->pos] > s_bot_steps, G.map[hp->pos]); }
        int res[9] = { (G.now - now0) / TICK, done, mc, ncell, left, nit, orc, st_search - srch0, t_seen };
        if (getenv("EXPV")) printf("  floor g%d d%d: %s in %d turns (map-knower %d; all seen at %d; %d searches), unseen %d/%d cells, items left %d/%d\n", g, d, done ? "done" : "UNFINISHED", res[0], orc, t_seen, res[7], mc, ncell, left, nit);
        if (write(fds[1], res, sizeof res) != sizeof res) {}
        _exit(0);
      }
      close(fds[1]); int res[9] = { 0 }; if (read(fds[0], res, sizeof res) != sizeof res) res[1] = -1; close(fds[0]); int st; waitpid(pid, &st, 0);
      nf++; tt += res[0]; fin += res[1] == 1; if (res[1] == 1) { tdone += res[0]; odone += res[6]; } nsrch += res[7]; { int sc = res[0] + 3 * res[2] + 40 * res[4] + (res[1] == 1 ? 0 : 2000); sc_tot += sc; if (nsc < 4096) scs[nsc++] = sc; } if (res[8] >= 0) { nseen++; tseen += res[8]; tafter += res[0] - res[8]; } miss_c += res[2]; tot_c += res[3]; miss_i += res[4]; tot_i += res[5];
    }
  }
  printf("explab: %ld floors, %ld finished (%.1f%%), %.1f turns/floor; finished floors %.1f turns vs map-knower %.1f (%.0f%% more), unseen cells %.2f%%, items left %.2f%%\n", nf, fin, 100.0 * fin / (nf ? nf : 1), (double)tt / (nf ? nf : 1), (double)tdone / (fin ? fin : 1), (double)odone / (fin ? fin : 1), 100.0 * (tdone - odone) / (odone ? odone : 1), 100.0 * miss_c / (tot_c ? tot_c : 1), 100.0 * miss_i / (tot_i ? tot_i : 1));
  { for (int i = 1; i < nsc; i++) for (int j = i; j > 0 && scs[j] < scs[j - 1]; j--) { int t = scs[j]; scs[j] = scs[j - 1]; scs[j - 1] = t; }
    long tr = 0; int lo = nsc / 10, hi = nsc - nsc / 10; for (int i = lo; i < hi; i++) tr += scs[i];
    printf("  SCORE %.1f mean, %d median, %.1f trimmed (turns + 3 per unseen tile + 40 per item left + 2000 unfinished; lower is better)\n", (double)sc_tot / (nf ? nf : 1), nsc ? scs[nsc / 2] : 0, (double)tr / (hi > lo ? hi - lo : 1)); }
  printf("  %.1f searches/floor; everything seen and taken after %.1f turns, then %.1f more to leave\n", (double)nsrch / (nf ? nf : 1), (double)tseen / (nseen ? nseen : 1), (double)tafter / (nseen ? nseen : 1));
  return 0;
}
int main(int argc, char **argv) {
  s_salt = getenv("BOTSALT") ? (uint32_t)atoi(getenv("BOTSALT")) : 0;
  if (!getenv("NOX")) { static const char *const X[] = { "X_OOZE", "X_DODGE", "X_DODGE2", "X_NOTEST", "X_DRYGOO", "X_OPENFIX", "X_NOBOSSTRAP" }; for (unsigned i = 0; i < sizeof X / sizeof *X; i++) setenv(X[i], "1", 0); }   // 0.52e: the boss audit's Goo/Tengu fixes on (NOX=1: off)
  signal(SIGALRM, on_alarm); signal(SIGVTALRM, on_alarm); setvbuf(stdout, NULL, _IONBF, 0);
  if (!game_init()) return 1;
  prm_load();
  if (getenv("LAK")) LA_K = atoi(getenv("LAK"));
  { s_noburnt = getenv("NOBURNT") != NULL; s_oldfire = getenv("OLDFIRE") != NULL; s_oldchoke = getenv("OLDCHOKE") != NULL; s_oldeat = getenv("OLDEAT") != NULL; if (getenv("EATLATE")) s_eatlate = atoi(getenv("EATLATE")); else if (getenv("PZTEST") && getenv("EATLATE50")) s_eatlate = 50; /* (0.92 e1, 120 games each: eat at Hungry 7.11, at Starving 6.81, Starving under 80% 6.50, under 50% 5.67: starving bleeds ~3 HP a 10 turns and stops mending) */ /* 0.92 (owner: "I always eat after starving down a little"): hunger stops at Starving, so turns spent there cost no food, only ~1.3 HP a 10 turns */ if (getenv("WARREAT")) s_warreat = atoi(getenv("WARREAT")); s_oldgear = getenv("OLDGEAR") != NULL; s_oldtrap = getenv("OLDTRAP") != NULL; if (getenv("SRCH_WIN")) s_srch_win = atoi(getenv("SRCH_WIN")); if (getenv("SRCH_K")) s_srch_k = atoi(getenv("SRCH_K")); s_olddead = getenv("OLDDEAD") != NULL; s_olddark = getenv("OLDDARK") != NULL; if (getenv("RISKP")) s_riskp = atof(getenv("RISKP")); s_nounk = getenv("NOUNK") != NULL; s_oldsleep = getenv("OLDSLEEP") != NULL; }
  s_dring_on = getenv("DEATHRING") != NULL; if (getenv("STARVELEAVE")) s_starve_leave = atoi(getenv("STARVELEAVE"));
  if (getenv("ARM_SPEEDK")) s_arm_speedk = atof(getenv("ARM_SPEEDK"));
  s_front_ortho = getenv("FRONT_ORTHO") != NULL; s_exw_diag = getenv("EXW_DIAG") ? atoi(getenv("EXW_DIAG")) : 0; s_exw_unk = getenv("EXW_UNK") ? atoi(getenv("EXW_UNK")) : 0; s_exp_tsp = getenv("EXP_TSP") != NULL; s_pocket = getenv("POCKET") ? atoi(getenv("POCKET")) : 0; s_plandbg = getenv("PLANDBG") ? atoi(getenv("PLANDBG")) : -1; s_detour = getenv("DETOUR") ? atoi(getenv("DETOUR")) : 0; s_kite2 = getenv("KITE2") != NULL; s_headb = getenv("HEADB") ? atoi(getenv("HEADB")) : 0; s_scrhold = getenv("SCRHOLD") ? atoi(getenv("SCRHOLD")) : 0; s_pztest = getenv("PZTEST") != NULL; s_firedoom = s_pztest && !getenv("NOFIREDOOM"); s_grassw = getenv("GRASSW") ? atoi(getenv("GRASSW")) : 0; s_expgain = getenv("EXPGAIN") ? atoi(getenv("EXPGAIN")) : 0; s_expcommit = getenv("EXPCOMMIT") ? atoi(getenv("EXPCOMMIT")) : 0; s_exw_far = getenv("EXW_FAR") ? atoi(getenv("EXW_FAR")) : 0; s_srch_dead = getenv("SRCH_DEAD") ? atoi(getenv("SRCH_DEAD")) : 150; s_exw_item = getenv("EXW_ITEM") ? atoi(getenv("EXW_ITEM")) : getenv("OLDITEMS") ? 0 : 20;   /* 0.58: far items after the near exploring (+0.35) */ s_exw_inear = getenv("EXW_INEAR") ? atoi(getenv("EXW_INEAR")) : getenv("OLDITEMS") ? 99 : 6; s_front_wall = getenv("FRONT_WALL") != NULL;
  if (getenv("ARRLAB")) { s_lab_b = argc > 1 ? atoi(argv[1]) : 1 << 30; const char *al = getenv("ARRLAB"); static char ab[300]; if (!strncmp(al, "up:", 3)) { snprintf(ab, sizeof ab, "../%s", al + 3); al = ab; } return run_lab(al); }   /* (0.93: "up:bundle/arr/d15": another bundle's arrivals, for a newer bot) */   /* (0.93: after the settings: the lab ran without PZTEST, DETOUR, KITE2...) */
  if (getenv("EXPLAB")) return run_explab();
  int N = argc > 1 ? atoi(argv[1]) : 100, hs = getenv("HS") ? atoi(getenv("HS")) : 3;
  int maxsteps = getenv("STEPS") ? atoi(getenv("STEPS")) : 150000, rt = getenv("RT") ? atoi(getenv("RT")) : 400, fk = getenv("FK") ? atoi(getenv("FK")) : 3000;
  if (getenv("STOP")) sscanf(getenv("STOP"), "%d:%d", &stop_game, &stop_step);
  V = getenv("V") != NULL;
  if (getenv("FAIR")) { setenv("NOSTRAT", "1", 1); if (getenv("FAIRNOLA")) setenv("NOLA", "1", 1); }   // 0.93: the upgrade look-ahead (worth nothing) off; the fight look-ahead plays a copy cut down to what a player knows (la_scrub)   // 0.93 review: both look-aheads play a copy of the real game (unseen floors, monsters, unknown items): off
  P = getenv("PRESS") ? atoi(getenv("PRESS")) : 0; s_pure = getenv("PURE") != NULL; s_toe = getenv("TOE") != NULL;
  if (P) { int z = getenv("ZOOM") ? atoi(getenv("ZOOM")) : 1; memcpy(g_view, PVIEW[z], 4); if ((g_assist & 5) == 5) memset(g_view, 0, 4); }   // 0.77: Assist Max (0.80: off-screen targets, ASSIST=7): the whole map
  static int s_seedv[512], s_seedc[512], s_seedn; if (getenv("SEEDS")) { const char *q = getenv("SEEDS"); while (*q && s_seedn < 512) { s_seedv[s_seedn] = atoi(q); s_seedc[s_seedn] = -1; while (*q >= '0' && *q <= '9') q++; if (*q == ':') { q++; s_seedc[s_seedn] = atoi(q); while (*q >= '0' && *q <= '9') q++; } s_seedn++; if (*q == ',') q++; else break; } }   // SEEDS=84:0,151:2 (game:class): game index i plays the i-th listed seed (the runner hands out index ranges)
  for (int gi = getenv("START") ? atoi(getenv("START")) : 0; gi < N; gi++) {
    int si = gi - (getenv("SEEDSBASE") ? atoi(getenv("SEEDSBASE")) : 0); if (s_seedn && (si < 0 || si >= s_seedn)) break; int g = s_seedn ? s_seedv[si] : gi; int seedcls = s_seedn ? s_seedc[si] : -1;
    cur_game = g; srand(g + 1 + s_salt * 7919); st_games++; lg_maxleft = 0;
    if (!getenv("OLDLEAK")) { g_ovr = (Ovr){ 0, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0 }; g_goal = -1; g_from = -1; memset(g_drop, 0, sizeof g_drop); g_drop_i = 0; }   // 0.64: no Assist state carried from game to game (a game replays alone the same)
    extern uint32_t g_host_time; extern uint16_t g_host_ms;
    g_host_time = 1000000 + g * 3600 * 5; g_host_ms = (uint16_t)(g * 40503u + 7);   // a different dungeon each game, night or day
    s_wait_n = 0; s_cooked = 0; memset(s_pot_guess, 0, sizeof s_pot_guess); s_brew_kind = -1; s_brew_i = 3; s_fork_until = -1; memset(s_bounce_n, 0, sizeof s_bounce_n);
    s_kite_uid = -1; s_kite_cell = -1; s_thief_uid = -1; s_thief_cell = -1; s_lurk_until = 0; s_sun = -1; s_onto = -1; s_want_jump = false; s_step_back = -1; s_altar_uid = -1; st_sou_read = 0;   // (review: per-game state)
    memset(g_keys, 0, sizeof g_keys);
    g_new_class = seedcls >= 0 ? seedcls : getenv("CLS") ? atoi(getenv("CLS")) : getenv("ZONES") ? (g / (getenv("ZMAX") ? atoi(getenv("ZMAX")) : 5)) % 4 : g % 4;
    g_trade = 0; g_trade_arg = 0; s_prev_now = 0; g_from = -1; g_goal = -1;     // (a window left open when the last game stopped)
    game_new();
    if (s_salt && getenv("DICESALT")) { extern uint32_t g_rng; g_rng ^= s_salt * 2654435761u; }   // 0.93: the same floors (they come from the seed), other dice for fights, spawns and drops
    head_start(hs);
    s_zone_start = getenv("ZONES") ? 1 + 5 * (g % (getenv("ZMAX") ? atoi(getenv("ZMAX")) : 5)) : 0;   // 0.48: equal numbers from each boss on
    if (s_zone_start > 1) zone_kit(s_zone_start);
    if (s_zone_start) st_zone_n[s_zone_start / 5]++;
    if (getenv("CAMPLOAD")) { FILE *f = fopen(getenv("CAMPLOAD"), "rb");   // 0.50 campaign: carry on from a checkpoint (same dice as the run that made it)
      if (!f || fread(g_keys, sizeof g_keys, 1, f) != 1 || fread(&g_rng, sizeof g_rng, 1, f) != 1 || fread(&s_camp_steps, sizeof s_camp_steps, 1, f) != 1 || !game_load()) { printf("CAMPLOAD failed\n"); return 1; } fclose(f);
      srand(G.depth * 7919 + cur_game * 104729); s_last_depth = G.depth; on_new_floor(false);
      if (getenv("CAMPSEED")) s_camp_reroll = atoi(getenv("CAMPSEED")); }   // (a flagged reroll: other dice from here, applied once the floor's checkpoint round trip is done)
    if (getenv("HG")) { uint32_t hsh = 2166136261u; const uint8_t *pp = (const uint8_t *)&G; for (size_t i = 0; i < sizeof G; i++) hsh = (hsh ^ pp[i]) * 16777619u; printf("  HG game %d hash %08x\n", g, hsh); }
    if (getenv("SD")) { int sd = atoi(getenv("SD")); for (int d = 2; d <= sd; d++) { switch_level(d, ARRIVE_ENTRANCE); game_level_commit(); }
      while (G.hero.lvl < sd) earn_exp(hero_max_exp() - G.hero.exp); G.hero.HT += 15 * hs; G.hero.HP = G.hero.HT;   /* (0.93: level-ups, not a bare lvl: attack and defense were left at level 1) */ G.hero.STR = 12 + sd / 4 + hs;
      Item a = item_make(sd >= 15 ? IK_SCALE_ARMOR : IK_MAIL_ARMOR, 1); a.lvl = sd / 4; a.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; a.dur = item_max_dur(&a); G.hero.armor = a;
      Item w = item_make(sd >= 15 ? IK_LONGSWORD : IK_SWORD, 1); w.lvl = sd / 4; w.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; w.dur = item_max_dur(&w); G.hero.weapon = w; }
    memset(s_ign_until, 0, sizeof s_ign_until);
    s_goo_calm = -1; s_goo_spot = -1; s_goo_hold = 0; s_stash = -1; s_stash_drink = -1; s_last_up = -1000000; s_upg_hold_until = 0; s_test_walk = -1; s_resting = false; s_step_back = -1;   // bot3: per-game state
    s_am_depth = 0; s_won = 0; s_amulet_scene = 0; s_goal = 99; s_curse_trip = 0; s_pick = -1; s_wnd_btn = 0; s_jump = false; s_last_depth = -1; s_nlog = 0; memset(s_last_log, 0, sizeof s_last_log);
    int last_now = G.now, same_now = 0, saved_at = G.now, prev_max = 0, s_level_changes = 0;
    int steps;
    for (steps = s_camp_steps; steps < maxsteps && !s_won; steps++) {
      cur_step = steps; s_bot_steps = steps; { struct itimerval it = { { 0, 0 }, { getenv("HANGSEC") ? atoi(getenv("HANGSEC")) : 60, 0 } }; setitimer(ITIMER_VIRTUAL, &it, NULL); }   /* 0.53: CPU time, so a paused run (PAUSE) doesn't count as a hang */
      if (getenv("MEM") && steps % 100 == 0) { FILE *f = fopen("/proc/self/statm", "r"); long a = 0, b = 0; if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) {} fclose(f); } printf("  mem step %d depth %d rss %ld MB\n", steps, G.depth, b * 4 / 1024); }
      if (g == stop_game && steps == stop_step) V = 1;
      if (g == stop_game && steps == stop_step && getenv("DUMP")) { dump_map(G.hero.pos); for (int i = 0; i < G.n_heaps; i++) printf("   heap %d type %d kind %d\n", G.heaps[i].pos, G.heaps[i].type, G.heaps[i].item.kind); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf("   mob %s at %d st %d fov %d uid %d hp %d\n", mob_name(G.mobs[i].type), G.mobs[i].pos, G.mobs[i].state, BIT_GET(G.fov, G.mobs[i].pos), G.mobs[i].uid, G.mobs[i].HP); }
      if (getenv("STOPD") && G.depth == atoi(getenv("STOPD"))) V = 1;
      if (getenv("MINDTEST") && g == atoi(getenv("MINDTEST")) && !s_is_child && G.hero.HP > 0 && !s_pend_depth && enemy_count() == 0) {   // the suggester reads the player's mind: walk 3 steps by "touch" towards plan B or C
        static int mt = 0, mx = -1, ma = -1, ms = 0; int16_t c[TARGETS_MAX]; uint8_t k[TARGETS_MAX]; int n = game_targets(c, k);
        if (mt == 0 && steps > 30 && n >= 3) for (int j = 1; j < n && mt == 0; j++) { int x = g_tgobj[j]; if (k[j] == TK_ENEMY || x == g_tgobj[0] || x < 0) continue;
            bfs(x, false); int st = -1, sd = b_dist[G.hero.pos]; if (sd < 4 || sd == 0x7FFF) continue;
            for (int q = 0; q < 8; q++) { int nn = G.hero.pos + N8[q]; if (nb_ok(G.hero.pos, nn) && b_dist[nn] < sd && !char_at(nn)) { sd = b_dist[nn]; st = nn; } }
            bfs(g_tgobj[0], false); if (st < 0 || b_dist[st] < b_dist[G.hero.pos]) continue;   // (a step that brings plan A nearer too proves nothing)
            mt = 1; mx = x; ma = g_tgobj[0]; printf("  MIND game %d step %d: plan A %d,%d; walking to entry %d's objective %d,%d (%d away)\n", g, steps, cx(ma), cy(ma), j, cx(mx), cy(mx), b_dist[G.hero.pos]); }
        if (mt == 1) {
          bfs(mx, false); int st = -1, sd = b_dist[G.hero.pos];
          for (int q = 0; q < 8; q++) { int nn = G.hero.pos + N8[q]; if (nb_ok(G.hero.pos, nn) && b_dist[nn] < sd && !char_at(nn)) { sd = b_dist[nn]; st = nn; } }
          if (st >= 0) { g_ovr.c = st; g_ovr.t = G.hero.pos - 65536; walk(game_tap_cell(st)); ms++; }   // main.c on_tap: the player's own way
          n = game_targets(c, k);
          printf("  MIND after %d touch steps: plan A objective %d,%d%s (entry 0 shown at %d,%d)\n", ms, cx(g_tgobj[0]), cy(g_tgobj[0]), g_tgobj[0] == mx ? " = the one walked to: PROMOTED" : "", cx(c[0]), cy(c[0]));
          if (ms >= 3 || st < 0) mt = 2;
          continue;
        }
      }
      if (getenv("WALLCHK") && !s_is_child && G.hero.HP > 0) {   // 0.71: any target on a cell you can't stand on (owner: "blank, empty, non-gold wall tiles")
        int16_t c[TARGETS_MAX]; uint8_t k[TARGETS_MAX]; int n = game_targets(c, k);
        for (int j = 0; j < n; j++) if (c[j] >= 0 && c[j] < MAP_LEN && !PASSABLE(G.map[c[j]]) && k[j] != TK_ENEMY && k[j] != TK_VEIN && k[j] != TK_MINE && G.map[c[j]] != T_DOOR && G.map[c[j]] != T_LOCKED_DOOR && G.map[c[j]] != T_ALCHEMY && G.map[c[j]] != T_WELL)
          printf("  WALLCHK game %d step %d depth %d: entry %d/%d kind %d at %d,%d tile %d vis %d (objective %d,%d tile %d) hero %d,%d src0 %d\n", g, steps, G.depth, j, n, k[j], cx(c[j]), cy(c[j]), G.map[c[j]], BIT_GET(G.visited, c[j]), cx(g_tgobj[j]), cy(g_tgobj[j]), g_tgobj[j] >= 0 ? G.map[g_tgobj[j]] : -1, cx(G.hero.pos), cy(G.hero.pos), G.tgm.src0);
      }
      if (getenv("STEPLOG") && g == atoi(getenv("STEPLOG")) && !s_is_child) printf("  STEP %d pos %d rng %08x now %d hp %d nm %d\n", steps, G.hero.pos, g_rng, G.now, G.hero.HP, G.n_mobs);
      if (getenv("FIREDBG") && g == atoi(getenv("FIREDBG")) && !s_is_child) {   // the fire around the hero: terrain letter, fire level
        bool near = G.hero.b.burning; for (int dy = -3; dy <= 3; dy++) for (int dx = -5; dx <= 5; dx++) { int x = cx(G.hero.pos) + dx, y = cy(G.hero.pos) + dy; if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && g_blob[BLOB_FIRE] && g_blob[BLOB_FIRE][x + y * MAP_W]) near = true; }
        if (near) { printf("  FIRE t%d pos %d,%d burn %d hp %d\n", G.now / TICK, cx(G.hero.pos), cy(G.hero.pos), G.hero.b.burning, G.hero.HP);
          for (int dy = -3; dy <= 3; dy++) { printf("   "); for (int dx = -5; dx <= 5; dx++) { int x = cx(G.hero.pos) + dx, y = cy(G.hero.pos) + dy, c = x + y * MAP_W; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) { printf("   "); continue; }
            int t = G.map[c]; char ch = c == G.hero.pos ? '@' : mob_at(c) >= 0 ? 'm' : IS_WATER(t) ? '~' : t == T_WALL || t == T_WALL_DECO ? '#' : t == T_GRASS ? ',' : t == T_HIGH_GRASS ? '"' : t == T_EMBERS ? 'e' : t == T_DOOR || t == T_OPEN_DOOR ? '+' : t == T_FIRE_TRAP ? 'F' : t == T_SECRET_FIRE_TRAP ? 'x' : (t >= T_TOXIC_TRAP && t <= T_PARALYTIC_TRAP) || (t >= T_POISON_TRAP && t <= T_SECRET_LIGHTNING_TRAP) || t == T_GRIPPING_TRAP || t == T_SECRET_GRIPPING_TRAP || t == T_SUMMONING_TRAP ? '^' : PASSABLE(t) ? '.' : '?';
            int f = g_blob[BLOB_FIRE] ? g_blob[BLOB_FIRE][c] : 0; printf(" %c%c", ch, f ? '0' + (f > 9 ? 9 : f) : (TFLAGS(t) & F_FLAMABLE) ? 'f' : ' '); } printf("\n"); } }
      }
      if (s_fork_until == steps) {                      // the fork replay check
        uint32_t mine = state_hash();
        if (s_is_child) { if (s_trace) fclose(s_trace); if (write(s_fork_fd, &mine, 4) != 4) {} _exit(0); }
        uint32_t theirs = 0; int rd = read(s_fork_fd, &theirs, 4); close(s_fork_fd); int stt; waitpid(s_fork_pid, &stt, 0);
        if (rd == 4 && theirs != mine) viol("FORK_DIVERGE", "a save/load %d decisions ago changed what happened since", fk / 10);
        s_fork_until = -1; st_fork++; if (s_trace) { fclose(s_trace); s_trace = NULL; }
      }
      if (s_pend_depth) {                               // the UI fades
        floor_report();   // (then commits the floor change, then saves)
        s_arrival = true; game_level_commit(); check_state("commit"); s_arrival = false;
        if (G.hero.HP > 0) { game_save(); saved_at = G.now; }
      }
      if (G.depth != s_last_depth && ++s_level_changes > (getenv("RESPAWN") ? 2000 : 300)) { viol("PINGPONG", "300 floor changes in one game (depth %d, goal %d)", G.depth, s_goal); st_stuck++; break; }
      if (G.depth != s_last_depth) { bool first = G.max_depth > prev_max; prev_max = G.max_depth; s_last_depth = G.depth; on_new_floor(first); }
      if (getenv("HANDCHK") && !s_rollout && G.hero.HP > 0) { static long tot, multi; int16_t c[TARGETS_MAX]; uint8_t k[TARGETS_MAX]; int n = game_targets(c, k); if (n > 0 && (k[0] == TK_BAG || k[0] == TK_REST)) { tot++; if (n > 1) multi++; if (tot % 50 == 1) printf("  HANDCHK %ld hand plans, %ld with alternatives (this one: %d entries, kinds %d %d %d)\n", tot, multi, n, k[0], n > 1 ? k[1] : -1, n > 2 ? k[2] : -1); } }
      if (s_upg_child && (G.hero.HP <= 0 || s_won || steps + 1 >= maxsteps)) { int r2[2] = { G.max_depth, s_won }; if (write(s_cf_out, r2, sizeof r2) != sizeof r2) {} _exit(0); }
      if (s_cf_child) { if (G.hero.HP <= 0) cf_report(0); if (G.max_depth > s_cf_d0) cf_report(1); if (steps - s_cf_s0 > 8000) cf_report(2); }
      else if (getenv("CF") && !s_rollout && !s_is_child && G.hero.HP > 0 && G.max_depth > s_cf_d0 && G.depth == G.max_depth && !s_pend_depth) { s_cf_d0 = G.max_depth; cf_checkpoint(g, steps); }
      else if (getenv("FLA") && !s_rollout && !s_is_child && G.hero.HP > 0 && G.max_depth > s_cf_d0 && G.depth == G.max_depth && !s_pend_depth) { s_cf_d0 = G.max_depth; fla_choose(g, steps); }
      if (s_zone_start && G.max_depth >= s_zone_start + 5) break;   // 0.48: this zone's boss beaten
      if (!s_trail_n || s_trail[(s_trail_n - 1) & 7] != G.hero.pos) s_trail[s_trail_n++ & 7] = G.hero.pos;
      if (G.depth != s_floor_lastpos >> 16) { memset(s_stepped, 0, sizeof s_stepped); s_floor_moves = 0; s_floor_t0 = G.now; s_floor_lastpos = G.depth << 16 | 0xFFFF; }

      s_floor_steps++;
      if (!s_rollout && !s_is_child) { static int sd = -1, g0, gl, nl, left[24], b0, s0; bool shop = false; int n = 0, tmp[24];   // 0.93: SHOP lines: what each shop had, what was bought, what gold was there
        for (int i = 0; i < G.n_heaps; i++) if (G.heaps[i].type == HEAP_FOR_SALE) { shop = true; if (n < 24) tmp[n++] = G.heaps[i].item.kind; }
        if (shop) { if (sd != G.depth) { if (sd > 0) printf("SHOP g%d d%d gold %d -> %d bought %d sold %d left:", cur_game, sd, g0, gl, st_buys - b0, st_sells - s0), ({ for (int j = 0; j < nl; j++) { Item t = item_make(left[j], 1); char nm[48]; item_name(&t, nm, sizeof nm); printf(" [%s]", nm); } 0; }), printf("\n");
            sd = G.depth; g0 = G.hero.gold; b0 = st_buys; s0 = st_sells; } gl = G.hero.gold; nl = n; memcpy(left, tmp, sizeof tmp); }
        else if (sd > 0 && G.depth != sd) { printf("SHOP g%d d%d gold %d -> %d bought %d sold %d left:", cur_game, sd, g0, gl, st_buys - b0, st_sells - s0); for (int j = 0; j < nl; j++) { Item t = item_make(left[j], 1); char nm[48]; item_name(&t, nm, sizeof nm); printf(" [%s]", nm); } printf(" | gold now %d\n", G.hero.gold); sd = -1; }
        if (G.hero.HP <= 0) sd = -1; }
      if (!s_rollout && !s_is_child) { Hero *bh = &G.hero; if (bh->weapon.kind && bh->weapon.lvl > 0 && item_broken(&bh->weapon)) st_brkw++; if (bh->armor.kind && bh->armor.lvl > 0 && item_broken(&bh->armor)) st_brka++; if (bh->weapon.lvl > 0 || bh->armor.lvl > 0) st_brkn++; }   // 0.93: steps with upgraded gear worn broken
      if (getenv("WPN")) { static int lw = -1; if (G.hero.weapon.kind != lw) { printf("  weapon %d -> %d at step %d depth %d; last logs: %s | %s\n", lw, G.hero.weapon.kind, steps, G.depth, s_last_log[(s_nlog - 2) & 7], s_last_log[(s_nlog - 1) & 7]); lw = G.hero.weapon.kind; } }
      if (getenv("NIGHT")) { static int lastn = -1; if (G.night != lastn) { printf("  night %d -> %d at step %d depth %d\n", lastn, G.night, steps, G.depth); lastn = G.night; } }
      if (getenv("BLAME") && !s_rollout && !s_is_child && !s_cf_child) { if (G.hero.HP <= 0) bl_on_death(); else if (G.action == HA_NONE && !s_pend_depth && steps % (getenv("BLK") ? atoi(getenv("BLK")) : 20) == 0) bl_checkpoint(g); }
      if (G.hero.HP <= 0 && s_cf_pid > 0 && !s_cf_child && !s_rollout && !s_is_child && (!getenv("CFD") || strstr(getenv("CFD"), ({ static char b[8]; snprintf(b, sizeof b, ",%d,", G.depth); b; })))) {   // (CFD="5,10,": only deaths on those floors)
        int *fd = &s_cf_fd; pid_t *pd = &s_cf_pid; if (getenv("CFBACK") && s_cf_pid2 > 0) { fd = &s_cf_fd2; pd = &s_cf_pid2; }
        fflush(stdout); char msg[2] = { 'g', (char)G.depth }; if (write(*fd, msg, 2) != 2) {} close(*fd); int st; waitpid(*pd, &st, 0); *pd = -1; *fd = -1; cf_kill(); }
      if (G.hero.HP <= 0 && getenv("RESPAWN") && s_resp_n < 40 && !s_rollout && !s_is_child) {   // 0.52g (owner): a test respawn: each death counted and logged, then on with the same hero, bag and floor
        { char c[48]; snprintf(c, sizeof c, "%s", G.death); char *o = strstr(c, " on level"); if (o) *o = 0; char *k = strncmp(c, "Killed by ", 10) == 0 ? c + 10 : c;
          size_t L = strlen(s_resp_log); if (L < sizeof s_resp_log - 48) { int bh = -1, bt = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS)) { bh = G.mobs[i].HP; bt = MOB_DEFS[G.mobs[i].type].HT; } if (bh >= 0) snprintf(s_resp_log + L, sizeof s_resp_log - L, " d%d %s (lvl %d, boss %d/%d);", G.depth, k, G.hero.lvl, bh, bt); else snprintf(s_resp_log + L, sizeof s_resp_log - L, " d%d %s (lvl %d);", G.depth, k, G.hero.lvl); } }
        s_resp_n++; st_resp++; st_resp_d[G.depth]++;
        Hero *hh = &G.hero; hh->HP = hh->HT; memset(&hh->b, 0, sizeof hh->b); hh->ooze = hh->weakness = hh->vertigo = 0; G.deferred = 0; G.health_pos = -1; if (hh->hunger >= 300) hh->hunger = 0;
        for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && !(MOB_DEFS[m->type].flags & (MD_BOSS | MD_NPC)) && distance(m->pos, hh->pos) <= 2) m->HP = 0; }
        G.action = HA_NONE; continue; }
      if (G.hero.HP <= 0) {                             // Hero.die: an Ankh offers another chance
        if (find_kind(IK_ANKH) >= 0 && !g_trade) { st_ankh++; s_wnd_btn = 0; game_ankh(); if (s_wnd_btn) { s_wnd_btn = 0; s_arrival = true; walk(game_wnd_choice(0)); s_arrival = false; } if (G.hero.HP > 0) { s_last_depth = -1; continue; } }
        break;
      }
      if (s_pick >= 0) { int p = s_pick; s_pick = -1; int ch = pick_for((PickMode)p);
        if (p == PICK_UPGRADE && getenv("UPGCF") && !s_rollout && !s_is_child && !s_cf_child && !s_upg_child && s_upgcf_n < (getenv("UPGCFN") ? atoi(getenv("UPGCFN")) : 2)) {   // 0.58 (owner): the same moment, the scroll into each choice, played out to the end
          s_upgcf_n++; int opt[3], no = 0; opt[no++] = ch; if (ch != -2 && game_pick_ok(PICK_UPGRADE, -2)) opt[no++] = -2; if (ch != -1 && game_pick_ok(PICK_UPGRADE, -1)) opt[no++] = -1;
          int R = getenv("UPGCFR") ? atoi(getenv("UPGCFR")) : 4; char out[600]; Hero *uh = &G.hero;
          int on = snprintf(out, sizeof out, "UPGCF game %d n%d d%d cls %d lvl %d STR %d arm %d%+d%s wpn %d%+d%s default %d:", g, s_upgcf_n, G.depth, g_new_class, uh->lvl, uh->STR, uh->armor.kind, uh->armor.lvl, (uh->armor.flags & IF_LEVEL_KNOWN) ? "" : "?", uh->weapon.kind, uh->weapon.lvl, (uh->weapon.flags & IF_LEVEL_KNOWN) ? "" : "?", ch);
          for (int o2 = 0; o2 < no; o2++) { int tot = 0, wins = 0;
            for (int r = 0; r < R; r++) { int pp[2]; if (pipe(pp)) break; fflush(stdout); pid_t k = fork();
              if (k == 0) { close(pp[0]); s_upg_child = 1; s_cf_out = pp[1]; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } V = 0;
                g_rng ^= (uint32_t)(r + 1) * 2654435761u; srand(r + 1); walk(game_pick((PickMode)p, opt[o2])); goto upg_child_go; }
              close(pp[1]); int res[2] = { 0, 0 }; if (read(pp[0], res, sizeof res) != sizeof res) res[0] = 0; close(pp[0]); int stt; waitpid(k, &stt, 0); tot += res[0]; wins += res[1]; }
            on += snprintf(out + on, sizeof out - on, " [%s %.2f%s]", opt[o2] == -2 ? "armour" : opt[o2] == -1 ? "weapon" : "other", R ? (double)tot / R : 0, wins ? "+wins" : ""); }
          printf("%s\n", out); fflush(stdout); }
        walk(game_pick((PickMode)p, ch)); upg_child_go: continue; }
      if (s_wnd_btn) { s_wnd_btn = 0; int b = wnd_for(); if (getenv("WNDDBG")) printf("  WND step %d trade %d arg %d b1 [%s] b2 [%s] -> %d gold %d\n", s_bot_steps, g_trade, g_trade_arg, s_wnd_b1, s_wnd_b2, b, G.hero.gold); walk(game_wnd_choice(b)); continue; }
      if (s_jump) { s_jump = false; if (s_want_jump) { s_want_jump = false; walk(game_jump()); continue; } }
      if ((G.dflags & DF_AMULET) && find_kind(IK_AMULET) < 0) { if (!s_am_depth) s_am_depth = G.depth; s_goal = s_am_depth > 0 ? s_am_depth : 0; }
      else { if (s_am_depth > 0) st_thief_back++; s_am_depth = 0; if (G.dflags & DF_AMULET) s_goal = 0; }
      // the UI's autosave: every 20 turns
      if (G.now - saved_at >= 20 * TICK) { game_save(); saved_at = G.now; }
      if (rt && rand() % rt == 0 && G.action == HA_NONE) roundtrip();
      if (fk && s_fork_until < 0 && rand() % fk == 0 && G.action == HA_NONE) {
        bool gas = false; for (int b = 0; b < BLOB_COUNT; b++) gas |= g_blob[b] != NULL;
        if (!gas && !(G.dflags & DF_AMULET)) {          // (gas amounts come back rounded; with the Amulet a load beckons everyone: GameScene.create)
          game_save(); uint32_t rng = g_rng; int fds[2];
          if (pipe(fds) == 0) {
            pid_t pid = fork();
            if (pid == 0) { close(fds[0]); s_is_child = 1; s_fork_fd = fds[1]; if (!game_load()) _exit(1); g_rng = rng; V = 0; }
            else {                                       // the parent drops its dead creatures' slots as a load does (their order
              close(fds[1]); s_fork_fd = fds[0]; s_fork_pid = pid;   // decides who acts first at equal times: arbitrary in PD too)
              int n = 0; uint16_t seen = 0;
              for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) { if (G.seen_mobs >> i & 1) seen |= 1u << n; G.mobs[n++] = G.mobs[i]; }
              G.n_mobs = n; G.seen_mobs = seen;
            }
            if (getenv("FT") && g == atoi(getenv("FT"))) { char fn[200]; snprintf(fn, sizeof fn, "%s/ft_%s_%d.txt", getenv("FTDIR") ? getenv("FTDIR") : ".", pid == 0 ? "child" : "parent", steps); if (s_trace) fclose(s_trace); s_trace = fopen(fn, "w"); }
            s_fork_until = steps + fk / 10;
          }
        }
      }
      if (s_fork_until >= 0 && s_trace) { fprintf(s_trace, "s%d now %d hero %d hp %d rng %u act %d |", steps, G.now, G.hero.pos, G.hero.HP, g_rng, G.action);
        for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) fprintf(s_trace, " %d:%d@%d/%d/st%d/t%d", G.mobs[i].type, G.mobs[i].uid, G.mobs[i].pos, G.mobs[i].HP, G.mobs[i].state, G.mobs[i].time - G.now);
        fprintf(s_trace, " | heaps %d diffs %d\n", G.n_heaps, G.n_diffs);
        if (getenv("FTBIN")) { char fn[200]; snprintf(fn, sizeof fn, "%s/g_%s_%d.bin", getenv("FTDIR"), s_is_child ? "c" : "p", steps); FILE *f = fopen(fn, "wb"); fwrite(g_game, sizeof(Game), 1, f); fclose(f); } }
      if (V && getenv("MOBS")) { printf("  hero %d (t%d) hp %d:", G.hero.pos, G.map[G.hero.pos], G.hero.HP); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf(" %s@%d(t%d,st%d,hp%d)", mob_name(G.mobs[i].type), G.mobs[i].pos, G.map[G.mobs[i].pos], G.mobs[i].state, G.mobs[i].HP); printf("\n"); }
      int wk_mv0 = s_floor_moves, wk_now0 = G.now;       int dmg_hp0 = G.hero.HP, dmg_adj = 0, dmg_pos = G.hero.pos, dmg_d = G.depth; for (int q = 0; q < G.n_mobs; q++) if (G.mobs[q].HP > 0 && !ally(q) && adjacent(G.mobs[q].pos, G.hero.pos)) dmg_adj++;
      int32_t now0 = G.now;
#ifdef RNGLOG
      extern FILE *g_rnglog;   // build with level_rt_rnglog.c.txt as level_rt.c: it logs each draw's callers here
#else
      static FILE *g_rnglog;
#endif
      if (getenv("RNGLOG") && s_fork_until >= 0 && steps == atoi(getenv("RNGLOG"))) { char fn[200]; snprintf(fn, sizeof fn, "%s/rng_%s.txt", getenv("FTDIR"), s_is_child ? "c" : "p"); g_rnglog = fopen(fn, "w");
        fprintf(g_rnglog, "before: now %d hero.time %d turn_due %d level_t0 %d night %d |", G.now, G.hero.time, G.turn_due, G.level_t0, G.night); for (int i = 0; i < G.n_mobs; i++) fprintf(g_rnglog, " %d:%d hp%d t%d", i, G.mobs[i].type, G.mobs[i].HP, G.mobs[i].time); fprintf(g_rnglog, "\n"); }
      photo_step(g, steps);
      char *snapb = NULL; static FILE *s_rec; if (getenv("REC") && !s_rec && !s_is_child) s_rec = fopen(getenv("REC"), "w"); int nlog0 = s_nlog;   // 0.49: REC=file: every step of every game, one JSON line each
      bool snap_on = (s_rec && !s_is_child) || (getenv("SNAPG") && g == atoi(getenv("SNAPG"))) && steps >= (getenv("SNAPA") ? atoi(getenv("SNAPA")) : 0) && steps <= (getenv("SNAPB") ? atoi(getenv("SNAPB")) : 1 << 30);
      bool snap_death = getenv("SNAPDEATH") != NULL && !s_is_child;
      s_did[0] = 0;
      if (snap_on || snap_death) snapb = snap_state(g, steps);
      int32_t tbefore = G.now;
      if (!getenv("NIGHTOK")) G.night = 0;   // 0.54 (owner): daytime only for now
      int tpos0 = G.hero.pos; bool tfoe0 = enemy_count() > 0;
      sp_stat(); bool did = decide();
      if (getenv("TSPLIT") && !s_is_child) { static long ts[5]; static int tg = -1; if (tg != g) { if (tg >= 0) printf("TSPLIT g%d walk %ld fight %ld search %ld idle %ld\n", tg, ts[0] / TICK, ts[1] / TICK, ts[2] / TICK, ts[3] / TICK); memset(ts, 0, sizeof ts); tg = g; }
        long dt = G.now - tbefore; int k = tfoe0 || enemy_count() ? 1 : G.hero.pos != tpos0 ? 0 : strstr(s_did, "search") ? 2 : 3; ts[k] += dt;
        if (G.hero.HP <= 0) { printf("TSPLIT g%d walk %ld fight %ld search %ld idle %ld\n", tg, ts[0] / TICK, ts[1] / TICK, ts[2] / TICK, ts[3] / TICK); memset(ts, 0, sizeof ts); tg = -1; } }
      snprintf(s_ring[s_ring_n++ & 63], 96, "s%d d%d t%d @%d,%d hp%d/%d h%d: %s", steps, G.depth, G.now / TICK, G.hero.pos % MAP_W, G.hero.pos / MAP_W, G.hero.HP, G.hero.HT, G.hero.hunger, s_did[0] ? s_did : "-");
      { static const char *CAT[] = { "fight", "walk to", "rest to heal", "Drink", "Read", "Eat", "out of", "search", "grave", "on fire", "to water" }; static long tot[12]; static int last_g = -1;
        int k = 11; for (int i = 0; i < 11; i++) if (!strncmp(s_did[0] ? s_did : "rest", CAT[i], strlen(CAT[i]))) { k = i; break; }
        if (strstr(s_did, "[search") || strstr(s_did, "[stay")) k = 2;
        { static long wt[16]; static const char *WR[] = { "[target]", "[frontier]", "[search", "[stay]", "[stairs]", "[trap]", "[quest]", "[puzzle", "[grave", "look-ahead", "rest", "fight", "[amulet", "[boss", "[key" };
          int w = 15; for (int i = 0; i < 15; i++) if (strstr(s_did, WR[i])) { w = i; break; } wt[w] += (G.now - tbefore) / TICK;
          if (getenv("TURNS") && G.hero.HP <= 0) { printf("by reason:"); for (int i = 0; i < 16; i++) printf(" %s=%ld", i < 15 ? WR[i] : "other", wt[i]); printf("\n"); } }
        tot[k] += (G.now - tbefore) / TICK;
        if (getenv("TURNS2")) { static char key[200][32]; static long kt[200]; static int nk;
          char b[32]; int j = 0, words = 0; for (const char *q = s_did[0] ? s_did : "rest/wait"; *q && j < 31; q++) { if (*q == ' ' && ++words >= 3) break; if (*q == ':' || *q == '(' || *q == '[') break; if ((*q >= '0' && *q <= '9') || *q == ',') continue; b[j++] = *q; } b[j] = 0;
          if (strstr(s_did, "[")) { const char *q = strstr(s_did, "["); int jj = j; while (*q && *q != ']' && jj < 31) b[jj++] = *q++; b[jj] = 0; }
          int f = -1; for (int i = 0; i < nk; i++) if (!strcmp(key[i], b)) { f = i; break; } if (f < 0 && nk < 200) { f = nk++; strcpy(key[f], b); } if (f >= 0) kt[f] += (G.now - tbefore) / TICK;
          if (G.hero.HP <= 0 || s_won) { long sum = 0; for (int i = 0; i < nk; i++) sum += kt[i]; printf("TURNS2 total %ld depth %d:\n", sum, G.depth); for (int r = 0; r < 30; r++) { int bi = -1; for (int i = 0; i < nk; i++) if (kt[i] > 0 && (bi < 0 || kt[i] > kt[bi])) bi = i; if (bi < 0) break; printf("   %6ld %s\n", kt[bi], key[bi]); kt[bi] = -kt[bi]; } for (int i = 0; i < nk; i++) if (kt[i] < 0) kt[i] = -kt[i]; } }
        if (getenv("TURNS") && (g != last_g || G.hero.HP <= 0)) { last_g = g; }
        if (getenv("TURNS") && G.hero.HP <= 0) { printf("turns:"); for (int i = 0; i < 12; i++) printf(" %s=%ld", i < 11 ? CAT[i] : "other", tot[i]); printf("\n"); } }
      if (!s_did[0]) snprintf(s_did, sizeof s_did, "rest / wait / search");
      if (G.depth == dmg_d && !s_is_child && !s_cf_child) { int dt = (G.now - wk_now0) / TICK, dm = s_floor_moves - wk_mv0; if (G.hero.pos != dmg_pos && dm < distance(dmg_pos, G.hero.pos)) { dm++; s_floor_moves++; BIT_SET(s_stepped, G.hero.pos); if (s_stepcnt[G.hero.pos] < 255) s_stepcnt[G.hero.pos]++; } if (dt < dm) dt = dm;
        int cat = !strncmp(s_did, "rest", 4) ? 1 : !strncmp(s_did, "fight", 5) || !strncmp(s_did, "look-ahead", 10) ? 2 : strstr(s_did, "search") ? 3 : 0; s_wk[0] += dm; s_wk[1 + cat] += dt - dm;
        if (getenv("WKDBG2") && cat == 0 && dt - dm > 0 && !strncmp(s_did, "walk to", 7)) printf("  WK2 s%d d%d %s: dt %d dm %d pos %d->%d action %d log %s\n", steps, G.depth, s_did, dt, dm, dmg_pos, G.hero.pos, G.action, s_last_log[(s_nlog - 1) & 7]);
        if (getenv("WKDBG") && cat == 0 && dt - dm > 0) { static char lab[64][24]; static int lt[64], nl; char b[24]; int j = 0; for (const char *q = s_did; *q && j < 20; q++) { if (*q == ':' || *q == '(' || *q == '[' || (*q >= '0' && *q <= '9')) break; b[j++] = *q; } b[j] = 0; int f = -1; for (int i = 0; i < nl; i++) if (!strcmp(lab[i], b)) f = i; if (f < 0 && nl < 64) { f = nl++; strcpy(lab[f], b); } if (f >= 0) lt[f] += dt - dm;
          if (G.hero.HP <= 0 || steps % 500 == 0) { printf("  WKDBG:"); for (int i = 0; i < nl; i++) if (lt[i] > 20) printf(" [%s] %d", lab[i], lt[i]); printf("\n"); } } }
      if (getenv("FIGHTLOG") && !s_is_child && !s_cf_child && !s_upg_child && G.hero.HP > 0) {   // 0.58: what each fight cost against what the duel sum predicted
        static struct { int uid, type, hp0, sl, d; float pred; int adj; } ft[32]; static int nft;
        for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & (MD_NPC | MD_BOSS)) || distance(m->pos, G.hero.pos) > 2) continue; if (m->state != ST_HUNTING && !adjacent(m->pos, G.hero.pos)) continue;
          bool have = false; for (int j = 0; j < nft; j++) if (ft[j].uid == m->uid) have = true;
          if (!have && nft < 32) { ft[nft].uid = m->uid; ft[nft].type = m->type; ft[nft].hp0 = G.hero.HP; ft[nft].sl = m->state == ST_SLEEPING; ft[nft].d = G.depth; ft[nft].pred = (float)hp_cost_vs(m->type, G.hero.weapon.kind ? &G.hero.weapon : NULL, G.hero.armor.kind ? &G.hero.armor : NULL); ft[nft].adj = 0; nft++; } }
        for (int j = 0; j < nft; j++) { bool alive = false, near = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].uid == ft[j].uid && G.mobs[i].HP > 0) { alive = true; if (distance(G.mobs[i].pos, G.hero.pos) <= 8) near = true; }
          if (!alive || !near || G.depth != ft[j].d) { if (!alive && G.depth == ft[j].d) printf("FIGHT d%d %s lost %d pred %.1f lvl %d cls %d hp %d/%d sleep %d\n", ft[j].d, mob_name(ft[j].type), ft[j].hp0 - G.hero.HP, ft[j].pred, G.hero.lvl, g_new_class, ft[j].hp0, G.hero.HT, ft[j].sl); ft[j] = ft[--nft]; j--; } } }
      if (getenv("DMGLOG") && !s_is_child && !s_cf_child) { static long tot[4][3][3][4]; int dmg = dmg_hp0 - G.hero.HP; if (dmg > 0 && dmg_d <= 4 && G.depth == dmg_d) {   // 0.58: where the early damage is taken
          int cat = !strncmp(s_did, "fight", 5) ? 0 : !strncmp(s_did, "look-ahead", 10) ? 1 : !strncmp(s_did, "rest", 4) ? 2 : 3;
          int pos = (G.map[dmg_pos] == T_DOOR || G.map[dmg_pos] == T_OPEN_DOOR) ? 0 : room_at(dmg_pos) < 0 ? 1 : 2;
          int adj = dmg_adj >= 2 ? 2 : dmg_adj; int band = dmg_hp0 * 4 / (G.hero.HT ? G.hero.HT : 1); if (band > 3) band = 3;
          tot[cat][pos][adj][band] += dmg; }
        if (G.hero.HP <= 0 || s_won || steps + 1 >= maxsteps) { static const char *C[] = { "fight", "lookahead", "rest", "other" }, *P[] = { "door", "corridor", "room" }; long all = 0; for (int a = 0; a < 4; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) for (int e = 0; e < 4; e++) all += tot[a][b][c][e];
          printf("DMG total %ld\n", all); for (int a = 0; a < 4; a++) for (int b = 0; b < 3; b++) for (int c = 0; c < 3; c++) { long t = 0; for (int e = 0; e < 4; e++) t += tot[a][b][c][e]; if (t) printf("DMG %s %s adj%d %ld (hp quarters %ld %ld %ld %ld)\n", C[a], P[b], c, t, tot[a][b][c][0], tot[a][b][c][1], tot[a][b][c][2], tot[a][b][c][3]); } } }
      if (s_dring_on) { static char ring[24][240]; static int rn; snprintf(ring[rn++ % 24], 240, "d%d t%d %d,%d hp%d/%d burn%d ooze%d %s | %s", G.depth, G.now / TICK, cx(G.hero.pos), cy(G.hero.pos), G.hero.HP, G.hero.HT, G.hero.b.burning, G.hero.ooze, s_did, s_last_log[(s_nlog - 1) & 7]);
        if (G.hero.HP <= 0) { printf("DEATHRING game %d:\n", g); for (int i = rn > 24 ? rn - 24 : 0; i < rn; i++) printf("   %s\n", ring[i % 24]); rn = 0; } }
      if (snapb) {
        size_t L = strlen(snapb); char *w = malloc(L + 400); int n = sprintf(w, "%s,\"hp_after\":%d,\"did\":\"", snapb, G.hero.HP);
        for (const char *q = s_did; *q; q++) if (*q != '"') w[n++] = *q;
        if (s_rec && !s_is_child) { w[n] = 0; fprintf(s_rec, "%s\",\"hp_now\":%d,\"depth_now\":%d,\"newlog\":[", w, G.hero.HP, G.depth);
          for (int i = nlog0; i < s_nlog; i++) { if (i < s_nlog - 8) continue; fputc('"', s_rec); for (const char *q = s_last_log[i & 7]; *q; q++) if (*q != '"' && *q != '\\') fputc(*q, s_rec); fprintf(s_rec, "\"%s", i + 1 < s_nlog ? "," : ""); }
          fprintf(s_rec, "]"); rec_extra(s_rec, g); fprintf(s_rec, "}\n"); fflush(s_rec); }
        strcpy(w + n, "\"}"); free(snapb);
        if (snap_on && !s_rec) { const char *dir = getenv("SNAPDIR") ? getenv("SNAPDIR") : "."; char fn[300]; snprintf(fn, sizeof fn, "%s/g%03d_s%06d.json", dir, g, steps); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%s\n", w); fclose(f); } }
        if (snap_death) { free(s_snapbuf[s_snap_n % 16]); s_snapbuf[s_snap_n++ % 16] = w; } else free(w);
      }
      if (snap_death && G.hero.HP <= 0) { const char *dir = getenv("SNAPDIR") ? getenv("SNAPDIR") : "."; int from = s_snap_n > 12 ? s_snap_n - 12 : 0;
        for (int i = from; i < s_snap_n; i++) { char fn[300]; snprintf(fn, sizeof fn, "%s/death_g%03d_%02d.json", dir, g, i - from); FILE *f = fopen(fn, "w"); if (f) { fprintf(f, "%s\n", s_snapbuf[i % 16]); fclose(f); } }
        s_snap_n = 0; }
      if (g_rnglog) { fprintf(g_rnglog, "after: now %d hero.time %d turn_due %d level_t0 %d night %d |", G.now, G.hero.time, G.turn_due, G.level_t0, G.night); for (int i = 0; i < G.n_mobs; i++) fprintf(g_rnglog, " %d:%d hp%d t%d", i, G.mobs[i].type, G.mobs[i].HP, G.mobs[i].time); fprintf(g_rnglog, "\n"); fclose(g_rnglog); g_rnglog = NULL; }
      if (s_is_child) continue;
      if (!did && getenv("MAP")) { dump_map(G.hero.pos); printf("   keys %x boss %x\n", G.keys[G.depth], G.boss); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf("   mob %s at %d st %d\n", mob_name(G.mobs[i].type), G.mobs[i].pos, G.mobs[i].state); }
      if (!did && getenv("MAP")) { bfs(G.hero.pos, false); for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W; putchar(c == G.hero.pos ? '@' : !BIT_GET(G.visited, c) ? ' ' : b_dist[c] < 0x7FFF ? '.' : PASSABLE(G.map[c]) ? 'x' : (TFLAGS(G.map[c]) & F_AVOID) ? '^' : '#'); } printf("\n"); } }
      if (!did) {                                       // a secret door that a search beside it should have found?
        for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_SECRET_DOOR && BIT_GET(s_searched, c)) {
          bool nearpass = false; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && BIT_GET(G.visited, n) && PASSABLE(G.map[n])) nearpass = true; }
          if (nearpass) viol("SEARCH_MISSED", "secret door at %d stayed hidden though searched beside it", c); }
        int sd = 0, sdv = 0; for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_SECRET_DOOR) { sd++; for (int k = 0; k < 8; k++) { int n = c + N8[k]; if (nb_ok(c, n) && BIT_GET(G.visited, n) && PASSABLE(G.map[n])) { sdv++; break; } } }
        if (getenv("D")) printf("  stuck: %d secret doors left, %d beside known ground\n", sd, sdv);
      }
      if (!did) { viol("BOT_STUCK", "nothing to do: exit %d (terrain %d, keys %x, hero %d, black %d at %d, mob %d) visited %d, searched %d spots", G.exit, G.map[G.exit], G.keys[G.depth], G.hero.pos, s_black[G.exit], s_bot_steps, mob_at(G.exit), BIT_GET(G.visited, G.exit), s_floor_searches); st_stuck++; break; }
      if (G.now == now0 && G.now == last_now) { if (++same_now == 300 && getenv("MAP")) { dump_map(G.hero.pos); printf("   hero %d terr %d roots %d para %d action %d time %d now %d trade %d:", G.hero.pos, G.map[G.hero.pos], G.hero.b.roots, G.hero.b.paralysis, G.action, G.hero.time, G.now, g_trade); for (int k = 0; k < 8; k++) { int n = G.hero.pos + N8[k]; printf(" %d(t%d)", n, G.map[n]); } ActResult rr = tap_safe(G.hero.pos + N8[0]); printf(" tap->%d pos %d time %d\n", rr, G.hero.pos, G.hero.time);  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf("   mob %s at %d st %d\n", mob_name(G.mobs[i].type), G.mobs[i].pos, G.mobs[i].state); }
        if (same_now == 300) viol("NO_PROGRESS", "300 decisions without time passing (last choice cell %d, pick %d, wnd %d)", s_last_choice_cell, s_pick, s_wnd_btn); }
      else same_now = 0;
      { static int lp = -1, ln = 0; static uint8_t sb[MAP_LEN]; if (G.hero.pos == lp && s_last_choice_cell != G.hero.pos && enemy_count() == 0 && !getenv("OLDUNSTICK2")) { if ((unsigned)s_last_choice_cell < MAP_LEN) sb[s_last_choice_cell] = 1; if (++ln % 24 == 0) {   // 0.54 (night lockout: every target failing in turn, the rest every 8 kept the counter above from ever reaching its blacklist)
            for (int c = 0; c < MAP_LEN; c++) if (sb[c]) s_black[c] = s_bot_steps + 200;
            for (int k = 0; k < 8; k++) { int n = G.hero.pos + N8[(k + ln / 24) & 7]; if (nb_ok(G.hero.pos, n) && PASSABLE(G.map[n]) && !(TFLAGS(G.map[n]) & F_AVOID) && !char_at(n) && n != G.entrance && n != G.exit && heap_at(n) < 0) { walk(game_tap_cell(n)); break; } } } }
        else { lp = G.hero.pos; ln = 0; memset(sb, 0, sizeof sb); } }
      if (same_now && same_now % 8 == 0 && !getenv("OLDUNSTICK") && G.hero.HP > 0) { game_rest(false); memset(s_black, 0, sizeof s_black); if (same_now >= 16 && (unsigned)s_last_choice_cell < MAP_LEN) s_black[s_last_choice_cell] = s_bot_steps + 60; }   /* (0.53: and the target it kept failing at: aside for a while) */   // 0.50 (review g11): nothing moved for 8 decisions (an NPC in the corridor): wait a turn, let it wander off
      if (same_now > 300) { st_stuck++; break; }
      last_now = G.now;
      if (s_floor_steps > 20000 && getenv("MAP")) { dump_map(G.hero.pos);
        printf("   keys %x boss %x bag %d/%d\n", G.keys[G.depth], G.boss, bag_used(), BAG_SIZE); for (int k = 0; k < G.n_heaps; k++) if (G.heaps[k].item.kind == IK_SKELETON_KEY && G.heaps[k].pos < MAP_LEN) { int kp = G.heaps[k].pos; bfs(G.hero.pos, false); printf("   key dist %d visited %d roots %d para %d; tap:", b_dist[kp], BIT_GET(G.visited, kp), G.hero.b.roots, G.hero.b.paralysis); ActResult rr = tap_safe(kp); int gd = 0; while (rr == ACT_WALK && gd++ < 200) rr = game_continue(); printf(" r %d hero %d keys %x\n", rr, G.hero.pos, G.keys[G.depth]); } for (int k = 0; k < G.n_heaps; k++) printf("   heap %d kind %d type %d black %d\n", G.heaps[k].pos, G.heaps[k].item.kind, G.heaps[k].type, G.heaps[k].pos < MAP_LEN ? s_black[G.heaps[k].pos] : -1); for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf("   mob %s at %d st %d hp %d\n", mob_name(G.mobs[i].type), G.mobs[i].pos, G.mobs[i].state, G.mobs[i].HP);
        for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W; putchar(c == G.hero.pos ? '@' : !BIT_GET(G.visited, c) ? ' ' : frontier(c) ? 'F' : PASSABLE(G.map[c]) ? '.' : '#'); } printf("\n"); } printf("   entrance %d exit %d hero %d spot %d\n", G.entrance, G.exit, G.hero.pos, s_spot); }
      if (s_floor_steps > 20000 && P) { printf("   keys %x hero %d\n", G.keys[G.depth], G.hero.pos); for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c]; putchar(c == G.hero.pos ? '@' : !BIT_GET(G.visited, c) ? ' ' : c == G.exit ? '>' : c == G.entrance ? '<' : heap_at(c) >= 0 ? '*' : t == T_LOCKED_DOOR ? 'L' : t == T_LOCKED_EXIT ? 'X' : t == T_DOOR ? '+' : PASSABLE(t) ? '.' : '#'); } putchar('\n'); } for (int y = 6; y < 16; y++) { printf("   "); for (int x = 22; x < 34; x++) printf("%3d%c", G.map[x + y * MAP_W], BIT_GET(G.visited, x + y * MAP_W) ? 118 : 32); printf("\n"); } }
      if (s_floor_steps > 20000) { viol("FLOOR_STUCK", "20000 decisions on this floor (goal %d, exit visited %d)", s_goal, BIT_GET(G.visited, G.exit)); st_stuck++; break; }
    }
    if (s_upg_child) { int r2[2] = { G.max_depth, s_won }; if (write(s_cf_out, r2, sizeof r2) != sizeof r2) {} _exit(0); }
    if (s_cf_child) cf_report(2);
    cf_kill(); s_cf_d0 = 0;
    s_upgcf_n = 0;
    bl_kill_all();
    if (s_is_child) _exit(0);
    if (s_fork_until >= 0) { close(s_fork_fd); int stt; kill(s_fork_pid, SIGKILL); waitpid(s_fork_pid, &stt, 0); s_fork_until = -1; }
    { struct itimerval it = { { 0, 0 }, { 0, 0 } }; setitimer(ITIMER_VIRTUAL, &it, NULL); }
    st_turns += G.now / TICK; st_floors += G.max_depth - 1;
    if (s_won) st_wins++;
    if (G.hero.HP <= 0) { /* 0.51 (owner): unused options at death: a death with the kit spent is a fair one */
      int u = 0, cls[6] = {0}; for (int sl = -1; sl < PACK0 + 32; sl = sl == BAG_SIZE - 1 ? PACK0 : sl + 1) { Item *it = game_slot(sl); int k = it->kind, c = item_cat(k); if (!k) continue;
        int w = c == CAT_WAND ? (it->ench > 0 ? 0 : -1) : c == CAT_MISSILE || k == IK_BOMB || k == IK_HONEYPOT ? 1 : (c == CAT_POTION && (!item_known(k) || harmful_potion(k) || k == IK_POT_HEALING || k == IK_POT_INVIS)) ? 2 : (c == CAT_SCROLL && (!item_known(k) || k == IK_SCR_TELEPORT || k == IK_SCR_TERROR || k == IK_SCR_LULLABY || k == IK_SCR_MIRROR || k == IK_SCR_PSIONIC)) ? 3 : (c == CAT_SEED && k != IK_SEED_ROTBERRY) ? 4 : -1;
        if (w >= 0) { u++; cls[w]++; } }
      st_unused += u; for (int q = 0; q < 5; q++) st_unused_c[q] += cls[q]; if (u == 0) st_unused0++; }
    if (G.hero.HP <= 0) { st_deaths++; st_deaths_d[G.depth]++; snprintf(st_deathmsg[G.depth], 64, "%s", G.death); }
    if (G.dflags & DF_AMULET) st_amulet++;
    st_maxd[G.max_depth]++;
    if (s_zone_start) { int zi = s_zone_start / 5; if (G.max_depth >= s_zone_start + 5) st_zone_clear[zi]++; st_zone_depth[zi] += G.max_depth - s_zone_start; }
    for (int d = 5; d <= 25; d += 5) if (G.max_depth > d) st_boss_kill[d]++;
    st_qs[0][G.q.ghost]++; st_qs[1][G.q.wand]++; st_qs[2][G.q.smith]++; st_qs[3][G.q.imp]++;
    if (G.hero.HP <= 0) ledger_death();
    if (getenv("D")) { int fd = 0; for (int i = 0; i < BAG_SIZE; i++) if (item_cat(G.hero.bag[i].kind) == CAT_FOOD) fd += G.hero.bag[i].qty; printf("DEATHINFO g%d hunger %d food %d depth %d turn %d\n", g, G.hero.hunger, fd, G.depth, G.now / TICK); }
    if (getenv("D")) printf("game %d cls %d: %s depth %d (max %d) lvl %d turns %d steps %d HP %d/%d STR %d arm %d+%d wpn %d+%d quests %d%d%d%d%s%s\n", g, g_new_class, s_won ? "WON" : G.hero.HP <= 0 ? "died" : "stopped",
                           G.depth, G.max_depth, G.hero.lvl, G.now / TICK, steps, G.hero.HP, G.hero.HT, G.hero.STR, G.hero.armor.kind, G.hero.armor.lvl, G.hero.weapon.kind, G.hero.weapon.lvl,
                           G.q.ghost, G.q.wand, G.q.smith, G.q.imp, G.hero.HP <= 0 ? ": " : "", G.hero.HP <= 0 ? G.death : "");
    if (getenv("DUMPEND")) { printf("  END MAP d%d hero %d,%d exit %d,%d:\n", G.depth, cx(G.hero.pos), cy(G.hero.pos), cx(G.exit), cy(G.exit)); for (int y = 0; y < MAP_H; y++) { printf("   "); for (int x = 0; x < MAP_W; x++) { int c = x + y * MAP_W, t = G.map[c]; char ch = !BIT_GET(G.visited, c) ? (t == T_SECRET_DOOR ? 's' : ' ') : c == G.hero.pos ? '@' : t == T_SECRET_DOOR ? 'S' : t == T_WALL || t == T_WALL_DECO ? '#' : t == T_DOOR || t == T_OPEN_DOOR ? '+' : t == T_LOCKED_DOOR ? 'L' : t == T_LOCKED_EXIT ? 'X' : t == T_EXIT ? '>' : t == T_ENTRANCE ? '<' : (TFLAGS(t) & F_PIT) ? ':' : IS_WATER(t) ? '~' : (TFLAGS(t) & F_AVOID) ? '^' : PASSABLE(t) ? '.' : '?'; if (heap_at(c) >= 0 && BIT_GET(G.visited, c)) ch = '*'; putchar(ch); } putchar('\n'); } for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0) printf("   mob %s at %d,%d st %d\n", mob_name(G.mobs[i].type), cx(G.mobs[i].pos), cy(G.mobs[i].pos), G.mobs[i].state); }
    if (getenv("RESPAWN")) { printf("  respawns %d:%s\n", s_resp_n, s_resp_log); } s_resp_n = 0; s_resp_log[0] = 0;
    if (getenv("D")) { Item *a = &G.hero.armor, *w = &G.hero.weapon; printf("  worn: armour %s%d%s, weapon %s%d%s, rings %s%s; SoU spawned %d read %ld in bag %d; STR potions spawned %d; hunger at end %d food %d; broken steps w %ld a %ld of %ld\n", a->lvl >= 0 ? "+" : "", a->lvl, (a->flags & IF_CURSED) ? " CURSED" : "", w->lvl >= 0 ? "+" : "", w->lvl, (w->flags & IF_CURSED) ? " CURSED" : "", (G.hero.ring[0].flags & IF_CURSED) && G.hero.ring[0].kind ? "CURSED " : "", (G.hero.ring[1].flags & IF_CURSED) && G.hero.ring[1].kind ? "CURSED" : "", G.sou_count, st_sou_read, find_kind(IK_SCR_UPGRADE) >= 0 ? game_slot(find_kind(IK_SCR_UPGRADE))->qty : 0, G.pos_count, G.hero.hunger, food_budget(), st_brkw, st_brka, st_brkn); st_brkw = st_brka = st_brkn = 0; }
  }
  printf("strategy: %ld upgrade decisions\n", st_sla_points);
  printf("look-ahead: %ld moments, %ld changed, %ld playouts\n", st_la_points, st_la_changed, st_la_forks);
  printf("upgrades held %ld times; scrolls stashed for a potion test %ld drops, %ld stashes left behind on a floor\n", st_upg_holds, st_stash, st_stash_lost);
  printf("== %d games: won %d, died %d, stuck %d, amulet taken %d, avg turns %d, longest walk %d, round trips %d, fork checks %d\n", st_games, st_wins, st_deaths, st_stuck, st_amulet, st_turns / (st_games ? st_games : 1), s_walk_max, st_rt, st_fork);
  if (getenv("ZONES")) { double prod = 1; printf("zones:"); for (int z = 0; z < 5; z++) { double r = st_zone_n[z] ? (double)st_zone_clear[z] / st_zone_n[z] : 0; prod *= r; printf(" d%d %d/%d (%.0f%%, %.1f floors)", 1 + 5 * z, st_zone_clear[z], st_zone_n[z], 100 * r, st_zone_n[z] ? (double)st_zone_depth[z] / st_zone_n[z] : 0); } printf("; all five: %.2f%%\n", 100 * prod); }
  printf("max depth:"); for (int d = 1; d <= 26; d++) if (st_maxd[d]) printf(" %d:%d", d, st_maxd[d]); printf("\n");
  printf("unused options at death: %.2f per death (wands %d, missiles/bombs %d, potions %d, scrolls %d, seeds %d); deaths with none left %d; hurt-retreats from a wanderer %d; losing-duel kit used: wand %d curare %d missile %d unknown wand %d scroll %d potion/bomb %d; free throws %d; stair flees %d\n", st_deaths ? (double)st_unused / st_deaths : 0, st_unused_c[0], st_unused_c[1], st_unused_c[2], st_unused_c[3], st_unused_c[4], st_unused0, st_prep, st_kit[0], st_kit[1], st_kit[2], st_kit[3], st_kit[4], st_kit[6], st_kit[7], st_kit[5]);
  printf("deaths by depth:"); for (int d = 1; d <= 26; d++) if (st_deaths_d[d]) printf(" %d:%d", d, st_deaths_d[d]); printf("\n");
  printf("bosses passed: d5 %d d10 %d d15 %d d20 %d d25 %d\n", st_boss_kill[5], st_boss_kill[10], st_boss_kill[15], st_boss_kill[20], st_boss_kill[25]);
  static const char *QN[4] = { "ghost", "wandmaker", "blacksmith", "imp" };
  for (int q = 0; q < 4; q++) printf("%s: none %d spawned %d given %d done %d rewarded %d\n", QN[q], st_qs[q][0], st_qs[q][1], st_qs[q][2], st_qs[q][3], st_qs[q][4]);
  printf("search walk steps %d; fast-enemy step backs %d; search phase turns %ld floors %d; trap rooms opened %d (throws %d); backed out of a crowd %d; quest bounces %d; evidence of a hidden room: floors %d, searches %d, resolved %d; altar: lure moves %d, completed %d, Wipe Out read %d; barricades lit %d; seeds thrown in fights %d; pit jumps %d; door kites %d; class specials %d; XP bounces before a boss %d; thief chase steps %d, corner waits %d; trap prizes %d; boomerang throws %d; rush steps %d\n", st_srch_w, st_fastback, st_sp_turns, st_sp_n, st_trap_rooms, st_trap_throws, st_surr, st_bounce, st_ev_floors, st_ev_search, st_ev_found, st_altar_moves, st_altar_done, st_wipe, st_barr, st_seedthrow, st_pitjump, st_kite, st_special, st_xpb, st_thief_chase, st_lurk, st_trapgrab, st_boom, st_rush); printf("explore: target list %d, own search %d, traps stepped on %d, searches %d; wells %d, pots %d, buys %d, sells %d, ankhs %d; detours %d\n", st_explore_tgt, st_explore_bfs, st_traps, st_search, st_wells, st_pot, st_buys, st_sells, st_ankh, st_detour);
  printf("actions:"); for (int a = 0; a < ACN_COUNT; a++) if (st_uses[a]) printf(" %s %d", ACTION_NAMES[a], st_uses[a]); printf("\n");
  printf("talks:"); for (int t = 0; t < MT_COUNT; t++) if (st_talks[t]) printf(" %s %d", mob_name(t), st_talks[t]); printf("; bad zaps %d\n", s_bad_zap);
  printf("quests left under way: ghost rose/rat/curse %d/%d/%d, wandmaker rotberry/dust/fish %d/%d/%d, smith gold/blood %d/%d, imp golems/monks %d/%d\n",
         st_quest_left[0][0], st_quest_left[0][1], st_quest_left[0][2], st_quest_left[1][0], st_quest_left[1][1], st_quest_left[1][2], st_quest_left[2][0], st_quest_left[2][1], st_quest_left[3][0], st_quest_left[3][1]);
  printf("arrivals onto a creature on the stairs (PD quirk, not reported): %d checks\n", st_overlap);
  printf("amulet thief: hunting decisions %d, amulet got back %d times\n", st_thief_hunt, st_thief_back);
  if (getenv("TT")) printf("tgt build: %ld calls, avg %.1f us, max %.1f us; over 25/50/100/200/400/800 us: %ld %ld %ld %ld %ld %ld\n", tt_n, tt_sum / 1000.0 / (tt_n ? tt_n : 1), tt_max / 1000.0, tt_h[0], tt_h[1], tt_h[2], tt_h[3], tt_h[4], tt_h[5]);
  if (s_toe) printf("toe: upgrades planned %ld, held %ld, bought %ld, kept to grow into %ld, kept for the smith %ld\n", st_toe_upg, st_toe_hold, st_toe_buy, st_toe_keep, st_toe_smith);
  if (ct_trips) { long t = 0; for (int i = 0; i < 12; i++) t += ct_n[i]; printf("contra audit: %d trips (%d arrived, %d gave up), %ld hops judged:", ct_trips, ct_arrived, ct_abort, t); for (int i = 0; i < 12; i++) if (ct_n[i]) printf(" %s %ld (%.1f%%)", CT_NAME[i], ct_n[i], 100.0 * ct_n[i] / t); printf("\n");
    static const char *MN[4] = { "far explored cell", "back to the way up", "the way down early", "another unexplored part" };
    for (int m = 0; m < 4; m++) { long t = 0; for (int i = 0; i < 12; i++) t += ct_mn[m][i]; if (t) printf("  %s: %d trips, %ld hops: with %.1f%% beside %.1f%% AGAINST %.1f%% item against %.1f%% stand %.1f%% hands %.1f%%\n", MN[m], ct_mtrips[m], t, 100.0 * ct_mn[m][0] / t, 100.0 * ct_mn[m][1] / t, 100.0 * ct_mn[m][2] / t, 100.0 * ct_mn[m][5] / t, 100.0 * ct_mn[m][6] / t, 100.0 * ct_mn[m][7] / t); } }
  if (P) printf("assist audit: steps straight back %d (an enemy in sight %d); dithering (a cell a third time in 24 moves, nothing new seen, no enemy) %d\n", st_turnback, st_turnback_foe, st_dither);
  if (P && getenv("NEAR")) printf("far audit: %d presses going somewhere with something unexplored reachable, objective far beyond the nearest: %d; flips (A, B, A, nothing new seen): %d; meals while starving %d, of them with the floor done at half health or more: %d\n", st_far_n, st_far, st_flip, st_eatst_n, st_eatst);
  if (P && getenv("NEAR")) printf("near audit: %d presses with an unexplored spot within 3, entry 0 elsewhere far: %d (that spot not in the list at all: %d)\n", st_near_n, st_near_skip, st_near_any);
  if (P) printf("assist audit: searches %d (with a foe hunting %d, with the way down known %d); hungry, no food, food within 3: %d presses, not entry 0: %d\n", st_srch, st_srch_foe, st_srch_exit, st_food_near, st_food_left);
  if (P) printf("entry 0 off screen (no fight/item/override): %d, of them while an on-screen frontier existed: %d; tall grass trampled %d (%.1f a floor), left behind %d\n", st_offf_n, st_offf, st_tramp, st_tramp / (double)(st_floors ? st_floors : 1), st_grass_left);
  ledger_print();
  if (getenv("SNEAK") || getenv("TACTBOT") || getenv("DEWREST")) printf("owner ideas: sneak steps %d, door trick moves %d, dew rests %d\n", st_sneak, st_door, st_dewrest);
  printf("food: energy eaten %ld (%.1f rations), wasted %ld; warrior emergency meals %d; paced descents %d\n", st_eat_energy, st_eat_energy / 260.0, st_eat_waste, st_warreat, st_pace);
  printf("floors cleared %d (%.1f a game); decisions %d, presses %d (%.1f a floor), entry 0 off screen %d, oscillations %d\n", st_floors, st_floors / (double)(st_games ? st_games : 1), st_press_n, st_press, st_press / (double)(st_floors ? st_floors : 1), st_press_off, st_osc);
  printf("veins: mined %d, walks to one %d (did nothing %d)\n", st_mine, st_mine_walk, st_mine_none);
  printf("fallbacks (entry 0 did nothing) %d; waits for an enemy a step short %d (longest run %d)\n", st_fb, st_hold, st_hold_max);
  if (P == 2) printf("step backs from an enemy %d, then: attack %d, wait %d, ADVANCE %d, other %d\n", st_backs, st_back_fight, st_back_hold, st_back_adv, st_back_other);
  if (P == 2) printf("overrides %d (g_ovr.c set %d); watched presses after: %d, pointing back %d; while the way was on: %d, pointing back %d (of them before the chosen cell was reached: %d, back %d)\n", st_ovr, st_ovr_set, st_watch8, st_turn8, st_on_pr, st_turn_on, st_pend_pr, st_turn_pend);
  printf("violations: %d total\n", total_viol);
  for (int k = 0; k < n_vk; k++) printf("  %-20s %6d times in %d games\n", vk_name[k], vk_count[k], vk_games[k]);
  return 0;
}
