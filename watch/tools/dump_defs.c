// host tool: writes the item table (src/c/item_defs.inc) as resources/data/item_defs.bin (byte layout = the watch's)
#include "pd.h"
#include "items_int.h"
#include "item_defs.inc"
int main(int argc, char **argv) {
  _Static_assert(sizeof(ItemDef) == 14, "ItemDef layout");
  FILE *f = fopen(argv[1], "wb");
  if (!f) return 1;
  fwrite(DEFS_TABLE, sizeof(ItemDef), IK_COUNT, f);
  fclose(f);
  return 0;
}
