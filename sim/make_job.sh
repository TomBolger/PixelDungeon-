#!/bin/bash
# make_job.sh JOB BUNDLE.tgz GAMES CHUNK FIRST "run line"... : writes sim/out/JOB.job for the runner.
#   A run line is "name SETTING=value ...", e.g. "base MODE=1 MAXDEPTH=26".
set -e; cd "$(dirname "$0")"; mkdir -p out
J=$1 B=$(basename "$2") N=$3 C=$4 F=$5; shift 5
{ echo "# $J"; echo "bundle=$B"; echo "bin=simbot"; echo "games=$N"; echo "chunk=$C"; echo "first=$F"; for r in "$@"; do echo "run $r"; done; } > "out/$J.job"
echo "out/$J.job"
