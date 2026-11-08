"""NCAA 07 body animation decoder, following the game's own code (default.xbe).

ANIMDATA payloads (.dat/.app) hold a BFME container of MCFB (body) / MFFB
blocks. A compressed MCFB block (flag word +6 bit 0) is decoded exactly as the
game does it:

  0x1D8CF0  frame fetch: copy raw frames, or call the codec for compressed ones
  0x1C65B0  codec(out, packet, shared, mirror):
              header = 10 bytes, 16 if packet[0] bit 6, +6 if bit 7; copied raw
              channels unpacked by 0x1C0830, then for the first 88 channels
              value = A + ((q * B) >> 16) * 2          (MMX pmulhw/psllw/paddw)
              with A/B = shared arrays 1/2 (mirrored playback: 3/4)
  0x1C0830  unpack: field widths are the high bytes of shared array 5; fields
              are read in channel order from a little-endian bitstream (64-bit
              words, lowest bit first, 0x1D5850) and left-aligned into 16 bits:
              q = field << (16 - width); mirrored playback stores channel k at
              index (array5[k] & 255).

Block (relative offsets): +4 u16 frame count, +6 u16 flags, +0x10 frame table
(u32 offsets of each frame's packet), +0x14 shared section = {u32 channels,
u32 A, B, C, D, E} (offsets).
"""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np


def _u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def blocks(data: bytes):
    """(tag, offset, size) of every block in a BFME container payload."""
    em = _u32(data, 0) + 4
    if data[em:em + 4] != b'BFME':
        raise ValueError('not a BFME animation payload')
    count = _u32(data, em + 8) * _u32(data, em + 12)
    out = []
    for i in range(count):
        o = em + 16 + i * 16
        tag = data[o + 4:o + 8].decode('ascii', 'replace')
        size, start = struct.unpack_from('<II', data, o + 8)
        if size:
            out.append((tag, start, size))
    return out


class Block:
    def __init__(self, data: bytes, start: int, size: int):
        self.data = data
        self.start = start
        self.size = size
        b = data
        self.frames = struct.unpack_from('<H', b, start + 4)[0]
        self.flags = struct.unpack_from('<H', b, start + 6)[0]
        table = _u32(b, start + 0x10)
        shared = _u32(b, start + 0x14)
        self.offsets = struct.unpack_from(f'<{self.frames}I', b, start + table)
        n, a, bb, c, d, e = struct.unpack_from('<6I', b, start + shared)
        self.channels = n
        arr = lambda off: np.frombuffer(b, '<i2', n, start + off).astype(np.int32)
        self.A, self.B, self.C, self.D = arr(a), arr(bb), arr(c), arr(d)
        desc = np.frombuffer(b, '<u2', n, start + e).astype(np.int32)
        self.widths = desc >> 8
        self.mirror_index = desc & 255

    @property
    def compressed(self):
        return bool(self.flags & 1)

    def frame(self, index: int, mirror: bool = False):
        """(header bytes, int16[channels]) of one frame, as the game decodes it."""
        b = self.data
        p = self.start + self.offsets[index]
        if not self.compressed:
            raw = np.frombuffer(b, '<i2', self.channels, p).astype(np.int32)
            return b'', raw
        flags = b[p] >> 6
        hdr = 10 + (6 if flags & 1 else 0) + (6 if flags & 2 else 0)
        header = bytes(b[p:p + hdr])
        # LSB-first bitstream from p + hdr
        need = (int(self.widths.sum()) + 7) // 8 + 8
        stream = int.from_bytes(b[p + hdr:p + hdr + need], 'little')
        q = np.zeros(self.channels, dtype=np.int32)
        pos = 0
        for k in range(self.channels):
            w = int(self.widths[k])
            field = (stream >> pos) & ((1 << w) - 1) if w else 0
            pos += w
            v = (field << (16 - w)) & 0xFFFF if w < 16 else field & 0xFFFF
            slot = int(self.mirror_index[k]) if mirror else k
            q[slot] = v - 0x10000 if v & 0x8000 else v
        base, span = (self.C, self.D) if mirror else (self.A, self.B)
        out = q.copy()
        n = (self.channels - 2) // 4 * 4 if self.channels >= 4 else 0   # MMX loop: 88 of 90
        prod = (q[:n] * span[:n]) >> 16                                  # pmulhw (signed)
        scaled = ((prod << 1) + base[:n]) & 0xFFFF                       # psllw 1, paddw
        out[:n] = np.where(scaled & 0x8000, scaled - 0x10000, scaled)
        return header, out

    def all_frames(self, mirror=False):
        return [self.frame(i, mirror) for i in range(self.frames)]


def body_rig(path: Path):
    """The 30 body channel names (1LKS name table), in channel order."""
    b = Path(path).read_bytes()
    if b[:4] != b'1LKS':
        raise ValueError('not a 1LKS rig')
    n = struct.unpack_from('<H', b, 6)[0]
    node_base, names_base = struct.unpack_from('<II', b, 8)
    names = [b[names_base + 20 * i + 4:names_base + 20 * i + 20].split(b'\0')[0].decode('ascii')
             for i in range(n)]
    return names


if __name__ == '__main__':
    import sys
    p = Path(sys.argv[1])
    data = p.read_bytes()
    for tag, start, size in blocks(data):
        if tag != 'MCFB':
            continue
        blk = Block(data, start, size)
        print(tag, start, size, 'frames', blk.frames, 'flags', hex(blk.flags), 'channels', blk.channels)
        for i in (0, blk.frames // 2):
            h, v = blk.frame(i)
            print('  frame', i, 'hdr', h.hex(), v[:15].tolist())
        break


ROOT_SCALE = 1.0 / 4096           # default.xbe 0x59FFE4


def root_motion(header: bytes):
    """Root (IDEAL_OUT) position and heading from a compressed frame header,
    as 0x1C66D0 reads it: byte 1 flags; u16 +2 heading; +4/+8 horizontal
    position (18-bit when flag 0x80: high bits in flag bits 0-1 / 3-4, sign
    flags 0x04 / 0x20); i16 +6 height. Units 1/4096 m; heading 65536 = turn."""
    if len(header) < 10:
        return None
    flags = header[1]
    heading, lo_x, height, lo_z = struct.unpack_from('<hHhH', header, 2)
    if flags & 0x80:
        x = ((flags & 3) << 16) | lo_x
        if flags & 4 and x:
            x -= 1 << 18
        z = ((flags & 0x18) << 13) | lo_z
        if flags & 0x20 and z:
            z -= 1 << 18
    else:
        x = lo_x - 0x10000 if lo_x & 0x8000 else lo_x
        z = lo_z - 0x10000 if lo_z & 0x8000 else lo_z
    return (x * ROOT_SCALE, height * ROOT_SCALE, z * ROOT_SCALE, heading / 65536.0)


def clip(data: bytes, mirror=False):
    """Every MCFB segment of a payload, concatenated: [(header, values)]."""
    frames = []
    for tag, start, size in blocks(data):
        if tag == 'MCFB':
            frames += Block(data, start, size).all_frames(mirror)
    return frames


# ---------------------------------------------------------------- library

def _first_id(path: Path):
    try:
        data = path.read_bytes()
        for tag, start, size in blocks(data):
            if tag == 'MCFB':
                return _u32(data, start) >> 16
    except Exception:
        return None
    return None


def groups(folder: Path):
    """Metadata VAP -> its payload files, in archive order. Payload groups are
    found where the first MCFB identifier restarts (ChatGPT's audit; checked
    against every metadata record's payload index and segment count)."""
    folder = Path(folder)
    payloads = sorted(p for p in folder.iterdir() if p.suffix in ('.dat', '.app') and _first_id(p) is not None)
    runs, run, previous = [], [], 1 << 20
    for p in payloads:
        ident = _first_id(p)
        if ident <= previous and run:
            runs.append(run)
            run = []
        run.append(p)
        previous = ident
    if run:
        runs.append(run)
    vaps = [p for p in sorted(folder.glob('*.vap')) if p.name != '00000001.vap']
    if len(vaps) != len(runs):
        raise ValueError(f'{len(vaps)} metadata files but {len(runs)} payload groups')
    return dict(zip(vaps, runs))


def _tag(raw: bytes) -> str:
    text = raw[::-1].decode('ascii', 'replace').strip('\0').replace('_', '')
    return text if text and text != 'NONE' else ''


def records(vap: Path, payloads: list):
    """Every animation a metadata file names: (name, payload, mirror)."""
    b = Path(vap).read_bytes()
    n = _u32(b, 4)
    offsets = struct.unpack_from(f'<{n}I', b, 8)
    out = []
    for i, o in enumerate(offsets):
        index = _u32(b, o) >> 16
        mirror = bool(struct.unpack_from('<H', b, o + 8)[0] & 0x8000)
        tag = _u32(b, o + 24)
        name = '_'.join(t for t in (_tag(b[tag:tag + 4]), _tag(b[tag + 20:tag + 24])) if t)
        label = f'{Path(vap).stem[-3:]}_{i:03d}' + (f'_{name}' if name else '') + ('_M' if mirror else '')
        if index < len(payloads):
            out.append((label, payloads[index], mirror))
    return out


def library_clips(vap: Path, payloads: list, rig: list, fps: float = 30.0, limit: int | None = None):
    clips = []
    for label, payload, mirror in records(vap, payloads)[:limit]:
        frames = clip(payload.read_bytes(), mirror)
        if frames:
            clips.append({'name': label, 'frames': frames, 'rig': rig, 'fps': fps, 'mirror': mirror})
    return clips


def catalog(folder: Path):
    """App rows: one per animation group (metadata VAP)."""
    rows = []
    for vap, payloads in groups(folder).items():
        recs = records(vap, payloads)
        tags = sorted({r[0].split('_', 2)[2].replace('_M', '') for r in recs if r[0].count('_') >= 2})
        rows.append({'name': f'Animations {vap.stem[-3:]}: {len(recs)} clips ({", ".join(tags[:6])})',
                     'file': str(vap.resolve()), 'nodes': len(recs), 'bones': 30, 'textures': False,
                     'archive_index': vap.stem, 'kind': 'animations'})
    return rows


def body_rig_file(folder: Path) -> Path:
    for p in sorted(Path(folder).glob('*.1lk')):
        try:
            if 'rknee' in body_rig(p):
                return p
        except Exception:
            continue
    raise FileNotFoundError('No 30-bone body rig (.1lk) in the animation folder')
