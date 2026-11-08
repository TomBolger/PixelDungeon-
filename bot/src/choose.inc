// ---- choose(): one scorer for every turn with an awake foe about (Thomas 2026-11-07, bot/CLAUDE.md) ----
// Every option the hero has this turn is listed and priced in one currency, HP: the HP the rest of the fight is
// expected to cost (tact_eval: the game's hit odds, damage rolls, DR, speeds, who can reach which cell), plus the
// chance it kills me times what a death costs (CH_DEATH x HT), plus what the option uses up (an item's worth in HP).
// The cheapest is done. Nothing here says "in situation X do Y": a new behaviour is a new option or a better number.
// Options: hold here (strike the foe beside me, or let it come); walk to any seen cell within reach and fight from
// there (the first step is taken); throw a missile at a foe in line; drink a known Healing; drink an unknown potion
// (its odds from IDB); drink the dew vial; read a known Teleportation; take the stairs I stand on.
// Not yet covered (the old code still decides): bosses, wands, scrolls other than Teleportation, sleeping foes.
static double idb_p(int label, int kind);
static long st_ch[12];
static double ch_k(const char *n, double d) { const char *v = getenv(n); return v ? atof(v) : d; }

// The fight played out N times with the game's dice (CH_RUNS): each blow a hit-or-miss draw on the attacker's accuracy
// against the defender's evasion (Char.hit), its damage a NormalIntRange roll less an armour roll 0..DR (Char.damage);
// the foes move as tact_eval moves them. The price: the HP it costs on average plus the share of runs that kill me x
// CH_DEATH x HT. (tact_eval played one average fight: a 3-HP gnoll was dead in its one sim and alive in half the real ones.)
static double c_ph[24], c_pf[24], c_rpf[24]; static int c_hlo[24], c_hhi[24], c_hex[24], c_fdr[24], c_flo[24], c_fhi[24], c_rlo[24], c_rhi[24], c_rt[24]; static bool c_mag[24], c_rn[24]; static int c_hdr; static double c_hdef;   // c_r*: the shot from afar (its own dice, time and armour rule)
static double c_l1, c_turns, c_won; static uint32_t c_rs = 0x9e3779b9u, c_seed = 0x9e3779b9u; static int c_type[24]; static int c_pump0[24];
#define m_shaman(k) (c_type[k] == MT_SHAMAN || c_type[k] == MT_WARLOCK)   // uniform rnd_int2 rolls
static inline double c_rnd(void) { c_rs ^= c_rs << 13; c_rs ^= c_rs >> 17; c_rs ^= c_rs << 5; return (c_rs >> 8) / 16777216.0; }
static inline int c_norm(int lo, int hi) { if (hi <= lo) return lo; return lo + (int)((c_rnd() + c_rnd()) * (hi - lo + 1) / 2); }
static inline int c_uni(int lo, int hi) { if (hi <= lo) return lo; return lo + (int)(c_rnd() * (hi - lo + 1)); }
static void ch_prep(TFoe *F, int nf) {
  Hero *h = &G.hero; const Item *w = h->weapon.kind ? &h->weapon : NULL; int st = hero_str();
  double rate = w ? TICK / (double)attack_delay(w) : 1; c_hdr = hero_dr(); c_hdef = hero_def_est();
  for (int k = 0; k < nf; k++) { Mob *m = &G.mobs[F[k].i]; const MobDef *d = &MOB_DEFS[m->type]; c_type[k] = m->type; c_pump0[k] = m->type == MT_GOO ? m->aux2 : 0;
    double p = (m->b.paralysis || m->b.frost) ? 1 : phit(hero_attack_skill(NULL, m->pos), mst(m, STAT_DEF, d->def)); c_ph[k] = p * rate > 1 ? 1 : p * rate;
    if (w) { c_hlo[k] = weapon_min(w); c_hhi[k] = weapon_max(w); c_hex[k] = h->hclass != 3 && st > item_str(w) ? st - item_str(w) : 0; } else { c_hlo[k] = 1; c_hhi[k] = st > 10 ? st - 9 : 1; c_hex[k] = 0; }
    c_fdr[k] = mst(m, STAT_DR, d->dr);
    c_mag[k] = magic_shooter(m->type) && distance(m->pos, h->pos) > 1;
    c_pf[k] = phit(mst(m, STAT_ATT, d->att) * (c_mag[k] ? 2 : 1), hero_def_est());
    c_flo[k] = d->dmg_lo; c_fhi[k] = d->dmg_hi;
    if (g_mh.stats && m->type != MT_GOO) { uint32_t r0 = g_rng; int lo = 9999, hi = -1; for (int t = 0; t < 64; t++) { int v = g_mh.stats(F[k].i, STAT_DMG); if (v < 0) break; if (v < lo) lo = v; if (v > hi) hi = v; } g_rng = r0; if (hi >= 0) { c_flo[k] = lo; c_fhi[k] = hi; } }   // (CHCAL: a brute's table says 10-40, its overlay rolls 8-18 till enraged; a mimic grows with depth: the creature's own dice, its range read off 64 rolls, the game's dice put back)
    c_pf[k] = phit(mst(m, STAT_ATT, d->att), hero_def_est());   // a blow beside me: its plain accuracy, its dice less my armour
    // the shot from afar, from the source: shaman lightning 2-11 (x1.5 standing in water), 2 turns (mob_shaman.inc); warlock 12-17, a turn (city.c);
    // evil eye 14-20 NormalIntRange (halls.c); all magic: the roll doubled, armour ignored (char_hit(.., true))
    c_rn[k] = true; c_rpf[k] = phit(mst(m, STAT_ATT, d->att) * 2, hero_def_est()); c_rt[k] = TICK;
    if (m->type == MT_SHAMAN) { c_rlo[k] = 2; c_rhi[k] = 11; c_rt[k] = 2 * TICK; }
    else if (m->type == MT_WARLOCK) { c_rlo[k] = 12; c_rhi[k] = 17; }
    else if (m->type == MT_EYE) { c_rlo[k] = 14; c_rhi[k] = 20; c_rt[k] = d->atk_delay ? d->atk_delay : TICK; }
    else { c_rn[k] = false; c_rlo[k] = d->dmg_lo; c_rhi[k] = d->dmg_hi; c_rpf[k] = c_pf[k]; c_rt[k] = d->atk_delay ? d->atk_delay : TICK; } } }

// tact_field (the foes' walking distances to the hero's cell), kept for the cells asked about this decision
static int16_t c_fld[48][MAP_LEN]; static int16_t c_fpos[48]; static int c_fn, c_fstep = -1, c_fgame = -1;
static void ch_field(int hc) {
  if (c_fstep != s_bot_steps || c_fgame != cur_game) { c_fn = 0; c_fstep = s_bot_steps; c_fgame = cur_game; }
  for (int i = 0; i < c_fn; i++) if (c_fpos[i] == hc) { memcpy(s_td, c_fld[i], sizeof s_td); return; }
  tact_field(hc); int slot = c_fn < 48 ? c_fn++ : (int)(c_rnd() * 48); c_fpos[slot] = hc; memcpy(c_fld[slot], s_td, sizeof s_td); }
static double ch_mc(TFoe *F0, int nf, int P, const int *path, int plen, int tk, double tdmg, int runs, double hp0) {
  Hero *h = &G.hero; double lost = 0, lost1 = 0, turns = 0, won = 0; int deaths = 0; static int field_at = -1; static int field_game = -1, field_step = -1;
  if (field_game != cur_game || field_step != s_bot_steps) { field_at = -1; field_game = cur_game; field_step = s_bot_steps; }
  c_rs = c_seed ^ (uint32_t)(s_bot_steps * 2654435761u) ^ (uint32_t)cur_game; if (!c_rs) c_rs = 1;   // the same dice for every option this turn (common random numbers): the options differ by what they do, not by luck; the cheapest of 150 noisy guesses is otherwise just the luckiest
  for (int r = 0; r < runs; r++) {
    int pump[24]; memcpy(pump, c_pump0, sizeof pump); int ooze = h->ooze ? 1 : 0, para = 0;
    TFoe F[24]; memcpy(F, F0, sizeof(TFoe) * nf); double hp = hp0 + (vial_full() ? h->HT : 0); int hc = h->pos; bool dead = false;   // (a full dew vial brings me back at full health: Hero.die)
    double l0 = lost; int tt = 0;
    for (int t = 0; t < 60 && !dead; t++) { tt = t; if (t == 1) lost1 += lost - l0;
      bool any = false; for (int k = 0; k < nf; k++) if (F[k].hp > 0) any = true; if (!any) { won++; break; }
      if (para > 0) para--;
      else if (t < plen) { int nx = path[t]; for (int k = 0; k < nf; k++) if (F[k].hp > 0 && F[k].pos == nx) return 1e9; hc = nx; }
      else if (t == 0 && tk >= 0) { if (c_rnd() < 0.85) F[tk].hp -= tdmg / 0.85; }
      else if (({ bool dodged = false; bool hit2;   // (CHCAL: the Goo forecast 55% deaths, 24% happened: the future me re-plans each turn and steps out of a pumped Goo's reach, as the scorer does now; the plan's later turns must too)
          for (int k = 0; k < nf && !dodged; k++) if (F[k].hp > 0 && pump[k] && distance(F[k].pos, hc) <= 2 && (adjacent(F[k].pos, hc) || ballistica(F[k].pos, hc, &hit2) == hc)) {
            for (int j = 0; j < 8 && !dodged; j++) { int q2 = hc + N8[j]; if (!nb_ok(hc, q2) || !BIT_GET(G.visited, q2) || !PASSABLE(G.map[q2]) || (TFLAGS(G.map[q2]) & F_AVOID)) continue; bool occ = false; for (int o = 0; o < nf; o++) if (F[o].hp > 0 && F[o].pos == q2) occ = true; if (occ) continue;
              if (distance(F[k].pos, q2) > 2 || ballistica(F[k].pos, q2, &hit2) != q2) { hc = q2; dodged = true; } } }
          dodged; })) {}
      else { int b = -1; double bv = -1; for (int k = 0; k < nf; k++) if (F[k].hp > 0 && adjacent(F[k].pos, hc)) { double v = F[k].dpt / (F[k].hp > 1 ? F[k].hp : 1); if (v > bv) { bv = v; b = k; } }
        if (b >= 0) { if (c_rnd() < c_ph[b]) { int dm = c_norm(c_hlo[b], c_hhi[b]) + (c_hex[b] ? c_uni(0, c_hex[b]) : 0) - c_uni(0, c_fdr[b]); if (dm > 0) F[b].hp -= dm; if (c_type[b] == MT_FETID_RAT) para = 10; } }   // (sewers.c defense_proc: a struck fetid rat puffs paralytic gas onto its cell: beside it, 10 turns paralysed (game.c), while it, immune, bites on)
        else { int rr = -1, rd = 99; for (int k = 0; k < nf; k++) if (F[k].hp > 0 && F[k].rng && distance(F[k].pos, hc) < rd) { rd = distance(F[k].pos, hc); rr = k; }
          if (rr >= 0) { int bq = -1, bd = rd; for (int j = 0; j < 8; j++) { int q2 = hc + N8[j]; if (!nb_ok(hc, q2) || !BIT_GET(G.visited, q2) || !PASSABLE(G.map[q2]) || (TFLAGS(G.map[q2]) & F_AVOID)) continue; bool occ = false; for (int o = 0; o < nf; o++) if (F[o].hp > 0 && F[o].pos == q2) occ = true; if (occ) continue; if (distance(q2, F[rr].pos) < bd) { bd = distance(q2, F[rr].pos); bq = q2; } }
            if (bq >= 0) hc = bq; } } }
      if (ooze) { if (IS_WATER(G.map[hc])) ooze = 0; else { hp -= 1; lost += 1; if (hp <= 0) { dead = true; break; } } }   // ooze: a point a turn till I stand in water
      for (int k = 0; k < nf; k++) if (c_type[k] == MT_GOO && F[k].hp > 0 && IS_WATER(G.map[F[k].pos]) && F[k].hp < G.mobs[F[k].i].HT) F[k].hp += 1;   // Goo mends in water
      if (field_at != hc) { ch_field(hc); field_at = hc; }
      for (int k = 0; k < nf && !dead; k++) { TFoe *f = &F[k]; if (f->hp <= 0) continue;
        f->tm += TICK;
        for (int guard = 0; guard < 6 && f->tm > 0; guard++) { bool hit;
          if (c_type[k] == MT_GOO) {   // Goo (sewers.c): beside me, 2 in 3 a blow (2-12, accuracy 15), else it pumps up (2 turns); pumped, it reaches 2 cells in a clear line, jumps beside me and strikes for 5-30 (accuracy 30 unless it jumped); moving off cancels the pump; in water it mends 1 a turn
            bool adj = adjacent(f->pos, hc), inl = pump[k] && distance(f->pos, hc) <= 2 && ballistica(f->pos, hc, &hit) == hc;
            if (pump[k] && (adj || inl)) { f->tm -= TICK; double pa = phit(adj ? 30 : 15, c_hdef); pump[k] = 0;
              if (!adj) { int bq = -1; for (int j = 0; j < 8; j++) { int q = hc + N8[j]; if (nb_ok(hc, q) && PASSABLE(G.map[q]) && distance(q, f->pos) < distance(f->pos, hc) + 1) { bq = q; break; } } if (bq >= 0) f->pos = bq; }
              if (c_rnd() < pa) { int dm = c_norm(5, 30) - c_uni(0, c_hdr); if (dm > 0) { hp -= dm; lost += dm; if (c_rnd() < 1 / 3.0) ooze = 1; if (hp <= 0) { dead = true; break; } } } continue; }
            if (adj) { if (c_rnd() < 2 / 3.0) { f->tm -= TICK; if (c_rnd() < c_pf[k]) { int dm = c_norm(2, 12) - c_uni(0, c_hdr); if (dm > 0) { hp -= dm; lost += dm; if (c_rnd() < 1 / 3.0) ooze = 1; if (hp <= 0) { dead = true; break; } } } }
              else { f->tm -= 2 * TICK; pump[k] = 1; } continue; }
            pump[k] = 0; }
          bool reach = adjacent(f->pos, hc) || (f->rng && distance(f->pos, hc) <= 8 && ballistica(f->pos, hc, &hit) == hc);
          if (reach) { bool adj = adjacent(f->pos, hc); int dm = 0;
            if (adj || !c_rn[k]) { f->tm -= adj ? f->ac : c_rt[k]; if (c_rnd() < c_pf[k]) dm = c_norm(c_flo[k], c_fhi[k]) - c_uni(0, c_hdr); }
            else { f->tm -= c_rt[k]; if (c_rnd() < c_rpf[k]) { dm = m_shaman(k) ? c_uni(c_rlo[k], c_rhi[k]) : c_norm(c_rlo[k], c_rhi[k]); if (m_shaman(k) && IS_WATER(G.map[hc])) dm = dm * 3 / 2; } }
            if (dm > 0) { hp -= dm; lost += dm; if (hp <= 0) { dead = true; break; } } continue; }
          f->tm -= f->mc;
          int bq = -1, bd = s_td[f->pos]; for (int j = 0; j < 8; j++) { int q = f->pos + N8[j]; if (!nb_ok(f->pos, q) || q == hc || s_td[q] >= bd || (f->wat && !IS_WATER(G.map[q]))) continue; bool occ = false; for (int o = 0; o < nf; o++) if (o != k && F[o].hp > 0 && F[o].pos == q) occ = true; if (occ) continue; bd = s_td[q]; bq = q; }
          if (bq >= 0) f->pos = bq; else break; } } }
    turns += tt; if (dead) deaths++; }
  s_te_pd = deaths / (double)runs; s_te_E = lost / runs; c_l1 = lost1 / runs; c_turns = turns / runs; c_won = won / runs;
  return s_te_E; }

// CHCAL: each encounter's forecast (at its first priced turn) against what happened: the check on the model's numbers
static int c_cal_on, c_cal_hp0, c_cal_type, c_cal_nf, c_cal_d, c_cal_g; static double c_cal_pd, c_cal_E;
static void ch_cal_end(bool died) {
  if (!c_cal_on || !getenv("CHCAL")) { c_cal_on = 0; return; }
  printf("CAL g%d d%d %s nf %d hp0 %d ht %d pd %.3f E %.1f lost %d died %d\n", c_cal_g, c_cal_d, mob_name(c_cal_type), c_cal_nf, c_cal_hp0, G.hero.HT, c_cal_pd, c_cal_E, c_cal_hp0 - (G.hero.HP > 0 ? G.hero.HP : 0), died); c_cal_on = 0; }
static int choose(void) {
  Hero *h = &G.hero; if (s_rollout || h->b.paralysis || h->b.frost || (G.depth % 5 == 0 && G.depth != 5) || (G.depth == 5 && !getenv("CHGOO"))) return 0;   // (bosses: not yet; the Goo model (CHGOO) lost to the old Goo code on the PC, 31.3 vs 26.3% Goo deaths: its numbers are being fixed)
  TFoe F[24]; int nf = 0;
  // what I know of the foes: those in sight now, and those that were hunting me when last seen a few turns ago (where I saw them):
  // a foe that steps out of sight hasn't gone (the player knows it's there; the true position stays hidden)
  static uint32_t mu[MAX_MOBS]; static int16_t mp[MAX_MOBS]; static int32_t mt[MAX_MOBS]; static int md_ = -1, mg_ = -1;
  if (md_ != G.depth || mg_ != cur_game) { memset(mu, 0, sizeof mu); md_ = G.depth; mg_ = cur_game; }
  static double kmem = -1; if (kmem < 0) kmem = ch_k("CH_MEMT", 12);   // turns a foe out of sight is still counted
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP > 0 && (BIT_GET(G.fov, m->pos) || adjacent(m->pos, h->pos)) && m->state == ST_HUNTING) { mu[i] = m->uid; mp[i] = m->pos; mt[i] = G.now; } }
  for (int i = 0; i < G.n_mobs; i++) { Mob *m = &G.mobs[i]; if (m->HP <= 0 || ally(i)) continue;
    bool see = BIT_GET(G.fov, m->pos), adj = adjacent(m->pos, h->pos); int pos = m->pos;
    if (see || adj) { if (m->state != ST_HUNTING) continue; }
    else { if (mu[i] != m->uid || G.now - mt[i] > kmem * TICK || BIT_GET(G.fov, mp[i])) continue; pos = mp[i]; }   // (out of sight: where I last saw it; if I can see that cell now, it's moved on: lost)
    if (distance(pos, h->pos) > 9) continue;
    if (MOB_DEFS[m->type].flags & MD_NPC) continue; if ((MOB_DEFS[m->type].flags & MD_BOSS) && m->type != MT_GOO) return 0;
    if (m->b.paralysis || m->b.frost) continue;
    if (nf >= 24) break; TFoe *f = &F[nf++]; f->wat = MOB_DEFS[m->type].flags & MD_WATER; f->i = i; f->pos = pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi;
    f->acts = mob_move_cost(m) * 2 <= hero_move_cost() ? 2 : 1; f->mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (f->mc < 5) f->mc = 5;
    f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = h->time - m->time; if (f->tm > TICK) f->tm = TICK; if (f->tm < -4 * TICK) f->tm = -4 * TICK; f->rng = magic_shooter(m->type) || m->type == MT_SCORPIO; }   // (CALT: the game's clock: a Goo that just pumped (2 turns) or a slow blow acts later than my next move; its time debt is what a player sees it spend)
  if (nf == 0) { if (c_cal_on) ch_cal_end(false); return 0; }
  static double kdeath = -1, kreach, cheal, cunk, cvial, ctele, cstair, cthrow; if (kdeath < 0) {
    kdeath = ch_k("CH_DEATH", 4.0);     // a death, in HTs of HP (the run's whole worth: tuned)
    kreach = ch_k("CH_REACH", 8);       // how far a walk to fight from is looked at (cells)
    cheal = ch_k("CH_HEAL", 0.6);       // a known Healing used now, in HTs (it's not there for the next fight or the boss)
    cunk = ch_k("CH_UNK", 0.1);         // an unknown potion used now, in HTs
    cvial = ch_k("CH_VIAL", 0.05);      // a dew drop, in HTs (a full vial's revive is priced by the death chance itself)
    ctele = ch_k("CH_TELE", 0.3);       // a known Teleportation, in HTs
    cstair = ch_k("CH_STAIR", 0.15);    // the stairs: 36 food and the trip back, in HTs
    cthrow = ch_k("CH_THROW", 0.0); }   // a missile thrown (picked up again after)
  const double HT = h->HT > 0 ? h->HT : 1;
  // the options; each is priced by ch_price below
  typedef struct { int8_t kind; int16_t arg, P, n; int16_t path[16]; double extra, td; } Opt;
  static Opt O[700]; int no = 0;
  O[no++] = (Opt){ .kind = 0, .P = h->pos, .n = 0 };                                   // hold
  static int16_t hd2[MAP_LEN], par[MAP_LEN], q[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) hd2[c] = -1;
  int qh = 0, qt = 0; hd2[h->pos] = 0; par[h->pos] = -1; q[qt++] = h->pos;
  while (qh < qt) { int c = q[qh++]; if (hd2[c] >= kreach) continue; for (int k = 0; k < 8; k++) { int n = c + N8[k];
      if (!nb_ok(c, n) || hd2[n] >= 0 || !BIT_GET(G.visited, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID) || char_at(n) || bad_air(n) || n == G.exit || (h->b.roots && c == h->pos)) continue;
      hd2[n] = hd2[c] + 1; par[n] = c; q[qt++] = n; } }
  for (int qi = 1; qi < qt && no < 600; qi++) { int P = q[qi], n = hd2[P]; if (n > 16) continue; Opt *o = &O[no++]; *o = (Opt){ .kind = 1, .arg = P, .P = P, .n = n };
    for (int c = P, j = n - 1; j >= 0; j--, c = par[c]) o->path[j] = c; }   // walk there, fight from there
  for (int s = 0; s < BAG_SIZE; s++) { Item *it = &h->bag[s]; int k = it->kind; if (item_cat(k) != CAT_MISSILE || k == IK_BOOMERANG) continue;
    for (int j = 0; j < nf && no < 690; j++) { Mob *m = &G.mobs[F[j].i]; bool hit; if (distance(F[j].pos, h->pos) < 2 || ballistica(h->pos, F[j].pos, &hit) != F[j].pos) continue;
      int def = mst(m, STAT_DEF, MOB_DEFS[m->type].def), dr = mst(m, STAT_DR, MOB_DEFS[m->type].dr);
      O[no++] = (Opt){ .kind = 2, .arg = s * 32 + j, .P = h->pos, .td = phit(hero_attack_skill(it, F[j].pos), def) * dmg_vs_dr(weapon_min(it), weapon_max(it), dr), .extra = cthrow * HT }; } }
  int hs = find_kind(IK_POT_HEALING); if (hs >= 0 && item_known(IK_POT_HEALING) && h->HP < h->HT) O[no++] = (Opt){ .kind = 3, .arg = hs, .extra = cheal * HT };
  for (int s = 0; s < BAG_SIZE && no < 698; s++) { int k = h->bag[s].kind; if (item_cat(k) != CAT_POTION || item_known(k)) continue;
    bool dup = false; for (int t = 0; t < s; t++) if (h->bag[t].kind == k) dup = true; if (!dup) O[no++] = (Opt){ .kind = 4, .arg = s, .extra = cunk * HT }; }
  int vs = find_kind(IK_DEW_VIAL); if (vs >= 0 && G.dew > 0 && h->HP < h->HT && no < 699) O[no++] = (Opt){ .kind = 5, .arg = vs, .extra = cvial * G.dew * HT };
  int ts = find_kind(IK_SCR_TELEPORT); if (ts >= 0 && item_known(IK_SCR_TELEPORT) && !h->b.blind && no < 699) O[no++] = (Opt){ .kind = 6, .arg = ts, .extra = ctele * HT };
  bool on_st = (h->pos == G.entrance && G.depth > 1) || (h->pos == G.exit && !boss_depth(G.depth + 1)); if (on_st && no < 699) O[no++] = (Opt){ .kind = 7, .extra = cstair * HT };
  // the price: two rounds (CH_RUNS dice each, the same dice for every option), then the best few again with CH_RUNS2 dice:
  // the cheapest of hundreds of noisy prices is mostly the luckiest one; a second, bigger draw on the few best removes that
  static int runs = -1, runs2, keep; if (runs < 0) { runs = (int)ch_k("CH_RUNS", 32); runs2 = (int)ch_k("CH_RUNS2", 256); keep = (int)ch_k("CH_KEEP", 6); }
  bool mc = !getenv("CHNOMC"); if (mc) ch_prep(F, nf);
  double hold = 0, price[700]; int stay[1] = { h->pos };
  for (int pass = 0; pass < 2; pass++) { int R = pass ? runs2 : runs; uint32_t seed = pass ? 0x51ed27u : 0x9e3779b9u;
    for (int i = 0; i < no; i++) { Opt *o = &O[i]; if (pass && price[i] > 1e8) continue;
      #define EV(hp_, P_, path_, n_, tk_, td_) (c_seed = seed, s_te_hp = (hp_), mc ? ch_mc(F, nf, P_, path_, n_, tk_, td_, R, s_te_hp >= 0 ? s_te_hp : h->HP) + kdeath * s_te_pd * HT : tact_eval(F, nf, P_, path_, n_, NULL, tk_, td_) + (kdeath - 2.0) * s_te_pd * HT)
      double v;
      switch (o->kind) {
        case 0: v = EV(-1, h->pos, NULL, 0, -1, 0); break;
        case 1: { int path[16]; for (int j = 0; j < o->n; j++) path[j] = o->path[j]; v = EV(-1, o->P, path, o->n, -1, 0); break; }
        case 2: v = EV(-1, h->pos, NULL, 0, o->arg % 32, o->td); break;
        case 3: v = EV(h->HT, h->pos, stay, 1, -1, 0); break;
        case 4: { int k = h->bag[o->arg].kind; double ph = idb_p(k, IK_POT_HEALING), pt = idb_p(k, IK_POT_TOXIC), pf = idb_p(k, IK_POT_FLAME), pp = idb_p(k, IK_POT_PARALYTIC), pi = idb_p(k, IK_POT_INVIS);
          double near = 0; for (int j = 0; j < nf; j++) if (distance(F[j].pos, h->pos) <= 2) near += F[j].dpt;
          double vh = EV(h->HT, h->pos, stay, 1, -1, 0), v0 = EV(-1, h->pos, stay, 1, -1, 0);
          v = ph * vh + (1 - ph) * v0 + pt * 12 + pf * 10 + pp * near * 8 - pi * (v0 - hold) * 0.5; break; }   // gas on myself: ~12 (toxic) / ~10 (flame) HP; paralysed 8-12 turns with them beside me; invisible: they lose me
        case 5: { static const uint8_t PCT[11] = { 0, 5, 13, 21, 31, 41, 52, 63, 75, 88, 100 }; double nh = h->HP + h->HT * PCT[G.dew > 10 ? 10 : G.dew] / 100.0; if (nh > h->HT) nh = h->HT;
          int d0 = G.dew; G.dew = 0; v = EV(nh, h->pos, stay, 1, -1, 0); G.dew = d0; break; }   // (drunk: no revive left)
        case 6: v = 0; break;
        default: v = h->pos == G.entrance ? (o->kind == 7 ? hold : 0) : 0; break;   // (up: they stay by the stairs: the same fight when I come back, as I am now; down: a new floor)
      }
      #undef EV
      price[i] = v + o->extra; if (o->kind == 0) hold = price[i]; }
    if (!pass) {   // keep the hold and the best few for the second round
      int idx[700]; for (int i = 0; i < no; i++) idx[i] = i;
      for (int a = 0; a < no && a < keep + 1; a++) for (int b = a + 1; b < no; b++) if (price[idx[b]] < price[idx[a]]) { int t = idx[a]; idx[a] = idx[b]; idx[b] = t; }
      static bool sel[700]; memset(sel, 0, sizeof(bool) * no); sel[0] = true; for (int a = 0; a < no && a < keep; a++) sel[idx[a]] = true;
      for (int i = 0; i < no; i++) if (!sel[i]) price[i] = 1e9; } }
  if (!c_cal_on && getenv("CHCAL")) { c_seed = 0x51ed27u; s_te_hp = -1; ch_mc(F, nf, h->pos, NULL, 0, -1, 0, 256, h->HP); c_cal_on = 1; c_cal_hp0 = h->HP; c_cal_type = G.mobs[F[0].i].type; c_cal_nf = nf; c_cal_d = G.depth; c_cal_g = cur_game; c_cal_pd = s_te_pd; c_cal_E = s_te_E; }
  if (getenv("CALT") && G.depth == 5) { static int php = -1, pstep = -1; static double pl1; if (pstep == s_bot_steps - 1 && php >= 0) printf("CALT pred1 %.2f actual1 %d\n", pl1, php - h->HP);
    int bj = 0; for (int i = 1; i < no; i++) if (price[i] < price[bj]) bj = i; c_seed = 0x51ed27u; if (O[bj].kind == 1) { int path[16]; for (int j = 0; j < O[bj].n; j++) path[j] = O[bj].path[j]; ch_mc(F, nf, O[bj].P, path, O[bj].n, -1, 0, 256, h->HP); } else ch_mc(F, nf, h->pos, NULL, 0, -1, 0, 256, h->HP); int gi = F[0].i; printf("CALT2 hp %d/%d goo %d/%d pump %d adj %d pd %.2f E %.1f turns %.1f won %.2f\n", h->HP, h->HT, G.mobs[gi].HP, G.mobs[gi].HT, G.mobs[gi].aux2, adjacent(G.mobs[gi].pos, h->pos), s_te_pd, s_te_E, c_turns, c_won); php = h->HP; pstep = s_bot_steps; pl1 = c_l1; }
  s_te_hp = -1;
  int bi = 0; for (int i = 1; i < no; i++) if (price[i] < price[bi]) bi = i;
  double best = price[bi]; int bk = O[bi].kind, barg = O[bi].kind == 2 ? O[bi].arg : O[bi].kind == 1 ? O[bi].P : O[bi].arg;
  static const char *const KN[8] = { "hold", "walk", "throw", "heal", "unknown potion", "dew vial", "teleport", "stairs" }; const char *bw = KN[bk];
  if (getenv("CHDBG2")) { printf("   CHH at %d,%d -> hold %.1f", cx(h->pos), cy(h->pos), hold); for (int j = 0; j < nf; j++) printf(" | f%d %d,%d hp %.0f pf %.2f %d-%d rng %d ph %.2f", j, cx(F[j].pos), cy(F[j].pos), F[j].hp, c_pf[j], c_flo[j], c_fhi[j], F[j].rng, c_ph[j]); printf("\n"); }
  if (getenv("CHDBG")) printf("CH d%d t%d hp %d/%d foes %d hold %.1f -> %s %.1f\n", G.depth, (int)(G.now / TICK), h->HP, h->HT, nf, hold, bw, best);
  st_ch[bk]++;
  int32_t t0 = h->time; ActResult r = ACT_NONE;
  switch (bk) {
    case 0: { int t = -1; double tv = -1; for (int j = 0; j < nf; j++) if (adjacent(F[j].pos, h->pos)) { double v = F[j].dpt / (F[j].hp > 1 ? F[j].hp : 1); if (v > tv) { tv = v; t = j; } }
      if (t >= 0) { s_did[0] = 0; DID("choose: strike the %s (the fight costs ~%.0f HP as it stands)", mob_name(G.mobs[F[t].i].type), hold); r = game_tap_cell(F[t].pos); }
      else {   // (the plan priced as holding closes in on a shooter that won't come, as tact_eval plays it: do the same)
        int rr = -1, rd = 99; for (int j = 0; j < nf; j++) if (F[j].rng && distance(F[j].pos, h->pos) < rd) { rd = distance(F[j].pos, h->pos); rr = j; }
        int bq = -1; if (rr >= 0) { int bd = rd; for (int j = 0; j < 8; j++) { int q2 = h->pos + N8[j]; if (!nb_ok(h->pos, q2) || !BIT_GET(G.visited, q2) || !PASSABLE(G.map[q2]) || (TFLAGS(G.map[q2]) & F_AVOID) || char_at(q2)) continue; if (distance(q2, F[rr].pos) < bd) { bd = distance(q2, F[rr].pos); bq = q2; } } }
        if (bq >= 0) { s_did[0] = 0; DID("choose: close in on the %s (~%.0f)", mob_name(G.mobs[F[rr].i].type), hold); r = tap_safe(bq); }
        else { s_did[0] = 0; DID("choose: hold here, let it come (~%.0f)", hold); r = game_rest(false); } } break; }
    case 1: { int first = barg; while (par[first] != h->pos) first = par[first]; s_did[0] = 0; DID("choose: fight from %d,%d (~%.0f there vs ~%.0f here)", cx(barg), cy(barg), best, hold); r = tap_safe(first); break; }
    case 2: { s_did[0] = 0; DID("choose: throw at the %s (~%.0f vs ~%.0f)", mob_name(G.mobs[F[barg % 32].i].type), best, hold); r = game_throw(barg / 32, F[barg % 32].pos); break; }
    case 3: case 4: case 5: { s_did[0] = 0; DID("choose: drink the %s (~%.0f vs ~%.0f)", bw, best, hold); do_item(barg, ACN_DRINK); return 1; }
    case 6: { s_did[0] = 0; DID("choose: read Teleportation (~%.0f vs ~%.0f)", best, hold); do_item(barg, ACN_READ); return 1; }
    case 7: { s_did[0] = 0; DID("choose: the stairs (~%.0f vs ~%.0f)", best, hold); r = game_tap_cell(h->pos); break; }
  }
  if (r == ACT_NONE && h->time == t0) return 0;           // (it didn't take: the old code this turn)
  walk(r); return 1;
}
