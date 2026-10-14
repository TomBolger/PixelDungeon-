"""0.6.8: put the transcription on PD's real rhythm grid (run after transcribe.py; light, no neural net).

  python3 tools/audio/requant.py <pixel-dungeon/assets> theme game

Listening to the recordings (onset phase coherence, HPSS) shows both tracks are strict 16th-note grids in 4/4:
theme 16th = 0.1936 s (77.5 bpm), game 16th = 1/6 s (90 bpm); ~95-99% of the recording's onsets fall within 20 ms
of the grid. basic-pitch's note starts scatter 30-40 ms around it (only a third within 20 ms), which is what made
the rhythm feel sloppy, and the old drum finder assumed the wrong subdivision (4 per librosa beat).
Per track: the grid is fitted to the recording's onsets; every note start snaps to the grid point where the
recording actually has an attack (nearest one within ~60% of a 16th); notes with no attack there are held-note
fragments: joined to the same pitch before them, or dropped when quiet; ends snap to the grid; the piece is
shifted so its first 16th is at 0 and the loop is a whole number of 16ths. Drums: the recording's percussive
part (HPSS) per 16th in three bands; a bar position belongs to the pattern when it is struck in most bars, and
is played in each bar where it is actually struck: low band -> kick, high band on the backbeat -> snare, else hat.
"""
import sys, os, json, numpy as np, librosa
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)

ASSETS = sys.argv[1] if len(sys.argv) > 1 else PD_ASSETS
HERE = os.path.dirname(os.path.abspath(__file__))
SR, HOP = 22050, 64
TATUM = {"theme": 0.1936, "game": 1 / 6}            # first guesses (refined below)
# drum loudness against the notes, per kind (hat, snare, kick), set so that drum/mix level at the hits matches the
# recording's percussive/mix level (measured with listen/analysis/dvoice.py: theme's drums were ~3x too loud,
# game's hats ~2x too quiet)
# timbre: harmonic k of each register's wavetable times this, so that our render's harmonics (2..8, over the
# fundamental, median of isolated sustained notes) match the recording's (listen/analysis/timbre.py, chunk 4:
# theme's middle register had twice the recording's 2nd harmonic, game's middle one 5x)
FIX = {"theme": {0: [1, 1.19, .86, 1.17, 1.25, .53, 1.31, 1], 1: [1, .49, .37, .8, .63, .57, .67, 1]},
       "game": {1: [1, .33, .63, .45, .43, .4, 1.25, .67], 2: [1, 2.04, 1.21, 1, 1, 1, 1, 1]}}
# ring length per register (tau times this): our level 590 ms vs 100 ms into isolated sustained notes against the
# recording's (theme: middle rang too short, high too long; game's notes are too short and overlapping to measure)
TAUFIX = {"theme": [1, 1.49, 0.755], "game": [1, 1, 1]}
LEAD_DB = {"theme": 99, "game": -24}                 # dB under the track's loudest harmonic moment
GAIN = {"theme": {0: 0.5, 1: 0.35, 2: 0.4}, "game": {0: 1.8, 1: 0.9, 2: 0.9}}


def fit_grid(on, w, T0):
    best = None
    for T in np.arange(T0 * 0.995, T0 * 1.005, T0 * 0.00002):
        z = np.sum(w * np.exp(2j * np.pi * on / T)) / np.sum(w)
        if best is None or abs(z) > best[0]: best = (abs(z), T, (np.angle(z) / (2 * np.pi) * T) % T)
    return best


def band_hits(P, T, ph, n):
    S = np.abs(librosa.stft(P, n_fft=512, hop_length=HOP)); fr = librosa.fft_frequencies(sr=SR, n_fft=512)
    out = {}
    for b, m in (("lo", fr < 200), ("mid", (fr >= 200) & (fr < 2000)), ("hi", fr > 4000)):
        e = S[m].sum(0); d = np.maximum(0, np.diff(e, prepend=e[0]))
        v = np.array([d[max(0, int((ph + k * T - 0.025) * SR / HOP)):int((ph + k * T + 0.025) * SR / HOP)].max() for k in range(n)])
        out[b] = v / (np.percentile(v, 95) + 1e-9)
    return out


def requant(name):
    d = json.load(open(os.path.join(HERE, f"{name}_raw.json")))           # transcribe.py's output, kept
    y, _ = librosa.load(f"{ASSETS}/{name}.mp3", sr=SR)
    L = len(y) / SR
    o = librosa.onset.onset_strength(y=y, sr=SR, hop_length=HOP)
    on = librosa.onset.onset_detect(onset_envelope=o, sr=SR, hop_length=HOP, units="time")
    w = o[librosa.time_to_frames(on, sr=SR, hop_length=HOP)]
    R, T, ph = fit_grid(on, w / w.max(), TATUM[name])
    N = int(round(L / T))                                  # loop: whole 16ths (PD loops the whole file)
    # attack strength of the recording at each grid point
    att = np.zeros(N + 2)
    for t, s in zip(on, w / w.max()):
        k = int(round((t - ph) / T))
        if 0 <= k < len(att) and abs(t - ph - k * T) < 0.03: att[k] = max(att[k], s)
    notes = sorted(map(tuple, d["notes"]))
    amax = max(n[3] for n in notes)
    out = {}                                               # (grid index, pitch) -> [k0, k1, p, a]
    last_by_pitch = {}
    dropped = joined = 0
    for s, e, p, a in notes:
        x = (s - ph) / T; k = int(round(x))
        cands = [j for j in (k - 1, k, k + 1) if 0 <= j < len(att) and att[j] > 0.05 and abs(x - j) < 0.6]
        k1 = max(int(round((e - ph) / T)), 0)
        if os.environ.get('RQ_KEEPDUR'): k1 = None
        if os.environ.get('RQ_PLAIN'): cands = [k]
        if cands:
            k0 = min(cands, key=lambda j: abs(x - j))
        else:                                              # no attack in the recording here: an inner voice under a louder
            # attack, or a held note's fragment. Joining/dropping them lost pitch content (chroma 0.88 -> 0.86, game
            # 0.83 -> 0.79); kept on the nearest 16th, a little softer (0.8), they keep the harmony without extra attacks.
            prev = last_by_pitch.get(p)
            nojoin = not os.environ.get('RQ_JOIN')
            if nojoin: prev = None; a *= float(os.environ.get('RQ_SOFT', '0.8'))
            if prev is not None and k - out[prev][1] <= 2:
                out[prev][1] = max(out[prev][1], k1 if k1 is not None else k + max(1, int(round((e - s) / T)))); joined += 1; continue
            if a < 0.25 * amax and not nojoin: dropped += 1; continue
            k0 = k
        if k1 is None: k1 = k0 + max(1, int(round((e - s) / T)))
        k1 = max(k0 + 1, k1)
        key = (k0, p)
        if key in out: out[key][1] = max(out[key][1], k1); out[key][3] = max(out[key][3], a); continue
        prev = last_by_pitch.get(p)
        if prev is not None and out[prev][1] > k0 and not os.environ.get('RQ_NOCUT'): out[prev][1] = k0          # the same pitch again: the old one stops
        out[key] = [k0, k1, p, a]; last_by_pitch[p] = key
    # 0.6.9 the lead: per 16th, the loudest pitch of the recording's harmonic part when it is near the track's peak
    # level (game's melody from 10 s: a held, vibrato tone far louder than the rest). Its notes become held notes at
    # full velocity (basic-pitch split them at the vibrato and gave them the accompaniment's loudness); the others
    # sit under it. The lead's notes keep their pitches (checked against the pitch track).
    yh = librosa.effects.harmonic(librosa.resample(y, orig_sr=SR, target_sr=8000))
    C = librosa.amplitude_to_db(np.abs(librosa.cqt(yh, sr=8000, hop_length=64, fmin=librosa.midi_to_hz(36), n_bins=60)), ref=np.max)
    lead = {}
    for k in range(N):
        f0 = int((ph + k * T) * 8000 / 64); col = C[:, f0:f0 + max(1, int(T * 8000 / 64))].max(1)
        b = int(np.argmax(col))
        if col[b] > LEAD_DB[name] and 36 + b >= 57: lead[k] = 36 + b      # (the melody register; staccato phrases sit ~20 dB down in the recording)
    nl = 0
    for key in sorted(out):
        k0, k1, p, a = out[key]
        if lead.get(k0) == p or (lead.get(k0 + 1) == p and k1 > k0 + 1):
            out[key][3] = amax; nl += 1
            j = k0 + 1
            while lead.get(j) in (p, p - 1, p + 1) and j < N: j += 1     # held through its vibrato
            out[key][1] = max(k1, j)
        else: out[key][3] = a * 0.3
    for key in sorted(out):                                  # repeats of a held lead note inside it: gone
        if not out[key]: continue
        k0, k1, p, a = out[key]
        for q in range(k0 - 1, max(-1, k0 - 64), -1):
            o = out.get((q, p))
            if o and o[3] == amax and o[1] > k0:
                if a == amax: o[1] = max(o[1], k1)
                out[key] = None; break
    out = {k: v for k, v in out.items() if v}
    # melody 16ths the transcription has no note for: add them from the pitch track (a new note where the recording
    # has an attack or the pitch changes, else the previous one is held on)
    cover = set()
    for k0, k1, p, a in out.values():
        if a == amax: cover.update(range(k0, k1))
    added, cur = 0, None
    for k in range(N):
        p = lead.get(k)
        if p is None or k in cover: cur = None; continue
        if cur and cur[2] == p and att[k] <= 0.05: cur[1] = k + 1; continue
        cur = [k, k + 1, p, amax]; out[(k, p)] = cur; added += 1
    print(f"   lead added {added}")
    mp = os.path.join(HERE, f"{name}_melody.json")
    if os.path.exists(mp):                                   # 0.6.10: the hand-checked melody table replaces the lead found above
        mel = json.load(open(mp))                            # [k0, k1, pitch, level]: read off the recording 16th by 16th
        busy = {}                                            # (listen/analysis/mgrid.py: vibrato-averaged pitch, re-attacks)
        for k0, k1, p, _ in mel:
            for k in range(k0, k1): busy[k] = p
        out = {key: v for key, v in out.items() if v[3] != amax and v[2] < 84                    # old lead; high ghosts
               and not any(busy.get(k) == v[2] for k in range(v[0], v[1]))}                     # doubling the melody
        for k0, k1, p, _ in mel: out[(k0, p)] = [k0, k1, p, amax]
        print(f"   melody table: {len(mel)} notes")
    print(f"   lead: {len(lead)} 16ths, {nl} notes")
    ns = sorted((k0 * T, min(k1, N) * T, p + 120 if a == amax and lead and p >= 40 else p, a) for k0, k1, p, a in out.values() if k0 < N)
    if lead: d['lead_tau'] = 0.06; d['lead_profile'] = [1, 0.71, 0.33, 0, 0.2, 0.24, 0.14]; d['lead_attack'] = 0.004   # chiptune lead: 25% pulse (7 harmonics, < 4 kHz for its notes), instant attack, 60 ms decay to sustain
    # drums
    H, P = librosa.effects.hpss(y, margin=3.0)
    v = band_hits(P, T, ph, N)
    nb = N // 16
    dr = []
    pat = {b: v[b][:nb * 16].reshape(nb, 16).mean(0) for b in v}
    # what the low-band hit is: a boomy kick, or (game) a bright broadband knock -> the noise drum
    S = np.abs(librosa.stft(P, n_fft=512, hop_length=HOP)); fr = librosa.fft_frequencies(sr=SR, n_fft=512)
    lo_steps = [i for i in range(16) if pat["lo"][i] > 0.45 and pat["lo"][i] > 2 * np.median(pat["lo"])]
    fs = [int((ph + k * T) * SR / HOP) for k in range(N) if k % 16 in lo_steps]
    sp = np.mean([S[:, f:f + 8].mean(1) for f in fs], 0) if fs else np.ones(len(fr))
    lo_kind = 1 if np.sum(fr * sp) / np.sum(sp) > 2500 else 2
    for b, kind in (("lo", lo_kind), ("hi", 0)):
        steps = [i for i in range(16) if pat[b][i] > (0.3 if b == "hi" else 0.45) and pat[b][i] > (1.75 if b == "hi" else 2) * np.median(pat[b])]
        for k in range(N):
            if k % 16 in steps and v[b][k] > 0.35:
                kd = kind
                if b == "hi" and k % 8 == 4: kd = 1                   # backbeat: snare
                dr.append((k * T, float(min(1.0, v[b][k] * GAIN[name][kd])), kd))
    dr.sort()
    if os.environ.get('RQ_NODRUM'): dr = []
    if os.environ.get('RQ_DRUMONLY'): ns = []
    if os.environ.get('RQ_LEADONLY'): ns = [n for n in ns if n[2] >= 160]; dr = []
    print(f"{name}: 16th {T:.5f} s (coherence {R:.3f}) phase {ph:.3f} loop {N} 16ths = {N * T:.3f} s; "
          f"notes {len(notes)} -> {len(ns)} (joined {joined}, dropped {dropped}); drums {len(dr)}")
    for b in pat: print("   pattern", b, np.round(pat[b], 2))
    # attacks: transcribe.py measured 36-56 ms from basic-pitch's (early) starts; on the grid the recording's
    # attacks are sharp (8-12 ms ramps reproduce 70% of its onsets instead of 45%)
    d.update(notes=ns, drums=dr, length=N * T, grid=[T, 0.0], src_phase=ph, attack=[float(x) for x in os.environ.get('RQ_ATT', '0.008,0.008,0.012').split(',')])
    if not os.environ.get('RQ_NOFIX'):
        d['tau'] = [round(t * f, 3) for t, f in zip(d['tau'], TAUFIX[name])]
        for r, m in FIX[name].items():
            d["profile"][str(r)] = [round(h * f, 3) for h, f in zip(d["profile"][str(r)], m)]
    if os.environ.get('RQ_OLDATT'): d['attack'] = json.load(open(os.path.join(HERE, f'{name}_raw.json')))['attack']
    if dr: d["pluck"] = 0.0
    json.dump(d, open(os.path.join(HERE, f"{name}.json"), "w"))


for n in sys.argv[2:]: requant(n)
