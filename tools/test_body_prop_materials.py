import copy
import math
import struct
import unittest
from body_prop_materials import encode_materials, rigid_triangle_coordinates


class Materials(unittest.TestCase):
    def setUp(self):
        self.t = dict(width=1, height=1, color_space='linear', address='wrap', mips=[bytes([255, 63, 0, 255])])
        self.a = dict(key=[1, 2, 3], section=0, texture=0, skin=11, positions=12, color=[1, 1, 1, 1], uv=[[-.25, .25], [1.25, .25], [.25, 1.25]])

    def test_known_wire_layout(self):
        b = encode_materials([self.t], [self.a])
        self.assertEqual(b[:16], b'FVRTEX01' + struct.pack('<2I', 1, 1))
        self.assertEqual(struct.unpack_from('<3Q2I2QI', b, 44), (1, 2, 3, 0, 0, 11, 12, 3))
        self.assertEqual(struct.unpack_from('<6f', b, len(b)-24), (-.25, .25, 1.25, .25, .25, 1.25))

    def test_triangle_ownership_and_reflection(self):
        uv = [(1, 2), (3, 4), (5, 6), (7, 8)]
        self.assertEqual(rigid_triangle_coordinates([0, 1, 2, 1, 2, 3], [0]*3+[1]*3, 1, uv, reflect=True), [uv[1], uv[3], uv[2]])
        with self.assertRaises(ValueError):
            rigid_triangle_coordinates([0, 1, 2], [0, 1, 0], 0, uv, reflect=True)

    def test_mips_and_explicit_color_space(self):
        for change in ({'color_space': 'guess'}, {'mips': [bytes(3)]}, {'mips': [bytes(4), bytes(4)]}, {'width': 4096}):
            with self.assertRaises(ValueError):
                encode_materials([dict(self.t, **change)], [self.a])

    def test_ambiguity_and_invalid_coordinates(self):
        for uv in ([[math.nan, 0]]*3, [[65537, 0]]*3, [[0, 0]]):
            with self.assertRaises(ValueError):
                encode_materials([self.t], [dict(self.a, uv=uv)])
        with self.assertRaises(ValueError):
            encode_materials([self.t], [self.a, self.a])
        with self.assertRaises(ValueError):
            encode_materials([self.t, self.t], [self.a])


if __name__ == '__main__':
    unittest.main()
