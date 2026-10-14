#!/bin/sh
# score.sh [N] [shards]: N games, no head start, run in parallel shards; mean deepest floor, wins, death causes.
# Extra settings go in the environment, e.g.  AVOID=1 ./score.sh 200 4   (BIN=name picks another build/ program)
cd "$(dirname "$0")"
T=$(mktemp -d)
N=${1:-200}; P=${2:-2}
i=0; while [ $i -lt $P ]; do
  A=$((N * i / P)); B=$((N * (i + 1) / P))
  START=$A D=1 HS=${HS:-0} RT=${RT:-0} FK=${FK:-0} timeout 7200 build/${BIN:-deepbot} $B 2>/dev/null > $T/part$i.txt &
  i=$((i + 1))
done
wait
cat $T/part*.txt > $T/all.txt
python3 - $T/all.txt <<'P'
import re,collections,sys
t=open(sys.argv[1]).read()
g=re.findall(r'^game \d+ cls (\d): (\w+) depth \d+ \(max (\d+)\).*?(?:: (.*?)(?: on level \d+)?)?$',t,re.M)
tu=[int(x) for x in re.findall(r'^game .*? turns (\d+)',t,re.M)]; md=[int(x[2]) for x in g]
d=md; w=sum(1 for x in g if x[1]=='WON'); st=sum(1 for x in g if x[1]=='stopped')
print(f"games {len(g)} wins {w} stuck {st} turns/floor {sum(tu)/max(1,sum(md)):.0f} mean max depth {sum(d)/len(d):.2f}  >=5: {sum(1 for x in d if x>=5)}  >=6: {sum(1 for x in d if x>=6)}  >=10: {sum(1 for x in d if x>=10)}")
by=collections.Counter(x[3] for x in g if x[1]=='died'); print('  '+', '.join(f'{k} {v}' for k,v in by.most_common(8)))
cl=collections.defaultdict(list)
for x in g: cl[x[0]].append(int(x[2]))
print('  by class: '+', '.join(f"{['war','mag','rog','hun'][int(k)]} {sum(v)/len(v):.1f}" for k,v in sorted(cl.items())))
la=re.findall(r'look-ahead: (\d+) moments, (\d+) changed, (\d+) playouts',t)
vi=sum(int(x) for x in re.findall(r'violations: (\d+) total',t)); print(f'  violations (VIOL lines, should be 0): {vi}')
if la: print('  look-ahead: %d moments, %d changed, %d playouts' % tuple(sum(int(x[i]) for x in la) for i in range(3)))
P
rm -rf $T
