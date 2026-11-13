"""Offline assembled pickup lookup/refusal checks. Never opens a game process.

Set FVR_BC2_PICKUP_TEST_EXE to run the optional exact local PE/code proof check.
The synthetic graph checks the diagnostic, not native networking or lifetimes.
"""
import copy
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import read_bc2_pickup_identity as pickup
import read_bc2_provision_target as provision


def source_blob():
    blob = bytearray(1264)
    struct.pack_into('<4I', blob, 0, 0x31504342, 1, 1264, 0)
    struct.pack_into('<IIQQ', blob, 16, 42, 0, 99, 99)
    blob[40:72] = bytes.fromhex(pickup.EXPECTED)
    name = 'G:\\fixture\\BFBC2Game.exe'.encode('utf-16-le')
    blob[72:72 + len(name)] = name
    struct.pack_into('<QQQqqq', blob, 1096, 0, 0, 8, 0, 0, 123456)
    struct.pack_into('<4I3Q', blob, 1144, 0x200000, 0x210000, 0x220000, 0x230000, 10, 11, 12)
    struct.pack_into('<3Q9I', blob, 1184, 99, 10, 12, 0x240000, 0x250000, 0x260000,
                     0, 0x270000, 0x280000, 0x290000, 0x2a0000, 0x2b0000)
    return bytes(blob)


def source():
    blob = source_blob()
    return pickup.decode_source(blob, provision.parse_source(blob))


def source_kernel(*blobs, attributes=0, size=1264, count=1264):
    """File API fixture: a later publication is available on the next read."""
    kernel = mock.Mock()
    kernel.CreateFileW.return_value = 123
    publications = iter(blobs or (source_blob(),))

    def file_information(handle, info_class, target, length):
        target[0] = attributes
        return True

    def file_size(handle, target):
        target._obj.value = size
        return True

    def read_file(handle, target, length, observed, overlapped):
        target.raw = next(publications)
        observed._obj.value = count
        return True

    kernel.GetFileInformationByHandleEx.side_effect = file_information
    kernel.GetFileSizeEx.side_effect = file_size
    kernel.ReadFile.side_effect = read_file
    return kernel


class Process:
    base = 0x400000

    def __init__(self):
        self.bytes = {}

    def put(self, at, data):
        self.bytes.update((at + index, value) for index, value in enumerate(data))

    def words(self, at, *values):
        self.put(at, struct.pack('<' + 'I' * len(values), *values))

    def read(self, at, size):
        try:
            return bytes(self.bytes[at + n] for n in range(size))
        except KeyError:
            raise ValueError('Unmapped fixture memory ' + hex(at)) from None


class Reflection:
    def __init__(self, memory, image=None):
        self.m = memory

    def verify(self):
        pass

    def owner(self):
        return dict(pickup=self.m.u32(0x100000), weak=0x100000)

    def reflected(self, at, name):
        if name == 'ServerPickupEntity':
            return dict(table=self.m.u32(at), typeInfo=0x1bf2710)
        return dict(name=name, address=at)

    def text(self, at, limit):
        result = bytearray()
        for n in range(limit):
            value = self.m.read(at + n, 1)[0]
            if not value:
                return result.decode('ascii')
            result.append(value)
        raise ValueError('Unterminated fixture string')


def fixture():
    p, s = Process(), source()
    c, o = s['client'], s['owner']
    p.put(c['player'] + 0xccd, b'\x08')
    for at, value in ((c['player'] + 0xc54, c['weak']), (c['weak'], c['soldier'] + 4),
                       (c['player'] + 0xc68, c['soldier']), (c['soldier'] + 0x220, c['player']),
                       (o['player'] + 0xc3c, o['actor']), (o['player'] + 0xc6c, o['actor']),
                       (o['actor'] + 0x220, o['player']), (o['actor'] + 0x2b4, o['inventory']),
                       (o['inventory'] + 0x14c, o['slot']), (o['item'] + 4, o['data']),
                       (c['weapon'] + 4, o['data']), (c['weapon'] + 0x3c, o['clientFiring0']),
                       (c['weapon'] + 0x40, o['clientFiring1']), (o['item'] + 0x10, o['serverFiring'])):
        p.words(at, value)
    p.words(0x1566b20, 0x300000)
    p.words(0x300000, 0x1417680, 2)
    p.words(0x30006c, 0x301000)
    p.words(c['player'] + 0x154, 0)
    p.words(o['player'] + 0x154, 0)
    p.words(0x301000, o['player'])
    p.words(o['actor'], 0x1424850)
    p.words(o['actor'] + 0x2b8, 0x302000, 0x302004)
    p.words(0x302000, o['item'])
    p.put(c['soldier'] + 0x114, b'\x00')
    p.words(c['soldier'] + 0x248, 0x303000)
    p.words(0x30314c, 0)
    p.words(c['soldier'] + 0x260, 0x304000, 0x304004)
    p.words(0x304000, c['weapon'])
    p.words(0x1566b4c, 0x310000)
    p.words(0x310208, 0x311000, 0x311004)
    p.words(0x311000, 0x320000)
    p.words(0x321610, o['player'])
    p.words(0x1571430, 0x330000)
    p.words(0x100000, 0x340000)
    p.words(0x340000, 0x1436040)
    p.words(0x340050, 0x143601c)
    cm, sm = 0x3312cc, 0x3212cc
    for manager in (cm, sm):
        p.words(manager, 0x147dff4)
        p.words(manager + 0x240, 1024, 10)
    p.words(cm + 0x1d0, 0x350000, 2, 1)
    p.words(0x350000, 0, 0x351000, 0xffffffff)
    p.words(0x351000, 7, 0x352000, 0)
    p.put(0x352004, struct.pack('<H', 7))
    p.words(0x352008, 0x340050)
    p.words(sm + 0x148, 1)
    p.words(sm + 0x140, 0x360000)
    p.words(0x360000, 0, 0, sm + 0x138, 0, 7, 0x361000)
    p.put(0x361004, struct.pack('<H', 7))
    p.words(0x361008, 0x370050, 123)
    p.words(0x370050, 0x1424330)
    p.words(0x370080, 123)
    p.words(0x142434c, 0x6ad7d0)
    p.words(0x370000, 0x1424358)
    p.words(0x1424360, 0x68feb0)
    p.words(0x370034, 0x380000, 0x380020)
    p.words(0x380000, 0x390000, 0, 0, 0xffffffff, 0, 0, 0, 0)
    p.words(0x390064, 0x391000)
    p.words(0x39000c, 0x392000)
    p.words(0x390040, 0x393000)
    p.words(0x391040, 0x394000)
    for at, value in ((0x392000, b'FixtureWeapon\0'), (0x393000, b'Objects/Weapons/Fixture\0'),
                       (0x394000, b'FixturePersistent\0')):
        p.put(at, value)
    return p, s


class LookupTests(unittest.TestCase):
    def run_observation(self, process, src):
        memory = pickup.Memory(process)
        return pickup.observe(memory, Reflection(memory), src)

    def test_bound_source_uses_one_verified_handle_observation(self):
        blob = source_blob()
        later = bytearray(blob)
        struct.pack_into('<Q', later, 1112, 9)
        struct.pack_into('<q', later, 1136, 123457)
        kernel = source_kernel(blob, bytes(later))
        directory = Path('G:/fixture')
        with mock.patch.object(provision.c, 'WinDLL', return_value=kernel, create=True), \
                mock.patch.object(provision, 'no_link_ancestors') as no_links, \
                mock.patch.object(Path, 'open', side_effect=AssertionError('Second source open')):
            result = pickup.read_bound_source(directory, provision.read_source)
        self.assertEqual(result, source())
        self.assertEqual(result['source_sha256'], pickup.sha(blob))
        no_links.assert_called_once_with(directory / 'campaign-provision-source.bin')
        kernel.CreateFileW.assert_called_once_with(
            str(directory / 'campaign-provision-source.bin'), 0x80000000, 1 | 4, None, 3, 0x00200000, None)
        kernel.GetFileInformationByHandleEx.assert_called_once()
        kernel.GetFileSizeEx.assert_called_once()
        kernel.ReadFile.assert_called_once()
        kernel.CloseHandle.assert_called_once_with(123)

    def test_source_reader_default_metadata_unchanged(self):
        kernel = source_kernel()
        with mock.patch.object(provision.c, 'WinDLL', return_value=kernel, create=True), \
                mock.patch.object(provision, 'no_link_ancestors'):
            self.assertEqual(provision.read_source(Path('G:/fixture')), provision.parse_source(source_blob()))
        kernel.ReadFile.assert_called_once()
        kernel.CloseHandle.assert_called_once_with(123)

    def test_bound_source_preserves_file_and_schema_refusals(self):
        malformed = bytearray(source_blob())
        malformed[0] ^= 1
        for options in ({'attributes': 0x10}, {'attributes': 0x400}, {'size': 1263}, {'count': 1263},
                        {'blob': bytes(malformed)}):
            with self.subTest(options=options):
                kernel = source_kernel(options.get('blob', source_blob()),
                                       **{key: value for key, value in options.items() if key != 'blob'})
                with mock.patch.object(provision.c, 'WinDLL', return_value=kernel, create=True), \
                        mock.patch.object(provision, 'no_link_ancestors'), self.assertRaises(ValueError):
                    pickup.read_bound_source(Path('G:/fixture'), provision.read_source)
                kernel.CloseHandle.assert_called_once_with(123)

    def test_bound_source_preserves_ancestor_refusal(self):
        with mock.patch.object(provision.c, 'WinDLL', create=True) as native, \
                mock.patch.object(provision, 'no_link_ancestors', side_effect=ValueError('Reparse path refused')), \
                self.assertRaisesRegex(ValueError, 'Reparse path refused'):
            pickup.read_bound_source(Path('G:/fixture'), provision.read_source)
        native.assert_not_called()

    def test_actual_source_schema_and_padding(self):
        s = source()
        self.assertEqual((s['client']['actorGeneration'], s['owner']['slot']), (10, 0))
        for offset in (0, 1160, 1184, 1244, 1248):
            blob = bytearray(source_blob())
            blob[offset] ^= 1
            with self.assertRaises(ValueError):
                pickup.decode_source(bytes(blob), provision.parse_source(bytes(blob)))

    def test_source_hash_join_and_changed_generation(self):
        blob, s = source_blob(), source()
        with self.assertRaisesRegex(ValueError, 'byte join'):
            pickup.decode_source(blob[:-1] + b'x', provision.parse_source(blob))
        changed = copy.deepcopy(s)
        changed['client']['equipGeneration'] += 1
        with self.assertRaisesRegex(ValueError, 'owner/session'):
            pickup.same_source(s, changed)

    def test_full_two_observation_pipeline(self):
        p, s = fixture()
        verified = []
        with mock.patch.object(pickup, 'verify_image') as code_check:
            report = pickup.collect(p, object(), Reflection, lambda: copy.deepcopy(s),
                                    lambda *args: verified.append(args))
        self.assertTrue(report['diagnostic_complete'])
        self.assertFalse(report['native_authority'])
        self.assertEqual(report['observation']['server_catalog']['count'], 1)
        self.assertEqual(report['observation']['server_map']['pickup'], 0x370000)
        self.assertEqual(len(verified), 4)
        self.assertEqual(code_check.call_count, 4)

    def test_stale_owner_and_connection_refused(self):
        for address, value in ((0x250220, 0xdead000), (0x321610, 0xdead000),
                               (0x301000, 0xdead000), (0x230004, 0xdead000)):
            p, s = fixture()
            p.words(address, value)
            with self.assertRaises(ValueError):
                self.run_observation(p, s)

    def test_hash_cycle_duplicate_and_caps(self):
        for address, value in ((0x351008, 0x351000), (0x3312cc + 0x1d4, pickup.MAX_BUCKETS + 1),
                               (0x3312cc + 0x1d8, pickup.MAX_NODES + 1), (0x351000, 8)):
            p, s = fixture()
            p.words(address, value)
            with self.assertRaises(ValueError):
                self.run_observation(p, s)

    def test_tree_cycle_ordering_depth_and_record_identity(self):
        for mutator in (
                lambda p: p.words(0x360000, 0, 0x360000, 0, 0, 9, 0x361000),
                lambda p: p.words(0x3212cc + 0x148, pickup.MAX_NODES + 1),
                lambda p: p.put(0x361004, struct.pack('<H', 8)),
                lambda p: p.words(0x370050, 0x143601c)):
            p, s = fixture()
            mutator(p)
            with self.assertRaises(ValueError):
                self.run_observation(p, s)
        p, _ = fixture()
        p.words(0x3212cc + 0x148, 65)
        for index in range(65):
            p.words(0x360000 + index * 32, 0x360000 + (index + 1) * 32, 0, 0, 0, index + 1, 0)
        with self.assertRaisesRegex(ValueError, 'depth'):
            pickup.server_network_object(pickup.Memory(p), 0x3212cc, 80)

    def test_connection_vector_and_catalog_bounds(self):
        for address, value in ((0x31020c, 0x311000 + 257 * 4), (0x370038, 0x380000 + 129 * 32),
                               (0x38000c, 1)):
            p, s = fixture()
            p.words(address, value)
            with self.assertRaises(ValueError):
                self.run_observation(p, s)

    def test_second_pass_identity_change_refused(self):
        p, s = fixture()
        count = 0

        def source_reader():
            nonlocal count
            count += 1
            if count == 4:
                p.words(0x351000, 9)
                p.put(0x352004, struct.pack('<H', 9))
                p.words(0x360010, 9)
                p.put(0x361004, struct.pack('<H', 9))
            return copy.deepcopy(s)
        with mock.patch.object(pickup, 'verify_image'):
            with self.assertRaisesRegex(ValueError, 'observations differ'):
                pickup.collect(p, object(), Reflection, source_reader, lambda *args: None)

    def test_read_pointer_overflow_and_short_reads(self):
        memory = pickup.Memory(Process())
        for at, size in ((0xffffffff, 2), (1, 4), (0x10000, 65537), (0x10000, 0)):
            with self.assertRaisesRegex(ValueError, 'bound'):
                memory.read(at, size)
        process = mock.Mock(base=0x400000, read=lambda at, size: b'x')
        with self.assertRaisesRegex(ValueError, 'Short'):
            pickup.Memory(process).read(0x10000, 4)

    def test_pe32_header_bounds_and_x64_refusal(self):
        data = bytearray(512)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<H', data, 0x84, 0x14c)
        struct.pack_into('<H', data, 0x98, 0x10b)
        struct.pack_into('<I', data, 0xb4, 0x400000)
        pickup.verify_pe32_header(data)
        struct.pack_into('<H', data, 0x98, 0x20b)
        with self.assertRaisesRegex(ValueError, 'PE32'):
            pickup.verify_pe32_header(data)
        struct.pack_into('<I', data, 0x3c, 0xffffff00)
        with self.assertRaises(ValueError):
            pickup.verify_pe32_header(data)

    @unittest.skipUnless(os.environ.get('FVR_BC2_PICKUP_TEST_EXE'), 'explicit local executable required')
    def test_exact_offline_executable_and_all_native_ranges(self):
        # Local file-backed mirror, no process API. Exercise the SAME proof loop
        # and section translation as production's pinned Image.read contract.
        data = Path(os.environ['FVR_BC2_PICKUP_TEST_EXE']).read_bytes()
        nt = struct.unpack_from('<I', data, 0x3c)[0]
        count, optional = struct.unpack_from('<H', data, nt + 6)[0], struct.unpack_from('<H', data, nt + 20)[0]
        sections = []
        for index in range(count):
            off = nt + 24 + optional + index * 40
            _, rva, size, raw = struct.unpack_from('<4I', data, off + 8)
            sections.append((rva, size, raw))

        class Image:
            base = 0x400000

            def __init__(self):
                self.data = data

            def read(self, rva, size):
                candidates = [raw + rva - start for start, length, raw in sections
                              if start <= rva and rva + size <= start + length]
                if len(candidates) != 1:
                    raise ValueError('PE file-backed RVA bound')
                return data[candidates[0]:candidates[0] + size]
        image = Image()
        mirror = mock.Mock(base=0x400000, read=lambda address, size: image.read(address - image.base, size))
        pickup.verify_image(image, pickup.Memory(mirror))


if __name__ == '__main__':
    unittest.main()
