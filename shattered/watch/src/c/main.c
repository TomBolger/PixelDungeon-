// Shattered PD on the watch: the game runs on the phone; this app shows its screen and sends the buttons.
#include <pebble.h>
#include "scene.h"
#include "art.h"
#include "draw.h"

enum { KEY_DATA = 1, KEY_HELLO = 2, KEY_BUTTON = 3, KEY_STATS = 4, KEY_VERSION = 5 };
#define PROTOCOL 2   // must match Link.PROTOCOL on the phone
enum { B_UP = 1, B_DOWN, B_SELECT, B_BACK, B_LONG = 16 };

static Window *s_win;
static Layer *s_layer;
static AppTimer *s_timer;
static uint32_t s_last, s_input_at, s_data_at;
static bool s_connected;

static uint32_t now_ms(void) { time_t s; uint16_t ms; time_ms(&s, &ms); return (uint32_t)s * 1000 + ms; }

static void update(Layer *l, GContext *ctx) {
  uint32_t now = now_ms();
  if (g_head == NONE) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, layer_get_bounds(l), 0, GCornerNone);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_connected ? "Waiting for the game..." : "Open Shattered PD on your phone",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18), GRect(10, 90, 180, 60),
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
  if (awake && scene_moving(now)) s_timer = app_timer_register(50, tick, NULL);
#endif
}

static void kick(void) { if (!s_timer) { s_last = now_ms(); s_timer = app_timer_register(1, tick, NULL); } }

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
    scene_apply(t->value->data, t->length, s_data_at);
    if (g_dirty) { g_dirty = false; kick(); }
  }
}

static void send_u32(uint32_t key, uint32_t v) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_int32(it, key, (int32_t)v);   // the phone library reads signed numbers most easily
  app_message_outbox_send();
}

static void button(ClickRecognizerRef r, void *ctx) {
  int code = (int)ctx;
  s_input_at = now_ms();
  send_u32(KEY_BUTTON, code);
}

static void button_long(ClickRecognizerRef r, void *ctx) {
  s_input_at = now_ms();
  send_u32(KEY_BUTTON, (int)ctx | B_LONG);
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
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) == APP_MSG_OK) {
    uint32_t in = app_message_inbox_size_maximum();
    dict_write_int32(it, KEY_HELLO, in > 8000 ? 8000 : (int32_t)in);
    dict_write_int32(it, KEY_VERSION, PROTOCOL);
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
  uint32_t in = app_message_inbox_size_maximum(); if (in > 8200) in = 8200;
  app_message_open(in, 64);
  app_timer_register(300, hello, NULL);
}

int main(void) {
  init();
  app_event_loop();
  scene_reset();
  layer_destroy(s_layer);
  window_destroy(s_win);
}
