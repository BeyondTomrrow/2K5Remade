import socket,struct,select
HOST,PORT='127.0.0.1',1234; ADDR=0x0048ECD0
def cs(b): return sum(b)&255
def pkt(s):
 while s.recv(1)!=b'$': pass
 o=bytearray()
 while True:
  c=s.recv(1)
  if c==b'#':break
  o+=c
 s.recv(2);s.sendall(b'+');return bytes(o)
def q(s,t):
 b=t.encode();s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode());
 if s.recv(1)!=b'+': raise RuntimeError('ack')
 return pkt(s)
def cont(s):
 s.sendall(b'$c#63');
 if s.recv(1)!=b'+':raise RuntimeError('continue')
with socket.create_connection((HOST,PORT),timeout=5) as s:
 s.settimeout(20)
 if select.select([s],[],[],.25)[0]: print('initial',pkt(s).decode('ascii','replace'))
 print('break',q(s,f'Z0,{ADDR:x},1').decode())
 try:
  cont(s); print('stop',pkt(s).decode())
  raw=bytes.fromhex(q(s,'g').decode()); r=[struct.unpack_from('<I',raw,i)[0] for i in range(0,36,4)]
  ctx=r[1]; esp=r[4]; item=struct.unpack('<I',bytes.fromhex(q(s,f'm{esp+4:x},4').decode()))[0]
  print(f'ctx={ctx:08X} item={item:08X} ret={struct.unpack("<I",bytes.fromhex(q(s,f"m{esp:x},4").decode()))[0]:08X}')
  for name,a,n in [('ctx',ctx,0x280),('item',item,0x30),('route',ctx+0x260,0x20)]: print(name,q(s,f'm{a:x},{n:x}').decode())
 except Exception as e: print('error',repr(e))
 finally:
  try: print('clear',q(s,f'z0,{ADDR:x},1').decode())
  except Exception as e: print('clear error',e)
  try: cont(s)
  except:pass
