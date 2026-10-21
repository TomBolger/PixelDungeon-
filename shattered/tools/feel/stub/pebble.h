// Just enough of the Pebble SDK to run the watch app's play-out (main.c, scene.c, predict.c) on a computer, in
// simulated time (tools/feel/replay.c). Nothing is drawn.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef enum { APP_LOG_LEVEL_ERROR = 1, APP_LOG_LEVEL_WARNING = 50, APP_LOG_LEVEL_INFO = 100, APP_LOG_LEVEL_DEBUG = 200 } AppLogLevel;
void feel_log(int level, const char *fmt, ...);
#define APP_LOG(level, ...) feel_log(level, __VA_ARGS__)

typedef struct { int16_t x, y; } GPoint;
typedef struct { int16_t w, h; } GSize;
typedef struct { GPoint origin; GSize size; } GRect;
#define GRect(x, y, w, h) ((GRect){ { (x), (y) }, { (w), (h) } })
typedef union { uint8_t argb; } GColor;
#define GColorBlack ((GColor){ 0xC0 })
#define GColorWhite ((GColor){ 0xFF })
typedef struct GContext GContext;
typedef struct Layer Layer;
typedef struct Window Window;
typedef void (*LayerUpdateProc)(Layer *, GContext *);
typedef enum { GCornerNone = 0 } GCornerMask;
typedef enum { GTextOverflowModeWordWrap = 0 } GTextOverflowMode;
typedef enum { GTextAlignmentCenter = 1 } GTextAlignment;
typedef void *GFont;
#define FONT_KEY_GOTHIC_18 "g18"
#define PBL_DISPLAY_WIDTH 200
#define PBL_DISPLAY_HEIGHT 228
#define PBL_IF_ROUND_ELSE(a, b) (b)

typedef struct AppTimer AppTimer;
typedef void (*AppTimerCallback)(void *);
AppTimer *app_timer_register(uint32_t ms, AppTimerCallback cb, void *data);
void app_timer_cancel(AppTimer *t);

uint16_t time_ms(time_t *s, uint16_t *ms);
size_t heap_bytes_free(void);

Window *window_create(void);
void window_destroy(Window *w);
void window_set_background_color(Window *w, GColor c);
typedef void (*ClickConfigProvider)(void *);
void window_set_click_config_provider(Window *w, ClickConfigProvider p);
void window_stack_push(Window *w, bool animated);
Layer *window_get_root_layer(Window *w);
Layer *layer_create(GRect r);
void layer_destroy(Layer *l);
GRect layer_get_bounds(Layer *l);
void layer_set_update_proc(Layer *l, LayerUpdateProc p);
void layer_add_child(Layer *parent, Layer *child);
void layer_mark_dirty(Layer *l);
void graphics_context_set_fill_color(GContext *c, GColor col);
void graphics_context_set_text_color(GContext *c, GColor col);
void graphics_fill_rect(GContext *c, GRect r, uint16_t radius, GCornerMask m);
void graphics_draw_text(GContext *c, const char *t, GFont f, GRect r, GTextOverflowMode o, GTextAlignment a, void *attr);
GFont fonts_get_system_font(const char *k);

typedef void *ClickRecognizerRef;
typedef void (*ClickHandler)(ClickRecognizerRef, void *);
typedef enum { BUTTON_ID_BACK = 0, BUTTON_ID_UP, BUTTON_ID_SELECT, BUTTON_ID_DOWN } ButtonId;
void window_single_click_subscribe(ButtonId b, ClickHandler h);
void window_long_click_subscribe(ButtonId b, uint16_t ms, ClickHandler down, ClickHandler up);
void window_set_click_context(ButtonId b, void *ctx);

typedef enum { APP_MSG_OK = 0, APP_MSG_SEND_TIMEOUT = 2, APP_MSG_SEND_REJECTED = 4, APP_MSG_BUSY = 64 } AppMessageResult;
typedef enum { TUPLE_BYTE_ARRAY = 0 } TupleType;
typedef struct { uint32_t key; TupleType type; uint16_t length; union { uint8_t data[1]; } *value; } Tuple;
typedef struct DictionaryIterator DictionaryIterator;
Tuple *dict_find(DictionaryIterator *it, uint32_t key);
int dict_write_int32(DictionaryIterator *it, uint32_t key, int32_t v);
int dict_write_data(DictionaryIterator *it, uint32_t key, const uint8_t *d, uint16_t n);
typedef void (*AppMessageInboxReceived)(DictionaryIterator *, void *);
typedef void (*AppMessageInboxDropped)(AppMessageResult, void *);
typedef void (*AppMessageOutboxSent)(DictionaryIterator *, void *);
typedef void (*AppMessageOutboxFailed)(DictionaryIterator *, AppMessageResult, void *);
void app_message_register_inbox_received(AppMessageInboxReceived f);
void app_message_register_inbox_dropped(AppMessageInboxDropped f);
void app_message_register_outbox_sent(AppMessageOutboxSent f);
void app_message_register_outbox_failed(AppMessageOutboxFailed f);
uint32_t app_message_inbox_size_maximum(void);
AppMessageResult app_message_open(uint32_t in, uint32_t out);
AppMessageResult app_message_outbox_begin(DictionaryIterator **it);
AppMessageResult app_message_outbox_send(void);
typedef enum { SNIFF_INTERVAL_NORMAL = 0, SNIFF_INTERVAL_REDUCED = 1 } SniffInterval;
void app_comm_set_sniff_interval(SniffInterval s);

typedef enum { TouchEvent_Touchdown, TouchEvent_Liftoff, TouchEvent_PositionUpdate } TouchEventType;
typedef struct { TouchEventType type; int16_t x, y; bool non_navigational; } TouchEvent;
typedef void (*TouchHandler)(const TouchEvent *, void *);
void touch_service_subscribe(TouchHandler h, void *ctx);
void touch_service_unsubscribe(void);
bool touch_service_is_enabled(void);
typedef struct { void (*will_focus)(bool); void (*did_focus)(bool); } AppFocusHandlers;
void app_focus_service_subscribe_handlers(AppFocusHandlers h);
void app_event_loop(void);
