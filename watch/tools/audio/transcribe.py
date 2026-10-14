"""Turn PD's music into notes (0.6.6). Heavy (basic-pitch neural transcription): run once, the
results are kept in tools/audio/<track>.json and build_audio.py packs them for the watch.

  pip install basic-pitch (no-deps) onnxruntime librosa pretty_midi resampy soundfile
  python3 tools/audio/transcribe.py <pixel-dungeon/assets> theme game

Per track: the notes (basic-pitch, low thresholds: more notes matched the original better; fragments of
one held note, split where its vibrato wobbles, joined again), how long notes ring and how fast they
start in each register (measured from the recording), the
broadband hits as three drum kinds, and a harmonic profile per register measured from the recording
itself, so the synth's timbre follows the original instruments. Scores are printed (chroma and
mel-spectrogram similarity of a re-synthesis against the original).
"""
import sys, json, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mus
mus.ASSETS = sys.argv[1]
for name in sys.argv[2:]:
    y = mus.load(name)
    ns = mus.merge_fragments(y, mus.notes_for(name))    # 0.6.6.1: held notes stay whole (no warble)
    pr = mus.harmonic_profile(y, ns)
    dr = mus.grid_drums(y)                               # 0.6.7: on the beat grid, only where the recording has a pattern
    taus, _ = mus.decay_taus(y, ns)                       # how long notes ring, measured per register
    tau = tuple(min(4.0, max(0.15, taus[r])) for r in range(3))
    att = mus.attack_times(y, ns)
    attack = tuple(min(0.06, max(0.004, att[r])) for r in range(3))
    r = mus.render(ns, pr, dr, tau=tau, length=len(y))
    print(name, len(ns), "notes", len(dr), "hits", "chroma %.3f mel %.3f" % (mus.chroma_score(y, r), mus.mel_score(y, r)))
    print("  ring (s)", [round(t, 2) for t in tau], "attack (s)", [round(a, 3) for a in attack])
    pluck = 0.3 if not dr else 0.0                       # no drum pattern: its attacks are plucked notes (a click each)
    json.dump({"length": len(y) / mus.SR, "notes": ns, "drums": dr, "tau": tau, "attack": attack, "pluck": pluck,
               "profile": {str(k): [round(float(x), 3) for x in v] for k, v in pr.items()}},
              open(os.path.join(os.path.dirname(os.path.abspath(__file__)), f"{name}.json"), "w"))
