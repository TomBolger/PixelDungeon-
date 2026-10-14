"""Pack PD's music and sound effects for the watch (0.6.6).

Music: tools/audio/<track>.json (made once by tools/audio/transcribe.py) becomes an event list for the
synth in src/ovl/snd: resources/data/music_<track>.bin
  header: "PDM1", u16 events, u16 pluck (click at each note's start, x/256 of its loudness), u32 loop length in blocks (32 samples), int8 wavetables[4][64],
          u16 decay per block (Q16) [4], u8 attack blocks [4]
  events: 4 bytes: wait (blocks since the previous event), code (1..127 note, 128 + drum kind,
          0 = nothing, just waiting), length (16 ms units), velocity
Sound effects: PD's own samples, 8 kHz, trimmed, IMA ADPCM (4 bits a sample): resources/data/sfx.bin
  header: "PDS1", u16 count, u16 0, then per sound: u32 offset, u32 samples, i16 first sample, u8 step
          index, u8 0; then the data (low nibble first)
Also writes src/c/sfx_ids.auto.h (SND_* in the order below).

Usage: python3 tools/build_audio.py <pixel-dungeon/assets>
"""
import json, math, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ASSETS = sys.argv[1] if len(sys.argv) > 1 else PD_ASSETS
SR, BLOCK = 8000, 32
SFX_RMS = 0.27                                  # each effect's loudest 50 ms, about the music's level
MAX_S = 1.6                                     # sound effects are cut (with a fade) after this many seconds
TRACKS = ["theme", "game"]
SFX = ["click", "step", "water", "hit", "miss", "gold", "item", "door_open", "descend", "levelup", "death", "drink", "eat",
       "read", "alert", "trap", "shatter", "burning", "secret", "puff", "falling", "lightning", "challenge", "lullaby", "blast",
       "meld", "cursed", "degrade", "evoke", "unlock", "tomb", "bones", "dewdrop", "teleport", "zap", "ray", "rocks", "plant",
       "ghost", "boss", "beacon", "bee", "charms", "mimic", "mastery"]


def wavetable(h):
    t = np.arange(64) / 64
    w = sum(a * np.sin(2 * np.pi * (k + 1) * t) for k, a in enumerate(h))
    return np.round(w / np.abs(w).max() * 127).astype(np.int8)


def pack_music(name):
    d = json.load(open(os.path.join(ROOT, "tools", "audio", f"{name}.json")))
    prof = [d["profile"][str(r)] for r in range(3)]
    # band-limited per register: a harmonic above 4 kHz folds back as a stray tone that beats (warbles)
    tables = [wavetable(prof[0]), wavetable(prof[1]), wavetable(prof[2][:3]), wavetable(d.get("lead_profile", prof[2][:2]))]   # table 3: the lead (and notes 84+)
    tau = list(d["tau"]) + [d.get("lead_tau", d["tau"][2])]
    decay = [min(65535, round(65536 * math.exp(-BLOCK / (t * SR)))) for t in tau]
    att = list(d.get("attack", (0.004,) * 3)); att.append(d.get("lead_attack", att[2]))
    attack = [max(1, round(a * SR / BLOCK)) for a in att]
    notes = sorted(map(tuple, d["notes"]))       # (start s, end s, code, amplitude[, damped])
    nxt = {}                                       # a short gap before the same pitch again: hold through it (no on-off pulse)
    for i in range(len(notes) - 1 if not d.get("no_hold") else -1, -1, -1):
        s, e, p, a = notes[i][:4]
        if p in nxt and 0 <= nxt[p] - e < 0.12: notes[i] = (s, nxt[p], p, a) + notes[i][4:]
        nxt[p] = s
    d["notes"] = notes
    amax = max((n[3] for n in d["notes"]), default=1)
    dmp = d.get("damp_tau")                       # 0.43: notes flagged damped (5th field) decay with this time constant: odd velocity
    ev = [(round(n[0] * SR / BLOCK), n[2], max(1, round((n[1] - n[0]) * 62.5)), min(255, round(255 * n[3] / amax * d.get("vel_scale", 1)))) for n in d["notes"]]
    if dmp: ev = [(b, c, l, (v & ~1) | (1 if len(n) > 4 and n[4] else 0)) for (b, c, l, v), n in zip(ev, d["notes"])]
    ev += [(round(t * SR / BLOCK), 128 + k, 1, min(255, round(255 * a))) for t, a, k in d["drums"]]
    ev.sort()
    if dmp: ev.insert(0, (0, 134, 1, round((65535 - 65536 * math.exp(-BLOCK / (dmp * SR))) / 16)))   # before anything at time 0
    out, last = [], 0
    for b, code, ln, vel in ev:
        wait = b - last
        while wait > 255: out.append((255, 0, 0, 0)); wait -= 255
        out.append((wait, code, min(255, ln), vel)); last = b
    loop = round(d["length"] * SR / BLOCK)
    data = b"PDM1" + struct.pack("<HHI", len(out), round(d.get("pluck", 0) * 256), loop)
    data += b"".join(t.tobytes() for t in tables) + struct.pack("<4H", *decay) + bytes(attack)
    data += b"".join(struct.pack("<4B", *e) for e in out)
    open(os.path.join(ROOT, "resources", "data", f"music_{name}.bin"), "wb").write(data)
    print(f"music {name}: {len(out)} events, {len(data)} B")


STEPS = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
         130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
         1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
         8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm(x):
    """IMA ADPCM; returns (first sample, step index, nibbles). Decoder: src/ovl/snd/snd.c."""
    pred, idx, out = int(x[0]), 20, []
    for s in x:
        step = STEPS[idx]; diff = int(s) - pred; code = 0
        if diff < 0: code = 8; diff = -diff
        dq = step >> 3
        if diff >= step: code |= 4; diff -= step; dq += step
        if diff >= step >> 1: code |= 2; diff -= step >> 1; dq += step >> 1
        if diff >= step >> 2: code |= 1; dq += step >> 2
        pred = max(-32768, min(32767, pred - dq if code & 8 else pred + dq))
        idx = max(0, min(88, idx + IDX[code & 7]))
        out.append(code)
    return int(x[0]), 20, out


def pack_sfx():
    import librosa
    heads, blob = [], b""
    for n in SFX:
        y, _ = librosa.load(os.path.join(ASSETS, f"snd_{n}.mp3"), sr=SR, mono=True)
        pk = np.abs(y).max()
        on = np.where(np.abs(y) > pk * 0.05)[0]                                   # quiet tails are lost on the speaker anyway
        y = y[on[0]:min(on[-1] + 1, on[0] + int(MAX_S * SR))]                    # long ones (descend, challenge...) fade early
        f = min(len(y), int(0.25 * SR) if len(y) >= MAX_S * SR - 1 else int(0.02 * SR)); y[-f:] *= np.linspace(1, 0, f) ** 2
        w = len(y) // 400 * 400; lw = np.sqrt((y[:w].reshape(-1, 400) ** 2).mean(1)).max() if w else np.sqrt((y ** 2).mean())   # loudest 50 ms
        y = y * min(6.0, 0.9 / pk, SFX_RMS / max(lw, 1e-6))                       # 0.40: by loudness, not peak (item was 2x the music)
        x = np.clip(np.round(y * 32767), -32768, 32767).astype(np.int16)
        first, idx, nib = adpcm(x)
        if len(nib) % 2: nib.append(0)
        data = bytes(nib[i] | nib[i + 1] << 4 for i in range(0, len(nib), 2))
        heads.append((len(blob), len(x), first, idx)); blob += data
    table_len = 8 + 12 * len(SFX)
    out = b"PDS1" + struct.pack("<HH", len(SFX), 0)
    out += b"".join(struct.pack("<IIhBB", o + table_len, n, f, i, 0) for o, n, f, i in heads) + blob
    open(os.path.join(ROOT, "resources", "data", "sfx.bin"), "wb").write(out)
    print(f"sfx: {len(SFX)} sounds, {sum(h[1] for h in heads) / SR:.1f} s, {len(out)} B")
    hdr = ["// generated by tools/build_audio.py: PD's sound effects (Assets.SND_*), in sfx.bin order", "#pragma once",
           "enum { " + ", ".join(f"SND_{n.upper()}" for n in SFX) + ", SND_COUNT };"]
    open(os.path.join(ROOT, "src", "c", "sfx_ids.auto.h"), "w").write("\n".join(hdr) + "\n")


for t in TRACKS: pack_music(t)
pack_sfx()
