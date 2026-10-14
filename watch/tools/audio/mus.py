import os, sys, json, numpy as np, librosa
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from paths import PD_SRC, PD_ASSETS  # upstream Pixel Dungeon (third_party submodule)
SR = 8000
ASSETS = PD_ASSETS

def load(name, sr=SR):
    y, _ = librosa.load(f"{ASSETS}/{name}.mp3", sr=sr)
    return y

def notes_for(name, onset=0.3, frame=0.2, minlen=40):
    cache = os.path.join(os.environ.get("TMPDIR", "/tmp"), f"pd_{name}_{onset}_{frame}_{minlen}.json")
    if os.path.exists(cache): return [tuple(n) for n in json.load(open(cache))]
    from basic_pitch.inference import predict
    from basic_pitch import ICASSP_2022_MODEL_PATH
    _, _, ev = predict(f"{ASSETS}/{name}.mp3", ICASSP_2022_MODEL_PATH, onset_threshold=onset, frame_threshold=frame, minimum_note_length=minlen)
    ns = sorted([(float(s), float(e), int(p), float(a)) for s, e, p, a, _ in ev])
    json.dump(ns, open(cache, "w"))
    return ns

def reg_of(p): return 0 if p < 52 else 1 if p < 67 else 2

def harmonic_profile(y, notes):
    S = np.abs(librosa.stft(y, n_fft=2048, hop_length=128)); fr = librosa.fft_frequencies(sr=SR, n_fft=2048)
    prof = {0: [], 1: [], 2: []}
    for s, e, p, a in notes:
        f0 = 440 * 2 ** ((p - 69) / 12)
        t0 = int((s + 0.03) * SR / 128); t1 = max(t0 + 1, int(min(e, s + 0.15) * SR / 128))
        h = []
        for k in range(1, 9):
            f = f0 * k
            if f > SR / 2 - 100: h.append(0); continue
            i = np.argmin(abs(fr - f)); h.append(S[i - 1:i + 2, t0:t1].max())
        h = np.array(h); h /= h[0] + 1e-9
        prof[reg_of(p)].append(np.minimum(h, 2))
    return {r: np.median(v, axis=0) if v else np.array([1, .5, .25, .1, 0, 0, 0, 0]) for r, v in prof.items()}

def drums_for(y, thresh=0.25):
    """Broadband hits: onsets in the top band. Returns (time, amp, kind) kind 0 hat, 1 snare, 2 kick."""
    S = np.abs(librosa.stft(y, n_fft=256, hop_length=40)); fr = librosa.fft_frequencies(sr=SR, n_fft=256)
    hi = S[fr > 2500].sum(0); mid = S[(fr > 300) & (fr < 1500)].sum(0); lo = S[fr < 160].sum(0)
    d = np.maximum(0, np.diff(hi, prepend=hi[0]))
    d /= d.max()
    pk = librosa.util.peak_pick(d, pre_max=4, post_max=4, pre_avg=10, post_avg=10, delta=thresh * 0.3, wait=8)
    ev = []
    for i in pk:
        if d[i] < thresh * 0.4: continue
        t = i * 40 / SR
        dl = lo[min(len(lo) - 1, i + 3)] - lo[max(0, i - 3)]
        dm = mid[min(len(mid) - 1, i + 3)] - mid[max(0, i - 3)]
        kind = 2 if dl > 0.5 * lo.max() * 0.3 else 1 if dm > 0.3 * mid.max() * 0.3 else 0
        ev.append((t, float(d[i]), kind))
    return ev

def render(notes, prof, drums=(), tau=(0.5, 0.3, 0.25), dvol=0.6, length=None):
    L = length or int(max(e for _, e, _, _ in notes) * SR) + SR
    out = np.zeros(L)
    for s, e, p, a in notes:
        reg = reg_of(p)
        f0 = 440 * 2 ** ((p - 69) / 12)
        n = int((e - s + 0.05) * SR); t = np.arange(n) / SR
        w = sum(prof[reg][k] * np.sin(2 * np.pi * f0 * (k + 1) * t) for k in range(8) if f0 * (k + 1) < SR / 2)
        env = np.exp(-t / tau[reg]) * np.minimum(1, t / 0.005)
        rel = np.clip((e - s + 0.05 - t) / 0.05, 0, 1)
        i = int(s * SR); seg = a * w * env * rel; out[i:i + n] += seg[:max(0, L - i)]
    rng = np.random.default_rng(1)
    for t0, a, k in drums:
        dur = (0.04, 0.15, 0.12)[k]; n = int(dur * SR); tt = np.arange(n) / SR
        noise = rng.uniform(-1, 1, n)
        if k == 2: sig = np.sin(2 * np.pi * (120 - 400 * tt) * tt) * np.exp(-tt / 0.05)
        elif k == 1: sig = noise * np.exp(-tt / 0.05)
        else: sig = np.diff(noise, prepend=0) * np.exp(-tt / 0.012)
        i = int(t0 * SR); out[i:i + n] += dvol * a * sig[:max(0, L - i)]
    return out / (np.abs(out).max() + 1e-9)

def mel_score(y, r):
    n = min(len(r), len(y))
    m1 = librosa.power_to_db(librosa.feature.melspectrogram(y=y[:n], sr=SR, n_mels=48, hop_length=256))
    m2 = librosa.power_to_db(librosa.feature.melspectrogram(y=r[:n], sr=SR, n_mels=48, hop_length=256))
    m1 -= m1.mean(); m2 -= m2.mean()
    return np.sum(m1 * m2) / np.sqrt(np.sum(m1 * m1) * np.sum(m2 * m2))

def chroma_score(y, r):
    n = min(len(r), len(y))
    c1 = librosa.feature.chroma_stft(y=y[:n], sr=SR, hop_length=512); c2 = librosa.feature.chroma_stft(y=r[:n], sr=SR, hop_length=512)
    return np.mean((c1 * c2).sum(0) / (np.linalg.norm(c1, axis=0) * np.linalg.norm(c2, axis=0) + 1e-9))

def merge_fragments(y, notes, gap=0.04, jump=1.3, hi_gap=0.08, hi_jump=2.0):
    """basic-pitch splits a held note wherever its loudness wobbles (the flute's vibrato): join same-pitch
    notes separated by a short gap unless the recording shows a real new attack (energy jump) there."""
    S = np.abs(librosa.stft(y, n_fft=1024, hop_length=64)); fr = librosa.fft_frequencies(sr=SR, n_fft=1024)
    def energy(p, t):
        f0 = 440 * 2 ** ((p - 69) / 12); i = np.argmin(abs(fr - f0)); k = min(S.shape[1] - 1, max(0, int(t * SR / 64)))
        return S[max(0, i - 1):i + 2, k].max()
    out, last = [], {}
    for s, e, p, a in sorted(notes):
        j = last.get(p)
        if j is not None:
            ps, pe, _, pa = out[j]
            g, jm = (hi_gap, hi_jump) if reg_of(p) == 2 else (gap, jump)       # the high (flute) line: held notes
            if 0 <= s - pe < g or s < pe:
                before, after = energy(p, max(ps, s - 0.03)), energy(p, s + 0.02)
                if after < jm * before:
                    out[j] = (ps, max(pe, e), p, max(pa, a)); continue
        last[p] = len(out); out.append((s, e, p, a))
    return out

def decay_taus(y, notes):
    """Per register: how fast a held note fades in the recording (tau, s), from notes at least 0.4 s long."""
    S = np.abs(librosa.stft(y, n_fft=1024, hop_length=64)); fr = librosa.fft_frequencies(sr=SR, n_fft=1024)
    taus = {0: [], 1: [], 2: []}
    for s, e, p, a in notes:
        if e - s < 0.4: continue
        f0 = 440 * 2 ** ((p - 69) / 12); i = np.argmin(abs(fr - f0))
        k0, k1 = int((s + 0.05) * SR / 64), int((e - 0.05) * SR / 64)
        a0, a1 = S[i - 1:i + 2, k0].max(), S[i - 1:i + 2, k1].max()
        if a0 > 0 and a1 > 0 and a1 < a0: taus[reg_of(p)].append((e - s - 0.1) / np.log(a0 / a1))
        elif a1 >= a0: taus[reg_of(p)].append(5.0)
    return {r: float(np.median(v)) if v else 0.3 for r, v in taus.items()}, {r: len(v) for r, v in taus.items()}

def attack_times(y, notes):
    """Per register: median time from a note's start to its loudest point in the first 150 ms."""
    S = np.abs(librosa.stft(y, n_fft=512, hop_length=32)); fr = librosa.fft_frequencies(sr=SR, n_fft=512)
    at = {0: [], 1: [], 2: []}
    for s, e, p, a in notes:
        if e - s < 0.2: continue
        f0 = 440 * 2 ** ((p - 69) / 12); i = np.argmin(abs(fr - f0))
        k0 = max(0, int((s - 0.02) * SR / 32)); seg = S[max(0, i - 1):i + 2, k0:k0 + int(0.15 * SR / 32)].max(0)
        if len(seg): at[reg_of(p)].append(np.argmax(seg) * 32 / SR - 0.02)
    return {r: max(0.004, float(np.median(v))) if v else 0.004 for r, v in at.items()}

def grid_drums(y):
    """0.6.7: percussion only where the recording has a steady pattern, placed exactly on the beat grid.
    Per 16th step of a bar, the average broadband attack strength over all bars; steps clearly above the
    rest are the pattern (a hat), played in each bar where that step is actually struck. A track whose
    attacks don't follow the grid (they are its plucked notes) gets no drums."""
    _, beats = librosa.beat.beat_track(y=y, sr=SR, hop_length=64, units="time")
    S = np.abs(librosa.stft(y, n_fft=256, hop_length=32)); fr = librosa.fft_frequencies(sr=SR, n_fft=256)
    hi = S[fr > 2500].sum(0); d = np.maximum(0, np.diff(hi, prepend=hi[0])); d /= np.percentile(d, 99)
    grid = [a + (b - a) * k / 4 for a, b in zip(beats, beats[1:]) for k in range(4)]
    at = lambda t: d[max(0, int(t * SR / 32) - 2):int(t * SR / 32) + 3].max()
    H = np.array([at(t) for t in grid]); nb = len(H) // 16
    pat = H[:nb * 16].reshape(nb, 16).mean(0)
    if pat.max() < 2 * np.median(pat): return []
    steps = [k for k in range(16) if pat[k] > 0.5 * pat.max()]
    ev = []
    for i, t in enumerate(grid):
        if i % 16 in steps and H[i] > 0.15: ev.append((float(t), float(min(1.0, H[i])), 0))
    return ev
