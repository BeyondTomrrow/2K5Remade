"""Print generated functions from build/gen-locals compactly (read-only).

    python build/eras/office/fnl.py sub_00142880 142910 ...
"""
import glob, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
GEN = os.path.join(ROOT, 'build', 'gen-locals')
NOISE = re.compile(r'^\s*(\(void\)|int _flags|uint32_t _fa = 0|int32_t _fas = 0|#define fp_|g_fp_top|g_fp_stack|RECOMP_INTERP_GATE)')
_cache = None


def index():
    global _cache
    if _cache is None:
        _cache = {}
        for p in sorted(glob.glob(os.path.join(GEN, 'recomp_*.c'))):
            with open(p, encoding='utf-8', errors='replace') as f:
                lines = f.read().split('\n')
            for i, l in enumerate(lines):
                m = re.match(r'^void (sub_[0-9A-F]{8})\(void\)\s*$', l.rstrip('\r'))
                if m:
                    _cache[m.group(1)] = (p, lines, i)
    return _cache


def body(name):
    ent = index().get(name)
    if not ent:
        return None
    p, lines, i = ent
    out = []
    for j in range(i, len(lines)):
        l = lines[j].rstrip('\r')
        if NOISE.match(l) or not l.strip():
            continue
        if j > i and l.startswith('void sub_'):
            break
        out.append(l)
        if l.rstrip() == '}':
            break
    return os.path.basename(p), out


def norm(a):
    a = a.strip()
    if a.lower().startswith('sub_'):
        a = a[4:]
    if a.lower().startswith('0x'):
        a = a[2:]
    return 'sub_%08X' % int(a, 16)


if __name__ == '__main__':
    for n in sys.argv[1:]:
        n = norm(n)
        r = body(n)
        if not r:
            print('// %s not found' % n)
            continue
        print('// ---- %s (%s, %d lines)' % (n, r[0], len(r[1])))
        print('\n'.join(r[1]))
