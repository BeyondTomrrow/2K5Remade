#!/bin/bash
# Hands-off oracle recording: launch xemu (tools/audio/xemu_launch.ps1), ping the QEMU monitor every
# 15 s and relaunch on a hang (xemu deadlocked during boot in 3 of 5 background launches on
# 2026-10-22), wait for the title's attract "Demo Mode" (no input needed), then record REC seconds of
# the process loopback (session volume 0.001, gain 1000) and the VP voice table once a second.
# xemu_oracle_run.sh <tag> [demo_wait_s=330] [rec_s=240]
cd /e/NFL2K5-PC
TAG=$1; WAIT=${2:-330}; REC=${3:-240}; OUT=logs/audio3
ping() { python tools/audio/xemu_mon.py "info status" 2>/dev/null | grep -aq "VM status: running"; }
for attempt in 1 2 3 4 5 6; do
  echo "$(date +%T) launch attempt $attempt"
  powershell -NoProfile -File tools/audio/xemu_launch.ps1 | tail -1
  t0=$(date +%s); ok=1; sleep 45   # boot grace: the monitor is not up in the first seconds
  while [ $(( $(date +%s) - t0 )) -lt $WAIT ]; do
    sleep 15
    if ! ping; then echo "$(date +%T) hang/no monitor at +$(( $(date +%s) - t0 )) s"; ok=0; break; fi
  done
  [ $ok = 1 ] && break
done
[ $ok = 1 ] || { echo "gave up"; exit 1; }
PID=$(tr -d '\r\n ' < $OUT/xemu.pid)
python tools/audio/xemu_fb.py E:/NFL2K5-PC/$OUT/$TAG-xemu-start.png
echo "$(date +%T) recording $REC s, pid $PID"
./tools/audio/proc_loopback/proc_loopback.exe $PID E:/NFL2K5-PC/$OUT/$TAG-xemu.wav $REC 1000 > $OUT/$TAG-xemu-rec.out 2>&1 &
RP=$!
rm -f $OUT/$TAG-xemu-vp.bin
python tools/audio/xemu_vp_dump.py E:/NFL2K5-PC/$OUT/$TAG-xemu-vp.bin $REC > $OUT/$TAG-xemu-vp.out 2>&1 &
VP=$!
for k in 1 2 3 4; do sleep $(( REC / 4 )); python tools/audio/xemu_fb.py E:/NFL2K5-PC/$OUT/$TAG-xemu-$k.png; done
wait $RP $VP
cat $OUT/$TAG-xemu-rec.out $OUT/$TAG-xemu-vp.out
echo "$(date +%T) done"
