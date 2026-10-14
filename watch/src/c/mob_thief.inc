// Thief / Bandit (actors/mobs/Thief.java, Bandit.java): steals, flees, drops gold when hit; shared by band overlays.
static bool thief_act(int i) {                         // Thief.Fleeing.nowhereToRun: cornered without Terror, it turns to fight
  Mob *m = M_(i);
  int p = m->pos;
  if (m->state != ST_FLEEING || mob_paralysed(m)) return false;
  mob_act_default(i);                                  // Mob.Fleeing.act: a step away, or a turn spent with nowhere to run
  if (m->pos == p && m->state == ST_FLEEING && !m->b.terror && m->HP > 0) {
    m->state = ST_HUNTING; if (seen(m->pos)) ui_fx(FX_TEXT, m->pos, i, "enraged");
  }
  return true;
}
static int thief_attack_proc(int i, int def, int dmg) { // Thief.steal
  Mob *m = M_(i);
  if (def < 0 && carried(i) < 0 && G.n_heaps < MAX_HEAPS) {   // (with the floor's item table full it has nowhere to keep it: no theft)
    int s = random_unequipped();
    if (s >= 0) {
      Item it = G.hero.bag[s]; char n[40]; short_name(&it, n, sizeof n);
      char tn[32]; snprintf(tn, sizeof tn, "%s", mob_name(m->type)); cap_first(tn);
      LOG(C_WARN, "%s stole %s from you!", tn, n);
      G.hero.bag[s] = (Item){ 0 };
      carry(i, it);
      int r = it.kind - IK_SEED_POUCH;                 // a bag: what it holds goes along (held for it at BAG_POS, see bag_stash)
      if (r >= 0 && r < 3) {
        for (Item *p = G.pack + 12 * r - (r ? 4 : 0); p < G.pack + 12 * r + 8; p++) if (carry_at(BAG_POS(r), *p)) *p = (Item){ 0 };
        bags_sync();                                   // no room for it all: the rest spills
      }
      m->state = ST_FLEEING;
      if (m->type == MT_BANDIT) { int t = rnd_int2(5, 12); if (!G.hero.b.blind) ui_log(txt(TX_L_BLINDED), C_WARN); if (t > G.hero.b.blind) G.hero.b.blind = t; update_fov(); }   // Bandit: blinded
    }
  }
  return dmg;
}
static void thief_defense_proc(int i) { if (M_(i)->state == ST_FLEEING) heap_put(M_(i)->pos, item_make(IK_GOLD, 1)); }   // new Gold(): a single coin
