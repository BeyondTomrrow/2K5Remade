import sys
from xbe import Xbe
x = Xbe()
TYPES = {1: 'call +4(ecx=fsm)', 2: 'call +8(ecx=fsm,edx=?)', 3: 'call +0xC()', 4: 'call +0x10(ecx=+0x1C)',
         5: 'call +0x14(float dt)', 6: 'call +0x18()', 8: 'insert-below +0x20', 9: 'replace-top +0x20', 11: 'pop-to-root'}
def evt(t):
    print('event table %08X' % t)
    i = 0
    while True:
        m = x.u32(t + 8 * i); a = x.u32(t + 8 * i + 4)
        if not m: break
        print('  msg %d -> actions %08X' % (m, a))
        j = 0
        while True:
            w = [x.u32(a + 0x24 * j + 4 * k) for k in range(9)]
            if not w[0]: break
            print('     type %d %-22s %s' % (w[0], TYPES.get(w[0], '?'), ' '.join('%08X' % v for v in w[1:])))
            j += 1
        i += 1
def help(t):
    print('help bar %08X' % t)
    i = 0
    while True:
        b = x.u32(t + 12 * i)
        if not b: break
        print('   button %d: %r %r' % (b, x.wstr(x.u32(t + 12 * i + 4)), x.wstr(x.u32(t + 12 * i + 8))))
        i += 1
for a in sys.argv[1:]:
    kind, v = a.split(':')
    (evt if kind == 'e' else help)(int(v, 16))
