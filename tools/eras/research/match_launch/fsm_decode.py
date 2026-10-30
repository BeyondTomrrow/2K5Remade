"""Decode NFL 2K5 front-end state descriptors from the retail XBE (read-only).

    python build/eras/match-launch/fsm_decode.py 0x52728C 0x55D088 0x585474 0x4E7EC0

A state descriptor is {title, message list, screen handler, ...}. The message
list is {message id, action-record pointer} pairs ending in id 0; an action
record chain is 0x24-byte records ending in kind 0. Kinds, from the dispatcher
sub_0006E4E0 (jump table 0x6E5F8):

  1 call [+4](ecx=fsm)          2 call [+8](ecx=fsm, edx=msg arg)
  3 call [+0C]()                4 call [+10](ecx=[+1C])
  5 call [+14](float fsm+0x104) 6 call [+18]()
  7 pop (sub_0006E400)          8 sub_0006E340(fsm, [+20])
  9 sub_0006E2E0(fsm, [+20])    10 push [+20] (sub_0006E390)
  11 sub_0006E480(fsm)

Uses tools/xbe_peek.py's reader (original/disc/default.xbe). Writes nothing.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
from xbe_peek import Xbe  # noqa: E402

KIND = {0: "nop", 1: "call [+4](ecx=fsm)", 2: "call [+8](ecx=fsm,edx=arg)", 3: "call [+0C]()",
        4: "call [+10](ecx=[+1C])", 5: "call [+14](float)", 6: "call [+18]()", 7: "POP",
        8: "sub_0006E340(fsm,[+20])", 9: "REPLACE-TOP [+20] (sub_0006E2E0)", 10: "PUSH [+20]",
        11: "sub_0006E480(fsm)"}
FIELD = {1: 4, 2: 8, 3: 0xC, 4: 0x10, 5: 0x14, 6: 0x18, 8: 0x20, 9: 0x20, 10: 0x20}


def wstr(x, va, n=48):
    s = ""
    for i in range(n):
        try:
            c = x.u32(va + 2 * i) & 0xFFFF
        except ValueError:
            return "?"
        if c == 0:
            break
        s += chr(c) if c < 128 else "?"
    return s


def decode(x, desc):
    title = x.u32(desc)
    name = wstr(x, title) if 0xE60000 < title < 0xEC0000 else ""
    print("desc %08X %r = {%08X, %08X, %08X}" % (desc, name, title, x.u32(desc + 4), x.u32(desc + 8)))
    lst = x.u32(desc + 4)
    for i in range(40):
        msg, rec = x.u32(lst + 8 * i), x.u32(lst + 8 * i + 4)
        if msg == 0:
            break
        acts = []
        for j in range(8):
            r = rec + 0x24 * j
            kind = x.u32(r)
            if kind == 0:
                break
            f = FIELD.get(kind)
            acts.append("%s -> %08X" % (KIND.get(kind, "kind%d" % kind), x.u32(r + f) if f else 0))
        print("  msg %2d: %s" % (msg, "; ".join(acts)))


if __name__ == "__main__":
    xbe = Xbe()
    for a in sys.argv[1:]:
        decode(xbe, int(a, 0))
