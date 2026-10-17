"""Cameras and keyed camera poses of SCNE scenes.
Usage: python scenecams.py <folder with inventory.json> o23c4 o23c6 o23c5
(inventory.json from .tmp-2k-football-mod-tools/tools/nfl_resource_scan.py <disc>/vc_53450030/0 --json ...)
Writes build/eras/office/camera_keys.json."""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json, struct
from pathlib import Path
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pathlib import Path
from mod_editor.core import nfl2k5_models as M
import camcurve as CC
SP = Path(sys.argv[1])
src = M.ModelSource(Path(__import__("os").path.join(ROOT, r"original\disc\vc_53450030\0")), SP/"inventory.json")
result = {}
for key in sys.argv[2:]:
    resource, d, scene = src.parse(key)
    name = scene["name"]
    def u32(o): return struct.unpack_from('<I', d, o)[0]
    def rel(f):
        v = u32(f); return f + v - 1 if v & 1 else None
    def u16s(o):
        s = []
        while True:
            c = struct.unpack_from('<H', d, o)[0]
            if c == 0: break
            s.append(chr(c)); o += 2
        return ''.join(s)
    H = 0x100
    tables = {}
    for nm, cnt_off in (("anim",0x0C),("textures",0x14),("materials",0x1C),("nodes",0x24),("shapes",0x2C),("markers",0x34),("cameras",0x3C),("aux50",0x44)):
        tables[nm] = (u32(H+cnt_off), rel(H+cnt_off+4) if u32(H+cnt_off) else None)
    out = {"key": key, "name": name, "tables": {k: [v[0], hex(v[1]) if v[1] else None] for k, v in tables.items()}, "cameras": [], "anims": []}
    n, base = tables["cameras"]
    cam_ranges = []
    for i in range(n):
        r = base + 0x60*i
        nmp = rel(r)
        f = struct.unpack_from('<20f', d, r+0x10)
        cam = {"name": u16s(nmp) if nmp else None, "record": hex(r), "right": f[0:3], "up": f[4:7], "back": f[8:11], "eye": f[12:15], "focal_mm": f[16], "extra": f[17:20]}
        out["cameras"].append(cam); cam_ranges.append((r, r+0x60, cam["name"]))
    n, base = tables["anim"]
    for i in range(n):
        r = base + 0x14*i
        h = u32(r); z = u32(r+4); flags = u32(r+8); tgt = rel(r+12); dat = rel(r+16)
        owner = None
        for a, b, cn in cam_ranges:
            if a <= tgt < b: owner = f"camera {cn} +0x{tgt-a:x}"
        if owner is None:
            nb = tables["nodes"]
            if nb[1] and nb[1] <= tgt < nb[1] + 0x60*nb[0]:
                idx = (tgt-nb[1])//0x60; owner = f"node {scene['nodes'][idx]['name']} +0x{(tgt-nb[1])%0x60:x}"
        chs = CC.channels(d, dat)
        times = chs[0]["times"] if chs else []
        samples = {}
        if chs:
            end = max(c["times"][-1] for c in chs)
            for k in range(0, int(end/300)+1):
                samples[str(k)] = [round(CC.ev(c, k*300), 2) for c in chs]
        out["anims"].append({"hash": hex(h), "flags": hex(flags), "target": hex(tgt), "owner": owner, "data": hex(dat), "channels": len(chs), "key_ticks": times, "samples_per_time_unit": samples})
    result[key] = out
    print(json.dumps(out, indent=1)[:6000])
(Path(__import__("os").path.join(ROOT, r"build/eras/office"))/"camera_keys.json").write_text(json.dumps(result, indent=1))
