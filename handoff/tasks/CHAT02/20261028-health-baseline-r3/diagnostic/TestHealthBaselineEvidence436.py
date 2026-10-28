"""Offline baseline-gate regressions: no process access, native calls or input."""
from copy import deepcopy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from HealthBaselineEvidence436 import publish_baseline


ROOT = Path(__file__).resolve().parent
PID = 4321
PROCESS_START = 134000000000000001
START_TICK = 987654321
SOURCE_HASH = '0123456789abcdef' * 4


def snapshot(client, server, maximum=100.0):
    raw = [struct.pack('<f', value).hex()
           for value in (client, server, maximum)]
    return dict(monotonic_ns=45678901234, windows_tick_ms=START_TICK - 3000,
                owner=dict(client_manager=0x100000, client_player=0x110000,
                           weak=0x120000, client_actor=0x130000, player_id=7,
                           server_manager=0x140000, server_player=0x150000,
                           server_actor=0x160000, player_array=0x170000,
                           capacity=16, soldier_data=0x180000,
                           client_health_table=0x1437924,
                           server_health_table=0x1424824),
                client_health=client, server_health=server,
                static_max_health=maximum, raw_hex=raw,
                repeated_equal=True,
                static_config_bound_pass=(0 <= client <= maximum and
                                          0 <= server <= maximum),
                native_max_health_evaluated=False,
                both_positive=client > 0 and server > 0,
                both_zero=client == 0 and server == 0,
                health_copies_equal=raw[0] == raw[1])


class HealthBaselineEvidenceTests(unittest.TestCase):
    def setUp(self):
        # Explicit G:-workspace scratch; no dependency on the OS temp drive.
        self.scratch = tempfile.TemporaryDirectory(prefix='baseline-test-', dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.ready = Path(self.scratch.name) / 'health-ready.json'
        self.report = dict(source_sha256=SOURCE_HASH, pid=PID,
                           process_start_filetime=PROCESS_START,
                           actual_death=False, samples=[], errors=[])

    def publish(self, candidate):
        publish_baseline(candidate, self.report, self.ready, pid=PID,
                         process_start_filetime=PROCESS_START,
                         start_tick_ms=START_TICK)

    def assert_rejected(self, candidate, failures):
        original = deepcopy(candidate)
        with self.assertRaisesRegex(ValueError,
                                    '^Positive coherent pre-action health required$'):
            self.publish(candidate)
        self.assertEqual(self.report['baseline_candidate'], original)
        self.assertEqual(self.report['failed_checks'], failures)
        self.assertEqual(candidate, original)
        self.assertNotIn('baseline', self.report)
        self.assertFalse(self.ready.exists())
        self.assertFalse(self.ready.with_suffix('.tmp').exists())
        self.assertFalse(self.report['actual_death'])
        self.assertEqual(self.report['pid'], PID)
        self.assertEqual(self.report['process_start_filetime'], PROCESS_START)
        # Evidence is serializable with its exact values, identity and raw bytes.
        self.assertEqual(json.loads(json.dumps(self.report, allow_nan=False))
                         ['baseline_candidate'], original)
        candidate['owner']['client_actor'] += 4
        candidate['raw_hex'][0] = '00000000'
        self.assertEqual(self.report['baseline_candidate'], original)

    def test_zero_health_retained_without_admission(self):
        self.assert_rejected(snapshot(0.0, 0.0), ['both_positive'])

    def test_unequal_positive_health_retained_without_admission(self):
        self.assert_rejected(snapshot(70.0, 65.0), ['health_copies_equal'])

    def test_equal_above_static_bound_retained_without_admission(self):
        self.assert_rejected(snapshot(120.0, 120.0), ['static_config_bound_pass'])

    def test_zero_and_positive_reports_both_failures(self):
        self.assert_rejected(snapshot(0.0, 65.0),
                             ['both_positive', 'health_copies_equal'])

    def test_equal_negative_reports_both_failures(self):
        self.assert_rejected(snapshot(-1.0, -1.0),
                             ['both_positive', 'static_config_bound_pass'])

    def test_unequal_above_bound_reports_both_failures(self):
        self.assert_rejected(snapshot(120.0, 121.0),
                             ['health_copies_equal', 'static_config_bound_pass'])

    def test_all_failed_gates_retained(self):
        self.assert_rejected(snapshot(-1.0, 101.0),
                             ['both_positive', 'health_copies_equal',
                              'static_config_bound_pass'])

    def test_supplied_verified_gate_is_not_recomputed_or_relaxed(self):
        candidate = snapshot(75.0, 75.0)
        candidate['static_config_bound_pass'] = False
        self.assert_rejected(candidate, ['static_config_bound_pass'])

    def test_accepted_ready_payload_matches_original_r2_protocol(self):
        candidate = snapshot(75.0, 75.0)
        self.publish(candidate)
        expected = dict(schema='fvr.private.health_series395.ready.v1', pid=PID,
                        process_start_filetime=PROCESS_START,
                        start_tick_ms=START_TICK, positive_baseline=candidate,
                        source_sha256=SOURCE_HASH, read_only=True)
        self.assertEqual(self.ready.read_text(encoding='utf-8'),
                         json.dumps(expected, allow_nan=False))
        self.assertEqual(self.report['baseline'], candidate)
        self.assertEqual(self.report['baseline_candidate'], candidate)
        self.assertEqual(self.report['failed_checks'], [])
        self.assertFalse(self.ready.with_suffix('.tmp').exists())
        self.assertFalse(self.report['actual_death'])

    def test_existing_temp_blocks_and_remains_unmodified(self):
        temporary = self.ready.with_suffix('.tmp')
        temporary.write_bytes(b'existing exact temporary evidence\x00')
        before = temporary.read_bytes()
        with self.assertRaisesRegex(ValueError,
                                    '^Ready temporary path already exists$'):
            self.publish(snapshot(75.0, 75.0))
        self.assertEqual(temporary.read_bytes(), before)
        self.assertFalse(self.ready.exists())

    def test_rejected_candidate_preserves_existing_ready_and_temp(self):
        self.ready.write_bytes(b'prior ready evidence')
        temporary = self.ready.with_suffix('.tmp')
        temporary.write_bytes(b'prior temporary evidence')
        with self.assertRaisesRegex(ValueError,
                                    '^Positive coherent pre-action health required$'):
            self.publish(snapshot(0.0, 0.0))
        self.assertEqual(self.ready.read_bytes(), b'prior ready evidence')
        self.assertEqual(temporary.read_bytes(), b'prior temporary evidence')
        self.assertNotIn('baseline', self.report)

    def test_exclusive_open_rejects_temp_created_after_existence_check(self):
        temporary = self.ready.with_suffix('.tmp')
        temporary.write_bytes(b'concurrent temporary evidence')
        # Simulate the prior existence observation having missed a new writer.
        with patch.object(Path, 'exists', return_value=False):
            with self.assertRaises(FileExistsError):
                self.publish(snapshot(75.0, 75.0))
        self.assertEqual(temporary.read_bytes(), b'concurrent temporary evidence')
        self.assertFalse(self.ready.exists())

    def test_accepted_candidate_keeps_original_atomic_replace_semantics(self):
        self.ready.write_bytes(b'prior ready evidence')
        self.publish(snapshot(75.0, 75.0))
        self.assertEqual(json.loads(self.ready.read_text(encoding='utf-8'))
                         ['positive_baseline']['client_health'], 75.0)
        self.assertFalse(self.ready.with_suffix('.tmp').exists())

    def test_nonfinite_extra_evidence_cannot_publish_nonstandard_json(self):
        candidate = snapshot(75.0, 75.0)
        candidate['unexpected_nonfinite_evidence'] = float('nan')
        with self.assertRaises(ValueError):
            self.publish(candidate)
        self.assertFalse(self.ready.exists())
        temporary = self.ready.with_suffix('.tmp')
        self.assertTrue(temporary.exists())
        self.assertNotIn('NaN', temporary.read_text(encoding='utf-8'))
        # Preserve R2 failure semantics: partial temporary output blocks retry.
        before = temporary.read_bytes()
        with self.assertRaisesRegex(ValueError,
                                    '^Ready temporary path already exists$'):
            self.publish(snapshot(75.0, 75.0))
        self.assertEqual(temporary.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
