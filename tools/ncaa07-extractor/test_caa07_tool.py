import tempfile
import unittest
from pathlib import Path

from ncaa07_tool import find_directory, find_resources, parse_skeleton, scan_file


class ToolTests(unittest.TestCase):
    def test_terf_directory_and_tags(self):
        data = b"TERF" + b"\0" * 60 + b"DIR1" + (32).to_bytes(4, "little") + b"TMdl" + b"a" * 12 + b"Geom" + b"b" * 12
        self.assertEqual(find_directory(data), (64, 32))
        resources = find_resources(data, "fixture")
        self.assertEqual([r.tag for r in resources], ["TMdl", "Geom"])
        self.assertTrue(all(r.confidence == "high" for r in resources))


    def test_unknown_file_is_not_labeled(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "x.dat"
            path.write_bytes(b"not a game container")
            report = scan_file(path)
            self.assertEqual(report.container, "unknown")
            self.assertEqual(report.resources, [])

    def test_verified_skeleton_name_table(self):
        data = bytearray(0x18 + 2 * 16)
        data[0:4] = (2).to_bytes(4, "little")
        data[0x18:0x20] = b"root\0\0\0\0"
        data[0x28:0x30] = b"child\0\0\0"
        skeleton = parse_skeleton(bytes(data), 0, len(data))
        self.assertEqual(skeleton["bone_count"], 2)
        self.assertEqual(skeleton["bone_names"], ["root", "child"])
        self.assertTrue(skeleton["names_verified"])
