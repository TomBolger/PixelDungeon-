// Dwarf monk / senior monk (actors/mobs/Monk.java, Senior.java): knocks your weapon away; the senior stuns.
static int monk_attack_proc(int i, int def, int dmg) {
  if (M_(i)->type == MT_SENIOR && rnd_int(10) == 0) affect_paralysis(def, 1);   // Senior: Paralysis 1.1
  if (def < 0 && rnd_int(6) == 0) {
    Item *w = &G.hero.weapon;
    if (w->kind && w->kind != IK_KNUCKLES && !(w->flags & IF_CURSED)) {
      char n[32], wn[40]; snprintf(n, sizeof n, "%s", mob_name(M_(i)->type)); cap_first(n); short_name(w, wn, sizeof wn);
      LOG(C_WARN, "%s has knocked the %s from your hands!", n, wn);
      Item it = *w; *w = (Item){ 0 };
      level_drop(G.hero.pos, it);
    }
  }
  return dmg;
}
