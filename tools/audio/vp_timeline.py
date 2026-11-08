"""Per-voice timeline from a voice-table dump file (ours: RECOMP_APU_VP_DUMP, header = wav frames,
tick; xemu: xemu_vp_dump.py, header = ms, tick): vp_timeline.py <dump.bin> <hdr:wav|ms> [t0 t1]
Prints per record the time and active voices (compact), then a per-voice summary like voice_table.py."""
import sys, struct, collections, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vp_decode import decode, attenuate
p, kind = sys.argv[1], sys.argv[2]; t0 = float(sys.argv[3]) if len(sys.argv) > 3 else -1; t1 = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
buf = open(p, 'rb').read(); rec = 8 + 256 * 128
V = collections.defaultdict(lambda: dict(secs=0, hz=collections.Counter(), g=[], bins=collections.Counter(), fmt=collections.Counter(), kinds=collections.Counter(), fmode=collections.Counter(), ebo=collections.Counter()))
nrec = len(buf) // rec
for i in range(nrec):
    h = struct.unpack_from('<II', buf, i * rec); t = h[0] / 48000.0 if kind == 'wav' else h[0] / 1000.0
    if not (t0 <= t <= t1): continue
    rows = decode(buf[i * rec + 8:(i + 1) * rec])
    act = []
    for r in rows:
        f = dict(x.split('=', 1) for x in r.split(' bins[')[0].split()[1:] if '=' in x); v = r.split()[0]
        d = V[v]; d['secs'] += 1; d['hz'][int(float(f['hz']))] += 1; d['g'].append(float(f['g'])); d['fmt'][f['fmt']] += 1
        d['kinds'][r.split()[7] + '/' + r.split()[8] + '/' + r.split()[9]] += 1; d['fmode'][f['fmode']] += 1; d['ebo'][f['ebo']] += 1
        d['bins'][r.split(' bins[')[1].rstrip(']')] += 1
        act.append(f"{v}:{float(f['hz']):.0f}/{float(f['g']):.2f}")
    print(f"t={t:7.2f} n={len(rows):3d} " + ' '.join(act))
print()
print(f"{'voice':5} {'secs':>4} {'g_avg':>6} {'g_max':>6}  Hz (count)            fmt / kind / fmode / ebo / routing")
for v in sorted(V):
    d = V[v]; hz = ' '.join(f"{h}x{c}" for h, c in d['hz'].most_common(4))
    print(f"{v:5} {d['secs']:4} {sum(d['g'])/len(d['g']):6.3f} {max(d['g']):6.3f}  {hz:22} {d['fmt'].most_common(1)[0][0]} {d['kinds'].most_common(1)[0][0]} fm={d['fmode'].most_common(1)[0][0]} ebo={' '.join(e for e,_ in d['ebo'].most_common(2))} | {d['bins'].most_common(1)[0][0]}")
