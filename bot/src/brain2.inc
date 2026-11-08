// brain2.inc: the one-goal brain (owner, 2026-11-03), included into deepbot.c (it uses the old code's advisors).
//
// The goal: take everything on the floor worth its walk, and move on, in as few moves as possible.
// One currency: moves (each costs food). An item is worth so many moves (item_value). HP lost costs
// little in itself (it comes back while walking: 1 a 10 turns, resting or not: Regeneration.java), so
// it is priced mostly through the chance of dying (a death costs the whole run). XP comes from the
// fights you can't avoid, or that are cheap.
//
// Each turn:
//  1. reflexes (physics, not choices): the old rules handle burning, ooze, gas, paralysis, roots.
//  2. something hunting me: fight only when well ahead, or when I can't get away (it's faster, it
//     shoots, I'm cornered); otherwise walk on with it in tow (the same speed: it follows and never
//     strikes) and heal on the way; turn and fight once the sum says I'm well ahead.
//  3. safe: eat, then the item advisor (tests, upgrades, gear: use_items_when_safe, with its own
//     timing: e.g. potions tested once the floor is explored, a puzzle room's potion kept), then
//  4. the route: the target worth most over its detour (value - the extra walk it adds on the way
//     to the stairs); a sleeping monster near the way costs its fight times the chance it wakes.
//     Nothing worth its detour: down the stairs.
// Seams to the old rules (return -1): boss floors and the floor before a boss (the boss scripts),
// the physics states above, searching for secret doors when nothing else is left.
//
// Knobs (env, for tuning): B2HPV (moves per HP, 2), B2DEATH (moves a death costs, 1500), B2XPV
// (moves per exp point, 8), B2EPS (death chance a fight may carry, 0.03), B2EF (HP share a fight may
// cost, 0.5), B2LOOT (moves of loot expected on an unexplored floor, 300), B2STAIRV (finding the
// stairs, 400), B2DOWNHP (HP % to have before going down, 60), B2WAKE (wake chance a step near a
// sleeper, 0.1).

static double b2k(const char *n, double d) { const char *v = getenv(n); return v ? atof(v) : d; }
static double s_b2keep, s_b2hpv, s_b2death, s_b2xpv, s_b2eps, s_b2ef, s_b2loot, s_b2stair, s_b2down, s_b2wake; static int s_b2init;
static void b2_init(void) { if (s_b2init) return; s_b2init = 1;
  s_b2keep = b2k("B2KEEP", 30); s_b2hpv = b2k("B2HPV", 2); s_b2death = b2k("B2DEATH", 1500); s_b2xpv = b2k("B2XPV", 8); s_b2eps = b2k("B2EPS", 0.03); s_b2ef = b2k("B2EF", 0.5);
  s_b2loot = b2k("B2LOOT", 300); s_b2stair = b2k("B2STAIRV", 400); s_b2down = b2k("B2DOWNHP", 60); s_b2wake = b2k("B2WAKE", 0.1); }
static long st_b2[8];                                   // tallies: engage, in tow, route, down, legacy, items, eat, hunt

// A duel's price in moves, at the HP I have now: what it costs in HP and in the chance of dying, less its XP.
static double b2_duel(int mi, double *pd_out) {
  Mob *m = &G.mobs[mi]; Hero *h = &G.hero; double hd = hero_dpt(m), fd = foe_dpt(m);
  if (m->type == MT_BAT) { hd -= fd; }                  // (a vampire bat heals what it bites: losing_fight)
  if (hd < 0.2) hd = 0.2;
  double turns = m->HP / hd, E = turns * fd;
  if ((m->type == MT_BRUTE || m->type == MT_SHIELDED) && m->HP >= m->HT / 4) { int q = m->HT / 4; E += (q < m->HP ? q : m->HP) / hd * fd * 0.9; }   // (its last quarter enraged, 10-40 a blow: the top killer of floors 11-14)
  double V = E * MOB_DEFS[m->type].dmg_hi * 0.7 + 1;
  double pd = 0.5 * erfc((h->HP - E) / sqrt(V) / sqrt(2.0)); if (pd_out) *pd_out = pd;
  int ex = h->lvl <= MOB_DEFS[m->type].max_lvl ? MOB_DEFS[m->type].exp : 0, behind = G.depth + 2 - h->lvl;   // (no XP from one beneath me: Mob.exp)
  double xpv = s_b2xpv * (1 + (behind > 0 ? behind : 0));   // XP is worth more the further behind the floor I am
  return s_b2hpv * E + s_b2death * pd - xpv * ex;
}

// Walking costs over what I know: 1 a step, plus the danger of passing near a sleeping monster.
static float s_b2w[MAP_LEN]; static int16_t s_b2dh[MAP_LEN], s_b2ds[MAP_LEN], s_b2par[MAP_LEN];
static bool b2_open(int c) { return BIT_GET(G.visited, c) && PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & F_AVOID) && !PWATER(c) && !bad_air(c) && s_black[c] <= s_bot_steps; }
static void b2_weights(void) {
  for (int c = 0; c < MAP_LEN; c++) s_b2w[c] = 1;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i];
    if (m->HP <= 0 || ally(i) || m->state == ST_HUNTING || m->state == ST_PASSIVE || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & (MD_NPC | MD_BOSS))) continue;
    double c = b2_duel(i, NULL); if (c <= 0) continue;   // (a cheap one: no danger in waking it)
    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int x = cx(m->pos) + dx, y = cy(m->pos) + dy; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
      s_b2w[y * MAP_W + x] += (float)(c * s_b2wake); } }
}
// Dijkstra (costs are small: a bucket queue would do; the floor is 1024 cells, a plain scan is fine).
static void b2_dist(int from, int16_t *d, bool par) {
  static float dd[MAP_LEN]; static uint8_t done[MAP_LEN];
  for (int c = 0; c < MAP_LEN; c++) { dd[c] = 1e9f; done[c] = 0; d[c] = 0x7FFF; } dd[from] = 0; if (par) s_b2par[from] = -1;
  for (;;) { int b = -1; float bv = 1e9f; for (int c = 0; c < MAP_LEN; c++) if (!done[c] && dd[c] < bv) { bv = dd[c]; b = c; } if (b < 0) break; done[b] = 1; d[b] = (int16_t)(bv < 32000 ? bv : 32000);
    for (int k = 0; k < 8; k++) { int n = b + N8[k]; if (!nb_ok(b, n) || done[n]) continue; if (!b2_open(n) || ((n == G.exit || n == G.entrance) && n != from)) { if ((n == G.exit || n == G.entrance) && !done[n] && bv + 1 < dd[n]) { dd[n] = bv + 1; if (par) s_b2par[n] = b; } continue; }   // (the stairs: a place to end at, never to walk across: a step onto them takes them) if (char_at(n) && n != G.hero.pos) continue;
      float nv = bv + s_b2w[n]; if (nv < dd[n]) { dd[n] = nv; if (par) s_b2par[n] = b; } } }
}
static int b2_first_step(int t) { if (s_b2dh[t] >= 0x7FFF) return -1; int c = t; while (s_b2par[c] >= 0 && s_b2par[c] != G.hero.pos) c = s_b2par[c]; return s_b2par[c] == G.hero.pos ? c : -1; }
static int b2_step(int c, const char *what) { int32_t t0 = G.hero.time; ActResult r = tap_safe(c); if (r == ACT_NONE && G.hero.time == t0) return -1; s_did[0] = 0; DID("%s", what); walk(r); return 1; }

// The fight brain: something hunts me.
// Kite to heal (owner 2026-11-06: "you can always recover the same HP as a long rest by kiting an enemy in circles"):
// a same-speed melee foe that follows me never strikes while I keep stepping away, and I heal as I go (1 HP / 10 turns).
// So while a fight now would carry a real death risk, I step to the cell that keeps me clear of it, over ground I know
// (away from the unknown, other monsters and dead ends), until the fight is safe; then I hold and let it step into my blow.
static int b2_kite(void) {
  Hero *h = &G.hero; if (!getenv("B2KITE") || starve_block() || h->HP * 10 >= h->HT * 9) return 0;
  static double eps = -1; if (eps < 0) eps = b2k("B2KITEEPS", 0.01);
  TFoe F[24]; int nf = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & (MD_NPC | MD_WATER))) continue;
    bool see = BIT_GET(G.fov, m->pos), a = adjacent(m->pos, h->pos); if (!see && !a) continue; if (m->state != ST_HUNTING && !a) continue; if (distance(m->pos, h->pos) > 9) continue;
    if (MOB_DEFS[m->type].flags & MD_BOSS) return 0;
    int mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (magic_shooter(m->type) || m->type == MT_SCORPIO || mc < TICK || m->b.paralysis) return 0;   // (a shooter or a faster one: kiting won't work)
    if (nf >= 24) break; TFoe *f = &F[nf++]; f->i = i; f->pos = m->pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi; f->acts = 1; f->mc = mc; f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = 0; f->rng = false;
    if (m->type == MT_BAT) { f->hd -= f->dpt; if (f->hd < 0.2) f->hd = 0.2; } if ((m->type == MT_BRUTE || m->type == MT_SHIELDED) && m->HP >= m->HT / 4) f->dpt *= 1.3; }
  if (nf == 0) return 0;
  double tt; tact_eval(F, nf, h->pos, NULL, 0, &tt, -1, 0); double pd = s_te_pd;
  if (getenv("B2DBG")) printf("KITE step %d hp %d/%d foes %d pd %.3f\n", s_bot_steps, h->HP, h->HT, nf, pd);
  if (pd < eps) return 0;                               // (safe enough: fight it, on my terms)
  int bq = -1; double bs = -1e9;
  for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !b2_open(q) || char_at(q) || q == G.exit || q == G.entrance) continue;
    int mind = 99; for (int j = 0; j < nf; j++) { int d = distance(F[j].pos, q); if (d < mind) mind = d; } if (mind <= 1) continue;   // (never beside it after my step)
    int open = 0, unk = 0, oth = 0;
    for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (!nb_ok(q, r)) continue; if (b2_open(r) && !char_at(r)) { bool nearf = false; for (int f = 0; f < nf; f++) if (distance(F[f].pos, r) <= 1) nearf = true; if (!nearf) open++; } }
    for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int x = cx(q) + dx, y = cy(q) + dy; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue; if (!BIT_GET(G.visited, y * MAP_W + x)) unk++; }
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && m->state != ST_HUNTING && m->state != ST_PASSIVE && BIT_GET(G.fov, m->pos) && distance(m->pos, q) <= 3) oth++; }
    double sc = mind * 2 + open * 1.5 - unk * 0.6 - oth * 8 - (open == 0 ? 30 : 0);
    if (sc > bs) { bs = sc; bq = q; } }
  if (bq < 0) return 0;                                 // (cornered: fight)
  st_b2[1]++; char w[96]; snprintf(w, sizeof w, "b2: kite to heal (a fight now: %.0f%% death)", pd * 100); return b2_step(bq, w);
}
static int b2_threats_m(bool tow_only);
static int b2_threats(void) { return b2_threats_m(false); }
static int b2_threats_m(bool tow_only) {
  Hero *h = &G.hero; TFoe F[24]; int nf = 0; bool adj = false, can_go = true;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state == ST_PASSIVE) continue;
    bool see = BIT_GET(G.fov, m->pos), a = adjacent(m->pos, h->pos); if (m->state != ST_HUNTING && !a) continue;   // (one beside me strikes whatever its state says)
    if ((!see && !a) || distance(m->pos, h->pos) > 9) continue;   // (only what I can see: FAIR)
    if (MOB_DEFS[m->type].flags & MD_BOSS) return -1;
    if (MOB_DEFS[m->type].flags & (MD_WATER | MD_NPC) || m->type == MT_STATUE || m->b.paralysis || m->b.frost) continue;
    if (nf >= 24) break; TFoe *f = &F[nf++]; f->i = i; f->pos = m->pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi;
    f->acts = 1; f->mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (f->mc < 5) f->mc = 5; f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = 0;
    f->rng = magic_shooter(m->type) || m->type == MT_SCORPIO; if (a) adj = true;
    if (m->type == MT_BAT) { f->hd -= f->dpt; if (f->hd < 0.2) f->hd = 0.2; }   // (it heals what it bites)
    if ((m->type == MT_BRUTE || m->type == MT_SHIELDED) && m->HP >= m->HT / 4) f->dpt *= 1.3;   // (enraged for its last quarter)
    if (m->type == MT_WRAITH && m->aux2 == 1) return -1;   // (the ghost's curse: its own rules, curse_fight)
    if (f->rng || f->mc < TICK) can_go = false; }   // (a shooter or a faster one: no walking away from it)
  if (nf == 0) return 0;
  if (h->HP * 2 < h->HT && h->pos == G.entrance && G.depth > 1 && !adj) { st_b2[1]++; s_last_up = G.now; return b2_step(h->pos, "b2: hurt on the stairs up, one coming: up and away"); }   // (the old prep: a pack or a crab while low)
  double tt; tact_eval(F, nf, h->pos, NULL, 0, &tt, -1, 0); double E = s_te_E, pd = s_te_pd;
  bool ahead = pd < s_b2eps && E < h->HP * s_b2ef;
  if (getenv("B2DBG")) printf("B2 step %d hp %d/%d foes %d E %.1f pd %.3f ahead %d can_go %d\n", s_bot_steps, h->HP, h->HT, nf, E, pd, ahead, can_go);
  if (starve_block()) can_go = false;                 // (starving: no healing on the way, so putting it off only makes it worse)
  if (!ahead && can_go) {                               // walk on with it in tow: a step that leaves none beside me, the one that gets me furthest on my way
    int tgt = s_goal_cell2; int bq = -1; double bs = -1e9;
    for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; if (!nb_ok(h->pos, q) || !b2_open(q) || char_at(q) || q == G.exit) continue;
      bool near = false; int mind = 99; for (int j = 0; j < nf; j++) { int d = distance(F[j].pos, q); if (d <= 1) near = true; if (d < mind) mind = d; } if (near) continue;
      int open = 0; for (int j = 0; j < 8; j++) { int r = q + N8[j]; if (nb_ok(q, r) && b2_open(r) && !char_at(r)) open++; }
      double sc = mind * 2 + open * 0.5 + (tgt >= 0 && s_b2dh[tgt] < 0x7FFF ? -(double)distance(q, tgt) : 0);   // (away from them, room to keep going, on my way)
      if (sc > bs) { bs = sc; bq = q; } }
    if (bq >= 0) { st_b2[1]++; char w[96]; snprintf(w, sizeof w, "b2: walk on, %d in tow (a fight now: %.0f HP, %.0f%% death)", nf, E, pd * 100); return b2_step(bq, w); }
  }
  if (tow_only) return 0;                               // (the tow alone: the rest is the old fight advisor's)
  st_b2[0]++;                                           // fight, here and now: the kit if it's a hard one, then where and how (tact), then the blow
  int tg = -1; double tv = -1; for (int j = 0; j < nf; j++) { int ty = G.mobs[F[j].i].type; double v = ((ty == MT_THIEF || ty == MT_BANDIT) && distance(F[j].pos, h->pos) <= 4 ? 2000 : 0) + (adjacent(F[j].pos, h->pos) ? 1000 : 0) + F[j].dpt / (F[j].hp > 1 ? F[j].hp : 1) - distance(F[j].pos, h->pos); if (v > tv) { tv = v; tg = F[j].i; } }
  if (tg >= 0 && !adjacent(G.mobs[tg].pos, h->pos) && ranged_try(tg)) return 1;   // (wands never miss, bombs and harmful potions from afar, darts while it comes)
  if ((nf >= 2 || !adj) && tact()) return 1;          // (a crowd, or none beside me yet: first where to stand, how to meet it)
  if (tg >= 0 && kit_fight(tg)) return 1;
  if (tact()) return 1;
  if (tg >= 0) { char w[64]; snprintf(w, sizeof w, "b2: attack the %s", mob_name(G.mobs[tg].type)); int r = b2_step(G.mobs[tg].pos, w); if (r > 0) return r; }
  return -1;
}

// The route: the target worth most over its detour.
static int b2_route(void) {
  Hero *h = &G.hero; bool stairs = G.exit >= 0 && BIT_GET(G.visited, G.exit);
  { int hp = heap_at(h->pos); static int s_tried = -1;   // on a heap I came for: take it (a tap on my cell); still there after a try: aside for a while
    if (hp >= 0 && G.heaps[hp].type < HEAP_CARRIED && G.heaps[hp].type != HEAP_FOR_SALE && h->pos != G.exit && h->pos != G.entrance && s_black[h->pos] <= s_bot_steps && (G.heaps[hp].item.kind != IK_DEW || getenv("OLDB2DEW") || dew_take())) {   // (2026-11-07: the drop a trample leaves under my feet was taken hurt, whatever the dew rule said: half the run's dew went that way)
      if (s_tried != h->pos) { s_tried = h->pos; int r = b2_step(h->pos, "b2: take what lies here"); if (r > 0) return r; }
      s_black[h->pos] = s_bot_steps + 300; s_tried = -1; } else s_tried = -1; }
  { static int ring[10], rn; ring[rn++ % 10] = h->pos; int rep = 0; for (int i = 0; i < 10; i++) if (ring[i] == h->pos) rep++;   // A-B-A-B: the target I keep swapping to goes aside
    if (rep >= 4 && rn >= 10 && s_goal_cell2 >= 0) { s_black[s_goal_cell2] = s_bot_steps + 150; memset(ring, 0xff, sizeof ring); rn = 0; } }
  bfs(h->pos, false);                                   // (the old advisors read b_dist)
  if (!getenv("B2NEWPLAN") && !getenv("B2PLANONLY")) return -1;   // the route advisor: the old rules' whole quiet route (its puzzles, quests, trap prizes, the hungry dive, then its planner)
  if (!getenv("B2NEWPLAN")) { const char *w = "?"; int oc = plan_next(false, &w); if (oc < 0) return -1; static char wb[64]; snprintf(wb, sizeof wb, "b2: plan: %s", w); int r = b2_step(oc, wb); if (r > 0) return r; s_black[oc] = s_bot_steps + 50; return -1; }   // (test: the old planner's choice)
  b2_weights(); b2_dist(h->pos, s_b2dh, true); if (stairs) b2_dist(G.exit, s_b2ds, false);
  int back = stairs ? s_b2dh[G.exit] : 0;
  int best = -1; double bv = 0; const char *bw = "?"; static char bwhy[96];
  #define B2T(cell, val, what) do { int _c = (cell); if ((unsigned)_c < MAP_LEN && s_b2dh[_c] < 0x7FFF && _c != h->pos) { double _det = stairs ? (double)s_b2dh[_c] + (s_b2ds[_c] < 0x7FFF ? s_b2ds[_c] : s_b2dh[_c]) - back : s_b2dh[_c]; double _n = (val) - _det + (_c == s_goal_cell2 ? s_b2keep : 0); if (_n > bv) { bv = _n; best = _c; bw = (what); } } } while (0)
  static int s_tap[MAP_LEN / 8 + 1]; memset(s_tap, 0, sizeof s_tap);   // targets tapped from beside (a grave, a locked door, a well): reached at the nearest cell next to them
  #define B2TT(cell, val, what) do { int _t = (cell); if ((unsigned)_t < MAP_LEN) { if (s_b2dh[_t] < 0x7FFF) B2T(_t, val, what); else { int _b = -1, _bd = 0x7FFF; for (int _k = 0; _k < 8; _k++) { int _q = _t + N8[_k]; if (nb_ok(_t, _q) && s_b2dh[_q] < _bd) { _bd = s_b2dh[_q]; _b = _q; } } \
      if (_b == h->pos) { if ((val) > bv) { best = _t; bv = (val); bw = (what); BIT_SET(s_tap, _t); } } else if (_b >= 0) { int _o = best; B2T(_b, val, what); if (best != _o && best == _b) BIT_SET(s_tap, _b); } } } } while (0)
  for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i)) { const Item *it = &G.heaps[i].item; int k = it->kind, q = it->qty > 0 ? it->qty : 1;
    double v = is_food(k) ? (k == IK_RATION ? 260 : k == IK_PASTY ? 360 : k == IK_MYSTERY_MEAT ? 100 : 150) * q : item_value(it);   // food: the turns it feeds, in the brain's own currency
    static double iv = -1; if (iv < 0) iv = b2k("B2IV", 1); B2T(G.heaps[i].pos, v * iv * (G.heaps[i].type == HEAP_PLAIN ? 1.0 : 1.2), "item"); }
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && BIT_GET(G.fov, m->pos) && npc_wanted(i) && s_black[m->pos] <= s_bot_steps) { int d = 0x7FFF, c0 = -1; for (int k = 0; k < 8; k++) { int q = m->pos + N8[k]; if (nb_ok(m->pos, q) && s_b2dh[q] < d) { d = s_b2dh[q]; c0 = q; } } if (c0 >= 0) B2T(c0, 150, "npc"); } }
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i];   // a cheap kill on the way: its XP (a sleeper: my first blow lands)
    if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & (MD_NPC | MD_BOSS | MD_WATER))) continue;
    double pdm; double c = b2_duel(i, &pdm); static double hk = -1; if (hk < 0) hk = b2k("B2HUNTK", 0);
    if (hk > 0 && pdm < 0.05) c = -(hk + (c < 0 ? -c : 0));   // (B2HUNTK: a monster I'd beat is worth taking out now, on my terms, before it wakes on its own at a bad moment)
    if (c >= 0) continue; int d = 0x7FFF, c0 = -1; for (int k = 0; k < 8; k++) { int q = m->pos + N8[k]; if (nb_ok(m->pos, q) && s_b2dh[q] < d) { d = s_b2dh[q]; c0 = q; } }
    if (c0 >= 0) B2T(c0, -c, "hunt"); }
  { const char *pw = NULL; int pt = puzzle_task(&pw); if (pt == -2) { st_b2[2]++; return 1; } if (pt >= 0) { if (getenv("B2TAP")) B2TT(pt, 150, pw ? pw : "puzzle"); else B2T(pt, 150, pw ? pw : "puzzle"); } }
  { int q = quest_task(); if (q == -2) return 1; if (q >= 0) { if (getenv("B2TAP")) B2TT(q, 200, "quest"); else B2T(q, 200, "quest"); } }
  { int tp = trap_prize(); if (tp == -2) return 1; if (tp >= 0) { if (getenv("B2TAP")) B2TT(tp, 150, "trap prize"); else B2T(tp, 150, "trap prize"); } }
  if (G.keys[G.depth] & 15) for (int c = 0; c < MAP_LEN; c++) if (G.map[c] == T_LOCKED_DOOR && BIT_GET(G.visited, c)) B2TT(c, 300, "locked door");   // (the key in hand: the vault behind it)
  // the unknown: the loot still to find, and the stairs if not found, shared among the frontier cells by the unknown beyond each
  int seen = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) seen++;
  double left = 1 - seen / 420.0; if (left < 0.05) left = 0.05;
  static int fc[MAP_LEN]; static uint8_t fu[MAP_LEN]; int nfc = 0; double su = 0;
  for (int c = 0; c < MAP_LEN; c++) if (frontier(c) && s_b2dh[c] < 0x7FFF) { int u = 0; for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) { int x = cx(c) + dx, y = cy(c) + dy; if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && !BIT_GET(G.visited, y * MAP_W + x)) u++; } if (u) { fc[nfc] = c; fu[nfc++] = u; su += u; } }
  double pool = s_b2loot * left + (stairs ? 0 : s_b2stair) + (!s_ev_food && !boss_depth(G.depth) ? 260 * (left > 0.3 ? 1 : left / 0.3) : 0);   // (a normal floor has its ration: not found yet, it's out there)
  if (!getenv("B2SHARE")) { int nb = -1; double nd = 1e9; for (int i = 0; i < nfc; i++) { double d = s_b2dh[fc[i]] - fu[i] * 0.2 - (fc[i] == s_goal_cell2 ? 3 : 0); if (d < nd) { nd = d; nb = fc[i]; } }   // the nearest edge of the unknown carries the whole worth of exploring on: steady, and the shortest tour
    if (nb >= 0) B2T(nb, pool, "explore"); }
  else for (int i = 0; i < nfc; i++) B2T(fc[i], pool * fu[i] / su * (nfc > 1 ? 2.0 : 1.0), "explore");   // (B2SHARE: the old spread)
  #undef B2T
  #undef B2TT
  if (best >= 0) {
    if (best != h->pos && adjacent(best, h->pos) && !b2_open(best)) { st_b2[2]++; snprintf(bwhy, sizeof bwhy, "b2: %s at %d,%d (beside it: tap it)", bw, cx(best), cy(best)); int r = b2_step(best, bwhy); if (r > 0) return r; s_black[best] = s_bot_steps + 100; return -1; }
    s_goal_cell2 = best;
    if (getenv("B2FAR")) { st_b2[2]++; snprintf(bwhy, sizeof bwhy, "b2: %s at %d,%d (worth %.0f over its walk)", bw, cx(best), cy(best), bv); int r = b2_step(best, bwhy); if (r > 0) return r; s_black[best] = s_bot_steps + 50; return -1; }   // (test: the game walks me there, as the old rules do)
    int st = b2_first_step(best); if (st < 0) { s_black[best] = s_bot_steps + 50; return -1; }
    st_b2[2]++; snprintf(bwhy, sizeof bwhy, "b2: %s at %d,%d (worth %.0f over its walk)", bw, cx(best), cy(best), bv); return b2_step(st, bwhy); }
  s_goal_cell2 = -1;
  if (getenv("B2WHY")) { const char *w = "?"; bfs(h->pos, false); int oc = plan_next(false, &w); printf("B2WHY step %d d%d nothing left (stairs %d); the old plan: %s at %d,%d dist %d\n", s_bot_steps, G.depth, stairs, oc >= 0 ? w : "none", oc >= 0 ? cx(oc) : -1, oc >= 0 ? cy(oc) : -1, oc >= 0 ? b_dist[oc] : -1);  if (oc >= 0 && heap_at(oc) >= 0) { int hh = heap_at(oc); char nm[48]; item_name(&G.heaps[hh].item, nm, sizeof nm); printf("   B2WHY item %s value %d myd %d stairsd %d back %d type %d\n", nm, item_value(&G.heaps[hh].item), s_b2dh[oc], s_b2ds[oc], back, G.heaps[hh].type); } }
  if (!stairs) return -1;                               // nothing left I can see and no stairs: the old rules search for the way on
  if (boss_depth(G.depth + 1)) return -1;               // (the floor before a boss: the old rules prepare for it)
  if (!getenv("B2NEWEND")) return -1;                  // the old rules end the floor: hidden-room searches on evidence, the shop, wells, dead ends, a trap in the way, the weak floor, the stairs
  if (h->HP * 100 < h->HT * s_b2down && !starve_block() && threat_count() == 0) { st_b2[3]++; s_did[0] = 0; DID("b2: top up before the stairs (%d/%d)", h->HP, h->HT); walk(game_rest(false)); return 1; }
  st_b2[3]++; return b2_step(G.exit, "b2: nothing here worth its walk: down");
}


// ---- The floor's worth still unfound, against the moves to find it (owner 2026-11-06: "a decision with the risk of
// consequences weighed against rewards ... we know ALL of the odds"). From the source (content.c level_content): every
// normal floor holds one food (a ration, 1 in 5 a pasty); a Strength potion and an Upgrade scroll on their quotas (the
// odds over how many have spawned so far: q_unseen); an Enchant scroll (depth / 12(1+found) a floor); the dew vial once on
// floors 1-3; and 3 random items, +1 at 40% again and again (Generator: potion 50, scroll 40, gold 50, weapon 15, armour
// 10, seed 5, wand 4, ring 2, misc 5), each on a random cell. What I've seen of the floor (cells, heaps) gives the odds
// of what's still out there; each is priced by item_value, a move by what food is worth to me now.
// Knobs: B2MOVEV (points a move costs with food at ease: a ration's 90 over its 260 turns), B2FOODK (how fast that climbs
// as food runs short), B2MPC (moves to explore an unseen cell), B2FLOORN (cells on a normal floor: 285 measured),
// B2EVK (weight on the floor's worth; 1).
static uint8_t s_fh_seen[MAP_LEN / 8 + 1]; static int s_fh_d = -1, s_fh_game = -1, s_fh_k, s_fh_soe; static long st_evleave;
static void b2_heap_track(void) {
  if (s_fh_d != G.depth || s_fh_game != cur_game) { memset(s_fh_seen, 0, sizeof s_fh_seen); s_fh_d = G.depth; s_fh_game = cur_game; s_fh_k = 0; }
  for (int i = 0; i < G.n_heaps; i++) { Heap *hp = &G.heaps[i]; int c = hp->pos; if (!BIT_GET(G.fov, c) || BIT_GET(s_fh_seen, c) || BIT_GET(s_dropped, c) || hp->type == HEAP_FOR_SALE || hp->type >= HEAP_CARRIED) continue;
    BIT_SET(s_fh_seen, c); int k = hp->item.kind; if (k == IK_SCR_ENCHANT) s_fh_soe++;
    if (!is_food(k) && k != IK_POT_STRENGTH && k != IK_SCR_UPGRADE && k != IK_SCR_ENCHANT && k != IK_DEW_VIAL) s_fh_k++; }   // (a random one: the guaranteed ones are counted apart)
}
static double b2_kind_avg(const uint16_t *W, const uint8_t *K, int n) { double v = 0, w = 0; for (int i = 0; i < n; i++) if (W[i]) { Item it = item_make(K[i], 1); v += W[i] * item_value(&it); w += W[i]; } return w > 0 ? v / w : 0; }
static double b2_move_price(void) {                    // a move's worth in item points: food at ease, a ration's 90 over 260 turns; dearer as it runs short
  static double mv = -1, fk; if (mv < 0) { mv = b2k("B2MOVEV", 90.0 / 260); fk = b2k("B2FOODK", 450); }
  int fb = food_budget(); double sc = fk / (fb > 150 ? fb : 150); if (sc < 0.5) sc = 0.5; if (sc > 4) sc = 4; return mv * sc; }
static double b2_floor_left(double *moves_out, double *s_out) {
  static double mpc = -1, fln, evk; if (mpc < 0) { mpc = b2k("B2MPC", 0.9); fln = b2k("B2FLOORN", 285); evk = b2k("B2EVK", 1); }
  int seen = 0; for (int c = 0; c < MAP_LEN; c++) if (BIT_GET(G.visited, c) && PASSABLE(G.map[c])) seen++;
  double tot = seen + 20 > fln ? seen + 20 : fln, sf = seen / tot; if (sf > 0.99) sf = 0.99; double u = 1 - sf;
  // random items: n ~ 3 + geometric(0.4); each seen with chance sf: E[the unseen | k seen]
  int k = s_fh_k; double num = 0, den = 0; for (int n = 3; n <= 18; n++) { if (n < k) continue; double pn = 0.6 * pow(0.4, n - 3), c = 1; for (int j = 0; j < k; j++) c = c * (n - j) / (j + 1); double lik = c * pow(sf, k) * pow(u, n - k); num += pn * lik * (n - k); den += pn * lik; }
  double eun = den > 0 ? num / den : 0;
  static const uint16_t CW[9] = { 15, 10, 50, 40, 4, 2, 5, 50, 5 };   // weapon, armour, potion, scroll, wand, ring, seed, gold, misc
  double cv[9]; cv[0] = b2k("B2VWPN", 30); cv[1] = b2k("B2VARM", 30); cv[2] = b2_kind_avg(IDB_PW, IDB_PK, 12); cv[3] = b2_kind_avg(IDB_SW, IDB_SK, 12);
  if (!getenv("OLDEVGEAR")) {   // (owner 2026-11-07, the build planner): a weapon or armour here is worth what it could add to this hero: the share of its kinds above what I wear (the generator picks kinds evenly)
    int at = G.hero.armor.kind ? item_tier(G.hero.armor.kind) : 0, wt = G.hero.weapon.kind ? item_tier(G.hero.weapon.kind) + G.hero.weapon.lvl / 3 : 0, na = 0, nwk = 0, ba = 0, bw = 0;
    for (int k = 0; k < IK_COUNT; k++) { int c = item_cat(k); if (c == CAT_ARMOR && !CLASS_ARMOR(k)) { na++; if (item_tier(k) > at) ba++; } else if (c == CAT_WEAPON && k < IK_DART) { nwk++; if (item_tier(k) > wt) bw++; } }
    cv[0] *= nwk ? 2.0 * bw / nwk : 0; cv[1] *= na ? 2.0 * ba / na : 0; }
  cv[4] = b2k("B2VWAND", 120); cv[5] = b2k("B2VRING", 60); cv[6] = b2k("B2VSEED", 15); cv[7] = b2k("B2VGOLD", 0.3) * (30 + 10 * G.depth); cv[8] = b2k("B2VMISC", 20);
  double rv = 0, rw = 0; for (int i = 0; i < 9; i++) { rv += CW[i] * cv[i]; rw += CW[i]; } rv /= rw;
  double v = eun * rv;
  double lam = b2_move_price();
  if (!getenv("OLDEVXP")) {   // (the worth of exploring is more than its items: the monsters still to meet give XP; a level is +11 points at Goo): unseen share x a floor's start (3 + depth%5 + 1) x XP a kill
    double nm = u * (4 + G.depth % 5), xp = 1 + G.depth / 2.0; if (G.hero.lvl > 2 * G.depth + 6) xp *= 0.3;   // (too high a level: a monster's XP stops at its max level)
    v += nm * xp * s_b2xpv * lam * b2k("B2EVXP", 1); }
  if (!s_ev_food) v += 260 * lam * 1.1;                 // the floor's food, not found: it's out there (as many turns of food as it holds)
  { Item a = item_make(IK_POT_STRENGTH, 1), b = item_make(IK_SCR_UPGRADE, 1), e = item_make(IK_SCR_ENCHANT, 1);
    double pp = q_unseen(s_q_pos, Q_POS, G.depth, s_q_fp, u), ps = q_unseen(s_q_sou, Q_SOU, G.depth, s_q_fs, u);
    double pe = G.depth / (12.0 * (1 + s_fh_soe)); if (pe > 1) pe = 1; pe = pe * u / (1 - pe * (1 - u) > 1e-6 ? 1 - pe * (1 - u) : 1e-6);
    v += pp * item_value(&a) + ps * item_value(&b) + pe * item_value(&e); }
  if (G.depth <= 3 && find_kind(IK_DEW_VIAL) < 0 && !(G.dflags & DF_VIAL_SPAWNED)) v += u * 200.0 / (4 - G.depth);   // (the vial: one of floors 1-3; the player knows only that it hasn't turned up)
  if (moves_out) *moves_out = mpc * (tot - seen); if (s_out) *s_out = sf;
  return v * evk;
}
// Leave now? When what the planner would do next is more exploring (or the search for what's hidden), weigh what's
// probably still here against what finding it costs; seen items it still wants are fetched by the planner first.
static int b2_leave_check(void) {
  Hero *h = &G.hero; if (!getenv("B2EV")) return 0;   // (off: 480 games, 5.31 on vs 5.51 off; leaving sooner is worse still: the worth of exploring is more than its items)
  if (boss_depth(G.depth) || boss_depth(G.depth + 1) || G.depth == 21 || G.depth >= 25 || G.exit < 0 || !BIT_GET(G.visited, G.exit)) return 0;
  if (G.map[G.exit] == T_LOCKED_EXIT && !(G.keys[G.depth] & 64)) return 0;
  if (quest_on_floor() || shop_hunt_ok() || s_goal <= G.depth) return 0;
  bfs(h->pos, false); if (b_dist[G.exit] >= 0x7FFF) return 0;
  const char *why = "?"; int c = plan_next(false, &why);
  bool more = c < 0 || !strcmp(why, "explore") || !strcmp(why, "frontier") || !strcmp(why, "dead end") || !strcmp(why, "search");
  if (!more) return 0;
  for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i) && G.heaps[i].item.kind != IK_GOLD && G.heaps[i].item.kind != IK_DEW && b_dist[G.heaps[i].pos] < 0x7FFF) return 0;
  double mv, sf; double v = b2_floor_left(&mv, &sf); if (c < 0) mv = mv > 40 ? mv : 40;   // (the search for a hidden part: a few dozen moves at least)
  double cost = mv * b2_move_price();
  if (getenv("B2EVDBG")) { static int ls = -1; if (ls != s_bot_steps / 20) { ls = s_bot_steps / 20; printf("B2EV d%d step %d seen %.2f k %d left %.0f moves %.0f price %.2f cost %.0f food %d next %s -> %s\n", G.depth, s_bot_steps, sf, s_fh_k, v, mv, b2_move_price(), cost, food_budget(), why, v < cost ? "LEAVE" : "stay"); } }
  if (v >= cost) return 0;
  st_evleave++;
  if (h->HP * 10 < h->HT * 7 && !starve_block() && threat_count() == 0 && !gas_at(h->pos) && !rest_cap()) { s_did[0] = 0; DID("b2: rest before going down"); walk(game_rest(false)); return 1; }
  static char wb[96]; snprintf(wb, sizeof wb, "b2: what's likely left (%.0f) isn't worth the moves (%.0f): down", v, cost); return b2_step(G.exit, wb);
}

// Hurt (owner 2026-11-06): explore while hurt, but don't start fights. Nothing about: the normal route (I heal as I go).
// Something asleep or wandering in view: sneak past it (the old planner's target, over a way that keeps clear of it; never a hunt).
// Something hunting me: well ahead, finish it; else walk on with it in tow; faster, shooting, or cornered: the old fight advisor.
static int b2_hurt(void) {
  Hero *h = &G.hero; static double hu = -2; if (hu < -1) hu = b2k("B2HURT", -1); if (hu <= 0 || h->HP * 100 >= h->HT * hu) return 0;
  bool hunted = false, seen = false; int near[24], nn = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state == ST_PASSIVE || (MOB_DEFS[m->type].flags & (MD_NPC | MD_BOSS))) continue;
    bool see = BIT_GET(G.fov, m->pos); if (!see && !adjacent(m->pos, h->pos)) continue;
    if (m->state == ST_HUNTING || adjacent(m->pos, h->pos)) hunted = true; else { seen = true; if (nn < 24) near[nn++] = m->pos; } }
  if (!hunted && !seen) return 0;
  if (hunted) { int t = b2_threats_m(true); if (t > 0) { st_b2[7]++; return t; } return 0; }   // (tow, or the old fight advisor)
  // sneak: the old planner's target, by a way clear of the sleepers and wanderers
  bfs(h->pos, false);
  for (int tries = 0; tries < 3; tries++) {
    const char *w = "?"; int oc = plan_next(false, &w); if (oc < 0) return -1;
    bool bad = mob_at(oc) >= 0; for (int j = 0; j < nn; j++) if (distance(near[j], oc) <= 1) bad = true;   // (a hunt, or beside one: not now)
    if (bad) { s_black[oc] = s_bot_steps + 40; bfs(h->pos, false); continue; }
    for (int c = 0; c < MAP_LEN; c++) s_b2w[c] = 1;
    for (int j = 0; j < nn; j++) for (int dy = -3; dy <= 3; dy++) for (int dx = -3; dx <= 3; dx++) { int x = cx(near[j]) + dx, y = cy(near[j]) + dy; if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue; int d = dx * dx > dy * dy ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy); s_b2w[y * MAP_W + x] += d <= 1 ? 200 : d == 2 ? 40 : 10; }
    b2_dist(h->pos, s_b2dh, true); int st = b2_first_step(oc); if (st < 0) { s_black[oc] = s_bot_steps + 40; bfs(h->pos, false); continue; }
    for (int j = 0; j < nn; j++) if (distance(near[j], st) <= 1) return -1;   // (no way past without brushing it: the old rules)
    static char wb[80]; snprintf(wb, sizeof wb, "b2: hurt: sneak on to %s, clear of %d", w, nn); st_b2[7]++; return b2_step(st, wb); }
  return -1;
}

static int brain2(void) {
  b2_init(); Hero *h = &G.hero;
  if (boss_depth(G.depth)) { st_b2[4]++; return -1; }  // boss floors: the boss scripts
  if (h->b.paralysis || h->b.frost || h->b.roots || h->b.burning || h->ooze || bad_air(h->pos) || h->b.levitation || h->b.invis) { st_b2[4]++; return -1; }   // physics, and the puzzle runs (levitating, invisible): the old rules
  for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_BOSS) && BIT_GET(G.fov, G.mobs[i].pos)) { st_b2[4]++; return -1; }
  if (IS_WATER(G.map[h->pos]) || s_piranhas) for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_WATER) && distance(G.mobs[i].pos, h->pos) <= 2) { st_b2[4]++; return -1; }   // piranhas: never fought, out of their water (physics: the old rules)
  if (emergency()) return 1;                            // badly hurt: the potions, the escapes
  // Monsters: the fight advisor. By default the old one (its door kites, avoids, kit, look-ahead) for anything in view or beside me;
  // B2NEWFIGHT: the new one (fight only when well ahead, else walk on with it in tow) for those hunting me.
  if (getenv("B2SPEND")) {                              // a risky fight: spend what lowers the risk (a wand charge comes back; a death doesn't): zap / throw from afar before it reaches me
    static double se = -1; if (se < 0) se = b2k("B2SPENDEPS", 0.02);
    for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & (MD_NPC | MD_BOSS | MD_WATER))) continue;
      double pdm; b2_duel(i, &pdm); if (pdm < se) continue;
      if (!adjacent(m->pos, h->pos) && ranged_try(i)) { st_b2[0]++; return 1; }
      if (adjacent(m->pos, h->pos) && kit_fight(i)) { st_b2[0]++; return 1; } } }
  { int r = b2_kite(); if (r != 0) return r; }        // a same-speed hunter I'd risk dying to now: kite it while I heal
  { int r = b2_hurt(); if (r != 0) return r; }
  bool adjm = false; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && !ally(i) && adjacent(G.mobs[i].pos, h->pos) && G.mobs[i].state != ST_PASSIVE) adjm = true;
  if (getenv("B2NEWFIGHT")) { int t = b2_threats(); if (t != 0) { if (t < 0) st_b2[4]++; return t; } }
  else if (getenv("B2TOW") && !adjm) { int t = b2_threats_m(true); if (t > 0) return t; }   // (the tow alone, before the old fight advisor: a same-speed melee hunter I am not well ahead of follows me while I heal on my way)
  if (!getenv("B2NEWFIGHT") || getenv("B2OLDSEEN")) if (enemy_count() > 0 || adjm) { st_b2[0]++; return -1; }   // (the old fight advisor: the rest of decide())
  b2_heap_track();
  if (getenv("B2OLDROUTE")) { st_b2[4]++; return -1; }   // (test: the old route)
  if (eat_rule()) { st_b2[6]++; return 1; }
  { static double lo = -2; if (lo < -1) lo = b2k("B2RESTLO", -1); static bool resting; if (lo > 0 && !game_starving()) { if (h->HP * 100 < h->HT * lo) resting = true; if (h->HP * 100 >= h->HT * (lo + 30 > 100 ? 100 : lo + 30)) resting = false; if (resting) { s_did[0] = 0; DID("b2: rest (test knob B2RESTLO)"); walk(game_rest(false)); return 1; } } }   // (a test: does resting pay at all?)
  if (getenv("RESTK") && !game_starving() && !gas_at(h->pos) && h->HP < h->HT) {   // the formula, piece 2 (owner 2026-11-06): a turn resting vs a turn exploring. Both regenerate the same; exploring meets
    // fights (EATR a turn), resting meets only the wanderers (RESTR a turn). Rest while the death risk it keeps off,
    // r_e (f(x) - f(1)) - r_r f(x), outweighs the turn's food (a ration = RESTK of a run).
    static double K = -1, re, rr; if (K < 0) { K = atof(getenv("RESTK")); re = getenv("EATR") ? atof(getenv("EATR")) : 0.025; rr = getenv("RESTR") ? atof(getenv("RESTR")) : 0.005; }
    double x = (double)h->HP / h->HT, gain = re * (fight_risk(x) - fight_risk(1.0)) - rr * fight_risk(x);
    if (gain > K / 260.0) { s_did[0] = 0; DID("b2: rest (the risk ahead at %d%% health outweighs the food)", (int)(x * 100)); walk(game_rest(false)); return 1; } }
  if (!getenv("B2NOPRE")) { if (step_back() || escape_air() || prep_retreat() || (s_ambush > s_bot_steps && amb_retreat())) { st_b2[4]++; return 1; } }   // the old safety rules: step back off a seed, out of the air, a retreat when hurt from the dark
  if (enemy_count() == 0 && use_items_when_safe()) { st_b2[5]++; return 1; }   // (none in sight, not even asleep: a scroll can wake the floor)
  if (barricade_task()) { st_b2[5]++; return 1; }      // a storage barricade burned: 3-4 items behind it (the painter's Liquid Flame)
  { int r = b2_leave_check(); if (r != 0) { st_b2[3]++; return r; } }   // what's likely left on the floor against the moves to find it
  int r = b2_route(); if (r < 0) st_b2[4]++; return r;
}
