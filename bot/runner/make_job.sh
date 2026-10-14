#!/bin/bash
# make_job.sh BUNDLE JOB GAMES CHUNK FIRST "run line"...
#   Packs the current bot build (build/deepbot + overlay libraries + game data) into BUNDLE.tgz and writes JOB.job,
#   both into $OUT (default: build/runner). Copy them into the runner's bundles\ and jobs\ folders.
#   Each run line is "name SETTING=value SETTING=value ...", e.g.
#     runner/make_job.sh b001 base 192 1 0 "base D=1 HS=0 RT=0 FK=0" "avoid AVOID=1 D=1 HS=0 RT=0 FK=0"
#   Bundles are cached by name on the runner, so give every new build a new bundle name.
set -e
cd "$(dirname "$0")/.."
[ $# -ge 6 ] || { sed -n 2,8p "$0"; exit 1; }
V=$1; J=$2; N=$3; C=$4; F=$5; shift 5
OUT=${OUT:-build/runner}; mkdir -p "$OUT"
[ -x build/deepbot ] || ./build.sh
T=$(mktemp -d); mkdir -p "$T/data"
cp build/deepbot build/libovl_*.so "$T/"
cp ../watch/resources/data/{text,mobs,item_defs}.bin "$T/data/"
tar czf "$OUT/$V.tgz" -C "$T" .; rm -rf "$T"
{ echo "# $J"; echo "bundle=$V.tgz"; echo "bin=deepbot"; echo "games=$N"; echo "chunk=$C"; echo "first=$F"; for r in "$@"; do echo "run $r"; done; } > "$OUT/$J.job"
echo "wrote $OUT/$V.tgz and $OUT/$J.job"; cat "$OUT/$J.job"
