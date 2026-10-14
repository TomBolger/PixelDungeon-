// Vampire bat (actors/mobs/Bat.java): drinks what it bites; shared by band overlays.
static int bat_attack_proc(int i, int dmg) {
  Mob *m = M_(i);
  int reg = dmg < m->HT - m->HP ? dmg : m->HT - m->HP;
  if (reg > 0) { m->HP += reg; if (seen(m->pos)) ui_fx(FX_HEAL, m->pos, i, NULL); }
  return dmg;
}
