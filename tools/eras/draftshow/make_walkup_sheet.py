"""M12 walk-up check: a 10 fps strip of one pick's walk-up, handshake and lift -> logs/draftshow/walkup-sheet.png

    python -I tools/eras/draftshow/make_walkup_sheet.py <pick video.mp4> <start s> <end s> [out.png]

Frames at 10 fps, 8 a row, each stamped with its time in the video (the show's clock when the
recording started at the show's start, NFL2K5_ERAS_DRAFTSHOW_REC).
"""
import os, subprocess, sys, glob, shutil
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))


def main():
    video, start, end = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
    out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(ROOT, 'logs', 'draftshow', 'walkup-sheet.png')
    tmp = os.path.join(ROOT, 'logs', 'draftshow', 'polish', 'walkup-frames')
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', str(start), '-t', str(end - start), '-i', video, '-vf', 'fps=10',
                    os.path.join(tmp, 'f-%03d.png')], check=True)
    files = sorted(glob.glob(os.path.join(tmp, 'f-*.png')))
    cols, W, H = 8, 320, 180
    try:
        font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 14)
    except OSError:
        font = ImageFont.load_default()
    rows = (len(files) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * W, rows * (H + 16) + 30), (18, 18, 22))
    d = ImageDraw.Draw(sheet)
    d.text((8, 6), 'Draft show walk-up at 10 fps: %s, %.1f-%.1f s (walk clip at the clip\'s own speed, turn, handshake from one frame, lift)'
           % (os.path.basename(video), start, end), fill=(255, 220, 120), font=font)
    for i, f in enumerate(files):
        im = Image.open(f).convert('RGB').resize((W, H))
        x, y = (i % cols) * W, 30 + (i // cols) * (H + 16)
        sheet.paste(im, (x, y + 16))
        d.text((x + 4, y + 1), '%.1f s' % (start + i * 0.1), fill=(255, 255, 0), font=font)
    sheet.save(out)
    print('wrote', out, sheet.size, len(files), 'frames')


if __name__ == '__main__':
    main()
