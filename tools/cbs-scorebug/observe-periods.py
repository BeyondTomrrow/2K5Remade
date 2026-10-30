"""Read-only period/flow evidence for an owned native-review process.

Does not send input, write guest memory, create game events or launch a game.
Tick timestamps align with the private harness's composed D3D11 captures.
"""
import ctypes
import json
import pathlib
import struct
import sys
import time
from ctypes import wintypes

review = pathlib.Path(sys.argv[1]).resolve()
seconds = min(600, max(1, int(sys.argv[2]) if len(sys.argv) > 2 else 180))
record = json.loads((review / 'process.json').read_text())
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
    ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
    wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel.GetTickCount64.restype = ctypes.c_ulonglong
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
handle = kernel.OpenProcess(0x410, False, record['id'])
if not handle:
    raise RuntimeError('Review process is unavailable')
try:
    image = ctypes.create_unicode_buffer(32768)
    capacity = wintypes.DWORD(len(image))
    if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity)) or \
            pathlib.Path(image.value).resolve() != review / 'root/NFL2K5_CBSReview.exe':
        raise RuntimeError('PID does not belong to this private review')

    def read(address, format='I'):
        data = ctypes.create_string_buffer(4)
        if not kernel.ReadProcessMemory(handle, ctypes.c_void_p(address + 0x10000), data, 4, None):
            raise RuntimeError('Guest state unavailable or game exited')
        return struct.unpack('<' + format, data.raw)[0]

    output = review / ('period-flow-' + str(kernel.GetTickCount64()) + '.csv')
    print('Read-only trace:', output, flush=True)
    previous = None
    end = time.monotonic() + seconds
    with output.open('w', encoding='utf-8', newline='') as log:
        log.write('host_tick,period,flow,phase,clock_seconds\n')
        while time.monotonic() < end:
            period, flow, phase = read(0xE602C4), read(0xE602B8), read(0xE602B4)
            clock = read(read(0xE6028C) + 0x10, 'f')
            signature = (period, flow, phase, int(clock))
            if signature != previous:
                line = f'{kernel.GetTickCount64()},{period},0x{flow:02X},{phase},{clock:.3f}'
                log.write(line + '\n')
                log.flush()
                if previous is None or signature[:3] != previous[:3]:
                    print(line, flush=True)
                previous = signature
            time.sleep(.1)
finally:
    kernel.CloseHandle(handle)
