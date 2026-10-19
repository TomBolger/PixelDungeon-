#!/bin/bash
# Runs the screenless game for the watch (see tools/bridge.py). NEWGAME=0..5 skips straight into a new game.
cd "$(dirname "$0")"
H=$PWD
cd spd/core/src/main/assets && exec java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.Server "$@"
