#!/bin/bash
# Scripted watch button presses with pictures (see server/src/pebxel/Buttons.java)
cd "$(dirname "$0")"
H=$PWD
cd spd/core/src/main/assets && exec java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.Buttons "$@"
