from pathlib import Path
import struct
from capstone import *
p=Path('original/default.xbe').read_bytes(); base=struct.unpack_from('<I',p,0x104)[0]; n=struct.unpack_from('<I',p,0x11c)[0]; sh=struct.unpack_from('<I',p,0x120)[0]
def code_at(addr):
 for i in range(n):
  _,va,vsz,raw,_=struct.unpack_from('<IIIII',p,sh-base+i*56)
  if va<=addr<va+vsz: return p[raw+addr-va:raw+addr-va+256]
md=Cs(CS_ARCH_X86,CS_MODE_32)
for start in (0x44df0,0x44bb0,0x44da0,0x44dc0):
 print('\n',hex(start))
 for x in md.disasm(code_at(start),start):
  print(f'{x.address:08X}: {x.mnemonic:7} {x.op_str}')
  if x.mnemonic.startswith('ret') or x.mnemonic=='jmp': break
