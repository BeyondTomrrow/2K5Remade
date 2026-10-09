"""The game's own files, read from the player's disc (never shipped).

  Archive(disc_dir)            the disc's vc_53450030 archive
  .read(name)                  a file by name ("25a2.iff"), or None
  .team_files(code)            a team's uniform / helmet files by its asset code
  .roster_template(...)        a one-team roster block (the game's historic
                               teams' format) to build a new team on
  recolor_iff(data, mapping)   a uniform/helmet file with its texture palettes
                               recoloured (TSET sets and paletted TXTRs)

File names are found by CRC32 of the upper-cased UTF-16 name (the index keys
files that way), so a team's files are discovered by trying the name
patterns the game itself formats with an asset code.
"""
from __future__ import annotations

import colorsys
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "vendor"))
import nfl_outer  # noqa: E402
import nfl_txtr as T  # noqa: E402

# Historic teams the creator can build on: (file, label). Their rosters are the
# game's one-team format; the creator replaces every player and the identity.
TEMPLATES = ("h-25-1994-49ers-2.iff", "h-22-1995-steelers-0.iff", "h-15-1998-vikings-0.iff")


def name_id(name: str) -> int:
    return zlib.crc32(name.upper().encode("utf-16le")) & 0xFFFFFFFF


class Archive:
    def __init__(self, disc_dir: str | Path):
        self.dir = Path(disc_dir)
        self.a = nfl_outer.parse_archive(self.dir / "vc_53450030" / "0")
        self.ids = {e.name_id: e for e in self.a.entries}

    def has(self, name: str) -> bool:
        return name_id(name) in self.ids

    def read(self, name: str) -> bytes | None:
        e = self.ids.get(name_id(name))
        return nfl_outer.read_entry_range(self.a, e, 0, e.size) if e else None

    def team_files(self, code: str) -> list[str]:
        """Uniform and helmet files of an asset code (the patterns of 0xE61060..)."""
        found = []
        for side in "ha":
            for style in range(10):
                n = f"{code}{side}{style}.iff"
                if self.has(n):
                    found.append(n)
            for c1 in "abcdefghijklmnopqrstuvwxyz0123456789":
                for c2 in "abcdefghijklmnopqrstuvwxyz0123456789":
                    n = f"{code}{side}{c1}{c2}.iff"
                    if self.has(n):
                        found.append(n)
        return found

    def roster_template(self, name: str = TEMPLATES[0]) -> bytes:
        data = self.read(name)
        if data is None:
            raise FileNotFoundError(name)
        for c in T.parse_chunks(data, allow_trailing=True):
            if c.kind == "ROST":
                body, _ = T.decode_chunk(data, c)
                return body[0x40:]          # what the game hands its import (0xC1030)
        raise ValueError(f"{name} has no roster")


# ---- recolouring --------------------------------------------------------------

def _hls(rgb):
    return colorsys.rgb_to_hls(*(c / 255 for c in rgb))


def make_mapper(base_colors: list[tuple[int, int, int]], new_colors: list[tuple[int, int, int]]):
    """A palette colour -> new colour function: each saturated colour goes to
    the new colour of the base colour nearest in hue, keeping its shading
    (lightness relative to the base colour); greys, white and black stay."""
    base = [_hls(c) for c in base_colors]
    new = [_hls(c) for c in new_colors]

    def f(rgb):
        h, l, s = _hls(rgb)
        if s < 0.18 or l < 0.06 or l > 0.97:
            return rgb
        best, bd = 0, 9.0
        for i, (bh, bl, bs) in enumerate(base):
            if bs < 0.15:
                continue
            d = min(abs(h - bh), 1 - abs(h - bh)) + abs(l - bl) * 0.3
            if d < bd:
                best, bd = i, d
        bh, bl, bs = base[best]
        nh, nl, ns = new[best]
        nl2 = max(0.0, min(1.0, nl + (l - bl)))     # same shading step around the colour
        ns2 = max(0.0, min(1.0, ns * (s / bs if bs else 1)))
        r, g, b = colorsys.hls_to_rgb(nh, nl2, ns2)
        return (round(r * 255), round(g * 255), round(b * 255))
    return f


def _recolor_palette(buf: bytearray, at: int, f) -> None:
    for i in range(256):
        b, g, r, a = buf[at + i * 4: at + i * 4 + 4]
        nr, ng, nb = f((r, g, b))
        buf[at + i * 4: at + i * 4 + 3] = bytes((nb, ng, nr))


def recolor_iff(data: bytes, f) -> bytes:
    """Every TSET texture set's palettes (clean and mud, the last 2 KB of its
    video data) and every 8-bit paletted TXTR's palette, recoloured by f."""
    chunks = T.parse_chunks(data, allow_trailing=True)
    out = bytearray()
    for c in chunks:
        raw = data[c.offset:c.end_offset]
        if c.kind in ("TSET", "TXTR") and c.compressed:
            dec, _ = T.decode_chunk(data, c)
            dec = bytearray(dec)
            changed = False
            if c.kind == "TSET":
                n = len(dec) - c.system_bytes
                for po in (n - 2048, n - 1024):
                    if po >= 0:
                        _recolor_palette(dec, c.system_bytes + po, f)
                        changed = True
            else:
                try:
                    info = T.parse_texture(bytes(dec), c)
                    if getattr(info, "palette_offset", None) and info.format_code in (0x0B,):
                        _recolor_palette(dec, c.system_bytes + info.palette_offset, f)
                        changed = True
                except Exception:
                    pass
            if changed:
                comp, _ = T.compress_vc_lz(bytes(dec))
                scratch = (T.minimum_vc_lz_overlap_scratch(comp, len(comp), len(dec)) + 15) & ~15
                comp = comp + bytes(-len(comp) % 16)
                raw = struct.pack("<4s7I", c.kind.encode(), len(comp), c.system_bytes, c.video_bytes,
                                  c.compression_magic, scratch + 16, 0, 0) + comp
        out += raw
    out += data[chunks[-1].end_offset:]
    return bytes(out)


def texture_previews(data: bytes, limit: int = 4):
    """(width, height, RGBA) of the first few TSET sets' top mip, clean palette."""
    out = []
    for c in T.parse_chunks(data, allow_trailing=True):
        if c.kind != "TSET" or not c.compressed or len(out) >= limit:
            continue
        dec, _ = T.decode_chunk(data, c)
        video = dec[c.system_bytes:]
        n = len(video)
        pal = video[n - 2048:n - 1024]
        # top mip: the largest power-of-two square that fits before the palettes
        side = 256
        while side * side > n - 2048:
            side //= 2
        idx = T.unswizzle_2d(video[:side * side], side, side, 1)
        rgba = bytearray()
        for i in idx:
            b, g, r, a = pal[i * 4:i * 4 + 4]
            rgba += bytes((r, g, b, 255))
        out.append((side, side, bytes(rgba)))
    return out
