"""Crowd-reaction pitch against the wall clock (docs/AUDIO-REPORT-HOTKEY-20261022.md, crowd timing).

    python -P tools/audio/crowd_clock.py --ours logs/audio2/<tag>.stderr.log [--ours2 <log> --label2 <text>]
                                         [--xemu logs/audio2/xemu-crowd.log] [--out logs/audio2/crowd-clock.png]
    python -P tools/audio/crowd_clock.py --slow logs/audio2/<tag>.stderr.log --frames logs/audio2/<tag>-frametime.csv

Ours: NFL2K5_CROWD_PROBE=1 lines "[CROWD] qpc=<ms> p=<old>-><new> hz=<Hz> ..." (voice 0x44, every pitch
change). xemu: tools/audio/xemu_crowd_clock.gdb lines "CROWD tick <KeTickCount ms> hz <Hz> ..." (guest
clock; it stops while gdb holds the VM, so the breakpoints do not stretch it).
--slow: a run with RECOMP_TEST_SLOW_FRAMES: the pitch slope (cents per second of wall time) while frames
are slow against the slope around it, during the crowd's slides.
"""
import bisect
import csv
import math
import re
import sys


def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def ours(path):
    pts = []
    for line in open(path, errors='replace'):
        m = re.match(r'\[CROWD\] qpc=([\d.]+) p=(-?\d+)->(-?\d+) hz=(\d+)', line)
        if m:
            pts.append((float(m.group(1)) / 1000.0, int(m.group(4))))
    return pts


def xemu(path):
    pts = []
    for line in open(path, errors='replace'):
        m = re.match(r'CROWD tick (\d+) hz (\d+)', line)
        if m:
            pts.append((int(m.group(1)) / 1000.0, int(m.group(2))))
    return pts


def swell(pts, lo=19500, hi=19800, peak=23000):
    """The title-screen swell: the first point in [lo, hi] that rises past `peak` within 10 s."""
    for i, (t, hz) in enumerate(pts):
        if lo <= hz <= hi:
            later = [p for p in pts[i:] if p[0] - t < 10]
            if any(p[1] > peak for p in later):
                return [(p[0] - t, p[1]) for p in pts[i:] if p[0] - t < 30]
    return []


def cents(a, b):
    return 1200 * math.log2(b / a)


def overlay():
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    out = arg('--out', 'logs/audio2/crowd-clock.png')
    fig, ax = plt.subplots(figsize=(9, 4.8))
    for path, label, style in ((arg('--xemu'), 'xemu (Xbox), guest clock', 'o-'),
                               (arg('--ours'), arg('--label', 'NFL2K5-PC'), '.-'),
                               (arg('--ours2'), arg('--label2', 'NFL2K5-PC (2)'), 'x-')):
        if not path:
            continue
        pts = xemu(path) if 'xemu' in label else ours(path)
        s = swell(pts)
        if not s:
            print(label, ': no title swell found')
            continue
        pk = max(s, key=lambda p: p[1])
        half = next((p for p in s if p[0] > pk[0] and p[1] < (s[0][1] + pk[1]) / 2), None)
        print(f'{label}: start {s[0][1]} Hz, peak {pk[1]} Hz at {pk[0]:.2f} s, '
              f'half-way back down at {half[0]:.2f} s' if half else f'{label}: peak {pk[1]} Hz at {pk[0]:.2f} s')
        ax.plot([p[0] for p in s], [p[1] for p in s], style, ms=3, label=label)
    ax.set_xlabel('seconds since the swell started (wall clock / Xbox guest clock)')
    ax.set_ylabel('crowd-reaction stream frequency (Hz)')
    ax.set_title('Title-screen crowd swell against time')
    ax.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=90)
    print('wrote', out)


def slow():
    pts = ours(arg('--slow'))
    ft = list(csv.DictReader(open(arg('--frames'))))
    ts = [float(r['t_ms']) / 1000.0 for r in ft]
    ms = [float(r['frame_ms']) for r in ft]

    def fps_at(t):
        i, j = bisect.bisect_left(ts, t - 0.5), bisect.bisect_left(ts, t + 0.5)
        return j - i

    # slopes over 0.5 s windows during slides (pitch moving monotonically)
    slow_s, fast_s = [], []
    for k in range(len(pts) - 1):
        t0, h0 = pts[k]
        j = bisect.bisect_left([p[0] for p in pts], t0 + 0.5)
        if j >= len(pts) or pts[j][0] - t0 > 0.7:
            continue
        t1, h1 = pts[j]
        c = cents(h0, h1)
        if abs(c) < 2:
            continue
        rate = abs(c) / (t1 - t0)
        f = fps_at((t0 + t1) / 2)
        (slow_s if f < 30 else fast_s if f >= 55 else []).append(rate)
    import statistics
    if slow_s and fast_s:
        print(f'crowd pitch slides: {statistics.median(fast_s):.0f} cents/s at 55-60 fps ({len(fast_s)} windows), '
              f'{statistics.median(slow_s):.0f} cents/s under 30 fps ({len(slow_s)} windows): '
              f'ratio {statistics.median(slow_s) / statistics.median(fast_s):.2f}')
    else:
        print('not enough slides', len(slow_s), len(fast_s))


if __name__ == '__main__':
    if '--slow' in sys.argv:
        slow()
    else:
        overlay()
