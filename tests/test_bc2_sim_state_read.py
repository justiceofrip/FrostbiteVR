"""Deterministic local Win32 sharing/clock fixtures; no game/native input."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import uuid

from test_bc2_feed_driver_observation import d, Harness, MS, NS


class Clock:
    def __init__(self):
        self.now = NS
        self.sleeps = []

    def perf_counter_ns(self):
        return self.now

    def sleep(self, seconds):
        self.sleeps.append(seconds)
        self.now += round(seconds * NS)


@unittest.skipUnless(os.name == "nt", "real Win32 sharing contracts")
class StateReadTests(unittest.TestCase):
    def setUp(self):
        # Ordinary inherited-access fixture directory; no ACL editing. Python
        # 3.13's mode0700 tempfile directories exclude this restricted test token.
        self.directory = Path(tempfile.gettempdir()) / ("sim-state-read-" + uuid.uuid4().hex)
        self.directory.mkdir()
        self.addCleanup(self.directory.rmdir)
        self.path = self.directory / "state.json"
        self.payload = b'{"cmdSeq":16,"lastCmd":"note owned","frame":103}\n'
        self.path.write_bytes(self.payload)
        self.addCleanup(self.path.unlink)
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
            ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        self.api.CreateFileW.restype = wintypes.HANDLE
        self.api.CloseHandle.argtypes = (wintypes.HANDLE,)

    def handle(self, access, sharing):
        handle = self.api.CreateFileW(str(self.path), access, sharing, None, 3, 0x80, None)
        if handle == wintypes.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.addCleanup(self.api.CloseHandle, handle)
        return handle

    def test_delete_owner_reproduces_old_denial_and_shared_reader_succeeds(self):
        # A rename/delete-access owner cannot coexist with CRT's missing
        # FILE_SHARE_DELETE. This is an actual kernel sharing check, no mocks.
        self.handle(0x10000, 7)
        with self.assertRaises(PermissionError) as denied:
            self.path.read_bytes()
        self.assertEqual(denied.exception.errno, 13)
        value, raw = d.read_json(self.path, 16384)
        self.assertEqual(raw, self.payload)
        self.assertEqual(value["cmdSeq"], 16)

    def test_state_handle_denies_in_place_mutation(self):
        with d.open_snapshot(self.path, share_write=False) as stream:
            handle = self.api.CreateFileW(str(self.path), 0x40000000, 7, None, 3, 0x80, None)
            self.assertEqual(handle, wintypes.HANDLE(-1).value)
            self.assertEqual(ctypes.get_last_error(), 32)
            self.assertEqual(stream.read(), self.payload)

    def test_real_sharing_violation_recovers_after_old_40ms_cap(self):
        handle = self.api.CreateFileW(str(self.path), 0x80000000, 0, None, 3, 0x80, None)
        self.assertNotEqual(handle, wintypes.HANDLE(-1).value)
        clock, released = Clock(), False
        def sleep(seconds):
            nonlocal released
            clock.sleep(seconds)
            if clock.now >= NS + 80 * MS and not released:
                self.api.CloseHandle(handle)
                released = True
        try:
            with patch.object(d.time, "perf_counter_ns", clock.perf_counter_ns), \
                    patch.object(d.time, "sleep", sleep):
                value, raw = d.read_until(NS + 100 * MS, d.read_json, self.path, 16384)
        finally:
            if not released:
                self.api.CloseHandle(handle)
        self.assertTrue(released)
        self.assertEqual(clock.now, NS + 80 * MS)
        self.assertEqual(raw, self.payload)
        self.assertEqual(value["cmdSeq"], 16)

    def test_persistent_sharing_stops_at_original_submillisecond_deadline(self):
        self.handle(0x80000000, 0)
        clock = Clock()
        with patch.object(d.time, "perf_counter_ns", clock.perf_counter_ns), \
                patch.object(d.time, "sleep", clock.sleep), \
                self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            d.read_until(NS + 2_500_000, d.read_json, self.path, 16384)
        self.assertEqual(clock.now, NS + 2_500_000)
        self.assertEqual(clock.sleeps, [.001, .001, .0005])

    def test_state_retry_is_bounded_without_outer_deadline(self):
        self.handle(0x80000000, 0)
        clock = Clock()
        with patch.object(d.time, "perf_counter_ns", clock.perf_counter_ns), \
                patch.object(d.time, "sleep", clock.sleep), self.assertRaises(OSError) as caught:
            d.read_json(self.path, 16384)
        self.assertEqual(caught.exception.winerror, 32)
        self.assertEqual(len(clock.sleeps), 249)
        self.assertEqual(clock.now, NS + 249 * MS)

    def test_access_denied_and_other_terminal_errors_are_not_retried(self):
        for code in (5, 87):
            with self.subTest(winerror=code), \
                    patch.object(d, "open_snapshot", side_effect=ctypes.WinError(code)) as opened, \
                    patch.object(d.time, "sleep") as slept, self.assertRaises(OSError) as caught:
                d.read_json(self.path, 16384)
            self.assertEqual(caught.exception.winerror, code)
            opened.assert_called_once_with(self.path, share_write=False)
            slept.assert_not_called()

    def test_transient_missing_and_lock_errors_retry_without_altering_bytes(self):
        opened = d.open_snapshot
        for code in (2, 33):
            with self.subTest(winerror=code):
                clock = Clock()
                with opened(self.path, share_write=False) as stream, \
                        patch.object(d, "open_snapshot", side_effect=[ctypes.WinError(code), stream]) as attempt, \
                        patch.object(d.time, "perf_counter_ns", clock.perf_counter_ns), \
                        patch.object(d.time, "sleep", clock.sleep):
                    value, raw = d.read_until(NS + 10 * MS, d.read_json, self.path, 16384)
                self.assertEqual(attempt.call_count, 2)
                self.assertEqual(raw, self.payload)
                self.assertEqual(value["cmdSeq"], 16)

    def test_late_success_and_nested_scope_cannot_renew_original_deadline(self):
        clock = Clock()
        opened = d.open_snapshot
        def slow_open(*args, **kwargs):
            clock.now = NS + 5 * MS
            return opened(*args, **kwargs)
        with patch.object(d, "open_snapshot", side_effect=slow_open), \
                patch.object(d.time, "perf_counter_ns", clock.perf_counter_ns), \
                self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            d.read_until(NS + 5 * MS, d.read_until, NS + 100 * MS, d.read_json, self.path, 16384)
        self.assertIsNone(d._read_deadline_ns.get())

    def test_corrupt_json_and_size_violation_are_terminal(self):
        for raw, kind in ((b'{"cmdSeq":', json.JSONDecodeError),
                          (b'{"cmdSeq":NaN}', d.REFUSAL),
                          (b' ' * 16385, d.REFUSAL)):
            with self.subTest(raw=raw[:30]):
                self.path.write_bytes(raw)
                with patch.object(d, "open_snapshot", wraps=d.open_snapshot) as opened, \
                        patch.object(d.time, "sleep") as slept, self.assertRaises(kind):
                    d.read_json(self.path, 16384)
                opened.assert_called_once()
                slept.assert_not_called()


class AckBudgetTests(unittest.TestCase):
    def test_original_100ms_source_lease_caps_publication_state_read(self):
        h = Harness(self)
        v = h.driver
        v.monitor = False
        original = v.read_state
        source_deadline = h.now + 100 * MS
        def read():
            if v.pending_command:
                h.sleep(.100)
            return original()
        v.read_state = read
        with self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            v.send(["grip l 1"], source_deadline=source_deadline)
        self.assertEqual(h.now, source_deadline)
        self.assertEqual(h.driver_publication_deadline, source_deadline)
        self.assertEqual(h.command_writes, [])
        self.assertFalse(v.pending_command)

    def test_late_ack_file_cannot_commit_an_already_sent_batch(self):
        h = Harness(self, ack_ms=1)
        v = h.driver
        v.monitor = False
        def read(path, maximum):
            if path == v.ack_path:
                h.sleep(.250)
            return h.files[path]
        with patch.object(d, "read_bytes", side_effect=read), \
                self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            v.send(["grip l 1"])
        self.assertTrue(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)

    def test_late_state_cannot_commit_or_republish_sent_input(self):
        h = Harness(self, ack_ms=1)
        v = h.driver
        v.monitor = False
        original = v.read_state
        deadlines = v.deadline, v.stage_deadline, v.hard_deadline
        requested = h.now
        def read():
            state = original()
            if v.pending_command and h.command_writes:
                h.sleep(.250)
                state = original()
            return state
        v.read_state = read
        with self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            v.send(["grip l 1"])
        self.assertEqual(h.now, requested + 250 * MS)
        self.assertTrue(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline), deadlines)
        self.assertEqual(h.driver_publication_deadline, requested + 250 * MS)

    def test_cleanup_late_state_does_not_settle_pending_command(self):
        h = Harness(self, ack_ms=1)
        v = h.driver
        v.monitor = False
        v.send(["grip l 1"])
        v.pending_command = True
        original = v.read_state
        def read():
            h.sleep(.100)
            return original()
        v.read_state = read
        with self.assertRaisesRegex(d.REFUSAL, "original acknowledgement/read deadline"):
            v.settle_pending_for_release()
        self.assertTrue(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)


if __name__ == "__main__":
    unittest.main()
