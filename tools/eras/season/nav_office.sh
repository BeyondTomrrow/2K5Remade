#!/usr/bin/env bash
# Drive a background season_test.ps1 run (launched with NFL2K5_ERAS_HUB_TEST=1: "My NFL Eras (test)" is the
# Main Menu's first row, where the cursor starts) from the title into the office (M14 tests): START until the
# Main Menu is pushed, A on the hub, A on Continue League. Then, with a row count, down x N and A on that
# office row. Rows are counted among the VISIBLE office rows (hidden rows are skipped by the list).
#   bash tools/eras/season/nav_office.sh <tag> [downs]
tag=${1:-s1}; downs=${2:-}
cd "$(dirname "$0")/../../../logs/season" || exit 1
L=$tag.stderr.log; P=$tag-press.txt
pushes() { grep -a -c "FSMOP\] push" "$L"; }
newpush() { grep -a "FSMOP\] push" "$L" | tail -n +$(( $1 + 1 )) | grep -o "desc=[0-9A-F]*" | tr '\n' ' '; }
until grep -a -q "desc=004E7EC0" "$L"; do sleep 1; done
for i in $(seq 1 20); do
  n=$(pushes); echo start > "$P"; sleep 8
  case "$(newpush $n)" in *00515660*) echo "main menu after $i presses"; break;; *0052728C*) echo b > "$P"; sleep 5;; esac
done
hub=$(grep -a -o "menu: hub [0-9A-F]*" "$L" | awk '{print $3}')
sleep 3
n=$(pushes); echo a > "$P"; sleep 6
got=$(newpush $n); echo "main menu A: $got"
case "$got" in *$hub*) ;; *) echo "not the hub; stopping"; exit 1;; esac
n=$(pushes); echo a > "$P"; sleep 10; echo "hub A (Continue): $(newpush $n)"
grep -a "league loaded\|playoffs:" "$L" | tail -12
if [ -n "$downs" ]; then
  for k in $(seq 1 "$downs"); do echo down > "$P"; sleep 1; done
  sleep 1
  n=$(pushes); echo a > "$P"; sleep 8
  echo "office row (down x$downs) A: $(newpush $n)"
fi
