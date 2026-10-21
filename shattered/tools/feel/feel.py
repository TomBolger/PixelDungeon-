"""How evenly the watch shows motion (see feel.sh): reads replay.c's output (F frames drawn, A the phone's frames as
played, P the phone's hero, S scenario marks, O messages in), one or more files, and prints:

Per scenario (all files together):
  frames    gaps between frames drawn while something moves (mean, 95th percentile, worst; real ms)
  camera    the map's scroll: change in speed from frame to frame (px/s, mean), times it turned back, jumps
  play      the phone's frames as played (game ms): spread of the shift between the phone's clock and the time they
            count from on the watch, how far that shift moved in all, changes of over 20 ms (each one: everything on
            screen speeds up or slows down for a moment), frames played over 60 ms after their time (their pictures
            jump on to catch up), and how many frames
  press     a press to the watch playing the phone's first frame after it took the press (real ms): mean, p90
  arrived   how late the phone's frames reached the watch (real ms, over the quickest lately): p50, p90, p95
Per walk of the hero (from a Select to the next press): short (up to 12 steps), long, and cut (walks the phone cut
short, scenario wstop: "back" is the hero shown past where he stopped and sliding back):
  steps     how long each step from cell to cell takes on screen (mean, spread, shortest, longest; real ms), and
            stalls (steps over 1.5 times the pace)
  speed     the hero's speed frame to frame: spread (% of the mean), frames under half or over 1.5 times the pace
  back      the hero going backwards (times, pixels), and jumps (moves too big for the time: times, pixels)
  delay     the hero shown against the phone's own (real ms): mean (negative: ahead), and its spread
  cam       the camera over the walk: change in speed from frame to frame (px/s), jumps
Units: watch pixels at the map's zoom.
"""
import sys, math, bisect, os, re

CELL = 128   # a cell in the map's units x8
STEP = 133.3   # real ms a step
DETAIL = os.environ.get('DETAIL')

def stats(v):
    if not v: return (0, 0, 0, 0)
    m = sum(v) / len(v)
    return m, math.sqrt(sum((x - m) ** 2 for x in v) / len(v)), min(v), max(v)

def pct(v, p):
    if not v: return 0
    s = sorted(v); return s[min(len(s) - 1, int(len(s) * p))]

def u32d(a, b):
    d = (a - b) & 0xFFFFFFFF
    return d - (1 << 32) if d >= (1 << 31) else d

def load(path):
    F, P, S, A, B, Q = [], [], [], [], [], []
    for line in open(path):
        a = line.split()
        if not a: continue
        if a[0] == 'F': F.append((float(a[1]),) + tuple(int(x) for x in a[2:11]))
        elif a[0] == 'P': P.append((float(a[1]),) + tuple(int(x) for x in a[2:7]))
        elif a[0] == 'S': S.append((float(a[1]), a[2]))
        elif a[0] == 'A': A.append((float(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]) if len(a) > 5 else 0))
        elif a[0] == 'B': B.append((float(a[1]), int(a[2])))
        elif a[0] == 'Q': Q.append((float(a[1]), int(a[2])))
    # each press: when the phone played its first frame after taking it (real ms after the press)
    R = []
    for (tb, code), (tq, stamp) in zip(B, Q):
        got = next((x[0] for x in A if x[0] >= tb and u32d(x[1], stamp) >= 0), None)
        if got is not None: R.append((tb, got - tb))
    return F, P, S, A, B, R

def cheb(dx, dy): return max(abs(dx), abs(dy))

def walks_of(pts, still_ms=250):
    """Runs of motion in (t, x, y, ...) samples, split where nothing moved for still_ms."""
    out, cur, last_move = [], [], None
    for i in range(1, len(pts)):
        t, x, y = pts[i][:3]; pt, px, py = pts[i - 1][:3]
        if (x, y) != (px, py):
            if cur and pt - last_move > still_ms: out.append(cur); cur = []
            if not cur: cur.append(pts[i - 1])
            cur.append(pts[i]); last_move = t
        elif cur and t - last_move > still_ms: out.append(cur); cur = []
    if cur: out.append(cur)
    return out

def crossings(w, ox, oy):
    """Times the hero passes a cell's place (interpolated between frames)."""
    ts = []
    for (t0, x0, y0), (t1, x1, y1) in zip(w, w[1:]):
        for a0, a1, o in ((x0, x1, ox), (y0, y1, oy)):
            if a0 == a1: continue
            lo, hi = sorted((a0, a1))
            k = math.ceil((lo - o) / CELL)
            while o + k * CELL <= hi:
                c = o + k * CELL
                if c != a0: ts.append(t0 + (t1 - t0) * (c - a0) / (a1 - a0))
                k += 1
    ts.sort()
    out = []
    for t in ts:
        if not out or t - out[-1] > 20: out.append(t)
    return out

class Acc:
    def __init__(s):
        s.steps, s.speeds, s.lag, s.cdv = [], [], [], []
        s.slow = s.fast = s.back_n = s.jump_n = s.cjump = s.walks = 0
        s.back_px = s.jump_px = 0.0

def scenario(F, A, out):
    gaps = [b[0] - a[0] for a, b in zip(F, F[1:]) if (a[2], a[3], a[6], a[7]) != (b[2], b[3], b[6], b[7])]
    out.setdefault('gaps', []).extend(gaps)
    cdv, turns, jumps, pv = [], 0, 0, None
    for a, b in zip(F, F[1:]):
        dt = b[0] - a[0]
        if dt <= 0: continue
        z = b[8] / 256.0
        vx, vy = (b[6] - a[6]) / dt, (b[7] - a[7]) / dt
        if pv is not None and ((vx, vy) != (0, 0) or pv != (0, 0)): cdv.append(math.hypot(vx - pv[0], vy - pv[1]) / 8 * z * 1000)
        if pv and vx * pv[0] + vy * pv[1] < 0: turns += 1
        if cheb(b[6] - a[6], b[7] - a[7]) > 1.6 * CELL / STEP * dt + 16: jumps += 1
        pv = (vx, vy)
    out.setdefault('cdv', []).extend(cdv); out['turns'] = out.get('turns', 0) + turns; out['cjumps'] = out.get('cjumps', 0) + jumps
    sh = [u32d(a[2], a[1]) for a in A]
    m = sum(sh) / len(sh) if sh else 0
    out.setdefault('sh', []).extend(x - m for x in sh)   # (each run's own mean: runs differ in their clocks)
    dsh = [abs(b - a) for a, b in zip(sh, sh[1:])]
    out['moved'] = out.get('moved', 0) + sum(dsh)
    out['changes'] = out.get('changes', 0) + sum(1 for d in dsh if d > 20)
    out['late'] = out.get('late', 0) + sum(1 for a in A if 60 < u32d(a[3], a[2]) < 100000)
    out['n'] = out.get('n', 0) + len(A)
    # how late each frame came (real ms), over the quickest within 8 s either side (the clocks' offset)
    arr = [(a[0], u32d(a[4], a[1])) for a in A if a[4]]
    ts = [x[0] for x in arr]
    for t, l in arr:
        lo, hi = bisect.bisect_left(ts, t - 8000), bisect.bisect_right(ts, t + 8000)
        out.setdefault('arr', []).append((l - min(x[1] for x in arr[lo:hi])) / 1.5)

def hero_walks(F, P, B, accs, tag):
    """Each walk: from a Select to the next press."""
    sel = [b[0] for b in B if b[1] == 3]
    ends = [b[0] for b in B] + [1e18]
    for t0 in sel:
        t1 = min(e for e in ends if e > t0)
        w = [(f[0], f[2], f[3], f[6], f[7], f[8]) for f in F if t0 - 50 <= f[0] < t1]
        pw = [(p[0], p[2], p[3]) for p in P if t0 - 50 <= p[0] < t1]
        if len(w) < 3: continue   # (pw: the phone's hero, when the run has it: the emulator's watch logs don't)
        # from the first frame he moves to the last
        mv = [i for i in range(1, len(w)) if w[i][1:3] != w[i - 1][1:3]]
        if not mv: continue
        w = w[mv[0] - 1:mv[-1] + 1]
        w3 = [(p[0], p[1], p[2]) for p in w]
        ox, oy = w3[0][1] % CELL, w3[0][2] % CELL
        cs = crossings(w3, ox, oy)
        n_steps = len(crossings(pw, pw[0][1] % CELL, pw[0][2] % CELL)) if len(pw) >= 3 else len(cs)
        if n_steps < 2: continue
        z = w[-1][5] / 256.0
        a = accs['cut' if tag == 'wstop' else 'long' if n_steps > 12 else 'short']
        a.walks += 1
        ws = [b - x for x, b in zip(cs, cs[1:])]
        a.steps += ws
        sp = []
        for (t0, x0, y0), (t1, x1, y1) in zip(w3[1:-1], w3[2:-1]):
            if t1 > t0: sp.append(cheb(x1 - x0, y1 - y0) / (t1 - t0))
        a.speeds += sp
        a.slow += sum(1 for v in sp if v < 0.5 * CELL / STEP); a.fast += sum(1 for v in sp if v > 1.5 * CELL / STEP)
        prev = None
        for (t0, x0, y0), (t1, x1, y1) in zip(w3, w3[1:]):
            dx, dy = x1 - x0, y1 - y0
            if (dx, dy) == (0, 0): continue
            if prev and dx * prev[0] + dy * prev[1] < 0: a.back_n += 1; a.back_px += cheb(dx, dy) / 8 * z
            if cheb(dx, dy) > 1.6 * CELL / STEP * max(1, t1 - t0) + 16: a.jump_n += 1; a.jump_px += cheb(dx, dy) / 8 * z
            prev = (dx, dy)
        pv = None
        for p0, p1 in zip(w, w[1:]):
            dt = p1[0] - p0[0]
            if dt <= 0: continue
            vx, vy = (p1[3] - p0[3]) / dt, (p1[4] - p0[4]) / dt
            if pv is not None: a.cdv.append(math.hypot(vx - pv[0], vy - pv[1]) / 8 * z * 1000)
            if cheb(p1[3] - p0[3], p1[4] - p0[4]) > 1.6 * CELL / STEP * dt + 16: a.cjump += 1
            pv = (vx, vy)
        # delay against the phone's hero over the same walk
        lag = []
        prog, tt = [0.0], [pw[0][0] if pw else 0]
        for (q0, x0, y0), (q1, x1, y1) in zip(pw, pw[1:]):
            prog.append(prog[-1] + cheb(x1 - x0, y1 - y0)); tt.append(q1)
        total, wp = prog[-1], 0.0
        for (q0, x0, y0), (q1, x1, y1) in zip(w3, w3[1:]):
            wp += cheb(x1 - x0, y1 - y0)
            if 0 < wp < total - 4:
                i = bisect.bisect_left(prog, wp)
                if 0 < i < len(prog):
                    g0, g1 = prog[i - 1], prog[i]
                    lag.append(q1 - (tt[i - 1] + (tt[i] - tt[i - 1]) * ((wp - g0) / (g1 - g0) if g1 > g0 else 0)))
        a.lag += lag
        if DETAIL:
            print('  %s walk at %.0f (%d steps): steps %s' % (tag, w3[0][0], n_steps, ' '.join('%.0f' % d for d in ws)))
            print('    speed (px/s): ' + ' '.join('%.0f' % (cheb(b[1] - x[1], b[2] - x[2]) / 8 * z / max(1, b[0] - x[0]) * 1000) for x, b in zip(w3, w3[1:])))
            if lag: print('    delay (ms): ' + ' '.join('%.0f' % x for x in lag[::2]))

def main():
    scen, accs = {}, {'short': Acc(), 'long': Acc(), 'cut': Acc()}
    for path in sys.argv[1:]:
        F, P, S, A, Bs, R = load(path)
        # (after a floor's own scenario, dN, the next ones are named after it: d7.walks)
        fl, S2 = '', []
        for t, nm in S:
            if re.match(r'd\d+$', nm): fl = nm + '.'; S2.append((t, nm))
            else: S2.append((t, fl + nm if os.environ.get('BYFLOOR') else nm))
        bounds = S2 + [(1e18, 'end')]
        for (t0, name), (t1, _) in zip(bounds, bounds[1:]):
            if name == 'end': continue
            f = [x for x in F if t0 <= x[0] < t1]
            if not f: continue
            scenario(f, [x for x in A if t0 <= x[0] < t1], scen.setdefault(name, {}))
            scen[name].setdefault('resp', []).extend(r[1] for r in R if t0 <= r[0] < t1)
            hero_walks(f, [x for x in P if t0 <= x[0] < t1], [x for x in Bs if t0 <= x[0] < t1], accs, name)
    print('%-7s | %-19s | %-25s | %-41s | %-23s | %s' % ('', 'frames: mean p95 max', 'camera: dv/frame turns jumps', 'play: spread moved changes late (frames)', 'press->answer: mean p90', 'arrived late: p50 p90 p95'))
    for name, o in scen.items():
        g = o['gaps']
        rs = o.get('resp', [])
        print('%-7s | %5.0f %5.0f %5.0f   | %7.1f %5d %5d         | %5.0f %7.0f %5d %5d (%5d)          | %5.0f %5.0f' % (name, sum(g) / len(g) if g else 0, pct(g, 0.95), max(g) if g else 0,
              sum(o['cdv']) / len(o['cdv']) if o['cdv'] else 0, o['turns'], o['cjumps'], stats(o['sh'])[1], o['moved'], o['changes'], o['late'], o['n'],
              sum(rs) / len(rs) if rs else 0, pct(rs, 0.9)) + '        | %5.0f %5.0f %5.0f' % (pct(o.get('arr', []), 0.5), pct(o.get('arr', []), 0.9), pct(o.get('arr', []), 0.95)))
    print('%-7s | %-31s | %-24s | %-22s | %-19s | %s' % ('walks', 'steps: mean spread min max (n) stalls', 'speed: spread slow fast', 'back: n px   jumps n px', 'delay: mean spread', 'cam: dv/frame jumps'))
    for k, a in accs.items():
        st = stats(a.steps); m = sum(a.speeds) / len(a.speeds) if a.speeds else 0; lg = stats(a.lag)
        print('%-7s | %5.0f %5.1f %5.0f %5.0f (%3d) %3d | %5.1f%% %5d %5d       | %3d %5.0f  %3d %5.0f   | %6.0f %6.1f       | %7.1f %4d   (%d walks)' % (
            k, st[0], st[1], st[2], st[3], len(a.steps), sum(1 for d in a.steps if d > 1.5 * STEP), stats(a.speeds)[1] / m * 100 if m else 0, a.slow, a.fast,
            a.back_n, a.back_px, a.jump_n, a.jump_px, lg[0], lg[1], sum(a.cdv) / len(a.cdv) if a.cdv else 0, a.cjump, a.walks))

if __name__ == '__main__':
    main()
