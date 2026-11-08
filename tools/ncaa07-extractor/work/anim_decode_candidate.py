"""Evidence-driven ANIMDATA structural decoder. No glTF or pose claims.

Usage: python work/anim_decode_candidate.py audit
       python work/anim_decode_candidate.py hand 0000001e.dat 0
       python work/anim_decode_candidate.py body 00000468.dat 0 0
The body command exposes raw arrays/frames and an explicitly speculative bit
unpack, not certified rotations. No source files are modified.
"""
from pathlib import Path
import argparse
import collections
import json
import struct
import anim_probe as probe

def rig(p):
    b = p.read_bytes()
    assert b[:4] == b'1LKS'
    n = struct.unpack_from('<H',b,6)[0]
    node_base, names_base = struct.unpack_from('<II',b,8)
    assert names_base + 20*n == node_base
    nodes = [probe.u32(b,names_base+20*i) for i in range(n)]
    rows = []
    for i,o in enumerate(nodes):
        children_or_links, depth, index = struct.unpack_from('<BBH',b,o)
        parent_ptr = probe.u32(b,o+4)
        rows.append(dict(index=i, name=b[names_base+20*i+4:names_base+20*i+20].split(b'\0')[0].decode('ascii'),
                         node_offset=o, node_index=index, depth_candidate=depth,
                         link_count_byte=children_or_links, parent_pointer=parent_ptr,
                         parent_index_candidate=nodes.index(parent_ptr) if parent_ptr in nodes else None))
    return rows

def player_names():
    b = (probe.ROOT/'outputs/plydata_extracted/00000004.tmd').read_bytes()
    assert b[:4] == b'TMdl'
    count = struct.unpack_from('<H',b,8)[0]
    for i in range(count):
        o = 16+i*16
        if b[o:o+4] == b'Skel':
            off,size = struct.unpack_from('<II',b,o+4)
            n = probe.u32(b,off)
            assert n==75 and 24+16*n<=size
            return [b[off+24+16*j:off+40+16*j].split(b'\0')[0].decode('ascii') for j in range(n)]
    raise ValueError('No Skel')

def raw_hand(p, frame=0):
    b=p.read_bytes()
    assert p.name in ('0000001d.dat','0000001e.dat','0000001f.dat','00000026.dat')
    h=struct.unpack_from('<8I',b)
    count=h[1]&65535
    assert 0<=frame<count and h[4]+count*4==h[3]
    offsets=struct.unpack_from('<'+str(count)+'I',b,h[4])
    assert all(y-x==104 for x,y in zip(offsets,offsets[1:])) and len(b)-offsets[-1]==104
    flag=struct.unpack_from('<H',b,offsets[frame])[0]
    values=struct.unpack_from('<51h',b,offsets[frame]+2)
    return dict(file=p.name, sample_count=count, sample=frame, absolute_offset=offsets[frame],
                flags=flag, triples_i16=[values[i:i+3] for i in range(0,51,3)],
                interpretation='17 ordered triples; handedness, angle scale/order and bind conversion unconfirmed')

def body_frame(p, segment=0, frame=0):
    b=p.read_bytes()
    x=probe.inspect_clip(p)
    if not x: raise ValueError('Not an embedded BFME container')
    c=[c for c in x['chunks'] if c['tag_bytes']=='MCFB'][segment]
    base=c['offset']; h=c['header']; count=c['sample_count_candidate']
    assert 0<=frame<count
    offsets=struct.unpack_from('<'+str(count)+'I',b,base+h[4])
    # A logical sample table may repeat or revisit an existing physical packet.
    start=offsets[frame]; end=min((v for v in offsets if v>start),default=c['size'])
    arrays=[struct.unpack_from('<90h',b,base+o) for o in c['shared_section'][1:5]]
    descriptors=c['remap']; widths=[v>>8 for v in descriptors]
    width_bytes=(sum(widths)+7)//8
    # Use the tail only as a testable candidate; framing remains unresolved.
    skip=end-start-width_bytes
    payload=b[base+start:base+end]
    q=[]; pos=0; bits=int.from_bytes(payload[skip:],'little') if 0<=skip else 0
    for width in widths:
        q.append((bits>>pos)&((1<<width)-1)); pos+=width
    return dict(file=p.name,segment=segment,sample=frame,absolute_offset=base+start,
                frame_size=end-start,raw_hex=payload.hex(' '), arrays_i16=arrays,
                mirror_channels=[v&255 for v in descriptors], candidate_bit_widths=widths,
                candidate_prefix_bytes=skip,candidate_unsigned_values=q,
                warning='Tail-aligned LSB-first values are a hypothesis. No angles or transforms emitted.')

def audit():
    paths=sorted(p for p in probe.ANIM.iterdir() if p.suffix in ('.dat','.app'))
    clips=[x for p in paths if (x:=probe.inspect_clip(p))]
    chunks=[c for x in clips for c in x['chunks'] if c['tag_bytes']=='MCFB']
    groups=[]; group=[]; previous=65536
    for x in clips:
        index=x['chunks'][0]['header'][0]>>16
        if index<=previous:
            if group: groups.append(group)
            group=[]
        group.append(x); previous=index
    if group: groups.append(group)
    vaps=[p for p in sorted(probe.ANIM.glob('*.vap')) if p.name!='00000001.vap']
    assert len(vaps)==len(groups)==17
    links=[]; entries=0; pointer_bad=0; duration_bad=0; extra_count_bad=0; id_bad=0
    for p,g in zip(vaps,groups):
        b=p.read_bytes(); n=probe.u32(b,4)
        offsets=struct.unpack_from('<'+str(n)+'I',b,8)
        assert offsets[0]==8+4*n and list(offsets)==sorted(offsets)
        for i,o in enumerate(offsets):
            word=probe.u32(b,o)
            id_bad += (word&65535)!=i
            x=g[word>>16]
            track_chunks=[c for c in x['chunks'] if c['tag_bytes']=='MCFB']
            extra_count_bad += probe.u32(b,o+4)+1 != len(track_chunks)
            tag_ptr=probe.u32(b,o+24)
            pointer_bad += not(o<=tag_ptr<tag_ptr+64<=len(b))
            duration_bad += probe.u32(b,tag_ptr+8)!=track_chunks[-1]['sample_count_candidate']
        entries+=n
        links.append(dict(vap=p.name,entries=n,payloads=len(g),first=g[0]['file'],last=g[-1]['file']))
    names=player_names(); rigs={p.name:rig(p) for p in sorted(probe.ANIM.glob('*.1lk'))}
    body={r['name'] for r in rigs['00000011.1lk']}
    left={r['name'] for r in rigs['00000019.1lk']}
    right={r['name'] for r in rigs['0000001a.1lk']}
    union=body|left|right
    name_maps={k:[dict(r, player_index=names.index(r['name']) if r['name'] in names else None) for r in rows] for k,rows in rigs.items()}
    mirror_range_pass=0; nonmonotonic=[]
    for x in clips:
        b=(probe.ANIM/x['file']).read_bytes()
        for c in x['chunks']:
            if c['tag_bytes']!='MCFB': continue
            aa=struct.unpack_from('<90h',b,c['offset']+c['shared_section'][2])
            zz=struct.unpack_from('<90h',b,c['offset']+c['shared_section'][4])
            mirror_range_pass+=all(zz[i]==((aa[c['remap'][i]&255]*(1 if i%3==0 else -1)+32768)%65536-32768) for i in range(90))
            if any(int(k)<0 for k in c['sample_stride_hist']):
                nonmonotonic.append(dict(file=x['file'],chunk_offset=c['offset'],strides=c['sample_stride_hist']))
    return dict(payloads=len(clips),dat_payloads=sum(x['file'].endswith('.dat') for x in clips),
                mcfb_chunks=len(chunks),all_chunks_in_bounds=all(c['in_bounds'] for x in clips for c in x['chunks']),
                sample_tables_in_bounds=all(c['sample_offsets_valid'] for c in chunks),
                channels=dict(collections.Counter(c['channel_count_candidate'] for c in chunks)),
                low_byte_permutation_pass=sum(c['low_bytes_are_permutation'] for c in chunks),
                mirrored_array0_exact_pass=sum(c['mirror_baseline_mismatches']==0 for c in chunks),
                mirrored_array1_exact_pass=mirror_range_pass,nonmonotonic_sample_tables=nonmonotonic,
                metadata_entries=entries,metadata_id_mismatch=id_bad,metadata_count_mismatch=extra_count_bad,
                metadata_pointer_mismatch=pointer_bad,metadata_sample_count_mismatch=duration_bad,
                vap_groups=links,player_bone_count=len(names),body_names=len(body),left_names=len(left),right_names=len(right),
                union_names=len(union),missing_player_names=[s for s in names if s not in union],rig_name_maps=name_maps,
                raw_hands=[{k:v for k,v in raw_hand(probe.ANIM/n).items() if k!='triples_i16'} for n in ('0000001d.dat','0000001e.dat','0000001f.dat','00000026.dat')])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=('audit','hand','body'))
    parser.add_argument('file',nargs='?'); parser.add_argument('index',type=int,nargs='?',default=0)
    parser.add_argument('frame',type=int,nargs='?',default=0)
    args=parser.parse_args()
    if args.mode=='audit': result=audit()
    elif args.mode=='hand': result=raw_hand(probe.ANIM/args.file,args.index)
    else: result=body_frame(probe.ANIM/args.file,args.index,args.frame)
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
