#!/bin/sh
# Build the headless original Pixel Dungeon 1.9 (Java) used to cross-check the C port.
# Sources come from the third_party submodules, with headless.patch applied (see PATCHES.md).
# Output: build/classes. Run with:  java -cp build/classes sim.Sim <scenario> <runs>
set -e
cd "$(dirname "$0")"
ROOT=../..
PD="$ROOT/third_party/pixel-dungeon"
PDC="$ROOT/third_party/pd-classes"
[ -d "$PD/src/com" ] && [ -d "$PDC/com" ] || { echo "Missing submodules: run  git submodule update --init  first"; exit 1; }
rm -rf build && mkdir -p build/src build/classes
cp -r "$PD/src/com" build/src/
cp -r "$PDC/com/." build/src/com/
(cd build/src && patch -s -p1 < ../../headless.patch)
find stubs build/src sim -name '*.java' > build/sources.txt
javac -nowarn -encoding UTF-8 -d build/classes @build/sources.txt 2>&1 | grep -v '^Note:\|JAVA_TOOL_OPTIONS' || true
test -f build/classes/sim/Sim.class && echo "build OK"
