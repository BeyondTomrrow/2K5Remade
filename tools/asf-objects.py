# List (and optionally drop) the top-level objects inside an ASF/WMA header.
#   python tools/asf-objects.py file.wma                 list
#   python tools/asf-objects.py in.wma out.wma GUID...   copy without those objects
import struct, sys, uuid
d = open(sys.argv[1], 'rb').read()
hsize, count = struct.unpack_from('<QI', d, 16)
pos, objs = 30, []
for _ in range(count):
    g = str(uuid.UUID(bytes_le=d[pos:pos + 16])).upper()
    size = struct.unpack_from('<Q', d, pos + 16)[0]
    objs.append((g, pos, size))
    pos += size
if len(sys.argv) < 4:
    for g, p, s in objs: print('%-38s @%5d size %d' % (g, p, s))
    sys.exit()
drop = {x.upper() for x in sys.argv[3:]}
keep = [d[p:p + s] for g, p, s in objs if g not in drop]
body = b''.join(keep)
head = bytearray(d[:30])
struct.pack_into('<QI', head, 16, 30 + len(body), len(keep))
open(sys.argv[2], 'wb').write(bytes(head) + body + d[hsize:])
