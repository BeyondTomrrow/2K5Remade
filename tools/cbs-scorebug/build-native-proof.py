"""Build an explicitly labelled review video from composed native captures.

No game control or fabricated events. Separate from the broadcast package.
"""
import json
import pathlib
import re
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

review = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
def read_review(folder):
    events, shots = [], []
    for line in (folder / 'stderr.log').read_text(errors='replace').splitlines():
        if '[HTMLPRES] event ' in line:
            try:
                events.append(json.loads(line.split('[HTMLPRES] event ', 1)[1]))
            except ValueError:
                pass
        m = re.search(r'\[PSHOT\] (.+) t=(\d+)', line)
        if m:
            shots.append((int(m[2]) / 1000, pathlib.Path(m[1])))
    return events, shots
events, shots = read_review(review)
if len(sys.argv) > 3:
    newer, more_shots = read_review(pathlib.Path(sys.argv[3]).resolve())
    # Prefer the final run's current receiver layout. Other earlier native
    # evidence remains usable; nothing is resynthesized from event text.
    events = newer + events
    shots = sorted(shots + more_shots)
font = lambda size: ImageFont.truetype('C:/Windows/Fonts/arial.ttf', size)
specs = [
    ('WIDE RECEIVER', 'PLAYER_STAT', lambda e: e.get('player', {}).get('position') == 'WR'),
    ('RUNNING BACK', 'PLAYER_STAT', lambda e: e.get('player', {}).get('position') == 'HB'),
    ('DEFENSIVE PLAYER', 'PLAYER_STAT', lambda e: e.get('player', {}).get('position') == 'DT'),
    ('TEAM DEFENSE', 'TEAM_STAT', lambda e: e.get('category') == 'defense'),
    ('TEAM TIME OF POSSESSION', 'TEAM_STAT', lambda e: e.get('category') == 'possession'),
    ('NATIVE DRIVE SUMMARY', 'DRIVE_SUMMARY', lambda e: e.get('drive', {}).get('timeOfPossession', 0) >= 100),
]
manifest = []
for index, (title, name, predicate) in enumerate(specs):
    event = next((e for e in events if e['name'] == name and predicate(e)), None)
    if not event:
        raise RuntimeError('No actual native evidence for ' + title)
    shot = next((s for s in shots if s[0] >= event['time'] + 2), None)
    if not shot or shot[0] - event['time'] > 4:
        raise RuntimeError('No timely composed capture for ' + title)
    image = Image.new('RGB', (1280, 720), '#111a23')
    draw = ImageDraw.Draw(image)
    draw.text((50, 40), 'ACTUAL NATIVE D3D11 OUTPUT / LIVE GAME DATA', font=font(23), fill='#91bcdb')
    draw.text((50, 88), title, font=font(36), fill='white')
    with Image.open(shot[1]) as capture:
        image.paste(capture.convert('RGB').resize((640, 480), Image.Resampling.NEAREST), (50, 166))
        capture.save(out / ('native-' + str(index + 1) + '.png'))
    lines = []
    if name == 'PLAYER_STAT':
        p = event['player']
        lines = [p['fullName'], p['position'] + ' / ' + event['team'].upper(), '']
        if p['position'] == 'WR':
            lines += [str(p['receptions']) + ' RECEPTIONS', str(p['receivingYards']) + ' RECEIVING YARDS']
        elif p['position'] == 'HB':
            lines += [str(p['carries']) + ' RUSH', str(p['rushingYards']) + ' RUSH YARDS']
        else:
            lines += [str(p['tackles']) + ' TACKLES', str(p['sacks']) + ' SACKS', str(p['defensiveInterceptions']) + ' INT']
    elif name == 'TEAM_STAT':
        fields = ['sacks', 'tackles', 'defensiveInterceptions'] if event['category'] == 'defense' else ['firstDowns', 'timeOfPossession']
        for side in ['away', 'home']:
            lines.append(side.upper())
            for key in fields:
                value = event['teams'][side].get(key)
                if key == 'timeOfPossession' and value is not None:
                    value = str(int(value) // 60) + ':' + str(int(value) % 60).zfill(2)
                lines.append(key + ': ' + str(value))
            lines.append('')
    else:
        d = event['drive']; seconds = d['timeOfPossession']
        lines = [str(d['plays']) + ' PLAYS', str(d['yards']) + ' YARDS',
                 str(seconds // 60) + ':' + str(seconds % 60).zfill(2) + ' POSSESSION TIME', '',
                 'Native summary fields only.', 'No invented rush/pass split.']
    for row, text in enumerate(lines):
        draw.text((735, 170 + row * 36), text, font=font(24), fill='white')
    draw.text((50, 674), 'Recorded 640x480 test window; original capture 320x240, enlarged here. Still evidence, not a motion capture.', font=font(18), fill='#abc0d0')
    frame = out / ('proof-' + str(index + 1) + '.png')
    image.save(frame)
    manifest.append("file '" + frame.as_posix() + "'\nduration 5\n")
    (out / ('proof-' + str(index + 1) + '.json')).write_text(json.dumps({'event': event, 'capture': str(shot[1])}, indent=2))
listing = out / 'concat.txt'
listing.write_text(''.join(manifest) + "file '" + frame.as_posix() + "'\n")
subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(listing),
    '-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '18', '-movflags', '+faststart', str(out.parent / 'CBS-2026-native-stat-proof.mp4')], check=True)
