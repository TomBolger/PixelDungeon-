// The watch's copy of Shattered PD's screen, kept up to date by the phone (see server/src/pebxel/Link.java).
#pragma once
#include <pebble.h>

// commands
enum { C_RESET = 1, C_CAM, C_NEW, C_SET, C_DEL, C_ORDER, C_TILES, C_DTEX, C_MOVE, C_ANIM, C_TIME, C_INPUT, C_LATEST, C_SOUND, C_COMMIT, C_PREDICT };   // C_SOUND: kind (0 effect, 1 music, 2 settings), value; C_PREDICT: the plan for the next press (predict.c)
// picture kinds
enum { K_IMG = 1, K_RECT, K_NINE, K_TILES, K_TEXT, K_SKIN, K_QUADS, K_SHAPE };
enum { F_FLIPH = 1, F_FLIPV = 2, F_ANIM = 16, F_FOG = 32 };   // F_FOG: the fog of war (draw.c draw_fog)
#define NONE 0xFFFF

#define MAX_ID 1024
#define MAX_CAM 16

// one picture
typedef struct Obj {
  uint8_t kind, cam, tex, flags;
  uint16_t prev, next;
  int16_t ox, oy, w, h;          // origin and unscaled size (x8)
  int16_t tx, ty, tw, th;        // texture rectangle (or animation id in tx)
  uint8_t col[7];                // multiply r g b, add r g b a (0-255)
  uint8_t frame, timer;          // animation position when last told
  // moving parts and their speeds per second: x, y (x8), alpha, scale x/y (x256), angle (1/64 deg), water offset x/y (x8)
  int16_t dyn[8], vel[8];
  uint32_t at;                   // when they were told (ms)
  uint16_t until;                // the speeds stop this long after (ms; 0: they don't)
  void *ext;                     // text (char*), tile cells, nine margins, rect colour
} Obj;

typedef struct {
  int16_t x, y, w, h;            // viewport, watch px x8
  uint16_t zoom;                 // watch px per unit x256
  int32_t sx, sy;                // scroll, units x8 (x256 for smooth following)
  uint8_t mode, used;
  uint16_t follow;
  int16_t ofx, ofy, px, py;      // follow offset / pan point, x8
  uint8_t intensity, dead;       // x16, x255
} Cam;

typedef struct {
  uint8_t tex, n, looped, _pad;
  uint16_t delay_ms;
  int16_t rects[];               // n x (x, y, w, h)
} Anim;

typedef struct {                 // a tile map's cells
  uint8_t cw, ch, fw, fh, mw, mh;
  uint8_t wide;                  // cells stored as uint16 (texture has more than 255 tiles)
  uint8_t _pad;
  uint8_t cells[];
} Tiles;

typedef struct {                 // a picture made at run time (fog of war, glows)
  uint16_t w, h;
  uint8_t bits;                  // 2 or 4: palette indices packed per byte; 8: plain colour bytes
  uint8_t ncol;
  uint8_t pal[16];
  uint8_t px[];
} DynTex;
// one pixel of a run-time picture, as a Pebble colour
static inline uint8_t dyn_px(const DynTex *d, int i) {
  if (d->bits == 8) return d->px[i];
  if (d->bits == 4) return d->pal[(d->px[i >> 1] >> ((i & 1) * 4)) & 15];
  return d->pal[(d->px[i >> 2] >> ((i & 3) * 2)) & 3];
}

extern Obj *g_obj[MAX_ID];
extern Cam g_cam[MAX_CAM];
extern uint16_t g_head;
extern bool g_dirty;             // a complete update arrived: redraw
extern bool g_partial;           // a frame is half applied (its end is still on the way): keep showing the last whole one
extern bool g_lost;              // the copy can't go on (out of memory, or out of step with the phone): main.c starts over

// applies commands up to the next time stamp (C_TIME) after the start; returns the bytes used
int scene_apply(const uint8_t *data, int len, uint32_t now);
void scene_reset(void);
Anim *scene_anim(int id);
DynTex *scene_dyntex(int id);
int32_t obj_dyn(const Obj *o, int i, uint32_t now);       // as shown (a press may be shown ahead of the phone)
int32_t obj_dyn_raw(const Obj *o, int i, uint32_t now);   // as the phone last said
bool obj_moving(const Obj *o, uint32_t now);
bool scene_moving(uint32_t now);
void cams_follow(uint32_t now, uint32_t dt);

// touch (see main.c): the map's camera, and the map moved by the finger ahead of the phone (cam units x256)
extern int8_t g_map_cam;
extern int32_t g_pan_x, g_pan_y;
int scene_hit(int x, int y, uint32_t now);   // the camera of the topmost picture under (x, y), -1 none
void touch_acked(uint8_t seq);               // main.c: the phone has applied touches up to seq
