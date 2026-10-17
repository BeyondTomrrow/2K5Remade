ROOT = __import__('os').path.dirname(__import__('os').path.abspath(__file__))
while not __import__('os').path.isdir(__import__('os').path.join(ROOT, 'src', 'recomp')) and __import__('os').path.dirname(ROOT) != ROOT:
    ROOT = __import__('os').path.dirname(ROOT)   # the repository root
import importlib.util, struct, os, sys
spec = importlib.util.spec_from_file_location('bop', __import__("os").path.join(ROOT, r'tools\eras\build-office-pack.py'))
bop = importlib.util.module_from_spec(spec); spec.loader.exec_module(bop)
DISC = bop.DISC
def index():
    banks = [os.path.join(DISC, '%X' % i) for i in range(16)]
    with open(banks[0], 'rb') as f:
        head = f.read(0x9C); count = struct.unpack_from('<I', head, 0)[0]; table = f.read(12*count)
    return {struct.unpack_from('<III', table, 12*i)[0]: struct.unpack_from('<III', table, 12*i)[1:] for i in range(count)}
IDX = index()
def has(name): return bop.crc(name) in IDX
def size(name): return IDX[bop.crc(name)][0] & 0x0FFFFFFF
def get(name): return bop.archive_file(name)
