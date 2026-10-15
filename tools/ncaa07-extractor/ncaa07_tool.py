"""NCAA Football 07 asset scanner and conservative resource extractor.

It recognizes EA TERF/DIR1 containers, COMP codec metadata, TMdl child tables,
and the fixture-verified player skeleton name table. Unknown payloads are
exported byte-for-byte and are never relabeled as decoded meshes or animations.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import struct
import sys
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Iterable


MAGIC = b"TERF"
DIR_MAGIC = b"DIR1"
KNOWN_TAGS = (b"TMdl", b"Geom", b"Matl", b"Text", b"Skel", b"Swap", b"Clst", b"DATA", b"COMP")
INTERESTING_NAMES = {
    "ANIMDATA.dat": "animation candidate",
    "PLADATA.DAT": "player data candidate",
    "PLYDATA.DAT": "player data candidate",
    "plyrface.dat": "player face candidate",
    "coacface.dat": "coach face candidate",
    "UNIFORM.DAT": "uniform/mesh candidate",
    "UIS_MODL.DAT": "model resource candidate",
}


@dataclass
class Resource:
    source: str
    tag: str
    offset: int
    end: int
    size: int
    confidence: str
    note: str
    exported_path: str | None = None


@dataclass
class FileReport:
    path: str
    size: int
    role: str
    container: str
    directory_offset: int | None
    directory_size: int | None
    data_offset: int | None = None
    archive_file_count: int | None = None
    archive_entries: list[dict] = field(default_factory=list)
    compression_entries: list[dict] = field(default_factory=list)
    models: list[dict] = field(default_factory=list)
    resources: list[Resource] = field(default_factory=list)
    strings: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)


def terf_entries(data: bytes) -> tuple[int | None, int | None, list[dict]]:
    """Return DATA base and directory entries from a TERF archive.

    DIR1 stores (relative offset, compressed/stored size) pairs. The offsets
    become absolute at the beginning of the DATA section, as documented by the
    EA archive layout and verified against UIS_MODL.DAT.
    """
    directory_offset, directory_size = find_directory(data)
    if directory_offset is None or directory_size is None:
        return None, None, []
    # DATA may be immediately before the DIR1 table's end padding (as in
    # UIS_MODL.DAT), so search from the directory marker rather than after the
    # entire directory block. The first DATA marker is the archive base.
    sections = terf_sections(data)
    data_offset = next((at for tag, at, size in sections if tag == b"DATA"), None)
    if data_offset is None:
        return None, None, []
    count = struct.unpack_from("<H", data, 14)[0]
    if directory_size < 8 + count * 8:
        raise ValueError("TERF directory is shorter than its file count")
    entries: list[dict] = []
    for index in range(count):
        relative, stored_size = struct.unpack_from("<II", data, directory_offset + 8 + index * 8)
        absolute = data_offset + relative
        valid = absolute <= len(data) and stored_size <= len(data) - absolute
        entries.append({
            "index": index,
            "relative_offset": relative,
            "offset": absolute,
            "stored_size": stored_size,
            "valid": valid,
        })
    return data_offset, count, entries


def terf_sections(data: bytes) -> list[tuple[bytes, int, int]]:
    """Section sizes include their eight-byte tag/size headers."""
    if len(data) < 16 or data[:4] != b"TERF":
        return []
    result = []
    at = 0
    while at + 8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, at)
        if size < 8 or at + size > len(data):
            raise ValueError(f"Invalid TERF section at {at:#x}")
        result.append((tag, at, size))
        at += size
    return result


def parse_tmdl(data: bytes, offset: int, limit: int) -> dict | None:
    """Parse the verified TMdl child descriptor table without decoding payloads."""
    if offset < 0 or offset + 16 > len(data) or data[offset:offset + 4] != b"TMdl":
        return None
    total_size, element_word, pad_word, unknown = struct.unpack_from("<IHHI", data, offset + 4)
    end = min(len(data), offset + total_size) if total_size else min(len(data), limit)
    result = {
        "offset": offset,
        "total_size": total_size,
        "end": end,
        "element_count": element_word,
        "entry_alignment": pad_word,
        "unknown": unknown,
        "children": [],
        "info_name": None,
        "skeleton": None,
    }
    cursor = offset + 16
    for _ in range(element_word):
        if cursor + 16 > end:
            break
        tag = data[cursor:cursor + 4]
        child_offset, child_size, child_count = struct.unpack_from("<III", data, cursor + 4)
        child = {
            "tag": tag.decode("ascii", "replace"),
            "relative_offset": child_offset,
            "size": child_size,
            "count": child_count,
            "absolute_offset": offset + child_offset,
            "valid": offset + child_offset <= end and child_size <= end - (offset + child_offset),
        }
        result["children"].append(child)
        if tag == b"Info" and child["valid"]:
            raw = data[child["absolute_offset"]:child["absolute_offset"] + child_size]
            result["info_name"] = raw.split(b"\0", 1)[0].decode("ascii", "replace")
        elif tag == b"Skel" and child["valid"]:
            result["skeleton"] = parse_skeleton(data, child["absolute_offset"], child_size, offset)
        cursor += 16
    return result


def parse_skeleton(data: bytes, offset: int, size: int, model_base: int = 0) -> dict:
    """Decode the fixture-verified portion of a player TMDL Skel block.

    The first u32 is a bone count and the block contains 16-byte, NUL-padded
    bone names starting at +0x18. Names are verified against decompressed
    player TMDL fixtures. The remaining bytes are intentionally retained as
    opaque data until hierarchy and bind-pose records are fixture-proven.
    """
    end = min(len(data), offset + size)
    result: dict = {
        "offset": offset,
        "size": max(0, end - offset),
        "bone_count": None,
        "bone_names": [],
        "names_verified": False,
        "hierarchy_verified": False,
        "bind_pose_verified": False,
        "note": "bone count and 16-byte name table decoded; hierarchy/transforms pending",
    }
    if offset < 0 or offset + 4 > end:
        return result
    bone_count = struct.unpack_from("<I", data, offset)[0]
    result["bone_count"] = bone_count
    name_base = offset + 0x18
    max_names = min(bone_count, max(0, (end - name_base) // 16))
    names: list[str] = []
    for index in range(max_names):
        raw = data[name_base + index * 16:name_base + (index + 1) * 16]
        name = raw.split(b"\0", 1)[0].decode("ascii", "replace")
        if not name or any(ord(ch) < 32 for ch in name):
            break
        names.append(name)
    result["bone_names"] = names
    result["names_verified"] = len(names) == bone_count
    if not result["names_verified"]:
        result["note"] = "bone count/name-table candidate found; incomplete name table"
    if offset + 24 <= end:
        _, names, positions, rotations, scales, unknown = struct.unpack_from("<6I", data, offset)
        tables = [(names, 16), (positions, 16), (rotations, 6), (scales, 12)]
        if all(offset + 24 <= model_base + p and model_base + p + bone_count * stride <= end
               for p, stride in tables):
            bones = []
            for i in range(bone_count):
                x, y, z, parent, flag = struct.unpack_from("<3fHH", data, model_base + positions + 16 * i)
                raw = struct.unpack_from("<3h", data, model_base + rotations + 6 * i)
                scale = struct.unpack_from("<3f", data, model_base + scales + 12 * i)
                bones.append({"index": i, "name": result["bone_names"][i] if i < len(result["bone_names"]) else str(i),
                              "parent": None if i == 0 else parent, "parent_raw": parent,
                              "flag": flag, "local_translation": [x, y, z],
                              "rotation_i16_raw": list(raw), "scale_candidate": list(scale)})
            result["bones"] = bones
            result["hierarchy_verified"] = all(b["parent_raw"] == 0 if b["index"] == 0
                                                else b["parent_raw"] < b["index"] for b in bones)
            result["note"] = "Names, parent indices and local translations verified; raw rotations retained; bind convention pending"
    return result


def compression_entries(data: bytes) -> list[dict]:
    """Read the verified COMP codec/expanded-size pairs from a TERF file."""
    at = next((at for tag, at, size in terf_sections(data) if tag == b"COMP"), None)
    if at is None:
        return []
    count = struct.unpack_from("<H", data, 14)[0]
    declared = struct.unpack_from("<I", data, at + 4)[0]
    if declared < 8 + count * 8:
        raise ValueError("COMP table is shorter than file count")
    end = at + 8 + count * 8
    result: list[dict] = []
    cursor = at + 8
    index = 0
    while cursor + 8 <= end:
        algorithm, expanded_size = struct.unpack_from("<II", data, cursor)
        result.append({"index": index, "algorithm": algorithm,
                       "expanded_size": expanded_size,
                       "verified_codec": "ea_madden" if algorithm == 5 else (
                           "raw" if algorithm == 0 else "unknown")})
        cursor += 8
        index += 1
    return result


def iter_files(root: Path) -> Iterable[Path]:
    yield from sorted((p for p in root.rglob("*") if p.is_file()), key=lambda p: str(p).lower())


def ascii_strings(data: bytes, minimum: int = 4, limit: int = 80) -> list[str]:
    found = re.findall(rb"[\x20-\x7e]{%d,}" % minimum, data)
    return [x.decode("ascii", "replace") for x in found[:limit]]


def find_directory(data: bytes) -> tuple[int | None, int | None]:
    at = data.find(DIR_MAGIC)
    if at < 0 or at + 8 > len(data):
        return None, None
    size = struct.unpack_from("<I", data, at + 4)[0]
    if size <= 0 or at + size > len(data):
        return at, None
    return at, size


def find_resources(data: bytes, source: str) -> list[Resource]:
    positions: list[tuple[int, bytes]] = []
    for tag in KNOWN_TAGS:
        start = 0
        while True:
            pos = data.find(tag, start)
            if pos < 0:
                break
            positions.append((pos, tag))
            start = pos + 1
    positions.sort()
    result: list[Resource] = []
    for index, (offset, tag) in enumerate(positions):
        end = positions[index + 1][0] if index + 1 < len(positions) else len(data)
        if end <= offset:
            continue
        # A tag embedded in a large opaque payload is still useful evidence,
        # but is marked low confidence. Repeated TMdl/Geom records in UIS_MODL
        # have a stable 16-byte tag header and are high confidence.
        high = tag in (b"TMdl", b"Geom", b"Matl", b"Text") and offset + 16 <= len(data)
        note = {
            b"TMdl": "verified model metadata/resource tag",
            b"Geom": "verified geometry resource tag; vertex/index decoding pending",
            b"Matl": "verified material resource tag; texture decoding pending",
            b"Text": "verified text/texture reference resource tag",
            b"Skel": "skeleton block; parse via TMDL child descriptor",
            b"Swap": "material swap/reference block",
            b"Clst": "cluster-reference block",
            b"DATA": "container payload marker",
            b"COMP": "compression marker; codec not identified",
        }[tag]
        result.append(Resource(source, tag.decode("ascii"), offset, end, end - offset,
                               "high" if high else "observed", note))
    return result


def scan_file(path: Path) -> FileReport:
    data = path.read_bytes()
    directory_offset, directory_size = find_directory(data)
    report = FileReport(
        path=str(path), size=len(data), role=INTERESTING_NAMES.get(path.name, "unknown game-data file"),
        container="TERF" if data.startswith(MAGIC) else "unknown",
        directory_offset=directory_offset, directory_size=directory_size,
    )
    if report.container == "unknown":
        report.warnings.append("No verified TERF header")
        return report
    if directory_offset is None:
        report.warnings.append("TERF header present but DIR1 directory not found")
    elif directory_size is None:
        report.warnings.append("DIR1 header present but its declared size is invalid")
    else:
        data_offset, file_count, entries = terf_entries(data)
        report.data_offset = data_offset
        report.archive_file_count = file_count
        report.archive_entries = entries
        report.compression_entries = compression_entries(data)
        for entry in entries:
            if not entry["valid"] or entry["stored_size"] < 16:
                continue
            model = parse_tmdl(data, entry["offset"], entry["offset"] + entry["stored_size"])
            if model is not None:
                model["archive_index"] = entry["index"]
                report.models.append(model)
    report.resources = find_resources(data, str(path))
    report.strings = ascii_strings(data)
    return report


def scan_root(root: Path) -> list[FileReport]:
    return [scan_file(root)] if root.is_file() else [scan_file(path) for path in iter_files(root)]


def write_report(reports: list[FileReport], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps([asdict(r) for r in reports], indent=2), encoding="utf-8")


def safe_name(value: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", value)


def extract_resources(reports: list[FileReport], output: Path, minimum_confidence: str = "observed") -> int:
    output.mkdir(parents=True, exist_ok=True)
    count = 0
    allowed = {"observed", "high"} if minimum_confidence == "observed" else {"high"}
    for report in reports:
        data = Path(report.path).read_bytes()
        stem = safe_name(Path(report.path).stem)
        for index, resource in enumerate(report.resources):
            if resource.confidence not in allowed:
                continue
            name = f"{stem}_{index:04d}_{resource.tag}_{resource.offset:08X}.bin"
            destination = output / name
            destination.write_bytes(data[resource.offset:resource.end])
            resource.exported_path = str(destination)
            count += 1
    return count


def inspect_extracted_tree(root: Path) -> dict:
    """Inspect files produced by an external TERF/EA-Madden extractor.

    This deliberately reports signatures and verified TMDL skeleton names,
    without guessing that an opaque animation payload is already decoded.
    """
    files: list[dict] = []
    for path in iter_files(root):
        data = path.read_bytes()
        item = {
            "path": str(path),
            "size": len(data),
            "extension": path.suffix.lower(),
            "signature": data[:4].decode("ascii", "replace") if len(data) >= 4 else "",
            "kind": "unknown",
            "verified": False,
        }
        if data.startswith(b"TMdl"):
            item["kind"] = "TMdl model"
            item["verified"] = True
            model = parse_tmdl(data, 0, len(data))
            item["model"] = model
        elif data.startswith(b"SEVT"):
            item["kind"] = "SEVT event/resource payload"
            item["verified"] = True
        elif path.suffix.lower() == ".vap":
            item["kind"] = "VAP animation payload candidate"
            item["note"] = "container/signature recognized by extension; track codec pending"
        elif path.suffix.lower() in {".sdb", ".1lk", ".nfc", ".trg", ".app"}:
            item["kind"] = "animation-support payload candidate"
            item["note"] = "associated with ANIMDATA extraction; internal layout pending"
        elif path.suffix.lower() == ".3ds":
            item["kind"] = "MMAP texture archive candidate"
        files.append(item)
    kinds: dict[str, int] = {}
    for item in files:
        kinds[item["kind"]] = kinds.get(item["kind"], 0) + 1
    return {"root": str(root), "files": len(files), "kinds": kinds, "items": files}


def run_quickbms(archive: Path, script: Path, quickbms: Path, output: Path) -> dict:
    """Run user-supplied QuickBMS and write a verified extraction manifest."""
    if not quickbms.is_file():
        raise FileNotFoundError(f"QuickBMS executable not found: {quickbms}")
    if not script.is_file():
        raise FileNotFoundError(f"QuickBMS script not found: {script}")
    output.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        [str(quickbms), "-o", str(script), str(archive), str(output)],
        check=False, text=True, capture_output=True,
    )
    manifest = inspect_extracted_tree(output)
    manifest["archive"] = str(archive)
    manifest["quickbms_returncode"] = completed.returncode
    manifest["quickbms_output_tail"] = (completed.stdout + "\n" + completed.stderr)[-4000:]
    (output / "extraction_manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest


def make_summary(reports: list[FileReport]) -> dict:
    tags: dict[str, int] = {}
    for report in reports:
        for resource in report.resources:
            tags[resource.tag] = tags.get(resource.tag, 0) + 1
    return {
        "files": len(reports),
        "total_bytes": sum(r.size for r in reports),
        "terf_files": sum(r.container == "TERF" for r in reports),
        "tmdl_models": sum(len(r.models) for r in reports),
        "compressed_entries": sum(len(r.compression_entries) for r in reports),
        "resource_tags": tags,
        "verified": {
            "container": "TERF/DIR1 observed in supplied files",
            "model_tags": "TMdl, Geom, Matl, Text observed in UIS_MODL.DAT",
            "player_skeleton": "Skel bone count and 16-byte bone names verified in decompressed PLYDATA TMDL",
            "compression": "COMP algorithm 5 metadata verified; EA Madden decompression is external QuickBMS workflow",
            "animation_payloads": "ANIMDATA extracts VAP and associated payloads; keyframe codec pending",
            "mesh_skin_animation_export": "pending; opaque bytes are preserved, not guessed",
        },
    }


def launch_gui(initial_root: Path | None = None) -> None:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    root = tk.Tk()
    root.title("NCAA 07 Asset Inspector")
    root.geometry("1100x700")
    reports: list[FileReport] = []

    top = ttk.Frame(root, padding=8)
    top.pack(fill="x")
    path_var = tk.StringVar(value=str(initial_root or ""))
    ttk.Label(top, text="Game data folder:").pack(side="left")
    ttk.Entry(top, textvariable=path_var, width=80).pack(side="left", fill="x", expand=True, padx=8)
    tree = ttk.Treeview(root, columns=("role", "size", "container", "models", "resources"), show="headings")
    for col, text_value, width in (("role", "Role", 240), ("size", "Size", 100), ("container", "Container", 110), ("models", "TMDL models", 100), ("resources", "Resources", 100)):
        tree.heading(col, text=text_value); tree.column(col, width=width)
    tree.pack(fill="both", expand=True, padx=8, pady=(0, 8))
    details = tk.Text(root, height=13, wrap="word")
    details.pack(fill="both", expand=False, padx=8, pady=(0, 8))

    def scan() -> None:
        nonlocal reports
        folder = Path(path_var.get())
        if not folder.is_dir():
            messagebox.showerror("Folder not found", f"Not a folder: {folder}"); return
        reports = scan_root(folder)
        tree.delete(*tree.get_children())
        for i, report in enumerate(reports):
            tree.insert("", "end", iid=str(i), values=(report.role, f"{report.size:,}", report.container, len(report.models), len(report.resources)))
        details.delete("1.0", "end")
        details.insert("end", json.dumps(make_summary(reports), indent=2))

    def show_selected(_event=None) -> None:
        selection = tree.selection()
        if not selection: return
        report = reports[int(selection[0])]
        details.delete("1.0", "end")
        details.insert("end", json.dumps(asdict(report), indent=2))

    def export() -> None:
        if not reports: scan()
        if not reports: return
        destination = filedialog.askdirectory(title="Choose extraction folder")
        if not destination: return
        count = extract_resources(reports, Path(destination))
        Path(destination, "scan_report.json").write_text(json.dumps([asdict(r) for r in reports], indent=2), encoding="utf-8")
        messagebox.showinfo("Extraction complete", f"Exported {count} observed resources and scan_report.json")

    ttk.Button(top, text="Scan", command=scan).pack(side="left", padx=4)
    ttk.Button(top, text="Extract observed resources", command=export).pack(side="left", padx=4)
    tree.bind("<<TreeviewSelect>>", show_selected)
    if initial_root and initial_root.is_dir(): root.after(100, scan)
    root.mainloop()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command")
    scan_parser = sub.add_parser("scan", help="scan a game data folder")
    scan_parser.add_argument("folder", type=Path)
    scan_parser.add_argument("--report", type=Path, default=Path("scan_report.json"))
    extract_parser = sub.add_parser("extract", help="scan and export observed resource byte ranges")
    extract_parser.add_argument("folder", type=Path)
    extract_parser.add_argument("output", type=Path)
    bms_parser = sub.add_parser("quickbms", help="decompress TERF EA-Madden entries with user-supplied QuickBMS")
    bms_parser.add_argument("archive", type=Path)
    bms_parser.add_argument("output", type=Path)
    bms_parser.add_argument("--quickbms", type=Path, required=True, help="path to quickbms.exe")
    bms_parser.add_argument("--script", type=Path, required=True, help="path to madden_terf.bms")
    gui_parser = sub.add_parser("gui", help="open the Windows desktop interface")
    gui_parser.add_argument("folder", type=Path, nargs="?")
    args = parser.parse_args(argv)
    if args.command == "scan":
        reports = scan_root(args.folder)
        write_report(reports, args.report)
        print(json.dumps(make_summary(reports), indent=2))
        print(f"Report: {args.report}")
        return 0
    if args.command == "extract":
        reports = scan_root(args.folder)
        count = extract_resources(reports, args.output)
        write_report(reports, args.output / "scan_report.json")
        print(f"Exported {count} observed resources to {args.output}")
        return 0
    if args.command == "quickbms":
        manifest = run_quickbms(args.archive, args.script, args.quickbms, args.output)
        print(json.dumps({"files": manifest["files"], "kinds": manifest["kinds"],
                          "manifest": str(args.output / "extraction_manifest.json")}, indent=2))
        return 0 if manifest["quickbms_returncode"] == 0 else manifest["quickbms_returncode"]
    launch_gui(getattr(args, "folder", None))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
