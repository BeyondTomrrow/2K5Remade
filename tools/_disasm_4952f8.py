from pathlib import Path
import struct
from capstone import *
p=Path('original/default.xbe').read_bytes()
base=struct.unpack_from('<I',p,0x104)[0]; n=struct.unpack_from('<I',p,0x11c)[0]; sh=struct.unpack_from('<I',p,0x120)[0]
for i in range(n):
    flags,va,vsz,raw,rsz=struct.unpack_from('<IIIII',p,sh-base+i*56)
    if va <= 0x4952f8 < va+vsz:
        f=raw+0x4952f8-va; code=p[f:f+512]; break
md=Cs(CS_ARCH_X86,CS_MODE_32)
for x in md.disasm(code,0x4952f8):
 print(f'{x.address:08X}: {x.mnemonic:7} {x.op_str}')
 if x.address >=0x49534b: break
