"""walk.py RESULT.txt...: moves, cells stepped and turns spent per floor (needs FOODLOG=1 output: WALK lines)."""
import re,sys,collections
for f in sys.argv[1:]:
    t=open(f).read(); m=collections.Counter(); n=collections.Counter(); tt=collections.Counter(); st=collections.Counter()
    for g in re.split(r'\ngame \d+ cls',t):
        seen=set()
        for d,mv,stp,pas,sn,tu in re.findall(r'WALK d(\d+) moves (\d+) stepped (\d+) passable (\d+) seen (\d+) turns (\d+)',g):
            d=int(d)
            if d in seen or d>4: continue
            seen.add(d); n[d]+=1; m[d]+=int(mv); tt[d]+=int(tu); st[d]+=int(stp)
    print(f, ' '.join(f'd{d}: moves {m[d]/n[d]:.0f} turns {tt[d]/n[d]:.0f} uniq {st[d]/n[d]:.0f}' for d in sorted(n)))
