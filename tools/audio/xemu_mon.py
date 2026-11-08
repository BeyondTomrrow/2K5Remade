"""QEMU monitor helper for xemu (127.0.0.1:4444): xemu_mon.py <cmd> [<cmd>...]
'shot <png>' = screendump via a temp ppm then PNG."""
import socket, sys, time, os
def mon(cmds, wait=0.4):
    s = socket.create_connection(('127.0.0.1', 4444), timeout=5); s.settimeout(0.3)
    def drain():
        out = b''
        while True:
            try:
                p = s.recv(65536)
                if not p: break
                out += p
            except (TimeoutError, socket.timeout): break
        return out.decode('ascii', 'replace')
    time.sleep(0.3); drain(); res = []
    for c in cmds:
        s.sendall((c + '\n').encode()); time.sleep(wait); res.append(drain())
    s.close(); return res
if __name__ == '__main__':
    args = sys.argv[1:]
    if args and args[0] == 'shot':
        png = args[1]; ppm = png[:-4] + '.ppm'
        try: os.remove(ppm)
        except FileNotFoundError: pass
        mon(['screendump ' + ppm.replace(chr(92), '/')], wait=2.0)
        from PIL import Image
        Image.open(ppm).save(png); os.remove(ppm); print('saved', png)
    else:
        for r in mon(args): print(r)
