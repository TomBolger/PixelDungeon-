#!/bin/bash
# emu_install.sh <pbw> [emery|gabbro] [wipe]: start a headless emulator (Xvfb, for Linux servers without a screen) and
# install the pbw, retrying if the emulator is slow to come up. "wipe" erases the emulators' storage (saves) first.
# The emulator never reclaims the room an installed app took: after about 32 installs of a ~1.3 MB app its 32 MB of
# storage is full and every install fails ("Failed" on the screen, even after a restart). Then the storage is wiped
# and the install tried again. A real watch is not affected.
export DISPLAY=:99 PATH=$HOME/.local/bin:$PATH
PBW=$1; P=emery; WIPE=
for a in "$2" "$3"; do case "$a" in emery|gabbro) P=$a ;; wipe) WIPE=1 ;; esac; done
pgrep -x Xvfb >/dev/null || { setsid nohup Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 & sleep 2; }
restart() { pgrep -f "qemu-system|qemu-pebble" >/dev/null && pebble kill >/dev/null 2>&1 || rm -f /tmp/pb-emulator.json; sleep 2; }
[ -n "$WIPE" ] && { restart; pebble wipe >/dev/null 2>&1; }
for try in 1 2 3 4; do
  timeout 170 pebble install --emulator $P "$PBW" > /tmp/inst.log 2>&1
  grep -q "succeeded" /tmp/inst.log && { echo installed; exit 0; }
  if grep -q "install failed" /tmp/inst.log; then
    echo "the emulator's storage is full: wiping it" >&2; restart; pebble wipe >/dev/null 2>&1
  elif ! grep -q "Connection refused" /tmp/inst.log; then restart; fi
  sleep 2
  pgrep -x Xvfb >/dev/null || { setsid nohup Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 & sleep 2; }
done
echo "install failed"; tail -3 /tmp/inst.log; exit 1
