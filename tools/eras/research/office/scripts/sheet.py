import sys
from pathlib import Path
from PIL import Image, ImageDraw
d = Path(sys.argv[1]); out = Path(sys.argv[2])
files = sorted(d.glob("*.png"))
cell = 160; cols = 6
rows = (len(files)+cols-1)//cols
sheet = Image.new("RGB", (cols*cell, rows*(cell+14)), (60,60,60))
dr = ImageDraw.Draw(sheet)
for i,f in enumerate(files):
    im = Image.open(f).convert("RGBA")
    w,h = im.size
    bg = Image.new("RGBA", im.size, (255,0,255,255)); bg.alpha_composite(im)
    im = bg.convert("RGB"); im.thumbnail((cell,cell))
    x = (i%cols)*cell; y = (i//cols)*(cell+14)
    sheet.paste(im,(x,y+14))
    dr.text((x+2,y), f"{f.stem[:22]} {w}x{h}", fill=(255,255,0))
sheet.save(out)
