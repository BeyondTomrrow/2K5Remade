import socket, time, os
out = r'E:\\NFL2K5-PC\\analysis\\xemu-captures\\xemu-live.ppm'
try: os.remove(out)
except FileNotFoundError: pass
s = socket.create_connection(('127.0.0.1', 4444), timeout=3)
s.settimeout(.3)
time.sleep(.3)
def recv_all():
    parts=[]
    while True:
        try:
            p=s.recv(8192)
            if not p: break
            parts.append(p)
        except TimeoutError: break
    return b''.join(parts).decode('ascii','replace')
reply=recv_all()
s.sendall(b'info status\n')
time.sleep(.25)
reply+=recv_all()
s.sendall(('screendump '+out+'\n').encode('ascii'))
time.sleep(3)
reply+=recv_all()
s.close()
print(reply)
print('exists=', os.path.exists(out), 'size=', os.path.getsize(out) if os.path.exists(out) else 0)
