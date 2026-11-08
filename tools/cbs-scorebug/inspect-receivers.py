"""Read-only receiver diagnostic for the owned review process.

The decoded scope-0 receiving getters use live+0x10: nine reception bucket
bytes at +0x0A..0x12 and signed receiving yards at +4. This is a diagnostic,
not a replacement producer or a guest accessor call from a host thread.
"""
import ctypes
import json
import pathlib
import struct
import sys
from ctypes import wintypes

review = pathlib.Path(sys.argv[1]).resolve()
pid = json.loads((review / 'process.json').read_text())['id']
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.restype = wintypes.HANDLE
k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
h = k.OpenProcess(0x410, False, pid)
if not h:
    raise RuntimeError('Review is not running')
try:
    path = ctypes.create_unicode_buffer(32768); length = wintypes.DWORD(len(path))
    if not k.QueryFullProcessImageNameW(h, 0, path, ctypes.byref(length)) or pathlib.Path(path.value).resolve() != review / 'root' / 'NFL2K5_CBSReview.exe':
        raise RuntimeError('Process is not this owned review')
    def read(va, size):
        data = ctypes.create_string_buffer(size); done = ctypes.c_size_t()
        if not va or not k.ReadProcessMemory(h, ctypes.c_void_p(va + 0x10000), data, size, ctypes.byref(done)) or done.value != size:
            raise RuntimeError('Unreadable guest address ' + hex(va))
        return data.raw
    def u32(va): return struct.unpack('<I', read(va, 4))[0]
    def text(at):
        raw=u32(at); relative=at+struct.unpack('<i',struct.pack('<I',raw))[0]
        for base in [raw,relative-1,relative]:
            if not 0x10000<=base<=0x8fffffff: continue
            try:
                data=read(base,80)
                if data[1]==0: value=data.decode('utf-16-le').split('\0',1)[0]
                else: value=data.split(b'\0',1)[0].decode('ascii')
                if value and all(32<=ord(c)<127 for c in value): return value
            except (RuntimeError,UnicodeError): pass
        return '(unavailable)'
    output=[]
    for side, obj in [('away', 0xE5FC60), ('home', 0xE5FC20)]:
        roster=u32(obj+0x1c); count=read(roster+0x11c,1)[0]
        for index in range(min(count,71)):
            player=u32(roster+index*4); position=read(player+0x35,1)[0]
            if position not in (3,9): continue
            slot=u32(player+0x30); live=u32(slot) if slot else 0
            receive=u32(live+0x10) if live else 0
            output.append(dict(side=side,position='TE' if position==9 else 'WR',
                name=text(player+0x10)+' '+text(player+0x14),record=hex(player),live=hex(live),receiving=hex(receive),
                receptions=sum(read(receive+0xA,9)) if receive else None,
                yards=struct.unpack('<h',read(receive+4,2))[0] if receive else None))
    print(json.dumps(output,indent=2))
finally:
    k.CloseHandle.argtypes=[wintypes.HANDLE];k.CloseHandle(h)
