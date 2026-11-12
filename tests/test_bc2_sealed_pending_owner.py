"""Actual Windows owner custody; synthetic sessions/clocks, no game evidence."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as feed
import test_bc2_reload_stimulus as reload

d = feed.d


@unittest.skipUnless(os.name == "nt", "actual Windows HANDLE identity")
class SealedPendingOwnerTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory(prefix="bc2-sealed-owner-")))

    def record(self, raw=b"exact-owner"):
        value = d.PendingOwner(d.Driver.__new__(d.Driver), self.directory / "live-driver-owner.json", raw)
        self.addCleanup(value.close)
        return value

    def test_sealed_validation_has_no_payload_io_and_rejects_wrong_expected_bytes(self):
        record = self.record()
        self.assertIsInstance(record._raw, bytes)
        self.assertIsInstance(record._identity, tuple)
        for method in ("read", "write", "seek", "flush"):
            self.assertFalse(hasattr(record.stream, method))
        with patch.object(Path, "open", side_effect=AssertionError("payload reopen")), \
                patch.object(d.os, "fstat", side_effect=AssertionError("payload fstat")):
            for _ in range(3):
                self.assertEqual(record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS), b"exact-owner")
        with patch.object(d, "owner_file_identity", side_effect=AssertionError("wrong bytes reached metadata")), \
                self.assertRaisesRegex(d.REFUSAL, "owner record replaced"):
            record.validate(b"other-host-sequence-hash", d.time.perf_counter_ns() + d.NS)

    def test_cached_identity_must_match_both_live_and_path_handles(self):
        for changed_call in (1, 2):
            with self.subTest(changed_call=changed_call):
                record = self.record()
                identity = d.owner_file_identity
                calls = 0
                def changed(api, handle):
                    nonlocal calls
                    calls += 1
                    actual = identity(api, handle)
                    return (actual[0], b"different-file-id") if calls == changed_call else actual
                with patch.object(d, "owner_file_identity", changed), \
                        self.assertRaisesRegex(d.REFUSAL, "identity changed"):
                    record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS)
                self.assertIsNone(record._path_handle)
                record.close()

    def test_closed_custody_cannot_authorize_cached_bytes(self):
        record = self.record()
        record.close()
        self.assertIsNone(record._raw)
        self.assertIsNone(record._identity)
        with self.assertRaisesRegex(d.REFUSAL, "custody unavailable"):
            record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS)

    def test_incomplete_write_flush_or_initial_readback_never_publishes_owner(self):
        path = self.directory / "live-driver-owner.json"
        path.write_bytes(b"previous-owner")
        original_open = d.open_prepared_command
        for failure in ("write", "flush", "readback"):
            with self.subTest(failure=failure):
                class FailedStream:
                    def __init__(self, stream):
                        self.stream = stream

                    def __getattr__(self, name):
                        return getattr(self.stream, name)

                    def write(self, raw):
                        return self.stream.write(raw[:-1] if failure == "write" else raw)

                    def flush(self):
                        if failure == "flush":
                            raise OSError("synthetic flush failure")
                        self.stream.flush()

                    def read(self, size):
                        return b"wrong readback" if failure == "readback" else self.stream.read(size)
                def failing_open(*args, **kwargs):
                    return FailedStream(original_open(*args, **kwargs))
                driver = d.Driver.__new__(d.Driver)
                with patch.object(d, "open_prepared_command", failing_open), self.assertRaises((d.REFUSAL, OSError)):
                    d.PendingOwner(driver, path, b"new-owner")
                self.assertEqual(path.read_bytes(), b"previous-owner")
                self.assertEqual(list(self.directory.glob("*.tmp")), [])
                self.assertEqual(getattr(driver, "_prepared_cleanup_pending", []), [])

    def test_failed_metadata_close_retains_one_record_and_bounded_diagnostics(self):
        record = self.record()
        driver = d.Driver.__new__(d.Driver)
        driver._publication_owner = record
        with patch.object(record._api, "CloseHandle", return_value=0):
            with self.assertRaises(OSError):
                record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS)
            self.assertIsNotNone(record._path_handle)
            for _ in range(20):
                driver.dispose_publication_owner()
            self.assertIs(driver._publication_owner, record)
            self.assertEqual(driver._prepared_cleanup_pending, [record])
            self.assertEqual(len(driver._prepared_cleanup_errors), 16)
            self.assertEqual(record._raw, b"exact-owner")
        driver.dispose_publication_owner()
        self.assertIsNone(driver._publication_owner)
        self.assertIsNone(record._path_handle)
        self.assertIsNone(record._raw)
        self.assertEqual(driver._prepared_cleanup_pending, [])

    def test_deadline_before_and_after_metadata_never_renews_or_leaks_probe(self):
        record = self.record()
        for expired_call in (0, 1, 2):
            with self.subTest(expired_call=expired_call):
                now, deadline, calls = [1], 10, 0
                identity = d.owner_file_identity
                def delayed(api, handle):
                    nonlocal calls
                    calls += 1
                    value = identity(api, handle)
                    if calls == expired_call:
                        now[0] = deadline
                    return value
                if expired_call == 0:
                    now[0] = deadline
                with patch.object(d.time, "perf_counter_ns", lambda: now[0]), \
                        patch.object(d, "owner_file_identity", delayed), \
                        self.assertRaises(d.ReadDeadlineExceeded):
                    record.validate(b"exact-owner", deadline)
                self.assertEqual(calls, expired_call)
                self.assertIsNone(record._path_handle)
                self.assertIsNotNone(record.stream)

    @staticmethod
    def replace_posix(path, raw):
        temporary = path.with_name("posix-replacement.tmp")
        stream = d.open_prepared_command(temporary)
        published = False
        try:
            stream.write(raw)
            stream.flush()
            class RenameInfo(ctypes.Structure):
                _fields_ = [("Flags", wintypes.DWORD), ("RootDirectory", wintypes.HANDLE),
                            ("FileNameLength", wintypes.DWORD), ("FileName", wintypes.WCHAR * 1)]
            name = str(path).encode("utf-16-le")
            buffer = ctypes.create_string_buffer(max(ctypes.sizeof(RenameInfo), RenameInfo.FileName.offset + len(name) + 2))
            info = RenameInfo.from_buffer(buffer)
            info.Flags, info.FileNameLength = 3, len(name)
            ctypes.memmove(ctypes.addressof(buffer) + RenameInfo.FileName.offset, name, len(name))
            api, handle = d.prepared_file_api(stream)
            if not api.SetFileInformationByHandle(handle, 22, buffer, len(buffer)):
                raise ctypes.WinError(ctypes.get_last_error())
            published = True
        finally:
            if not published:
                d.delete_prepared_command(stream)
            stream.close()

    def test_exclusive_custody_refuses_actual_posix_replacement(self):
        record = self.record()
        with self.assertRaises(OSError):
            self.replace_posix(record.path, b"foreign-owner")
        self.assertEqual(record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS), b"exact-owner")

    def test_path_identity_refuses_actual_detachment_even_if_handle_name_appears_unchanged(self):
        import msvcrt
        def shared_control(path, *, readable=False):
            # Deliberately weaken only this fixture to enable the documented
            # POSIX-replacement control. Production retains share=0 throughout.
            api = ctypes.WinDLL("kernel32", use_last_error=True)
            api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                       ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
            api.CreateFileW.restype = wintypes.HANDLE
            handle = api.CreateFileW(str(path), 0xC0010000, 7, None, 1, 0x80, None)
            if handle in (None, wintypes.HANDLE(-1).value):
                raise ctypes.WinError(ctypes.get_last_error())
            return os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDWR | os.O_BINARY), "w+b")
        with patch.object(d, "open_prepared_command", shared_control):
            record = self.record()
        identity = record._identity
        self.replace_posix(record.path, b"foreign-owner")
        self.assertEqual(record.path.read_bytes(), b"foreign-owner")
        self.assertEqual(d.owner_file_identity(*d.prepared_file_api(record.stream)), identity)
        with patch.object(d, "same_path", return_value=True), \
                self.assertRaisesRegex(d.REFUSAL, "pathname identity changed"):
            record.validate(b"exact-owner", d.time.perf_counter_ns() + d.NS)
        self.assertIsNone(record._path_handle)

    def reload_harness(self):
        clock = reload.Clock()
        helper = reload.StimulusTests()
        h = self.enterContext(helper.ack_driver(clock))
        driver = h.driver
        driver.run = self.directory
        driver.save_owner = reload.m.ReloadStimulus.save_owner.__get__(driver)
        lock = reload.m.transport.open_prepared_command(self.directory / "live-driver.lock")
        self.addCleanup(lock.close)
        import msvcrt
        driver.lock = msvcrt.get_osfhandle(lock.fileno())
        self.enterContext(patch.object(reload.m.transport, "time", clock))
        self.addCleanup(driver.dispose_publication_owner)
        return h, clock

    def test_reload_shared_owner_survives_callback_and_command_handoff(self):
        h, clock = self.reload_harness()
        driver = h.driver
        deadlines = driver.deadline, driver.stage_deadline, driver.hard_deadline
        publish = reload.m.transport.atomic.side_effect
        checked = []
        def guarded(path, raw, **kwargs):
            record = driver._publication_owner
            checked.append(json.loads(record._raw))
            with self.assertRaises(OSError):
                record.path.write_bytes(b"foreign")
            publish(path, raw, **kwargs)
            self.assertIsNotNone(record.stream)
            self.assertEqual(record.validate(driver.owner_bytes(True), kwargs["deadline_ns"]), record._raw)
        with patch.object(reload.m.transport, "atomic", side_effect=guarded):
            driver.send(reload.sample(0, "acquire", 1, .1))
        self.assertEqual(len(h.published), 1)
        self.assertTrue(checked[0]["pending"])
        self.assertEqual(checked[0]["host"], driver.host)
        self.assertEqual(checked[0]["sequence"], driver.sequence)
        self.assertEqual(deadlines, (driver.deadline, driver.stage_deadline, driver.hard_deadline))
        self.assertIsNone(driver._publication_owner)
        self.assertFalse(json.loads((self.directory / "live-driver-owner.json").read_bytes())["pending"])

    def test_reload_pending_owner_preserves_explicit_lock_guard(self):
        driver = reload.m.ReloadStimulus.__new__(reload.m.ReloadStimulus)
        for lock in (None, wintypes.HANDLE(-1).value):
            with self.subTest(lock=lock):
                driver.lock = lock
                with patch.object(reload.m.transport.Driver, "save_owner") as shared, \
                        self.assertRaisesRegex(reload.m.Refusal, "exclusive live-driver.lock"):
                    driver.save_owner(True)
                shared.assert_not_called()

    def test_reload_metadata_expiry_restores_previous_owner_without_publishing(self):
        h, clock = self.reload_harness()
        driver = h.driver
        original = driver.sequence, driver.sentinel, driver.command_hash, driver.commands
        deadline = clock.now + reload.m.ACK_NS
        identity, delayed = reload.m.transport.owner_file_identity, False
        def slow(api, handle):
            nonlocal delayed
            value = identity(api, handle)
            if getattr(driver, "_publication_owner", None) is not None and not delayed:
                delayed = True
                clock.now = deadline
            return value
        with patch.object(reload.m.transport, "owner_file_identity", slow), self.assertRaises(reload.m.ReadDeadline):
            driver.send(reload.sample(0, "acquire", 1, .1))
        self.assertEqual(h.published, [])
        self.assertEqual(original, (driver.sequence, driver.sentinel, driver.command_hash, driver.commands))
        self.assertFalse(driver.pending_command or driver.unsafe_owner)
        self.assertIsNone(driver._publication_owner)
        self.assertEqual(driver._publication_failures[0].publication_deadline, deadline)
        self.assertFalse(json.loads((self.directory / "live-driver-owner.json").read_bytes())["pending"])


if __name__ == "__main__":
    unittest.main()
