#!/usr/bin/env python3
"""Strict paired-seed summary of host runs. No wins or improvements inferred from partial logs."""
import collections
import math
import pathlib
import re
import sys

expected, lhs, rhs = int(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
pattern = re.compile(r"^game (\\d+) cls (\\d): (\\w+) depth \\d+ \\(max (\\d+)\\)(.*)$", re.M)
# Compensate for python string literal / regex clarity: the regex above is raw
# and should contain one backslash per regex operator.
pattern = re.compile(r"^game (\d+) cls (\d): (\w+) depth \d+ \(max (\d+)\)(.*)$", re.M)

def load(path):
    if not path.exists():
        return {}, "MISSING"
    text = path.read_text(errors="replace")
    results = {}
    for match in pattern.finditer(text):
        seed, cls, outcome, max_depth, trailing = match.groups()
        results[int(seed)] = dict(cls=int(cls), status=outcome, max_depth=int(max_depth),
                                  details=trailing)
    return results, text

a, at = load(lhs)
b, bt = load(rhs)
ids = sorted(set(a) & set(b))
def report(name, dat, logs):
    values = list(dat.values())
    if not values:
        print(f"{name}: NO COMPLETE GAMES")
        return
    depths = [v["max_depth"] for v in values]
    print(f"{name}: n={len(values)}, mean max floor={sum(depths)/len(depths):.3f}, "
          f">=5:{sum(d>=5 for d in depths)}, >=6:{sum(d>=6 for d in depths)}, "
          f">=10:{sum(d>=10 for d in depths)}, >=20:{sum(d>=20 for d in depths)}, "
          f"wins:{sum(v['status']=='WON' for v in values)}")
    by_cls = collections.defaultdict(list)
    for v in values:
        by_cls[v["cls"]].append(v["max_depth"])
    print("  by class: " + ", ".join(
        f"{['war','mag','rog','hun'][c]} n={len(x)} mean={sum(x)/len(x):.2f}"
        for c,x in sorted(by_cls.items())))
    causes=collections.Counter(v["details"].strip() for v in values if v["status"] == "died")
    print("  top fatal causes: " + ", ".join(f"{why}: {count}" for why,count in causes.most_common(8)))
    look=re.findall(r"look-ahead: (\d+) moments, (\d+) changed, (\d+) playouts", logs)
    if look:
        print("  lookahead (moments, changes, playouts): " +
              ", ".join(str(sum(int(z[i]) for z in look)) for i in range(3)))

report("BASELINE", a, at)
report("EARLYLA", b, bt)
if ids:
    delta=[b[k]["max_depth"]-a[k]["max_depth"] for k in ids]
    mean=sum(delta)/len(delta)
    sd=math.sqrt(sum((v-mean)**2 for v in delta)/(len(delta)-1)) if len(delta)>1 else 0
    print(f"PAIRED: n={len(ids)} mean improvement={mean:+.3f} floors; SE={sd/math.sqrt(len(delta)):.3f}; "
          f"better={sum(d>0 for d in delta)} worse={sum(d<0 for d in delta)} unchanged={sum(d==0 for d in delta)}")
    print("  seeds showing different outcomes: " + ", ".join(
        f"{i}:{a[i]['max_depth']}->{b[i]['max_depth']}" for i in ids if a[i]['max_depth']!=b[i]['max_depth']))
if len(a)!=expected or len(b)!=expected or set(a)!=set(b):
    print(f"INCOMPLETE: expected {expected} per side, baseline={len(a)} experimental={len(b)}; do NOT use as final comparison.")
    sys.exit(2)
