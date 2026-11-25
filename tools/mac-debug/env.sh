# Shared settings for the mac-debug scripts. Source it, or override first:
#   NFL2K5_DEBUG_DIR  work directory for dumps, press file and toggles
#   NFL2K5_EXE        game binary
NFL2K5_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
: "${NFL2K5_DEBUG_DIR:=${TMPDIR:-/tmp}/nfl2k5-debug}"
: "${NFL2K5_EXE:=$NFL2K5_ROOT/build-mac-game/NFL2K5}"
TOOLS="$NFL2K5_ROOT/tools/mac-debug"
mkdir -p "$NFL2K5_DEBUG_DIR/tog"
sleep_s() { perl -e "select(undef,undef,undef,$1)"; }
