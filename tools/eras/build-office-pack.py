"""My NFL Eras office packages, built from the player's own disc (docs/eras/ERAS-OFFICE-DESIGN.md T3.0).

    python -I tools/eras/build-office-pack.py            write the two packages
    python -I tools/eras/build-office-pack.py --check    list them and compare with the disc

Writes mods/eras/office/files/ (gitignored: these are retail data and never committed):

  erasoffice.iff  byte copies of the SCNE records coach_desk and front_office and the
                  LAYT records coach_desk and front_office out of fr.iff (archive entry 23),
                  each with its 0x20-byte record header, at 16-byte boundaries. The payloads
                  stay as they are on the disc (VC-LZ, magic 0xFEEDBEEF). The game loads the
                  file under its own context (ERASOFFICE), never Franchise's FRANCHISE1.
  erasskin.iff    fr31.iff byte for byte (the NFL shield logo and the black-and-white action
                  photo atlases of the spare team code 31), loaded as ERASSKIN.

The archive is read through its own index: bank 0 starts with a 0x9C-byte header (entry
count, 16 banks, bank sizes in 2 KB blocks) and 12-byte entries {CRC32 of the upper-cased
UTF-16 name, size, offset in 2 KB blocks}; the banks are concatenated.
"""
import hashlib, os, struct, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DISC = os.path.join(ROOT, 'original', 'disc', 'vc_53450030')
OUT = os.path.join(ROOT, 'mods', 'eras', 'office', 'files')
WANT = (('SCNE', 'coach_desk'), ('SCNE', 'front_office'), ('LAYT', 'coach_desk'), ('LAYT', 'front_office'))


def crc(name):
    c = 0xFFFFFFFF
    for ch in name.upper():
        for b in (ord(ch), 0):
            c ^= b
            for _ in range(8):
                c = (c >> 1) ^ (0xEDB88320 & (-(c & 1) & 0xFFFFFFFF))
    return (~c) & 0xFFFFFFFF


def archive_file(name):
    banks = [os.path.join(DISC, '%X' % i) for i in range(16)]
    sizes = [os.path.getsize(b) for b in banks]
    with open(banks[0], 'rb') as f:
        head = f.read(0x9C)
        count = struct.unpack_from('<I', head, 0)[0]
        table = f.read(12 * count)
    want = crc(name)
    for i in range(count):
        h, size, block = struct.unpack_from('<III', table, 12 * i)
        if h != want:
            continue
        size &= 0x0FFFFFFF
        off = block * 2048
        bank = 0
        while bank < 15 and off >= sizes[bank]:
            off -= sizes[bank]
            bank += 1
        with open(banks[bank], 'rb') as f:
            f.seek(off)
            return f.read(size)
    sys.exit('not in the archive index: ' + name)


def u16z(p, o):
    s = ''
    while 0 <= o and o + 1 < len(p):
        c = struct.unpack_from('<H', p, o)[0]
        if not c:
            break
        s += chr(c)
        o += 2
    return s


def vclz(stream):
    out_size = struct.unpack_from('<I', stream, 0)[0]
    bits = stream[8]
    dmask, lmask = (1 << bits) - 1, (1 << (16 - bits)) - 1
    out = bytearray()
    src = 10
    flags, fm = stream[9], 1
    while len(out) < out_size:
        if flags & fm:
            code = struct.unpack_from('<H', stream, src)[0]
            src += 2
            dist, ln = code & dmask, ((code >> bits) & lmask) + 3
            if dist == 0 or dist > len(out):
                break
            st = len(out) - dist
            for k in range(ln):
                out.append(out[st + k])
        else:
            out.append(stream[src])
            src += 1
        fm = (fm << 1) & 0xFF
        if fm == 0 and len(out) < out_size:
            flags, fm = stream[src], 1
            src += 1
    return bytes(out[:out_size])


def records(d):
    """(offset, tag, record bytes incl. the 0x20 header) of every record."""
    o = 0
    while o + 0x20 <= len(d):
        tag = d[o:o + 4]
        if not all(65 <= c <= 90 for c in tag):
            break
        size = struct.unpack_from('<I', d, o + 4)[0]
        yield o, tag.decode(), d[o:o + 0x20 + size]
        o += 0x20 + size
        if o < len(d) and d[o:o + 4] == b'\0\0\0\0':
            o = (o + 15) & ~15


def record_name(tag, rec):
    body = rec[0x20:]
    p = vclz(body) if struct.unpack_from('<I', rec, 0x10)[0] == 0xFEEDBEEF else body
    if tag == 'SCNE':        # self-relative pointer (bit 0 set) at +0x10 to the UTF-16 name
        v = struct.unpack_from('<i', p, 0x10)[0]
        return u16z(p, 0x10 - 1 + v) if v else ''
    for i in range(0, min(len(p), 0x80) - 2, 2):     # LAYT: the first UTF-16 run
        s = u16z(p, i)
        if len(s) >= 3 and all(32 <= ord(c) < 127 for c in s):
            return s
    return ''


def pick(src):
    got = {}
    for o, tag, rec in records(src):
        if tag in ('SCNE', 'LAYT'):
            n = record_name(tag, rec)
            if (tag, n) in WANT and (tag, n) not in got:
                got[(tag, n)] = rec
    missing = [w for w in WANT if w not in got]
    if missing:
        sys.exit('fr.iff: missing %s' % missing)
    return [got[w] for w in WANT]


def pad16(b):
    return b + b'\0' * (-len(b) & 15)


def main():
    fr = archive_file('fr.iff')
    skin = archive_file('fr31.iff')
    recs = pick(fr)
    office = b''.join(pad16(r) for r in recs)
    if '--check' in sys.argv:
        ok = True
        for fn, want in (('erasoffice.iff', office), ('erasskin.iff', skin)):
            p = os.path.join(OUT, fn)
            if not os.path.exists(p):
                print('%s: MISSING' % fn)
                ok = False
                continue
            have = open(p, 'rb').read()
            if fn == 'erasoffice.iff' and len(have) > len(want):
                # a fifth record, coach_d80s, is the made IBM PC desk (build-office-ibmpc.py)
                same = have[:len(want)] == want
            else:
                same = have == want
            ok &= same
            print('%s: %d bytes, sha1 %s, %s' % (fn, len(have), hashlib.sha1(have).hexdigest()[:12],
                                                 'matches the disc' if same else 'DIFFERS from the disc'))
            for o, tag, rec in records(have):
                print('  %08X %s %8d %-14s payload sha1 %s' % (o, tag, len(rec) - 0x20, record_name(tag, rec),
                                                            hashlib.sha1(rec[0x20:]).hexdigest()[:12]))
        src = {hashlib.sha1(r[0x20:]).hexdigest() for _, _, r in records(fr)} | \
              {hashlib.sha1(r[0x20:]).hexdigest() for _, _, r in records(skin)}
        for fn in ('erasoffice.iff', 'erasskin.iff'):
            p = os.path.join(OUT, fn)
            if os.path.exists(p):
                for _, tag, rec in records(open(p, 'rb').read()):
                    if hashlib.sha1(rec[0x20:]).hexdigest() not in src:
                        made = record_name(tag, rec) == 'coach_d80s'
                        print('  %s %s %s: %s' % (fn, tag, record_name(tag, rec),
                              'made from coach_desk (the IBM PC 5150, build-office-ibmpc.py)' if made else 'payload not found on the disc'))
                        ok = ok and made
        print('check: %s' % ('PASS' if ok else 'FAIL'))
        sys.exit(0 if ok else 1)
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'erasoffice.iff'), 'wb') as f:
        f.write(office)
    with open(os.path.join(OUT, 'erasskin.iff'), 'wb') as f:
        f.write(skin)
    print('wrote %s (%d bytes: %s) and erasskin.iff (%d bytes)' %
          (os.path.join(OUT, 'erasoffice.iff'), len(office), ', '.join('%s %s' % w for w in WANT), len(skin)))
    # the 1980s office's IBM PC 5150 (optional: needs 2K Football Mod Tools)
    import subprocess
    subprocess.call([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'build-office-ibmpc.py')])


if __name__ == '__main__':
    main()
