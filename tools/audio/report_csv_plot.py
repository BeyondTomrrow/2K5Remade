"""Plot an F9 audio-report CSV: per-second rate range (Hz) and gain of chosen voices, voices playing,
fps. report_csv_plot.py <report.csv> <out.png> [voices regex, default 04[4-5A-F]|05.]"""
import sys, re, csv, numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
f, out = sys.argv[1], sys.argv[2]; pat = re.compile(sys.argv[3] if len(sys.argv) > 3 else r'^(04[45A-F]|05[0-9A-F])$')
rows = list(csv.DictReader(open(f)))
t = np.array([float(r['wav_seconds']) for r in rows]); fps = np.array([float(r['game_fps']) for r in rows]); np_ = np.array([int(r['voices_playing']) for r in rows])
ent = re.compile(r'([0-9A-F]{3}):(\d+)\.\.(\d+)Hz/g([\d.]+)')
per = {}
for r in rows:
    for m in ent.finditer(r['voices_rate_changed']):
        v = m.group(1)
        if pat.match(v): per.setdefault(v, []).append((float(r['wav_seconds']), int(m.group(2)), int(m.group(3)), float(m.group(4))))
fig, ax = plt.subplots(3, 1, figsize=(16, 11), sharex=True)
cols = plt.cm.tab20(np.linspace(0, 1, max(1, len(per))))
for i, v in enumerate(sorted(per)):
    a = np.array(per[v])
    ax[0].vlines(a[:, 0], a[:, 1], a[:, 2], color=cols[i], lw=3, label=v)
    ax[1].scatter(a[:, 0], a[:, 3], color=cols[i], s=18)
ax[0].set_ylabel('rate range in the second (Hz)'); ax[0].legend(ncol=8, fontsize=8); ax[0].grid(alpha=.3)
ax[0].set_title(f + ' -- voices whose rate moved (044/045 crowd reaction, 05x 2D SFX pool); vertical bar = min..max in that second')
ax[1].set_ylabel('gain (loudest bin x envelope)'); ax[1].grid(alpha=.3)
ax[2].plot(t, fps, label='game fps'); ax[2].plot(t, np_, label='voices playing'); ax[2].legend(); ax[2].grid(alpha=.3); ax[2].set_xlabel('wav seconds')
plt.tight_layout(); plt.savefig(out, dpi=80); print('saved', out)
