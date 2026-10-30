#!/usr/bin/env python3
"""Contact sheet of the exported office textures, labelled with the materials
that use them (office_inventory.json). Local reference only.

    python -I build/eras/office/contact_sheet.py build/eras/office <scene> <out.png>
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw


def main(argv):
    base, scene, out = Path(argv[1]), argv[2], Path(argv[3])
    inv = json.loads((base / "office_inventory.json").read_text(encoding="utf-8"))
    res = next(r for p in inv["packages"] if p["file"] == "fr.iff" for r in p["resources"]
               if r["kind"] == "SCNE" and r.get("name") == scene)
    users = {}
    for m in res["materials"]:
        if m["texture_index"] is not None:
            users.setdefault(m["texture_index"], []).append(m["name"])
    cell, cols = 200, 6
    texs = res["embedded_textures"]
    rows = (len(texs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 34)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, t in enumerate(texs):
        p = base / "textures" / "fr.iff" / f"{scene}_tex{i:03d}_{t['w']}x{t['h']}.png"
        x, y = (i % cols) * cell, (i // cols) * (cell + 34)
        if p.is_file():
            im = Image.open(p).convert("RGBA")
            im.thumbnail((cell - 8, cell - 8))
            bg = Image.new("RGBA", im.size, (128, 128, 128, 255))
            bg.alpha_composite(im)
            sheet.paste(bg.convert("RGB"), (x + 4, y + 4))
        label = f"#{i} {t['w']}x{t['h']}"
        d.text((x + 4, y + cell), label, fill=(255, 255, 0))
        d.text((x + 4, y + cell + 14), ",".join(users.get(i, ["-"]))[:34], fill=(220, 220, 220))
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main(sys.argv)
