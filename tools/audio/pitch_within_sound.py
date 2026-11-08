"""How far each sound's pitch moves while it plays (docs/AUDIO-FIX-20261021.md).

    python -P tools/audio/pitch_within_sound.py logs/audio/<tag> [--out logs/audio/x.png] [--xemu logs/audio/xemu-freq.log]

Needs RECOMP_APU_PITCH_STACK=3d (3D voices: every pitch write + every sound start) and
RECOMP_APU_PITCH_LOG=1. A sound = a voice from one [PITCHON] to the next; its drift = the largest
distance of any later pitch write from the pitch it started with. A per-second view mixes
different sounds that reuse a voice (a new sound at another sample rate looks like a 1-2 octave
"swing"); this view does not.
"""
import re
import sys

import numpy as np


def main():
    tag = sys.argv[1]
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else tag + '-within-sound.png'
    xemu = sys.argv[sys.argv.index('--xemu') + 1] if '--xemu' in sys.argv else None
    events = []          # (qpc ms, voice, kind, value)
    for line in open(tag + '.stderr.log', errors='replace'):
        m = re.match(r'\[PITCHON\] qpc=([\d.]+) ([0-9A-F]{3}) pitch (-?\d+)', line)
        if m:
            events.append((float(m.group(1)), m.group(2), 'on', int(m.group(3))))
            continue
        m = re.match(r'\[PITCHSET\] qpc=([\d.]+) ([0-9A-F]{3}) (-?\d+) -> (-?\d+)', line)
        if m:
            events.append((float(m.group(1)), m.group(2), 'set', int(m.group(4))))
    # The title sets a new sound's frequency just before it starts the voice: a write followed
    # within 5 ms by that voice's [PITCHON] belongs to the new sound, not to the old one.
    next_on = {}
    skip = set()
    for i in range(len(events) - 1, -1, -1):
        q, v, kind, val = events[i]
        if kind == 'on':
            next_on[v] = q
        elif v in next_on and next_on[v] - q < 5.0:
            skip.add(i)
    start, drift, sounds = {}, {}, []
    q0 = events[0][0] if events else 0
    for i, (q, v, kind, val) in enumerate(events):
        if kind == 'on':
            if v in start:
                sounds.append((start[v][0], v, drift[v]))
            start[v] = ((q - q0) / 1000, val)
            drift[v] = 0.0
        elif i not in skip and v in start:
            drift[v] = max(drift[v], abs(val - start[v][1]) * 1200 / 4096)
    if not sounds:
        print('no [PITCHON] data')
        return
    d = np.array([s[2] for s in sounds])
    print(f'{len(d)} sounds on 3D voices: pitch drift while playing median {np.median(d):.1f} c, '
          f'p95 {np.percentile(d, 95):.1f} c, p99 {np.percentile(d, 99):.1f} c, max {d.max():.1f} c; '
          f'over 50 c: {(d > 50).sum()}, over 100 c: {(d > 100).sum()}')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    n = 2 if xemu else 1
    fig, ax = plt.subplots(1, n, figsize=(7 * n, 4.5))
    ax = np.atleast_1d(ax)
    ax[0].hist(np.clip(d, 0, 300), bins=60, color='tab:blue')
    ax[0].set_xlabel('pitch drift while the sound plays (cents; 100 c = one semitone)')
    ax[0].set_ylabel('sounds')
    ax[0].set_title(f'{tag}: {len(d)} 3D sounds, p99 {np.percentile(d, 99):.0f} c, max {d.max():.0f} c')
    if xemu:
        x = [int(m.group(1)) for m in (re.match(r'FREQ (\d+) stream 810b8cd8', l) for l in open(xemu)) if m]
        ours = []
        for line in open(tag.replace(tag.split('\\')[-1].split('/')[-1], 'b1') + '.stderr.log', errors='replace'):
            m = re.match(r'\[PITCHSET\] t=\d+ 044 -?\d+ -> (-?\d+)', line)
            if m:
                ours.append(round(48000 * 2 ** (int(m.group(1)) / 4096)))

        def ramp(a):
            for i, v in enumerate(a):
                if 19600 < v < 19700:
                    return a[i:]
            return a
        xa, oa = ramp(x)[:130], ramp(ours)[:130]
        ax[1].plot(xa, 'o-', ms=3, label='xemu (the game on an emulated Xbox)')
        ax[1].plot(oa, '.-', ms=3, label='NFL2K5-PC')
        ax[1].set_xlabel('successive frequency changes, crowd-reaction stream (voices 0x44/0x45)')
        ax[1].set_ylabel('Hz')
        ax[1].set_title('Title-screen crowd swell: same start (19644 Hz) and peak (23352 Hz)')
        ax[1].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(out, dpi=85)
    print('wrote', out)


if __name__ == '__main__':
    main()
