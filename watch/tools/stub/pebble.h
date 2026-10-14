#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define PERSIST_DATA_MAX_LENGTH 256
typedef struct GBitmap GBitmap; typedef struct GContext GContext;
static inline uint16_t time_ms(time_t *s, uint16_t *ms) { if (s) *s = 0; if (ms) *ms = 0; return 0; }
// in-memory persist with the 4 KB budget
typedef struct { bool used; int len; uint8_t d[256]; } PKey;
extern PKey g_keys[256]; extern int g_persist_fail;
static inline int persist_total(void) { int t = 0; for (int i = 0; i < 256; i++) if (g_keys[i].used) t += g_keys[i].len; return t; }
static inline int persist_write_data(uint32_t k, const void *d, size_t n) {
  int old = g_keys[k].used ? g_keys[k].len : 0;
  if (persist_total() - old + (int)n > 4096) { g_persist_fail++; return -1; }
  g_keys[k].used = true; g_keys[k].len = n; memcpy(g_keys[k].d, d, n); return n; }
static inline int persist_write_int(uint32_t k, int32_t v) { return persist_write_data(k, &v, 4); }
static inline int persist_read_data(uint32_t k, void *d, size_t n) { if (!g_keys[k].used) return -1; int l = g_keys[k].len < (int)n ? g_keys[k].len : (int)n; memcpy(d, g_keys[k].d, l); return l; }
static inline int32_t persist_read_int(uint32_t k) { int32_t v = 0; persist_read_data(k, &v, 4); return v; }
static inline bool persist_exists(uint32_t k) { return g_keys[k].used; }
static inline int persist_delete(uint32_t k) { g_keys[k].used = false; return 0; }
// resources: read straight from the project's files (PD_DATA_DIR is relative to watch/, where tools/build.sh runs)
#ifndef PD_DATA_DIR
#define PD_DATA_DIR "resources/data"
#endif
typedef uint32_t ResHandle;
#include "res_ids.h"
static inline ResHandle resource_get_handle(uint32_t id) { return id; }
static inline size_t resource_load_byte_range(ResHandle h, uint32_t off, uint8_t *buf, size_t n) {
  FILE *f = fopen(h == RESOURCE_ID_TEXT ? PD_DATA_DIR "/text.bin" :
                  h == RESOURCE_ID_MUSIC_THEME ? PD_DATA_DIR "/music_theme.bin" :
                  h == RESOURCE_ID_MUSIC_GAME ? PD_DATA_DIR "/music_game.bin" :
                  h == RESOURCE_ID_SFX ? PD_DATA_DIR "/sfx.bin" :
                  h == RESOURCE_ID_MOB_DEFS ? PD_DATA_DIR "/mobs.bin" :
                  h == RESOURCE_ID_ITEM_DEFS ? PD_DATA_DIR "/item_defs.bin" : "/dev/null", "rb");
  if (!f) return 0;
  fseek(f, off, SEEK_SET); size_t r = fread(buf, 1, n, f); fclose(f); return r;
}
#define APP_LOG(level, ...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define APP_LOG_LEVEL_ERROR 1
#define APP_LOG_LEVEL_DEBUG 2

// palette constants (values only matter on the watch)
#define GColorOrangeARGB8 0xC1
#define GColorIndigoARGB8 0xC2
#define GColorBlackARGB8 0xC3
#define GColorBrassARGB8 0xC4
#define GColorBulgarianRoseARGB8 0xC5
#define GColorBlueMoonARGB8 0xC6
#define GColorWhiteARGB8 0xC7
#define GColorGreenARGB8 0xC8
#define GColorRoseValeARGB8 0xC9
#define GColorBrilliantRoseARGB8 0xCA
#define GColorBlueARGB8 0xCB
#define GColorPictonBlueARGB8 0xCC
#define GColorDarkCandyAppleRedARGB8 0xCD
#define GColorJaegerGreenARGB8 0xCE
#define GColorLavenderIndigoARGB8 0xCF
#define GColorKellyGreenARGB8 0xD0

// speaker: the host build records the stream (snd_host.c)
typedef enum { SpeakerPcmFormat_8kHz_8bit, SpeakerPcmFormat_16kHz_8bit, SpeakerPcmFormat_8kHz_16bit, SpeakerPcmFormat_16kHz_16bit } SpeakerPcmFormat;
bool speaker_stream_open(SpeakerPcmFormat f, uint8_t vol);
uint32_t speaker_stream_write(const void *d, uint32_t n);
void speaker_stream_close(void);
void speaker_stop(void);
