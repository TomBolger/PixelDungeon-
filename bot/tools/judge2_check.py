# judge2_check: fit on the first file set (old games), check calibration on the second (the search's games).
# usage: python3 judge2_check.py old.txt -- new.txt
import sys, numpy as np
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_auc_score
i = sys.argv.index('--'); A, B = sys.argv[1:i], sys.argv[i+1:]
src = open('tools/judge2.py').read(); pre = src[:src.index('for s in snaps:')]
def load(files):
    g = {}; sys.argv = ['x'] + files; exec(pre, g)
    X = []; ypb = []; yd = []
    for s in g['snaps']:
        k = (s['f'], s['g'])
        if k not in g['ends']: continue
        dmax, turns, died = g['ends'][k]; d = s['d']; boss = (d + 4) // 5 * 5
        X.append(g['feats'](s) + [1 if d == q else 0 for q in range(1, 26)]); ypb.append(dmax > boss); yd.append(died and turns - s['t'] <= 50 and dmax == d)
    return np.array(X, float), np.array(ypb, int), np.array(yd, int)
XA, pa, da = load(A); XB, pb, db = load(B)
for name, ya, yb in (('past_boss', pa, pb), ('die50', da, db)):
    m = XA.mean(0); sd = XA.std(0) + 1e-9; clf = LogisticRegression(max_iter=5000).fit((XA - m) / sd, ya)
    q = clf.predict_proba((XB - m) / sd)[:, 1]
    print(f'{name}: on search games predicted mean {q.mean():.3f}, actual {yb.mean():.3f}, AUC {roc_auc_score(yb, q) if 0 < yb.mean() < 1 else float("nan"):.3f}  (n {len(yb)})')
    for d in (1, 2, 3, 4):
        sel = XB[:, 25 + d - 1] == 1
        if sel.sum(): print(f'   floor {d}: predicted {q[sel].mean():.3f} actual {yb[sel].mean():.3f} n {sel.sum()}')
