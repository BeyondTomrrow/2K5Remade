"""Read dwords from the XBE image at guest virtual addresses.

    python tools/xbe_peek.py 0x4F6708 [count]      # dump dwords
    python tools/xbe_peek.py --fsm 0x4F6708        # decode a state descriptor

State descriptors (see PROJECT_STATUS.md, "What sub_0006E4E0 ... are"):
desc = {0, msg_list, ?}; msg_list = {msg_id, handler_record} pairs ending in
msg_id 0; a type-1 record {1, function} is `call [rec+4]` with ecx = fsm.
"""
import os
import struct
import sys

XBE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "original", "disc", "default.xbe")


class Xbe:
    def __init__(self, path=XBE):
        self.data = open(path, "rb").read()
        base, = struct.unpack_from("<I", self.data, 0x104)
        nsec, secaddr = struct.unpack_from("<II", self.data, 0x11C)
        self.sections = []
        for i in range(nsec):
            off = secaddr - base + i * 56
            _, va, vsize, raw, rsize = struct.unpack_from("<IIIII", self.data, off)
            self.sections.append((va, vsize, raw, rsize))

    def u32(self, va):
        for sva, vsize, raw, rsize in self.sections:
            if sva <= va < sva + vsize:
                o = va - sva
                if o + 4 > rsize:
                    return 0          # .bss
                return struct.unpack_from("<I", self.data, raw + o)[0]
        raise ValueError("0x%X not in the image" % va)


def fsm(x, desc):
    print("desc %08X = {%08X, %08X, %08X}" % (desc, x.u32(desc), x.u32(desc + 4), x.u32(desc + 8)))
    lst = x.u32(desc + 4)
    for i in range(32):
        msg, rec = x.u32(lst + 8 * i), x.u32(lst + 8 * i + 4)
        if msg == 0 and rec == 0:
            break
        try:
            typ, fn = x.u32(rec), x.u32(rec + 4)
            print("  msg %2d -> rec %08X type %d arg %08X" % (msg, rec, typ, fn))
        except ValueError:
            print("  msg %2d -> %08X" % (msg, rec))


if __name__ == "__main__":
    x = Xbe()
    if sys.argv[1] == "--fsm":
        for a in sys.argv[2:]:
            fsm(x, int(a, 0))
    else:
        va = int(sys.argv[1], 0)
        n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 8
        for i in range(n):
            print("%08X: %08X" % (va + 4 * i, x.u32(va + 4 * i)))
