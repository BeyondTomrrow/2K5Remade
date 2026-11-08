"""Check a recording of the game's output (RECOMP_AUDIO_WAV) for audible defects.

    python tools/audio-check.py logs/<tag>.wav [--skip SECONDS]

Reports, per 10-second window: level, dropouts (sudden silence >= 5 ms
inside sound), clipping (samples at full scale), clicks (single-sample
jumps far beyond the local slope), and repeated blocks (the same 1024-sample
buffer played twice in a row, a stutter).
"""
import sys
import wave

import numpy as np


def main():
    path = sys.argv[1]
    skip = float(sys.argv[sys.argv.index('--skip') + 1]) if '--skip' in sys.argv else 0.0
    with wave.open(path, 'rb') as w:
        rate, ch = w.getframerate(), w.getnchannels()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, ch).astype(np.int32)
    data = data[int(skip * rate):]
    mono = data.mean(1)
    n = len(mono)
    print(f'{path}: {n / rate:.1f} s at {rate} Hz, {ch} ch')
    win = 10 * rate
    totals = dict(drop=0, clip=0, click=0, repeat=0)
    for start in range(0, n, win):
        seg = data[start:start + win]
        m = mono[start:start + win]
        if len(m) < rate:
            break
        rms = np.sqrt((m.astype(float) ** 2).mean()) / 32768
        clip = int((np.abs(seg) >= 32767).sum())
        # dropouts: >= 240 zero samples (5 ms) with sound on both sides
        z = (seg[:, 0] == 0) & (seg[:, 1] == 0)
        drops = 0
        i = 0
        while i < len(z):
            if z[i]:
                j = i
                while j < len(z) and z[j]:
                    j += 1
                if j - i >= 240 and i > 480 and j < len(z) - 480:
                    before = np.abs(m[i - 480:i]).mean()
                    after = np.abs(m[j:j + 480]).mean()
                    if before > 300 and after > 300:
                        drops += 1
                i = j
            else:
                i += 1
        d = np.diff(m)
        dd = np.abs(np.diff(d))
        thresh = max(6000.0, 12 * (np.median(dd) + 1))
        clicks = int((dd > thresh).sum())
        reps = 0
        for b in range(0, len(seg) - 2048, 1024):
            a1, a2 = seg[b:b + 1024], seg[b + 1024:b + 2048]
            if np.abs(a1).mean() > 200 and np.array_equal(a1, a2):
                reps += 1
        t = (start / rate) + skip
        print(f'  {t:6.0f}s  level {rms:.3f}  dropouts {drops:3d}  clipped {clip:6d}  clicks {clicks:4d}  repeats {reps:3d}')
        totals['drop'] += drops; totals['clip'] += clip; totals['click'] += clicks; totals['repeat'] += reps
    print('TOTAL', totals)


if __name__ == '__main__':
    main()
