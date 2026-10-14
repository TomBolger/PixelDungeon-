#!/usr/bin/env python3
"""Draw a bot snapshot (JSON from deepbot SNAPG/SNAPDEATH) like the watch shows it, plus a side panel.
   python3 snap.py in.json out.png [zoom 1=medium]"""
import json, sys, re, struct
from PIL import Image, ImageDraw, ImageFont
import os
WATCH = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "watch"))
R = os.path.join(WATCH, "resources", "images") + "/"
MAP_W = 32
d = json.load(open(sys.argv[1])); out = sys.argv[2]
REG = ["sewers", "prison", "caves", "city", "halls"][min(4, (d["depth"] - 1) // 5)]
tiles = Image.open(R + f"tiles_{REG}.png").convert("RGBA")
water = Image.open(R + f"water_{REG}.png").convert("RGBA")
items = Image.open(R + "items_02.png").convert("RGBA")
icons = Image.open(R + "icons.png").convert("RGBA")
keys = re.findall(r"RESOURCE_ID_IMAGE_MOB_(\w+)|(\b0\b)", open(os.path.join(WATCH, "src", "c", "mob_res.auto.h")).read().split("{", 1)[1])
mobkeys = [k[0] if k[0] else None for k in keys]
art = open(os.path.join(WATCH, "resources", "data", "mob_art.bin"), "rb").read(); REC = 74
def mob_frame(t):
    if t >= len(mobkeys) or not mobkeys[t]: return None
    fw, fh = art[t * REC], art[t * REC + 1]; f0 = art[t * REC + 2]
    sh = Image.open(R + f"mob_{mobkeys[t].lower()}.png").convert("RGBA")
    return sh.crop((f0 * fw, 0, f0 * fw + fw, fh))
CLS = ["warrior", "mage", "rogue", "huntress"]
hero = Image.open(R + f"hero_{CLS[d['hero']['cls']]}_0.png").convert("RGBA").crop((0, 0, 12, 15))
W, H, T = 200, 228, 16
hp = d["hero"]["pos"]; hx, hy = hp % MAP_W, hp // MAP_W
ox, oy = W // 2 - hx * T - T // 2, (H - 40) // 2 - hy * T - T // 2   # the map area above the toolbar
scr = Image.new("RGBA", (W, H), (0, 0, 0, 255))
def at(c): return ox + (c % MAP_W) * T, oy + (c // MAP_W) * T
vis = d["vis"]
for c, t in enumerate(d["map"]):
    if vis[c] == "0": continue
    x, y = at(c)
    if x < -T or y < -T or x > W or y > H: continue
    if t >= 48: scr.alpha_composite(water.crop(((c % 2) * 16, (c // MAP_W % 2) * 16, (c % 2) * 16 + 16, (c // MAP_W % 2) * 16 + 16)), (x, y)) if 0 <= x and 0 <= y and x + T <= W and y + T <= H else None
    if t != 63:
        tile = tiles.crop(((t % 16) * 16, (t // 16) * 16, (t % 16) * 16 + 16, (t // 16) * 16 + 16))
        tmp = Image.new("RGBA", (W, H)); tmp.paste(tile, (x, y)); scr.alpha_composite(tmp)
SPC = {1: 104, 2: 105, 3: 106, 4: 107, 5: 108}   # (unused: chests etc. fall back to the item sprite)
for h in d["heaps"]:
    x, y = at(h["pos"]); s = h["spr"]; spr = items.crop(((s % 10) * 16, (s // 10) * 16, (s % 10) * 16 + 16, (s // 10) * 16 + 16))
    tmp = Image.new("RGBA", (W, H)); tmp.paste(spr, (x, y)); scr.alpha_composite(tmp)
# fog over remembered-but-unseen cells
fog = Image.new("RGBA", (W, H), (0, 0, 0, 0)); fd = ImageDraw.Draw(fog)
for c in range(len(vis)):
    if vis[c] == "1": x, y = at(c); fd.rectangle([x, y, x + T - 1, y + T - 1], fill=(0, 0, 0, 140))
scr.alpha_composite(fog)
for m in d["mobs"]:
    fr = mob_frame(m["type"]); x, y = at(m["pos"])
    if fr is None: continue
    tmp = Image.new("RGBA", (W, H)); tmp.paste(fr, (x + (T - fr.width) // 2, y + T - fr.height)); scr.alpha_composite(tmp)
    dr = ImageDraw.Draw(scr); w = 14; dr.rectangle([x + 1, y - 3, x + w, y - 2], fill=(200, 0, 0)); dr.rectangle([x + 1, y - 3, x + 1 + (w - 1) * m["hp"] // max(1, m["ht"]), y - 2], fill=(0, 170, 0))
    if m["state"] == 0: dr.text((x + 10, y - 10), "z", fill=(255, 255, 255))
tmp = Image.new("RGBA", (W, H)); hh = hero.transpose(Image.FLIP_LEFT_RIGHT) if d["hero"]["left"] else hero
x, y = at(hp); tmp.paste(hh, (x + 2, y + 1)); scr.alpha_composite(tmp)
ret = icons.crop((67, 0, 83, 16))
if d["targets"]:
    c0 = d["targets"][0][0]; x, y = at(c0); tmp = Image.new("RGBA", (W, H)); tmp.paste(ret, (x, y)); scr.alpha_composite(tmp)
dr = ImageDraw.Draw(scr)
for i, (c, k) in enumerate(d["targets"][1:6], 1):
    x, y = at(c); dr.text((x + 5, y + 2), str(i), fill=(255, 255, 120))
# a plain status strip on top, a toolbar band at the bottom
dr.rectangle([0, 0, W, 13], fill=(40, 40, 40)); hpw = 80 * d["hero"]["hp"] // d["hero"]["ht"]
dr.rectangle([4, 4, 84, 9], fill=(90, 0, 0)); dr.rectangle([4, 4, 4 + hpw, 9], fill=(200, 30, 30))
dr.text((90, 1), f"{d['hero']['hp']}/{d['hero']['ht']}  D{d['depth']}  L{d['hero']['lvl']}", fill=(255, 255, 255))
dr.rectangle([0, H - 30, W, H], fill=(60, 60, 60))
big = scr.resize((W * 2, H * 2), Image.NEAREST)
panel = Image.new("RGBA", (420, H * 2), (24, 24, 28, 255)); pd = ImageDraw.Draw(panel)
try: font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15); fb = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except Exception: font = fb = ImageFont.load_default()
h = d["hero"]; y = 10
def line(t, f=font, col=(230, 230, 230)):
    global y
    import textwrap
    for part in textwrap.wrap(t, 46) or [""]: pd.text((12, y), part, font=f, fill=col); y += 20
line(f"Game {d['game']}  step {d['step']}  floor {d['depth']}  turn {d['turn']}", fb, (255, 220, 120))
line(f"{CLS[h['cls']].title()} level {h['lvl']}, STR {h['str']}, HP {h['hp']}/{h['ht']} -> {d.get('hp_after','?')}")
line(f"Weapon: {h['weapon'] or '-'}"); line(f"Armor: {h['armor'] or '-'}")
y += 6; line("Monsters in view:", fb)
ST = {0: "asleep", 1: "wandering", 2: "hunting", 3: "fleeing", 4: "passive"}
for m in d["mobs"]: line(f"  {m['name']} {m['hp']}/{m['ht']} ({ST.get(m['state'], m['state'])}){' ally' if m['ally'] else ''}")
if not d["mobs"]: line("  none")
y += 6; line("Backpack:", fb); line("  " + ", ".join(d["bag"]) if d["bag"] else "  empty")
y += 6; line("Recent messages:", fb)
for l in d["log"]:
    if l: line("  " + l, font, (180, 180, 200))
y += 6; line("Bot did:", fb, (140, 220, 255)); line("  " + d["did"], fb, (140, 220, 255))
canvas = Image.new("RGBA", (W * 2 + 420, H * 2), (0, 0, 0, 255)); canvas.paste(big, (0, 0)); canvas.paste(panel, (W * 2, 0))
canvas.convert("RGB").save(out)
