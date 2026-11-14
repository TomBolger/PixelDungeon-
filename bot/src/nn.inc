// ---- Neural instinct and judge (owner 2026-11-13: "a neural network is what I thought we were doing") ----
// Two small networks (one hidden layer of NH units, tanh), each added on top of what the bot already has, and each starting at
// zero, so round 0 plays exactly as the bot does now:
//  - the instinct: for each root move, a correction to the old instinct's opinion (its log share), from what the hero sees
//    around it and what the move does; the search's priors are softmax(log old share + correction);
//  - the judge: a correction to judge2's two scores ('past the next boss', 'dead within 50 turns') from the same view.
// They learn from the search: the instinct towards the moves whose tries came out better than the average of the position
// (the search's averages, not just where its tries went: with 300 tries those mostly follow the prior), pulled back towards
// zero so only clear evidence moves it; the judge from how each line of play ended.
#define NNP 103   // the position: judge2's 51 inputs, the 8 cells around the hero (4 each), 20 more
#define NNM 37    // a move
#define NNH 48
typedef struct { int in, out; float mu[NNP + NNM], sd[NNP + NNM]; float W1[NNH][NNP + NNM], b1[NNH], W2[2][NNH], b2[2]; int on; } Net;
static Net s_pnet, s_vnet; static int s_nn_loaded = -1;
static float nn_clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static int nn_foe(int c) { int mi = (unsigned)c < MAP_LEN ? mob_at(c) : -1; if (mi < 0 || ally(mi) || G.mobs[mi].HP <= 0 || (MOB_DEFS[G.mobs[mi].type].flags & MD_NPC)) return -1; return mi; }
static void nn_pos(const double *f, float *P) {
  Hero *h = &G.hero; int k = 0; for (int i = 0; i < 51; i++) P[k++] = (float)f[i];
  double hp = h->HP > 0 ? h->HP : 1;
  for (int d = 0; d < 8; d++) { int q = h->pos + N8[d]; int mi = nn_foe(q); P[k++] = nb_ok(h->pos, q) && (PASSABLE(G.map[q]) && BIT_GET(G.visited, q)); P[k++] = mi >= 0;
    P[k++] = mi >= 0 ? (float)G.mobs[mi].HP / (G.mobs[mi].HT > 0 ? G.mobs[mi].HT : 1) : 0; P[k++] = mi >= 0 ? nn_clampf(foe_dpt(&G.mobs[mi]) / hp, 0, 3) : 0; }
  int nh = 0, n2 = 0, md = 9; double dsum = 0, loss = 0;
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i) || !BIT_GET(G.fov, m->pos) || (MOB_DEFS[m->type].flags & MD_NPC) || m->state != ST_HUNTING) continue;
    nh++; int d = distance(m->pos, h->pos); if (d <= 2) n2++; if (d < md) md = d; double fd = foe_dpt(m), hd = hero_dpt(m); dsum += fd; loss += fd * m->HP / (hd > 0.2 ? hd : 0.2); }
  int pn = 0; for (int d = 0; d < 8; d++) { int q = h->pos + N8[d]; if (nb_ok(h->pos, q) && (PASSABLE(G.map[q]) && BIT_GET(G.visited, q))) pn++; }
  int heal = 0, unkp = 0, scr = 0, thr = 0, wnd = 0;
  for (int s = 0; s < BAG_SIZE; s++) { int kd = h->bag[s].kind; if (!kd) continue; int c = item_cat(kd), q = h->bag[s].qty > 0 ? h->bag[s].qty : 1;
    if (kd == IK_POT_HEALING && item_known(kd)) heal += q; else if (c == CAT_POTION && !item_known(kd)) unkp += q; else if (c == CAT_SCROLL) scr += q; else if (c == CAT_MISSILE) thr += q; else if (c == CAT_WAND) wnd++; }
  static int16_t sd_[MAP_LEN], sq_[MAP_LEN]; memcpy(sd_, b_dist, sizeof sd_); memcpy(sq_, b_q, sizeof sq_);   // (the caller's distances are put back after)
  bfs(h->pos, false); int de = G.exit >= 0 && b_dist[G.exit] < 0x7FFF ? b_dist[G.exit] : 99, dn = G.entrance >= 0 && b_dist[G.entrance] < 0x7FFF ? b_dist[G.entrance] : 99;
  P[k++] = nn_clampf(nh / 4.0f, 0, 2); P[k++] = nn_clampf(n2 / 3.0f, 0, 2); P[k++] = nn_clampf(dsum / hp, 0, 3); P[k++] = md / 9.0f; P[k++] = nn_clampf(loss / hp, 0, 4);
  P[k++] = pn / 8.0f; P[k++] = h->pos == G.exit || h->pos == G.entrance; P[k++] = nn_clampf(de / 20.0f, 0, 5); P[k++] = nn_clampf(dn / 20.0f, 0, 5);
  P[k++] = nn_clampf(heal, 0, 3); P[k++] = nn_clampf(unkp / 3.0f, 0, 3); P[k++] = nn_clampf(scr / 3.0f, 0, 3); P[k++] = nn_clampf(thr / 5.0f, 0, 3); P[k++] = nn_clampf(wnd, 0, 3);
  memcpy(b_dist, sd_, sizeof sd_); memcpy(b_q, sq_, sizeof sq_);
  P[k++] = h->hunger / 400.0f; P[k++] = G.depth / 25.0f; P[k++] = boss_depth(G.depth); P[k++] = (float)h->STR / 20.0f; P[k++] = h->HT / 100.0f; P[k++] = hp / (h->HT > 0 ? h->HT : 1);
  (void)k; }
static void nn_move(int id, double prior, float *M) {
  Hero *h = &G.hero; memset(M, 0, sizeof(float) * NNM); int k;
  double hp = h->HP > 0 ? h->HP : 1; int cell = -1, foe = -1;
  if (id < 8) { int q = h->pos + N8[id]; foe = nn_foe(q); M[foe >= 0 ? 1 : 0] = 1; M[10 + id] = 1; if (foe < 0) cell = q; }
  else if (id == 8) M[2] = 1; else if (id == 9) M[3] = 1;
  else if (id < 512) { int s = (id - 16) / 4, u = (id - 16) % 4; M[4 + (u == 0 ? 0 : u == 1 ? 1 : 2)] = 1;
    if (s >= 0 && s < BAG_SIZE) { int kd = h->bag[s].kind; int c = item_cat(kd); bool kn = item_known(kd);
      M[25] = kd == IK_POT_HEALING && kn; M[26] = c == CAT_POTION && !kn; M[27] = c == CAT_POTION && kn && kd != IK_POT_HEALING; M[28] = c == CAT_SCROLL && !kn; M[29] = c == CAT_SCROLL && kn; M[30] = is_food(kd); } }
  else if (id < 4096) { M[7] = 1; foe = (id - 512) % 32; foe = foe < G.n_mobs ? foe : -1; }
  else if (id < 8192) { M[8] = 1; foe = (id - 4096) % 32; foe = foe < G.n_mobs ? foe : -1; }
  else { M[9] = 1; cell = id - 8192; }
  if (cell >= 0 && (unsigned)cell < MAP_LEN) { int pn = 0, fa = 0; for (int d = 0; d < 8; d++) { int q = cell + N8[d]; if ((unsigned)q < MAP_LEN && nb_ok(cell, q) && (PASSABLE(G.map[q]) && BIT_GET(G.visited, q))) pn++; if ((unsigned)q < MAP_LEN && nn_foe(q) >= 0) fa++; }
    int d0 = pol_foe_dist(h->pos), d1 = pol_foe_dist(cell); if (d0 > 8) d0 = 8; if (d1 > 8) d1 = 8;
    M[18] = pn / 8.0f; M[19] = fa / 3.0f; M[20] = (d1 - d0) / 3.0f; M[21] = G.exit >= 0 ? nn_clampf((distance(cell, G.exit) - distance(h->pos, G.exit)) / 10.0f, -2, 2) : 0;
    M[22] = G.map[cell] == T_DOOR || G.map[cell] == T_OPEN_DOOR; M[23] = G.map[cell] == T_WATER; M[24] = distance(cell, h->pos) / 8.0f; }
  if (foe >= 0 && foe < G.n_mobs) { Mob *m = &G.mobs[foe]; double hd = hero_dpt(m); M[31] = (float)m->HP / (m->HT > 0 ? m->HT : 1); M[32] = nn_clampf(m->HP / (hd > 0.2 ? hd : 0.2) / 10.0f, 0, 3); M[33] = nn_clampf(foe_dpt(m) / hp, 0, 3); M[34] = distance(m->pos, h->pos) / 8.0f; }
  k = 35; M[k++] = (float)log(prior > 1e-4 ? prior : 1e-4) / 5.0f; M[k++] = 1;   // (the old share, and a constant)
}
// ---- the networks ----
static void nn_init(Net *n, int in, int out, uint32_t seed) { memset(n, 0, sizeof *n); n->in = in; n->out = out;
  for (int i = 0; i < in; i++) { n->mu[i] = 0; n->sd[i] = 1; }
  for (int j = 0; j < NNH; j++) for (int i = 0; i < in; i++) { seed = seed * 1664525u + 1013904223u; n->W1[j][i] = ((seed >> 8) / 16777216.0f - 0.5f) * 2.0f / sqrtf((float)in); }
  n->on = 1; }   // (W2 = 0: the network adds nothing until it has learned)
static bool nn_read(Net *n, const char *fn, int in, int out) {
  FILE *f = fn ? fopen(fn, "r") : NULL; if (!f) return false; int a, b, hh; bool ok = fscanf(f, "%d %d %d", &a, &b, &hh) == 3 && a == in && b == out && hh == NNH;
  memset(n, 0, sizeof *n); n->in = in; n->out = out;
  for (int i = 0; ok && i < in; i++) ok = fscanf(f, "%f %f", &n->mu[i], &n->sd[i]) == 2;
  for (int j = 0; ok && j < NNH; j++) { for (int i = 0; ok && i < in; i++) ok = fscanf(f, "%f", &n->W1[j][i]) == 1; ok = ok && fscanf(f, "%f", &n->b1[j]) == 1; }
  for (int o = 0; ok && o < out; o++) { for (int j = 0; ok && j < NNH; j++) ok = fscanf(f, "%f", &n->W2[o][j]) == 1; ok = ok && fscanf(f, "%f", &n->b2[o]) == 1; }
  fclose(f); n->on = ok; return ok; }
static void nn_write(const Net *n, const char *fn) {
  char tmp[700]; snprintf(tmp, sizeof tmp, "%s.tmp", fn); FILE *f = fopen(tmp, "w"); if (!f) return; fprintf(f, "%d %d %d\n", n->in, n->out, NNH);
  for (int i = 0; i < n->in; i++) fprintf(f, "%.6g %.6g\n", n->mu[i], n->sd[i]);
  for (int j = 0; j < NNH; j++) { for (int i = 0; i < n->in; i++) fprintf(f, "%.6g ", n->W1[j][i]); fprintf(f, "%.6g\n", n->b1[j]); }
  for (int o = 0; o < n->out; o++) { for (int j = 0; j < NNH; j++) fprintf(f, "%.6g ", n->W2[o][j]); fprintf(f, "%.6g\n", n->b2[o]); }
  fclose(f); rename(tmp, fn); }
static void nn_load(void) { s_nn_loaded = 1; nn_read(&s_pnet, getenv("POLNN"), NNP + NNM, 1); nn_read(&s_vnet, getenv("VALNN"), NNP, 2); }
static void nn_fwd(const Net *n, const float *x, float *hid, float *out) {
  float z[NNP + NNM]; for (int i = 0; i < n->in; i++) z[i] = (x[i] - n->mu[i]) / n->sd[i];
  for (int j = 0; j < NNH; j++) { float a = n->b1[j]; for (int i = 0; i < n->in; i++) a += n->W1[j][i] * z[i]; hid[j] = tanhf(a); }
  for (int o = 0; o < n->out; o++) { float a = n->b2[o]; for (int j = 0; j < NNH; j++) a += n->W2[o][j] * hid[j]; out[o] = a; } }
// the judge's correction for the game as it stands (f: judge2's inputs, which the caller may have edited)
static void nn_value(const double *f, double *zb, double *zd) {
  if (s_nn_loaded < 0) nn_load(); if (!s_vnet.on) return;
  float P[NNP], hid[NNH], o[2]; nn_pos(f, P); nn_fwd(&s_vnet, P, hid, o); *zb += o[0]; *zd += o[1]; }
// the instinct's correction for each root move (on entry s_rp holds the old instinct's shares)
static float s_nn_P[NNP], s_nn_M[256][NNM]; static double s_nn_p0[256];
static void nn_policy(void) {
  if (s_nn_loaded < 0) nn_load();
  double f[51]; j2_feats(f); nn_pos(f, s_nn_P);
  for (int i = 0; i < s_rn && i < 256; i++) { s_nn_p0[i] = s_rp[i]; nn_move(s_rid[i], s_rp[i], s_nn_M[i]); }
  if (!s_pnet.on) return;
  double z[256], mx = -1e18; float x[NNP + NNM], hid[NNH], o[1]; memcpy(x, s_nn_P, sizeof s_nn_P);
  for (int i = 0; i < s_rn && i < 256; i++) { memcpy(x + NNP, s_nn_M[i], sizeof s_nn_M[i]); nn_fwd(&s_pnet, x, hid, o); z[i] = log(s_rp[i] > 1e-6 ? s_rp[i] : 1e-6) + o[0]; if (z[i] > mx) mx = z[i]; }
  double tot = 0; for (int i = 0; i < s_rn && i < 256; i++) { z[i] = exp(z[i] - mx); tot += z[i]; }
  for (int i = 0; i < s_rn && i < 256; i++) s_rp[i] = z[i] / tot < 0.003 ? 0.003 : z[i] / tot;
  tot = 0; for (int i = 0; i < s_rn; i++) tot += s_rp[i]; for (int i = 0; i < s_rn; i++) s_rp[i] /= tot; }
// ---- records (TREC=2): the position once, then the root moves (the 16 that got most tries or most prior) ----
static void nn_record(int chosen) {
  if (!getenv("TREC") || atoi(getenv("TREC")) != 2 || s_is_child || s_rollout) return;
  if (((uint32_t)(s_bot_steps * 2654435761u + cur_game * 97u + s_rb_branch * 131u) >> 29) >= (uint32_t)(getenv("NNREC_EIGHTHS") ? atoi(getenv("NNREC_EIGHTHS")) : 3)) return;   // (3 in 8 decisions)
  double f[51]; j2_feats(f); double zb = 0, zd = 0; for (int i = 0; i < 51; i++) { zb += s_j2_pb[i] * f[i]; zd += s_j2_d5[i] * f[i]; }
  int ord[256], n = s_rn < 256 ? s_rn : 256; double key[256];
  for (int i = 0; i < n; i++) { int c = mc_child(0, s_rid[i]); key[i] = (c >= 0 ? s_mn[c].n : 0) + 1000 * s_nn_p0[i]; ord[i] = i; }
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (key[ord[b]] > key[ord[a]]) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
  int m = n < 16 ? n : 16;
  printf("TN %d g%d s%d t%d d%d ch %d zb %.4f zd %.4f |", s_rb_branch, cur_game, s_bot_steps, (int)(G.now / TICK), G.depth, chosen, zb, zd);
  for (int i = 0; i < NNP; i++) printf(" %.4g", s_nn_P[i]);
  printf(" | %d", m);
  for (int r = 0; r < m; r++) { int i = ord[r]; int c = mc_child(0, s_rid[i]); int nv = c >= 0 && s_mn[c].w > -500 ? s_mn[c].n : 0; double q = nv ? s_mn[c].w / nv : 0;
    printf(" %d:%d:%.4f:%.4g", s_rid[i], nv, q, s_nn_p0[i]); for (int k = 0; k < NNM; k++) printf(":%.3g", s_nn_M[i][k]); }
  printf("\n"); }
static void nn_snap(void) {   // the judge's view every 25 steps, fights or not
  if (!getenv("TREC") || atoi(getenv("TREC")) != 2 || s_is_child || s_rollout || s_bot_steps % 25) return;
  static int last = -1; if (last == s_bot_steps) return; last = s_bot_steps;
  double f[51]; j2_feats(f); double zb = 0, zd = 0; j2_score(f); for (int i = 0; i < 51; i++) { zb += s_j2_pb[i] * f[i]; zd += s_j2_d5[i] * f[i]; }
  float P[NNP]; nn_pos(f, P); printf("TV %d g%d s%d t%d d%d zb %.4f zd %.4f |", s_rb_branch, cur_game, s_bot_steps, (int)(G.now / TICK), G.depth, zb, zd); for (int i = 0; i < NNP; i++) printf(" %.4g", P[i]); printf("\n"); }

// ---- the trainer (TRAINNN=dir[:dir...], output to TRAINOUT): Adam, minibatches, the networks warm-started from POLNN/VALNN ----
typedef struct { float *P; int nm; float *M; float *tgt; float *lp0; } NnDec;
typedef struct { float *P; float zb, zd; int yb, yd; char key[64]; int t, d; } NnPos;
static void nn_adam(float *w, float *g, float *m, float *v, int n, float lr, int t) {
  float b1 = 0.9f, b2 = 0.999f, c1 = 1 - powf(b1, t), c2 = 1 - powf(b2, t);
  for (int i = 0; i < n; i++) { m[i] = b1 * m[i] + (1 - b1) * g[i]; v[i] = b2 * v[i] + (1 - b2) * g[i] * g[i]; w[i] -= lr * (m[i] / c1) / (sqrtf(v[i] / c2) + 1e-8f); g[i] = 0; } }
static void nn_standardise(Net *n, float *const *xs, int count, int in) {   // (only when a network is new: its inputs' means and spreads)
  for (int i = 0; i < in; i++) { double s = 0, s2 = 0; for (int r = 0; r < count; r++) { s += xs[r][i]; s2 += xs[r][i] * xs[r][i]; } double mu = count ? s / count : 0, var = count ? s2 / count - mu * mu : 1;
    n->mu[i] = (float)mu; n->sd[i] = var > 1e-6 ? (float)sqrt(var) : 1; } }
static void nn_train(void) {
  const char *dirs = getenv("TRAINNN"); char cmd[4096] = "ls"; { char b[2048]; snprintf(b, sizeof b, "%s", dirs); for (char *s2 = NULL, *e = strtok_r(b, ":", &s2); e; e = strtok_r(NULL, ":", &s2)) { size_t l = strlen(cmd); snprintf(cmd + l, sizeof cmd - l, " %s/p*.txt", e); } }
  strncat(cmd, " 2>/dev/null", sizeof cmd - strlen(cmd) - 1);
  char out[1024]; if (getenv("TRAINOUT")) snprintf(out, sizeof out, "%s", getenv("TRAINOUT")); else { snprintf(out, sizeof out, "%s", dirs); char *c = strchr(out, ':'); if (c) *c = 0; }
  int dcap = 1 << 17, nd = 0, pcap = 1 << 18, np = 0, ecap = 1 << 15, ne = 0;
  NnDec *D = malloc(sizeof(NnDec) * dcap); NnPos *V = malloc(sizeof(NnPos) * pcap);
  typedef struct { char key[64]; int died, maxd, turn; } End; End *E = malloc(sizeof(End) * ecap);
  double cq = getenv("NN_CQ") ? atof(getenv("NN_CQ")) : 2.0; int minn = getenv("NN_MINN") ? atoi(getenv("NN_MINN")) : 3;
  FILE *ls = popen(cmd, "r"); char fn[700]; static char ln[1 << 20];
  while (ls && fgets(fn, sizeof fn, ls)) { fn[strcspn(fn, "\n")] = 0; FILE *f = fopen(fn, "r"); if (!f) continue;
    while (fgets(ln, sizeof ln, f)) {
      if (!strncmp(ln, "TRE ", 4)) { int b, g, died, d, md, t; if (ne < ecap && sscanf(ln, "TRE %d g%d died %d depth %d maxd %d turn %d", &b, &g, &died, &d, &md, &t) == 6) { snprintf(E[ne].key, 64, "%s:%d", fn, b); E[ne].died = died; E[ne].maxd = md; E[ne].turn = t; ne++; } continue; }
      bool isn = !strncmp(ln, "TN ", 3), isv = !strncmp(ln, "TV ", 3); if (!isn && !isv) continue;
      int b, g, s, t, d, ch = -1; float zb, zd; char *p;
      if (isn) { if (sscanf(ln, "TN %d g%d s%d t%d d%d ch %d zb %f zd %f", &b, &g, &s, &t, &d, &ch, &zb, &zd) != 8) continue; }
      else if (sscanf(ln, "TV %d g%d s%d t%d d%d zb %f zd %f", &b, &g, &s, &t, &d, &zb, &zd) != 7) continue;
      p = strchr(ln, '|'); if (!p) continue; p++;
      float *P = malloc(sizeof(float) * NNP); bool ok = true; for (int i = 0; i < NNP && ok; i++) { char *e; P[i] = strtof(p, &e); ok = e != p; p = e; }
      if (!ok) { free(P); continue; }
      if (np < pcap) { NnPos *X = &V[np++]; X->P = P; X->zb = zb; X->zd = zd; X->t = t; X->d = d; snprintf(X->key, 64, "%s:%d", fn, b); X->yb = -1; }
      if (!isn || nd >= dcap) continue;
      char *q = strchr(p, '|'); if (!q) continue; q++; int nm = strtol(q, &q, 10); if (nm < 2 || nm > 16) continue;
      NnDec *T = &D[nd]; T->P = P; T->nm = nm; T->M = malloc(sizeof(float) * nm * NNM); T->tgt = malloc(sizeof(float) * nm); T->lp0 = malloc(sizeof(float) * nm);
      int nvs[16]; double qs[16], pr[16];
      for (int m = 0; m < nm && ok; m++) { while (*q == ' ') q++; strtol(q, &q, 10); if (*q != ':') { ok = false; break; } q++; nvs[m] = strtol(q, &q, 10); q++; qs[m] = strtod(q, &q); q++; pr[m] = strtod(q, &q);
        for (int k = 0; k < NNM; k++) { if (*q != ':') { ok = false; break; } q++; T->M[m * NNM + k] = strtof(q, &q); } }
      if (!ok) continue;
      // the improved instinct: the old shares, tilted towards moves whose average beat the position's (in spreads of the averages)
      double qm = 0, qn = 0, qv = 0; for (int m = 0; m < nm; m++) if (nvs[m] >= minn) { qm += qs[m] * nvs[m]; qn += nvs[m]; }
      if (qn < 30) continue; qm /= qn; for (int m = 0; m < nm; m++) if (nvs[m] >= minn) qv += nvs[m] * (qs[m] - qm) * (qs[m] - qm); qv = sqrt(qv / qn) + 0.01;
      double tz[16], mx = -1e18, ps = 0; for (int m = 0; m < nm; m++) ps += pr[m];
      for (int m = 0; m < nm; m++) { double p0 = pr[m] / (ps > 0 ? ps : 1); T->lp0[m] = (float)log(p0 > 1e-6 ? p0 : 1e-6); tz[m] = T->lp0[m] + (nvs[m] >= minn ? cq * (qs[m] - qm) / qv : 0); if (tz[m] > mx) mx = tz[m]; }
      double tt = 0; for (int m = 0; m < nm; m++) { tz[m] = exp(tz[m] - mx); tt += tz[m]; } for (int m = 0; m < nm; m++) T->tgt[m] = (float)(tz[m] / tt);
      nd++; (void)ch; }
    fclose(f); }
  if (ls) pclose(ls);
  // labels for the judge's positions: how the line that saw them ended
  int nl = 0, npb = 0, nd5 = 0;
  for (int i = 0; i < np; i++) for (int e = 0; e < ne; e++) if (!strcmp(E[e].key, V[i].key)) { int boss = (V[i].d + 4) / 5 * 5; V[i].yb = E[e].maxd > boss; V[i].yd = E[e].died && E[e].turn - V[i].t <= 50 && E[e].maxd == V[i].d; nl++; npb += V[i].yb; nd5 += V[i].yd; break; }
  printf("NNTRAIN: %d decisions, %d positions (%d labelled: %d past the boss, %d dead within 50), %d line ends\n", nd, np, nl, npb, nd5, ne);
  int epochs = getenv("NN_EPOCHS") ? atoi(getenv("NN_EPOCHS")) : 8; float lr = getenv("NN_LR") ? atof(getenv("NN_LR")) : 1e-3f, l2o = getenv("NN_L2O") ? atof(getenv("NN_L2O")) : 0.02f;
  uint32_t rs = 12345;
  // -- the instinct --
  if (nd > 100) { Net *n = &s_pnet; if (!nn_read(n, getenv("POLNN"), NNP + NNM, 1)) { nn_init(n, NNP + NNM, 1, 777);
      float **xs = malloc(sizeof(float *) * nd); float *buf = malloc(sizeof(float) * nd * (NNP + NNM));
      for (int i = 0; i < nd; i++) { xs[i] = buf + (size_t)i * (NNP + NNM); memcpy(xs[i], D[i].P, sizeof(float) * NNP); memcpy(xs[i] + NNP, D[i].M, sizeof(float) * NNM); }
      nn_standardise(n, xs, nd, NNP + NNM); free(xs); free(buf); }
    int nw1 = NNH * (NNP + NNM); float *g1 = calloc(nw1, 4), *m1 = calloc(nw1, 4), *v1 = calloc(nw1, 4), gb1[NNH] = { 0 }, mb1[NNH] = { 0 }, vb1[NNH] = { 0 }, g2[NNH] = { 0 }, m2[NNH] = { 0 }, v2[NNH] = { 0 }, gb2[1] = { 0 }, mb2[1] = { 0 }, vb2[1] = { 0 };
    int step = 0, bs = 32; double ph = 0, p0h = 0;
    for (int ep = 0; ep < epochs; ep++) { double loss = 0, kl0 = 0; int cnt = 0;
      for (int i0 = 0; i0 < nd; i0++) { rs = rs * 1664525u + 1013904223u; int di = rs % (uint32_t)nd; if (di % 10 == 9) continue; NnDec *T = &D[di];   // (every 10th decision held out)
        float z[16], hid[16][NNH], zin[16][NNP + NNM], o[16], mx = -1e9f;
        for (int m = 0; m < T->nm; m++) { float x[NNP + NNM]; memcpy(x, T->P, sizeof(float) * NNP); memcpy(x + NNP, T->M + m * NNM, sizeof(float) * NNM);
          for (int i = 0; i < n->in; i++) zin[m][i] = (x[i] - n->mu[i]) / n->sd[i];
          for (int j = 0; j < NNH; j++) { float a = n->b1[j]; for (int i = 0; i < n->in; i++) a += n->W1[j][i] * zin[m][i]; hid[m][j] = tanhf(a); }
          o[m] = n->b2[0]; for (int j = 0; j < NNH; j++) o[m] += n->W2[0][j] * hid[m][j]; z[m] = T->lp0[m] + o[m]; if (z[m] > mx) mx = z[m]; }
        float s = 0, p[16]; for (int m = 0; m < T->nm; m++) { p[m] = expf(z[m] - mx); s += p[m]; }
        for (int m = 0; m < T->nm; m++) { p[m] /= s; loss -= T->tgt[m] * logf(p[m] + 1e-9f); kl0 += o[m] * o[m];
          float d = p[m] - T->tgt[m] + 2 * l2o * o[m];   // (the pull towards adding nothing)
          gb2[0] += d; for (int j = 0; j < NNH; j++) { g2[j] += d * hid[m][j]; float dh = d * n->W2[0][j] * (1 - hid[m][j] * hid[m][j]); gb1[j] += dh; for (int i = 0; i < n->in; i++) g1[j * n->in + i] += dh * zin[m][i]; } }
        cnt++;
        if (cnt % bs == 0) { step++; float sc = 1.0f / bs; for (int i = 0; i < nw1; i++) g1[i] *= sc; for (int j = 0; j < NNH; j++) { gb1[j] *= sc; g2[j] *= sc; } gb2[0] *= sc;
          nn_adam(&n->W1[0][0], g1, m1, v1, nw1, lr, step); nn_adam(n->b1, gb1, mb1, vb1, NNH, lr, step); nn_adam(n->W2[0], g2, m2, v2, NNH, lr, step); nn_adam(n->b2, gb2, mb2, vb2, 1, lr, step); } }
      double hl = 0, h0 = 0; int hn = 0; float xx[NNP + NNM], hd2[NNH], oo[1];
      for (int di = 9; di < nd; di += 10) { NnDec *T = &D[di]; float z[16], mx = -1e9f; double s0 = 0; for (int m = 0; m < T->nm; m++) { memcpy(xx, T->P, sizeof(float) * NNP); memcpy(xx + NNP, T->M + m * NNM, sizeof(float) * NNM); nn_fwd(n, xx, hd2, oo); z[m] = T->lp0[m] + oo[0]; if (z[m] > mx) mx = z[m]; s0 += exp(T->lp0[m]); }
        double s = 0; for (int m = 0; m < T->nm; m++) s += exp(z[m] - mx); for (int m = 0; m < T->nm; m++) { hl -= T->tgt[m] * (z[m] - mx - log(s)); h0 -= T->tgt[m] * (T->lp0[m] - log(s0)); } hn++; }
      printf("NNTRAIN instinct epoch %d: loss %.4f, mean square correction %.4f | held out: network %.4f, old instinct %.4f (n %d)\n", ep, loss / (cnt ? cnt : 1), kl0 / (cnt ? cnt : 1), hn ? hl / hn : 0, hn ? h0 / hn : 0, hn); ph = hn ? hl / hn : 0; p0h = hn ? h0 / hn : 0; }
    // what the target itself scores (the floor the loss can reach) and what the old instinct alone scores
    double lt = 0, lo = 0; for (int i = 0; i < nd; i++) for (int m = 0; m < D[i].nm; m++) { if (D[i].tgt[m] > 0) lt -= D[i].tgt[m] * log(D[i].tgt[m]); double s0 = 0; for (int k = 0; k < D[i].nm; k++) s0 += exp(D[i].lp0[k]); lo -= D[i].tgt[m] * (D[i].lp0[m] - log(s0)); }
    printf("NNTRAIN instinct: the old instinct alone %.4f, the best possible %.4f\n", lo / nd, lt / nd);
    // kept only if it beats the old instinct on the decisions it never trained on (a network that got worse is not passed on)
    char fo[1100]; snprintf(fo, sizeof fo, "%s/polnn.txt", out);
    if (ph < p0h - 0.002) { nn_write(n, fo); printf("NNTRAIN instinct: kept (held out %.4f vs the old instinct %.4f)\n", ph, p0h); }
    else { printf("NNTRAIN instinct: not kept (held out %.4f vs the old instinct %.4f); the previous network stays\n", ph, p0h); if (getenv("POLNN")) { Net t; if (nn_read(&t, getenv("POLNN"), NNP + NNM, 1)) nn_write(&t, fo); } }
    free(g1); free(m1); free(v1); }
  // -- the judge --
  if (nl > 200 && npb >= 30 && nd5 >= 30) { Net *n = &s_vnet; if (!nn_read(n, getenv("VALNN"), NNP, 2)) { nn_init(n, NNP, 2, 999);
      float **xs = malloc(sizeof(float *) * np); int c = 0; for (int i = 0; i < np; i++) if (V[i].yb >= 0) xs[c++] = V[i].P; nn_standardise(n, xs, c, NNP); free(xs); }
    int nw1 = NNH * NNP; float *g1 = calloc(nw1, 4), *m1 = calloc(nw1, 4), *v1 = calloc(nw1, 4), gb1[NNH] = { 0 }, mb1[NNH] = { 0 }, vb1[NNH] = { 0 }, g2[2 * NNH] = { 0 }, m2[2 * NNH] = { 0 }, v2[2 * NNH] = { 0 }, gb2[2] = { 0 }, mb2[2] = { 0 }, vb2[2] = { 0 };
    int step = 0, bs = 64; double vh = 0, v0h = 0;
    for (int ep = 0; ep < epochs; ep++) { double loss = 0, loss0 = 0; int cnt = 0;
      for (int i0 = 0; i0 < np; i0++) { rs = rs * 1664525u + 1013904223u; int vi = rs % (uint32_t)np; if (vi % 10 == 9) continue; NnPos *X = &V[vi]; if (X->yb < 0) continue;
        float zin[NNP], hid[NNH]; for (int i = 0; i < NNP; i++) zin[i] = (X->P[i] - n->mu[i]) / n->sd[i];
        for (int j = 0; j < NNH; j++) { float a = n->b1[j]; for (int i = 0; i < NNP; i++) a += n->W1[j][i] * zin[i]; hid[j] = tanhf(a); }
        float dout[2];
        for (int o = 0; o < 2; o++) { float a = n->b2[o]; for (int j = 0; j < NNH; j++) a += n->W2[o][j] * hid[j]; float zz = (o ? X->zd : X->zb) + a, pp = 1 / (1 + expf(-zz)), z0 = o ? X->zd : X->zb, p0 = 1 / (1 + expf(-z0)); int y = o ? X->yd : X->yb;
          loss -= y ? logf(pp + 1e-9f) : logf(1 - pp + 1e-9f); loss0 -= y ? logf(p0 + 1e-9f) : logf(1 - p0 + 1e-9f); dout[o] = pp - y + 2 * l2o * a; }
        for (int o = 0; o < 2; o++) { gb2[o] += dout[o]; for (int j = 0; j < NNH; j++) g2[o * NNH + j] += dout[o] * hid[j]; }
        for (int j = 0; j < NNH; j++) { float dh = (dout[0] * n->W2[0][j] + dout[1] * n->W2[1][j]) * (1 - hid[j] * hid[j]); gb1[j] += dh; for (int i = 0; i < NNP; i++) g1[j * NNP + i] += dh * zin[i]; }
        cnt++;
        if (cnt % bs == 0) { step++; float sc = 1.0f / bs; for (int i = 0; i < nw1; i++) g1[i] *= sc; for (int j = 0; j < NNH; j++) gb1[j] *= sc; for (int j = 0; j < 2 * NNH; j++) g2[j] *= sc; gb2[0] *= sc; gb2[1] *= sc;
          nn_adam(&n->W1[0][0], g1, m1, v1, nw1, lr, step); nn_adam(n->b1, gb1, mb1, vb1, NNH, lr, step); nn_adam(&n->W2[0][0], g2, m2, v2, 2 * NNH, lr, step); nn_adam(n->b2, gb2, mb2, vb2, 2, lr, step); } }
      double hl = 0, h0 = 0; int hn = 0; float hd2[NNH], oo[2];
      for (int vi = 9; vi < np; vi += 10) { NnPos *X = &V[vi]; if (X->yb < 0) continue; nn_fwd(n, X->P, hd2, oo);
        for (int o = 0; o < 2; o++) { int y = o ? X->yd : X->yb; float z0 = o ? X->zd : X->zb, p = 1 / (1 + expf(-(z0 + oo[o]))), p0 = 1 / (1 + expf(-z0)); hl -= y ? logf(p + 1e-9f) : logf(1 - p + 1e-9f); h0 -= y ? logf(p0 + 1e-9f) : logf(1 - p0 + 1e-9f); } hn++; }
      printf("NNTRAIN judge epoch %d: loss %.4f (judge2 alone %.4f) | held out: network %.4f, judge2 alone %.4f (n %d)\n", ep, loss / (cnt ? cnt : 1), loss0 / (cnt ? cnt : 1), hn ? hl / hn : 0, hn ? h0 / hn : 0, hn); vh = hn ? hl / hn : 0; v0h = hn ? h0 / hn : 0; }
    char fo[1100]; snprintf(fo, sizeof fo, "%s/valnn.txt", out);
    if (vh < v0h - 0.002) { nn_write(n, fo); printf("NNTRAIN judge: kept (held out %.4f vs judge2 %.4f)\n", vh, v0h); }
    else { printf("NNTRAIN judge: not kept (held out %.4f vs judge2 %.4f)\n", vh, v0h); if (getenv("VALNN")) { Net t; if (nn_read(&t, getenv("VALNN"), NNP, 2)) nn_write(&t, fo); } } free(g1); free(m1); free(v1); }
  else printf("NNTRAIN judge: kept as it was (too few labelled positions or answers of each kind)\n");
}
