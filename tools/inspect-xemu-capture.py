import glob
import os
import struct
p = max(glob.glob(r'E:\NFL2K5-PC\analysis\xemu-captures\*.ram.bin'), key=os.path.getmtime)
with open(p, 'rb') as f:
    d = f.read()
print(p)
for address in (0x00B04EC0, 0x00B04EC4, 0x00B04D18, 0x00B04D1C, 0x00B034F4, 0x00B034F8, 0x00B057D8, 0x00B068F0, 0x00B068F4, 0x00A84B14, 0x00A84B18, 0x00AF58C8, 0x00AF58D0, 0x00AF58D4, 0x00AF58D8, 0x004409A8):
    print(f'0x{address:08X} = 0x{struct.unpack_from(chr(60)+chr(73), d, address)[0]:08X}')
