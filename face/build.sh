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
import json, sys, re, os
w = sys.argv[1]
p = json.load(open(w + "/package.json"))
p["name"] = "pixel-dungeon-face"
p["pebble"]["displayName"] = "Pixel Dungeon Face"
p["pebble"]["uuid"] = "37f0622a-65c2-4eb6-b4ad-92181e5a899c"
p["pebble"]["watchapp"]["watchface"] = True
p["pebble"]["messageKeys"] = ["CFG", "WX", "CLK_ON", "CLK_FMT", "CLK_SIZE", "CLK_LINE", "DATE_ORD", "UNIT_F", "SL_DEPTH", "SL_KEYS", "SL_LVL", "SL_HP", "SL_GOLD", "VIBE", "CMD", "CTL_NEW", "CTL_TELE", "GOD", "REGEN", "HEAL", "UNBREAK", "LOOT", "HUNGER", "STAGE", "CLASSES"]
p["pebble"]["enableMultiJS"] = True
p["pebble"]["capabilities"] = ["location", "configurable", "health"]
# the face is silent (sfx.c): its music and sound data (about 125 KB) are replaced by one byte each (the resource names stay: the host tools list them)
for n in ("music_theme", "music_game", "sfx"): open(w + "/resources/data/" + n + ".bin", "wb").write(b"\0")
# the face's own overlay (face/ovl/fstage: the starting stage's setup), a resource after the app's: the normal app has neither
m = p["pebble"]["resources"]["media"]
if not any(x["name"] == "OVL_FSTAGE" for x in m): m.append({"type": "raw", "name": "OVL_FSTAGE", "file": "data/ovl_fstage.bin", "targetPlatforms": ["emery", "gabbro"]})
json.dump(p, open(w + "/package.json", "w"), indent=2)
s = open(w + "/wscript").read()
if "js_entry_file" not in s:                              # the face carries the phone-side JS (src/pkjs); the app's own bundle has none
    s = s.replace("ctx.pbl_bundle(binaries=binaries)", "ctx.pbl_bundle(binaries=binaries, js=ctx.path.ant_glob(['src/pkjs/**/*.js', 'src/pkjs/**/*.json']), js_entry_file='src/pkjs/index.js')")
if "PD_FACE" not in s:
    s = s.replace("    ctx.load('pebble_sdk')\n\ndef build", "    ctx.load('pebble_sdk')\n    for e in ctx.all_envs.values(): e.append_value('DEFINES', ['PD_FACE'] + os.environ.get('FACE_DEFINES', '').split())\n\ndef build", 1)
    open(w + "/wscript", "w").write(s)
PY
mkdir -p "$W/src/pkjs" && cp "$FACE"/pkjs/*.js "$W/src/pkjs/"
cp -r "$FACE"/ovl/* "$W/src/ovl/"
cd "$W"
# The settings page is the pebble-clay package (phone-side JS only). It is not declared as a Pebble package (its manifest lacks
# gabbro, and it carries no watch code anyway): its ready-made script is fetched from npm and bundled as src/pkjs/clay.js.
CL="$FACE/work-clay"; [ -f "$CL/node_modules/pebble-clay/dist.zip" ] || { mkdir -p "$CL"; (cd "$CL" && npm init -y >/dev/null && npm install --no-audit --no-fund pebble-clay@1.0.4 >/dev/null); }
[ -f "$CL/node_modules/pebble-clay/dist/js/index.js" ] || (cd "$CL/node_modules/pebble-clay" && python3 -c "import zipfile; zipfile.ZipFile('dist.zip').extractall('dist')")
cp "$CL/node_modules/pebble-clay/dist/js/index.js" "$W/src/pkjs/clay.js"
grep -q MESSAGE_KEY_CLASSES build/include/message_keys.auto.h 2>/dev/null || rm -rf build/include build/js   # (a stale key header from before the settings existed)
rm -rf build/.conf_check* 2>/dev/null || true
# (the defines live in the configure cache: a new FACE_DEFINES means a new configure)
STAMP="PD_FACE ${FACE_DEFINES:-}"; [ "$(cat build/.defs 2>/dev/null)" = "$STAMP" ] || { rm -rf build/c4che build/config.log; mkdir -p build; echo "$STAMP" > build/.defs; }
sh tools/build.sh
cp build/work.pbw "$FACE/pixel-dungeon-face.pbw" 2>/dev/null || cp build/*.pbw "$FACE/pixel-dungeon-face.pbw"
ls -l "$FACE/pixel-dungeon-face.pbw"
