import copy
import math
import struct
import unittest

from bc2_vertex_channels import decode_channel, mesh_declaration


def element(index, fmt, offset, slot=0, kind=0, step=0):
    return dict(semantic='TEXCOORD', index=index, dxgi_format=fmt,
                byte_offset=offset, input_slot=slot, input_class=kind, instance_step=step)


def fixture(half=True):
    section = dict(stride=48, vertices=3, vertex_offset=16,
                   position_format='Half4' if half else 'Float3')
    declaration = [element(0, 10 if half else 6, 0), element(1, 30, 8 if half else 12),
                   element(5, 10, 32), element(6, 2, 0, 1, 1, 1)]
    data = bytearray(16 + section['stride'] * section['vertices'])
    values = [(-.5, .25, 2., 7.), (4., -.75, .125, 0.), (1., 1., -1., -2.)]
    for n, value in enumerate(values):
        struct.pack_into('<4e', data, 16 + n * 48 + 32, *value)
    return section, declaration, bytes(data), values


class VertexChannels(unittest.TestCase):
    def test_half_channel_keeps_index_order_all_components_and_repeat_coordinates(self):
        section, declaration, data, values = fixture()
        result = decode_channel(data, section, declaration, 'texcoord', 5)
        self.assertEqual(result['values'], values)
        self.assertEqual(result['count'], 3)
        self.assertFalse(result['uv_assignment_verified'])
        self.assertEqual(len(mesh_declaration(section, declaration)['instances']), 1)

    def test_float_position_same_stride_requires_different_skin_offset(self):
        section, declaration, data, values = fixture(False)
        self.assertEqual(decode_channel(data, section, declaration, 'TEXCOORD', 5)['values'], values)
        declaration[1]['byte_offset'] = 8
        with self.assertRaises(ValueError): decode_channel(data, section, declaration, 'TEXCOORD', 5)

    def test_float_uv_pair_is_not_flipped_or_clamped(self):
        section, declaration, data, _ = fixture()
        declaration[2] = element(5, 16, 32)
        data = bytearray(data)
        expected = [(1.25, -2.5), (0., .5), (-3., 4.)]
        for n, values in enumerate(expected):struct.pack_into('<2f', data, 16 + n * 48 + 32, *values)
        self.assertEqual(decode_channel(bytes(data), section, declaration, 'TEXCOORD', 5)['values'], expected)

    def test_instance_and_missing_channels_are_not_read_from_mesh_bytes(self):
        section, declaration, data, _ = fixture()
        for index in (6, 7):
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode_channel(data, section, declaration, 'TEXCOORD', index)

    def test_conflicting_layouts_reject(self):
        section, declaration, _, _ = fixture()
        for fault in range(8):
            bad = copy.deepcopy(declaration)
            if fault == 0: bad.append(copy.deepcopy(bad[2]))
            if fault == 1: bad[2]['byte_offset'] = 44
            if fault == 2: bad[2]['byte_offset'] = 4
            if fault == 3: bad[2]['dxgi_format'] = 99
            if fault == 4: bad[2]['input_slot'] = 2
            if fault == 5: bad[2]['instance_step'] = 1
            if fault == 6: bad[0]['dxgi_format'] = 6
            if fault == 7: bad[3]['input_slot'] = 0
            with self.subTest(fault=fault), self.assertRaises(ValueError): mesh_declaration(section, bad)

    def test_bounds_and_nonfinite_values_reject(self):
        section, declaration, data, _ = fixture()
        for value in (math.nan, math.inf, -math.inf):
            bad = bytearray(data);struct.pack_into('<e', bad, 48, value)
            with self.subTest(value=value), self.assertRaises(ValueError):
                decode_channel(bytes(bad), section, declaration, 'TEXCOORD', 5)
        for key, value in (('vertices', 65537), ('vertices', 4), ('vertex_offset', -1), ('stride', True)):
            bad = dict(section);bad[key] = value
            with self.subTest(key=key,value=value), self.assertRaises(ValueError):
                decode_channel(data, bad, declaration, 'TEXCOORD', 5)
        with self.assertRaises(ValueError):decode_channel(data[:-1], section, declaration, 'TEXCOORD', 5)


if __name__ == '__main__':unittest.main()
