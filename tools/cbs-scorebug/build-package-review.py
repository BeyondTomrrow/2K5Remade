"""Reproducible chaptered MP4 from existing rendered CBS review components.

Uses generated graphics and labelled native still evidence, never broadcast
reference footage. Does not build, launch or control NFL2K5.exe.
"""
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[2]
base = root / 'outputs/cbs-2026'
out = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else base / 'CBS-2026-package-followup-20261018.mp4'
parts = [
    ('Motion and replay wipes', base / 'hd-followup-20261018/CBS-2026-motion-hd-20261018.mp4'),
    ('Player, team, TOP and drive graphics', base / 'hd-followup-20261018/CBS-2026-stats-extended-hd-20261018.mp4'),
    ('Large player comparison', base / 'CBS-2026-comparison-review-cut.mp4'),
    ('Actual period-ending state: sample demonstration', base / 'hd-followup-20261018/CBS-2026-period-state-hd-20261018.mp4'),
    ('Earlier native D3D11 live-stat evidence', base / 'CBS-2026-native-stat-proof.mp4'),
]
if len(sys.argv) > 2:
    parts.append(('Latest native D3D11 period verification', pathlib.Path(sys.argv[2]).resolve()))
out.parent.mkdir(parents=True, exist_ok=True)
chapters, listing = [], []
elapsed = 0
for title, path in parts:
    duration = float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries',
        'format=duration', '-of', 'default=noprint_wrappers=1:nokey=1', str(path)]))
    chapters.append({'title': title, 'start': elapsed, 'duration': duration, 'file': str(path)})
    listing.append("file '" + path.as_posix().replace("'", "'\\''") + "'")
    elapsed += duration
concat = out.with_suffix('.concat.txt')
concat.write_text('\n'.join(listing) + '\n', encoding='utf-8')
metadata = out.with_suffix('.ffmetadata')
meta = [';FFMETADATA1', 'title=NFL on CBS package review - October 18 2026',
    'comment=Sample graphics followed by labelled actual native still evidence; not a complete broadcast identity claim.']
for chapter in chapters:
    meta.extend(['[CHAPTER]', 'TIMEBASE=1/1000', 'START=' + str(round(chapter['start'] * 1000)),
        'END=' + str(round((chapter['start'] + chapter['duration']) * 1000)), 'title=' + chapter['title']])
metadata.write_text('\n'.join(meta) + '\n', encoding='utf-8')
out.with_suffix('.chapters.json').write_text(json.dumps(chapters, indent=2), encoding='utf-8')
subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0',
    '-i', str(concat), '-i', str(metadata), '-map', '0:v:0', '-map_metadata', '1', '-map_chapters', '1',
    '-vf', 'fps=30,format=yuv420p', '-c:v', 'libx264', '-preset', 'fast', '-crf', '18',
    '-movflags', '+faststart', str(out)], check=True)
print(out, f'({elapsed:.1f} seconds; {len(chapters)} chapters)')
