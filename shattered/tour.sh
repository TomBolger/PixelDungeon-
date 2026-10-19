#!/bin/bash
# A picture of every main screen and window as the watch shows it. Args: picture-folder [names]
cd "$(dirname "$0")"
H=$PWD
cd spd/core/src/main/assets && exec java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.Tour "$@"
