"""Focused deterministic cache joins and retained owner guards; no live process."""
import ast
import copy
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
TOOL = ROOT / 'tools/read_bc2_input_cache.py'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


observer = load('input_cache_observer_test', TOOL)
provision = load('input_cache_provision_test', ROOT / 'tools/read_bc2_provision_target.py')


class FakeImage:
    base = 0x400000

    def section(self, rva, size=1):
        if self.base+rva != 0x410038 or size != 4:
            raise ValueError('Synthetic image bounds')
        return (rva, 4, 0, 4, 0x20000000)

    def read(self, rva, size):
        return struct.pack('<I', 0x420000)[:size]


class FakeProcess:
    base = FakeImage.base

    def __init__(self):
        self.data, self.sequences, self.counts, self.reads = {}, {}, {}, []

    def word(self, address, value):
        for index, byte in enumerate(struct.pack('<I', value)):
            self.data[address+index] = byte

    def read(self, address, size):
        self.reads.append((address, size))
        if address in self.sequences:
            values = self.sequences[address]
            count = self.counts.get(address, 0)
            self.counts[address] = count+1
            return struct.pack('<I', values[min(count, len(values)-1)])
        try:
            return bytes(self.data[address+index] for index in range(size))
        except KeyError as error:
            raise ValueError('Unmapped synthetic read') from error


class CacheChecks(unittest.TestCase):
    def setUp(self):
        self.process, self.image = FakeProcess(), FakeImage()
        self.p = dict(context=0x500000, manager_table=0x450000,
                      controlled_getter=0x420000, router_table=0x0141c8e8,
                      cache_table=observer.EXPECTED_INPUT_CACHE_VTABLE)
        self.manager, self.player, self.weak = 0x20000000, 0x21000000, 0x22000000
        self.soldier, self.entry = 0x23000000, 0x24000000
        self.router, self.cache, self.array, self.other = 0x25000000, 0x26000000, 0x27000000, 0x28000000
        # Only the pointer read at player+c70 supplies the pair receiver.
        self.pair, self.second_pair = 0x2a000000, 0x29000000
        self.native = dict(client=dict(player=self.player, soldier=self.soldier, weak=self.weak))
        words = {
            self.p['context']+8: self.manager, self.manager: self.p['manager_table'],
            self.manager+0xb4: self.player, self.player: 0x410000, 0x410038: 0x420000,
            self.player+0xc54: self.weak, self.weak: self.soldier+4,
            self.player+0xc68: self.soldier, self.player+0xc60: 0,
            self.soldier+0x220: self.player, self.player+0xc6c: 0,
            self.soldier+0x7c: self.array, self.soldier+0x80: self.array+4,
            self.array: self.entry, self.entry+0x198: self.router,
            self.player+0xc74: self.cache, self.router: self.p['router_table'],
            self.cache: self.p['cache_table'], self.other: self.p['cache_table'],
            self.entry+0x1c4: self.player, self.player+0xc70: self.pair,
            # Gather words are deliberately not cache pointers.
            self.router+0x1a0: 0x0100ffff, self.router+0x1a4: 1,
        }
        for address, value in words.items():
            self.process.word(address, value)
        for pointer in (self.pair, self.second_pair, self.cache):
            self.process.word(pointer, self.p['cache_table'])
            self.process.word(pointer+0xd0, self.p['cache_table'])
            self.process.word(pointer+0x1a0, pointer)
            self.process.word(pointer+0x1a4, pointer+0xd0)
        self.process.data[self.player+0xccd] = 8
        self.journal = dict(entries=[], truncated=False)
        self.memory = provision.BudgetedProcess(observer.ScalarReadJournal(self.process, self.journal),
                                              None, observer.MAX_BYTES, observer.MAX_SECONDS)

    def resolve(self):
        return observer.resolve(self.memory, self.image, self.p, self.native)

    def expected_resolve_reads(self):
        return [(address, 1 if address == self.player+0xccd else 4) for address in (
            self.p['context']+8, self.manager, self.manager+0xb4, self.player,
            0x410038, self.player+0xccd, self.player+0xc54, self.weak,
            self.player+0xc68, self.player+0xc60, self.soldier+0x220,
            self.player+0xc6c, self.soldier+0x7c, self.soldier+0x80,
            self.array, self.entry+0x198, self.player+0xc74, self.router, self.cache,
            self.entry+0x1c4, self.player+0xc70, self.pair,
            self.pair+0x1a0, self.pair+0x1a4, self.pair, self.pair+0xd0,
            self.manager+0xb4, self.weak)]

    def observe(self):
        # This isolates added reads; unchanged PE/reflection verification is not mocked into evidence.
        def reflection(memory, image, obj, offset):
            return dict(type='ClientSoldierEntity' if offset == 0xfc else 'SyntheticEntry',
                        object=obj, dispatch_offset=offset)
        with patch.object(observer, 'reflected', reflection):
            return observer.observe(self.memory, self.image, self.p, self.native)

    def test_equal_and_unequal_cache_joins(self):
        self.assertEqual(self.p['cache_table'], 0x01419350)
        for pointer in (self.cache, self.pair):
            with self.subTest(pointer=hex(pointer)):
                self.process.word(self.player+0xc70, pointer)
                first, second = self.observe(), self.observe()
                observer.compare_observations(first, second)
                snapshots = first['cache_snapshots']+second['cache_snapshots']
                self.assertEqual(len(snapshots), 4)
                self.assertEqual(observer.cache_variation(snapshots), [])
                for sample in snapshots:
                    self.assertEqual(sample['entry_player'], self.player)
                    self.assertEqual(sample['dispatch_cache_pair'], pointer)
                    self.assertEqual(sample['dispatch_cache_pair_vtable'], 0x01419350)
                    self.assertEqual(sample['entry_router'], self.router)
                    self.assertEqual(sample['entry_router_vtable'], 0x0141c8e8)
                    self.assertEqual(sample['pair_current_cache'], pointer)
                    self.assertEqual(sample['pair_previous_cache'], pointer+0xd0)
                    self.assertEqual(sample['player_cache'], self.cache)
                    self.assertIs(sample['dispatch_cache_pair_equals_player_cache'], pointer == self.cache)
                    self.assertIs(sample['dispatch_cache_pair_equals_pair_current_cache'], True)
                    self.assertIs(sample['dispatch_cache_pair_equals_pair_previous_cache'], False)
                    self.assertIs(sample['player_cache_equals_pair_current_cache'], pointer == self.cache)
                    self.assertIs(sample['player_cache_equals_pair_previous_cache'], False)
        self.assertLess(self.memory.bytes_read, observer.MAX_BYTES)

    def test_cache_rotation_within_and_between_passes_is_recorded(self):
        self.process.sequences[self.pair+0x1a0] = [self.pair, self.pair+0xd0, self.pair+0xd0, self.pair]
        self.process.sequences[self.pair+0x1a4] = [self.pair+0xd0, self.pair, self.pair, self.pair+0xd0]
        first, second = self.observe(), self.observe()
        observer.compare_observations(first, second)
        snapshots = first['cache_snapshots']+second['cache_snapshots']
        self.assertEqual([s['pair_current_cache'] for s in snapshots],
                         [self.pair, self.pair+0xd0, self.pair+0xd0, self.pair])
        self.assertNotIn('dispatch_cache_pair', observer.cache_variation(snapshots))
        self.assertIn('pair_current_cache', observer.cache_variation(snapshots))
        self.assertIn('pair_previous_cache', observer.cache_variation(snapshots))
        for address in (self.entry+0x1c4, self.player+0xc70, self.pair+0x1a0, self.pair+0x1a4):
            self.assertEqual(self.process.reads.count((address, 4)), 4)

    def test_each_resolve_uses_its_new_pair_receiver_never_gather_offsets(self):
        self.process.sequences[self.player+0xc70] = [self.pair, self.second_pair, self.second_pair, self.pair]
        first, second = self.observe(), self.observe()
        observer.compare_observations(first, second)
        snapshots = first['cache_snapshots']+second['cache_snapshots']
        self.assertEqual([s['dispatch_cache_pair'] for s in snapshots],
                         [self.pair, self.second_pair, self.second_pair, self.pair])
        self.assertIn('dispatch_cache_pair', observer.cache_variation(snapshots))
        self.assertEqual(first['owner']['router'], self.router)
        for offset in (0x1a0, 0x1a4):
            self.assertNotIn((self.router+offset, 4), self.process.reads)
            for pointer in (self.pair, self.second_pair):
                self.assertEqual(self.process.reads.count((pointer+offset, 4)), 2)
        self.assertNotIn((0x0100ffff, 4), self.process.reads)

    def test_pointer_bounds_and_entry_owner(self):
        for address, message in ((self.entry+0x1c4, 'Entry player'),
                                 (self.player+0xc70, 'Dispatch cache pair'),
                                 (self.pair+0x1a0, 'Pair current cache'),
                                 (self.pair+0x1a4, 'Pair previous cache')):
            for value in (0, 0xffff, 0xffffffff):
                with self.subTest(address=hex(address), value=hex(value)):
                    self.setUp()
                    self.process.word(address, value)
                    with self.assertRaisesRegex(ValueError, '^'+message+' pointer bounds$'):
                        self.resolve()
        self.setUp()
        self.process.word(self.entry+0x1c4, self.player+0x1000)
        with self.assertRaisesRegex(ValueError, '^Entry player differs from current owner$'):
            self.resolve()

    def test_wrong_dispatch_type_including_router_is_refused(self):
        for vtable in (self.p['router_table'], self.p['cache_table']+4):
            with self.subTest(vtable=hex(vtable)):
                self.setUp()
                self.process.word(self.player+0xc70, self.other)
                self.process.word(self.other, vtable)
                with self.assertRaisesRegex(ValueError, '^Dispatch cache pair first-cache vtable:'):
                    self.resolve()

    def test_wrong_pair_cache_types_are_refused(self):
        for offset, label in ((0x1a0, 'current'), (0x1a4, 'previous')):
            with self.subTest(label=label):
                self.setUp()
                self.process.word(self.pair+offset, self.other)
                self.process.word(self.other, self.p['router_table'])
                with self.assertRaisesRegex(ValueError, '^Pair '+label+' cache vtable$'):
                    self.resolve()

    def test_pair_extent_bounds_before_any_pair_dereference(self):
        pointer = 0x100000000-0x1a4-3
        self.process.word(self.player+0xc70, pointer)
        with self.assertRaisesRegex(ValueError, '^Dispatch cache pair pointer bounds$'):
            self.resolve()
        self.assertEqual(self.process.reads[-1], (self.player+0xc70, 4))
        self.assertEqual(self.journal['entries'][-1],
                         dict(address=self.player+0xc70, length=4, hex=struct.pack('<I', pointer).hex()))

    def test_first_inputcache_type_does_not_substitute_for_pair_fields(self):
        self.process.word(self.player+0xc70, self.other)
        with self.assertRaisesRegex(provision.Incomplete, '^Unreadable scanned/typed region at 0x280001a0 size=4 '):
            self.resolve()
        self.assertEqual(self.process.reads[-1], (self.other+0x1a0, 4))
        self.assertNotIn((self.router+0x1a0, 4), self.process.reads)

    def test_original_player_cache_still_must_be_stable(self):
        for sequence, message in (([self.cache, self.other], '^Resolve tuple changed around dispatch observation$'),
                                  ([self.cache, self.cache, self.other, self.other], '^Two newly resolved observations differ$')):
            with self.subTest(sequence=sequence):
                self.setUp()
                self.process.sequences[self.player+0xc74] = sequence
                with self.assertRaisesRegex(ValueError, message):
                    observer.compare_observations(self.observe(), self.observe())

    def test_original_tuple_and_reflections_all_remain_compared(self):
        first = self.observe()
        self.assertEqual(set(first['owner']), set('manager player weak soldier controlled attached slot begin end entry router cache'.split()))
        for group in ('owner', 'controlled', 'entry'):
            for key in first[group]:
                with self.subTest(group=group, key=key):
                    second = copy.deepcopy(first)
                    second[group][key] = None
                    with self.assertRaisesRegex(ValueError, '^Two newly resolved observations differ$'):
                        observer.compare_observations(first, second)

    def test_entry_player_and_source_owner_changes_are_refused(self):
        self.process.sequences[self.entry+0x1c4] = [self.player, self.player+0x1000]
        with self.assertRaisesRegex(ValueError, '^Entry player differs from current owner$'):
            self.observe()
        self.setUp()
        self.native['client']['weak'] += 4
        with self.assertRaisesRegex(ValueError, '^Fresh source/Resolve owner differs$'):
            self.resolve()

    def test_original_resolve_refusals_and_unaffected_read_order_preserved(self):
        cases = (
            ('Manager vtable', 2, lambda: self.process.word(self.manager, 0)),
            ('Player getter', 5, lambda: self.p.update(controlled_getter=0x420004)),
            ('Not local player', 6, lambda: self.process.data.update({self.player+0xccd: 0})),
            ('Weak target', 8, lambda: self.process.word(self.weak, 3)),
            ('Not plain on-foot Resolve route', 10, lambda: self.process.word(self.player+0xc68, self.soldier+4)),
            ('Not plain on-foot Resolve route', 10, lambda: self.process.word(self.player+0xc60, self.other)),
            ('Soldier owner', 11, lambda: self.process.word(self.soldier+0x220, self.other)),
            ('Fresh source/Resolve owner differs', 11, lambda: self.native['client'].update(weak=self.weak+4)),
            ('Entry bounds', 14, lambda: self.process.word(self.player+0xc6c, 1)),
            ('Router/cache vtables', 19, lambda: self.process.word(self.router, self.p['cache_table'])),
            ('Router/cache vtables', 19, lambda: self.process.word(self.cache, self.p['router_table'])),
            ('Owner changed within Resolve', 27, lambda: self.process.sequences.update({self.manager+0xb4: [self.player, self.other]})),
            ('Owner changed within Resolve', 28, lambda: self.process.sequences.update({self.weak: [self.soldier+4, self.soldier+8]})),
        )
        for message, count, mutate in cases:
            with self.subTest(message=message, mutate=mutate):
                self.setUp()
                mutate()
                with self.assertRaises(ValueError) as after:
                    self.resolve()
                self.assertEqual(str(after.exception), message)
                expected = self.expected_resolve_reads()[:count]
                self.assertEqual(self.process.reads, expected)
                self.assertEqual(self.memory.bytes_read, sum(size for _, size in expected))

    def test_source_freshness_generation_and_session_guards_preserved(self):
        native = dict(identity=dict(pid=1), client=dict(player=1, soldier=2, weak=3,
                      weapon=4, actorGeneration=5, equipGeneration=6, space=7),
                      source_sequence=10, source_observed_ns=1000000000)
        for group in ('identity', 'client'):
            for key in native[group]:
                with self.subTest(group=group, key=key):
                    changed = copy.deepcopy(native)
                    changed[group][key] += 1
                    with self.assertRaisesRegex(ValueError, '^Source owner/session changed$'):
                        observer.same_source(native, changed)
        for key in ('source_sequence', 'source_observed_ns'):
            with self.subTest(key=key):
                changed = copy.deepcopy(native)
                changed[key] -= 1
                with self.assertRaisesRegex(ValueError, '^Source owner/session changed$'):
                    observer.same_source(native, changed)
        for age in (-1, 200000001):
            with self.subTest(age=age):
                blob = bytes(1144)+struct.pack('<4I3Q', *native['client'].values())
                meta = dict(source_sha256='synthetic', source_observed_ns=1000000000)
                fake = types.SimpleNamespace(read_source=lambda *args, **kwargs: (meta, blob),
                                             digest=lambda value: 'synthetic')
                with patch.object(observer.time, 'perf_counter_ns', return_value=1000000000+age):
                    with self.assertRaisesRegex(ValueError, '^Source older than 200ms or future dated$'):
                        observer.source(fake, HERE)

    def test_read_functions_retain_reviewed_gates_and_read_order(self):
        # Keep the reviewed read primitives and source/owner gates frozen.
        # Optional orchestration is covered by the behavioral checks above;
        # adding an opt-in reader must not invalidate unrelated primitives.
        tree = ast.parse(TOOL.read_text(encoding='utf-8'))
        unchanged = ('ScalarReadJournal', 'require', 'source', 'same_source',
                     'profile', 'exact', 'reflected', 'resolve',
                     'compare_observations', 'cache_variation')
        nodes = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef))
                 and node.name in unchanged]
        self.assertEqual(tuple(node.name for node in nodes), unchanged)
        fingerprint = hashlib.sha256('\n'.join(ast.dump(node) for node in nodes).encode()).hexdigest()
        self.assertEqual(fingerprint, '7974066a8facbe576a68f3769fa40a3927fc821a402164fa5f93205b28f6810b')
        self.assertEqual((observer.MAX_BYTES, observer.MAX_SECONDS, observer.MAX_SCALAR_READS, observer.MAX_SCALAR_BYTES),
                         (65536, 5, 512, 16))


    def test_journal_retains_pair_word_with_exact_refusal_and_no_extra_reads(self):
        actual = 0x12345678
        self.process.word(self.pair+0x1a0, self.other)
        self.process.word(self.other, actual)
        baseline = copy.deepcopy(self.process)
        baseline_memory = provision.BudgetedProcess(baseline, None, observer.MAX_BYTES, observer.MAX_SECONDS)
        with self.assertRaises(ValueError) as before:
            observer.resolve(baseline_memory, self.image, self.p, self.native)
        with self.assertRaises(ValueError) as after:
            self.resolve()
        self.assertEqual(str(before.exception), 'Pair current cache vtable')
        self.assertEqual(str(after.exception), str(before.exception))
        self.assertEqual(self.process.reads, baseline.reads)
        self.assertEqual(self.memory.bytes_read, baseline_memory.bytes_read)
        self.assertEqual(self.journal['entries'][-1],
                         dict(address=self.other, length=4, hex='78563412'))
        self.assertFalse(self.journal['truncated'])
        self.assertIn(dict(address=self.pair+0x1a0, length=4,
                           hex=struct.pack('<I', self.other).hex()), self.journal['entries'])
        self.assertNotIn((self.router+0x1a0, 4), self.process.reads)

    def test_journal_success_adds_no_reads(self):
        baseline = copy.deepcopy(self.process)
        baseline_memory = provision.BudgetedProcess(baseline, None, observer.MAX_BYTES, observer.MAX_SECONDS)
        self.assertEqual(self.resolve(), observer.resolve(baseline_memory, self.image, self.p, self.native))
        self.assertEqual(self.process.reads, baseline.reads)
        self.assertEqual(self.process.reads, self.expected_resolve_reads())
        self.assertEqual(len(self.journal['entries']), len(baseline.reads))

    def test_journal_cap_scalar_limit_and_no_extra_reads(self):
        for index in range(17):
            self.process.data[self.other+index] = index
        self.memory.read(self.other, 17)
        self.assertEqual(self.journal['entries'], [])
        for _ in range(512):
            self.memory.read(self.other, 16)
        self.assertEqual(len(self.journal['entries']), 512)
        self.assertFalse(self.journal['truncated'])
        self.memory.read(self.other, 4)
        self.assertTrue(self.journal['truncated'])
        self.assertEqual(len(self.journal['entries']), 512)
        self.assertEqual(self.process.reads, [(self.other, 17)]+[(self.other, 16)]*512+[(self.other, 4)])
        self.assertTrue(all(row['length'] == 16 and len(row['hex']) == 32 for row in self.journal['entries']))
        self.assertLess(len(json.dumps(self.journal, indent=2).encode()), 131072)

    def test_journal_preserves_post_read_clock_and_byte_refusals(self):
        self.memory.clock = lambda: self.memory.deadline-1 if not self.process.reads else self.memory.deadline
        with self.assertRaisesRegex(provision.Incomplete, '^Scan time cap reached$'):
            self.memory.u32(self.cache)
        self.assertEqual(self.journal['entries'],
                         [dict(address=self.cache, length=4, hex='50934101')])
        self.assertEqual(self.process.reads, [(self.cache, 4)])
        self.setUp()
        self.memory.max_bytes = 3
        with self.assertRaisesRegex(provision.Incomplete, '^Scan and typed-read byte cap reached$'):
            self.memory.u32(self.cache)
        self.assertEqual(self.journal['entries'], [])
        self.assertEqual(self.process.reads, [])

    def test_main_publishes_journal_on_success_and_refusal(self):
        for refused in (False, True):
            with self.subTest(refused=refused):
                self.setUp()
                if refused:
                    self.process.word(self.pair+0x1a0, self.other)
                    self.process.word(self.other, 0x12345678)
                self.process.close = lambda: None
                self.image.data = b'synthetic PE'
                native = dict(self.native, identity=dict(pid=1, executable=str(TOOL),
                              executable_sha256=observer.EXPECTED), source_sequence=1, source_observed_ns=1)
                published = []
                fake_provision = types.SimpleNamespace(
                    no_link_ancestors=lambda path: None,
                    load_dependencies=lambda path: ({}, None, {
                        'read_bc2': types.SimpleNamespace(Process=lambda *args: self.process),
                        'capture_reload_state': types.SimpleNamespace(Image=lambda path: self.image)}),
                    digest=lambda blob: observer.EXPECTED,
                    read_source=lambda path: native,
                    verify_process=lambda *args: None,
                    BudgetedProcess=provision.BudgetedProcess,
                    publish=lambda path, result: published.append(copy.deepcopy(result)))
                fake_spec = types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module: None))
                argv = ['--pid', '1', '--session-dir', str(HERE),
                        '--pins', str(TOOL), '--output', str(HERE/'synthetic-unused.json')]
                def reflection(memory, image, obj, offset):
                    return dict(type='ClientSoldierEntity' if offset == 0xfc else 'SyntheticEntry',
                                object=obj, dispatch_offset=offset)
                with patch.object(observer.importlib.util, 'spec_from_file_location', return_value=fake_spec), \
                     patch.object(observer.importlib.util, 'module_from_spec', return_value=fake_provision), \
                     patch.object(observer, 'profile', return_value=dict(self.p, signatures={})), \
                     patch.object(observer, 'source', return_value=native), \
                     patch.object(observer, 'reflected', reflection), contextlib.redirect_stdout(io.StringIO()):
                    code = observer.main(argv)
                result = published[0]
                self.assertEqual(code, 3 if refused else 0)
                self.assertIs(result['diagnostic_complete'], not refused)
                self.assertEqual(result['schema'], 'fvr.bc2.input-cache-observation.v1')
                self.assertTrue(result['read_only'])
                self.assertEqual(result['process_access'], 0x410)
                self.assertEqual(result['bounds']['process_read_bytes'], 65536)
                self.assertEqual(result['bounds']['process_read_seconds'], 5)
                self.assertEqual(result['bounds']['passes'], 2)
                self.assertEqual(result['bounds']['output_bytes'], 131072)
                self.assertFalse(result['native_authority'])
                self.assertFalse(result['native_functions_called'])
                self.assertFalse(result['unobserved_reuse_excluded'])
                self.assertTrue(result['external_reads_are_not_atomic'])
                self.assertEqual(len(result['scalar_read_journal']['entries']), len(self.process.reads))
                self.assertFalse(result['scalar_read_journal']['truncated'])
                if refused:
                    self.assertEqual(result['refusal'], 'Pair current cache vtable')
                    self.assertEqual(result['scalar_read_journal']['entries'][-1],
                                     dict(address=self.other, length=4, hex='78563412'))
                else:
                    self.assertNotIn('refusal', result)
                    self.assertEqual(len(result['cache_snapshots']), 4)
                    self.assertEqual(result['observation']['owner']['router'], self.router)


class DescriptorChecks(unittest.TestCase):
    def setUp(self):
        self.process = FakeProcess()
        self.entry, self.table, self.receiver = 0x24000000, 0x28000000, 0x29000000
        self.vtable = 0x450000
        self.root = self.table+16
        self.process.word(self.entry+0x24, self.table)
        self.process.data[self.entry+0x28] = 0
        self.process.data[self.table+0x1d] = 0xff
        self.row(self.root, self.receiver, flags=3, descendants=0)
        self.process.word(self.receiver, self.vtable)
        ranges = [(0x007d3ec3, 0x7a, True), (0x007bcdc0, 0x1bf, True),
                  (self.vtable, 0x110, False), (0x420000, 48, True)]
        for address, size, _ in ranges:
            for index in range(size):
                self.process.data[address+index] = 0x90
        for index, offset in enumerate(observer.DESCRIPTOR_VIRTUAL_SLOTS):
            self.process.word(self.vtable+offset, 0x420000+16*index)
        immutable = dict(self.process.data)
        class Image:
            base = 0x400000
            def section(_, rva, size=1):
                address = _.base+rva
                for start, length, code in ranges:
                    if start <= address and address+size <= start+length:
                        return (start-_.base, length, 0, length, 0x20000000 if code else 0x40000000)
                raise ValueError('Synthetic PE section bounds')
            def read(_, rva, size):
                _.section(rva, size)
                return bytes(immutable[_.base+rva+index] for index in range(size))
        self.image = Image()
        self.journal = dict(entries=[], truncated=False)
        self.memory = provision.BudgetedProcess(observer.ScalarReadJournal(self.process, self.journal),
                                              None, observer.MAX_BYTES, observer.MAX_SECONDS)

    def row(self, address, receiver, flags=1, descendants=0, reference=0):
        for offset, value in ((0, receiver), (4, reference), (8, flags)):
            self.process.word(address+offset, value)
        self.process.data[address+0xe] = descendants

    def observe(self):
        return observer.descriptor_targets(self.memory, self.image, self.entry)

    def test_selected_receiver_is_read_from_descriptor_not_entry_vtable(self):
        result = self.observe()
        self.assertEqual(result['root']['receiver'], self.receiver)
        self.assertEqual(result['root']['vtable'], self.vtable)
        self.assertEqual([row['offset'] for row in result['root']['targets']], [0xf4, 0xf8, 0x10c])
        self.assertNotIn((self.entry, 4), self.process.reads)
        self.assertFalse(result['invocation_observed'])
        self.assertFalse(result['lifetime_retirement_proven'])
        self.assertFalse(result['allocation_extent_proven'])

    def test_related_entry_uses_its_own_selector_and_table(self):
        other_entry, other_table = 0x2a000000, 0x2b000000
        self.process.data[self.table+0x1d] = 0
        self.process.word(self.table+16, other_entry)
        self.process.word(other_entry+0x24, other_table)
        self.process.data[other_entry+0x28] = 2
        self.row(other_table+48, self.receiver)
        result = self.observe()
        self.assertEqual(result['selection']['related_entry'], other_entry)
        self.assertEqual(result['root']['address'], other_table+48)

    def test_selector_and_address_caps_precede_table_indirection(self):
        for address, value, message in ((self.entry+0x28, 255, 'Descriptor selector'),
                                        (self.table+0x1d, 254, 'Related descriptor selector')):
            with self.subTest(address=address):
                self.setUp()
                self.process.data[address] = value
                with self.assertRaisesRegex(ValueError, message):
                    self.observe()
                self.assertNotIn((self.receiver, 4), self.process.reads)
        self.setUp()
        self.process.word(self.entry+0x24, 0xfffffff0)
        with self.assertRaisesRegex(ValueError, 'Descriptor table pointer bounds'):
            self.observe()
        self.assertNotIn((0x10000000d, 1), self.process.reads)

    def test_child_walk_skips_flattened_subtrees_and_never_recurses(self):
        self.process.data[self.root+0xe] = 3
        self.row(self.root+16, self.receiver, descendants=1)
        # The grandchild at+32 is deliberately unmapped.
        self.row(self.root+48, 0, flags=0)
        result = self.observe()
        self.assertEqual([row['address'] for row in result['children']], [self.root+16, self.root+48])
        self.assertFalse(result['recursive_descendants_observed'])
        self.assertFalse(result['children'][1]['targets_observed'])
        self.assertNotIn((self.root+32, 4), self.process.reads)

    def test_large_count_is_partial_and_malformed_subtree_refuses_before_receiver_read(self):
        self.process.data[self.root+0xe] = 255
        result = self.observe()
        self.assertEqual(result['children'], [])
        self.assertIn('cap', result['child_omission'])
        self.assertNotIn((self.root+16, 4), self.process.reads)
        self.setUp()
        self.process.data[self.root+0xe] = 1
        foreign = 0x2e000000
        self.row(self.root+16, foreign, descendants=255)
        with self.assertRaisesRegex(ValueError, 'Child descriptor subtree exceeds parent extent'):
            self.observe()
        self.assertNotIn((foreign, 4), self.process.reads)

    def test_foreign_or_changed_virtual_table_refuses(self):
        self.process.word(self.receiver, 0x2e000000)
        with self.assertRaisesRegex(ValueError, 'Synthetic PE section bounds'):
            self.observe()
        self.setUp()
        self.process.word(self.vtable+0xf4, 0x430000)
        with self.assertRaisesRegex(ValueError, 'Live PE bytes differ'):
            self.observe()
        self.assertNotIn((0x430000, 16), self.process.reads)

    def test_descriptor_identity_participates_in_original_two_pass_check(self):
        descriptor = self.observe()
        first = dict(owner={}, controlled={}, entry={}, descriptor_identity=descriptor['identity'])
        second = copy.deepcopy(first)
        observer.compare_observations(first, second)
        second['descriptor_identity']['root']['receiver'] += 4
        with self.assertRaisesRegex(ValueError, 'Two newly resolved observations differ'):
            observer.compare_observations(first, second)


class CommandChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='input-cache-test-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.output = self.directory / 'observation.json'
        self.pins = self.directory / 'pins.json'
        self.import_marker = self.directory / 'reader-imported.txt'

    def write_pins(self):
        # Synthetic modules can never attach to a process. The actual shared
        # dependency loader still verifies every hash and size before imports.
        files = {}
        keys = ['read_bc2', 'capture_reload_state', 'read_flynn_catalog',
                'read_stock_sniper_resources', 'read_weapon_resources',
                'variants', 'selectors'] + ['fixture_%02d' % n for n in range(18)]
        for key in keys:
            path = self.directory / (key+'.py')
            blob = b'# synthetic dependency\n'
            if key == 'read_bc2':
                blob = ("from pathlib import Path\n"
                        "Path(%r).write_text('imported', encoding='utf-8')\n"
                        "class Process:\n"
                        "    def __init__(self, *args):\n"
                        "        raise AssertionError('No process access in offline CLI test')\n"
                        % str(self.import_marker)).encode()
            path.write_bytes(blob)
            files[key] = dict(path=str(path), bytes=len(blob), sha256=provision.digest(blob))
        self.pins.write_text(json.dumps(dict(schema='fvr.bc2.provision-reader-pins.v1', files=files)),
                             encoding='utf-8')
        self.assertEqual(len(files), 25)
        return files

    def command(self, *extra):
        return subprocess.run([sys.executable, '-B', str(TOOL), '--pid', '1',
                               '--session-dir', str(self.directory), '--pins', str(self.pins),
                               '--output', str(self.output), *extra],
                              cwd=self.directory, capture_output=True, text=True, timeout=20)

    def test_assembled_cli_uses_sibling_api_and_publishes_source_refusal(self):
        self.write_pins()
        result = self.command()
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertTrue(self.import_marker.exists())
        report = json.loads(self.output.read_text(encoding='utf-8'))
        self.assertEqual(json.loads(result.stdout)['diagnostic_complete'], False)
        self.assertEqual(report['schema'], 'fvr.bc2.input-cache-observation.v1')
        self.assertIn('Read source file', report['refusal'])
        self.assertEqual(report['helper_sha256'], provision.digest(TOOL.read_bytes()))
        api = TOOL.with_name('read_bc2_provision_target.py')
        self.assertEqual(report['provision_api_sha256'], provision.digest(api.read_bytes()))
        self.assertEqual(report['dependency_manifest_sha256'], provision.digest(self.pins.read_bytes()))
        self.assertEqual(report['scalar_read_journal']['entries'], [])
        self.assertEqual(report['cache_snapshots'], [])
        self.assertNotIn('reads', report)
        self.assertFalse(report['native_authority'])
        self.assertFalse(report['native_functions_called'])
        self.assertTrue(report['external_reads_are_not_atomic'])
        self.assertFalse(self.output.with_suffix('.json.pending').exists())

    def test_all_25_pins_checked_before_reader_import(self):
        files = self.write_pins()
        Path(files['fixture_17']['path']).write_bytes(b'changed')
        result = self.command()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Pinned dependency changed: fixture_17', result.stderr)
        self.assertFalse(self.import_marker.exists())
        self.assertFalse(self.output.exists())

    def test_descriptor_opt_in_keeps_dependency_and_source_admission(self):
        self.write_pins()
        result = self.command('--descriptor-targets')
        self.assertEqual(result.returncode, 3, result.stderr)
        report = json.loads(self.output.read_text(encoding='utf-8'))
        self.assertTrue(report['descriptor_targets_requested'])
        self.assertEqual(report['descriptor_snapshots'], [])
        self.assertEqual(report['descriptor_bounds']['maximum_flattened_child_rows'], 8)
        self.assertIn('Read source file', report['refusal'])
        self.assertFalse(report['native_authority'])

    def test_missing_manifest_fails_before_source_or_process(self):
        result = self.command()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('FileNotFoundError', result.stderr)
        self.assertFalse(self.output.exists())

    def test_existing_output_and_pending_failure_remain_unchanged(self):
        self.write_pins()
        for suffix in ('', '.pending'):
            with self.subTest(suffix=suffix):
                path = self.output.with_name(self.output.name+suffix)
                path.write_bytes(b'earlier failure must be retained\n')
                result = self.command()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Output must be new', result.stderr)
                self.assertEqual(path.read_bytes(), b'earlier failure must be retained\n')
                self.assertFalse(self.import_marker.exists())
                path.unlink()

    def test_published_refusal_cannot_be_replaced(self):
        self.write_pins()
        result = self.command()
        self.assertEqual(result.returncode, 3, result.stderr)
        before = self.output.read_bytes()
        result = self.command()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Output must be new', result.stderr)
        self.assertEqual(self.output.read_bytes(), before)

    def test_cli_requires_explicit_pins_and_rejects_pointer_arguments(self):
        common = [sys.executable, '-B', str(TOOL), '--pid', '1', '--session-dir', str(self.directory),
                  '--output', str(self.output)]
        for args, message in (([], '--pins'),
                              (['--pins', str(self.pins), '--player', '0x21000000'], 'unrecognized arguments')):
            with self.subTest(args=args):
                result = subprocess.run(common+args, cwd=self.directory, capture_output=True,
                                        text=True, timeout=20)
                self.assertEqual(result.returncode, 2)
                self.assertIn(message, result.stderr)
                self.assertFalse(self.output.exists())

    def test_main_argv_pid_bounds_precede_dependency_loading(self):
        for pid in (0, -1, 0x100000000):
            with self.subTest(pid=pid), patch.object(observer.importlib.util, 'spec_from_file_location') as loader:
                with self.assertRaisesRegex(ValueError, '^PID bounds$'):
                    observer.main(['--pid', str(pid), '--session-dir', str(self.directory),
                                   '--pins', str(self.pins), '--output', str(self.output)])
                loader.assert_not_called()

    def test_output_limit_refuses_before_creating_pending_file(self):
        with self.assertRaisesRegex(ValueError, '^Snapshot output bound$'):
            provision.publish(self.output, dict(over_limit='x'*131072))
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.with_name(self.output.name+'.pending').exists())


if __name__ == '__main__':
    unittest.main(verbosity=2)
