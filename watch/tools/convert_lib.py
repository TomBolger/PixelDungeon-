"""Shared Pebble art helpers (from convert_art.py)."""
import os, colorsys
from PIL import Image
PAL = [(r, g, b) for r in (0, 85, 170, 255) for g in (0, 85, 170, 255) for b in (0, 85, 170, 255)]
TILES, SPRITES, CHARS = (40, 205), (20, 230), (19, 230)
_cache = {}

def near(c):
    return min(PAL, key=lambda p: 2 * (p[0] - c[0]) ** 2 + 4 * (p[1] - c[1]) ** 2 + 3 * (p[2] - c[2]) ** 2)


def pebbleize(img, levels, alpha_cut=100, keep_gray=False):
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
            key = (r, g, b, lo, hi, keep_gray)
            if keep_gray and max(r, g, b) - min(r, g, b) < 24:
                v = max(0, min(255, (((r + g + b) / 3) - lo) * 255 / (hi - lo)))
                _cache[key] = (min((0, 85, 170, 255), key=lambda p: abs(p - v)),) * 3
            if key not in _cache:
                # 0.5.2 "punch" for the dim Pebble screen: lift mid-tones (gamma 0.85), keep near-greys neutral
                # (tiles only), boost already-colourful pixels' saturation 1.3x
                if levels == SPRITES:                      # UI art: unchanged mapping
                    _cache[key] = near(tuple(max(0, min(255, (v - lo) * 255 / (hi - lo))) for v in (r, g, b)))
                    px[x, y] = _cache[key] + (255,)
                    continue
                c = [max(0.0, min(1.0, (v - lo) / (hi - lo))) ** (0.85 if levels == TILES else 1.0) for v in (r, g, b)]
                if levels == TILES and max(r, g, b) - min(r, g, b) < 28:
                    m = sum(c) / 3 * 255
                    _cache[key] = (min((0, 85, 170, 255), key=lambda p: abs(p - m)),) * 3
                else:
                    h, l, sat = colorsys.rgb_to_hls(*c)
                    if sat > 0.3: sat = min(1.0, sat * 1.3)
                    _cache[key] = near(tuple(v * 255 for v in colorsys.hls_to_rgb(h, l, sat)))
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
