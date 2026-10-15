#!/usr/bin/env bash
# Launch the native Linux build. The Windows build links with a 16 MB
# main-thread stack (/STACK:16777216) and the recompiled game runs on the
# main thread; Linux defaults to 8 MB, so raise the soft limit first.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exe="${NFL2K5_EXE:-$here/build-linux-game/NFL2K5}"
ulimit -s 16384 2>/dev/null || ulimit -s unlimited 2>/dev/null || \
    echo "warning: could not raise the stack limit (ulimit -s is $(ulimit -s))" >&2
cd "$here"
exec "$exe" "$@"
