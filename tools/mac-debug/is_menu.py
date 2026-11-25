"""Exit 0 if the PNG shows the main menu: the red QUICK GAME header present."""
import struct, sys, zlib
d=open(sys.argv[1],'rb').read(); w,h=struct.unpack('>II',d[16:24]); raw=b''; p=8
while p<len(d):
    L=struct.unpack('>I',d[p:p+4])[0]; t=d[p+4:p+8]
    if t==b'IDAT': raw+=d[p+8:p+8+L]
    p+=12+L
raw=zlib.decompress(raw); row=w*3+1
def px(x,y):
    i=int(y)*row+1+int(x)*3; return raw[i],raw[i+1],raw[i+2]
# main menu red header ~ frame (0.20..0.42 w, 0.13..0.16 h)
red=0; tot=0
for y in range(int(h*0.13),int(h*0.17),2):
  for x in range(int(w*0.20),int(w*0.42),3):
    r,g,b=px(x,y); tot+=1
    if r>110 and g<60 and b<60: red+=1
sys.exit(0 if tot and red/tot>0.4 else 1)
