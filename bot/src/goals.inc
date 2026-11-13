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
static double fg_value(const double *f0, double T, double hp_to, double dfood, double dunk, double dstr, double dup, double dlvl, double dat, double dwt) {
  Hero *h = &G.hero; double f[51]; memcpy(f, f0, sizeof f);
  double hunger = h->hunger + T, food = f[10] + dfood; int nfood = 0; for (int s = 0; s < BAG_SIZE; s++) if (is_food(h->bag[s].kind)) nfood += h->bag[s].qty > 0 ? h->bag[s].qty : 1;
  double fbag = nfood + dfood;
  while (hunger >= 260 && fbag >= 1) { hunger -= 300; fbag -= 1; }
  double starve_t = hunger > 360 ? hunger - 360 : 0, hp = hp_to > 0 ? hp_to : h->HP + (T - starve_t) / 10.0; if (hp > h->HT) hp = h->HT;
  hp -= starve_t / 10.0; if (hp < 1) return 0;   // (starving: a point lost every 10 turns or so)
  double x = hp / (h->HT > 0 ? h->HT : 1);
  f[0] = x; f[1] = x * x; f[2] = 0; f[3] = 0; f[4] = 0; f[5] = 0;   // (at the arrival: no foe yet)
  // food, as a running stock: the turns eat it a 300th of a ration each, smoothly (the hungry and starving flags stay as they
  // are unless the food runs out on the way): read through the flags, 96 turns of exploring cost 0.06 on floor 1 at 20/20,
  // more than all its loot, because the judge's hungry flag flipped on the way (a step the next floor's meal undoes)
  (void)food; double stock = nfood + dfood - T / 300.0;
  if (getenv("FGFLAGS")) { f[8] = hunger >= 360; f[9] = hunger >= 260 && hunger < 360; stock = fbag; }
  else if (stock + (360 - h->hunger) / 300.0 < 0) { f[8] = 1; f[9] = 0; }   // (starving only once the stomach and the bag are both empty)
  f[10] = stock < 3 ? (stock > -1 ? stock : -1) : 3;   // (below none: the judge's line carried on a little, so the turns still cost)
  f[12] = f[12] + dunk > 5 ? 5 : f[12] + dunk; f[13] += dstr; f[17] += dup; f[6] += dlvl; f[7] += dlvl * 5; f[14] += dat; f[16] += dwt;   // (a level: +5 max HP)
  f[21] = 0; f[22] = 0; f[23] = 0; f[24] = (G.now / (double)TICK + T) / 100.0;
  int d = G.depth + 1 > 25 ? 25 : G.depth + 1; for (int k = 1; k <= 25; k++) f[24 + k] = k == d;
  if (getenv("FGDBG3")) { printf("FGF T %.0f:", T); for (int i = 0; i < 25; i++) printf(" %d=%.3f", i, f[i]); printf("\n"); }
  return j2_score(f); }
// the floors the goal owns (boss floors and the floor before one, and floors with a quest: the old rules, for now): there, the
// old 'hungry: down' rule is gone (the goal prices the hunger: the food likely still here, against the turns)
static bool fgoal_owns(void) {
  if (!getenv("FGOAL") || boss_depth(G.depth) || boss_depth(G.depth + 1) || G.depth >= 25 || G.exit < 0 || !BIT_GET(G.visited, G.exit)) return false;
  if (G.map[G.exit] == T_LOCKED_EXIT && !(G.keys[G.depth] & 64)) return false;
  return !quest_on_floor() && !ghost_pending(); }
static int fgoal(void) {
  Hero *h = &G.hero; if (!getenv("FGOAL") || s_rollout || s_is_child) return 0;
  if (!fgoal_owns()) return 0;
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
  // a better weapon or armour among the random items still unseen (3.67 a floor on average, of which a weapon 15 in 181, an
  // armour 10 in 181; the kinds picked evenly): the chance one beats what I wear, a tier up when it does
  double eun = u * 3.67; int at = h->armor.kind ? item_tier(h->armor.kind) : 0, wt = h->weapon.kind ? item_tier(h->weapon.kind) : 0, na = 0, nw2 = 0, ba = 0, bw = 0;
  for (int k = 0; k < IK_COUNT; k++) { int c = item_cat(k); if (c == CAT_ARMOR && !CLASS_ARMOR(k)) { na++; if (item_tier(k) > at) ba++; } else if (c == CAT_WEAPON && k < IK_DART) { nw2++; if (item_tier(k) > wt) bw++; } }
  double dat = na ? eun * 10 / 181.0 * ba / na : 0, dwt = nw2 ? eun * 15 / 181.0 * bw / nw2 : 0;
  double rest_t = (h->HT - h->HP) * 10.0;
  double v_leave = fg_value(f0, te, 0, 0, 0, 0, 0, 0, 0, 0);
  double v_rest = h->HP < h->HT && !game_hungry() ? fg_value(f0, te + rest_t, h->HT, 0, 0, 0, 0, 0, 0, 0) : -1;
  double v_expl = mv > 0 ? fg_value(f0, mv + te, 0, pfood, punk, pp, ps, dlvl, dat, dwt) : -1;
  static int ls = -1; if (getenv("FGDBG") && ls != s_bot_steps) { ls = s_bot_steps; printf("FG d%d t%d hp %d/%d hunger %d seen %.2f moves %.0f | leave %.4f rest %.4f explore %.4f (food %.2f str %.2f up %.2f)\n", G.depth, (int)(G.now / TICK), h->HP, h->HT, h->hunger, sf, mv, v_leave, v_rest, v_expl, pfood, pp, ps); }
  if (v_expl >= v_leave && v_expl >= v_rest) { st_fg[0]++; return 0; }   // keep exploring: the old route
  if (v_rest > v_leave) { st_fg[1]++; s_did[0] = 0; DID("goal: rest, then down (judge %.3f vs down now %.3f, exploring %.3f)", v_rest, v_leave, v_expl); walk(game_rest(false)); return 1; }
  st_fg[2]++; s_did[0] = 0; DID("goal: down (judge %.3f vs exploring %.3f)", v_leave, v_expl);
  if (h->pos == G.exit) { walk(game_tap_cell(h->pos)); return 1; }
  ActResult r = tap_safe(G.exit); (void)r; return 1; }
