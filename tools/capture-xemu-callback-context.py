import socket,struct
HOST,PORT='127.0.0.1',1234
def cs(b): return sum(b)&255
def recv(s):
 while s.recv(1)!=b'$': pass
 b=bytearray()
 while (x:=s.recv(1))!=b'#': b+=x
 s.recv(2);s.sendall(b'+');return bytes(b)
def q(s,t):
 b=t.encode();s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode());
 if s.recv(1)!=b'+': raise RuntimeError('ack')
 return recv(s)
with socket.create_connection((HOST,PORT),timeout=5) as s:
 print('status',q(s,'?').decode())
 for a in (0x0003E910,0x003CD120): print('break',hex(a),q(s,f'Z1,{a:x},1').decode())
 s.sendall(b'$c#63');s.recv(1)
 stop=recv(s).decode(); print('stop',stop)
 raw=bytes.fromhex(q(s,'g').decode()); regs=[struct.unpack_from('<I',raw,i)[0] for i in range(0,36,4)]
 print('eax ecx edx ebx esp ebp esi edi eip',*[f'{x:08X}' for x in regs])
 esp=regs[4]; stack=bytes.fromhex(q(s,f'm{esp:x},80').decode()); print('stack',' '.join(f'{struct.unpack_from(chr(60)+chr(73),stack,i)[0]:08X}' for i in range(0,len(stack),4)))
 for a in (0x0003E910,0x003CD120): print('clear',hex(a),q(s,f'z1,{a:x},1').decode())
 s.sendall(b'$c#63');s.recv(1)
