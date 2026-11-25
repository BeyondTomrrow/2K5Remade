#!/usr/bin/env bash
# press.sh <seconds-to-wait-after> <button>... : queue buttons into the running
# game (start back a b x y up down left right lb rb lt rt l3 r3 lsl lsr lsu lsd).
source "$(dirname "$0")/env.sh"
wait=$1; shift
echo "$*" > "$NFL2K5_DEBUG_DIR/live.press"
sleep_s "$wait"
