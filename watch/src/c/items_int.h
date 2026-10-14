/*
 * Pixel Dungeon for Pebble - items.c internals shared with the overlays.
 */
#pragma once
#include "pd.h"
// tier / STR / accuracy and delay x100 (melee), min-max (missiles); name and desc are text ids
typedef struct { uint16_t name; uint8_t cat, stack, sprite, tier, str, acu, dly, mn, mx; uint16_t desc; } ItemDef;
extern const ItemDef *DEFS;
extern const char *const POTION_COLORS[12], *const SCROLL_RUNES[12], *const WAND_WOODS[12], *const RING_GEMS[12];
int melee_max0(int k);                     // typical max damage of a +0 melee weapon
