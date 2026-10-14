// Fire elemental (actors/mobs/Elemental.java): sets you alight; fire feeds it, frost hurts it (game.c affect_*).
static int elemental_attack_proc(int def, int dmg) { if (rnd_int(2) == 0) affect_burning(def); return dmg; }
