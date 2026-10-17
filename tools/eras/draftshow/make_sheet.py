"""Labelled contact sheet of draft-show frames.

    python -I tools/eras/draftshow/make_sheet.py out.png "frame.png|label" ["frame.png|label" ...]

Tiles are 640x360 (16:9 frames are scaled to fit), three per row, with the label above each.
"""
import sys
from PIL import Image, ImageDraw, ImageFont


def main():
    out, items = sys.argv[1], sys.argv[2:]
    W, H, L, cols = 640, 360, 26, 3
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGB', (W * cols, (H + L) * rows + 34), (18, 20, 26))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('segoeuib.ttf', 18)
        big = ImageFont.truetype('segoeuib.ttf', 20)
    except OSError:
        font = big = ImageFont.load_default()
    d.text((8, 6), 'My NFL Eras draft show - in-game frames (NFL2K5.exe --retail, background mode)', fill=(255, 220, 90), font=big)
    for i, it in enumerate(items):
        path, _, label = it.partition('|')
        im = Image.open(path).convert('RGB')
        im.thumbnail((W, H))
        x, y = (i % cols) * W, 34 + (i // cols) * (H + L)
        d.text((x + 6, y + 3), '%d. %s' % (i + 1, label), fill=(255, 255, 255), font=font)
        sheet.paste(im, (x + (W - im.width) // 2, y + L))
    sheet.save(out)
    print(out)


if __name__ == '__main__':
    main()
