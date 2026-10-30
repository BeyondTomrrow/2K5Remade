"""Find code/data references to a 32-bit value in the retail XBE (read-only).

    python build/eras/office/xref.py 522190 [52533C ...]

Prints each hit with its section; for .text hits, the enclosing generated
function (build/gen-locals) and the instruction at the hit.
"""
import bisect, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fsm_graph import x, fn_starts, md  # noqa: E402


def hits(v):
    pat = struct.pack('<I', v)
    out = []
    for name, va, vsz, raw, rsz in x.secs:
        seg = x.d[raw:raw + rsz]
        i = seg.find(pat)
        while i >= 0:
            out.append((name, va + i))
            i = seg.find(pat, i + 1)
    return out


def insn_at(addr):
    st = fn_starts()
    k = bisect.bisect_right(st, addr) - 1
    fn = st[k] if k >= 0 else None
    if fn is None:
        return None, None
    o = x.off(fn)
    for ins in md.disasm(x.d[o:o + (addr - fn) + 16], fn):
        if ins.address <= addr < ins.address + ins.size:
            return fn, '%08X %s %s' % (ins.address, ins.mnemonic, ins.op_str)
    return fn, '?'


if __name__ == '__main__':
    for a in sys.argv[1:]:
        v = int(a, 16)
        print('== %08X %r' % (v, x.wstr(x.u32(v)) if x.u32(v) and x.off(x.u32(v)) else None))
        for sec, va in hits(v):
            if sec == '.text':
                fn, ins = insn_at(va)
                print('   %-8s %08X  in sub_%08X: %s' % (sec, va, fn or 0, ins))
            else:
                print('   %-8s %08X' % (sec, va))
