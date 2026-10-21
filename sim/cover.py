#!/usr/bin/env python3
# cover.py REPORT.txt...: adds up the "COVER k=v;..." lines the Shattered bot prints (shattered/server/src/pebxel/Coverage.java)
# and the RESULT lines: wins, ends, quests, bosses, subclasses, item kinds seen/used per catalogue, windows, problems.
# Every catalogue entry of the game is listed (from ALL.txt next to this script if present, else what was seen).
import sys, collections, re

games = []
results = []
for path in sys.argv[1:]:
    for line in open(path, errors="replace"):
        if line.startswith("COVER "):
            d = {}
            for kv in line[6:].strip().split(";"):
                if "=" in kv:
                    k, v = kv.split("=", 1); d[k] = v
            games.append(d)
        elif line.startswith("RESULT "):
            results.append(line.strip())

n = len(games)
print(f"games {n}")
if not n: sys.exit()
C = collections.Counter
def items(d, k): return [x for x in d.get(k, "").split(",") if x]
ends = C(g.get("end", "?") for g in games)
print("ends:", dict(ends.most_common()))
byclass = collections.defaultdict(C)
for g in games: byclass[g.get("class", "?")][g.get("end", "?")] += 1
for c, e in sorted(byclass.items()): print(f"  {c}: {dict(e)}")
print("amulet held:", sum(g.get("amulet") == "true" for g in games))
q = collections.defaultdict(C)
for g in games:
    for part in items(g, "quests"):
        name, st = part.split(":"); q[name][st] += 1
print("quests:", {k: dict(v) for k, v in q.items()})
print("bosses beaten:", dict(C(b for g in games for b in set(items(g, "bosses"))).most_common()))
print("subclasses:", dict(C(g.get("subclass", "") for g in games).most_common()))
print("abilities:", dict(C(g.get("ability", "") for g in games).most_common()))
seen = C(x for g in games for x in set(items(g, "seen")))
used = C(x for g in games for x in set(items(g, "used")))
tried = C(x.split(":")[0] for g in games for x in items(g, "tried"))
cats = collections.defaultdict(set)
for x in list(seen) + list(used): cats[x.split(".", 1)[0]].add(x.split(".", 1)[1])
print("\nitem kinds (catalogue): used / seen in N games (bot tries)")
for cat in sorted(cats):
    names = sorted(cats[cat])
    nu = sum(1 for x in names if used[cat + "." + x])
    print(f"  {cat}: {nu}/{len(names)} used")
    for x in names:
        k = cat + "." + x
        t = tried.get(x, 0)
        flag = "" if used[k] else ("  <- never used" if not t else "  <- tried, not counted")
        print(f"      {x}: used {used[k]}, seen {seen[k]}, tried {t}{flag}")
print("\nwindows:", dict(C(w.split("=")[0] for g in games for w in items(g, "windows")).most_common()))
print("scenes:", dict(C(s for g in games for s in set(items(g, "scenes"))).most_common()))
probs = C(re.sub(r"depth \d+", "depth N", p) for g in games for p in set(items(g, "problems")))
print("problems:", dict(probs.most_common(25)))
kl = [int(g.get("keylisteners", "0") or 0) for g in games]
print("key listeners max:", max(kl))
