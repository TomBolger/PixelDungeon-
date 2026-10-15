/*
 * Pixel Dungeon for Pebble - item names (an overlay, 0.75: out of the app, room for the Assist's Off and Max modes).
 * Item.toString without the quantity (PD shows that on the slot): name, +level, and " :STR" once known (missiles: always).
 */
#include "pd.h"
#include "items_int.h"
#include "game_int.h"
#include "ovl.h"

static const char *const WE_WORDS[WE_COUNT] = { "", "blazing", "venomous", "grim", "stunning", "vampiric", "chilling", "shocking",
  "unstable", "eldritch", "lucky", "tempered" };
static void name_of(const Item *it, char *buf, int len) {
  char tmp[40]; int k = it->kind, c = DEFS[k].cat; const char *n;
  if (c == CAT_POTION && !item_known(k)) { snprintf(tmp, sizeof tmp, "%s potion", POTION_COLORS[G.potion_flavor[k - IK_FIRST_POTION]]); n = tmp; }
  else if (c == CAT_SCROLL && !item_known(k)) { snprintf(tmp, sizeof tmp, "scroll \"%s\"", SCROLL_RUNES[G.scroll_flavor[k - IK_FIRST_SCROLL]]); n = tmp; }
  else if (c == CAT_WAND && !item_known(k)) { snprintf(tmp, sizeof tmp, "%s wand", WAND_WOODS[G.wand_flavor[k - IK_FIRST_WAND]]); n = tmp; }
  else if (c == CAT_RING && !item_known(k)) { snprintf(tmp, sizeof tmp, "%s ring", RING_GEMS[G.ring_flavor[k - IK_FIRST_RING]]); n = tmp; }
  else n = txt(DEFS[k].name);
  bool lk = it->flags & IF_LEVEL_KNOWN;
  char lvl[8] = "";
  if (lk && it->lvl) snprintf(lvl, sizeof lvl, "%+d", it->lvl);
  char en[40];                                        // Weapon.name / Armor.name: the enchantment's words
  if (it->ench && c == CAT_ARMOR && it->ench < AG_COUNT) { snprintf(en, sizeof en, "%s of %s", n, ench_glyph_name(it->ench)); n = en; }
  else if (it->ench && c != CAT_WAND && it->ench < WE_COUNT) { snprintf(en, sizeof en, "%s %s", WE_WORDS[it->ench], n); n = en; }   // weapons, the boomerang
  bool br = lk && item_broken(it);
  if (lk && (c == CAT_WEAPON || c == CAT_ARMOR || c == CAT_MISSILE)) snprintf(buf, len, "%s%s%s :%d", br ? "broken " : "", n, lvl, item_str(it));
  else if (lk && c == CAT_WAND) {                     // Wand.toString: " (charges/max)"
    char ch[4]; snprintf(ch, sizeof ch, "%d", it->ench);
    snprintf(buf, len, "%s%s%s (%s/%d)", br ? "broken " : "", n, lvl, it->flags & IF_CHARGE_KNOWN ? ch : "?", wand_max(it));
  } else if (k == IK_DEW_VIAL) snprintf(buf, len, "%s (%d/10)", n, G.dew);   // DewVial.toString: its status
  else snprintf(buf, len, "%s%s%s", br ? "broken " : "", n, lvl);
}
OVL_ENTRY(name, (void *)name_of);
