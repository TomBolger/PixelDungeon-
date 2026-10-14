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
    tp = cl.pebbleize(t, cl.TILES)
    if r == 2:   # 0.56 (owner: "the DM-300 room is just flat grey"): the caves' floor gets grit back. PD's floor is three close
        # shades; the darkest, where it clusters, becomes black on every other pixel (a checker), so the stone reads rough, not flat
        src = Image.open(os.path.join(SRC, f"tiles{r}.png")).convert("RGB"); sp = src.load(); tpp = tp.load()
        cellpx = lambda i: src.crop(((i % 16) * 16, (i // 16) * 16, (i % 16) * 16 + 16, (i // 16) * 16 + 16)).tobytes()
        floors = [i for i in range(64) if cellpx(i) == cellpx(1)] + [24]   # EMPTY, the secret traps drawn as EMPTY (they must match it
        for cell in floors:                            # exactly, or they give themselves away), and EMPTY_DECO (its pebbles stay)
            ox, oy = (cell % 16) * 16, (cell // 16) * 16
            V = [[max(sp[ox + x, oy + y]) for x in range(16)] for y in range(16)]
            for y in range(16):
                for x in range(16):
                    v = V[y][x]
                    if not 30 <= v < 60: continue
                    n = sum(V[(y + dy) % 16][(x + dx) % 16] == v for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                    if v == min(min(row) for row in V) and n >= 2 and (x + y) % 2 == 0: tpp[ox + x, oy + y] = (0, 0, 0, 255)
        ox, oy = 14 * 16, 0                            # EMPTY_SP (shops and other special rooms): PD's bevelled slabs and rivets,
        for y in range(16):                            # not one flat grey: the dark seams and rivets black, the lit edges light grey
            for x in range(16):
                v = max(sp[ox + x, oy + y]); tpp[ox + x, oy + y] = ((0, 0, 0) if v < 58 else (170, 170, 170) if v >= 72 else (85, 85, 85)) + (255,)
    if r == 3:   # 0.58 (owner: "what are these grey squares?"): the King's hall pedestals were one flat grey block, and the
        # decorated floor's darker diamond snapped to a solid black hole. The pedestal gets its bricks back (light bricks, dark
        # mortar, the front band kept with its gold studs); the diamond becomes a checker, darker floor rather than a pit
        src = Image.open(os.path.join(SRC, f"tiles{r}.png")).convert("RGBA"); sp = src.load(); tpp = tp.load()
        ox, oy = 11 * 16, 0                            # PEDESTAL
        for y in range(16):
            for x in range(16):
                R, G, B, A = sp[ox + x, oy + y]
                if not A: continue
                if B > R + 10: tpp[ox + x, oy + y] = ((170, 170, 170) if max(R, G, B) >= 0x58 else (85, 85, 85)) + (255,)
                elif max(R, G, B) < 0x60: tpp[ox + x, oy + y] = ((85, 85, 85) if max(R, G, B) >= 0x48 else (0, 0, 0)) + (255,)
        ox, oy = (24 % 16) * 16, (24 // 16) * 16       # EMPTY_DECO: where it differs from EMPTY (cell 1) is the diamond
        for y in range(16):
            for x in range(16):
                if sp[ox + x, oy + y] == sp[16 + x, y]: continue
                if max(sp[ox + x, oy + y][:3]) <= 0x43: tpp[ox + x, oy + y] = ((0, 0, 0) if (x + y) % 2 == 0 else (85, 85, 85)) + (255,)
    tp.save(os.path.join(OUT, f"tiles_{REGIONS[r]}.png"))
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
    if r == 1:   # 0.59 (owner: "that doesn't look like water at all"): lifted onto the palette, PD's dark slate-teal water came out
        # as greys and read as metal. Deep teal with its brightest ripples (the top 15%) in a lighter teal, as close as the palette gets
        ls = sorted(sum(wp[x, y][:3]) for y in range(32) for x in range(32)); t1 = ls[len(ls) * 85 // 100]
        for y in range(32):
            for x in range(32): op[x, y] = ((85, 170, 170) if sum(wp[x, y][:3]) > t1 else (0, 85, 85)) + (255,)
        out.save(os.path.join(OUT, f"water_{REGIONS[r]}.png")); print(REGIONS[r], "water: teal"); continue
    if r == 4:   # 0.56 (owner: "the lava is too featureless"): five heat levels, a dark crust to bubbling yellow
        ls = sorted(sum(wp[x, y][:3]) for y in range(32) for x in range(32))
        t1, t2, t3 = (ls[int(len(ls) * p)] for p in (0.35, 0.68, 0.82))
        cols = [(170, 0, 0), (255, 0, 0), (255, 85, 0), (255, 255, 0), (255, 255, 170)]
        for y in range(32):
            for x in range(32):
                v = sum(wp[x, y][:3]); k = 0 if v <= t1 else 1 if v <= t2 else 2 if v <= t3 else 3
                if k == 3 and all(sum(wp[(x + dx) % 32, (y + dy) % 32][:3]) >= t3 for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))): k = 4   # a bubble's hot core
                op[x, y] = cols[k] + (255,)
        out.save(os.path.join(OUT, f"water_{REGIONS[r]}.png")); print(REGIONS[r], "lava", cols); continue
    for y in range(32):
        for x in range(32):
            v = sum(wp[x, y][:3]); op[x, y] = cols[0 if v <= q1 else 1 if v <= q2 else 2] + (255,)
    out.save(os.path.join(OUT, f"water_{REGIONS[r]}.png"))
    print(REGIONS[r], "water", cols)
