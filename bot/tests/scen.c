// scen: the javapd scenarios (tools/javapd/sim/sim/Sim.java) against the C port, same setup and same printout.
//   ./scen melee rat|crab [runs] | burn door|grass|floor [runs] | gas [runs]   [--seed N] [--asleep] [--fire N] [--amount N] [--trace]
// Build: OUT=scen MAIN=scen.c ./build.sh
#include "pd.h"
#include "game_int.h"
#include <unistd.h>
PKey g_keys[256]; int g_persist_fail;
static int TRACE;
static int JTIES;   // --jties (diagnostic, not the default): per run, coin-flip the tie orders that Java's HashSet leaves to identity hashes
void ui_log(const char *m, MsgColor c) { if (TRACE) printf("    log: %s\n", m); }
// every hero_damage call (HP > 0 before) shows FX_DAMAGE on the hero with the amount: Sim.SimHero.damage's log
static long s_dmg_total, s_dmg_events;
void ui_fx(FxKind k, int16_t c, int w, const char *t) {
  if (k == FX_DAMAGE && w == -1) {
    int d = atoi(t), before = G.hero.HP + d;   // (HP is already lowered here, not yet set to 0 by a death)
    if (d > before) d = before;                     // SimHero counts before - HP, and Char.die -> destroy sets HP = 0
    s_dmg_total += d; s_dmg_events++;
    if (TRACE) printf("    hero takes %d -> HP %d\n", d, G.hero.HP);
  }
}
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
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_free_cache(int m) {}
void ui_wnd_info(const Item *it) {}

// ---------------- TestLevel: ASCII art at (8, 8) of the 32x32 map, the rest wall ----------------
#define OX 8
#define OY 8
static const char **s_art; static int s_rows;
static int cell_of(char ch) {
  for (int y = 0; y < s_rows; y++) { const char *p = strchr(s_art[y], ch); if (p) return (OY + y) * MAP_W + OX + (int)(p - s_art[y]); }
  return -1;
}
static uint32_t s_seed; static int s_run;
// Sim.newGame + Sim.start: a fresh warrior (game_new: HeroClass.initHero), depth 1, the test level, hero on '@'
static void new_game(const char **art, int rows, char hero_tile) {
  g_new_class = 0; game_new();
  s_art = art; s_rows = rows;
  blob_free_all();
  G.depth = 1; G.n_mobs = 0; G.n_heaps = 0; G.n_plants = 0; G.n_diffs = 0;
  memset(G.hg, 0, sizeof G.hg); memset(G.visited, 0, sizeof G.visited);
  for (int i = 0; i < MAP_LEN; i++) G.map[i] = T_WALL;
  for (int y = 0; y < rows; y++) for (int x = 0; art[y][x]; x++) {
    char ch = art[y][x]; if (ch == '@') ch = hero_tile;
    int t;
    switch (ch) {
      case '#': case ' ': t = T_WALL; break;
      case '+': t = T_DOOR; break;
      case '"': t = T_GRASS; break;
      case 'G': t = T_HIGH_GRASS; break;
      case '~': t = T_WATER; break;
      case 'E': t = T_EMBERS; break;
      default: t = T_EMPTY;
    }
    G.map[(OY + y) * MAP_W + OX + x] = t;
  }
  build_los();
  // one room (not STANDARD: the respawner finds nowhere, as TestLevel's nMobs() = 0 spawns nothing)
  G.n_rooms = 1; G.rooms[0] = (Room){ OX, OY, OX + 8, OY + rows - 1, RT_NULL, -1 };
  G.entrance = G.exit = cell_of('@');
  G.well_pos = G.altar_pos = G.health_pos = -1; G.boss = 0; G.dflags = 0; memset(G.lvars, 0, sizeof G.lvars);
  G.night = 0;                                    // nightMode = false
  // Actor.init: the hero acts first; everything else (buffs, blobs, mobs) from time 0
  G.now = 0; G.turn_due = 0; G.level_t0 = -TICK; G.hero.time = 0;
  G.hero.pos = G.entrance;
  G.action = HA_NONE; G.interrupted = false; G.last_action = HA_NONE; G.last_target = -1; G.seen_mobs = 0;
  g_rng = mix(s_seed, (uint32_t)s_run, 0x51u) | 1;
  // --jties: Java's Regeneration first acts at t=0 (then every 10 turns), before or after the other t=0 actors by hash order;
  // the port's starts at 10 turns. Emulated: heal at t=0 before anything (useless at full HP), or at t=1 before the rest
  // (= at t=0 after them), and every 10 turns from there.
  if (JTIES) G.regen_left = rnd_int(2) ? 2 * TICK : TICK;
  s_dmg_total = s_dmg_events = 0;
}
static void start(void) { update_fov(); check_visible_mobs(); G.interrupted = false; }

static const char *ROOM7[] = {
  "#########",
  "#.......#",
  "#.......#",
  "#.......#",
  "#..@a...#",
  "#.......#",
  "#.......#",
  "#.......#",
  "#########",
};
static void melee(const char *kind, int runs, bool awake) {
  int type = !strcmp(kind, "rat") ? MT_RAT : !strcmp(kind, "crab") ? MT_CRAB : !strcmp(kind, "gnoll") ? MT_GNOLL : -1;
  if (type < 0) { printf("unknown mob %s\n", kind); exit(1); }
  int wins = 0, deaths = 0, timeouts = 0;
  long sumTurns = 0, sumTurnsWin = 0, sumHpLeftWin = 0, sumHeroDmg = 0, sumHeroHits = 0;
  int hpHist[64] = { 0 }, heroHT = 0;
  char mobName[40] = "";
  for (s_run = 0; s_run < runs; s_run++) {
    new_game(ROOM7, 9, '.');
    int m = spawn_mob(type, cell_of('a'), awake ? ST_HUNTING : ST_SLEEPING, 0);
    G.mobs[m].uid = (uint8_t)(rnd_int(250) + 1);   // (a different creature each run: its fixed place at equal times with the hero, as Java's identity hash)
    if (JTIES) {                                  // the port runs a creature before the hero at equal times, the turn's buffs before both
      if (rnd_int(2)) G.mobs[m].time += 1;        // hero wins its ties with the mob (1/60 of a turn later)
    }
    snprintf(mobName, sizeof mobName, "%s", mob_name(type));
    start();
    heroHT = G.hero.HT;
    if (TRACE) printf("run %d: hero HP %d/%d vs %s HP %d\n", s_run, G.hero.HP, G.hero.HT, mobName, G.mobs[m].HP);
    int turns = 0;
    while (turns < 500) {
      if (G.hero.HP <= 0 || G.mobs[m].HP <= 0) break;
      turns++;
      if (TRACE) printf("  hero action %d: hero HP %d, %s HP %d\n", turns, G.hero.HP, mobName, G.mobs[m].HP);
      if (adjacent(G.hero.pos, G.mobs[m].pos)) game_tap_cell(G.mobs[m].pos); else game_rest(false);
    }
    sumTurns += turns; sumHeroDmg += s_dmg_total; sumHeroHits += s_dmg_events;
    if (G.hero.HP <= 0) deaths++;
    else if (G.mobs[m].HP <= 0) { wins++; sumTurnsWin += turns; sumHpLeftWin += G.hero.HP; hpHist[G.hero.HP < 63 ? G.hero.HP : 63]++; }
    else timeouts++;
  }
  printf("MELEE %s vs %s (%s), runs=%d\n", "warrior", mobName, awake ? "mob awake/hunting" : "mob asleep", runs);
  printf("  hero win rate     : %.2f%%  (wins %d, deaths %d, timeouts %d)\n", 100.0 * wins / runs, wins, deaths, timeouts);
  printf("  mean hero actions : %.3f (all runs), %.3f (wins)\n", (double)sumTurns / runs, wins > 0 ? (double)sumTurnsWin / wins : 0);
  printf("  mean HP left (win): %.3f / %d\n", wins > 0 ? (double)sumHpLeftWin / wins : 0, heroHT);
  printf("  mean dmg taken    : %.3f in %.3f hits/run\n", (double)sumHeroDmg / runs, (double)sumHeroHits / runs);
  printf("  HP-left histogram (wins): ");
  for (int i = 0; i < 64; i++) if (hpHist[i]) printf("%d:%d ", i, hpHist[i]);
  printf("\n");
}

static const char *BURN_DOOR[] = {
  "#########",
  "#.......#",
  "#.......#",
  "#.......@....",
  "#.......#",
  "#.......#",
  "#########",
};
static const char *BURN_GRASS[] = {
  "#########",
  "#.......#",
  "#..\"\"\"..#",
  "#..\"@\"..#",
  "#..\"\"\"..#",
  "#.......#",
  "#########",
};
static const char *BURN_FLOOR[] = {
  "#########",
  "#.......#",
  "#.......#",
  "#...@...#",
  "#.......#",
  "#.......#",
  "#########",
};
static bool fire_left(void) {
  if (!g_blob[BLOB_FIRE]) return false;
  for (int i = 0; i < MAP_LEN; i++) if (g_blob[BLOB_FIRE][i]) return true;
  return false;
}
static void burn(const char *where, int runs, int fireSeed) {
  const char **art; char tile;
  if (!strcmp(where, "door")) { art = BURN_DOOR; tile = '+'; }
  else if (!strcmp(where, "grass")) { art = BURN_GRASS; tile = '"'; }
  else if (!strcmp(where, "floor")) { art = BURN_FLOOR; tile = '.'; }
  else { printf("unknown %s\n", where); exit(1); }
  long sumBurnTicks = 0, sumDmg = 0, sumTurns = 0, sumFireDmg = 0; int deaths = 0;
  int tickHist[200] = { 0 }, terrainBefore = -1, terrHist[64] = { 0 };
  for (s_run = 0; s_run < runs; s_run++) {
    new_game(art, 7, tile);
    start();
    int pos = G.hero.pos;
    hero_press(pos);                                // Level.press(pos, hero): DOOR -> OPEN_DOOR
    if (s_run == 0) terrainBefore = G.map[pos];
    affect_burning(-1);                             // Buff.affect(hero, Burning).reignite(hero)
    if (fireSeed > 0) blob_seed(BLOB_FIRE, pos, fireSeed);
    int turns = 0;
    while (turns < 300) {
      if (G.hero.HP <= 0) break;
      if (!G.hero.b.burning && !fire_left()) break;
      game_rest(false);
      turns++;
    }
    int ticks = (int)s_dmg_events;                  // every hero damage here is Burning's
    sumBurnTicks += ticks; tickHist[ticks < 199 ? ticks : 199]++;
    sumDmg += s_dmg_total; sumTurns += turns; sumFireDmg += s_dmg_total;
    if (G.hero.HP <= 0) deaths++;
    terrHist[G.map[pos]]++;
    if (TRACE) printf("run %d: burn ticks %d, dmg %ld, turns %d, HP %d/%d\n", s_run, ticks, s_dmg_total, turns, G.hero.HP, G.hero.HT);
  }
  printf("BURN hero(%s) standing on %s, initial Fire seed %d, resting; runs=%d\n", "warrior", where, fireSeed, runs);
  printf("  mean burn ticks (Burning dmg events): %.3f\n", (double)sumBurnTicks / runs);
  printf("  mean Burning damage                 : %.3f\n", (double)sumFireDmg / runs);
  printf("  mean total damage taken             : %.3f\n", (double)sumDmg / runs);
  printf("  mean hero turns until fire out      : %.3f   deaths: %d\n", (double)sumTurns / runs, deaths);
  printf("  burn-ticks histogram: ");
  for (int i = 0; i < 200; i++) if (tickHist[i]) printf("%d:%d ", i, tickHist[i]);
  printf("\n  terrain under hero: before=%d, afterwards (Terrain id:count) {", terrainBefore);
  bool first = true;
  for (int i = 0; i < 64; i++) if (terrHist[i]) { printf("%s%d=%d", first ? "" : ", ", i, terrHist[i]); first = false; }
  printf("}   [1=EMPTY 2=GRASS 5=DOOR 6=OPEN_DOOR 9=EMBERS]\n");
}

static const char *GAS_ROOM[] = {
  "#########",
  "#.......#",
  "#.......#",
  "#.......#",
  "#.@.a...#",
  "#.......#",
  "#.......#",
  "#.......#",
  "#########",
};
static void gas(int runs, int amount) {
  long sumDmg = 0, sumHits = 0, sumTurns = 0; int deaths = 0;
  int hist[200] = { 0 };
  for (s_run = 0; s_run < runs; s_run++) {
    new_game(GAS_ROOM, 9, '.');
    start();
    blob_seed(BLOB_TOXIC, cell_of('a'), amount);
    int turns = 0;
    while (turns < 1000) {
      if (G.hero.HP <= 0) break;
      if (!g_blob[BLOB_TOXIC]) break;                // freed once its volume is 0
      game_rest(false);
      turns++;
    }
    int dmg = (int)s_dmg_total;                     // every hero damage here is ToxicGas's
    sumDmg += dmg; sumHits += s_dmg_events; sumTurns += turns;
    hist[dmg < 199 ? dmg : 199]++;
    if (G.hero.HP <= 0) deaths++;
    if (TRACE) printf("run %d: gas dmg %d in %ld hits, turns %d, HP %d/%d\n", s_run, dmg, s_dmg_events, turns, G.hero.HP, G.hero.HT);
  }
  printf("TOXIC GAS seed %d at room centre, hero(%s) 2 cells away resting; runs=%d\n", amount, "warrior", runs);
  printf("  mean ToxicGas damage : %.3f in %.3f hits\n", (double)sumDmg / runs, (double)sumHits / runs);
  printf("  mean turns until gas gone: %.3f   deaths: %d\n", (double)sumTurns / runs, deaths);
  printf("  damage histogram: ");
  for (int i = 0; i < 200; i++) if (hist[i]) printf("%d:%d ", i, hist[i]);
  printf("\n");
}

int main(int argc, char **argv) {
  const char *pos[4]; int np = 0;
  bool awake = true; int fireSeed = 0, gasAmount = 300;
  s_seed = (uint32_t)time(NULL) ^ (uint32_t)getpid() * 2654435761u;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--seed")) s_seed = (uint32_t)atol(argv[++i]);
    else if (!strcmp(argv[i], "--trace")) TRACE = 1;
    else if (!strcmp(argv[i], "--jties")) JTIES = 1;
    else if (!strcmp(argv[i], "--asleep")) awake = false;
    else if (!strcmp(argv[i], "--fire")) fireSeed = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--amount")) gasAmount = atoi(argv[++i]);
    else if (np < 4) pos[np++] = argv[i];
  }
  if (!np) { printf("usage: scen melee <rat|crab> [runs] | burn <door|grass|floor> [runs] | gas [runs]\n"); return 1; }
  game_init();
  if (!strcmp(pos[0], "melee")) melee(pos[1], np > 2 ? atoi(pos[2]) : 2000, awake);
  else if (!strcmp(pos[0], "burn")) burn(pos[1], np > 2 ? atoi(pos[2]) : 2000, fireSeed);
  else if (!strcmp(pos[0], "gas")) gas(np > 1 ? atoi(pos[1]) : 2000, gasAmount);
  else printf("unknown scenario %s\n", pos[0]);
  return 0;
}
