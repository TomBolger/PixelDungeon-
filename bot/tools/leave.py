"""leave.py RESULT.txt...: turn and food state when the hero leaves each depth (needs FOODLOG=1 output)."""
import re,collections,sys
def stats(f):
    t=open(f).read(); games=re.split(r'\ngame \d+ cls',t)
    first=collections.defaultdict(list)
    for g in games:
        seen=set()
        for d,tt,h,b in re.findall(r'FOOD leave d(\d+) t(\d+) hunger (\d+) bag (\d+)',g):
            d=int(d)
            if d not in seen: seen.add(d); first[d].append((int(tt),int(h),int(b)))
    return first
for f in sys.argv[1:]:
    s=stats(f); row=[]
    for d in range(1,6):
        v=s[d]; row.append(f'd{d}: n{len(v)} t{sum(x[0] for x in v)//max(1,len(v))} food{sum(x[2] for x in v)/max(1,len(v)):.2f}')
    print(f, ' | '.join(row))
