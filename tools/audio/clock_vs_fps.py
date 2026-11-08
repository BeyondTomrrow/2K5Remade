"""Game time against wall time, by frame rate (docs/AUDIO-REPORT-HOTKEY-20261022.md, crowd timing).

    python -P tools/audio/clock_vs_fps.py logs/audio2/<tag>

Needs NFL2K5_CLOCK_LOG=1 ([CLOCK] lines: the play clock's game seconds per wall second, once a second
while it runs), RECOMP_APU_PITCH_LOG=1 ([PITCH] lines carry the QPC time the [CLOCK] lines lack; each
[CLOCK] line takes the time of the [PITCH] line before it) and NFL2K5_FRAMETIME_CSV.
"""
import bisect
import csv
import re
import statistics
import sys

for tag in sys.argv[1:]:
    ft = list(csv.DictReader(open(tag + '-frametime.csv')))
    ts = [float(r['t_ms']) for r in ft]
    q = None
    pairs = []
    for line in open(tag + '.stderr.log', errors='replace'):
        m = re.match(r'\[PITCH\] qpc=(\d+)', line)
        if m:
            q = float(m.group(1))
            continue
        m = re.search(r'\[CLOCK\] t=[\d.]+ play [\d.]+ -> [\d.]+: ([\d.]+) game-s per s', line)
        if m and q is not None:
            fps = bisect.bisect_left(ts, q) - bisect.bisect_left(ts, q - 1000)
            pairs.append((fps, float(m.group(1))))
    pairs = [p for p in pairs if 0.05 < p[1] < 1.5]
    print(f'{tag}: {len(pairs)} seconds with the play clock running')
    for lo, hi in ((55, 99), (45, 55), (30, 45), (15, 30), (0, 15)):
        v = [r for f, r in pairs if lo <= f < hi]
        if v:
            print(f'  {lo:2d}-{hi:2d} fps: {len(v):4d} s, play clock {statistics.median(v):.2f} game-s per wall-s '
                  f'(mean {statistics.mean(v):.2f})')
