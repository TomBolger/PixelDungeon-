"""Packs the converted art (build/art) into one resource for the watch: watch/resources/data/art.bin.

Layout (little-endian):
  u16 count, then u32 offset of each picture's block (index = picture id - 1; 0 = none), then u32 end.
  Block: u16 width, u16 height (as stored), u8 shrink (texel coords on the wire are divided by this),
         u8 strip height, u16 strips, u32 strip offsets (from block start) x (strips + 1), strip data.
  Pixels are Pebble colour bytes (2 bits each alpha, red, green, blue); each strip is an LZ4 block
  (the standard block format, no header), decoded from its start.
Huge splash pictures are stored shrunk (title art, class backgrounds): the watch is 200 px wide."""
import os, struct, sys
import lz4.block
sys.path.insert(0, os.path.dirname(__file__))
from convert_art import paths, OUT as ART
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
DST = os.path.join(HERE, '..', 'watch', 'resources', 'data', 'art.bin')
STRIP = 16

def pebble(r, g, b, a):
    q = lambda v: min(3, (v + 42) // 85)
    return (q(a) << 6) | (q(r) << 4) | (q(g) << 2) | q(b)

def rle(data):
    out = bytearray(); i = 0; n = len(data)
    while i < n:
        r = 1
        while i + r < n and data[i + r] == data[i] and r < 129: r += 1
        if r >= 3:
            out += bytes((126 + r, data[i])); i += r; continue
        s = i
        while i < n and i - s < 128:
            r = 1
            while i + r < n and data[i + r] == data[i] and r < 3: r += 1
            if r >= 3: break
            i += 1
        out.append(i - s - 1); out += data[s:i]
    return bytes(out)

def block(path):
    im = Image.open(os.path.join(ART, path.rsplit('.', 1)[0] + '.png')).convert('RGBA')
    k = 1
    if path.startswith('splashes/'):
        while max(im.size) // k > 256: k *= 2
        im = im.resize((max(1, im.width // k), max(1, im.height // k)), Image.NEAREST)
    w, h = im.size
    px = im.load()
    strips = []
    for y0 in range(0, h, STRIP):
        data = bytes(pebble(*px[x, y]) if px[x, y][3] else 0 for y in range(y0, min(h, y0 + STRIP)) for x in range(w))
        strips.append(lz4.block.compress(data, store_size=False, mode='high_compression', compression=12))
    head = struct.pack('<HHBBH', w, h, k, STRIP, len(strips))
    offs, pos = [], len(head) + 4 * (len(strips) + 1)
    for s in strips: offs.append(pos); pos += len(s)
    offs.append(pos)
    return head + struct.pack('<%dI' % len(offs), *offs) + b''.join(strips)

if __name__ == '__main__':
    ps = paths()
    blocks = [block(p) for p in ps]
    table = 2 + 4 * (len(ps) + 1)
    offs, pos = [], table
    for b in blocks: offs.append(pos); pos += len(b)
    offs.append(pos)
    with open(DST, 'wb') as f:
        f.write(struct.pack('<H', len(ps)) + struct.pack('<%dI' % len(offs), *offs) + b''.join(blocks))
    print('art.bin', pos, 'bytes,', len(ps), 'pictures')
    if '-v' in sys.argv:
        for p, b in sorted(zip(ps, blocks), key=lambda x: -len(x[1]))[:15]: print('  %7d %s' % (len(b), p))
