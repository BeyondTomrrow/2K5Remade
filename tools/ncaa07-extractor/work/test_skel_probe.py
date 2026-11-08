"""Regression tests pinned to the real extracted fixtures, never synthetic rigs."""
import math
from pathlib import Path
import unittest
from skel_probe import audit, decode
from skel_rotation_experiment import world_positions, euler
from skel_geom_bridge import bridge

FIXTURES = Path(__file__).resolve().parents[1] / 'outputs/plydata_extracted'
RIG75 = 'a2434c2b43e430ed96ab8088f3575de7aa1bdc336ff16ecf34519d1faf18b500'
RIG32 = '308e10df907942563070e0ddd2639fc8f6e157b3f4bf268cefa3eaea96955c1d'


class RealFixtureTests(unittest.TestCase):
    def test_target_bytes_and_fields(self):
        model = decode((FIXTURES / '00000003.tmd').read_bytes())
        self.assertEqual(model['file_sha256'],
                         '690263124a570e7d5dbdb6ac11b80bd09ccfa231af8ed77df04f6640b852aa20')
        self.assertEqual(model['header_u32'], [75, 152, 1360, 2560, 3024, 0])
        self.assertEqual(model['skel_size'], 3796)
        by_name = {b['name']: b for b in model['bones']}
        for child, parent in [('main', 'IDEAL_OUT'), ('rhip', 'main'),
                              ('rknee', 'rhip'), ('rankle', 'rknee'),
                              ('low_torso', 'main'), ('rclavicle', 'up_torso'),
                              ('rringLow', 'rwrist'), ('RFOOTBALL', 'rwrist'),
                              ('LFOOTBALL', 'lwrist'), ('eye_knuckle', 'neckhi'),
                              ('lthumbEnd', 'lthumbHi')]:
            self.assertEqual(by_name[child]['parent_name'], parent)
        self.assertEqual(by_name['main']['parent_raw'], 0)
        self.assertEqual(by_name['main']['flag_raw'], 1)
        self.assertEqual(by_name['rhip']['parent_raw'], 1)
        self.assertEqual(by_name['rankle']['rotation_i16_raw'], [20700, 440, -7])
        self.assertAlmostEqual(by_name['rknee']['local_translation'][1], -0.47780001163482666)

    def test_all_fixture_groups_and_negative_controls(self):
        result = audit(sorted(FIXTURES.glob('*.tmd')))
        self.assertEqual(result['fixture_count'], 26)
        self.assertEqual({k: len(v) for k, v in result['normalized_groups'].items()},
                         {RIG75: 10, RIG32: 16})
        self.assertEqual(result['cross_rig_comparisons'][0]['common_names'], 30)
        self.assertEqual(result['cross_rig_comparisons'][0]['flag_differences'], ['neckhi'])
        self.assertEqual(len(result['negative_controls']), 3)

    def test_relocated_32_bone_rig(self):
        first = decode((FIXTURES / '00000011.tmd').read_bytes())
        moved = decode((FIXTURES / '00000015.tmd').read_bytes())
        self.assertEqual(first['header_u32'], [32, 152, 672, 1184, 1376, 0])
        self.assertEqual(moved['header_u32'], [32, 168, 688, 1200, 1392, 0])
        self.assertEqual(moved['skel_offset'] - first['skel_offset'], 16)
        self.assertNotEqual(first['skel_sha256'], moved['skel_sha256'])
        self.assertEqual(first['normalized_skel_sha256'], moved['normalized_skel_sha256'])

    def test_euler_orders_remain_ambiguous(self):
        bones = decode((FIXTURES / '00000003.tmd').read_bytes())['bones']
        xyz = world_positions(bones, lambda raw: euler(raw, 'XYZ', math.tau / 65536))
        zyx = world_positions(bones, lambda raw: euler(raw, 'ZYX', math.tau / 65536))
        # Both hypotheses are symmetric but yield materially different wrist depths.
        for candidate in (xyz, zyx):
            self.assertLess(abs(candidate['rwrist'][0] + candidate['lwrist'][0]), 1e-5)
        self.assertGreater(abs(xyz['rwrist'][2] - zyx['rwrist'][2]), 0.09)

    def test_geometry_palettes_across_real_fixtures(self):
        total = 0
        for path in sorted(FIXTURES.glob('*.tmd')):
            result = bridge(path.read_bytes())
            total += result['selector_count']
            if path.name == '00000003.tmd':
                self.assertEqual(result['nodes'][1]['cluster_use_counts'], {6: 441, 4: 9})
                self.assertEqual(result['nodes'][0]['palettes'][0][0]['bone_name'], 'neckhi')
            if path.name == '00000004.tmd':
                self.assertEqual(result['active_nodes'], 137)
                multi = [n for n in result['nodes'] if n['palette_count'] > 1]
                self.assertEqual([(n['node'], n['palette_count']) for n in multi], [(55, 7)])
        self.assertEqual(total, 78036)


if __name__ == '__main__':
    unittest.main()
