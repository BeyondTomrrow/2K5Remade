"""The M13 draft contact sheet: logs/eras/draft-sheet.png from the test runs' frames.

    python -I tools/eras/drafts/make_draft_sheet.py

Panels (left to right, top to bottom): the Big Board (Historical league, Saints on the clock), the
candidates screen at the user's pick (Alternate History, Seahawks), the armed pick, the show
announcing the user's pick, the recap with the user's pick, the show announcing a real pick
(Historical round 1, Freeman McNeil), the recap of the Historical round 1, round 3 of the recap.
Each panel is labelled. Missing frames leave a grey panel with the label.
"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
D = os.path.join(ROOT, 'logs', 'draftshow')
OUT = os.path.join(ROOT, 'logs', 'eras', 'draft-sheet.png')
PANELS = [
    ('m13a-board1.png', 'The Big Board (Historical league): Saints on the clock, R1 #1'),
    ('m13b-cand.png', 'Candidates at the user\'s pick (Alternate History, Seahawks R1 #12)'),
    ('m13b-armed.png', 'A once: the pick armed (A again drafts him)'),
    ('m13b-usershow.png', 'The show announces the user\'s pick: Lawrence Taylor, Seahawks'),
    ('m13b-recap_user.png', 'Draft Order / Recap: the user\'s pick at 12, Chiefs on the clock'),
    ('m13a-show2.png', 'The show announces a real pick (Historical): #3 Freeman McNeil, Jets'),
    ('m13a-recap1.png', 'Recap, Historical round 1: the real 1981 order'),
    ('m13c-complete.png', 'Alternate History draft complete (recap; rosters written)'),
    ('m13e-bigboard2.png', 'Regression: retail Franchise The Big Board still works'),
    ('m13e-draftroom2.png', 'Regression: retail Franchise Draft room candidates, clock running'),
]
W, H, COLS = 640, 360, 2


def main():
    try:
        font = ImageFont.truetype('arial.ttf', 15)
    except Exception:
        font = ImageFont.load_default()
    rows = (len(PANELS) + COLS - 1) // COLS
    sheet = Image.new('RGB', (COLS * W, rows * (H + 24)), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for i, (name, label) in enumerate(PANELS):
        x, y = (i % COLS) * W, (i // COLS) * (H + 24)
        p = os.path.join(D, name)
        if os.path.exists(p):
            im = Image.open(p).convert('RGB').resize((W, H))
            sheet.paste(im, (x, y + 24))
        else:
            d.rectangle([x, y + 24, x + W, y + 24 + H], fill=(60, 60, 60))
            label += ' (missing)'
        d.rectangle([x, y, x + W, y + 24], fill=(0, 0, 0))
        d.text((x + 6, y + 4), '%d. %s' % (i + 1, label), fill=(255, 255, 255), font=font)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    sheet.save(OUT)
    print('wrote', OUT)


if __name__ == '__main__':
    main()
