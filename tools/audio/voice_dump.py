"""Turn RECOMP_APU_VOICE_DUMP=<dir> per-voice records into WAV files (docs/AUDIO-FIX-20261021.md).

    python -P tools/audio/voice_dump.py <dir> [voice-hex ...]

Each <dir>\\v<handle>.raw holds records {uint32 VP frame, int16[32] samples (left, at the
voice's loudest bin gain)}. Frames the voice did not play are filled with silence, so the
WAVs of different voices line up on the VP frame clock (48 kHz, 32 samples a frame).
"""
import os
import sys
import wave

import numpy as np


def load(path):
    raw = np.fromfile(path, dtype=np.uint8)
    rec = raw[: len(raw) // 68 * 68].reshape(-1, 68)
    frames = rec[:, :4].copy().view(np.uint32).ravel()
    samples = rec[:, 4:].copy().view(np.int16).reshape(-1, 32)
    return frames, samples


def main():
    d = sys.argv[1]
    want = {int(v, 16) for v in sys.argv[2:]}
    loaded = {}
    for name in sorted(os.listdir(d)):
        if not (name.startswith('v') and name.endswith('.raw')):
            continue
        v = int(name[1:4], 16)
        if want and v not in want:
            continue
        loaded[v] = load(os.path.join(d, name))
    if not loaded:
        print('no voice files')
        return
    f0 = min(fr.min() for fr, _ in loaded.values())
    f1 = max(fr.max() for fr, _ in loaded.values())
    for v, (fr, sm) in loaded.items():
        out = np.zeros(((f1 - f0 + 1) * 32,), np.int16)
        idx = (fr - f0).astype(np.int64)
        for k in range(32):
            out[idx * 32 + k] = sm[:, k]
        p = os.path.join(d, f'v{v:03X}.wav')
        with wave.open(p, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(out.tobytes())
        print(f'{p}: {len(fr)} frames played, peak {np.abs(sm).max()}')


if __name__ == '__main__':
    main()
