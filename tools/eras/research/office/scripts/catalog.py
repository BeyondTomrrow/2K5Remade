ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, json
sys.path.insert(0, __import__("os").path.join(ROOT, r".tmp-2k-football-mod-tools"))
from pathlib import Path
from mod_editor.core import nfl2k5_models as M
SP = Path(sys.argv[1])
src = M.ModelSource(Path(__import__("os").path.join(ROOT, r"original\disc\vc_53450030\0")), SP/"inventory.json")
entries = src.catalog()
rows = [dict(key=e.key, outer=e.outer_index, chunk=e.chunk_index, name=e.name, group=e.group, stored=e.stored_size, decoded=e.decoded_size) for e in entries]
(SP/"scne_catalog.json").write_text(json.dumps(rows, indent=1))
print(len(rows))
