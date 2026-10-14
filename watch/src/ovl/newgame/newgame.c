/*
 * Pixel Dungeon for Pebble - a new game (an overlay, 0.14: out of the "floor" overlay, which must fit in memory
 * at every save): Dungeon.init, HeroClass.initHero, the item flavours, the first floor.
 */
#include "pd.h"
#include "game_int.h"
#include "ovl.h"

// ItemStatusHandler for wands (woods) and rings (gems)
static void shuffle_flavors(uint8_t *wands, uint8_t *rings) {
  for (int k = 0; k < 2; k++) {
    uint8_t *f = k ? rings : wands;
    for (int i = 0; i < 12; i++) f[i] = i;
    for (int i = 11; i > 0; i--) { int j = rnd_int(i + 1); uint8_t t = f[i]; f[i] = f[j]; f[j] = t; }
  }
}
#ifdef TEST_BUILD
#include "testkit.h"                               // the test kit's floors (0.34: gear here, consumables on depth 2)
static void test_floor(void) {
  Hero *h = &G.hero;
  h->lvl = 30; h->HT += 29 * 5; h->HP = h->HT; h->attack_skill += 29; h->defense_skill += 29; h->STR = 25;
  for (int k = 1; k < IK_COUNT; k++) set_known(k);
  kit_gear();
  G.dew = 10;
  bag_add(item_make(IK_SEED_POUCH, 1));            // a pouch of seeds to try the pot with (depth 2)
  for (int k = IK_FIRST_SEED; k < IK_FIRST_SEED + 3; k++) bag_add(item_make(k, 3));
}
#endif
static void f_new(void) {
  s_pend_depth = 0;
  blob_free_all();
  memset(&G, 0, sizeof G);
  floor_op(FO_ERASE, 0, NULL);                    // no floors kept from an older game
  G.seed = (uint32_t)time(NULL) ^ ((uint32_t)time_ms(NULL, NULL) << 16);
  // HeroClass.initHero: initCommon, then the class's own things (everything starts identified)
  int cls = g_new_class & 3;
  Hero *h = &G.hero;
  *h = (Hero){ .HP = 20, .HT = 20, .lvl = 1, .attack_skill = 10, .defense_skill = 5, .STR = 10, .hclass = cls };
  h->armor = item_make(IK_CLOTH_ARMOR, 1);
  h->bag[0] = item_make(IK_RATION, 1);
  static const uint8_t WEAPON[4] = { IK_SHORT_SWORD, IK_KNUCKLES, IK_DAGGER, IK_DAGGER };
  h->weapon = item_make(WEAPON[cls], 1);
  switch (cls) {
    case 0: h->STR++; h->bag[1] = item_make(IK_DART, 8); h->quick[0] = IK_DART; break;      // initWarrior
    case 1: h->bag[1] = item_make(IK_WND_MM, 1); h->bag[1].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN | IF_CHARGE_KNOWN;   // initMage
            h->quick[0] = IK_WND_MM; break;
    case 2: h->ring[0] = item_make(IK_RNG_SHADOWS, 1); h->ring[0].lvl = 1;               // initRogue
            h->ring[0].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; h->ring[0].dur = item_max_dur(&h->ring[0]);
            h->bag[1] = item_make(IK_DART, 8); h->quick[0] = IK_DART; break;
    case 3: h->HP = h->HT = 15;                                                            // initHuntress
            h->bag[1] = item_make(IK_BOOMERANG, 1); h->bag[1].flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN; h->quick[0] = IK_BOOMERANG; break;
  }
  h->weapon.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
  h->armor.flags |= IF_LEVEL_KNOWN | IF_CURSED_KNOWN;
  // ItemStatusHandler: shuffle PD's 12 colors / runes and hand them to the item types
  g_rng = G.seed ^ 0x9E3779B9u;
  uint8_t pool[12];
  for (int i = 0; i < 12; i++) pool[i] = i;
  for (int i = 11; i > 0; i--) { int j = rnd_int(i + 1); uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
  for (int i = 0; i < N_POTIONS; i++) G.potion_flavor[i] = pool[i];
  for (int i = 11; i > 0; i--) { int j = rnd_int(i + 1); uint8_t t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
  for (int i = 0; i < N_SCROLLS; i++) G.scroll_flavor[i] = pool[i];
  shuffle_flavors(G.wand_flavor, G.ring_flavor);
  G.regen_left = 10 * TICK;
  static const uint8_t KNOWN[4] = { IK_POT_STRENGTH, IK_SCR_IDENTIFY, IK_SCR_MAPPING, IK_NONE };   // the class's known potion / scroll
  if (KNOWN[cls]) set_known(KNOWN[cls]);
  set_known(IK_WND_MM); if (cls == 2) set_known(IK_RNG_SHADOWS);
  G.now = 0; G.turn_due = 0; G.level_t0 = -TICK; g_ovr.c = -1;
  G.max_depth = 1;
  floor_op(FO_BUILD, 1, (void *)1);
  rnd_reseed_runtime();
#ifdef TEST_BUILD                                 // test kit (0.17): a level-30 hero and a first floor laid out with one of everything
  test_floor();
#endif
  G.hero.pos = G.entrance;
  G.last_target = -1; G.quick_uid = -1;
  update_fov();
  respawn();
  check_visible_mobs(); G.interrupted = false;
  ui_log(txt(TX_WELCOME_TO_THE_LEVEL_2), C_INFO);
  { time_t t = time(NULL); G.night = localtime(&t)->tm_hour < 7; if (G.night) ui_log(txt(TX_L_NIGHT), C_WARN); }   // Dungeon.switchLevel: nightMode (a load sets it too)
  ui_message(NULL, txt(TX_STORY_0));               // WndStory.showChapter(ID_SEWERS)
  if (!persist_exists(205)) {                      // IntroScene, before the very first game (PixelDungeon.intro)
    persist_write_bool(205, true);
    extern char *s_text_buf;
    ui_message(NULL, txt(TX_INTRO)); int n = strlen(s_text_buf); snprintf(s_text_buf + n, 720 - n, "\n\n%s", txt(TX_STORY_0));
  }
}

OVL_ENTRY(newgame, (void *)f_new);
