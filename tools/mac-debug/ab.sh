#!/usr/bin/env bash
# ab.sh <name> <toggle> : snapshot, switch a renderer path off for a moment
# (toggle = novp | nospec | no3d | nomip | nofloor), snapshot again, and write
# a side-by-side $NFL2K5_DEBUG_DIR/ab_<name>.png.
source "$(dirname "$0")/env.sh"
D=$NFL2K5_DEBUG_DIR
rm -f "$D"/tog/*
"$TOOLS/snap.sh" "$1_gpu" >/dev/null
touch "$D/tog/$2"; sleep_s 2.5
"$TOOLS/snap.sh" "$1_$2" >/dev/null
rm -f "$D/tog/$2"
python3 -I "$TOOLS/sheet.py" "$D/ab_$1.png" "$D/$1_gpu.png" "$D/$1_$2.png" && echo "$D/ab_$1.png"
