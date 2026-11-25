#!/usr/bin/env bash
# snap.sh <name> : dump the next presented GPU frame (read back from the GPU)
# to $NFL2K5_DEBUG_DIR/<name>.png. Needs a game started with run-live.sh.
source "$(dirname "$0")/env.sh"
D=$NFL2K5_DEBUG_DIR
rm -f "$D/live.bmp"; touch "$D/live.bmp.req"
for i in $(seq 1 60); do [ -f "$D/live.bmp" ] && break; sleep_s 0.25; done
sleep_s 0.3
python3 -I "$TOOLS/bmp2png.py" "$D/live.bmp" "$D/$1.png" && echo "$D/$1.png"
