#!/usr/bin/env bash
# SOFTDRINK 2K28 (v0.6 / v0.6.1): drive a music_test.ps1 -Pack run from the title to the Crib
# (profile VIP1 (3), 4th in the list) and on to the Jukebox. SOFTDRINK's START on the title opens
# Play Now's Team Select, so: START x3 (title, Settings / VIP prompts), A, B back to the Main Menu.
# Its Main Menu falls back to the title after ~20 s idle: keep the presses going.
#   bash tools/music/nav_jukebox_sd.sh <tag> [menu|crib|jukebox]
tag=${1:-sd1}; stop=${2:-jukebox}
root="$(cd "$(dirname "$0")/../.." && pwd)"
L="$root/logs/music/$tag.stderr.log"
press() { powershell -NoProfile -ExecutionPolicy Bypass -File "$root/tools/music/music_test.ps1" -Do press -Tag "$tag" -Steps "$1" -Name "${2:-nav}" >/dev/null; }
until grep -a -q "library ready" "$L" 2>/dev/null; do sleep 3; done
sleep 45
for s in start:6 start:6 start:6 a:6 b:5; do press $s; done
press down:2; press down:2 menu
[ "$stop" = menu ] && exit 0
press a:6; press down:2; press down:2; press down:2; press a:20 crib
[ "$stop" = crib ] && exit 0
for s in right:5 up:7 left:5 up:7 up:6 right:5 up:6 up:6 left:5; do press $s; done
press a:8 jukebox
