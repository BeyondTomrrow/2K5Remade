"""A/B of two captures of the same moment: ab_compare.py <a.wav> <a_t0> <b.wav> <b_t0> <seconds> <out.png> [labelA labelB]
Side-by-side mono spectrograms (0-8 kHz), long-term average spectrum, 1-s band levels
(20-150 LF, 150-500, 500-2k speech/crowd, 2k-8k), stereo width (S/M), and the 20 s clips
are written next to the PNG as <out>-A.wav / <out>-B.wav."""
import sys, wave, numpy as np
from scipy.signal import stft, welch
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
def load(p, t0, secs):
    w = wave.open(p); sr = w.getframerate(); ch = w.getnchannels()
    w.setpos(int(t0 * sr)); n = int(secs * sr)
    d = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, ch).astype(np.float32) / 32768.0
    if ch == 1: d = np.repeat(d, 2, axis=1)
    return sr, d
def save(p, sr, d):
    w = wave.open(p, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr)
    w.writeframes((np.clip(d, -1, 1) * 32767).astype(np.int16).tobytes()); w.close()
def db(x): return 20 * np.log10(np.sqrt(np.mean(x * x)) + 1e-9)
a, at0, b, bt0, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3], float(sys.argv[4]), float(sys.argv[5]), sys.argv[6]
la = sys.argv[7] if len(sys.argv) > 7 else 'A'; lb = sys.argv[8] if len(sys.argv) > 8 else 'B'
sr, A = load(a, at0, secs); _, B = load(b, bt0, secs)
save(out[:-4] + '-A.wav', sr, A); save(out[:-4] + '-B.wav', sr, B)
fig, ax = plt.subplots(3, 2, figsize=(18, 12))
bands = [(20, 150), (150, 500), (500, 2000), (2000, 8000)]
for col, (D, lab) in enumerate(((A, la), (B, lb))):
    m = D.mean(axis=1); s = (D[:, 0] - D[:, 1]) / 2
    f, t, Z = stft(m, sr, nperseg=4096, noverlap=3072)
    S = 20 * np.log10(np.abs(Z) + 1e-9)
    ax[0, col].pcolormesh(t, f[f <= 8000], S[f <= 8000], vmin=-100, vmax=-30, shading='auto', cmap='magma')
    ax[0, col].set_title(f"{lab}: {db(m):.1f} dBFS mono, L {db(D[:,0]):.1f} R {db(D[:,1]):.1f}, side {db(s):.1f} dBFS, peak {20*np.log10(np.abs(D).max()+1e-9):.1f}")
    ax[0, col].set_ylabel('Hz')
    fw, P = welch(m, sr, nperseg=8192)
    ax[1, col].semilogx(fw[1:], 10 * np.log10(P[1:] + 1e-14)); ax[1, col].set_xlim(20, 20000); ax[1, col].set_ylim(-140, -40); ax[1, col].grid(True, which='both', alpha=.3)
    ax[1, col].set_title('long-term average spectrum (dB/Hz)')
    n1 = int(sr); tt = np.arange(0, len(m) - n1 + 1, n1)
    for lo, hi in bands:
        fl, Pl = welch(m, sr, nperseg=4096)
        lv = []
        for i in tt:
            seg = m[i:i + n1]; ff, PP = welch(seg, sr, nperseg=4096); sel = (ff >= lo) & (ff < hi)
            lv.append(10 * np.log10(np.trapezoid(PP[sel], ff[sel]) + 1e-14))
        ax[2, col].plot(tt / sr, lv, label=f'{lo}-{hi} Hz')
    ax[2, col].set_ylim(-90, -10); ax[2, col].legend(); ax[2, col].set_xlabel('s'); ax[2, col].set_title('band levels per second (dBFS)'); ax[2, col].grid(alpha=.3)
plt.tight_layout(); plt.savefig(out, dpi=80); print('saved', out)
for D, lab in ((A, la), (B, lb)):
    m = D.mean(axis=1); s = (D[:, 0] - D[:, 1]) / 2
    ff, PP = welch(m, sr, nperseg=4096)
    bl = ' '.join(f"{lo}-{hi}:{10*np.log10(np.trapezoid(PP[(ff>=lo)&(ff<hi)], ff[(ff>=lo)&(ff<hi)])+1e-14):.1f}" for lo, hi in bands)
    print(f"{lab}: mono {db(m):.1f} dBFS side {db(s):.1f} dBFS peak {20*np.log10(np.abs(D).max()+1e-9):.1f} bands {bl}")
