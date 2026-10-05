"""Read the paused Xbox CPU state from Xemu's local GDB stub."""
import socket

HOST, PORT = "127.0.0.1", 1234

def checksum(data):
    return sum(data) & 0xFF

def recv_packet(sock):
    while sock.recv(1) != b"$":
        pass
    body = bytearray()
    while True:
        c = sock.recv(1)
        if c == b"#":
            break
        body += c
    expected = int(sock.recv(2), 16)
    if checksum(body) != expected:
        raise RuntimeError("bad GDB packet checksum")
    sock.sendall(b"+")
    return bytes(body)

def send_packet(sock, text):
    data = text.encode("ascii")
    sock.sendall(b"$" + data + b"#" + f"{checksum(data):02x}".encode("ascii"))
    if sock.recv(1) != b"+":
        raise RuntimeError("Xemu did not acknowledge GDB command")
    return recv_packet(sock)

with socket.create_connection((HOST, PORT), timeout=5) as sock:
    # QEMU reports its current execution state in response to the initial
    # status query; this also establishes the remote-protocol handshake.
    stop = send_packet(sock, "?").decode("ascii", "replace")
    raw = send_packet(sock, "g").decode("ascii")
    values = [int.from_bytes(bytes.fromhex(raw[i:i + 8]), "little")
              for i in range(0, min(len(raw), 128), 8)]
    names = ("eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
             "eip", "eflags", "cs", "ss", "ds", "es", "fs", "gs")
    print("Xemu stopped:", stop)
    for name, value in zip(names, values):
        print(f"{name} = 0x{value:08X}")
    # Continue only after status has been read.
    data = b"c"
    sock.sendall(b"$c#63")
    sock.recv(1)
