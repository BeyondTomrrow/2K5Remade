"""Contact sheet of field archive entries: field_range.py start count out.png"""
from pathlib import Path
import sys, io
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ncaa_formats import sections
from ncaa_textures import parse_mmap, decode_texture
from PIL import Image, ImageDraw
root = Path(r'H:\NCAA07_Extractor_Work\field_raw')
start, count = int(sys.argv[1], 0), int(sys.argv[2], 0)
cols = 6; sheet = Image.new('RGB', (cols * 170, ((count + cols - 1) // cols) * 170), (100, 100, 100)); draw = ImageDraw.Draw(sheet)
for k in range(count):
    i = start + k; x, y = (k % cols) * 170, (k // cols) * 170
    try:
        b = (root / f'{i:08x}.tmd').read_bytes(); r, t = sections(b); s = t['Text']; raw = b[s['offset']:s['offset'] + s['size']]
        img = Image.open(io.BytesIO(decode_texture(raw, parse_mmap(raw), 0))); w, h = img.size; img.thumbnail((160, 140))
        sheet.paste(img, (x, y + 22), img if img.mode == 'RGBA' else None)
        draw.text((x, y + 4), f'{i:04x} {w}x{h} ' + r.string(t['Info']['offset'])[:14], fill='white')
    except Exception as e:
        draw.text((x, y + 4), f'{i:04x} ERR', fill='red')
sheet.save(sys.argv[3])
