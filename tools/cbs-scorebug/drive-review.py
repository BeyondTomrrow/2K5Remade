"""Attempt a real coach challenge with ordinary title buttons, then observe.

Only the existing private input file is written. Guest RAM is read-only;
native rules may reject a non-reviewable play, which is reported honestly.
Run after drive-cpu.py in an owned private native-review directory.
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
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                   ctypes.c_size_t, ctypes.c_void_p]
kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                            wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
pid = json.loads((review / 'process.json').read_text(encoding='utf-8-sig'))['id']
handle = kernel.OpenProcess(0x1010, False, pid)
if not handle:
    raise RuntimeError('Owned review process is unavailable')

try:
    image = ctypes.create_unicode_buffer(32768)
    capacity = wintypes.DWORD(len(image))
    if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(capacity)) or \
            Path(image.value).resolve() != review / 'root/NFL2K5_CBSReview.exe':
        raise RuntimeError('PID does not belong to this private review')

    def read(va, fmt='I'):
        data = ctypes.create_string_buffer(4)
        if not kernel.ReadProcessMemory(handle, ctypes.c_void_p(va + 0x10000), data, 4, None):
            raise RuntimeError('Guest state unavailable or process exited')
        return struct.unpack('<' + fmt, data.raw)[0]

    def press(button):
        path = review / 'press.txt'
        limit = time.monotonic() + 3
        while path.exists() and time.monotonic() < limit:
            time.sleep(.05)
        if path.exists():
            raise RuntimeError('Previous ordinary input was not consumed')
        path.write_text(button, encoding='ascii')
        print('REVIEW PRESS', button, flush=True)
        time.sleep(.7)

    def menu():
        top = read(0xA84C18)
        return read(0xA84B18 + top * 8) if top < 32 else 0

    def wait_menu(expected, seconds=6):
        limit = time.monotonic() + seconds
        while time.monotonic() < limit:
            if menu() == expected:
                return True
            time.sleep(.05)
        return False

    if read(0xE5FE88) not in (1, 2):
        raise RuntimeError('Coach challenges require an ordinarily assigned controller; CPU-only is ineligible')

    # Play with ordinary A presses until two post-play windows have been
    # observed. Native rules alone decide whether that play is reviewable.
    limit = time.monotonic() + 240
    previous = None
    completed = 0
    next_input = time.monotonic()
    while time.monotonic() < limit:
        flow = read(0xE602B8)
        if flow == 0x12 and previous != flow:
            completed += 1
        if completed >= 2 and flow in (0x0B, 0x0C, 0x12) and not read(0xB38C30):
            break
        if flow != 0x12 and menu() not in (0x4E9078, 0x52DC20) and time.monotonic() >= next_input:
            press('a')
            next_input = time.monotonic() + 4
        previous = flow
        time.sleep(.1)
    else:
        print('No suitable post-play input window observed; natural game continues.', flush=True)
        raise SystemExit(0)
    print('Observed completed plays:', completed, 'flow', hex(flow), flush=True)
    press('start')
    if not wait_menu(0x4E9078):
        print('Ordinary Start did not open pause; no challenge was forced.', flush=True)
    else:
        # The ordinary pause list: Resume, Replay, Timeout, Challenge.
        for _ in range(3):
            press('down')
        press('a')
        if not wait_menu(0x52DC20):
            raise RuntimeError('Native challenge menu was not reached; inspect the capture')
        # Ball Spot Error opens a native confirmation with Cancel selected.
        # Select Call Challenge explicitly rather than assuming A confirms.
        press('a')
        time.sleep(1)
        press('down')
        press('a')
        print('Coach challenge requested through the real menu; native rules decide eligibility.', flush=True)
        if wait_menu(0x4E7EC0, seconds=8):
            print('Native pause/reason screens closed after confirmation.', flush=True)
        else:
            raise RuntimeError('Challenge did not return to the match; inspect the capture')
    limit = time.monotonic() + 90
    while time.monotonic() < limit:
        log = (review / 'stderr.log').read_text(encoding='utf-8', errors='replace')
        if '[REVIEW-CARD] accepted' in log and "stage=ruling" in log:
            print('Actual accepted review and ruling both logged.', flush=True)
            time.sleep(12)
            break
        time.sleep(.5)
    else:
        print('No accepted review/ruling pair observed; this is not a successful native review test.', flush=True)
finally:
    kernel.CloseHandle(handle)
