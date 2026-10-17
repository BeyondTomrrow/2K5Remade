"""Print generated functions (src/recomp/gen) compactly: python fn.py sub_XXXXXXXX [...]"""
import glob, os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..')
GEN = os.path.join(ROOT, 'src', 'recomp', 'gen')
NOISE = re.compile(r'^\s*(\(void\)|int _flags|uint32_t _fa = 0|int32_t _fas = 0|#define fp_|g_fp_top|g_fp_stack|RECOMP_INTERP_GATE)')
NOISE2 = re.compile(r'\s*_fas = \(int32_t\)\(int32_t\)\(_fa\); _fbs = \(int32_t\)\(int32_t\)\(_fb\);')
NOISE3 = re.compile(r'_fa = \(uint32_t\)\(\w+\) & 0xFFFFFFFFu; _fas = \(int32_t\)\(int32_t\)\(_fa\); /\* \w+ result \*/')
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
        if NOISE.match(l):
            continue
        l = NOISE2.sub('', l)
        l = NOISE3.sub('', l)
        if not l.strip():
            continue
        if j > i and l.startswith('void sub_'):
            break
        out.append(l)
        if l.rstrip() == '}':
            break
    return os.path.basename(p), out


if __name__ == '__main__':
    for n in sys.argv[1:]:
        r = body(n)
        if not r:
            print('// %s not found' % n)
            continue
        print('// ---- %s (%s)' % (n, r[0]))
        print('\n'.join(r[1]))
