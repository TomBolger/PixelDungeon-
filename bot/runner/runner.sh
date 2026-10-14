#!/bin/bash
# PebxelSim job runner (Claude). Start once in WSL:  bash /mnt/c/Users/<you>/Documents/PebxelSim/runner.sh
# It only does one thing: unpacks bot bundles from bundles\ and runs them on job files dropped in jobs\.
# Job files are plain settings, never shell commands. Stop it with Ctrl+C, or create a file named STOP.
# Pause: a file named PAUSE in this folder freezes every running game (PAUSE-sim.cmd makes it, RESUME-sim.cmd removes it).
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$HOME/pebxelsim"
CORES=$(nproc)
mkdir -p "$ROOT/jobs" "$ROOT/bundles" "$ROOT/results" "$ROOT/done" "$WORK/b" "$WORK/out"
rm -f "$ROOT/STOP"
status() { printf 'time %s\nstate %s\ncores %s\nglibc %s\nload %s\n' "$(date -Is)" "$1" "$CORES" "$(ldd --version | head -1 | awk '{print $NF}')" "$(cut -d' ' -f1-3 /proc/loadavg)" > "$ROOT/status.txt"; }
okname() { [[ "$1" =~ ^[A-Za-z0-9_.-]+$ ]]; }
oktok() { [[ "$1" =~ ^[A-Z][A-Z0-9_]*=[A-Za-z0-9._,:/-]*$ ]] && [[ "$1" != LD_* && "$1" != PATH=* && "$1" != HOME=* && "$1" != *..* && "${1#*=}" != /* ]]; }

run_job() {   # $1 = job file
  local jf="$1" job bundle="" bin="deepbot" games=0 first=0 chunk=10 runs=() line
  job=$(basename "$jf" .job); okname "$job" || { mv "$jf" "$ROOT/done/bad_name.job"; return; }
  local res="$ROOT/results/$job"; mkdir -p "$res"; local log="$res/runner.log"; : > "$log"
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"; [[ -z "$line" || "$line" == \#* ]] && continue
    case "$line" in
      bundle=*) bundle="${line#bundle=}";; bin=*) bin="${line#bin=}";;
      games=*) games="${line#games=}";; first=*) first="${line#first=}";; chunk=*) chunk="${line#chunk=}";;
      run\ *) runs+=("${line#run }");;
      *) echo "ignored line: $line" >> "$log";;
    esac
  done < "$jf"
  okname "$bundle" && okname "$bin" && [[ "$games$first$chunk" =~ ^[0-9]+$ ]] && [ "$chunk" -gt 0 ] || { echo "bad job settings" >> "$log"; mv "$jf" "$ROOT/done/"; return; }
  local bd="$WORK/b/${bundle%.tgz}"
  if [ ! -d "$bd" ]; then mkdir -p "$bd" && tar -xzf "$ROOT/bundles/$bundle" -C "$bd" 2>>"$log" || { echo "cannot unpack $bundle" >> "$log"; rm -rf "$bd"; mv "$jf" "$ROOT/done/"; return; }; fi
  [ -x "$bd/$bin" ] || { echo "no program $bin in $bundle" >> "$log"; mv "$jf" "$ROOT/done/"; return; }
  local out="$WORK/out/$job"; rm -rf "$out"; mkdir -p "$out"; local tasks="$out/tasks"; : > "$tasks"
  for r in "${runs[@]}"; do
    read -ra parts <<< "$r"; local name="${parts[0]}"; okname "$name" || { echo "bad run name $name" >> "$log"; continue; }
    local envs=() ok=1
    for t in "${parts[@]:1}"; do oktok "$t" && envs+=("$t") || { echo "rejected setting $t" >> "$log"; ok=0; }; done
    [ $ok = 1 ] || continue
    mkdir -p "$out/$name"; printf '%s\n' "${envs[*]}" > "$out/$name/settings"
    for ((s = first; s < first + games; s += chunk)); do e=$((s + chunk)); [ $e -gt $((first + games)) ] && e=$((first + games)); echo "$name $s $e" >> "$tasks"; done
  done
  local total; total=$(wc -l < "$tasks"); echo "start $(date -Is) tasks $total cores $CORES" >> "$log"
  export BD="$bd" BIN="$bin" OUT="$out"
  xargs -P "$CORES" -L 1 bash -c 'read -ra E < "$OUT/$0/settings"; cd "$BD" && env -i PATH=/usr/bin:/bin "${E[@]}" START=$1 nice -n 10 timeout 86400 "./$BIN" $2 > "$OUT/$0/p$(printf %06d $1).tmp" 2>/dev/null; mv "$OUT/$0/p$(printf %06d $1).tmp" "$OUT/$0/p$(printf %06d $1).txt"' < "$tasks" &
  local xp=$! paused=0
  while kill -0 $xp 2>/dev/null; do
    local doneN; doneN=$(find "$out" -name 'p*.txt' | wc -l)
    status "job $job: $doneN of $total chunks done"; printf '%s of %s\n' "$doneN" "$total" > "$res/progress.txt"
    for r in "$out"/*/; do [ -f "$r/settings" ] && cat "$r"/p*.txt > "$res/$(basename "$r").txt" 2>/dev/null; done
    if [ -f "$ROOT/PAUSE" ]; then
      [ "$paused" = 1 ] || { pkill -STOP -P $xp; pkill -STOP -f "^./$bin"; pkill -STOP -f "timeout 86400 ./$bin"; paused=1; echo "paused $(date -Is)" >> "$log"; }
      status "PAUSED job $job: $doneN of $total chunks done (delete PAUSE or run RESUME-sim.cmd to carry on)"
    elif [ "$paused" = 1 ]; then pkill -CONT -f "^./$bin"; pkill -CONT -f "timeout 86400 ./$bin"; pkill -CONT -P $xp; paused=0; echo "resumed $(date -Is)" >> "$log"; fi
    if [ -f "$ROOT/STOP" ] || [ -f "$ROOT/jobs/$job.cancel" ]; then pkill -CONT -f "^./$bin"; pkill -P $xp; kill $xp; pkill -f "^./$bin" ; echo "cancelled" >> "$log"; rm -f "$ROOT/jobs/$job.cancel"; fi
    sleep 15
  done
  for r in "$out"/*/; do [ -f "$r/settings" ] && cat "$r"/p*.txt > "$res/$(basename "$r").txt" 2>/dev/null; done
  echo "end $(date -Is)" >> "$log"; printf 'done\n' > "$res/progress.txt"; mv "$jf" "$ROOT/done/"
}

echo "PebxelSim runner: $CORES cores. Watching $ROOT/jobs. Leave this window open (minimise it is fine). Ctrl+C to stop."
while [ ! -f "$ROOT/STOP" ]; do
  status idle
  for jf in "$ROOT"/jobs/*.job; do [ -f "$jf" ] || continue; echo "$(date +%T) running $(basename "$jf")"; run_job "$jf"; echo "$(date +%T) finished"; [ -f "$ROOT/STOP" ] && break; done
  sleep 10
done
status stopped; rm -f "$ROOT/STOP"; echo "stopped"
