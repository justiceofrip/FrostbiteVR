"""Prepared transport regressions; synthetic sources, no gameplay evidence.

Run: python -B -m unittest discover -s tests -p test_bc2_command_preparation.py
WindowsCustodyTests uses actual temporary files and Windows handle operations;
the remaining cases reuse the in-memory driver harness. Frozen holster adapter
ordering is outside this suite.
"""
import copy
from pathlib import Path
import tempfile
import time
import types
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as memory

d, MS = memory.d, memory.MS


class PreparedHarness(memory.Harness):
    def __init__(self, testcase):
        super().__init__(testcase)
        self.driver.monitor = False
        self.operations, self.prepare_delay, self.validation_delay = [], 0, 0
        self.owner_reads = []
        outer = self

        class Stream:
            def __init__(self, path):
                self.path, self.closed = path, False

            def write(self, raw):
                outer.operations.append("prepare_write")
                outer.files[self.path] = raw

            def flush(self):
                outer.operations.append("prepare_flush")

            def close(self):
                if not self.closed:
                    outer.operations.append("prepare_close")
                    self.closed = True

        def open_prepared(path):
            self.operations.append("prepare_open")
            self.now += self.prepare_delay
            return Stream(path)

        def replace(stream, target):
            self.operations.append("replace")
            self.write(target, self.files.pop(stream.path))

        def delete(stream):
            self.operations.append("discard")
            del self.files[stream.path]

        def local_path(path):
            if path.suffix == ".tmp":
                self.now += self.validation_delay
            return path

        testcase.enterContext(patch.object(d, "local_path", local_path))
        testcase.enterContext(patch.object(d, "open_prepared_command", open_prepared))
        testcase.enterContext(patch.object(d, "prepare_command_rename", lambda stream, path: replace))
        testcase.enterContext(patch.object(d, "delete_prepared_command", delete))
        self.real_owner_read = self.driver.read_publication_owner
        self.driver.read_publication_owner = self.read_owner

    def read_owner(self, expected_bytes, deadline_ns):
        self.operations.append("fresh_owner")
        self.owner_reads.append((expected_bytes, deadline_ns))
        return self.real_owner_read(expected_bytes, deadline_ns)

    def prepare(self):
        self.driver.prepare_next_command(["grip l 1"])
        return self.driver._prepared_command


class CommandPreparationTests(unittest.TestCase):
    def test_preparation_preserves_source_and_ownership_until_fresh_admission(self):
        h = PreparedHarness(self)
        v = h.driver
        h.prepare_delay = 30 * MS
        original = v.sequence, v.sentinel, v.command_hash, v.commands, v.last_write
        source = copy.deepcopy(v.reference)
        request, snapshot = h.files[v.request_path], h.files[v.snapshot_path]
        prepared = h.prepare()
        self.assertEqual(h.command_writes, [])
        self.assertEqual((v.sequence, v.sentinel, v.command_hash, v.commands, v.last_write), original)
        self.assertEqual((h.files[v.request_path], h.files[v.snapshot_path]), (request, snapshot))
        self.assertEqual(v.reference, source)
        self.assertEqual(v.latest_snapshot, source)
        self.assertNotIn(v.run / "live-driver-owner.json", h.files)
        _, fresh = memory.fixture(h.now, v.pending_nonce)
        h.publish(fresh)
        v.accept_snapshot(*v.receive_snapshot())
        source_deadline = fresh["native_deadline_ns"]
        v.send(["grip l 1"], source_deadline=source_deadline)
        self.assertEqual(h.owner_reads, [(v.owner_bytes(True), source_deadline)])
        self.assertEqual(h.command_writes, [(fresh["observed_ns"], prepared.raw)])
        self.assertEqual(v.artifacts.saved["snapshot-001.json"], d.json_bytes(fresh))
        self.assertEqual(v.reference, source)
        self.assertEqual(v.artifacts.events[0]["type"], "command_request")
        self.assertEqual(v.artifacts.events[0]["commands"], prepared.batch)
        self.assertLess(h.operations.index("prepare_flush"), h.operations.index("fresh_owner"))
        self.assertLess(h.operations.index("fresh_owner"), h.operations.index("replace"))
        self.assertEqual(v._receive_timing.publication_deadline_ns, source_deadline)
        self.assertIsNone(v._prepared_command)
        self.assertNotIn(prepared.temporary, h.files)

    def test_mismatched_body_or_owner_discards_without_publication(self):
        for changed in ("body", "sequence", "sentinel", "hash", "count", "path"):
            with self.subTest(changed=changed):
                h = PreparedHarness(self)
                prepared = h.prepare()
                if changed in ("sequence", "sentinel", "hash"):
                    index = ("sequence", "sentinel", "hash").index(changed)
                    previous = list(prepared.previous)
                    previous[index] = "foreign"
                    prepared.previous = tuple(previous)
                elif changed == "count":
                    prepared.command_count += 1
                elif changed == "path":
                    prepared.path = Path("foreign")
                with self.assertRaisesRegex(d.REFUSAL, "prepared command does not match"):
                    h.driver.send(["grip l 0"] if changed == "body" else ["grip l 1"])
                self.assertEqual(h.command_writes, [])
                self.assertIsNone(h.driver._prepared_command)
                self.assertNotIn(prepared.temporary, h.files)
                with self.assertRaisesRegex(d.REFUSAL, "changed or consumed"):
                    prepared.publish(prepared.path, prepared.raw, h.now + MS, lambda: None)

    def test_expired_source_final_owner_change_and_validation_delay_refuse(self):
        for failure in ("source", "owner", "validation_deadline"):
            with self.subTest(failure=failure):
                h = PreparedHarness(self)
                prepared = h.prepare()
                until = h.now + 5 * MS
                if failure == "source":
                    until = h.now
                elif failure == "owner":
                    h.driver.read_publication_owner = lambda expected_bytes, deadline_ns: b"foreign"
                else:
                    h.validation_delay = 6 * MS
                with self.assertRaises(d.REFUSAL):
                    h.driver.send(["grip l 1"], source_deadline=until)
                self.assertEqual(h.command_writes, [])
                self.assertNotIn(prepared.temporary, h.files)
                self.assertEqual(h.driver.unsafe_owner, failure == "owner")
                self.assertFalse(h.driver.pending_command)
                self.assertEqual(h.driver.sequence, 10)

    def test_refused_replace_rechecks_owner_and_never_reprepares_or_reconsumes(self):
        h = PreparedHarness(self)
        prepared = h.prepare()
        rename, attempts = prepared.rename, []

        def once_denied(source, target):
            attempts.append(h.now)
            if len(attempts) == 1:
                error = PermissionError("synthetic sharing conflict")
                error.winerror = 32
                raise error
            rename(source, target)

        prepared.rename = once_denied
        source_deadline = h.now + 10 * MS
        h.driver.send(["grip l 1"], source_deadline=source_deadline)
        self.assertEqual(len(attempts), 2)
        self.assertEqual(h.operations.count("fresh_owner"), 2)
        self.assertEqual(h.owner_reads, [(h.driver.owner_bytes(True), source_deadline)] * 2)
        self.assertEqual(h.operations.count("prepare_open"), 1)
        self.assertEqual(len(h.command_writes), 1)
        with self.assertRaisesRegex(d.REFUSAL, "changed or consumed"):
            prepared.publish(prepared.path, prepared.raw, h.now + MS, lambda: None)
        self.assertEqual(len(h.command_writes), 1)

    def test_failed_evidence_write_retains_source_and_allows_only_owned_release(self):
        h = PreparedHarness(self)
        prepared = h.prepare()
        raw = h.files[h.driver.snapshot_path]
        h.driver.artifacts.save("snapshot-001.json", raw)
        deadlines = h.driver.deadline, h.driver.stage_deadline, h.driver.hard_deadline
        with patch.object(h.driver.artifacts, "event", side_effect=OSError("evidence unavailable")):
            with self.assertRaisesRegex(OSError, "evidence unavailable"):
                h.driver.send(["grip l 1"], source_deadline=h.now + 5 * MS)
        self.assertEqual(h.command_writes, [])
        self.assertNotIn(prepared.temporary, h.files)
        self.assertEqual(h.driver.artifacts.saved["snapshot-001.json"], raw)
        self.assertEqual(h.driver.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertEqual(h.command_writes[0][1].decode().splitlines()[:-1], ["grip l 0"])
        self.assertEqual((h.driver.deadline, h.driver.stage_deadline, h.driver.hard_deadline), deadlines)

    def test_cleanup_discards_preparation_and_preserves_original_failed_deadline(self):
        for failed_publication in (False, True):
            with self.subTest(failed_publication=failed_publication):
                h = PreparedHarness(self)
                prepared = h.prepare()
                source_deadline = h.now + 5 * MS
                if failed_publication:
                    h.validation_delay = 6 * MS
                    with self.assertRaisesRegex(d.REFUSAL, "original command publication deadline"):
                        h.driver.send(["grip l 1"], source_deadline=source_deadline)
                h.driver.release_after_failure()
                self.assertNotIn(prepared.temporary, h.files)
                self.assertEqual(len(h.command_writes), 1)
                self.assertNotEqual(h.command_writes[0][1], prepared.raw)
                self.assertEqual(h.command_writes[0][1].decode().splitlines()[:-1], ["grip l 0"])
                if failed_publication:
                    report = h.driver._receive_timing.report()
                    self.assertEqual(report["originalPublicationDeadlineNs"], source_deadline)
                    self.assertEqual(report["firstPhaseError"]["phase"], "publish_file")

    def test_failed_create_never_deletes_unowned_collision(self):
        h = PreparedHarness(self)
        with patch.object(d, "open_prepared_command", side_effect=FileExistsError("collision")):
            with self.assertRaises(FileExistsError):
                h.prepare()
        self.assertNotIn("discard", h.operations)
        self.assertIsNone(getattr(h.driver, "_prepared_command", None))
        self.assertEqual(h.command_writes, [])

    def test_disposal_failure_cannot_prevent_owned_release_or_mask_first_refusal(self):
        for failure in ("delete", "close"):
            for mismatch in (False, True):
                with self.subTest(failure=failure, mismatch=mismatch):
                    h = PreparedHarness(self)
                    prepared = h.prepare()
                    deadlines = h.driver.deadline, h.driver.stage_deadline, h.driver.hard_deadline
                    target, member = ((d, "delete_prepared_command") if failure == "delete"
                                      else (prepared.stream, "close"))
                    with patch.object(target, member, side_effect=PermissionError("temporary disposal refused")):
                        if mismatch:
                            with self.assertRaisesRegex(d.REFUSAL, "prepared command does not match"):
                                h.driver.send(["grip l 0"])
                        self.assertEqual(h.driver.release_after_failure(), "ordinary owned controller release acknowledged")
                    self.assertEqual(len(h.command_writes), 1)
                    self.assertEqual(h.command_writes[0][1].decode().splitlines()[:-1], ["grip l 0"])
                    self.assertFalse(h.driver.held_left or h.driver.unsafe_owner or h.driver.pending_command)
                    self.assertEqual((h.driver.deadline, h.driver.stage_deadline, h.driver.hard_deadline), deadlines)
                    self.assertIn(prepared, h.driver._prepared_cleanup_pending)
                    h.driver._receive_timing.freeze(h.driver.pending_command, h.driver.sequence)
                    report = h.driver._receive_timing.report()
                    if mismatch:
                        self.assertIn("prepared command does not match", str(report["firstPhaseError"]))
                        self.assertNotIn("temporary disposal refused", str(report["firstPhaseError"]))
                    else:
                        self.assertIsNone(report["firstPhaseError"])
                    h.driver.save_prepared_cleanup_failure()
                    evidence = h.driver.artifacts.saved["prepared-command-cleanup-failure.json"]
                    self.assertEqual(evidence["retainedTemporaryCount"], 1)
                    self.assertIn("temporary disposal refused", evidence["errors"][0]["error"])
                    with self.assertRaisesRegex(d.REFUSAL, "unambiguous current ownership"):
                        h.driver.prepare_next_command(["grip l 1"])
                    h.driver.dispose_prepared_command(prepared)
                    self.assertEqual(h.driver._prepared_cleanup_pending, [])
                    self.assertNotIn(prepared.temporary, h.files)

    def test_committed_rename_close_failure_cannot_roll_back_or_republish(self):
        h = PreparedHarness(self)
        prepared = h.prepare()
        with patch.object(prepared.stream, "close", side_effect=PermissionError("close refused after commit")):
            h.driver.send(["grip l 1"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(h.driver.command_hash, prepared.hash)
        self.assertEqual(h.driver.sequence, prepared.target)
        self.assertEqual(h.driver.commands, len(prepared.batch))
        self.assertFalse(prepared.owned)
        self.assertTrue(prepared.consumed)
        self.assertIn(prepared, h.driver._prepared_cleanup_pending)
        h.driver.dispose_prepared_command(prepared)
        with self.assertRaisesRegex(d.REFUSAL, "changed or consumed"):
            prepared.publish(prepared.path, prepared.raw, h.now + MS, lambda: None)
        self.assertEqual(len(h.command_writes), 1)

    def test_failed_preparation_preserves_original_error_when_disposal_fails(self):
        h = PreparedHarness(self)
        with patch.object(d, "prepare_command_rename", side_effect=RuntimeError("original preparation error")):
            with patch.object(d, "delete_prepared_command", side_effect=PermissionError("disposal error")):
                with self.assertRaisesRegex(RuntimeError, "original preparation error"):
                    h.prepare()
        self.assertIsNone(getattr(h.driver, "_prepared_command", None))
        self.assertEqual(len(h.driver._prepared_cleanup_pending), 1)
        h.driver.dispose_prepared_command(h.driver._prepared_cleanup_pending[0])

    def test_driver_close_retries_disposal_and_saves_separate_evidence(self):
        h = PreparedHarness(self)
        prepared = h.prepare()
        with patch.object(d, "delete_prepared_command", side_effect=PermissionError("retained disposal error")):
            with self.assertRaisesRegex(d.REFUSAL, "prepared command does not match"):
                h.driver.send(["grip l 0"])
        h.driver._receive_timing.freeze(h.driver.pending_command, h.driver.sequence)
        actions = []
        h.driver.owner = types.SimpleNamespace(close=lambda: actions.append("owner closed"))
        h.driver.lock = None
        h.driver.close()
        self.assertEqual(actions, ["owner closed"])
        self.assertEqual(h.driver._prepared_cleanup_pending, [])
        self.assertNotIn(prepared.temporary, h.files)
        evidence = h.driver.artifacts.saved
        self.assertIn("prepared command does not match", str(evidence["receive-timing-failure.json"]["firstPhaseError"]))
        self.assertIn("retained disposal error", str(evidence["prepared-command-cleanup-failure.json"]["errors"]))
        self.assertEqual(evidence["prepared-command-cleanup-failure.json"]["retainedTemporaryCount"], 0)


@unittest.skipUnless(d.os.name == "nt", "Windows FILE_OBJECT custody")
class WindowsCustodyTests(unittest.TestCase):
    def fixture(self):
        folder = self.enterContext(tempfile.TemporaryDirectory())
        driver = types.SimpleNamespace(command_path=Path(folder) / "command.txt",
            sequence=1, sentinel="old", command_hash="oldhash", commands=0,
            dispose_prepared_command=lambda value: value.close())
        driver.command_path.write_bytes(b"original command")
        prepared = d.PreparedCommand(driver, ["grip r 0"])
        self.addCleanup(prepared.close)  # Close before TemporaryDirectory exits on failure.
        intruder = Path(folder) / "intruder.txt"
        intruder.write_bytes(b"foreign")
        return driver, prepared, intruder

    def assert_exclusive(self, prepared, intruder):
        changed = prepared.raw.replace(b"grip r 0", b"grip r 1")
        for attempt in (lambda: prepared.temporary.write_bytes(changed),
                        lambda: prepared.temporary.unlink(),
                        lambda: d.os.replace(intruder, prepared.temporary),
                        lambda: d.os.replace(prepared.temporary, intruder)):
            with self.assertRaises(PermissionError):
                attempt()
        self.assertFalse(prepared.stream.closed)

    def test_tamper_and_replacement_refused_through_actual_rename(self):
        driver, prepared, intruder = self.fixture()
        actual_rename, probes = prepared.rename, []

        def guarded_rename(source, destination):
            self.assert_exclusive(prepared, intruder)
            actual_rename(source, destination)
            with self.assertRaises(PermissionError):
                destination.write_bytes(b"foreign after rename")
            probes.append(True)

        prepared.rename = guarded_rename
        prepared.publish(driver.command_path, prepared.raw, time.perf_counter_ns() + d.NS,
                         lambda: self.assert_exclusive(prepared, intruder))
        self.assertFalse(prepared.owned)
        prepared.close()
        self.assertEqual(driver.command_path.read_bytes(), prepared.raw)
        self.assertEqual(d.digest(driver.command_path.read_bytes()), prepared.hash)
        self.assertFalse(prepared.temporary.exists())
        self.assertEqual(intruder.read_bytes(), b"foreign")
        self.assertEqual(probes, [True])

    def test_real_destination_sharing_conflict_revalidates_same_held_source(self):
        driver, prepared, intruder = self.fixture()
        reader = self.enterContext(driver.command_path.open("rb"))  # Denies delete-sharing.
        validations = []

        def validate():
            validations.append(True)
            self.assert_exclusive(prepared, intruder)
            if len(validations) == 2:
                reader.close()

        prepared.publish(driver.command_path, prepared.raw, time.perf_counter_ns() + d.NS, validate)
        prepared.close()
        self.assertEqual(len(validations), 2)
        self.assertEqual(driver.command_path.read_bytes(), prepared.raw)

    def test_expiry_after_validation_retains_destination_and_exclusive_disposal(self):
        driver, prepared, intruder = self.fixture()
        now, deadline = [100], 200

        def validate():
            now[0] = deadline

        with patch.object(d.time, "perf_counter_ns", lambda: now[0]):
            with self.assertRaisesRegex(d.REFUSAL, "original command publication deadline"):
                prepared.publish(driver.command_path, prepared.raw, deadline, validate)
        self.assertEqual(driver.command_path.read_bytes(), b"original command")
        self.assert_exclusive(prepared, intruder)
        prepared.close()
        self.assertFalse(prepared.temporary.exists())
        self.assertEqual(intruder.read_bytes(), b"foreign")


if __name__ == "__main__":
    unittest.main()
