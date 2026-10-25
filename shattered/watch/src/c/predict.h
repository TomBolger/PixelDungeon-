// Shattered PD on the watch - answering presses at once (see predict.c).
#pragma once
#include "scene.h"

#define PLAN_T 12                // targets planned
#define PLAN_S 32                // steps planned per walk (Controls.PLAN_STEPS)
typedef struct { int16_t x, y; uint8_t k; uint8_t dir[PLAN_S]; } PlanTarget;
typedef struct { bool on; uint16_t marker, hero, step_ms; uint8_t n, cur; PlanTarget t[PLAN_T]; } Plan;

Plan *predict_plan(void);                              // a new plan (C_PREDICT): read it into this,
void predict_set(void);                                // then say so
bool predict_button(int code, uint32_t now);           // a press: true if the screen was changed ahead of the phone
void predict_touch(void);                              // a touch: the plan no longer holds
bool predict_pos(const Obj *o, int i, uint32_t now, int32_t *v);   // a picture's place while it is being predicted
bool predict_offset(uint16_t id, uint32_t now, int32_t *dx, int32_t *dy);   // how far ahead of the phone a picture is shown
void predict_tick(uint32_t now);                       // hand back to the phone's frames once they agree
bool predict_active(void);
void predict_heard(uint32_t due, uint32_t now);       // a frame of the phone's was played (when it was due, and now)
void predict_lag(int32_t ms);
void predict_ready(int32_t x, int32_t y);              // the phone's hero is ready again, there (C_READY, on arrival)                          // how far behind the phone its frames play now (game ms)
bool predict_walking(uint32_t now, uint32_t *since);   // walking the hero ahead of the phone, since the press (a recent one)
void predict_reset(void);
