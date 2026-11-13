// ---- Training (owner 2026-11-13: "put in the days of search"): AlphaZero-style on one seed.
// The search plays; at every search decision it writes what it found (TR lines: the position for the judge, and for each
// root move the tries it got, its average and the features the instinct reads). Between rounds a trainer fits:
//  - the instinct (policy): a softmax over the root moves, from features of each move (the old dice-model instinct's own
//    opinion is one feature, so round 0 is exactly the old instinct), towards where the search spent its tries;
//  - the judge (value): judge2's logistic again, from the positions and how each line ended.
// The next round's search starts from the new instinct and judge.
// Rollback (training only; owner: allowed in training, never in the final bot): every RB_EVERY steps the game is kept in a
// sleeping copy (a forked process waiting on a pipe). When the hero dies, the newest copy with tries left wakes, and plays on
// with fresh dice and a different search seed; after RB_TRIES tries a copy is spent and the one before it is used.
// Every line writes its own records; the trainer labels each by how that line went.

// ---- the instinct's features of a root move ----
#define NPF 16
static double s_polw[NPF]; static int s_polw_on = -1;
static void pol_load(void) {
  s_polw_on = 0; memset(s_polw, 0, sizeof s_polw); s_polw[0] = 1;   // (round 0: the old instinct alone)
  const char *fn = getenv("POLW"); if (!fn) return; FILE *f = fopen(fn, "r"); if (!f) return;
  int n = 0; for (int i = 0; i < NPF; i++) if (fscanf(f, "%lf", &s_polw[i]) == 1) n++; fclose(f); s_polw_on = n == NPF; if (n != NPF) { memset(s_polw, 0, sizeof s_polw); s_polw[0] = 1; } }
static int pol_foe_dist(int c) {   // the nearest awake foe I can see, in steps (Chebyshev), from cell c
  int best = 99; for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & MD_NPC) || m->state == ST_SLEEPING) continue; int d = distance(c, m->pos); if (d < best) best = d; }
  return best; }
static void pol_feats(int id, double prior, double *phi) {
  Hero *h = &G.hero; memset(phi, 0, sizeof(double) * NPF);
  double hurt = h->HT > 0 ? 1.0 - (double)h->HP / h->HT : 0; int d0 = pol_foe_dist(h->pos); if (d0 > 8) d0 = 8;
  int adjf = 0; for (int k = 0; k < 8; k++) { int q = h->pos + N8[k]; int mi = nb_ok(h->pos, q) ? mob_at(q) : -1; if (mi >= 0 && !ally(mi) && !(MOB_DEFS[G.mobs[mi].type].flags & MD_NPC)) adjf++; }
  phi[0] = log(prior > 1e-4 ? prior : 1e-4);
  int cell = -1;
  if (id < 8) { int q = h->pos + N8[id]; int mi = mob_at(q); if (mi >= 0 && !ally(mi)) { phi[1] = 1; phi[10] = hurt; phi[14] = adjf; } else { phi[2] = 1; cell = q; } }
  else if (id == 8) phi[3] = 1;
  else if (id == 9) phi[4] = 1;
  else if (id < 512) { phi[5] = 1; phi[12] = hurt; int s = (id - 16) / 4, u = (id - 16) % 4; if (s >= 0 && s < BAG_SIZE && u == 0 && h->bag[s].kind == IK_POT_HEALING && item_known(IK_POT_HEALING)) phi[13] = 1; }
  else if (id < 4096) phi[6] = 1;
  else if (id < 8192) phi[7] = 1;
  else { phi[8] = 1; cell = id - 8192; }
  if (cell >= 0 && (unsigned)cell < MAP_LEN) { int d1 = pol_foe_dist(cell); if (d1 > 8) d1 = 8; double dd = d1 - d0; if (dd > 3) dd = 3; if (dd < -3) dd = -3; phi[9] = dd; if (dd > 0) phi[11] = hurt;
    if (s_mc_goal >= 0) { int g0 = distance(h->pos, s_mc_goal), g1 = distance(cell, s_mc_goal); phi[15] = g0 > g1 ? 1 : g0 < g1 ? -1 : 0; } }
}
// the learned instinct over the root moves (s_rid/s_rp hold the old instinct's shares on entry): kept with the features for the record
static double s_tr_phi[256][NPF], s_tr_p0[256];
static void pol_apply(void) {
  if (s_polw_on < 0) pol_load();
  double z[256], mx = -1e18;
  for (int i = 0; i < s_rn && i < 256; i++) { s_tr_p0[i] = s_rp[i]; pol_feats(s_rid[i], s_rp[i], s_tr_phi[i]); z[i] = 0; for (int k = 0; k < NPF; k++) z[i] += s_polw[k] * s_tr_phi[i][k]; if (z[i] > mx) mx = z[i]; }
  if (!s_polw_on) return;
  double tot = 0; for (int i = 0; i < s_rn && i < 256; i++) { z[i] = exp(z[i] - mx); tot += z[i]; }
  for (int i = 0; i < s_rn && i < 256; i++) { double p = z[i] / tot; s_rp[i] = p < 0.005 ? 0.005 : p; }
  tot = 0; for (int i = 0; i < s_rn; i++) tot += s_rp[i]; for (int i = 0; i < s_rn; i++) s_rp[i] /= tot; }

// ---- rollback ----
typedef struct { int pid, wfd, tries, step, alive; } RbSlot;
typedef struct { volatile int done, nbranch, lines; RbSlot s[64]; int n; } RbShared;
static RbShared *s_rb; static int s_rb_root = -1, s_rb_branch, s_rb_game = -1;
static int rb_k(const char *n, int d) { return getenv(n) ? atoi(getenv(n)) : d; }
static void rb_line_end(int died) {   // this line's end, for the trainer
  printf("TRE %d g%d died %d depth %d maxd %d turn %d steps %d lvl %d\n", s_rb_branch, cur_game, died, G.depth, G.max_depth, (int)(G.now / TICK), s_bot_steps, G.hero.lvl); fflush(stdout); }
static void rb_kill_all(void) { if (!s_rb) return; for (int i = 0; i < s_rb->n; i++) if (s_rb->s[i].alive) { int m = -1; if (write(s_rb->s[i].wfd, &m, sizeof m)) {} s_rb->s[i].alive = 0; } }
static void rb_new_game(void) {
  if (s_rb) munmap(s_rb, sizeof *s_rb);
  s_rb = mmap(NULL, sizeof *s_rb, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); if (s_rb == MAP_FAILED) { s_rb = NULL; return; }
  memset(s_rb, 0, sizeof *s_rb); s_rb_root = getpid(); s_rb_branch = 0; s_rb_salt = 0; s_rb_game = cur_game; signal(SIGPIPE, SIG_IGN); }
// each step of the real game (not a look-ahead): a sleeping copy every RB_EVERY steps
static void rb_tick(void) {
  if (!getenv("RB") || s_is_child || s_rollout) return;
  if (s_rb_game != cur_game) rb_new_game(); if (!s_rb) return;
  static int last = -1; int every = rb_k("RB_EVERY", 40);
  if (s_bot_steps == last || s_bot_steps % every || G.hero.HP <= 0) return; last = s_bot_steps;
  int keep = rb_k("RB_K", 6), alive = 0; for (int i = 0; i < s_rb->n; i++) alive += s_rb->s[i].alive;
  while (alive >= keep) { for (int i = 0; i < s_rb->n; i++) if (s_rb->s[i].alive) { int m = -1; if (write(s_rb->s[i].wfd, &m, sizeof m)) {} s_rb->s[i].alive = 0; alive--; break; } }
  if (s_rb->n >= 64) return;
  int slot = s_rb->n; int p[2]; if (pipe(p)) return; fflush(stdout); pid_t pid = fork(); if (pid < 0) { close(p[0]); close(p[1]); return; }
  if (pid == 0) {   // the copy: sleeps until a line dies and wakes it; each wake starts a new line from here
    signal(SIGCHLD, SIG_IGN);
    for (;;) { int seed; if (read(p[0], &seed, sizeof seed) != sizeof seed || seed < 0) _exit(0);
      fflush(stdout); pid_t c = fork(); if (c < 0) continue;
      if (c == 0) { signal(SIGCHLD, SIG_DFL); s_rb_salt = (uint32_t)seed * 2654435761u + 1; s_rb_branch = __sync_add_and_fetch(&s_rb->nbranch, 1); __sync_add_and_fetch(&s_rb->lines, 1);
        if (!getenv("RB_SAMEDICE")) g_rng ^= s_rb_salt;   // fresh dice from here on
        printf("TRB %d g%d from step %d (copy %d, try %d)\n", s_rb_branch, cur_game, s_bot_steps, slot, s_rb->s[slot].tries); fflush(stdout);
        last = s_bot_steps; return; } } }
  close(p[0]); RbSlot *S = &s_rb->s[slot]; S->pid = pid; S->wfd = p[1]; S->tries = 0; S->step = s_bot_steps; S->alive = 1; s_rb->n++; }
// the hero died in this line: wake the newest copy with tries left (this line then ends); false: none left, this is the last line
static bool rb_on_death(void) {
  if (!getenv("RB") || !s_rb || s_is_child) return false;
  rb_line_end(1);
  int tries = rb_k("RB_TRIES", 3), maxl = rb_k("RB_MAXLINES", 40);
  if (s_rb->lines < maxl) for (int i = s_rb->n - 1; i >= 0; i--) { RbSlot *S = &s_rb->s[i]; if (!S->alive) continue;
    if (S->tries >= tries) { int m = -1; if (write(S->wfd, &m, sizeof m)) {} S->alive = 0; continue; }
    S->tries++; int seed = (int)(((uint32_t)rand() ^ (uint32_t)(s_rb->lines * 7919u) ^ (uint32_t)cur_game) & 0x7FFFFFFF);
    if (write(S->wfd, &seed, sizeof seed) != sizeof seed) { S->alive = 0; continue; }
    fflush(stdout);
    if (getpid() == s_rb_root) { while (!s_rb->done) usleep(200000); _exit(0); }   // (the first process waits for the last line: the runner counts it)
    _exit(0); }
  return false; }
static void rb_final(void) {   // the last line (the hero won, or no copy is left): the copies are let go, and the first process may end
  if (!getenv("RB") || !s_rb || s_is_child) return;
  if (G.hero.HP > 0) rb_line_end(0);
  rb_kill_all(); fflush(stdout); s_rb->done = 1; }

// ---- the record of a search decision ----
static void tr_record(int chosen) {
  if (!getenv("TREC") || s_is_child || s_rollout) return;
  double f[51]; j2_feats(f);
  printf("TR %d g%d s%d t%d d%d hp %d/%d ch %d |", s_rb_branch, cur_game, s_bot_steps, (int)(G.now / TICK), G.depth, G.hero.HP, G.hero.HT, chosen);
  for (int i = 0; i < 51; i++) printf(" %.4g", f[i]);
  printf(" | %d", s_rn);
  for (int i = 0; i < s_rn && i < 256; i++) { int c = mc_child(0, s_rid[i]); int n = c >= 0 && s_mn[c].w > -500 ? s_mn[c].n : 0; double q = n ? s_mn[c].w / n : 0;
    printf(" %d:%d:%.4f", s_rid[i], n, q); for (int k = 0; k < NPF; k++) printf(":%.3g", s_tr_phi[i][k]); }
  printf("\n"); }
static void tr_snap(void) {   // the judge's inputs every 25 steps, fights or not (the judge needs quiet positions too)
  if (!getenv("TREC") || s_is_child || s_rollout || s_bot_steps % 25) return;
  static int last = -1; if (last == s_bot_steps) return; last = s_bot_steps;
  double f[51]; j2_feats(f); printf("TS %d g%d s%d t%d d%d |", s_rb_branch, cur_game, s_bot_steps, (int)(G.now / TICK), G.depth); for (int i = 0; i < 51; i++) printf(" %.4g", f[i]); printf("\n"); }
// ---- the trainer (TRAIN=dir: reads every *.txt there, writes polw.txt and jw2.txt next to them; no Python needed) ----
typedef struct { int nm; int ch; float *phi; float *pi; } TrDec;
static int tr_cmp_dummy;
static void tr_train(void) {
  // TRAIN=dir[:dir...]: the rounds' records (the newest first; the last few rounds, as AlphaZero keeps a window of recent games);
  // the weights are written to TRAINOUT (default: the first dir)
  const char *dirs = getenv("TRAIN"); char cmd[4096] = "ls"; { char b[2048]; snprintf(b, sizeof b, "%s", dirs); for (char *s2 = NULL, *e = strtok_r(b, ":", &s2); e; e = strtok_r(NULL, ":", &s2)) { size_t l = strlen(cmd); snprintf(cmd + l, sizeof cmd - l, " %s/*.txt", e); } }
  strncat(cmd, " 2>/dev/null", sizeof cmd - strlen(cmd) - 1);
  char dir[1024]; if (getenv("TRAINOUT")) snprintf(dir, sizeof dir, "%s", getenv("TRAINOUT")); else { snprintf(dir, sizeof dir, "%s", dirs); char *c = strchr(dir, ':'); if (c) *c = 0; }
  FILE *ls = popen(cmd, "r"); if (!ls) return;
  int cap = 1 << 16, nd = 0; TrDec *D = malloc(sizeof(TrDec) * cap);
  int vcap = 1 << 18, nv = 0; float (*VX)[51] = malloc(sizeof(float) * 51 * vcap); int *vb = malloc(sizeof(int) * vcap), *vt = malloc(sizeof(int) * vcap), *vd = malloc(sizeof(int) * vcap); char (*vg)[64] = malloc(64 * vcap);
  typedef struct { char key[64]; int died, maxd, turn; } End; int ecap = 1 << 14, ne = 0; End *E = malloc(sizeof(End) * ecap);
  char fn[600]; static char ln[1 << 20];
  while (fgets(fn, sizeof fn, ls)) { fn[strcspn(fn, "\n")] = 0; FILE *f = fopen(fn, "r"); if (!f) continue;
    while (fgets(ln, sizeof ln, f)) {
      if (!strncmp(ln, "TRE ", 4) && ne < ecap) { int b, g, died, d, md, t; if (sscanf(ln, "TRE %d g%d died %d depth %d maxd %d turn %d", &b, &g, &died, &d, &md, &t) == 6) { snprintf(E[ne].key, 64, "%s:%d:%d", fn, g, b); E[ne].died = died; E[ne].maxd = md; E[ne].turn = t; ne++; } continue; }
      bool isr = !strncmp(ln, "TR ", 3), iss = !strncmp(ln, "TS ", 3); if (!isr && !iss) continue;
      int b, g, s, t, d; if (sscanf(ln + 3, "%d g%d s%d t%d d%d", &b, &g, &s, &t, &d) != 5) continue;
      char *p = strchr(ln, '|'); if (!p) continue; p++;
      if (nv < vcap) { char *q = p; int ok = 1; for (int i = 0; i < 51; i++) { char *e; VX[nv][i] = strtof(q, &e); if (e == q) { ok = 0; break; } q = e; } if (ok) { vb[nv] = b; vt[nv] = t; vd[nv] = d; snprintf(vg[nv], 64, "%s:%d:%d", fn, g, b); nv++; } }
      if (!isr || nd >= cap) continue;
      int ch = -1; char *c = strstr(ln, " ch "); if (c) ch = atoi(c + 4);
      char *q = strchr(p, '|'); if (!q) continue; q++; int nm = strtol(q, &q, 10); if (nm <= 1 || nm > 256) continue;
      TrDec *T = &D[nd]; T->nm = nm; T->phi = malloc(sizeof(float) * nm * NPF); T->pi = malloc(sizeof(float) * nm); double tot = 0; int ok = 1;
      for (int m = 0; m < nm; m++) { while (*q == ' ') q++; int id = strtol(q, &q, 10); if (*q != ':') { ok = 0; break; } q++; int n = strtol(q, &q, 10); q++; strtod(q, &q);
        for (int k = 0; k < NPF; k++) { if (*q != ':') { ok = 0; break; } q++; T->phi[m * NPF + k] = strtof(q, &q); } if (!ok) break; T->pi[m] = n; tot += n; (void)id; }
      if (!ok || tot < 10) { free(T->phi); free(T->pi); continue; }
      for (int m = 0; m < nm; m++) T->pi[m] /= tot; T->ch = ch; nd++; }
    fclose(f); }
  pclose(ls);
  printf("TRAIN: %d search decisions, %d judge positions, %d line ends\n", nd, nv, ne);
  // the instinct: softmax regression towards the search's shares of tries, from the current weights
  pol_load(); double w[NPF]; memcpy(w, s_polw, sizeof w); double lr = getenv("TR_LR") ? atof(getenv("TR_LR")) : 0.05, l2 = 1e-3;
  for (int ep = 0; ep < 60 && nd > 0; ep++) { double g[NPF] = { 0 }, loss = 0;
    for (int i = 0; i < nd; i++) { TrDec *T = &D[i]; double z[256], mx = -1e18; for (int m = 0; m < T->nm; m++) { z[m] = 0; for (int k = 0; k < NPF; k++) z[m] += w[k] * T->phi[m * NPF + k]; if (z[m] > mx) mx = z[m]; }
      double s = 0; for (int m = 0; m < T->nm; m++) { z[m] = exp(z[m] - mx); s += z[m]; }
      for (int m = 0; m < T->nm; m++) { double p = z[m] / s; if (T->pi[m] > 0) loss -= T->pi[m] * log(p > 1e-12 ? p : 1e-12); for (int k = 0; k < NPF; k++) g[k] += (p - T->pi[m]) * T->phi[m * NPF + k]; } }
    for (int k = 0; k < NPF; k++) w[k] -= lr * (g[k] / nd + l2 * (w[k] - (k == 0 ? 1 : 0)));
    if (ep % 20 == 0 || ep == 59) printf("TRAIN policy epoch %d loss %.4f\n", ep, loss / nd); }
  snprintf(fn, sizeof fn, "%s/polw.txt", dir); FILE *o = fopen(fn, "w"); if (o) { for (int k = 0; k < NPF; k++) fprintf(o, "%.6f\n", w[k]); fclose(o); }
  printf("TRAIN policy weights:"); for (int k = 0; k < NPF; k++) printf(" %.3f", w[k]); printf("\n");
  // the judge: two logistic fits (past the next boss; dead within 50 turns), from the current weights, labelled by each line's end
  double pb[51], d5[51]; jw_load(); if (s_jw_on) { memcpy(pb, s_jw_pb, sizeof pb); memcpy(d5, s_jw_d5, sizeof d5); } else { memcpy(pb, J2_PB, sizeof pb); memcpy(d5, J2_D50, sizeof d5); }
  int *ypb = malloc(sizeof(int) * (nv + 1)), *yd = malloc(sizeof(int) * (nv + 1)); int nl = 0;
  for (int i = 0; i < nv; i++) { ypb[i] = -1; for (int e = 0; e < ne; e++) if (!strcmp(E[e].key, vg[i])) { int boss = (vd[i] + 4) / 5 * 5; ypb[i] = E[e].maxd > boss; yd[i] = E[e].died && E[e].turn - vt[i] <= 50 && E[e].maxd == vd[i]; nl++; break; } }
  // fitted on standardised inputs (the clock runs to 60, HP to 100: unscaled, one rate can't suit them all), pulled
  // towards the weights it starts from (TR_JL2), and only when both answers occur at least TR_JMIN times (one seed's lines
  // all dying would teach 'always die' and nothing else)
  double mu[51] = { 0 }, sd[51] = { 0 }; int nlab = 0; for (int i = 0; i < nv; i++) if (ypb[i] >= 0) { nlab++; for (int k = 0; k < 51; k++) mu[k] += VX[i][k]; }
  if (nlab) for (int k = 0; k < 51; k++) mu[k] /= nlab;
  for (int i = 0; i < nv; i++) if (ypb[i] >= 0) for (int k = 0; k < 51; k++) sd[k] += (VX[i][k] - mu[k]) * (VX[i][k] - mu[k]);
  for (int k = 0; k < 51; k++) { sd[k] = nlab ? sqrt(sd[k] / nlab) : 0; if (sd[k] < 1e-6) sd[k] = 0; }
  double jl2 = getenv("TR_JL2") ? atof(getenv("TR_JL2")) : 0.1; int jmin = getenv("TR_JMIN") ? atoi(getenv("TR_JMIN")) : 50;
  for (int which = 0; which < 2; which++) { double *W = which ? d5 : pb; int npos = 0; for (int i = 0; i < nv; i++) if (ypb[i] >= 0) npos += which ? yd[i] : ypb[i];
    if (npos < jmin || nlab - npos < jmin) { printf("TRAIN judge %s: kept as it was (%d of %d positive)\n", which ? "die50" : "past_boss", npos, nlab); continue; }
    // to standardised form: z = sum v_k (x_k - mu_k)/sd_k + c  (constant and fixed inputs folded into c)
    double v[51], v0[51], c = 0; for (int k = 0; k < 51; k++) { if (sd[k] > 0) { v[k] = W[k] * sd[k]; c += W[k] * mu[k]; } else { v[k] = 0; c += W[k] * (k == 50 ? 1 : mu[k]); } }
    memcpy(v0, v, sizeof v); double c0 = c, lrv = 0.1;
    for (int ep = 0; ep < 300; ep++) { double g[51] = { 0 }, gc = 0, loss = 0; int n = 0;
      for (int i = 0; i < nv; i++) { if (ypb[i] < 0) continue; int y = which ? yd[i] : ypb[i]; double z = c; for (int k = 0; k < 51; k++) if (sd[k] > 0) z += v[k] * (VX[i][k] - mu[k]) / sd[k];
        double p = 1 / (1 + exp(-z)); loss -= y ? log(p + 1e-12) : log(1 - p + 1e-12); for (int k = 0; k < 51; k++) if (sd[k] > 0) g[k] += (p - y) * (VX[i][k] - mu[k]) / sd[k]; gc += p - y; n++; }
      for (int k = 0; k < 51; k++) v[k] -= lrv * (g[k] / n + jl2 * (v[k] - v0[k])); c -= lrv * (gc / n + jl2 * (c - c0));
      if (ep % 100 == 0 || ep == 299) printf("TRAIN judge %s epoch %d loss %.4f (n %d, %d positive)\n", which ? "die50" : "past_boss", ep, loss / n, n, npos); }
    // back: W_k = v_k/sd_k; the constant takes c - sum v_k mu_k/sd_k (inputs that never varied keep their old weight, inside c)
    double cb = c; for (int k = 0; k < 51; k++) if (sd[k] > 0) { W[k] = v[k] / sd[k]; cb -= v[k] * mu[k] / sd[k]; }
    for (int k = 0; k < 50; k++) if (sd[k] == 0) cb -= W[k] * mu[k]; W[50] = cb; }
  snprintf(fn, sizeof fn, "%s/jw2.txt", dir); o = fopen(fn, "w"); if (o) { for (int k = 0; k < 51; k++) fprintf(o, "%.6f\n", pb[k]); for (int k = 0; k < 51; k++) fprintf(o, "%.6f\n", d5[k]); fclose(o); }
  (void)tr_cmp_dummy; (void)vb; }
