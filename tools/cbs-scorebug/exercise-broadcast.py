"""Bounded exhibition exercise using ordinary buttons and read-only guest RAM.

Run after drive-cpu.py in an owned private review. No event, injury, penalty,
replay, clock or result is manufactured. Native rules choose what happens.
"""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import shutil
import struct
import sys
import time

review = Path(sys.argv[1]).resolve()
seconds = min(600, max(1, int(sys.argv[2]) if len(sys.argv) > 2 else 300))
idle_first = next((min(60, max(0, int(arg.split('=', 1)[1]))) for arg in sys.argv[3:] if arg.startswith('--idle-first=')), 0)
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
record = json.loads((review / 'process.json').read_text(encoding='utf-8-sig'))
handle = kernel.OpenProcess(0x1010, False, record['id'])
if not handle:
    raise RuntimeError('Owned exhibition is not running')
try:
    image = ctypes.create_unicode_buffer(32768)
    count = wintypes.DWORD(len(image))
    if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(count)) or Path(image.value).resolve() != review / 'root/NFL2K5_CBSReview.exe':
        raise RuntimeError('PID does not match this private exhibition')

    def data(va, size):
        out = ctypes.create_string_buffer(size)
        if not kernel.ReadProcessMemory(handle, ctypes.c_void_p(va + 0x10000), out, size, None):
            raise RuntimeError('Guest mapping is unavailable or process exited')
        return out.raw

    def read(va):
        return struct.unpack('<I', data(va, 4))[0]

    def title(ctx):
        return data(ctx + 0x4C, 128).decode('utf-16-le', errors='replace').split('\0')[0]

    end = time.monotonic() + seconds
    next_input = time.monotonic() + idle_first
    previous = None
    press = review / 'press.txt'
    while time.monotonic() < end:
        if shutil.disk_usage(review).free < 128 * 1024 * 1024:
            raise RuntimeError('Stopping bounded test before diagnostic drive fills')
        top = read(0xA84C18)
        descriptor = read(0xA84B18 + top * 8) if top < 32 else 0
        period, flow, phase = read(0xE602C4), read(0xE602B8), read(0xE602B4)
        slots = []
        review_active = False
        for slot in (0, 1):
            ctx = 0xB61B50 + slot * 0xF20
            if read(ctx + 4) == 1:
                callback = read(ctx + 0xF14)
                slots.append((slot, read(ctx + 0x48), title(ctx), f'{callback:08X}'))
                review_active |= callback in (0x1172F0, 0x117300)
        signature = (period, flow, phase, descriptor, tuple(slots))
        if signature != previous:
            print('EXHIBITION', signature, flush=True)
            previous = signature
        # Let replay and cinematic screens run without an artificial skip.
        # A selects/snap/passes/confirms only through normal native controls.
        if period >= 1 and descriptor == 0x4E7EC0 and flow != 0x12 and not review_active and time.monotonic() >= next_input and not press.exists():
            press.write_text('a', encoding='ascii')
            next_input = time.monotonic() + 4
        if flow == 0x0A:
            print('Native game-over state reached.', flush=True)
            break
        time.sleep(.1)
finally:
    kernel.CloseHandle(handle)
