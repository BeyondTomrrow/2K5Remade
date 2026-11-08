"""Contact sheet: midfield 0x4c+slot and endzone 0x445+slot for chosen slots."""
from pathlib import Path
import sys, io
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from ncaa_formats import sections
from ncaa_textures import parse_mmap, decode_texture
from PIL import Image, ImageDraw
root = Path(r'H:\NCAA07_Extractor_Work\field_raw')
slots = [int(s) for s in sys.argv[1].split(',')]
sheet = Image.new('RGB', (420, 170 * len(slots)), (100, 100, 100)); draw = ImageDraw.Draw(sheet)
for k, slot in enumerate(slots):
    for j, base in enumerate((0x4c, 0x445)):
        i = base + slot; p = root / f'{i:08x}.tmd'
        try:
            b = p.read_bytes(); r, t = sections(b); s = t['Text']; raw = b[s['offset']:s['offset'] + s['size']]
            img = Image.open(io.BytesIO(decode_texture(raw, parse_mmap(raw), 0))); img.thumbnail((190, 140))
            sheet.paste(img, (j * 210, k * 170 + 22), img if img.mode == 'RGBA' else None)
            draw.text((j * 210, k * 170 + 4), f'{slot} {i:04x} ' + r.string(t['Info']['offset']), fill='white')
        except Exception as e:
            draw.text((j * 210, k * 170 + 4), f'{slot} {i:04x} ERR {e}'[:40], fill='red')
sheet.save(sys.argv[2])
