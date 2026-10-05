"""Capture the first live retail call to the frontend pump at 0x003CC280."""
import socket
import select

HOST, PORT = '127.0.0.1', 1234

def checksum(data): return sum(data) & 0xff
def packet(sock):
    while sock.recv(1) != b'$': pass
    body = bytearray()
    while True:
        byte = sock.recv(1)
        if byte == b'#': break
        body += byte
    sock.recv(2); sock.sendall(b'+'); return bytes(body)
def command(sock, text):
    data = text.encode('ascii')
    sock.sendall(b'$' + data + b'#' + f'{checksum(data):02x}'.encode('ascii'))
    if sock.recv(1) != b'+': raise RuntimeError('no GDB acknowledgement')
    return packet(sock)
def resume(sock):
    sock.sendall(b'$c#63')
    if sock.recv(1) != b'+': raise RuntimeError('no continue acknowledgement')

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    sock.settimeout(15)
    if select.select([sock], [], [], .25)[0]:
        print('initial', packet(sock).decode('ascii', 'replace'))
    print('break', command(sock, 'Z0,3cc280,1').decode('ascii', 'replace'))
    resume(sock)
    print('stop', packet(sock).decode('ascii', 'replace'))
    regs = command(sock, 'g').decode('ascii', 'replace')
    values = [int.from_bytes(bytes.fromhex(regs[i:i+8]), 'little')
              for i in range(0, min(len(regs), 72), 8)]
    print('eip=%08X esp=%08X ecx=%08X' % (values[8], values[4], values[1]))
    print('stack', command(sock, 'm%x,20' % values[4]).decode('ascii', 'replace'))
    print('queue', command(sock, 'maf57c8,100').decode('ascii', 'replace'))
    print('state', command(sock, 'maf5880,20').decode('ascii', 'replace'))
    print('clear', command(sock, 'z0,3cc280,1').decode('ascii', 'replace'))
    resume(sock)
