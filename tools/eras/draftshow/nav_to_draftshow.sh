#!/usr/bin/env bash
# Drive a background draftshow_test.ps1 run (launched with NFL2K5_ERAS_DRAFTSHOW_TEST=1, which puts
# "Draft Show (test)" first in the Main Menu) from the title to the draft show through the press file:
# START until the Main Menu (0x515660) is pushed (the title's attract demo eats the first presses),
# then A on the first row.
#   bash tools/eras/draftshow/nav_to_draftshow.sh <tag>
tag=${1:-ds1}
cd "$(dirname "$0")/../../../logs/draftshow" || exit 1
L=$tag.stderr.log; P=$tag-press.txt
until grep -a -q "desc=004E7EC0" "$L"; do sleep 1; done       # the title
for i in $(seq 1 20); do
  n=$(grep -a -c "FSMOP\] push" "$L")
  echo start > "$P"; sleep 8
  if grep -a "FSMOP\] push" "$L" | tail -n +$(( n + 1 )) | grep -q "desc=00515660"; then echo "main menu after $i presses"; break; fi
  # START on the title opens Quick Game's Team Select: back out with B
  if grep -a "FSMOP\] push" "$L" | tail -n +$(( n + 1 )) | grep -q "desc=0052728C"; then echo b > "$P"; sleep 5; fi
done
sleep 2
echo a > "$P"; sleep 8
grep -a -E "ERAS\] (menu push|draftshow)" "$L" | tail -8
