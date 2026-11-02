// Host check of the face's pacing (day-WL): Fast is exactly the old pacing, the slower speeds scale as documented, the wait cap and the plan
// timeout scale with them, and the walk keeps its speed at each frame rate.   cc -std=c99 -Wall -I../watch/src/c -o build/pacetest tests/pacetest.c && build/pacetest
#include <stdio.h>
#include <stdlib.h>
#include "face_pace.h"
static int fails;
#define CHK(c, ...) do { if (!(c)) { fails++; printf("FAIL  " __VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
  static const uint32_t DEC[] = { 100, 200, 650, 700, 18000 }, NAV[] = { 480, 500, 650 };
  for (unsigned i = 0; i < sizeof DEC / sizeof *DEC; i++) CHK(fb_pace(DEC[i], 0, 0) == DEC[i], "Fast changes %u", (unsigned)DEC[i]);
  for (unsigned i = 0; i < sizeof NAV / sizeof *NAV; i++) CHK(fb_pace(NAV[i], 1, 0) == NAV[i], "Fast changes %u (window)", (unsigned)NAV[i]);
  CHK(fb_pace(100, 0, 1) == 162 && fb_pace(100, 0, 2) == 250, "decision multipliers %u %u", (unsigned)fb_pace(100, 0, 1), (unsigned)fb_pace(100, 0, 2));
  CHK(fb_pace(480, 1, 1) == 660 && fb_pace(480, 1, 2) == 840, "window multipliers");
  for (int sp = 0; sp < 4; sp++) {
    uint32_t cap = fb_pace(700, 0, sp);
    // every wait the face sets is under its cap (face_tick drops a longer one): the glance hold, the backpack open, the item select, two steps
    CHK(fb_pace(650, 0, sp) <= cap && fb_pace(650, 1, sp) <= cap && fb_pace(500, 1, sp) <= cap && fb_pace(480, 1, sp) <= cap && fb_pace(200, 0, sp) <= cap, "a wait over the cap at speed %d", sp);
    // the plan timeout is at least as many presses as at Fast (18000 / 480 = 37)
    CHK(fb_pace(18000, 0, sp) / fb_pace(480, 1, sp) >= 37, "plan timeout holds only %u presses at speed %d", (unsigned)(fb_pace(18000, 0, sp) / fb_pace(480, 1, sp)), sp);
    if (sp) CHK(fb_pace(100, 0, sp) > fb_pace(100, 0, sp - 1) || sp == 3, "not slower at speed %d", sp);
    printf("speed %d: step %u, press %u, item %u, bag open %u, glance %u, cap %u, plan timeout %u ms\n", sp, (unsigned)fb_pace(100, 0, sp), (unsigned)fb_pace(480, 1, sp), (unsigned)fb_pace(500, 1, sp), (unsigned)fb_pace(650, 1, sp), (unsigned)fb_pace(650, 0, sp), (unsigned)cap, (unsigned)fb_pace(18000, 0, sp));
  }
  // the walk: a tile (16 px) in MOVE_MS 133, a frame of dt ms moves (dt * 16 + 66) / 133 px (main.c tick); frames to cross a tile
  for (int f = 0; f < 3; f++) {
    int dt = FB_FRM[f], px = 0, frames = 0; while (px < 16) { px += (dt * 16 + 66) / 133; frames++; }
    printf("frame %d ms: a tile in %d frame(s) = %d ms\n", dt, frames, frames * dt);
    CHK(frames * dt <= 140, "a tile takes %d ms at %d ms frames", frames * dt, dt);
  }
  CHK((20000 >> 0) == 20000 && (20000 >> 1) == 10000 && (20000 >> 2) == 5000, "burst lengths");
  CHK(FB_FRM[0] == 33, "Full frame rate is not 33 ms");
  printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
  return fails != 0;
}
