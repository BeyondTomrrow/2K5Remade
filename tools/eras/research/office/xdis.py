"""Disassemble retail default.xbe code with capstone, annotating constants (read-only).

    python build/eras/office/xdis.py 142880 [n_insns] [--all]

Stops at the first ret unless --all (then runs n_insns). Constants that point at
UTF-16 strings, ASCII strings or 0x34-byte menu descriptors are annotated.
"""
import os, sys, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'native-menu'))
from xbe import Xbe  # noqa: E402
import capstone  # noqa: E402

x = Xbe()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = False


def astr(va, n=48):
    b = x.bytes(va, n)
    if not b:
        return None
    s = b.split(b'\0')[0]
    if len(s) >= 3 and all(32 <= c < 127 for c in s):
        return s.decode()
    return None


def ann(v):
    if v < 0x10000 or x.off(v) is None:
        return ''
    out = []
    w = x.wstr(v, 40)
    if w and len(w) >= 2 and '<' not in w[:2]:
        out.append('L"%s"' % w)
    else:
        a = astr(v)
        if a:
            out.append('"%s"' % a)
    # descriptor?  title wstr at +0 and handler in .text at +8
    t = x.u32(v)
    h = x.u32(v + 8)
    if t and h and 0x10000 < h < 0x400000 and x.off(t) is not None:
        tw = x.wstr(t, 40)
        if tw and len(tw) >= 2:
            out.append('desc{%s}' % tw)
    if v & 0xFF000000 == 0 and 0x3F000000 <= v <= 0x47000000:
        pass
    return ' ; ' + ' '.join(out) if out else ''


def fl(v):
    try:
        f = struct.unpack('<f', struct.pack('<I', v))[0]
        if 0x3C000000 <= v <= 0x47FFFFFF or 0xBC000000 <= v <= 0xC7FFFFFF:
            return ' ; %gf' % f
    except Exception:
        pass
    return ''


def dis(va, n=400, all_=False):
    o = x.off(va)
    code = x.d[o:o + n * 16]
    cnt = 0
    for ins in md.disasm(code, va):
        extra = ''
        for tok in ins.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
            if tok.startswith('0x'):
                try:
                    v = int(tok, 16)
                except ValueError:
                    continue
                extra += ann(v) or fl(v)
        print('  %08X  %-8s %-40s%s' % (ins.address, ins.mnemonic, ins.op_str, extra))
        cnt += 1
        if cnt >= n:
            break
        if not all_ and ins.mnemonic in ('ret', 'retn'):
            break


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    a = int(args[0].replace('sub_', ''), 16)
    n = int(args[1]) if len(args) > 1 else 400
    dis(a, n, '--all' in sys.argv)
