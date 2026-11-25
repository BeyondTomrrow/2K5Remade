import zlib,struct,sys
def load(f):
    d=open(f,'rb').read(); w,h=struct.unpack('>II',d[16:24]); raw=b''; p=8
    while p<len(d):
        L=struct.unpack('>I',d[p:p+4])[0]; t=d[p+4:p+8]
        if t==b'IDAT': raw+=d[p+8:p+8+L]
        p+=12+L
    raw=zlib.decompress(raw); return w,h,[raw[y*(w*3+1)+1:(y+1)*(w*3+1)] for y in range(h)]
out_path=sys.argv[1]; files=sys.argv[2:]; ims=[load(f) for f in files]
w,h=360,240; cols=4; rows_n=(len(ims)+3)//4; W,H=w*cols,h*rows_n; out=[bytearray(W*3) for _ in range(H)]
for k,(iw,ih,rows) in enumerate(ims):
    ox,oy=(k%cols)*w,(k//cols)*h; sx=iw/w; sy=ih/h
    for y in range(h):
        r=rows[int(y*sy)]
        for x in range(w):
            q=int(x*sx)*3; out[oy+y][(ox+x)*3:(ox+x)*3+3]=r[q:q+3]
def ch(t,x): return struct.pack('>I',len(x))+t+x+struct.pack('>I',zlib.crc32(t+x)&0xffffffff)
open(out_path,'wb').write(b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(b''.join(b'\0'+bytes(r) for r in out)))+ch(b'IEND',b''))
