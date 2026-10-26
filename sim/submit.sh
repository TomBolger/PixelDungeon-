#!/bin/bash
# submit.sh [--runner] [bundle.tgz] [job.job...]: puts a bot bundle and job files on the sim-jobs branch, which the
# PebxelSim runner on Monolith polls (sim/runner.sh pull_jobs). The branch is one commit, force-pushed (only the newest
# few bundles kept), so it never grows the repo's history. --runner also ships sim/runner.sh as runner-next.sh.
set -e
R=https://github.com/TomBolger/PixelDungeon-.git
cd "$(dirname "$0")"; HERE=$PWD
# (several agents submit at once: one at a time on this machine (flock), each onto the branch as it is now,
#  pushed with a lease on that state so a submit from elsewhere in between isn't wiped; on a lost lease, try again)
exec 9>/tmp/sim-submit.lock; flock 9
ARGS=("$@")
for try in 1 2 3 4 5; do
  D=/home/claude/sim-jobs   # (kept between submits so a fetch brings only what's new; the lock keeps it to one submit at a time)
  if [ ! -d "$D/.git" ]; then rm -rf "$D"; mkdir -p "$D"; git -C "$D" init -q; git -C "$D" remote add origin "$R"; fi
  cd "$D"; base=""; git checkout -q -f --detach 2>/dev/null || true; git clean -q -fdx
  if git fetch -q --depth 1 origin sim-jobs 2>/dev/null; then base=$(git rev-parse FETCH_HEAD); git checkout -q -f FETCH_HEAD; fi
  mkdir -p bundles jobs; rm -f runner-next.sh
  for a in "${ARGS[@]}"; do
    case "$a" in
      --runner) cp "$HERE/runner.sh" runner-next.sh;;
      *.tgz) cp "$HERE/$a" bundles/ 2>/dev/null || cp "$a" bundles/;;
      *.job|*.cancel) cp "$HERE/$a" jobs/ 2>/dev/null || cp "$a" jobs/;;
    esac
  done
  ls -t bundles/*.tgz 2>/dev/null | tail -n +9 | xargs -r rm -f   # (the newest eight bundles: several agents submit)
  ls -t jobs/* 2>/dev/null | tail -n +61 | xargs -r rm -f
  printf 'PebxelSim jobs for the runner on Monolith (sim/runner.sh). Replaced on every submit.\n' > README.md
  git branch -q -D new 2>/dev/null || true; git checkout -q --orphan new   # (one commit, no history: old bundles never pile up in the repo)
  git add -A; git -c user.name=Claude -c user.email=noreply@anthropic.com commit -q -m "sim jobs $(date -Is)"
  if git push -q --force-with-lease=sim-jobs:$base origin HEAD:sim-jobs 2>/tmp/sim-submit.err; then
    git ls-remote origin refs/heads/sim-jobs | cut -c1-12; exit 0
  fi
  grep -v "moved\|new location\|^remote: *$" /tmp/sim-submit.err || true; echo "submit: the branch moved meanwhile; again ($try)"; sleep 3
done
echo "submit: gave up after 5 tries"; exit 1
