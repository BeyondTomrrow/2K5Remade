"""List resources in an NFL 2K5 .iff archive file (read-only).

    python build/eras/office/iffls.py <file.iff> | --entry <name.iff>

Records are a 0x20-byte wrapper {tag, payload size, ...} followed by the payload;
the payload repeats the tag and carries a UTF-16 name. --entry finds the file by
the CRC32 of its upper-cased UTF-16 name in analysis/archive-index.csv and reads
it from original/disc/vc_53450030/<bank>.
"""
import csv, os, struct, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))


def crc(name):
    c = 0xFFFFFFFF
    for ch in name.upper():
        for b in (ord(ch), 0):
            c ^= b
            for _ in range(8):
                c = (c >> 1) ^ (0xEDB88320 & (-(c & 1) & 0xFFFFFFFF))
    return (~c) & 0xFFFFFFFF


def entry(name):
    want = crc(name)
    for r in csv.DictReader(open(os.path.join(ROOT, 'analysis', 'archive-index.csv'))):
        if int(r['id'], 16) == want:
            with open(os.path.join(ROOT, 'original', 'disc', 'vc_53450030', r['bank']), 'rb') as f:
                f.seek(int(r['offset']))
                return f.read(int(r['size_candidate']))
    return None


def wname(d, o, lim):
    best = ''
    i = o
    while i < lim - 2:
        j = i
        s = ''
        while j + 1 < lim and 32 <= d[j] < 127 and d[j + 1] == 0:
            s += chr(d[j]); j += 2
        if len(s) >= 3:
            return s
        i += 2 if j == i else (j - i)
    return best


def records(d):
    o = 0
    while o + 8 <= len(d):
        tag = d[o:o + 4]
        if not all(65 <= c <= 90 for c in tag):
            break
        size = struct.unpack_from('<I', d, o + 4)[0]
        yield o, tag.decode(), size, wname(d, o + 0x20, min(len(d), o + 0x20 + 0x60))
        o += 0x20 + size
        o = (o + 15) & ~15 if o < len(d) and d[o:o+4] == b'\0\0\0\0' else o


if __name__ == '__main__':
    if sys.argv[1] == '--entry':
        d = entry(sys.argv[2])
        if d is None:
            sys.exit('not in archive index: ' + sys.argv[2])
    else:
        d = open(sys.argv[1], 'rb').read()
    print('size', len(d))
    for o, tag, size, name in records(d):
        print('  %08X %s %8d %s' % (o, tag, size, name))


def vclz(stream, limit=None):
    """VC-LZ decoder (same token rules as the mod tools' decode_vc_lz_prefix)."""
    out_size = struct.unpack_from('<I', stream, 0)[0]
    bits = stream[8]
    dmask = (1 << bits) - 1
    lmask = (1 << (16 - bits)) - 1
    want = out_size if limit is None else min(limit, out_size)
    out = bytearray()
    src = 9
    flags = stream[src]; src += 1
    fm = 1
    while len(out) < want:
        if flags & fm:
            if src + 2 > len(stream):
                break
            code = struct.unpack_from('<H', stream, src)[0]; src += 2
            dist = code & dmask
            ln = ((code >> bits) & lmask) + 3
            if dist == 0 or dist > len(out):
                break
            st = len(out) - dist
            for k in range(ln):
                out.append(out[st + k])
        else:
            if src >= len(stream):
                break
            out.append(stream[src]); src += 1
        fm = (fm << 1) & 0xFF
        if fm == 0 and len(out) < want:
            if src >= len(stream):
                break
            flags = stream[src]; src += 1
            fm = 1
    return bytes(out[:want])


def payload(d, o):
    """Decoded payload of the record at o (raw payload if not compressed)."""
    size = struct.unpack_from('<I', d, o + 4)[0]
    magic = struct.unpack_from('<I', d, o + 0x10)[0]
    body = d[o + 0x20:o + 0x20 + size]
    if magic == 0xFEEDBEEF:
        return vclz(body)
    return body


TABLES = (('aux_14', 0x0C, 0x10, 0x14), ('textures', 0x14, 0x18, 0x20), ('materials', 0x1C, 0x20, 0x80),
          ('nodes', 0x24, 0x28, 0x60), ('shapes', 0x2C, 0x30, 0x100), ('markers', 0x34, 0x38, 0x40),
          ('aux_60', 0x3C, 0x40, 0x60), ('aux_50', 0x44, 0x48, 0x50))


def rel(p, field):
    v = struct.unpack_from('<i', p, field)[0]
    return None if v == 0 else field - 1 + v


def u16z(p, o):
    s = ''
    while o + 1 < len(p):
        c = struct.unpack_from('<H', p, o)[0]
        if not c:
            break
        s += chr(c); o += 2
    return s


def scene_tables(d, o):
    """Material / node / camera names of the SCNE record at o (layout from the mod
    tools' nfl_scne_inventory.py: self-relative pointers, target = field - 1 + value)."""
    sysz = struct.unpack_from('<I', d, o + 8)[0]
    p = payload(d, o)
    desc = rel(p, 0x14)
    out = {'name': u16z(p, rel(p, 0x10)), 'system_size': sysz}
    for key, co, po, stride in TABLES:
        n = struct.unpack_from('<I', p, desc + co)[0]
        st = rel(p, desc + po)
        names = []
        if key in ('materials', 'nodes', 'aux_60', 'markers') and st is not None:
            for i in range(n):
                r = st + i * stride
                t = rel(p, r) if key != 'shapes' else None
                nm = u16z(p, t) if t is not None and 0 <= t < sysz else None
                extra = ''
                if key == 'materials':
                    tx = rel(p, r + 0x30)
                    extra = ' tex' if tx is not None else ''
                    flags = struct.unpack_from('<I', p, r + 8)[0]
                    extra += ' flags=%X' % flags
                names.append((nm, extra))
        out[key] = (n, names)
    return out


def scene_shapes(d, o):
    """[(shape name, [material names])] for the SCNE record at o."""
    p = payload(d, o)
    desc = rel(p, 0x14)
    mn = struct.unpack_from('<I', p, desc + 0x1C)[0]; ms = rel(p, desc + 0x20)
    mats = [u16z(p, rel(p, ms + i * 0x80)) for i in range(mn)]
    sn = struct.unpack_from('<I', p, desc + 0x2C)[0]; ss = rel(p, desc + 0x30)
    out = []
    for i in range(sn):
        r = ss + i * 0x100
        t = rel(p, r + 0x40)
        nm = u16z(p, t) if t is not None else '?'
        k = struct.unpack_from('<H', p, r + 0x54)[0]
        sub = rel(p, r + 0x70)
        ml = []
        for j in range(k if sub is not None else 0):
            mi = struct.unpack_from('<H', p, sub + j * 0x80)[0]
            ml.append(mats[mi] if mi < len(mats) else '#%d' % mi)
        out.append((nm, ml))
    return out
