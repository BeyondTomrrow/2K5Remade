import struct,zlib,sys
def load(f):
    d=open(f,'rb').read(); w,h=struct.unpack('>II',d[16:24]); raw=b''; p=8
    while p<len(d):
        L=struct.unpack('>I',d[p:p+4])[0]; t=d[p+4:p+8]
        if t==b'IDAT': raw+=d[p+8:p+8+L]
        p+=12+L
    raw=zlib.decompress(raw); return w,h,[raw[y*(w*3+1)+1:(y+1)*(w*3+1)] for y in range(h)]
def ch(t,x): return struct.pack('>I',len(x))+t+x+struct.pack('>I',zlib.crc32(t+x)&0xffffffff)
out_path=sys.argv[1]; x0,y0,x1,y1=map(int,sys.argv[2:6]); files=sys.argv[6:]
rows=[]
for f in files:
    w,h,r=load(f); rows.append([bytes(r[y][x0*3:x1*3]) for y in range(y0,y1)])
out=[b''.join(rr[i] for rr in rows) for i in range(y1-y0)]
open(out_path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',(x1-x0)*len(files),y1-y0,8,2,0,0,0))+ch(b'IDAT',zlib.compress(b''.join(b'\0'+r for r in out)))+ch(b'IEND',b''))
