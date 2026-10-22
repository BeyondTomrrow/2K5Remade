"""Decode a dump of the MCPX VP voice table (256 x 128 bytes, NV_PAVS_VOICE_*) into one line per
active voice: vp_decode.py <dump.bin> [label]. Shared by the xemu oracle (pmemsave of VPVADDR)
and our build (RECOMP_APU_VP_DUMP), so both sides are compared with the same decoder."""
import sys, struct, math
def attenuate(vol):
    vol &= 0xFFF
    return 0.0 if vol == 0xFFF else 10 ** (vol / (64.0 * -20.0))
def decode(buf, label=''):
    rows = []
    for v in range(256):
        w = struct.unpack_from('<32I', buf, v * 128)
        state = w[0x54 // 4]
        active = (state >> 21) & 1
        if not active: continue
        vbin = w[0]; fmt = w[1]
        bins = [(vbin >> s) & 0x1F for s in (0, 5, 10, 16, 21, 26)] + [fmt & 0x1F, (fmt >> 5) & 0x1F]
        vola, volb, volc = w[0x60 // 4], w[0x64 // 4], w[0x68 // 4]
        vols = [(vola >> 4) & 0xFFF, (vola >> 20) & 0xFFF, (volb >> 4) & 0xFFF, (volb >> 20) & 0xFFF,
                (volc >> 4) & 0xFFF, (volc >> 20) & 0xFFF,
                (vola & 0xF) | ((volb & 0xF) << 4) | ((volc & 0xF) << 8),
                ((vola >> 16) & 0xF) | (((volb >> 16) & 0xF) << 4) | (((volc >> 16) & 0xF) << 8)]
        pitch = struct.unpack('<h', struct.pack('<H', (w[0x7C // 4] >> 16) & 0xFFFF))[0]
        ps = struct.unpack('<b', bytes([(w[2] >> 24) & 0xFF]))[0]
        hz = 48000.0 / (2 ** (-pitch / 4096.0))
        paused = (state >> 18) & 1; efcur = (state >> 24) & 0xF; eacur = (state >> 28) & 0xF
        cbo = w[0x58 // 4] & 0xFFFFFF; ealvl = w[0x58 // 4] >> 24
        ebo = w[0x5C // 4] & 0xFFFFFF; eflvl = w[0x5C // 4] >> 24
        lbo = w[0x24 // 4] & 0xFFFFFF; ba = w[0x20 // 4] & 0xFFFFFF
        stream = (fmt >> 24) & 1; loop = (fmt >> 25) & 1; stereo = (fmt >> 27) & 1
        ssize = (fmt >> 28) & 3; csize = (fmt >> 30) & 3; spb = ((fmt >> 16) & 0x1F) + 1
        fmode = (w[0x18 // 4] >> 16) & 3
        fca = w[0x74 // 4]; fcb = w[0x78 // 4]
        gains = [attenuate(x) for x in vols]
        g = max(gains) * (ealvl / 255.0)
        routing = ' '.join(f"{b}:{gains[i]:.2f}" for i, b in enumerate(bins) if gains[i] > 0.001)
        rows.append(f"{label} {v:03X} p={pitch} ps={ps} hz={hz:.0f} g={g:.3f} ea={eacur} ealvl={ealvl} ef={efcur} eflvl={eflvl} "
                    f"fmt={fmt:08X} {'strm' if stream else 'buf'} {'loop' if loop else 'once'} {'st' if stereo else 'mono'} ss={ssize} cs={csize} spb={spb} fmode={fmode} "
                    f"ba={ba:06X} cbo={cbo:06X} ebo={ebo:06X} lbo={lbo:06X} fc={fca:08X}/{fcb:08X} {'PAUSED ' if paused else ''}bins[{routing}]")
    return rows
if __name__ == '__main__':
    buf = open(sys.argv[1], 'rb').read()
    for r in decode(buf, sys.argv[2] if len(sys.argv) > 2 else ''): print(r)
