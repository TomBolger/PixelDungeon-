"""0.9: tile sheets and water for the prison, caves, city and halls (the sewers' come from convert_art.py).
Writes resources/images/tiles_<region>.png and water_<region>.png; split_sheets.py then cuts the tiles into strips."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)
sys.path.insert(0, os.path.dirname(__file__))
import convert_lib as cl
from PIL import Image
SRC = PD_ASSETS
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "images")
REGIONS = ["sewers", "prison", "caves", "city", "halls"]
for r in range(1, 5):
    t = Image.open(os.path.join(SRC, f"tiles{r}.png")).convert("RGBA").crop((0, 0, 256, 64))
    px = t.load()
    for y in range(t.height):
        for x in range(t.width):
            R, G, B, A = px[x, y]
            if A and r == 2 and 30 <= max(R, G, B) < 60:   # the caves' dark floor (it would snap to black, or speckle): one even grey
                R, G, B = 72, 70, 68; px[x, y] = (R, G, B, A)
            if A and r == 4 and 40 <= max(R, G, B) < 60 and R > G:   # 0.12: the halls' dark red-brown floor, one even maroon (not black)
                R, G, B = 85, 0, 0; px[x, y] = (R, G, B, A)
            if A and G - max(R, B) > 25:              # greenery: lifted a shade like the sewers' grass
                lifted = [min(255, int(v * 1.45)) for v in (R, G, B)]
                px[x, y] = cl.near(tuple(max(0, min(255, (v - 45) * 255 / 165)) for v in lifted)) + (254,)
    cl.pebbleize(t, cl.TILES).save(os.path.join(OUT, f"tiles_{REGIONS[r]}.png"))
    # water: three shades by brightness (deep, mid, light), each the region's colour lifted onto the Pebble palette
    w = Image.open(os.path.join(SRC, f"water{r}.png")).convert("RGBA")
    wp = w.load()
    ls = sorted(sum(wp[x, y][:3]) for y in range(32) for x in range(32))
    q1, q2 = ls[len(ls) * 50 // 100], ls[len(ls) * 88 // 100]
    buckets = [[], [], []]
    for y in range(32):
        for x in range(32):
            v = sum(wp[x, y][:3]); buckets[0 if v <= q1 else 1 if v <= q2 else 2].append(wp[x, y][:3])
    cols = []
    for k, b in enumerate(buckets):
        if not b: b = buckets[k - 1] if k else buckets[1]
        m = [sum(c[i] for c in b) / max(1, len(b)) for i in range(3)]
        lift = 1.6 + 0.5 * k
        cols.append(cl.near(tuple(min(255, v * lift + 10 * k) for v in m)))
    out = Image.new("RGBA", (32, 32))
    op = out.load()
    for y in range(32):
        for x in range(32):
            v = sum(wp[x, y][:3]); op[x, y] = cols[0 if v <= q1 else 1 if v <= q2 else 2] + (255,)
    out.save(os.path.join(OUT, f"water_{REGIONS[r]}.png"))
    print(REGIONS[r], "water", cols)
