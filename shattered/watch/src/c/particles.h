// Shattered PD on the watch - particles it makes itself (see particles.c, and the phone's pebxel/Particles.java).
#pragma once
#include "scene.h"

#define MAX_RECIPE 64            // (Link.RECIPES)

typedef struct {                 // how one emitter's particles look and move (C_RECIPE)
  uint8_t flags, tex, tw, th;
  uint16_t tx, ty;
  uint16_t life0, life1;         // game ms
  uint16_t colA, colB;           // multiply r g b, add r g b: 2 bits each
  uint8_t s[4], mul0, mul1, a[4];   // size curve (birth, knee age, knee, death), each one's factor (x64), alpha curve
  int16_t v[4];                  // box: x lowest, highest, y lowest, highest | angle start, span, speed lowest, highest
  int16_t ax, ay;                // pull, x8 units a second squared
  int8_t ox, oy, back;           // start offset (x8), back along the path (/64)
} Recipe;
enum { R_FRAME = 1, R_FLICKER = 2, R_RANDOM = 4, R_FADE = 8, R_BOX = 16, R_PULL = 32 };

typedef struct {                 // an emitter (K_EMIT picture's ext)
  uint8_t recipe, flags, quantity, starts, pre, fresh;
  uint16_t follow, interval, count;
  int16_t w, h;
  uint32_t next;                 // when the next particle is due (game ms)
  uint8_t cs, cl, ct, cw, ch; int8_t bound[4];   // over cells (flags & 4); the part of a cell they start in, 1/64 cell
  uint8_t bits[];
} Emit;
enum { E_FILL = 1, E_ON = 2, E_CELLS = 4, E_SIGHT = 8 };   // E_SIGHT: only in cells in sight (the fog of war)

typedef struct {                 // one particle to draw
  int32_t x, y;                  // its centre, camera units x8
  int32_t size;                  // square: its side (x8); a frame: its scale (x4096)
  uint8_t col[7];                // multiply r g b, add r g b, 0
  uint8_t alpha;
} PDraw;

void recipe_set(int id, const uint8_t *b, int len);
const Recipe *recipe_get(int id);
void recipes_reset(void);
void particles_reset(void);
void particles_free(uint16_t owner);
// makes the particles due by now (from an emitter whose ext is an Emit), then hands them out one by one
void emit_update(uint16_t id, Obj *o, uint32_t now);
bool emit_next(uint16_t id, uint32_t now, int *iter, PDraw *d);
