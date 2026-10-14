"""cfsum.py RESULT.txt...: summarise CF what-if lines (a checkpoint replayed under each CFV variant)."""
import re,collections,sys
for f in sys.argv[1:]:
    tot=collections.defaultdict(lambda:[0,0]); n=0; byc=collections.defaultdict(lambda:collections.defaultdict(lambda:[0,0]))
    for l in open(f):
        if not l.startswith('CF'): continue
        n+=1; cls=int(re.search(r'cls (\d)',l).group(1))
        for nm,a,b in re.findall(r' (\w+) (\d+)/(\d+)',l):
            tot[nm][0]+=int(a); tot[nm][1]+=int(b); byc[cls][nm][0]+=int(a); byc[cls][nm][1]+=int(b)
    print(f, n,'deaths:', '  '.join(f'{k} {v[0]}/{v[1]} ({100*v[0]/max(1,v[1]):.0f}%)' for k,v in tot.items()))
    for c in sorted(byc): print('   cls',c,'  '.join(f'{k} {v[0]}/{v[1]}' for k,v in byc[c].items()))
