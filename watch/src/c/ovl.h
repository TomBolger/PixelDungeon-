/*
 * Overlays: code that only runs now and then (the level generator, ...) lives in a resource and is
 * loaded into the heap while needed. tools/build_ovl.py links each overlay against the final app
 * image and records which words must be moved by the overlay's base or by the app's base.
 */
#pragma once
extern int g_ovl_need, g_ovl_big;                 // the last overlay that found no room: its size, the largest free block
#include <pebble.h>

#define OVL_MAGIC 0x4C564F50u                     // "POVL": first word of a loaded overlay image
typedef struct { uint32_t magic; void *fn[6]; } OvlHeader;
// resource layout: OvlFileHeader, image (img_size bytes), u16 offsets moved by the overlay base,
// u16 offsets moved by the app base
#define OVL_FILE_MAGIC 0x31564F50u                // "POV1"
typedef struct {
  uint32_t magic;
  uint16_t img_size, bss_size, n_rel_ovl, n_rel_app;
  uint32_t anchor_data, anchor_code;              // link addresses of TERRAIN_FLAGS and rnd_next in the app this was built for
} OvlFileHeader;

// each overlay ends with OVL_ENTRY(name, fn0, fn1, ...): its entry table, first in the image.
// The host test build links overlays into the program, so there each table gets its own name.
#ifdef OVL_HOST
#define OVL_ENTRY(name, ...) const OvlHeader ovl_##name = { OVL_MAGIC, { __VA_ARGS__ } }
#else
#define OVL_ENTRY(name, ...) __attribute__((section(".ovl_header"), used)) const OvlHeader ovl_header = { OVL_MAGIC, { __VA_ARGS__ } }
#endif

const OvlHeader *ovl_load(uint32_t res_id);       // NULL if missing, out of memory, or built for another app image
void ovl_free(const OvlHeader *o);
const OvlHeader *ovl_keep(uint32_t res_id);      // 0.20: a resident overlay (on the heap, not in the arena)
void ovl_arena_init(void);
int ovl_arena_free(void);
