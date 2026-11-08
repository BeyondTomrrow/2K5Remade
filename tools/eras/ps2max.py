"""Unpack Action Replay MAX (.max) PS2 saves into save directories.

    python -I tools/eras/ps2max.py <in .max or folder> <out folder>

A .max file: 0x5C-byte header ("Ps2PowerSave", CRC-32, save directory name,
title, compressed size, file count, unpacked size) then the files, LZARI
compressed (Haruhiko Okumura's LZARI: N 4096, F 60, threshold 2, 15-bit
arithmetic coder). Unpacked: per file u32 size, 32-byte name, the data,
then padding to the next 16-byte boundary of the stream (+8). Each save is
written to <out>/<directory name>/<file>, the layout tools/nfl2k5_ps2_save.py
reads. Data only: nothing in a save is executed.
"""
import struct
import sys
import zlib
from pathlib import Path

N, F, THRESHOLD = 4096, 60, 2
M = 15
Q1, Q2, Q3, Q4 = 1 << M, 2 << M, 3 << M, 4 << M
MAX_CUM = Q1 - 1
N_CHAR = 256 - THRESHOLD + F


def lzari_decode(src: bytes, size: int) -> bytes:
    bitpos = 0
    nbits = len(src) * 8

    def getbit():
        nonlocal bitpos
        if bitpos >= nbits:
            bitpos += 1
            return 0
        b = (src[bitpos >> 3] >> (7 - (bitpos & 7))) & 1
        bitpos += 1
        return b

    char_to_sym = [0] * N_CHAR
    sym_to_char = [0] * (N_CHAR + 1)
    sym_freq = [0] * (N_CHAR + 1)
    sym_cum = [0] * (N_CHAR + 1)
    pos_cum = [0] * (N + 1)
    for sym in range(N_CHAR, 0, -1):
        ch = sym - 1
        char_to_sym[ch] = sym
        sym_to_char[sym] = ch
        sym_freq[sym] = 1
        sym_cum[sym - 1] = sym_cum[sym] + sym_freq[sym]
    sym_freq[0] = 0
    for i in range(N, 0, -1):
        pos_cum[i - 1] = pos_cum[i] + 10000 // (i + 200)

    def update(sym):
        if sym_cum[0] >= MAX_CUM:
            c = 0
            for i in range(N_CHAR, 0, -1):
                sym_cum[i] = c
                sym_freq[i] = (sym_freq[i] + 1) >> 1
                c += sym_freq[i]
            sym_cum[0] = c
        i = sym
        while sym_freq[i] == sym_freq[i - 1]:
            i -= 1
        if i < sym:
            ch_i, ch_sym = sym_to_char[i], sym_to_char[sym]
            sym_to_char[i], sym_to_char[sym] = ch_sym, ch_i
            char_to_sym[ch_i], char_to_sym[ch_sym] = sym, i
        sym_freq[i] += 1
        while i > 0:
            i -= 1
            sym_cum[i] += 1

    low, high, value = 0, Q4, 0
    for _ in range(M + 2):
        value = 2 * value + getbit()

    def renorm():
        nonlocal low, high, value
        while True:
            if low >= Q2:
                value -= Q2; low -= Q2; high -= Q2
            elif low >= Q1 and high <= Q3:
                value -= Q1; low -= Q1; high -= Q1
            elif high > Q2:
                break
            low += low; high += high
            value = 2 * value + getbit()

    def decode_char():
        nonlocal low, high
        rng = high - low
        x = ((value - low + 1) * sym_cum[0] - 1) // rng
        i, j = 1, N_CHAR
        while i < j:
            k = (i + j) >> 1
            if sym_cum[k] > x: i = k + 1
            else: j = k
        sym = i
        high = low + (rng * sym_cum[sym - 1]) // sym_cum[0]
        low += (rng * sym_cum[sym]) // sym_cum[0]
        renorm()
        ch = sym_to_char[sym]
        update(sym)
        return ch

    def decode_position():
        nonlocal low, high
        rng = high - low
        x = ((value - low + 1) * pos_cum[0] - 1) // rng
        i, j = 1, N
        while i < j:
            k = (i + j) >> 1
            if pos_cum[k] > x: i = k + 1
            else: j = k
        pos = i - 1
        high = low + (rng * pos_cum[pos]) // pos_cum[0]
        low += (rng * pos_cum[pos + 1]) // pos_cum[0]
        renorm()
        return pos

    text = bytearray(b' ' * N)
    r = N - F
    out = bytearray()
    while len(out) < size:
        c = decode_char()
        if c < 256:
            out.append(c); text[r] = c; r = (r + 1) & (N - 1)
        else:
            i = (r - decode_position() - 1) & (N - 1)
            for k in range(c - 255 + THRESHOLD):
                b = text[(i + k) & (N - 1)]
                out.append(b); text[r] = b; r = (r + 1) & (N - 1)
    return bytes(out[:size])


def unpack(path: Path):
    raw = path.read_bytes()
    if raw[:12] != b'Ps2PowerSave':
        raise ValueError(f'{path.name}: not an Action Replay MAX save')
    crc, = struct.unpack_from('<I', raw, 12)
    dirname = raw[0x10:0x30].split(b'\0')[0].decode('ascii', 'replace')
    title = raw[0x30:0x50].split(b'\0')[0].decode('ascii', 'replace')
    csize, nfiles, usize = struct.unpack_from('<III', raw, 0x50)
    body = lzari_decode(raw[0x5C:0x5C + csize], usize)
    check = zlib.crc32(raw[:12] + b'\0\0\0\0' + raw[16:]) & 0xFFFFFFFF
    files, off = [], 0
    for _ in range(nfiles):
        size, = struct.unpack_from('<I', body, off)
        name = body[off + 4:off + 36].split(b'\0')[0].decode('ascii', 'replace')
        data = body[off + 36:off + 36 + size]
        files.append((name, data))
        off += 36 + size
        off = ((off + 8 + 15) & ~15) - 8              # the next entry starts 8 short of a 16-byte boundary
    return dict(dirname=dirname, title=title, crc_ok=check == crc, files=files)


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    paths = sorted(src.glob('*.max')) if src.is_dir() else [src]
    for p in paths:
        try:
            s = unpack(p)
        except Exception as exc:                      # report and carry on with the rest
            print(f'{p.name}: FAILED {exc}')
            continue
        d = out / p.stem / s['dirname']          # the save directory, named like its payload
        d.mkdir(parents=True, exist_ok=True)
        for name, data in s['files']:
            (d / name).write_bytes(data)
        print(f"{p.name}: {s['dirname']} '{s['title']}' crc {'ok' if s['crc_ok'] else 'MISMATCH'}: "
              + ', '.join(f'{n} {len(b)}' for n, b in s['files']))


if __name__ == '__main__':
    main()
