// Succubus (actors/mobs/Succubus.java): charms what it hits; blinks closer when it sees you; shared by band overlays.
#define BLINK_DELAY 5
static int succubus_attack_proc(int i, int def, int dmg) {
  if (rnd_int(3) == 0) {                               // Charm: the victim won't strike back for a while
    Buffs *b = buffs_of(def);
    int t = elem_duration(def, rnd_range(3, 7));
    if (def < 0) { G.charm_uid = M_(i)->uid; if (!b->charm) ui_log(txt(TX_L_CHARMED), C_WARN); }   // Hero.add(Charm): a new one only
    b->charm = b->charm + t > 255 ? 255 : b->charm + t;   // (0.93 audit: Buff.affect on a FlavourBuff spends the time on top)
    ui_sound(SND_CHARMS);
    if (seen(pos_of(def))) ui_fx(FX_TEXT, pos_of(def), def, "charmed");
  }
  return dmg;
}
// Succubus.getCloser (Mob.getCloser hook), wandering or hunting: a target cell in its sight and over 2 away is blinked to,
// rooted or not, at no cost (spend(-1 / speed) against the state's 1 / speed); each plain step counts the delay (aux) down
static int succubus_closer(int i, int target) {
  Mob *m = M_(i);
  if (m->aux > 0 || distance(m->pos, target) <= 2 || !g_mh.sees(i, SEES_CELL(target))) { if (m->aux > 0) m->aux--; return target; }
  int16_t tr[BOLT_MAX + 1];
  int n = ballistica_cast(m->pos, target, true, true, tr), cell = tr[n - 1];
  if (n > 1 && char_at(cell)) cell = tr[n - 2];
  ui_fx(FX_TELEPORT, m->pos, i, NULL);
  mob_move(i, cell);                                   // WandOfBlink.appear: Char.move
  ui_fx(FX_TELEPORT, cell, i, NULL);
  m->aux = BLINK_DELAY;
  mob_spend(m, -mob_move_cost(m));
  return CL_MOVED;
}
