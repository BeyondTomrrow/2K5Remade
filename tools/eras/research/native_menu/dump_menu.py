"""Dump a 0x34-byte menu header and its 0x34-byte row array from default.xbe.

usage: python -I dump_menu.py <header_va_hex> [depth]
Follows type-0 link rows (submenu header at +8) up to `depth` levels.
"""
import sys
from xbe import Xbe

x = Xbe()
ROW = 0x34


def words(va, n):
    return [x.u32(va + 4 * i) for i in range(n)]


def show(header, depth, indent='', seen=None):
    seen = seen if seen is not None else set()
    if header in seen:
        print(indent + '(already shown %08X)' % header)
        return
    seen.add(header)
    h = words(header, ROW // 4)
    if None in h:
        print(indent + 'header %08X not in image' % header)
        return
    print(indent + 'HEADER %08X title=%r words=%s' % (header, x.wstr(h[0]), ' '.join('%08X' % w for w in h)))
    rows = h[4]
    for i in range(40):
        r = rows + ROW * i
        w = words(r, ROW // 4)
        if None in w:
            print(indent + '  row %d at %08X not in image' % (i, r))
            break
        t = w[0]
        label = x.wstr(w[1]) if w[1] and x.off(w[1]) is not None else None
        print(indent + '  row %2d @%08X type %d label=%r  %s' % (i, r, t, label, ' '.join('%08X' % v for v in w)))
        if t == 3:
            break
        if t == 0 and depth > 0 and w[2]:
            show(w[2], depth - 1, indent + '      ', seen)


if __name__ == '__main__':
    show(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 0)
