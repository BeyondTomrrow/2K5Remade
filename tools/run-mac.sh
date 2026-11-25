#!/usr/bin/env bash
# Launch the native macOS build (see docs/MACOS.md). The game thread gets its
# 16 MB stack from main.c; the main thread runs the window.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exe="${NFL2K5_EXE:-$here/build-mac-game/NFL2K5}"
cd "$here"
exec "$exe" "$@"
