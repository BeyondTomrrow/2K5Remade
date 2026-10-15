"""Read-only 1LKS transform audit against actual player Skel and animation.

python work/ilk_rest_probe.py
All results go to stdout. No matrices or quaternions are invented.
"""
import collections
import hashlib
import json
import math
import struct
import anim_decode_candidate as anim
from skel_probe import decode


def decode_rest(path):
    b=path.read_bytes()
    rows=anim.rig(path)
    names=struct.unpack_from('<I',b,12)[0]
    assert names==0x590
    n=len(rows)
    assert 0<n<=64
    assert not any(b[0x10+16*n:0x410])
    assert not any(b[0x410+6*n:0x590])
    for r in rows:
        i=r['index']
        x,y,z,parent,flag=struct.unpack_from('<3fHH',b,0x10+16*i)
        rotation=struct.unpack_from('<3h',b,0x410+6*i)
        assert all(math.isfinite(v) for v in (x,y,z))
        assert parent==(65535 if r['parent_index_candidate'] is None else r['parent_index_candidate'])
        assert r['node_index']==i
        r.update(local_translation=[x,y,z],parent_u16=parent,flag_u16=flag,
                 rotation_i16=list(rotation),translation_offset=0x10+16*i,
                 rotation_offset=0x410+6*i)
        expected_end=r['node_offset']+8+4*r['link_count_byte']
        next_node=min((x['node_offset'] for x in rows if x['node_offset']>r['node_offset']),default=len(b))
        assert expected_end==next_node
    return dict(file=path.name,size=len(b),sha256=hashlib.sha256(b).hexdigest(),
                transform_capacity=64,translation_base=0x10,rotation_base=0x410,
                names_base=0x590,unused_transform_slots_zero=True,
                complete_structural_coverage=True,bones=rows)


def audit():
    model=decode((anim.probe.ROOT/'outputs/plydata_extracted/00000004.tmd').read_bytes())
    byname={r['name']:r for r in model['bones']}
    rigs=[decode_rest(p) for p in sorted(anim.probe.ANIM.glob('*.1lk'))]
    summaries=[]
    for rig in rigs:
        matches=[]
        for r in rig['bones']:
            if r['name'] not in byname:continue
            m=byname[r['name']]
            sk=m['rotation_i16_raw']
            expected=[math.trunc(v/16) for v in sk]
            r['player_index']=m['index']
            r['skel_raw_rotation']=sk
            r['skel_rotation_div16_trunc_match']=r['rotation_i16']==expected
            r['skel_translation_exact_match']=r['local_translation']==m['local_translation']
            matches.append(r)
        summaries.append(dict(file=rig['file'],nodes=len(rig['bones']),matched_player_names=len(matches),
                              rotation_triples_match=sum(r['skel_rotation_div16_trunc_match'] for r in matches),
                              translation_triples_match=sum(r['skel_translation_exact_match'] for r in matches),
                              parent_fields_match_graph=True))
    primary=[r for r in rigs if r['file'] in ('00000011.1lk','00000019.1lk','0000001a.1lk')]
    total=sum(len(r['bones'])*3 for r in primary)
    assert total==192
    assert all(b['skel_rotation_div16_trunc_match'] and b['skel_translation_exact_match'] for r in primary for b in r['bones'])
    # Distinguish truncation toward zero from a signed arithmetic right shift.
    negatives=[(b['skel_raw_rotation'][i],b['rotation_i16'][i]) for r in primary for b in r['bones'] for i in range(3)
               if b['skel_raw_rotation'][i]<0 and b['skel_raw_rotation'][i]%16]
    assert negatives and all(raw//16!=stored for raw,stored in negatives)
    hand_fits=[]
    for filename in ('0000001d.dat','0000001e.dat','0000001f.dat','00000026.dat'):
        p=anim.probe.ANIM/filename
        bank=anim.raw_hand(p)
        samples=[anim.raw_hand(p,i)['triples_i16'] for i in range(bank['sample_count'])]
        fits=[]
        for rig in rigs:
            if len(rig['bones'])!=17:continue
            target=[byname[r['name']]['rotation_i16_raw'] for r in rig['bones']]
            # Compare against Skel rather than silently multiplying a coarser rest pose.
            ranked=sorted((sum(abs(pose[j][k]-target[j][k]) for j in range(1,17) for k in range(3)),i)
                          for i,pose in enumerate(samples))
            fits.append(dict(rig=rig['file'],best_l1_error_excluding_wrist=ranked[0][0],sample=ranked[0][1]))
        hand_fits.append(dict(file=filename,nearest_name_order_fits=fits,
                              warning='Pose-distance corroboration only, not a bank-to-rig resource linkage'))
    return dict(summary=summaries,primary_rotation_components_matched=total,
                primary_translation_components_matched=total,
                truncation_not_arithmetic_shift_examples=negatives[:5],
                hand_pose_correspondence=hand_fits,rigs=rigs,
                conclusion='1LKS stores 64 translation/parent slots and 64 coarse rotation triples. '
                           'Primary rigs exactly match Skel translations and trunc(Skel_raw/16). '
                           'This proves relative scale, not absolute angle units or Euler order.')


if __name__=='__main__':
    print(json.dumps(audit(),indent=2))
