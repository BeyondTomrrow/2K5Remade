import socket,struct,select
HOST,PORT='127.0.0.1',1234
ADDR=0x0048ED56
def cs(b): return sum(b)&255
def pkt(s):
    while s.recv(1)!=b'$': pass
    out=bytearray()
    while True:
        c=s.recv(1)
        if c==b'#': break
        out+=c
    s.recv(2);s.sendall(b'+');return bytes(out)
def q(s,t):
    b=t.encode();s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode())
    if s.recv(1)!=b'+':raise RuntimeError('ack')
    return pkt(s)
def cont(s):
    s.sendall(b'$c#63')
    if s.recv(1)!=b'+':raise RuntimeError('continue ack')
with socket.create_connection((HOST,PORT),timeout=5) as s:
 s.settimeout(15)
 if select.select([s],[],[],.25)[0]: print('initial',pkt(s).decode('ascii','replace'))
 print('break',q(s,f'Z0,{ADDR:x},1').decode())
 try:
  cont(s); print('stop',pkt(s).decode('ascii','replace'))
  raw=bytes.fromhex(q(s,'g').decode()); r=[struct.unpack_from('<I',raw,i)[0] for i in range(0,36,4)]
  print('eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X eip=%08X'%tuple(r))
  print('stack',q(s,f'm{r[4]:x},60').decode())
  for a in (r[0],r[1],r[2],r[3],r[6],r[7]):
   if 0<a<0x02000000:
    try: print(f'mem-{a:08X}',q(s,f'm{a:x},20').decode())
    except: pass
 except socket.timeout:
  print('timeout: 0048ED56 not reached')
  s.sendall(b'\x03');print('interrupt',pkt(s).decode('ascii','replace'))
 finally:
  try: print('clear',q(s,f'z0,{ADDR:x},1').decode())
  except Exception as e: print('clear failure',e)
  try: cont(s)
  except: pass

