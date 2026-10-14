#!/bin/sh
# record_games.sh [N] [FIRST] [LABEL]: play N bot games (game numbers FIRST..FIRST+N-1, normal rules: no head start,
# no second lives) with the current bot, and pack them into www/games/ for the replay site.
#   tools/record_games.sh 48 0 "Oct 13"
set -e
cd "$(dirname "$0")/.."
N=${1:-48}; FIRST=${2:-0}; LABEL=${3:-$(date +"%b %d")}
../bot/build.sh >/dev/null
REC=$(mktemp -d)
seq $FIRST $((FIRST + N - 1)) | xargs -P "$(nproc)" -I{} sh -c \
  'START={} REC='"$REC"'/g{}.jsonl D=1 HS=0 RT=0 FK=0 ../bot/build/deepbot $(({} + 1)) > '"$REC"'/g{}.out 2>&1'
rm -rf www/games && python3 tools/make_index.py "$REC" www/games "$LABEL"
rm -rf "$REC"
