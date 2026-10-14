#!/bin/sh
# Full build: texts, app, overlays linked against the app, then the app again so the bundle carries
# the fresh overlays. The second pass must leave the app's code unchanged (checked).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
LOG=build/logs; mkdir -p $LOG
python3 tools/mobs.py text
python3 tools/build_text.py
python3 tools/mobs.py bin
python3 tools/stub/gen_res.py && gcc -std=c99 -I tools/stub -I src/c -o build/pd_dump_defs tools/dump_defs.c && build/pd_dump_defs resources/data/item_defs.bin
for o in $(ls src/ovl); do for p in emery gabbro; do [ -f resources/data/ovl_$o~$p.bin ] || printf 'none' > resources/data/ovl_$o~$p.bin; done; done
# the keep table (app symbols the overlays call) comes first; compiling overlays needs the generated headers
if [ ! -f build/emery/src/resource_ids.auto.h ] || [ ! -f src/c/ovl_keep.auto.c ] || [ package.json -nt build/emery/src/resource_ids.auto.h ]; then
  echo '// kept by linker flags' > src/c/ovl_keep.auto.c
  pebble build >$LOG/pass0.log 2>&1 || true
fi
python3 tools/build_ovl.py --keep || [ $? -eq 3 ]
pebble build >$LOG/pass1.log 2>&1 || { grep -E "error|Exception" $LOG/pass1.log; exit 1; }
sha1sum build/emery/pebble-app.elf build/gabbro/pebble-app.elf > $LOG/app.sha
set +e; python3 tools/build_ovl.py; rc=$?; set -e
[ $rc -ne 0 ] && [ $rc -ne 3 ] && exit 1
pebble build >$LOG/pass2.log 2>&1 || { grep -E "error|Exception" $LOG/pass2.log; exit 1; }
sha1sum -c --quiet $LOG/app.sha || { echo "app image changed between passes: overlays would not match"; exit 1; }
grep -E "Free RAM|load size" $LOG/pass2.log | head -2
echo "build OK"
