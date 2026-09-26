"""Export each team's logo from the game's own data into mods/teams/<ABBR>/logos/scorebug.png.

Uses 2K5 Mod Studio (installed on this machine) for the logo locations
(nfl2k5_scorebug_resources.TEAM_LOGOS: 256x256 'teamlogo' textures in pack
vc_53450030/0) and its texture decoder (tools/nfl_txtr.py: chunks, VC-LZ,
unswizzle, palette). Run with Mod Studio's Python:

  "%LOCALAPPDATA%\\Programs\\2K5-Mod-Studio\\runtime\\python.exe" tools\\export_team_logos.py
"""
import hashlib
import os
import sys
from pathlib import Path

STUDIO = Path(os.environ.get("LOCALAPPDATA", "")) / "Programs" / "2K5-Mod-Studio" / "app"
sys.path.insert(0, str(STUDIO))
sys.path.insert(0, str(STUDIO / "tools"))

import nfl_txtr as tx  # noqa: E402
from mod_editor.core.nfl2k5_scorebug_resources import TEAM_LOGOS  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "original" / "disc" / "vc_53450030" / "0"


def main() -> int:
    pack = PACK.read_bytes()
    done = 0
    for abbr, r in sorted(TEAM_LOGOS.items()):
        span = pack[r["pack_offset"]:r["pack_offset"] + r["span_size"]]
        if hashlib.sha256(span).hexdigest() != r["span_sha256"]:
            print(f"{abbr}: span hash differs, skipped")
            continue
        chunks = tx.parse_chunks(span, allow_trailing=True)
        chunk = chunks[r["chunk"]]
        output, _ = tx.decode_chunk(span, chunk)
        tex = tx.parse_texture(output, chunk)
        rgba = tx.texture_to_rgba(output, chunk, tex)
        out = ROOT / "mods" / "teams" / abbr / "logos" / "scorebug.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        tx.write_png(out, tex.width, tex.height, rgba)
        done += 1
        print(f"{abbr}: {r['name']} {tex.width}x{tex.height} -> {out.relative_to(ROOT)}")
    print(f"{done} logos")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
