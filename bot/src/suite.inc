// ---- the test suite (Thomas 2026-11-12: chess-engine style test positions) ----
// SUITEDIR=dir: while playing, save a few fight positions per game (the game's own save, its dice and step count).
// SUITE=1 with CAMPLOAD=file: load one, and for every move the hero has, play SUITE_K honest futures (fresh dice, only what
// the player knows) with the old bot playing on until the fight is over (or 60 steps), judged by judge2 (0 if dead). Then ask
// the search and the instinct what they'd do here, and print each one's regret: the best move's value minus its pick's.
// SUITEDEATH: keep the last few threatened positions of the real game; when the hero dies, write them out (the
// moments a run is lost are the ones worth testing; a 1-in-16 sample rarely catches them)
#define SD_RING 10
static PKey s_sd_keys[SD_RING][256]; static uint32_t s_sd_rng[SD_RING]; static int s_sd_steps[SD_RING], s_sd_n, s_sd_game = -1;
static void suite_death_keep(void) {
  if (s_sd_game != cur_game) { s_sd_game = cur_game; s_sd_n = 0; }
  int k = s_sd_n % SD_RING; if (s_sd_n > 0 && s_sd_steps[(s_sd_n - 1) % SD_RING] == s_bot_steps) return;
  game_save(); memcpy(s_sd_keys[k], g_keys, sizeof g_keys); memcpy(&s_sd_rng[k], &g_rng, sizeof g_rng); s_sd_steps[k] = s_bot_steps; s_sd_n++; game_load(); }
static void suite_death_dump(void) {
  if (s_sd_game != cur_game) return;
  for (int i = s_sd_n > SD_RING ? s_sd_n - SD_RING : 0; i < s_sd_n; i++) { int k = i % SD_RING; char fn[300]; snprintf(fn, sizeof fn, "%s/d%04d_s%05d.bin", getenv("SUITEDEATH"), cur_game, s_sd_steps[k]);
    FILE *f = fopen(fn, "wb"); if (f) { fwrite(s_sd_keys[k], sizeof g_keys, 1, f); fwrite(&s_sd_rng[k], sizeof g_rng, 1, f); fwrite(&s_sd_steps[k], sizeof s_sd_steps[k], 1, f); fclose(f); } } }
static void suite_save(void) {
  static int ng = -1, cnt; if (ng != cur_game) { ng = cur_game; cnt = 0; }
  if (cnt >= (getenv("SUITEN") ? atoi(getenv("SUITEN")) : 4) || G.depth % 5 == 0) return;
  Hero *h = &G.hero; bool threat = false;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && m->state == ST_HUNTING && !(MOB_DEFS[m->type].flags & MD_NPC) && distance(m->pos, h->pos) <= 6) threat = true; }
  if (!threat || ((uint32_t)(s_bot_steps * 2654435761u + cur_game * 40503u) >> 28) != 0) return;   // (1 in 16, without touching the dice)
  cnt++; game_save(); char fn[300]; snprintf(fn, sizeof fn, "%s/g%04d_s%05d.bin", getenv("SUITEDIR"), cur_game, s_bot_steps);
  FILE *f = fopen(fn, "wb"); if (f) { fwrite(g_keys, sizeof g_keys, 1, f); fwrite(&g_rng, sizeof g_rng, 1, f); fwrite(&s_bot_steps, sizeof s_bot_steps, 1, f); fclose(f); }
  game_load(); }
static double suite_run(int id, uint32_t seed) {   // one honest future after move id, the old bot playing on; judged
  int fds[2]; if (pipe(fds)) return -1; fflush(stdout); pid_t pid = fork(); if (pid < 0) { close(fds[0]); close(fds[1]); return -1; }
  if (pid == 0) { close(fds[0]); s_rollout = 1; V = 0; int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); }
    g_rng ^= seed * 2654435761u; srand(seed); la_scrub(seed); Hero *h = &G.hero; double v = -1;
    static int sst = -1; if (sst < 0) sst = getenv("SUITE_STEPS") ? atoi(getenv("SUITE_STEPS")) : 60;
    if (mc_apply(id)) { int calm = 0, k = 0; for (k = 0; k < sst && h->HP > 0 && !s_pend_depth && calm < 3; k++) { if (!la_step()) { if (getenv("SUITEDBG") && seed == 7777u) fprintf(stderr, "  la_step false at k %d: %s\n", k, s_did); break; } calm = hostile_near(8, NULL) ? 0 : calm + 1; } if (getenv("SUITEDBG") && seed == 7777u) fprintf(stderr, "  move %d: %d steps, calm %d, pend %d\n", id, k, calm, s_pend_depth); v = h->HP <= 0 ? 0 : judge2(); if (getenv("SUITEDBG") && seed == 7777u) fprintf(stderr, "  after move %d: hp %d lvl %d exp %d foes-owe? t %d seen? judge2 %.3f\n", id, h->HP, h->lvl, h->exp, (int)(G.now / TICK), v); }
    if (write(fds[1], &v, sizeof v)) {} _exit(0); }
  close(fds[1]); double v = -1; if (read(fds[0], &v, sizeof v) != sizeof v) v = -1; close(fds[0]); int st; waitpid(pid, &st, 0); return v; }
static int suite_pick(int which) {   // 0: the search's move, 1: the instinct's
  int fds[2]; if (pipe(fds)) return -1; fflush(stdout); pid_t pid = fork(); if (pid < 0) return -1;
  if (pid == 0) { close(fds[0]); int dn = open("/dev/null", O_WRONLY); if (dn >= 0) { dup2(dn, 1); close(dn); } int id = -1;
    if (which == 0) { s_mc_last_id = -1; mcts(); id = s_mc_last_id; }
    else { s_ch_prior = 1; s_pr_n = 0; choose(); s_ch_prior = 0; double bv = 1e18; for (int j = 0; j < s_pr_n; j++) if (s_pr_v[j] < bv) { bv = s_pr_v[j]; id = s_pr_id[j]; } }
    if (write(fds[1], &id, sizeof id)) {} _exit(0); }
  close(fds[1]); int id = -1; if (read(fds[0], &id, sizeof id) != sizeof id) id = -1; close(fds[0]); int st; waitpid(pid, &st, 0); return id; }
static void suite_eval(void) {
  Hero *h = &G.hero; int mv[256]; int n = mc_moves(mv); int K = getenv("SUITE_K") ? atoi(getenv("SUITE_K")) : 200;
  double val[256], se[256]; int best = -1;
  for (int i = 0; i < n; i++) { double s = 0, s2 = 0; int c = 0; for (int r = 0; r < K; r++) { double v = suite_run(mv[i], 7777u + r * 104729u); if (v < 0) { c = -1; break; } s += v; s2 += v * v; c++; }
    if (c <= 0) { val[i] = -1; se[i] = 0; continue; } val[i] = s / c; se[i] = sqrt((s2 / c - val[i] * val[i]) / c); if (best < 0 || val[i] > val[best]) best = i; }
  int ps = suite_pick(0), pi = suite_pick(1); double vs = -1, vi = -1;
  for (int i = 0; i < n; i++) { if (mv[i] == ps) vs = val[i]; if (mv[i] == pi) vi = val[i]; }
  int mi = -1, md = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && !ally(i) && BIT_GET(G.fov, m->pos) && distance(m->pos, h->pos) < md) { md = distance(m->pos, h->pos); mi = i; } }
  printf("SUITE %s d%d hp %d/%d nearest %s(%d hp, %d away) moves %d | best %d %.3f+-%.3f | search %d %.3f regret %.3f | instinct %d %.3f regret %.3f\n", getenv("CAMPLOAD"), G.depth, h->HP, h->HT,
    mi >= 0 ? mob_name(G.mobs[mi].type) : "-", mi >= 0 ? G.mobs[mi].HP : 0, md, n, best >= 0 ? mv[best] : -1, best >= 0 ? val[best] : -1, best >= 0 ? se[best] : 0, ps, vs, vs >= 0 && best >= 0 ? val[best] - vs : -1, pi, vi, vi >= 0 && best >= 0 ? val[best] - vi : -1);
  if (getenv("SUITEV")) for (int i = 0; i < n; i++) printf("  move %d %.3f +- %.3f\n", mv[i], val[i], se[i]);
  fflush(stdout); }
