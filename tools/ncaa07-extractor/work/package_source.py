from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

root = Path(__file__).resolve().parents[1]
files = sorted(root.glob('ncaa*.py')) + sorted(root.glob('test_*.py'))
files += [root / name for name in ('README.md', 'Launch.cmd', 'requirements.txt')]
files += sorted((root / 'tools').glob('*'))
files += [p for p in (root / 'work').glob('*') if p.suffix in ('.py', '.md', '.json')]
files += [root / 'outputs' / name for name in ('START_HERE.md', 'VERIFIED_FORMATS.md', 'stadium_texture_audit.md')]
files += [root / 'outputs/fixes/READ_ME_v2.md']
destination = root / 'outputs/fixes/NCAA07_Blender_Source_v2.zip'
with ZipFile(destination, 'w', ZIP_DEFLATED, compresslevel=6) as archive:
    for file in files:
        if file.is_file(): archive.write(file, file.relative_to(root).as_posix())
print('Source package:', destination, destination.stat().st_size)
