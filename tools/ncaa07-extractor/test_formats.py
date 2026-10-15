"""Integration checks against actual NCAA 07 fixtures, plus corrupt-input tests."""
import json
import struct
import tempfile
import unittest
from pathlib import Path

from ncaa_formats import decode_model, export_glb, FormatError, sections
from ncaa07_tool import scan_file

ROOT = Path(__file__).resolve().parent
PLAYER = ROOT / 'outputs/plydata_extracted/00000004.tmd'
STADIUM = Path(r'H:\NCAA07_Extractor_Work\stadiums_raw\00000024.tmd')


class RealFixtureTests(unittest.TestCase):
    @unittest.skipUnless(PLAYER.exists(), 'Real player fixture unavailable')
    def test_clean_swap_profile_excludes_overlapping_variants_and_shadows(self):
        from ncaa_player_options import read_options,clean_selection
        data=PLAYER.read_bytes();options=read_options(data);selected,choices,_=clean_selection(data)
        self.assertEqual(len(options),40)
        self.assertEqual(options['Standard']['Default'],[201,206])
        self.assertEqual(options['Head']['HelmetStd'],[312,311])
        self.assertIn(313,selected);self.assertIn(314,selected)
        self.assertFalse(selected & {55,155,315,316,317,318,131+1,137,143,144})
        self.assertEqual(choices['LeftWrist'],'Bare')

    @unittest.skipUnless(Path(r'H:\NCAA07_Extractor_Work\field_raw\0000004a.tmd').exists(), 'Real field fixture unavailable')
    def test_field_geometry_and_diffuse_color_order(self):
        source=Path(r'H:\NCAA07_Extractor_Work\field_raw\0000004a.tmd')
        model=decode_model(source.read_bytes(),str(source))
        self.assertEqual(len(model['meshes']),18)
        # Source FVF_DIFFUSE bytes 08 D9 FB FF are ARGB DWORD 0xFFFBD908.
        colors=model['meshes'][1]['colors']
        self.assertIn((251/255,217/255,8/255,1),colors)

    @unittest.skipUnless(PLAYER.exists(), 'Real player fixture unavailable')
    def test_player_geometry_and_actual_bone_hierarchy(self):
        m = decode_model(PLAYER.read_bytes(), str(PLAYER))
        self.assertEqual(m['name'], 'pm2400ps2.ea3')
        self.assertEqual(len(m['meshes']), 137)
        self.assertEqual(sum(len(x['positions']) for x in m['meshes']), 16344)
        self.assertTrue(m['skeleton']['hierarchy_verified'])
        self.assertEqual(m['skeleton']['bones'][3]['name'], 'rknee')
        self.assertEqual(m['skeleton']['bones'][3]['parent'], 2)
        self.assertAlmostEqual(m['skeleton']['bones'][3]['local_translation'][1], -.4778, places=5)

    @unittest.skipUnless(STADIUM.exists(), 'Real stadium fixture unavailable')
    def test_stadium_textures_embedded_in_blender_glb(self):
        m = decode_model(STADIUM.read_bytes(), str(STADIUM))
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'alabama.glb'
            result = export_glb(m, p)
            self.assertEqual(result['parts'], 148)
            self.assertEqual(result['textures'], 73)
            self.assertEqual(result['warnings'], [])
            b = p.read_bytes()
            magic, version, length = struct.unpack_from('<4sII', b)
            self.assertEqual((magic, version, length), (b'glTF', 2, len(b)))
            jslen, = struct.unpack_from('<I', b, 12)
            doc = json.loads(b[20:20+jslen])
            self.assertEqual(len(doc['images']), 73)
            self.assertTrue(all(im['mimeType'] == 'image/png' for im in doc['images']))

    @unittest.skipUnless(PLAYER.exists(), 'Real player fixture unavailable')
    def test_bad_index_and_truncated_model_rejected(self):
        raw = PLAYER.read_bytes()
        with self.assertRaises(FormatError):
            decode_model(raw[:-1])
        b = bytearray(raw)
        r, table = sections(b)
        np, nc, sp, sc = r.unpack('<4I', table['Geom']['offset'])
        draw, = r.unpack('<I', np)
        ptr, = r.unpack('<I', draw+4)
        struct.pack_into('<H', b, ptr+16, 65535)
        with self.assertRaises(FormatError):
            decode_model(bytes(b))

    @unittest.skipUnless(Path(r'H:\07\NCAA Football 07  (axekin.com)\data\PLYDATA.DAT').exists(), 'Game archive unavailable')
    def test_terf_uses_real_count_not_alignment_padding(self):
        r = scan_file(Path(r'H:\07\NCAA Football 07  (axekin.com)\data\PLYDATA.DAT'))
        self.assertEqual(len(r.archive_entries), 35)
        self.assertEqual(len(r.compression_entries), 35)
        self.assertEqual(r.data_offset, 896)

    @unittest.skipUnless(PLAYER.exists(), 'Real player fixture unavailable')
    def test_experimental_rig_has_real_joints_uvs_and_normalized_weights(self):
        from ncaa_rig import export_preview
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory) / 'preview.glb'
            report = export_preview(PLAYER, p)
            self.assertTrue(report['experimental'])
            self.assertEqual(report['joint_count'], 75)
            self.assertLess(report['bind_identity_max_error_float64'], 1e-10)
            raw = p.read_bytes()
            jslen, = struct.unpack_from('<I', raw, 12)
            doc = json.loads(raw[20:20+jslen])
            self.assertTrue(doc['extras']['experimental'])
            self.assertEqual(len(doc['skins'][0]['joints']), 75)
            self.assertEqual(doc['nodes'][3]['name'], 'rknee')
            self.assertEqual(doc['nodes'][2]['children'], [3])
            attrs = doc['meshes'][0]['primitives'][0]['attributes']
            self.assertIn('TEXCOORD_0', attrs)
            self.assertIn('WEIGHTS_0', attrs)
            self.assertIn('JOINTS_0', attrs)


if __name__ == '__main__':
    unittest.main()
