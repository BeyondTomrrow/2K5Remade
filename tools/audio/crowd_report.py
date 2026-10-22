"""Crowd presence in an in-game capture (docs/AUDIO-FIX-20261021.md).

    python -P tools/audio/crowd_report.py logs/audio/<tag> [...more tags]

From the RECOMP_APU_PITCH_LOG lines (gain = volume x envelope as heard, after the mutes) of the
match part of a run (from the first second a 3D voice plays): the stadium crowd beds (voices
0x4A-0x4F, hard-panned pairs), the crowd-reaction layer (0x44/0x45 in a match), and the level of
the recording's stereo side (L-R, where the crowd and ambience live; commentary is centred).
"""
import re
import sys
import wave

import numpy as np


def run(tag):
    secs = []
    cur = None
    for line in open(tag + '.stderr.log', errors='replace'):
        m = re.match(r'\[PITCH\] qpc=(\d+) .* wav=(\d+)', line)
        if m:
            cur = {'wav': int(m.group(2)) / 48000.0, 'g': {}}
            secs.append(cur)
            continue
        m = re.match(r'\[PITCH\]\s+([0-9A-F]{3}) n=\d+ .* gain=([\d.]+)\.\.([\d.]+)', line)
        if m and cur is not None:
            cur['g'][int(m.group(1), 16)] = float(m.group(2))
    start = next((i for i, s in enumerate(secs) if any(v < 0x40 for v in s['g'])), None)
    if start is None:
        print(f'{tag}: no match seconds')
        return
    game = secs[start:]
    beds = np.array([sum(s['g'].get(v, 0.0) for v in range(0x4A, 0x50)) for s in game])
    react = np.array([s['g'].get(0x44, 0.0) + s['g'].get(0x45, 0.0) for s in game])
    try:
        with wave.open(tag + '.wav', 'rb') as w:
            r = w.getframerate()
            x = np.frombuffer(w.readframes(w.getnframes()), np.int16).reshape(-1, 2).astype(np.float32) / 32768
        side = []
        for s in game:
            a = x[int((s['wav'] - 1) * r):int(s['wav'] * r)]
            if len(a):
                side.append(20 * np.log10(np.sqrt((((a[:, 0] - a[:, 1]) / 2) ** 2).mean()) + 1e-9))
        side = np.array(side)
    except Exception:
        side = np.zeros(1)
    print(f'{tag}: {len(game)} match seconds | crowd beds 0x4A-0x4F gain: mean {beds.mean():.3f}, '
          f'silent (< 0.005) {np.mean(beds < 0.005) * 100:.0f} % of the time | reaction layer 0x44/45 mean {react.mean():.3f} | '
          f'stereo side median {np.median(side):.1f} dBFS, seconds below -60 dBFS {(side < -60).sum()}')


if __name__ == '__main__':
    for t in sys.argv[1:]:
        run(t)
