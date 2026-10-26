#!/bin/bash
# wait_report.sh JOB [DIR]: waits until the PebxelSim runner on Monolith has pushed JOB's report to the sim-reports
# branch, then unpacks it into DIR (default /tmp/simreports/JOB) and prints the file list. Run it in the background:
# it exits (one notification) the moment the report lands. Gives up after 6 hours.
J=$1; OUT=${2:-/tmp/simreports/$1}; R=https://github.com/TomBolger/PixelDungeon-.git; C=/tmp/simreports/.repo-$J   # (a clone per job: several agents wait at once)
mkdir -p "$OUT"; [ -d "$C/.git" ] || { mkdir -p "$C"; git -C "$C" init -q; git -C "$C" remote add origin "$R"; }
last=""
for ((i = 0; i < 720; i++)); do
  h=$(git ls-remote "$R" refs/heads/sim-reports 2>/dev/null | cut -f1)
  if [ -n "$h" ] && [ "$h" != "$last" ]; then
    last=$h
    git -C "$C" fetch -q --depth 1 origin sim-reports && git -C "$C" checkout -q -f FETCH_HEAD
    if grep -q " $J done" "$C/log.txt" 2>/dev/null && [ -d "$C/jobs/$J" ]; then
      for f in "$C/jobs/$J"/*.gz; do zcat "$f" > "$OUT/$(basename "$f" .gz)"; done
      echo "report $J: $(grep " $J done" "$C/log.txt" | tail -1)"; ls -la "$OUT"; exit 0
    fi
  fi
  sleep 30
done
echo "no report for $J after 6 hours"; exit 1
