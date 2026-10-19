#!/bin/bash
# Compiles Shattered PD's game code plus our screenless watch server. Needs Java 11+.
set -e
cd "$(dirname "$0")"
[ -d spd ] || ./fetch-spd.sh
CP="lib/*"
if [ ! -d build/spd ]; then
  mkdir -p build/spd
  find spd/SPD-classes/src/main/java spd/core/src/main/java spd/services/src/main/java \
    spd/services/news/debugNews/src spd/services/updates/debugUpdates/src -name '*.java' > build/spd.txt
  javac -nowarn -encoding UTF-8 --release 11 -cp "$CP" -d build/spd @build/spd.txt
fi
rm -rf build/server && mkdir -p build/server
javac -nowarn -encoding UTF-8 --release 11 -cp "build/spd:$CP" -d build/server $(find server/src -name '*.java')
echo built
