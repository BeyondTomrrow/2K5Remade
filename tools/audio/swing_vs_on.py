"""Lead 2 check: are the big per-second rate swings of the 3D voices (000-03F) new sounds on a reused
voice? For every [PITCH] voice line with rate max/min > 1.3 in a second, count whether a [VOICE] ON
for that voice fell in that second (RECOMP_APU_VOICE_LOG), and how many pitch writes (sets=) it had.
swing_vs_on.py <stderr.log>"""
import re, sys, collections
hdr = re.compile(r'\[PITCH\] qpc=\S+ t=(\d+) ms=(\d+)')
vl = re.compile(r'\[PITCH\]\s+([0-9A-F]{3}) n=(\d+) p=(-?\d+)\.\.(-?\d+) ps=(-?\d+) ef=[\d.]+\.\.[\d.]+ rate=([\d.]+)\.\.([\d.]+) sets=(\d+)')
on = re.compile(r'\[VOICE\] ([0-9A-F]{3}) ON .* \(t=(\d+)\)')
ons = collections.defaultdict(list); secs = []; cur = None
for line in open(sys.argv[1], errors='replace'):
    m = on.match(line)
    if m: ons[m.group(1)].append(int(m.group(2))); continue
    m = hdr.match(line)
    if m: cur = (int(m.group(1)) - int(m.group(2)), int(m.group(1))); continue
    m = vl.match(line)
    if m and cur and int(m.group(1), 16) < 0x40:
        secs.append((cur, m.group(1), float(m.group(7)) / float(m.group(6)), int(m.group(8))))
big = [s for s in secs if s[2] > 1.3]
with_on = sum(1 for (t0, t1), v, r, sets in big if any(t0 <= t <= t1 + 20 for t in ons[v]))
small_sets = [s for s in secs if s[2] <= 1.02]
print(f"3D voice-seconds: {len(secs)}; swings > 1.3x: {len(big)}; of those with a VOICE ON in that second: {with_on} ({100*with_on/max(1,len(big)):.0f} %)")
print(f"swings > 1.3x without an ON: {len(big)-with_on}; their pitch writes (sets) distribution: {collections.Counter(min(s[3],9) for s in big if not any(s[0][0] <= t <= s[0][1] + 20 for t in ons[s[1]]))}")
print(f"voice-seconds with < 2 % rate movement: {len(small_sets)} ({100*len(small_sets)/max(1,len(secs)):.0f} %)")
