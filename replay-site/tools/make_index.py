"""make_index.py REC_DIR OUT_DIR LABEL
Pack every REC_DIR/gN.jsonl (one recorded bot game, with its printout in gN.out) into OUT_DIR/gN.json for the viewer,
and write OUT_DIR/index.json, the game list the site shows. LABEL tags the bot version (e.g. a date)."""
import sys, json, re, glob, subprocess, os
rec, out, label = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
pack = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pack.py')
idx = []
for fn in sorted(glob.glob(os.path.join(rec, 'g*.jsonl')), key=lambda s: int(re.findall(r'g(\d+)\.jsonl$', s)[0])):
    n = int(re.findall(r'g(\d+)\.jsonl$', fn)[0])
    line = next(l for l in open(os.path.join(rec, f'g{n}.out')) if l.startswith(f'game {n} '))
    m = re.match(r'game \d+ cls (\d): (\w+) depth (\d+) \(max (\d+)\) lvl (\d+) turns (\d+) .*?: (.*)$', line.strip())
    cls, res, dep, mx, lvl, turns, cause = m.groups()
    subprocess.run([sys.executable, pack, os.path.join(out, f'g{n}.json'), f'{fn}@{label}'], check=True, capture_output=True)
    g = json.load(open(os.path.join(out, f'g{n}.json')))['games'][0]
    idx.append({'id': n, 'file': f'g{n}.json', 'cls': int(cls), 'result': res, 'depth': int(mx), 'lvl': int(lvl), 'turns': int(turns),
                'cause': cause.rstrip('.'), 'steps': len(g['frames']), 'bot': label})
json.dump({'games': idx}, open(os.path.join(out, 'index.json'), 'w'), indent=0)
print(len(idx), 'games packed into', out)
