"""The M15 draft-screens contact sheet: logs/eras/draft-screens-sheet.png, the retail Franchise
screen beside each Eras copy (docs/eras/results/M15-draft-screens.md).

    python -I tools/eras/drafts/make_draft_screens_sheet.py

Left column: the Eras screens from the test runs' frames (logs/draftshow/m15*-*.png). Right
column: the retail reference frame (logs/franchise-ref/*.png, M13's regression frames) of the
screen each one copies. Missing frames leave a grey panel with the label. Nothing here is
committed: the frames hold retail pixels and stay under logs\\.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
D = os.path.join(ROOT, 'logs', 'draftshow')
R = os.path.join(ROOT, 'logs', 'franchise-ref')
OUT = os.path.join(ROOT, 'logs', 'eras', 'draft-screens-sheet.png')
# (Eras frame, label, retail frame, label) per row
ROWS = [
    ('m15a-board1.png', 'Eras: The Big Board (Historical league), card + NAME POS STOCK SCHOOL OVR',
     os.path.join(R, '57-big-board.png'), 'Retail Franchise: The Big Board'),
    ('m15a-board-qb.png', 'Eras: The Big Board, Quarterbacks page (LT / RT)',
     os.path.join(R, '54-rookie-report.png'), 'Retail: Rookie Report (the position pages the copy uses)'),
    ('m15b-cands.png', 'Eras: NFL Draft candidates at the user\'s pick, the pick clock (Seahawks, Alt. History)',
     os.path.join(R, '59-draft-candidates.png'), 'Retail Franchise: Draft room candidates'),
    ('m15b-confirm.png', 'Eras: A on a prospect, retail\'s own Yes / No prompt',
     os.path.join(D, 'm13e-frpick2.png'), 'Retail: the Draft room pick (M13 regression frame)'),
    ('m15b-show.png', 'Eras: the show announces the user\'s pick',
     os.path.join(D, 'm13b-usershow.png'), 'M13: the same show'),
    ('m15b-order.png', 'Eras: Draft Order (ROUND PICK NAME POS TEAM) with the user\'s pick',
     os.path.join(R, '58-the-draft-room.png'), 'Retail Franchise: the Draft room\'s order board'),
    ('m15a-complete.png', 'Eras: Draft Order after a complete Historical draft (the recap)',
     'm13a-recap1.png', 'M13: the sheet-engine recap this replaces'),
    ('m15b-expired.png', 'Eras: the clock ran out, the CPU picked for the user',
     'm13b-cand.png', 'M13: the sheet-engine candidates this replaces'),
    ('m15e-bigboard.png', 'Regression: retail Franchise The Big Board after M15',
     'm13e-bigboard2.png', 'M13: the same screen before M15'),
    ('m15e-draftroom.png', 'Regression: retail Franchise Draft room candidates after M15',
     'm13e-draftroom2.png', 'M13: the same screen before M15'),
]
W, H = 640, 360


def main():
    try:
        font = ImageFont.truetype('arial.ttf', 15)
    except Exception:
        font = ImageFont.load_default()
    sheet = Image.new('RGB', (2 * W, len(ROWS) * (H + 24)), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for i, row in enumerate(ROWS):
        for c in range(2):
            name, label = row[2 * c], row[2 * c + 1]
            p = name if os.path.isabs(name) else os.path.join(D, name)
            x, y = c * W, i * (H + 24)
            if os.path.exists(p):
                sheet.paste(Image.open(p).convert('RGB').resize((W, H)), (x, y + 24))
            else:
                d.rectangle([x, y + 24, x + W, y + 24 + H], fill=(60, 60, 60))
                label += ' (missing)'
            d.rectangle([x, y, x + W, y + 24], fill=(0, 0, 0))
            d.text((x + 6, y + 4), '%d%s. %s' % (i + 1, 'ab'[c], label), fill=(255, 255, 255), font=font)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
