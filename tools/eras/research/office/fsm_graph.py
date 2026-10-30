"""Crawl NFL 2K5 front-end screens (FSM state descriptors) from the retail XBE (read-only).

    python build/eras/office/fsm_graph.py [root_hex ...] [--depth N] [--json out.json]

Defaults to the Franchise roots (Franchise Options 0x500DC8, Coach's Desk 0x522190).
For every descriptor reached it prints title, handler, layout, flags, help bar, event
table (with action kinds) and rows, then follows every reference to another descriptor:
row targets (kinds 0/1), event push/replace/insert targets (kinds 8/9/10), and
descriptor addresses used as immediates in the code of row actions, row update
callbacks and event callbacks (and, one call level down, the functions those call).

A descriptor is recognised by its frame words: +0x20 == 0x02400044 (every screen).
Function bodies are taken from the start address to the next generated function
start (build/gen-locals), capped at 0x1800 bytes. Writes nothing unless --json.
"""
import bisect, glob, json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'native-menu'))
from xbe import Xbe  # noqa: E402
import capstone  # noqa: E402

x = Xbe()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
KIND = {1: 'call(ecx=fsm)', 2: 'call(ecx=fsm,edx=arg)', 3: 'call()', 4: 'call(ecx=[+1C])',
        5: 'call(dt)', 6: 'call()', 7: 'POP', 8: 'INSERT-UNDER', 9: 'REPLACE-TOP', 10: 'PUSH', 11: 'POP-TO-ROOT'}
FNF = {1: 4, 2: 8, 3: 0xC, 4: 0x10, 5: 0x14, 6: 0x18}
HANDLERS = {0xF3E90: 'link list', 0xF3FC0: 'settings list', 0xF3DA0: 'custom', 0xF40F0: 'table'}
ROWT = {0: 'link', 1: 'replace-link', 2: 'kind2', 4: 'kind4', 5: 'off/on', 6: 'kind6', 7: 'value list', 8: 'kind8', 9: 'action', 3: 'end'}


def all_descriptors():
    out = set()
    for name, va, vsz, raw, rsz in x.secs:
        for off in range(0, rsz - 0x34, 4):
            if struct.unpack_from('<I', x.d, raw + off + 0x20)[0] == 0x02400044:
                out.add(va + off)
    return out


DESCS = all_descriptors()
_starts = None


def fn_starts():
    global _starts
    if _starts is None:
        s = set()
        for p in glob.glob(os.path.join(ROOT, 'build', 'gen-locals', 'recomp_*.c')):
            with open(p, encoding='utf-8', errors='replace') as f:
                for m in re.finditer(r'^void sub_([0-9A-F]{8})\(void\)', f.read(), re.M):
                    s.add(int(m.group(1), 16))
        _starts = sorted(s)
    return _starts


def body(fn):
    st = fn_starts()
    i = bisect.bisect_right(st, fn)
    end = st[i] if i < len(st) else fn + 0x1800
    end = min(end, fn + 0x1800)
    o = x.off(fn)
    if o is None:
        return []
    return list(md.disasm(x.d[o:o + max(end - fn, 16)], fn))


def scan_fn(fn, depth=1, seen=None):
    """-> (descriptor refs [(desc, via, how)], calls, strings)"""
    seen = seen if seen is not None else set()
    if fn in seen or not fn or x.off(fn) is None:
        return []
    seen.add(fn)
    refs = []
    for ins in body(fn):
        for tok in re.findall(r'0x[0-9a-f]+', ins.op_str):
            v = int(tok, 16)
            if v in DESCS:
                how = 'pending [0xAA2408]' if '0xaa2408' in ins.op_str else ins.mnemonic
                refs.append((v, fn, how))
        if ins.mnemonic == 'call' and depth > 0 and ins.op_str.startswith('0x'):
            c = int(ins.op_str, 16)
            if c not in (0x6E390, 0x6E400, 0x6E2E0, 0x6E340, 0x6E480):
                refs += [(d, v, h + ' (in callee)') for d, v, h in scan_fn(c, depth - 1, seen)]
    return refs


def strings_in(fn):
    out = []
    for ins in body(fn):
        for tok in re.findall(r'0x[0-9a-f]+', ins.op_str):
            v = int(tok, 16)
            if 0xE60000 <= v < 0xEC0000:
                w = x.wstr(v, 48)
                if w and len(w) >= 3:
                    out.append(w)
    return out


def wstr(v):
    return x.wstr(v, 64) if v and x.off(v) is not None else None


def decode(desc):
    w = [x.u32(desc + 4 * i) for i in range(13)]
    d = {'addr': desc, 'title': wstr(w[0]), 'events': w[1], 'handler': w[2], 'help': w[3], 'rows': w[4],
         'table': w[5], 'layout': wstr(w[6]), 'layout_ptr': w[6], 'binding': w[7], 'flags': w[10],
         'w2c': w[11], 'w30': w[12], 'event_list': [], 'help_list': [], 'row_list': [], 'refs': []}
    if w[1] and x.off(w[1]) is not None:
        for i in range(40):
            m, a = x.u32(w[1] + 8 * i), x.u32(w[1] + 8 * i + 4)
            if not m:
                break
            acts = []
            for j in range(16):
                r = a + 0x24 * j
                k = x.u32(r)
                if not k:
                    break
                if k in FNF:
                    fn = x.u32(r + FNF[k])
                    acts.append({'kind': k, 'fn': fn})
                    d['refs'] += [(t, v, 'msg %d: %s' % (m, h)) for t, v, h in scan_fn(fn)]
                elif k in (8, 9, 10):
                    t = x.u32(r + 0x20)
                    acts.append({'kind': k, 'target': t})
                    d['refs'].append((t, None, 'msg %d: %s' % (m, KIND[k])))
                else:
                    acts.append({'kind': k})
            d['event_list'].append({'msg': m, 'list': a, 'actions': acts})
    if w[3] and x.off(w[3]) is not None:
        for i in range(20):
            b = x.u32(w[3] + 12 * i)
            if not b:
                break
            d['help_list'].append((b, wstr(x.u32(w[3] + 12 * i + 4)), wstr(x.u32(w[3] + 12 * i + 8))))
    if w[4] and x.off(w[4]) is not None and w[2] in (0xF3E90, 0xF3FC0):
        for i in range(33):
            r = w[4] + 0x34 * i
            rw = [x.u32(r + 4 * k) for k in range(13)]
            row = {'addr': r, 'kind': rw[0], 'label': wstr(rw[1]), 'target': rw[2], 'action': rw[10],
                   'update': rw[11], 'value_fns': rw[3:10] if rw[0] in (5, 7) else None}
            d['row_list'].append(row)
            if rw[0] == 3:
                break
            if rw[0] in (0, 1) and rw[2]:
                d['refs'].append((rw[2], None, 'row %d %r' % (i, row['label'])))
            if rw[0] == 9 and rw[10]:
                d['refs'] += [(t, v, 'row %d %r action: %s' % (i, row['label'], h)) for t, v, h in scan_fn(rw[10])]
            if rw[11]:
                d['refs'] += [(t, v, 'row %d update: %s' % (i, h)) for t, v, h in scan_fn(rw[11], 0)]
    return d


def show(d):
    h = d['handler']
    print('=' * 100)
    print('%08X  %r   handler %08X (%s)  layout %r  flags 0x%X  binding %08X  table %08X  +2C %08X +30 %08X' % (
        d['addr'], d['title'], h, HANDLERS.get(h, 'own'), d['layout'], d['flags'], d['binding'], d['table'], d['w2c'], d['w30']))
    for e in d['event_list']:
        s = []
        for a in e['actions']:
            if 'fn' in a:
                s.append('%s sub_%08X' % (KIND[a['kind']], a['fn']))
            elif 'target' in a:
                s.append('%s %08X %r' % (KIND[a['kind']], a['target'], wstr(x.u32(a['target']))))
            else:
                s.append(KIND.get(a['kind'], '?'))
        print('   msg %2d [%08X]: %s' % (e['msg'], e['list'], '; '.join(s)))
    for b in d['help_list']:
        print('   help id %d: %r %r' % b)
    for i, r in enumerate(d['row_list']):
        if r['kind'] == 3:
            print('   row %2d end' % i)
            break
        extra = ''
        if r['kind'] in (0, 1):
            extra = '-> %08X %r' % (r['target'], wstr(x.u32(r['target'])) if r['target'] else None)
        elif r['kind'] == 9:
            extra = 'action sub_%08X' % r['action']
        elif r['value_fns']:
            extra = 'fns ' + ' '.join('%X' % f for f in r['value_fns'])
        if r['update']:
            extra += '  update sub_%08X' % r['update']
        print('   row %2d %-12s %-34r %s' % (i, ROWT.get(r['kind'], r['kind']), r['label'], extra))
    seen = set()
    for t, via, how in d['refs']:
        k = (t, how)
        if k in seen:
            continue
        seen.add(k)
        print('   -> %08X %-32r via %s %s' % (t, wstr(x.u32(t)), ('sub_%08X' % via) if via else '', how))


def crawl(roots, depth):
    out = {}
    q = [(r, 0) for r in roots]
    while q:
        a, dep = q.pop(0)
        if a in out or a not in DESCS:
            continue
        d = decode(a)
        out[a] = d
        show(d)
        if dep < depth:
            for t, _, _ in d['refs']:
                if t not in out:
                    q.append((t, dep + 1))
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    depth = 3
    js = None
    if '--depth' in args:
        i = args.index('--depth'); depth = int(args[i + 1]); del args[i:i + 2]
    if '--json' in args:
        i = args.index('--json'); js = args[i + 1]; del args[i:i + 2]
    roots = [int(a, 16) for a in args] or [0x500DC8, 0x522190]
    res = crawl(roots, depth)
    print('\n%d descriptors decoded (of %d in the XBE)' % (len(res), len(DESCS)))
    if js:
        with open(js, 'w') as f:
            json.dump({'%08X' % k: {kk: vv for kk, vv in v.items()} for k, v in res.items()}, f, indent=1, default=str)
