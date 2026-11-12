"""Real Windows owner-file custody with synthetic driver/session/clocks.

Run: python -B -m unittest discover -s tests -p test_bc2_pending_owner.py
No game, native, headset or actual publication-latency evidence is produced.
The portable transport regressions live in test_bc2_feed_driver_observation.py.
"""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as memory

d, MS = memory.d, memory.MS


@unittest.skipUnless(os.name == "nt", "actual Windows HANDLE custody")
class PendingOwnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='bc2-pending-owner-')))

    def driver(self):
        return d.Driver.__new__(d.Driver)

    def record(self, raw=b'owner'):
        record = d.PendingOwner(self.driver(), self.directory / 'live-driver-owner.json', raw)
        self.addCleanup(record.close)
        return record

    def harness(self):
        h = memory.Harness(self, real_pending_owner=True)
        h.driver.run = self.directory
        h.driver.monitor = False
        self.addCleanup(h.driver.dispose_publication_owner)
        return h

    def test_actual_exclusive_file_rejects_reopen_mutation_unlink_and_replacement(self):
        record = self.record(b'exact-host-sequence-sentinel-hash')
        other = self.directory / 'other'
        other.write_bytes(b'foreign')
        for operation in (lambda: record.path.open('rb'), lambda: record.path.write_bytes(b'foreign'),
                          lambda: record.path.unlink(), lambda: os.replace(other, record.path)):
            with self.subTest(operation=operation), self.assertRaises(OSError):
                operation()
        self.assertEqual(record.validate(b'exact-host-sequence-sentinel-hash', d.time.perf_counter_ns() + d.NS),
                         b'exact-host-sequence-sentinel-hash')
        record.close()
        self.assertEqual(record.path.read_bytes(), b'exact-host-sequence-sentinel-hash')

    def test_validation_uses_metadata_without_reopening_owner_payload(self):
        record = self.record()
        with patch.object(Path, 'open', side_effect=AssertionError('owner reopen')):
            self.assertEqual(record.validate(b'owner', d.time.perf_counter_ns() + d.NS), b'owner')
            self.assertEqual(record.validate(b'owner', d.time.perf_counter_ns() + d.NS), b'owner')

    def test_handle_path_mismatch_is_refused(self):
        record = self.record()
        record.path = self.directory / 'another-owner.json'
        with self.assertRaisesRegex(d.REFUSAL, 'HANDLE path changed'):
            record.validate(b'owner', d.time.perf_counter_ns() + d.NS)

    def test_parent_rename_cannot_admit_stale_handle_at_original_path(self):
        nested = self.directory / 'run'
        nested.mkdir()
        record = d.PendingOwner(self.driver(), nested / 'live-driver-owner.json', b'exact')
        self.addCleanup(record.close)
        moved = self.directory / 'moved'
        try:
            nested.rename(moved)
        except OSError:
            self.assertEqual(record.validate(b'exact', d.time.perf_counter_ns() + d.NS), b'exact')
        else:
            nested.mkdir()
            record.path.write_bytes(b'foreign')
            with self.assertRaisesRegex(d.REFUSAL, 'HANDLE path changed'):
                record.validate(b'exact', d.time.perf_counter_ns() + d.NS)

    def test_failed_owner_rename_cleans_exact_temporary_and_preserves_predecessor(self):
        path = self.directory / 'live-driver-owner.json'
        path.write_bytes(b'old-owner')
        def rename(*args):
            raise OSError('synthetic owner publication failure')
        with patch.object(d, 'prepare_command_rename', return_value=rename), self.assertRaisesRegex(OSError, 'publication'):
            d.PendingOwner(self.driver(), path, b'new-owner')
        self.assertEqual(path.read_bytes(), b'old-owner')
        self.assertEqual(list(self.directory.glob('*.tmp')), [])

    def test_driver_success_joins_exact_bytes_then_closes_owner_and_writes_settled_record(self):
        h = self.harness()
        v = h.driver
        original_deadlines = v.deadline, v.stage_deadline, v.hard_deadline
        deadline = h.now + 100 * MS
        observed = []
        real_validate = d.PendingOwner.validate
        def validate(record, expected_bytes, deadline_ns):
            raw = real_validate(record, expected_bytes, deadline_ns)
            observed.append(raw)
            with self.assertRaises(OSError):
                record.path.write_bytes(b'foreign')
            return raw
        with patch.object(d.PendingOwner, 'validate', validate):
            v.send(['grip l 1'], source_deadline=deadline)
        self.assertEqual(len(observed), 1)
        pending = json.loads(observed[0])
        self.assertEqual((pending['host'], pending['sequence'], pending['sentinel'], pending['commandSha256']),
                         (v.host, v.sequence, v.sentinel, v.command_hash))
        self.assertTrue(pending['pending'])
        self.assertEqual(h.driver_publication_deadline, deadline)
        self.assertEqual(original_deadlines, (v.deadline, v.stage_deadline, v.hard_deadline))
        self.assertIsNone(v._publication_owner)
        self.assertFalse(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(h.files[v.run / 'live-driver-owner.json'], v.owner_bytes(False))

    def test_same_handle_bytes_mismatch_refuses_without_command_or_unsafe_rollback(self):
        h = self.harness()
        v = h.driver
        with patch.object(d.PendingOwner, 'validate', return_value=b'wrong-host-or-command'), \
                self.assertRaisesRegex(d.REFUSAL, 'owner record replaced'):
            v.send(['grip l 1'], source_deadline=h.now + 100 * MS)
        self.assertTrue(v.unsafe_owner)
        self.assertEqual(h.command_writes, [])
        self.assertIsNone(v._publication_owner)

    def test_expiry_inside_owner_validation_retains_original_deadline_and_no_replacement(self):
        h = self.harness()
        v = h.driver
        deadline = h.now + 100 * MS
        original = v.sequence, v.sentinel, v.command_hash, v.commands
        real_validate = d.PendingOwner.validate
        def delay(record, expected_bytes, deadline_ns):
            raw = real_validate(record, expected_bytes, deadline_ns)
            h.now = deadline
            d.require_read_time(deadline_ns)
            return raw
        with patch.object(d.PendingOwner, 'validate', delay), self.assertRaises(d.ReadDeadlineExceeded):
            v.send(['grip l 1'], source_deadline=deadline)
        self.assertEqual(original, (v.sequence, v.sentinel, v.command_hash, v.commands))
        self.assertEqual(h.command_writes, [])
        self.assertFalse(v.pending_command or v.unsafe_owner)
        self.assertIsNone(v._publication_owner)
        self.assertEqual(v._receive_timing.publication_deadline_ns, deadline)

    def test_expired_original_lease_then_owned_release_preserves_frozen_failure(self):
        h = self.harness()
        v = h.driver
        deadline = h.now + 100 * MS
        real_validate = d.PendingOwner.validate
        delayed = False
        def delay_once(record, expected_bytes, deadline_ns):
            nonlocal delayed
            raw = real_validate(record, expected_bytes, deadline_ns)
            if not delayed:
                delayed = True
                h.now = deadline
            d.require_read_time(deadline_ns)
            return raw
        with patch.object(d.PendingOwner, 'validate', delay_once), self.assertRaises(d.ReadDeadlineExceeded):
            v.send(['grip l 1'], source_deadline=deadline)
        v._receive_timing.freeze(v.pending_command, v.sequence)
        self.assertEqual(v.release_after_failure(), 'ordinary owned controller release acknowledged')
        self.assertEqual(v._receive_timing.publication_deadline_ns, deadline)
        self.assertEqual(len(h.command_writes), 1)
        self.assertTrue(h.command_writes[0][1].startswith(b'grip l 0\n'))
        self.assertFalse(v.held_left or v.pending_command or v.unsafe_owner)
        self.assertIsNone(v._publication_owner)

    def test_changed_session_or_command_still_refuses_after_exact_owner_validation(self):
        for change in ('session', 'command'):
            with self.subTest(change=change):
                h = self.harness()
                v = h.driver
                real_validate = d.PendingOwner.validate
                def interfere(record, expected_bytes, deadline_ns):
                    raw = real_validate(record, expected_bytes, deadline_ns)
                    if change == 'session':
                        v.read_state = lambda: d.require(False, 'linked native session changed')
                    else:
                        h.files[v.command_path] = b'other-writer'
                    return raw
                with patch.object(d.PendingOwner, 'validate', interfere), self.assertRaises(d.REFUSAL):
                    v.send(['grip l 1'], source_deadline=h.now + 100 * MS)
                self.assertTrue(v.unsafe_owner)
                self.assertEqual(h.command_writes, [])
                self.assertIsNone(v._publication_owner)

    def test_close_failure_is_retained_and_cannot_prevent_later_disposal(self):
        record = self.record()
        driver = self.driver()
        driver._publication_owner = record
        real_close = record.close
        with patch.object(record, 'close', side_effect=OSError('synthetic close refusal')):
            driver.dispose_publication_owner()
        self.assertIs(driver._publication_owner, record)
        self.assertEqual(driver._prepared_cleanup_pending, [record])
        self.assertEqual(len(driver._prepared_cleanup_errors), 1)
        driver.dispose_publication_owner()
        self.assertIsNone(driver._publication_owner)
        self.assertEqual(driver._prepared_cleanup_pending, [])
        real_close()


if __name__ == "__main__":
    unittest.main()
