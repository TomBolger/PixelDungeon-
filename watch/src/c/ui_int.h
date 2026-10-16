/* Pixel Dungeon for Pebble - what main.c shares with the window overlay (src/ovl/ui) */
#pragma once
#include "pd.h"

enum { A_IDLE, A_RUN, A_ATTACK, A_DIE, A_OPERATE, A_READ, A_COUNT };
typedef struct { uint8_t fw, fh; uint8_t seq[A_COUNT][10]; uint8_t len[A_COUNT]; uint8_t fps[A_COUNT]; } CharArt;
typedef enum { SC_TITLE, SC_GAME, SC_MENU, SC_DIAG, SC_DEAD, SC_BAG, SC_ITEM, SC_TARGET,
  SC_SELECT, SC_CONFIRM, SC_SHEET, SC_INSPECT, SC_INFO, SC_OPTIONS, SC_AMULET, SC_RANKINGS, SC_CATALOG } Scene;
#define FRONT(s) ((1 << (s)) & (1 << SC_TITLE | 1 << SC_SELECT | 1 << SC_CONFIRM | 1 << SC_AMULET | 1 << SC_RANKINGS))   // the "ui" overlay's screens
#define OWN_INPUT(s) ((1 << (s)) & (1 << SC_TITLE | 1 << SC_SELECT | 1 << SC_CONFIRM | 1 << SC_AMULET | 1 << SC_RANKINGS | 1 << SC_CATALOG | 1 << SC_SHEET | 1 << SC_BAG | 1 << SC_ITEM | 1 << SC_MENU | 1 << SC_OPTIONS))    // screens whose overlay handles their own taps and buttons
typedef struct { int16_t x, y; uint32_t t0; uint8_t anim, uid; bool active; } Vis;   // t0: when the animation started
#define SP_H 35                                   // status_pane.png height
#define SC(v) ((v) * 3 / 2)                       // status pane scale
#define TB_H 38
#define TB_N 6
enum { TB_WAIT, TB_SEARCH, TB_INFO, TB_BAG, TB_Q2, TB_Q1 };
#define TEXT_BUF 720
#define BAG_COLS 5                                 // 0.18: a 5x5 grid filling the screen (weapon, armour, rings, gold; 19 slots; Exit)
#define BAG_ROWS 5
#define SLOT_W 35
#define SLOT_H 34
#define BIG_W 179                                  // the big windows' content width: 5 slots and their gaps
int bag_ntabs(void);                               // tabs under the backpack window (0: a selector without bags)
int bag_tab_kind(int t);
#define BAG_UI_TAB0 25
void blit_shrink(GRect r, int dx, int dy);       // blit.c (0.42)
#define BAG_ROWM 64                                // 0.42: s_sel from here: a whole row chosen (the buttons), by one of its cells
#define BAG_EXIT 24                                // the bottom-right cell: close the backpack
enum { SL_GOLD = -10, SL_KEY = -11, SL_NONE = -12, SL_EXIT = -13 };   // bag window cells that aren't game slots
#define TAB_W 36                                   // WndBag's tabs, grown to hold the bag icons at 2x
#define TAB_H 28
#define SHEET_TAB_W 50
#define SHEET_TAB_H 26                             // LabeledTab: room under the frame for a line of text
#define ARGB(c) ((c).argb)
#define WELL(x, y, w, h) blit_fill((x) - 2, (y), (w) + 4, (h), ARGB(GColorBlack))   // 0.18: black under a window's body (its title stays on the grey)

extern Sheet s_water, s_hero, s_mob[MT_COUNT], s_toolbar, s_logo, s_chrome, s_status, s_hpbar,
  s_arcs, s_play, s_avatars, s_banner, s_tag, s_tag_blue, s_buffs, s_icons, s_gameover;

enum { BI_POISON, BI_HUNGER, BI_STARVING, BI_CRIPPLE, BI_BLEEDING, BI_FIRE, BI_PARALYSIS, BI_LEVITATION, BI_MIND,
       BI_INVISIBLE, BI_FROST, BI_BLIND, BI_ROOTS, BI_SLOW, BI_BARKSKIN, BI_IMMUNITY, BI_CHARM, BI_HERBAL, BI_DEFERRED,
       BI_OOZE, BI_VERTIGO, BI_WEAKNESS, BI_HEALING, BI_AMOK, BI_TERROR, BI_LIGHT, BI_COMBO, BI_FURY, BI_SHADOWS, BI_RAGE, BI_COUNT };

// main.c, for the windows
Item *bag_item(int i);
bool bag_selectable(int i);
int bag_ui_to_slot(int i);
void cap_first(char *s);
int hero_buffs(uint8_t *out);
GRect info_rect(void);
void item_icon(const Item *it, int x, int y);
GRect item_rect(void);
int item_rows(void);                               // the item window's action buttons: two to a row
// the windows' input (0.18): an overlay returns CMD(c, arg) for the app to finish once it is out of the way
#define CMD(c, a) ((c) << 8 | (a))
enum { C_NONE, C_START, C_ITEM, C_BAG_CLOSE, C_DO, C_PAGE, C_MENU, C_JUMP };
enum { A_RETURN, A_SAVE, A_CATALOG, A_JOURNAL, A_MUSIC, A_SOUND, A_ASSIST, A_ITEMS, A_NEW, A_RANKINGS, A_MAIN };   // the game menu's buttons
extern Sheet s_micons;                             // menu_icons.png: the big buttons' pictures
int map_center_y(void);
const Sheet *mob_sheet(int type);
uint32_t now_ms(void);
void panel(GRect r);
void red_button(GRect r, bool selected);
void text(GContext *ctx, const char *s, GFont f, GColor c, int x, int y, int w, GTextAlignment a);
void text_outlined(GContext *ctx, const char *s, GFont f, GColor c, int x, int y, int w, GTextAlignment a);
void text_outlined_rect(GContext *ctx, const char *s, GFont f, GColor c, GRect r, GTextAlignment a);
void text_wrap(GContext *ctx, const char *s, GFont f, GColor c, GRect r);
void text_wrap_c(GContext *ctx, const char *s, GFont f, GColor c, GRect r);
extern GFont s_font_hud;
extern GFont s_font_log;
extern GFont s_font_big;
extern GFont s_font_tiny;
const CharArt *mob_art(int t);
extern Scene s_scene;
extern int s_sel;
extern bool s_has_save;
extern uint8_t s_save_depth;
extern uint8_t s_save_lvl;
extern uint32_t s_died_ms;
extern int s_sheet_tab;
extern int s_w;
extern int s_h;
extern int s_pick;
extern int s_pick_pending;
extern int s_opt;
extern int s_item_slot;
extern uint8_t s_acts[ACN_COUNT];
extern int s_n_acts;
extern char (*s_msg)[100];                 // 0.51: two lines on the heap (the app's size limit)
extern char s_info_title[40];
extern int s_info_mob;
extern char * s_text_buf;
extern int s_text_scroll;
extern int s_text_h;
extern int s_item_text_h;
extern int s_item_head;
extern int s_bag_tab;
extern int s_amulet; extern GRect s_btn_rect[2];
void redraw(void); void to_title(void); void open_select(void); // the front screens' input (ui overlay)
extern char s_wnd_title[48], s_wnd_b[2][36];
#define s_wnd_text s_text_buf
void item_spr(int sp, int x, int y, int z);
#ifdef PBL_ROUND
extern GTextAttributes *s_flow; extern GRect s_text_r;   // 0.35: the text layer's place, set by the window overlays
bool flow_set(GRect f); int flow_h(const char *t, GRect f);
#endif
#define ITEM_SPR(sp, x, y, z) item_spr(sp, x, y, z)
