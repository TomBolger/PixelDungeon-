/*
 * Pixel Dungeon for Pebble - a fresh floor's monsters and items (part of the level overlay).
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Level.create / createMobs / createItems, Generator, Weapon.random,
 * Armor.random, Dungeon.posNeeded / souNeeded.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"
#ifdef TEST_BUILD
#include "testkit.h"
#endif

// item generation lives in the "gen" overlay (src/ovl/gen), shared with loot, quests and shops
static int random_drop_cell(void) {                    // RegularLevel.randomDropCell
  for (int i = 0; i < 2000; i++) {
    int c = level_random_room_cell(RT_STANDARD, 1);
    if (c >= 0 && PASSABLE(G.map[c])) return c;
  }
  for (int i = 0; i < 4000; i++) { int c = rnd_int(MAP_LEN); if (PASSABLE(G.map[c])) return c; }
  return G.exit;
}
static bool quota_needed(const uint8_t *quota, int have) {   // Dungeon.chance
  for (int i = 0; i < 10; i += 2)
    if (G.depth <= quota[i]) return rnd_int(1000) < 1000 * (quota[i + 1] - have) / (quota[i] - G.depth + 1);
  return false;
}
// Level.itemsToSpawn: what must appear on this floor; special rooms may take some as their prize
static Item s_spawn[16]; static int s_nspawn;
static void add_spawn(Item it) { if (s_nspawn < 16) s_spawn[s_nspawn++] = it; }
static bool spawn_prize(Item *out) {                   // Level.itemToSpanAsPrize
  if (s_nspawn && rnd_int(s_nspawn + 1) > 0) { int k = rnd_int(s_nspawn); *out = s_spawn[k]; s_spawn[k] = s_spawn[--s_nspawn]; return true; }
  return false;
}
static Item key_item(int kind) { Item k = item_make(kind, 1); k.lvl = G.depth; return k; }
static int room_cell(const Room *r, int tile, bool no_heap) {   // room.random() until the tile matches (and nothing lies there)
  for (int n = 0; n < 400; n++) {                      // (PD's painters drop before paintWater / paintGrass / decorate: what
    int c = level_room_random(r, 0), t = G.map[c];     // those made of EMPTY counts as EMPTY, else a small library can run out)
    bool ok = tile < 0 || t == tile || (tile == T_EMPTY && PASSABLE(t) && t != T_EMPTY_SP && !(TFLAGS(t) & (F_AVOID | F_PIT)));
    if (ok && (!no_heap || heap_at(c) < 0)) return c;
  }
  return level_room_random(r, 0);
}
static Item best_of(int cats, int tries) {             // the highest level of several draws (cats: bit 0 armour, bit 1 weapon)
  Item best = (Item){ 0 };
  for (int i = 0; i < tries; i++) {
    int c = cats == 3 ? (rnd_int(2) ? GC_WEAPON : GC_ARMOR) : cats == 1 ? GC_ARMOR : GC_WEAPON;
    Item o = gen_item(c);
    if (!best.kind || o.lvl > best.lvl) best = o;
  }
  return best;
}
static Item prize_or(int cat) { Item p; if (spawn_prize(&p)) return p; return gen_item(cat); }
static Item shop_item(int kind, bool known) { Item it = item_make(kind, 1); if (known) it.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; return it; }
// ShopPainter: the wares round the walls, starting across from the door
static int pas_w, pas_h;
static int xy2p(const Room *r, int x, int y) {
  if (y == r->top) return x - r->left - 1;
  if (x == r->right) return y - r->top - 1 + pas_w;
  if (y == r->bottom) return r->right - x - 1 + pas_w + pas_h;
  if (y == r->top + 1) return 0;
  return r->bottom - y - 1 + pas_w * 2 + pas_h;
}
static int p2xy(const Room *r, int p) {
  if (p < pas_w) return r->left + 1 + p + (r->top + 1) * MAP_W;
  if (p < pas_w + pas_h) return r->right - 1 + (r->top + 1 + p - pas_w) * MAP_W;
  if (p < pas_w * 2 + pas_h) return r->right - 1 - (p - pas_w - pas_h) + (r->bottom - 1) * MAP_W;
  return r->left + 1 + (r->bottom - 1 - (p - pas_w * 2 - pas_h)) * MAP_W;
}
static void shop_content(int ri) {
  Room *r = &G.rooms[ri];
  Item range[16]; int n = 0;
  switch (G.depth) {
    case 6: range[n++] = shop_item(rnd_int(2) == 0 ? IK_QUARTERSTAFF : IK_SPEAR, true); range[n++] = shop_item(IK_LEATHER_ARMOR, true);
            range[n++] = shop_item(IK_SEED_POUCH, false); range[n++] = shop_item(IK_WEIGHTSTONE, false); break;
    case 11: range[n++] = shop_item(rnd_int(2) == 0 ? IK_SWORD : IK_MACE, true); range[n++] = shop_item(IK_MAIL_ARMOR, true);
             range[n++] = shop_item(IK_SCROLL_HOLDER, false); range[n++] = shop_item(IK_WEIGHTSTONE, false); break;
    case 16: range[n++] = shop_item(rnd_int(2) == 0 ? IK_LONGSWORD : IK_BATTLE_AXE, true); range[n++] = shop_item(IK_SCALE_ARMOR, true);
             range[n++] = shop_item(IK_WAND_HOLSTER, false); range[n++] = shop_item(IK_WEIGHTSTONE, false); break;
    case 21: { static const uint8_t K[3] = { IK_GLAIVE, IK_WAR_HAMMER, IK_PLATE_ARMOR }; range[n++] = shop_item(K[rnd_int(3)], true);
               range[n++] = shop_item(IK_TORCH, false); range[n++] = shop_item(IK_TORCH, false); break; }
  }
  range[n++] = item_make(IK_POT_HEALING, 1);
  for (int i = 0; i < 3; i++) range[n++] = gen_item(GC_POTION);
  range[n++] = item_make(IK_SCR_IDENTIFY, 1); range[n++] = item_make(IK_SCR_REMOVE_CURSE, 1); range[n++] = item_make(IK_SCR_MAPPING, 1);
  range[n++] = gen_item(GC_SCROLL);
  range[n++] = item_make(IK_OVERPRICED, 1); range[n++] = item_make(IK_OVERPRICED, 1);
  range[n++] = item_make(IK_ANKH, 1);
  for (int i = n - 1; i > 0; i--) { int j = rnd_int(i + 1); Item t = range[i]; range[i] = range[j]; range[j] = t; }   // Random.shuffle
  pas_w = r->right - r->left - 2; pas_h = r->bottom - r->top - 2;
  int per = pas_w * 2 + pas_h * 2, door = G.rooms[ri].cell;
  int pos = xy2p(r, door % MAP_W, door / MAP_W) + (per - n) / 2;
  for (int i = 0; i < n; i++, pos++) {
    int cell = p2xy(r, ((pos % per) + per) % per);
    if (heap_at(cell) >= 0) cell = room_cell(r, -1, true);
    heap_put_t(cell, range[i], HEAP_FOR_SALE);
  }
  int c = room_cell(r, -1, true);
  spawn_mob(G.depth == 21 ? MT_IMP_SHOP : MT_SHOPKEEPER, c, ST_PASSIVE, 0);
  if (G.depth == 21)                                   // ShopPainter.placeShopkeeper on the last shop: a moat round the imp
    for (int k = -1; k < 8; k++) { int p = k < 0 ? c : c + N8[k]; if (G.map[p] == T_EMPTY_SP) set_terrain(p, T_WATER); }
}
static void special_content(void) {                    // what each special room's painter drops and spawns
  for (int ri = 0; ri < G.n_rooms; ri++) {
    Room *r = &G.rooms[ri];
    int cell = G.rooms[ri].cell;
    switch (r->type) {
      case RT_ARMORY: {
        int n = 3 + (rnd_int(4) == 0);
        for (int i = 0; i < n; i++) {
          int c = room_cell(r, T_EMPTY, true);
          heap_put(c, rnd_int(6) == 0 ? item_make(IK_BOMB, rnd_range(1, 3)) : gen_item(rnd_int(2) ? GC_WEAPON : GC_ARMOR));
        }
        add_spawn(key_item(IK_IRON_KEY));
        break;
      }
      case RT_CRYPT: add_spawn(key_item(IK_IRON_KEY)); if (cell >= 0) heap_put_t(cell, best_of(1, 4), HEAP_TOMB); break;
      case RT_GARDEN:
        if (rnd_int(2) == 0) heap_put(level_room_random(r, 0), item_make(IK_HONEYPOT, 1));
        else {
          int bushes = rnd_int(5) == 0 ? 2 : 1;
          for (int i = 0; i < bushes; i++) {
            int c = level_room_random(r, 0);
            G.map[c] = T_GRASS;
            int p = plant_at(c); if (p >= 0) G.plants[p] = G.plants[--G.n_plants];
            if (G.n_plants < MAX_PLANTS) G.plants[G.n_plants++] = (Plant){ (int16_t)c, 4 };   // Sungrass
          }
        }
        break;
      case RT_LABORATORY: {
        int n = rnd_range(2, 3);
        for (int i = 0; i < n; i++) {
          int c = room_cell(r, T_EMPTY_SP, true);
          Item p = { 0 }; if (spawn_prize(&p) && item_cat(p.kind) != CAT_POTION) { add_spawn(p); p = gen_item(GC_POTION); } else if (!p.kind) p = gen_item(GC_POTION);
          heap_put(c, p);
        }
        add_spawn(key_item(IK_IRON_KEY));
        break;
      }
      case RT_LIBRARY: {
        int n = rnd_range(2, 3);
        for (int i = 0; i < n; i++) {
          int c = room_cell(r, T_EMPTY, true);
          Item p = { 0 }; if (spawn_prize(&p) && item_cat(p.kind) != CAT_SCROLL) { add_spawn(p); p = gen_item(GC_SCROLL); } else if (!p.kind) p = gen_item(GC_SCROLL);
          heap_put(c, p);
        }
        add_spawn(key_item(IK_IRON_KEY));
        break;
      }
      case RT_PIT: {
        int c = room_cell(r, -1, false);
        for (int g = 0; g < 100 && G.map[c] == T_EMPTY_WELL; g++) c = level_room_random(r, 0);
        heap_put_t(c, key_item(IK_IRON_KEY), HEAP_SKELETON);
        heap_put_t(c, rnd_int(5) == 0 ? gen_item(GC_RING) : gen_item(rnd_int(2) ? GC_ARMOR : GC_WEAPON), HEAP_SKELETON);
        int n = rnd_range(1, 2);
        static const uint8_t C4[4] = { GC_POTION, GC_SCROLL, GC_FOOD, GC_GOLD };
        for (int i = 0; i < n; i++) heap_put_t(c, prize_or(C4[rnd_int(4)]), HEAP_SKELETON);
        break;
      }
      case RT_POOL: {
        if (cell < 0) break;
        Item p; if (!spawn_prize(&p)) p = best_of(3, 5);
        heap_put_t(cell, p, rnd_int(3) == 0 ? HEAP_CHEST : HEAP_PLAIN);
        add_spawn(item_make(IK_POT_INVIS, 1));
        for (int i = 0; i < 3; i++) {                  // NPIRANHAS
          int c = -1;
          for (int g = 0; g < 200 && c < 0; g++) { int t = level_room_random(r, 0); if (IS_WATER(G.map[t]) && !char_at(t)) c = t; }
          if (c >= 0) spawn_mob(MT_PIRANHA, c, ST_SLEEPING, 0);
        }
        break;
      }
      case RT_STATUE: add_spawn(key_item(IK_IRON_KEY)); if (cell >= 0) spawn_mob(MT_STATUE, cell, ST_PASSIVE, 0); break;
      case RT_STORAGE: {
        int n = rnd_range(3, 4);
        static const uint8_t C5[5] = { GC_POTION, GC_SCROLL, GC_FOOD, GC_GOLD, GC_MISC };
        for (int i = 0; i < n; i++) { int c = room_cell(r, T_EMPTY_SP, false); heap_put(c, prize_or(C5[rnd_int(5)])); }
        add_spawn(item_make(IK_POT_FLAME, 1));
        break;
      }
      case RT_TRAPS:
        if (cell >= 0) { Item p; if (!spawn_prize(&p)) p = best_of(3, 4); heap_put_t(cell & 0x3FFF, p, (cell & 0x4000) ? HEAP_CHEST : HEAP_PLAIN); }
        add_spawn(item_make(IK_POT_LEVITATION, 1));
        break;
      case RT_TREASURY: {
        int type = rnd_int(2) == 0 ? HEAP_CHEST : HEAP_PLAIN, n = rnd_range(2, 3);
        for (int i = 0; i < n; i++) {
          int c = room_cell(r, T_EMPTY, true);
          heap_put_t(c, item_make(IK_GOLD, gold_amount()), i == 0 && type == HEAP_CHEST ? HEAP_MIMIC : type);
        }
        if (type == HEAP_PLAIN) for (int i = 0; i < 6; i++) heap_put(room_cell(r, T_EMPTY, false), item_make(IK_GOLD, rnd_range(1, 3)));
        add_spawn(key_item(IK_IRON_KEY));
        break;
      }
      case RT_VAULT: {
        if (cell < 0) break;
        int c = cell & 0xFFF, k = cell >> 12;
        Item a = gen_item(rnd_int(2) ? GC_RING : GC_WAND);
        if (k == 0) { heap_put_t(c, a, HEAP_LOCKED_CHEST); add_spawn(key_item(IK_GOLDEN_KEY)); }
        else if (k == 1) {
          Item b = gen_item(rnd_int(2) ? GC_RING : GC_WAND); int g = 0;
          while (b.kind == a.kind && ++g < 50) { a = gen_item(rnd_int(2) ? GC_RING : GC_WAND); b = gen_item(rnd_int(2) ? GC_RING : GC_WAND); }   // do { i1, i2 } while same class
          heap_put_t(c, a, HEAP_CRYSTAL_CHEST);
          heap_put_t(c + N8[rnd_int(8)], b, HEAP_CRYSTAL_CHEST);
          add_spawn(key_item(IK_GOLDEN_KEY));
        } else heap_put(c, a);
        add_spawn(key_item(IK_IRON_KEY));
        break;
      }
      case RT_ALTAR: G.lvars[0] = 5 + G.depth * 5 + 1; break;   // SacrificialFire volume (+1: 0 means spent)
      case RT_SHOP: shop_content(ri); break;
      case RT_BLACKSMITH: {                            // BlacksmithPainter: two pieces of gear lying about, and the troll
        for (int i = 0; i < 2; i++) heap_put(room_cell(r, T_EMPTY_SP, false), gen_item(rnd_int(2) ? GC_WEAPON : GC_ARMOR));
        int c = -1;
        for (int g = 0; g < 200 && c < 0; g++) { int t = level_room_random(r, 1); if (heap_at(t) < 0 && !char_at(t)) c = t; }
        if (c >= 0) spawn_mob(MT_BLACKSMITH, c, ST_PASSIVE, 0);
        break;
      }
    }
  }
}
static int room_of(int type) { for (int r = 0; r < G.n_rooms; r++) if (G.rooms[r].type == type) return r; return -1; }
static void bones_drop(void) {                         // Bones.get: what a fallen hero left on this depth, cursed
  uint8_t b[1 + sizeof(Item)];
  if (persist_read_data(KEY_BONES, b, sizeof b) != (int)sizeof b || b[0] != G.depth) return;
  persist_delete(KEY_BONES);
  Item it; memcpy(&it, b + 1, sizeof it);
  if (!item_stackable(it.kind)) {
    it.flags |= IF_CURSED | IF_CURSED_KNOWN;
    if (item_upgradable(it.kind)) { int lvl = (G.depth - 1) * 3 / 5 + 1; if (lvl < it.lvl) it.lvl = lvl; it.flags &= ~IF_LEVEL_KNOWN; }
  }
  int c = -1, d = G.depth, e = room_of(RT_ENTRANCE);
  if (!boss_depth(d) && d != 21) c = random_drop_cell();
  else {                                               // boss floors and the last shop: the arrival room (Caves / Halls: below its top row,
    int l, t, r, bt, k = d == 15 ? 2 : 1;              // City: the chamber), not on the stairs or the sign
    if (d == 15 || d == 25) { l = MAP_W / 2 - k; r = MAP_W / 2 + k; t = MAP_H / 2 - k + 1; bt = MAP_H / 2 + k; }
    else if (d == 20) { l = 13; r = 17; t = 18; bt = 20; }   // CityBossLevel: LEFT + 1 .. LEFT + HALL_WIDTH - 2, TOP + HALL_HEIGHT + 1 .. + CHAMBER_HEIGHT
    else if (e >= 0) { const Room *m = &G.rooms[e]; l = m->left + 1; r = m->right - 1; t = m->top + 1; bt = m->bottom - 1; }
    else return;
    for (int g = 0; g < 100 && (c < 0 || c == G.entrance || G.map[c] == T_SIGN); g++) c = rnd_range(l, r) + rnd_range(t, bt) * MAP_W;
  }
  heap_put_t(c, it, HEAP_SKELETON);
}
static void sewer_boss_content(void) {                // SewerBossLevel.createMobs / RatKingPainter
  int ex = room_of(RT_BOSS_EXIT);
  if (ex >= 0) { int c; do c = level_room_random(&G.rooms[ex], 0); while (!PASSABLE(G.map[c])); spawn_mob(MT_GOO, c, ST_SLEEPING, 0); }   // SewerBossLevel.createMobs: roomExit.random()
  int rk = room_of(RT_RAT_KING);
  if (rk < 0) return;
  Room *r = &G.rooms[rk];
  int door = -1;                                       // the hidden door: the room's wall cell that isn't wall
  for (int y = r->top; y <= r->bottom; y++) for (int x = r->left; x <= r->right; x++)
    if ((x == r->left || x == r->right || y == r->top || y == r->bottom) && G.map[x + y * MAP_W] == T_SECRET_DOOR) door = x + y * MAP_W;
  int first = G.n_heaps;
  for (int y = r->top + 1; y < r->bottom; y++) for (int x = r->left + 1; x < r->right; x++) {
    if (x != r->left + 1 && x != r->right - 1 && y != r->top + 1 && y != r->bottom - 1) continue;   // around the walls
    int p = x + y * MAP_W;
    if (door >= 0 && (p == door - 1 || p == door + 1 || p == door - MAP_W || p == door + MAP_W)) continue;
    Item prize;
    switch (rnd_int(10)) {
      case 0: prize = gen_item(GC_WEAPON); if (item_cat(prize.kind) == CAT_MISSILE) prize.qty = 1; else { prize.lvl -= rnd_int(3); prize.dur = item_max_dur(&prize); } break;
      case 1: prize = gen_item(GC_ARMOR); prize.lvl -= rnd_int(3); prize.dur = item_max_dur(&prize); break;
      default: prize = item_make(IK_GOLD, rnd_range(1, 5)); break;
    }
    heap_put_t(p, prize, HEAP_CHEST);
  }
  if (G.n_heaps > first) G.heaps[first + rnd_int(G.n_heaps - first)].type = HEAP_MIMIC;
  int c; int guard = 0;
  do c = level_room_random(r, 1); while ((char_at(c) || heap_at(c) >= 0) && ++guard < 50);
  spawn_mob(MT_RAT_KING, c, ST_SLEEPING, 0);
}
static void prison_boss_content(void) {               // PrisonBossLevel.createItems: the arena's key in a chest in the anteroom
  int a = room_of(RT_TUNNEL);                          // the anteroom (level_gen: no RT_STANDARD room here)
  if (a < 0) return;
  int c, guard = 0;
  do c = level_room_random(&G.rooms[a], 0); while (!PASSABLE(G.map[c]) && ++guard < 200);
  Item k = item_make(IK_IRON_KEY, 1); k.lvl = G.depth;
  heap_put_t(c, k, HEAP_CHEST);
}
static Item quest_wand(const uint8_t *kinds) {         // Wandmaker.Quest.spawn: wand.random().upgrade()
  Item w = item_make(kinds[rnd_int(5)], 1);
  for (int n = 0; n < 2 && rnd_pct(n ? 15 : 50); n++) { w.lvl++; if (w.ench < wand_max(&w)) w.ench++; }
  w.lvl++; if (w.ench < wand_max(&w)) w.ench++;
  w.dur = item_max_dur(&w);
  return w;
}
static void wandmaker_spawn(void) {                    // Wandmaker.Quest.spawn, in the entrance room
  Quests *q = &G.q;
  if (q->wand != QS_NONE || G.depth <= 6 || rnd_int(10 - G.depth) != 0) return;
  int r = room_of(RT_ENTRANCE);
  if (r < 0) return;
  int c, guard = 0;
  do c = level_room_random(&G.rooms[r], 0); while ((G.map[c] == T_ENTRANCE || G.map[c] == T_SIGN || char_at(c)) && ++guard < 200);
  if (spawn_mob(MT_WANDMAKER, c, ST_PASSIVE, 0) < 0) return;
  q->wand = QS_SPAWNED; q->wand_depth = G.depth;
  q->wand_alt = rnd_int(3);
  if (q->wand_alt == 2) {                              // the fish only where water is scarce
    int water = 0;
    for (int i = 0; i < MAP_LEN; i++) if (IS_WATER(G.map[i]) && ++water > MAP_LEN / 16) { q->wand_alt = rnd_int(2); break; }
  }
  static const uint8_t W1[5] = { IK_WND_AVALANCHE, IK_WND_DISINTEGRATION, IK_WND_FIREBOLT, IK_WND_LIGHTNING, IK_WND_POISON };
  static const uint8_t W2[5] = { IK_WND_AMOK, IK_WND_BLINK, IK_WND_REGROWTH, IK_WND_SLOW, IK_WND_REACH };
  q->wand1 = quest_wand(W1);
  q->wand2 = quest_wand(W2);
}
static void imp_spawn(void) {                          // Imp.Quest.spawn
  Quests *q = &G.q;
  if (q->imp != QS_NONE || G.depth <= 16 || rnd_int(20 - G.depth) != 0) return;
  int c = -1;
  for (int g = 0; g < 200 && c < 0; g++) { int t = random_respawn_cell(); if (t >= 0 && heap_at(t) < 0) c = t; }
  if (c < 0 || spawn_mob(MT_IMP, c, ST_PASSIVE, 0) < 0) return;
  q->imp = QS_SPAWNED; q->imp_depth = G.depth;
  q->imp_alt = rnd_int(2) == 0;
  Item r; int g = 0;
  do r = gen_item(GC_RING); while ((r.flags & IF_CURSED) && ++g < 50);
  r.lvl += 2; r.flags |= IF_CURSED; r.dur = item_max_dur(&r);   // reward.upgrade(2); reward.cursed = true
  q->imp_ring = r;
}
void level_content(const int16_t *tombs, int n_tombs, uint32_t seed) {
  g_rng = seed;
  if (boss_depth(G.depth)) {                           // boss floors: their own monsters, nothing else to find
    gen_hold(true); gen_reset();
    G.n_mobs = 0; G.n_heaps = 0;
    if (G.depth == 5) sewer_boss_content();
    else if (G.depth == 10) prison_boss_content();
    // depth 15 (CavesBossLevel): nothing until the hero leaves the first room
    bones_drop();                                       // <Boss>Level.createItems: Bones.get
    gen_hold(false);
    return;
  }
  gen_hold(true);
  gen_reset();                                          // Generator.reset
  if (G.depth == 26) {                                  // LastLevel.createItems: the Amulet on its pedestal
    G.n_mobs = 0; G.n_heaps = 0; heap_put(4 * (MAP_W + 1), item_make(IK_AMULET, 1)); gen_hold(false); return;
  }
  // Level.create: items that must spawn on this floor (LastShopLevel counts them too, but never drops them)
  s_nspawn = 0;
  if (region_of(G.depth) == 4) add_spawn(item_make(IK_TORCH, 1));   // HallsLevel.create
  add_spawn(gen_item(GC_FOOD));
  gen_reset();
  static const uint8_t POS_Q[10] = { 4, 2, 9, 4, 14, 6, 19, 8, 24, 9 }, SOU_Q[10] = { 5, 3, 10, 6, 15, 9, 20, 12, 25, 13 };
  // 0.33: kinder than PD: the last shop (21) rolls as PD does but doesn't use up a strength potion / upgrade / enchantment
  // scroll it would never drop (PD's quirk); they come on a later floor
  if (quota_needed(POS_Q, G.pos_count) && G.depth != 21) { add_spawn(item_make(IK_POT_STRENGTH, 1)); G.pos_count++; }
  if (quota_needed(SOU_Q, G.sou_count) && G.depth != 21) { add_spawn(item_make(IK_SCR_UPGRADE, 1)); G.sou_count++; }
  if (rnd_int(12 * (1 + G.soe_count)) < G.depth && G.depth != 21) { add_spawn(item_make(IK_SCR_ENCHANT, 1)); G.soe_count++; }   // soeNeeded
  special_content();                                    // the painters' prizes (they may take from the list above)
  for (int i = 0; i < n_tombs; i++)                    // StandardPainter.paintGraveyard: one prize, the rest a coin each
    heap_put_t(tombs[i] & 0x3FF, (tombs[i] & 0x4000) ? gen_item(-1) : item_make(IK_GOLD, 1), HEAP_TOMB);
  if (G.depth == 21) { bones_drop(); gen_hold(false); return; }   // LastShopLevel: no creatures, no loot: only the imp's shop (if any)
  if (G.depth <= 3 && !(G.dflags & DF_VIAL_SPAWNED) && rnd_int(4 - G.depth) == 0) {   // SewerLevel.createItems: the dew vial
    add_spawn(item_make(IK_DEW_VIAL, 1)); G.dflags |= DF_VIAL_SPAWNED;
  }
  // createMobs
  memset(G.fov, 0, sizeof G.fov);
  int n = 2 + G.depth % 5 + rnd_int(3);
  for (int i = 0; i < n; i++) {
    int c = -1;
    for (int t = 0; t < 100 && c < 0; t++) c = random_respawn_cell();
    if (c >= 0) spawn_mob(bestiary_mob(G.depth), c, ST_SLEEPING, 0);
  }
  if (region_of(G.depth) == 1) wandmaker_spawn();      // PrisonLevel.createMobs
  if (G.depth >= 2 && G.depth <= 4 && G.q.ghost == QS_NONE && rnd_int(5 - G.depth) == 0) {   // Ghost.Quest.spawn
    int c = -1;
    for (int t = 0; t < 100 && c < 0; t++) c = random_respawn_cell();
    if (c >= 0) {
      spawn_mob(MT_GHOST, c, ST_WANDERING, 0);
      Quests *q = &G.q;
      q->ghost = QS_SPAWNED; q->ghost_depth = G.depth; q->ghost_alt = rnd_int(3); q->ghost_left = 8;
      Item w = gen_item(GC_MELEE);
      for (int i = 1; i < 4; i++) { Item o = gen_item(GC_MELEE); if (o.lvl > w.lvl) w = o; }
      Item a = gen_item(GC_ARMOR);
      for (int i = 0; i < 3; i++) { Item o = gen_item(GC_ARMOR); if (o.lvl > a.lvl) a = o; }
      w.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; a.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;   // identify()
      q->ghost_weapon = w; q->ghost_armor = a;
    }
  }
  // createItems: heaps, chests, skeleton remains (mimics later: chests for now)
  int ni = 3;
  while (rnd_pct(40)) ni++;
  for (int i = 0; i < ni; i++) {
    int r = rnd_int(20), type = r == 0 ? HEAP_SKELETON : r < 5 || (r == 5 && G.depth == 1) ? HEAP_CHEST : r == 5 ? HEAP_MIMIC : HEAP_PLAIN;
    heap_put_t(random_drop_cell(), gen_item(-1), type);
  }
  for (int i = 0; i < s_nspawn; i++) {
    int c = random_drop_cell();                         // a scroll of upgrade never on a fire trap
    for (int g = 0; g < 100 && s_spawn[i].kind == IK_SCR_UPGRADE && (G.map[c] == T_FIRE_TRAP || G.map[c] == T_SECRET_FIRE_TRAP); g++) c = random_drop_cell();
    heap_put(c, s_spawn[i]);
  }
  bones_drop();
  if (region_of(G.depth) == 3) imp_spawn();             // CityLevel.createItems: after the loot (the imp never stands on a heap)
#ifdef TEST_BUILD
  if (G.depth == 2) kit_consumables();                  // the test kit: depth 2 holds the consumables
#endif
  gen_hold(false);
}
OVL_ENTRY(content, (void *)level_content);
