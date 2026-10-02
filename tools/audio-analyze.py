# Analyze a RECOMP_AUDIO_WAV recording: level and clipping per second, a
# spectrogram PNG, and "horn" detection -- narrow spectral peaks that stay at
# the same frequency for over a second while standing well above their
# neighbours (a sustained tone, unlike speech, crowd or impacts).
#   python tools/audio-analyze.py logs/ingame2.wav [start_s] [end_s]
import sys, wave
import numpy as np

path = sys.argv[1]
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
w = wave.open(path, 'rb')
rate, ch = w.getframerate(), w.getnchannels()
n = w.getnframes()
a = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, ch).astype(np.float32) / 32768.0
a = a[int(t0 * rate):int(min(t1, n / rate) * rate)]
mono = a.mean(axis=1)
dur = len(mono) / rate
print(f"{path}: {rate} Hz, {ch} ch, {dur:.1f} s analysed (from {t0:.0f} s)")

sec = rate
print("\nper 10 s: rms dBFS / peak / clipped samples")
for s in range(0, int(dur), 10):
    seg = a[s * sec:(s + 10) * sec]
    if not len(seg): break
    rms = np.sqrt((seg ** 2).mean()) + 1e-9
    print(f"  {t0 + s:6.0f}s  {20 * np.log10(rms):6.1f}  {np.abs(seg).max():.3f}  {(np.abs(seg) >= 0.999).sum()}")

nfft, hop = 8192, 2048
win = np.hanning(nfft).astype(np.float32)
frames = 1 + (len(mono) - nfft) // hop
spec = np.empty((frames, nfft // 2 + 1), np.float32)
for i in range(frames):
    spec[i] = np.abs(np.fft.rfft(mono[i * hop:i * hop + nfft] * win))
db = 20 * np.log10(spec + 1e-7)
freqs = np.fft.rfftfreq(nfft, 1 / rate)

# Tonal peaks: a bin at least 18 dB above the median of +-40 bins around it,
# above -60 dB absolute, 60..4000 Hz.
lo, hi = np.searchsorted(freqs, 60), np.searchsorted(freqs, 4000)
k = 40
cs = np.cumsum(np.pad(db, ((0, 0), (k, k + 1)), mode='edge'), axis=1)
ctx = (cs[:, 2 * k + 1:] - cs[:, :-(2 * k + 1)]) / (2 * k + 1)
peak = np.zeros_like(db, dtype=bool)
peak[:, 1:-1] = (db[:, 1:-1] >= db[:, :-2]) & (db[:, 1:-1] >= db[:, 2:])
tonal = peak & (db > -60) & (db - ctx[:, :db.shape[1]] > 18)
tonal[:, :lo] = False
tonal[:, hi:] = False
# Runs: the same bin (+-1) tonal in consecutive frames for > 1 s.
min_frames = int(1.0 * rate / hop)
runs = []
active = {}
for i in range(frames):
    cols = set(np.nonzero(tonal[i])[0])
    nxt = {}
    for b in cols:
        start = min((active[k] for k in (b - 1, b, b + 1) if k in active), default=i)
        nxt[b] = start
    for b, start in active.items():
        if b not in nxt and not any(k in nxt for k in (b - 1, b + 1)) and i - start >= min_frames:
            runs.append((start, i, b))
    active = nxt
for b, start in active.items():
    if frames - start >= min_frames:
        runs.append((start, frames, b))
runs.sort()
print(f"\nsustained tones (> 1 s): {len(runs)}")
for start, end, b in runs[:80]:
    print(f"  {t0 + start * hop / rate:7.2f}s - {t0 + end * hop / rate:7.2f}s  {freqs[b]:7.1f} Hz  "
          f"{db[start:end, b].mean():6.1f} dB")

try:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    out = path.rsplit('.', 1)[0] + f'-spec-{int(t0)}.png'
    fig, ax = plt.subplots(figsize=(24, 8))
    top = np.searchsorted(freqs, 6000)
    vmax = float(np.percentile(db[:, :top], 99.7))
    ax.imshow(db[:, :top].T, origin='lower', aspect='auto', vmin=vmax - 60, vmax=vmax,
              extent=[t0, t0 + frames * hop / rate, 0, freqs[top]], cmap='magma')
    ax.set_xlabel('s'); ax.set_ylabel('Hz')
    fig.savefig(out, dpi=80, bbox_inches='tight')
    print('\nspectrogram:', out)
except Exception as e:
    print('no spectrogram:', e)
