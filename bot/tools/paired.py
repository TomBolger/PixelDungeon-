"""paired.py BASE.txt OTHER.txt...: paired comparison of each file against BASE on the same game numbers (mean depth change and its error)."""
import re,sys,math
def r(f): return {int(a):int(b) for a,b in re.findall(r'^game (\d+) cls \d: \w+ depth \d+ \(max (\d+)\)',open(f).read(),re.M)}
base=r(sys.argv[1])
for f in sys.argv[2:]:
    b=r(f); k=sorted(set(base)&set(b)); d=[b[g]-base[g] for g in k]; m=sum(d)/len(d); sd=(sum((x-m)**2 for x in d)/(len(d)-1))**.5
    print(f'{f}: {m:+.2f} ± {sd/len(d)**.5:.2f} floors vs base (n={len(d)})')
