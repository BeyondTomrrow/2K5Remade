"""M12 polish sheet: before / after per item -> logs/draftshow/polish-sheet.png

    python -I tools/eras/draftshow/make_polish_sheet.py

Before: frames of logs/draftshow/pick1.mp4 (M11). After: frames of the M12 runs (logs/draftshow/
m12a-frames, m12b-frames) and pick1-v2.mp4. Edit ITEMS below when the frames move.
"""
import os, subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
L = os.path.join(ROOT, 'logs', 'draftshow')
W, H = 640, 360


def frame_of(video, t):
    out = os.path.join(L, 'polish', 'tmp_%s_%.1f.png' % (os.path.basename(video).split('.')[0], t))
    if not os.path.exists(out):
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(t), '-i', video, '-frames:v', '1', out], check=True)
    return Image.open(out).convert('RGB').resize((W, H))


def bmp(run, n):
    return Image.open(os.path.join(L, '%s-frames' % run, 'p-%03d.bmp' % n)).convert('RGB').resize((W, H))


def png(name):
    return Image.open(os.path.join(L, name)).convert('RGB').resize((W, H))


# (title, before image, after image, caption)
ITEMS = [
    ('1  Music never dies mid-pick',
     png('polish/before-music.png'), png('polish/after-music.png'),
     'before: retail menu track ends / restarts, 1.9-4.3 s gaps inside picks (ds7)   after: looping bed, 192 s of show, no gap >= 0.4 s under -50 dBFS (m12a)'),
    ('2  The player is not the commissioner',
     frame_of(os.path.join(L, 'pick1.mp4'), 27.0), bmp('m12a', 134),
     'before: commissioner\'s head on the player   after: coach head (dhd04) on the suited body, hands tinted to its skin (Lawrence Taylor, pick 2)'),
    ('3  Suits and ties',
     frame_of(os.path.join(L, 'pick1.mp4'), 24.0), png('polish/after-suits.png'),
     'before: one charcoal suit, red tie for every pick   after: charcoal / navy / black, never twice in a row; tie in the team colour'),
    ('4  Chest hold: wrists',
     frame_of(os.path.join(L, 'pick1.mp4'), 27.5), bmp('m12a', 106),
     'before: hands above the jersey top   after: hands 4 cm lower, jersey 3.5 cm higher: wrists behind the jersey (push-in)'),
    ('5  Captions and the card',
     png('polish/before-cardin.png'), png('polish/after-cardin.png'),
     'the "...SELECT" captions cut at the team clip\'s last sample, the card slides in next frame (10 fps strip, 13.4-13.7 s)'),
    ('6  Draft Show in the office Options',
     png('ds8-office-row.png'), png('m12b-opt3.png'),
     'before: env switch only   after: Office Options row Off / First Round / Your Picks, league.ini draft_show, env switch overrides'),
    ('7  Missing clips summary',
     png('polish/before-summary.png'), png('polish/after-summary.png'),
     'one line at the round\'s end: "audio summary: 1 pick(s) had no clip to record: 5 cardinals (team)"'),
]


def main():
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 22)
        small = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 16)
    except OSError:
        font = small = ImageFont.load_default()
    rowh = H + 70
    sheet = Image.new('RGB', (2 * W + 60, len(ITEMS) * rowh + 60), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    d.text((20, 14), 'My NFL Eras draft show: M12 polish, before (left) / after (right)', fill=(255, 220, 120), font=font)
    for i, (title, before, after, cap) in enumerate(ITEMS):
        y = 60 + i * rowh
        d.text((20, y), title, fill=(255, 255, 255), font=font)
        sheet.paste(before, (20, y + 32))
        sheet.paste(after, (40 + W, y + 32))
        d.text((20, y + 36 + H), cap, fill=(200, 200, 200), font=small)
    out = os.path.join(L, 'polish-sheet.png')
    sheet.save(out)
    print('wrote', out, sheet.size)


if __name__ == '__main__':
    main()
