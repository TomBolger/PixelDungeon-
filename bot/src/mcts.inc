// ---- mcts(): the search (Thomas 2026-11-08: "play like Stockfish: we have the opponent's source code") ----
// No model of the monsters: the game itself is the model. Each iteration forks a copy of the game, cut down to what the
// player knows (la_scrub: only creatures in sight, hidden traps and doors as they look, unknown items drawn from their
// odds, fresh dice), walks down the tree of the hero's own moves (Monte Carlo tree search, UCT), lets the real game play
// every monster's answer, adds one new move at the bottom, and scores where it ended with the judge (judge_value: the
// chance of getting past the next boss from that state, fitted from 44k floor arrivals, tools/judge.py). The move tried
// most at the root is played. Every move the hero has is in the tree: each step (or blow) in 8 directions, waiting,
// each potion, scroll, seed and food, each throw and zap at each creature in sight, the stairs underfoot. Nothing says
// what to do when: door ambushes, pump dodges or potion timing are found by the search or not at all.
static const double JW5[21] = { 0.7115, 1.3105, -0.0862, -0.3190, -0.2242, 0.3993, 0.7872, 0.1268, 0.2990, 0.1609, 0.5094, 0.4987, 0.0625, 0.9900, 0.0888, -0.0298, -4.0813, -4.4791, -5.2210, -5.7416, -6.1904 };
static const double JW10[21] = { 0.2312, 1.0950, 0.0150, -0.1917, -0.1430, 0.2337, 0.5004, 0.0622, 0.1367, 0.4663, 0.2730, 0.3393, 0.0530, 0.2478, 0.1651, -0.0305, -6.6390, -7.7626, -8.0628, -8.3067, -8.5725 };
static double judge_value(double hp) {                 // hp: the HP to judge with (the fight still owed taken off)
  Hero *h = &G.hero; if (hp <= 0) return 0;
  int nfood = 0, nheal = 0, nunk = 0;
  for (int s = 0; s < BAG_SIZE; s++) { Item *it = &h->bag[s]; int k = it->kind; if (!k) continue; int q = it->qty > 0 ? it->qty : 1;
    if (is_food(k)) nfood += q; else if (k == IK_POT_HEALING && item_known(k)) nheal += q; else if (item_cat(k) == CAT_POTION && !item_known(k)) nunk += q; }
  int vs = find_kind(IK_DEW_VIAL), ak = h->armor.kind ? item_tier(h->armor.kind) : 0;
  double f[21] = { (double)h->lvl, hp / (h->HT > 0 ? h->HT : 1), (double)h->HT, h->hunger >= 360, h->hunger >= 260 && h->hunger < 360, nfood < 3 ? nfood : 3, nheal < 3 ? nheal : 3, nunk < 5 ? nunk : 5,
    (double)hero_str(), (double)ak, h->armor.kind ? (h->armor.lvl > -2 ? h->armor.lvl : -2) : 0, h->weapon.kind ? (h->weapon.lvl > -2 ? h->weapon.lvl : -2) : 0, (double)G.dew, G.dew >= 10 && vs >= 0, vs >= 0, (G.now / TICK) / 100.0 };
  int d = G.depth; const double *W = d <= 5 ? JW5 : JW10; int slot = d <= 5 ? d - 1 : (d <= 10 ? d - 6 : 4); f[16 + slot] = 1;
  double z = 0; for (int i = 0; i < 21; i++) z += W[i] * f[i];
  return 1 / (1 + exp(-z)); }
// the judge after n more turns at hp (resting, say): hunger and the clock move on; past starving, a meal from the bag is eaten
static double judge_after(double hp, int n) {
  Hero *h = &G.hero; int hu0 = h->hunger; int32_t now0 = G.now; int fs = -1;
  h->hunger += n; G.now += n * TICK;
  if (h->hunger >= 360) for (int s2 = 0; s2 < BAG_SIZE; s2++) if (is_food(h->bag[s2].kind) && h->bag[s2].qty > 0) { fs = s2; h->bag[s2].qty--; h->hunger -= h->bag[s2].kind == IK_PASTY ? 360 : h->bag[s2].kind == IK_RATION ? 260 : 100; if (h->hunger < 0) h->hunger = 0; break; }
  int k0 = fs >= 0 ? h->bag[fs].kind : 0; if (fs >= 0 && h->bag[fs].qty == 0) h->bag[fs].kind = 0;
  double v = judge_value(hp);
  if (fs >= 0) { h->bag[fs].kind = k0; h->bag[fs].qty++; }
  h->hunger = hu0; G.now = now0; return v; }
// judge2 (2026-11-12, tools/judge2.py on 88,765 snapshots of the old code's play every 25 turns): the value of a position mid-floor,
// mid-fight = P(past the next boss) x (1 - P(dead within 50 turns)). Features: health, the awake foes in sight (their HP over my
// damage, their damage), food, potions, gear, the dew, the floor seen, the floor's turns. Its check: full health beside a rat 0.53,
// 4 HP beside a crab 0.16 (the first judge, fitted at floor arrivals only, couldn't tell them apart through the search's noise)
static const double J2_PB[51] = { 1.37308, -0.56017, -0.34519, -0.06929, 0.02486, 0.15776, 0.18194, 0.02862, -0.24492, -0.11590, 0.22680, 0.48044, 0.09516, 0.15789, 0.23769, 0.20188, 0.53411, 0.32267, 0.07810, 0.26317, 0.17373, -0.06110, -0.13600, -0.00241, -0.02351, 3.17284, 2.70709, 2.13493, 1.79003, 0.00000, -0.66364, -1.19378, -1.68865, -1.92249, 0.00000, -4.98660, -5.24522, -5.25024, -5.08780, 0.00000, -14.02703, -14.44167, -14.89316, -14.97434, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000, -7.36915 };
static const double J2_D50[51] = { -7.28120, 3.19619, 0.53419, -0.27250, 0.39590, 2.47697, -0.11181, -0.02295, 0.03597, 0.05281, 0.01699, -0.70470, -0.07134, -0.11692, -0.33235, -0.10910, -0.13884, -0.21596, -0.15797, -0.79075, 0.14553, -0.37186, -0.02327, -0.06083, 0.00987, -2.78311, -1.67932, -0.93904, -0.76228, 0.00000, 0.11464, 0.52491, 1.14453, 1.64076, 0.00000, 2.50990, 3.61332, 3.77858, 4.43387, 0.00000, 5.85710, 7.17044, 7.24158, 7.41281, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000, 3.70580 };
static double judge2(void) {
  Hero *h = &G.hero; if (h->HP <= 0) return 0;
  int nfood = 0, nheal = 0, nunk = 0;
  for (int s2 = 0; s2 < BAG_SIZE; s2++) { Item *it = &h->bag[s2]; int k = it->kind; if (!k) continue; int q = it->qty > 0 ? it->qty : 1;
    if (is_food(k)) nfood += q; else if (k == IK_POT_HEALING && item_known(k)) nheal += q; else if (item_cat(k) == CAT_POTION && !item_known(k)) nunk += q; }
  int nf = 0; double owe = 0, fdpt = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state == ST_PASSIVE || m->state == ST_SLEEPING || (MOB_DEFS[m->type].flags & MD_NPC) || (!BIT_GET(G.fov, m->pos) && m->state != ST_HUNTING) || distance(m->pos, h->pos) > 12) continue;
    double hd = hero_dpt(m), fd = foe_dpt(m), kt = m->HP / (hd > 0.2 ? hd : 0.2); nf++; fdpt += fd; owe += kt * fd; }
  int seen = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) seen++;
  double ht = h->HT > 0 ? h->HT : 1, x = h->HP / ht, r = owe / (h->HP > 1 ? h->HP : 1);
  double f[51] = { x, x * x, r < 3 ? r : 3, r > 1, nf, fdpt / ht, h->lvl, ht, h->hunger >= 360, h->hunger >= 260 && h->hunger < 360, nfood < 3 ? nfood : 3, nheal < 3 ? nheal : 3, nunk < 5 ? nunk : 5,
    hero_str(), h->armor.kind ? item_tier(h->armor.kind) : 0, h->armor.lvl > -2 ? h->armor.lvl : -2, h->weapon.kind ? item_tier(h->weapon.kind) : 0, h->weapon.lvl > -2 ? h->weapon.lvl : -2,
    G.dew, G.dew >= 10, find_kind(IK_DEW_VIAL) >= 0, seen / 300.0, (G.now - s_floor_turn0) / (double)TICK / 100.0, G.exit >= 0 && BIT_GET(G.visited, G.exit), G.now / (double)TICK / 100.0 };
  int d = G.depth < 1 ? 1 : G.depth > 25 ? 25 : G.depth; f[24 + d] = 1; f[50] = 1;
  double zb = 0, zd = 0; for (int i = 0; i < 51; i++) { zb += J2_PB[i] * f[i]; zd += J2_D50[i] * f[i]; }
  return 1 / (1 + exp(-zb)) * (1 - 1 / (1 + exp(-zd))); }
// the fight still owed at a leaf: each awake foe in sight, its HP over my damage a turn, times its damage a turn (the game's odds)
static double mc_owed(void) {
  Hero *h = &G.hero; double owed = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & MD_NPC)) continue;
    double hd = hero_dpt(m); owed += m->HP / (hd > 0.2 ? hd : 0.2) * foe_dpt(m); }
  (void)h; return owed; }
// the leaf: if a fight is still on, the rest of it is played out with the dice model (ch_mc: the game's hit and damage
// rolls, calibrated against 57k real encounters) standing my ground from here; the judge takes what's left of me
// the current goal (the planner's target: an item, the edge of the unknown, a puzzle, the quest, the stairs): the search is
// paid a little for each step of progress toward it (MC_GK a step, in the judge's units), so a fight on the way doesn't
// stall the goal; damage and death still count in full
static int s_mc_goal = -1, s_mc_gd0;
static int mc_goal_dist(void) { if (s_mc_goal < 0) return 0; Hero *h = &G.hero; static int16_t d[MAP_LEN], q[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) d[c] = -1;
  int qh = 0, qt = 0; d[h->pos] = 0; q[qt++] = h->pos;
  while (qh < qt) { int c = q[qh++]; if (c == s_mc_goal) return d[c]; if (d[c] > 60) break; for (int k = 0; k < 8; k++) { int x = c + N8[k]; if (!nb_ok(c, x) || d[x] >= 0 || !BIT_GET(G.visited, x) || (!PASSABLE(G.map[x]) && x != s_mc_goal)) continue; d[x] = d[c] + 1; q[qt++] = x; } }
  return 61; }
static double mc_goal_bonus(void) { static double gk = -1; if (gk < 0) gk = getenv("MC_GK") ? atof(getenv("MC_GK")) : 0.003; if (s_mc_goal < 0 || gk <= 0) return 0; return gk * (s_mc_gd0 - mc_goal_dist()); }
static double mc_leaf0(void);
// the leaf: the fight played on to its end by a quick default player (MC_RO steps at most; the old bot, as any MCTS rollout),
// then judged. Judged at once, a rat killed and a rat left alive scored alike (0.540 both): what a move earns shows only a few
// turns on (the suite: hitting a 3-HP rat 0.698 against waiting 0.574 once played out)
static int hostile_near(int r, int *adj);
static bool mc_calm(void);
// every line is played on to the same moment (MC_RH turns after the root, and the fight over): stopping at 'calm' made
// walk-first-then-kill end nearer the goal than kill-then-walk, so the search towed a 4-HP rat round two floors at full health (g1)
static int64_t s_mc_now0; static int s_mc_rh = 25;
static double mc_leaf(void) { Hero *h = &G.hero; if (h->HP <= 0) return 0;
  static int ro = -1; if (ro < 0) ro = getenv("MC_RO") ? atoi(getenv("MC_RO")) : 30;
  if (s_pend_depth && s_pend_depth <= G.max_depth && !getenv("MC_NOUP")) {   // the stairs to a floor I have been on: arrive and play on (the player will come back down;
    s_arrival = true; game_level_commit(); s_arrival = false; if (G.depth != s_last_depth) { s_last_depth = G.depth; on_new_floor(false); } la_scrub((uint32_t)rand()); }   //  judged at the arrival, the stairs looked like the fight's end: g6 went up and down beside a rat till it died)
  if (ro > 0 && s_rollout) { int calm = 0; for (int k = 0; k < ro * 2 && h->HP > 0 && (calm < 3 || (k < ro && G.now < s_mc_now0 + s_mc_rh * TICK)); k++) { if (s_pend_depth) { if (s_pend_depth > G.max_depth || getenv("MC_NOUP")) break; s_arrival = true; game_level_commit(); s_arrival = false; if (G.depth != s_last_depth) { s_last_depth = G.depth; on_new_floor(false); } la_scrub((uint32_t)rand()); } if (!la_step()) break; calm = (getenv("MC_ROSEEN") ? hostile_near(8, NULL) != 0 : !mc_calm()) ? 0 : calm + 1; } if (h->HP <= 0) return 0; }
  double v = getenv("MC_J1") ? mc_leaf0() : judge2(); return v > 0 ? v + mc_goal_bonus() : v; }   // (MC_J1: the first judge with the dice-model playout)
// health's worth before the next rest: the fights still to meet on the way (MC_FA of them) at this health, each with the measured
// death risk fight_risk(HP/HT) (4,400 logged fights). The judge alone, fitted at floor arrivals where health is mostly full,
// put 1 HP at 0.013: below the search's noise, so it wandered about at 25/25 with a foe in sight
static double mc_ahead(double hp) { static double fa = -1; if (fa < 0) fa = getenv("MC_FA") ? atof(getenv("MC_FA")) : 0;   /* (3: 20 local games, 13 died on floors 1-4 against 9: off till the judge is refit) */ Hero *h = &G.hero; double r = fight_risk(hp / (h->HT > 0 ? h->HT : 1)); return pow(1 - r, fa); }
static double mc_leaf0(void) { Hero *h = &G.hero; if (h->HP <= 0) return 0;
  TFoe F[24]; int nf = 0;
  for (int i = 0; i < G.n_mobs && nf < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || (MOB_DEFS[m->type].flags & MD_NPC) || distance(m->pos, h->pos) > 12) continue;
    TFoe *f = &F[nf++]; f->wat = MOB_DEFS[m->type].flags & MD_WATER; f->i = i; f->pos = m->pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi;
    f->acts = mob_move_cost(m) * 2 <= hero_move_cost() ? 2 : 1; f->mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (f->mc < 5) f->mc = 5;
    f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = h->time - m->time; if (f->tm > TICK) f->tm = TICK; if (f->tm < -4 * TICK) f->tm = -4 * TICK; f->rng = magic_shooter(m->type) || m->type == MT_SCORPIO; }
  if (nf == 0 || getenv("MC_OWED")) { double hp = h->HP + (vial_full() ? h->HT : 0) - (nf ? mc_owed() : 0); if (hp > h->HT) hp = h->HT; return judge_value(hp) * mc_ahead(hp); }
  ch_prep(F, nf); c_seed = (uint32_t)rand(); double E = ch_mc(F, nf, h->pos, NULL, 0, -1, 0, 16, h->HP); double pd = s_te_pd;
  double hp = h->HP + (vial_full() ? h->HT : 0) - E; if (hp > h->HT) hp = h->HT; if (hp < 1) hp = 1;
  return (1 - pd) * judge_value(hp) * mc_ahead(hp); }
// the hero's moves, as ids: 0-7 a step or blow that way, 8 wait, 9 the stairs underfoot, 16+slot*4+use (drink/read/eat/plant), 512+slot*32+foe (throw), 4096+slot*32+foe (zap)
static int mc_moves(int *mv) {
  Hero *h = &G.hero; int n = 0, hp = h->pos;
  for (int k = 0; k < 8; k++) { int q = hp + N8[k]; if (!nb_ok(hp, q) || !PASSABLE(G.map[q])) continue; int mi = mob_at(q); if (mi >= 0 && (ally(mi) || (MOB_DEFS[G.mobs[mi].type].flags & MD_NPC))) continue; mv[n++] = k; }
  mv[n++] = 8;
  if (hp == G.exit || hp == G.entrance) mv[n++] = 9;
  int foes[24], nf = 0; for (int i = 0; i < G.n_mobs && nf < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && !(MOB_DEFS[m->type].flags & MD_NPC)) foes[nf++] = i; }
  bool seen[IK_COUNT] = { 0 };
  for (int s = 0; s < BAG_SIZE && n < 120; s++) { Item *it = &h->bag[s]; int k = it->kind, cat = item_cat(k); if (!k || seen[k]) continue; seen[k] = true;
    if (cat == CAT_POTION) mv[n++] = 16 + s * 4 + 0;
    else if (cat == CAT_SCROLL && !h->b.blind) mv[n++] = 16 + s * 4 + 1;
    else if (is_food(k) && h->hunger >= 150) mv[n++] = 16 + s * 4 + 2;
    else if (cat == CAT_SEED && plant_at(hp) < 0 && heap_at(hp) < 0 && hp != G.exit && hp != G.entrance) mv[n++] = 16 + s * 4 + 3;
    if (cat == CAT_MISSILE || cat == CAT_POTION) for (int j = 0; j < nf && n < 120; j++) if (!adjacent(G.mobs[foes[j]].pos, hp) || cat == CAT_MISSILE) mv[n++] = 512 + s * 32 + j;
    if (cat == CAT_WAND && it->ench > 0) for (int j = 0; j < nf && n < 120; j++) mv[n++] = 4096 + s * 32 + j; }
  // walks: any known cell 2..MC_WALK steps away, as one move (a corridor, a doorway, out of sight, the stairs): the search
  // can't find a plan four steps long by trying single steps, but it can by trying the walk as a whole
  static int wr = -1; if (wr < 0) wr = getenv("MC_WALK") ? atoi(getenv("MC_WALK")) : 6;
  if (wr >= 2 && !h->b.roots) { static int16_t dd[MAP_LEN], q[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) dd[c] = -1; int qh = 0, qt = 0; dd[hp] = 0; q[qt++] = hp;
    while (qh < qt) { int c = q[qh++]; if (dd[c] >= wr) continue; for (int k = 0; k < 8; k++) { int x = c + N8[k];
        if (!nb_ok(c, x) || dd[x] >= 0 || !BIT_GET(G.visited, x) || !PASSABLE(G.map[x]) || (TFLAGS(G.map[x]) & F_AVOID) || (char_at(x) && x != hp)) continue; dd[x] = dd[c] + 1; q[qt++] = x; } }
    for (int i = 1; i < qt && n < 250; i++) if (dd[q[i]] >= 2) mv[n++] = 8192 + q[i]; }
  return n; }
static bool mc_apply(int id) {
  Hero *h = &G.hero; int32_t t0 = h->time; int d0 = G.depth, hp = h->pos;
  int foes[24], nf = 0; for (int i = 0; i < G.n_mobs && nf < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && !(MOB_DEFS[m->type].flags & MD_NPC)) foes[nf++] = i; }
  if (id < 8) walk(game_tap_cell(hp + N8[id]));
  else if (id == 8) walk(game_rest(false));
  else if (id == 9) walk(game_tap_cell(hp));
  else if (id < 512) { int s = (id - 16) / 4, u = (id - 16) % 4; do_item(s, u == 0 ? ACN_DRINK : u == 1 ? ACN_READ : u == 2 ? ACN_EAT : ACN_PLANT); }
  else if (id < 4096) { int s = (id - 512) / 32, j = (id - 512) % 32; if (j >= nf) return false; walk(game_throw(s, G.mobs[foes[j]].pos)); }
  else if (id < 8192) { int s = (id - 4096) / 32, j = (id - 4096) % 32; if (j >= nf) return false; walk(game_zap(s, G.mobs[foes[j]].pos)); }
  else { int dest = id - 8192; static int16_t pr[MAP_LEN], q[MAP_LEN];   // a walk: step by step along the shortest known way; it ends early if a foe comes beside me (the next move is chosen then)
    for (int st = 0; st < 12 && h->pos != dest && h->HP > 0 && !s_pend_depth; st++) {
      if (st > 0) { bool adj = false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && m->state == ST_HUNTING && adjacent(m->pos, h->pos)) adj = true; } if (adj) break; }
      for (int c = 0; c < MAP_LEN; c++) pr[c] = -2; int qh = 0, qt = 0; pr[h->pos] = -1; q[qt++] = h->pos;
      while (qh < qt && pr[dest] == -2) { int c = q[qh++]; for (int k = 0; k < 8; k++) { int x = c + N8[k]; if (!nb_ok(c, x) || pr[x] != -2 || !BIT_GET(G.visited, x) || !PASSABLE(G.map[x]) || (TFLAGS(G.map[x]) & F_AVOID) || (char_at(x) && x != dest)) continue; pr[x] = c; q[qt++] = x; } }
      if (pr[dest] == -2) break; int nx = dest; while (pr[nx] != h->pos) nx = pr[nx];
      int32_t ts = h->time; walk(game_tap_cell(nx)); if (h->time == ts) break; } }
  for (int g = 0; g < 4 && (s_pick >= 0 || s_wnd_btn); g++) la_step();   // (a picker or window a move opened: answered as ever)
  return h->time != t0 || G.depth != d0 || s_pend_depth; }
static bool mc_calm(void) { Hero *hh = &G.hero; if (bad_air(hh->pos) || hh->b.burning || hh->b.poison) return false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC)) return false; } return true; }   // (no foe hunting me at all, seen or not: the search judged hiding round a corner as the fight won)
typedef struct { int32_t first; int32_t next; int32_t id; int32_t n; double w; } MNode;   // first child, next sibling
#define MC_MAXN 60000
static MNode s_mn[MC_MAXN]; static int s_mnn;
static int mc_child(int p, int id) { for (int c = s_mn[p].first; c >= 0; c = s_mn[c].next) if (s_mn[c].id == id) return c; return -1; }
static int mc_add(int p, int id) { if (s_mnn >= MC_MAXN) return -1; int c = s_mnn++; s_mn[c] = (MNode){ -1, s_mn[p].first, id, 0, 0 }; s_mn[p].first = c; return c; }
static int s_mc_last_id; static long st_mc_calls, st_mc_iters; static double s_qlo, s_qhi;   // the values seen this search: every comparison is on their spread (MuZero's normalised Q), so a Tengu floor where all is 0.00-0.07 still ranks its moves
static double s_rp[256]; static int s_rid[256], s_rn;   // the root moves' instinct shares
#define QN(q) (s_qhi > s_qlo + 1e-9 ? ((q) - s_qlo) / (s_qhi - s_qlo) : 0.5)
static int mcts(void) {
  if (getenv("RNGDBG") && !s_rollout && !s_is_child) printf("RNG mcts-entry %u t%d\n", g_rng, (int)(G.now / TICK));
  Hero *h = &G.hero; if (s_rollout || s_is_child || getenv("NOMCTS") || h->b.paralysis) return 0;
  bool threat = false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && (BIT_GET(G.fov, m->pos) || adjacent(m->pos, h->pos)) && m->state != ST_PASSIVE && (m->state != ST_SLEEPING || distance(m->pos, h->pos) <= 2) && !(MOB_DEFS[m->type].flags & MD_NPC) && distance(m->pos, h->pos) <= 8) threat = true; }   // (any creature awake in sight, or asleep beside me: whether to take it on is the search's call too; g10: the old rules walked into a crab at 8/30)
  if (bad_air(h->pos) || h->b.burning || h->b.poison || h->b.bleed) threat = true;   // (harm going on with no foe in sight: gas, fire, poison, bleeding: the search's turn too; g10 rested to death in toxic gas)
  if (!threat) return 0;
  static int iters = -1, horizon, calmk; static double cu, pw; if (iters < 0) { pw = getenv("MC_PW") ? atof(getenv("MC_PW")) : 1.5; iters = getenv("MC_IT") ? atoi(getenv("MC_IT")) : 300; horizon = getenv("MC_H") ? atoi(getenv("MC_H")) : 20; cu = getenv("MC_C") ? atof(getenv("MC_C")) : 0.3; calmk = 2; }
  int mv0[256]; int n0 = mc_moves(mv0); if (n0 <= 1) return 0;
  // the instinct (AlphaZero's "policy"): the dice-model scorer prices every root move; the search spends its tries mostly where
  // the instinct points (PUCT) and checks them against the real game, free to overrule it. A move it didn't price gets a small share.
  static double mt_ = -1, cp; if (mt_ < 0) { mt_ = getenv("MC_T") ? atof(getenv("MC_T")) : 0.25;   /* (0.05 HT: the instinct took 93% of the tries on one move, and the search could not overrule it) */ cp = getenv("MC_CP") ? atof(getenv("MC_CP")) : 1.0; }
  s_mc_goal = getenv("MC_NOGOAL") ? -1 : s_goal_cell2;
  if (s_mc_goal < 0 && !getenv("MC_NOGOAL")) { const char *gw = "?"; bfs(h->pos, false); int gc = plan_next(false, &gw); if (gc >= 0) s_mc_goal = gc; else if (G.exit >= 0 && BIT_GET(G.visited, G.exit)) s_mc_goal = G.exit; }   // (no goal planner on: the old planner's next target; with no goal every move scored alike and the hero wandered, t666-676)
  s_mc_gd0 = mc_goal_dist();
  s_ch_prior = 1; s_pr_n = 0; int chr = choose(); s_ch_prior = 0; if (getenv("MCDBG")) printf("MCPRIOR ret %d n %d\n", chr, s_pr_n);
  { double mn = 1e18; for (int i = 0; i < s_pr_n; i++) if (s_pr_v[i] < mn) mn = s_pr_v[i]; double T = mt_ * (h->HT > 0 ? h->HT : 1), tot = 0;
    for (int i = 0; i < n0; i++) { double p = 0; for (int j = 0; j < s_pr_n; j++) if (s_pr_id[j] == mv0[i]) { double e = exp(-(s_pr_v[j] - mn) / T); if (e > p) p = e; } if (p < 0.01) p = 0.01; s_rp[i] = p; s_rid[i] = mv0[i]; tot += p; }
    for (int i = 0; i < n0; i++) s_rp[i] /= tot; s_rn = n0;
    if (getenv("MCDBG")) { printf("MCP"); for (int i = 0; i < n0; i++) if (s_rp[i] > 0.03) printf(" %d:%.2f", s_rid[i], s_rp[i]); printf(" | priced:"); for (int j = 0; j < s_pr_n && j < 12; j++) printf(" %d=%.1f", s_pr_id[j], s_pr_v[j]); printf("\n"); } }
  if (getenv("RNGDBG")) printf("RNG pre-loop %u t%d\n", g_rng, (int)(G.now / TICK));
  s_mc_now0 = G.now; if (getenv("MC_RH")) s_mc_rh = atoi(getenv("MC_RH"));
  s_mnn = 1; s_mn[0] = (MNode){ -1, -1, -1, 0, 0 }; s_qlo = 1e9; s_qhi = -1e9;
  uint32_t base = (uint32_t)(cur_game * 1000003u + s_bot_steps * 7919u);
  st_mc_calls++; struct timespec t_a, t_b; clock_gettime(CLOCK_MONOTONIC, &t_a);
  for (int it = 0; it < iters; it++) {
    int fds[2]; if (pipe(fds)) break; fflush(stdout); pid_t pid = fork(); if (pid < 0) { close(fds[0]); close(fds[1]); break; }
    if (pid == 0) {   // the child: one walk down the tree in its own copy of the game
      if (getenv("MC_NOP")) { int z = 0; if (write(fds[1], &z, sizeof z)) {} _exit(0); }
      if (getenv("MC_NOSCRUB")) {} 
      close(fds[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); }
      uint32_t seed = base + it * 104729u; g_rng ^= seed * 2654435761u; srand(seed); la_scrub(seed);
      int path[64], np = 0, node = 0; double val = -1; int calm = 0;
      for (int dep = 0; dep < horizon; dep++) {
        if (h->HP <= 0) { val = 0; break; }
        if (s_pend_depth) { val = mc_leaf(); break; }
        if (mc_calm()) { if (++calm >= calmk) { val = mc_leaf(); break; } } else calm = 0;
        int mv[256]; int n = mc_moves(mv); if (!n) { val = mc_leaf(); break; }
        if (dep == 0) {   // the root: PUCT over every move, tried or not (an untried one counts as the root's average so far)
          double nr = s_mn[0].n, q0 = nr > 0 ? QN(s_mn[0].w / nr) : 0.5, bu = -1e18; int bid = -1, bc = -1;
          for (int i = 0; i < s_rn; i++) { int c = mc_child(0, s_rid[i]); double q = c >= 0 ? (s_mn[c].w > -500 ? QN(s_mn[c].w / s_mn[c].n) : -1e9) : q0; int cn = c >= 0 ? s_mn[c].n : 0;
            double u = q + cp * s_rp[i] * sqrt(nr + 1) / (1 + cn) + (rand() % 1000) * 1e-9; if (u > bu) { bu = u; bid = s_rid[i]; bc = c; } }
          if (bid < 0) { val = mc_leaf(); break; }
          path[np++] = bid; if (!mc_apply(bid)) { val = -2; break; }
          if (bc < 0) { val = h->HP <= 0 ? 0 : mc_leaf(); break; }   // (a new root move: judged here)
          node = bc; continue; }
        // UCT over the moves open here; one not yet tried is tried first (in a random order)
        int untried[256], nu = 0; for (int i = 0; i < n; i++) if (node < 0 || mc_child(node, mv[i]) < 0) untried[nu++] = mv[i];
        int pick; int nch = n - nu;
        // progressive widening (MC_PW): a node opens a new move only while it has fewer than PW*sqrt(visits) children, so
        // the tries go deeper down the good lines instead of one level across every move
        bool widen = nu > 0 && (node < 0 || nch < 1 || nch < pw * sqrt((double)(s_mn[node].n + 1)));
        if (widen) { pick = untried[rand() % nu]; path[np++] = pick; if (!mc_apply(pick)) { if (getenv("MCDBG2")) fprintf(stderr, "MCFAIL id %d dep %d hp %d pos %d,%d time %d\n", pick, dep, h->HP, cx(h->pos), cy(h->pos), h->time); val = -2; break; }
          // the new leaf: judged as it stands, the fight still owed taken off
          if (h->HP <= 0) val = 0; else val = mc_leaf(); break; }
        double bv = -1e9; int bc = -1, bid = -1; double ln = log((double)(s_mn[node].n > 0 ? s_mn[node].n : 1));
        for (int i = 0; i < n; i++) { int c = mc_child(node, mv[i]); if (c < 0) continue; double u = (s_mn[c].w > -500 ? QN(s_mn[c].w / s_mn[c].n) : -1e9) + cu * sqrt(ln / s_mn[c].n); if (u > bv) { bv = u; bc = c; bid = mv[i]; } }
        if (bc < 0) { val = mc_leaf(); break; }
        path[np++] = bid; if (!mc_apply(bid)) { val = -2; break; } node = bc; }
      if (val < -1.5) val = -1000;   // (a move that doesn't take here (a scroll that can't be read...): marked dead, never tried again)
      if (val < 0) val = mc_leaf();
      double v = val;
      if (write(fds[1], &np, sizeof np) != sizeof np || write(fds[1], path, sizeof(int) * np) != (ssize_t)(sizeof(int) * np) || write(fds[1], &v, sizeof v) != sizeof v) {}
      _exit(0); }
    close(fds[1]); int np = 0; int path[64]; double v = 0; bool ok = read(fds[0], &np, sizeof np) == sizeof np && np >= 0 && np <= 64 && read(fds[0], path, sizeof(int) * np) == (ssize_t)(sizeof(int) * np) && read(fds[0], &v, sizeof v) == sizeof v;
    close(fds[0]); int st; waitpid(pid, &st, 0); if (!ok || np == 0) continue;
    st_mc_iters++;
    if (v > -500) { if (v < s_qlo) s_qlo = v; if (v > s_qhi) s_qhi = v; }
    int node = 0; s_mn[0].n++; s_mn[0].w += v < -500 ? 0 : v;
    if (v < -500) { for (int i = 0; i < np; i++) { int c = mc_child(node, path[i]); if (c < 0) c = mc_add(node, path[i]); if (c < 0) break; if (i == np - 1) { s_mn[c].n = 1; s_mn[c].w = -1000; } node = c; } s_mn[0].n--; continue; }
    for (int i = 0; i < np; i++) { int c = mc_child(node, path[i]); if (c < 0) c = mc_add(node, path[i]); if (c < 0) break; s_mn[c].n++; s_mn[c].w += v; node = c; } }
  // the move: the best average among those tried enough to trust (MC_MINN tries, or 5% of all); the most tried alone followed the
  // instinct's favourite: in seed 8 the hero stood in toxic gas hitting (226 tries, judged 0.04) while stepping off was judged 0.07
  static int minn = -1; if (minn < 0) minn = getenv("MC_MINN") ? atoi(getenv("MC_MINN")) : 8;
  int best = -1, bn = 0; for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].w > -500 && s_mn[c].n > bn) { bn = s_mn[c].n; best = c; }
  { int need = minn > s_mn[0].n / 20 ? minn : s_mn[0].n / 20; double bq = -1; int bb = -1;
    for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].w > -500 && s_mn[c].n >= need && s_mn[c].w / s_mn[c].n > bq) { bq = s_mn[c].w / s_mn[c].n; bb = c; }
    if (bb >= 0) best = bb; }
  if (best < 0) { if (getenv("MC_NOP")) { struct timespec t_c; clock_gettime(CLOCK_MONOTONIC, &t_c); printf("MCNOP us/iter %.0f\n", ((t_c.tv_sec - t_a.tv_sec) * 1e6 + (t_c.tv_nsec - t_a.tv_nsec) / 1e3) / iters); } return 0; }
  if (getenv("RNGDBG")) printf("RNG post-loop %u\n", g_rng);
  clock_gettime(CLOCK_MONOTONIC, &t_b); double us = (t_b.tv_sec - t_a.tv_sec) * 1e6 + (t_b.tv_nsec - t_a.tv_nsec) / 1e3;
  if (getenv("MCDBG")) { printf("MC d%d t%d hp %d/%d iters %d nodes %d us/iter %.0f:", G.depth, (int)(G.now / TICK), h->HP, h->HT, s_mn[0].n, s_mnn, us / (iters > 0 ? iters : 1));
    for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].n >= 5) printf(" [%d n%d v%.3f]", s_mn[c].id, s_mn[c].n, s_mn[c].w / s_mn[c].n); printf("\n"); }
  int id = s_mn[best].id; s_mc_last_id = id; s_did[0] = 0; DID("search: move %d (%d of %d tries, judged %.3f)", id, s_mn[best].n, s_mn[0].n, s_mn[best].w / s_mn[best].n);
  uint32_t arng = g_rng; int32_t t0 = h->time; int d0 = G.depth; int amh[64], ap0 = h->pos; for (int i = 0; i < G.n_mobs && i < 64; i++) amh[i] = G.mobs[i].HP;
  mc_apply(id);
  if (getenv("MCAPPDBG")) { printf("APP rng %u after %u att %d id %d pos %d,%d->%d,%d time +%d:", arng, g_rng, h->attack_skill, id, cx(ap0), cy(ap0), cx(h->pos), cy(h->pos), (int)(h->time - t0)); for (int i = 0; i < G.n_mobs && i < 64; i++) if (distance(G.mobs[i].pos, h->pos) <= 1 || amh[i] != G.mobs[i].HP) printf(" [%s %d,%d hp %d->%d st %d tm %d]", mob_name(G.mobs[i].type), cx(G.mobs[i].pos), cy(G.mobs[i].pos), amh[i], G.mobs[i].HP, G.mobs[i].state, (int)(h->time - G.mobs[i].time)); printf(" log: %s\n", s_nlog ? s_last_log[(s_nlog - 1) & 7] : ""); }
  if (h->time == t0 && G.depth == d0 && !s_pend_depth) return 0;
  return 1; }
