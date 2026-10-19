#!/bin/bash
# Plays Shattered PD with no screen using a simple explorer. Args: class(0-3) last-floor frames [resume]
cd "$(dirname "$0")"
H=$PWD
cd spd/core/src/main/assets && exec java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.Smoke "$@"
