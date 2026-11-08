#!/usr/bin/env bash
# Base game (--retail) music check: Main Menu (pause) -> the Crib -> the Jukebox -> R3 skip x2 -> back
# out to the Main Menu (pause); stage seconds in logs/music/<tag>-notes.txt (checknotes.py <tag>).
tag=$1; root="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$root"
N=logs/music/$tag-notes.txt; : > $N
press() { powershell -NoProfile -ExecutionPolicy Bypass -File tools/music/music_test.ps1 -Do press -Tag "$tag" -Steps "$1" -Name "${2:-nav}" >/dev/null; }
now() { python -c "import os;print(int((os.path.getsize('logs/music/$tag.wav')-44)/192000))"; }
note() { echo "$1 $(now)" >> $N; }
bash tools/music/nav_jukebox.sh "$tag" menu
note menu_start; sleep 14; note menu_end
press down:3; press down:3; press a:10; press a:25 crib
note crib_start; sleep 20; note crib_end
for s in right:5 up:7 left:5 up:7 up:6 right:5 up:6 up:6 left:5; do press $s; done
press a:8 jukebox; note jukebox; sleep 10; note play_end
press r3:15 r3; note r3_end
press r3:15 r3b; note r3b_end
press b:4; press b:4; press start:3; press up:3; press a:4; press down:3; press a:20 out
note menu2_start; sleep 14; note menu2_end
echo done
