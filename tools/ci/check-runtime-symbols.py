"""Check that every xbox_* function the game declares `extern` is defined
somewhere in the xboxrecomp submodule.

This catches the failure we hit when the fork lived in a hand-exported patch:
the game called xbox_user_path() and friends, but the code that defines them
was never published, so a fresh clone could not link. A full link needs the
retail game's generated code, which CI cannot have, so this is the cheap
stand-in.

Usage: python tools/ci/check-runtime-symbols.py [game_src] [runtime_src]
"""
import pathlib
import re
import sys

game = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "src")
runtime = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "external/xboxrecomp/src")
exts = {".c", ".cpp", ".h", ".inc"}

def sources(root):
    for p in root.rglob("*"):
        if p.is_file() and (p.suffix in exts or p.name.endswith(".inc.c")):
            yield p, p.read_text(encoding="utf-8", errors="replace")

# Names the game expects the runtime to provide.
extern_re = re.compile(r'\bextern\b(?:\s+"C")?[^;{}()]*?\b(xbox_\w+)\s*\(')
needed = {}
for path, text in sources(game):
    for name in extern_re.findall(text):
        needed.setdefault(name, path)

if not runtime.is_dir():
    sys.exit(f"runtime source not found at {runtime} (submodule not checked out?)")

# A definition: the name followed by a parameter list and then a body,
# not a semicolon (which would only be a declaration).
runtime_text = "\n".join(text for _, text in sources(runtime))
missing = []
for name, where in sorted(needed.items()):
    if not re.search(r'\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{', runtime_text):
        missing.append((name, where))

print(f"{len(needed)} runtime functions referenced by the game")
for name, where in missing:
    print(f"MISSING: {name} (first used in {where.as_posix()})")
sys.exit(1 if missing else 0)
