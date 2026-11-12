# judge2: the mid-floor judge, fitted on SNAP lines (SNAPLOG=1) joined with each game's end.
# usage: python3 judge2.py snaps.txt [more.txt ...]
import sys, re, numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_auc_score
snaps = []; ends = {}
for f in sys.argv[1:]:
    for l in open(f):
        if l.startswith('SNAP '):
            p = l.split(); d = {'g': int(p[1][1:]), 't': int(p[2][1:]), 'd': int(p[3][1:])}
            for i in range(4, len(p) - 1, 2): d[p[i]] = float(p[i + 1])
            snaps.append(d)
        elif l.startswith('game '):
            p = l.split(); g = int(p[1]); dmax = int(p[p.index('(max') + 1].rstrip(')')); turns = int(p[p.index('turns') + 1]); died = ': died' in l
            ends[g] = (dmax, turns, died)
X = []; ypb = []; yd50 = []
def feats(s):
    ht = max(s['ht'], 1); x = s['hp'] / ht; owe = s['owe']; r = owe / max(s['hp'], 1)
    return [x, x * x, min(r, 3), 1 if r > 1 else 0, s['nf'], s['fdpt'] / ht, s['lvl'], ht, s['hunger'] >= 360, 260 <= s['hunger'] < 360, min(s['food'], 3), min(s['heal'], 3), min(s['unk'], 5),
            s['str'], s['at'], max(s['al'], -2), s['wt'], max(s['wl'], -2), s['dew'], s['dew'] >= 10, s['vial'], s['seen'] / 300, s['ft'] / 100, s['stairs'], s['t'] / 100]
NAMES = ['hp_frac', 'hp_frac2', 'owe_over_hp', 'owe_gt_hp', 'n_foes', 'foe_dpt_over_ht', 'lvl', 'HT', 'starving', 'hungry', 'food', 'heal', 'unk', 'str', 'armor_tier', 'armor_lvl', 'wpn_tier', 'wpn_lvl', 'dew', 'vial_full', 'has_vial', 'seen_frac', 'floor_turns', 'stairs_known', 'turn']
for s in snaps:
    if s['g'] not in ends: continue
    dmax, turns, died = ends[s['g']]; d = s['d']; boss = (d + 4) // 5 * 5
    f = feats(s) + [1 if d == k else 0 for k in range(1, 26)]
    X.append(f); ypb.append(1 if dmax > boss else 0); yd50.append(1 if died and turns - s['t'] <= 50 and dmax == d else 0)
X = np.array(X, float); ypb = np.array(ypb); yd50 = np.array(yd50)
print(len(X), 'snapshots;', 'past next boss rate', ypb.mean().round(3), '; died within 50 turns', yd50.mean().round(4))
for name, y in (('past_boss', ypb), ('die50', yd50)):
    m = X.mean(0); sd = X.std(0) + 1e-9
    clf = LogisticRegression(max_iter=5000, C=1.0).fit((X - m) / sd, y)
    p = clf.predict_proba((X - m) / sd)[:, 1]; print(f'\n== {name}: AUC {roc_auc_score(y, p):.3f}')
    w = clf.coef_[0] / sd; b = clf.intercept_[0] - (clf.coef_[0] * m / sd).sum()
    for n_, c in zip(NAMES, w): print(f'  {n_:16s} {c:+.4f}')
    print(f'static const double J2_{name.upper()}[{len(w)+1}] = {{', ', '.join(f'{c:.5f}' for c in list(w) + [b]), '};')
