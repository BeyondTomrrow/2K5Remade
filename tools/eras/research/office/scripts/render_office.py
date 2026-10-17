"""Preview renders of the exported offices at each keyed camera pose.
Usage: python render_office.py [coach_desk front_office draft_menu]  (needs ../*_o23c*/ exports and ../camera_keys.json)"""
ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pathlib import Path
import numpy as np
from PIL import Image
import render as R
OUT = Path(__import__("os").path.join(ROOT, r"build\eras\office\previews")); OUT.mkdir(parents=True, exist_ok=True)
ST = Path(__import__("os").path.join(ROOT, r"build\eras\office\standalone_textures\o24"))
def img(p): return np.asarray(Image.open(p).convert('RGBA')).astype(np.float32)/255
team = {"teamlogo": img(ST/"000_00_teamlogo_00_h0.png"), "wallphoto": img(ST/"002_wallphoto.png"), "office_photos": img(ST/"003_office_photos.png")}
cams = json.load(open(__import__("os").path.join(ROOT, r"build/eras/office/camera_keys.json")))
jobs = {
 "coach_desk": ("coach_desk_o23c4/coach_desk.gltf", "o23c4", {"0 idle (hub)": 0, "1 ESPN.com/Features/Options (monitor)": 1, "5 Schedule (day planner)": 5, "9 Gameplan (notebook)": 9, "13 Front Office / Crib (door)": 13, "17 unused (file cabinet)": 17}),
 "front_office": ("front_office_o23c6/front_office.gltf", "o23c6", {"0 entry": 0, "1 idle (menu)": 1, "3 Status/Cap/Needs/Trades (monitor)": 3, "7 Contracts/Progress (desk papers)": 7}),
 "draft_menu": ("draft_menu_o23c5/draft_menu.gltf", "o23c5", {"4 centre": 4, "6 left monitor": 6, "14 PDA": 14, "18 draft board": 18}),
}
which = sys.argv[1:] or list(jobs)
for name in which:
    gl, key, poses = jobs[name]
    mats, tris = R.load_gltf(Path(__import__("os").path.join(ROOT, r"build\eras\office")) / gl, team)
    samples = cams[key]["anims"][0]["samples_per_time_unit"]
    tiles = []
    for label, t in poses.items():
        s = samples[str(t)]
        im = R.render(mats, tris, s[:3], s[3:6], W=480, H=360)
        im.save(OUT / f"{name}_t{t:02d}.png")
        tiles.append((label, im))
        print(name, label, s)
    from PIL import ImageDraw
    cols = 2; rows = (len(tiles)+1)//2
    sheet = Image.new("RGB", (cols*480, rows*380), (30,30,30)); d = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(tiles):
        x = (i % cols)*480; y = (i//cols)*380
        sheet.paste(im, (x, y+20)); d.text((x+4, y+4), f"{name} t={label}", fill=(255,255,0))
    sheet.save(OUT / f"{name}_camera_poses.png")
