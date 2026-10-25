/*
 * Shattered PD on the watch - answering presses at once.
 * A press otherwise shows only when the phone's frames come back, a radio round trip later. The phone sends a plan
 * for the next press (Controls.plan, C_PREDICT): where the target box goes for each target, and the steps the hero
 * would walk to it. Down moves the box at once; Select starts the hero walking at once, the map following him.
 * The phone's own frames come later: while they are behind, the predicted place is shown; once they agree, they take
 * over again. If they disagree (a monster came into view and the hero stopped, the press did something else), the
 * picture slides back to the phone's in a moment.
 */
#include "predict.h"

enum { B_UP = 1, B_DOWN, B_SELECT, B_BACK, B_LONG = 16 };   // as main.c
#define CELL 128                 // one cell in the map's units x8 (16 px tiles)
#define EASE_MS 180              // sliding back after a disagreement
#define WAIT_MS 4000             // no sign of the phone doing it by then (game ms): give up
#define STILL_MS 200             // the phone's frames this long after his last step ended (game ms of the phone's own time,
                                 // four of its frames) show him still, short of the planned end: he stopped there

static Plan s_plan;
static bool s_wplanoff, s_wdone; // since the press: the phone's plan went (its hero set off), and came back (he is done)
// the target box, shown at a predicted place
static bool s_box; static int32_t s_bx, s_by, s_rx, s_ry; static uint32_t s_bt; static uint8_t s_bpend;   // (s_rx/ry: the phone's last place; s_bpend: presses it hasn't answered)
// the hero, walking a predicted path
static bool s_walk; static uint16_t s_wid; static int32_t s_wx0, s_wy0; static uint32_t s_wt;
static uint8_t s_wn; static uint8_t s_wdir[PLAN_S]; static uint16_t s_wstep, s_wmarker = NONE; static bool s_wmoved;
// sliding back
static uint16_t s_eid = NONE; static int32_t s_edx, s_edy; static uint32_t s_et;

static int dxof(int d) { return d % 3 - 1; }
static int dyof(int d) { return d / 3 - 1; }
static Obj *obj(uint16_t id) { return id < MAX_ID ? g_obj[id] : NULL; }
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

void predict_reset(void) { s_plan.on = false; s_box = s_walk = false; s_eid = NONE; }
bool predict_active(void) { return s_box || s_walk || s_eid != NONE; }
bool predict_walking(uint32_t now, uint32_t *since) { if (!s_walk || now - s_wt > 3000) return false; *since = s_wt; return true; }
void predict_touch(void) { s_plan.on = false; }

#ifdef TRACE_WALK
static uint16_t s_thero = NONE;
#endif
// A new plan (C_PREDICT) is read straight into the one in use (with 32 steps a walk, a second copy cost 400 bytes
// of the heap): predict_plan() before, predict_set() after
static bool s_was_on; static uint16_t s_was_marker; static uint8_t s_was_n, s_was_cur;
Plan *predict_plan(void) {
  s_was_on = s_plan.on; s_was_marker = s_plan.marker; s_was_n = s_plan.n; s_was_cur = s_plan.cur;
  memset(&s_plan, 0, sizeof s_plan);
  return &s_plan;
}
void predict_set(void) {
  Plan *p = &s_plan;
#ifdef TRACE_WALK
  if (p->on) s_thero = p->hero;
#endif
  bool keep = s_box && s_was_on && p->on && p->marker == s_was_marker && p->n == s_was_n;
  if (keep) s_plan.cur = s_was_cur;   // our own presses aren't in it yet: keep counting from ours
  // While the phone's hero walks there is no plan (he isn't ready); the next one says he is ready again: the walk is
  // over, here (he arrived, or something stopped him), even if nothing else is sent after it
  if (s_walk) { if (!p->on) s_wplanoff = true; else if (s_wplanoff) s_wdone = true; }
}

// The hero walks at the phone's own pace (a step each s_wstep) from the press on, whatever the radio does: over
// real Bluetooth the phone's frames come a few at a time, a round trip apart, and the phone's hero, followed as he
// was, went in jumps of a cell or two. (His first step used to be drawn out until the phone's hero set off: every
// walk began at 40% of the pace, then sped up.) He is never shown behind the phone's hero, nor further ahead than
// the phone's frames are behind it (from the press until he set off, or the play-out's delay) and LEAD steps more,
// so if the phone stops him early (a monster came into view) he only slides back a little.
#define LEAD 512                 // steps x256
static int32_t s_p0; static uint32_t s_t0;   // shown at s_p0 (steps x256) at s_t0, going on at the walking pace
static uint32_t s_lat = 500;     // the usual delay before the phone's hero sets off (game ms)
static int32_t s_plag;           // how far behind the phone its frames play now (main.c, game ms)
static uint32_t s_heard;         // the newest of the phone's frames played: when it was due here (its time on the phone,
                                 // in the watch's: a backlog played at once after a radio hold-up is still from then)
static uint32_t s_heard_now;     // when the phone's last frame was played (watch time)
void predict_heard(uint32_t due, uint32_t now) { s_heard = due; s_heard_now = now; }
void predict_lag(int32_t ms) { s_plag = ms < 0 ? 0 : ms; }
static int32_t raw_prog(int32_t rx, int32_t ry);
static int32_t s_endp = -1;      // the phone said its hero is ready again here (C_READY): the walk ends here
// The phone's hero is ready again at (x, y), as its message arrives (C_READY, at its head): the walk ends there. Seen
// only when its frames played (some 200 ms later) and his stop shown then, the hero walked on 3-4 cells past where a
// monster had stopped him and slid back. (The start of the walk is the last walk's end: a late word of that is no news.)
void predict_ready(int32_t x, int32_t y) {
  if (!s_walk) return;
  int32_t pr = raw_prog(x, y);
  if (pr > 0) s_endp = pr;
}

static void cell_at(int j, int32_t *x, int32_t *y) {   // the j-th cell of the path
  int32_t px = s_wx0, py = s_wy0;
  for (int k = 0; k < j && k < s_wn; k++) { px += dxof(s_wdir[k]) * CELL; py += dyof(s_wdir[k]) * CELL; }
  *x = px; *y = py;
}
static void prog_at(int32_t prog, int32_t *x, int32_t *y) {   // a place along the path (steps x256)
  if (prog < 0) prog = 0;
  if (prog > s_wn * 256) prog = s_wn * 256;
  int j = prog >> 8, f = prog & 255;
  cell_at(j, x, y);
  if (j < s_wn) { *x += dxof(s_wdir[j]) * CELL * f / 256; *y += dyof(s_wdir[j]) * CELL * f / 256; }
}
// how far along the path the phone's hero is (steps x256), -1: he's off it
static int32_t raw_prog(int32_t rx, int32_t ry) {
  int32_t best = -1, cx = s_wx0, cy = s_wy0;
  for (int j = 0; j <= s_wn; j++) {
    int32_t ex = iabs(rx - cx), ey = iabs(ry - cy), d = ex > ey ? ex : ey;
    if (j < s_wn) {
      int32_t nx = cx + dxof(s_wdir[j]) * CELL, ny = cy + dyof(s_wdir[j]) * CELL;
      bool on = rx >= (cx < nx ? cx : nx) - 8 && rx <= (cx > nx ? cx : nx) + 8 && ry >= (cy < ny ? cy : ny) - 8 && ry <= (cy > ny ? cy : ny) + 8;
      if (on) { int32_t f = d * 256 / CELL; if (f > 256) f = 256; best = j * 256 + f; }
      cx = nx; cy = ny;
    } else if (d <= 8) best = s_wn * 256;
  }
  return best;
}
static int32_t s_plast;          // where he was last shown (steps x256)
static uint32_t s_plast_t;       // ... and when
#define QUIET_MS 400             // no frame of the phone's played this long (game ms): the radio is held up
static int32_t shown_prog(uint32_t now) {
  int32_t p = s_p0 + (int32_t)((now - s_t0) * 256 / s_wstep);
  Obj *h = obj(s_wid);
  // (off the path, the phone's hero holds nothing back: he went elsewhere, and predict_tick slides back from here.
  // Held back to the start of the path instead, the hero leapt back to his second step at the end of every walk
  // longer than the plan)
  int32_t rp = h ? raw_prog(obj_dyn_raw(h, 0, now), obj_dyn_raw(h, 1, now)) : -1;
  if (rp >= 0) {
    uint32_t behind = (uint32_t)s_plag > s_lat ? (uint32_t)s_plag : s_lat;
    // (until the phone's hero sets off, a step less: if the phone does something else, less to take back)
    int32_t lead = (s_wmoved ? LEAD : LEAD / 2) + (int32_t)(behind * 256 / s_wstep);
    // Held back where he is (never pulled back: the allowance shrinks as the delay does), the pace going on from
    // there without a jump later. Not while the radio is held up (a lost message stops everything for 3 s): he
    // walks on as planned, rather than standing 3 s and then leaping to wherever the phone's hero had got to.
    bool quiet = now - s_heard_now > QUIET_MS;
    int32_t hold = rp + lead > s_plast ? rp + lead : s_plast;
    if (p > hold && !quiet) { p = hold; s_p0 = p; s_t0 = now; }
    else if (p < rp) { p = rp; s_p0 = p; s_t0 = now; }
  }
  p = p < 0 ? 0 : p > s_wn * 256 ? s_wn * 256 : p;
  if (s_endp >= 0 && p > s_endp) {
    // past where the phone's hero stopped: back to it, at twice the pace, and there he stays
    int32_t back = s_plast - (int32_t)((now - s_plast_t) * 512 / s_wstep);
    p = back > s_endp ? (back < p ? back : p) : s_endp;
    s_p0 = p; s_t0 = now;
  }
  s_plast = p; s_plast_t = now;
  return p;
}
static void walk_at(uint32_t now, int32_t *x, int32_t *y) { prog_at(shown_prog(now), x, y); }

bool predict_button(int code, uint32_t now) {
  bool down = (code & 15) == B_DOWN, select = code == B_SELECT;
  if (!down && !select) { s_plan.on = false; return false; }
  if (!s_plan.on || s_walk || !s_plan.n) { s_plan.on = false; return false; }
  if (down) {
    int n = s_plan.n, c = (s_plan.cur + ((code & B_LONG) ? n - 1 : 1)) % n;
    PlanTarget *t = &s_plan.t[c];
    if (t->x == 0x7FFF || !obj(s_plan.marker)) { s_plan.on = false; return false; }
    s_plan.cur = c;
    Obj *m = obj(s_plan.marker);
    if (!s_box) { s_rx = obj_dyn_raw(m, 0, now); s_ry = obj_dyn_raw(m, 1, now); s_bpend = 0; }
    s_box = true; s_bx = t->x; s_by = t->y; s_bt = now; s_bpend++;
    APP_LOG(APP_LOG_LEVEL_INFO, "predict: box to target %d", c);
    return true;
  }
  PlanTarget *t = &s_plan.t[s_plan.cur < s_plan.n ? s_plan.cur : 0];
  Obj *h = obj(s_plan.hero);
  s_plan.on = false;                                     // (whatever Select does, the next plan comes from the phone)
  if (!t->k || !h) return false;
  s_wid = s_plan.hero; s_wmarker = s_plan.marker; s_box = false; s_wx0 = obj_dyn(h, 0, now); s_wy0 = obj_dyn(h, 1, now);
  s_wt = now; s_wn = t->k; memcpy(s_wdir, t->dir, s_wn); s_wstep = s_plan.step_ms ? s_plan.step_ms : 100;
  s_wmoved = false; s_walk = true; s_p0 = 0; s_t0 = now; s_plast = 0; s_plast_t = now; s_wplanoff = s_wdone = false; s_endp = -1;
  APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk %d steps", s_wn);
  // the map follows him closely while he walks, as the phone's camera will (HeroSprite.move: panFollow at 20 a
  // second). Left at its resting pace until the phone's frames said so, the view lagged behind the hero for the
  // first half second of every walk, then caught up with a lurch.
  // A map standing still (SPD's zoom stops its camera) or moved over to show a target (Controls.view) follows him
  // from the press too, as the phone's will (Select gives its camera back to the hero): it stood still under the
  // walk shown ahead, the hero walking off towards the edge till the phone's frames came.
  if (g_map_cam >= 0) {
    Cam *c = &g_cam[g_map_cam];
    if (c->mode == 1 && c->follow == s_wid) c->intensity = 255;
    else if (c->mode != 1) { c->mode = 1; c->follow = s_wid; c->ofx = c->ofy = 0; c->dead = 0; c->intensity = 255; c->cx = c->cy = 0; }
  }
  if (s_eid == s_wid) s_eid = NONE;
  return true;
}

bool predict_offset(uint16_t id, uint32_t now, int32_t *dx, int32_t *dy) {
  Obj *o = obj(id);
  if (!o || !s_walk || id != s_wid) return false;
  int32_t x, y; walk_at(now, &x, &y);
  *dx = x - obj_dyn_raw(o, 0, now); *dy = y - obj_dyn_raw(o, 1, now);
  return true;
}

bool predict_pos(const Obj *o, int i, uint32_t now, int32_t *v) {
  if (i == 2) { if (s_walk && o == obj(s_wmarker)) { *v = 0; return true; } return false; }   // the box goes while he walks (as on the phone)
  if (s_walk && o == obj(s_wid)) { int32_t x, y; walk_at(now, &x, &y); *v = i ? y : x; return true; }
  if (s_box && o == obj(s_plan.marker)) { *v = i ? s_by : s_bx; return true; }
  if (s_eid != NONE && o == obj(s_eid)) {
    uint32_t el = now - s_et;
    if (el >= EASE_MS) return false;
    *v = obj_dyn_raw(o, i, now) + (i ? s_edy : s_edx) * (int32_t)(EASE_MS - el) / EASE_MS;
    return true;
  }
  return false;
}

static void slide_back(uint16_t id, int32_t px, int32_t py, uint32_t now) {
  Obj *o = obj(id);
  if (!o) { s_eid = NONE; return; }
  s_eid = id; s_et = now; s_edx = px - obj_dyn_raw(o, 0, now); s_edy = py - obj_dyn_raw(o, 1, now);
}

#ifdef TRACE_WALK
// (timing tests) the hero's place as drawn, the phone's, and the map's scroll, whenever one changes
static void trace(uint32_t now) {
  static int32_t lx, ly, lsx; Obj *h = obj(s_thero);
  if (!h) return;
  int32_t x = obj_dyn(h, 0, now) / 8, y = obj_dyn(h, 1, now) / 8, sx = g_map_cam >= 0 ? g_cam[g_map_cam].sx / 2048 : 0;
  if (x == lx && y == ly && sx == lsx) return;
  lx = x; ly = y; lsx = sx;
  APP_LOG(APP_LOG_LEVEL_INFO, "TW %lu %ld %ld raw %ld %ld cam %ld walk %d", (unsigned long)now, (long)x, (long)y, (long)(obj_dyn_raw(h, 0, now) / 8), (long)(obj_dyn_raw(h, 1, now) / 8), (long)sx, s_walk);
}
#endif

void predict_tick(uint32_t now) {
#ifdef TRACE_WALK
  trace(now);
#endif
  if (s_eid != NONE && (now - s_et >= EASE_MS || !obj(s_eid))) s_eid = NONE;
  if (s_box) {
    Obj *m = obj(s_plan.marker);
    if (!m) s_box = false;
    else {
      int32_t rx = obj_dyn_raw(m, 0, now), ry = obj_dyn_raw(m, 1, now);
      if (rx != s_rx || ry != s_ry) { s_rx = rx; s_ry = ry; if (s_bpend) s_bpend--; }   // the phone has answered a press
      if ((iabs(rx - s_bx) <= 4 && iabs(ry - s_by) <= 4) || !s_bpend) {   // it agrees, or it answered them all (its place stands)
        s_box = false; APP_LOG(APP_LOG_LEVEL_INFO, "predict: box %s after %d ms", s_bpend ? "confirmed" : "answered", (int)(now - s_bt)); }
    }
    if (s_box && now - s_bt > WAIT_MS) { s_box = false; APP_LOG(APP_LOG_LEVEL_INFO, "predict: box not confirmed"); }   // it never did: its place stands
  }
  if (s_walk) {
    Obj *h = obj(s_wid);
    if (!h) { s_walk = false; return; }
    int32_t rx = obj_dyn_raw(h, 0, now), ry = obj_dyn_raw(h, 1, now), px, py, fx = s_wx0, fy = s_wy0;
    walk_at(now, &px, &py);
    if (s_endp >= 0) prog_at(s_endp, &fx, &fy);   // (where the phone said he stopped)
    else for (int j = 0; j < s_wn; j++) { fx += dxof(s_wdir[j]) * CELL; fy += dyof(s_wdir[j]) * CELL; }
    if (!s_wmoved && (iabs(rx - s_wx0) > 8 || iabs(ry - s_wy0) > 8)) {   // the phone's hero has set off
      s_wmoved = true;
      uint32_t l = now - s_wt; if (l > 1500) l = 1500;
      s_lat = (3 * s_lat + l) / 4;
    }
    // the phone's hero is ready again (predict_set): he is where he stays. (Waiting for frames to show him still was
    // slow, and when the phone then had nothing more to send, the hero stood a cell or two too far for seconds.)
    if (s_wdone) {
      s_walk = false; slide_back(s_wid, px, py, now);
      APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk over, %d ms after the press, back %d,%d", (int)(now - s_wt), (int)((px - rx) / 8), (int)((py - ry) / 8));
      return;
    }
    if (s_wmoved && raw_prog(rx, ry) < 0) { s_walk = false; slide_back(s_wid, px, py, now); APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk went elsewhere"); return; }
    // (over a slow radio his frames come late, already past: he goes from place to place without ever moving; any
    // change of place counts as moving)
    static int32_t s_lrx, s_lry;
    bool moved = rx != s_lrx || ry != s_lry; s_lrx = rx; s_lry = ry;
    if (s_wmoved && !obj_moving(h, now) && !moved) {
      if (iabs(rx - fx) <= 16 && iabs(ry - fy) <= 16) {   // arrived: the same place
        s_walk = false; slide_back(s_wid, px, py, now); APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk confirmed, %d ms after the press", (int)(now - s_wt)); return; }
      // it stopped short: the phone's frames from well after his last step ended still show him there. (Counted in
      // the phone's own time, its frames as played: counted in the watch's, as it was, a late message played at once
      // looked like a long time still, and real stops were seen only 400 ms on, the hero shown walking on past)
      uint32_t end = h->at + h->until;
      if (s_heard - end < 0x80000000u && s_heard - end > STILL_MS) {
        s_walk = false; slide_back(s_wid, px, py, now); APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk stopped short, back %d,%d", (int)((px - rx) / 8), (int)((py - ry) / 8)); return; }
    }
    // never set off: the phone did something else with the press (a monster came into view at once, say). Seen when
    // its frames from well after the press come with him still. (Not merely when nothing comes: a message lost on the
    // radio holds everything up 3 s, and giving up then took him back to the start, to leap on again when the
    // phone's frames came.) At the latest after 2 WAIT_MS
    bool idle = !s_wmoved && s_heard - s_wt < 0x80000000u && s_heard - s_wt > (uint32_t)s_plag + 600;
    if ((!s_wmoved && now - s_wt > 2 * WAIT_MS) || idle || now - s_wt > (uint32_t)s_wn * s_wstep + 5000 + 3 * WAIT_MS) {
      s_walk = false; slide_back(s_wid, px, py, now); APP_LOG(APP_LOG_LEVEL_INFO, "predict: walk not confirmed (moved %d)", s_wmoved); }
  }
}
