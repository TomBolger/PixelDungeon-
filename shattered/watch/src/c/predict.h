// Shattered PD on the watch - answering presses at once (see predict.c).
#pragma once
#include "scene.h"

#define PLAN_T 12                // targets planned
#define PLAN_S 12                // steps planned per walk
typedef struct { int16_t x, y; uint8_t k; uint8_t dir[PLAN_S]; } PlanTarget;
typedef struct { bool on; uint16_t marker, hero, step_ms; uint8_t n, cur; PlanTarget t[PLAN_T]; } Plan;

void predict_set(const Plan *p);                       // a new plan (C_PREDICT)
bool predict_button(int code, uint32_t now);           // a press: true if the screen was changed ahead of the phone
void predict_touch(void);                              // a touch: the plan no longer holds
bool predict_pos(const Obj *o, int i, uint32_t now, int32_t *v);   // a picture's place while it is being predicted
bool predict_offset(uint16_t id, uint32_t now, int32_t *dx, int32_t *dy);   // how far ahead of the phone a picture is shown
void predict_tick(uint32_t now);                       // hand back to the phone's frames once they agree
bool predict_active(void);
void predict_reset(void);
