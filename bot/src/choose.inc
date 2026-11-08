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
static int choose(void) {
  Hero *h = &G.hero; if (s_rollout || h->b.paralysis || h->b.frost || G.depth % 5 == 0) return 0;
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
    if (MOB_DEFS[m->type].flags & (MD_BOSS | MD_NPC)) return 0;
    if (m->b.paralysis || m->b.frost) continue;
    if (nf >= 24) break; TFoe *f = &F[nf++]; f->wat = MOB_DEFS[m->type].flags & MD_WATER; f->i = i; f->pos = pos; f->hp = m->HP; f->dpt = foe_dpt(m); f->hd = hero_dpt(m); f->hi = MOB_DEFS[m->type].dmg_hi;
    f->acts = mob_move_cost(m) * 2 <= hero_move_cost() ? 2 : 1; f->mc = mob_move_cost(m) * TICK / (hero_move_cost() > 0 ? hero_move_cost() : TICK); if (f->mc < 5) f->mc = 5;
    f->ac = MOB_DEFS[m->type].atk_delay ? MOB_DEFS[m->type].atk_delay : TICK; f->tm = 0; f->rng = magic_shooter(m->type) || m->type == MT_SCORPIO; }
  if (nf == 0) return 0;
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
  // the price of a plan: the fight's HP plus its death chance; tact_eval gives E + 2 pd HT + unknown-ground: re-weigh the death part
  #define PRICE(v) ((v) + (kdeath - 2.0) * s_te_pd * HT)
  double best; int bk = 0, barg = -1; const char *bw = "hold";
  s_te_hp = -1; best = PRICE(tact_eval(F, nf, h->pos, NULL, 0, NULL, -1, 0)); double hold = best;
  // walk to a cell and fight from it
  static int16_t hd2[MAP_LEN], par[MAP_LEN], q[MAP_LEN]; for (int c = 0; c < MAP_LEN; c++) hd2[c] = -1;
  int qh = 0, qt = 0; hd2[h->pos] = 0; par[h->pos] = -1; q[qt++] = h->pos;
  while (qh < qt) { int c = q[qh++]; if (hd2[c] >= kreach) continue; for (int k = 0; k < 8; k++) { int n = c + N8[k];
      if (!nb_ok(c, n) || hd2[n] >= 0 || !BIT_GET(G.visited, n) || !PASSABLE(G.map[n]) || (TFLAGS(G.map[n]) & F_AVOID) || char_at(n) || bad_air(n) || n == G.exit || (h->b.roots && c == h->pos)) continue;
      hd2[n] = hd2[c] + 1; par[n] = c; q[qt++] = n; } }
  for (int qi = 1; qi < qt; qi++) { int P = q[qi]; int path[16], n = hd2[P]; if (n > 16) continue; for (int c = P, j = n - 1; j >= 0; j--, c = par[c]) path[j] = c;
    double v = PRICE(tact_eval(F, nf, P, path, n, NULL, -1, 0)); if (v < best) { best = v; bk = 1; barg = P; bw = "walk"; } }
  // throw a missile at a foe in line, two or more away
  for (int s = 0; s < BAG_SIZE; s++) { Item *it = &h->bag[s]; int k = it->kind; if (item_cat(k) != CAT_MISSILE || k == IK_BOOMERANG) continue;
    for (int j = 0; j < nf; j++) { Mob *m = &G.mobs[F[j].i]; bool hit; if (distance(F[j].pos, h->pos) < 2 || ballistica(h->pos, F[j].pos, &hit) != F[j].pos) continue;
      int def = mst(m, STAT_DEF, MOB_DEFS[m->type].def), dr = mst(m, STAT_DR, MOB_DEFS[m->type].dr);
      double td = phit(hero_attack_skill(it, F[j].pos), def) * dmg_vs_dr(weapon_min(it), weapon_max(it), dr);
      double v = PRICE(tact_eval(F, nf, h->pos, NULL, 0, NULL, j, td)) + cthrow * HT; if (v < best) { best = v; bk = 2; barg = s * 32 + j; bw = "throw"; } } }
  // a turn spent on something (a drink, a read): the foes act; then the fight goes on from here with the new HP
  int stay[1] = { h->pos };
  #define AFTER_TURN(hpnew) (s_te_hp = (hpnew), PRICE(tact_eval(F, nf, h->pos, stay, 1, NULL, -1, 0)))
  int hs = find_kind(IK_POT_HEALING);
  if (hs >= 0 && item_known(IK_POT_HEALING) && h->HP < h->HT) { double v = AFTER_TURN(h->HT) + cheal * HT; if (v < best) { best = v; bk = 3; barg = hs; bw = "heal"; } }
  for (int s = 0; s < BAG_SIZE; s++) { int k = h->bag[s].kind; if (item_cat(k) != CAT_POTION || item_known(k)) continue;
    bool dup = false; for (int t = 0; t < s; t++) if (h->bag[t].kind == k) dup = true; if (dup) continue;
    double ph = idb_p(k, IK_POT_HEALING), pt = idb_p(k, IK_POT_TOXIC), pf = idb_p(k, IK_POT_FLAME), pp = idb_p(k, IK_POT_PARALYTIC), pi = idb_p(k, IK_POT_INVIS);
    double near = 0; for (int j = 0; j < nf; j++) if (distance(F[j].pos, h->pos) <= 2) near += F[j].dpt;
    double vh = AFTER_TURN(h->HT), v0 = AFTER_TURN(-1);
    double v = ph * vh + (1 - ph) * v0 + pt * 12 + pf * 10 + pp * near * 8 - pi * (v0 - hold) * 0.5 + cunk * HT;   // gas on myself: ~12 (toxic) / ~10 (flame) HP; paralysed 8-12 turns with them beside me; invisible: they lose me
    if (v < best) { best = v; bk = 4; barg = s; bw = "unknown potion"; } }
  int vs = find_kind(IK_DEW_VIAL);
  if (vs >= 0 && G.dew > 0 && h->HP < h->HT) { static const uint8_t PCT[11] = { 0, 5, 13, 21, 31, 41, 52, 63, 75, 88, 100 };
    double nh = h->HP + h->HT * PCT[G.dew > 10 ? 10 : G.dew] / 100.0; if (nh > h->HT) nh = h->HT;
    int d0 = G.dew; G.dew = 0; double v = AFTER_TURN(nh) + cvial * d0 * HT; G.dew = d0;   // (drunk: no revive left)
    if (v < best) { best = v; bk = 5; barg = vs; bw = "dew vial"; } }
  int ts = find_kind(IK_SCR_TELEPORT);
  if (ts >= 0 && item_known(IK_SCR_TELEPORT) && !h->b.blind) { double v = ctele * HT; if (v < best) { best = v; bk = 6; barg = ts; bw = "teleport"; } }
  if ((h->pos == G.entrance && G.depth > 1) || (h->pos == G.exit && !boss_depth(G.depth + 1))) { double v = cstair * HT; if (v < best) { best = v; bk = 7; bw = "stairs"; } }
  s_te_hp = -1;
  #undef AFTER_TURN
  #undef PRICE
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
