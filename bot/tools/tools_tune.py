#!/usr/bin/env python3
"""Coordinate search over the bot's judgment parameters (PRM), rules only (no look-ahead), N games per try."""
import subprocess, re, sys, json, os
N = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
names = ["rest_lo", "rest_hi", "rest_lo_b", "rest_hi_b", "engage", "emerg", "seedhp", "waitn", "front_bias", "testhp"]
cur = [40, 70, 25, 40, 0, 33, 60, 3, 1, 90]
steps = [10, 10, 10, 10, 20, 10, 15, 1, 1, 10]
lims = [(0, 95), (10, 100), (0, 95), (10, 100), (0, 100), (0, 90), (0, 100), (0, 8), (0, 6), (0, 100)]
def run(p):
    env = dict(os.environ, PRM=",".join(map(str, p)), NOLA="1", NOSTRAT="1")
    out = subprocess.run(["./score.sh", str(N), "2"], env=env, capture_output=True, text=True).stdout
    m = re.search(r"mean max depth ([\d.]+)", out); return float(m.group(1)) if m else 0
best = run(cur); print("start", cur, best, flush=True)
for rnd in range(2):
    for i in range(len(cur)):
        for d in (-1, 1):
            p = list(cur); p[i] = min(lims[i][1], max(lims[i][0], p[i] + d * steps[i]))
            if p == cur: continue
            v = run(p); print(f"  {names[i]} {cur[i]}->{p[i]}: {v:.3f}", flush=True)
            if v > best + 0.02: best, cur = v, p; print("   keep", cur, best, flush=True)
    print("round", rnd, cur, best, flush=True)
json.dump({"prm": cur, "score": best}, open("tuned.json", "w"))
