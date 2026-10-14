/*
 * Pixel Dungeon for Pebble - long texts read from the TEXT resource (tools/build_text.py).
 * Keeping them out of the code image is what lets the game fit Pebble's 64 KB app limit.
 */
#include "pd.h"

#define RING 4
static char *s_ring[RING];
static int s_next;

const char *txt(int id) {
  if (id <= 0 || id >= TX_COUNT) return "";
  ResHandle h = resource_get_handle(RESOURCE_ID_TEXT);
  uint16_t off[2];
  if (resource_load_byte_range(h, 2 + 2 * id, (uint8_t *)off, 4) != 4) return "";
  int len = off[1] - off[0];
  char *b = malloc(len + 1);
  if (!b) return "";
  resource_load_byte_range(h, 2 + 2 * (TX_COUNT + 1) + off[0], (uint8_t *)b, len);
  b[len] = 0;
  free(s_ring[s_next]); s_ring[s_next] = b; s_next = (s_next + 1) % RING;
  return b;
}
