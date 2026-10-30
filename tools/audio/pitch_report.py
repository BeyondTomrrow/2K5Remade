"""Pitch / pacing / crowd report for an in-game capture (docs/AUDIO-FIX-20261021.md).

    python tools/audio/pitch_report.py logs/audio/<tag> [--out logs/audio/pitch-<x>.png] [--from S] [--to S]

Reads <tag>.stderr.log ([PITCH] lines from RECOMP_APU_PITCH_LOG=1, [MIXCTL]), <tag>-frametime.csv
(NFL2K5_FRAMETIME_CSV) and <tag>.wav (RECOMP_AUDIO_WAV). Prints per-voice pitch statistics and
writes a figure: (1) every playing voice's rate relative to its own median, in cents, (2) the
game's frame rate, (3) the speaker queue and the VP frames per second, (4) mix level and crowd bed.
"""
import re
import sys
import wave
from collections import defaultdict

import numpy as np


def load_log(path):
    secs = []           # dicts: qpc, t, vpframes, qmin, qmax, drops, under, bed, comm, mute
    voices = defaultdict(list)   # v -> [(qpc, n, pmin, pmax, ps, efmin, efmax, rmin, rmax, sets, centre)]
    mix = []
    hdr = re.compile(r'\[PITCH\] qpc=(\d+) t=(\d+) ms=(\d+) vpframes=(\d+) queue=(-?\d+)\.\.(-?\d+) submits=(\d+) '
                     r'dropped=(\d+) underruns=(\d+)(?: bed=([\d.]+) comm=([\d.]+) mute_music=(\d))?(?: wav=(\d+))?(?: lockwaits=(\d+) lockwait_ms=([\d.]+))?(?: stalls=(\d+) stall_ms=([\d.]+))?')
    vl = re.compile(r'\[PITCH\]\s+([0-9A-F]{3}) n=(\d+) p=(-?\d+)\.\.(-?\d+) ps=(-?\d+) ef=([\d.]+)\.\.([\d.]+) '
                    r'rate=([\d.]+)\.\.([\d.]+) sets=(\d+)(?: gain=([\d.]+)\.\.([\d.]+))?( C)?')
    cur = None
    with open(path, 'r', errors='replace') as f:
        for line in f:
            m = hdr.search(line)
            if m:
                g = m.groups()
                cur = float(g[0])
                secs.append(dict(qpc=cur, t=int(g[1]), ms=int(g[2]), vpframes=int(g[3]), qmin=int(g[4]),
                                 qmax=int(g[5]), subs=int(g[6]), drops=int(g[7]), under=int(g[8]),
                                 bed=float(g[9] or 1), comm=float(g[10] or 1), mute=int(g[11] or 0),
                                 wav=int(g[12]) if g[12] else None,
                                 lockms=float(g[14] or 0), stallms=float(g[16] or 0)))
                continue
            m = vl.search(line)
            if m and cur is not None:
                g = m.groups()
                voices[g[0]].append((cur, int(g[1]), int(g[2]), int(g[3]), int(g[4]), float(g[5]), float(g[6]),
                                     float(g[7]), float(g[8]), int(g[9]), bool(g[12]),
                                     float(g[10]) if g[10] else None, float(g[11]) if g[11] else None))
                continue
            if line.startswith('[MIXCTL]'):
                mix.append(line.strip())
    return secs, voices, mix


def load_frametimes(path):
    try:
        d = np.genfromtxt(path, delimiter=',', names=True)
        return d['t_ms'], d['frame_ms']
    except Exception:
        return np.array([]), np.array([])


def wav_levels(path, start_qpc=None):
    with wave.open(path, 'rb') as w:
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).astype(np.float32) / 32768
    n = len(x) // rate
    x = x[:n * rate].reshape(n, rate, 2)
    mid = x.mean(2)
    side = (x[:, :, 0] - x[:, :, 1]) / 2
    rms = np.sqrt((mid ** 2).mean(1))
    srms = np.sqrt((side ** 2).mean(1))
    return rms, srms, rate


def main():
    tag = sys.argv[1]
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else tag + '-pitch.png'
    t_from = float(sys.argv[sys.argv.index('--from') + 1]) if '--from' in sys.argv else None
    t_to = float(sys.argv[sys.argv.index('--to') + 1]) if '--to' in sys.argv else None
    secs, voices, mix = load_log(tag + '.stderr.log')
    if not secs:
        print('no [PITCH] lines (run with RECOMP_APU_PITCH_LOG=1)')
        return
    q0 = secs[0]['qpc']
    print(f'{len(secs)} seconds of [PITCH] data, {len(voices)} voices')
    vp = np.array([s['vpframes'] * 1000.0 / max(1, s['ms']) for s in secs])
    print(f'VP frames/s: median {np.median(vp):.0f} (nominal 1500), min {vp.min():.0f}, max {vp.max():.0f}')
    print(f"voice-lock waits: {sum(s['lockms'] for s in secs) / 1000:.1f} s total; ISR stall waits: {sum(s['stallms'] for s in secs) / 1000:.1f} s")
    print(f"speaker queue: min {min(s['qmin'] for s in secs)}, max {max(s['qmax'] for s in secs)}; "
          f"dropped {sum(s['drops'] for s in secs)}, underruns {sum(s['under'] for s in secs)}")
    # per voice: rate spread within a second, and drift against the voice's own median rate
    worst = []
    for v, rows in sorted(voices.items()):
        r = np.array([(row[7] + row[8]) / 2 for row in rows])
        spread = np.array([1200 * np.log2(row[8] / row[7]) if row[7] > 0 else 0 for row in rows])
        med = np.median(r)
        dev = 1200 * np.log2(r / med)
        sets = sum(row[9] for row in rows)
        pvals = sorted(set(x for row in rows for x in (row[2], row[3])))
        psv = sorted(set(row[4] for row in rows))
        worst.append((max(np.abs(dev).max(), spread.max()), v, len(rows), med, dev.min(), dev.max(), spread.max(),
                      sets, pvals[:6], psv, any(row[10] for row in rows)))
    worst.sort(reverse=True)
    print('voice  secs  median_rate  drift_cents(min..max)  in-second_spread  pitch_writes  pitch_values  ps  centre')
    for w in worst[:40]:
        print(f'  {w[1]}  {w[2]:4d}  {w[3]:.4f}  {w[4]:8.1f} .. {w[5]:7.1f}  {w[6]:8.1f}  {w[7]:6d}  {w[8]}  {w[9]}  {"C" if w[10] else ""}')
    for m in mix[:40]:
        print(m)
    # 3D voices (handles 0x00-0x3F, the positional effects and player voices): how far the pitch
    # moves inside one second while the title keeps rewriting it (>= 5 writes, a moving sound),
    # leaving out seconds that touch the parked value (-12288 and below: voice idle / new sound).
    sw = defaultdict(float)
    for v, rows in voices.items():
        if int(v, 16) >= 0x40:
            continue
        for row in rows:
            if row[9] >= 5 and row[2] > -12200:
                sw[row[0]] = max(sw[row[0]], (row[3] - row[2]) * 1200 / 4096)
    vals = np.array(list(sw.values())) if sw else np.zeros(1)
    print(f"3D pitch swing within a second (moving voices): {len(vals)} voice-active seconds, "
          f"median {np.median(vals):.0f} c, p95 {np.percentile(vals, 95):.0f} c, max {vals.max():.0f} c, "
          f"seconds over 100 c: {(vals > 100).sum()}, over 300 c: {(vals > 300).sum()}")

    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(4, 1, figsize=(14, 12), sharex=True)
    for w in worst:
        v = w[1]
        rows = voices[v]
        t = np.array([(row[0] - q0) / 1000 for row in rows])
        lo = np.array([1200 * np.log2(row[7] / w[3]) for row in rows])
        hi = np.array([1200 * np.log2(row[8] / w[3]) for row in rows])
        ax[0].vlines(t, lo, hi, lw=1.5, alpha=0.6)
        ax[0].plot(t, (lo + hi) / 2, '.', ms=2, label=v if w[0] > 20 else None)
    ax[0].set_ylabel('voice rate vs its median (cents)')
    ax[0].axhline(0, color='k', lw=0.5)
    ax[0].set_title(f'{tag}: per-voice playback rate (pitch register + envelope) every second')
    if any(w[0] > 20 for w in worst):
        ax[0].legend(fontsize=6, ncol=8)
    ft_t, ft_ms = load_frametimes(tag + '-frametime.csv')
    if len(ft_t):
        tt = (ft_t - q0) / 1000
        bins = np.arange(np.floor(tt.min()), np.ceil(tt.max()) + 1)
        cnt, _ = np.histogram(tt, bins)
        ax[1].plot(bins[:-1], cnt, lw=1)
        worst_ft = np.zeros(len(bins) - 1)
        idx = np.digitize(tt, bins) - 1
        for i, ms in zip(idx, ft_ms):
            if 0 <= i < len(worst_ft):
                worst_ft[i] = max(worst_ft[i], ms)
        ax1b = ax[1].twinx()
        ax1b.plot(bins[:-1], worst_ft, 'r', lw=0.6, alpha=0.6)
        ax1b.set_ylabel('worst frame ms', color='r')
    ax[1].set_ylabel('game fps')
    t = np.array([(s['qpc'] - q0) / 1000 for s in secs])
    ax[2].fill_between(t, [s['qmin'] for s in secs], [s['qmax'] for s in secs], step='mid', alpha=0.4, label='queue min..max (buffers of 1024)')
    ax[2].plot(t, [s['under'] for s in secs], 'r.', ms=3, label='underruns')
    ax[2].plot(t, [s['drops'] for s in secs], 'm.', ms=3, label='dropped')
    ax[2].plot(t, [s['lockms'] / 10 for s in secs], 'c-', lw=0.6, label='voice-lock wait (x10 ms)')
    ax[2].plot(t, [s['stallms'] / 10 for s in secs], 'y-', lw=0.6, label='ISR stall wait (x10 ms)')
    ax2b = ax[2].twinx()
    ax2b.plot(t, vp, 'g', lw=0.6)
    ax2b.set_ylabel('VP frames/s', color='g')
    ax[2].legend(fontsize=7)
    try:
        rms, srms, _ = wav_levels(tag + '.wav')
        # the [PITCH] lines carry the recording position (wav=), so WAV seconds map onto the log's clock
        pairs = [((sx['qpc'] - q0) / 1000, sx['wav'] / 48000.0) for sx in secs if sx.get('wav')]
        if len(pairs) > 2:
            lt, ws = np.array(pairs).T
            wt = np.interp(np.arange(len(rms)) + 0.5, ws, lt)
        else:
            wt = np.arange(len(rms)) + (secs[0]['qpc'] - q0) / 1000 - 1
        ax[3].plot(wt, 20 * np.log10(rms + 1e-6), lw=0.7, label='mix (mid) dBFS')
        ax[3].plot(wt, 20 * np.log10(srms + 1e-6), lw=0.7, label='stereo side (crowd/ambience) dBFS')
        ax[3].legend(fontsize=7)
    except Exception as e:
        print('wav:', e)
    ax[3].plot(t, [20 * np.log10(max(1e-3, s['bed'])) for s in secs], 'k--', lw=0.8)
    ax[3].set_ylabel('dB')
    ax[3].set_xlabel('seconds (process QPC clock, from the first [PITCH] line)')
    if t_from is not None:
        ax[3].set_xlim(t_from, t_to)
    fig.tight_layout()
    fig.savefig(out, dpi=90)
    print('wrote', out)


if __name__ == '__main__':
    main()
