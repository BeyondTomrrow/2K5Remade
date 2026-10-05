import struct,json,sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
d=open('original/disc/default.xbe','rb').read()
base=struct.unpack_from('<I',d,0x104)[0]; nsec=struct.unpack_from('<I',d,0x11C)[0]; sh=struct.unpack_from('<I',d,0x120)[0]-base
secs=[struct.unpack_from('<5I',d,sh+i*56)[1:5] for i in range(nsec)]
def off(va):
    for v,vs,ra,rs in secs:
        if v<=va<v+rs and va<0x420114: return ra+va-v
md=Cs(CS_ARCH_X86,CS_MODE_32)
W=0x800
def crossing(a):
    lo=max(0x11000,a-W); cross=[]; seen=set()
    for ph in range(16):
        st=lo+ph; o=off(st)
        if o is None: continue
        for i in md.disasm(d[o:o+(a-st)+W],st):
            if i.address in seen: break
            seen.add(i.address)
            if i.mnemonic.startswith('j') and i.op_str.startswith('0x'):
                t=int(i.op_str,16)
                if abs(t-i.address)<W and ((i.address<a<t) or (t<a<=i.address)):
                    cross.append((i.address,t))
    return cross
if __name__=='__main__':
    seeds=json.load(open('analysis/seed_functions.json',encoding='utf-8'))
    res=[]
    for s in seeds:
        a=int(s['start'],16)
        if not (0x11000<=a<0x420114): continue
        c=crossing(a)
        if c: res.append((a,c[:3],s.get('note','')[:50]))
    print(len(res),'of',len(seeds))
    for a,c,n in res: print('%X'%a,' '.join('%X->%X'%x for x in c),'|',n)
