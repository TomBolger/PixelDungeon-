/*
 * Pixel Dungeon for Pebble - the test kit's floors (TEST_BUILD only; included by the newgame and content overlays).
 * 0.34: depth 1 holds the gear (armour, weapons, wands, rings, bags and tools), depth 2 the consumables (scrolls, potions,
 * seeds and the alchemy pot, food, missiles, bombs); no creatures on either (and no respawning until depth 3).
 */
static int s_tr, s_tc;                             // the room being filled, and the next cell in it
static int8_t s_rooms[MAX_ROOMS]; static int s_nrooms;
static bool free_cell(int c) {
  return PASSABLE(G.map[c]) && !(TFLAGS(G.map[c]) & (F_AVOID | F_SECRET | F_PIT)) && c != G.entrance && c != G.exit && heap_at(c) < 0 && mob_at(c) < 0;
}
static void next_room(void) { s_tr++; s_tc = 0; }
static void lay(Item it) {
  it.flags = (it.flags & ~IF_CURSED) | IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN;
  for (int guard = 0; guard < MAX_ROOMS * 2 && G.n_heaps < MAX_HEAPS; guard++) {
    Room *R = &G.rooms[s_rooms[s_tr % s_nrooms]];
    int w = R->right - R->left - 1, h = R->bottom - R->top - 1;
    while (s_tc < w * h) {
      int c = (R->left + 1 + s_tc % w) + (R->top + 1 + s_tc / w) * MAP_W;
      s_tc++;
      if (free_cell(c)) { G.heaps[G.n_heaps++] = (Heap){ c, HEAP_PLAIN, it }; return; }
    }
    next_room();
  }
}
static void lay_gear(int kind, int ench) { Item it = item_make(kind, 1); it.lvl = 3; it.ench = ench; it.dur = item_max_dur(&it); lay(it); }
static void lay_n(int kind, int n) { Item it = item_make(kind, n); it.qty = n; lay(it); }
static void kit_rooms(void) {                     // rooms nearest the entrance first; the floor's own things make way
  G.n_heaps = 0;
  int k = 0; for (int i = 0; i < G.n_mobs; i++) if (G.mobs[i].HP > 0 && (MOB_DEFS[G.mobs[i].type].flags & MD_NPC)) G.mobs[k++] = G.mobs[i];
  G.n_mobs = k;                                    // only the people stay
  memset(G.visited, 0xFF, sizeof G.visited);
  s_nrooms = 0;
  int ex = G.entrance % MAP_W, ey = G.entrance / MAP_W;
  for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type != RT_TUNNEL && G.rooms[r].type != RT_PASSAGE && G.rooms[r].type != RT_NULL) s_rooms[s_nrooms++] = r;
  for (int a = 0; a < s_nrooms; a++) for (int b = a + 1; b < s_nrooms; b++) {
    Room *A = &G.rooms[s_rooms[a]], *B = &G.rooms[s_rooms[b]];
    int da = abs((A->left + A->right) / 2 - ex) + abs((A->top + A->bottom) / 2 - ey), db = abs((B->left + B->right) / 2 - ex) + abs((B->top + B->bottom) / 2 - ey);
    if (db < da) { int8_t t = s_rooms[a]; s_rooms[a] = s_rooms[b]; s_rooms[b] = t; }
  }
  s_tr = 0; s_tc = 0;
}
static void kit_gear(void) {                       // depth 1
  kit_rooms();
  if (!s_nrooms) return;
  for (int g = 1; g < AG_COUNT; g++) lay_gear(IK_PLATE_ARMOR, g);              // plate armour of every glyph
  next_room();
  for (int e = 1; e < WE_COUNT; e++) lay_gear(IK_WAR_HAMMER, e);               // war hammers of every enchantment
  next_room();
  for (int k = IK_FIRST_WAND; k < IK_FIRST_WAND + N_WANDS; k++) { Item w = item_make(k, 1); w.lvl = 3; w.ench = wand_max(&w); lay(w); }
  { Item w = item_make(IK_WND_MM, 1); w.lvl = 3; w.ench = wand_max(&w); lay(w); }
  next_room();
  for (int k = IK_FIRST_RING; k < IK_FIRST_RING + N_RINGS; k++) lay_gear(k, 0);
  next_room();
  static const uint8_t WPN[10] = { IK_DAGGER, IK_KNUCKLES, IK_SHORT_SWORD, IK_QUARTERSTAFF, IK_SPEAR, IK_MACE, IK_SWORD, IK_LONGSWORD, IK_BATTLE_AXE, IK_GLAIVE };
  for (int i = 0; i < 10; i++) lay_gear(WPN[i], 0);
  for (int k = IK_CLOTH_ARMOR; k < IK_CLOTH_ARMOR + 5; k++) lay_gear(k, 0);
  lay_gear(IK_BOOMERANG, 0);
  next_room();
  static const uint8_t MISC[11] = { IK_SEED_POUCH, IK_SCROLL_HOLDER, IK_WAND_HOLSTER, IK_DEW_VIAL, IK_ANKH, IK_ANKH, IK_MASTERY, IK_ARMOR_KIT,
                                    IK_BEACON, IK_WEIGHTSTONE, IK_PICKAXE };
  for (int i = 0; i < 11; i++) lay_n(MISC[i], 1);
}
static void kit_consumables(void) {                // depth 2
  kit_rooms();
  if (!s_nrooms) return;
  for (int k = IK_FIRST_SCROLL; k < IK_FIRST_SCROLL + N_SCROLLS; k++) lay_n(k, 5);
  lay_n(IK_WIPE_OUT, 2);
  next_room();
  for (int k = IK_FIRST_POTION; k < IK_FIRST_POTION + N_POTIONS; k++) lay_n(k, 5);
  next_room();
  for (int k = IK_FIRST_SEED; k < IK_FIRST_SEED + 8; k++) lay_n(k, 5);
  lay_n(IK_DEW, 1); set_terrain(G.heaps[--G.n_heaps].pos, T_ALCHEMY);   // an alchemy pot beside the seeds
  next_room();
  static const uint8_t FOOD[6] = { IK_RATION, IK_PASTY, IK_MYSTERY_MEAT, IK_OVERPRICED, IK_CHARGRILLED, IK_CARPACCIO };
  for (int i = 0; i < 6; i++) lay_n(FOOD[i], 3);
  next_room();
  static const uint8_t MIS[6] = { IK_DART, IK_INCENDIARY_DART, IK_CURARE_DART, IK_SHURIKEN, IK_JAVELIN, IK_TOMAHAWK };
  for (int i = 0; i < 6; i++) lay_n(MIS[i], 15);
  lay_n(IK_TORCH, 5); lay_n(IK_BOMB, 5); lay_n(IK_HONEYPOT, 3);
}
