#!/usr/bin/env python3
"""whatif_blob.py CHECKPOINT OUT: turn a bot checkpoint into a blob for the PD What-If build (scripts/whatif.sh).

Checkpoints come from the bot with CAMPDIR=dir (one per new floor: dir/gGGGG_dDD.bin). A checkpoint starts with the
host's persistent storage, 256 keys of {bool used; int len; uint8_t d[256]} (bot/src/pebble.h). The blob holds the
keys in use as records [key u8][len u16 little-endian][data], ended by key 255; the What-If build writes them into
the watch's storage when it has no save of its own (watch/src/c/main.c, WHATIF)."""
import struct, sys
src, out = sys.argv[1], sys.argv[2]
raw = open(src, 'rb').read()
REC = 4 + 4 + 256                      # bool + padding, int len, data
blob = bytearray()
for k in range(255):
    used, n = struct.unpack_from('<?3xi', raw, k * REC)
    if used and n > 0: blob += struct.pack('<BH', k, n) + raw[k * REC + 8: k * REC + 8 + n]
blob.append(255)
open(out, 'wb').write(blob)
print(f'{out}: {len(blob)} bytes')
