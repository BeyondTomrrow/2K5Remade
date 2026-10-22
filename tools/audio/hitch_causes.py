"""What the long frames of a run were waiting on ([HITCH] lines, nv2a_gpu_perf.inc.c perf_hitch).

    python -P tools/audio/hitch_causes.py logs/audio2/<tag>.stderr.log [...]

Each [HITCH] line splits one long frame: texture uploads (and texture creation), pixel shaders that
became ready (compiled), file reads, the render thread's own time, and the game thread's wait for the
guest lock. The biggest share names the frame's cause. The log keeps the first 200 hitches only.
"""
import re
import sys
import collections

for path in sys.argv[1:]:
    causes = collections.Counter()
    ms_by = collections.Counter()
    n = 0
    for line in open(path, errors='replace'):
        m = re.search(r'\[HITCH\] ([\d.]+) ms: present ([\d.]+) ms, reads \d+ \(\d+ KB, ([\d.]+) ms\), '
                      r'tex uploads \d+ \(([\d.]+) ms, create ([\d.]+)\), new targets \d+, ps ready (\d+), draws \d+; '
                      r'render thread ([\d.]+) ms, queue wait ([\d.]+) ms, capture ([\d.]+) ms, '
                      r'game thread lock wait (-?[\d.]+) ms', line)
        if not m:
            continue
        ft, present, reads, tex, create, ps, render, qwait, cap, lock = (float(x) for x in m.groups())
        n += 1
        parts = {'texture uploads': tex, 'file reads': reads, 'game thread waited for the guest lock': max(lock, 0),
                 'render thread (draws)': max(render - tex, 0), 'present': present, 'video capture': cap}
        if ps > 0:
            parts['new pixel shaders'] = render     # a compile shows up as render-thread time
        if lock < 0 or lock > 5 * ft:
            parts['game thread waited for the guest lock'] = 0
        top = max(parts, key=parts.get)
        if parts[top] < ft * 0.4:
            top = 'game thread (guest code)'
        causes[top] += 1
        ms_by[top] += ft
    print(f'{path}: {n} hitches')
    for k, v in causes.most_common():
        print(f'  {v:4d}  {ms_by[k]:8.0f} ms  {k}')
