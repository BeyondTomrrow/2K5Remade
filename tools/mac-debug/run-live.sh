#!/usr/bin/env bash
# Launch the game for scripted driving: GPU renderer, press file, on-request
# GPU frame dumps and runtime toggles all pointed at $NFL2K5_DEBUG_DIR.
# Extra VAR=value arguments are passed through (e.g. RECOMP_SDLGPU_SCALE=2).
# Stops itself after ${NFL2K5_TIMEOUT:-3000} seconds.
source "$(dirname "$0")/env.sh"
D=$NFL2K5_DEBUG_DIR
rm -f "$D"/live.* "$D"/tog/*
cd "$NFL2K5_ROOT"
exec env RECOMP_SDLGPU=1 RECOMP_SDLGPU_FPS=1 RECOMP_SDLGPU_TOGGLE_DIR="$D/tog" \
    NFL2K5_PRESS_FILE="$D/live.press" RECOMP_SDLGPU_DUMP="$D/live.bmp" \
    RECOMP_SDLGPU_DUMP_AT=999999999 "$@" \
    perl -e "alarm ${NFL2K5_TIMEOUT:-3000}; exec @ARGV" "$NFL2K5_EXE" > "$D/live.log" 2>&1
