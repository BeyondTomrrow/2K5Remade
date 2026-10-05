# Read guest memory from a running NFL2K5.exe: python tools/peek.py <pid> <guest_va_hex> <dwords>
import ctypes, sys, struct
from ctypes import wintypes
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wintypes.HANDLE
k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
pid, va, n = int(sys.argv[1]), int(sys.argv[2], 16), int(sys.argv[3])
h = k.OpenProcess(0x0010 | 0x0400, False, pid)
buf = ctypes.create_string_buffer(n * 4); got = ctypes.c_size_t()
if not k.ReadProcessMemory(h, ctypes.c_void_p(va + 0x10000), buf, n * 4, ctypes.byref(got)):
    sys.exit('read failed %d' % ctypes.get_last_error())
w = struct.unpack('<%dI' % n, buf.raw)
for i in range(0, n, 4):
    print('%08X: ' % (va + i * 4) + ' '.join('%08X' % x for x in w[i:i + 4]))
