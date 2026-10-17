"""Tiny XBE reader: map guest VA -> file bytes (original/default.xbe)."""
import struct, os
XBE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'original', 'default.xbe')


class Xbe:
    def __init__(self, path=XBE):
        self.d = open(path, 'rb').read()
        d = self.d
        self.base = struct.unpack_from('<I', d, 0x104)[0]
        nsec = struct.unpack_from('<I', d, 0x11C)[0]
        sh = struct.unpack_from('<I', d, 0x120)[0] - self.base
        self.secs = []
        for i in range(nsec):
            flags, va, vsz, raw, rsz, nameaddr = struct.unpack_from('<IIIIII', d, sh + i * 56)
            name = d[nameaddr - self.base:].split(b'\0')[0].decode('latin1')
            self.secs.append((name, va, vsz, raw, rsz))

    def off(self, va):
        for name, sva, vsz, raw, rsz in self.secs:
            if sva <= va < sva + rsz:
                return raw + (va - sva)
        return None

    def u32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from('<I', self.d, o)[0]

    def f32(self, va):
        o = self.off(va)
        return None if o is None else struct.unpack_from('<f', self.d, o)[0]

    def bytes(self, va, n):
        o = self.off(va)
        return None if o is None else self.d[o:o + n]

    def wstr(self, va, n=128):
        if not va:
            return None
        o = self.off(va)
        if o is None:
            return None
        out = []
        for i in range(n):
            if o + 2 * i + 2 > len(self.d):
                break
            c = struct.unpack_from('<H', self.d, o + 2 * i)[0]
            if c == 0:
                break
            out.append(chr(c) if 32 <= c < 0x7F else '<%04x>' % c)
        return ''.join(out)

    def sec_of(self, va):
        for name, sva, vsz, raw, rsz in self.secs:
            if sva <= va < sva + vsz:
                return name
        return None


if __name__ == '__main__':
    x = Xbe()
    for s in x.secs:
        print('%-10s va %08X vsz %08X raw %08X rsz %08X' % s)
