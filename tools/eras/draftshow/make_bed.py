"""My NFL Eras draft show: the looping music bed (M12 item 1) -> mods/eras/draft/files/audio/bed.wav

    python -I tools/eras/draftshow/make_bed.py <recording.wav> <start s> <end s> [--out path]
                                              [--xfade 4] [--rms -31] [--check bed.wav]

The retail menu music (femusic) ends and restarts with a 2-4 s gap that fell inside picks. The
runtime plays this bed on its own looping channel instead (the game's music muted while the show
runs). The user's own bed (mods\\eras\\draft\\audio\\bed.wav or .mp3) wins over this file.

Cut: the recording (RECOMP_AUDIO_WAV, 48 kHz stereo s16: a background run idling at the Main Menu
records the menu music alone) from start to end, the last <xfade> seconds equal-power crossfaded
into the first, so the loop point is inaudible; normalised to <rms> dBFS (the menu music under the
M11 show measured -33 dBFS; the voice clips are -18 LUFS), peaks under -1 dBFS. --check prints the
largest level dip over a given file (what the runtime's loop will sound like over two laps).
"""
import os, struct, sys
import numpy as np

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
OUT = os.path.join(ROOT, 'mods', 'eras', 'draft', 'files', 'audio', 'bed.wav')
SR = 48000


def read_wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    raw = d[i + 8:]
    raw = raw[:len(raw) - len(raw) % 4]
    return np.frombuffer(raw, np.int16).reshape(-1, 2).astype(np.float32) / 32768


def write_wav(path, pcm):
    x = np.clip(pcm * 32767, -32768, 32767).astype('<i2')
    data = x.tobytes()
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, SR, SR * 4, 4, 16) + b'data' + struct.pack('<I', len(data))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(hdr + data)


def rms_db(x):
    return 20 * np.log10(max(1e-9, float(np.sqrt(np.mean(x ** 2)))))


def dips(pcm, win=0.4, floor=-45):
    """(start, end) runs where the 100 ms RMS stays under floor dBFS for at least win s."""
    mono = pcm.mean(1)
    w = SR // 10
    n = len(mono) // w
    db = 20 * np.log10(np.maximum(np.sqrt((mono[:n * w].reshape(n, w) ** 2).mean(1)), 1e-6))
    runs, s = [], None
    for k, q in enumerate(db < floor):
        if q and s is None:
            s = k
        if not q and s is not None:
            if (k - s) / 10 >= win:
                runs.append((s / 10, k / 10))
            s = None
    return runs, db


def main():
    args = sys.argv[1:]
    if '--check' in args:
        pcm = read_wav(args[args.index('--check') + 1])
        two = np.concatenate([pcm, pcm])
        runs, db = dips(two)
        print('bed %.1f s, rms %.1f dBFS, peak %.1f dBFS, quietest 100 ms %.1f dBFS; dips >= 0.4 s under -45 dBFS over two laps: %s'
              % (len(pcm) / SR, rms_db(pcm), 20 * np.log10(max(1e-9, float(np.abs(pcm).max()))), db.min(), runs or 'none'))
        return
    rec, start, end = args[0], float(args[1]), float(args[2])
    out = args[args.index('--out') + 1] if '--out' in args else OUT
    xfade = float(args[args.index('--xfade') + 1]) if '--xfade' in args else 4.0
    want = float(args[args.index('--rms') + 1]) if '--rms' in args else -31.0
    pcm = read_wav(rec)[int(start * SR):int(end * SR)]
    n = int(xfade * SR)
    if len(pcm) < 3 * n:
        sys.exit('the cut is too short for a %.1f s crossfade' % xfade)
    head, body, tail = pcm[:n], pcm[n:len(pcm) - n], pcm[len(pcm) - n:]
    t = np.linspace(0, np.pi / 2, n, dtype=np.float32)[:, None]
    loop = np.concatenate([body, tail * np.cos(t) + head * np.sin(t)])   # the tail fades into the head: the loop point is seamless
    loop *= 10 ** ((want - rms_db(loop)) / 20)
    peak = float(np.abs(loop).max())
    if peak > 10 ** (-1 / 20):
        loop *= 10 ** (-1 / 20) / peak
    write_wav(out, loop)
    runs, db = dips(np.concatenate([loop, loop]))
    print('wrote %s: %.1f s from %s %.1f-%.1f s, crossfade %.1f s, rms %.1f dBFS, peak %.1f dBFS, quietest 100 ms %.1f dBFS, dips over two laps: %s'
          % (out, len(loop) / SR, os.path.basename(rec), start, end, xfade, rms_db(loop), 20 * np.log10(max(1e-9, float(np.abs(loop).max()))), db.min(), runs or 'none'))


if __name__ == '__main__':
    main()
