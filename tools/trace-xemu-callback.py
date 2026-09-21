import socket,time
HOST,PORT='127.0.0.1',1234
def cs(b): return sum(b)&255
def receive_packet(s):
 while True:
  q=s.recv(1)
  if q==b'$': break
 payload=bytearray()
 while True:
  c=s.recv(1)
  if c==b'#': break
  payload+=c
 check=s.recv(2); s.sendall(b'+'); return bytes(payload)
def command(s,t):
 b=t.encode();s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode()); ack=s.recv(1);print('ack',repr(ack), 'for',t)
 if ack!=b'+': return b''
 return receive_packet(s)
with socket.create_connection((HOST,PORT),timeout=5) as s:
 s.settimeout(3);time.sleep(.2)
 print('status',command(s,'?'))
 print('set',command(s,'Z0,3cd120,1'))
 b=b'c';s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode()); print('continue ack',repr(s.recv(1))); print('break stop',receive_packet(s))
 print('regs',command(s,'g').decode())
 print('clear',command(s,'z0,3cd120,1'))
 b=b'c';s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode());print('resume ack',repr(s.recv(1)))
