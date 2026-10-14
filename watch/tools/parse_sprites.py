"""Parse PD's *Sprite.java: texture, frame size, animations {name: (fps, loop, [frames])}."""
import re, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)
PD = PD_SRC
def assets():
    a = {}
    for m in re.finditer(r'public static final String\s+(\w+)\s*=\s*"([^"]+)"', open(os.path.join(PD, "Assets.java")).read()):
        a[m.group(1)] = m.group(2)
    return a
def parse(name):
    s = open(os.path.join(PD, "sprites", name + ".java")).read()
    A = assets()
    tex = re.search(r'texture\(\s*Assets\.(\w+)\s*\)', s)
    film = re.search(r'new TextureFilm\(\s*texture\s*,\s*(\d+)\s*(?:,\s*(\d+)\s*)?\)', s)
    anims = {}
    for m in re.finditer(r'(\w+)\s*=\s*new Animation\(\s*(\d+)\s*,\s*(true|false)\s*\)', s):
        anims[m.group(1)] = [int(m.group(2)), m.group(3) == 'true', []]
    for m in re.finditer(r'(\w+)\.frames\(\s*\w+\s*,([^)]*)\)', s):
        if m.group(1) in anims:
            anims[m.group(1)][2] = [int(x) for x in m.group(2).replace(' ', '').split(',') if x]
    for m in re.finditer(r'(\w+)\s*=\s*(\w+)\.clone\(\)', s):
        if m.group(2) in anims: anims[m.group(1)] = list(anims[m.group(2)])
    return dict(texture=A.get(tex.group(1)) if tex else None, fw=int(film.group(1)) if film else None,
                fh=int(film.group(2) or film.group(1)) if film else None, anims=anims, extends=re.search(r'extends (\w+)', s).group(1))
if __name__ == "__main__":
    for f in sorted(os.listdir(os.path.join(PD, "sprites"))):
        if f.endswith("Sprite.java"):
            p = parse(f[:-5])
            print(f[:-5], p['texture'], p['fw'], p['fh'], p['extends'], {k: (v[0], len(v[2])) for k, v in p['anims'].items()})
