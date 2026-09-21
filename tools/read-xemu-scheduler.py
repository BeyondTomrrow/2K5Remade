import socket
HOST,PORT='127.0.0.1',1234
def c(b): return sum(b)&255
def rx(s):
 while s.recv(1)!=b'$': pass
 b=bytearray()
 while (x:=s.recv(1))!=b'#': b+=x
 s.recv(2);s.sendall(b'+');return bytes(b)
def q(s,t):
 b=t.encode();s.sendall(b'$'+b+b'#'+f'{c(b):02x}'.encode());s.recv(1);return rx(s)
with socket.create_connection((HOST,PORT),timeout=5) as s:
 q(s,'?')
 for a in (0x00B04D18,0x00B04D20,0x004409A8):
  h=q(s,f'm{a:x},90').decode(); raw=bytes.fromhex(h)
  print(f'0x{a:08X}')
  for off in range(0,len(raw),16): print(f' +{off:02X}: '+' '.join(f'{int.from_bytes(raw[i:i+4],"little"):08X}' for i in range(off,min(off+16,len(raw)),4)))
 s.sendall(b'$c#63');s.recv(1)
