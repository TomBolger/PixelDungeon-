// Host check of the real watch renderer's framebuffer ownership and draw order.
// cc -O1 -Itools/feel/stub -Iwatch/src/c tools/feel/draw_test.c -o /tmp/draw-test && /tmp/draw-test
#include "stub/pebble.h"
#include <assert.h>
#define FONT_KEY_GOTHIC_09 "g9"
#define FONT_KEY_GOTHIC_14 "g14"
#define FONT_KEY_GOTHIC_14_BOLD "g14b"
#define FONT_KEY_GOTHIC_18_BOLD "g18b"
#define FONT_KEY_GOTHIC_24 "g24"
#define FONT_KEY_GOTHIC_24_BOLD "g24b"
#define GTextOverflowModeFill 1
#define GTextAlignmentLeft 0
#define GColorBlackARGB8 0xc0
#define GColorFromRGB(r,g,b) ((GColor){0xff})
typedef struct { uint8_t px[200*228]; } GBitmap;
static GBitmap bitmap;
static bool captured;
static int captures, releases, texts;
static GBitmap *graphics_capture_frame_buffer(GContext *c) { assert(!captured); captured = true; captures++; return &bitmap; }
static void graphics_release_frame_buffer(GContext *c, GBitmap *b) { assert(captured && b == &bitmap); captured = false; releases++; }
static uint8_t *gbitmap_get_data(GBitmap *b) { assert(captured); return b->px; }
static int gbitmap_get_bytes_per_row(GBitmap *b) { return 200; }
#include "../../watch/src/c/draw.c"

Obj *g_obj[MAX_ID]; Cam g_cam[MAX_CAM]; uint16_t g_head;
int32_t sin_lookup(int32_t a) { return 0; }
int32_t cos_lookup(int32_t a) { return 65536; }
int8_t g_map_cam = -1; int32_t g_pan_x, g_pan_y;
int32_t obj_dyn(const Obj *o, int i, uint32_t now) { return o->dyn[i]; }
Anim *scene_anim(int id) { return NULL; }
DynTex *scene_dyntex(int id) { return NULL; }
bool art_size(int t, int *w, int *h) { return false; }
int art_shrink(int t) { return 1; }
bool art_open(ArtSrc *s, int t, int x, int y, int w, int h) { return false; }
const uint8_t *art_row(ArtSrc *s, int r) { return NULL; }
void art_frame(void) { }
const Recipe *recipe_get(int id) { return NULL; }
void emit_update(uint16_t id, Obj *o, uint32_t now) { }
bool emit_next(uint16_t id, uint32_t now, int *it, PDraw *d) { return false; }
GFont fonts_get_system_font(const char *k) { return NULL; }
void graphics_context_set_text_color(GContext *c, GColor col) { assert(!captured); }
void graphics_draw_text(GContext *c, const char *t, GFont f, GRect r, GTextOverflowMode o, GTextAlignment a, void *attr) {
    assert(!captured); texts++;
    bitmap.px[10*200+10] = (uint8_t)texts; // text drawn over raster objects must keep its order
}

int main(void) {
    g_cam[0] = (Cam){.w=200*8, .h=228*8, .zoom=256, .used=1};
    Obj objects[5] = {0};
    for (int i=0; i<5; i++) {
        Obj *o = &objects[i]; g_obj[i] = o; o->next = i==4 ? NONE : i+1;
        o->kind = K_TEXT; o->w = 1; o->ext = "text"; o->dyn[2] = 255;
        o->dyn[3] = o->dyn[4] = 256;
    }
    // Four adjacent text objects require only the initial capture (for the black clear).
    objects[3].next = NONE;
    draw_scene(NULL, GRect(0,0,200,228), 0);
    assert(!captured && captures==1 && releases==1 && texts==4);
    // A raster object between text runs takes the framebuffer again, then releases it for text.
    objects[3].next = 4; objects[2].kind = K_RECT; objects[2].w=objects[2].h=20*8;
    static uint8_t white[] = {255,255,255}; objects[2].ext=white;
    objects[2].col[0]=objects[2].col[1]=objects[2].col[2]=255;
    captures=releases=texts=0;
    draw_scene(NULL, GRect(0,0,200,228), 0);
    assert(!captured && captures==2 && releases==2 && texts==4);
    assert(bitmap.px[10*200+10]==4);
    objects[2].kind = K_EMIT; objects[2].ext = NULL;
    captures=releases=texts=0;
    draw_scene(NULL, GRect(0,0,200,228), 0);
    assert(!captured && captures==2 && releases==2 && texts==4);
    puts("PASS: adjacent text captures once; mixed raster/text preserves ownership and order");
}
