#!/usr/bin/env python3
"""hstats.py report.txt... : honest bot games summed up: depth reached per class (and subclass), what killed them
by depth, where health was lost (STATS lines, from BotStats), what was left unused. With two reports and --ab,
a paired comparison by seed (the same games played by A and B)."""
import re, sys, collections

CLS = ["WARRIOR", "MAGE", "ROGUE", "HUNTRESS", "DUELIST", "CLERIC"]

def games(path):
    out, cur = [], None
    for line in open(path, errors="replace"):
        if line.startswith("== game "):
            m = re.match(r"== game (\d+) class (\d+) seed (\d+)", line)
            if m:
                cur = {"game": int(m[1]), "cls": CLS[int(m[2])], "seed": int(m[3]), "log": "", "stats": {}}
                out.append(cur)
            continue
        if cur is None: continue
        if line.startswith("RESULT"):
            m = re.search(r": (.*?) depth (\d+) \(max (\d+)\) turns (\d+) lvl (\d+)", line)
            if m: cur.update(end=m[1], depth=int(m[2]), max=int(m[3]), turns=int(m[4]), lvl=int(m[5]))
            else:
                cur.update(end=line.split(": ", 1)[-1].strip(), max=cur.get("max", 0))
        elif line.startswith("  last log:"): cur["log"] = line[11:].strip()
        elif line.startswith(" / --") or (line.startswith(" /") and "killed you" in line): cur["log"] += line.strip()
        elif line.startswith("STATS "):
            for kv in line[6:].strip().split(";"):
                if "=" in kv: k, v = kv.split("=", 1); cur["stats"][k] = v
        elif line.startswith("depth ") and "turn" in line:
            cur["max"] = max(cur.get("max", 0), int(line.split()[1]))
    return [g for g in out if "end" in g or "max" in g]

def cause(g):
    e = g.get("end", "?")
    if e != "died": return e.split(" (")[0][:40]
    m = re.findall(r"-- ([^/]*?) killed you", g["log"])
    if m: return m[-1].strip()
    for k, w in (("bled to death", "bleeding"), ("Suffocated", "toxic gas"), ("burned to death", "fire"), ("starved", "starving"),
                 ("Zapped", "magic"), ("poison", "poison"), ("Ooze", "ooze"), ("died to", "?")):
        if k in g["log"]: return w
    return "?"

def dist(vals):
    c = collections.Counter(vals)
    return " ".join(f"{d}:{c[d]}" for d in sorted(c))

def summary(gs, title):
    print(f"== {title}: {len(gs)} games, mean max depth {sum(g['max'] for g in gs)/max(1,len(gs)):.2f}, wins {sum(1 for g in gs if g.get('end')=='WIN')}")
    for c in CLS:
        cg = [g for g in gs if g["cls"] == c]
        if not cg: continue
        ds = sorted(g["max"] for g in cg)
        print(f"  {c:9s} n={len(cg):3d} mean {sum(ds)/len(ds):5.2f} median {ds[len(ds)//2]:2d} best {ds[-1]:2d}  depths {dist(ds)}")
    subs = collections.defaultdict(list)
    for g in gs:
        s = g["stats"].get("sub", "NONE")
        if s != "NONE": subs[s].append(g["max"])
    for s, ds in sorted(subs.items()): print(f"  sub {s:12s} n={len(ds):3d} mean {sum(ds)/len(ds):5.2f} depths {dist(sorted(ds))}")
    byreg = collections.defaultdict(collections.Counter)
    for g in gs:
        r = (g["max"] - 1) // 5
        byreg[r][cause(g)] += 1
    names = ["1-5 sewers", "6-10 prison", "11-15 caves", "16-20 city", "21-26 halls"]
    print("  ends by region:")
    for r in sorted(byreg): print(f"    {names[min(r,4)]:12s} " + ", ".join(f"{k} {v}" for k, v in byreg[r].most_common(10)))
    lost = collections.Counter(); tl = th = tr = ts = 0; left = collections.Counter(); unsp = 0
    for g in gs:
        st = g["stats"]
        if not st: continue
        tl += int(st.get("lost", 0)); th += int(st.get("healed", 0)); tr += int(st.get("rest", 0)); ts += int(st.get("starve", 0))
        for kv in st.get("by", "").split(","):
            if ":" in kv:
                k, v = kv.rsplit(":", 1); lost[k] += int(v)
        for kv in st.get("left", "").split(","):
            if ":" in kv:
                k, v = kv.split(":"); left[k] += int(v)
        unsp += int(st.get("talentsUnspent", 0))
    tt = sum(g.get("turns", 0) for g in gs)
    print(f"  health lost {tl}, regained {th}; turns {tt}, resting {tr} ({100*tr/max(1,tt):.0f}%), starving {ts} ({100*ts/max(1,tt):.0f}%)")
    print("  lost to: " + ", ".join(f"{k} {v}" for k, v in lost.most_common(14)))
    print("  left at the end (all games): " + ", ".join(f"{k} {v}" for k, v in sorted(left.items())) + f", talent points unspent {unsp}")

def ab(a, b):
    A = {(g["cls"], g["seed"]): g for g in a}; B = {(g["cls"], g["seed"]): g for g in b}
    keys = sorted(set(A) & set(B))
    better = worse = same = 0; diff = 0
    per = collections.defaultdict(lambda: [0, 0, 0, 0])
    for k in keys:
        d = B[k]["max"] - A[k]["max"]; diff += d
        p = per[k[0]]; p[0] += 1; p[1] += d
        if d > 0: better += 1; p[2] += 1
        elif d < 0: worse += 1; p[3] += 1
        else: same += 1
    n = max(1, len(keys))
    print(f"== paired A/B on {len(keys)} games: B-A mean depth {diff/n:+.2f}; B deeper {better}, shallower {worse}, same {same}")
    for c in CLS:
        if c in per: p = per[c]; print(f"  {c:9s} n={p[0]:3d} {p[1]/p[0]:+.2f}  (+{p[2]} -{p[3]})")
    # sign test p-value (two-sided)
    from math import comb
    m = better + worse; k = min(better, worse)
    pv = sum(comb(m, i) for i in range(0, k + 1)) / 2 ** m * 2 if m else 1
    print(f"  sign test p = {min(1, pv):.3f}")

if __name__ == "__main__":
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    if "--ab" in sys.argv and len(args) == 2:
        a, b = games(args[0]), games(args[1])
        summary(a, "A " + args[0]); summary(b, "B " + args[1]); ab(a, b)
    else:
        gs = []
        for p in args: gs += games(p)
        summary(gs, " + ".join(args))
