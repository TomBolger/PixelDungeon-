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
        films = re.findall(r'new TextureFilm\(\s*texture\s*,\s*(\d+)\s*,', s)
        for t in re.findall(r'texture\(\s*Assets\.Sprites\.(\w+)\s*\)', s):
            if t in consts and films and consts[t] not in NO_OUTLINE: out[consts[t]] = int(films[0])
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

if __name__ == '__main__':
    FRAME_W.update(frame_widths())
    ps = paths()
    for p in ps:
        dst = os.path.join(OUT, p.rsplit('.', 1)[0] + '.png')
        if '-f' not in sys.argv and os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(src(p)): continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im = Image.open(src(p)).convert('RGBA')
        if p.startswith('pebxel/'): im.save(dst); continue   # already converted
        if p in PD_ITEMS:   # exactly as Pebble PD did its items (hue kept, no greying)
            pd_items(im.copy()).save(dst); continue
        pre = floors_lifted(p, im.copy()) if p.startswith('environment/') else im.copy()
        out = pebbleize(greys(pre, levels(p)) if not p.startswith(('environment/', 'splashes/')) else pre, levels(p), alpha_cut=1)
        if p.startswith('environment/'): cave_grit(p, im, out)
        a, o = im.load(), out.load()
        cut = CUT.get(p.split('/')[0], CUT.get(p))
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
        if p in FRAME_W: outline(out, FRAME_W[p], (85, 85, 85, 255) if p == 'sprites/goo.png' else (0, 0, 0, 255))
        if p == 'interfaces/chrome.png': simple_frames(out)
        if p in ('interfaces/buffs.png', 'interfaces/large_buffs.png'): spread_greys(im, out, 7 if 'large' not in p else 16)
        out.save(dst)
    print(len(ps), 'pictures converted')
