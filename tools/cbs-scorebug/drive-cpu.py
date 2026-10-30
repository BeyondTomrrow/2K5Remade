"""Drive the owned native-review session with ordinary press-file input.

FSM/port addresses are confirmed in this retail build's generated code and
the October 17 captures. Memory access is read-only. Unlike the older timed
menu recipe this stops if the expected menu or CPU assignment is not reached.
"""
import ctypes
import json
import pathlib
import struct
import sys
import time
from ctypes import wintypes

review = pathlib.Path(sys.argv[1]).resolve()
human_review = '--human-review' in sys.argv[2:]
pid = json.loads((review / "process.json").read_text())["id"]
kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
    ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
handle = kernel.OpenProcess(0x410, False, pid)  # query + read, never write
if not handle:
    raise RuntimeError("Review process is unavailable")
press_file = review / "press.txt"


def read(va):
    data = ctypes.create_string_buffer(4)
    size = ctypes.c_size_t()
    if not kernel.ReadProcessMemory(handle, ctypes.c_void_p(va + 0x10000),
                                   data, 4, ctypes.byref(size)) or size.value != 4:
        raise RuntimeError("Guest state read failed")
    return struct.unpack("<I", data.raw)[0]


def state():
    count = read(0xA84C18)
    return read(0xA84B18 + count * 8) if count <= 30 else 0


def press(button):
    end = time.monotonic() + 3
    while press_file.exists() and time.monotonic() < end:
        time.sleep(0.1)
    if press_file.exists():
        raise RuntimeError("Previous input was not consumed")
    press_file.write_text(button, encoding="ascii")
    print("PRESS", button, flush=True)


def wait_state(states, seconds=30):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        current = state()
        if current in states:
            print("STATE", hex(current), "SIDE", read(0xE5FE88), flush=True)
            return
        time.sleep(0.1)
    raise RuntimeError(f"Expected menu not reached: {state():08X}")


try:
    # The private launcher returns before the XBE's mapping is necessarily
    # readable. Wait for that actual prerequisite, without sending blind input.
    mapped_by = time.monotonic() + 45
    while True:
        try:
            state()
            break
        except RuntimeError:
            if time.monotonic() >= mapped_by:
                raise
            time.sleep(0.1)
    end = time.monotonic() + 100
    next_press = 0
    while state() != 0x515660:
        if time.monotonic() > end:
            raise RuntimeError("Main menu not reached; inspect the captured frame")
        if time.monotonic() >= next_press and not press_file.exists():
            press("start")
            next_press = time.monotonic() + 8
        time.sleep(0.1)
    time.sleep(1)
    press("a")
    wait_state({0x52728C})
    side = read(0xE5FE88)
    if not human_review and side in (1, 2):
        press("left" if side == 1 else "right")
        time.sleep(3)
    if human_review and side == 0:
        press("right")
        time.sleep(3)
    expected_side = read(0xE5FE88) if human_review else 0
    if read(0xE5FE88) != expected_side or (human_review and expected_side not in (1, 2)):
        raise RuntimeError("Controller assignment was not confirmed on Team Select")
    print("Controller side confirmed:", expected_side, "human review" if human_review else "CPU", flush=True)
    press("start")
    wait_state({0x585474, 0x55D088})
    if read(0xE5FE88) != expected_side:
        raise RuntimeError("Controller reassigned at Coach Match Up")
    press("a")
    wait_state({0x4E7EC0}, seconds=90)
    if read(0xE5FE88) != expected_side:
        raise RuntimeError("Controller reassigned during match loading")
    # The retail pregame can retain a default-port coin-toss choice even
    # with both teams CPU. Acknowledge those actual screens, not guest RAM.
    end = time.monotonic() + 240
    next_press = time.monotonic() + 6
    while read(0xE602C4) == 0:
        if time.monotonic() > end:
            raise RuntimeError("Pregame did not finish; inspect the captured frame")
        if time.monotonic() >= next_press and not press_file.exists():
            press("a")
            next_press = time.monotonic() + 7
        time.sleep(0.1)
    if read(0xE5FE88) != expected_side:
        raise RuntimeError("Controller reassigned during pregame")
    print("Loaded match; controller side", expected_side, "period", read(0xE602C4), flush=True)
finally:
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle(handle)
