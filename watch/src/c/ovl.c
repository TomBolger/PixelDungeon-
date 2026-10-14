/*
 * Pixel Dungeon for Pebble - overlay loader (see ovl.h) and the calls into overlays.
 */
#include "pd.h"
#include "ovl.h"
#include "game_int.h"

#ifndef OVL_HOST                                       // the host tests link overlays in (hosttest/host_ovl.c)
static void relocate(ResHandle h, int off, int n, uint8_t *img, int img_size, uintptr_t delta) {
  uint16_t chunk[32];
  for (int i = 0; i < n; i += 32) {
    int k = n - i < 32 ? n - i : 32;
    resource_load_byte_range(h, off + 2 * i, (uint8_t *)chunk, 2 * k);
    for (int j = 0; j < k; j++) {
      if (chunk[j] + 4 > img_size) continue;
      uint32_t w; memcpy(&w, img + chunk[j], 4); w += delta; memcpy(img + chunk[j], &w, 4);
    }
  }
}

int g_ovl_need, g_ovl_big;                               // the last overlay that found no room: its size, the largest free block
// 0.20: the overlay arena. Overlays that come and go (actions, windows, the save) are stacked in one block taken at
// start-up, so they never leave holes between the pictures; the resident ones (creatures, sound) use the heap.
// One that doesn't fit falls back to the heap.
#define ARENA_SIZE 8192
#define ARENA_N 6
static uint8_t *s_arena;
static uint16_t s_a_off[ARENA_N], s_a_top; static uint8_t s_a_n, s_a_dead;   // live blocks' offsets; dead: a bit per freed block
void ovl_arena_init(void) { if (!s_arena) s_arena = malloc(ARENA_SIZE); }
int ovl_arena_free(void) { return s_arena ? ARENA_SIZE - s_a_top : 0; }
static uint8_t *arena_alloc(int n) {
  n = (n + 7) & ~7;
  if (!s_arena || s_a_n == ARENA_N || s_a_top + n > ARENA_SIZE) return NULL;
  s_a_off[s_a_n] = s_a_top; s_a_dead &= ~(1 << s_a_n); s_a_n++; s_a_top += n;
  return s_arena + s_a_top - n;
}
static bool arena_free(const void *p) {
  if (!s_arena || (const uint8_t *)p < s_arena || (const uint8_t *)p >= s_arena + ARENA_SIZE) return false;
  for (int i = 0; i < s_a_n; i++) if (s_arena + s_a_off[i] == p) s_a_dead |= 1 << i;
  while (s_a_n && (s_a_dead >> (s_a_n - 1) & 1)) s_a_top = s_a_off[--s_a_n];   // pop what's freed from the top
  return true;
}
static const OvlHeader *ovl_load_in(uint32_t res_id, bool keep);
const OvlHeader *ovl_load(uint32_t res_id) { return ovl_load_in(res_id, false); }
const OvlHeader *ovl_keep(uint32_t res_id) { return ovl_load_in(res_id, true); }
uint32_t g_idle_at;                                // when the app last let the system run (a frame ended, or a pause here)
uint32_t now_ms(void);
static const OvlHeader *ovl_load_in(uint32_t res_id, bool keep) {
  // 0.36: long work loads overlay after overlay (a floor being built: ~180 ms): once 30 ms have gone by without a break,
  // a 2 ms pause first. The speaker is refilled by a system task below the app, which runs only while the app waits.
  if (now_ms() - g_idle_at > 30) { psleep(2); g_idle_at = now_ms(); }
  ResHandle h = resource_get_handle(res_id);
  OvlFileHeader fh;
  if (!h || resource_load_byte_range(h, 0, (uint8_t *)&fh, sizeof fh) != sizeof fh || fh.magic != OVL_FILE_MAGIC) return NULL;
  uintptr_t app = (uintptr_t)TERRAIN_FLAGS - fh.anchor_data;                 // where the app image was loaded
  if ((((uintptr_t)rnd_next) & ~1u) != ((fh.anchor_code + app) & ~1u)) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "ovl %d: wrong app image", (int)res_id);
    return NULL;
  }
  uint8_t *img = keep ? NULL : arena_alloc(fh.img_size + fh.bss_size);
  if (!img) img = malloc(fh.img_size + fh.bss_size);
  for (int k = 0; !img && k < 2; k++) { ui_free_cache(k); img = malloc(fh.img_size + fh.bss_size); }   // drop monster pictures, then tiles (they reload), and retry
  if (!img) {
    int big = 0; for (int step = 16384; step >= 128; step /= 2) { void *t = malloc(big + step); if (t) { free(t); big += step; } }   // the largest free block (8 tries)
    g_ovl_need = fh.img_size + fh.bss_size; g_ovl_big = big;
    APP_LOG(APP_LOG_LEVEL_ERROR, "ovl %d: need %d B, free %d, big %d", (int)res_id, g_ovl_need, (int)heap_bytes_free(), big);
    return NULL;
  }
  int off = sizeof fh;
  resource_load_byte_range(h, off, img, fh.img_size); off += fh.img_size;
  memset(img + fh.img_size, 0, fh.bss_size);
  relocate(h, off, fh.n_rel_ovl, img, fh.img_size, (uintptr_t)img); off += 2 * fh.n_rel_ovl;
  relocate(h, off, fh.n_rel_app, img, fh.img_size, app);
  __asm__ volatile ("dsb\n\tisb" ::: "memory");                    // the new code is visible to the core
  const OvlHeader *o = (const OvlHeader *)img;
  if (o->magic != OVL_MAGIC) { ovl_free(o); return NULL; }
#ifdef DEBUG_TOOLS
  APP_LOG(APP_LOG_LEVEL_DEBUG, "ovl %d: %d B, free %d", (int)res_id, fh.img_size + fh.bss_size, (int)heap_bytes_free());
#endif
  return o;
}
void ovl_free(const OvlHeader *o) { if (!arena_free(o)) free((void *)o); }
#else
const OvlHeader *ovl_keep(uint32_t res_id) { return ovl_load(res_id); }
void ovl_arena_init(void) {}
int ovl_arena_free(void) { return 0; }
#endif

// ---- calls into overlays ----
static const OvlHeader *s_reg; static int8_t s_reg_id = -1;   // the region overlay (mreg_load below)

// the "use" overlay: Item.execute for every action, InventoryScroll / ShortSword choices, Potion.shatter
static const OvlHeader *use_ovl(void) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_USE);
  if (!o) ui_log("Not enough memory to do that right now.", C_BAD);
  return o;
}
ActResult game_item_do(int slot, ItemAction a) {
  mreg_ensure();
  const OvlHeader *o = use_ovl();
  if (!o) return ACT_NONE;
  ActResult r = ((ActResult (*)(int, ItemAction))o->fn[0])(slot, a);
  ovl_free(o);
  return r;
}
ActResult game_pick(PickMode mode, int slot) {
  if (mode == PICK_SMITH) {                            // WndBlacksmith: the band's creatures handle it (wnd_choice 100 + slot + 4)
    mreg_ensure();
    if (g_mh.wnd_choice) g_mh.wnd_choice(slot == -1000 ? 99 : 104 + slot);
    return g_level_changed ? ACT_LEVEL : ACT_NONE;
  }
  if (mode == PICK_SELL || mode == PICK_SEED || mode >= PICK_ARMOR)   // selling, the alchemy pot, the armor kit, a weightstone: the hero overlay
    return slot == -1000 ? ACT_NONE : (ActResult)hero_op(mode == PICK_SELL ? HO_SELL : mode == PICK_SEED ? HO_COOK : HO_APPLY, slot, mode, NULL);
  mreg_ensure();
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_PICK);   // 0.30: its own overlay (the use overlay outgrew the arena)
  if (!o) { ui_log("Not enough memory to do that right now.", C_BAD); return ACT_NONE; }
  ActResult r = ((ActResult (*)(PickMode, int))o->fn[0])(mode, slot);
  ovl_free(o);
  return r;
}
void potion_shatter(int k, int cell) {
  const OvlHeader *o = use_ovl();
  if (!o) return;
  ((void (*)(int, int))o->fn[2])(k, cell);
  ovl_free(o);
}

int game_targets(int16_t *cell, uint8_t *kind) {   // 0.23: the button target cycle (0.36: its own overlay; -1: no room now)
  int n = -100;                                      // 0.48: two overlays: tgt (the distances, plan A), then tgt2 (plans B, C...)
  for (int pass = 0; pass < 2 && n == -100; pass++) {   // (-100: tgt2 read the player's mind: a new plan A, both again)
    const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_TGT);
    if (!o) return -1;
    n = ((int (*)(int16_t *, uint8_t *))o->fn[0])(cell, kind);
    ovl_free(o);
    if (!(o = ovl_load(RESOURCE_ID_OVL_TGT2))) return n;   // (no room: plan A alone)
    n = ((int (*)(int16_t *, uint8_t *, int))o->fn[0])(cell, kind, n);
    ovl_free(o);
  }
  return n < 0 ? 0 : n;
}

// the "info" overlay: inspect and the item window's text
void game_inspect(int16_t cell, char *title, int tlen, char *desc, int dlen) {
  title[0] = desc[0] = 0;
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_INFO);
  if (!o) { snprintf(desc, dlen, "(not enough memory to describe this)"); return; }
  ((void (*)(int16_t, char *, int, char *, int))o->fn[0])(cell, title, tlen, desc, dlen);
  ovl_free(o);
}
void item_info(const Item *it, char *buf, int len) {
  buf[0] = 0;
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_INFO);
  if (!o) { snprintf(buf, len, "(not enough memory to describe this)"); return; }
  ((void (*)(const Item *, char *, int))o->fn[1])(it, buf, len);
  ovl_free(o);
}

// the "ench" overlay: enchantment and glyph effects (only loaded when equipment has one)
void ench_weapon_proc(int def, int dmg, Item *w) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_ENCH);
  if (!o) return;
  ((void (*)(int, int, Item *))o->fn[0])(def, dmg, w);
  ovl_free(o);
}
int ench_armor_proc(int att, int dmg) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_ENCH);
  if (!o) return dmg;
  int r = ((int (*)(int, int))o->fn[1])(att, dmg);
  ovl_free(o);
  return r;
}

// the "wand" overlay: what a wand's bolt does (Wand.onZap)
void wand_effect(int k, int power, int level, const int16_t *trace, int n) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_WAND);
  if (!o) { ui_log("Not enough memory to do that right now.", C_BAD); return; }
  ((void (*)(int, int, int, const int16_t *, int))o->fn[0])(k, power, level, trace, n);
  ovl_free(o);
}

void newgame_ovl(void) { const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_NEWGAME); if (o) { ((void (*)(void))o->fn[0])(); ovl_free(o); } }
// the "floor" overlay: saves, floor changes (see game_int.h FO_*)
int floor_op(int op, int a, void *p) {
  const OvlHeader *o = ovl_load(op == FO_SAVE || op == FO_SAVE_SIZE || op == FO_PEEK || op == FO_RANK || op == FO_ERASE ? RESOURCE_ID_OVL_SAVE : RESOURCE_ID_OVL_FLOOR);
  if (!o) { ui_log("Not enough memory to do that right now.", C_BAD); return 0; }
  int r = ((int (*)(int, int, void *))o->fn[0])(op, a, p);
  ovl_free(o);
  return r;
}

// the "mobs" overlay: loaded once and kept (creature behaviours, game_int.h MobHooks)
static const OvlHeader *s_mobs;
void mobs_ovl_init(void) {
  if (s_mobs) return;
  s_mobs = ovl_keep(RESOURCE_ID_OVL_MOBS);
  if (s_mobs) ((void (*)(MobHooks *))s_mobs->fn[0])(&g_mh);
  g_mh_base = g_mh;
}
// the region's creatures (0.9): one overlay per region, swapped when the hero changes region
MobHooks g_mh_base;
static const uint32_t REG_RES[5] = { RESOURCE_ID_OVL_SEWERS, RESOURCE_ID_OVL_PRISON, RESOURCE_ID_OVL_CAVES, RESOURCE_ID_OVL_CITY, RESOURCE_ID_OVL_HALLS };   // by depth band
void mreg_load(int region) {
  if (region == s_reg_id && (s_reg || region < 0 || !REG_RES[region])) return;
  if (s_reg) { ovl_free(s_reg); s_reg = NULL; }
  g_mh = g_mh_base;
  s_reg_id = region;
  if (region < 0 || !REG_RES[region]) return;
  s_reg = ovl_keep(REG_RES[region]);
  if (s_reg) ((void (*)(MobHooks *, const MobHooks *))s_reg->fn[0])(&g_mh, &g_mh_base);
}
// a window that needs the memory may drop the band's creatures; every game action brings them back first
void mreg_drop(void) { if (s_reg) { ovl_free(s_reg); s_reg = NULL; g_mh = g_mh_base; } }
void mreg_spare(void) { if (!ovl_arena_free()) mreg_drop(); }   // 0.20: windows go in the arena; without one they need the room
void mreg_ensure(void) { if (!s_reg && s_reg_id >= 0) mreg_load(s_reg_id); }
void trap_ovl(int cell, int who) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_TRAP);
  if (!o) { set_terrain(cell, T_INACTIVE_TRAP); return; }
  ((void (*)(int, int))o->fn[0])(cell, who);
  ovl_free(o);
}
void plant_ovl(int cell, int who) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_TRAP);
  if (!o) return;
  ((void (*)(int, int))o->fn[1])(cell, who);
  ovl_free(o);
}
void land_ovl(Item it, int cell) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_TRAP);
  if (!o) { heap_put(cell, it); return; }
  ((void (*)(Item *, int))o->fn[2])(&it, cell);
  ovl_free(o);
}
void feature_ovl(int cell, int who) {
  const OvlHeader *o = ovl_load(RESOURCE_ID_OVL_TRAP);
  if (!o) return;
  ((void (*)(int, int))o->fn[3])(cell, who);
  ovl_free(o);
}
static const OvlHeader *s_gen;
void gen_hold(bool keep) {
  if (keep && !s_gen) s_gen = ovl_load(RESOURCE_ID_OVL_GEN);
  else if (!keep && s_gen) { ovl_free(s_gen); s_gen = NULL; }
}
Item gen_item(int cat) {
  const OvlHeader *o = s_gen ? s_gen : ovl_load(RESOURCE_ID_OVL_GEN);
  if (!o) return item_make(IK_GOLD, gold_amount());
  Item it = ((Item (*)(int))o->fn[0])(cat);
  if (!s_gen) ovl_free(o);
  return it;
}
void gen_reset(void) {
  const OvlHeader *o = s_gen ? s_gen : ovl_load(RESOURCE_ID_OVL_GEN);
  if (!o) return;
  ((void (*)(void))o->fn[1])();
  if (!s_gen) ovl_free(o);
}

// the "hero" overlay: picking up, opening, zapping, throwing, the item menu (0.9: out of the app image)
int hero_op(int op, int a, int b, void *p) {
  if (op <= HO_THROW || op == HO_COOK || op == HO_TRADE) mreg_ensure();   // the ones that pass time (a trade: buying and selling take a turn)
  const OvlHeader *o = ovl_load(op <= HO_CONFIRM ? RESOURCE_ID_OVL_HERO : RESOURCE_ID_OVL_HERO2);   // 0.17: two halves
  if (!o) { ui_log("Not enough memory to do that right now.", C_BAD); return 0; }
  int r = ((int (*)(int, int, int, void *))o->fn[0])(op, a, b, p);
  ovl_free(o);
  if (g_wnd_item.kind) { ui_wnd_info(&g_wnd_item); g_wnd_item.kind = 0; }   // a trade window's item description
  return r;
}
Item g_wnd_item;
ActResult pick_up_ex(int c, bool reach) { return (ActResult)hero_op(HO_PICKUP, c, reach, NULL); }
ActResult open_heap(int c) { return (ActResult)hero_op(HO_OPEN, c, 0, NULL); }
ActResult game_zap(int slot, int16_t target) { return (ActResult)hero_op(HO_ZAP, slot, target, NULL); }
ActResult game_throw(int slot, int16_t target) { return (ActResult)hero_op(HO_THROW, slot, target, NULL); }
ActResult game_aim(int what, int slot, int16_t target) { return (ActResult)hero_op(what == 1 ? HO_ZAP : what == 2 ? HO_LEAP : HO_THROW, what == 2 ? target : slot, target, NULL); }
void game_ankh(void) { hero_op(HO_ANKH, 0, 0, NULL); }
ActResult game_quick_throw(int slot) { int t = game_nearest_enemy(); return t < 0 ? ACT_NONE : game_throw(slot, t); }
int game_item_actions(int slot, uint8_t *out) { return hero_op(HO_ACTIONS, slot, 0, out); }
int game_confirm_needed(int slot, ItemAction a) { return hero_op(HO_CONFIRM, slot, a, NULL); }
#ifdef DEBUG_JUMP
void game_debug_map(void) { hero_op(HO_DEBUG, 0, 0, NULL); }
#endif
