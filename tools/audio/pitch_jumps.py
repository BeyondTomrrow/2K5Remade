"""3D voice pitch jumps against the game's frame times (docs/AUDIO-FIX-20261021.md).

    python -P tools/audio/pitch_jumps.py logs/audio/<tag>

Needs a run with RECOMP_APU_PITCH_STACK=3d (every 3D-voice pitch write: [PITCHSET] with the QPC time,
[PITCHON] at each new sound) and NFL2K5_FRAMETIME_CSV (each presented frame: QPC time, frame ms).
For every pitch write it takes the change from the voice's previous write (a new sound restarts
the chain) and the duration of the game frame it happened in; prints how big changes are spread
over frame durations, and the callers (stack) of the big ones.
"""
import re
import sys
from collections import Counter, defaultdict

import numpy as np


def main():
    tag = sys.argv[1]
    writes = []
    last = {}
    stacks = Counter()
    for line in open(tag + '.stderr.log', errors='replace'):
        m = re.match(r'\[PITCHON\] qpc=([\d.]+) ([0-9A-F]{3}) pitch (-?\d+)', line)
        if m:
            last[m.group(2)] = int(m.group(3))
            continue
        m = re.match(r'\[PITCHSET\] qpc=([\d.]+) ([0-9A-F]{3}) (-?\d+) -> (-?\d+) cbo [0-9A-F]+ stack:(.*)', line)
        if m:
            q, v, was, now = float(m.group(1)), m.group(2), int(m.group(3)), int(m.group(4))
            prev = last.get(v, was)
            writes.append((q, v, now - prev, m.group(5).split()[:6]))
            last[v] = now
    if not writes:
        print('no [PITCHSET] lines (RECOMP_APU_PITCH_STACK=3d)')
        return
    d = np.genfromtxt(tag + '-frametime.csv', delimiter=',', names=True)
    ft, fms = d['t_ms'], d['frame_ms']
    rows = []
    for q, v, dp, st in writes:
        i = np.searchsorted(ft, q)          # the frame presented next after the write
        if i <= 0 or i >= len(ft):
            continue
        rows.append((abs(dp) * 1200 / 4096, fms[i], fms[i - 1]))
        if abs(dp) * 1200 / 4096 > 300:
            stacks[' '.join(st)] += 1
    r = np.array(rows)
    print(f'{len(r)} pitch writes on 3D voices; {np.sum(r[:, 0] > 100)} change by > 100 cents, {np.sum(r[:, 0] > 300)} by > 300 cents')
    for lo, hi in ((0, 20), (20, 30), (30, 50), (50, 100), (100, 1e9)):
        m = (r[:, 1] >= lo) & (r[:, 1] < hi)
        if m.sum():
            print(f'  frames of {lo:>3.0f}-{hi if hi < 1e9 else 999:>3.0f} ms: {m.sum():6d} writes, median change {np.median(r[m, 0]):6.1f} c, '
                  f'p95 {np.percentile(r[m, 0], 95):6.1f} c, share > 300 c {np.mean(r[m, 0] > 300) * 100:5.1f} %')
    print('callers of the > 300-cent changes:')
    for s, n in stacks.most_common(8):
        print(f'  {n:5d}  {s}')


if __name__ == '__main__':
    main()
