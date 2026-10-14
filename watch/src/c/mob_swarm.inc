// Swarm of flies (actors/mobs/Swarm.java): splits when hit, drops less with each generation; shared by band overlays.
static void swarm_defense_proc(int i, int dmg) {
  Mob *m = M_(i);
  if (m->HP < dmg + 2) return;
  int c = free_nb(m->pos, true);
  if (c < 0) return;
  int j = spawn_mob(MT_SWARM, c, ST_HUNTING, TICK);
  if (j < 0) return;
  Mob *cl = M_(j); m = M_(i);
  cl->aux2 = m->aux2 + 1;                              // generation
  cl->HP = (m->HP - dmg) / 2;
  if (m->b.burning) cl->b.burning = m->b.burning;
  if (m->b.poison) cl->b.poison = 2;
  if (G.map[c] == T_DOOR) set_terrain(c, T_OPEN_DOOR);
  m->HP -= cl->HP;
  ui_fx(FX_SPAWN, c, j, NULL);
}
static void swarm_die(int i) {                         // Swarm.dropLoot
  Mob *m = M_(i);
  if (G.hero.lvl <= MOB_DEFS[MT_SWARM].max_lvl + 2 && rnd_int(5 * (m->aux2 + 1)) == 0) heap_put(m->pos, item_make(IK_POT_HEALING, 1));
}
