#!/bin/bash
# emu_shot.sh <platform> <name> [buttons...]: press buttons in the emulator, then save a screenshot as <name>.png
# Buttons: select, up, down, back; "L" = long-press select, "LU"/"LD" = long-press up/down. SHOTS=dir picks the folder.
export PATH=$HOME/.local/bin:$PATH
S=${SHOTS:-.}; P=$1; N=$2; shift 2
for b in "$@"; do
  case $b in
    L) pebble emu-button --emulator $P push select >/dev/null 2>&1; sleep 0.9; pebble emu-button --emulator $P release select >/dev/null 2>&1;;
    LD) pebble emu-button --emulator $P push down >/dev/null 2>&1; sleep 0.9; pebble emu-button --emulator $P release down >/dev/null 2>&1;;
    LU) pebble emu-button --emulator $P push up >/dev/null 2>&1; sleep 0.9; pebble emu-button --emulator $P release up >/dev/null 2>&1;;
    *) pebble emu-button --emulator $P click $b >/dev/null 2>&1;;
  esac
  sleep 0.5
done
sleep 1.5
timeout 30 pebble screenshot --emulator $P --no-open $S/$N.png >/dev/null 2>&1 && echo "shot $N"
