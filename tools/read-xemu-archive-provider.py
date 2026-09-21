import socket
HOST,PORT='127.0.0.1',1234
def cs(b): return sum(b)&255
def pkt(s):
    while s.recv(1)!=b'$': pass
    b=bytearray()
    while True:
        c=s.recv(1)
        if c==b'#': break
        b+=c
    n=int(s.recv(2),16)
    if cs(b)!=n: raise RuntimeError('checksum')
    s.sendall(b'+'); return bytes(b)
def send(s,t):
    b=t.encode(); s.sendall(b'$'+b+b'#'+f'{cs(b):02x}'.encode())
    if s.recv(1)!=b'+': raise RuntimeError('no ack')
    return pkt(s)
with socket.create_connection((HOST,PORT),timeout=5) as s:
    # Xemu sends an initial asynchronous stop notification as soon as GDB
    # connects. Consume it before issuing a request, otherwise that packet is
    # mistaken for the acknowledgement to the first command.
    print('initial',pkt(s))
    print('status',send(s,'?'))
    for a in (0x00010000,0x00016BD1,0x004409A8,0x00B04EC0,0x00B04D18,
              0x00B04EC8,0x00A75BC8,0x00B057D4,0x00B09570,0x00B122AC,
              0x00B17BF0,0x00A84B14,0x00469350,0x00A70874,0x00A70878,
              0x00A7087C,0x00B018B0,0x00B018B4,
              0x00B02678,0x00B02680,0x00B02684,0x00B02688,
              0x00B02828,0x00B02848,0x00B0284C,0x80023120):
        print(f'0x{a:08X}:',send(s,f'm{a:x},20').decode('ascii','replace'))
    # Read-only reference capture of the active FIFO range observed in Xemu.
    print('fifo-010f0f18',send(s,'m10f0f18,100').decode('ascii','replace'))
    s.sendall(b'$c#63'); s.recv(1)
