"""0.6.11: PD's game music from its score (a Songsterr tab, tools/audio/game_tab.txt) instead of a transcription.

  python3 tools/audio/from_tab.py        -> tools/audio/game.json (then tools/build_audio.py packs it)

Parts -> synth voices (src/ovl/snd): lead guitar = the lead voice (25% pulse, instant attack, quick decay to a
sustain level; 'v' notes with vibrato, 's' staccato, 'h' hammer-on/pull-off without a new pluck, 'T' tied onto the
previous note); rhythm guitar = palm-muted power chords (root + fifth, short, dark) and the off-beat stab (top 3
notes, brighter); bass = a soft square, a whole note per measure; drums: 35/36 kick, 38 snare, 46 open hat, 52/57
cymbal, ghost notes quieter. 180 bpm: an 8th is 1/6 s. The recording loops after 54 measures (72 s: the tab's 53
plus a measure of rest) and starts 58 ms in (mp3 padding; measured by onset cross-correlation, no drift over the loop); our time 0 is the tab's tick 0.
"""
import json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
TICK = 1 / 1440                                   # s per tick (480 a quarter at 180 bpm)
M = 1920                                          # ticks a measure
LOOP = 54 * M * TICK

notes, drums = [], []                             # (start s, end s, code, amplitude), (time s, amplitude, kind)
def note(t0, dur, code, a, damp=0): notes.append([t0 * TICK, (t0 + dur) * TICK, code, a] + ([1] if damp else []))

# ---- part 0: the lead
OUTL = 0.35
for ln in open(os.path.join(HERE, "game_tab.txt")):
    if ln.startswith("## Part 1"): break
    f = ln.split()
    if len(f) < 3 or not f[0].isdigit(): continue
    t0, dur = int(f[0]), int(f[1])
    ps = [(int(re.match(r"\d+", x).group()), re.sub(r"\d+", "", x)) for x in f[2].split(",")]
    ps.sort(reverse=True)
    for i, (p, fl) in enumerate(ps):
        if "T" in fl:                             # tied: the previous note of this pitch goes on
            for n in reversed(notes):
                if n[2] % 40 == p % 40 and abs(n[1] - t0 * TICK) < 1e-6: n[1] = (t0 + dur) * TICK; break
            continue
        d = dur // 2 if "s" in fl else dur
        o = OUTL if t0 >= 50 * M else 1               # 0.43: the closing chord (measures 50-51) much softer: it swelled
        if i == 0:
            note(t0, d, p + (160 if "v" in fl and d >= 720 else 120), (0.63 if "h" in fl else 1.0) * o)   # (vibrato on held notes only)
            if "G" in fl: drums.append((t0 * TICK, 0.0, 5))   # marker (code 133): the next lead note glides down a half step
        else: note(t0, d, p, 0.5 * o)             # a chord in the lead part: its lower notes as guitar
        # (0.45: the extra note after the staccato phrase, added in 0.43, is gone again: the owner wants the score as written)
        if fl == "h" and p == 63 and i == 0:     # ... and slides into the next phrase (63, and its 70) from a half step below
            for k in (0, 480): drums.append(((t0 + k) * TICK, 0x0F / 255, 5))
# glide markers (code 133): velocity = steps of 8 blocks << 4 | semitones (signed 4 bits; + from above, - from below; 0: 1 from above in 80 ms)
# (the tie search compares pitch classes mod 40 so lead codes p+120 / p+160 match: 120 and 160 are multiples of 40)

# ---- part 1: rhythm guitar, part 2: bass, part 3: drums
C = [(0, 480, (48, 43), "pm"), (480, 480, (70, 63, 60), "A"), (960, 240, (48, 43), "pm"), (1200, 240, (48, 43), "pm"),
     (1440, 240, (50, 45), "pm"), (1680, 240, (51, 46), "pm")]
D = [(0, 480, (49, 44), "pm"), (480, 480, (59, 58, 56), ""), (960, 240, (49, 44), "pm"), (1200, 240, (49, 44), "pm"),
     (1440, 240, (51, 46), "pm"), (1680, 240, (52, 47), "pm")]
DA = [(0, (36,)), (480, (35,)), (1200, ("35g",)), (1440, (36,)), (1680, (35,))]
DB = [(0, ("X", 36)), (240, (36,)), (360, (35,)), (480, (46, 38)), (720, (36,)), (840, ("38g", 35)), (960, (52, 36)),
      (1080, ("38g", 36)), (1200, (35,)), (1320, (36,)), (1440, (46, 38)), (1680, (36,)), (1800, (35,))]
PM, BASS, OUT0, OUTG = 0.55, 0.885, 0.16, 0.04          # 0.43: bass louder (x1.18, and x1.56 in the synth), outro much softer
KIND = {35: 2, 36: 2, 38: 1, 46: 3, 52: 4, 57: 4}
for m in range(52):
    base = m * M
    if m < 48:
        pat = C if (m // 4) % 2 == 0 else D
        for off, dur, ps, fl in pat:
            if fl == "pm": [note(base + off, dur, p, PM, 1) for p in ps]       # palm-muted: dark, fading out (0.43: was cut after 0.1 s)
            else: [note(base + off, 480, p, 0.65 if fl == "A" else 0.55, 1) for p in ps]   # the stab: fades over a beat, like PD's
        note(base, M, 36 if pat is C else 32, BASS * (1.13 if m in (0, 9, 17, 25, 33, 41) else 1))
    else:                                         # outro: one chord, struck again each measure, growing
        for p in (67, 63, 60, 55, 48, 43): note(base, M, p, OUT0 + OUTG * (m - 48))   # 0.43: softer (was 0.3 + 0.13/measure)
    pat = DB if 24 <= m < 32 or m >= 48 else DA
    for off, hits in pat:
        for h in hits:
            if h == "X": h = 57 if m in (24, 48) else 52
            g = isinstance(h, str); k = int(h[:2]) if g else h
            drums.append(((base + off) * TICK, 0.35 if g else 0.85 if k != 52 and k != 57 else 0.65, KIND[k]))

d = {"length": LOOP, "notes": sorted(notes), "drums": sorted(drums), "src_phase": 0.058, "no_hold": True, "vel_scale": 0.85, "pluck": 0.0,
     "profile": {"0": [round(k ** -0.8, 3) for k in range(1, 21)],          # 0.43: bright (was a soft square): the bass is heard on a tiny speaker
                 "1": [1, 0.5, 0.33, 0.25, 0.2, 0.16, 0.14, 0.12],            # guitar (saw-like): the stabs
                 "2": [1, 0.5, 0.33, 0.25, 0.2, 0.16, 0.14, 0.12]},
     "damp_tau": 0.12,                                                     # 0.43: the stabs and palm-muted chords fade (PD: ~-10 dB in 0.1 s)
     "tau": [0.6, 0.35, 0.35], "attack": [0.004, 0.004, 0.004],
     "lead_profile": [1, 0.71, 0.33, 0, 0.2, 0.24, 0.14], "lead_tau": 0.06, "lead_attack": 0.004}
json.dump(d, open(os.path.join(HERE, "game.json"), "w"))
print(f"game from the tab: {len(notes)} notes ({sum(1 for n in notes if n[2] >= 160)} lead), {len(drums)} drum hits, loop {LOOP:.2f} s")
