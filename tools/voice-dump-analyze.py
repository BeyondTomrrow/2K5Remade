# Rank voices from a RECOMP_APU_VOICE_DUMP folder by their energy in a band
# (default 600-850 Hz) and in total, over a time window of the APU clock.
#   python tools/voice-dump-analyze.py logs/vdump2 [t0_s] [t1_s] [lo_hz] [hi_hz]
import glob, os, sys
import numpy as np

d = sys.argv[1]
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 150
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
lo = float(sys.argv[4]) if len(sys.argv) > 4 else 600
hi = float(sys.argv[5]) if len(sys.argv) > 5 else 850
rec = np.dtype([('frame', '<u4'), ('s', '<i2', 32)])
rate = 48000
rows = []
for p in sorted(glob.glob(os.path.join(d, 'v*.raw'))):
    a = np.fromfile(p, dtype=rec)
    if not len(a): continue
    t = a['frame'] * 32.0 / rate
    a = a[(t >= t0) & (t < t1)]
    if len(a) < 1500: continue
    x = a['s'].reshape(-1).astype(np.float32) / 32768.0
    n = 8192
    segs = len(x) // n
    if not segs: continue
    win = np.hanning(n)
    ps = np.zeros(n // 2 + 1)
    for i in range(segs):
        ps += np.abs(np.fft.rfft(x[i * n:(i + 1) * n] * win)) ** 2
    f = np.fft.rfftfreq(n, 1 / rate)
    band = ps[(f >= lo) & (f < hi)].sum()
    tot = ps.sum() + 1e-12
    pk = f[np.argmax(ps[f > 40]) + np.argmax(f > 40)]
    rows.append((band, os.path.basename(p)[1:4], len(a) * 32 / rate, 10 * np.log10(band + 1e-12), band / tot, pk,
                 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-9)))
rows.sort(reverse=True)
print(f"window {t0:.0f}-{t1:.0f} s, band {lo:.0f}-{hi:.0f} Hz")
print(" voice  active_s  band_dB  band_share  peak_Hz  rms_dBFS")
for band, v, act, bdb, share, pk, rms in rows:
    print(f"  {v}   {act:7.1f}  {bdb:7.1f}   {share:8.2%}  {pk:7.1f}  {rms:7.1f}")
