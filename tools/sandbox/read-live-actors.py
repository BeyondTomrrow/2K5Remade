"""Read-only actor/roster correlation for a running development game.

No process suspension, debugger attachment or WriteProcessMemory. The offset
must come from this run's MemoryLayoutInit log; addresses are guest addresses.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import struct

p = argparse.ArgumentParser()
p.add_argument("--pid", required=True, type=int)
p.add_argument("--offset", required=True, type=lambda s: int(s, 0))
p.add_argument("--output", required=True, type=Path)
args = p.parse_args()
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.ReadProcessMemory.restype = w.BOOL
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
k.QueryFullProcessImageNameW.restype = w.BOOL
k.CloseHandle.argtypes = [w.HANDLE]
h = k.OpenProcess(0x1010, False, args.pid)  # query limited + VM read only
if not h:
    raise c.WinError(c.get_last_error())

try:
    image = c.create_unicode_buffer(32768)
    image_len = w.DWORD(len(image))
    if not k.QueryFullProcessImageNameW(h, 0, image, c.byref(image_len)):
        raise c.WinError(c.get_last_error())
    project = Path(__file__).resolve().parents[2]
    exe = Path(image.value).resolve()
    if exe.name.lower() != "nfl2k5.exe" or project not in exe.parents:
        raise RuntimeError("Refusing to inspect a process outside this game's project")

    def read(address, size):
        if not (0 < size <= 8192 and (0x10000 <= address < 0x08000000 or 0x80000000 <= address < 0x88000000)):
            return b""
        buf = c.create_string_buffer(size)
        done = c.c_size_t()
        if not k.ReadProcessMemory(h, address + args.offset, buf, size, c.byref(done)) or done.value != size:
            return b""
        return buf.raw

    def u32(address):
        data = read(address, 4)
        return struct.unpack("<I", data)[0] if data else 0

    def text(address):
        data = read(address, 96)
        if not data:
            return ""
        return data.decode("utf-16le", "replace").split("\0", 1)[0][:40]

    rosters = []
    known = {}
    for side, team in (("home", 0xB30864), ("away", 0xB30A58)):
        count = u32(team + 0x11C) & 0xFF  # byte count; following bytes are flags
        if count > 71:
            count = 0
        for index in range(count):
            address = u32(team + index * 4)
            if not address:
                continue
            name = (text(u32(address + 0x10)) + " " + text(u32(address + 0x14))).strip()
            row = dict(side=side, index=index, record=address, name=name,
                       number=(u32(address + 0x20) >> 3) & 127,
                       live_slot=u32(address + 0x30))
            rosters.append(row)
            known[address] = f"{side}:{index}:{name}"

    lists = []
    for root in (0xE60268, 0xE60274):
        node, seen = u32(root), set()
        nodes = []
        while node and node not in seen and len(seen) < 64:
            seen.add(node)
            data = read(node, 0x50)
            if not data:
                break
            fields = struct.unpack("<20I", data)
            row = dict(node=node, payload=fields[1], geometry=fields[6], context_38=fields[14],
                       context_3c=fields[15], descriptor=u32(fields[15]+0x30), flags=fields[18], matches=[])
            payload = read(fields[1], 0x340)
            if payload:
                row['translations'] = [struct.unpack_from('<3f', payload, at) for at in (0x130,0x230,0x330)]
            for offset in (0x4, 0x10, 0x14, 0x18, 0x38, 0x3C):
                obj = fields[offset // 4]
                buf = read(obj, 0x800)
                if not buf:
                    continue
                for pos in range(0, len(buf), 4):
                    value = struct.unpack_from("<I", buf, pos)[0]
                    if value in known:
                        row["matches"].append(dict(via=offset, object=obj, offset=pos, roster=known[value]))
            descriptor = row['descriptor']
            data = read(descriptor, 0x200)
            if data:
                row['descriptor_words'] = list(struct.unpack('<128I',data))
                for pos in range(0,len(data),4):
                    value=struct.unpack_from('<I',data,pos)[0]
                    if value in known:
                        row['matches'].append(dict(via='ctx3c+30',object=descriptor,offset=pos,roster=known[value]))
            nodes.append(row)
            node = fields[12]
        lists.append(dict(root=root, nodes=nodes))
    output = dict(pid=args.pid, guest_offset=args.offset, clock=u32(0xE6028C),
                  stage=u32(0xA83A18), period=u32(0xE602C4), rosters=rosters, lists=lists)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2), encoding="utf-8")
    hits = sum(len(row["matches"]) for group in lists for row in group["nodes"])
    print(f"Read {len(rosters)} roster records and {sum(len(g['nodes']) for g in lists)} pool nodes; {hits} pointer correlations. No game memory changed.")
finally:
    k.CloseHandle(h)
