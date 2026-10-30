#!/usr/bin/env bash
# Drive a background draftshow_test.ps1 run (launched with NFL2K5_ERAS_HUB_TEST=1, which puts
# "My NFL Eras (test)" first in the Main Menu, where the cursor starts) from the title to the office's
# "1981 Draft" row (M13): START until the Main Menu is pushed (the title's attract demo eats the first
# presses), A on the first row (the hub), A on Continue League (row 0), then down x7 (one press at a
# time; the office opens on row 0) and A. Reports the [ERAS] lines of the draft. The draft screen is
# then driven by the caller through the press file: start (advance / sim), y (skip to my pick), a (arm /
# make a pick), lt / rt (position or round tabs), lb / rb (Big Board <-> Draft Order), b (back).
#   bash tools/eras/drafts/nav_draft.sh <tag>
tag=${1:-m13a}
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
sleep 3
n=$(pushes); echo a > "$P"; sleep 6
got=$(newpush $n); echo "main menu A: $got"
case "$got" in *$hub*) ;; *) echo "not the hub; stopping"; exit 1;; esac
n=$(pushes); echo a > "$P"; sleep 10; echo "hub A (Continue): $(newpush $n)"
for k in 1 2 3 4 5 6 7; do echo down > "$P"; sleep 1; done
sleep 1
n=$(pushes); echo a > "$P"; sleep 8
echo "office Draft row: $(newpush $n)"
grep -a -E "ERAS\] (menu draft row|draft screen opened|draft [0-9]{4} (opened|order)|draft: )" "$L" | tail -6
