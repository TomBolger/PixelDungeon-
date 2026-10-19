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

def paths():
    out = []
    for d in DIRS:
        for root, _, files in os.walk(os.path.join(SRC, d)):
            for f in files:
                if f.endswith('.png') or f.endswith('.jpg'):
                    out.append(os.path.relpath(os.path.join(root, f), SRC).replace(os.sep, '/'))
    return sorted(out)

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
    """Plainer window frames for the small screen: a black edge, a light band, a dark grey inside
    (Shattered's bevelled, riveted frames turn to noise in Pebble's few colours)."""
    px = im.load()
    def frame(x0, y0, w, h, rings):
        for y in range(h):
            for x in range(w):
                d = min(x, y, w - 1 - x, h - 1 - y)
                c = rings[d] if d < len(rings) else rings[-1]
                px[x0 + x, y0 + y] = (c, c, c, 255)
    K, D, L, W = 0, 85, 170, 255
    frame(0, 0, 20, 20, [K, L, L, K, D])      # window
    frame(64, 0, 20, 20, [K, L, L, K, D])     # window with tabs
    frame(86, 0, 22, 22, [K, W, W, K, D])     # silver window (rankings, special)
    frame(20, 0, 9, 9, [K, L, D])             # toast
    frame(20, 9, 9, 9, [D, K])                # see-through toast / grey button: solid black, grey edge
    frame(29, 9, 9, 9, [L, K])                # heavier one

if __name__ == '__main__':
    ps = paths()
    for p in ps:
        dst = os.path.join(OUT, p.rsplit('.', 1)[0] + '.png')
        if '-f' not in sys.argv and os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(os.path.join(SRC, p)): continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im = Image.open(os.path.join(SRC, p)).convert('RGBA')
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
