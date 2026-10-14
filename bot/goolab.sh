#!/bin/sh
# goolab.sh N [extra env]: start N heroes on floor 5 (SD=5: level 5, mail armour+1, sword+1) and count Goo wins
cd "$(dirname "$0")"
T=$(mktemp -d)
N=${1:-200}
for i in 0 1; do A=$((N * i / 2)); B=$((N * (i + 1) / 2)); START=$A SD=5 D=1 HS=0 RT=0 FK=0 STEPS=4000 timeout 3000 build/${BIN:-deepbot} $B 2>/dev/null > $T/part$i.txt & done; wait
cat $T/part*.txt > $T/all.txt
python3 - $T/all.txt <<'P'
import re,collections,sys
t=open(sys.argv[1]).read()
g=re.findall(r'^game \d+ cls (\d): (\w+) depth (\d+) \(max (\d+)\).*?(?:: (.*?)(?: on level \d+)?)?$',t,re.M)
p=sum(1 for x in g if int(x[3])>=6); print(f'goo lab: {len(g)} heroes, {p} beat Goo ({100*p/max(1,len(g)):.0f}%)', collections.Counter(x[4] for x in g if int(x[3])<6).most_common(4))
P
rm -rf $T
