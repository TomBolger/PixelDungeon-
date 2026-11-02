#pragma once
#include <stdint.h>
// The face's pacing (day-WL; pure, no game headers: face.inc includes it, bot/tests/pacetest.c checks it). Game speed 0 Fast (x1: the face's pacing as it always was), 1 Normal, 2 Relaxed:
// decisions between moves x1 / 1.6 / 2.5, a press in a window (they are slow already) x1 / 1.375 / 1.75. Battery saver: the play after a wake
// 20 s >> burst (0 Long, 1 Medium 10 s, 2 Short 5 s); the redraw interval fps: Full 33 ms, Reduced 66, Low 133 (one tile per frame at the walk's speed).
static const uint8_t FB_SPD[2][4] = { { 16, 26, 40, 40 }, { 16, 22, 28, 28 } };
static const uint8_t FB_FRM[4] = { 33, 66, 133, 133 };
static inline uint32_t fb_pace(uint32_t ms, int nav, int speed) { return ms * FB_SPD[nav][speed] >> 4; }
