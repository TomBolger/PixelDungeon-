#!/bin/bash
# package.sh <version>: build the release app and the test kit into dist/
#   dist/pixel-dungeon-<version>.pbw          the game ("Pixel Dungeon")
#   dist/pixel-dungeon-<version>-testkit.pbw  a separate app ("PD Test Kit": own save, test build features on)
# The release build is redone last, so watch/build/ always ends up holding the plain release.
set -e -o pipefail
V=${1:?usage: scripts/package.sh <version, e.g. 0.56>}
ROOT=$(cd "$(dirname "$0")/.." && pwd); W=$ROOT/watch; D=$ROOT/dist; mkdir -p "$D"
cd "$W"
grep -q "^#define TEST_BUILD" src/c/pd.h && { echo "src/c/pd.h still defines TEST_BUILD: refusing to package"; exit 1; }
grep -q "\"v$V\"" src/ovl/ui/ui.c || echo "note: the title screen version string in src/ovl/ui/ui.c is not v$V"
./tools/build.sh | tail -1
cp build/watch.pbw "$D/pixel-dungeon-$V.pbw"
# test kit: a different app id and name, TEST_BUILD on; the edited files are put back however the script ends
BK=$(mktemp -d); cp package.json src/c/pd.h src/ovl/ui/ui.c "$BK/"
restore() { cp "$BK/package.json" package.json; cp "$BK/pd.h" src/c/pd.h; cp "$BK/ui.c" src/ovl/ui/ui.c; rm -rf "$BK"; }
trap restore EXIT
sed -i 's/6a37c89f-b93c-4210-921d-59710f0dcf1f/6a37c89f-b93c-4210-921d-59710f0dc0de/; s/"displayName": "Pixel Dungeon"/"displayName": "PD Test Kit"/' package.json
sed -i 's|#include "text_ids.h"|#include "text_ids.h"\n#define TEST_BUILD|' src/c/pd.h
sed -i "s/\"v$V\"/\"v$V TEST\"/" src/ovl/ui/ui.c
./tools/build.sh | tail -1
cp build/watch.pbw "$D/pixel-dungeon-$V-testkit.pbw"
restore; trap - EXIT
./tools/build.sh | tail -1
ls -la "$D" | grep -- "-$V"
