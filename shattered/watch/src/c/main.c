// Shattered PD on the watch: the game runs on the phone; this app shows its screen and sends the buttons.
#include <pebble.h>
#include "scene.h"
#include "art.h"
#include "draw.h"

enum { KEY_DATA = 1, KEY_HELLO = 2, KEY_BUTTON = 3, KEY_STATS = 4, KEY_VERSION = 5, KEY_TOUCH = 6, KEY_SCREEN = 7 };
#define PROTOCOL 5   // must match Link.PROTOCOL on the phone
enum { B_UP = 1, B_DOWN, B_SELECT, B_BACK, B_LONG = 16 };

static Window *s_win;
static Layer *s_layer;
static AppTimer *s_timer;
static uint32_t s_last, s_input_at, s_data_at;
static bool s_connected;

// A clock that only goes forward: the wall clock can be set back (the phone syncing the time), which would
// otherwise hold up the play-out queue until it caught up again.
static uint32_t now_ms(void) {
  static uint32_t mono, raw_last; static bool started;
  time_t s; uint16_t ms; time_ms(&s, &ms);
  uint32_t raw = (uint32_t)s * 1000 + ms;
  int32_t d = (int32_t)(raw - raw_last);
  if (started && d > 0 && d < 60000) mono += d;
  raw_last = raw; started = true;
  return mono;
}

static void update(Layer *l, GContext *ctx) {
  uint32_t now = now_ms();
  if (g_head == NONE) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_connected ? "Waiting for the game..." : "Open Shattered PD on your phone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(20, PBL_DISPLAY_HEIGHT / 2 - 24, PBL_DISPLAY_WIDTH - 40, 60),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }
  draw_scene(ctx, layer_get_bounds(l), now);
  static int frames; static uint32_t spent;
  spent += now_ms() - now;
  if (++frames % 20 == 0) {
    APP_LOG(APP_LOG_LEVEL_INFO, "draw: %d ms per frame", (int)(spent / 20)); spent = 0;
#ifdef PROFILE
    extern uint32_t g_prof[8];
    APP_LOG(APP_LOG_LEVEL_INFO, "img %d rect %d nine %d tiles %d text %d skin %d", (int)g_prof[1] / 20, (int)g_prof[2] / 20, (int)g_prof[3] / 20, (int)g_prof[4] / 20, (int)g_prof[5] / 20, (int)g_prof[6] / 20);
    memset(g_prof, 0, sizeof(g_prof));
#endif
  }
}

// keep drawing while something moves or animates; stop (and save power) when the screen is still
static void tick(void *data) {
  s_timer = NULL;
  uint32_t now = now_ms(), dt = now - s_last; if (dt > 200) dt = 200;
  s_last = now;
  cams_follow(now, dt);
  layer_mark_dirty(s_layer);
#ifndef STILL
  // power saving (as in the Pixel Dungeon port): idle animations stop 10 s after the last button
  // press, unless the game is still sending something new
  bool awake = now - s_input_at < 10000 || now - s_data_at < 1500;
  if (awake && scene_moving(now)) s_timer = app_timer_register(40, tick, NULL);
#endif
}

static void kick(void) { if (!s_timer) { s_last = now_ms(); s_timer = app_timer_register(1, tick, NULL); } }

// ---- play-out: each frame's changes at the pace they happened on the phone ----
// Messages arrive in bursts (one per radio round trip), each holding several game frames stamped with
// the phone's clock. They wait here and are played a little behind, so movements come out smooth
// rather than in jumps.
typedef struct Msg { struct Msg *next; uint16_t len, pos; uint8_t data[]; } Msg;
static Msg *s_q, *s_qtail;
static int s_qbytes;
static int32_t s_off;        // watch time = phone time + s_off (enough to have heard of a frame before playing it)
static bool s_have_off;
static uint32_t s_cur_at;    // when the frame being played happened, in watch time
static uint32_t s_cur_stamp; // ... and in phone time
static AppTimer *s_ptimer;
#define MARGIN 40            // ms of slack for uneven radio
#define QMAX 6000            // more waiting than this (or memory running short): catch up at once

static uint32_t stamp_at(const uint8_t *d) { return d[1] | d[2] << 8 | d[3] << 16 | (uint32_t)d[4] << 24; }

static void queue_clear(void) {
  while (s_q) { Msg *m = s_q; s_q = m->next; free(m); }
  s_qtail = NULL; s_qbytes = 0; s_have_off = false;
}

static void pump(void *data);

static void pump_at(uint32_t due, uint32_t now) {
  if (s_ptimer) app_timer_cancel(s_ptimer);
  s_ptimer = app_timer_register(due > now ? due - now : 1, pump, NULL);
}

// play every frame that is due
static void pump(void *data) {
  s_ptimer = NULL;
  uint32_t now = now_ms();
  bool rush = s_qbytes > QMAX || heap_bytes_free() < 10000;
  while (s_q) {
    Msg *m = s_q;
    if (m->pos < m->len && m->data[m->pos] == C_TIME && m->len - m->pos >= 5) {
      uint32_t due = stamp_at(m->data + m->pos) + s_off;
      if ((int32_t)(due - now) > 0 && !rush) { pump_at(due, now); return; }
      s_cur_at = rush ? now : due;
      s_cur_stamp = stamp_at(m->data + m->pos);
    }
    int used = scene_apply(m->data + m->pos, m->len - m->pos, s_cur_at);
    m->pos = used > 0 ? m->pos + used : m->len;
    if (g_dirty) { g_dirty = false; kick(); }
    if (m->pos >= m->len) { s_q = m->next; if (!s_q) s_qtail = NULL; s_qbytes -= m->len; free(m); }
  }
}

static void enqueue(const uint8_t *data, int len, uint32_t now) {
  if (len > 0 && data[0] == C_TIME && len >= 5) {
    // how late did this arrive? play everything at least that late (coming down slowly when it gets quicker)
    int32_t want = (int32_t)(now - stamp_at(data)) + MARGIN;
    if (!s_have_off || want > s_off) s_off = want;
    else s_off -= (s_off - want) / 8;
    s_have_off = true;
  }
  Msg *m = malloc(sizeof(Msg) + len);
  if (!m) { pump(NULL); m = malloc(sizeof(Msg) + len); }   // short of memory: play what's waiting first
  if (!m) { s_cur_at = now; scene_apply(data, len, now); return; }
  m->next = NULL; m->len = len; m->pos = 0; memcpy(m->data, data, len);
  if (s_qtail) s_qtail->next = m; else s_q = m;
  s_qtail = m; s_qbytes += len;
  pump(NULL);
}

static void dropped(AppMessageResult r, void *ctx) { APP_LOG(APP_LOG_LEVEL_WARNING, "message dropped: %d", (int)r); }

static void inbox(DictionaryIterator *it, void *ctx) {
  s_connected = true;
  static int n;
  if (++n % 20 == 1) {
    extern void scene_stats(int *, int *, int *, int *); extern int art_cache_bytes(void);
    int o, t, a, d; scene_stats(&o, &t, &a, &d);
    APP_LOG(APP_LOG_LEVEL_INFO, "msg %d free %d pics %d tiles %d anim %d dyn %d cache %d", n, (int)heap_bytes_free(), o, t, a, d, art_cache_bytes());
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
static TMsg s_tq[8];
static int s_ntq;
static bool s_sending;
static uint8_t s_seq;
typedef struct { uint8_t seq; int16_t dx, dy; } PanDelta;   // map drags the phone hasn't applied yet
static PanDelta s_pd[8];
static int s_npd;

static void tq_send(void *data) {
  if (s_sending || !s_ntq) return;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) { app_timer_register(50, tq_send, NULL); return; }
  if (s_tq[0].b[0] == T_BUTTON) dict_write_int32(it, KEY_BUTTON, s_tq[0].b[4]);   // the phone library reads signed numbers most easily
  else dict_write_data(it, KEY_TOUCH, s_tq[0].b, 12);
  if (app_message_outbox_send() == APP_MSG_OK) { s_sending = true; s_ntq--; memmove(s_tq, s_tq + 1, s_ntq * sizeof(TMsg)); }
  else app_timer_register(50, tq_send, NULL);
}
static void sent(DictionaryIterator *it, void *ctx) { s_sending = false; tq_send(NULL); }
static void failed(DictionaryIterator *it, AppMessageResult r, void *ctx) { s_sending = false; app_timer_register(100, tq_send, NULL); }

static int s_tcam = -1;
static uint32_t s_tstamp;
static void put16(uint8_t *p, int v) { p[0] = v; p[1] = v >> 8; }

static void out_msg(int kind, int x, int y, bool lng) {
  TMsg *last = s_ntq ? &s_tq[s_ntq - 1] : NULL;
  if (last && last->b[0] == kind && (kind == T_PAN || kind == T_MOVE)) {   // still waiting: fold into it
    if (kind == T_PAN) { x += (int16_t)(last->b[4] | last->b[5] << 8); y += (int16_t)(last->b[6] | last->b[7] << 8); }
  } else {
    if (s_ntq == 8) return;
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
  tq_send(NULL);
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
}

static void button_long(ClickRecognizerRef r, void *ctx) {
  s_input_at = now_ms();
  send_button((int)ctx | B_LONG);
}

// ---- touch (as in Pebble PD: under 40 px of movement is a tap, held 500 ms a long one; 24 px drags the map) ----
#define TAP_SLOP 32
#define PAN_START 24
#define LONG_MS 500
static int16_t s_tx, s_ty, s_lx, s_ly, s_moved;
static bool s_down, s_drag, s_map;
static uint32_t s_tms;

static void touch_start(int x, int y, uint32_t now) {
  s_tstamp = s_cur_stamp + (now - s_cur_at);   // what was on screen, in the phone's time
  s_tcam = scene_hit(x, y, now);
  s_map = s_tcam >= 0 && s_tcam == g_map_cam;
}

static void touch(const TouchEvent *e, void *ctx) {
  uint32_t now = now_ms();
  s_input_at = now;
  switch (e->type) {
    case TouchEvent_Touchdown:
      s_down = true; s_drag = false; s_tx = s_lx = e->x; s_ty = s_ly = e->y; s_moved = 0; s_tms = now;
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
      if (s_drag) { if (!s_map) out_msg(T_UP, e->x, e->y, false); }
      else if (!s_down) { touch_start(e->x, e->y, now); out_msg(T_TAP, e->x, e->y, false); }   // a quick tap can come as a lone liftoff
      else out_msg(T_TAP, s_tx, s_ty, now - s_tms >= LONG_MS);
      s_down = s_drag = false;
      break;
    default: break;
  }
  kick();
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
  if (s_connected) return;
  queue_clear();
  s_ntq = 0; s_npd = 0; g_pan_x = g_pan_y = 0;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) == APP_MSG_OK) {
    uint32_t in = app_message_inbox_size_maximum();
    dict_write_int32(it, KEY_HELLO, in > 8000 ? 8000 : (int32_t)in);
    dict_write_int32(it, KEY_VERSION, PROTOCOL);
    dict_write_int32(it, KEY_SCREEN, (PBL_IF_ROUND_ELSE(1, 0) << 24) | (PBL_DISPLAY_WIDTH << 12) | PBL_DISPLAY_HEIGHT);   // round?, width, height
    app_message_outbox_send();
  }
  app_timer_register(2000, hello, NULL);
}

static void init(void) {
  art_init();
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
  if (touch_service_is_enabled()) touch_service_subscribe(touch, NULL);
  uint32_t in = app_message_inbox_size_maximum(); if (in > 8200) in = 8200;
  app_message_open(in, 64);
  app_timer_register(300, hello, NULL);
}

int main(void) {
  init();
  app_event_loop();
  touch_service_unsubscribe();
  queue_clear();
  scene_reset();
  layer_destroy(s_layer);
  window_destroy(s_win);
}
