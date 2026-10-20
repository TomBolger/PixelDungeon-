#!/bin/bash
# Staged views for visual checks (see server/src/pebxel/Gallery.java). Args: picture-folder [stage names]
cd "$(dirname "$0")"
H=$PWD
cd spd/core/src/main/assets && exec java -cp "$H/build/server:$H/build/spd:$H/lib/*" pebxel.Gallery "$@"
