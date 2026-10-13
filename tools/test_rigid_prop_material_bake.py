import unittest
import numpy as np
from rigid_prop_material_bake import sample_rgba, bake_triangle_material


class Bake(unittest.TestCase):
    def test_sampling_centers_wrap_clamp(self):
        im = np.array([[[255, 0, 0, 255], [0, 255, 0, 255]]], np.uint8)
        np.testing.assert_allclose(sample_rgba(im, [.25, .5])[:3], [1, 0, 0])
        np.testing.assert_allclose(sample_rgba(im, [-.25, .5])[:3], [0, 1, 0])
        np.testing.assert_allclose(sample_rgba(im, [-.25, .5], 'clamp')[:3], [1, 0, 0])
        np.testing.assert_allclose(sample_rgba(im, [.5, .5])[:3], [.5, .5, 0])

    def test_barycentric_mapping_and_separate_tiles(self):
        coords = {'a': [[[0, 0], [1, 0], [0, 1]], [[1, 1], [2, 1], [1, 2]]]}
        def material(q):
            return np.concatenate((q['a'], np.zeros(q['a'].shape[:-1]+(1,))), -1)
        texture, uv = bake_triangle_material(coords, material)
        im = np.frombuffer(texture['mips'][0], np.uint8).reshape(texture['height'], texture['width'], 4)
        for i, expected in enumerate(([0, 0], [1, 0], [0, 1], [1, 1], [1, 1], [1, 1])):
            np.testing.assert_allclose(sample_rgba(im, uv[i], 'clamp')[:2], expected, atol=1/255)
        self.assertEqual(len(uv), 6)
        self.assertEqual(len(texture['mips']), 3)

    def test_unknowns_and_inconsistent_inputs_reject(self):
        a = np.zeros((1, 3, 2))
        for channels in ({'a': a, 'b': np.zeros((2, 3, 2))}, {'a': a*np.nan}, {'a': np.zeros((0, 3, 2))}):
            with self.assertRaises(ValueError):
                bake_triangle_material(channels, lambda q: np.zeros((32, 32, 3)))
        with self.assertRaises(ValueError):
            bake_triangle_material({'a': a}, lambda q: np.full((32, 32, 3), np.nan))


if __name__ == '__main__':
    unittest.main()
