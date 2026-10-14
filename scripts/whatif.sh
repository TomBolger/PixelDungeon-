#!/bin/bash
# whatif.sh <label> <blob>: build "PD What-If", the test kit as its own app that starts from <blob>
# (a frozen bot game, written by the bot's checkpoint switches) whenever it has no save of its own.
#   -> dist/pixel-dungeon-<label>-whatif.pbw
set -e -o pipefail
L=${1:?usage: scripts/whatif.sh <label> <blob>}; BLOB=$(realpath "${2:?usage: scripts/whatif.sh <label> <blob>}")
ROOT=$(cd "$(dirname "$0")/.." && pwd); W=$ROOT/watch; D=$ROOT/dist; mkdir -p "$D"
cd "$W"
grep -q "^#define TEST_BUILD" src/c/pd.h && { echo "src/c/pd.h still defines TEST_BUILD"; exit 1; }
BK=$(mktemp -d); cp package.json src/c/pd.h src/ovl/ui/ui.c "$BK/"
restore() { cp "$BK/package.json" package.json; cp "$BK/pd.h" src/c/pd.h; cp "$BK/ui.c" src/ovl/ui/ui.c; rm -f resources/data/whatif.bin; rm -rf "$BK"; }
trap restore EXIT
cp "$BLOB" resources/data/whatif.bin
python3 - <<'PY'
import json
p = 'package.json'; j = json.load(open(p))
j['pebble']['uuid'] = '6a37c89f-b93c-4210-921d-59710f0dc0ff'; j['pebble']['displayName'] = 'PD What-If'
j['pebble']['resources']['media'].append({"type": "raw", "name": "WHATIF", "file": "data/whatif.bin"})
json.dump(j, open(p, 'w'), indent=2)
PY
sed -i 's|#include "text_ids.h"|#include "text_ids.h"\n#define TEST_BUILD\n#define WHATIF|' src/c/pd.h
sed -i "s/\"v[0-9.]*\"/\"v$L WHAT-IF\"/" src/ovl/ui/ui.c
./tools/build.sh | tail -1
cp build/watch.pbw "$D/pixel-dungeon-$L-whatif.pbw"
ls -la "$D/pixel-dungeon-$L-whatif.pbw"
