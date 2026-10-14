"""goolab.py RESULT.txt...: Goo win rate and causes of death from boss-lab runs (SD=5)."""
import re,collections,sys
for f in sys.argv[1:]:
    t=open(f).read()
    g=re.findall(r'^game \d+ cls (\d): (\w+) depth (\d+) \(max (\d+)\).*?(?:: (.*?)(?: on level \d+)?)?$',t,re.M)
    p=sum(1 for x in g if int(x[3])>=6); c=collections.Counter(x[0] for x in g if int(x[3])>=6)
    print(f'{f}: {len(g)} heroes, {p} beat Goo ({100*p/max(1,len(g)):.0f}%) by class', dict(sorted(c.items())), collections.Counter(x[4] for x in g if int(x[3])<6).most_common(3))
