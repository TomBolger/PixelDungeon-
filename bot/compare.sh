#!/bin/sh
# compare.sh [runs] [scen args, e.g. --jties]: the javapd scenarios (original Java, headless) next to the C port's (scen.c), side by side.
# z: (C - Java) / standard error, from the histograms (means) or the counts (rates); |z| > 3 is beyond noise.
cd "$(dirname "$0")"
RUNS=${1:-2000}; shift 2>/dev/null; EXTRA="$*"
OUT=scen MAIN=tests/scen.c ./build.sh >/dev/null 2>&1 || { echo "scen build failed"; exit 1; }
[ -f ../tools/javapd/build/classes/sim/Sim.class ] || ../tools/javapd/build.sh >/dev/null || { echo "javapd build failed"; exit 1; }
T=$(mktemp -d)
set -- "melee rat" "melee crab" "burn door" "burn grass" "burn floor" "gas"
i=0
for s in "$@"; do
  i=$((i + 1))
  (cd ../tools/javapd && java -cp build/classes sim.Sim $s $RUNS > $T/j$i 2>/dev/null) &
  build/scen $s $RUNS $EXTRA > $T/c$i 2>&1 &
done
wait
i=0
for s in "$@"; do
  i=$((i + 1))
  python3 - "$s" $T/j$i $T/c$i <<'PY'
import re, sys, math
name, jf, cf = sys.argv[1:4]
J = [l.rstrip() for l in open(jf) if l.strip() and not l.strip().startswith("(") and "JAVA_TOOL_OPTIONS" not in l]
C = [l.rstrip() for l in open(cf) if l.strip()]
num = lambda s: re.findall(r"-?\d+(?:\.\d+)?", s)
def hist(line):
    return {int(k): int(v) for k, v in re.findall(r"(\d+):(\d+)", line.split(":", 1)[1])}
def stats(h):
    n = sum(h.values()); m = sum(k * v for k, v in h.items()) / n
    var = sum(v * (k - m) ** 2 for k, v in h.items()) / max(n - 1, 1)
    return n, m, var
print("=" * 100); print("SCENARIO %s   (left: Java original, right: C port)" % name)
for j, c in zip(J, C):
    if "histogram" in j:
        hj, hc = hist(j), hist(c)
        label = j.split(":", 1)[0].strip()
        keys = sorted(set(hj) | set(hc))
        print("  %s" % label)
        print("    %-6s %8s %8s" % ("value", "java", "C"))
        for k in keys: print("    %-6d %8d %8d" % (k, hj.get(k, 0), hc.get(k, 0)))
        nj, mj, vj = stats(hj); nc, mc, vc = stats(hc)
        se = math.sqrt(vj / nj + vc / nc) or 1e-9
        print("    mean %.3f vs %.3f  (n %d vs %d)  z = %+.2f" % (mj, mc, nj, nc, (mc - mj) / se))
    else:
        print("  J: %s" % j.strip()); print("  C: %s" % c.strip())
        if "win rate" in j:
            wj, wc = int(num(j)[1]), int(num(c)[1]); nj = nc = int(re.search(r"runs=(\d+)", J[0]).group(1))
            p = (wj + wc) / (nj + nc); se = math.sqrt(p * (1 - p) * (2 / nj)) or 1e-9
            print("     win rate z = %+.2f" % ((wc / nc - wj / nj) / se))
        if "deaths:" in j:
            dj, dc = int(num(j)[-1]), int(num(c)[-1]); n = int(re.search(r"runs=(\d+)", J[0]).group(1))
            p = (dj + dc) / (2 * n); se = math.sqrt(p * (1 - p) * 2 / n) or 1e-9
            print("     death rate z = %+.2f" % ((dc - dj) / n / se))
PY
done
rm -rf $T
