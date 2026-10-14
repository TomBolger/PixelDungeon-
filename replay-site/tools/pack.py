# pack.py out.json rec1.jsonl [rec2.jsonl ...]: compact replay data for the viewer
import sys, json, base64, re
HEAPSPR = {1: 56, 6: 56, 2: 57, 3: 115, 4: 117, 5: 116}
def pack2(vis):
    b = bytearray(256)
    for i, ch in enumerate(vis): b[i >> 2] |= (ord(ch) - 48) << ((i & 3) * 2)
    return base64.b64encode(bytes(b)).decode()
games = []
for arg in sys.argv[2:]:
    fn, _, ver = arg.partition('@')
    strs, sidx = [], {}
    def S(x):
        if x not in sidx: sidx[x] = len(strs); strs.append(x)
        return sidx[x]
    maps, midx, frames, gid, cls, info = [], {}, [], None, 0, {}
    for line in open(fn):
        d = json.loads(line)
        gid = d['game']; h = d['hero']; cls = h['cls']
        mb = base64.b64encode(bytes(d['map'])).decode()
        key = (d['depth'], mb)
        if key not in midx: midx[key] = len(maps); maps.append([d['depth'], mb])
        frames.append([d['step'], d['turn'], d['depth'], midx[key], pack2(d['vis']),
            [h['pos'], h['hp'], h['ht'], h['lvl'], h['str'], h['hunger']], S(h['weapon']), S(h['armor']),
            [[m['pos'], m['type'], m['hp'], m['ht'], m['state'], m['ally']] for m in d['mobs']],
            [[x['pos'], HEAPSPR.get(x['type'], x['spr']), x['type'], S(x['name'])] for x in d['heaps']],
            d['targets'], S(d['did']), [S(x) for x in d['newlog']], d['hp_now'], [S(x) for x in d['bag']],
            [None if e is None else [e[0], e[1], S(e[2]), e[3]] for e in d.get('eq', [])],
            [[e[0], e[1], S(e[2]), e[3]] for e in d.get('bagi', [])],
            [[x[0], HEAPSPR.get(x[2], x[1]), x[2], S(x[3]), x[4], x[5]] for x in d.get('allheaps', [])],
            d.get('plants', []), d.get('gas', [])])
        for k, v in d.get('info', {}).items(): info[S(k)] = S(v)
    last = frames[-1]
    games.append({'ver': ver, 'id': gid, 'cls': cls, 'maps': maps, 'strs': strs, 'frames': frames, 'info': info})
json.dump({'games': games}, open(sys.argv[1], 'w'), separators=(',', ':'))
print(sys.argv[1], sum(len(g['frames']) for g in games), 'frames')
