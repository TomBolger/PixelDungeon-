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

if __name__ == '__main__':
    ps = paths()
    for p in ps:
        dst = os.path.join(OUT, p.rsplit('.', 1)[0] + '.png')
        if '-f' not in sys.argv and os.path.exists(dst) and os.path.getmtime(dst) > os.path.getmtime(os.path.join(SRC, p)): continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im = Image.open(os.path.join(SRC, p)).convert('RGBA')
        out = pebbleize(im.copy(), levels(p), alpha_cut=1)
        # keep see-through pixels (shadows, glows) as Pebble's four alpha levels
        a, o = im.load(), out.load()
        for y in range(im.height):
            for x in range(im.width):
                al = a[x, y][3]
                if 0 < al < 255:
                    q = min(3, (al + 42) // 85)
                    o[x, y] = o[x, y][:3] + ((0, 85, 170, 255)[q],)
        out.save(dst)
    print(len(ps), 'pictures converted')
