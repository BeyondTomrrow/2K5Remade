"""Read-only probes for ANIMDATA. All interpretations below are candidates."""
from pathlib import Path
import collections
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
ANIM = ROOT / 'outputs/animdata_extracted'

def u32(b, o=0):
    return struct.unpack_from('<I', b, o)[0]

def inspect_clip(p):
    b = p.read_bytes()
    if len(b) < 260:
        return None
    em = u32(b) + 4
    if (em != 4 and em < 256) or em + 20 > len(b) or b[em:em+4] != b'BFME':
        return None
    # File bytes BFME, MCFB, MFFB; reversed FourCC spellings EMFB/BFCM/BFFM.
    count = u32(b, em+8) * u32(b, em+12)
    if em+16+count*16 > len(b):
        return None
    chunks = []
    for i in range(count):
        o = em+16+i*16
        tag = b[o+4:o+8].decode('ascii', 'replace')
        size, start = struct.unpack_from('<II', b, o+8)
        c = dict(type=u32(b,o),tag_bytes=tag, offset=start, size=size, in_bounds=start+size<=len(b))
        if tag == 'MCFB' and size >= 32 and start+size <= len(b):
            h = struct.unpack_from('<8I', b, start)
            c['header'] = h
            # Low halfword +4 matches the length of the offset table at +0x10.
            s = h[4]
            if s+4 <= size:
                frames = h[1] & 65535
                c['sample_count_candidate'] = frames
                if 0 < frames < 100000 and s+4*frames <= size:
                    offs = struct.unpack_from('<' + str(frames)+'I',b,start+s)
                    c['sample_offsets_valid'] = all(s+4*frames <= x < size for x in offs)
                    c['sample_offsets_first_last'] = [offs[0],offs[-1]]
                    c['sample_stride_hist'] = dict(collections.Counter(y-x for x,y in zip(offs,offs[1:])))
                    # +0x14 is a shared section; six u32 offsets/counts occur here.
            s = h[5]
            if s and s+24 <= size:
                vals = struct.unpack_from('<6I',b,start+s)
                c['shared_section'] = vals
                n, *ptrs = vals
                if 0 < n <= 1000 and all(0 <= x < size for x in ptrs):
                    c['channel_count_candidate'] = n
                    c['array_lengths'] = [y-x for x,y in zip(ptrs,ptrs[1:])]
                    if ptrs[4]+n*2 <= size:
                        remap = struct.unpack_from('<' + str(n)+'H',b,start+ptrs[4])
                        c['remap_is_permutation'] = sorted(remap) == list(range(n))
                        c['remap'] = remap
                        c['low_bytes_are_permutation'] = sorted(x&255 for x in remap)==list(range(n))
                        c['bit_width_sum_candidate'] = sum(x>>8 for x in remap)
                        c['packed_bytes_candidate'] = (sum(x>>8 for x in remap)+7)//8
                    c['arrays_i16_first_18'] = [struct.unpack_from('<18h',b,start+x) for x in ptrs[:4] if x+36<=size]
                    if ptrs[0]+2*n <= size and ptrs[2]+2*n <= size:
                        aa = struct.unpack_from('<'+str(n)+'h',b,start+ptrs[0])
                        zz = struct.unpack_from('<'+str(n)+'h',b,start+ptrs[2])
                        c['array0_gt_array2'] = sum(x>y for x,y in zip(aa,zz))
                        if c.get('low_bytes_are_permutation'):
                            c['mirror_baseline_mismatches'] = sum(zz[i] != ((aa[remap[i]&255] * (1 if i%3==0 else -1)+32768)%65536-32768) for i in range(n))
        chunks.append(c)
    tracks = []
    for i in range(15 if em >= 256 else 0):
        o = 16+i*16
        flags, start, end, data = struct.unpack_from('<IffI',b,o)
        n = flags >> 16
        tracks.append(dict(flags=flags, count=n, time_start=start, time_end=end, offset=data,
                           fits_28_byte_records=data+28*n<=em,
                           records=[dict(control_word=u32(b,data+j*28),floats=struct.unpack_from('<6f',b,data+j*28+4)) for j in range(min(n,3))] if data+28*n<=em else []))
    return dict(file=p.name,size=len(b),emfb_offset=em,container_header=struct.unpack_from('<4I',b,em),
                chunk_count=count,chunks=chunks,prefix_tracks=tracks)

def main():
    clips = [x for p in sorted(ANIM.iterdir()) if p.suffix in ('.dat','.app') and (x:=inspect_clip(p))]
    print('clips',len(clips))
    print('container header',collections.Counter(tuple(x['container_header'][1:]) for x in clips))
    print('chunk tags',collections.Counter(c['tag_bytes'] for x in clips for c in x['chunks']))
    print('channels',collections.Counter(c.get('channel_count_candidate') for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('invalid samples',sum(c.get('sample_offsets_valid') is False for x in clips for c in x['chunks']))
    print('array0>2',collections.Counter(c.get('array0_gt_array2') for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('array lengths',collections.Counter(tuple(c.get('array_lengths',[])) for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('remaps',collections.Counter(c.get('remap_is_permutation') for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('low byte permutation',collections.Counter(c.get('low_bytes_are_permutation') for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('mirrored baseline mismatches',collections.Counter(c.get('mirror_baseline_mismatches') for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB'))
    print('stride minus packed bytes by type',collections.Counter((c['type'],int(k)-c.get('packed_bytes_candidate',0)) for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB' for k in c.get('sample_stride_hist',{})))
    for name in ['0000002f.dat','00000267.dat','00000468.dat']:
        x=next((x for x in clips if x['file']==name),None)
        if x:
            print(name,[(c['type'],c['tag_bytes'],c.get('sample_count_candidate'),c.get('sample_stride_hist'),c.get('packed_bytes_candidate'),c.get('sample_offsets_first_last')) for c in x['chunks']])

if __name__=='__main__':
    main()
