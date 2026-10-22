#!/usr/bin/env bash
# One SOFTDRINK music check (docs/MUSIC-FIX-20261021.md) on a background run already launched with
#   music_test.ps1 -Do launch -Tag <tag> -Pack <pack> -SetEnv RECOMP_AUDIO_WAV_SECONDS=3600
# title -> Main Menu (pause) -> the Crib as VIP1 (3) (pause) -> the Jukebox -> Play -> R3 skip x2
# -> leave the Crib -> Main Menu (pause). Each stage's capture second goes to logs/music/<tag>-notes.txt
# (logs/music/checknotes.py <tag> scores them).
#   bash tools/music/sd_music_check.sh <tag> [menu]
tag=$1; stop=$2; root="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$root"
L=logs/music/$tag.stderr.log; N=logs/music/$tag-notes.txt; : > $N
press() { powershell -NoProfile -ExecutionPolicy Bypass -File tools/music/music_test.ps1 -Do press -Tag "$tag" -Steps "$1" -Name "${2:-nav}" >/dev/null; }
now() { python -c "import os;print(int((os.path.getsize('logs/music/$tag.wav')-44)/192000))"; }
note() { echo "$1 $(now)" >> $N; }
# title / prompt (a box over the title) / menu (blue) / teams (red) / other
screen() {
  powershell -NoProfile -ExecutionPolicy Bypass -File tools/music/music_test.ps1 -Do shot -Tag "$tag" -Name scr >/dev/null
  python -W ignore - "logs/music/$tag-scr.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); w, h = im.size
def avg(b):
    px = list(im.crop((int(w*b[0]), int(h*b[1]), int(w*b[2]), int(h*b[3]))).resize((8, 8)).getdata())
    return tuple(sum(p[i] for p in px) // 64 for i in range(3))
r, g, b = avg((.55, .45, .75, .60)); side = sum(avg((.94, .45, .99, .60))) // 3
if side > 170: print('prompt' if max(r, g, b) < 95 else 'title')
elif b > r + 40: print('menu')
elif r > 110 and g < 90 and b < 90: print('teams')
else: print('other')
PY
}
until grep -a -q "library ready" $L 2>/dev/null; do sleep 3; done
sleep 40
for i in $(seq 1 25); do          # the title, its Settings / VIP / Stadium Music prompts, then the Main Menu
  sc=$(screen); echo "  screen $i: $sc"
  case $sc in menu) break;; prompt) press a:5;; teams|other) press b:6;; *) press start:6;; esac
done
note menu_start; sleep 14; note menu_end
[ "$stop" = menu ] && exit 0
press down:2; press down:2; press a:6; press down:2; press down:2; press down:2; press a:25 crib
note crib_start; sleep 20; note crib_end
for s in right:5 up:7 left:5 up:7 up:6 right:5 up:6 up:6 left:5; do press $s; done
press a:8 jukebox; note jukebox
sleep 10; note play_end          # the playlist is already playing (NOW PLAYING red): no Play press, A there pauses
press r3:15 r3; note r3_end
press r3:15 r3b; note r3b_end
press b:4; press b:4; press start:3; press up:3; press a:4; press down:3; press a:20 out
note menu2_start; sleep 14; note menu2_end
echo done
