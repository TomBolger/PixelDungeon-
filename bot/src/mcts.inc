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
// the fight still owed at a leaf: each awake foe in sight, its HP over my damage a turn, times its damage a turn (the game's odds)
static double mc_owed(void) {
  Hero *h = &G.hero; double owed = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & MD_NPC)) continue;
    double hd = hero_dpt(m); owed += m->HP / (hd > 0.2 ? hd : 0.2) * foe_dpt(m); }
  (void)h; return owed; }
// the leaf: if a fight is still on, the rest of it is played out with the dice model (ch_mc: the game's hit and damage
// rolls, calibrated against 57k real encounters) standing my ground from here; the judge takes what's left of me
static double mc_leaf(void) { Hero *h = &G.hero; if (h->HP <= 0) return 0;
  TFoe F[24]; int nf = 0;
  for (int i = 0; i < G.n_mobs && nf < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || m->state != ST_HUNTING || (MOB_DEFS[m->type].flags & MD_NPC) || distance(m->pos, h->pos) > 12) continue;
    TFoe *f = &F[nf++]; f->wat = MOB_DEFS[m->type].flags & MD_WATER; f->i = i; f->pos = m->pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi;
    f->acts = mob_move_cost(m) * 2 <= hero_move_cost() ? 2 : 1; f->mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (f->mc < 5) f->mc = 5;
    f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = h->time - m->time; if (f->tm > TICK) f->tm = TICK; if (f->tm < -4 * TICK) f->tm = -4 * TICK; f->rng = magic_shooter(m->type) || m->type == MT_SCORPIO; }
  if (nf == 0 || getenv("MC_OWED")) { double hp = h->HP + (vial_full() ? h->HT : 0) - (nf ? mc_owed() : 0); if (hp > h->HT) hp = h->HT; return judge_value(hp); }
  ch_prep(F, nf); c_seed = (uint32_t)rand(); double E = ch_mc(F, nf, h->pos, NULL, 0, -1, 0, 16, h->HP); double pd = s_te_pd;
  double hp = h->HP + (vial_full() ? h->HT : 0) - E; if (hp > h->HT) hp = h->HT; if (hp < 1) hp = 1;
  return (1 - pd) * judge_value(hp); }
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
  return n; }
static bool mc_apply(int id) {
  Hero *h = &G.hero; int32_t t0 = h->time; int d0 = G.depth, hp = h->pos;
  int foes[24], nf = 0; for (int i = 0; i < G.n_mobs && nf < 24; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && !(MOB_DEFS[m->type].flags & MD_NPC)) foes[nf++] = i; }
  if (id < 8) walk(game_tap_cell(hp + N8[id]));
  else if (id == 8) walk(game_rest(false));
  else if (id == 9) walk(game_tap_cell(hp));
  else if (id < 512) { int s = (id - 16) / 4, u = (id - 16) % 4; do_item(s, u == 0 ? ACN_DRINK : u == 1 ? ACN_READ : u == 2 ? ACN_EAT : ACN_PLANT); }
  else if (id < 4096) { int s = (id - 512) / 32, j = (id - 512) % 32; if (j >= nf) return false; walk(game_throw(s, G.mobs[foes[j]].pos)); }
  else { int s = (id - 4096) / 32, j = (id - 4096) % 32; if (j >= nf) return false; walk(game_zap(s, G.mobs[foes[j]].pos)); }
  for (int g = 0; g < 4 && (s_pick >= 0 || s_wnd_btn); g++) la_step();   // (a picker or window a move opened: answered as ever)
  return h->time != t0 || G.depth != d0 || s_pend_depth; }
static bool mc_calm(void) { for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC)) return false; } return true; }   // (no foe hunting me at all, seen or not: the search judged hiding round a corner as the fight won)
typedef struct { int32_t first; int32_t next; int32_t id; int32_t n; double w; } MNode;   // first child, next sibling
#define MC_MAXN 60000
static MNode s_mn[MC_MAXN]; static int s_mnn;
static int mc_child(int p, int id) { for (int c = s_mn[p].first; c >= 0; c = s_mn[c].next) if (s_mn[c].id == id) return c; return -1; }
static int mc_add(int p, int id) { if (s_mnn >= MC_MAXN) return -1; int c = s_mnn++; s_mn[c] = (MNode){ -1, s_mn[p].first, id, 0, 0 }; s_mn[p].first = c; return c; }
static long st_mc_calls, st_mc_iters;
static int mcts(void) {
  Hero *h = &G.hero; if (s_rollout || s_is_child || getenv("NOMCTS") || h->b.paralysis) return 0;
  bool threat = false; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && (BIT_GET(G.fov, m->pos) || adjacent(m->pos, h->pos)) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC)) threat = true; }
  if (!threat) return 0;
  static int iters = -1, horizon, calmk; static double cu, pw; if (iters < 0) { pw = getenv("MC_PW") ? atof(getenv("MC_PW")) : 1.5; iters = getenv("MC_IT") ? atoi(getenv("MC_IT")) : 300; horizon = getenv("MC_H") ? atoi(getenv("MC_H")) : 20; cu = getenv("MC_C") ? atof(getenv("MC_C")) : 0.3; calmk = 2; }
  int mv0[128]; int n0 = mc_moves(mv0); if (n0 <= 1) return 0;
  s_mnn = 1; s_mn[0] = (MNode){ -1, -1, -1, 0, 0 };
  uint32_t base = (uint32_t)(cur_game * 1000003u + s_bot_steps * 7919u);
  st_mc_calls++;
  for (int it = 0; it < iters; it++) {
    int fds[2]; if (pipe(fds)) break; fflush(stdout); pid_t pid = fork(); if (pid < 0) { close(fds[0]); close(fds[1]); break; }
    if (pid == 0) {   // the child: one walk down the tree in its own copy of the game
      close(fds[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); }
      uint32_t seed = base + it * 104729u; g_rng ^= seed * 2654435761u; srand(seed); la_scrub(seed);
      int path[64], np = 0, node = 0; double val = -1; int calm = 0;
      for (int dep = 0; dep < horizon; dep++) {
        if (h->HP <= 0) { val = 0; break; }
        if (s_pend_depth) { val = mc_leaf(); break; }
        if (mc_calm()) { if (++calm >= calmk) { val = mc_leaf(); break; } } else calm = 0;
        int mv[128]; int n = mc_moves(mv); if (!n) { val = mc_leaf(); break; }
        // UCT over the moves open here; one not yet tried is tried first (in a random order)
        int untried[128], nu = 0; for (int i = 0; i < n; i++) if (node < 0 || mc_child(node, mv[i]) < 0) untried[nu++] = mv[i];
        int pick; int nch = n - nu;
        // progressive widening (MC_PW): a node opens a new move only while it has fewer than PW*sqrt(visits) children, so
        // the tries go deeper down the good lines instead of one level across every move
        bool widen = nu > 0 && (node < 0 || nch < 1 || nch < pw * sqrt((double)(s_mn[node].n + 1)));
        if (widen) { pick = untried[rand() % nu]; path[np++] = pick; if (!mc_apply(pick)) { val = -2; break; }
          // the new leaf: judged as it stands, the fight still owed taken off
          if (h->HP <= 0) val = 0; else val = mc_leaf(); break; }
        double bv = -1e9; int bc = -1, bid = -1; double ln = log((double)(s_mn[node].n > 0 ? s_mn[node].n : 1));
        for (int i = 0; i < n; i++) { int c = mc_child(node, mv[i]); if (c < 0) continue; double u = s_mn[c].w / s_mn[c].n + cu * sqrt(ln / s_mn[c].n); if (u > bv) { bv = u; bc = c; bid = mv[i]; } }
        if (bc < 0) { val = mc_leaf(); break; }
        path[np++] = bid; if (!mc_apply(bid)) { val = -2; break; } node = bc; }
      if (val < -1.5) { np = 0; val = -1; }   // (a move that didn't take: nothing learnt)
      if (val < 0) val = mc_leaf();
      double v = val;
      if (write(fds[1], &np, sizeof np) != sizeof np || write(fds[1], path, sizeof(int) * np) != (ssize_t)(sizeof(int) * np) || write(fds[1], &v, sizeof v) != sizeof v) {}
      _exit(0); }
    close(fds[1]); int np = 0; int path[64]; double v = 0; bool ok = read(fds[0], &np, sizeof np) == sizeof np && np >= 0 && np <= 64 && read(fds[0], path, sizeof(int) * np) == (ssize_t)(sizeof(int) * np) && read(fds[0], &v, sizeof v) == sizeof v;
    close(fds[0]); int st; waitpid(pid, &st, 0); if (!ok || np == 0) continue;
    st_mc_iters++;
    int node = 0; s_mn[0].n++; s_mn[0].w += v;
    for (int i = 0; i < np; i++) { int c = mc_child(node, path[i]); if (c < 0) c = mc_add(node, path[i]); if (c < 0) break; s_mn[c].n++; s_mn[c].w += v; node = c; } }
  // the move: the best average among those tried at least a tenth as often as the most tried (the most-tried alone follows early luck)
  int best = -1, bn = 0; for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].n > bn) bn = s_mn[c].n;
  double bv0 = -1; for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].n * 10 >= bn && s_mn[c].w / s_mn[c].n > bv0) { bv0 = s_mn[c].w / s_mn[c].n; best = c; }
  if (best < 0) return 0;
  if (getenv("MCDBG")) { printf("MC d%d t%d hp %d/%d iters %d nodes %d:", G.depth, (int)(G.now / TICK), h->HP, h->HT, s_mn[0].n, s_mnn);
    for (int c = s_mn[0].first; c >= 0; c = s_mn[c].next) if (s_mn[c].n >= 5) printf(" [%d n%d v%.3f]", s_mn[c].id, s_mn[c].n, s_mn[c].w / s_mn[c].n); printf("\n"); }
  int id = s_mn[best].id; s_did[0] = 0; DID("search: move %d (%d of %d tries, judged %.3f)", id, s_mn[best].n, s_mn[0].n, s_mn[best].w / s_mn[best].n);
  int32_t t0 = h->time; int d0 = G.depth; mc_apply(id); if (h->time == t0 && G.depth == d0 && !s_pend_depth) return 0;
  return 1; }
