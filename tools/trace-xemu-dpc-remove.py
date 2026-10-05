import socket,struct,select
HOST,PORT='127.0.0.1',1234; ADDR=0x0048ED4D
def checksum(data): return sum(data)&0xff
def packet(sock):
 while sock.recv(1)!=b'$': pass
 body=bytearray()
 while True:
  byte=sock.recv(1)
  if byte==b'#':break
  body+=byte
 sock.recv(2);sock.sendall(b'+');return bytes(body)
def command(sock,text):
 data=text.encode('ascii');sock.sendall(b'$'+data+b'#'+f'{checksum(data):02x}'.encode('ascii'))
 if sock.recv(1)!=b'+':raise RuntimeError('no ack')
 return packet(sock)
def resume(sock):
 sock.sendall(b'$c#63')
 if sock.recv(1)!=b'+':raise RuntimeError('no continue ack')
with socket.create_connection((HOST,PORT),timeout=5) as sock:
 sock.settimeout(15)
 if select.select([sock],[],[],.25)[0]:print('initial',packet(sock).decode('ascii','replace'))
 print('break',command(sock,f'Z0,{ADDR:x},1').decode())
 try:
  resume(sock);print('stop',packet(sock).decode('ascii','replace'))
  regs=command(sock,'g').decode('ascii','replace'); vals=[int.from_bytes(bytes.fromhex(regs[i:i+8]),'little') for i in range(0,min(len(regs),72),8)]
  print('eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X eip=%08X'%tuple(vals))
  print('stack',command(sock,'m%x,40'%vals[4]).decode('ascii','replace'))
  argslot=vals[5]-4; arg=int.from_bytes(bytes.fromhex(command(sock,'m%x,4'%argslot).decode()),'little')
  print('prior-call-arg=%08X dpc=%s'%(arg,command(sock,'m%x,40'%arg).decode('ascii','replace')))
 except socket.timeout:print('timeout')
 finally:
  try:print('clear',command(sock,f'z0,{ADDR:x},1').decode())
  except Exception as e:print('clear failed',e)
  try:resume(sock)
  except:pass
