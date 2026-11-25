#!/usr/bin/env bash
# Drive a game started with run-live.sh from boot into a Quick Game and keep
# kicking/snapping until an offensive play-call screen is on, then open the
# play cards (A) and snapshot them as playcall.png. Boot timing varies; check
# the snapshots if it lands somewhere else.
source "$(dirname "$0")/env.sh"
T=$TOOLS; D=$NFL2K5_DEBUG_DIR
sleep_s 10
for b in start start start start; do "$T/press.sh" 3 $b; done
"$T/press.sh" 3 a; "$T/press.sh" 3 a; "$T/press.sh" 55 a      # quick game, coach, start
"$T/press.sh" 1 down; "$T/press.sh" 6 a                       # coin toss choice
for i in $(seq 1 200); do
    "$T/snap.sh" scan >/dev/null
    if python3 -I "$T/is_playcall.py" "$D/scan.png"; then
        "$T/press.sh" 2 a; "$T/snap.sh" playcall; exit 0
    fi
    "$T/press.sh" 0.5 a                                       # kick meters, skip cut-ins
done
echo "no play-call screen found" >&2; exit 1
