"""Track the dominant low-frequency tone in a WAV: per second, the strongest peak
in 200-1500 Hz (left+right mono), its level, and the band RMS; also overall
RMS/peak per channel. tone_track.py <wav> [<png>]"""
import sys, numpy as np, wave
from scipy.signal import stft
def load(p):
    w = wave.open(p); n = w.getnframes(); sr = w.getframerate(); ch = w.getnchannels()
    d = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, ch).astype(np.float32) / 32768.0
    return sr, d
sr, d = load(sys.argv[1])
dur = len(d) / sr
print(f"{sys.argv[1]}: {dur:.1f}s sr={sr} ch={d.shape[1]}")
for c in range(d.shape[1]):
    x = d[:, c]; print(f"  ch{c}: rms {20*np.log10(np.sqrt(np.mean(x*x))+1e-9):.1f} dBFS peak {20*np.log10(np.max(np.abs(x))+1e-9):.1f} dBFS clipped {np.mean(np.abs(x)>0.999)*100:.3f}%")
m = d.mean(axis=1)
f, t, Z = stft(m, sr, nperseg=8192, noverlap=8192-4800)
P = np.abs(Z) ** 2
band = (f >= 200) & (f <= 1500)
rows = []
for i in range(0, len(t), 10):   # every ~1 s
    sl = P[:, i:i+10].mean(axis=1)
    bi = np.argmax(sl * band)
    f0 = f[bi]; pk = 10*np.log10(sl[bi] + 1e-12)
    med = 10*np.log10(np.median(sl[band]) + 1e-12)
    # harmonic check: energy at 2f0, 3f0 relative to f0
    def at(ff):
        j = int(round(ff / (f[1]-f[0]))); return 10*np.log10(sl[max(0,j-1):j+2].max()+1e-12)
    rows.append((t[i], f0, pk, pk-med, at(2*f0)-pk, at(3*f0)-pk))
print(" t     f0Hz  peakdB  above-median  2f0rel  3f0rel")
for r in rows: print("%5.1f %6.1f %7.1f %8.1f %8.1f %8.1f" % r)
if len(sys.argv) > 2:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
    fig, ax = plt.subplots(2, 1, figsize=(16, 9), sharex=True)
    S = 10*np.log10(P + 1e-12)
    ax[0].pcolormesh(t, f[f<=4000], S[f<=4000], vmin=-110, vmax=-40, shading='auto', cmap='magma')
    ax[0].set_ylabel('Hz'); ax[0].set_title(sys.argv[1] + ' (mono spectrogram 0-4 kHz)')
    rr = np.array(rows); ax[1].plot(rr[:,0], rr[:,1], '.-'); ax[1].set_ylabel('dominant 200-1500 Hz peak'); ax[1].set_xlabel('s')
    plt.tight_layout(); plt.savefig(sys.argv[2], dpi=90); print('saved', sys.argv[2])
