/*
 * Pixel Dungeon for Pebble - descriptions (an overlay): the item window's text and the inspect tool.
 * Ported from Pixel Dungeon 1.9.1 (GPLv3): Item.info of MeleeWeapon, MissileWeapon, Armor, Potion, Scroll,
 * Gold; Terrain names and descriptions (Level.tileName / tileDesc); WndInfoCell, WndInfoMob, Blob.tileDesc.
 */
#include "pd.h"
#include "items_int.h"
#include "game_int.h"
#include "ovl.h"

static const char *tile_name(int t) {                  // Level.tileName and each region's own (SewerLevel ... HallsLevel)
  int rg = region_of(G.depth);
  static const char *const WATER[5] = { "Murky water", "Dark cold water.", "Freezing cold water.", "Suspiciously colored water", "Cold lava" };
  if (t >= T_WATER_TILES) return WATER[rg];
  if (rg == 2 && t == T_GRASS) return "Fluorescent moss";
  if (rg == 2 && t == T_HIGH_GRASS) return "Fluorescent mushrooms";
  if (rg == 3 && t == T_HIGH_GRASS) return "High blooming flowers";
  if (rg == 4 && t == T_GRASS) return "Embermoss";
  if (rg == 4 && t == T_HIGH_GRASS) return "Emberfungi";
  if (rg == 4 && (t == T_STATUE || t == T_STATUE_SP)) return "Pillar";
  if (t != T_CHASM && (TFLAGS(t) & F_PIT)) return "Chasm";
  switch (t) {
    case T_CHASM: return "Chasm";
    case T_EMPTY: case T_EMPTY_SP: case T_EMPTY_DECO: case T_SECRET_TOXIC_TRAP: case T_SECRET_FIRE_TRAP:
    case T_SECRET_PARALYTIC_TRAP: case T_SECRET_POISON_TRAP: case T_SECRET_ALARM_TRAP: case T_SECRET_LIGHTNING_TRAP:
    case T_SECRET_GRIPPING_TRAP: case T_SECRET_SUMMONING_TRAP: return "Floor";
    case T_GRASS: return "Grass";
    case T_WALL: case T_WALL_DECO: case T_SECRET_DOOR: return "Wall";
    case T_DOOR: return "Closed door";
    case T_OPEN_DOOR: return "Open door";
    case T_ENTRANCE: return "Depth entrance";
    case T_EXIT: return "Depth exit";
    case T_EMBERS: return "Embers";
    case T_LOCKED_DOOR: return "Locked door";
    case T_PEDESTAL: return "Pedestal";
    case T_BARRICADE: return "Barricade";
    case T_HIGH_GRASS: return "High grass";
    case T_LOCKED_EXIT: return "Locked depth exit";
    case T_UNLOCKED_EXIT: return "Unlocked depth exit";
    case T_SIGN: return "Sign";
    case T_WELL: return "Well";
    case T_EMPTY_WELL: return "Empty well";
    case T_STATUE: case T_STATUE_SP: return "Statue";
    case T_TOXIC_TRAP: return "Toxic gas trap";
    case T_FIRE_TRAP: return "Fire trap";
    case T_PARALYTIC_TRAP: return "Paralytic gas trap";
    case T_POISON_TRAP: return "Poison dart trap";
    case T_ALARM_TRAP: return "Alarm trap";
    case T_LIGHTNING_TRAP: return "Lightning trap";
    case T_GRIPPING_TRAP: return "Gripping trap";
    case T_SUMMONING_TRAP: return "Summoning trap";
    case T_INACTIVE_TRAP: return "Triggered trap";
    case T_BOOKSHELF: return "Bookshelf";
    case T_ALCHEMY: return "Alchemy pot";
    default: return "???";
  }
}
static const char *tile_desc(int t) {                  // Level.tileDesc and the regions' own
  int rg = region_of(G.depth);
  if (t == T_BOOKSHELF) return txt(TX_I_SHELF_0 + rg);
  if (t == T_EMPTY_DECO) return rg == 0 ? txt(TX_I_T0) : rg == 1 ? txt(TX_I_T1) : rg == 3 ? txt(TX_I_T2) : "";
  if (rg == 2) switch (t) {
    case T_ENTRANCE: return txt(TX_I_T3);
    case T_EXIT: return txt(TX_I_T4);
    case T_HIGH_GRASS: return txt(TX_I_T5);
    case T_WALL_DECO: return txt(TX_I_T6);
  }
  if (rg == 3) switch (t) {
    case T_ENTRANCE: return txt(TX_I_T7);
    case T_EXIT: return G.depth == 21 ? "A ramp leads down to the Inferno." : txt(TX_I_T8);   // LastShopLevel
    case T_WALL_DECO: return txt(TX_I_T2);
    case T_EMPTY_SP: return txt(TX_I_T9);
    case T_STATUE: case T_STATUE_SP: return txt(TX_I_T10);
  }
  if (rg == 4 && t >= T_WATER_TILES) return txt(TX_I_T11);
  if (rg == 4 && (t == T_STATUE || t == T_STATUE_SP)) return txt(TX_I_T12);
  switch (t) {
    case T_CHASM: return "You can't see the bottom.";
    case T_ENTRANCE: return txt(TX_I_T13);
    case T_EXIT: case T_UNLOCKED_EXIT: return txt(TX_I_T14);
    case T_EMBERS: return "Embers cover the floor.";
    case T_HIGH_GRASS: return txt(TX_I_T15);
    case T_LOCKED_DOOR: return txt(TX_THIS_DOOR_IS_LOCKED);
    case T_LOCKED_EXIT: return txt(TX_HEAVY_BARS_BLOCK_THE);
    case T_BARRICADE: return txt(TX_THE_WOODEN_BARRICADE_IS);
    case T_SIGN: return txt(TX_I_T16);
    case T_TOXIC_TRAP: case T_FIRE_TRAP: case T_PARALYTIC_TRAP: case T_POISON_TRAP:
    case T_ALARM_TRAP: case T_LIGHTNING_TRAP: case T_GRIPPING_TRAP: case T_SUMMONING_TRAP:
      return txt(TX_STEPPING_ONTO_A_HIDDEN);
    case T_INACTIVE_TRAP: return txt(TX_THE_TRAP_HAS_BEEN);
    case T_STATUE: case T_STATUE_SP: return txt(TX_SOMEONE_WANTED_TO_ADORN);
    case T_ALCHEMY: return txt(TX_I_T17);
    case T_EMPTY_WELL: return "The well has run dry.";
    default:
      if (t >= T_WATER_TILES) return txt(TX_IN_CASE_OF_BURNING);
      if (TFLAGS(t) & F_PIT) return "You can't see the bottom.";
      return "";
  }
}
#define ADD(...) do { int _n = snprintf(buf + n, len > n ? len - n : 0, __VA_ARGS__); if (_n > 0) n += _n; } while (0)
static bool equipped(const Item *it) { return it == &G.hero.weapon || it == &G.hero.armor || it == &G.hero.ring[0] || it == &G.hero.ring[1]; }
// Belongings.backpack.items.contains: in the pack (also the copy a sell window describes), not on the floor or worn
static bool in_pack(const Item *it) { return (it >= G.hero.bag && it < G.hero.bag + BAG_CAP) || (it == &g_wnd_item && g_trade == 2 && g_trade_arg >= 0); }   // (a worn one: not in the backpack)
// PD's info() texts: MeleeWeapon, MissileWeapon, Armor, Potion, Scroll, Gold
static void info_item(const Item *it, char *buf, int len) {
  int n = 0, k = it->kind;
  const ItemDef *d = &DEFS[k];
  bool lk = it->flags & IF_LEVEL_KNOWN;
  int hstr = hero_str();
  if (d->cat == CAT_POTION && !item_known(k)) {
    ADD(txt(TX_THIS_FLASK_CONTAINS_A_2), POTION_COLORS[G.potion_flavor[k - IK_FIRST_POTION]]);
    return;
  }
  if (d->cat == CAT_SCROLL && !item_known(k)) {
    ADD(txt(TX_THIS_PARCHMENT_IS_COVERED_2), SCROLL_RUNES[G.scroll_flavor[k - IK_FIRST_SCROLL]]);
    return;
  }
  if (d->cat == CAT_WAND) {                            // Wand.info
    if (item_known(k)) ADD("%s", txt(d->desc)); else ADD(txt(TX_WND_WOOD), WAND_WOODS[G.wand_flavor[k - IK_FIRST_WAND]]);
    if ((G.hero.hclass & 3) == 1) { if (lk) ADD(txt(TX_I_WAND_MELEE), weapon_min(it) + (weapon_max(it) - weapon_min(it)) / 2); else ADD("%s", txt(TX_I_WAND_WEAPON)); }   // a mage's
    return;
  }
  if (d->cat == CAT_RING) {                            // Ring.info / desc
    char nm[40]; short_name(it, nm, sizeof nm);           // Ring.name()
    if (item_known(k)) ADD("%s", txt(d->desc)); else ADD(txt(TX_RNG_GEM), RING_GEMS[G.ring_flavor[k - IK_FIRST_RING]]);
    if (equipped(it)) ADD("\n\nThe %s is on your finger%s", nm, (it->flags & IF_CURSED) ? txt(TX_I_T18) : ".");
    else if ((it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CURSED)) ADD("\n\nYou can feel a malevolent magic lurking within the %s.", nm);
    return;
  }
  if (k == IK_GOLD) { if (it->qty == 1) ADD("One gold coin. "); else ADD("A pile of %d gold coins. ", it->qty); ADD(txt(TX_COLLECT_GOLD_COINS_TO)); return; }
  ADD("%s", txt(d->desc));
  if (k == IK_BEACON && it->lvl) ADD(txt(TX_I_BEACON_SET), it->lvl);   // LloydsBeacon.info
  if (d->cat == CAT_WEAPON && k != IK_PICKAXE) {         // (Pickaxe.info: just its words)
    int lvl = lk ? it->lvl : 0;
    const char *q = lvl > 0 ? (item_broken(it) ? "a broken" : "an upgraded") : lvl < 0 ? "a degraded" : "a";
    ADD(txt(TX_THIS_IS_TIER_MELEE), txt(d->name), q, d->tier);
    if (lk) { int mn = weapon_min(it), mx = weapon_max(it); ADD(txt(TX_ITS_AVERAGE_DAMAGE_IS), mn + (mx - mn) / 2); }
    else {
      int mn = d->tier, mx = melee_max0(k);
      ADD(txt(TX_ITS_TYPICAL_AVERAGE_DAMAGE), mn + (mx - mn) / 2, d->str);
      if (d->str > hstr) ADD(txt(TX_PROBABLY_THIS_WEAPON_IS));
    }
    if (d->dly != 100) {
      ADD("This is a rather %s", d->dly < 100 ? "fast" : "slow");
      if (d->acu != 100) ADD("%s%s", (d->acu > 100) == (d->dly < 100) ? " and " : " but ", d->acu > 100 ? "accurate" : "inaccurate");
      ADD(" weapon. ");
    } else if (d->acu != 100) ADD("This is a rather %s weapon. ", d->acu > 100 ? "accurate" : "inaccurate");
    if (it->flags & IF_FAST) ADD(txt(TX_I_T19));             // Weapon.Imbue
    else if (it->flags & IF_ACCURATE) ADD(txt(TX_I_T20));
    if (it->ench) ADD("It is enchanted.");
    if (lk && in_pack(it)) {
      if (item_str(it) > hstr) ADD(txt(TX_BECAUSE_OF_YOUR_INADEQUATE), txt(d->name));
      if (item_str(it) < hstr) ADD(txt(TX_BECAUSE_OF_YOUR_EXCESS), txt(d->name));
    }
    if (equipped(it)) ADD("\n\nYou hold the %s at the ready%s", txt(d->name), (it->flags & IF_CURSED) ? txt(TX_AND_BECAUSE_IT_IS) : ".");
    else if ((it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CURSED)) ADD(txt(TX_YOU_CAN_FEEL_A), txt(d->name));
  } else if (d->cat == CAT_MISSILE) {
    int mn = weapon_min(it);                            // MissileWeapon.info: min() / max() (the boomerang's grow with its level)
    ADD(txt(TX_AVERAGE_DAMAGE_OF_THIS), mn + (weapon_max(it) - mn) / 2);
    if (in_pack(it)) {
      if (d->str > hstr) ADD(txt(TX_BECAUSE_OF_YOUR_INADEQUATE_2), txt(d->name));
      if (d->str < hstr) ADD(txt(TX_BECAUSE_OF_YOUR_EXCESS_2), txt(d->name));
    }
    if (equipped(it)) ADD("\n\nYou hold the %s at the ready.", txt(d->name));
  } else if (d->cat == CAT_ARMOR) {
    char an[40]; short_name(it, an, sizeof an);         // Armor.info: name(), with its glyph
    if (lk) {
      ADD(txt(TX_THIS_PROVIDES_DAMAGE_ABSORPTION), an, armor_dr(it));
      if (item_str(it) > hstr) {
        if (equipped(it)) ADD(txt(TX_BECAUSE_OF_YOUR_INADEQUATE_3));
        else ADD(txt(TX_BECAUSE_OF_YOUR_INADEQUATE_4));
      }
    } else {
      ADD(txt(TX_TYPICAL_PROVIDES_DAMAGE_ABSORPTION), an, d->tier * 2, d->str);
      if (d->str > hstr) ADD(txt(TX_PROBABLY_THIS_ARMOR_IS));
    }
    if (it->ench) ADD("It is enchanted.");
    if (equipped(it)) ADD("\n\nYou are wearing the %s%s", an, (it->flags & IF_CURSED) ? txt(TX_AND_BECAUSE_IT_IS_2) : ".");
    else if ((it->flags & IF_CURSED_KNOWN) && (it->flags & IF_CURSED)) ADD(txt(TX_YOU_CAN_FEEL_A_2), an);
  }
}

static void info_inspect(int16_t c, char *title, int tlen, char *desc, int dlen) {
  desc[0] = 0;
  if (!BIT_GET(G.visited, c)) { snprintf(title, tlen, "Unexplored"); snprintf(desc, dlen, "%s", txt(TX_I_T21)); return; }
  int m = mob_at(c);
  if (m >= 0 && BIT_GET(G.fov, c)) {
    Mob *mb = &G.mobs[m];
    const MobDef *d = &MOB_DEFS[mb->type];
    snprintf(title, tlen, "%s", mob_name(mb->type)); cap_first(title);
    static const char *ST[5] = { "sleeping", "wandering", "hunting", "fleeing", "passive" };
    const char *nm = mob_name(mb->type), *ds = txt(d->desc);
    char sd[200];
    int ci = -1; for (int h = 0; h < G.n_heaps; h++) if (G.heaps[h].type == HEAP_CARRIED && G.heaps[h].pos == CARRIED_POS(mb->uid)) ci = h;
    char cn[40] = ""; if (ci >= 0) short_name(&G.heaps[ci].item, cn, sizeof cn);   // weapon.name() / item.name()
    if (mb->type == MT_WRAITH && mb->aux2 == 1) { nm = "curse personification"; ds = txt(TX_I_CURSE_DESC); snprintf(title, tlen, "Curse personification"); }   // Ghost's CursePersonification
    if (mb->type == MT_STATUE && ci >= 0) {            // Statue.description: the weapon it wields
      const char *u = ds; while (*u && !(u[0] == '_' && u[1] == '_')) u++;
      if (*u) { snprintf(sd, sizeof sd, "%.*s%s%s", (int)(u - ds), ds, cn, u + 2); ds = sd; }
    }
    int n = snprintf(desc, dlen, "%s", ds);
    if ((mb->type == MT_THIEF || mb->type == MT_BANDIT) && ci >= 0 && n < dlen) {   // Thief.description: what it stole
      char who[24]; snprintf(who, sizeof who, "%s", nm); cap_first(who);
      n += snprintf(desc + n, dlen - n, txt(TX_I_CARRIES), who, cn);
    }
    if (n < dlen) snprintf(desc + n, dlen - n, "\n\nThis %s is %s.", nm, ST[mb->state < 5 ? mb->state : 1]);   // WndInfoMob: state.status(), allies too

    return;
  }
  if (!BIT_GET(G.fov, c)) goto cell;                  // Toolbar.informer: a cell out of view shows its terrain only
  if (c == G.hero.pos) { snprintf(title, tlen, "You"); static const char *const C[4] = { "warrior", "mage", "rogue", "huntress" }; snprintf(desc, dlen, "This is you, a %s of the dungeon.", C[G.hero.hclass & 3]); return; }
  int hi = heap_at(c);
  if (hi >= 0) {
    Heap *h = &G.heaps[hi];
    if (h->type == HEAP_CHEST || h->type == HEAP_MIMIC) { snprintf(title, tlen, "Chest"); snprintf(desc, dlen, "%s", txt(TX_YOU_WON_T_KNOW)); return; }   // WndInfoItem
    if (h->type == HEAP_TOMB) { snprintf(title, tlen, "Tomb"); snprintf(desc, dlen, "%s", txt(TX_I_TOMB_OWNER)); return; }
    if (h->type == HEAP_LOCKED_CHEST) { snprintf(title, tlen, "Locked chest"); snprintf(desc, dlen, "%s", txt(TX_I_NEED_KEY)); return; }
    if (h->type == HEAP_CRYSTAL_CHEST) { char n[44]; short_name(&h->item, n + 3, sizeof n - 3); n[0] = 'a'; n[1] = VOWEL(n[3]) ? 'n' : ' '; n[2] = VOWEL(n[3]) ? ' ' : 0;
      if (!VOWEL(n[3])) memmove(n + 2, n + 3, strlen(n + 3) + 1);   // Utils.indefinite
      snprintf(title, tlen, "Crystal chest"); snprintf(desc, dlen, txt(TX_I_INSIDE), n); return; }
    if (h->type == HEAP_SKELETON) { snprintf(title, tlen, "Skeletal remains"); snprintf(desc, dlen, "%s", txt(TX_THIS_IS_ALL_THAT)); return; }
    item_name(&h->item, title, tlen);                  // Item.toString: " x3" for a stack (gold too)
    if (h->item.qty > 1) {                             // " x7" before a missile's " :STR" (Weapon.toString wraps Item.toString)
      char *p = title, tail[8];
      while (*p && !(p[0] == ' ' && p[1] == ':')) p++;
      snprintf(tail, sizeof tail, "%s", p); *p = 0;
      int tn = strlen(title); snprintf(title + tn, tlen - tn, " x%d%s", h->item.qty, tail);
    }
    info_item(&h->item, desc, dlen);
     return;
  }
  for (int i = 0; i < G.n_plants; i++) if (G.plants[i].pos == c) {   // WndInfoPlant (0.17)
    snprintf(title, tlen, "%s", txt(TX_PL_FIREBLOOM + 4 * (G.plants[i].type & 7)));
    snprintf(desc, dlen, "%s", txt(TX_PD_FIREBLOOM + 4 * (G.plants[i].type & 7)));
    return;
  }
cell:;
  int t = G.map[c], n;                               // WndInfoCell: the tile, then what lies on it
  snprintf(title, tlen, "%s", tile_name(t));
  n = snprintf(desc, dlen, "%s", tile_desc(t));
  #define MORE(str) do { if (n < dlen) n += snprintf(desc + n, dlen - n, "%s%s", n ? "\n" : "", (str)); } while (0)
  if (c == G.well_pos && t == T_WELL) MORE(G.well_kind == 1 ? txt(TX_I_T22)
    : G.well_kind == 0 ? txt(TX_I_T23)
    : txt(TX_I_T24));   // WellWater.tileDesc
  if (c == G.altar_pos && G.lvars[0] > 1) MORE(txt(TX_I_T25));
  for (int r = 0; r < G.n_rooms; r++) { Room *R = &G.rooms[r];   // Foliage.tileDesc
    if (R->type == RT_GARDEN && c % MAP_W > R->left && c % MAP_W < R->right && c / MAP_W > R->top && c / MAP_W < R->bottom) { MORE(txt(TX_I_T26)); break; } }
  for (int b = 0; b < BLOB_COUNT; b++) if (b != BLOB_REGROWTH && g_blob[b] && g_blob[b][c])   // Blob.tileDesc
    MORE(b == BLOB_FIRE ? "A fire is raging here." : b == BLOB_CONFUSION ? txt(TX_I_T27) : b == BLOB_WEB ? txt(TX_I_T28)
         : txt(b == BLOB_TOXIC ? TX_A_GREENISH_CLOUD_OF : TX_A_CLOUD_OF_PARALYTIC));
  if (!n) snprintf(desc, dlen, "There is nothing here.");
}

OVL_ENTRY(info, (void *)info_inspect, (void *)info_item);
