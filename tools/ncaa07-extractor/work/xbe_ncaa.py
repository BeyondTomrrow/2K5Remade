"""NCAA 07 default.xbe helpers: section map, find immediates, disassemble.

  python work/xbe_ncaa.py find 0x4246434D          # code using an immediate
  python work/xbe_ncaa.py dis 0x123456 0x123600     # disassemble a VA range
  python work/xbe_ncaa.py str MCFB                  # where a byte string lives
"""
import struct
import sys
from pathlib import Path

import capstone

XBE = Path(r'H:\07\NCAA Football 07  (axekin.com)\default.xbe')


class Xbe:
    def __init__(self, path=XBE):
        self.data = d = Path(path).read_bytes()
        base = struct.unpack_from('<I', d, 0x104)[0]
        n = struct.unpack_from('<I', d, 0x11C)[0]
        table = struct.unpack_from('<I', d, 0x120)[0] - base
        self.sections = []
        for i in range(n):
            flags, va, vsize, raw, rsize, name_addr = struct.unpack_from('<6I', d, table + i * 56)
            name = d[name_addr - base:name_addr - base + 16].split(b'\0')[0].decode('ascii', 'replace')
            self.sections.append((name, va, vsize, raw, rsize, flags))

    def off(self, va):
        for name, sva, vs, raw, rs, fl in self.sections:
            if sva <= va < sva + rs:
                return raw + va - sva
        return None

    def va(self, off):
        for name, sva, vs, raw, rs, fl in self.sections:
            if raw <= off < raw + rs:
                return sva + off - raw
        return None

    def read(self, va, n):
        o = self.off(va)
        return self.data[o:o + n]

    def code_sections(self):
        return [s for s in self.sections if s[5] & 4]          # executable


def dis(x, lo, hi):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    for i in md.disasm(x.read(lo, hi - lo), lo):
        print('%08x  %-6s %s' % (i.address, i.mnemonic, i.op_str))


def find_imm(x, value):
    """Every instruction in executable sections with `value` as an operand."""
    needle = struct.pack('<I', value)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for name, va, vs, raw, rs, fl in x.code_sections():
        blob = x.data[raw:raw + rs]
        pos = blob.find(needle)
        while pos >= 0:
            # back up a few bytes and decode to find the instruction containing it
            for back in range(1, 8):
                start = pos - back
                ins = list(md.disasm(blob[start:start + 16], va + start, count=1))
                if ins and ins[0].address + ins[0].size >= va + pos + 4 and ins[0].address <= va + pos:
                    print('%08x  %-6s %s   [%s]' % (ins[0].address, ins[0].mnemonic, ins[0].op_str, name))
                    break
            pos = blob.find(needle, pos + 1)


if __name__ == '__main__':
    x = Xbe()
    cmd = sys.argv[1]
    if cmd == 'sections':
        for s in x.sections:
            print('%-10s va %08x vsize %08x raw %08x rsize %08x flags %x' % s)
    elif cmd == 'find':
        find_imm(x, int(sys.argv[2], 16))
    elif cmd == 'dis':
        dis(x, int(sys.argv[2], 16), int(sys.argv[3], 16))
    elif cmd == 'str':
        s = sys.argv[2].encode()
        p = x.data.find(s)
        while p >= 0:
            print('%08x (file %x)' % (x.va(p) or 0, p))
            p = x.data.find(s, p + 1)
