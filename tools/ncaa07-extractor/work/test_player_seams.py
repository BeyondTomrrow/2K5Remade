"""Real-fixture regressions for batch carry-forward and the two rigid hip matches."""
import unittest
import numpy as np
from skel_seam_fit import ROOT, load, bone_matrices, transformed


class PlayerSeamTests(unittest.TestCase):
    def test_all_fixture_batch_resolution(self):
        total = 0
        for path in sorted((ROOT/'outputs/plydata_extracted').glob('*.tmd')):
            model = load(path)
            self.assertEqual(sum(n['vertices_with_multiple_clusters'] for n in model['draw_audit']), 0)
            total += len(model['rows'])
        self.assertEqual(total, 78036)

    def test_fixed_hip_seams_distinguish_euler_orders(self):
        model = load(ROOT/'outputs/plydata_extracted/00000004.tmd')
        lookup = {(int(row[0]),int(row[1])): i for i,row in enumerate(model['rows'])}
        pairs = [((55,255),(206,83)), ((55,282),(206,60))]
        errors = {}
        for order in ['XYZ','XZY','YXZ','YZX','ZXY','ZYX']:
            points = transformed(model, bone_matrices(model, order))
            errors[order] = np.sqrt(np.mean([np.linalg.norm(points[lookup[a]]-points[lookup[b]])**2 for a,b in pairs]))
        self.assertLess(errors['ZYX'], 3e-5)
        self.assertGreater(min(e for o,e in errors.items() if o!='ZYX'), 9*errors['ZYX'])
        self.assertGreater(errors['XYZ'], .006)

    def test_file04_seven_batches_need_carried_slots(self):
        model = load(ROOT/'outputs/plydata_extracted/00000004.tmd')
        node = next(n for n in model['draw_audit'] if n['node']==55)
        self.assertEqual(node['palette_count'], 7)
        self.assertEqual(sorted(node['draws_per_batch']), list(range(7)))
        self.assertEqual(node['vertices_with_multiple_clusters'], 0)


if __name__ == '__main__':
    unittest.main()
