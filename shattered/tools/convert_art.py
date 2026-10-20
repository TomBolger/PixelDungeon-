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
    nine(64, 0, 22, 22, 7, 64, 0, 20, 20, 6)    # window with tabs: grey body
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

if __name__ == '__main__':
    ps = paths()
    for p in ps:
        dst = os.path.join(OUT, p.rsplit('.', 1)[0] + '.png')
        if '-f' not in sys.argv and os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(src(p)): continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im = Image.open(src(p)).convert('RGBA')
        if p.startswith('pebxel/'): im.save(dst); continue   # already converted
        out = pebbleize(greys(im.copy(), levels(p)) if not p.startswith(('environment/', 'splashes/')) else im.copy(), levels(p), alpha_cut=1)
        # keep see-through pixels (shadows, glows) as Pebble's four alpha levels
        a, o = im.load(), out.load()
        for y in range(im.height):
            for x in range(im.width):
                al = a[x, y][3]
                if 0 < al < 255:
                    q = min(3, (al + 42) // 85)
                    o[x, y] = o[x, y][:3] + ((0, 85, 170, 255)[q],)
        if p == 'interfaces/chrome.png': simple_frames(out)
        out.save(dst)
    print(len(ps), 'pictures converted')
