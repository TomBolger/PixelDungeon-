// Gnoll shaman (actors/mobs/Shaman.java): lightning from afar; shared by band overlays.
static bool shaman_zap(int i, int enemy) {
  Mob *m = M_(i);
  int epos = pos_of(enemy);
  bool visible = seen(m->pos) || seen(epos);
  if (visible) {
    int16_t pts[2] = { m->pos, (int16_t)epos };
    ui_zap(IK_WND_LIGHTNING - IK_FIRST_WAND, m->pos, epos, pts, 2);
    ui_sound(SND_LIGHTNING);
  }
  mob_spend(m, 2 * TICK);                              // TIME_TO_ZAP
  if (char_hit(i, enemy, true)) {
    int dmg = rnd_int2(2, 12);
    bool fly = enemy < 0 ? hero_flying() : (MOB_DEFS[M_(enemy)->type].flags & MD_FLY) != 0;
    if (IS_WATER(G.map[epos]) && !fly) dmg = dmg * 3 / 2;
    if (enemy < 0) {
      hero_damage(elem_resist(-1, dmg), "Killed by a gnoll shaman on level %d");
      if (G.hero.HP <= 0) ui_log("Gnoll shaman's lightning bolt killed you...", C_BAD);
    } else { g_dmg_src = DS_SHOCK; mob_damage(enemy, dmg); }
  } else if (visible) ui_fx(FX_TEXT, epos, enemy, enemy < 0 ? "dodged" : "evaded");
  return true;
}

