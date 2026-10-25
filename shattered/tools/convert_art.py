"""Converts Shattered PD's art to Pebble colours, using the same tuned mapping as the Pixel Dungeon port.
Output: build/art/<same path>.png (one file per picture, numbered like server Assets.java)."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'watch', 'tools'))
from convert_lib import pebbleize, TILES, SPRITES, CHARS
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'spd', 'core', 'src', 'main', 'assets')
OUT = os.path.join(HERE, '..', 'build', 'art')
DIRS = ["effects", "environment", "fonts", "interfaces", "splashes", "sprites"]

PD = os.path.join(HERE, '..', 'common', 'assets')   # Pebble PD's own interface art (pebxel/pd/), already in Pebble colours

def src(p):
    return os.path.join(PD if p.startswith('pebxel/') else SRC, p)

def paths():
    out = ['pebxel/pd/' + f for f in sorted(os.listdir(os.path.join(PD, 'pebxel', 'pd'))) if f.endswith('.png')]
    for d in DIRS:
        for root, _, files in os.walk(os.path.join(SRC, d)):
            for f in files:
                if f.endswith('.png') or f.endswith('.jpg'):
                    out.append(os.path.relpath(os.path.join(root, f), SRC).replace(os.sep, '/'))
    return out[:0] + sorted(x for x in out if not x.startswith('pebxel/')) + [x for x in out if x.startswith('pebxel/')]

def levels(p):
    if p.startswith('environment/'): return TILES
    if p.startswith('sprites/'): return CHARS
    return SPRITES

GREY = 44   # colours this close to grey become Pebble greys (Shattered's interface greys are tinted green or
            # beige; the nearest Pebble colours for those are olive, green and pink)

def greys(im, lv):
    lo, hi = lv
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a and max(r, g, b) - min(r, g, b) < GREY:
                v = max(0, min(255, ((0.3 * r + 0.5 * g + 0.2 * b) - lo) * 255 / (hi - lo)))
                q = min((0, 85, 170, 255), key=lambda c: abs(c - v))
                px[x, y] = (q, q, q, 254 if a == 255 else a)   # 254: pebbleize keeps it as is
    return im

# ---- Pebble PD's item conversion (its convert_art.py: pebbleize(sheet, CHARS, hue_guard=True)), used as is ----
import colorsys
PAL = [(r, g, b) for r in (0, 85, 170, 255) for g in (0, 85, 170, 255) for b in (0, 85, 170, 255)]
def _near(c):
    return min(PAL, key=lambda p: 2 * (p[0] - c[0]) ** 2 + 4 * (p[1] - c[1]) ** 2 + 3 * (p[2] - c[2]) ** 2)
def _hue(c):
    h, _, s = colorsys.rgb_to_hls(*(v / 255 for v in c)); return h * 360, s
_PAL_HUE = {p: _hue(p)[0] for p in PAL}
def _near_hue(c, lim=35):
    h0, s0 = _hue(c)
    if s0 < 0.12: return _near(c)
    return min((p for p in PAL if p[0] == p[1] == p[2] or min(abs(_PAL_HUE[p] - h0), 360 - abs(_PAL_HUE[p] - h0)) <= lim),
               key=lambda p: 2 * (p[0] - c[0]) ** 2 + 4 * (p[1] - c[1]) ** 2 + 3 * (p[2] - c[2]) ** 2)
def pd_items(img):
    lo, hi = CHARS
    img = img.convert('RGBA'); px = img.load(); cache = {}
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = px[x, y]
            if a < 100: px[x, y] = (0, 0, 0, 0); continue
            k = (r, g, b)
            if k not in cache:
                c = [max(0.0, min(1.0, (v - lo) / (hi - lo))) for v in (r, g, b)]
                h, l, sat = colorsys.rgb_to_hls(*c)
                if sat > 0.3: sat = min(1.0, sat * 1.3)
                cache[k] = _near_hue(tuple(v * 255 for v in colorsys.hls_to_rgb(h, l, sat)))
            px[x, y] = cache[k] + (255,)
    return img
PD_ITEMS = {'sprites/items.png', 'sprites/item_icons.png'}

# An unknown potion is known only by its colour. As Pebble PD did for its potions (GEM_FIX), each liquid's tones are
# picked by hand so they stay apart on the panel: golden's body had snapped to amber's orange, silver's to a
# purple-blue like indigo's, ivory's to silver's grey; indigo and azure, magenta and crimson ran together.
# Shattered's potions (items.png row 23, and the exotic ones in row 24, same liquids): (main, light, dark) tones.
POTION_FIX = {
    1: {(217, 79, 0): (170, 85, 0)},                                                          # amber: orange over brown
    2: {(204, 170, 0): (255, 255, 0)},                                                        # golden: yellow
    5: {(55, 72, 182): (0, 0, 170)},                                                          # azure: blue over navy
    6: {(88, 34, 181): (85, 0, 170), (111, 51, 218): (85, 0, 170), (65, 17, 144): (85, 0, 85)},   # indigo: deep violet
    7: {(173, 20, 135): (255, 0, 255), (214, 30, 154): (255, 0, 255)},                        # magenta: pink-purple
    8: {(98, 62, 35): (85, 0, 0)},                                                            # bistre: brown over maroon
    10: {(100, 115, 129): (170, 170, 170)},                                                   # silver: grey
    11: {(187, 167, 134): (255, 255, 255), (217, 200, 166): (255, 255, 255), (158, 134, 102): (170, 170, 170)},   # ivory: white
}

def potion_fix(src, out):
    sp, op = src.load(), out.load()
    for row in (22, 23):
        for cell, m in POTION_FIX.items():
            for y in range(row * 16, row * 16 + 16):
                for x in range(cell * 16, cell * 16 + 16):
                    if sp[x, y][3] >= 100 and sp[x, y][:3] in m: op[x, y] = m[sp[x, y][:3]] + (255,)

CUT = {'interfaces': 128, 'sprites': 100, 'effects/spell_icons.png': 100, 'effects/text_icons.png': 100}

def frame_widths():
    """Monster sheets and their frame widths, from Shattered's sprite classes (new TextureFilm(texture, w, ...)):
    those get Pebble PD's black outline. (Heroes, items and NPC-free sheets don't, as in Pebble PD.)"""
    import re, glob
    j = os.path.join(HERE, '..', 'spd', 'core', 'src', 'main', 'java', 'com', 'shatteredpixel', 'shatteredpixeldungeon')
    consts = dict(re.findall(r'public static final String (\w+)\s*=\s*"(sprites/[^"]+)"', open(os.path.join(j, 'Assets.java')).read()))
    out = {}
    for f in glob.glob(os.path.join(j, 'sprites', '*.java')):
        s = open(f).read()
        films = re.findall(r'new TextureFilm\(\s*texture\s*,\s*(\d+)\s*,\s*(\d*)', s)   # (a height worked out at run time: none)
        for t in re.findall(r'texture\(\s*Assets\.Sprites\.(\w+)\s*\)', s):
            if t in consts and films:
                if films[0][1]: FRAME_H.setdefault(consts[t], int(films[0][1]))
                if consts[t] not in NO_OUTLINE: out[consts[t]] = int(films[0][0])
    for h in ('warrior', 'mage', 'rogue', 'huntress', 'duelist', 'cleric'): FRAME_H['sprites/%s.png' % h] = 15   # HeroSprite: a row per tier
    return out

NO_OUTLINE = {'sprites/items.png', 'sprites/item_icons.png', 'sprites/avatars.png', 'sprites/amulet.png', 'sprites/pet.png',
              'sprites/warrior.png', 'sprites/mage.png', 'sprites/rogue.png', 'sprites/huntress.png', 'sprites/duelist.png', 'sprites/cleric.png'}

def outline(im, fw, col=(0, 0, 0, 255)):
    """Pebble PD's monster outline (convert_lib.outlined): every see-through pixel touching (4 ways) a solid one in
    the same frame column turns black. Goo, being black, gets grey."""
    src = im.copy().load(); px = im.load(); w, h = im.size
    for y in range(h):
        for x in range(w):
            if src[x, y][3]: continue
            f0 = x // fw * fw
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if f0 <= nx < min(w, f0 + fw) and 0 <= ny < h and src[nx, ny][3]:
                    px[x, y] = col; break

FRAME_W = {}
FRAME_H = {}   # creature sheets' frame heights: their colours are folded a row (one creature, or one armour tier) at a time

# ---- Pebble PD's fold16 (its convert_art.py, 0.60): at most 15 colours per strip, dropping first the colour whose loss
# looks least on the panel. Pebble PD folds every creature's strip and hero tier; Shattered's richer shading kept
# shades a pixel or two strong (speckle at the watch's size).
PAL = [(r, g, b) for r in (0, 85, 170, 255) for g in (0, 85, 170, 255) for b in (0, 85, 170, 255)]
PANEL = dict(zip(PAL, [((v >> 16) & 255, (v >> 8) & 255, v & 255) for v in (
    0x000000, 0x001E41, 0x004387, 0x0068CA, 0x2B4A2C, 0x27514F, 0x16638D, 0x007DCE, 0x5E9860, 0x5C9B72, 0x57A5A2,
    0x4CB4DB, 0x8EE391, 0x8EE69E, 0x8AEBC0, 0x84F5F1, 0x4A161B, 0x482748, 0x40488A, 0x2F6BCC, 0x564E36, 0x545454,
    0x4F6790, 0x4180D0, 0x759A64, 0x759D76, 0x71A6A4, 0x69B5DD, 0x9EE594, 0x9DE7A0, 0x9BECC2, 0x95F6F2, 0x99353F,
    0x983E5A, 0x955694, 0x8F74D2, 0x9D5B4D, 0x9D6064, 0x9A7099, 0x9587D5, 0xAFA072, 0xAEA382, 0xABABAB, 0xA7BAE2,
    0xC9E89D, 0xC9EAA7, 0xC7F0C8, 0xC3F9F7, 0xE35462, 0xE25874, 0xE16AA3, 0xDE83DC, 0xE66E6B, 0xE6727C, 0xE37FA7,
    0xE194DF, 0xF1AA86, 0xF1AD93, 0xEFB5B8, 0xECC3EB, 0xFFEEAB, 0xFFF1B5, 0xFFF6D3, 0xFFFFFF)]))
def panel_lab(c):
    def lin(u): u /= 255; return ((u + 0.055) / 1.055) ** 2.4 if u > 0.04045 else u / 12.92
    r, g, b = (lin(v) for v in PANEL[tuple(min(3, (v + 42) // 85) * 85 for v in c)])   # (a source pixel at alpha 254 is kept unsnapped)
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    X, Y, Z = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.9505, 0.2126 * r + 0.7152 * g + 0.0722 * b, (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.089
    return 116 * f(Y) - 16, 500 * (f(X) - f(Y)), 200 * (f(Y) - f(Z))
def fold16(img, box):
    x0, y0, x1, y1 = box; px = img.load(); counts = {}
    for y in range(y0, y1):
        for x in range(x0, x1):
            if px[x, y][3]: counts[px[x, y][:3]] = counts.get(px[x, y][:3], 0) + 1
    if len(counts) <= 15: return 0
    lab = {c: panel_lab(c) for c in counts}
    d = lambda a, b: sum((u - v) ** 2 for u, v in zip(lab[a], lab[b])) ** 0.5
    orig, remap = dict(counts), {}
    while len(counts) > 15:
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
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b, a = px[x, y]
            if a and (r, g, b) in remap: px[x, y] = remap[(r, g, b)] + (a,); n += 1
    return n

def spread_greys(src, out, cell):
    """Buff icons: two greys of one icon mustn't become the same Pebble grey (the warrior's armour buff turned into a
    plain grey box): per icon, its greys (darkest to lightest) go to distinct Pebble greys, as near as they can."""
    import itertools
    sp, op = src.load(), out.load()
    G = (0, 85, 170, 255)
    for cy in range(0, src.height, cell):
        for cx in range(0, src.width, cell):
            greys = {}
            for y in range(cy, min(src.height, cy + cell)):
                for x in range(cx, min(src.width, cx + cell)):
                    r, g, b, a = sp[x, y]
                    if a >= 128 and max(r, g, b) - min(r, g, b) < GREY:
                        greys.setdefault((r, g, b), []).append((x, y))
            if len(greys) < 2: continue
            levels = sorted(greys, key=lambda c: 0.3 * c[0] + 0.5 * c[1] + 0.2 * c[2])
            lum = [0.3 * c[0] + 0.5 * c[1] + 0.2 * c[2] for c in levels]
            k = len(levels)
            if k > 4:   # too many: those closest together share
                best = None
            # each grey to a Pebble grey, keeping their order and as many apart as there are greys to go round
            best, bestCost = None, 1e9
            for combo in itertools.combinations_with_replacement(range(4), k):
                distinct = len(set(combo))
                if distinct < min(k, 4): continue
                cost = sum(abs(G[t] - l) for t, l in zip(combo, lum))
                if cost < bestCost: best, bestCost = combo, cost
            for c, t in zip(levels, best):
                for (x, y) in greys[c]: op[x, y] = (G[t], G[t], G[t], 255)

def simple_frames(im):
    """Pebble PD's window frames in place of Shattered's (its bevelled, tinted frames turn to noise in Pebble's
    few colours): each region is Pebble PD's frame, nine-sliced to Shattered's size."""
    pd = Image.open(os.path.join(PD, 'pebxel', 'pd', 'chrome.png')).convert('RGBA').load()
    px = im.load()
    def nine(sx, sy, sw, sh, sm, dx, dy, dw, dh, dm):
        def pick(d, size, m, ssize, sm):
            if d < m: return d * sm // m if m else 0                      # the near corner/edge band
            if d >= size - m: return ssize - (size - d) * sm // m if m else ssize - 1
            return sm + (d - m) * (ssize - 2 * sm) // max(1, size - 2 * m)   # the middle, stretched
        for y in range(dh):
            for x in range(dw):
                px[dx + x, dy + y] = pd[sx + pick(x, dw, dm, sw, sm), sy + pick(y, dh, dm, sh, sm)]
    nine(0, 0, 22, 22, 7, 0, 0, 20, 20, 6)      # window: light frame, black body (Pebble PD's WELL)
    nine(0, 0, 22, 22, 7, 64, 0, 20, 20, 6)     # window with tabs: black body too (small text reads best on black)
    nine(0, 0, 22, 22, 7, 32, 32, 32, 32, 5)    # the story scroll: a plain window (its text is shown white)
    nine(0, 0, 22, 22, 7, 86, 0, 22, 22, 7)     # silver window: as the window
    def rings(x0, y0, w, h, cs):                # small frames: plain rings of grey
        for y in range(h):
            for x in range(w):
                c = cs[min(min(x, y, w - 1 - x, h - 1 - y), len(cs) - 1)]
                px[x0 + x, y0 + y] = (c, c, c, 255)
    rings(20, 0, 9, 9, [0, 170, 85])            # toast
    rings(20, 9, 9, 9, [85, 0])                 # see-through toast / grey-button tag: solid black, grey edge
    rings(29, 9, 9, 9, [170, 0])                # the heavier one
    nine(58, 0, 6, 6, 2, 38, 0, 6, 6, 2)        # red button

# ---- Pebble PD's dungeon tile fixes (its convert_regions.py), by region ----
def region(p):
    n = p.rsplit('/', 1)[-1]
    for r in ('sewer', 'prison', 'caves', 'city', 'halls'):
        if n.startswith(('tiles_' + r, r)): return r
    return None

CAVE_FLOOR = {(44, 41, 39), (41, 36, 35), (48, 44, 42)}
HALL_FLOOR = {(46, 31, 31), (57, 31, 31), (57, 41, 41)}

def floors_lifted(p, im):
    """Pebble PD's dark floors (before the palette): the caves' near-black stone floor becomes one even grey and the
    halls' dark red-brown floor one even maroon. Both snapped to black, the same as the unexplored dark around them.
    Only the floor's own shades change, so holes, pits and shadows stay black."""
    r = region(p)
    if r not in ('caves', 'halls'): return im
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            R, G, B, A = px[x, y]
            if A < 255: continue
            if r == 'caves' and (R, G, B) in CAVE_FLOOR: px[x, y] = (85, 85, 85, 254)
            if r == 'halls' and (R, G, B) in HALL_FLOOR and 'custom' not in p: px[x, y] = (85, 0, 0, 254)   # (not Yog's dark pit)
    return im

def cave_grit(p, src, out):
    """Pebble PD's cave floor grit: the floor's darkest shade, where it clusters, is black on every other pixel, so the
    lifted floor reads as rough stone, not a flat grey (the same rule for every cell, so secret traps still match)."""
    if region(p) != 'caves': return
    sp, op = src.load(), out.load()
    dark = (41, 36, 35, 255); floor = {(44, 41, 39, 255), dark, (48, 44, 42, 255)}
    for cy in range(0, src.height - 15, 16):
        for cx in range(0, src.width - 15, 16):
            cell = [[sp[cx + x, cy + y] for x in range(16)] for y in range(16)]
            if sum(c in floor for row in cell for c in row) < 160: continue   # floor cells only
            for y in range(16):
                for x in range(16):
                    if cell[y][x] != dark or (x + y) % 2: continue
                    n = sum(cell[(y + dy) % 16][(x + dx) % 16] == dark for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    if n >= 2: op[cx + x, cy + y] = (0, 0, 0, 255)

def warm_stone(p, src, out):
    """Pebble PD's tile_rules.warm_stone: light tan and beige stone (the prison's and city's walls, door frames, stairs)
    snapped to pink (255,170,170), since the palette has no cream between pale yellow and pink. Khaki keeps the hue."""
    if region(p) in (None, 'sewer'): return
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            r, g, b, a = sp[x, y]
            if a == 255 and op[x, y][:3] == (255, 170, 170) and r - b >= 15 and g - b >= 6: op[x, y] = (170, 170, 85, 255)

# The Duelist's brown skin is low in colour, so the interface-grey rule (and the plain snap) turned her face grey.
# Picked by hand as Pebble PD picked its potion colours: light tones rose-brown, the middle brown, shadows maroon.
SKIN = {(115, 78, 77): (170, 85, 85), (108, 69, 64): (170, 85, 85), (95, 58, 52): (170, 85, 0),
        (53, 28, 21): (85, 0, 0), (44, 27, 19): (85, 0, 0)}
SKIN_SHEETS = {'sprites/duelist.png', 'sprites/avatars.png'}

def skin(p, im):
    if p not in SKIN_SHEETS: return im
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if a >= 100 and (r, g, b) in SKIN: px[x, y] = SKIN[(r, g, b)] + (254,)
    return im

# Pebble PD's per-creature fixes (its mobs.py RECOLOR), on the same art in Shattered's sheets: colour, or (colour, alpha)
_K, _G, _S, _W = (0, 0, 0), (85, 85, 85), (170, 170, 170), (255, 255, 255)
RECOLOR = {
    # Goo: a black body with a white shine (its 85-grey body was the sewer floor's own colour); its edge is grey
    'sprites/goo.png': {(0, 0, 0): _K, (6, 6, 6): _K, (38, 38, 38): _K, (64, 64, 64): _G, (128, 128, 128): _W},
    # the Sad Ghost's 40% white body snapped to white, one flat sheet that melts into the sewers' white walls
    'sprites/ghost.png': {((255, 255, 255), 102): _S},
    # the wraith: a 50% black body (it was all black, under a black outline: invisible on dark ground) under its
    # black hood; Shattered's tormented spirit, its red twin, the same way in maroon
    'sprites/wraith.png': {((0, 0, 0), 128): _G, ((0, 0, 0), 127): _G, ((51, 0, 0), 128): (85, 0, 0), ((51, 0, 0), 127): (85, 0, 0)},
    # the golem's lit side snapped to 85-grey, the city floor, so only its black half showed
    'sprites/golem.png': {(102, 90, 87): _S},
    # the bat's dark red wing membranes snapped to black with the rest of it
    'sprites/bat.png': {(51, 0, 8): (85, 0, 0)},
    # Yog's blood vessels are pink a shade off white: they clipped to white and the eyeball went blank
    'sprites/yog.png': {c: (255, 170, 170) for c in ((255, 229, 229), (255, 215, 215), (255, 204, 204), (235, 211, 211), (214, 192, 192))},
    # the bandit (the thief in a purple hood): its darkest purple snapped to maroon and the light one to raspberry
    'sprites/thief.png': {(58, 6, 42): (85, 0, 85), (95, 10, 77): (85, 0, 85), (100, 42, 82): (85, 0, 85), (122, 40, 104): (170, 0, 170)},
}

def plant_edges(p, im):
    """Pebble PD's plants: their 40% black shadow (a dotted checker on the watch) is a solid black edge, so they stand
    off grass and floor without see-through dots (terrain_features.png, the plants' row)."""
    if p != 'environment/terrain_features.png': return im
    px = im.load()
    for y in range(112, 128):
        for x in range(im.width):
            if px[x, y][:3] == (0, 0, 0) and 90 <= px[x, y][3] < 255: px[x, y] = (0, 0, 0, 255)
    return im

def recolor(p, im):
    """Pebble PD's fixes: matched pixels become that Pebble colour, solid (alpha 254: kept as is by the mapping)."""
    rc = RECOLOR.get(p)
    if not rc and p != 'sprites/piranha.png': return im
    px = im.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if not a: continue
            if p == 'sprites/piranha.png':   # its idle frames are a 30% black shadow under the water: solid grey
                if a < 128: px[x, y] = _G + (254,)
                continue
            to = rc.get(((r, g, b), a), rc.get((r, g, b)) if a >= 100 else None)
            if to is not None: px[x, y] = to + (254,)
    return im

def pd_water(p, im):
    """Pebble PD's water (convert_art.py, convert_regions.py): a few shades by brightness, each region's own colours.
    Snapped pixel by pixel, the prison's water came out one flat grey (it read as metal), the caves' black (it vanished
    into the dark), the city's as black blots on maroon."""
    from convert_lib import near
    k = int(p[-5]); w = im.convert('RGBA'); wp = w.load(); W, H = w.size
    v = lambda x, y: sum(wp[x, y][:3])
    ls = sorted(v(x, y) for y in range(H) for x in range(W))
    q = lambda f: ls[min(len(ls) - 1, int(len(ls) * f))]
    out = Image.new('RGBA', (W, H)); op = out.load()
    if k == 0:   # sewers: deep teal, jade, soft teal
        q1, q2 = q(0.50), q(0.88)
        for y in range(H):
            for x in range(W): op[x, y] = ((0, 85, 85) if v(x, y) < q1 else (0, 170, 85) if v(x, y) < q2 else (85, 170, 170)) + (255,)
    elif k in (1, 3):   # prison: deep teal, its brightest ripples lighter; city: deep maroon with rose-brown ripples
        t1 = q(0.85); lo, hi = ((0, 85, 85), (85, 170, 170)) if k == 1 else ((85, 0, 0), (170, 85, 85))
        for y in range(H):
            for x in range(W): op[x, y] = (hi if v(x, y) > t1 else lo) + (255,)
    elif k == 4:   # halls: five heat levels, a dark crust to bubbling yellow
        t1, t2, t3 = q(0.35), q(0.68), q(0.82)
        cols = [(170, 0, 0), (255, 0, 0), (255, 85, 0), (255, 255, 0), (255, 255, 170)]
        for y in range(H):
            for x in range(W):
                c = v(x, y); i = 0 if c <= t1 else 1 if c <= t2 else 2 if c <= t3 else 3
                if i == 3 and all(v((x + dx) % W, (y + dy) % H) >= t3 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): i = 4
                op[x, y] = cols[i] + (255,)
    else:   # caves: deep, mid and light by brightness, the water's colour lifted onto the palette
        q1, q2 = q(0.50), q(0.88)
        b = [[], [], []]
        for y in range(H):
            for x in range(W): b[0 if v(x, y) <= q1 else 1 if v(x, y) <= q2 else 2].append(wp[x, y][:3])
        cols = []
        for i, bb in enumerate(b):
            if not bb: bb = b[i - 1] if i else b[1]
            m = [sum(c[j] for c in bb) / len(bb) for j in range(3)]
            cols.append(near(tuple(min(255, c * (1.6 + 0.5 * i) + 10 * i) for c in m)))
        for y in range(H):
            for x in range(W): op[x, y] = cols[0 if v(x, y) <= q1 else 1 if v(x, y) <= q2 else 2] + (255,)
    return out

if __name__ == '__main__':
    FRAME_W.update(frame_widths())
    ps = paths()
    for p in ps:
        dst = os.path.join(OUT, p.rsplit('.', 1)[0] + '.png')
        if '-f' not in sys.argv and os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(src(p)): continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im = Image.open(src(p)).convert('RGBA')
        if p.startswith('pebxel/'): im.save(dst); continue   # already converted
        im = plant_edges(p, recolor(p, im))
        if p.startswith('environment/water'):
            pd_water(p, im).save(dst); continue
        if p in PD_ITEMS:   # exactly as Pebble PD did its items (hue kept, no greying)
            out = pd_items(im.copy())
            if p == 'sprites/items.png': potion_fix(im, out)
            out.save(dst); continue
        pre = floors_lifted(p, im.copy()) if p.startswith('environment/') else skin(p, im.copy())
        out = pebbleize(greys(pre, levels(p)) if not p.startswith(('environment/', 'splashes/')) else pre, levels(p), alpha_cut=1)
        if p.startswith('environment/'): cave_grit(p, im, out); warm_stone(p, im, out)
        a, o = im.load(), out.load()
        cut = CUT.get(p.split('/')[0], CUT.get(p))
        if p.startswith('environment/tiles_'): cut = 100   # Pebble PD's tiles: the water's foam edge solid, faint shadows gone (both were dotted checkers)
        if cut:
            # Pebble PD's rule: a pixel is there or it isn't (see-through edges and glows dither into a grey fuzz)
            for y in range(im.height):
                for x in range(im.width):
                    al = a[x, y][3]
                    o[x, y] = o[x, y][:3] + ((255,) if al >= cut else (0,))
        else:
            # keep see-through pixels (shadows, fog, gas) as Pebble's four alpha levels
            for y in range(im.height):
                for x in range(im.width):
                    al = a[x, y][3]
                    if 0 < al < 255:
                        q = min(3, (al + 42) // 85)
                        o[x, y] = o[x, y][:3] + ((0, 85, 170, 255)[q],)
        if p in FRAME_H:
            for y0 in range(0, out.height, FRAME_H[p]): fold16(out, (0, y0, out.width, min(out.height, y0 + FRAME_H[p])))
        if p in FRAME_W: outline(out, FRAME_W[p], (85, 85, 85, 255) if p == 'sprites/goo.png' else (0, 0, 0, 255))
        if p == 'interfaces/chrome.png': simple_frames(out)
        if p in ('interfaces/buffs.png', 'interfaces/large_buffs.png'): spread_greys(im, out, 7 if 'large' not in p else 16)
        out.save(dst)
    print(len(ps), 'pictures converted')
