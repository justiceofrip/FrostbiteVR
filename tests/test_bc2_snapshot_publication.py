"""Snapshot transport failure behavior; no process/game/native acceptance.

Run: python -B -m unittest discover -s tests -p test_bc2_snapshot_publication.py
The Windows-only check uses real temporary files and ReplaceFileW, not the game.
"""
import copy
import ctypes
from ctypes import wintypes
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "snapshot_publication_memory", ROOT / "tests/test_bc2_feed_driver_observation.py")
memory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(memory)
d = memory.d
MS = 1_000_000


class SnapshotPublicationTests(unittest.TestCase):
    def failure(self, h, nonce=None, stage="replace_final", error=32, attempts=2):
        nonce = h.driver.pending_nonce if nonce is None else nonce
        value = dict(schema="fvr.bc2.mechanism-snapshot-failure.v1", nonce=nonce,
                     publication=dict(nonce=nonce, status="failed", stage=stage, win32_error=error,
                                      first_failure_stage=stage, first_win32_error=error,
                                      cleanup_error=0, failure_record_error=0, attempts=attempts,
                                      accepted_ns=h.now - 100 * MS, serialized_ns=h.now - 90 * MS,
                                      completed_ns=h.now, payload_bytes=1000), nonce_end=nonce)
        path = h.driver.snapshot_path.with_name("mechanism-snapshot-failure.json")
        h.files[path] = d.json_bytes(value)
        return path, value

    def test_terminal_publication_failure_refuses_without_refresh_or_input(self):
        for stage, error, attempts in (("replace_final", 32, 2), ("replace_final", 1175, 1),
                                       ("replace_final", 1175, 2), ("replace_final", 1176, 1),
                                       ("replace_final", 1177, 1), ("write_pending", 112, 1),
                                       ("create_pending", 80, 1), ("payload_size", 0, 0)):
            with self.subTest(stage=stage, error=error, attempts=attempts):
                h = memory.Harness(self)
                v = h.driver
                original = copy.deepcopy(v.latest_snapshot)
                _, failure = self.failure(h, stage=stage, error=error, attempts=attempts)
                with self.assertRaisesRegex(d.REFUSAL, f"stage={stage} win32_error={error} attempts={attempts}"):
                    v.send(["grip l 1"])
                self.assertEqual(v.latest_snapshot, original)
                self.assertEqual(v.snapshot_index, 1)
                self.assertEqual(h.command_writes, [])
                self.assertIsNone(v.snapshot_refresh_started)
                self.assertEqual(v.artifacts.events[0]["publication"], failure["publication"])
                self.assertIn("snapshot-001-failure.json", v.artifacts.saved)

    def test_partial_or_superseded_failure_is_not_consumed(self):
        h = memory.Harness(self)
        path, value = self.failure(h)
        for raw in (b"", d.json_bytes(value)[:-1], b'{"schema":\n',
                    d.json_bytes(dict(value, nonce_end=value["nonce"] + 1))):
            h.files[path] = raw
            self.assertIsNone(h.driver.snapshot_failure())
        h.driver.pending_nonce += 1
        h.files[path] = d.json_bytes(value)
        self.assertIsNone(h.driver.snapshot_failure())

    def test_failure_from_competing_future_request_refuses(self):
        h = memory.Harness(self)
        self.failure(h, nonce=h.driver.pending_nonce + 1)
        with self.assertRaisesRegex(d.REFUSAL, "later competing request"):
            h.driver.receive_snapshot()
        self.assertEqual(h.command_writes, [])

    def test_request_logs_preparation_and_publication_monotonic_times(self):
        h = memory.Harness(self)
        v = h.driver
        v.pending_nonce = None
        started = h.now
        write = h.write

        def delayed_publish(path, raw):
            h.now += 12 * MS
            write(path, raw)

        with patch.object(d, "atomic", delayed_publish):
            v.request_snapshot()
        timing = v.artifacts.saved["request-002-timing.json"]
        self.assertEqual(timing["requestStartedNs"], started)
        self.assertEqual(timing["requestPublishedNs"], started + 12 * MS)
        self.assertEqual(timing["responseDeadlineNs"], started + 250 * MS)
        self.assertEqual(v.last_request, started)

    def test_timeout_preserves_late_response_without_accepting_or_refreshing(self):
        h = memory.Harness(self)
        v = h.driver
        v.last_request_published = v.last_request + 2 * MS
        h.now = v.last_request + 250 * MS + 1
        _, late = memory.fixture(h.now, v.pending_nonce)
        h.publish(late)
        original = copy.deepcopy(v.latest_snapshot)
        with self.assertRaisesRegex(d.REFUSAL, "response not received within request budget"):
            v.receive_snapshot()
        event = v.artifacts.events[-1]
        self.assertEqual(event["elapsedNs"], 250 * MS + 1)
        self.assertEqual(event["timeoutEvaluatedNs"], h.now)
        self.assertEqual(event["requestPublishedNs"], v.last_request_published)
        self.assertEqual(event["lastObservedResponseNonce"], v.pending_nonce)
        self.assertIn("snapshot-001-at-timeout.json", v.artifacts.saved)
        self.assertEqual(v.latest_snapshot, original)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual(h.command_writes, [])

    def test_response_read_cannot_cross_250ms_budget(self):
        h = memory.Harness(self)
        v = h.driver
        h.now = v.last_request + 249 * MS
        _, snapshot = memory.fixture(h.now, v.pending_nonce)
        h.publish(snapshot)
        read = h.read_json

        def slow_read(path, maximum=128 * 1024):
            value = read(path, maximum)
            h.now += 2 * MS
            return value

        with patch.object(d, "read_json", slow_read):
            with self.assertRaisesRegex(d.REFUSAL, "elapsed_ns=251000000"):
                v.receive_snapshot()
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.artifacts.events[-1]["elapsedNs"], 251 * MS)

    @unittest.skipUnless(os.name == "nt", "Windows replacement-sharing contract")
    def test_snapshot_reader_allows_replace_and_retains_original_bytes(self):
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.ReplaceFileW.argtypes = (wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                    wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p)
        api.ReplaceFileW.restype = wintypes.BOOL
        # Root selects TEMP/TMP on G: before running tests per workstation policy.
        with tempfile.TemporaryDirectory(prefix="bc2-snapshot-publication-") as directory:
            final = Path(directory) / "mechanism-snapshot.json"
            pending = Path(directory) / "mechanism-snapshot.pending"
            final.write_bytes(b'{"nonce":1}\n')
            pending.write_bytes(b'{"nonce":2}\n')
            with d.open_snapshot(final) as original:
                self.assertTrue(api.ReplaceFileW(str(final), str(pending), None, 0, None, None),
                                f"ReplaceFileW error={ctypes.get_last_error()}")
                self.assertEqual(original.read(), b'{"nonce":1}\n')
                self.assertEqual(d.read_bytes(final, 32768), b'{"nonce":2}\n')


if __name__ == "__main__":
    unittest.main()
