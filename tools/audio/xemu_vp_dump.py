"""xemu oracle: dump the VP voice table once a second. xemu_vp_dump.py <out.bin> <seconds> [<vpvaddr>]
Reads NV_PAPU_VPVADDR (0xFE80202C) over gdb when not given, then every second a pmemsave of the
256 x 128-byte table appended to <out.bin> after an 8-byte header (ms since this script started,
QPC-free wall tick), the layout tools/audio/vp_decode.py / vp_timeline.py expect."""
import sys, os, time, subprocess, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xemu_mon import mon
out, secs = sys.argv[1], float(sys.argv[2])
if len(sys.argv) > 3: base = int(sys.argv[3], 0)
else:
    g = os.path.join(os.path.dirname(out), 'vpaddr.gdb')
    open(g, 'w').write("set pagination off\nset confirm off\nset architecture i386\ntarget remote 127.0.0.1:1234\nx/1xw 0xFE80202C\ndetach\nquit\n")
    r = subprocess.run([r'E:\NFL2K5-PC\dependencies\msys2\ucrt64\bin\gdb.exe', '--batch', '-x', g], capture_output=True, text=True, timeout=60)
    m = re.search(r'0xfe80202c:\s+0x([0-9a-f]+)', r.stdout + r.stderr)
    base = int(m.group(1), 16)
print('VPVADDR', hex(base)); sys.stdout.flush()
tmp = out + '.tmp'
f = open(out, 'ab'); t0 = time.time(); n = 0
while time.time() - t0 < secs:
    try: os.remove(tmp)
    except FileNotFoundError: pass
    mon(['pmemsave 0x%X %d "%s"' % (base & 0x03FFFFFF, 256 * 128, tmp.replace(chr(92), '/'))], wait=0.5)
    try: d = open(tmp, 'rb').read()
    except FileNotFoundError: d = b''
    if len(d) == 256 * 128:
        ms = int((time.time() - t0) * 1000)
        f.write(ms.to_bytes(4, 'little') + int(time.time() * 1000 % 2**32).to_bytes(4, 'little') + d); f.flush(); n += 1
    time.sleep(max(0, 1.0 - ((time.time() - t0) % 1.0)))
print('records', n)
