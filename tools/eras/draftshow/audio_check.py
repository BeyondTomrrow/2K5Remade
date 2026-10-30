"""Check the draft show's call in a recording of the game's audio (RECOMP_AUDIO_WAV: the speaker mix,
with the presentation layer's voice channel mixed in, recorded even when the speakers are muted).

    python -I tools/eras/draftshow/audio_check.py <recording.wav> <stderr.log> <out-prefix> [pick]

Finds each "pick is in" chime by cross-correlation with the clip the game played, cuts one full
pick (chime -> number -> team, plus 2 s either side) to <out-prefix>.wav, draws a waveform +
spectrogram with the clip spans from the log to <out-prefix>.png, and prints: clip spans in the
recording, gaps between clips, the peak level (clipping), and the game audio's level before, under
and after the call (menu music ducking).
"""
import os, re, struct, sys
import numpy as np


def read_wav(path):
    d = open(path, 'rb').read()
    i = d.find(b'data')
    pcm = np.frombuffer(d[i + 8:len(d) - ((len(d) - i - 8) % 4)], np.int16).reshape(-1, 2).astype(np.float32) / 32768
    return pcm


def rms_db(x):
    return 20 * np.log10(max(1e-9, float(np.sqrt(np.mean(x ** 2)))))


def main():
    rec, log, out = sys.argv[1], sys.argv[2], sys.argv[3]
    want = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    sr = 48000
    pcm = read_wav(rec)
    mono = pcm.mean(axis=1)
    text = open(log, 'r', errors='replace').read()
    # the voice clips the game played, with their GetTickCount start and file
    plays = [(int(t), p, float(n)) for t, p, n in re.findall(r'\[PRES\] voice t=(\d+) (?:ch \d+ )?playing (\S+) \(([\d.]+) s', text)]
    if not plays:
        sys.exit('no voice clips in the log')
    root = os.path.dirname(os.path.abspath(log))
    chime_path = next(p for _t, p, _n in plays if 'chime' in p)
    for base in (os.path.join(root, 'sbx-root'), os.getcwd()):
        cp = os.path.join(base, chime_path)
        if os.path.exists(cp):
            break
    ref = read_wav(cp).mean(axis=1)[:int(3.3 * sr)] * 0.8     # the chime's body (the call starts at 3.4 s)
    # cross-correlate at 4 kHz to find chime onsets
    k = 12
    a = mono[:len(mono) // k * k].reshape(-1, k).mean(axis=1)
    b = ref[:len(ref) // k * k].reshape(-1, k).mean(axis=1)
    n = 1
    while n < len(a) + len(b):
        n *= 2
    c = np.fft.irfft(np.fft.rfft(a, n) * np.conj(np.fft.rfft(b, n)), n)[:len(a)]
    e = np.concatenate([[0.0], np.cumsum(a.astype(np.float64) ** 2)])
    win = np.array([e[min(i + len(b), len(a))] - e[i] for i in range(len(a))])
    c /= (np.sqrt(np.maximum(win, 0)) * np.sqrt(np.sum(b ** 2)) + 1e-9)
    c[win / len(b) < 10 ** (-40 / 10)] = 0          # digital silence / near-silence: no match
    onsets = []
    for i in np.argsort(-c):
        if c[i] < 0.45:
            break
        if all(abs(i - j) > len(b) for j in onsets):
            onsets.append(int(i))
    onsets = sorted(onsets)
    print('chimes found at %s s (correlation >= 0.45)' % ', '.join('%.2f' % (o * k / sr) for o in onsets))
    chimes_log = [t for t, p, _n in plays if 'chime' in p]
    if len(onsets) < want or len(chimes_log) < want:
        sys.exit('pick %d not found' % want)
    # map GetTickCount to recording time with the wanted pick's chime
    t0 = chimes_log[want - 1]
    s0 = onsets[want - 1] * k / sr
    spans = []
    for t, p, dur in plays:
        st = s0 + (t - t0) / 1000.0
        if t >= t0 and (len(spans) == 0 or 'chime' not in p):
            spans.append((os.path.basename(p), st, st + dur))
        elif t > t0 and 'chime' in p:
            break
    print('clips in the recording (start, end s):')
    for nm, a0, a1 in spans:
        print('  %-28s %7.2f %7.2f  (%.2f s)' % (nm, a0, a1, a1 - a0))
    for (n1, _, e1), (n2, s2, _) in zip(spans, spans[1:]):
        print('  gap %s -> %s: %.2f s' % (n1, n2, s2 - e1))
    first, last = spans[0][1], spans[-1][2]
    pre = pcm[int((first - 6) * sr):int((first - 1) * sr)]
    under = pcm[int((last + 0.3) * sr):int((last + 2.2) * sr)]                        # the card beat (ducked)
    post = pcm[int((last + 4) * sr):int((last + 9) * sr)]
    print('game audio level: before the chime %.1f dBFS, card beat (ducked) %.1f dBFS, 4-9 s after the call %.1f dBFS' %
          (rms_db(pre), rms_db(under), rms_db(post)))
    seg = pcm[int((first - 2) * sr):int((last + 2) * sr)]
    peak = float(np.max(np.abs(seg)))
    clipped = int(np.sum(np.abs(seg) >= 0.999))
    print('call segment: peak %.1f dBFS, %d clipped samples' % (20 * np.log10(max(peak, 1e-9)), clipped))
    with open(out + '.wav', 'wb') as f:
        data = (np.clip(seg, -1, 1) * 32767).astype(np.int16).tobytes()
        f.write(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' + struct.pack('<IHHIIHH', 16, 1, 2, sr, sr * 4, 4, 16) +
                b'data' + struct.pack('<I', len(data)) + data)
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    t = np.arange(len(seg)) / sr + first - 2
    fig, ax = plt.subplots(2, 1, figsize=(14, 6), sharex=True)
    ax[0].plot(t[::20], seg[::20, 0], lw=0.4, color='#335')
    for nm, a0, a1 in spans:
        ax[0].axvspan(a0, a1, alpha=0.15, color='#c41a1a' if 'chime' in nm else '#1a6ac4')
        ax[0].text(a0, 0.9, nm.replace('.wav', ''), fontsize=8)
    ax[0].set_ylim(-1, 1); ax[0].set_ylabel('waveform')
    ax[0].set_title('Draft show pick %d: the game\'s audio output (RECOMP_AUDIO_WAV), clip spans from the log' % want)
    ax[1].specgram(seg.mean(axis=1), NFFT=1024, Fs=sr, noverlap=512, xextent=(t[0], t[-1]), cmap='magma')
    ax[1].set_ylim(0, 12000); ax[1].set_ylabel('Hz'); ax[1].set_xlabel('recording time (s)')
    fig.tight_layout()
    fig.savefig(out + '.png', dpi=100)
    print('wrote %s.wav and %s.png' % (out, out))


if __name__ == '__main__':
    main()
