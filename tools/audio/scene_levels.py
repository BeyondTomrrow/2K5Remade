"""Band levels by scene class, using the voice-table dumps to classify each second of a capture:
quiet field (no 3D / 2D-pool voice heard), play (>= N 3D voices). scene_levels.py <label> <dump> <wav|ms> <wav> [N=8]
Prints medians of LF 20-150, speech 300-3k, 3-8k, 8-11k, 11-20k, rms, peak."""
import sys, struct, wave, numpy as np, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from vp_decode import decode
from scipy.signal import welch
lab, dump, kind, wavp = sys.argv[1:5]; N = int(sys.argv[5]) if len(sys.argv) > 5 else 8
buf = open(dump, 'rb').read(); rec = 8 + 32768
w = wave.open(wavp); sr = w.getframerate(); nfr = w.getnframes()
def lv(t0):
    if t0 < 0 or (t0 + 1) * sr > nfr: return None
    w.setpos(int(t0 * sr)); x = np.frombuffer(w.readframes(sr), dtype=np.int16).reshape(-1, 2).astype(np.float32) / 32768
    m = x.mean(axis=1); f, P = welch(m, sr, nperseg=4096)
    b = lambda lo, hi: 10 * np.log10(np.trapezoid(P[(f >= lo) & (f < hi)], f[(f >= lo) & (f < hi)]) + 1e-14)
    return (b(20, 150), b(300, 3000), b(3000, 8000), b(8000, 11000), b(11000, 20000), 20 * np.log10(np.sqrt((m * m).mean()) + 1e-9), 20 * np.log10(np.abs(x).max() + 1e-9))
cls = {'quiet': [], 'play': []}
for i in range(len(buf) // rec):
    h = struct.unpack_from('<II', buf, i * rec); t = h[0] / 48000.0 if kind == 'wav' else h[0] / 1000.0
    vs = {r.split()[0]: r for r in decode(buf[i * rec + 8:(i + 1) * rec])}
    n3 = sum(1 for v, r in vs.items() if int(v, 16) < 0x40 and ' g=0.000' not in r)
    n2 = sum(1 for v, r in vs.items() if 0x50 <= int(v, 16) < 0x80 and ' g=0.000' not in r)
    if not any(v in vs for v in ('044', '04A')): continue          # not in a match
    c = 'quiet' if n3 == 0 and n2 == 0 else ('play' if n3 >= N else None)
    if c:
        r = lv(t - 0.5)
        if r: cls[c].append(r)
for c, A in cls.items():
    if A:
        m = np.median(np.array(A), axis=0)
        print(f"{lab:10} {c:5} n={len(A):3}  LF {m[0]:6.1f}  speech {m[1]:6.1f}  3-8k {m[2]:6.1f}  8-11k {m[3]:6.1f}  11-20k {m[4]:6.1f}  rms {m[5]:6.1f}  peak {m[6]:6.1f}")
