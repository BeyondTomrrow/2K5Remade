#!/usr/bin/env bash
# The real path (no test row): title -> Main Menu -> My NFL Eras -> Continue League -> the office ->
# "Draft Show (preview)" (office row 8), through the press file of a background draftshow_test.ps1 run.
#   bash tools/eras/draftshow/nav_office_row.sh <tag>
tag=${1:-ds8}
cd "$(dirname "$0")/../../../logs/draftshow" || exit 1
L=$tag.stderr.log; P=$tag-press.txt
pushes() { grep -a -c "FSMOP\] push" "$L"; }
newpush() { grep -a "FSMOP\] push" "$L" | tail -n +$(( $1 + 1 )) | grep -o "desc=[0-9A-F]*" | tr '\n' ' '; }
until grep -a -q "desc=004E7EC0" "$L"; do sleep 1; done
for i in $(seq 1 20); do
  n=$(pushes); echo start > "$P"; sleep 8
  case "$(newpush $n)" in *00515660*) echo "main menu after $i presses"; break;; *0052728C*) echo b > "$P"; sleep 5;; esac
done
hub=$(grep -a -o "menu: hub [0-9A-F]*" "$L" | awk '{print $3}')
office=$(grep -a -o "office [0-9A-F]* your team" "$L" | awk '{print $2}')
for try in 1 2 3; do
  n=$(pushes); echo "down down down" > "$P"; sleep 3; echo a > "$P"; sleep 5
  got=$(newpush $n); echo "main menu A: $got"
  case "$got" in *$hub*) break;; *) echo b > "$P"; sleep 4; echo b > "$P"; sleep 4; echo "up up up up up up up up" > "$P"; sleep 4;; esac
done
n=$(pushes); echo a > "$P"; sleep 10; echo "hub A (Continue): $(newpush $n)"
echo "down down down down down down down" > "$P"; sleep 4
echo a > "$P"; sleep 10
grep -a -E "ERAS\] (menu office row|draftshow (create|show))" "$L" | tail -4
