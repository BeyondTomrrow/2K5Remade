"""Losslessly compact only this tool's explicitly named development captures.

Keeps every frame. A BMP is removed only after its PNG is reopened and all
RGB pixels match. No game assets, configurations, mods or saves are touched.
"""
import argparse
from pathlib import Path
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('--prefix', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
logs = (root / 'logs').resolve()
if not a.prefix.startswith('sandbox-') or any(c not in 'abcdefghijklmnopqrstuvwxyz0123456789-' for c in a.prefix):
    raise SystemExit('Expected an explicit sandbox test prefix')
total = 0
for index, path in enumerate(sorted(logs.glob(a.prefix + '-*.bmp'))):
    if path.resolve().parent != logs or path.is_symlink():
        raise SystemExit('Capture escaped the logs directory')
    target = path.with_suffix('.png')
    with Image.open(path) as source:
        rgb = source.convert('RGB')
        if not target.exists():
            temp = target.with_suffix('.png.tmp')
            rgb.save(temp, format='PNG', compress_level=1)
            temp.replace(target)
        with Image.open(target) as verified:
            if verified.size != rgb.size or verified.convert('RGB').tobytes() != rgb.tobytes():
                raise SystemExit('PNG verification failed; original preserved: ' + str(path))
    total += path.stat().st_size - target.stat().st_size
    path.unlink()
    if index % 100 == 0:
        print(f'{index+1} captures preserved; {total/1024**2:.0f} MiB reclaimed', flush=True)
print(f'Finished: {total/1024**3:.2f} GiB reclaimed; all frames preserved as PNG', flush=True)
