#!/bin/sh
# NFL 2K5 launcher. Runs the game from this folder and keeps a copy of its log
# in logs/session.log (the previous run's in logs/session.prev.log).
here="$(dirname "$(readlink -f "$0")")"
if [ -e "$here/portable.txt" ]; then
    data="$here"
else
    data="${HOME:-$here}/.local/share/NFL2K5-PC"
fi
mkdir -p "$data/logs"
log="$data/logs/session.log"
[ -f "$log" ] && mv -f "$log" "$data/logs/session.prev.log"

if [ -t 2 ]; then
    "$here/NFL2K5" "$@" 2>&1 | tee "$log"
else
    exec "$here/NFL2K5" "$@" >"$log" 2>&1
fi
