// Shattered PD's pictures, stored compressed in the ART resource (shattered/tools/pack_art.py),
// or made at run time and sent by the phone (fog of war, glows).
#pragma once
#include <pebble.h>

void art_init(void);
// size as the phone knows it (before any shrinking for the watch)
bool art_size(int tex, int *w, int *h);
// how many fw x fh tiles the picture holds
int art_cells(int tex, int fw, int fh);
// how much the stored picture was shrunk (texture coordinates on the wire are divided by this)
int art_shrink(int tex);

// A rectangle of pixels (Pebble colour bytes) to draw from: call art_row for each source row.
typedef struct {
  int tex, x, y, w, h;      // in stored pixels
  const uint8_t *px;        // whole rectangle (cached or run-time picture), or NULL: rows come one at a time
  int stride;
} ArtSrc;

bool art_open(ArtSrc *s, int tex, int x, int y, int w, int h);
const uint8_t *art_row(ArtSrc *s, int row);    // row 0..h-1 of the rectangle, w pixels

void art_forget(int tex);
void art_forget_dynamic(void);
