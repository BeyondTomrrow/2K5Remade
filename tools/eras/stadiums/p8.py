"""P8 (paletted, swizzled) textures for NFL 2K5 static scenes, and their decode for previews.

A scene texture record (0x20 bytes) keeps its format word at +0x0C: low byte = DMA/border/dimensionality bits
(0x29 in retail stadiums), (fmt >> 8) & 0xFF = 0x0B (P8), (fmt >> 16) & 0xF = mip levels, (fmt >> 20) & 0xF = log2
width, (fmt >> 24) & 0xF = log2 height. Pixels: every mip level, NV2A-swizzled (Morton), one index byte per pixel;
palette: 256 BGRA entries (1024 bytes). Retail chains stop at an 8-pixel side.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path

import numpy as np

VENDOR = Path(__file__).resolve().parents[2] / "team-creator" / "vendor"
if str(VENDOR) not in sys.path:
    sys.path.insert(0, str(VENDOR))
import nfl_txtr as T  # noqa: E402

P8_FORMAT = 0x0B


def _pow2_at_most(n, cap):
    p = 1
    while p * 2 <= min(n, cap):
        p *= 2
    return max(p, 8)


def levels_for(w, h):
    levels = 1
    while (w >> levels) >= 8 and (h >> levels) >= 8 and levels < 8:
        levels += 1
    return levels


def mip_chain(img, cap=128, opaque=False):
    """[RGBA uint8 arrays] for a PIL image: power-of-two sides up to ``cap``, full chain to an 8-pixel side."""
    from PIL import Image
    w = _pow2_at_most(img.width, cap)
    h = _pow2_at_most(img.height, cap)
    base = img.convert("RGBA").resize((w, h), Image.LANCZOS) if (w, h) != img.size else img.convert("RGBA")
    out = []
    for k in range(levels_for(w, h)):
        lw, lh = max(1, w >> k), max(1, h >> k)
        lvl = base if k == 0 else base.resize((lw, lh), Image.BOX)
        a = np.asarray(lvl, dtype=np.uint8).copy()
        if opaque:
            a[..., 3] = 255
        else:
            a[..., 3] = np.where(a[..., 3] >= 128, 255, 0)   # alpha-tested: a clean cut-out
        out.append(a)
    return out


def quantize(levels):
    """(palette Nx4 RGBA, [index arrays]) with one palette for every level (k-means refined median cut)."""
    from PIL import Image
    allpx = np.concatenate([l.reshape(-1, 4) for l in levels], axis=0)
    has_cut = bool((allpx[:, 3] == 0).any())
    opaque_px = allpx[allpx[:, 3] > 0][:, :3]
    ncol = 255 if has_cut else 256
    if len(opaque_px) == 0:
        opaque_px = np.zeros((1, 3), np.uint8)
    side = int(np.ceil(np.sqrt(len(opaque_px))))
    pad = np.zeros((side * side, 3), np.uint8)
    pad[:len(opaque_px)] = opaque_px
    pad[len(opaque_px):] = opaque_px[0]
    q = Image.fromarray(pad.reshape(side, side, 3), "RGB").quantize(colors=ncol, method=Image.Quantize.MEDIANCUT)
    pal = np.array(q.getpalette()[:3 * ncol], dtype=np.int32).reshape(-1, 3)
    palette = np.zeros((256, 4), np.uint8)
    palette[:len(pal), :3] = pal
    palette[:len(pal), 3] = 255
    cut_index = len(pal) if has_cut else None
    if cut_index is not None:
        palette[cut_index] = (0, 0, 0, 0)
    out = []
    pf = pal.astype(np.float32)
    for l in levels:
        px = l.reshape(-1, 4)
        idx = np.empty(len(px), np.uint8)
        rgb = px[:, :3].astype(np.float32)
        for s in range(0, len(px), 8192):
            d = ((rgb[s:s + 8192, None, :] - pf[None, :, :]) ** 2).sum(axis=2)
            idx[s:s + 8192] = d.argmin(axis=1)
        if cut_index is not None:
            idx[px[:, 3] == 0] = cut_index
        out.append(idx.reshape(l.shape[:2]))
    return palette, out


def encode(img, template_record, cap=128, opaque=False):
    """(texture record 0x20, swizzled pixels of every level, 1024-byte BGRA palette)."""
    levels = mip_chain(img, cap, opaque)
    palette, idx = quantize(levels)
    pixels = b"".join(T.swizzle_2d(i.tobytes(), i.shape[1], i.shape[0], 1) for i in idx)
    h, w = levels[0].shape[:2]
    rec = bytearray(template_record)
    low = struct.unpack_from("<I", rec, 0x0C)[0] & 0xFF
    fmt = low | (P8_FORMAT << 8) | (len(levels) << 16) | ((w.bit_length() - 1) << 20) | ((h.bit_length() - 1) << 24)
    struct.pack_into("<I", rec, 0x0C, fmt)
    struct.pack_into("<II", rec, 4, 0, 0)
    bgra = palette[:, [2, 1, 0, 3]].tobytes()
    return bytes(rec), pixels, bgra


def decode(record, pixels, palette):
    """RGBA array of level 0 (for previews)."""
    fmt = struct.unpack_from("<I", record, 0x0C)[0]
    w, h = 1 << ((fmt >> 20) & 0xF), 1 << ((fmt >> 24) & 0xF)
    lin = T.unswizzle_2d(bytes(pixels[:w * h]), w, h, 1)
    pal = np.frombuffer(palette, np.uint8).reshape(256, 4)[:, [2, 1, 0, 3]]
    return pal[np.frombuffer(lin, np.uint8)].reshape(h, w, 4)
