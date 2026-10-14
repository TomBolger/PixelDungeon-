#!/bin/bash
# emu_install.sh <pbw> [wipe]: start a headless Emery emulator (Xvfb, for Linux servers without a screen) and install the
# pbw, retrying if the emulator is slow to come up. "wipe" erases the emulator's storage (saves) first.
export DISPLAY=:99 PATH=$HOME/.local/bin:$PATH
pgrep -x Xvfb >/dev/null || { setsid nohup Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 & sleep 2; }
for try in 1 2 3; do
  if [ "$2" = wipe ] || [ $try -gt 1 ]; then pgrep -f "qemu-system" >/dev/null && pebble kill >/dev/null 2>&1 || rm -f /tmp/pb-emulator.json; sleep 2; [ "$2" = wipe ] && pebble wipe >/dev/null 2>&1; fi
  timeout 170 pebble install --emulator emery "$1" > /tmp/inst.log 2>&1 && grep -q "succeeded" /tmp/inst.log && { echo installed; exit 0; }
  pgrep -x Xvfb >/dev/null || { setsid nohup Xvfb :99 -screen 0 1280x1024x24 >/dev/null 2>&1 & sleep 2; }
done
echo "install failed"; tail -3 /tmp/inst.log; exit 1
