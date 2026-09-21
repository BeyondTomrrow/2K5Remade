"""Capture the retail worker-completion handoff at sub_00035CE0 in Xemu."""
import socket
import select

HOST, PORT = "127.0.0.1", 1234

def checksum(data):
    return sum(data) & 0xff

def packet(sock):
    while sock.recv(1) != b"$":
        pass
    body = bytearray()
    while True:
        char = sock.recv(1)
        if char == b"#":
            break
        body += char
    sock.recv(2)
    sock.sendall(b"+")
    return bytes(body)

def command(sock, text):
    data = text.encode("ascii")
    sock.sendall(b"$" + data + b"#" + f"{checksum(data):02x}".encode("ascii"))
    if sock.recv(1) != b"+":
        raise RuntimeError("GDB command was not acknowledged")
    return packet(sock)

def resume(sock):
    data = b"c"
    sock.sendall(b"$" + data + b"#" + f"{checksum(data):02x}".encode("ascii"))
    if sock.recv(1) != b"+":
        raise RuntimeError("GDB continue was not acknowledged")

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    sock.settimeout(15)
    if select.select([sock], [], [], 0.25)[0]:
        print("initial", packet(sock).decode("ascii", "replace"))
    print("break", command(sock, "Z0,35ce0,1").decode("ascii", "replace"))
    resume(sock)
    print("stop", packet(sock).decode("ascii", "replace"))
    regs = command(sock, "g").decode("ascii", "replace")
    vals = [int.from_bytes(bytes.fromhex(regs[i:i+8]), "little")
            for i in range(0, min(len(regs), 72), 8)]
    print("eip=0x%08X esp=0x%08X ecx=0x%08X" % (vals[8], vals[4], vals[1]))
    print("stack", command(sock, "m%x,40" % vals[4]).decode("ascii", "replace"))
    print("worker", command(sock, "mb02678,20").decode("ascii", "replace"))
    print("clear", command(sock, "z0,35ce0,1").decode("ascii", "replace"))
    resume(sock)
