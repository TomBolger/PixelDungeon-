// Shattered PD on the watch: the game runs on the phone; this app shows its screen and sends the buttons.
#include <pebble.h>
#include "scene.h"
#include "art.h"
#include "draw.h"
#include "snd.h"
#include "predict.h"

enum { KEY_DATA = 1, KEY_HELLO = 2, KEY_BUTTON = 3, KEY_STATS = 4, KEY_VERSION = 5, KEY_TOUCH = 6, KEY_SCREEN = 7 };
#define GAME_SPEED_NUM 3   // 1.5 x real time: must match Game.pebxelSpeed (WatchGame.java)
#define GAME_SPEED_DEN 2
#define PROTOCOL 10  // must match Link.PROTOCOL on the phone
enum { B_UP = 1, B_DOWN, B_SELECT, B_BACK, B_LONG = 16 };

static Window *s_win;
static Layer *s_layer;
static AppTimer *s_timer;
static uint32_t s_last, s_input_at, s_data_at, s_frame_end;
static bool s_connected;
static bool s_wait_reset;   // the copy was given up (resync): the phone's messages wait until its fresh start comes

// The wall clock in ms only goes forward here: it can be set back (the phone syncing the time), which would otherwise
// hold up the play-out queue until it caught up again.
// Its seconds and milliseconds can also disagree for a moment at the turn of a second: a reading comes out a whole
// second off (the emulator does it a few times a minute: +1000 one frame, back the next). Taken as it came, that pushed
// the watch's time a second ahead for good: everything on screen jumped 1.5 s at once, and frames waiting played in a
// rush. So a reading about a whole second away from the last waits: if the clock goes on from it for 100 ms, it was a real gap.
static uint32_t now_ms(void) {
  static uint32_t mono, raw_last, odd_raw; static bool started; static uint8_t odd;
  time_t s; uint16_t ms; time_ms(&s, &ms);
  uint32_t raw = (uint32_t)s * 1000 + ms;
  int32_t d = (int32_t)(raw - raw_last), e = d > 0 ? d - 1000 : d + 1000;
  if (started && e > -100 && e < 100) {
    int32_t g = (int32_t)(raw - odd_raw);
    if (!odd || g < 0 || g > 1000) { odd = 1; odd_raw = raw; goto out; }
    if (g < 100) goto out;              // (it has to go on from there for a while: a slip lasts a few ms, and a busy
  }                                     // loop can read the clock many times within one)
  odd = 0;
  if (started && d > 0 && d < 60000) mono += d;
  raw_last = raw; started = true;
out:
  // the game's clock: its animations run GAME_SPEED times real time (Game.pebxelSpeed on the phone, which must match);
  // frame stamps, moves and touches are all in that time
  return (uint32_t)((uint64_t)mono * GAME_SPEED_NUM / GAME_SPEED_DEN);
}

// the least heap free since the last report, and allocations that failed (scene.c g_oom)
static uint32_t s_minfree = 0xFFFFFFFF;
static void note_free(void) { uint32_t f = heap_bytes_free(); if (f < s_minfree) s_minfree = f; }

static void update(Layer *l, GContext *ctx) {
  uint32_t now = now_ms();
  if (g_head == NONE) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_wait_reset ? "Loading..." : s_connected ? "Waiting for the game..." : "Open Shattered PD on your phone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(20, PBL_DISPLAY_HEIGHT / 2 - 24, PBL_DISPLAY_WIDTH - 40, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }
  predict_tick(now);
  draw_scene(ctx, layer_get_bounds(l), now);
  note_free();
  uint32_t drawn = now_ms();
  if (drawn - now > 300) APP_LOG(APP_LOG_LEVEL_WARNING, "slow frame: %d ms (game time)", (int)(drawn - now));   // (rare: worth knowing)
  s_frame_end = drawn;
  sound_poll();
  static int frames; static uint32_t spent, spent_snd;
  spent += drawn - now; spent_snd += now_ms() - drawn;
  if (++frames % 20 == 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "draw: %d ms per frame (sound %d)", (int)(spent / 20), (int)(spent_snd / 20)); spent = spent_snd = 0;
#ifdef PROFILE
    extern uint32_t g_prof[8];
    APP_LOG(APP_LOG_LEVEL_INFO, "img %d rect %d nine %d tiles %d text %d skin %d; strips decoded %d per frame", (int)g_prof[1] / 20, (int)g_prof[2] / 20, (int)g_prof[3] / 20, (int)g_prof[4] / 20, (int)g_prof[5] / 20, (int)g_prof[6] / 20, art_decodes() / 20);
    memset(g_prof, 0, sizeof(g_prof));
#endif
  }
}

// keep drawing while something moves or animates; stop (and save power) when the screen is still
// A breather after every frame (as in Pebble PD): the system task that reads the touch screen (and refills the
// speaker) runs below the app, only while the app waits. Frames slower than the timer followed each other at once and
// starved it: touches came seconds late or not at all (a quick tap was over before it was read) while anything moved.
#define MIN_IDLE (10 * GAME_SPEED_NUM / GAME_SPEED_DEN)   // (10 ms real)
static void tick(void *data) {
  s_timer = NULL;
  uint32_t since = now_ms() - s_frame_end;
  if (since < MIN_IDLE) { s_timer = app_timer_register((MIN_IDLE - since) * GAME_SPEED_DEN / GAME_SPEED_NUM + 1, tick, NULL); return; }
  uint32_t now = now_ms(), dt = now - s_last; if (dt > 200) dt = 200;
  s_last = now;
  cams_follow(now, dt);
  // (a frame only half here isn't drawn: on a floor change the new floor came in over several seconds of radio, a
  // sheet of water first, then walls, then the fog; the loading screen stays until the whole floor is here)
  if (!g_partial) layer_mark_dirty(s_layer);
#ifndef STILL
  // power saving (as in the Pixel Dungeon port): idle animations stop 10 s after the last button
  // press, unless the game is still sending something new
  bool awake = now - s_input_at < 15000 || now - s_data_at < 2250;   // (game time: 10 s and 1.5 s real)
  if (awake && scene_moving(now)) s_timer = app_timer_register(40, tick, NULL);
#endif
}

static void kick(void) { if (!s_timer) { s_last = now_ms(); s_timer = app_timer_register(1, tick, NULL); } }

// ---- play-out: each frame's changes at the pace they happened on the phone ----
// Messages arrive in bursts (one per radio round trip), each holding several game frames stamped with
// the phone's clock. They wait here and are played a steady while behind, so movements come out smooth
// rather than in jumps.
// The delay stays put: it was set by each message as it came (the newest frame played as it arrived), and since
// every message comes a little sooner or later than the last (its size, the radio), everything on screen kept
// speeding up and slowing down, even on the emulator's instant link (a walking hero went 160, 125, 110, 100 px/s
// within each step). Now it covers how late most messages come lately and moves only slowly (frames play at most
// 1/DRIFT quicker or slower), so the pace on screen is the phone's; a message later than that plays as it comes.
typedef struct Msg { struct Msg *next; uint16_t len, pos; uint8_t data[]; } Msg;
static Msg *s_q, *s_qtail;
static int s_qbytes;
static int32_t s_off;        // watch time = phone time + s_off (enough to have heard of a frame before playing it)
static int32_t s_base;       // the quickest messages lately: watch time - phone time of their newest frame
static int32_t s_jit;        // how much later than the quickest the slower messages come lately (most of them)
static uint32_t s_off_at;    // when s_off last moved
static bool s_have_off;
static uint32_t s_cur_at;    // when the frame being played happened, in watch time
static uint32_t s_cur_stamp; // ... and in phone time
static AppTimer *s_ptimer;
static bool s_flush;         // play everything waiting now, due or not
#define MARGIN 40            // ms of slack for uneven radio
#define QMAX 6000            // more waiting than this (or memory running short): catch up at once
// the inbox comes out of the heap; the phone sends at most 2.4 KB at a time (Pacer.CAP: a slow radio gains
// nothing from bigger ones), so 8 KB of it was 4 KB of memory the dungeon could use
#define INBOX 4096
#define LATE_MAX 450         // ms (game time) of delay kept for slow messages beyond the quickest: a message later
                             // than that (one sent again after a refusal or a loss, a big one) plays as it comes,
                             // rather than holding everything after it back as late (that played "fast then slow")
#define DRIFT 16             // the delay moves at most 1/16 of the time passing (6%: too little to see)
#define SNAP 900             // ...unless it is this far off (game ms): the phone's clock jumped, or stood still

// the delay towards where the messages lately want it (at most DRIFT's pace, a jump of the phone's clock at once)
static void off_move(uint32_t now) {
  int32_t want = s_base + MARGIN + s_jit, d = want - s_off, step = (int32_t)(now - s_off_at) / DRIFT;
  s_off_at = now;
  if (d > SNAP || d < -SNAP) s_off = want;
  else s_off += d > step ? step : d < -step ? -step : d;
  predict_lag(s_off - s_base);
}

static uint32_t stamp_at(const uint8_t *d) { return d[1] | d[2] << 8 | d[3] << 16 | (uint32_t)d[4] << 24; }

static void queue_clear(void) {
  while (s_q) { Msg *m = s_q; s_q = m->next; free(m); }
  s_qtail = NULL; s_qbytes = 0; s_have_off = false;
}

static void pump(void *data);
static void hello(void *data);
static bool s_resyncing;   // asking for the whole screen again (a picture was lost): the presses waiting are kept

// A picture couldn't be kept (memory ran out, scene.c g_lost): from then on the watch's copy of the screen is wrong
// (a picture missing for good, or worse, every later message about it cut short). Ask the phone for the whole
// screen again, as when the app starts; at most every 5 s.
static void resync(void *data) {
  static uint32_t s_asked;
  uint32_t now = now_ms();
  if (!g_lost || (s_asked && now - s_asked < 5000 * GAME_SPEED_NUM / GAME_SPEED_DEN)) return;
  s_asked = now; g_lost = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "a picture was lost (free %d): asking for the whole screen", (int)heap_bytes_free());
  // the copy goes now (its memory back at once; "Loading..." meanwhile), and what the phone sent before it heard
  // the hello is skipped (inbox, s_wait_reset): it builds on the copy given up
  scene_reset();
  s_wait_reset = true;
  if (s_layer) layer_mark_dirty(s_layer);
  s_connected = false; s_resyncing = true;
  hello(NULL);
}

static void pump_at(uint32_t due, uint32_t now) {
  if (s_ptimer) app_timer_cancel(s_ptimer);
  s_ptimer = app_timer_register(due > now ? (due - now) * GAME_SPEED_DEN / GAME_SPEED_NUM + 1 : 1, pump, NULL);   // (real ms)
}

// play every frame that is due
static void pump(void *data) {
  s_ptimer = NULL;
  uint32_t now = now_ms();
  bool rush = s_flush || s_qbytes > QMAX || heap_bytes_free() < 10000;
  uint32_t t0 = now;
  if (s_have_off) off_move(now);
  while (s_q) {
    Msg *m = s_q;
    if (m->pos < m->len && m->data[m->pos] == C_TIME && m->len - m->pos >= 5) {
      uint32_t due = stamp_at(m->data + m->pos) + s_off;
      if ((int32_t)(due - now) > 0 && !rush) { pump_at(due, now); return; }
      // (a frame that comes late plays as at most LATE_MAX late: its pictures' guessed moves don't run on for
      // the whole of a radio hiccup)
      s_cur_at = rush ? now : (int32_t)(now - due) > LATE_MAX ? now - LATE_MAX : due;
      s_cur_stamp = stamp_at(m->data + m->pos);
    }
    int used = scene_apply(m->data + m->pos, m->len - m->pos, s_cur_at);
    m->pos = used > 0 ? m->pos + used : m->len;
    predict_heard(now);
    note_free();
    if (g_dirty) { g_dirty = false; kick(); }
    if (m->pos >= m->len) { s_q = m->next; if (!s_q) s_qtail = NULL; s_qbytes -= m->len; free(m); }
  }
  if (g_lost) app_timer_register(1, resync, NULL);   // (not from in here: it empties the queue)
  if (now_ms() - t0 > 300) APP_LOG(APP_LOG_LEVEL_WARNING, "slow messages: %d ms (game time)", (int)(now_ms() - t0));
}

static void enqueue(const uint8_t *data, int len, uint32_t now) {
  if (len >= 5 && data[0] == C_LATEST) {
    // how late this message is: its newest frame (the stamp up front), and its oldest (the first frame's), which
    // waited longest on the phone for the radio
    int32_t lat = (int32_t)(now - stamp_at(data));
    int32_t old = len >= 10 && data[5] == C_TIME ? (int32_t)(now - stamp_at(data + 5)) : lat;
    // the quickest lately: down at once, up slowly (the phone's clock may fall behind, when it stalls)
    if (!s_have_off || lat < s_base) s_base = lat; else s_base += (lat - s_base) / 32;
    // the slower ones: up quickly, down slowly (it settles where few come later than it)
    int32_t need = old - s_base; if (need < 0) need = 0; if (need > LATE_MAX) need = LATE_MAX;
    if (!s_have_off) s_jit = need;
    else if (need > s_jit) s_jit += (need - s_jit + 3) / 4; else s_jit -= (s_jit - need) / 32;
    if (!s_have_off) { s_off = s_base + MARGIN + s_jit; s_off_at = now; }
    s_have_off = true;
    off_move(now);
  }
  Msg *m = malloc(sizeof(Msg) + len);
  // short of memory: the pictures' cache gives some back (it can decode them again; a message can't be had again)
  if (!m) m = art_alloc(sizeof(Msg) + len);
  if (!m) {
    // still none: play everything waiting at once, then this message, all of it. (Only its first frame was played
    // here, in fact only its header: the rest was lost, and with it pictures the next messages speak of.)
    s_flush = true; pump(NULL); s_flush = false;
    s_cur_at = now;
    for (int pos = 0; pos < len; ) { int used = scene_apply(data + pos, len - pos, now); pos += used > 0 ? used : len; }
    if (g_dirty) { g_dirty = false; kick(); }
    if (g_lost) app_timer_register(1, resync, NULL);
    return;
  }
  m->next = NULL; m->len = len; m->pos = 0; memcpy(m->data, data, len);
  if (s_qtail) s_qtail->next = m; else s_q = m;
  s_qtail = m; s_qbytes += len;
  pump_at(now, now);   // (in a moment, not here: the phone hears the answer to this message when this returns)
}

static void dropped(AppMessageResult r, void *ctx) { APP_LOG(APP_LOG_LEVEL_WARNING, "message dropped: %d", (int)r); }

// the phone's whole screen from the start (Link.reset: its first message begins with C_RESET after the time stamps)
static bool fresh_start(const uint8_t *d, int len) {
  return (len >= 1 && d[0] == C_RESET) || (len >= 11 && d[0] == C_LATEST && d[5] == C_TIME && d[10] == C_RESET);
}

static void inbox(DictionaryIterator *it, void *ctx) {
  Tuple *dt = dict_find(it, KEY_DATA);
  if (s_wait_reset) {
    // what the phone sent before it heard the hello builds on the copy given up: skip it
    if (!dt || dt->type != TUPLE_BYTE_ARRAY || !fresh_start(dt->value->data, dt->length)) return;
    s_wait_reset = false;
  }
  s_connected = true;
  static int n;
  if (++n % 20 == 1) {
    extern void scene_stats(int *, int *, int *, int *); extern int art_cache_bytes(void);
    int o, t, a, d, af, as; scene_stats(&o, &t, &a, &d); art_stats(&af, &as);
    APP_LOG(APP_LOG_LEVEL_INFO, "msg %d free %d pics %d tiles %d anim %d dyn %d cache %d (failed %d uncached %d)", n, (int)heap_bytes_free(), o, t, a, d, art_cache_bytes(), af, as);
    extern int g_oom;
    APP_LOG(APP_LOG_LEVEL_INFO, "least free %d, allocations failed %d", (int)s_minfree, g_oom);
    s_minfree = 0xFFFFFFFF;
  }
  Tuple *t = dict_find(it, KEY_DATA);
  if (t && t->type == TUPLE_BYTE_ARRAY) {
    s_data_at = now_ms();
    enqueue(t->value->data, t->length, s_data_at);
  }
}

// ---- to the phone: buttons and touches, one message at a time (a small queue; drags coalesce) ----
enum { T_BUTTON = 0, T_TAP = 1, T_PAN, T_DOWN, T_MOVE, T_UP };
typedef struct { uint8_t b[12]; } TMsg;
#define TQ 8
static TMsg s_tq[TQ];
static int s_ntq;
static bool s_sending;           // s_tq[0] is out, waiting for the phone to take it (it leaves the queue then)
static uint32_t s_sent_at;
static int s_tries;
static AppTimer *s_tq_timer;
static uint8_t s_seq;
typedef struct { uint8_t seq; int16_t dx, dy; } PanDelta;   // map drags the phone hasn't applied yet
static PanDelta s_pd[8];
static int s_npd;
#define SEND_LOST 20000          // (game ms, over 13 s real) the system answers every send by then; if not, it never will

static void tq_send(void);
static void tq_timer(void *data) { s_tq_timer = NULL; tq_send(); }
static void tq_later(int ms) { if (!s_tq_timer) s_tq_timer = app_timer_register(ms, tq_timer, NULL); }

static void failed(DictionaryIterator *it, AppMessageResult r, void *ctx);
#ifdef TEST_SENDFAIL
static void test_refuse(void *data) { failed(NULL, APP_MSG_SEND_REJECTED, NULL); }
#endif
static void tq_send(void) {
  if (s_sending && now_ms() - s_sent_at > SEND_LOST) { APP_LOG(APP_LOG_LEVEL_WARNING, "send never answered"); failed(NULL, APP_MSG_SEND_TIMEOUT, NULL); return; }
  if (s_sending) { if (s_ntq > 1) tq_later(1000); return; }   // (more waiting: look again in a while, in case no answer comes)
  if (!s_ntq) return;
#ifdef TEST_SENDFAIL
  // (tests: the emulator's phone side answers every message, so refusals and lost answers are made up here: every
  // 3rd send is refused, every 7th never answered)
  static int n; n++;
  if (n % 3 == 0 || n % 7 == 0) {
    s_sending = true; s_sent_at = now_ms();
    APP_LOG(APP_LOG_LEVEL_INFO, "test: send %d %s", n, n % 3 == 0 ? "refused" : "lost");
    if (n % 3 == 0) app_timer_register(80, test_refuse, NULL);
    return;
  }
#endif
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) { tq_later(50); return; }
  if (s_tq[0].b[0] == T_BUTTON) dict_write_int32(it, KEY_BUTTON, s_tq[0].b[4]);   // the phone library reads signed numbers most easily
  else dict_write_data(it, KEY_TOUCH, s_tq[0].b, 12);
  if (app_message_outbox_send() == APP_MSG_OK) { s_sending = true; s_sent_at = now_ms(); }
  else tq_later(50);
}
static void tq_pop(void) { s_ntq--; memmove(s_tq, s_tq + 1, s_ntq * sizeof(TMsg)); s_tries = 0; }
static void pan_preview(void);
static void sent(DictionaryIterator *it, void *ctx) { if (s_sending && s_ntq) tq_pop(); s_sending = false; tq_send(); }
static void failed(DictionaryIterator *it, AppMessageResult r, void *ctx) {
  if (s_sending && s_ntq) {
    // a touch the phone turned away (or never got) goes again: the phone knows a repeat by its number. Not a button
    // (it may have been taken: not twice), nor anything that waited out the whole time-out (too late to act on)
    uint8_t *b = s_tq[0].b;
    bool again = b[0] != T_BUTTON && r != APP_MSG_SEND_TIMEOUT && now_ms() - s_sent_at < 3000 && ++s_tries < 4;
    APP_LOG(APP_LOG_LEVEL_WARNING, "send failed (%d): %s", (int)r, again ? "again" : "let go");
    if (!again) {
      if (b[0] == T_PAN) {   // its drag will never be applied: take it off the map's preview
        int k = 0; for (int i = 0; i < s_npd; i++) if (s_pd[i].seq != b[1]) s_pd[k++] = s_pd[i];
        if (k != s_npd) { s_npd = k; pan_preview(); }
      }
      tq_pop();
    }
  }
  s_sending = false; tq_later(100);
}

static int s_tcam = -1;
static uint32_t s_tstamp;
static void put16(uint8_t *p, int v) { p[0] = v; p[1] = v >> 8; }

static void out_msg(int kind, int x, int y, bool lng) {
  TMsg *last = s_ntq > (s_sending ? 1 : 0) ? &s_tq[s_ntq - 1] : NULL;   // (the one out can't change any more)
  if (last && last->b[0] == kind && (kind == T_PAN || kind == T_MOVE)) {   // still waiting: fold into it
    if (kind == T_PAN) { x += (int16_t)(last->b[4] | last->b[5] << 8); y += (int16_t)(last->b[6] | last->b[7] << 8); }
  } else if (s_ntq == TQ && last && last->b[0] == T_MOVE && kind == T_UP) {
    // full (the radio is held up): the lift-off takes the place of the move waiting before it (it ends there anyway)
  } else {
    if (s_ntq == TQ) { APP_LOG(APP_LOG_LEVEL_WARNING, "send queue full: %d dropped", kind); return; }
    last = &s_tq[s_ntq++]; memset(last->b, 0, 12); last->b[1] = ++s_seq;
  }
  uint8_t *b = last->b;
  b[0] = kind; b[2] = s_tcam < 0 ? 0xFF : s_tcam; b[3] = lng;
  put16(b + 4, x); put16(b + 6, y);
  b[8] = s_tstamp; b[9] = s_tstamp >> 8; b[10] = s_tstamp >> 16; b[11] = s_tstamp >> 24;
  if (kind == T_PAN) {
    if (s_npd && s_pd[s_npd - 1].seq == b[1]) { s_pd[s_npd - 1].dx = x; s_pd[s_npd - 1].dy = y; }
    else if (s_npd < 8) s_pd[s_npd++] = (PanDelta){ b[1], x, y };
  }
  tq_send();
}

static void send_button(int code) { out_msg(T_BUTTON, code, 0, false); }

// the map follows the finger at once; the phone's own scroll takes over when it has applied the drag
static void pan_preview(void) {
  int dx = 0, dy = 0;
  for (int i = 0; i < s_npd; i++) { dx += s_pd[i].dx; dy += s_pd[i].dy; }
  Cam *c = g_map_cam >= 0 ? &g_cam[g_map_cam] : NULL;
  g_pan_x = c && c->zoom ? -(int32_t)((int64_t)dx * 8 * 65536 / c->zoom) : 0;
  g_pan_y = c && c->zoom ? -(int32_t)((int64_t)dy * 8 * 65536 / c->zoom) : 0;
  kick();
}

void touch_acked(uint8_t seq) {
  int k = 0;
  for (int i = 0; i < s_npd; i++) if ((int8_t)(s_pd[i].seq - seq) > 0) s_pd[k++] = s_pd[i];
  if (k != s_npd) { s_npd = k; pan_preview(); }
}

static void button(ClickRecognizerRef r, void *ctx) {
  s_input_at = now_ms();
  send_button((int)ctx);
  if (predict_button((int)ctx, s_input_at)) kick();   // shown at once; the phone's frames confirm it later
}

static void button_long(ClickRecognizerRef r, void *ctx) {
  s_input_at = now_ms();
  send_button((int)ctx | B_LONG);
  if (predict_button((int)ctx | B_LONG, s_input_at)) kick();
}

// ---- touch (as in Pebble PD: under 40 px of movement is a tap, held 500 ms a long one; 24 px drags the map) ----
#define TAP_SLOP 32
#define PAN_START 24
#define LONG_MS 500   // (real ms)
static int16_t s_tx, s_ty, s_lx, s_ly, s_moved;
static bool s_down, s_drag, s_map, s_long;
static AppTimer *s_ltimer;

// A press is long when this timer comes before the lift-off. (Timing the lift-off against the touch-down instead
// counted any wait for the watch to finish a slow frame: quick taps came out as long presses while it was busy.)
static void long_held(void *data) {
  s_ltimer = NULL; if (s_down && !s_drag) s_long = true;
#ifdef TRACE_WALK
  APP_LOG(APP_LOG_LEVEL_INFO, "TT %lu long", (unsigned long)now_ms());
#endif
}
static void long_stop(void) { if (s_ltimer) { app_timer_cancel(s_ltimer); s_ltimer = NULL; } }

static void touch_start(int x, int y, uint32_t now) {
  s_tstamp = s_cur_stamp + (now - s_cur_at);   // what was on screen, in the phone's time
  s_tcam = scene_hit(x, y, now);
  s_map = s_tcam >= 0 && s_tcam == g_map_cam;
}

static void touch(const TouchEvent *e, void *ctx) {
  uint32_t now = now_ms();
#ifdef TRACE_WALK
  APP_LOG(APP_LOG_LEVEL_INFO, "TT %lu %d %d,%d nn %d", (unsigned long)now, (int)e->type, e->x, e->y, e->non_navigational);
#endif
  s_input_at = now;
  predict_touch();
  switch (e->type) {
    case TouchEvent_Touchdown:
      s_down = true; s_drag = s_long = false; s_tx = s_lx = e->x; s_ty = s_ly = e->y; s_moved = 0;
      long_stop(); s_ltimer = app_timer_register(LONG_MS, long_held, NULL);
      touch_start(e->x, e->y, now);
      break;
    case TouchEvent_PositionUpdate: {
      if (!s_down) break;
      int m = abs(e->x - s_tx) + abs(e->y - s_ty);
      if (m > s_moved) s_moved = m;
      if (!s_drag && s_moved >= (s_map ? PAN_START : TAP_SLOP)) {
        s_drag = true;
        if (!s_map) out_msg(T_DOWN, s_tx, s_ty, false);
      }
      if (s_drag) {
        if (s_map) { out_msg(T_PAN, e->x - s_lx, e->y - s_ly, false); pan_preview(); }
        else out_msg(T_MOVE, e->x, e->y, false);
        s_lx = e->x; s_ly = e->y;
      }
      break;
    }
    case TouchEvent_Liftoff:
      long_stop();
      if (s_drag) { if (!s_map) out_msg(T_UP, e->x, e->y, false); }
      else if (!s_down) { touch_start(e->x, e->y, now); out_msg(T_TAP, e->x, e->y, false); }   // a quick tap can come as a lone liftoff
      else out_msg(T_TAP, s_tx, s_ty, s_long);
      s_down = s_drag = false;
      break;
    default: break;
  }
  kick();
}

// back from something over the app (a notification, say): a finger that went down there never comes up here; start
// touches afresh (subscribing again also wakes a touch sensor that stopped answering, if nothing else holds it)
static void focused(bool in) {
  if (!in) return;
  s_down = s_drag = false; long_stop();
  touch_service_unsubscribe();
  touch_service_subscribe(touch, NULL);
  APP_LOG(APP_LOG_LEVEL_INFO, "focus back: touch %s", touch_service_is_enabled() ? "on" : "switched off");
}

static void clicks(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, button);
  window_single_click_subscribe(BUTTON_ID_DOWN, button);
  window_single_click_subscribe(BUTTON_ID_SELECT, button);
  window_single_click_subscribe(BUTTON_ID_BACK, button);
  window_long_click_subscribe(BUTTON_ID_SELECT, 400, button_long, NULL);
  window_long_click_subscribe(BUTTON_ID_UP, 400, button_long, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 400, button_long, NULL);
  // the context (which button) goes with every recognizer of that button, so set it last
  window_set_click_context(BUTTON_ID_UP, (void *)B_UP);
  window_set_click_context(BUTTON_ID_DOWN, (void *)B_DOWN);
  window_set_click_context(BUTTON_ID_SELECT, (void *)B_SELECT);
  window_set_click_context(BUTTON_ID_BACK, (void *)B_BACK);
}

static void hello(void *data) {
  // tell the phone how big a message may be; it resends the whole screen. Repeat until it answers.
  if (s_connected && !s_resyncing) return;
  queue_clear();
  if (!s_resyncing) { s_ntq = 0; s_npd = 0; s_sending = false; s_tries = 0; g_pan_x = g_pan_y = 0; }
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) == APP_MSG_OK) {
    uint32_t in = app_message_inbox_size_maximum(); if (in > INBOX) in = INBOX;
    dict_write_int32(it, KEY_HELLO, (int32_t)in);
    dict_write_int32(it, KEY_VERSION, PROTOCOL);
    dict_write_int32(it, KEY_SCREEN, (PBL_IF_ROUND_ELSE(1, 0) << 24) | (PBL_DISPLAY_WIDTH << 12) | PBL_DISPLAY_HEIGHT);   // round?, width, height
    if (app_message_outbox_send() == APP_MSG_OK) s_resyncing = false;
  }
  app_timer_register(s_resyncing ? 100 : 2000, hello, NULL);   // (resyncing: the outbox was busy with a press)
}

static void init(void) {
#ifdef BALLAST
  // (testing: BALLAST=bytes at build time holds that much heap, to try the watch short of memory)
  static void *ballast; ballast = malloc(BALLAST); APP_LOG(APP_LOG_LEVEL_INFO, "ballast %p", ballast);
#endif
  art_init();
  sound_init();
  s_win = window_create();
  window_set_background_color(s_win, GColorBlack);
  window_set_click_config_provider(s_win, clicks);
  window_stack_push(s_win, false);
  Layer *root = window_get_root_layer(s_win);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, update);
  layer_add_child(root, s_layer);
  app_message_register_inbox_received(inbox);
  app_message_register_inbox_dropped(dropped);
  app_message_register_outbox_sent(sent);
  app_message_register_outbox_failed(failed);
  // always: touch_service_is_enabled() is only whether touch is switched on right now (the watch's quick toggle);
  // asking first left touch dead for the whole game when it was off at the start and switched on later
  touch_service_subscribe(touch, NULL);
  app_focus_service_subscribe_handlers((AppFocusHandlers){ .did_focus = focused });
  uint32_t in = app_message_inbox_size_maximum(); if (in > INBOX) in = INBOX;
  app_message_open(in, 64);
  // the game streams from the phone: the quick radio setting cuts each round trip (at some battery cost while open)
  app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
  app_timer_register(300, hello, NULL);
}

int main(void) {
  init();
  app_event_loop();
  touch_service_unsubscribe();
  sound_deinit();
  queue_clear();
  scene_reset();
  layer_destroy(s_layer);
  window_destroy(s_win);
}
