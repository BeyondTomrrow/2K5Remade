# Benchmarks

Produced by tools\bench.ps1. GPU ms is the span between a frame's first and last GPU timestamp (it includes GPU idle time when the frame is CPU-bound). CPU ms is all of the process's threads per presented frame. 60 FPS is the ceiling: the game simulates at most 60 frames a second and the clock is never sped up.

### 2026-10-09 03:15 -- base (build\Release\NFL2K5.exe)

GPU: NVIDIA GeForce RTX 2060 SUPER; CPU: Intel(R) Xeon(R) W-2235 CPU @ 3.80GHz. CPU-vs-CPU quick game, 120 s of play per scale, first 30 s skipped.

| Scale | Avg FPS | 1% low | 0.1% low | Frame ms avg / worst | GPU ms/frame | CPU ms/frame (process) | Working set / private / VRAM MB |
|---|---|---|---|---|---|---|---|

### 2026-10-09 03:15 -- baseline before perf work (old GPU timing; gpu_ms not reliable in this run)

GPU: NVIDIA GeForce RTX 2060 SUPER. CPU-vs-CPU quick game; the last 9 10-second windows of play per scale.

| Scale | Avg FPS | 1% low (worst window) | 0.1% low (worst window) | Frame ms avg / worst | GPU ms/frame (util) | CPU ms/frame (process) | Working set / private / VRAM MB |
|---|---|---|---|---|---|---|---|
| 2x (1440x960) | 59.7 | 43.8 | 20.5 | 16.74 / 98.6 | n/a | 32.60 | 636 / 971 / 454 |
| 4x (2880x1920) | 59.4 | 36.2 | 18.1 | 16.84 / 89.4 | n/a | 33.06 | 619 / 991 / 456 |
| 8x (5760x3840) | 59.9 | 51.7 | 23.9 | 16.69 / 65.4 | n/a | 32.19 | 632 / 1667 / 946 |
| 12x (8640x5760) | 57.5 | 33.0 | 20.8 | 17.44 / 82.5 | n/a | 37.12 | 607 / 2784 / 1815 |
