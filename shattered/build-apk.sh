#!/bin/bash
# Builds the phone app (Shattered with the watch link) into dist/. Needs the Android SDK (ANDROID_HOME).
set -e
cd "$(dirname "$0")"
[ -d spd ] || ./fetch-spd.sh
[ -f spd/local.properties ] || echo "sdk.dir=${ANDROID_HOME:-/opt/android-sdk}" > spd/local.properties
python3 tools/convert_art.py -f > /dev/null   # (always afresh: the hand colour fixes live in the converter; a stale build/art once rolled them back)
python3 tools/pack_art.py > /dev/null
(cd spd && ./gradlew --no-daemon -q :android:assembleDebug)
mkdir -p dist
cp spd/android/build/outputs/apk/debug/android-debug.apk dist/shattered-pebxel-debug.apk
echo "dist/shattered-pebxel-debug.apk"
