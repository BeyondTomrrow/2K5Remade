"""Capture one window by its title to a PNG (PrintWindow, works in the background).

    python tools/capture-window.py "NFL 2K5 Team Creator" logs/out.png
"""
import ctypes
import sys
from ctypes import wintypes

from PIL import Image

u, g = ctypes.windll.user32, ctypes.windll.gdi32
u.SetProcessDPIAware()
h = u.FindWindowW(None, sys.argv[1])
if not h:
    sys.exit("no window titled %r" % sys.argv[1])
r = wintypes.RECT()
u.GetWindowRect(h, ctypes.byref(r))
w, hh = r.right - r.left, r.bottom - r.top
dc = u.GetWindowDC(h)
mdc = g.CreateCompatibleDC(dc)
bmp = g.CreateCompatibleBitmap(dc, w, hh)
g.SelectObject(mdc, bmp)
u.PrintWindow(h, mdc, 2)
buf = ctypes.create_string_buffer(w * hh * 4)


class BMI(ctypes.Structure):
    _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                ("biPlanes", wintypes.WORD), ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                ("biSizeImage", wintypes.DWORD), ("a", ctypes.c_long), ("b", ctypes.c_long),
                ("c", wintypes.DWORD), ("d", wintypes.DWORD)]


bmi = BMI(ctypes.sizeof(BMI), w, -hh, 1, 32, 0, 0, 0, 0, 0, 0)
g.GetDIBits(mdc, bmp, 0, hh, buf, ctypes.byref(bmi), 0)
Image.frombuffer("RGBA", (w, hh), buf, "raw", "BGRA", 0, 1).convert("RGB").save(sys.argv[2])
print("saved", sys.argv[2], w, hh)
