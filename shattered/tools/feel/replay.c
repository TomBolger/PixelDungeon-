// The watch app's own play-out code (main.c, scene.c, predict.c) run on a computer in simulated time, fed what the
// watch got and when (a RadioTest TRACE): prints where the hero and the map's camera are on every frame drawn, for
// feel.py to measure how evenly things move. Nothing is drawn; a frame costs DRAW_MS of the watch's time.
// Build and run: tools/feel/feel.sh (see there).
#include <stdarg.h>
#include "pebble.h"
#include "scene.h"

static uint64_t vt = 1000000000ull;   // simulated time, us
static double DRAW_MS = 30, APPLY_US_PER_BYTE = 4, ACK_MS = 100;
static int LOG;

void feel_log(int level, const char *fmt, ...) {
  if (!LOG) return;
  va_list a; va_start(a, fmt); fprintf(stderr, "%8.1f ", (vt - 1000000000ull) / 1000.0); vfprintf(stderr, fmt, a); fputc('\n', stderr); va_end(a);
}

// ---- timers ----
struct AppTimer { uint64_t due; AppTimerCallback cb; void *data; bool alive; };
#define NT 256
static struct AppTimer s_timers[NT];
AppTimer *app_timer_register(uint32_t ms, AppTimerCallback cb, void *data) {
  for (int i = 0; i < NT; i++) if (!s_timers[i].alive) { s_timers[i] = (struct AppTimer){ vt + (uint64_t)ms * 1000, cb, data, true }; return &s_timers[i]; }
  fprintf(stderr, "out of timers\n"); exit(1);
}
void app_timer_cancel(AppTimer *t) { if (t) t->alive = false; }

// SLIP=ms: the emulator's clock, whose seconds and milliseconds disagree for that long after each turn of a second,
// every 7 s (readings a second back)
static int SLIP;
uint16_t time_ms(time_t *s, uint16_t *ms) {
  uint64_t t = vt / 1000;
  *s = t / 1000; *ms = t % 1000;
  if (SLIP && (*s % 7) == 0 && *ms < SLIP) *s -= 1;
  return *ms;
}
size_t heap_bytes_free(void) { return 50000; }

struct Window { int x; }; struct Layer { LayerUpdateProc proc; bool dirty; };
static struct Window s_w; static struct Layer s_layers[4]; static int s_nl;
static Layer *s_draw_layer;
Window *window_create(void) { return &s_w; }
void window_destroy(Window *w) { }
void window_set_background_color(Window *w, GColor c) { }
void window_set_click_config_provider(Window *w, ClickConfigProvider p) { }
void window_stack_push(Window *w, bool animated) { }
Layer *window_get_root_layer(Window *w) { return &s_layers[s_nl++]; }
Layer *layer_create(GRect r) { return &s_layers[s_nl++]; }
void layer_destroy(Layer *l) { }
GRect layer_get_bounds(Layer *l) { return GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT); }
void layer_set_update_proc(Layer *l, LayerUpdateProc p) { l->proc = p; s_draw_layer = l; }
void layer_add_child(Layer *parent, Layer *child) { }
void layer_mark_dirty(Layer *l) { l->dirty = true; }
void graphics_context_set_fill_color(GContext *c, GColor col) { }
void graphics_context_set_text_color(GContext *c, GColor col) { }
void graphics_fill_rect(GContext *c, GRect r, uint16_t radius, GCornerMask m) { }
void graphics_draw_text(GContext *c, const char *t, GFont f, GRect r, GTextOverflowMode o, GTextAlignment a, void *attr) { }
GFont fonts_get_system_font(const char *k) { return NULL; }
void window_single_click_subscribe(ButtonId b, ClickHandler h) { }
void window_long_click_subscribe(ButtonId b, uint16_t ms, ClickHandler down, ClickHandler up) { }
void window_set_click_context(ButtonId b, void *ctx) { }

// ---- messages out: answered after ACK_MS ----
static AppMessageOutboxSent s_sent_cb; static bool s_out_busy;
static void out_done(void *d) { s_out_busy = false; if (s_sent_cb) s_sent_cb(NULL, NULL); }
Tuple *dict_find(DictionaryIterator *it, uint32_t key) { return NULL; }
int dict_write_int32(DictionaryIterator *it, uint32_t key, int32_t v) { return 0; }
int dict_write_data(DictionaryIterator *it, uint32_t key, const uint8_t *d, uint16_t n) { return 0; }
void app_message_register_inbox_received(AppMessageInboxReceived f) { }
void app_message_register_inbox_dropped(AppMessageInboxDropped f) { }
void app_message_register_outbox_sent(AppMessageOutboxSent f) { s_sent_cb = f; }
void app_message_register_outbox_failed(AppMessageOutboxFailed f) { }
uint32_t app_message_inbox_size_maximum(void) { return 8200; }
AppMessageResult app_message_open(uint32_t in, uint32_t out) { return APP_MSG_OK; }
AppMessageResult app_message_outbox_begin(DictionaryIterator **it) { return s_out_busy ? APP_MSG_BUSY : APP_MSG_OK; }
AppMessageResult app_message_outbox_send(void) { s_out_busy = true; app_timer_register(ACK_MS, out_done, NULL); return APP_MSG_OK; }
void app_comm_set_sniff_interval(SniffInterval s) { }
void touch_service_subscribe(TouchHandler h, void *ctx) { }
void touch_service_unsubscribe(void) { }
bool touch_service_is_enabled(void) { return true; }
void app_focus_service_subscribe_handlers(AppFocusHandlers h) { }
void app_event_loop(void) { }
#include <math.h>
int32_t sin_lookup(int32_t a) { return (int32_t)(sin(a * 6.283185307 / 65536) * 0xffff); }
int32_t cos_lookup(int32_t a) { return (int32_t)(cos(a * 6.283185307 / 65536) * 0xffff); }

// ---- pictures and sound: none ----
void art_init(void) { }
bool art_size(int tex, int *w, int *h) { *w = *h = 0; return false; }
int art_cells(int tex, int fw, int fh) { return 0; }
int art_shrink(int tex) { return 1; }
void art_frame(void) { }
void art_stats(int *f, int *s) { *f = *s = 0; }
int art_decodes(void) { return 0; }
void *art_alloc(int n) { return malloc(n); }
void art_release(int need) { }
void art_forget(int tex) { }
void art_forget_dynamic(void) { }
int art_cache_bytes(void) { return 0; }
void sound_settings(int m) { } void sound_music(int t) { } void sound_effect(int id) { } void sound_poll(void) { }
void sound_init(void) { } void sound_deinit(void) { }

// applying a message takes the watch's time too
int scene_apply_real(const uint8_t *data, int len, uint32_t now);
static uint32_t feel_now(void);
static uint32_t feel_arrived(void);
int scene_apply(const uint8_t *data, int len, uint32_t now) {
  // a frame of the phone's starting: when it happened there (stamp), the time its pictures count from here (at),
  // and when it was played (now)
  if (len >= 5 && data[0] == C_TIME)
    printf("A %.3f %u %u %u %u\n", (vt - 1000000000ull) / 1000.0, (unsigned)(data[1] | data[2] << 8 | data[3] << 16 | (uint32_t)data[4] << 24), (unsigned)now, (unsigned)feel_now(), (unsigned)feel_arrived());
  int used = scene_apply_real(data, len, now);
  vt += (uint64_t)((used > 0 ? used : len) * APPLY_US_PER_BYTE);
  return used;
}

// ---- a frame drawn: where the hero (the map camera's followed picture) and the camera are ----
static int s_hero = -1;
void draw_scene(GContext *ctx, GRect bounds, uint32_t now) {
  if (g_map_cam >= 0) {
    Cam *c = &g_cam[g_map_cam];
    if (c->mode == 1 && c->follow < MAX_ID && g_obj[c->follow]) s_hero = c->follow;
    Obj *h = s_hero >= 0 ? g_obj[s_hero] : NULL;
    printf("F %.3f %u %d %d %d %d %d %d %d %d\n", (vt - 1000000000ull) / 1000.0, now,
           h ? (int)obj_dyn(h, 0, now) : 0, h ? (int)obj_dyn(h, 1, now) : 0, h ? (int)obj_dyn_raw(h, 0, now) : 0, h ? (int)obj_dyn_raw(h, 1, now) : 0,
           (int)(c->sx / 256), (int)(c->sy / 256), c->zoom, c->mode);
  }
  vt += (uint64_t)(DRAW_MS * 1000);
}

#define main watch_main
#include "main.c"
#undef main
static uint32_t feel_now(void) { return now_ms(); }
// when each message waiting came (watch time), for A lines
#define NARR 64
static struct { void *m; uint32_t at; } s_arr[NARR];
static int s_narr;
static uint32_t feel_arrived(void) {   // (the newest first: freed messages' memory comes round again)
  for (int k = 1; k <= NARR && k <= s_narr; k++) { int i = (s_narr - k) % NARR; if (s_arr[i].m == s_q && s_q) return s_arr[i].at; }
  return 0;
}

// ---- the trace ----
typedef struct { uint64_t at; char kind; int code; uint8_t *data; int len; char *text; } Ev;
static Ev *s_ev; static int s_nev, s_cap;
static int hexv(int c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

static void fire(Ev *e) {
  switch (e->kind) {
    case 'M':
      if (s_wait_reset && !fresh_start(e->data, e->len)) break;
      s_wait_reset = false; s_connected = true;
      s_data_at = now_ms();
      enqueue(e->data, e->len, s_data_at);
      if (s_qtail) { s_arr[s_narr % NARR].m = s_qtail; s_arr[s_narr % NARR].at = s_data_at; s_narr++; }
      printf("O %.3f %d %d %d %d %d\n", (vt - 1000000000ull) / 1000.0, (int)s_off, (int)s_base, e->len >= 5 && e->data[0] == C_LATEST ? (int)(s_data_at - stamp_at(e->data)) : -1, e->len,
             e->len >= 10 && e->data[0] == C_LATEST && e->data[5] == C_TIME ? (int)(s_data_at - stamp_at(e->data + 5)) : -1);
      break;
    case 'B':
      if (e->code & B_LONG) button_long(NULL, (void *)(intptr_t)(e->code & 15)); else button(NULL, (void *)(intptr_t)e->code);
      printf("B %.3f %d\n", (vt - 1000000000ull) / 1000.0, e->code);
      break;
    case 'S': printf("S %.3f %s\n", (vt - 1000000000ull) / 1000.0, e->text); break;
  }
}

int main(int argc, char **argv) {
  if (getenv("DRAW_MS")) DRAW_MS = atof(getenv("DRAW_MS"));
  if (getenv("APPLY_US")) APPLY_US_PER_BYTE = atof(getenv("APPLY_US"));
  LOG = getenv("LOG") != NULL;
  if (getenv("SLIP")) SLIP = atoi(getenv("SLIP"));
  FILE *f = fopen(argv[1], "r");
  if (!f) { perror(argv[1]); return 1; }
  static char line[1 << 20];
  while (fgets(line, sizeof line, f)) {
    char k = line[0]; long long t;
    if (k != 'M' && k != 'B' && k != 'S' && k != 'P' && k != 'Q') continue;
    if (s_nev == s_cap) { s_cap = s_cap ? s_cap * 2 : 1024; s_ev = realloc(s_ev, s_cap * sizeof(Ev)); }
    Ev *e = &s_ev[s_nev]; memset(e, 0, sizeof *e);
    char *p = line + 2; t = strtoll(p, &p, 10); while (*p == ' ') p++;
    e->at = 1000000000ull + t * 1000; e->kind = k;
    if (k == 'P' || k == 'Q') { line[strcspn(line, "\n")] = 0; printf("%s\n", line); continue; }   // (the phone's truth: passed on)
    if (k == 'M') { int n = strcspn(p, "\n") / 2; e->data = malloc(n); e->len = n; for (int i = 0; i < n; i++) e->data[i] = hexv(p[2 * i]) << 4 | hexv(p[2 * i + 1]); }
    else if (k == 'B') e->code = atoi(p);
    else { e->text = strdup(p); e->text[strcspn(e->text, "\n")] = 0; }
    s_nev++;
  }
  init();
  int next = 0;
  uint64_t end = s_nev ? s_ev[s_nev - 1].at + 2000000 : 0;
  while (vt < end) {
    // the earliest of: a timer, the next thing from the trace
    struct AppTimer *tm = NULL;
    for (int i = 0; i < NT; i++) if (s_timers[i].alive && (!tm || s_timers[i].due < tm->due)) tm = &s_timers[i];
    uint64_t te = next < s_nev ? s_ev[next].at : UINT64_MAX;
    if (!tm && te == UINT64_MAX) break;
    if (tm && tm->due <= te) { if (tm->due > vt) vt = tm->due; tm->alive = false; tm->cb(tm->data); }
    else { if (te > vt) vt = te; fire(&s_ev[next++]); }
    if (s_draw_layer && s_draw_layer->dirty) { s_draw_layer->dirty = false; s_draw_layer->proc(s_draw_layer, NULL); }
  }
  return 0;
}
