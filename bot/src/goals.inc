// ---- Floor goals (owner 2026-11-12): one decision for the floor, in the judge's one currency.
// When nothing is about, the hero can leave now, rest and then leave, or keep exploring and then leave. Each is priced
// by the judge (judge2) at the next floor's arrival: the turns it takes (the food they eat, the regen they give, the
// clock), and for exploring, the loot still likely here. The loot can't be found by trying in a practice game (the copy
// has the real map: walking into unseen rooms would peek), so it comes from the model of what a floor holds
// (b2_floor_left's odds, from the source): food not yet found, the potions, the Strength potion and Upgrade scroll on
// their quotas. Off unless FGOAL=1 for now. Exploring is the old route's job; leaving and resting are done here.
static long st_fg[3];
// a plan's end, as the judge's inputs: T more turns pass (hunger rising, a meal eaten from the bag when it passes 260,
// 1 HP back each 10 turns while not starving), then the stairs: the next floor, unseen, its clock at zero
static double fg_value(const double *f0, double T, double hp_to, double dfood, double dunk, double dstr, double dup, double dlvl) {
  Hero *h = &G.hero; double f[51]; memcpy(f, f0, sizeof f);
  double hunger = h->hunger + T, food = f[10] + dfood; int nfood = 0; for (int s = 0; s < BAG_SIZE; s++) if (is_food(h->bag[s].kind)) nfood += h->bag[s].qty > 0 ? h->bag[s].qty : 1;
  double fbag = nfood + dfood;
  while (hunger >= 260 && fbag >= 1) { hunger -= 300; fbag -= 1; }
  double starve_t = hunger > 360 ? hunger - 360 : 0, hp = hp_to > 0 ? hp_to : h->HP + (T - starve_t) / 10.0; if (hp > h->HT) hp = h->HT;
  hp -= starve_t / 10.0; if (hp < 1) return 0;   // (starving: a point lost every 10 turns or so)
  double x = hp / (h->HT > 0 ? h->HT : 1);
  f[0] = x; f[1] = x * x; f[2] = 0; f[3] = 0; f[4] = 0; f[5] = 0;   // (at the arrival: no foe yet)
  f[8] = hunger >= 360; f[9] = hunger >= 260 && hunger < 360; food = fbag; f[10] = food < 3 ? (food > 0 ? food : 0) : 3;
  f[12] = f[12] + dunk > 5 ? 5 : f[12] + dunk; f[13] += dstr; f[17] += dup; f[6] += dlvl; f[7] += dlvl * 5;   // (a level: +5 max HP)
  f[21] = 0; f[22] = 0; f[23] = 0; f[24] = (G.now / (double)TICK + T) / 100.0;
  int d = G.depth + 1 > 25 ? 25 : G.depth + 1; for (int k = 1; k <= 25; k++) f[24 + k] = k == d;
  return j2_score(f); }
static int fgoal(void) {
  Hero *h = &G.hero; if (!getenv("FGOAL") || s_rollout || s_is_child) return 0;
  if (boss_depth(G.depth) || boss_depth(G.depth + 1) || G.depth >= 25 || G.exit < 0 || !BIT_GET(G.visited, G.exit)) return 0;   // (boss floors and the floor before one: the old rules, for now)
  if (G.map[G.exit] == T_LOCKED_EXIT && !(G.keys[G.depth] & 64)) return 0;
  if (!mc_calm() || enemy_count() > 0 || quest_on_floor() || ghost_pending()) return 0;
  bfs(h->pos, false); if (b_dist[G.exit] >= 0x7FFF) return 0;
  for (int i = 0; i < G.n_heaps; i++) if (heap_wanted(i) && !far_missile(i) && b_dist[G.heaps[i].pos] < 0x7FFF) return 0;   // (an item in sight: the route fetches it; seen loot is the route's)
  double f0[51]; j2_feats(f0); double te = b_dist[G.exit];
  // what exploring the rest is likely to bring (the floor model), and the moves it takes
  double mv = 0, sf = 0; b2_floor_left(&mv, &sf); double u = 1 - sf;
  double pfood = !s_ev_food && !boss_depth(G.depth) ? (u > 0.3 ? 1 : u / 0.3) : 0;
  double pp = q_unseen(s_q_pos, Q_POS, G.depth, s_q_fp, u), ps = q_unseen(s_q_sou, Q_SOU, G.depth, s_q_fs, u);
  double punk = u * 3 * 0.5 * 0.28;   // (3 random items a floor, about half seen so far per unseen share; a potion 50 in 181 of the generator)
  // the monsters still to meet: the unseen share of a floor's start (3 + depth%5 + 1), each its XP (1 + depth/2; little once I'm far above it), a level every 5 + 5*lvl
  double nm = u * (4 + G.depth % 5), xp = 1 + G.depth / 2.0; if (h->lvl > 2 * G.depth + 6) xp *= 0.3; double dlvl = nm * xp / (5.0 + 5.0 * h->lvl);
  double rest_t = (h->HT - h->HP) * 10.0;
  double v_leave = fg_value(f0, te, 0, 0, 0, 0, 0, 0);
  double v_rest = h->HP < h->HT && !game_hungry() ? fg_value(f0, te + rest_t, h->HT, 0, 0, 0, 0, 0) : -1;
  double v_expl = mv > 0 ? fg_value(f0, mv + te, 0, pfood, punk, pp, ps, dlvl) : -1;
  static int ls = -1; if (getenv("FGDBG") && ls != s_bot_steps) { ls = s_bot_steps; printf("FG d%d t%d hp %d/%d hunger %d seen %.2f moves %.0f | leave %.4f rest %.4f explore %.4f (food %.2f str %.2f up %.2f)\n", G.depth, (int)(G.now / TICK), h->HP, h->HT, h->hunger, sf, mv, v_leave, v_rest, v_expl, pfood, pp, ps); }
  if (v_expl >= v_leave && v_expl >= v_rest) { st_fg[0]++; return 0; }   // keep exploring: the old route
  if (v_rest > v_leave) { st_fg[1]++; s_did[0] = 0; DID("goal: rest, then down (judge %.3f vs down now %.3f, exploring %.3f)", v_rest, v_leave, v_expl); walk(game_rest(false)); return 1; }
  st_fg[2]++; s_did[0] = 0; DID("goal: down (judge %.3f vs exploring %.3f)", v_leave, v_expl);
  if (h->pos == G.exit) { walk(game_tap_cell(h->pos)); return 1; }
  ActResult r = tap_safe(G.exit); (void)r; return 1; }
