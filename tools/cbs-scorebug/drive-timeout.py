"""Request a timeout through the real pause menu of an owned review process.
Guest memory is read-only. Native eligibility alone decides acceptance.
"""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import struct
import sys
import time

review = Path(sys.argv[1]).resolve()
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
record = json.loads((review / 'process.json').read_text(encoding='utf-8-sig'))
handle = kernel.OpenProcess(0x1010, False, record['id'])
if not handle:
    raise RuntimeError('Owned review is unavailable')
try:
    image = ctypes.create_unicode_buffer(32768)
    capacity = wintypes.DWORD(len(image))
    if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity)) or Path(image.value).resolve() != review / 'root/NFL2K5_CBSReview.exe':
        raise RuntimeError('PID is not this private review')

    def read(va, fmt='I'):
        out = ctypes.create_string_buffer(4)
        if not kernel.ReadProcessMemory(handle, ctypes.c_void_p(va + 0x10000), out, 4, None):
            raise RuntimeError('Guest state is unavailable')
        return struct.unpack('<' + fmt, out.raw)[0]

    def menu():
        top = read(0xA84C18)
        return read(0xA84B18 + top * 8) if top < 32 else 0

    def press(button):
        p = review / 'press.txt'
        deadline = time.monotonic() + 3
        while p.exists() and time.monotonic() < deadline:
            time.sleep(.05)
        if p.exists():
            raise RuntimeError('Previous input was not consumed')
        p.write_text(button, encoding='ascii')
        print('TIMEOUT PRESS', button, flush=True)
        time.sleep(.7)

    side = read(0xE5FE88)
    if side not in (1, 2):
        raise RuntimeError('An ordinarily assigned controller is required')
    team = 0xE5FC60 if side == 1 else 0xE5FC20
    deadline = time.monotonic() + 180
    next_input = 0
    while time.monotonic() < deadline:
        flow = read(0xE602B8)
        if flow == 0x12 and not read(0xB38C30):
            break
        if flow != 0x12 and menu() == 0x4E7EC0 and time.monotonic() >= next_input:
            press('a')
            next_input = time.monotonic() + 4
        time.sleep(.1)
    else:
        raise RuntimeError('No native post-play timeout opportunity appeared')
    press('start')
    deadline = time.monotonic() + 6
    while menu() != 0x4E9078 and time.monotonic() < deadline:
        time.sleep(.05)
    if menu() != 0x4E9078:
        raise RuntimeError('Ordinary pause menu did not open')
    press('down')
    press('down')
    press('a')
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        if read(0xE602B8) == 0x13 and read(0xE602F8) == team:
            print('Actual team timeout accepted:', hex(team), 'presentation', hex(read(0xB616C0)), flush=True)
            break
        time.sleep(.05)
    else:
        raise RuntimeError(f'Native timeout was not accepted; active menu {menu():08X}')
    previous = None
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        clock = read(0xE6028C)
        signature = (read(0xE602B8), read(0xB616C0), read(0xE602F8), read(clock + 0x10, 'f'), read(0xA95520), read(0xA95524))
        if signature != previous:
            print('TIMEOUT NATIVE', signature, flush=True)
            previous = signature
        time.sleep(.1)
finally:
    kernel.CloseHandle(handle)
