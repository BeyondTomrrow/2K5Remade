"""Stop Xemu when the NFL 2K5 worker counter at B0284C changes."""
import socket
import select

HOST, PORT = "127.0.0.1", 1234


def checksum(data):
    return sum(data) & 0xFF


def packet(sock):
    while sock.recv(1) != b"$":
        pass
    body = bytearray()
    while True:
        byte = sock.recv(1)
        if byte == b"#":
            break
        body += byte
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
    # QEMU sends this on some connections but not all reconnects.
    if select.select([sock], [], [], 0.25)[0]:
        print("initial", packet(sock).decode("ascii", "replace"))
    print("watch", command(sock, "Z2,b0284c,4").decode("ascii", "replace"))
    resume(sock)
    print("stop", packet(sock).decode("ascii", "replace"))
    registers = command(sock, "g").decode("ascii", "replace")
    values = [int.from_bytes(bytes.fromhex(registers[i:i + 8]), "little")
              for i in range(0, min(len(registers), 72), 8)]
    print("eip=0x%08X esp=0x%08X" % (values[8], values[4]))
    print("stack", command(sock, "m%x,40" % values[4]).decode("ascii", "replace"))
    print("counter", command(sock, "mb02848,8").decode("ascii", "replace"))
    print("clear", command(sock, "z2,b0284c,4").decode("ascii", "replace"))
    resume(sock)
