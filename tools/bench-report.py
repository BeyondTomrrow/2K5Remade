"""Summarise [BENCH] lines from benchmark runs into a markdown table.

    python tools/bench-report.py <tag> <scale> [<scale> ...] [--skip N] [--out docs/BENCHMARKS.md] [--title T]

Reads logs/<tag>-<scale>.stderr.log for each scale (tools/bench.ps1 writes
them). Each [BENCH] line covers 10 s (external/xboxrecomp nv2a_gpu_perf.inc.c).
The first --skip lines of play are dropped (menus/loading/coin toss; play
starts about 140 s after launch, so the default skips by time: only lines
after the last 'Start Game' period are kept -- the last N lines, see --keep).
"""
import argparse, os, re, statistics, subprocess, datetime

def parse(path):
    rows = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"\[BENCH\] (.*)", line.replace("\0", ""))
        if not m:
            continue
        p = m.group(1).split()
        d = {}
        for i in range(0, len(p) - 1, 2):
            try:
                d[p[i]] = float(p[i + 1])
            except ValueError:
                pass
        rows.append(d)
    return rows

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("scales", nargs="+", type=int)
    ap.add_argument("--keep", type=int, default=9, help="last N 10-second windows (play)")
    ap.add_argument("--out", default="")
    ap.add_argument("--title", default="")
    a = ap.parse_args()
    lines = ["| Scale | Avg FPS | 1% low (worst window) | 0.1% low (worst window) | Frame ms avg / worst | GPU ms/frame (util) | CPU ms/frame (process) | Working set / private / VRAM MB |",
             "|---|---|---|---|---|---|---|---|"]
    for s in a.scales:
        path = os.path.join("logs", "%s-%d.stderr.log" % (a.tag, s))
        if not os.path.exists(path):
            lines.append("| %dx | (no log) | | | | | | |" % s)
            continue
        r = parse(path)[-a.keep:]
        if not r:
            lines.append("| %dx | (no data) | | | | | | |" % s)
            continue
        avg = lambda k: statistics.mean(x[k] for x in r if k in x) if any(k in x for x in r) else float("nan")
        mn = lambda k: min((x[k] for x in r if k in x), default=float("nan"))
        mx = lambda k: max((x[k] for x in r if k in x), default=float("nan"))
        gpu = avg("gpu_ms")
        gpu_s = "%.2f (%.0f%%)" % (gpu, avg("gpu_util")) if "gpu_util" in r[-1] and gpu >= 0 else "n/a"
        lines.append("| %dx (%dx%d) | %.1f | %.1f | %.1f | %.2f / %.1f | %s | %.2f | %d / %d / %d |" % (
            s, 720 * s, 480 * s, avg("fps"), mn("low1"), mn("low01"), avg("ft_avg"), mx("ft_max"),
            gpu_s, avg("cpu_ms"), mx("ws_mb"), mx("priv_mb"), mx("vram_mb")))
    table = "\n".join(lines)
    try:
        gpu = subprocess.run(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"], capture_output=True, text=True).stdout.strip()
    except OSError:
        gpu = "?"
    head = "\n### %s -- %s\n\nGPU: %s. CPU-vs-CPU quick game; the last %d 10-second windows of play per scale.\n\n" % (
        datetime.datetime.now().strftime("%Y-%m-%d %H:%M"), a.title or a.tag, gpu, a.keep)
    print(head + table)
    if a.out:
        new = not os.path.exists(a.out)
        with open(a.out, "a", encoding="utf-8") as f:
            if new:
                f.write("# Benchmarks\n\nProduced by tools\\bench.ps1 / tools\\bench-report.py from the renderer's [BENCH] lines "
                        "(external/xboxrecomp/src/kernel/nv2a_gpu_perf.inc.c). 60 FPS is the ceiling: the game simulates at most "
                        "60 frames a second and its clock is never sped up. GPU ms = the process's 3D-engine utilisation x frame "
                        "time (Windows GPU Engine counter). CPU ms = all threads of the process per presented frame. "
                        "1%/0.1% lows are the FPS of the 99th/99.9th percentile frame time within each 10 s window; the table "
                        "shows the worst window.\n")
            f.write(head + table + "\n")

if __name__ == "__main__":
    main()
