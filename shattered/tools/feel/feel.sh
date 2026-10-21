#!/bin/bash
# How evenly the watch shows motion, measured: a RadioTest run (the real game, Link and Pacer, a model of the
# radio, in simulated time) records what the watch gets; replay.c plays it through the watch app's own C code
# (main.c, scene.c, predict.c from WATCH_SRC, default watch/src/c); feel.py measures steps, speeds, slides, the
# camera and gaps between frames.
#   tools/feel/feel.sh OUT [scenarios...]      (default: idle walks wstop wfight menu)
# Env: RadioTest's (RADIO=0, RTT_MS, RATE, SEED...), PEBXEL_STEP, PACE_*; DRAW_MS (a frame's cost on the watch,
# default 30), WATCH_SRC (another copy of the watch's C code to compare), TRACE_IN (replay an earlier trace).
set -e
H="$(cd "$(dirname "$0")/../.." && pwd)"
OUT=$1; shift; mkdir -p "$OUT"
SRC=${WATCH_SRC:-$H/watch/src/c}
cc -O1 -w -I"$H/tools/feel/stub" -I"$SRC" -Dscene_apply=scene_apply_real -c "$SRC/scene.c" -o "$OUT/scene.o"
cc -O1 -w -I"$H/tools/feel/stub" -I"$SRC" -c "$SRC/predict.c" -o "$OUT/predict.o"
P=""; [ -f "$SRC/particles.c" ] && { cc -O1 -w -I"$H/tools/feel/stub" -I"$SRC" -c "$SRC/particles.c" -o "$OUT/particles.o"; P="$OUT/particles.o"; }
cc -O1 -w -I"$H/tools/feel/stub" -I"$SRC" "$H/tools/feel/replay.c" "$OUT/scene.o" "$OUT/predict.o" $P -o "$OUT/replay" -lm
if [ -z "$TRACE_IN" ]; then
  (cd "$H/spd/core/src/main/assets" && FRAME_SLEEP=${FRAME_SLEEP:-5} TRACE="$OUT/trace.txt" java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.RadioTest ${@:-idle walks wstop wfight menu} 2>&1 | grep -v JAVA_TOOL > "$OUT/radiotest.txt")
  TRACE_IN=$OUT/trace.txt
fi
"$OUT/replay" "$TRACE_IN" > "$OUT/frames.txt"
python3 "$H/tools/feel/feel.py" "$OUT/frames.txt"
