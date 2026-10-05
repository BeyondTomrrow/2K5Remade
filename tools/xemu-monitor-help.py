import socket,time
s=socket.create_connection(('127.0.0.1',4444),timeout=3);s.settimeout(.3);time.sleep(.2)
def recv():
 a=[]
 while True:
  try:
   x=s.recv(32768)
   if not x:break
   a.append(x)
  except TimeoutError:break
 return b''.join(a).decode('ascii','replace')
recv();s.sendall(b'help\n');time.sleep(.5);print(recv());s.close()
