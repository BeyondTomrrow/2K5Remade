"""Music continuity of the draft show (M12 item 1): no gap in a recording of the game's audio while
the show runs.

    python -I tools/eras/draftshow/music_gap_check.py <recording.wav> <stderr.log> [--floor -50] [--gap 0.4]

The recording is RECOMP_AUDIO_WAV (48 kHz stereo s16, the mix the speakers would get, recorded even
when muted). The show's clock is aligned to the recording by the first chime: the log says when
the chime started on the show's clock ("audio: pick_is_in_chime.mp3 <- ... (t T s)") and a
cross-correlation with the chime file finds it in the recording. Then every 100 ms window from the
show's start (clock 0) to its last logged event is measured and the runs under <floor> dBFS that
last <gap> s or longer are listed (the bed ducked under the call sits at about -43 dBFS, a true gap
at -120). Exit code 1 when any run is found.
"""
import os, re, sys
import numpy as np

SR = 48000


def read_wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    raw = d[i + 8:]
    raw = raw[:len(raw) - len(raw) % 4]
    return np.frombuffer(raw, np.int16).reshape(-1, 2).astype(np.float32) / 32768


def main():
    args = sys.argv[1:]
    rec, log = args[0], args[1]
    floor = float(args[args.index('--floor') + 1]) if '--floor' in args else -50.0
    gap = float(args[args.index('--gap') + 1]) if '--gap' in args else 0.4
    text = open(log, 'r', errors='replace').read()
    m = re.search(r'audio: pick_is_in_chime\.mp3 <- (\S+), volume [\d.]+.*?\(t ([\d.]+) s\)', text)
    if not m:
        sys.exit('no chime in the log')
    chime_rel, chime_t = m.group(1), float(m.group(2))
    root = os.path.dirname(os.path.abspath(log))
    chime_path = None
    for base in (os.path.join(root, 'sbx-root'), os.getcwd()):
        for sub in ('', 'mods\\packs\\MyNFLEras\\generated', 'mods\\packs\\MyNFLEras', 'mods\\eras'):
            p = os.path.join(base, sub, chime_rel)
            if os.path.isfile(p):
                chime_path = p
                break
        if chime_path:
            break
    if not chime_path:
        sys.exit('chime file %s not found' % chime_rel)
    times = [float(t) for t in re.findall(r'\(t ([\d.]+) s\)', text)]
    show_end = max(times)
    pcm = read_wav(rec)
    mono = pcm.mean(1)
    ch = read_wav(chime_path).mean(1)[:int(3.0 * SR)]
    # coarse cross-correlation (decimated envelopes), then the offset
    dec = 48
    a = np.abs(mono[::dec]); b = np.abs(ch[::dec])
    a = a - a.mean(); b = b - b.mean()
    corr = np.correlate(a, b, mode='valid')
    at = int(np.argmax(corr)) * dec / SR
    offset = at - chime_t
    print('chime in the recording at %.2f s = show clock %.2f s: show clock 0 at %.2f s; last logged event at clock %.1f s' % (at, chime_t, offset, show_end))
    s0, s1 = max(0, int(offset * SR)), min(len(mono), int((offset + show_end) * SR))
    seg = mono[s0:s1]
    w = SR // 10
    n = len(seg) // w
    db = 20 * np.log10(np.maximum(np.sqrt((seg[:n * w].reshape(n, w) ** 2).mean(1)), 1e-6))
    runs, s = [], None
    for k, q in enumerate(db < floor):
        if q and s is None:
            s = k
        if not q and s is not None:
            runs.append((s, k)); s = None
    if s is not None:
        runs.append((s, n))
    bad = [(a0 / 10, b0 / 10, float(db[a0:b0].max())) for a0, b0 in runs if (b0 - a0) / 10 >= gap]
    print('window %.1f-%.1f s of the recording (%.0f s of show): level mean %.1f dBFS, quietest 100 ms %.1f dBFS'
          % (s0 / SR, s1 / SR, (s1 - s0) / SR, db.mean(), db.min()))
    if bad:
        print('GAPS (>= %.1f s under %.0f dBFS): %s' % (gap, floor, ', '.join('clock %.1f-%.1f s (max %.0f dB)' % (a0, b0, mx) for a0, b0, mx in bad)))
        sys.exit(1)
    longest = max(((b0 - a0) / 10 for a0, b0 in runs), default=0.0)
    print('no gap >= %.1f s under %.0f dBFS while the show ran (longest quiet run %.1f s)' % (gap, floor, longest))


if __name__ == '__main__':
    main()
