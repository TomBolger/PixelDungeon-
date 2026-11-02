#!/bin/sh
# Builds the Pixel Dungeon watchface from the watch app's own sources (nothing is forked: face/work is a fresh copy of ../watch
# with the face's package.json and the PD_FACE switch). Same rule as the app: always through watch/tools/build.sh, so the code
# overlays are linked to this app image. Output: face/pixel-dungeon-face.pbw
set -e
cd "$(dirname "$0")"
FACE=$(pwd); W=$FACE/work
export PD_ROOT=${PD_ROOT:-$FACE/../third_party/pixel-dungeon}
[ -f "$PD_ROOT/src/com/watabou/pixeldungeon/actors/mobs/Rat.java" ] || { echo "set PD_ROOT to a checkout of watabou/pixel-dungeon"; exit 1; }
mkdir -p "$W"
# fresh sources, but keep the build directory (a clean build takes minutes)
tar -C ../watch --exclude=./build --exclude='.lock-waf*' --exclude='ovl_*.bin' --exclude='keep_syms*.txt' --exclude=ovl_keep.auto.c -cf - . | tar -C "$W" -xf -
python3 - "$W" <<'PY'
import json, sys, re
w = sys.argv[1]
p = json.load(open(w + "/package.json"))
p["name"] = "pixel-dungeon-face"
p["pebble"]["displayName"] = "Pixel Dungeon Face"
p["pebble"]["uuid"] = "37f0622a-65c2-4eb6-b4ad-92181e5a899c"
p["pebble"]["watchapp"]["watchface"] = True
json.dump(p, open(w + "/package.json", "w"), indent=2)
s = open(w + "/wscript").read()
if "PD_FACE" not in s:
    s = s.replace("    ctx.load('pebble_sdk')\n\ndef build", "    ctx.load('pebble_sdk')\n    for e in ctx.all_envs.values(): e.append_value('DEFINES', 'PD_FACE')\n\ndef build", 1)
    open(w + "/wscript", "w").write(s)
PY
cd "$W"
rm -rf build/.conf_check* 2>/dev/null || true
[ -f build/c4che/emery_cache.py ] && grep -q PD_FACE build/c4che/emery_cache.py || { rm -rf build/c4che build/config.log; }
sh tools/build.sh
cp build/work.pbw "$FACE/pixel-dungeon-face.pbw" 2>/dev/null || cp build/*.pbw "$FACE/pixel-dungeon-face.pbw"
ls -l "$FACE/pixel-dungeon-face.pbw"
