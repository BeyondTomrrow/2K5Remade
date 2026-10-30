"""Per-voice summary of a RECOMP_APU_PITCH_LOG run (+ RECOMP_APU_VOICE_LOG if present):
seconds heard, pitch values, Hz range, gain, centre-bin flag, formats, ONs. voice_table.py <stderr.log> [t0 t1]"""
import re, sys, collections, math
log = sys.argv[1]; t0 = float(sys.argv[2]) if len(sys.argv) > 2 else -1e18; t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e18
hdr = re.compile(r'\[PITCH\] qpc=(\S+) t=(\d+) .*?wav=(\d+)')
vl = re.compile(r'\[PITCH\]\s+([0-9A-F]{3}) n=(\d+) p=(-?\d+)\.\.(-?\d+) ps=(-?\d+) ef=([\d.]+)\.\.([\d.]+) rate=([\d.]+)\.\.([\d.]+) sets=(\d+) gain=([\d.]+)\.\.([\d.]+)( C)?')
on = re.compile(r'\[VOICE\] ([0-9A-F]{3}) ON ba (\w+) ebo (\w+) lbo (\w+) fmt (\w+) pitch (-?\d+)')
V = collections.defaultdict(lambda: dict(secs=0, pitches=collections.Counter(), hzmin=1e9, hzmax=0, gsum=0.0, gmax=0.0, c=0, fmts=collections.Counter(), ons=0, ebos=collections.Counter(), sets=0))
wav = 0; first = None; inwin = False
for line in open(log, errors='replace'):
    m = hdr.match(line)
    if m:
        wav = int(m.group(3)) / 48000.0; inwin = t0 <= wav <= t1; continue
    m = vl.match(line)
    if m and inwin:
        v = V[m.group(1)]; v['secs'] += 1; v['pitches'][int(m.group(3))] += 1; v['pitches'][int(m.group(4))] += 1
        v['hzmin'] = min(v['hzmin'], 48000 / float(m.group(9))); v['hzmax'] = max(v['hzmax'], 48000 / float(m.group(8)))
        v['gsum'] += float(m.group(11)); v['gmax'] = max(v['gmax'], float(m.group(12))); v['sets'] += int(m.group(10))
        if m.group(13): v['c'] += 1
        continue
    m = on.match(line)
    if m and inwin:
        v = V[m.group(1)]; v['ons'] += 1; v['fmts'][m.group(5)] += 1; v['ebos'][int(m.group(3), 16)] += 1
print(f"{'voice':5} {'secs':>5} {'gain_avg':>8} {'gmax':>5} {'C':>3} {'Hz range':>14} {'ons':>4} {'sets':>5}  top pitches / fmt / ebo")
for k in sorted(V):
    v = V[k]
    if not v['secs'] and not v['ons']: continue
    hz = f"{v['hzmin']:.0f}..{v['hzmax']:.0f}" if v['secs'] else '-'
    tp = ' '.join(f"{p}" for p, _ in v['pitches'].most_common(4))
    fm = ' '.join(f"{f}x{n}" for f, n in v['fmts'].most_common(2)); eb = ' '.join(f"{e:X}" for e, _ in v['ebos'].most_common(3))
    print(f"{k:5} {v['secs']:5} {v['gsum']/max(1,v['secs']):8.3f} {v['gmax']:5.2f} {v['c']:3} {hz:>14} {v['ons']:4} {v['sets']:5}  {tp} | {fm} | {eb}")
