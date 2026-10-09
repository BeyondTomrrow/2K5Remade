#!/usr/bin/env python3
"""Inspect and decompress NFL 2K5 Xbox resource collections containing TXTR.

This is an evidence-driven parser for the outer 0x20-byte resource header and
the LZ stream consumed by default.xbe at virtual address 0x004DC00.  It does
not yet interpret the decompressed Xbox texture descriptors or untile pixels.
"""

from __future__ import annotations

import argparse
from collections import defaultdict, deque
import hashlib
import json
import re
import struct
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path


COMPRESSED_SENTINEL = 0xFEEDBEEF
HEADER = struct.Struct("<4s7I")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
TXTR_MAGIC = b"TXTR"

XBOX_FORMAT_NAMES = {
    0x00: "L8",
    0x01: "AL8",
    0x02: "A1R5G5B5",
    0x03: "X1R5G5B5",
    0x04: "A4R4G4B4",
    0x05: "R5G6B5",
    0x06: "A8R8G8B8",
    0x07: "X8R8G8B8",
    0x0B: "P8",
    0x0C: "DXT1",
    0x0E: "DXT3",
    0x0F: "DXT5",
    # VC-specific explicit-size palettized atlas. This is not a retail Xbox
    # D3DFORMAT enum value; 635 uniform `names` textures establish its layout.
    0x7F: "VC_P8_LINEAR",
}


class TxtrError(ValueError):
    """Raised when bounded TXTR parsing or decompression fails."""


@dataclass(frozen=True)
class Chunk:
    index: int
    offset: int
    kind: str
    stored_size: int
    system_bytes: int
    video_bytes: int
    compression_magic: int
    overlap_scratch_bytes: int
    reserved0: int
    reserved1: int

    @property
    def body_offset(self) -> int:
        return self.offset + HEADER.size

    @property
    def end_offset(self) -> int:
        return self.body_offset + self.stored_size

    @property
    def output_size(self) -> int:
        return self.system_bytes + self.video_bytes

    @property
    def compressed(self) -> bool:
        return self.compression_magic == COMPRESSED_SENTINEL


@dataclass(frozen=True)
class DecodeInfo:
    declared_output_size: int
    stream_tag: int
    offset_bits: int
    length_bits: int
    consumed_bytes: int
    literal_count: int
    match_count: int


@dataclass(frozen=True)
class CompressInfo:
    """Auditable statistics for a deterministic VC-LZ encoding."""

    declared_output_size: int
    stream_tag: int
    offset_bits: int
    length_bits: int
    encoded_bytes: int
    token_count: int
    flag_bytes: int
    literal_count: int
    match_count: int
    maximum_distance_used: int
    maximum_length_used: int
    candidate_comparisons: int
    verified_roundtrip: bool


@dataclass(frozen=True)
class FixedSpanRebuildInfo:
    """Result metadata for a size-preserving compressed resource rebuild."""

    kind: str
    stored_size: int
    system_bytes: int
    video_bytes: int
    stream_tag: int
    offset_bits: int
    original_consumed_bytes: int
    original_unused_bytes: int
    recompressed_bytes: int
    zero_padding_bytes: int
    original_overlap_scratch_bytes: int
    exact_minimum_overlap_scratch_bytes: int
    required_overlap_scratch_bytes: int
    rebuilt_overlap_scratch_bytes: int
    overlap_scratch_changed: bool
    loader_in_place_end_guard: bool
    loader_in_place_alias_guard: bool
    template_decoded_matches_input: bool
    compressed_stream_matches_template: bool
    complete_span_matches_template: bool
    decoded_sha256: str
    rebuilt_span_sha256: str


@dataclass(frozen=True)
class TextureInfo:
    name: str
    name_offset: int
    descriptor_offset: int
    pixel_offset: int
    palette_offset: int
    packed_format: int
    packed_size: int
    descriptor_flags: int
    dimensions: int
    format_code: int
    format_name: str
    mip_levels: int
    width: int
    height: int
    depth: int


def parse_chunks(data: bytes, allow_trailing: bool = False) -> list[Chunk]:
    chunks: list[Chunk] = []
    offset = 0
    while offset < len(data):
        if len(data) - offset < HEADER.size:
            if allow_trailing and chunks:
                break
            raise TxtrError(
                f"trailing {len(data) - offset} bytes at 0x{offset:x}; "
                "a TXTR header requires 0x20 bytes"
            )
        fields = HEADER.unpack_from(data, offset)
        if any(byte < 0x20 or byte > 0x7E for byte in fields[0]):
            if allow_trailing and chunks:
                break
            raise TxtrError(
                f"chunk {len(chunks)} at 0x{offset:x}: invalid resource FourCC "
                f"{fields[0]!r}"
            )
        chunk = Chunk(
            index=len(chunks),
            offset=offset,
            kind=fields[0].decode("ascii"),
            stored_size=fields[1],
            system_bytes=fields[2],
            video_bytes=fields[3],
            compression_magic=fields[4],
            overlap_scratch_bytes=fields[5],
            reserved0=fields[6],
            reserved1=fields[7],
        )
        if chunk.stored_size == 0:
            if allow_trailing and chunks:
                break
            raise TxtrError(f"chunk {chunk.index} at 0x{offset:x}: zero stored size")
        if chunk.end_offset > len(data):
            if allow_trailing and chunks:
                break
            raise TxtrError(
                f"chunk {chunk.index} at 0x{offset:x}: end 0x{chunk.end_offset:x} "
                f"exceeds file size 0x{len(data):x}"
            )
        chunks.append(chunk)
        offset = chunk.end_offset
    return chunks


def decompress_vc_lz(stream: bytes, expected_size: int | None = None) -> tuple[bytes, DecodeInfo]:
    """Port the x86 decoder at default.xbe:0x004DC00.

    Token flags are consumed least-significant bit first.  A zero flag emits a
    literal; a one flag emits a two-byte (distance, length) match.  The game
    copies matches backwards, so this implementation deliberately preserves
    that behavior rather than silently substituting conventional LZSS overlap
    semantics.
    """

    if len(stream) < 10:
        raise TxtrError("compressed stream is shorter than its 10-byte prefix")
    output_size, stream_tag = struct.unpack_from("<II", stream, 0)
    if expected_size is not None and output_size != expected_size:
        raise TxtrError(
            f"stream declares 0x{output_size:x} output bytes, "
            f"header declares 0x{expected_size:x}"
        )
    offset_bits = stream[8]
    if not 1 <= offset_bits <= 15:
        raise TxtrError(f"invalid offset bit count {offset_bits}")
    length_bits = 16 - offset_bits
    distance_mask = (1 << offset_bits) - 1
    length_mask = (1 << length_bits) - 1

    out = bytearray(output_size)
    src = 9
    flags = stream[src]
    src += 1
    flag_mask = 1
    dst = 0
    literal_count = 0
    match_count = 0

    while dst < output_size:
        if flags & flag_mask:
            if src + 2 > len(stream):
                raise TxtrError(f"truncated match token at stream offset 0x{src:x}")
            code = struct.unpack_from("<H", stream, src)[0]
            src += 2
            distance = code & distance_mask
            length = ((code >> offset_bits) & length_mask) + 3
            if distance == 0 or distance > dst:
                raise TxtrError(
                    f"invalid match distance {distance} at output offset 0x{dst:x}"
                )
            if dst + length > output_size:
                raise TxtrError(
                    f"match length {length} at output offset 0x{dst:x} "
                    "exceeds declared output size"
                )
            for index in range(length - 1, -1, -1):
                out[dst + index] = out[dst - distance + index]
            dst += length
            match_count += 1
        else:
            if src >= len(stream):
                raise TxtrError(f"truncated literal at stream offset 0x{src:x}")
            out[dst] = stream[src]
            src += 1
            dst += 1
            literal_count += 1

        flag_mask = (flag_mask << 1) & 0xFF
        if flag_mask == 0 and dst < output_size:
            if src >= len(stream):
                raise TxtrError(f"missing flag byte at stream offset 0x{src:x}")
            flags = stream[src]
            src += 1
            flag_mask = 1

    return bytes(out), DecodeInfo(
        declared_output_size=output_size,
        stream_tag=stream_tag,
        offset_bits=offset_bits,
        length_bits=length_bits,
        consumed_bytes=src,
        literal_count=literal_count,
        match_count=match_count,
    )


def minimum_vc_lz_overlap_scratch(
    stream: bytes,
    stored_size: int,
    expected_size: int | None = None,
) -> int:
    """Return the exact minimum ``wrapper[+0x14]`` for in-place decode.

    NFL 2K5 reads the complete fixed-size stored body at
    ``base + decoded_size + scratch - stored_size`` and decompresses forward
    into ``base``.  At each non-final token boundary, the output endpoint must
    remain below the next unread compressed byte.  This routine evaluates that
    constraint over the complete token stream rather than assuming that a
    separate-buffer round trip is sufficient.
    """

    if len(stream) < 10:
        raise TxtrError("compressed stream is shorter than its 10-byte prefix")
    output_size, _stream_tag = struct.unpack_from("<II", stream, 0)
    if expected_size is not None and output_size != expected_size:
        raise TxtrError(
            f"stream declares 0x{output_size:x} output bytes, "
            f"header declares 0x{expected_size:x}"
        )
    if stored_size < len(stream):
        raise TxtrError("stored body is shorter than the consumed VC-LZ stream")
    offset_bits = stream[8]
    if not 1 <= offset_bits <= 15:
        raise TxtrError(f"invalid offset bit count {offset_bits}")
    distance_mask = (1 << offset_bits) - 1
    length_mask = (1 << (16 - offset_bits)) - 1
    source = 10
    flags = stream[9]
    flag_mask = 1
    destination = 0
    maximum = 0

    while destination < output_size:
        token_start = destination
        if flags & flag_mask:
            if source + 2 > len(stream):
                raise TxtrError(f"truncated match token at stream offset 0x{source:x}")
            code = struct.unpack_from("<H", stream, source)[0]
            source += 2
            distance = code & distance_mask
            length = ((code >> offset_bits) & length_mask) + 3
            if distance == 0 or distance > token_start:
                raise TxtrError(
                    f"invalid match distance {distance} at output offset 0x{token_start:x}"
                )
        else:
            if source >= len(stream):
                raise TxtrError(f"truncated literal at stream offset 0x{source:x}")
            source += 1
            length = 1
        destination += length
        if destination > output_size:
            raise TxtrError("VC-LZ token exceeds declared output size")
        if destination < output_size:
            maximum = max(
                maximum,
                stored_size - output_size + destination - source,
            )

        flag_mask = (flag_mask << 1) & 0xFF
        if flag_mask == 0 and destination < output_size:
            if source >= len(stream):
                raise TxtrError(f"missing flag byte at stream offset 0x{source:x}")
            flags = stream[source]
            source += 1
            flag_mask = 1

    return maximum


def compress_vc_lz(
    source: bytes,
    *,
    stream_tag: int = 3,
    offset_bits: int = 12,
    max_encoded_size: int | None = None,
    max_candidate_comparisons: int = 50_000_000,
    verify_roundtrip: bool = True,
) -> tuple[bytes, CompressInfo]:
    """Deterministically encode bytes for :func:`decompress_vc_lz`.

    The sampled retail streams are reproduced by an exhaustive, nearest-first
    search over the preceding ``(1 << offset_bits) - 1`` bytes.  Ties retain
    the nearest candidate.  Matches are limited to their backward distance
    because the title's decoder copies each match from high index to low index;
    unlike a conventional forward LZSS copy, an overlapping match would read
    target bytes before they have been written.

    ``max_encoded_size`` bounds the complete stream including its nine-byte
    prefix.  ``max_candidate_comparisons`` bounds adversarial search time.
    Both limits fail closed rather than emitting a partial stream.
    """

    if not source:
        raise TxtrError("VC-LZ cannot encode an empty output buffer")
    if len(source) > 0xFFFFFFFF:
        raise TxtrError("VC-LZ output exceeds its u32 size field")
    if not 0 <= stream_tag <= 0xFFFFFFFF:
        raise TxtrError(f"VC-LZ stream tag {stream_tag} is outside u32")
    if not 1 <= offset_bits <= 15:
        raise TxtrError(f"invalid offset bit count {offset_bits}")
    if max_encoded_size is not None and max_encoded_size < 10:
        raise TxtrError("VC-LZ encoded-size bound is shorter than a usable stream")
    if max_candidate_comparisons <= 0:
        raise TxtrError("VC-LZ candidate-comparison bound must be positive")

    # Vectorize the exact three-byte keys once. All token decisions remain
    # nearest-first and integer-only, including comparison-bound accounting.
    import numpy as np
    from bisect import bisect_left, bisect_right
    pixels = np.frombuffer(source, dtype=np.uint8).astype(np.uint32)
    keys = (pixels[:-2] | (pixels[1:-1] << 8) | (pixels[2:] << 16)).tolist()
    length_bits = 16 - offset_bits
    maximum_distance = (1 << offset_bits) - 1
    maximum_length = ((1 << length_bits) - 1) + 3
    minimum_match = 3
    chains: dict[int, deque[int]] = defaultdict(deque)
    encoded = bytearray(struct.pack("<IIB", len(source), stream_tag, offset_bits))
    pending: list[tuple[bool, int, int]] = []
    position = 0
    literal_count = 0
    match_count = 0
    flag_bytes = 0
    maximum_distance_used = 0
    maximum_length_used = 0
    candidate_comparisons = 0

    def key_at(offset: int) -> int:
        return keys[offset]

    def add_position(offset: int) -> None:
        if offset + minimum_match <= len(source):
            chains[key_at(offset)].append(offset)

    def flush_tokens() -> None:
        nonlocal flag_bytes
        if not pending:
            return
        flags = 0
        payload = bytearray()
        for token_index, (is_match, first, second) in enumerate(pending):
            if is_match:
                flags |= 1 << token_index
                code = first | ((second - minimum_match) << offset_bits)
                payload.extend(struct.pack("<H", code))
            else:
                payload.append(first)
        required = 1 + len(payload)
        if max_encoded_size is not None and len(encoded) + required > max_encoded_size:
            raise TxtrError(
                f"VC-LZ stream needs more than the {max_encoded_size}-byte bound"
            )
        encoded.append(flags)
        encoded.extend(payload)
        flag_bytes += 1
        pending.clear()

    while position < len(source):
        best_length = 0
        best_distance = 0
        remaining = len(source) - position
        if remaining >= minimum_match:
            candidates = chains.get(key_at(position))
            if candidates:
                cutoff = position - maximum_distance
                while candidates and candidates[0] < cutoff:
                    candidates.popleft()
                # Search the same window in C when the hash chain is long.
                # Binary search finds the longest legal non-overlapping match;
                # rfind retains the nearest tie. Charge the original traversal's
                # full comparison count, including skipped short candidates.
                searched = len(candidates) > 16
                if searched:
                    upper = min(maximum_length, remaining)
                    low, high, nearest = 2, upper + 1, -1
                    fragment = source[position:position + upper]
                    full = source.rfind(fragment, max(0, cutoff), position)
                    if full >= 0:
                        low, nearest = upper, full
                    else:
                        high = upper
                        while low + 1 < high:
                            middle = (low + high) // 2
                            found = source.rfind(fragment[:middle], max(0, cutoff), position)
                            if found >= 0:
                                low, nearest = middle, found
                            else:
                                high = middle
                    charged = bisect_right(candidates, position - minimum_match)
                    if low == maximum_length:
                        charged -= bisect_left(candidates, nearest)
                    candidate_comparisons += charged
                    if candidate_comparisons > max_candidate_comparisons:
                        raise TxtrError("VC-LZ candidate-comparison bound exceeded")
                    if low >= minimum_match:
                        best_length, best_distance = low, position - nearest
                for candidate in (() if searched else reversed(candidates)):
                    distance = position - candidate
                    if distance < minimum_match:
                        # A minimum three-byte non-overlapping match is
                        # impossible at distance one or two.
                        continue
                    candidate_comparisons += 1
                    if candidate_comparisons > max_candidate_comparisons:
                        raise TxtrError(
                            "VC-LZ candidate-comparison bound exceeded"
                        )
                    limit = min(maximum_length, remaining, distance)
                    fragment = source[position:position + limit]
                    if source.startswith(fragment, candidate):
                        length = limit
                    else:
                        low, high = minimum_match, limit
                        while low + 1 < high:
                            middle = (low + high) // 2
                            if source.startswith(fragment[:middle], candidate):
                                low = middle
                            else:
                                high = middle
                        length = low
                    if length > best_length:
                        best_length = length
                        best_distance = distance
                    # No candidate can exceed the format's maximum length.
                    if best_length == maximum_length:
                        break

        if best_length >= minimum_match:
            pending.append((True, best_distance, best_length))
            consumed = best_length
            match_count += 1
            maximum_distance_used = max(maximum_distance_used, best_distance)
            maximum_length_used = max(maximum_length_used, best_length)
        else:
            pending.append((False, source[position], 0))
            consumed = 1
            literal_count += 1

        for consumed_offset in range(position, position + consumed):
            add_position(consumed_offset)
        position += consumed
        if len(pending) == 8:
            flush_tokens()

    flush_tokens()
    if max_encoded_size is not None and len(encoded) > max_encoded_size:
        raise TxtrError(
            f"VC-LZ stream is {len(encoded)} bytes, exceeds {max_encoded_size}"
        )

    verified = False
    if verify_roundtrip:
        decoded, decode_info = decompress_vc_lz(bytes(encoded), len(source))
        if decoded != source:
            raise TxtrError("VC-LZ encoder failed its internal round-trip check")
        if decode_info.consumed_bytes != len(encoded):
            raise TxtrError("VC-LZ decoder did not consume the complete encoded stream")
        verified = True

    info = CompressInfo(
        declared_output_size=len(source),
        stream_tag=stream_tag,
        offset_bits=offset_bits,
        length_bits=length_bits,
        encoded_bytes=len(encoded),
        token_count=literal_count + match_count,
        flag_bytes=flag_bytes,
        literal_count=literal_count,
        match_count=match_count,
        maximum_distance_used=maximum_distance_used,
        maximum_length_used=maximum_length_used,
        candidate_comparisons=candidate_comparisons,
        verified_roundtrip=verified,
    )
    return bytes(encoded), info


def rebuild_compressed_chunk_fixed_span(
    template_span: bytes,
    decoded: bytes,
    *,
    max_candidate_comparisons: int = 50_000_000,
) -> tuple[bytes, FixedSpanRebuildInfo]:
    """Recompress ``decoded`` into an existing compressed resource span.

    The stored-body size and complete resource span are preserved.  The newly
    consumed stream is followed by zero bytes to fill the fixed body.  The
    title decodes that fixed body forward in-place, so wrapper word ``+0x14``
    is raised, when necessary, to the next 16-byte boundary at or above the
    unused tail size.  This keeps the meaningful compressed stream ending at
    or beyond the decoded destination while leaving resource traversal and all
    descriptor bytes unchanged.

    The function independently decodes the template and rebuilt span and
    returns only after the rebuilt bytes reproduce ``decoded`` exactly.
    """

    if len(template_span) < HEADER.size + 10:
        raise TxtrError("compressed template span is too short")
    fields = HEADER.unpack_from(template_span)
    raw_kind, stored_size, system_bytes, video_bytes, compression_magic, \
        overlap_scratch_bytes, reserved0, reserved1 = fields
    if len(template_span) != HEADER.size + stored_size:
        raise TxtrError("compressed template span size disagrees with wrapper")
    if compression_magic != COMPRESSED_SENTINEL:
        raise TxtrError("fixed-span rebuild requires a compressed template")
    if reserved0 != 0 or reserved1 != 0:
        raise TxtrError("compressed template reserved wrapper words are nonzero")
    if len(decoded) != system_bytes + video_bytes:
        raise TxtrError(
            f"decoded input has {len(decoded)} bytes; wrapper requires "
            f"{system_bytes + video_bytes}"
        )
    try:
        kind = raw_kind.decode("ascii")
    except UnicodeDecodeError as exc:
        raise TxtrError("compressed template FourCC is not ASCII") from exc
    if any(character < " " or character > "~" for character in kind):
        raise TxtrError("compressed template FourCC is not printable")

    chunk = Chunk(
        index=0,
        offset=0,
        kind=kind,
        stored_size=stored_size,
        system_bytes=system_bytes,
        video_bytes=video_bytes,
        compression_magic=compression_magic,
        overlap_scratch_bytes=overlap_scratch_bytes,
        reserved0=reserved0,
        reserved1=reserved1,
    )
    template_decoded, template_decode_info = decode_chunk(template_span, chunk)
    if template_decode_info is None:
        raise TxtrError("compressed template unexpectedly decoded as raw")
    template_stream = template_span[
        HEADER.size:HEADER.size + template_decode_info.consumed_bytes
    ]
    if len(template_stream) < 9:
        raise TxtrError("compressed template stream prefix is truncated")
    template_output_size, stream_tag = struct.unpack_from("<II", template_stream, 0)
    offset_bits = template_stream[8]
    if template_output_size != len(decoded):
        raise TxtrError("compressed template stream/output size mismatch")

    recompressed, _ = compress_vc_lz(
        decoded,
        stream_tag=stream_tag,
        offset_bits=offset_bits,
        max_encoded_size=stored_size,
        max_candidate_comparisons=max_candidate_comparisons,
        verify_roundtrip=True,
    )
    padding_size = stored_size - len(recompressed)
    exact_minimum_overlap_scratch_bytes = minimum_vc_lz_overlap_scratch(
        recompressed, stored_size, len(decoded)
    )
    required_overlap_scratch_bytes = (
        max(padding_size, exact_minimum_overlap_scratch_bytes) + 15
    ) & ~15
    rebuilt_overlap_scratch_bytes = max(
        overlap_scratch_bytes, required_overlap_scratch_bytes
    )
    rebuilt_header = HEADER.pack(
        raw_kind, stored_size, system_bytes, video_bytes, compression_magic,
        rebuilt_overlap_scratch_bytes, reserved0, reserved1,
    )
    rebuilt = rebuilt_header + recompressed + bytes(padding_size)
    if len(rebuilt) != len(template_span):
        raise TxtrError("fixed-span rebuild changed the complete span size")
    rebuilt_decoded, rebuilt_decode_info = decode_chunk(rebuilt, chunk)
    if rebuilt_decode_info is None or rebuilt_decoded != decoded:
        raise TxtrError("fixed-span rebuilt chunk failed independent decode verification")
    if rebuilt_decode_info.consumed_bytes != len(recompressed):
        raise TxtrError("fixed-span rebuilt stream consumption mismatch")
    if rebuilt[HEADER.size + len(recompressed):] != bytes(padding_size):
        raise TxtrError("fixed-span rebuilt tail is not zero padded")

    info = FixedSpanRebuildInfo(
        kind=kind,
        stored_size=stored_size,
        system_bytes=system_bytes,
        video_bytes=video_bytes,
        stream_tag=stream_tag,
        offset_bits=offset_bits,
        original_consumed_bytes=template_decode_info.consumed_bytes,
        original_unused_bytes=stored_size - template_decode_info.consumed_bytes,
        recompressed_bytes=len(recompressed),
        zero_padding_bytes=padding_size,
        original_overlap_scratch_bytes=overlap_scratch_bytes,
        exact_minimum_overlap_scratch_bytes=
            exact_minimum_overlap_scratch_bytes,
        required_overlap_scratch_bytes=required_overlap_scratch_bytes,
        rebuilt_overlap_scratch_bytes=rebuilt_overlap_scratch_bytes,
        overlap_scratch_changed=(
            rebuilt_overlap_scratch_bytes != overlap_scratch_bytes
        ),
        loader_in_place_end_guard=(
            rebuilt_overlap_scratch_bytes >= padding_size
        ),
        loader_in_place_alias_guard=(
            rebuilt_overlap_scratch_bytes >=
            exact_minimum_overlap_scratch_bytes
        ),
        template_decoded_matches_input=template_decoded == decoded,
        compressed_stream_matches_template=recompressed == template_stream,
        complete_span_matches_template=rebuilt == template_span,
        decoded_sha256=hashlib.sha256(decoded).hexdigest(),
        rebuilt_span_sha256=hashlib.sha256(rebuilt).hexdigest(),
    )
    return rebuilt, info


def decode_chunk(data: bytes, chunk: Chunk) -> tuple[bytes, DecodeInfo | None]:
    body = data[chunk.body_offset : chunk.end_offset]
    if not chunk.compressed:
        # Raw resource types such as LAYT and CRED reuse the 0x20-byte span
        # header but give words 0x08/0x0c type-specific meanings.  Return the
        # bounded body verbatim; do not pretend those words are an output size.
        return body, None
    return decompress_vc_lz(body, chunk.output_size)


def parse_texture(output: bytes, chunk: Chunk) -> TextureInfo:
    if chunk.kind != "TXTR":
        raise TxtrError(f"chunk {chunk.index} is {chunk.kind}, not TXTR")
    if len(output) < chunk.system_bytes or chunk.system_bytes < 0x20:
        raise TxtrError(f"TXTR chunk {chunk.index} has a truncated system buffer")
    if output[0x0C:0x10] != TXTR_MAGIC:
        raise TxtrError(
            f"TXTR chunk {chunk.index}: decoded object lacks TXTR at offset 0x0c"
        )

    name_offset = struct.unpack_from("<I", output, 0x10)[0] + 0x0F
    descriptor_offset = struct.unpack_from("<I", output, 0x14)[0] + 0x13
    if not 0x18 <= name_offset < descriptor_offset:
        raise TxtrError(
            f"TXTR chunk {chunk.index}: invalid name/descriptor offsets "
            f"0x{name_offset:x}/0x{descriptor_offset:x}"
        )
    if descriptor_offset + 0x18 > chunk.system_bytes:
        raise TxtrError(
            f"TXTR chunk {chunk.index}: descriptor at 0x{descriptor_offset:x} "
            f"exceeds 0x{chunk.system_bytes:x}-byte system buffer"
        )

    name_end = None
    for offset in range(name_offset, descriptor_offset - 1, 2):
        if output[offset : offset + 2] == b"\0\0":
            name_end = offset
            break
    if name_end is None:
        raise TxtrError(f"TXTR chunk {chunk.index}: unterminated UTF-16LE name")
    try:
        name = output[name_offset:name_end].decode("utf-16le")
    except UnicodeDecodeError as exc:
        raise TxtrError(f"TXTR chunk {chunk.index}: invalid UTF-16LE name") from exc

    pixel_offset, palette_offset, packed_format, packed_size, flags = struct.unpack_from(
        "<5I", output, descriptor_offset + 4
    )
    dimensions = (packed_format >> 4) & 0xF
    format_code = (packed_format >> 8) & 0xFF
    mip_levels = (packed_format >> 16) & 0xF
    if packed_size == 0:
        width = 1 << ((packed_format >> 20) & 0xF)
        height = 1 << ((packed_format >> 24) & 0xF)
    else:
        # VC's descriptor stores explicit texel dimensions as two u16 values,
        # not the retail D3D PixelContainer Size bitfield.  Loader helpers at
        # 0x0034E60 read these halfwords directly when descriptor +0x14 marks
        # an explicit-size resource.
        #
        # The linear P8 format puts them the other way round, and reading them
        # in the swizzled order transposed every one of those textures.  It is
        # what made the Team Kit's Nameplate Atlas export as confetti: `names`
        # carries 0x04000020 and is a 1024x32 character strip -- rendering it
        # 32x1024 shreds the letterforms.  The only other texture in this
        # format, `ticker_src`, carries the identical word and is a scrolling
        # news ticker, which is wide and short for the same reason.  Decoded
        # the right way round the first atlas reads "` - A B C D E F G H ...".
        if format_code == 0x7F:
            width = (packed_size >> 16) & 0xFFFF
            height = packed_size & 0xFFFF
        else:
            width = packed_size & 0xFFFF
            height = (packed_size >> 16) & 0xFFFF
    depth = 1 << ((packed_format >> 28) & 0xF)
    return TextureInfo(
        name=name,
        name_offset=name_offset,
        descriptor_offset=descriptor_offset,
        pixel_offset=pixel_offset,
        palette_offset=palette_offset,
        packed_format=packed_format,
        packed_size=packed_size,
        descriptor_flags=flags,
        dimensions=dimensions,
        format_code=format_code,
        format_name=XBOX_FORMAT_NAMES.get(format_code, f"UNKNOWN_0x{format_code:02X}"),
        mip_levels=mip_levels,
        width=width,
        height=height,
        depth=depth,
    )


def unswizzle_2d(source: bytes, width: int, height: int, bytes_per_pixel: int) -> bytes:
    if width <= 0 or height <= 0 or bytes_per_pixel <= 0:
        raise TxtrError("unswizzle dimensions and bytes-per-pixel must be positive")
    required = width * height * bytes_per_pixel
    if len(source) < required:
        raise TxtrError(
            f"swizzled buffer has 0x{len(source):x} bytes; needs 0x{required:x}"
        )

    mask_x = 0
    mask_y = 0
    interleaved_bit = 1
    dimension_bit = 1
    while dimension_bit < width or dimension_bit < height:
        if dimension_bit < width:
            mask_x |= interleaved_bit
            interleaved_bit <<= 1
        if dimension_bit < height:
            mask_y |= interleaved_bit
            interleaved_bit <<= 1
        dimension_bit <<= 1

    result = bytearray(required)
    swizzled_y = 0
    for y in range(height):
        swizzled_x = 0
        for x in range(width):
            source_pixel = (swizzled_x | swizzled_y) * bytes_per_pixel
            target_pixel = (y * width + x) * bytes_per_pixel
            result[target_pixel : target_pixel + bytes_per_pixel] = source[
                source_pixel : source_pixel + bytes_per_pixel
            ]
            swizzled_x = (swizzled_x - mask_x) & mask_x
        swizzled_y = (swizzled_y - mask_y) & mask_y
    return bytes(result)


def swizzle_2d(source: bytes, width: int, height: int, bytes_per_pixel: int) -> bytes:
    """Inverse of :func:`unswizzle_2d` for the proved NV2A Morton layout."""

    if width <= 0 or height <= 0 or bytes_per_pixel <= 0:
        raise TxtrError("swizzle dimensions and bytes-per-pixel must be positive")
    required = width * height * bytes_per_pixel
    if len(source) != required:
        raise TxtrError(
            f"linear buffer has 0x{len(source):x} bytes; requires exactly 0x{required:x}"
        )

    mask_x = 0
    mask_y = 0
    interleaved_bit = 1
    dimension_bit = 1
    while dimension_bit < width or dimension_bit < height:
        if dimension_bit < width:
            mask_x |= interleaved_bit
            interleaved_bit <<= 1
        if dimension_bit < height:
            mask_y |= interleaved_bit
            interleaved_bit <<= 1
        dimension_bit <<= 1

    result = bytearray(required)
    swizzled_y = 0
    for y in range(height):
        swizzled_x = 0
        for x in range(width):
            target_pixel = (swizzled_x | swizzled_y) * bytes_per_pixel
            source_pixel = (y * width + x) * bytes_per_pixel
            result[target_pixel:target_pixel + bytes_per_pixel] = source[
                source_pixel:source_pixel + bytes_per_pixel
            ]
            swizzled_x = (swizzled_x - mask_x) & mask_x
        swizzled_y = (swizzled_y - mask_y) & mask_y
    return bytes(result)


def texture_to_rgba(output: bytes, chunk: Chunk, texture: TextureInfo) -> bytes:
    if texture.dimensions != 2 or texture.depth != 1:
        raise TxtrError(
            f"PORTME: TXTR chunk {chunk.index} {texture.name!r} uses "
            f"{texture.dimensions} dimensions/depth {texture.depth}"
        )
    if texture.format_code not in (0x02, 0x06, 0x0B, 0x0C, 0x7F):
        raise TxtrError(
            f"PORTME: TXTR chunk {chunk.index} {texture.name!r} uses Xbox "
            f"format {texture.format_name}; the verified converter currently supports "
            "A1R5G5B5/A8R8G8B8/P8/DXT1/VC_P8_LINEAR"
        )

    video = output[chunk.system_bytes : chunk.system_bytes + chunk.video_bytes]
    if texture.format_code == 0x0C:
        block_width = (texture.width + 3) // 4
        block_height = (texture.height + 3) // 4
        encoded_size = block_width * block_height * 8
        encoded_end = texture.pixel_offset + encoded_size
        if encoded_end > len(video):
            raise TxtrError(
                f"TXTR chunk {chunk.index}: DXT1 range exceeds video buffer"
            )
        return decode_dxt1(
            video[texture.pixel_offset:encoded_end], texture.width, texture.height
        )

    if texture.format_code in (0x02, 0x06):
        bytes_per_pixel = 2 if texture.format_code == 0x02 else 4
        byte_count = texture.width * texture.height * bytes_per_pixel
        pixel_end = texture.pixel_offset + byte_count
        if pixel_end > len(video):
            raise TxtrError(
                f"TXTR chunk {chunk.index}: {texture.format_name} range exceeds video buffer"
            )
        pixels = video[texture.pixel_offset:pixel_end]
        if texture.packed_size == 0:
            pixels = unswizzle_2d(
                pixels, texture.width, texture.height, bytes_per_pixel
            )
        rgba = bytearray(texture.width * texture.height * 4)
        if texture.format_code == 0x02:
            for pixel in range(texture.width * texture.height):
                value = struct.unpack_from("<H", pixels, pixel * 2)[0]
                alpha = 255 if value & 0x8000 else 0
                red5 = (value >> 10) & 0x1F
                green5 = (value >> 5) & 0x1F
                blue5 = value & 0x1F
                rgba[pixel * 4 : pixel * 4 + 4] = bytes(
                    (
                        (red5 << 3) | (red5 >> 2),
                        (green5 << 3) | (green5 >> 2),
                        (blue5 << 3) | (blue5 >> 2),
                        alpha,
                    )
                )
        else:
            for pixel in range(texture.width * texture.height):
                blue, green, red, alpha = pixels[pixel * 4 : pixel * 4 + 4]
                rgba[pixel * 4 : pixel * 4 + 4] = bytes((red, green, blue, alpha))
        return bytes(rgba)

    if texture.format_code == 0x7F:
        if texture.packed_size == 0 or texture.palette_offset == 0:
            raise TxtrError(
                f"PORTME: TXTR chunk {chunk.index} {texture.name!r} has an "
                "unverified VC_P8_LINEAR descriptor"
            )

    pixel_count = texture.width * texture.height
    pixel_end = texture.pixel_offset + pixel_count
    palette_end = texture.palette_offset + 256 * 4
    if pixel_end > len(video) or palette_end > len(video):
        raise TxtrError(
            f"TXTR chunk {chunk.index}: pixel/palette range exceeds video buffer"
        )
    indices = video[texture.pixel_offset:pixel_end]
    if texture.format_code == 0x0B:
        indices = unswizzle_2d(indices, texture.width, texture.height, 1)
    palette = video[texture.palette_offset:palette_end]
    rgba = bytearray(pixel_count * 4)
    for pixel, palette_index in enumerate(indices):
        blue, green, red, alpha = palette[palette_index * 4 : palette_index * 4 + 4]
        rgba[pixel * 4 : pixel * 4 + 4] = bytes((red, green, blue, alpha))
    return bytes(rgba)


def rgb565(value: int) -> tuple[int, int, int, int]:
    red5 = (value >> 11) & 0x1F
    green6 = (value >> 5) & 0x3F
    blue5 = value & 0x1F
    return (
        (red5 << 3) | (red5 >> 2),
        (green6 << 2) | (green6 >> 4),
        (blue5 << 3) | (blue5 >> 2),
        255,
    )


def decode_dxt1(source: bytes, width: int, height: int) -> bytes:
    block_width = (width + 3) // 4
    block_height = (height + 3) // 4
    required = block_width * block_height * 8
    if len(source) < required:
        raise TxtrError(
            f"DXT1 buffer has 0x{len(source):x} bytes; needs 0x{required:x}"
        )
    rgba = bytearray(width * height * 4)
    position = 0
    for block_y in range(block_height):
        for block_x in range(block_width):
            color0, color1, selectors = struct.unpack_from("<HHI", source, position)
            position += 8
            first = rgb565(color0)
            second = rgb565(color1)
            if color0 > color1:
                third = tuple((2 * first[i] + second[i]) // 3 for i in range(3)) + (255,)
                fourth = tuple((first[i] + 2 * second[i]) // 3 for i in range(3)) + (255,)
            else:
                third = tuple((first[i] + second[i]) // 2 for i in range(3)) + (255,)
                fourth = (0, 0, 0, 0)
            colors = (first, second, third, fourth)
            for local_y in range(4):
                y = block_y * 4 + local_y
                if y >= height:
                    continue
                for local_x in range(4):
                    x = block_x * 4 + local_x
                    if x >= width:
                        continue
                    selector = (selectors >> (2 * (local_y * 4 + local_x))) & 3
                    target = (y * width + x) * 4
                    rgba[target : target + 4] = bytes(colors[selector])
    return bytes(rgba)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def encode_rgba_png(width: int, height: int, rgba: bytes) -> bytes:
    """Return a deterministic non-interlaced RGBA8 PNG payload."""

    if len(rgba) != width * height * 4:
        raise TxtrError("RGBA buffer size does not match PNG dimensions")
    scanlines = b"".join(
        b"\0" + rgba[row * width * 4 : (row + 1) * width * 4]
        for row in range(height)
    )
    return (
        PNG_SIGNATURE
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + png_chunk(b"IEND", b"")
    )


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    payload = encode_rgba_png(width, height, rgba)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def safe_texture_name(name: str, fallback: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9._-]+", "_", name).strip("._")
    return safe or fallback


def chunk_record(data: bytes, chunk: Chunk, decode: bool) -> dict[str, object]:
    record: dict[str, object] = asdict(chunk)
    record["compressed"] = chunk.compressed
    record["end_offset"] = chunk.end_offset
    record["output_size"] = chunk.output_size
    if decode:
        output, info = decode_chunk(data, chunk)
        record["decoded_sha256"] = hashlib.sha256(output).hexdigest()
        record["system_head_hex"] = output[:32].hex()
        record["video_head_hex"] = output[chunk.system_bytes : chunk.system_bytes + 32].hex()
        if info is not None:
            record["decode"] = asdict(info)
            record["unused_stored_bytes"] = chunk.stored_size - info.consumed_bytes
        if chunk.kind == "TXTR":
            record["texture"] = asdict(parse_texture(output, chunk))
    return record


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inspect/decompress NFL 2K5 Xbox resource chunks, including TXTR."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("--decode", action="store_true", help="validate and summarize every LZ stream")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--extract-chunk", type=int, metavar="INDEX")
    parser.add_argument("--png-chunk", type=int, metavar="INDEX")
    parser.add_argument(
        "--png-dir",
        type=Path,
        help="convert every supported TXTR in this resource entry; print PORTME for others",
    )
    parser.add_argument("--output", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    data = args.input.read_bytes()
    chunks = parse_chunks(data)

    if args.extract_chunk is not None and args.png_chunk is not None:
        raise SystemExit("--extract-chunk and --png-chunk are mutually exclusive")
    if args.extract_chunk is not None:
        if args.output is None:
            raise SystemExit("--extract-chunk requires --output")
        if not 0 <= args.extract_chunk < len(chunks):
            raise SystemExit(f"chunk index must be between 0 and {len(chunks) - 1}")
        output, _ = decode_chunk(data, chunks[args.extract_chunk])
        args.output.write_bytes(output)
        print(f"wrote {len(output)} decoded bytes to {args.output}")
        return 0


    if args.png_chunk is not None:
        if args.output is None:
            raise SystemExit("--png-chunk requires --output")
        if not 0 <= args.png_chunk < len(chunks):
            raise SystemExit(f"chunk index must be between 0 and {len(chunks) - 1}")
        chunk = chunks[args.png_chunk]
        output, _ = decode_chunk(data, chunk)
        texture = parse_texture(output, chunk)
        rgba = texture_to_rgba(output, chunk, texture)
        write_png(args.output, texture.width, texture.height, rgba)
        print(
            f"wrote {texture.width}x{texture.height} {texture.format_name} "
            f"texture {texture.name!r} to {args.output}"
        )
        return 0


    if args.png_dir is not None:
        converted = 0
        blocked = 0
        for chunk in chunks:
            if chunk.kind != "TXTR":
                continue
            output, _ = decode_chunk(data, chunk)
            texture = parse_texture(output, chunk)
            try:
                rgba = texture_to_rgba(output, chunk, texture)
            except TxtrError as exc:
                blocked += 1
                print(str(exc))
                continue
            filename = safe_texture_name(texture.name, f"txtr_{chunk.index:04}") + ".png"
            target = args.png_dir / filename
            write_png(target, texture.width, texture.height, rgba)
            print(f"converted TXTR chunk {chunk.index} {texture.name!r} -> {target}")
            converted += 1
        print(f"converted {converted} texture(s); PORTME blockers {blocked}")
        return 0

    records = [chunk_record(data, chunk, args.decode) for chunk in chunks]
    if args.json:
        print(json.dumps({"input": str(args.input), "size": len(data), "chunks": records}, indent=2))
        return 0

    texture_count = sum(chunk.kind == "TXTR" for chunk in chunks)
    print(
        f"{args.input}: {len(chunks)} resource chunk(s), {texture_count} TXTR, "
        f"0x{len(data):x} bytes"
    )
    for record in records:
        line = (
            f"[{record['index']:4}] {record['kind']} off=0x{record['offset']:08x} "
            f"stored=0x{record['stored_size']:x} sys=0x{record['system_bytes']:x} "
            f"video=0x{record['video_bytes']:x} scratch=0x{record['overlap_scratch_bytes']:x}"
        )
        if args.decode:
            decode_info = record.get("decode")
            if isinstance(decode_info, dict):
                line += (
                    f" bits={decode_info['offset_bits']}/{decode_info['length_bits']} "
                    f"used=0x{decode_info['consumed_bytes']:x} "
                    f"tail=0x{record['unused_stored_bytes']:x}"
                )
        print(line)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TxtrError) as exc:
        raise SystemExit(f"error: {exc}") from exc
