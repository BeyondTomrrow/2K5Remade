#!/usr/bin/env python3
"""List or extract bounded entries from NFL 2K5's vc_53450030 packs."""

from __future__ import annotations

import argparse
import bisect
import json
import struct
import sys
from collections import Counter, OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO


ALIGNMENT = 0x800
PACK_SLOT_COUNT = 36
PACK_NAMES = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
HEADER_SIZE = 0x0C + PACK_SLOT_COUNT * 4
ENTRY_SIZE = 12
MAX_ENTRIES = 1_000_000


class FormatError(ValueError):
    pass


@dataclass(frozen=True)
class Pack:
    ordinal: int
    name: str
    blocks: int
    size: int
    virtual_start: int
    path: Path

    @property
    def virtual_end(self) -> int:
        return self.virtual_start + self.size


@dataclass(frozen=True)
class Segment:
    pack_ordinal: int
    pack_name: str
    pack_offset: int
    size: int


@dataclass(frozen=True)
class Entry:
    table_index: int
    name_id: int
    size: int
    offset_blocks: int
    virtual_offset: int
    head_hex: str
    head_ascii: str
    segments: tuple[Segment, ...]

    @property
    def virtual_end(self) -> int:
        return self.virtual_offset + self.size


@dataclass(frozen=True)
class Archive:
    index_path: Path
    reserved: int
    populated_pack_count: int
    packs: tuple[Pack, ...]
    entries: tuple[Entry, ...]

    @property
    def virtual_size(self) -> int:
        return self.packs[-1].virtual_end

    @property
    def table_end(self) -> int:
        return HEADER_SIZE + len(self.entries) * ENTRY_SIZE


def read_exact(stream: BinaryIO, size: int, label: str) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise FormatError(f"truncated {label}: wanted {size}, received {len(data)}")
    return data


def align_up(value: int, alignment: int = ALIGNMENT) -> int:
    return (value + alignment - 1) // alignment * alignment


def range_segments(
    packs: tuple[Pack, ...], starts: list[int], offset: int, size: int
) -> tuple[Segment, ...]:
    if size <= 0:
        raise FormatError("zero-sized entry")
    end = offset + size
    first = bisect.bisect_right(starts, offset) - 1
    if first < 0 or end > packs[-1].virtual_end:
        raise FormatError(f"range 0x{offset:x}..0x{end:x} outside archive")
    result: list[Segment] = []
    for pack in packs[first:]:
        segment_start = max(offset, pack.virtual_start)
        segment_end = min(end, pack.virtual_end)
        if segment_start < segment_end:
            result.append(
                Segment(
                    pack.ordinal,
                    pack.name,
                    segment_start - pack.virtual_start,
                    segment_end - segment_start,
                )
            )
        if segment_end == end:
            break
    if sum(segment.size for segment in result) != size:
        raise FormatError(f"could not map complete range at 0x{offset:x}")
    return tuple(result)


def read_head(packs: tuple[Pack, ...], segments: tuple[Segment, ...]) -> bytes:
    first = segments[0]
    with packs[first.pack_ordinal].path.open("rb") as stream:
        stream.seek(first.pack_offset)
        return read_exact(stream, min(4, first.size), "entry head")


# A multi-edit build resolves targets against the same canonical index volume
# once per edit; the entry table and pack layout are image-constant, so the
# parsed Archive is memoized per index-volume identity (path, device, inode,
# size, mtime).  Every Archive/Entry/Pack/Segment is a frozen dataclass, so a
# shared instance cannot be observably mutated.  Span bytes are never cached:
# downstream readers always read the live packs and hash-check what they read.
_PARSE_CACHE_LIMIT = 8
_PARSE_CACHE: "OrderedDict[tuple[object, ...], Archive]" = OrderedDict()


def clear_parse_cache() -> None:
    """Forget every memoized archive parse (tests and fresh sessions)."""

    _PARSE_CACHE.clear()


def parse_archive(index_path: Path) -> Archive:
    index_path = index_path.expanduser()
    if not index_path.is_file():
        raise FormatError(f"index is not a regular file: {index_path}")
    if index_path.name != "0":
        raise FormatError("NFL index must be the first volume named '0'")
    info = index_path.stat()
    key = (
        str(index_path),
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mtime_ns,
    )
    cached = _PARSE_CACHE.get(key)
    if cached is not None:
        _PARSE_CACHE.move_to_end(key)
        return cached
    archive = _parse_archive_uncached(index_path)
    _PARSE_CACHE[key] = archive
    _PARSE_CACHE.move_to_end(key)
    while len(_PARSE_CACHE) > _PARSE_CACHE_LIMIT:
        _PARSE_CACHE.popitem(last=False)
    return archive


def _parse_archive_uncached(index_path: Path) -> Archive:
    with index_path.open("rb") as stream:
        entry_count, reserved, populated_pack_count = struct.unpack(
            "<III", read_exact(stream, 12, "fixed header")
        )
        if not 1 <= entry_count <= MAX_ENTRIES:
            raise FormatError(f"implausible entry count {entry_count}")
        if reserved != 0:
            raise FormatError(f"reserved field is 0x{reserved:08x}, not zero")
        if not 1 <= populated_pack_count <= PACK_SLOT_COUNT:
            raise FormatError(f"implausible pack count {populated_pack_count}")
        block_counts = struct.unpack(
            f"<{PACK_SLOT_COUNT}I",
            read_exact(stream, PACK_SLOT_COUNT * 4, "pack size slots"),
        )
        if any(blocks == 0 for blocks in block_counts[:populated_pack_count]):
            raise FormatError("a populated pack has zero blocks")
        if any(blocks != 0 for blocks in block_counts[populated_pack_count:]):
            raise FormatError("an unused pack slot is nonzero")

        table_end = HEADER_SIZE + entry_count * ENTRY_SIZE
        if table_end > index_path.stat().st_size:
            raise FormatError("entry directory exceeds first pack")
        raw_entries = [
            struct.unpack("<III", read_exact(stream, ENTRY_SIZE, f"entry {index}"))
            for index in range(entry_count)
        ]

    packs: list[Pack] = []
    virtual_start = 0
    for ordinal, blocks in enumerate(block_counts[:populated_pack_count]):
        name = PACK_NAMES[ordinal]
        path = index_path.parent / name
        if not path.is_file():
            raise FormatError(f"missing declared pack {name}: {path}")
        size = blocks * ALIGNMENT
        actual = path.stat().st_size
        if actual != size:
            raise FormatError(
                f"pack {name}: declared 0x{size:x} bytes, actual 0x{actual:x}"
            )
        packs.append(Pack(ordinal, name, blocks, size, virtual_start, path))
        virtual_start += size

    pack_tuple = tuple(packs)
    starts = [pack.virtual_start for pack in pack_tuple]
    entries: list[Entry] = []
    previous_end = 0
    seen_ids: set[int] = set()
    for index, (name_id, size, offset_blocks) in enumerate(raw_entries):
        offset = offset_blocks * ALIGNMENT
        if name_id in seen_ids:
            raise FormatError(f"duplicate entry ID 0x{name_id:08x}")
        seen_ids.add(name_id)
        if offset < previous_end:
            raise FormatError(f"entry {index} overlaps or is out of order")
        if index > 0 and offset != align_up(previous_end):
            raise FormatError(
                f"entry {index} starts 0x{offset:x}; expected 0x{align_up(previous_end):x}"
            )
        segments = range_segments(pack_tuple, starts, offset, size)
        head = read_head(pack_tuple, segments)
        head_ascii = "".join(chr(value) if 0x20 <= value < 0x7F else "." for value in head)
        entries.append(
            Entry(index, name_id, size, offset_blocks, offset, head.hex(),
                  head_ascii, segments)
        )
        previous_end = offset + size

    if entries[0].virtual_offset != align_up(table_end):
        raise FormatError(
            f"first payload is 0x{entries[0].virtual_offset:x}; "
            f"aligned table end is 0x{align_up(table_end):x}"
        )
    if entries[-1].virtual_end != pack_tuple[-1].virtual_end:
        raise FormatError("last entry does not reach the virtual archive end")
    return Archive(index_path, reserved, populated_pack_count, pack_tuple,
                   tuple(entries))


def manifest(archive: Archive) -> dict[str, object]:
    return {
        "schema": "nfl2k5_outer_manifest/v1",
        "source_index": str(archive.index_path),
        "format": {
            "byte_order": "little",
            "alignment": ALIGNMENT,
            "header_size": HEADER_SIZE,
            "table_end": archive.table_end,
            "aligned_table_end": align_up(archive.table_end),
            "entry_count": len(archive.entries),
            "pack_count": len(archive.packs),
            "virtual_size": archive.virtual_size,
        },
        "validation": {
            "all_pack_sizes_match": True,
            "all_entry_ranges_in_bounds": True,
            "entry_ids_unique": True,
            "entries_monotonic_nonoverlapping": True,
            "coverage_reaches_virtual_end": True,
            "cross_volume_entry_count": sum(
                len(entry.segments) > 1 for entry in archive.entries
            ),
        },
        "entry_head_counts": dict(
            sorted(Counter(entry.head_ascii for entry in archive.entries).items())
        ),
        "packs": [
            {
                "ordinal": pack.ordinal,
                "name": pack.name,
                "blocks": pack.blocks,
                "size": pack.size,
                "virtual_start": pack.virtual_start,
                "virtual_end": pack.virtual_end,
            }
            for pack in archive.packs
        ],
        "entries": [
            {
                "table_index": entry.table_index,
                "name_id": f"0x{entry.name_id:08x}",
                "size": entry.size,
                "offset_blocks": entry.offset_blocks,
                "virtual_offset": entry.virtual_offset,
                "virtual_end": entry.virtual_end,
                "head_hex": entry.head_hex,
                "head_ascii": entry.head_ascii,
                "segments": [
                    {
                        "pack_ordinal": segment.pack_ordinal,
                        "pack_name": segment.pack_name,
                        "pack_offset": segment.pack_offset,
                        "size": segment.size,
                    }
                    for segment in entry.segments
                ],
            }
            for entry in archive.entries
        ],
    }


def print_summary(archive: Archive) -> None:
    cross = sum(len(entry.segments) > 1 for entry in archive.entries)
    print(
        f"NFL outer archive: {len(archive.entries)} entries, "
        f"{len(archive.packs)} packs, virtual size 0x{archive.virtual_size:x}, "
        f"cross-volume entries {cross}"
    )


def print_listing(archive: Archive) -> None:
    print("index\tname_id\tvirtual_offset\tsize\thead\tsegments")
    for entry in archive.entries:
        segments = ",".join(
            f"{part.pack_name}:0x{part.pack_offset:x}+0x{part.size:x}"
            for part in entry.segments
        )
        print(
            f"{entry.table_index}\t0x{entry.name_id:08x}\t"
            f"0x{entry.virtual_offset:x}\t0x{entry.size:x}\t"
            f"{entry.head_ascii}\t{segments}"
        )


def extract_entry(archive: Archive, entry: Entry, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    written = 0
    with output.open("wb") as destination:
        for segment in entry.segments:
            pack = archive.packs[segment.pack_ordinal]
            with pack.path.open("rb") as source:
                source.seek(segment.pack_offset)
                remaining = segment.size
                while remaining:
                    block = source.read(min(1024 * 1024, remaining))
                    if not block:
                        raise FormatError(
                            f"short read in pack {pack.name} at 0x{source.tell():x}"
                        )
                    destination.write(block)
                    written += len(block)
                    remaining -= len(block)
    if written != entry.size:
        raise FormatError(f"extracted {written} bytes; expected {entry.size}")


def read_entry_bytes(
    archive: Archive, entry: Entry, max_size: int | None = None
) -> bytes:
    """Read one bounded entry, stitching pack segments without a temp file."""
    if max_size is not None and entry.size > max_size:
        raise FormatError(
            f"entry {entry.table_index} is 0x{entry.size:x} bytes; "
            f"limit is 0x{max_size:x}"
        )
    result = bytearray()
    for segment in entry.segments:
        pack = archive.packs[segment.pack_ordinal]
        with pack.path.open("rb") as source:
            source.seek(segment.pack_offset)
            result.extend(read_exact(source, segment.size, f"entry {entry.table_index}"))
    if len(result) != entry.size:
        raise FormatError(f"read {len(result)} bytes; expected {entry.size}")
    return bytes(result)


def read_entry_range(
    archive: Archive, entry: Entry, relative_offset: int, size: int
) -> bytes:
    """Read a bounded relative range without materializing the full entry."""
    if relative_offset < 0 or size < 0:
        raise FormatError("entry range offset/size must be non-negative")
    relative_end = relative_offset + size
    if relative_end > entry.size:
        raise FormatError(
            f"entry {entry.table_index} range 0x{relative_offset:x}.."
            f"0x{relative_end:x} exceeds size 0x{entry.size:x}"
        )
    result = bytearray()
    entry_segment_start = 0
    for segment in entry.segments:
        entry_segment_end = entry_segment_start + segment.size
        part_start = max(relative_offset, entry_segment_start)
        part_end = min(relative_end, entry_segment_end)
        if part_start < part_end:
            pack = archive.packs[segment.pack_ordinal]
            pack_offset = segment.pack_offset + part_start - entry_segment_start
            with pack.path.open("rb") as source:
                source.seek(pack_offset)
                result.extend(
                    read_exact(
                        source,
                        part_end - part_start,
                        f"entry {entry.table_index} range",
                    )
                )
        entry_segment_start = entry_segment_end
        if part_end == relative_end:
            break
    if len(result) != size:
        raise FormatError(f"read {len(result)} range bytes; expected {size}")
    return bytes(result)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=Path, help="path to vc_53450030/0")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--manifest", type=Path, metavar="PATH")
    selector = parser.add_mutually_exclusive_group()
    selector.add_argument("--extract-index", type=int)
    selector.add_argument("--extract-id", type=lambda value: int(value, 0))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)

    try:
        archive = parse_archive(args.index)
        if args.list:
            print_listing(archive)
        elif args.manifest != Path("-"):
            print_summary(archive)
        if args.manifest is not None:
            text = json.dumps(manifest(archive), indent=2) + "\n"
            if args.manifest == Path("-"):
                sys.stdout.write(text)
            else:
                args.manifest.parent.mkdir(parents=True, exist_ok=True)
                args.manifest.write_text(text, encoding="utf-8", newline="\n")

        selected = None
        if args.extract_index is not None:
            if not 0 <= args.extract_index < len(archive.entries):
                raise FormatError("extract index out of range")
            selected = archive.entries[args.extract_index]
        elif args.extract_id is not None:
            selected = next(
                (entry for entry in archive.entries if entry.name_id == args.extract_id),
                None,
            )
            if selected is None:
                raise FormatError(f"entry ID 0x{args.extract_id:08x} not found")
        if selected is not None:
            if args.output is None:
                raise FormatError("--output is required when extracting")
            extract_entry(archive, selected, args.output)
            print(
                f"extracted index {selected.table_index} ID "
                f"0x{selected.name_id:08x} ({selected.size} bytes) to {args.output}"
            )
        elif args.output is not None:
            raise FormatError("--output requires --extract-index or --extract-id")
    except BrokenPipeError:
        return 0
    except (FormatError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
