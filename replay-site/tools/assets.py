# assets.py: rebuild www/assets.json (data URIs for tiles/water per region, items, plants, hero first frames, mob first idle frames)
# from the upstream Pixel Dungeon art. Only needed if the art or the mob list changes.
import sys, os, io, json, base64
HERE = os.path.dirname(os.path.abspath(__file__))
WTOOLS = os.path.normpath(os.path.join(HERE, '..', '..', 'watch', 'tools'))
sys.path.insert(0, WTOOLS)
from PIL import Image
import mobs
from parse_sprites import parse
from paths import PD_ASSETS as A
def uri(im):
    b = io.BytesIO(); im.save(b, 'PNG', optimize=True); return 'data:image/png;base64,' + base64.b64encode(b.getvalue()).decode()
out = {'tiles': [uri(Image.open(f'{A}/tiles{i}.png').convert('RGBA')) for i in range(5)],
       'water': [uri(Image.open(f'{A}/water{i}.png').convert('RGBA')) for i in range(5)],
       'items': uri(Image.open(f'{A}/items.png').convert('RGBA')),
       'plants': uri(Image.open(f'{A}/plants.png').convert('RGBA'))}
# hero: first frame (12x15) of each class sheet; tier row 0
hs = Image.new('RGBA', (48, 15))
for i, f in enumerate(['warrior', 'mage', 'rogue', 'ranger']):
    hs.paste(Image.open(f'{A}/{f}.png').convert('RGBA').crop((0, 0, 12, 15)), (i * 12, 0))
out['hero'] = uri(hs)
# mobs: first idle frame of each MT_* in order, packed in a strip; meta [x, w, h]
frames, meta = [], []
for m in mobs.MOBS:
    sp = m['sprite']
    if not sp: frames.append(None); meta.append([0, 0, 0]); continue
    p = parse(sp)
    if p['texture'] is None and p['extends'] != 'MobSprite': p2 = parse(p['extends']); p2['anims'].update(p['anims']); p = p2
    tex = Image.open(os.path.join(A, p['texture'])).convert('RGBA'); fw, fh = p['fw'], p['fh']
    idle = p['anims'].get('idle'); f0 = idle[2][0] if idle and idle[2] else 0
    cols = tex.width // fw
    fr = tex.crop(((f0 % cols) * fw, (f0 // cols) * fh, (f0 % cols) * fw + fw, (f0 // cols) * fh + fh))
    frames.append(fr); meta.append([0, fw, fh])
W = sum(f.width for f in frames if f); H = max(f.height for f in frames if f)
strip = Image.new('RGBA', (W, H)); x = 0
for i, f in enumerate(frames):
    if f: strip.paste(f, (x, 0)); meta[i][0] = x; x += f.width
out['mobs'] = uri(strip); out['mobmeta'] = meta; out['mobnames'] = [m['name'] for m in mobs.MOBS]
src = open(os.path.join(WTOOLS, 'convert_art.py')).read(); i = src.index('ITEM_IDX = ('); j = src.index('items = Image.open', i)
ns = {}; exec(src[i:j], ns); out['itemidx'] = ns['ITEM_IDX']
json.dump(out, open(os.path.join(HERE, '..', 'www', 'assets.json'), 'w'))
print('ok', sum(len(v) if isinstance(v, str) else sum(len(z) for z in v if isinstance(z, str)) for v in out.values() if not isinstance(v, list) or isinstance(v[0], str)))
