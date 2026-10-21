#!/bin/bash
# submit.sh [--runner] [bundle.tgz] [job.job...]: puts a bot bundle and job files on the sim-jobs branch, which the
# PebxelSim runner on Monolith polls (sim/runner.sh pull_jobs). The branch is one commit, force-pushed (only the newest
# few bundles kept), so it never grows the repo's history. --runner also ships sim/runner.sh as runner-next.sh.
set -e
D=/home/claude/sim-jobs; R=https://github.com/TomBolger/PixelDungeon-.git
cd "$(dirname "$0")"; HERE=$PWD
if [ ! -d "$D/.git" ]; then rm -rf "$D"; mkdir -p "$D"; git -C "$D" init -q; git -C "$D" remote add origin "$R"; fi
cd "$D"; git fetch -q --depth 1 origin sim-jobs 2>/dev/null && git checkout -q -f FETCH_HEAD 2>/dev/null || true
mkdir -p bundles jobs; rm -f runner-next.sh
for a in "$@"; do
  case "$a" in
    --runner) cp "$HERE/runner.sh" runner-next.sh;;
    *.tgz) cp "$HERE/$a" bundles/ 2>/dev/null || cp "$a" bundles/;;
    *.job|*.cancel) cp "$HERE/$a" jobs/ 2>/dev/null || cp "$a" jobs/;;
  esac
done
ls -t bundles/*.tgz 2>/dev/null | tail -n +4 | xargs -r rm -f   # (the newest three bundles)
ls -t jobs/* 2>/dev/null | tail -n +41 | xargs -r rm -f
printf 'PebxelSim jobs for the runner on Monolith (sim/runner.sh). Replaced on every submit.\n' > README.md
git checkout -q --orphan new 2>/dev/null || git checkout -q -B new
git add -A; git -c user.name=Claude -c user.email=noreply@anthropic.com commit -q -m "sim jobs $(date -Is)"
git push -q -f origin HEAD:sim-jobs 2>&1 | grep -v "moved\|new location\|^remote: *$" || true
git ls-remote origin refs/heads/sim-jobs | cut -c1-12
