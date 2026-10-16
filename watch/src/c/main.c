/*
 * Pixel Dungeon for Pebble - screens, drawing, input, timing.
 * Layout follows PD: status pane on top, toolbar (wait / search / backpack / quickslot) at
 * the bottom, item windows for the backpack.
 */
#include "pd.h"
#include "sheets.auto.h"
#include "ovl.h"
#include "ui_int.h"
static void ui_win(int what, GContext *ctx);
static void ui_win_free(void);
static void ui_input(int ev, int x, int y);
static void zoom_save(void);

// ---------------- art ----------------
static void sheet_load(Sheet *s, uint32_t res);
// Item sprites and (0.20) the floor's tiles are raw 4-bit cells (items_raw.bin, tiles_raw.bin: 16 palette bytes +
// 16x16 at 4 bits) in one LRU cache: one block, ~7 KB, where the whole tile set alone took 8.4 KB in four bitmaps
#define SPR_N 48
#define SPR_SIZE 144
static uint8_t *s_spr; static uint16_t s_spr_id[SPR_N]; static uint32_t *s_spr_use, s_spr_clock;   // id: item sprite, or 256 + tile
static int s_spr_rg, s_spr_n; static uint8_t s_spr_hint[32];                        // the region whose tiles are cached; slots (fewer on a tight heap)
static void ispr_free(void) { free(s_spr); s_spr = NULL; }
static const uint8_t *spr_get(int id) {
  if (!s_spr) {
    for (s_spr_n = SPR_N; s_spr_n >= 12 && !(s_spr = malloc(s_spr_n * SPR_SIZE)); s_spr_n -= 12) {}
    if (!s_spr) return NULL;
    memset(s_spr_id, 0xFF, sizeof s_spr_id);
  }
  int slot = s_spr_hint[id & 31];                    // 0.35: where this id was last found (a search of every slot per tile was slow)
  if (slot < s_spr_n && s_spr_id[slot] == id) goto hit;
  slot = 0;
  for (int i = 0; i < s_spr_n; i++) {
    if (s_spr_id[i] == id) { slot = i; goto found; }
    if (s_spr_use[i] < s_spr_use[slot]) slot = i;
  }
  resource_load_byte_range(resource_get_handle(id < 256 ? RESOURCE_ID_ITEMS_RAW : RESOURCE_ID_TILES_RAW),
                           (id < 256 ? id : s_spr_rg * 64 + id - 256) * SPR_SIZE, s_spr + slot * SPR_SIZE, SPR_SIZE);
  s_spr_id[slot] = id;
found:
  s_spr_hint[id & 31] = slot;
hit:
  s_spr_use[slot] = ++s_spr_clock;
  return s_spr + slot * SPR_SIZE;
}
void item_spr(int sp, int x, int y, int z) {
  const uint8_t *p = sp >= 0 && sp < N_ITEM_SPRITES ? spr_get(sp) : NULL;
  if (p) blit_spr4(p, x, y, z);
}
#define ITEM_SPR(sp, x, y, z) item_spr(sp, x, y, z)
Sheet s_plants;
Sheet s_water, s_hero, s_mob[MT_COUNT], s_toolbar, s_logo, s_chrome, s_status, s_hpbar,
  s_arcs, s_play, s_avatars, s_banner, s_tag, s_tag_blue, s_tag_white, s_buffs, s_icons, s_gameover, s_micons;
GFont s_font_hud, s_font_log, s_font_big, s_font_tiny;

// Character sprite sheets: PD's TextureFilm frames, row 2 mirrored (Pebble can't flip bitmaps)
// Animations exactly as PD's *Sprite classes: frames and frames per second. Idle and run loop; the rest play once.
// Like PD's CharSprite, a monster keeps its run loop after a step until something sets it idle (an attack
// finishing, falling asleep), so hunting monsters hop the whole fight; the hero goes idle when ready.
#define HERO_SEQ { { 0, 0, 0, 1, 0, 0, 1, 1 }, { 2, 3, 4, 5, 6, 7 }, { 13, 14, 15, 0 }, { 8, 9, 10, 11, 12, 11 }, \
                   { 16, 17, 16, 17 }, { 19, 20, 20, 20, 20, 20, 20, 20, 20, 19 } }
static const CharArt HERO_ART = { 12, 15, HERO_SEQ, { 8, 6, 4, 6, 4, 10 }, { 1, 20, 15, 20, 8, 20 } };   // HeroSprite
// each creature's animations (tools/mobs.py -> mob_art.bin) load with its sheet; the mirror image wears the hero's
#include "item_sp.h"
int heap_sprite(int h) {                            // ItemSprite: what a heap looks like (a mimic is a chest)
  switch (G.heaps[h].type) {
    case HEAP_CHEST: case HEAP_MIMIC: return SP_CHEST;
    case HEAP_SKELETON: return SP_BONES;
    case HEAP_LOCKED_CHEST: return SP_LOCKED_CHEST;
    case HEAP_CRYSTAL_CHEST: return SP_CRYSTAL_CHEST;
    case HEAP_TOMB: return SP_TOMB;
  }
  return item_sprite(G.heaps[h].item.kind);
}
static const CharArt MIRROR_ART = { 12, 15, { { 0, 0, 0, 1, 0, 0, 1, 1 }, { 2, 3, 4, 5, 6, 7 }, { 13, 14, 15, 0 }, { 0 } }, { 8, 6, 4, 1, 1, 1 }, { 1, 20, 15, 20, 1, 1 } };
static CharArt *s_mob_art[MT_COUNT];
const CharArt *mob_art(int t) {
  if (t == MT_MIRROR) return &MIRROR_ART;
  if (!s_mob_art[t]) {
    CharArt *a = malloc(sizeof(CharArt));
    if (!a) return &MIRROR_ART;
    resource_load_byte_range(resource_get_handle(RESOURCE_ID_MOB_ART), t * sizeof(CharArt), (uint8_t *)a, sizeof(CharArt));
    s_mob_art[t] = a;
  }
  return s_mob_art[t];
}
// a mirror image wears the hero's sheet


static void sheet_load(Sheet *s, uint32_t res) {
#ifdef DEBUG_TOOLS
  int f0 = heap_bytes_free();
#endif
  s->bmp = gbitmap_create_with_resource(res);
#ifdef DEBUG_TOOLS
  if (!s->bmp) APP_LOG(APP_LOG_LEVEL_ERROR, "bitmap %d: no memory (free %d)", (int)res, (int)heap_bytes_free());
  else APP_LOG(APP_LOG_LEVEL_DEBUG, "bmp %d: %d B (free %d)", (int)res, f0 - (int)heap_bytes_free(), (int)heap_bytes_free());
#endif
}
static void sheet_free(Sheet *s) { if (s->bmp) { gbitmap_destroy(s->bmp); blit_forget(); } s->bmp = NULL; }
// monster sheets load when that kind is first drawn and are dropped when the hero leaves the floor
#include "mob_res.auto.h"
const Sheet *mob_sheet(int type) {
  if (type == MT_MIRROR) return &s_hero;
  if (!s_mob[type].bmp && MOB_RES[type]) sheet_load(&s_mob[type], MOB_RES[type]);
  return &s_mob[type];
}
static void mob_sheets_free(void) { for (int t = 0; t < MT_COUNT; t++) { sheet_free(&s_mob[t]); free(s_mob_art[t]); s_mob_art[t] = NULL; } }
static void tiles_load(bool load);
void ui_free_cache(int more) { mob_sheets_free(); ispr_free(); if (more) tiles_load(false); }   // the next frame in the game reloads them
// HeroSprite.updateArmor: one sheet per armour tier, only the worn one in RAM
// the sheets are listed class by class (warrior, mage, rogue, huntress), tiers 0..6 each, in package.json
static int s_hero_tier = -1;
void ui_hero_tier(void) {
  int t = hero_tier();
  if (t < 0 || t > 6) t = 0;
  if (t == s_hero_tier && s_hero.bmp) return;
  sheet_free(&s_hero); sheet_load(&s_hero, RESOURCE_ID_IMAGE_HERO_WARRIOR_0 + (G.hero.hclass & 3) * 7 + t); s_hero_tier = t;
}
// pictures only some windows use: loaded while shown
static void sheet_need(Sheet *s, uint32_t res, bool need) { if (need && !s->bmp) sheet_load(s, res); else if (!need && s->bmp) sheet_free(s); }

#define KEY_ZOOM 200                                // (102 was inside the floor records, KEY_LEVEL(20) + 2)
// title and hero-select art only lives while those screens are up (~12 KB of RAM)
// the dungeon's pictures (~30 KB) only while playing: the title screens and the game never hold both
static void game_art(bool load) {
  if (load == (s_status.bmp != NULL)) return;
  if (!load) {
    tiles_load(false);
    ispr_free();
    Sheet *all[] = { &s_plants, &s_buffs, &s_icons, &s_toolbar, &s_status, &s_hpbar, &s_tag, &s_tag_blue, &s_tag_white, &s_hero };
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) sheet_free(all[i]);
    s_hero_tier = -1;
    return;
  }
  tiles_load(true);
  sheet_load(&s_plants, RESOURCE_ID_IMAGE_PLANTS);
  sheet_load(&s_buffs, RESOURCE_ID_IMAGE_BUFFS);
  sheet_load(&s_icons, RESOURCE_ID_IMAGE_ICONS);
  sheet_load(&s_toolbar, RESOURCE_ID_IMAGE_TOOLBAR);
  sheet_load(&s_status, RESOURCE_ID_IMAGE_STATUS_PANE);
  sheet_load(&s_hpbar, RESOURCE_ID_IMAGE_HP_BAR);
  sheet_load(&s_tag, RESOURCE_ID_IMAGE_TAG_RED);
  sheet_load(&s_tag_blue, RESOURCE_ID_IMAGE_TAG_BLUE);
  sheet_load(&s_tag_white, RESOURCE_ID_IMAGE_TAG_WHITE);
}
// the floor's water, and its tiles' region for the sprite cache (0.20: the tiles themselves are cached cells)
static void tiles_load(bool load) {
  if (!load) { ispr_free(); sheet_free(&s_water); return; }
  int rg = region_of(G.depth);
  if (rg != s_spr_rg) { s_spr_rg = rg; for (int i = 0; i < SPR_N; i++) if (s_spr_id[i] >= 256) s_spr_id[i] = 0xFFFF; }
  static const uint32_t WATER_RES[5] = { RESOURCE_ID_IMAGE_WATER_SEWERS, RESOURCE_ID_IMAGE_WATER_PRISON, RESOURCE_ID_IMAGE_WATER_CAVES,
                                          RESOURCE_ID_IMAGE_WATER_CITY, RESOURCE_ID_IMAGE_WATER_HALLS };
  sheet_load(&s_water, WATER_RES[rg]);
}
static void front_art(bool load) {
  if (load == (s_logo.bmp != NULL)) return;
  if (load) { game_art(false); mob_sheets_free(); game_free_fx(); }
  if (load) {
    sheet_need(&s_logo, RESOURCE_ID_IMAGE_LOGO, true); sheet_need(&s_arcs, RESOURCE_ID_IMAGE_ARCS, true); sheet_need(&s_play, RESOURCE_ID_IMAGE_PLAY, true);
    sheet_need(&s_avatars, RESOURCE_ID_IMAGE_AVATARS, true); sheet_need(&s_banner, RESOURCE_ID_IMAGE_BANNER_SELECT, true);
  } else { sheet_free(&s_logo); sheet_free(&s_arcs); sheet_free(&s_play); sheet_free(&s_avatars); sheet_free(&s_banner); }   // game art: the caller
}

// ---------------- state ----------------
static Window *s_window;
static Layer *s_layer;
Scene s_scene = SC_TITLE;
int s_sel;
bool s_has_save; uint8_t s_save_depth, s_save_lvl;
static bool s_walking;
uint32_t s_died_ms;
static bool s_long_press;                          // the tap being handled was held down
int s_sheet_tab;                            // WndHero: 0 stats, 1 buffs
static AppTimer *s_timer; static uint32_t s_timer_ms;
int s_w, s_h;
static int s_zoom = 1;                    // 0: 8px tiles, 1: 16px, 2: 32px
int s_pick = -1, s_pick_pending = -1;      // PickMode while the bag is an item selector
int s_opt;                                  // WndOptions: 0 chasm jump, 1 harmful drink, 2 beneficial throw
int s_item_slot;                    // slot shown in the item window / being thrown
static int s_aim_q = -1, s_aim_mob = -1;  // QuickSlot targeting: which quickslot is aiming, and at which monster
static uint32_t s_check_t0; static int16_t s_check_pos = -1; static uint8_t s_check_r = 1; static bool s_quake;   // 0.28: a quake (avalanche, a leap's slam)
static uint32_t s_fms;                            // 0.34: the frame's clock: the camera holds still while a frame is drawn
static int shake(void) { uint32_t e = s_fms - s_check_t0; return s_quake && e < 400 ? ((e >> 5) & 1 ? 3 : -3) : 0; }   // Camera.shake
static uint32_t s_leap_t0; static int16_t s_leap_x, s_leap_y;   // 0.29: HeroSprite.jump, from where
#define FADE_MS 300
static int s_fade; static uint32_t s_fade_t0;      // floor change: 1 fading out on the old floor, 2 fading in on the new   // CheckedCell: when and where the hero last searched
uint8_t s_acts[ACN_COUNT]; int s_n_acts;

static Vis s_vis_hero, *s_vis_mob;   // (0.54: the creatures' on the heap)
static uint32_t *s_alert_until;            // EmoIcon.Alert over a mob that just noticed you
// camera: offset from the hero in world units (16 per tile); dragging pans, recentering zeroes it
static int16_t s_cam_dx, s_cam_dy;
static bool s_panning; static int16_t s_pan_dx0, s_pan_dy0;
// 0.25: a new offset is reached by a swoop (speeding up, then slowing into it) rather than a jump
#define GLIDE_MS 300
static int16_t s_glide_x, s_glide_y; static uint32_t s_glide_t0;   // the offset the swoop started from, and when
static int glide_f(void) {                          // 0..256 along the swoop (smoothstep)
  int32_t e = (int32_t)(s_fms - s_glide_t0);       // (0.34: per frame, else rows drawn later saw a later camera: torn strips)
  if (!s_glide_t0 || e >= GLIDE_MS) return 256;
  if (e < 0) e = 0;
  int f = e * 256 / GLIDE_MS; return f * f * (768 - 2 * f) >> 16;
}
static int cam_ox(void) { return s_glide_x + (s_cam_dx - s_glide_x) * glide_f() / 256; }
static int cam_oy(void) { return s_glide_y + (s_cam_dy - s_glide_y) * glide_f() / 256; }
static int shake(void);
static int cam_x(void) { return s_vis_hero.x + cam_ox() + shake(); }
static int cam_y(void) { return s_vis_hero.y + cam_oy(); }
static bool cam_free(void) { return s_cam_dx || s_cam_dy; }
static void cam_glide(void) { s_glide_x = cam_ox(); s_glide_y = cam_oy(); s_glide_t0 = now_ms() | 1; }
static void cam_center(void) { if (cam_free()) cam_glide(); s_cam_dx = s_cam_dy = 0; }
static void pan_to(int dx, int dy);
static void cam_to(int c) {                        // 0.76 (owner): Assist Max: a quick swoop that puts the target mid-screen (same zoom)
  int x = s_cam_dx, y = s_cam_dy; s_cam_dx = s_cam_dy = 0;
  if (c >= 0) pan_to((c % MAP_W - G.hero.pos % MAP_W) * TS, (c / MAP_W - G.hero.pos / MAP_W) * TS);
  if (s_cam_dx != x || s_cam_dy != y) { int nx = s_cam_dx, ny = s_cam_dy; s_cam_dx = x; s_cam_dy = y; cam_glide(); s_cam_dx = nx; s_cam_dy = ny; }
}
typedef struct { int16_t cell; uint32_t t0; GColor color; char text[12]; } Floater;   // t0: when it appeared
#define MAX_FLOAT 6
#define FLOAT_MS 460                               // how long damage numbers rise
static Floater *s_float; static int s_n_float;   // (0.52: on the heap)
typedef struct { bool active; int16_t x0, y0, x1, y1; uint32_t t0; uint16_t ms; int sprite; uint8_t n_heaps; int16_t to; } Proj;
static Proj s_proj;
static bool s_flames_shown;                       // fire on screen: redraw often enough for rising flames
// effects of a throw (hits, numbers, deaths, what the monsters did next) wait until the missile lands
typedef struct { uint8_t kind; int8_t who; int16_t cell; char text[12]; } QueuedFx;
#define MAX_QFX 12
static QueuedFx *s_qfx; static int s_n_qfx;   // (0.51: on the heap: the app's size limit)

// PD GameLog, two newest lines; they fade together a few seconds after the last one
char (*s_msg)[100]; static GColor s_msg_color[2]; static uint32_t s_msg_until;

uint32_t now_ms(void) { time_t s; uint16_t ms; time_ms(&s, &ms); return (uint32_t)s * 1000 + ms; }
static Vis *vis_of(int who) { return who < 0 ? &s_vis_hero : &s_vis_mob[who]; }
static void vis_anim(Vis *v, int a) { v->anim = a; v->t0 = now_ms(); }
static void vis_snap(Vis *v, int16_t cell) {
  v->x = (cell % MAP_W) * TS; v->y = (cell / MAP_W) * TS; v->active = true;
  vis_anim(v, A_IDLE); v->t0 -= (uint32_t)(cell * 373) % 2000;           // each sprite idles on its own beat
}
static void vis_reset_all(void) {
  s_cam_dx = s_cam_dy = 0; s_glide_t0 = 0;
  vis_snap(&s_vis_hero, G.hero.pos);
  for (int i = 0; i < MAX_MOBS; i++) s_vis_mob[i].active = false;
  s_n_float = 0; s_walking = false; s_proj.active = false; s_n_qfx = 0;
}

// ---------------- layout ----------------
// Status pane = PD StatusPane art drawn at 1.5x (52 px). Toolbar = 6 big buttons built from PD's
// toolbar frame (9-slice) with the icons drawn at 2x.
static int tile_px(void) { return 8 << s_zoom; }
#ifdef PBL_ROUND
// Gabbro (0.35): the HUD rings the round screen and the map fills it. A grey cap over the top arc holds the health and
// experience bars, health, depth and keys; the portrait hangs from it on the left, the menu button on the right; the
// toolbar's buttons follow the bottom arc (a smile); the buttons' tags sit at the right rim beside Up, Select, Down
#define CAP_H 38
static int status_y(void) { return 26; }
static int status_x(void) { return 36; }
static int status_w(void) { return s_w; }
static int status_bottom(void) { return CAP_H; }
static int tb_w(void) { return 30; }
static int toolbar_y(void) { return 199; }               // the inner buttons' top: the log ends above it
static const uint8_t TBX[TB_N] = { 36, 67, 98, 132, 163, 194 }, TBY[TB_N] = { 174, 199, 211, 211, 199, 174 };
static int tb_x(int i) { return TBX[i]; }
static int tb_in(int x, int y) { for (int i = 0; i < TB_N; i++) if (x >= TBX[i] && x < TBX[i] + 30 && y >= TBY[i] - 6 && y < TBY[i] + TB_H) return i; return -1; }
#define TB_ROW(x, y) (tb_in(x, y) >= 0)
#define TB_HIT(x, y) tb_in(x, y)
#define TB_Y(i) TBY[i]
#else
static int status_y(void) { return 0; }
static int status_w(void) { return s_w; }
static int status_x(void) { return (s_w - status_w()) / 2; }
static int status_bottom(void) { return status_y() + SC(33); }
static int tb_w(void) { return 32; }
static int tb_gap(void) { return 8; }
static int toolbar_y(void) { return s_h - TB_H; }
static int toolbar_x0(void) { return (s_w - TB_N * tb_w() - tb_gap()) / 2; }
static int tb_x(int i) { return toolbar_x0() + i * tb_w() + (i >= 3 ? tb_gap() : 0); }
static int tb_hit(int x) { for (int i = 0; i < TB_N; i++) if (x >= tb_x(i) && x < tb_x(i) + tb_w()) return i; return -1; }
#define TB_ROW(x, y) (y >= toolbar_y() && y < toolbar_y() + TB_H)
#define TB_HIT(x, y) tb_hit(x)
#define TB_Y(i) ty
#endif
// 0.18 combat mode: while an enemy is in sight (or a throw / zap is aimed) three red tags line up with the buttons
// that work them: zoom by Up, attack (PD AttackIndicator, with the weapon about to be used) by Select, the next
// target by Down (only when there is more than one in range). They replace PD's DangerIndicator.
// 0.23: the buttons always work these tags. Down picks the next target (game_targets: enemies, a step away, doors,
// things, stairs...); Select acts on it, as a tap on its cell would (attack, walk, pick up). Aiming a throw or a zap,
// the targets are the enemies in the line of fire.
#ifdef PBL_ROUND                                    // 0.36: the cells around the hero at least half on screen, per zoom: across, up, down,
static const uint8_t VIEW[3][4] = { { 15, 11, 8, 240 }, { 7, 5, 4, 60 }, { 3, 2, 2, 15 } };   // radius squared
#else                                               // (Emery, 0.38: left, up, down, right: the map between the status pane, y 49,
static const uint8_t VIEW[3][4] = { { 12, 8, 8, 9 }, { 6, 4, 4, 4 }, { 3, 2, 2, 2 } };   // and the toolbar, y 190; the tags from x 178)
#endif
static int16_t s_tg = -1, s_tg0 = -1; static uint8_t s_tgk, s_tgu, s_tgn, s_tgi, s_tgk0;   // the chosen target's cell and kind (an enemy: its uid)
static void tg_refresh(int dir) {                 // keep the chosen target while it's in the list, else the best; dir: +1 next, -1 back
  static int32_t s_now = -1, s_t0; static int16_t s_pos; static int8_t s_z;   // the list only changes when time passes, the hero moves or the zoom changes
  if (!dir && G.now == s_now && G.hero.pos == s_pos && s_zoom == s_z) return;
  if (G.hero.pos != s_pos) g_from = s_t0 == G.level_t0 ? s_pos : -1;   // 0.39: the way the hero went: the targets ahead first, then clockwise (0.47: not from the last floor)
  if (s_t0 != G.level_t0) { g_goal = -1; memset(g_drop, 0xFF, sizeof g_drop); g_ovr.on &= ~4; }   // 0.77: a new floor: no goal, drops or skipped grass from the last
  s_t0 = G.level_t0;
  s_now = G.now; s_pos = G.hero.pos; s_z = s_zoom;
  if (s_walking && !dir) { s_now = -1; return; }     // 0.43 (audio): not on every step of a walk (Select/taps stop it first): when it ends
  memcpy(g_view, VIEW[s_zoom], 4);                   // 0.36: only what's on screen
  if (AS_OFFCAM) memset(g_view, 0, 4);              // 0.75: Max (0.80: off-screen targets): anywhere
  int16_t c[TARGETS_MAX]; uint8_t k[TARGETS_MAX];
  int n = G.hero.HP > 0 ? game_targets(c, k) : 0, cur = -1;
  if (n < 0) { s_now = -1; return; }                 // 0.36: no room for the overlay now: the list as it was, tried again next time
  for (int i = 0; i < n && cur < 0; i++) {
    int m = k[i] == TK_ENEMY ? mob_at(c[i]) : -1;
    if (k[i] == s_tgk && (m >= 0 ? G.mobs[m].uid == s_tgu : c[i] == s_tg)) cur = i;
  }
  int i = 0;                                         // 0.40: after each step the first one (what a good player would do next)
  if (dir && cur >= 0) i = (cur + dir + n) % n;      // Down / long Down: the next / the one before
  else if (dir < 0) i = n - 1;
  else if (cur >= 0 && !AS_ON) i = cur;  // 0.75 Off: the enemy chosen stays chosen (PD's last target)
  s_tgn = n; s_tgi = i; s_tg = n ? c[i] : -1; s_tgk = n ? k[i] : 0; s_tgk0 = n ? k[0] : 0; s_tg0 = n ? c[0] : -1;
  if (n && s_tgk == TK_ENEMY) { s_tgu = G.mobs[mob_at(s_tg)].uid; G.last_target = s_tg; }
  if (AS_OFFCAM) cam_to(s_tg);              // 0.76: Max: the camera on whatever is chosen, cycled to or not
}
void hero_spend(int t); void process(void);
static int cheb(int a, int b) { int dx = abs((a & 31) - (b & 31)), dy = abs((a >> 5) - (b >> 5)); return dx > dy ? dx : dy; }
static void pass_hands(int c) {                   // the player chose otherwise than plan A (c: where): the hands' suggestion passed over (0.53), and
  G.tgm.ra = 0;                                      // (0.77: the interrupted walk isn't taken up again over the player's choice)
  if (s_tgk0 == TK_BAG || s_tgk0 == TK_REST) { int i = G.tgm.dn++ & 7; G.tgm.dk[i] = s_tgk0 == TK_REST ? 0 : game_slot(g_ovr.hs)->kind; G.tgm.da[i] = s_tgk0 == TK_REST ? 255 : g_ovr.ha; G.tgm.dd = G.depth; }   // (0.67: 8 remembered, not just the last)
  if (s_tgk0 == TK_ITEM && (unsigned)g_tgobj[0] < MAP_LEN && cheb(c, g_tgobj[0]) >= cheb(G.hero.pos, g_tgobj[0])) g_drop[g_drop_i++ & 3] = g_tgobj[0];   // 0.78 (owner): a thing passed over (not walked towards): not offered again
  if (s_tgk0 == TK_ENEMY && abs((c & 31) - (G.hero.pos & 31)) <= 1 && abs((c >> 5) - (G.hero.pos >> 5)) <= 1) return;   // (a step back from an enemy: the doorway fight, still on)
  int m = s_tgk0 == TK_ENEMY && s_tg0 >= 0 ? mob_at(s_tg0) : -1;   // 0.66 (owner: "I cannot be fighting the assist"): what made plan A
  G.tgm.vs = G.tgm.src0; G.tgm.vu = m >= 0 ? G.mobs[m].uid : 255; G.tgm.vt = G.now + 15 * TICK;   // (an enemy: that one) is vetoed 15 turns (ovl/tgt)
}
static ActResult tg_do(void) {                    // Select: as a tap on the target (0.36: rooted, 0.40: the hero's own cell: search)
  int c = s_tg, k = s_tgk, hp = G.hero.pos;
  if (s_tgi) pass_hands(s_tg);                         // 0.53 (owner): plan B over the hands: pass that one over
  if (k == TK_MINE) return game_item_do(g_ovr.ms, ACN_MINE);   // 0.41: beside a dark gold vein: mine it (TK_VEIN: walk there)
  if (k == TK_BAG) return game_item_do(g_ovr.hs, (ItemAction)g_ovr.ha);   // 0.48 test kit (0.51: Assist High): the bot's hands (0.50: a throw or a zap at the nearest foe in line: use overlay)
  if (k == TK_REST) return game_rest(true);
  if (!s_tgi) { if (g_ovr.d & 128) { mreg_ensure(); hero_spend(TICK); process(); return ACT_TURN; } }   // 0.41: the first, an enemy
  else if (k != TK_ENEMY) { g_ovr.c = g_tgobj[s_tgi] | 0x4000; g_ovr.t = hp - 65536; }   // 0.47 (owner): the objective itself becomes plan A till met (0x4000: from the list)   // a step short: wait a turn for it (holding a doorway; game_rest(false)
                                                    // inline); not the first: the player's own way (from here; ovl/tgt)
  return (G.hero.b.roots || c == hp) && k == TK_GO ? game_search() : game_tap_cell(c);
}
static int combat_target(void) {                   // the enemy the middle tag acts on, or -1
  if (s_scene == SC_TARGET) return s_aim_mob >= 0 && G.mobs[s_aim_mob].HP > 0 ? s_aim_mob : -1;
  return s_scene == SC_GAME && s_tg >= 0 && s_tgk == TK_ENEMY ? mob_at(s_tg) : -1;
}
static bool combat(void) { return (s_scene == SC_TARGET || s_scene == SC_GAME) && G.hero.HP > 0; }   // the tags are up
static bool cycle_shown(void) { return s_scene == SC_TARGET ? combat_target() >= 0 : s_tgn > 0; }
static GRect ctag(int k) {                          // 0 zoom (Up), 1 attack (Select), 2 next target (Down)
#ifdef PBL_ROUND
  return GRect(k == 1 ? 236 : 224, 80 + k * 38, 22, 24);   // at the rim, by the buttons
#else
  int bot = toolbar_y() - 2, top = s_h - bot;       // 0.20: zoom and next target mirror each other, attack at the centre
  return GRect(s_w - 22, k == 0 ? top : k == 1 ? s_h / 2 - 12 : bot - 24, 22, 24);   // 0.24: slim indicators
#endif
}
static bool loot_shown(void) { int h = heap_at(G.hero.pos); return h >= 0 && G.hero.HP > 0 && s_scene == SC_GAME && !s_walking; }
static bool resume_shown(void) { return G.last_action && G.hero.HP > 0 && s_scene == SC_GAME && !s_walking; }
// PD StatusPane.layoutTags: LootIndicator (blue, what lies underfoot) and ResumeButton (pale) stack at the right edge
// under the status pane (0.17); 0.23: at the left edge, the right one holds the buttons' tags
static GRect tag_rect(int which) {
  int y = PBL_IF_ROUND_ELSE(92, status_bottom() + 2);
  if (which > 1 && loot_shown()) y += 38;
  return GRect(PBL_IF_ROUND_ELSE(0, -4), y, 40, which == 1 ? 36 : 30);
}
static GRect menu_button_rect(void) { return PBL_IF_ROUND_ELSE(GRect(182, 40, 30, 30), GRect(s_w - 27, 2, 27, 30)); }   // 0.20, Emery: bigger, drawn over the pane's
int map_center_y(void) { return PBL_IF_ROUND_ELSE(s_h / 2, (status_bottom() + toolbar_y()) / 2); }
// 0.42 gloves on: Back walks a highlight round the screen's buttons: the toolbar left to right (0-5), the menu button (6),
// the portrait (7), then none (Pebble keeps a long Back for leaving the app). Select presses the highlighted one as a tap
// would (long Select: a held tap), Up goes back one, Down drops it
static int8_t s_hl = -1; static uint8_t s_hlp;   // (0.54: where Back is in g_hl_order)
static GRect hl_rect(int k) {
  return k < TB_N ? GRect(tb_x(k), PBL_IF_ROUND_ELSE(TBY[k], toolbar_y()), tb_w(), TB_H) : k == 7 ? GRect(status_x(), status_y(), SC(30), SC(30)) : menu_button_rect();
}
__attribute__((noinline)) void reticle(GRect r) {   // Icons.TARGET's corners at 2x around r (its dot left out)
  for (int i = 0; i < 4; i++) blit(&s_icons, 67 + (i & 1) * 10, (i & 2) * 5, 6, 6, r.origin.x + (i & 1) * (r.size.w - 12), r.origin.y + (i >> 1) * (r.size.h - 12), 2);
}
// any picture scaled to any size (nearest neighbour): a NinePatch with no border
static void blit_scaled(const Sheet *sh, int sx, int sy, int sw, int shh, int dx, int dy, int dw, int dh) {
  if (sw > 0 && dw > 0) blit_nine(sh, sx, sy, sw, shh, 0, dx, dy, dw, dh);
}
// ---------------- callbacks from the rules ----------------
static GColor msg_color(MsgColor c) {
  switch (c) {
    case C_GOOD: return GColorGreen; case C_BAD: return GColorSunsetOrange; case C_WARN: return GColorRajah;
    case C_GOLD: return GColorChromeYellow; default: return GColorWhite;
  }
}
void cap_first(char *s) { if (s[0] >= 'a' && s[0] <= 'z') s[0] -= 32; }
void ui_log(const char *msg, MsgColor c) {
  if (s_msg_until < now_ms()) s_msg[0][0] = s_msg[1][0] = 0;
  if (s_msg[1][0]) { memcpy(s_msg[0], s_msg[1], sizeof s_msg[0]); s_msg_color[0] = s_msg_color[1]; }
  snprintf(s_msg[1], sizeof s_msg[1], "%s", msg); s_msg_color[1] = msg_color(c); cap_first(s_msg[1]);
  s_msg_until = now_ms() + 4000;
}
static void log_clear(void) { s_msg[0][0] = s_msg[1][0] = 0; }
static void add_float(int16_t cell, const char *text, GColor c) {
  if (s_n_float == MAX_FLOAT) memmove(s_float, s_float + 1, sizeof(Floater) * --s_n_float);
  Floater *f = &s_float[s_n_float++];
  f->cell = cell; f->t0 = now_ms(); f->color = c; snprintf(f->text, sizeof f->text, "%s", text); cap_first(f->text);
}
static uint32_t s_boss_until;
void ui_fx(FxKind kind, int16_t cell, int who, const char *text) {
  if (s_proj.active && kind != FX_LEAVE && kind != FX_DESCEND && s_n_qfx < MAX_QFX) {
    QueuedFx *q = &s_qfx[s_n_qfx++];
    q->kind = kind; q->who = who; q->cell = cell; snprintf(q->text, sizeof q->text, "%s", text ? text : "");
    return;
  }
  Vis *v = vis_of(who);
  switch (kind) {
    case FX_ATTACK: if (v->active) vis_anim(v, A_ATTACK); break;
    case FX_DIE: if (v->active) vis_anim(v, A_DIE); break;
    case FX_OPERATE: if (v->active) vis_anim(v, A_OPERATE); break;     // searching, opening, eating, drinking
    case FX_READ: if (v->active) vis_anim(v, A_READ); break;
    case FX_SEARCHED: case FX_QUAKE: s_check_pos = cell; s_check_t0 = now_ms(); s_quake = kind == FX_QUAKE; s_check_r = s_quake ? who : 1; break;   // QUAKE: who = radius
    case FX_JUMP: s_leap_t0 = now_ms() | 1; s_leap_x = s_vis_hero.x; s_leap_y = s_vis_hero.y; break;
    case FX_BOSS_SLAIN: s_boss_until = now_ms() + 5000; ui_sound(SND_BOSS); break;   // GameScene.bossSlain: the banner for 5 s
    case FX_DAMAGE: add_float(cell, text, who < 0 ? GColorOrange : GColorRajah); break;
    case FX_TEXT: case FX_SEARCH: add_float(cell, text, GColorWhite); break;
    case FX_ALERT: if (who >= 0) s_alert_until[who] = now_ms() + 1200; break;
    case FX_HEAL: add_float(cell, text, GColorGreen); break;
    case FX_GOLD: add_float(cell, text, GColorChromeYellow); break;
    case FX_HURT: vibes_enqueue_custom_pattern((VibePattern){ .durations = (uint32_t[]){ 40 }, .num_segments = 1 }); break;
    case FX_DESCEND: vis_reset_all(); break;
    case FX_LEAVE: mob_sheets_free(); break;      // more room for the next floor's generator
    case FX_TELEPORT: if (v->active || who < 0) vis_snap(v, who < 0 ? G.hero.pos : cell); break;
    case FX_SPAWN: if (who >= 0) s_vis_mob[who].active = false; break;
    case FX_THROW: break;
  }
}
char s_info_title[40]; static const char *s_info_desc; int s_info_mob = -1;
// long texts (item info, descriptions) sit in their own clipping layer and scroll by drag or tap
static Layer *s_text_layer;
char *s_text_buf; int s_text_scroll, s_text_h, s_item_text_h, s_item_head = 22;   // head: title height (long names wrap)
void ui_message(const char *title, const char *text) {      // PD WndMessage (signs, "One does not simply leave...")
  snprintf(s_info_title, sizeof s_info_title, "%s", title ? title : "");
  snprintf(s_text_buf, TEXT_BUF, "%s", text ? text : ""); s_info_desc = s_text_buf; s_info_mob = -1;   // texts from txt() rotate: keep a copy
  s_scene = SC_INFO; s_text_scroll = 0;
}
void ui_confirm_jump(void) { s_scene = SC_OPTIONS; s_opt = 0; s_sel = 1; }
char s_wnd_title[48], s_wnd_b[2][36];   // 0.30: the quest window's text shares s_text_buf (never shown with an info window)
const char *g_wnd_title;
void ui_wnd_info(const Item *it) { int n = strlen(s_wnd_text); if (n < TEXT_BUF - 1) item_info(it, s_wnd_text + n, TEXT_BUF - n); }
void ui_wnd(int mob, const char *text, const char *b1, const char *b2) {   // PD WndQuest: a creature speaks, maybe with two choices
  const char *name = mob >= 0 ? mob_name(G.mobs[mob].type) : g_wnd_title ? g_wnd_title : "";
  if (!b1) { ui_message(name, text); s_info_mob = mob; cap_first(s_info_title); return; }
  snprintf(s_wnd_title, sizeof s_wnd_title, "%s", name); cap_first(s_wnd_title);
  if (text != s_wnd_text) snprintf(s_wnd_text, TEXT_BUF, "%s", text);
  snprintf(s_wnd_b[0], sizeof s_wnd_b[0], "%s", b1); snprintf(s_wnd_b[1], sizeof s_wnd_b[1], "%s", b2 ? b2 : "");
  s_scene = SC_OPTIONS; s_opt = 5; s_sel = 0;
}
void ui_throw(int16_t from, int16_t to, int sprite) {
  s_proj = (Proj){ true, (from % MAP_W) * TS, (from / MAP_W) * TS, (to % MAP_W) * TS, (to / MAP_W) * TS, now_ms(), 0, sprite, 0, to };
  int dx = abs(s_proj.x1 - s_proj.x0), dy = abs(s_proj.y1 - s_proj.y0);
  s_proj.ms = ((dx > dy ? dx : dy) / 8 + 1) * 33;           // half a tile per 33 ms
  s_proj.n_heaps = G.n_heaps;                                // heaps made by the throw show once it lands
  s_n_qfx = 0;
}
// a wand's bolt: MagicMissile (a coloured spark with a trail, 200 px/s), or Lightning's arcs, or DeathRay's beam
static int16_t s_arc[20]; static int s_narc;
void ui_zap(int k, int16_t from, int16_t to, const int16_t *pts, int npts) {
  ui_throw(from, to, -1 - k);
  int dx = abs(s_proj.x1 - s_proj.x0), dy = abs(s_proj.y1 - s_proj.y0);
  s_proj.ms = (dx > dy ? dx : dy) * 5 + 40;                  // 16 px a tile: 80 ms a tile
  if (k == IK_WND_LIGHTNING - IK_FIRST_WAND) {
    s_narc = npts > 20 ? 20 : npts; memcpy(s_arc, pts, s_narc * sizeof *pts); s_proj.ms = 300;   // Lightning.DURATION
  } else if (k == IK_WND_DISINTEGRATION - IK_FIRST_WAND) s_proj.ms = 400;
}
static void proj_land(void) {
  s_proj.active = false;
  for (int i = 0; i < s_n_qfx; i++) ui_fx((FxKind)s_qfx[i].kind, s_qfx[i].cell, s_qfx[i].who, s_qfx[i].text);
  s_n_qfx = 0;
}
// AmuletScene (mode 0 with its text, 1 without) and SurfaceScene (2): the game's two endings
int s_amulet; GRect s_btn_rect[2];
void ui_amulet(int mode) {
  s_walking = false; s_amulet = mode; s_sel = 0; s_scene = SC_AMULET;
  if (mode == 2) {                                            // Dungeon.win, deleteGame; only the arcs and the avatar stay
    mreg_drop(); game_art(false); mob_sheets_free(); game_free_fx();
    game_erase(); game_rank(1); s_has_save = false;
    sheet_need(&s_arcs, RESOURCE_ID_IMAGE_ARCS, true); sheet_need(&s_avatars, RESOURCE_ID_IMAGE_AVATARS, true);
  }
}
void ui_pick_item(PickMode mode) {
  if (mode == PICK_RING) { s_opt = 3; s_sel = 0; s_scene = SC_OPTIONS; return; }   // Ring.doEquip's WndOptions: which ring comes off
  s_pick_pending = mode;
}

// ---------------- timing ----------------
static void tick(void *data);
static uint32_t s_last_in;                          // 0.55 (owner): the last button or touch: 10 s without one, the idle animations stop
static void schedule(uint32_t ms) {
  if (ms >= 125 && now_ms() - s_last_in > 10000) { if (s_timer) app_timer_cancel(s_timer); s_timer = NULL; return; }   // (power saving: asleep till the next press)
  if (s_timer && s_timer_ms == ms) return;
  if (s_timer) app_timer_cancel(s_timer);
  s_timer_ms = ms;
  s_timer = app_timer_register(ms, tick, NULL);
}
static uint32_t s_last_tick;
static void busy(void);
static void woke(void) { s_last_in = now_ms(); if (!s_timer) busy(); }
// waking from an idle wait: the first frame counts as one 33 ms step, not the whole wait (that made a step a jump)
static void busy(void) { if (s_timer_ms != 33) s_last_tick = now_ms() - 33; schedule(33); }
static void idle(void) { schedule(s_scene == SC_TITLE || s_scene == SC_SELECT ? 250 : 500); }
static uint32_t anim_wait(void);
// nothing sliding: wake for the next animation frame of anything in sight (at most ~12 redraws a second)
static void idle_anim(void) {
  if (s_scene == SC_TITLE || s_scene == SC_SELECT || s_scene == SC_CONFIRM || s_scene == SC_RANKINGS) { idle(); return; }
  uint32_t w = anim_wait(), lo = s_flames_shown ? 66 : 125;   // flames rise smoothly; other animations redraw at most 8 times a second
  if (s_flames_shown) w = lo;
  schedule(w < lo ? lo : w > 1000 ? 1000 : w);
}
static void text_sync(void); static GRect text_frame(void); static int text_max_scroll(void); static bool text_page(int dir);
void redraw(void) { layer_mark_dirty(s_layer); text_sync(); }

static bool anim_loops(int a) { return a == A_IDLE || a == A_RUN; }
#define CORPSE_MS 3000                                       // MobSprite.FADE_TIME
static uint32_t die_ms(const CharArt *art) { return art->fps[A_DIE] ? art->len[A_DIE] * 1000u / art->fps[A_DIE] : 0; }
static int anim_index(const Vis *v, const CharArt *art) {        // frame number within the sequence
  uint32_t i = (now_ms() - v->t0) * art->fps[v->anim] / 1000;
  return anim_loops(v->anim) ? (int)(i % art->len[v->anim]) : i >= art->len[v->anim] ? art->len[v->anim] : (int)i;
}
static int anim_frame(const Vis *v, const CharArt *art) {
  int i = anim_index(v, art);
  if (i >= art->len[v->anim]) i = art->len[v->anim] - 1;
  return art->seq[v->anim][i];
}
// ms until this sprite shows its next frame (for redraw timing)
static __attribute__((noinline)) uint32_t anim_next(const Vis *v, const CharArt *art) {
  uint32_t fps = art->fps[v->anim], el = now_ms() - v->t0;
  if (!fps) return 1000;
  uint32_t st = 1000 / fps, i = el / st, k = 1, n = art->len[v->anim];
  const uint8_t *q = art->seq[v->anim];
  if (anim_loops(v->anim)) while (k < n && q[(i + k) % n] == q[i % n]) k++;   // 0.35: a frame that repeats the picture is not drawn
  return (i + k) * st - el;
}
// one display tick: slide towards the cell, finish one-shot animations. True while something must keep moving
// sprites slide a tile in MOVE_MS whatever the frame rate: slow frames take bigger steps instead of slowing play
#define MOVE_MS 133
static int s_step = 4;
static bool step_vis(Vis *v, int16_t cell, const CharArt *art, bool hero) {
  int tx = (cell % MAP_W) * TS, ty = (cell / MAP_W) * TS;
  bool moving = false;
  if (v->x != tx || v->y != ty) {
    int dx = tx - v->x, dy = ty - v->y, mv = s_step;
    v->x += dx > mv ? mv : dx < -mv ? -mv : dx;
    v->y += dy > mv ? mv : dy < -mv ? -mv : dy;
    if (v->anim != A_RUN && v->anim != A_DIE) vis_anim(v, A_RUN);          // CharSprite.move
    moving = true;
  } else if (v->anim == A_RUN && hero && !s_walking) vis_anim(v, A_IDLE);  // Hero.ready
  if (v->anim == A_DIE) {                                                 // MobSprite: the body stays, fading over 3 s
    uint32_t el = now_ms() - v->t0;
    if (el >= die_ms(art) + (hero ? 0 : CORPSE_MS)) v->active = false;
    else if (el < die_ms(art)) moving = true;                          // afterwards the frame-timed redraws are enough
  } else if (!anim_loops(v->anim) && anim_index(v, art) >= art->len[v->anim]) vis_anim(v, A_IDLE);   // onComplete
  else if (!anim_loops(v->anim)) moving = true;
  return moving;
}

#define HAND_MS 700
static int hand_btn(void) { int k = game_slot(g_ovr.hs)->kind; return k && k == G.hero.quick[0] ? TB_Q1 : k && k == G.hero.quick[1] ? TB_Q2 : TB_BAG; }   // 0.54: a quickslot item flies from its slot
static uint32_t s_hand_t0; static int s_hand_key = -1;   // 0.51: the hands' item flying from the backpack to the middle tag
static __attribute__((noinline)) void until(uint32_t *w, uint32_t t) { uint32_t n = now_ms(); if (t > n && t - n + 1 < *w) *w = t - n + 1; }
// 0.35: ms until the picture changes (frames that would look the same are not drawn: they cost the speaker too)
static uint32_t anim_wait(void) {
  uint32_t w = G.hero.HP > 0 && G.action == HA_REST ? 500 - now_ms() % 500 : 1000;   // the Zzz bob
  for (int i = 0; i < G.n_mobs; i++) {
    until(&w, s_alert_until[i]);                                      // a ! goes
    if (s_vis_mob[i].active && (G.mobs[i].state == ST_SLEEPING || now_ms() < s_alert_until[i])) until(&w, now_ms() - now_ms() % 500 + 500);
  }
  if (s_msg[1][0]) until(&w, s_msg_until);                            // the log clears
  until(&w, s_boss_until);
  { uint32_t e = now_ms() - s_hand_t0; if (e < HAND_MS) w = HAND_MS - e < 33 ? HAND_MS - e : 33; }   // 0.61 (owner): the last frame lands on the tag
  if (s_vis_hero.active && G.hero.HP > 0) { uint32_t n = anim_next(&s_vis_hero, &HERO_ART); if (n < w) w = n; }
  for (int i = 0; i < G.n_mobs; i++) {
    if (!s_vis_mob[i].active) continue;
    uint32_t n = anim_next(&s_vis_mob[i], mob_art(G.mobs[i].type)); if (n < w) w = n;
  }
  return w;
}
static void after_turn(ActResult r);
static void open_bag(int pick);
static void assign_quickslot(int q, int kind);
static int s_quick_q;                              // the empty quickslot being filled from the backpack
#ifdef PERF_PROBE
uint32_t g_perf[12];                               // count, sum ms, max ms: 0 frames, 3 draw_map, 6 hud art, 9 hud text
uint32_t perf_ms(uint32_t d) { return d > 0x7FFFFFFF ? d + 1000 : d >= 1000 ? d - 1000 : d; }   // (the emulator's clock steps back 1 s at times)
void perf_add(int k, uint32_t t0) { uint32_t d = perf_ms(now_ms() - t0); g_perf[k]++; g_perf[k + 1] += d; if (d > g_perf[k + 2]) g_perf[k + 2] = d; }
#endif
// 0.35: a breather after every frame. The speaker is refilled by a system task that runs below the app, only while the
// app waits: frames slower than the 33 ms step used to follow each other at once, and long glides and drags starved it.
#define MIN_IDLE 8
static uint32_t s_frame_end; extern uint32_t g_idle_at;
static void tick(void *data) {
  s_timer = NULL;
  { uint32_t since = now_ms() - s_frame_end; if (since < MIN_IDLE) { s_timer = app_timer_register(MIN_IDLE - since, tick, NULL); return; } }
  snd_poll();
  { uint32_t t = now_ms(), dt = t - s_last_tick; s_last_tick = t;
    if (dt > 200) dt = 33;                         // first tick after a pause
    s_step = (int)(dt * TS + MOVE_MS / 2) / MOVE_MS; if (s_step < 1) s_step = 1; if (s_step > TS) s_step = TS; }
  bool moving = false;
  if (s_scene != SC_TITLE) {
    uint32_t le = now_ms() - s_leap_t0;
    if (s_leap_t0 && le < 300) {                       // a leap: straight there in 300 ms (the arc is drawn)
      s_vis_hero.x = s_leap_x + ((G.hero.pos % MAP_W) * TS - s_leap_x) * (int)le / 300;
      s_vis_hero.y = s_leap_y + ((G.hero.pos / MAP_W) * TS - s_leap_y) * (int)le / 300; moving = true;
    } else if (s_leap_t0) { s_leap_t0 = 0; vis_snap(&s_vis_hero, G.hero.pos); }
    else if (!s_proj.active) moving |= step_vis(&s_vis_hero, G.hero.pos, &HERO_ART, true);   // in flight, everyone waits (PD: hero.busy)
    for (int i = 0; i < G.n_mobs && !s_proj.active; i++) {
      Vis *v = &s_vis_mob[i];
      if (!v->active) continue;
      if (G.mobs[i].HP <= 0 && v->anim != A_DIE && !s_proj.active) { v->active = false; continue; }
      if (G.mobs[i].state == ST_SLEEPING && v->anim == A_RUN) vis_anim(v, A_IDLE);   // Sleep: sprite.idle()
      moving |= step_vis(v, G.mobs[i].pos, mob_art(G.mobs[i].type), false);
    }
    for (int i = 0; i < s_n_float; ) { if (now_ms() - s_float[i].t0 >= FLOAT_MS) s_float[i] = s_float[--s_n_float]; else i++; }
    if (s_n_float) moving = true;
    if (s_check_pos >= 0 && now_ms() - s_check_t0 < 850) moving = true;
    if (glide_f() < 256) moving = true;               // the camera's swoop
    if (s_fade) {
      moving = true;
      if (now_ms() - s_fade_t0 >= FADE_MS) {
        if (s_fade == 1) { game_art(false); ui_free_cache(0); ui_win_free(); game_level_commit(); game_art(true); ui_hero_tier(); s_fade = 2; s_fade_t0 = now_ms(); after_turn(ACT_LEVEL); }   // new floor (pictures out of the way), then save
        else s_fade = 0;
      }
    }
    if (s_proj.active) { if (now_ms() - s_proj.t0 >= s_proj.ms) proj_land(); moving = true; }
    bool hero_arrived = s_vis_hero.x == (G.hero.pos % MAP_W) * TS && s_vis_hero.y == (G.hero.pos / MAP_W) * TS;
    if (s_walking && hero_arrived && s_scene == SC_GAME) {
      ActResult r = game_continue();
      after_turn(r);
      if (r != ACT_WALK) s_walking = false;
      else step_vis(&s_vis_hero, G.hero.pos, &HERO_ART, true);   // start the next tween this frame: no pause between tiles
    }
    if (s_walking && s_scene != SC_GAME) s_walking = false;
    if (s_walking) moving = true;
    if (s_msg[1][0] && now_ms() > s_msg_until) log_clear();
  }
  bool win = s_scene != SC_GAME && s_scene != SC_TARGET && s_scene != SC_INSPECT && s_scene != SC_DEAD && !FRONT(s_scene);
  if (!win || moving) redraw();                      // 0.28: a window over the game redraws for input only (a full bag ground the watch down)
  if (moving) busy(); else if (win) idle(); else idle_anim();
}

// ---------------- game flow ----------------
void to_title(void) {
  snd_music(MUS_THEME);
  mob_sheets_free();
  front_art(true);
  s_scene = SC_TITLE; s_sel = 0;
  s_has_save = game_peek_save(&s_save_depth, &s_save_lvl);
  redraw(); idle();
}
void open_select(void) {
  front_art(true);
  s_has_save = game_peek_save(&s_save_depth, &s_save_lvl);
  s_scene = SC_SELECT; s_sel = 0; redraw(); idle();
}
static int32_t s_saved_at;
void start_game(bool cont) {
  g_new_class = s_sel;
  ui_win_free();
  front_art(false);                                // the floor is built with no pictures loaded: the generator needs the room
  log_clear();
  s_scene = SC_GAME; s_sel = 0;
  snd_hold(true);                                  // the title music's engine and buffers would split the heap
  if (!(cont && game_load())) { game_new(); game_save(); }
  snd_hold(false);
  game_art(true);
  snd_music(MUS_GAME);
  ui_hero_tier();
  s_saved_at = G.now;
  vis_reset_all();
  redraw(); idle();
}
static void after_turn(ActResult r) {
  if (r == ACT_LEVEL && game_level_pending() && G.hero.HP > 0) {   // stairs or a fall: fade out first
    s_walking = false; s_fade = 1; s_fade_t0 = now_ms(); busy();
    return;
  }
  if (G.hero.HP <= 0 && s_scene != SC_DEAD) {
    s_walking = false; s_has_save = false;
    if (game_bag_find(IK_ANKH) >= 0) { persist_delete(1); game_ankh(); return; }   // Hero.die: an Ankh offers another chance (the floors are kept)
    game_erase(); game_rank(0);                                              // Dungeon.fail
    vibes_long_pulse();
    s_scene = SC_DEAD; s_died_ms = now_ms();
    return;
  }
  // flash writes are slow: save on new floors and every 20 turns, not every step
  if (r == ACT_LEVEL || (r != ACT_NONE && G.now - s_saved_at >= 20 * TICK)) { game_save(); s_saved_at = G.now; }
}
static void act(ActResult r) {
  if (r != ACT_NONE) cam_center();
  if (r == ACT_WALK) { after_turn(r); if (s_scene == SC_GAME) s_walking = true; busy(); return; }
  if (r != ACT_NONE) after_turn(r);
  if (s_pick_pending >= 0) { int p = s_pick_pending; s_pick_pending = -1; if (s_scene != SC_DEAD) open_bag(p); }
  redraw();
  busy();
}


// ---------------- windows ----------------
// PD WndGame. While the hero is dead it offers a new game instead
// PD WndSettings' Music and Sound FX switches sit in this menu
static void menu_select(int a) {                   // the game menu's buttons (uiw overlay), PD WndGame / WndSettings
  ui_sound(SND_CLICK);
  switch (a) {
    case A_SAVE: ui_win_free(); game_save(); to_title(); return;   // the window's code out of the way of the save's
    case A_CATALOG: case A_JOURNAL: s_scene = SC_CATALOG; s_sheet_tab = a == A_JOURNAL ? 2 : 0; s_sel = 0; break;   // WndCatalogus / WndJournal
    case A_SOUND: { int m = g_sound & 3; snd_set((g_sound & ~3) | (m == 3 ? SOUND_FX : m == SOUND_FX ? 0 : 3)); break; }   // 0.80 (owner): on, effects only, mute (stays open)
    case A_VOL: snd_set((g_sound & 3) | ((SOUND_VOL(g_sound) + 1) % 3) << 2); ui_sound(SND_CLICK); break;   // (a long press: loud, medium, quiet)
    case A_ASSIST: g_assist ^= 1; zoom_save(); break; case A_ITEMS: g_assist ^= 2; zoom_save(); break; case A_OFFCAM: g_assist ^= 4; zoom_save(); break;
    case A_NEW: start_game(false); return;
    case A_RANKINGS: case A_MAIN: to_title(); if (a == A_MAIN) return; s_scene = SC_RANKINGS; break;
    default: s_scene = SC_GAME;
  }
  redraw();
}

// PD WndBag, 0.18: a 5x5 grid of big cells (src/ovl/bag). Row 0 = weapon, armor, two rings, gold; then the
// backpack's 19 slots; Exit in the bottom-right cell. Tabs under the window: backpack, the bags, keyring.
// UI index: 0 weapon, 1 armor, 2-3 rings, 4 gold, 5..23 backpack slots, 24 Exit; 25.. tabs
int s_bag_tab;                              // the open tab (bag_tab_kind: 0 backpack, 1-3 seed pouch / scroll holder / wand holster, 4 keyring)
static bool own_bags(void) { return game_bag_find(IK_SEED_POUCH) >= 0 || game_bag_find(IK_SCROLL_HOLDER) >= 0 || game_bag_find(IK_WAND_HOLSTER) >= 0; }
int bag_ntabs(void) { return s_pick >= 0 && !own_bags() ? 0 : 2 + (game_bag_find(IK_SEED_POUCH) >= 0) + (game_bag_find(IK_SCROLL_HOLDER) >= 0) + (game_bag_find(IK_WAND_HOLSTER) >= 0); }
int bag_tab_kind(int t) {
  for (int k = 0, n = 0; k < 5; k++) if (k == 0 || k == 4 || game_bag_find(IK_SEED_POUCH + k - 1) >= 0) { if (n++ == t) return k; }
  return 0;
}
// the backpack covers the toolbar now
static bool toolbar_hidden(void) { return s_scene == SC_BAG; }
int bag_ui_to_slot(int i) {                 // game slot (-1 weapon, -2 armour, -3 / -4 rings), or SL_KEY / SL_GOLD / SL_EXIT / SL_NONE
  int k = bag_tab_kind(s_bag_tab);
  static const uint8_t O[4] = { 0, 8, 20, 32 };     // the bags' sizes: 8, 12, 12
  if (i == BAG_EXIT) return SL_EXIT;
  if (k >= 1 && k <= 3) return i < O[k] - O[k - 1] ? PACK0 + O[k - 1] + i : SL_NONE;
  if (k == 4) return i < 12 ? SL_KEY : SL_NONE;     // Keyring.size = 12
  if (i < 4) return -1 - i;                         // the top row: weapon, armour, two rings, gold
  if (i == 4) return SL_GOLD;
  return i - 5 < BAG_SIZE ? i - 5 : SL_NONE;
}
Item *bag_item(int i) { int sl = bag_ui_to_slot(i); return sl >= -4 ? game_slot(sl) : NULL; }
bool bag_selectable(int i) {
  if (i >= BAG_UI_TAB0) return i - BAG_UI_TAB0 < bag_ntabs();
  int sl = bag_ui_to_slot(i);
  if (s_pick < 0 || sl == SL_EXIT) return sl != SL_NONE;
  if (s_pick == PICK_QUICK) return sl >= 0 && item_quickable(game_slot(sl)->kind);
  return sl >= -4 && game_pick_ok((PickMode)s_pick, sl);
}
static void act(ActResult r);
static void open_bag(int pick) {                   // pick: -1 plain backpack, else GameScene.selectItem mode
  mreg_spare();                                    // the window and its pictures need the room (no turns pass while it's open)
  s_pick = pick; s_scene = SC_BAG; s_sel = BAG_ROWM + 5; s_bag_tab = pick == PICK_SEED && game_bag_find(IK_SEED_POUCH) >= 0;   // GameScene.selectItem: seeds open the pouch
  if (pick >= 0) {
    s_sel = BAG_ROWM + BAG_EXIT;                   // nothing fits: the window still opens, all greyed (PD)
    for (int i = 0; i < BAG_UI_TAB0; i++) if (bag_selectable(i)) { s_sel = BAG_ROWM + i; break; }   // 0.42: its row
    { int k = G.tgm.pk - 8, u = k < 0 ? -1 - k : k + 5; G.tgm.pk = 0; if (k >= -4 && k < BAG_SIZE && bag_selectable(u)) s_sel = BAG_ROWM + u; }   // 0.64: the hands' pick (Assist High)
  }
  redraw();
}
static void bag_close(void) {                      // a selector closed without a choice: WndBag.onBackPressed -> listener(null)
  s_scene = G.hero.HP > 0 ? SC_GAME : SC_DEAD;
  int p = s_pick; s_pick = -1;
  if (p >= 0 && p != PICK_QUICK) act(game_pick((PickMode)p, -1000)); else redraw();
}
static void open_item(int ui_index) {
  ui_win_free();                                   // the info / use overlays need the room; the window's code reloads on redraw
  Item *it = bag_item(ui_index);
  if (!it || it->kind == IK_NONE) return;
  int slot = bag_ui_to_slot(ui_index);
  if (s_pick >= 0) {
    if (!bag_selectable(ui_index)) return;
    int p = s_pick; s_pick = -1; s_scene = SC_GAME;
    if (p == PICK_QUICK) { assign_quickslot(s_quick_q, it->kind); redraw(); return; }
    act(game_pick((PickMode)p, slot)); return;
  }
  s_item_slot = slot;
  s_n_acts = G.hero.HP > 0 ? game_item_actions(slot, s_acts) : 0;   // after death: only a look
  item_info(it, s_text_buf, TEXT_BUF);
  s_text_scroll = 0; s_sel = 0; s_scene = SC_ITEM;
#ifndef PBL_ROUND                                    // Gabbro: the bag overlay lays the window out
  char name[40]; item_name(it, name, sizeof name); cap_first(name);   // IconTitle: the name wraps beside the icon
  int th = graphics_text_layout_get_content_size(name, s_font_hud, GRect(0, 0, BIG_W - 40, 60), GTextOverflowModeWordWrap, GTextAlignmentLeft).h;
  s_item_head = th + 6 > 36 ? th + 6 : 36;          // the icon at 2x beside the name
  int maxh = s_h - PBL_IF_ROUND_ELSE(64, 18) - s_item_head - 4 - item_rows() * (SLOT_H + 2);
  int ch = graphics_text_layout_get_content_size(s_text_buf, s_font_log, GRect(0, 0, BIG_W - 16, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft).h + 4;
  s_item_text_h = ch < maxh ? ch : maxh;
#endif
  redraw();
}
int item_rows(void) { return (s_n_acts + 1) / 2; }
#ifndef PBL_ROUND
GRect item_rect(void) { int h = s_item_head + s_item_text_h + 2 + item_rows() * (SLOT_H + 2); return GRect((s_w - BIG_W) / 2, (s_h - h) / 2, BIG_W, h); }
#endif
static uint8_t s_zap;                              // what the target scene aims: 0 a throw, 1 a wand, 2 the warrior's leap
static void item_do_confirmed(ItemAction a) {
  if (a == ACN_SPECIAL && (G.hero.armor.kind == IK_ARMOR_MAGE || G.hero.armor.kind == IK_ARMOR_HUNTRESS)) { s_scene = SC_GAME; act(game_aim(2, 0, -1)); return; }   // no aiming
  if (a == ACN_THROW || a == ACN_ZAP || a == ACN_SPECIAL) { s_zap = a == ACN_ZAP ? 1 : a == ACN_SPECIAL ? 2 : 0; s_aim_q = -1; s_aim_mob = game_auto_target(); s_scene = SC_TARGET; redraw(); return; }
  s_scene = SC_GAME;
  act(game_item_do(s_item_slot, a));
}
static void item_do(int a) {
  ItemAction ac = (ItemAction)s_acts[a];
  ui_win_free();
  ui_sound(SND_CLICK);
  if (ac == ACN_QUICK) { s_opt = 4; s_sel = G.hero.quick[0] == IK_NONE || G.hero.quick[1] != IK_NONE ? 0 : 1; s_scene = SC_OPTIONS; redraw(); return; }   // which slot?
  int c = game_confirm_needed(s_item_slot, ac);
  if (c) { s_opt = c; s_sel = 1; s_scene = SC_OPTIONS; redraw(); return; }
  item_do_confirmed(ac);
}
static void throw_at(int16_t cell) {               // also zaps, when aiming a wand
  ui_win_free(); s_scene = SC_GAME; s_aim_q = s_aim_mob = -1;
  act(game_aim(s_zap, s_item_slot, cell));
}
// PD QuickSlot: use the item of that kind (throwables aim at the nearest enemy)
static void quickslot(int q) {
  int kind = G.hero.quick[q];
#ifdef DEBUG_TOOLS
  APP_LOG(APP_LOG_LEVEL_DEBUG, "quickslot q%d kind %d long %d", q, kind, s_long_press);
#endif
  if (kind == IK_NONE) { s_quick_q = q; open_bag(PICK_QUICK); return; }      // an empty slot: choose what goes in it
  int slot = game_bag_find(kind);
  if (slot < 0) { Item it = { .qty = 1, .kind = kind }; char n[40]; item_name(&it, n, sizeof n); char m[64]; snprintf(m, sizeof m, "You have no %s.", n); ui_log(m, C_INFO); redraw(); return; }
  s_item_slot = slot;
  if (item_cat(kind) == CAT_MISSILE || item_cat(kind) == CAT_WAND) {   // QuickSlot: the first tap aims (reticle), the second throws / zaps
    if (s_scene == SC_TARGET && s_aim_q == q && s_aim_mob >= 0 && G.mobs[s_aim_mob].HP > 0) { throw_at(G.mobs[s_aim_mob].pos); return; }
    s_zap = item_cat(kind) == CAT_WAND;
    s_aim_q = q; s_aim_mob = game_auto_target();
    s_scene = SC_TARGET; redraw(); return;
  }
  uint8_t acts[ACN_COUNT];
  game_item_actions(slot, acts);
  int c = game_confirm_needed(slot, (ItemAction)acts[0]);
  if (c) { s_opt = c; s_sel = 1; s_scene = SC_OPTIONS; redraw(); return; }
  item_do_confirmed((ItemAction)acts[0]);
}
static void assign_quickslot(int q, int kind) {
  Hero *h = &G.hero;
#ifdef DEBUG_TOOLS
  APP_LOG(APP_LOG_LEVEL_DEBUG, "assign q%d kind %d", q, kind);
#endif
  if (h->quick[1 - q] == kind) h->quick[1 - q] = IK_NONE;   // one slot per kind
  h->quick[q] = kind;
}

// ---------------- input ----------------
// title: PD DashboardItem "Play" (staircase); hero select: PD StartScene 2x2 class grid
#ifdef TEST_BUILD
bool huntress_unlocked(void) { return true; }                          // the test kit: every class
#else
bool huntress_unlocked(void) { return persist_exists(KEY_HUNTRESS); }
#endif   // Badges.BOSS_SLAIN_3
// PD WndHero: a tabbed window (Stats, Buffs)
// tabs are small: accept taps a little below and around them
// PD WndOptions with two choices: Chasm.heroJump, and Potion's harmful-drink / beneficial-throw warnings
static void item_do_confirmed(ItemAction a);
static void jump_choose(int i) {
  ui_sound(SND_CLICK); ui_win_free();
  s_scene = SC_GAME;
  if (s_opt == 3) { act(game_pick(PICK_RING, i == 0 ? -3 : -4)); return; }
  if (s_opt == 4) { assign_quickslot(i, game_slot(s_item_slot)->kind); redraw(); return; }
  if (s_opt == 5) { act(game_wnd_choice(i)); return; }
  if (i != 0) { redraw(); return; }
  if (s_opt == 0) act(game_jump());
  else item_do_confirmed(s_opt == 1 ? ACN_DRINK : s_opt == 6 ? ACN_EQUIP : ACN_THROW);
}
static void open_info(int16_t cell) {
  ui_win_free(); mreg_spare(); game_inspect(cell, s_info_title, sizeof s_info_title, s_text_buf, TEXT_BUF); s_info_desc = s_text_buf; cap_first(s_info_title);
  s_info_mob = BIT_GET(G.fov, cell) ? mob_at(cell) : -1;
  s_scene = SC_INFO; s_text_scroll = 0; redraw();
}
static bool in_rect(GRect r, int x, int y) { return x >= r.origin.x && x < r.origin.x + r.size.w && y >= r.origin.y && y < r.origin.y + r.size.h; }
// When the camera is panned away and the hero is off screen, PD-style portrait at the edge
// pointing towards the hero; tapping it recenters.
static bool hero_pointer(GRect *out) {
  if (!cam_free() || s_scene != SC_GAME) return false;
  int T = tile_px();
  int hx = s_w / 2 + (s_vis_hero.x - cam_x()) * T / TS + T / 2, hy = map_center_y() + (s_vis_hero.y - cam_y()) * T / TS + T / 2;
  int x0 = PBL_IF_ROUND_ELSE(58, 16), x1 = s_w - x0, y1 = toolbar_y() - 16;
  int y0 = PBL_IF_ROUND_ELSE(58, (hx < status_x() + SC(35) + 8 ? status_bottom() : status_y() + SC(10)) + 8);   // only the portrait hangs low
  if (hx >= x0 - 8 && hx <= x1 + 8 && hy >= y0 - 8 && hy <= y1 + 8) return false;
  int px = hx < x0 ? x0 : hx > x1 ? x1 : hx, py = hy < y0 ? y0 : hy > y1 ? y1 : hy;
  *out = GRect(px - 14, py - 14, 28, 28);
  return true;
}
static int16_t screen_to_cell(int x, int y) {
  int T = tile_px();
  int wx = (x - s_w / 2) * TS / T + cam_x() + TS / 2, wy = (y - map_center_y()) * TS / T + cam_y() + TS / 2;
  if (wx < 0 || wy < 0) return -1;
  int cxx = wx / TS, cyy = wy / TS;
  if (cxx >= MAP_W || cyy >= MAP_H) return -1;
  return cyy * MAP_W + cxx;
}

uint8_t g_assist = ASSIST_NEW;
static void zoom_save(void) { persist_write_int(KEY_ZOOM, s_zoom | (g_assist | 8) << 4); }   // 0.51: the assist rides along (0.80: its switches, bit 7: that form)
static void zoom_cycle(void) { s_zoom = (s_zoom + 1) % 3; zoom_save(); redraw(); }   // combat: Up
static void combat_cycle(int dir) {                // Down: the next target (0.26: long press, the one before), the camera on it
  int c;
  if (s_scene == SC_TARGET) {
    int n, i = game_target(combat_target(), true, &n);
    for (int k = 2; dir < 0 && k < n; k++) i = game_target(i, true, NULL);   // back: forward all the way round but one
    if (i < 0) return;
    s_aim_mob = i; G.quick_uid = G.mobs[i].uid; c = G.mobs[i].pos;
  } else { tg_refresh(dir); if (!AS_OFFCAM) cam_center(); busy(); return; }   // 0.36: the targets are on screen: the camera stays on the hero (Max: tg_refresh swoops it there)
  cam_glide();
  pan_to((c % MAP_W - G.hero.pos % MAP_W) * TS, (c / MAP_W - G.hero.pos / MAP_W) * TS);
  busy();
}
static bool combat_tap(int x, int y) {             // the zoom and next-target tags
  if (!combat()) return false;
  if (in_rect(ctag(0), x, y)) { zoom_cycle(); return true; }
  if (cycle_shown() && in_rect(ctag(2), x, y)) { combat_cycle(1); return true; }
  return false;
}
static void on_tap(int x, int y) {
  if (s_fade) return;
  s_hl = -1;
  if (OWN_INPUT(s_scene)) { ui_input(s_long_press ? -2 : -1, x, y); return; }   // the windows' own code (-2: a press and hold)
  switch (s_scene) {
    case SC_INFO: if (in_rect(text_frame(), x, y) && text_page(1)) return; s_scene = SC_GAME; redraw(); return;
    case SC_INSPECT: { int16_t c = screen_to_cell(x, y); if (c == G.hero.pos) { s_scene = SC_SHEET; s_sheet_tab = 0; redraw(); } else if (c >= 0) open_info(c); else { s_scene = SC_GAME; redraw(); } return; }   // yourself: WndHero
    case SC_DEAD:
      if (PBL_IF_ROUND_ELSE(tb_in(x, y) == TB_BAG, y >= toolbar_y() && tb_hit(x) == TB_BAG)) { open_bag(-1); return; }   // the backpack still opens: everything in it is known now
      if (now_ms() - s_died_ms > 1000) { s_scene = SC_MENU; s_sel = 0; redraw(); } return;   // ignore taps that were meant for the fight
    case SC_TARGET: {
      if (combat_tap(x, y)) return;
      if (combat_target() >= 0 && in_rect(ctag(1), x, y)) { throw_at(G.mobs[s_aim_mob].pos); return; }   // the attack tag: fire at the target
      if (TB_ROW(x, y)) {
        int b = TB_HIT(x, y);
        if ((b == TB_Q1 || b == TB_Q2) && (b == TB_Q1 ? 0 : 1) == s_aim_q) { quickslot(s_aim_q); return; }
        s_scene = SC_GAME; s_aim_q = s_aim_mob = -1; redraw(); return;           // GameScene.cancel
      }
      int16_t c = screen_to_cell(x, y);
      if (c >= 0 && c != G.hero.pos) throw_at(c);
      return;
    }
    case SC_GAME: break;
    default: return;                                 // the front screens: ui_input
  }
#ifdef DEBUG_JUMP
  void game_debug_map(void);                         // test kit (0.26: was long press Down): a tap on the depth counter
  if (y < 20 && x >= 110 && x < 165) { game_debug_map(); if (game_level_pending()) act(ACT_LEVEL); else redraw(); return; }
#endif
  GRect hp; if (hero_pointer(&hp) && in_rect(GRect(hp.origin.x - 6, hp.origin.y - 6, hp.size.w + 12, hp.size.h + 12), x, y)) { cam_center(); redraw(); return; }   // 0.79 (owner): the portrait at the edge first, a finger's width round it
  GRect mb = menu_button_rect();
  if (in_rect(GRect(mb.origin.x - 8, mb.origin.y - 4, mb.size.w + 12, mb.size.h + 12), x, y)) { s_walking = false; mreg_spare(); s_scene = SC_MENU; s_sel = 0; redraw(); return; }   // (0.79: room for its pictures, as long Up)
  if (in_rect(GRect(status_x(), status_y(), SC(35), SC(35)), x, y)) { s_walking = false; s_scene = SC_SHEET; s_sheet_tab = 0; redraw(); return; }   // portrait: character sheet
  if (y < PBL_IF_ROUND_ELSE(CAP_H, status_y() + SC(10))) return;               // the health bar strip; below it (beside the portrait) is map
  if (TB_ROW(x, y)) {
    s_walking = false;
    switch (TB_HIT(x, y)) {
      case TB_WAIT: act(game_rest(s_long_press)); return;      // long press: rest until healed
      case TB_SEARCH: act(game_search()); return;
      case TB_INFO: s_scene = SC_INSPECT; redraw(); return;
      case TB_BAG: if (s_long_press) { s_scene = SC_CATALOG; s_sheet_tab = 0; s_sel = 0; redraw(); } else open_bag(-1); return;   // long press: WndCatalogus
      case TB_Q2: case TB_Q1: {
        int q = TB_HIT(x, y) == TB_Q1 ? 0 : 1;
        if (s_long_press) { s_quick_q = q; open_bag(PICK_QUICK); } else quickslot(q);   // QuickSlot.onLongClick: choose what goes in it
        return;
      }
      default: return;
    }
  }
  if (loot_shown() && in_rect(tag_rect(1), x, y)) { act(game_tap_cell(G.hero.pos)); return; }   // LootIndicator: hero.handle(hero.pos), even with an enemy beside
  if (resume_shown() && in_rect(tag_rect(2), x, y)) { act(game_resume()); return; }
  if (combat_tap(x, y)) return;
  if (s_tg >= 0 && in_rect(ctag(1), x, y)) { s_walking = false; act(tg_do()); return; }   // the middle tag
  s_walking = false;
  int16_t c = screen_to_cell(x, y);
  if (c >= 0) { if (c != s_tg0) pass_hands(c); g_ovr.c = c; g_ovr.t = G.hero.pos - 65536; act(game_tap_cell(c)); }   // (0.63 (owner): a tap passes over the hands too)   // 0.43: the player's own way (ovl/tgt), as Select on another target; yourself: Hero.handle(pos): the stairs, things underfoot, a sign (else nothing, no turn)
}

static int16_t s_touch_x, s_touch_y, s_touch_moved; static bool s_touching; static uint32_t s_touch_ms;
// 0.29, the backpack: holding a finger on an item picks it up at once (no need to let go); letting go elsewhere puts it there
static AppTimer *s_hold_tm; static bool s_held;
static void hold_fire(void *d) { s_hold_tm = NULL; if (s_touching && s_touch_moved < 40 && s_scene == SC_BAG) { s_held = true; ui_input(-2, s_touch_x, s_touch_y); } }
static int s_text_drag, s_text_scroll0;
// A fingertip on a small screen drifts while tapping: only a long slide counts as a drag (taps were lost at 14 px)
#define TAP_SLOP 40
#define PAN_START 24                                // a slide this long on the map starts panning
// keep the camera's centre over the dungeon
static void pan_to(int dx, int dy) {
  int x = s_vis_hero.x + dx, y = s_vis_hero.y + dy;
  x = x < 0 ? 0 : x > (MAP_W - 1) * TS ? (MAP_W - 1) * TS : x;
  y = y < 0 ? 0 : y > (MAP_H - 1) * TS ? (MAP_H - 1) * TS : y;
  s_cam_dx = x - s_vis_hero.x; s_cam_dy = y - s_vis_hero.y;
}
static void touch_handler(const TouchEvent *e, void *ctx) { woke();
  switch (e->type) {
    case TouchEvent_Touchdown:
      s_touching = true; s_touch_x = e->x; s_touch_y = e->y; s_touch_moved = 0; s_touch_ms = now_ms();
      s_held = false; if (s_scene == SC_BAG && !s_hold_tm) s_hold_tm = app_timer_register(500, hold_fire, NULL);
      s_text_drag = (s_scene == SC_ITEM || s_scene == SC_INFO) && in_rect(text_frame(), e->x, e->y) && text_max_scroll() ? 1 : 0;
      s_text_scroll0 = s_text_scroll;
      break;
    case TouchEvent_PositionUpdate: {
      int m = abs(e->x - s_touch_x) + abs(e->y - s_touch_y);
      if (m > s_touch_moved) s_touch_moved = m;
      if (s_text_drag && abs(e->y - s_touch_y) >= 8) { s_text_drag = 2; s_text_scroll = s_text_scroll0 - (e->y - s_touch_y); redraw(); break; }
      if (s_touching && s_scene == SC_GAME && !s_panning && m >= PAN_START && s_touch_y > status_bottom() && s_touch_y < toolbar_y()) {
        s_panning = true; s_walking = false; s_pan_dx0 = cam_ox(); s_pan_dy0 = cam_oy(); s_glide_t0 = 0;   // a drag takes over from a swoop
      }
      if (s_panning) { pan_to(s_pan_dx0 - (e->x - s_touch_x) * TS / tile_px(), s_pan_dy0 - (e->y - s_touch_y) * TS / tile_px()); busy(); }   // 0.35: drawn by the next frame, not once per touch report
      break;
    }
    case TouchEvent_Liftoff:
      s_long_press = s_touching && now_ms() - s_touch_ms >= 500;
#ifdef DEBUG_TOOLS
      APP_LOG(APP_LOG_LEVEL_DEBUG, "liftoff %d,%d touching %d held %d moved %d", e->x, e->y, s_touching, (int)(now_ms() - s_touch_ms), s_touch_moved);
#endif
      if (s_hold_tm) { app_timer_cancel(s_hold_tm); s_hold_tm = NULL; }
      if (s_held) { s_held = s_touching = false; if (s_touch_moved >= TAP_SLOP) ui_input(-1, e->x, e->y); break; }   // dragged there: put it down
      if (s_panning) { s_panning = false; s_touching = false; break; }   // a drag pans, it never taps
      if (s_text_drag == 2) { s_text_drag = 0; s_touching = false; break; }       // a drag scrolls text
      s_text_drag = 0;
      if (s_scene == SC_GAME && s_hl >= 0 && (!s_touching || s_touch_moved < TAP_SLOP)) { s_hl = -1; redraw(); }   // 0.54 (owner): a tap leaves the bar's Back selection, nothing more
      else if (!s_touching) on_tap(e->x, e->y);            // a quick tap can arrive as a lone liftoff
      else if (s_touch_moved < TAP_SLOP) on_tap(s_touch_x, s_touch_y);
      s_touching = false; s_long_press = false;
      break;
  }
}

static void hl_press(bool held) {                 // a tap on the highlighted button (on_tap drops the highlight)
  GRect r = hl_rect(s_hl);
  s_long_press = held; on_tap(r.origin.x + r.size.w / 2, r.origin.y + r.size.h / 2); s_long_press = false;
}
static void move_sel(int dir, int n) { s_sel = (s_sel + dir + n) % n; redraw(); }
static void button(ButtonId id) { woke();
  if (s_fade) return;
  if (OWN_INPUT(s_scene)) { ui_input(id, 0, 0); return; }
  int dir = id == BUTTON_ID_DOWN ? 1 : -1;
  switch (s_scene) {
    case SC_INFO: if (id == BUTTON_ID_SELECT || !text_page(dir)) { s_scene = SC_GAME; redraw(); } return;
    case SC_INSPECT: {
      if (id == BUTTON_ID_SELECT) open_info(s_tg >= 0 ? s_tg : G.hero.pos);   // the buttons' target
      return;
    }
    case SC_DEAD: s_scene = SC_MENU; s_sel = 0; redraw(); return;
    case SC_TARGET:
      if (id == BUTTON_ID_SELECT) {
        int t = s_aim_mob >= 0 && G.mobs[s_aim_mob].HP > 0 ? G.mobs[s_aim_mob].pos : game_nearest_enemy();
        if (t >= 0) throw_at(t); else { ui_log(txt(TX_L_NO_ENEMY), C_INFO); redraw(); }
      }
      else if (id == BUTTON_ID_UP) zoom_cycle(); else combat_cycle(1);
      return;
    case SC_GAME: break;
    default: return;                                 // the front screens: ui_input
  }
  if (s_walking) { s_walking = false; return; }
  if (s_hl >= 0) {                                   // 0.42: Select presses it, Up the one before (or none), Down: none
    if (id == BUTTON_ID_SELECT) hl_press(false); else { s_hl = id != BUTTON_ID_UP ? (!AS_ON && s_hl < 7 ? s_hl + 1 : -1) : AS_ITEMS ? (s_hlp ? g_hl_order[--s_hlp] : -1) : s_hl - 1; redraw(); }
    return;
  }
  if (id == BUTTON_ID_UP) zoom_cycle();              // 0.23: Up zooms, Down picks the next target, Select acts on it
  else if (id == BUTTON_ID_DOWN) { if (!AS_ON && !s_tgn) { s_hl = 0; redraw(); } else combat_cycle(1); }   // 0.75 Off: no enemy: the buttons
  else if (s_tg >= 0) { ActResult r = tg_do(); if (r == ACT_NONE) { tg_refresh(1); s_tgi = 0; } act(r); }   // nothing came of it: the next one (0.36: rooted: wait it out)
  else if (cam_free()) { cam_center(); redraw(); }
  else act(G.hero.pos == G.exit || heap_at(G.hero.pos) >= 0 ? game_tap_cell(G.hero.pos) : game_search());   // nothing in view: the stairs or things underfoot, else search (a closed room hides a door)
}
static void click_up(ClickRecognizerRef r, void *c) { button(BUTTON_ID_UP); }
static void click_sel(ClickRecognizerRef r, void *c) { button(BUTTON_ID_SELECT); }
static void click_down(ClickRecognizerRef r, void *c) { button(BUTTON_ID_DOWN); }
static void long_sel(ClickRecognizerRef r, void *c) { woke(); if (s_scene == SC_GAME && s_hl >= 0) hl_press(true); else if (s_scene == SC_GAME) { s_walking = false; open_bag(-1); } else if (s_scene == SC_BAG || s_scene == SC_MENU) ui_input(-3, 0, 0); }   // the bag: move an item (0.80: the menu: Sound's volume)
static void long_down(ClickRecognizerRef r, void *c) { woke(); if (s_scene == SC_GAME || s_scene == SC_TARGET) { s_walking = false; combat_cycle(-1); } }   // 0.26: the target before
static void long_up(ClickRecognizerRef r, void *c) { woke(); if (s_scene == SC_GAME) { s_walking = false; mreg_spare(); s_scene = SC_MENU; s_sel = 0; redraw(); } }
static void click_back(ClickRecognizerRef r, void *c) { woke();
  if (s_scene == SC_OPTIONS && G.hero.HP <= 0) return;   // WndResurrect: no way out but its buttons
  if (s_scene != SC_TITLE && OWN_INPUT(s_scene)) { ui_input(BUTTON_ID_BACK, 0, 0); return; }
  switch (s_scene) {
    case SC_TARGET: case SC_INSPECT: case SC_INFO: s_scene = SC_GAME; s_aim_q = s_aim_mob = -1; redraw(); return;
    case SC_DEAD: to_title(); return;
    case SC_GAME: s_walking = false; if (s_tgi) tg_refresh(-s_tgi);
      if (AS_ITEMS) { s_hlp = s_hl < 0 ? 0 : s_hlp + 1; s_hl = s_hlp < 8 ? g_hl_order[s_hlp] : -1; } else s_hl = s_hl == 7 ? -1 : s_hl + 1;   // 0.54 (owner): High: in the bot's order
      redraw(); return;   // 0.42: the buttons, one by one (0.47 (owner): and the plan back to A)
    default: break;
  }
  window_stack_pop(true);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, click_up);
  window_single_click_subscribe(BUTTON_ID_SELECT, click_sel);
  window_single_click_subscribe(BUTTON_ID_DOWN, click_down);
  window_single_click_subscribe(BUTTON_ID_BACK, click_back);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, long_sel, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 500, long_up, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, long_down, NULL);
}

// ---------------- drawing ----------------
void text(GContext *ctx, const char *s, GFont f, GColor c, int x, int y, int w, GTextAlignment a) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, GRect(x, y, w, 30), GTextOverflowModeTrailingEllipsis, a, NULL);
}
void text_wrap(GContext *ctx, const char *s, GFont f, GColor c, GRect r) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}
void text_wrap_c(GContext *ctx, const char *s, GFont f, GColor c, GRect r) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}
// PD's pixel fonts carry a black outline; draw the text 8 times in black around it
void text_outlined(GContext *ctx, const char *s, GFont f, GColor c, int x, int y, int w, GTextAlignment a) {
  text_outlined_rect(ctx, s, f, c, GRect(x, y, w, 30), a);   // (text() draws in a box 30 high)
}
// world (16 units per tile) -> screen
// 0.35: the camera is worked out once per frame (cam_frame), not for every tile and sprite
static int s_fcx, s_fcy, s_fT, s_fmy;
static void cam_frame(void) { s_fcx = cam_x() + TS / 2; s_fcy = cam_y() + TS / 2; s_fT = tile_px(); s_fmy = map_center_y(); }
static int sx_of(int wx) { return s_w / 2 + ((wx - s_fcx) * s_fT >> 4); }
static int sy_of(int wy) { return s_fmy + ((wy - s_fcy) * s_fT >> 4); }

// PD Chrome NinePatches (chrome.png): WINDOW (0,0,22,22,7), TOAST (22,0,18,18,5), BUTTON (58,0,6,6,2)
void panel(GRect r) { blit_nine(&s_chrome, 0, 0, 22, 22, 7, r.origin.x - 6, r.origin.y - 6, r.size.w + 12, r.size.h + 12); }
void red_button(GRect r, bool selected) {
  blit_nine(&s_chrome, 58, 0, 6, 6, 2, r.origin.x, r.origin.y, r.size.w, r.size.h);
  if (selected) blit_box(r.origin.x, r.origin.y, r.size.w, r.size.h, 2, ARGB(GColorChromeYellow));
}
void item_icon(const Item *it, int x, int y) {      // with its enchantment's glow
  int sp = item_sprite(it->kind);
  g_blit_glow = item_glow(it);
  ITEM_SPR(sp, x, y, 1);
  g_blit_glow = 0;
}



static void draw_char(const Sheet *sh, const Vis *v, bool left, const CharArt *art) {
  int f = anim_frame(v, art);
  bool was_ghost = g_blit_ghost;                    // the fading body: half its pixels for the second half of the fade
  if (v->anim == A_DIE && now_ms() - v->t0 > die_ms(art) + CORPSE_MS / 2) g_blit_ghost = true;
  int wx = v->x + (TS - art->fw) / 2, wy = v->y + TS - art->fh, le = now_ms() - s_leap_t0;
  if (v == &s_vis_hero && s_leap_t0 && le < 300) wy -= le * (300 - le) / 2250;   // the leap's arc: 10 px up at its height
  g_blit_flip = left; blit(sh, f * art->fw, 0, art->fw, art->fh, sx_of(wx), sy_of(wy), s_zoom); g_blit_flip = false;
  g_blit_ghost = was_ghost;
}

// EmoIcon position: over the sprite's top-right corner (Sleep 9x8 and Alert 8x8 from icons.png)
static void emo(const Vis *v, const CharArt *art, bool alert) {
  int wx = v->x + (TS + art->fw) / 2 - 4, wy = v->y + TS - art->fh - 8;
  if (!alert) wy -= (int)(now_ms() / 500 % 2);         // gentle bob instead of PD's pulsing scale
  blit(&s_icons, alert ? 9 : 0, 0, alert ? 8 : 9, 8, sx_of(wx), sy_of(wy), s_zoom);
}
// Blob emitters: toxic gas green, paralytic gas yellow, fire flickering orange and yellow; only in sight
// PD FlameParticle: orange squares (4 px) that appear anywhere in the cell, rise ever faster (acc 80 px/s2)
// and shrink away over 0.6 s; Fire pours one every 0.03 s, a burning character one every 0.06 s.
// Each particle is a pure function of the time, so nothing needs storing.
static void draw_flames(int px, int py, int seed, int n) {
  int T = tile_px();
  uint32_t now = now_ms();
  s_flames_shown = true;
  for (int i = 0; i < n; i++) {
    uint32_t t = now + (uint32_t)i * 600 / n + (uint32_t)seed * 97, a = t % 600, k = t / 600;
    if (a < 50) continue;                                          // alpha still ~0
    uint32_t h = (k * 2654435761u) ^ ((uint32_t)(seed * 31 + i) * 40503u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    int size = (4 * (600 - (int)a) + 300) / 600;                   // world pixels, shrinking from 4
    if (a < 120) size--;                                           // fading in: smaller and softer
    if (size < 1) continue;
    int rise = (int)(a * a / 25000);                               // 40 t^2 px, t in s
    int x = 2 + (int)(h % 12) - size / 2, y = 6 + (int)((h >> 4) % 10) - rise;
    int sz = size * T / 16; if (sz < 1) sz = 1;
    int sx = px + x * T / 16, sy = py + y * T / 16;
    blit_fill(sx, sy, sz, sz, seed < 0 ? 0xC8 : ARGB(GColorOrange));
    if (sz >= 3 && seed >= 0) blit_fill(sx + 1, sy + 1, sz - 2, sz - 2, ARGB(GColorChromeYellow));   // light mode: overlaps glow hotter
  }
}
// PD ToxicGas / ParalyticGas: every 0.6 s each gassy cell puffs out a Speck.TOXIC (green) / PARALYSIS (yellow)
// somewhere inside it; a puff lives 1-3 s, growing from 7 to 21 px while it fades in to half opacity and out
// again. Every puff is a function of the time and its cell, so nothing needs storing.
static void draw_gas(int px, int py, int c, int b) {
  static const uint8_t GAS_COLOR[3] = { GColorScreaminGreenARGB8, GColorIcterineARGB8, GColorShockingPinkARGB8 };   // toxic, paralytic, confusion
  int T = tile_px();
  uint32_t t = now_ms() + (uint32_t)(c * 7919 + b * 300) % 600, k0 = t / 600;
  s_flames_shown = true;
  for (uint32_t k = k0 - 4; k <= k0; k++) {                       // puffs from the last 3 s
    uint32_t h = (k * 2654435761u) ^ ((uint32_t)(c * 31 + b) * 40503u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    uint32_t age = t - k * 600, life = 1000 + (h >> 16) % 2000;
    if (age >= life) continue;
    int p = (int)(age * 256 / life), am = p < 128 ? p : 255 - p;  // alpha 0..0.5
    int r = (7 + 14 * p / 256) * T / 32;
    int x = px + (int)(h & 15) * T / 16, y = py + (int)((h >> 4) & 15) * T / 16;
    blit_puff(x, y, r, GAS_COLOR[b], am * 16 / 255 + 1, (int)(h >> 8) & 15);
  }
}
static void draw_line(int x0, int y0, int x1, int y1, int w, uint8_t c) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (int g = 0; g < 800; g++) {
    blit_fill(x0 - w / 2, y0 - w / 2, w, w, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
// MagicMissile colours and sizes per wand (Wand.wands order): cold, slowness, fire, poison, foliage, white,
// (lightning), purple, force, wool, (death ray), earth
static void draw_proj(void) {
  static const uint8_t HEAD[14] = { GColorBlueMoonARGB8, GColorWindsorTanARGB8, GColorOrangeARGB8, GColorGreenARGB8,
    GColorKellyGreenARGB8, GColorWhiteARGB8, GColorCelesteARGB8, GColorFollyARGB8, GColorWhiteARGB8, GColorLightGrayARGB8,
    GColorPurpureusARGB8, GColorLightGrayARGB8, GColorPurpleARGB8, GColorCelesteARGB8 };   // 12: MagicMissile.shadow (Warlock), 13: blueLight (magic missile)
  static const uint8_t TAIL[14] = { GColorPictonBlueARGB8, GColorBrassARGB8, GColorChromeYellowARGB8, GColorPurpleARGB8,
    GColorIslamicGreenARGB8, GColorLightGrayARGB8, GColorWhiteARGB8, GColorPurpleARGB8, GColorLightGrayARGB8, GColorWhiteARGB8,
    GColorWhiteARGB8, GColorDarkGrayARGB8, GColorImperialPurpleARGB8, GColorPictonBlueARGB8 };
  static const uint8_t SIZE[14] = { 4, 1, 4, 3, 4, 4, 0, 2, 1, 3, 0, 2, 3, 3 };
  int T = tile_px(), el = (int)(now_ms() - s_proj.t0); if (el > s_proj.ms) el = s_proj.ms;
  if (s_proj.sprite >= 0) {                                  // a thrown item
    int wx = s_proj.x0 + (s_proj.x1 - s_proj.x0) * el / s_proj.ms, wy = s_proj.y0 + (s_proj.y1 - s_proj.y0) * el / s_proj.ms;
    ITEM_SPR(s_proj.sprite, sx_of(wx), sy_of(wy), s_zoom);
    return;
  }
  int k = -1 - s_proj.sprite, h = TS / 2;
  if (k == IK_WND_LIGHTNING - IK_FIRST_WAND) {               // Lightning: jagged arcs between the creatures it jumped to
    for (int i = 0; i + 1 < s_narc; i++) {
      int ax = sx_of((s_arc[i] % MAP_W) * TS + h), ay = sy_of((s_arc[i] / MAP_W) * TS + h);
      int bx = sx_of((s_arc[i + 1] % MAP_W) * TS + h), by = sy_of((s_arc[i + 1] / MAP_W) * TS + h);
      uint32_t j = (now_ms() / 33 + i * 7) * 2654435761u;      // a new zigzag every frame (not the game's dice)
      int mx = (ax + bx) / 2 + (int)(j >> 28) % 9 - 4, my = (ay + by) / 2 + (int)((j >> 24) & 15) % 9 - 4;
      draw_line(ax, ay, mx, my, 2, GColorCelesteARGB8); draw_line(mx, my, bx, by, 2, GColorCelesteARGB8);
      draw_line(ax, ay, mx, my, 1, GColorWhiteARGB8); draw_line(mx, my, bx, by, 1, GColorWhiteARGB8);
    }
    return;
  }
  int x0 = sx_of(s_proj.x0 + h), y0 = sy_of(s_proj.y0 + h), x1 = sx_of(s_proj.x1 + h), y1 = sy_of(s_proj.y1 + h);
  if (k == IK_WND_DISINTEGRATION - IK_FIRST_WAND) {          // DeathRay: a beam that thins away
    int w = (s_proj.ms - el) * 6 * T / 16 / s_proj.ms + 1;
    draw_line(x0, y0, x1, y1, w, GColorPurpureusARGB8);
    if (w > 2) draw_line(x0, y0, x1, y1, w / 2, GColorWhiteARGB8);
    return;
  }
  int sz = (SIZE[k] + 2) * T / 16; if (sz < 2) sz = 2;
  for (int t = 3; t >= 0; t--) {                             // the trail, then the spark
    int e = el - t * 25; if (e < 0) continue;
    int px = x0 + (x1 - x0) * e / s_proj.ms, py = y0 + (y1 - y0) * e / s_proj.ms, z = t ? (sz + 1) / 2 : sz;
    blit_fill(px - z / 2, py - z / 2, z, z, t ? TAIL[k] : HEAD[k]);
  }
}
static void draw_web(int px, int py) {                // WebParticle: a few grey threads across the cell
  int T = tile_px(), s = T >= 16 ? T / 16 : 1;
  uint8_t c = ARGB(GColorLightGray);
  for (int i = s; i < T - s; i += s) {
    blit_fill(px + i, py + i, s, s, c); blit_fill(px + T - 1 - i, py + i, s, s, c);
    if (!(i & (2 * s - 1))) { blit_fill(px + T / 2, py + i, s, s, c); blit_fill(px + i, py + T / 2, s, s, c); }
  }
}
static void draw_blobs(void) {
  bool altar = G.altar_pos >= 0 && G.lvars[0] > 1;    // SacrificialFire burns on the altar until it is satisfied
  if (!g_blob[BLOB_TOXIC] && !g_blob[BLOB_PARALYTIC] && !g_blob[BLOB_FIRE] && !g_blob[BLOB_CONFUSION] && !g_blob[BLOB_WEB] && !altar) return;
  int T = tile_px();
  for (int c = 0; c < MAP_LEN; c++) {
    if (!BIT_GET(G.fov, c)) continue;
    if (s_proj.active && abs(c % MAP_W - s_proj.to % MAP_W) <= 2 && abs(c / MAP_W - s_proj.to / MAP_W) <= 2) continue;     // a shattering potion's cloud waits for the flask
    int px = sx_of((c % MAP_W) * TS), py = sy_of((c / MAP_W) * TS);
    if (px + 2 * T < 0 || px - T >= s_w || py + 2 * T < 0 || py - T >= s_h) continue;
    for (int b = 0; b < 2; b++) if (g_blob[b] && g_blob[b][c]) draw_gas(px, py, c, b);
    if (g_blob[BLOB_CONFUSION] && g_blob[BLOB_CONFUSION][c]) draw_gas(px, py, c, 2);
    if (g_blob[BLOB_FIRE] && g_blob[BLOB_FIRE][c]) draw_flames(px, py, c, 20);
    if (g_blob[BLOB_WEB] && g_blob[BLOB_WEB][c]) draw_web(px, py);
    if (altar && c == G.altar_pos) draw_flames(px, py, c, 8);
  }
}
static void draw_map(void) {
  int T = tile_px();
  s_flames_shown = false;
  blit_fill(0, 0, s_w, s_h, ARGB(GColorBlack));
  uint8_t hb[MAP_LEN / 8]; memset(hb, 0, sizeof hb);  // 0.34: which cells hold a heap (heap_at per cell was a search per tile)
  for (int i = 0; i < G.n_heaps; i++) if ((unsigned)G.heaps[i].pos < MAP_LEN) BIT_SET(hb, G.heaps[i].pos);
  for (int y = 0; y < MAP_H; y++) {
    int py = sy_of(y * TS);
    if (py + T < 0 || py >= s_h) continue;
    for (int x = 0; x < MAP_W; x++) {
      int px = sx_of(x * TS);
      if (px + T < 0 || px >= s_w) continue;
      int c = y * MAP_W + x;
      if (!BIT_GET(G.visited, c)) continue;
      int t = G.map[c];
      if (IS_WATER(t)) { g_blit_opaque = true; blit(&s_water, (x & 1) * TS, (y & 1) * TS, TS, TS, px, py, s_zoom); g_blit_opaque = false; }
      if (t != T_WATER) {                              // DungeonTilemap; most tiles are solid and simply copied
        const uint8_t *sp = spr_get(256 + t);
        if (sp) { g_blit_opaque = sp[0] >> 6; blit_spr4(sp, px, py, s_zoom); }   // an opaque first colour: no clear pixels
        g_blit_opaque = false;
      }
      int pl = plant_at(c);
      if (pl >= 0) blit(&s_plants, G.plants[pl].type * TS, 0, TS, TS, px, py, s_zoom);   // PlantSprite
      int hi = BIT_GET(hb, c) ? heap_at(c) : -1;
      if (hi >= 0 && !(s_proj.active && hi >= s_proj.n_heaps)) {                                   // ItemSprite: chests and remains hide what they hold
        int ty = G.heaps[hi].type, sp = heap_sprite(hi);
        if (ty == HEAP_PLAIN || ty == HEAP_FOR_SALE) g_blit_glow = item_glow(&G.heaps[hi].item);
        ITEM_SPR(sp, px, py, s_zoom);
        g_blit_glow = 0;
      }
      if (!BIT_GET(G.fov, c)) blit_fog(px, py, T);
    }
  }
  uint32_t ce = now_ms() - s_check_t0;
  if (s_check_pos >= 0 && ce < 800) {                // CheckedCell: a blue square on each searched cell, shrinking away (avalanche: rubble)
    int sz = T * (int)(800 - ce) / 1000, r = s_check_r;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      int x = s_check_pos % MAP_W + dx, y = s_check_pos / MAP_W + dy, c = y * MAP_W + x;
      if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H || !BIT_GET(G.fov, c)) continue;
      blit_fill(sx_of(x * TS) + (T - sz) / 2, sy_of(y * TS) + (T - sz) / 2, sz, sz, s_quake ? ARGB(GColorWindsorTan) : ARGB(GColorPictonBlue));
    }
  }
  for (int i = 0; i < G.n_mobs; i++) {
    Mob *m = &G.mobs[i]; Vis *v = &s_vis_mob[i];
    if (v->active && v->uid != m->uid) v->active = false;           // slot reused by a new monster
    bool dying = v->active && (v->anim == A_DIE || s_proj.active);  // killed by a throw still in the air: wait for it
    if (m->HP <= 0 && !dying) continue;
    if (!game_hero_visible_mob(i) && !dying) { v->active = false; continue; }
    if (!v->active) { vis_snap(v, m->pos); v->uid = m->uid; }
    draw_char(mob_sheet(m->type), v, m->flags & MF_LEFT, mob_art(m->type));
    if (m->type == MT_GOO && m->aux2 && m->HP > 0)   // GooSprite.pumpUp: black GooParticles from it; and (Shattered's kindness) on every cell its jump reaches
      for (int k = 0, c = m->pos - 2 - 2 * MAP_W; k < 25; k++, c += k % 5 ? 1 : MAP_W - 4) if (g_goo_warn >> k & 1 && BIT_GET(G.fov, c)) draw_flames(sx_of(c % MAP_W * TS), sy_of(c / MAP_W * TS), -1 - c, k == 12 ? 12 : 6);
    if (m->HP > 0 && m->b.burning) draw_flames(sx_of(v->x), sy_of(v->y), m->uid, 10);   // CharSprite.State.BURNING
    if (m->HP > 0 && m->type != MT_MIRROR) {          // EmoIcon: Zzz while asleep, ! just after noticing you
      bool alert = now_ms() < s_alert_until[i];
      if (alert || m->state == ST_SLEEPING) emo(v, mob_art(m->type), alert);
    }
  }
  int ht = s_scene == SC_GAME ? combat_target() : -1;   // PD HealthIndicator over the attack indicator's target
  if (ht >= 0 && s_vis_mob[ht].active) {
    const CharArt *art = mob_art(G.mobs[ht].type); Vis *v = &s_vis_mob[ht];
    int th = s_zoom == 2 ? 5 : s_zoom + 2;            // 0.70 (owner: "very important information"): 2 / 3 / 5 px thick by zoom, outlined in black
    int x = sx_of(v->x + (TS - art->fw) / 2), y = sy_of(v->y + TS - art->fh) - th - 3, w = art->fw << s_zoom >> 1;   // 0.43: was 0 wide (gone) at the far zoom
    blit_fill(x - 1, y - 1, w + 2, th + 2, ARGB(GColorBlack));
    blit_fill(x, y, w, th, ARGB(GColorRed)); blit_fill(x, y, w * G.mobs[ht].HP / G.mobs[ht].HT, th, ARGB(GColorGreen));
  }
  int tg = s_hl < 0 ? combat_target() : -1, cx = -1, cy = 0;
  if (tg >= 0 && s_vis_mob[tg].active) {
    Vis *v = &s_vis_mob[tg];                          // QuickSlot crossM: Icons.TARGET over the target's cell (0.18: melee too)
    cx = v->x; cy = v->y;
  } else if (s_scene == SC_GAME && s_hl < 0 && s_tg >= 0 && s_tgk != TK_ENEMY) {  // 0.23: a door, a thing, a spot
    cx = s_tg % MAP_W * TS; cy = s_tg / MAP_W * TS;
    if (s_tgk >= TK_BAG || (s_tgk == TK_GO && s_tg == G.hero.pos)) cx = -1;   // 0.48 test kit: a button's move: its reticle on the toolbar (below)
  }
  if (cx >= 0) {   // 0.43: never smaller than at the middle zoom (16 px): at the far zoom it spills 4 px over the neighbours
    int o = s_zoom ? 0 : 4;
    blit(&s_icons, 67, 0, 16, 16, sx_of(cx) - o, sy_of(cy) - o, s_zoom + !s_zoom);
  }
  g_blit_ghost = G.hero.b.invis > 0;                // HeroSprite alpha 0.4 while invisible
  bool blinking = s_proj.active && s_proj.sprite == -1 - (IK_WND_BLINK - IK_FIRST_WAND);   // WandOfBlink.fx: the hero vanishes
  if (G.hero.HP > 0 && !blinking) draw_char(&s_hero, &s_vis_hero, G.hero.facing_left, &HERO_ART);
  g_blit_ghost = false;
  if (G.hero.HP > 0 && G.hero.b.burning) draw_flames(sx_of(s_vis_hero.x), sy_of(s_vis_hero.y), 999, 10);
  draw_blobs();
  if (G.hero.HP > 0 && G.action == HA_REST) emo(&s_vis_hero, &HERO_ART, false);
  if (s_proj.active) draw_proj();
}

// PD BuffIndicator: 7x7 icons after the health numbers. Strip order (buffs.png / buffs_large.png):
// 0.42: a table (the chain of ifs was 464 B): where each buff's counter sits in G; HB_W: 16 bits, HB_POS: a cell or -1
#define HB(f, bi) { offsetof(Game, f), bi }
enum { HB_W = 0x40, HB_POS = 0x80 };
int hero_buffs(uint8_t *out) {
  static const struct { uint16_t off; uint8_t bi; } T[] = {
    HB(hero.b.burning, BI_FIRE), HB(hero.b.poison, BI_POISON), HB(hero.b.bleed, BI_BLEEDING), HB(hero.b.paralysis, BI_PARALYSIS),
    HB(hero.b.frost, BI_FROST), HB(hero.b.cripple, BI_CRIPPLE), HB(hero.b.roots, BI_ROOTS), HB(hero.b.slow, BI_SLOW),
    HB(hero.b.blind, BI_BLIND), HB(hero.b.levitation, BI_LEVITATION), HB(hero.b.mind, BI_MIND), HB(hero.b.invis, BI_INVISIBLE),
    HB(hero.b.barkskin, BI_BARKSKIN), HB(hero.b.gas_immune, BI_IMMUNITY), HB(hero.b.charm, BI_CHARM), HB(herbal, BI_HERBAL),
    HB(deferred, BI_DEFERRED | HB_W), HB(hero.ooze, BI_OOZE), HB(hero.vertigo, BI_VERTIGO), HB(hero.weakness, BI_WEAKNESS),
    HB(health_pos, BI_HEALING | HB_POS), HB(hero.b.terror, BI_TERROR), HB(hero.light, BI_LIGHT), HB(hero.combo, BI_COMBO),
    HB(hero.fury, BI_FURY) };
  int n = 0;
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    const uint8_t *p = (const uint8_t *)&G + T[i].off; int k = T[i].bi;
    if (k & HB_POS ? !(p[1] & 0x80) : p[0] || (k & HB_W && p[1])) out[n++] = k & 0x3F;
  }
  if (game_starving()) out[n++] = BI_STARVING; else if (game_hungry()) out[n++] = BI_HUNGER;
  return n;
}
// where the health bar, experience bar, depth and keys go (Emery: in the status pane)
#define HUD_HPX PBL_IF_ROUND_ELSE(82, x + SC(30))
#define HUD_HPY PBL_IF_ROUND_ELSE(10, y + SC(3))
#define HUD_XPY PBL_IF_ROUND_ELSE(18, y + SC(8))
#define HUD_DKX PBL_IF_ROUND_ELSE(206, x + w)
#define HUD_DKY PBL_IF_ROUND_ELSE(17, y)
static void draw_hud_art(void) {
  Hero *h = &G.hero;
  int x = status_x(), y = status_y(), w = status_w(), H = SC(SP_H);
#ifdef PBL_ROUND
  // Gabbro: the cap (StatusPane's grey and its light and black edge) over the top arc, the health and experience bars on
  // a black strip in it; PD's portrait block and level box at 1.5x hang from it on the left
  blit_fill(0, 0, s_w, CAP_H, ARGB(GColorDarkGray)); blit_fill(0, CAP_H, s_w, 1, ARGB(GColorLightGray)); blit_fill(0, CAP_H + 1, s_w, 2, ARGB(GColorBlack));
  blit_scaled(&s_status, 0, 0, 28, SP_H, x, y, SC(28), H);
  blit_scaled(&s_status, 28, 9, 6, SP_H - 9, x + SC(28), y + SC(9), SC(6), H - SC(9));
  blit_scaled(&s_status, 84, 3, 28, 15, HUD_DKX - SC(44), HUD_DKY + SC(3), SC(28), SC(15));
  blit_fill(HUD_HPX - 1, HUD_HPY - 1, SC(64) + 2, SC(8) - SC(3) + 4, ARGB(GColorBlack));
#else
  // Emery: PD StatusPane at 1.5x, whole: left 80 px and right 48 px of status_pane.png, column 80 repeated between
  blit_scaled(&s_status, 0, 0, 80, SP_H, x, y, SC(80), H);
  if (w > SC(128)) blit_scaled(&s_status, 80, 0, 1, SP_H, x + SC(80), y, w - SC(128), H);
  blit_scaled(&s_status, 80, 0, 48, SP_H, x + w - SC(48), y, SC(48), H);
#endif
  { GRect mb = menu_button_rect();                     // 0.20: the menu button, bigger than the pane's own
    red_button(mb, false);
    for (int i = 0; i < 3; i++) blit_fill(mb.origin.x + 7, mb.origin.y + 8 + i * 6, mb.size.w - 14, 3, ARGB(GColorWhite)); }
  blit(&s_hero, 0, 0, HERO_ART.fw, HERO_ART.fh, x + SC(15) - HERO_ART.fw, y + SC(16) - HERO_ART.fh, 2);   // avatar
  int hp = h->HP > 0 ? h->HP : 0, hpw = SC(64) * hp / h->HT;            // hp_bar.png squeezed to health (PD hp.scale.x)
  if (hpw > 0) blit_scaled(&s_hpbar, 0, 0, 64, 4, HUD_HPX, HUD_HPY, hpw, SC(4));
  blit_fill(HUD_HPX, HUD_XPY, SC(64) * h->exp / hero_max_exp(), 2, ARGB(GColorIcterine));
  uint8_t b[BI_COUNT]; int nb = hero_buffs(b);                         // BuffIndicator, icons at 2x
  int fit = PBL_IF_ROUND_ELSE(5, (w - SC(38) - SC(48) + 2) / 16);
  if (nb > fit) nb = fit;
  for (int i = 0; i < nb; i++) blit(&s_buffs, b[i] * 7, 0, 7, 7, PBL_IF_ROUND_ELSE(90, x + SC(38)) + i * 16, PBL_IF_ROUND_ELSE(CAP_H + 5, y + SC(22)), 2);   // clear of the level box
  // toolbar in PD order: wait, search, inspect | gap | backpack, quickslot 2, quickslot 1
  int ty = toolbar_y(), bw = tb_w();
  if (toolbar_hidden()) goto after_toolbar;
  for (int i = 0; i < TB_N; i++) blit_nine(&s_toolbar, 83, 0, 23, 25, 4, tb_x(i), TB_Y(i), bw, TB_H);
  blit(&s_toolbar, 5, 8, 10, 10, tb_x(TB_WAIT) + (bw - 20) / 2, TB_Y(TB_WAIT) + (TB_H - 20) / 2, 2);
  blit(&s_toolbar, 24, 7, 12, 12, tb_x(TB_SEARCH) + (bw - 24) / 2, TB_Y(TB_SEARCH) + (TB_H - 24) / 2, 2);
  blit(&s_toolbar, 46, 7, 8, 12, tb_x(TB_INFO) + (bw - 16) / 2, TB_Y(TB_INFO) + (TB_H - 24) / 2, 2);
  blit(&s_toolbar, 65, 5, 14, 15, tb_x(TB_BAG) + (bw - 28) / 2, TB_Y(TB_BAG) + (TB_H - 30) / 2, 2);
  for (int q = 0; q < 2; q++) {
    int kind = h->quick[q];
    if (kind == IK_NONE) continue;
    int sp = item_sprite(kind), bx = tb_x(q == 0 ? TB_Q1 : TB_Q2);
    ITEM_SPR(sp, bx + (bw - 32) / 2, TB_Y(5 - q) + 3, 2);
    if (game_bag_find(kind) < 0) blit_fog(bx + (bw - 32) / 2, TB_Y(5 - q) + 3, 32);
  }
after_toolbar:;
  GRect hpr;
  if (hero_pointer(&hpr)) {                        // off-screen hero: framed portrait at the edge
    blit_nine(&s_chrome, 22, 0, 18, 18, 5, hpr.origin.x, hpr.origin.y, hpr.size.w, hpr.size.h);
    g_blit_flip = G.hero.facing_left; blit(&s_hero, 0, 0, HERO_ART.fw, HERO_ART.fh, hpr.origin.x + 8, hpr.origin.y + 7, 1); g_blit_flip = false;
  }
  if (s_scene == SC_TARGET && s_aim_q >= 0 && s_aim_mob >= 0) {   // QuickSlot crossB over the aiming slot
    int bx = tb_x(s_aim_q == 0 ? TB_Q1 : TB_Q2);
    blit(&s_icons, 67, 0, 16, 16, bx + (bw - 32) / 2, TB_Y(5 - s_aim_q) + 3, 2);
  }
  if (resume_shown()) {                             // PD ResumeButton
    GRect a = tag_rect(2);
    blit_nine(&s_tag_white, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    blit(&s_icons, 83, 0, 12, 11, a.origin.x + (a.size.w - 24) / 2 + 2, a.origin.y + (a.size.h - 22) / 2, 2);
  }
  if (loot_shown()) {                               // PD LootIndicator: blue tag with the heap's picture
    GRect a = tag_rect(1);
    Heap *hp = &G.heaps[heap_at(G.hero.pos)];
    int sp = heap_sprite(heap_at(G.hero.pos)); (void)hp;
    blit_nine(&s_tag_blue, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    ITEM_SPR(sp, a.origin.x + (a.size.w - 32) / 2 + 2, a.origin.y + (a.size.h - 32) / 2, 2);
  }
  if (combat()) {                                   // the buttons' tags (0.24: slim, pictures at 1x), beside the buttons that work them
    GRect a = ctag(0);                                // zoom: the search glass and a pip per level
    blit_nine(&s_tag, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    blit(&s_tag, 16, 1, 12, 12, a.origin.x + 3, a.origin.y + 6, 1);   // 0.60: the glass without the button face (tag_red.png)
    for (int i = 0; i <= s_zoom; i++) blit_fill(a.origin.x + 17, a.origin.y + 17 - i * 5, 3, 3, ARGB(GColorIcterine));
  }
  #define AT1(a, s) (a).origin.x + 2 + ((a).size.w - (s)) / 2, (a).origin.y + ((a).size.h - (s)) / 2   // a picture s px wide, centred
  #define AT1X(a, s) ((a).origin.x + 2 + ((a).size.w - (s)) / 2)
  #define AT1Y(a, s) ((a).origin.y + ((a).size.h - (s)) / 2)
  int at = combat_target();
  if (cycle_shown()) {                              // the next target: Icons.TARGET
    GRect a = ctag(2);
    blit_nine(&s_tag, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    blit(&s_icons, 67, 0, 16, 16, AT1(a, 16), 1);
  }
  if (s_scene == SC_GAME && s_tg >= 0 && s_tgk != TK_ENEMY) {   // 0.23: go there (pale, the walk arrow) / pick it up (blue, the thing)
    GRect a = ctag(1);
    bool item = s_tgk == TK_ITEM && heap_at(s_tg) >= 0;
    blit_nine(item ? &s_tag_blue : &s_tag_white, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    if (item) item_spr(heap_sprite(heap_at(s_tg)), AT1(a, 16), 1);
    else if (s_tgk >= TK_MINE) item_spr(item_sprite(IK_PICKAXE), AT1(a, 16), 1);   // 0.41: a gold vein: the pickaxe
    else blit(&s_icons, 83, 0, 12, 11, AT1(a, 12), 1);
  }
  if (at >= 0) {                                    // PD AttackIndicator, with the weapon (or what is aimed) instead of the target
    GRect a = ctag(1);
    blit_nine(&s_tag, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    const Item *it = s_scene != SC_TARGET ? &G.hero.weapon : s_zap == 2 ? &G.hero.armor : game_slot(s_item_slot);
    if (it && it->kind) { g_blit_glow = item_glow(it); item_spr(item_sprite(it->kind), AT1(a, 16), 1); g_blit_glow = 0; }
    else {                                          // bare hands: the target, as in PD
      const CharArt *art = mob_art(G.mobs[at].type);
      blit(mob_sheet(G.mobs[at].type), 0, 0, art->fw, art->fh, a.origin.x + 2 + (a.size.w - art->fw) / 2, a.origin.y + (a.size.h - art->fh) / 2, 1);
    }
  }
  int hl = s_hl;
  if (hl < 0 && s_tg >= 0) hl = s_tgk == TK_BAG ? hand_btn() : s_tgk == TK_REST ? TB_WAIT : s_tgk == TK_GO && s_tg == G.hero.pos ? (g_ovr.d & 128 ? TB_WAIT : TB_SEARCH) : -1;   // (0.61: a hold: Select waits)   // 0.48 test kit: the bot's hands: that button's reticle
  if (hl >= 0 && s_scene == SC_GAME) {             // 0.42: the highlighted button, and a half-size copy of it on the middle tag
    GRect a = ctag(1), r = hl_rect(hl);
    blit_nine(&s_tag_white, 0, 0, 16, 14, 3, a.origin.x, a.origin.y, a.size.w + 4, a.size.h);
    if (s_hl < 0 && s_tgk == TK_BAG) {               // (the bag: the item it's for) 0.51 (owner): a new one flies out of the backpack first (0.61: onto an empty tag, not over the button)
      int key = g_ovr.hs << 4 | g_ovr.ha, x = AT1X(a, 16), y = AT1Y(a, 16); uint32_t e = now_ms() - s_hand_t0;
      if (key != s_hand_key) { s_hand_key = key; s_hand_t0 = now_ms(); e = 0; }
      if (e < HAND_MS) { GRect b = hl_rect(hand_btn()); int bx = AT1X(b, 16), by = AT1Y(b, 16); x = bx + (x - bx) * (int)e / HAND_MS; y = by + (y - by) * (int)e / HAND_MS; }
      item_spr(item_sprite(game_slot(g_ovr.hs)->kind), x, y, 1);
    } else { s_hand_key = -1; blit_shrink(r, a.origin.x + 2 + (a.size.w - r.size.w / 2) / 2, a.origin.y + (a.size.h - r.size.h / 2) / 2); }
    reticle(r);
  }
  #undef AT1
}
#ifndef PBL_ROUND
int mark_scan(GRect r, uint8_t mark, int color);   // blit.c
#endif
void text_outlined_rect(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a) {
#ifndef PBL_ROUND
  // 0.35: one text draw, not five (text was the costliest part of a frame with the log up): drawn in a colour the area
  // around it does not hold, then outlined in the frame buffer (blit.c mark_scan)
  GRect o = GRect(r.origin.x - 2, r.origin.y - 2, r.size.w + 4, r.size.h + 6);   // (glyphs may stray a little past the box)
  for (uint8_t m = GColorOxfordBlueARGB8; m <= GColorOxfordBlueARGB8 + 2; m++) {   // three dark blues, rare in the art
    blit_begin(ctx); int used = mark_scan(o, m, -1); blit_end(ctx);
    if (used) continue;
    graphics_context_set_text_color(ctx, (GColor){ .argb = m });
    graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
    blit_begin(ctx); mark_scan(o, m, c.argb); blit_end(ctx);
    return;
  }
#endif
  graphics_context_set_text_color(ctx, GColorBlack);
  static const int8_t O[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
  for (int i = 0; i < 4; i++)
    graphics_draw_text(ctx, s, f, GRect(r.origin.x + O[i][0], r.origin.y + O[i][1], r.size.w, r.size.h), GTextOverflowModeTrailingEllipsis, a, NULL);
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, r, GTextOverflowModeTrailingEllipsis, a, NULL);
}
static void draw_hud_text(GContext *ctx) {
  Hero *h = &G.hero;
  int x = status_x(), y = status_y(), w = status_w();
  char buf[16];
  snprintf(buf, sizeof buf, "%d", h->lvl);                                // level box under the portrait
  text_outlined(ctx, buf, s_font_hud, GColorWhite, x + SC(22) - 2, y + SC(21), SC(12) + 4, GTextAlignmentCenter);
  snprintf(buf, sizeof buf, "%d/%d", h->HP > 0 ? h->HP : 0, h->HT);
  text_outlined(ctx, buf, s_font_hud, GColorWhite, PBL_IF_ROUND_ELSE(HUD_HPX + 2, x + SC(31)), PBL_IF_ROUND_ELSE(HUD_XPY - 1, y + SC(7)), 60, GTextAlignmentLeft);
  // StatusPane: depth beside the stairs icon, iron keys beside the key icon, light grey with a black edge
  snprintf(buf, sizeof buf, "%d", G.depth);
  text_outlined(ctx, buf, s_font_hud, GColorLightGray, HUD_DKX - SC(42) - 24, HUD_DKY - 1, 24, GTextAlignmentRight);
  snprintf(buf, sizeof buf, "%d", G.keys[G.depth] & 15);              // IronKey.curDepthQuantity
  text_outlined(ctx, buf, s_font_hud, GColorLightGray, HUD_DKX - SC(26) - 24, HUD_DKY - 1, 24, GTextAlignmentRight);
  for (int q = 0; q < 2 && !toolbar_hidden(); q++) {
    int slot = game_bag_find(h->quick[q]);
    if (slot < 0) continue;
    const Item *qi = game_slot(slot);
    if (item_cat(qi->kind) == CAT_WAND && (qi->flags & IF_LEVEL_KNOWN)) {   // ItemSlot status: charges, top left
      if (qi->flags & IF_CHARGE_KNOWN) snprintf(buf, sizeof buf, "%d/%d", qi->ench, wand_max(qi)); else snprintf(buf, sizeof buf, "?/%d", wand_max(qi));
      text_outlined(ctx, buf, s_font_hud, GColorWhite, tb_x(q == 0 ? TB_Q1 : TB_Q2) + 3, PBL_IF_ROUND_ELSE(TBY[5 - q], toolbar_y()) - 1, tb_w() - 4, GTextAlignmentLeft);
      continue;
    }
    if (qi->qty < 2) continue;
    snprintf(buf, sizeof buf, "%d", qi->qty);
    text_outlined(ctx, buf, s_font_hud, GColorWhite, tb_x(q == 0 ? TB_Q1 : TB_Q2), PBL_IF_ROUND_ELSE(TBY[5 - q], toolbar_y()) + TB_H - 18, tb_w() - 4, GTextAlignmentRight);
  }
  for (int i = 0; i < s_n_float; i++) {
    Floater *f = &s_float[i];
    int px = sx_of((f->cell % MAP_W) * TS + TS / 2), py = sy_of((f->cell / MAP_W) * TS) - 8 - (int)(now_ms() - f->t0) / 33;
    text_outlined(ctx, f->text, s_font_hud, f->color, px - 40, py, 80, GTextAlignmentCenter);
  }
  // PD GameLog: newest lines over the map, bottom left, outlined (no box)
  const char *hint = s_scene == SC_TARGET ? txt(s_zap == 2 ? (G.hero.armor.kind == IK_ARMOR_ROGUE ? TX_H_JUMP : TX_H_LEAP) : s_zap ? TX_H_ZAP : TX_H_THROW) : s_scene == SC_INSPECT ? txt(TX_H_INSPECT) : NULL;
  const char *l0 = hint ? NULL : s_msg[0][0] ? s_msg[0] : NULL, *l1 = hint ? hint : s_msg[1][0] ? s_msg[1] : NULL;
  if (!l1 || s_scene == SC_DEAD) return;
  int mw = PBL_IF_ROUND_ELSE(124, s_w - 8 - 22), mx = PBL_IF_ROUND_ELSE((s_w - 124) / 2, 4);
  GRect fit = GRect(0, 0, mw, 60);
  int h1 = graphics_text_layout_get_content_size(l1, s_font_log, fit, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h;
  int h0 = l0 ? graphics_text_layout_get_content_size(l0, s_font_log, fit, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft).h : 0;
  if (h0 + h1 > 52) { l0 = NULL; h0 = 0; }
  int y1 = toolbar_y() - h1 - 4, y0 = y1 - h0;
  if (l0) text_outlined_rect(ctx, l0, s_font_log, s_msg_color[0], GRect(mx, y0, mw, h0 + 4), GTextAlignmentLeft);
  text_outlined_rect(ctx, l1, s_font_log, hint ? GColorPastelYellow : s_msg_color[1], GRect(mx, y1, mw, h1 + 4), GTextAlignmentLeft);
}

static const char *info_desc(void) { return s_info_desc && s_info_desc[0] ? s_info_desc : "Nothing special."; }
#ifdef PBL_ROUND
// 0.35 Gabbro: long texts flow along the round screen's edge, a page at a time (Pebble's text flow and paging). The
// windows' overlays lay their text out (s_text_r: where, s_text_h: how tall); the layer here only shows and scrolls it
GTextAttributes *s_flow; GRect s_text_r;
bool flow_set(GRect f) {                             // text in f flows along the screen's edge, paged by f
  if (!s_flow && !(s_flow = graphics_text_attributes_create())) return false;
  graphics_text_attributes_enable_screen_text_flow(s_flow, 12);
  graphics_text_attributes_enable_paging(s_flow, f.origin, f);
  return true;
}
int flow_h(const char *t, GRect f) {                 // the height of t flowed into f (for the overlays: the system call lives here)
  if (!flow_set(f)) return 600;
  return graphics_text_layout_get_content_size_with_attributes(t, s_font_log, GRect(f.origin.x, f.origin.y, f.size.w, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft, s_flow).h;
}
#else
static int info_head(void) { return s_info_mob >= 0 ? 38 : s_info_title[0] ? 20 : 0; }
GRect info_rect(void) {
  int w = 186;
  int th = graphics_text_layout_get_content_size(info_desc(), s_font_log, GRect(0, 0, w - 6, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft).h + 6;
  int h = info_head() + th, hmax = s_h - PBL_IF_ROUND_ELSE(64, 16);
  if (h > hmax) h = hmax;
  return GRect((s_w - w) / 2, (s_h - h) / 2, w, h);
}
#endif

// ---- the scrolling text layer ----
static const char *text_now(void) { return s_scene == SC_ITEM ? s_text_buf : s_scene == SC_INFO ? info_desc() : NULL; }
static GRect text_frame(void) {
#ifdef PBL_ROUND
  return s_text_r;
#else
  if (s_scene == SC_ITEM) { GRect r = item_rect(); return GRect(r.origin.x + 5, r.origin.y + s_item_head, r.size.w - 10, s_item_text_h); }
  GRect r = info_rect(); int hd = info_head();
  return GRect(r.origin.x, r.origin.y + hd - 2, r.size.w, r.size.h - hd + 2);
#endif
}
#ifdef PBL_ROUND
static int text_max_scroll(void) { int h = layer_get_frame(s_text_layer).size.h; return h > 0 && s_text_h > 5 ? (s_text_h - 5) / h * h : 0; }   // whole pages
#else
static int text_max_scroll(void) { int m = s_text_h - layer_get_frame(s_text_layer).size.h; return m > 0 ? m : 0; }
#endif
static void text_proc(Layer *l, GContext *ctx) {
  const char *t = text_now();
  if (!t) return;
  GRect b = layer_get_bounds(l);
#ifdef PBL_ROUND
  flow_set(layer_get_frame(l));
#endif
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, t, s_font_log, GRect(0, -s_text_scroll, b.size.w - PBL_IF_ROUND_ELSE(0, 6), s_text_h + 16), GTextOverflowModeWordWrap, GTextAlignmentLeft, PBL_IF_ROUND_ELSE(s_flow, NULL));
  int m = text_max_scroll();
  if (!m) return;
  graphics_context_set_fill_color(ctx, GColorIcterine);       // little arrows: more above / below (Gabbro: centred)
  for (int i = 0; i < 3; i++) {
    if (s_text_scroll > 0) graphics_fill_rect(ctx, GRect(PBL_IF_ROUND_ELSE(b.size.w / 2, b.size.w - 4) - i, 1 + i, 1 + 2 * i, 1), 0, GCornerNone);
    if (s_text_scroll < m) graphics_fill_rect(ctx, GRect(PBL_IF_ROUND_ELSE(b.size.w / 2, b.size.w - 4) - i, b.size.h - 2 - i, 1 + 2 * i, 1), 0, GCornerNone);
  }
}
static void text_sync(void) {
  if (!s_text_layer) return;
  const char *t = text_now();
  if (!t) { layer_set_hidden(s_text_layer, true); return; }
  GRect f = text_frame();
#ifndef PBL_ROUND
  s_text_h = graphics_text_layout_get_content_size(t, s_font_log, GRect(0, 0, f.size.w - 6, 2000), GTextOverflowModeWordWrap, GTextAlignmentLeft).h + 4;
#endif
  GRect old = layer_get_frame(s_text_layer);
  if (!grect_equal(&old, &f)) layer_set_frame(s_text_layer, f);
  int m = text_max_scroll();
  if (s_text_scroll > m) s_text_scroll = m;
  if (s_text_scroll < 0) s_text_scroll = 0;
  layer_set_hidden(s_text_layer, false);
  layer_mark_dirty(s_text_layer);
}
static bool text_page(int dir) {                     // a tap on overflowing text turns the page; false if it all fits
  int m = text_max_scroll();
  if (!m) return false;
  int step = layer_get_frame(s_text_layer).size.h - PBL_IF_ROUND_ELSE(0, 16);   // Gabbro: whole pages (the text is paged)
  if (dir > 0) s_text_scroll = s_text_scroll >= m ? 0 : s_text_scroll + step;
  else s_text_scroll = s_text_scroll <= 0 ? m : s_text_scroll - step;
  redraw();
  return true;
}

// Windows (title, hero select, menu, backpack, item, info, character sheet...) are drawn by the "ui" overlay
// (src/ovl/ui), loaded while one is open: code room for the game itself.
static const OvlHeader *s_ui; static uint32_t s_ui_res;   // "ui": the front screens; "uiw": the in-game windows
static void ui_win_free(void) { if (s_ui) { ovl_free(s_ui); s_ui = NULL; } }
static const OvlHeader *ui_ovl(int what) {
  uint32_t res = FRONT(what) ? RESOURCE_ID_OVL_UI : what == SC_BAG || what == SC_ITEM ? RESOURCE_ID_OVL_BAG
    : what == SC_SHEET || what == SC_CATALOG ? RESOURCE_ID_OVL_SHEET : RESOURCE_ID_OVL_UIW;   // 0.14: three window overlays, each small
  if (s_ui && s_ui_res != res) ui_win_free();
  if (!s_ui) { s_ui_res = res; s_ui = ovl_load(res); }
  return s_ui;
}
static void ui_input(int ev, int x, int y) {               // ev: a ButtonId, or -1 a tap
  int r = ui_ovl(s_scene) ? ((int (*)(int, int, int))s_ui->fn[1])(ev, x, y) : 0, a = r & 255;
  switch (r >> 8) {                                           // here, what frees the overlay that asked (CMD in ui_int.h)
    case C_START: start_game(a); break;
    case C_ITEM: open_item(a); break;
    case C_BAG_CLOSE: bag_close(); break;
    case C_DO: item_do(a); break;
    case C_PAGE: text_page(1); break;
    case C_MENU: menu_select(a); break;
    case C_JUMP: jump_choose(a); break;
  }
}
static void ui_win(int what, GContext *ctx) {                // what: a Scene
  if (ui_ovl(what)) ((void (*)(int, GContext *))s_ui->fn[0])(what, ctx);
  else if (ctx) {                                            // no room for the window's code: say so rather than show nothing
    char t[64]; snprintf(t, sizeof t, "Out of memory for this window (%d of %d B). Press Back.", g_ovl_big, g_ovl_need);
    text_outlined_rect(ctx, t, s_font_log, GColorSunsetOrange, GRect(8, s_h / 2 - 30, s_w - 16, 60), GTextAlignmentCenter);
  }
}
#ifdef PERF_PROBE
static void update_body(Layer *layer, GContext *ctx);
static void update_proc(Layer *l, GContext *ctx) { uint32_t t0 = now_ms(); update_body(l, ctx); perf_add(0, t0); }
#define update_proc update_body
#endif
static void cam_frame(void);
static void update_proc(Layer *layer, GContext *ctx) {
  s_fms = now_ms();
  cam_frame();
  sheet_need(&s_micons, RESOURCE_ID_IMAGE_MENU_ICONS, s_scene == SC_MENU || s_scene == SC_BAG);
  if (!s_micons.bmp && (s_scene == SC_MENU || s_scene == SC_BAG)) { ui_free_cache(s_scene == SC_MENU); sheet_load(&s_micons, RESOURCE_ID_IMAGE_MENU_ICONS); }   // 0.79 (owner: "the menu icons were all missing"): a full heap: the caches go first
  static uint32_t s_go_res;                                   // the end banner, the amulet or the surface
  bool boss = now_ms() < s_boss_until && s_scene == SC_GAME;
  uint32_t go = s_scene == SC_AMULET ? (s_amulet == 2 ? RESOURCE_ID_IMAGE_SURFACE : RESOURCE_ID_IMAGE_AMULET) : boss ? RESOURCE_ID_IMAGE_BANNER_BOSS : RESOURCE_ID_IMAGE_BANNER_GAMEOVER;
  if (go != s_go_res) { sheet_free(&s_gameover); s_go_res = go; }
  sheet_need(&s_gameover, go, s_scene == SC_DEAD || s_scene == SC_AMULET || boss);
  if (FRONT(s_scene)) { ui_win(s_scene, ctx); return; }
  if (s_scene != SC_GAME) s_hl = -1;
  if (s_scene == SC_GAME) tg_refresh(0);                  // 0.23: the buttons' target, kept or the best one now
  if (!s_water.bmp && s_status.bmp && (s_scene == SC_GAME || s_scene == SC_TARGET || s_scene == SC_INSPECT)) { ui_win_free(); tiles_load(true); }
  blit_begin(ctx);
#ifdef PERF_PROBE
#define PT(k, x) { uint32_t t0 = now_ms(); x; perf_add(k, t0); }
#else
#define PT(k, x) x
#endif
  PT(3, if (s_scene != SC_BAG) draw_map());           // 0.28: the backpack covers it (its sprites would churn the cache)
  snd_poll();                                                 // the map is the slow part: the speaker topped up after it
  PT(6, draw_hud_art());
  if (boss) blit(&s_gameover, 0, 0, 128, 35, (s_w - 128) / 2, map_center_y() - 50, 1);
  blit_end(ctx);
  PT(9, draw_hud_text(ctx));
  if (s_scene == SC_GAME || s_scene == SC_TARGET || s_scene == SC_INSPECT) ui_win_free();   // windows' code only while one is up
  else ui_win(s_scene, ctx);
  if (s_fade) {
    int el = (int)(now_ms() - s_fade_t0), lv = el * 16 / FADE_MS;
    if (lv > 16) lv = 16;
    blit_begin(ctx); blit_fade(s_fade == 1 ? lv : 16 - lv); blit_end(ctx);
  }
  g_idle_at = s_frame_end = now_ms();
}

#ifdef PERF_PROBE
#undef update_proc
#endif
// ---------------- app ----------------
static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_w = b.size.w; s_h = b.size.h;
  s_layer = layer_create(b);
  layer_set_update_proc(s_layer, update_proc);
  layer_add_child(root, s_layer);
  s_text_layer = layer_create(GRect(0, 0, 10, 10));
  layer_set_update_proc(s_text_layer, text_proc);
  layer_set_hidden(s_text_layer, true);
  layer_add_child(root, s_text_layer);
}
static void window_unload(Window *w) { layer_destroy(s_text_layer); s_text_layer = NULL; layer_destroy(s_layer); }

static void init(void) {
  if (!game_init()) return;                     // no memory for the game state: nothing sensible to show
  s_last_in = now_ms(); s_text_buf = malloc(TEXT_BUF); s_msg = calloc(2, 100); s_qfx = calloc(MAX_QFX, sizeof(QueuedFx)); s_float = calloc(MAX_FLOAT, sizeof(Floater)); s_spr_use = calloc(SPR_N, 4); s_vis_mob = calloc(MAX_MOBS, sizeof(Vis)); s_alert_until = calloc(MAX_MOBS, 4);
  ovl_arena_init();                            // 0.20: overlays that come and go stack up in here
  blit_init(PBL_DISPLAY_HEIGHT);
  if (!s_text_buf) return;
  snd_init();                                  // (loads the sound engine now: long-lived things first, then pictures)
#ifdef WHATIF
  int floor_op(int, int, void *); if (!persist_exists(1)) floor_op(9, 0, NULL);   // FO_WHATIF (game_int.h)   // 0.51 what-if build: no save: the frozen bot game (0.70: in the save overlay)
#endif
  if (persist_exists(KEY_ZOOM)) { int v = persist_read_int(KEY_ZOOM); s_zoom = v & 15; int a = (v >> 4) & 15, o = (a & 7) ^ ASSIST_DEF; g_assist = (a & 8) ? a & 7 : o == 4 ? 0 : o == 3 ? 7 : o == 2 ? 3 : 1; if (s_zoom > 2) s_zoom = 1; }   // (0.80: an old level: Low, Medium: on; High: + items; Max: + off-screen; Off)
  sheet_load(&s_chrome, RESOURCE_ID_IMAGE_CHROME);
  s_font_tiny = fonts_get_system_font(FONT_KEY_GOTHIC_09);
  s_font_hud = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_font_log = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  s_font_big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
  if (touch_service_is_enabled()) touch_service_subscribe(touch_handler, NULL);
  to_title();
}
static void deinit(void) {
  if (!s_window) return;
  // leaving the app any way at all keeps the game where it is (PD saves on pause): no going back to an older autosave
  if (g_game && G.hero.HP > 0 && s_scene != SC_DEAD && (!FRONT(s_scene) || (s_scene == SC_AMULET && s_amulet != 2))) { ui_win_free(); game_art(false); game_save(); }
  snd_deinit();
  if (s_timer) app_timer_cancel(s_timer);
  touch_service_unsubscribe();
  window_destroy(s_window);
  ispr_free();
  ispr_free();
  Sheet *all[] = { &s_water, &s_hero, &s_toolbar, &s_logo, &s_chrome,
    &s_status, &s_hpbar, &s_arcs, &s_play, &s_avatars, &s_banner, &s_tag, &s_tag_blue, &s_tag_white, &s_buffs, &s_icons, &s_gameover, &s_micons };
  for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) sheet_free(all[i]);
}
int main(void) { init(); app_event_loop(); deinit(); }
