"""Output dropouts of a background audio run, inside and outside loading screens.

    python -P tools/audio/dropout_report.py logs/audio2/<tag> [<tag2> ...]

Reads <tag>.stderr.log ([PITCH] lines, RECOMP_APU_PITCH_LOG=1: one per second with the speaker queue,
underruns, dropped buffers, VP frames and ISR waits) and <tag>-frametime.csv (NFL2K5_FRAMETIME_CSV).
A second counts as loading when a frame longer than 250 ms is presented within 2 s of it (the
loading screens hold one frame for seconds; play never does), or when no frame is presented at all.
docs/AUDIO-FIX-20261021.md (dropouts), docs/AUDIO-REPORT-HOTKEY-20261022.md.
"""
import bisect
import csv
import statistics
import sys


def load(tag):
    ft = list(csv.DictReader(open(tag + '-frametime.csv')))
    ts = [float(r['t_ms']) for r in ft]
    ms = [float(r['frame_ms']) for r in ft]
    long_at = [t for t, m in zip(ts, ms) if m > 250]
    rows = []
    sil_prev = None
    log = open(tag + '.stderr.log', errors='replace').read()
    buf = 256 if 'x 256-sample buffers' in log else 1024     # speaker buffer size of this build
    for line in open(tag + '.stderr.log', errors='replace'):
        if not line.startswith('[PITCH] qpc'):
            continue
        d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
        q = float(d['qpc'])
        i, j = bisect.bisect_left(ts, q - 1000), bisect.bisect_left(ts, q)
        frames = ms[i:j]
        k = bisect.bisect_left(long_at, q - 3000)
        near_long = k < len(long_at) and long_at[k] <= q + 2000
        qmin, qmax = (int(x) for x in d['queue'].split('..'))
        sil = int(d.get('silentvp', 0))       # cumulative VP frames emitted silent after a 40 ms ISR wait
        dsil = 0 if sil_prev is None else sil - sil_prev
        sil_prev = sil
        rows.append(dict(q=q, under=int(d['underruns']), drops=int(d['dropped']), vp=int(d['vpframes']),
                         qmin=qmin, qmax=qmax, fps=len(frames), worst=max(frames) if frames else 0.0,
                         loading=near_long or not frames, stall_ms=float(d['stall_ms']),
                         silent_ms=dsil * 32 / 48.0, buf_ms=buf / 48.0))
    return rows


def main():
    for tag in sys.argv[1:]:
        rows = load(tag)
        if not rows:
            print(tag, 'no [PITCH] lines')
            continue
        play = [r for r in rows if not r['loading']]
        lo = [r for r in rows if r['loading']]
        u_play = sum(r['under'] for r in play)
        u_load = sum(r['under'] for r in lo)
        fps = [r['fps'] for r in play]
        print(f'{tag}: {len(rows)} s ({len(play)} s play, {len(lo)} s loading)')
        print(f'  underruns: {u_play} outside loading ({sum(1 for r in play if r["under"])} s), {u_load} in loading; '
              f'dropped {sum(r["drops"] for r in rows)}')
        print(f'  per 10 min of play: {u_play * 600 / max(1, len(play)):.1f}')
        print(f'  game audio emitted silent after a 40 ms ISR wait: {sum(r["silent_ms"] for r in play):.0f} ms outside loading '
              f'({sum(1 for r in play if r["silent_ms"])} s), {sum(r["silent_ms"] for r in lo):.0f} ms in loading')
        bm = play[0]['buf_ms']
        print(f'  speaker queue outside loading: {min(r["qmin"] for r in play) * bm:.0f}..{max(r["qmax"] for r in play) * bm:.0f} ms '
              f'(median of per-second minimum {statistics.median(r["qmin"] for r in play) * bm:.0f} ms)')
        print(f'  play fps: median {statistics.median(fps):.0f}, seconds under 50 fps {sum(1 for f in fps if f < 50)}, '
              f'under 30 fps {sum(1 for f in fps if f < 30)}; VP frames median {statistics.median(r["vp"] for r in play):.0f}')
        bad = [r for r in play if r['under']]
        for r in bad[:12]:
            print(f'    t={(r["q"] - rows[0]["q"]) / 1000:7.1f} s underruns {r["under"]} queue {r["qmin"]}..{r["qmax"]} '
                  f'fps {r["fps"]} worst {r["worst"]:.0f} ms vp {r["vp"]} isr {r["stall_ms"]:.0f} ms')


if __name__ == '__main__':
    main()
