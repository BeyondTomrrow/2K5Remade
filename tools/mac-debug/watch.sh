#!/usr/bin/env bash
# watch.sh <x> <y> : log every 3D batch that changes guest pixel (x,y) during
# the next frame -- GPU and software alike -- as [WATCH] lines in live.log.
source "$(dirname "$0")/env.sh"
echo "$1 $2" > "$NFL2K5_DEBUG_DIR/tog/watch"
sleep_s 3
grep -E "one-shot|WATCH" "$NFL2K5_DEBUG_DIR/live.log" | tail -40
