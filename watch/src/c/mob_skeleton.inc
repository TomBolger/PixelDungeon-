// Skeleton (actors/mobs/Skeleton.java): its death bursts in bones; shared by band overlays.
static void skeleton_die(int i) {
  Mob *m = M_(i);
  bool hero_killed = false;
  for (int k = 0; k < 8; k++) {
    int c = m->pos + N8[k];
    if (!nb_ok(m->pos, c)) continue;
    if (c == G.hero.pos && G.hero.HP > 0) {
      int d = rnd_normal(3, 8) - rnd_range(0, hero_dr() / 2);
      if (d > 0) { hero_damage(d, "Killed by a skeleton on level %d"); if (G.hero.HP <= 0) hero_killed = true; }
    } else { int j = mob_at(c); if (j >= 0) { int d = rnd_normal(3, 8) - rnd_range(0, MOB_DEFS[M_(j)->type].dr / 2); if (d > 0) mob_damage(j, d); } }
  }
  if (seen(m->pos)) ui_sound(SND_BONES);
  if (hero_killed) ui_log("You were killed by the explosion of bones...", C_BAD);
}
