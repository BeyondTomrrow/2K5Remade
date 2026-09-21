"""Read Xemu's current guest/FIFO state without assuming an initial stop packet."""
import socket
import select

HOST, PORT = '127.0.0.1', 1234

def checksum(data):
    return sum(data) & 0xff

def packet(sock):
    while sock.recv(1) != b'$':
        pass
    body = bytearray()
    while True:
        byte = sock.recv(1)
        if byte == b'#':
            break
        body += byte
    sock.recv(2)
    sock.sendall(b'+')
    return bytes(body)

def command(sock, text):
    data = text.encode('ascii')
    sock.sendall(b'$' + data + b'#' + f'{checksum(data):02x}'.encode('ascii'))
    if sock.recv(1) != b'+':
        raise RuntimeError('GDB command was not acknowledged')
    return packet(sock)

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    sock.settimeout(8)
    if select.select([sock], [], [], 0.25)[0]:
        print('initial', packet(sock).decode('ascii', 'replace'))
    print('status', command(sock, '?').decode('ascii', 'replace'))
    for address, length in ((0x00044DF0, 0x100), (0x003CC280, 0x100), (0x00B04D18, 0x40),
                            (0x00B1206C, 0x90), (0x00B09570, 0x20),
                            (0x00B02848, 0x10), (0x00A70874, 0x20),
                            (0x00A70878, 0x20), (0x00A7087C, 0x20),
                            (0x810B8F68, 0x100), (0x810B8FA0, 0x100),
                            (0x010F0F18, 0x100)):
        print('0x%08X:' % address,
              command(sock, 'm%x,%x' % (address, length)).decode('ascii', 'replace'))
    data = b'c'
    sock.sendall(b'$c#63')
    sock.recv(1)
