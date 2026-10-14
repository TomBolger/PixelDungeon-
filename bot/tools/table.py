"""table.py NAME=RESULT.txt ...: a markdown table row per bot result file (games, wins, mean deepest floor, bosses passed, by class)."""
import re,sys
def stats(f):
    t=open(f).read()
    g=re.findall(r'^game (\d+) cls (\d): (\w+) depth \d+ \(max (\d+)\)',t,re.M)
    mx=[int(x[3]) for x in g]; n=len(g)
    won=sum(x[2]=='WON' for x in g)
    lock=len(re.findall(r'^game \d+ cls \d: stopped',t,re.M))
    resp=re.findall(r'^  respawns (\d+):',t,re.M)
    deathless=sum(1 for x,r in zip(g,resp) if x[2]=='WON' and r=='0') if resp else sum(x[2]=='WON' for x in g)
    d={}
    for x in g: d.setdefault(x[1],[]).append(int(x[3]))
    cls=' '.join(f"{'WMRH'[int(k)]}{sum(v)/len(v):.1f}" for k,v in sorted(d.items()))
    return f"{n} | {won} | {lock} | {sum(mx)/max(1,n):.1f} | {sum(m>5 for m in mx)} / {sum(m>10 for m in mx)} / {sum(m>15 for m in mx)} / {sum(m>20 for m in mx)} / {sum(m>25 for m in mx)} | {cls}"
print("| arm | games | wins | lockouts | mean max depth | past Goo / Tengu / DM-300 / King / Yog | by class |")
print("|---|---|---|---|---|---|---|")
for a in sys.argv[1:]:
    name,f=a.split('=')
    print(f"| {name} | {stats(f)} |")
