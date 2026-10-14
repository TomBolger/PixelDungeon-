"""0.60: hue fixes for the dungeon tile sheets, run on the Pebble sheet right after pebbleize (convert_art.py: sewers,
convert_regions.py: the other regions). Each rule reads the source pixel, so cells with the same source (EMPTY and the
hidden traps drawn as EMPTY) still come out identical."""
import colorsys

STATUES = (35, 36)
TRAP_INKS = {   # visible trap -> (hue range in degrees, min saturation, Pebble colour): one clear colour per kind of trap
    19: ((15, 45), 0.6, (255, 170, 0)),    # fire: orange (its shaded half snapped to (255, 85, 0), as red as the alarm trap's)
    21: ((30, 70), 0.4, (255, 255, 85)),   # paralytic: yellow (the sewers' half came out orange, like fire)
    32: ((180, 215), 0.4, (85, 170, 255)), # lightning: PD's sky blue, not the pale cyan of the summoning trap
    37: ((10, 40), 0.3, (170, 85, 0)),     # gripping: rusty brown teeth and grey, not the fire trap's peach
}


def _hsv(c):
    h, s, v = colorsys.rgb_to_hsv(*(x / 255 for x in c[:3])); return h * 360, s, v


def _cells(src, w):
    for y in range(64):
        for x in range(w):
            yield (y // 16) * 16 + x // 16, x, y


def warm_stone(src, tp, w=256):
    """Light tan and beige stone (the prison's and city's walls, door frames, stairs, statues, wells) snapped to pink
    (255,170,170): the luminance-weighted nearest colour has no cream between (255,255,170) and pink. Khaki (170,170,85)
    keeps the hue (on the panel (174,163,130), close to PD's (180,168,144))."""
    for c, x, y in _cells(src, w):
        r, g, b, a = src[x, y]
        if a == 255 and tp[x, y][:3] == (255, 170, 170) and r - b >= 15 and g - b >= 6: tp[x, y] = (170, 170, 85, 255)


def marble_statues(src, tp, w=256):
    """The statues' soft grey-mauve shading (the city's (135,100,102)) saturated to rose (170,85,85): with the pink it
    made a pale, flesh-toned figure. Light grey reads as carved stone."""
    for c, x, y in _cells(src, w):
        if c not in STATUES: continue
        r, g, b, a = src[x, y]
        if a == 255 and tp[x, y][:3] == (170, 85, 85) and max(r, g, b) - min(r, g, b) <= 45 and abs(g - b) < 8:
            tp[x, y] = (170, 170, 170, 255)


def trap_inks(src, tp, w=256):
    """A visible trap's coloured marks in one colour per kind, so fire/alarm, paralytic/fire and lightning/summoning
    stay apart on the panel. Only pixels that differ from the plain floor (cell 1) are touched."""
    for c, x, y in _cells(src, w):
        if c not in TRAP_INKS or not src[x, y][3] or src[x, y] == src[16 + x % 16, y % 16]: continue
        (h0, h1), smin, col = TRAP_INKS[c]
        h, s, v = _hsv(src[x, y])
        if s > smin and h0 <= h <= h1: tp[x, y] = col + (255,)
