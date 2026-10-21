#!/bin/bash
# PebxelSim job runner (Claude), v4. Start once in WSL:  bash /mnt/c/Users/<you>/Documents/PebxelSim/runner.sh
# v2: Java bundles (a JRE is fetched once into ~/pebxelsim/jre), a report pushed to GitHub when a job ends (branch
# sim-reports of the project repo, with a deploy key made here: deploy-key.pub), and runner-next.sh replaces this
# script between jobs (so Claude can update the runner without anyone restarting it).
# v3: jobs and bundles also come from GitHub (branch sim-jobs of the public project repo, read without any key), so
# Claude can start runs without the desktop app; the sim-jobs branch may also carry runner-next.sh.
# v4: every job's processes run in a session of their own (setsid), so PAUSE, a cancel and the end of a job reach the
# game JVMs too (before, only the simbot shells were stopped or killed: the JVMs of cancelled jobs ran on for hours at
# full CPU, five of them found eating 5 of the 12 cores); and before and after each job any process still running
# from a bundle folder is killed (strays.log says what).
# It only does one thing: unpacks bot bundles from bundles\ and runs them on job files dropped in jobs\.
# Job files are plain settings, never shell commands. Stop it with Ctrl+C, or create a file named STOP.
# Pause: a file named PAUSE in this folder freezes every running game (PAUSE-sim.cmd makes it, RESUME-sim.cmd removes it).
ROOT="$(cd "$(dirname "$0")" && pwd)"
WORK="$HOME/pebxelsim"
CORES=$(nproc)
mkdir -p "$ROOT/jobs" "$ROOT/bundles" "$ROOT/results" "$ROOT/done" "$WORK/b" "$WORK/out"
rm -f "$ROOT/STOP"
status() { printf 'time %s\nstate %s\ncores %s\nglibc %s\nload %s\n' "$(date -Is)" "$1" "$CORES" "$(ldd --version | head -1 | awk '{print $NF}')" "$(cut -d' ' -f1-3 /proc/loadavg)" > "$ROOT/status.txt"; }
# ---- Java for the Shattered bot bundles (once; nothing system-wide) ----
JRE="$WORK/jre"
java_ok() { [ -x "$JRE/bin/java" ]; }
get_java() {
  java_ok && return 0
  local t="$WORK/jre.tgz"; rm -rf "$JRE.part"; mkdir -p "$JRE.part"
  curl -fsSL -o "$t" "https://api.adoptium.net/v3/binary/latest/17/ga/linux/x64/jre/hotspot/normal/eclipse" && tar -xzf "$t" -C "$JRE.part" --strip-components=1 && rm -f "$t" && mv "$JRE.part" "$JRE"
  java_ok && echo "java: $("$JRE/bin/java" -version 2>&1 | head -1)" > "$ROOT/java.txt" || echo "java: download failed" > "$ROOT/java.txt"
}

# ---- reports to GitHub when a job ends (so Claude hears at once instead of checking on a timer) ----
KEY="$HOME/.ssh/pebxelsim_ed25519"
REPO_SSH="git@github.com:TomBolger/PixelDungeon-.git"; BRANCH="sim-reports"
setup_key() {
  [ -f "$KEY" ] || { mkdir -p "$HOME/.ssh"; ssh-keygen -q -t ed25519 -N "" -C "pebxelsim-runner" -f "$KEY"; }
  cp "$KEY.pub" "$ROOT/deploy-key.pub"
}
report() {   # $1 = job name: its results, compressed, plus a one-line summary, pushed to the sim-reports branch
  local job="$1" rd="$WORK/reports" res="$ROOT/results/$1"
  export GIT_SSH_COMMAND="ssh -i $KEY -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new"
  if [ ! -d "$rd/.git" ]; then
    rm -rf "$rd"; git clone -q --depth 1 -b "$BRANCH" "$REPO_SSH" "$rd" 2>>"$ROOT/report.log" || { mkdir -p "$rd" && git -C "$rd" init -q && git -C "$rd" checkout -q -b "$BRANCH" && git -C "$rd" remote add origin "$REPO_SSH"; }
    git -C "$rd" config user.name "PebxelSim runner"; git -C "$rd" config user.email "pebxelsim@localhost"
  fi
  git -C "$rd" pull -q --rebase origin "$BRANCH" 2>/dev/null
  mkdir -p "$rd/jobs/$job"; rm -f "$rd/jobs/$job"/*
  for f in "$res"/*.txt; do [ -f "$f" ] && gzip -c "$f" > "$rd/jobs/$job/$(basename "$f").gz"; done
  printf '%s %s done %s\n' "$(date -Is)" "$job" "$(cat "$res/progress.txt" 2>/dev/null)" >> "$rd/log.txt"
  git -C "$rd" add -A && git -C "$rd" commit -q -m "$job done" && git -C "$rd" push -q origin "HEAD:$BRANCH" 2>>"$ROOT/report.log" && echo "$(date -Is) reported $job" >> "$ROOT/report.log"
}

# ---- jobs from GitHub: the sim-jobs branch (bundles/*.tgz, jobs/*.job, maybe runner-next.sh) ----
REPO_HTTPS="https://github.com/TomBolger/PixelDungeon-.git"; JOBS_BRANCH="sim-jobs"
pull_jobs() {
  local jr="$WORK/jobsrepo" seen="$WORK/seen-jobs.txt" head
  head=$(git ls-remote "$REPO_HTTPS" "refs/heads/$JOBS_BRANCH" 2>/dev/null | cut -f1)
  [ -n "$head" ] && [ "$head" != "$(cat "$WORK/jobs-head" 2>/dev/null)" ] || return 0
  [ -d "$jr/.git" ] || { mkdir -p "$jr" && git -C "$jr" init -q && git -C "$jr" remote add origin "$REPO_HTTPS"; }
  git -C "$jr" fetch -q --depth 1 origin "$JOBS_BRANCH" && git -C "$jr" checkout -q -f FETCH_HEAD || return 0
  touch "$seen"
  for b in "$jr"/bundles/*.tgz; do [ -f "$b" ] && okname "$(basename "$b")" && [ ! -f "$ROOT/bundles/$(basename "$b")" ] && cp "$b" "$ROOT/bundles/"; done
  for j in "$jr"/jobs/*.job "$jr"/jobs/*.cancel; do
    [ -f "$j" ] || continue; local n; n=$(basename "$j"); okname "$n" || continue
    grep -qxF "$n" "$seen" && continue
    cp "$j" "$ROOT/jobs/$n" && echo "$n" >> "$seen"
  done
  [ -f "$jr/runner-next.sh" ] && ! grep -qxF "runner-$head" "$seen" && cp "$jr/runner-next.sh" "$ROOT/runner-next.sh" && echo "runner-$head" >> "$seen"
  echo "$head" > "$WORK/jobs-head"
}

strays() {   # kill whatever still runs from a bundle folder (no job runs between jobs, so nothing there should)
  local n=0 p c
  for p in /proc/[0-9]*; do
    c=$(readlink "$p/cwd" 2>/dev/null) || continue
    case "$c" in "$WORK/b/"*) local a; a=$(tr '\0' ' ' < "$p/cmdline" 2>/dev/null | cut -c1-120); kill -9 "${p#/proc/}" 2>/dev/null && n=$((n + 1)) && echo "$(date -Is) $1: killed ${p#/proc/} $a" >> "$ROOT/strays.log";; esac
  done
  echo $n
}

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
  export BD="$bd" BIN="$bin" OUT="$out" JRE="$JRE"
  [ -f "$bd/needs-java" ] && get_java
  local strayN; strayN=$(strays "before $job"); echo "strays killed before the job: $strayN" > "$res/runner.txt"
  setsid xargs -P "$CORES" -L 1 bash -c 'read -ra E < "$OUT/$0/settings"; cd "$BD" && env -i PATH=$JRE/bin:/usr/bin:/bin "${E[@]}" START=$1 nice -n 10 timeout 86400 "./$BIN" $2 > "$OUT/$0/p$(printf %06d $1).tmp" 2>/dev/null; mv "$OUT/$0/p$(printf %06d $1).tmp" "$OUT/$0/p$(printf %06d $1).txt"' < "$tasks" &
  local xp=$! paused=0 sid
  sleep 1; sid=$(ps -o sid= -p $xp 2>/dev/null | tr -d ' '); [ -n "$sid" ] || sid=$xp
  while kill -0 $xp 2>/dev/null; do
    local doneN; doneN=$(find "$out" -name 'p*.txt' | wc -l)
    status "job $job: $doneN of $total chunks done"; printf '%s of %s\n' "$doneN" "$total" > "$res/progress.txt"
    for r in "$out"/*/; do [ -f "$r/settings" ] && cat "$r"/p*.txt > "$res/$(basename "$r").txt" 2>/dev/null; done
    if [ -f "$ROOT/PAUSE" ]; then
      [ "$paused" = 1 ] || { pkill -STOP -s "$sid"; paused=1; echo "paused $(date -Is)" >> "$log"; }
      status "PAUSED job $job: $doneN of $total chunks done (delete PAUSE or run RESUME-sim.cmd to carry on)"
    elif [ "$paused" = 1 ]; then pkill -CONT -s "$sid"; paused=0; echo "resumed $(date -Is)" >> "$log"; fi
    if [ -f "$ROOT/STOP" ] || [ -f "$ROOT/jobs/$job.cancel" ]; then pkill -KILL -s "$sid"; echo "cancelled" >> "$log"; rm -f "$ROOT/jobs/$job.cancel"; fi
    pull_jobs
    sleep 15
  done
  pkill -KILL -s "$sid" 2>/dev/null; echo "strays killed after the job: $(strays "after $job")" >> "$res/runner.txt"
  for r in "$out"/*/; do [ -f "$r/settings" ] && cat "$r"/p*.txt > "$res/$(basename "$r").txt" 2>/dev/null; done
  echo "end $(date -Is)" >> "$log"; printf 'done\n' > "$res/progress.txt"; mv "$jf" "$ROOT/done/"
  report "$job"
}

setup_key; get_java
echo "strays killed at start: $(strays start)"
echo "PebxelSim runner: $CORES cores. Watching $ROOT/jobs. Leave this window open (minimise it is fine). Ctrl+C to stop."
while [ ! -f "$ROOT/STOP" ]; do
  status idle
  pull_jobs
  for jf in "$ROOT"/jobs/*.job; do [ -f "$jf" ] || continue; echo "$(date +%T) running $(basename "$jf")"; run_job "$jf"; echo "$(date +%T) finished"; [ -f "$ROOT/STOP" ] && break; done
  # a newer runner from Claude: take over between jobs
  if [ -f "$ROOT/runner-next.sh" ]; then mv "$ROOT/runner-next.sh" "$ROOT/runner.sh"; echo "$(date +%T) updated: restarting"; exec bash "$ROOT/runner.sh"; fi
  sleep 10
done
status stopped; rm -f "$ROOT/STOP"; echo "stopped"
