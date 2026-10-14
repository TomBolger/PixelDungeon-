// Gnoll brute / shielded brute (actors/mobs/Brute.java, Shielded.java): enraged below a quarter of its health.
static bool brute_enraged(int i) { return M_(i)->HP < M_(i)->HT / 4; }
static int brute_stats(int i, int w) {
  if (w == STAT_DMG) return brute_enraged(i) ? rnd_normal(10, 40) : rnd_normal(8, 18);
  return -1;
}
static void brute_damage(int i, int dmg) {             // Brute.damage: the moment it becomes enraged (before HP drops)
  Mob *m = M_(i);
  int after = m->HP - dmg;
  if (after > 0 && m->HP >= m->HT / 4 && after < m->HT / 4) {
    mob_spend(m, TICK);
    if (seen(m->pos)) { char n[32]; snprintf(n, sizeof n, "%s", mob_name(m->type)); cap_first(n); LOG(C_WARN, "%s becomes enraged!", n); ui_fx(FX_TEXT, m->pos, i, "enraged"); }
  }
}
