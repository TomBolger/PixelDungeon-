"""Convert Pixel Dungeon art to Pebble-ready PNGs (64-color palette).

Method: stretch contrast (levels), then snap each color to the nearest Pebble
color using a luminance-weighted distance. Alpha becomes fully on/off.
Tiles use a stronger stretch so the dark sewer stone keeps its mortar lines.
Character sheets get a mirrored second row, because Pebble can't flip bitmaps.

Usage: python convert_art.py <pixel-dungeon/assets> <project/resources/images>
"""
import sys, os, colorsys
from PIL import Image

SRC, OUT = sys.argv[1], sys.argv[2]
PAL = [(r, g, b) for r in (0, 85, 170, 255) for g in (0, 85, 170, 255) for b in (0, 85, 170, 255)]
TILES, SPRITES, CHARS = (40, 205), (20, 230), (19, 230)   # CHARS: heroes, monsters, items (0.5.2 punch)
_cache = {}

def near(c):
    return min(PAL, key=lambda p: 2 * (p[0] - c[0]) ** 2 + 4 * (p[1] - c[1]) ** 2 + 3 * (p[2] - c[2]) ** 2)

def _hue(c):
    h, _, s = colorsys.rgb_to_hls(*(v / 255 for v in c)); return h * 360, s
_PAL_HUE = {p: _hue(p)[0] for p in PAL}
def near_hue(c, lim=35):
    """0.60: near() without leaving the colour's hue family: a coloured pixel snaps to a grey or to a Pebble colour
    within `lim` degrees of its hue. Plain near() turned tan and parchment pink (scrolls, leather, the backpack icon)."""
    h0, s0 = _hue(c)
    if s0 < 0.12: return near(c)
    return min((p for p in PAL if p[0] == p[1] == p[2] or min(abs(_PAL_HUE[p] - h0), 360 - abs(_PAL_HUE[p] - h0)) <= lim),
               key=lambda p: 2 * (p[0] - c[0]) ** 2 + 4 * (p[1] - c[1]) ** 2 + 3 * (p[2] - c[2]) ** 2)

def pebbleize(img, levels, alpha_cut=100, keep_gray=False, hue_guard=False):
    lo, hi = levels
    img = img.convert("RGBA")
    px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a < alpha_cut:
                px[x, y] = (0, 0, 0, 0)
                continue
            if a == 254:                                   # already snapped by the caller
                px[x, y] = (r, g, b, 255)
                continue
            key = (r, g, b, lo, hi, keep_gray, hue_guard)
            snap = near_hue if hue_guard else near
            if keep_gray and max(r, g, b) - min(r, g, b) < 24:
                v = max(0, min(255, (((r + g + b) / 3) - lo) * 255 / (hi - lo)))
                _cache[key] = (min((0, 85, 170, 255), key=lambda p: abs(p - v)),) * 3
            if key not in _cache:
                # 0.5.2 "punch" for the dim Pebble screen: lift mid-tones (gamma 0.85), keep near-greys neutral
                # (tiles only), boost already-colourful pixels' saturation 1.3x
                if levels == SPRITES:                      # UI art: unchanged mapping
                    _cache[key] = snap(tuple(max(0, min(255, (v - lo) * 255 / (hi - lo))) for v in (r, g, b)))
                    px[x, y] = _cache[key] + (255,)
                    continue
                c = [max(0.0, min(1.0, (v - lo) / (hi - lo))) ** (0.85 if levels == TILES else 1.0) for v in (r, g, b)]
                if levels == TILES and max(r, g, b) - min(r, g, b) < 28:
                    m = sum(c) / 3 * 255
                    _cache[key] = (min((0, 85, 170, 255), key=lambda p: abs(p - m)),) * 3
                else:
                    h, l, sat = colorsys.rgb_to_hls(*c)
                    if sat > 0.3: sat = min(1.0, sat * 1.3)
                    _cache[key] = snap(tuple(v * 255 for v in colorsys.hls_to_rgb(h, l, sat)))
            px[x, y] = _cache[key] + (255,)
    return img

def with_mirror(img, fw):
    """Row 0 = original frames (facing right), row 1 = each frame mirrored."""
    out = Image.new("RGBA", (img.width, img.height * 2))
    out.paste(img, (0, 0))
    for i in range(img.width // fw):
        f = img.crop((i * fw, 0, i * fw + fw, img.height)).transpose(Image.FLIP_LEFT_RIGHT)
        out.paste(f, (i * fw, img.height))
    return out

def crop(name, box):
    return Image.open(os.path.join(SRC, name)).crop(box)

def save(img, name):
    img.save(os.path.join(OUT, name))
    data = img.get_flattened_data()
    n = len({p for p in data if p[3]}) + (1 if any(p[3] == 0 for p in data) else 0)
    b = 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8
    ram = img.width * img.height * b // 8
    print(f"{name:20s} {img.width:3d}x{img.height:<3d} {n:2d} colors -> {b}bpp ~{ram / 1024:.1f} KB")
    return ram

os.makedirs(OUT, exist_ok=True)
total = 0
# 0.5: the whole sewer tile sheet, indexed by terrain code like PD's DungeonTilemap
# 0.5.1: grass one Pebble shade brighter (greenish source pixels are lifted before snapping)
t0 = crop("tiles0.png", (0, 0, 256, 64)).convert("RGBA")
tp = t0.load()
for y in range(t0.height):
    for x in range(t0.width):
        r, g, b, a = tp[x, y]
        if a and g - max(r, b) > 25:   # grass: keep 0.5.1's look exactly (levels 45-210, no gamma)
            lifted = [min(255, int(v * 1.45)) for v in (r, g, b)]
            tp[x, y] = near(tuple(max(0, min(255, (v - 45) * 255 / 165)) for v in lifted)) + (254,)
ts = pebbleize(t0, TILES)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tile_rules   # 0.60: one clear colour per kind of trap (fire was as red as the alarm trap, paralytic as orange as fire)
tile_rules.trap_inks(crop("tiles0.png", (0, 0, 256, 64)).convert("RGBA").load(), ts.load())
total += save(ts, "tiles_sewers.png")

# Sewer water: PD scrolls a translucent texture over a base color; bake it opaque.
water = Image.new("RGBA", (32, 32), (40, 70, 66, 255))
w = Image.open(os.path.join(SRC, "water0.png")).convert("RGBA")
water.alpha_composite(w)
# 0.5.3: the baked texture's brightness drives PD's sewer green-teal, lifted like the grass (deep, jade, soft teal)
wp = water.load()
ls = sorted(sum(wp[x, y][:3]) for y in range(32) for x in range(32))
q1, q2 = ls[len(ls) * 50 // 100], ls[len(ls) * 88 // 100]
for y in range(32):
    for x in range(32):
        v = sum(wp[x, y][:3]); wp[x, y] = ((0, 85, 85) if v < q1 else (0, 170, 85) if v < q2 else (85, 170, 170)) + (255,)
total += save(water, "water_sewers.png")

def max16(img):
    """At most 16 colours (4 bpp halves the RAM): fold the rarest colour into its nearest until 16 remain."""
    px = img.load(); counts = {}
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y][3]: counts[px[x, y][:3]] = counts.get(px[x, y][:3], 0) + 1
    remap = {}
    while len(counts) > 15:                        # 15 + transparent
        rare = min(counts, key=counts.get)
        n = counts.pop(rare)
        to = min(counts, key=lambda c: sum((a - b) ** 2 for a, b in zip(c, rare)))
        counts[to] += n
        for k, v in list(remap.items()):
            if v == rare: remap[k] = to
        remap[rare] = to
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a and (r, g, b) in remap: px[x, y] = remap[(r, g, b)] + (255,)
    return img

# 0.60: what each Pebble colour looks like on the panel (the SDK's screenshot colour correction), in PAL order
PANEL = dict(zip(PAL, [((v >> 16) & 255, (v >> 8) & 255, v & 255) for v in (
    0x000000, 0x001E41, 0x004387, 0x0068CA, 0x2B4A2C, 0x27514F, 0x16638D, 0x007DCE, 0x5E9860, 0x5C9B72, 0x57A5A2,
    0x4CB4DB, 0x8EE391, 0x8EE69E, 0x8AEBC0, 0x84F5F1, 0x4A161B, 0x482748, 0x40488A, 0x2F6BCC, 0x564E36, 0x545454,
    0x4F6790, 0x4180D0, 0x759A64, 0x759D76, 0x71A6A4, 0x69B5DD, 0x9EE594, 0x9DE7A0, 0x9BECC2, 0x95F6F2, 0x99353F,
    0x983E5A, 0x955694, 0x8F74D2, 0x9D5B4D, 0x9D6064, 0x9A7099, 0x9587D5, 0xAFA072, 0xAEA382, 0xABABAB, 0xA7BAE2,
    0xC9E89D, 0xC9EAA7, 0xC7F0C8, 0xC3F9F7, 0xE35462, 0xE25874, 0xE16AA3, 0xDE83DC, 0xE66E6B, 0xE6727C, 0xE37FA7,
    0xE194DF, 0xF1AA86, 0xF1AD93, 0xEFB5B8, 0xECC3EB, 0xFFEEAB, 0xFFF1B5, 0xFFF6D3, 0xFFFFFF)]))
def panel_lab(c):
    """CIE L*a*b* of a Pebble colour as the panel shows it."""
    def lin(u): u /= 255; return ((u + 0.055) / 1.055) ** 2.4 if u > 0.04045 else u / 12.92
    r, g, b = (lin(v) for v in PANEL[c])
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    X, Y, Z = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.9505, 0.2126 * r + 0.7152 * g + 0.0722 * b, (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.089
    return 116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))
def fold16(img):
    """0.60: max16() as the panel sees it. Each step drops the colour whose loss costs least: for every pixel that would
    move, (how far apart the two colours look on the panel)^2, so many slightly-off pixels beat a few badly-off ones.
    max16 simply dropped the rarest colour into its RGB-nearest: firebloom lost its yellow heart, sungrass its buds, the
    fire buff its orange flame, levitation its cyan."""
    px = img.load(); counts = {}
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y][3]: counts[px[x, y][:3]] = counts.get(px[x, y][:3], 0) + 1
    lab = {c: panel_lab(c) for c in counts}
    d = lambda a, b: sum((u - v) ** 2 for u, v in zip(lab[a], lab[b])) ** 0.5
    orig, remap = dict(counts), {}
    while len(counts) > 15:                        # 15 + transparent
        best = None
        for c in sorted(counts):
            to = min((o for o in sorted(counts) if o != c), key=lambda o: d(c, o))
            cost = sum(orig[m] * d(m, to) ** 2 for m in [k for k, v in remap.items() if v == c] + [c])
            if best is None or cost < best[0]: best = (cost, c, to)
        _, c, to = best
        counts[to] += counts.pop(c)
        for k, v in list(remap.items()):
            if v == c: remap[k] = to
        remap[c] = to
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a and (r, g, b) in remap: px[x, y] = remap[(r, g, b)] + (255,)
    return img

# 0.6: HeroSprite.tiers(): one 15 px row per armour tier (0 none .. 5 plate); the game loads only the worn one
for cls, f in (("warrior", "warrior.png"), ("mage", "mage.png"), ("rogue", "rogue.png"), ("huntress", "ranger.png")):   # 0.13: every class
    for t in range(7):                              # 0.12: + 6, the class armour
        total += save(max16(pebbleize(crop(f, (0, t * 15, 252, t * 15 + 15)), CHARS)), f"hero_{cls}_{t}.png")   # 0.20: no mirrored row, the blitter flips

def outlined(img, fw):
    """0.6: 1 px black edge around the creature inside each frame, so grey monsters read on grey floors."""
    out = img.copy(); src = img.load(); dst = out.load()
    for y in range(img.height):
        for x in range(img.width):
            if src[x, y][3]: continue
            f0 = x // fw * fw
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if f0 <= nx < f0 + fw and 0 <= ny < img.height and src[nx, ny][3]:
                    dst[x, y] = (0, 0, 0, 255); break
    return out

def stone_text(img, cuts, hi=(255, 255, 255), mix=None, moss=None, base=None, other=None):
    """0.60: PD's carved-stone lettering (logo, banners) as a grey ramp by the pixel's brightness. Snapping each
    pixel on its own turned the olive-grey gradient into bands of khaki, grey and olive and merged the bevels into
    the body; a ramp keeps one hue, the light bevel, the body and the dark edge apart on the dim panel.
    cuts = (black below, dark grey below, light grey below); above the last: hi (the bevel).
    mix = (from, to): that band is checkered light/dark, a soft step in a long gradient (the logo).
    moss = (dark, light): greenish pixels (PD's moss on the logo) stay green.
    base = (below, colour): the darkest body tone is checkered with colour (the logo's mossy, shadowed foot).
    other(rgb): the colour for pixels that aren't stone (blood, the sword's hilt); default: the usual snap."""
    img = img.convert("RGBA"); px = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a < 100: px[x, y] = (0, 0, 0, 0); continue
            v, ge = (r + g + b) / 3, g - (r + b) / 2
            if moss and ge > 28 and v >= 40: c = moss[1] if v >= 80 else moss[0]
            elif max(r, g, b) - min(r, g, b) >= 40 or abs(ge) >= 28:
                c = other((r, g, b)) if other else near(tuple(max(0, min(255, (q - 20) * 255 / 210)) for q in (r, g, b)))
            elif v < cuts[0]: c = (0, 0, 0)
            elif mix and mix[0] <= v < mix[1]: c = (170, 170, 170) if (x + y) % 2 == 0 else (85, 85, 85)
            elif v < cuts[1]: c = base[1] if base and v < base[0] and (x + y) % 2 == 0 else (85, 85, 85)
            elif v < cuts[2]: c = (170, 170, 170)
            else: c = hi
            px[x, y] = c + (255,)
    return img

# 0.60: the logo: pale green bevel (the stone's tint), light "Pixel" over a darker "Dungeon" as in PD, mossy foot
total += save(stone_text(crop("banners.png", (0, 0, 128, 70)), (45, 92, 142), hi=(170, 255, 170), mix=(92, 110),
                         moss=((0, 85, 0), (85, 170, 85)), base=(62, (0, 85, 0))), "logo.png")

print(f"Total bitmap RAM if all loaded: ~{total / 1024:.1f} KB")

# ---- 0.2: item sheet (10 x 3) and toolbar ----
# order must match ITEM_SPRITES in items.c
# 0.6: every 0.6 item; order must match the SP_ enum in items.c
ITEM_IDX = ([14, 4, 81, 112, 113, 114, 116] + [19, 16, 2, 17, 29, 18, 20, 21, 22, 23, 30] + [31, 108, 109, 15, 110, 107]
            + [24, 25, 26, 27, 28] + list(range(56, 68)) + [40, 41, 42, 43, 44, 45, 46, 47, 76, 77, 78, 79]
            + [5, 6, 7, 11, 0]
            + [48, 49, 50, 51, 52, 53, 54, 55, 68, 69, 70, 71]          # 0.7: wands (Wand.woods order)
            + [32, 33, 34, 35, 36, 37, 38, 39, 72, 73, 74, 75]          # 0.7: rings (Ring.gems order)
            + list(range(88, 96)) + [120, 9, 10, 8] + [100, 103, 101, 102, 121, 118, 122]   # 0.8: seeds, vial, keys, quest items
            + [1, 82, 84, 85, 86, 87, 123, 124, 125] + [83, 104, 111, 115, 106]             # misc, bags, food, boomerang
            + [12, 13, 105, 126, 97, 96, 98, 99, 3]                     # chests, keyring, class armours, magic missile
            + [117]                                                     # 0.9: scroll of wipe out
            + [127])                                                    # 0.14: ItemSpriteSheet.SMTH (the catalogue's unknown "?")
items = Image.open(os.path.join(SRC, "items.png")).convert("RGBA")
sheet = Image.new("RGBA", (160, 16 * ((len(ITEM_IDX) + 9) // 10)))
for i, idx in enumerate(ITEM_IDX):
    x, y = (idx % 8) * 16, (idx // 8) * 16
    sheet.paste(items.crop((x, y, x + 16, y + 16)), ((i % 10) * 16, (i // 10) * 16))
# 0.60: an unknown potion is known only by its colour, so each liquid's (light, dark) tones are picked by hand to stay
# apart on the panel (worst pair, CIE dE as the panel shows it: 9 -> 28). The plain snap made golden and amber the same
# orange, gave crimson no shade, and sank bistre to black like charcoal. A ring is known by its gem: agate's dark tone
# was quartz's, so agate is grey.
GEM_FIX = {56: {(0, 196, 232): (0, 255, 255), (0, 131, 155): (0, 170, 170)},     # turquoise (as before)
           57: {(255, 0, 38): (255, 0, 0), (210, 0, 25): (170, 0, 0)},           # crimson: a dark shade again
           58: {(0, 62, 255): (0, 85, 255), (0, 41, 184): (0, 0, 170)},          # azure (as before)
           59: {(0, 180, 62): (0, 170, 85), (0, 116, 38): (0, 85, 0)},           # jade (as before)
           60: {(217, 172, 0): (255, 255, 0), (149, 118, 0): (170, 170, 0)},     # golden: yellow, not amber's orange
           61: {(238, 0, 156): (255, 0, 255), (165, 0, 107): (170, 0, 85)},      # magenta: away from crimson
           62: {(62, 62, 62): (85, 85, 85), (19, 19, 19): (0, 0, 0)},            # charcoal (as before)
           63: {(246, 236, 218): (255, 255, 255), (183, 175, 162): (170, 170, 170)},   # ivory (as before)
           64: {(255, 140, 9): (255, 170, 0), (202, 103, 5): (170, 85, 0)},      # amber: orange over brown
           65: {(107, 54, 6): (170, 85, 0), (54, 24, 3): (85, 0, 0)},            # bistre: brown, not black
           66: {(122, 0, 184): (85, 0, 170), (77, 0, 118): (85, 0, 85)},         # indigo (as before)
           67: {(143, 154, 154): (170, 170, 170), (70, 76, 76): (85, 85, 85)},   # silver (as before)
           75: {(138, 102, 102): (85, 85, 85)}}                                  # agate's dark tone
sp = sheet.load()
for i, idx in enumerate(ITEM_IDX):
    for y in range(16):
        for x in range(16):
            p = sp[(i % 10) * 16 + x, (i // 10) * 16 + y]
            if p[3] >= 100 and p[:3] in GEM_FIX.get(idx, {}): sp[(i % 10) * 16 + x, (i // 10) * 16 + y] = GEM_FIX[idx][p[:3]] + (254,)
total += save(pebbleize(sheet, CHARS, hue_guard=True), "items_02.png")   # 0.60: no more pink parchment and leather
# 0.60: PlantSprite frames (plants.png, 8 x 16 px), drawn on grass and floor; 16 colours (4 bpp)
# 0.60: PD rings each plant with a 30% black shadow that the alpha cut dropped, so plants melted into grass; it is
# now a solid black edge like the items' (the panel has no translucency). The hue guard keeps earthroot brown, not
# grey, and fold16 keeps firebloom's yellow heart and sungrass's yellow buds (max16 had folded them into orange).
plants = crop("plants.png", (0, 0, 128, 16)).convert("RGBA"); pp = plants.load()
for y in range(16):
    for x in range(128):
        if pp[x, y] == (0, 0, 0, 77): pp[x, y] = (0, 0, 0, 254)
total += save(fold16(pebbleize(plants, CHARS, hue_guard=True)), "plants.png")
# PD toolbar row: wait(0) search(20) info(40) backpack(60) quickslot(83..)
tsrc = crop("toolbar.png", (0, 7, 106, 32)).convert("RGBA"); tool = pebbleize(tsrc, SPRITES, keep_gray=True)
sp_, tp_ = tsrc.load(), tool.load()
for y in range(tool.height):                        # 0.60: the pictures' coloured pixels never snap to the button's grey,
    for x in range(83):                             # where they vanished (the glass's handle, the backpack's shading)
        r, g, b, a = sp_[x, y]
        if a < 100 or max(r, g, b) - min(r, g, b) < 24: continue
        if (r, g, b) == (90, 40, 31): tp_[x, y] = (170, 0, 0, 255); continue   # the "?": PD's dark red on a darker face
        if tp_[x, y][:3] == (85, 85, 85):           # read as a smudge; one step brighter it reads as red
            lv = tuple(max(0, min(255, (v - SPRITES[0]) * 255 / (SPRITES[1] - SPRITES[0]))) for v in (r, g, b))
            tp_[x, y] = min((p for p in PAL if p != (85, 85, 85)), key=lambda p: 2 * (p[0] - lv[0]) ** 2 + 4 * (p[1] - lv[1]) ** 2 + 3 * (p[2] - lv[2]) ** 2) + (255,)
total += save(tool, "toolbar.png")
print(f"Total with 0.2 sheets: ~{total / 1024:.1f} KB")

# ---- 0.3: PD chrome (window frames), status pane, hp bar ----
chrome = crop("chrome.png", (0, 0, 86, 36)).convert("RGBA"); cp = chrome.load()
for y in range(22, 36):                             # 0.6.1: tab interiors are the window's colour (they snapped to black)
    for x in range(64, 84):
        if cp[x, y][3] and cp[x, y][:3] in ((53, 56, 49), (55, 59, 52)): cp[x, y] = (85, 85, 85, 254)
chrome = pebbleize(chrome, SPRITES, keep_gray=True); cp = chrome.load()
for y in range(6, 16):                              # 0.6.5: window interiors black, so white text reads on the dim screen
    for x in range(6, 16): cp[x, y] = (0, 0, 0, 255)   # plain windows only; the backpack (tabbed) stays grey
total += save(chrome, "chrome.png")   # 0.5.1: + tab frames
total += save(pebbleize(crop("status_pane.png", (0, 0, 128, 36)), SPRITES, alpha_cut=24, keep_gray=True), "status_pane.png")
total += save(pebbleize(crop("hp_bar.png", (0, 0, 64, 4)), SPRITES), "hp_bar.png")
print(f"Total with 0.3 sheets: ~{total / 1024:.1f} KB")

# ---- 0.4: title + hero select + attack indicator ----
arcs = Image.new("RGBA", (64, 64))
bg = Image.open(os.path.join(SRC, "arcs1.png")).convert("RGBA")
for y in (0, 32):
    for x in (0, 32): arcs.paste(bg, (x, y))
arcs.alpha_composite(Image.open(os.path.join(SRC, "arcs2.png")).convert("RGBA"))
# arcs are ~12% grey: Pebble has nothing between black and 33% grey, so pillars are dithered
ap = arcs.load()
for y in range(64):
    for x in range(64):
        r, g, b, al = ap[x, y]
        if (r, g, b) == (29, 32, 33): ap[x, y] = (85, 85, 85, 255) if (x + y) % 2 == 0 else (0, 0, 0, 255)
        elif r > 40 or (r, g, b) == (26, 18, 11): ap[x, y] = (85, 0, 0, 255)
        else: ap[x, y] = (0, 0, 0, 255)
total += save(arcs, "arcs.png")
# 0.5.2: Play stairs hand-mapped so the three steps keep separate tones (light, checker, dark)
L_, M_, D_ = (170, 170, 170), (85, 85, 85), (0, 0, 0)
PLAY_MAP = {(143, 142, 124): L_, (124, 124, 105): M_, (109, 107, 93): L_, (89, 84, 75): M_, (92, 90, 79): "LM",
            (75, 71, 65): "MD", (76, 72, 66): M_, (64, 58, 56): D_, (61, 56, 54): M_}
play = crop("dashboard.png", (0, 0, 32, 32)).convert("RGBA"); pp = play.load()
for y in range(32):
    for x in range(32):
        r, g, b, a = pp[x, y]
        if a < 100: pp[x, y] = (0, 0, 0, 0); continue
        m = PLAY_MAP.get((r, g, b), D_)
        if m == "LM": m = L_ if (x + y) % 2 == 0 else M_
        if m == "MD": m = M_ if (x + y) % 2 == 0 else D_
        pp[x, y] = m + (255,)
# 0.14: + the Rankings podium (dashboard index 2) beside it
hs = crop("dashboard.png", (64, 0, 96, 32)).convert("RGBA"); hp = hs.load()
for y in range(32):
    for x in range(32):
        r, g, b, a = hp[x, y]
        if a < 100: hp[x, y] = (0, 0, 0, 0)
        elif max(r, g, b) - min(r, g, b) < 24: v = (r + g + b) // 3; hp[x, y] = ((255,) * 3 if v > 190 else (170,) * 3 if v > 120 else (85,) * 3) + (255,)   # the silver bar stays grey
        elif r > 185 and g > 130 and b < 60: hp[x, y] = (255, 255, 0, 254)   # 0.60: the gold bar yellow (orange shows as peach),
        elif (r, g, b) in ((153, 115, 0), (188, 140, 0)): hp[x, y] = (170, 170, 0, 254)   # its shade khaki, not the bronze's brown
both = Image.new("RGBA", (64, 32)); both.paste(play, (0, 0)); both.paste(pebbleize(hs, SPRITES), (32, 0))
total += save(both, "play.png")
av = pebbleize(crop("avatars.png", (0, 0, 96, 28)), SPRITES, keep_gray=True)   # 0.60: greys neutral (the rogue's coat was olive)
faded = av.copy(); fp = faded.load()                 # locked heroes: PD shows them faded
for y in range(faded.height):
    for x in range(faded.width):
        r, g, b, a = fp[x, y]
        if a: fp[x, y] = near((r * 0.35 + 20, g * 0.35 + 20, b * 0.35 + 20)) + (255,)
sheet = Image.new("RGBA", (96, 56)); sheet.paste(av, (0, 0)); sheet.paste(faded, (0, 28))
total += save(sheet, "avatars.png")
total += save(stone_text(crop("banners.png", (0, 140, 128, 161)), (60, 88, 118)), "banner_select.png")   # 0.60: as the logo
# The right-hand tags: PD's TAG (chrome.png) tinted. 0.60: its three tones (light top and left edge, body, dark
# bottom) each get their own colour of the tint's hue; tinting then snapping merged the edge into the body, turned
# the loot tag's blue to teal and the pale tag to green. The red's edge is the red buttons' (chrome BUTTON).
def tag_art(light, body, dark):
    tag = Image.open(os.path.join(SRC, "chrome.png")).convert("RGBA").crop((22, 18, 38, 32)); tp = tag.load()
    for y in range(tag.height):
        for x in range(tag.width):
            r, g, b, a = tp[x, y]
            tp[x, y] = (0, 0, 0, 0) if a < 100 else (light if r >= 165 else dark if r < 135 else body) + (255,)
    return tag
red = Image.new("RGBA", (28, 14)); red.paste(tag_art((255, 85, 0), (170, 0, 0), (85, 0, 0)), (0, 0))   # Tag.hardlight(0xFF4A4D): attack
glass = tool.crop((24, 7, 36, 19)); gp = glass.load()   # 0.60: + the zoom tag's search glass at (16, 1), without the button's grey face
for y in range(12):                                     # (main.c drew it from toolbar.png, a grey square inside the red tag)
    for x in range(12):
        if gp[x, y][:3] == (85, 85, 85): gp[x, y] = (0, 0, 0, 0)
# 0.67 (owner: "gold on red makes it hard to see"): on the red tag the glass is silver, its lens pale blue glass with the shine
SILVER = {(170, 170, 85): (170, 170, 170), (255, 255, 170): (255, 255, 255), (85, 85, 0): (85, 85, 85), (85, 0, 0): (0, 0, 0), (170, 170, 170): (85, 170, 255)}
for y in range(12):
    for x in range(12):
        c = gp[x, y]
        if c[3] and c[:3] in SILVER: gp[x, y] = SILVER[c[:3]] + (255,)
out = set(); todo = [(x, y) for x in range(12) for y in (0, 11)] + [(x, y) for y in range(12) for x in (0, 11)]
while todo:                                             # the lens: the clear cells the outside can't reach
    x, y = todo.pop()
    if not (0 <= x < 12 and 0 <= y < 12) or (x, y) in out or gp[x, y][3]: continue
    out.add((x, y)); todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
for y in range(12):
    for x in range(12):
        if not gp[x, y][3] and (x, y) not in out: gp[x, y] = (85, 170, 255, 255)
red.paste(glass, (16, 1))
total += save(red, "tag_red.png")
total += save(tag_art((0, 170, 255), (0, 85, 170), (0, 0, 85)), "tag_blue.png")           # 0.6.1: LootIndicator's Tag(0x1F75CC)
total += save(tag_art((255, 255, 255), (170, 170, 170), (85, 85, 85)), "tag_white.png")   # 0.17: ResumeButton's Tag(0xCDD5C0), pale
print(f"Total with 0.4 sheets: ~{total / 1024:.1f} KB")

# ---- 0.5: sewer monsters, buff icons ----
# 0.7: WandOfFlock.Sheep (SheepSprite: 16x15 frames)
# BuffIndicator icons (7x7): poison 3, hunger 5, starvation 6, cripple 23, bleeding 26
bf = Image.open(os.path.join(SRC, "buffs.png")).convert("RGBA")
strip = Image.new("RGBA", (7 * 30, 7))                 # 0.6: + fire, paralysis, levitation, mind vision, invisible, frost, blind, roots, slow, barkskin, immunity
for i, idx in enumerate((3, 5, 6, 23, 26, 2, 4, 1, 0, 12, 15, 16, 11, 7, 24, 25, 21, 20, 28, 8, 29, 14, 19, 9, 10, 22, 17, 18, 13, 30)):
    strip.paste(bf.crop(((idx % 18) * 7, (idx // 18) * 7, (idx % 18) * 7 + 7, (idx // 18) * 7 + 7)), (i * 7, 0))
def hunger_orange(strip, w):
    """0.60: Hunger's orange (242,115,24) snapped to (255,85,0), which on the panel is Starving's red (dE 12): it is
    now (255,170,0) (dE 40), so the warning that you are starving reads apart from plain hunger."""
    sp = strip.load()
    for y in range(strip.height):
        for x in range(w, 2 * w):                   # cell 1: BuffIndicator.HUNGER
            if sp[x, y][:3] in ((242, 115, 24), (242, 160, 102)): sp[x, y] = (255, 170, 0, 254)
    return strip
total += save(fold16(pebbleize(hunger_orange(strip, 7), SPRITES)), "buffs.png")   # 0.20: 4 bpp (0.60: fold16)
print(f"Total with 0.5 sheets: ~{total / 1024:.1f} KB")

# ---- 0.5.1: UI pass ----
ic = Image.open(os.path.join(SRC, "icons.png")).convert("RGBA")
# Icons: sleep (9x8), alert (8x8), then 10x10 bag tabs: backpack, keyring, seed pouch, scroll holder, wand holster
# 0.6.1: + the quickslot targeting reticle (Icons.TARGET, 16x16) at x 67
strip = Image.new("RGBA", (17 + 50 + 16 + 27, 16))   # 0.17: + Icons.RESUME (x 83, 12x11), SKULL (95, 8x8), COMPASS (103, 7x5)
strip.paste(ic.crop((13, 45, 22, 53)), (0, 0)); strip.paste(ic.crop((22, 45, 30, 53)), (9, 0))
for i, (x, y) in enumerate(((58, 0), (64, 29), (78, 0), (68, 0), (88, 0))):
    strip.paste(ic.crop((x, y, x + 10, y + 10)), (17 + i * 10, 0))
strip.paste(ic.crop((0, 13, 16, 29)), (67, 0))
strip.paste(ic.crop((114, 0, 126, 11)), (83, 0)); strip.paste(ic.crop((0, 0, 8, 8)), (95, 0)); strip.paste(ic.crop((0, 8, 7, 13)), (103, 0))
total += save(max16(pebbleize(strip, SPRITES)), "icons.png")   # 0.20: 4 bpp
lb = Image.open(os.path.join(SRC, "large_buffs.png")).convert("RGBA")
strip = Image.new("RGBA", (16 * 30, 16))                # WndHero buffs, same order as buffs.png
for i, idx in enumerate((3, 5, 6, 23, 26, 2, 4, 1, 0, 12, 15, 16, 11, 7, 24, 25, 21, 20, 28, 8, 29, 14, 19, 9, 10, 22, 17, 18, 13, 30)):
    strip.paste(lb.crop(((idx % 16) * 16, (idx // 16) * 16, (idx % 16) * 16 + 16, (idx // 16) * 16 + 16)), (i * 16, 0))
total += save(pebbleize(hunger_orange(strip, 16), SPRITES), "buffs_large.png")
total += save(stone_text(crop("banners.png", (0, 105, 128, 140)), (45, 80, 118)), "banner_gameover.png")   # 0.60: as the logo
def gilt(c):                                         # 0.60: the sword's bronze hilt by brightness, never rose or grey
    r, g, b = c; v = (r + g + b) / 3
    if not (r >= g >= b and r - g < 70): return near(tuple(max(0, min(255, (q - 20) * 255 / 210)) for q in c))   # the jewel
    return (255, 255, 85) if v >= 140 else (170, 170, 85) if v >= 100 else (170, 85, 0) if v >= 80 else (85, 85, 0) if v >= 55 else (85, 0, 0)
total += save(stone_text(crop("banners.png", (0, 70, 128, 105)), (45, 80, 118), other=gilt), "banner_boss.png")   # 0.17: GameScene.bossSlain
print(f"Total with 0.5.1 sheets: ~{total / 1024:.1f} KB")

# ---- 0.6.5: items and tiles go into 4-bit strips (half the RAM) ----
import subprocess
subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "split_sheets.py")], check=True)

# ---- 0.12: the endings: AmuletScene's amulet; SurfaceScene's frame, clouds, grass and pet (16 colours: 4 bits) ----
total += save(pebbleize(crop("amulet.png", (0, 0, 32, 32)), SPRITES), "amulet.png")
surf = Image.new("RGBA", (152, 141), (0, 0, 0, 0))
surf.paste(crop("surface.png", (0, 0, 152, 125)), (0, 0))
surf.paste(crop("pet.png", (0, 0, 32, 16)), (0, 125))
surf = pebbleize(surf, SPRITES)
from collections import Counter
freq = Counter(p for p in surf.get_flattened_data() if p[3])
keep = [c for c, _ in freq.most_common(15)]
sp = surf.load()
for y in range(surf.height):
    for x in range(surf.width):
        p = sp[x, y]
        if p[3] and p not in keep:
            sp[x, y] = min(keep, key=lambda k: sum((a - b) ** 2 for a, b in zip(k[:3], p[:3])))
total += save(surf, "surface.png")

# ---- 0.18: big-button menus: 16x16 icons drawn at 2x (menu_icons.png) ----
def pix(rows, pal):
    im = Image.new("RGBA", (16, 16))
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch != "." and ch in pal: im.putpixel((x, y), pal[ch] + (255,))
    return im
FLOPPY = ["................",
          ".BBBBBBBBBBBBB..",
          ".BBKKKKKKKKBBBB.",
          ".BBKKKKKWWKBBBB.",
          ".BBKKKKKWWKBBBB.",
          ".BBKKKKKKKKBBBB.",
          ".BBBBBBBBBBBBBB.",
          ".BBBBBBBBBBBBBB.",
          ".BBWWWWWWWWWWBB.",
          ".BBWLLLLLLLLWBB.",
          ".BBWWWWWWWWWWBB.",
          ".BBWLLLLLLLLWBB.",
          ".BBWWWWWWWWWWBB.",
          ".BBWWWWWWWWWWBB.",
          ".BBBBBBBBBBBBBB.",
          "................"]
QUILL = ["...........WWW..",
         "..........WWWL..",
         ".........WWWL...",
         "........WWWL....",
         ".PPPPPPWWWLPP...",
         ".PPPPPWWWLPPP...",
         ".PDDDWWWLDDDP...",
         ".PPPPWWLPPPPP...",
         ".PDDDWLDDDDDP...",
         ".PPPPKPPPPPPP...",
         ".PDDDKDDDDDDP...",
         ".PPPPPPPPPPPP...",
         ".PDDDDDDDDDPP...",
         ".PPPPPPPPPPPP...",
         ".PPPPPPPPPPPP...",
         "................"]
NOTE = ["................",
        ".......WWWWWWWW.",
        ".......WWWWWWWW.",
        ".......W......W.",
        ".......W......W.",
        ".......W......W.",
        ".......W......W.",
        ".......W......W.",
        ".......W......W.",
        "....WWWW...WWWW.",
        "...WWWWW..WWWWW.",
        "...WWWWW..WWWWW.",
        "....WWW....WWW..",
        "................",
        "................",
        "................"]
SPEAKER = ["................",
           "......W.........",
           ".....WW.....W...",
           "....WWW...W..W..",
           "WWWWWWW....W..W.",
           "WWWWWWW..W..W.W.",
           "WWWWWWW...W.W.W.",
           "WWWWWWW...W.W.W.",
           "WWWWWWW...W.W.W.",
           "WWWWWWW..W..W.W.",
           "WWWWWWW....W..W.",
           "....WWW...W..W..",
           ".....WW.....W...",
           "......W.........",
           "................",
           "................"]
GEAR = ["................",
        ".......LL.......",
        "...L..LWWL..L...",
        "..LWLLWWWWLLWL..",
        "...LWWWWWWWWL...",
        "...LWWWLLWWWL...",
        ".LLWWWL..LWWWLL.",
        ".LWWWL....LWWWL.",
        ".LWWWL....LWWWL.",
        ".LLWWWL..LWWWLL.",
        "...LWWWLLWWWL...",
        "...LWWWWWWWWL...",
        "..LWLLWWWWLLWL..",
        "...L..LWWL..L...",
        ".......LL.......",
        "................"]
STAIRS = ["................",
          "PPPP............",
          "DDDP............",
          "DDDPPPPP........",
          "DDDDDDDP........",
          "DDDDDDDPPPP.....",
          "DDDDDDDDDDP.....",
          "DDDDDDDDDDPPPP..",
          "DDDDDDDDDDDDDP..",
          "DDDDDDDDDDDDDPPP",
          "DDDDDDDDDDDDDDDP",
          "KKKKKKKKKKKKKKKK",
          "................",
          "................",
          "................",
          "................"]
MUTED = ["................",
         ".RR..........RR.",
         ".RRR........RRR.",
         "..RRR......RRR..",
         "...RRR....RRR...",
         "....RRR..RRR....",
         ".....RRRRRR.....",
         "......RRRR......",
         "......RRRR......",
         ".....RRRRRR.....",
         "....RRR..RRR....",
         "...RRR....RRR...",
         "..RRR......RRR..",
         ".RRR........RRR.",
         ".RR..........RR.",
         "................"]
MPAL = {"R": (255, 85, 85), "B": (0, 85, 170), "K": (0, 0, 0), "W": (255, 255, 255), "L": (170, 170, 170), "P": (255, 255, 170), "D": (170, 170, 85)}
ic = Image.open(os.path.join(SRC, "icons.png")).convert("RGBA")
def cell(box):
    c = ic.crop(box); out = Image.new("RGBA", (16, 16)); out.paste(c, ((16 - c.width) // 2, (16 - c.height) // 2)); return out
dash = Image.open(os.path.join(SRC, "dashboard.png")).convert("RGBA")
def small(box):
    c = dash.crop(box).resize((16, 16), Image.LANCZOS); px = c.load()
    for y in range(16):
        for x in range(16):
            if px[x, y][3] < 128: px[x, y] = (0, 0, 0, 0)
            else: px[x, y] = px[x, y][:3] + (255,)
    return c
items = Image.open(os.path.join(SRC, "items.png")).convert("RGBA")
def item(idx): return items.crop(((idx % 8) * 16, (idx // 8) * 16, (idx % 8) * 16 + 16, (idx // 8) * 16 + 16))
cells = [pix(FLOPPY, MPAL), pix(QUILL, MPAL), pix(GEAR, MPAL), pix(NOTE, MPAL), pix(SPEAKER, MPAL), cell((16, 0, 30, 13)),
         pix(STAIRS, MPAL), small((64, 0, 96, 32)), cell((98, 0, 114, 16)), cell((114, 0, 126, 11)), pix(MUTED, MPAL),
         cell((16, 13, 30, 27)), item(56)]
sheet = Image.new("RGBA", (16 * len(cells), 16))
def outline(im):                                  # a black edge, so the pictures read on the red buttons
    px = im.load(); out = im.copy(); o = out.load()
    for y in range(16):
        for x in range(16):
            if px[x, y][3] >= 128: continue
            if any(0 <= x + dx < 16 and 0 <= y + dy < 16 and px[x + dx, y + dy][3] >= 128 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                o[x, y] = (0, 0, 0, 255)
    return out
for i, c in enumerate(cells): sheet.paste(outline(c), (16 * i, 0))
total += save(max16(pebbleize(sheet, SPRITES)), "menu_icons.png")   # 0.20: 4 bpp
