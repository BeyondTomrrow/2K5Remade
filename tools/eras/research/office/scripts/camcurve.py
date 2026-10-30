import struct, sys, json, math
def load(path):
    return open(path,'rb').read()
def rel(d, field):
    v = struct.unpack_from('<I', d, field)[0]
    return field + v - 1 if v & 1 else None
def channels(d, base):
    # base: animation data block; first u32 = count of first channel? we parse triples until offsets run into coeff data
    out = []
    first_coeff = None
    o = base
    while True:
        cnt = struct.unpack_from('<I', d, o)[0]
        if first_coeff is not None and o >= first_coeff: break
        if cnt == 0 or cnt > 10000: break
        times = rel(d, o+4); coeffs = rel(d, o+8)
        if times is None or coeffs is None: break
        if first_coeff is None or coeffs < first_coeff: first_coeff = coeffs
        t = [struct.unpack_from('<H', d, times+2*i)[0] for i in range(cnt+1)]
        c = [struct.unpack_from('<4f', d, coeffs+16*i) for i in range(cnt)]
        out.append({'count': cnt, 'times': t, 'coeffs': c})
        o += 12
    return out
def ev(ch, tick):
    t = ch['times']
    i = 0
    while i < ch['count']-1 and tick >= t[i+1]: i += 1
    x = tick - t[i]
    a,b,c,dd = ch['coeffs'][i]
    return ((a*x + b)*x + c)*x + dd
if __name__ == '__main__':
    d = load(sys.argv[1]); base = int(sys.argv[2],16)
    chs = channels(d, base)
    print(len(chs), [c['count'] for c in chs])
    print('times0', chs[0]['times'])
    for T in [x*0.5 for x in range(0, 40)]:
        tick = int(T*300+0.5)
        print(f"t={T:5.1f}", ' '.join(f"{ev(c,tick):9.2f}" for c in chs))
