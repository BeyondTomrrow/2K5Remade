"""Real-disc roundtrip and staging tests. Writes only outputs/team-studio/tests."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from PIL import Image
import studio as S
import studio_assets as A
import game_assets as G
import build_team
import rosters

OUT=S.ROOT/'outputs/team-studio/tests'

class Studio(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.arc=S.archive();cls.source=cls.arc.read('25h0.iff');cls.digest=hashlib.sha256(cls.source).hexdigest()
        cls.rows=A.textures(cls.source);OUT.mkdir(parents=True,exist_ok=True)

    def test_real_archive_and_all_texture_types(self):
        self.assertGreater(len(self.rows),50)
        self.assertTrue(any(r['kind']=='TSET' and r['name']=='jersey00' for r in self.rows))
        self.assertTrue(any(r['kind']=='TXTR' and r['name']=='helmet00' for r in self.rows))
        self.assertTrue(all('image' in r for r in self.rows))

    def test_tset_write_readback_and_unchanged_other_chunks(self):
        row=next(r for r in self.rows if r['name']=='jersey00')
        image=Image.new('RGBA',(row['width'],row['height']),(40,180,160,255))
        result=A.replace_texture(self.source,row['id'],image)
        old=G.T.parse_chunks(self.source,allow_trailing=True);new=G.T.parse_chunks(result,allow_trailing=True)
        self.assertEqual(len(old),len(new))
        for a,b in zip(old,new):
            if a.index!=row['chunk']:self.assertEqual(self.source[a.offset:a.end_offset],result[b.offset:b.end_offset])
        c=new[row['chunk']];decoded,infos=A.texture_infos(result,c)
        pixels=G.T.texture_to_rgba(decoded,c,infos[row['reference']])
        self.assertEqual(pixels,bytes((40,180,160,255))*(row['width']*row['height']))
        self.assertEqual(hashlib.sha256(self.arc.read('25h0.iff')).hexdigest(),self.digest)

    def test_mud_palette_does_not_change_clean_image(self):
        clean=next(r for r in self.rows if r['name']=='jersey00');mud=next(r for r in self.rows if r['name']=='jersey00_mud')
        result=A.replace_texture(self.source,mud['id'],Image.new('RGBA',(mud['width'],mud['height']),(60,50,30,255)))
        rows=A.textures(result)
        self.assertEqual(next(r['image'] for r in rows if r['id']==clean['id']),clean['image'])

    def test_wrong_dimensions_and_paths_fail(self):
        row=self.rows[0]
        with self.assertRaises(ValueError):A.replace_texture(self.source,row['id'],Image.new('RGBA',(7,9)))
        with self.assertRaises(ValueError):S.project_path('../original')
        with self.assertRaises(ValueError):S.asset_path('test','../../studio.py')

    def test_original_model_topology_and_head_anchor(self):
        data=S.model_data();self.assertGreater(len(data['meshes']),10)
        self.assertIn('jersey00',{m['texture'] for m in data['meshes']})
        self.assertIn('pants00',{m['texture'] for m in data['meshes']})
        helmet=next(m for m in data['meshes'] if m['name']=='HI_HELMET_A')
        ys=helmet['positions'][1::3]
        # Source arrays include unused vertices; restrict bounds to actual draw indices.
        self.assertGreater(sum(ys[i] for i in set(helmet['indices']))/len(set(helmet['indices'])),.5)
        for m in data['meshes']:self.assertLess(max(m['indices']),len(m['positions'])//3)

def staged_build():
    arc=S.archive();source=arc.read('25h0.iff');digest=hashlib.sha256(source).hexdigest()
    rows=A.textures(source);jersey=next(r for r in rows if r['name']=='jersey00')
    custom=OUT/'test-jersey.png';Image.new('RGBA',(jersey['width'],jersey['height']),(30,110,160,255)).save(custom)
    primary=[30,110,160];white=[245,245,245]
    uniform=dict(jersey_color=primary,jersey_trim=white,pants_color=white,number_color=white,
        number_outline=primary,helmet_color=primary,facemask_color=[180,180,180],
        helmet_stripe=dict(on=True,color=white,width=18),pant_stripe=dict(on=True,color=primary,width=12))
    spec=dict(city='Studio',nickname='Verification',abbreviation='STU',folder='StudioVerification',base='25',
        base_colors=S.bootstrap()['colors']['25'],colors=[primary,white],roster=rosters.pick_53(rosters.generate(75,seed=20261029)),
        uniforms=dict(home=uniform,away=copy.deepcopy(uniform)),studio_version=1,
        texture_overrides={'25h0.iff|'+jersey['id']:str(custom)})
    destination=OUT/('pack-'+__import__('uuid').uuid4().hex[:8])
    path=build_team.build(spec,S.ROOT,print,output_dir=destination)
    info=json.loads((path/'team.json').read_text());assert len((path/'roster.csv').read_text().splitlines())==54
    output=(path/'files'/(info['code']+'h0.iff')).read_bytes()
    c=G.T.parse_chunks(output,allow_trailing=True)[jersey['chunk']];body,infos=A.texture_infos(output,c)
    actual=G.T.texture_to_rgba(body,c,infos[jersey['reference']])
    assert actual==bytes((30,110,160,255))*(jersey['width']*jersey['height'])
    assert hashlib.sha256(arc.read('25h0.iff')).hexdigest()==digest
    receipt=dict(status='PASS',pack=str(path),files=len(list((path/'files').glob('*.iff'))),
        rosterRows=53,customTextureReadback='PASS',sourceUnchanged='PASS',gameTest='NOT RUN')
    (OUT/'build-result.json').write_text(json.dumps(receipt,indent=2))
    print(json.dumps(receipt))

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    if '--build' in sys.argv:staged_build()
    else:unittest.main(verbosity=2)
