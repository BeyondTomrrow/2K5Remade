import struct,zlib,sys
d=open(sys.argv[1],'rb').read()
off=struct.unpack_from('<I',d,10)[0]; w,h=struct.unpack_from('<ii',d,18); bpp=struct.unpack_from('<H',d,28)[0]; B=bpp//8
rs=((w*B)+3)&~3; rows=[]
for y in range(abs(h)):
  sy=abs(h)-1-y if h>0 else y; r=d[off+sy*rs:off+sy*rs+w*B]; px=bytearray()
  for x in range(w): b,g,rr=r[x*B:x*B+3]; px+=bytes((rr,g,b))
  rows.append(b'\0'+bytes(px))
def ch(t,x): return struct.pack('>I',len(x))+t+x+struct.pack('>I',zlib.crc32(t+x)&0xffffffff)
open(sys.argv[2],'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,abs(h),8,2,0,0,0))+ch(b'IDAT',zlib.compress(b''.join(rows)))+ch(b'IEND',b''))
