"""Export SCNE scenes to glTF + PNG textures + scene_summary.json with 2K Football Mod Tools.
Usage: python export.py <folder with inventory.json> <out folder> o23c4 o23c6 ..."""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools"))
from pathlib import Path
from mod_editor.core import nfl2k5_models as M
SP = Path(sys.argv[1]); OUT = Path(sys.argv[2]); keys = sys.argv[3:]
src = M.ModelSource(Path(__import__("os").path.join(ROOT, r"original\disc\vc_53450030\0")), SP/"inventory.json")
inv = M._tools_module("nfl_scne_inventory"); txtr = M._tools_module("nfl_txtr")
for key in keys:
    resource, decoded, scene = src.parse(key)
    name = scene["name"]
    d = OUT / f"{name}_{key}"
    d.mkdir(parents=True, exist_ok=True)
    try:
        res = M.export_model(src, key, d / f"{name}.gltf")
        print(res.summary())
    except Exception as e:
        print("EXPORT FAIL", key, name, e)
    tdir = d / "textures"; tdir.mkdir(exist_ok=True)
    mapped = {}
    for m in scene["materials"]:
        if m.get("texture_index") is not None:
            mapped.setdefault(int(m["texture_index"]), []).append(m.get("name"))
    texrows = []
    for t in scene["embedded_textures"]:
        ti = int(t["index"])
        row = {"index": ti, "materials": mapped.get(ti, [])}
        try:
            info = inv.texture_info(decoded, int(t["descriptor_offset"]), str(name), ti)
            rgba = txtr.texture_to_rgba(decoded, resource.as_chunk(), info)
            nm = (mapped.get(ti) or [f"tex{ti}"])[0] or f"tex{ti}"
            fn = f"{ti:03d}_{M.safe_file_name(nm)}.png"
            (tdir / fn).write_bytes(txtr.encode_rgba_png(info.width, info.height, rgba))
            row.update(file=fn, width=info.width, height=info.height, fmt=str(info.format_name))
        except Exception as e:
            row["error"] = str(e)
        row["raw"] = {k: v for k, v in t.items() if isinstance(v, (int, str, float))}
        texrows.append(row)
    def clean(x):
        if isinstance(x, dict): return {k: clean(v) for k, v in x.items() if not isinstance(v, (bytes, bytearray))}
        if isinstance(x, list): return [clean(v) for v in x][:2000]
        return x
    summary = {"key": key, "name": name,
               "nodes": clean(scene.get("nodes", [])),
               "materials": clean(scene.get("materials", [])),
               "textures": texrows,
               "shape_count": len(scene.get("shapes", [])),
               "shapes": [clean({k: v for k, v in s.items() if k in ("index","name","vertex_count","node_index","bounds","name_offset")}) for s in scene.get("shapes", [])],
               "scene_keys": sorted(scene.keys())}
    (d / "scene_summary.json").write_text(json.dumps(summary, indent=1, default=str))
    print(key, name, "nodes", len(scene.get("nodes", [])), "materials", len(scene["materials"]), "textures", len(texrows))
