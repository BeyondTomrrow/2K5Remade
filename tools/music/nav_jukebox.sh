#!/usr/bin/env bash
# Drive a music_test.ps1 run from the title to the Crib Jukebox (VIP1 (3), the first profile) via the
# press file, one press per call (presses sent back to back are sometimes dropped).
#   bash tools/music/nav_jukebox.sh <tag> [menu|crib|jukebox]   (stop after that stage)
tag=${1:-m1}; stop=${2:-jukebox}
root="$(cd "$(dirname "$0")/../.." && pwd)"
L="$root/logs/music/$tag.stderr.log"; P="$root/logs/music/$tag-press.txt"
press() { powershell -NoProfile -ExecutionPolicy Bypass -File "$root/tools/music/music_test.ps1" -Do press -Tag "$tag" -Steps "$1" -Name "${2:-nav}" >/dev/null; }
until grep -a -q "desc=004E7EC0" "$L"; do sleep 2; done                      # the title
for i in $(seq 1 20); do                                                     # START until the Main Menu
  n=$(grep -a -c "FSMOP\] push" "$L")
  echo start > "$P"; sleep 8
  if grep -a "FSMOP\] push" "$L" | tail -n +$(( n + 1 )) | grep -q "desc=00515660"; then echo "main menu after $i"; break; fi
  if grep -a "FSMOP\] push" "$L" | tail -n +$(( n + 1 )) | grep -q "desc=0052728C"; then echo b > "$P"; sleep 6; fi
done
[ "$stop" = menu ] && exit 0
press down:3; press down:3; press a:10; press a:20                           # THE CRIB, VIP1 (3)
[ "$stop" = crib ] && exit 0
for s in right:5 up:7 left:5 up:7 up:6 right:5 up:6 up:6 left:5; do press $s; done   # hall -> bar -> lounge -> fireplace
press a:8 jukebox                                                            # "A Jukebox"
echo "jukebox: logs/music/$tag-jukebox.png"
