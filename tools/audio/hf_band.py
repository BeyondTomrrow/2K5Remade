"""Level of the 11-20 kHz band (imaging above the sources' Nyquist) and of 0.5-8 kHz, per file window:
hf_band.py <wav> <t0> <secs>"""
import sys, wave, numpy as np
from scipy.signal import welch
w = wave.open(sys.argv[1]); sr = w.getframerate(); w.setpos(int(float(sys.argv[2]) * sr))
d = np.frombuffer(w.readframes(int(float(sys.argv[3]) * sr)), dtype=np.int16).reshape(-1, 2).astype(np.float32).mean(axis=1) / 32768
f, P = welch(d, sr, nperseg=8192)
def band(lo, hi): s = (f >= lo) & (f < hi); return 10 * np.log10(np.trapezoid(P[s], f[s]) + 1e-14)
print(f"{sys.argv[1]} {sys.argv[2]}+{sys.argv[3]}s: 0.5-8k {band(500,8000):.1f} dBFS, 8-11k {band(8000,11000):.1f}, 11-20k {band(11000,20000):.1f} dBFS, total {band(20,20000):.1f}")
