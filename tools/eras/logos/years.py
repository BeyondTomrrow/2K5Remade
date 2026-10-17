ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import sys, struct, os; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__))); sys.path.insert(0, __import__("os").path.join(ROOT, r'tools\team-creator\vendor'))
from arc import IDX, bop
import nfl_txtr as T
size, block = IDX[0x4A37581D]; size &= 0x0FFFFFFF
banks = [os.path.join(bop.DISC, '%X' % i) for i in range(16)]; sizes=[os.path.getsize(b) for b in banks]
off = block*2048; b=0
while b < 15 and off >= sizes[b]: off -= sizes[b]; b += 1
raw = open(banks[b],'rb').read()[off:off+size]
ch = T.parse_chunks(raw, allow_trailing=True)
print([c.kind for c in ch][:10])
c = [c for c in ch if c.kind=='ROST'][0]
dec,_ = T.decode_chunk(raw, c)
def rel(d,a): return a + struct.unpack_from('<i', d, a)[0] - 1
def u16s(d,a):
    s=b''
    while d[a:a+2] != b'\0\0': s+=d[a:a+2]; a+=2
    return s.decode('utf-16le')
for body in (dec, dec[32:]):
    try:
        cnt = struct.unpack_from('<I', body, 0x58)[0]; table = rel(body, 0x5C)
        print('count', cnt, 'table', hex(table))
        for i in range(min(cnt, 40)):
            at = table + i*0x1F4
            code = u16s(body, rel(body, at+0x10C)); nick = u16s(body, rel(body, at+0x104))
            pairs = [struct.unpack_from('<HH', body, at+0x15A+4*k) for k in range(14)]
            print(code, nick, 'style', body[at+0x192], ' '.join('%d:%d-%d'%(k+1,a,b2) for k,(a,b2) in enumerate(pairs) if a or b2))
        break
    except Exception as e: print('fail', e)
