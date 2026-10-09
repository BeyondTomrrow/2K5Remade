"""Minimal read-only XDVDFS (Xbox disc image) reader.

    python tools/xiso_read.py <image.iso> ls
    python tools/xiso_read.py <image.iso> cat <path> <offset> <length> > out.bin

Used to compare a pack's overlay with the finished disc image it was built
from (tools/verify-overlay.py).
"""
import struct
import sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048


class XIso:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.base = None
        for off in (0, 0x18300000, 0xFD90000, 0x2090000, 0x89D80000, 0x2080000):
            self.f.seek(off + 32 * SECTOR)
            if self.f.read(20) == MAGIC:
                self.base = off
                break
        if self.base is None:
            raise SystemExit("not an XDVDFS image")
        self.f.seek(self.base + 32 * SECTOR + 20)
        self.root_sector, self.root_size = struct.unpack("<II", self.f.read(8))
        self.files = {}
        self._walk(self.root_sector, self.root_size, "")

    def _read(self, sector, size):
        self.f.seek(self.base + sector * SECTOR)
        return self.f.read(size)

    def _walk(self, sector, size, prefix):
        data = self._read(sector, size)
        seen = set()

        def entry(off):
            if off in seen or off + 14 > len(data):
                return
            seen.add(off)
            left, right, start, fsize, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
            if left == 0xFFFF and right == 0xFFFF and start == 0xFFFFFFFF:
                return
            name = data[off + 14:off + 14 + nlen].decode("latin-1")
            path = prefix + name
            if attr & 0x10:
                if fsize:
                    self._walk(start, fsize, path + "/")
            else:
                self.files[path] = (start, fsize)
            if left:
                entry(left * 4)
            if right:
                entry(right * 4)
        entry(0)

    def read(self, path, offset, length):
        start, size = self.files[path]
        length = max(0, min(length, size - offset))
        self.f.seek(self.base + start * SECTOR + offset)
        return self.f.read(length)


def main():
    iso = XIso(sys.argv[1])
    if sys.argv[2] == "ls":
        for p, (s, n) in sorted(iso.files.items()):
            print("%12d  %s" % (n, p))
    elif sys.argv[2] == "cat":
        sys.stdout.buffer.write(iso.read(sys.argv[3], int(sys.argv[4]), int(sys.argv[5])))


if __name__ == "__main__":
    main()
