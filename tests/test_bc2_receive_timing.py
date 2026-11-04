"""Shared driver failure timing regressions; all transport and clocks are in memory."""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("receive_timing_memory", ROOT / "tests/test_bc2_feed_driver_observation.py")
memory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(memory)
d = memory.d
REAL_READ_JSON = d.read_json
MS = 1_000_000


class ReceiveTimingTests(unittest.TestCase):
    def harness(self):
        h = memory.Harness(self)
        # Exercise the real read/decode boundary through the existing in-memory
        # byte reader. Legacy suite mocks remain compatible with read_json.
        self.enterContext(patch.object(d, "read_json", REAL_READ_JSON))
        h.driver.held_left = False
        return h

    def timeout_case(self, mode):
        h = self.harness()
        v = h.driver
        original = copy.deepcopy(v.latest_snapshot)
        started = v.last_request
        h.now = started + 102_512_700
        del h.files[v.snapshot_path]
        self.assertIsNone(v.receive_snapshot())
        _, response = memory.fixture(h.now, v.pending_nonce)
        response["valid_until_ns"] = started + 181_073_600
        h.publish(response)
        raw = h.files[v.snapshot_path]
        if mode == "between_calls":
            h.now += 186_679_200

        def read(path, maximum):
            if mode == "snapshot_read" and path == v.snapshot_path:
                h.now += 186_679_200
            return h.files[path]

        with patch.object(d, "read_bytes", read):
            with self.assertRaisesRegex(d.REFUSAL, "elapsed_ns=289191900") as failure:
                v.receive_snapshot()
        self.assertEqual(v.latest_snapshot, original)
        self.assertEqual(v.artifacts.saved["snapshot-001-at-timeout.json"], raw)
        self.assertEqual(v.last_request, started)
        self.assertEqual(v.pending_nonce, 1)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual(h.command_writes, [])
        trace = v._receive_timing
        trace.freeze(v.pending_command, v.sequence)
        report = trace.report()
        self.assertEqual(report["originalResponseDeadlineNs"], started + 250 * MS)
        self.assertEqual(report["timeoutEvaluatedNs"], started + 289_191_900)
        self.assertEqual(report["lastObservedResponseNonce"], "1")
        self.assertEqual(report["phaseNs"][mode]["maxNs"], 186_679_200)
        other = "snapshot_read" if mode == "between_calls" else "between_calls"
        self.assertEqual(report["phaseNs"][other]["maxNs"], 0)
        self.assertIn("snapshot_parse", report["phaseNs"])
        self.assertLess(len(d.json_bytes(report)), 8192)
        return str(failure.exception), v.artifacts.events[-1]

    def test_delayed_read_is_distinct_from_identical_between_poll_timeout(self):
        # Same response, request deadline, evaluation and refusal; only the
        # new passive timing identifies where the synthetic delay occurred.
        delayed_error, delayed_event = self.timeout_case("snapshot_read")
        gap_error, gap_event = self.timeout_case("between_calls")
        self.assertEqual(delayed_error, gap_error)
        self.assertEqual(delayed_event, gap_event)

    def test_ownership_failure_and_read_error_keep_original_refusal(self):
        for failure in ("owner", "io"):
            with self.subTest(failure=failure):
                h = self.harness()
                v = h.driver
                original = v.request_bytes
                if failure == "owner":
                    h.files[v.request_path] = b"99\n"
                    expected = d.REFUSAL
                    message = "snapshot request changed by another writer"
                    context = patch.object(d, "read_bytes", lambda p, n: h.files[p])
                else:
                    error = PermissionError("synthetic request sharing denial")
                    error.winerror = 5
                    expected, message = PermissionError, "synthetic request sharing denial"
                    context = patch.object(d, "read_bytes", side_effect=error)
                with context, self.assertRaisesRegex(expected, message):
                    v.receive_snapshot()
                self.assertEqual(v.request_bytes, original)
                self.assertEqual(v.pending_nonce, 1)
                self.assertEqual(h.command_writes, [])
                v._receive_timing.freeze(v.pending_command, v.sequence)
                report = v._receive_timing.report()
                if failure == "io":
                    self.assertEqual(report["firstPhaseError"]["phase"], "request_read")
                    self.assertEqual(report["firstPhaseError"]["winerror"], 5)

    def test_ownership_sidecar_and_existence_delay_are_individually_bracketed(self):
        for phase in ("request_read", "failure_read", "snapshot_exists"):
            with self.subTest(phase=phase):
                h = self.harness()
                v = h.driver
                h.now = v.last_request + 102_512_700
                _, response = memory.fixture(h.now, v.pending_nonce)
                h.publish(response)
                sidecar = v.snapshot_path.with_name("mechanism-snapshot-failure.json")
                h.files[sidecar] = b""  # Existing incomplete sidecar behavior.
                delayed = False

                def read(path, maximum):
                    nonlocal delayed
                    if not delayed and ((phase == "request_read" and path == v.request_path)
                                        or (phase == "failure_read" and path == sidecar)):
                        h.now += 186_679_200
                        delayed = True
                    return h.files[path]

                def exists(path):
                    nonlocal delayed
                    if not delayed and phase == "snapshot_exists" and path == v.snapshot_path:
                        h.now += 186_679_200
                        delayed = True
                    return path in h.files

                with patch.object(d, "read_bytes", read), patch.object(Path, "exists", exists):
                    with self.assertRaisesRegex(d.REFUSAL, "elapsed_ns=289191900"):
                        v.receive_snapshot()
                v._receive_timing.freeze(v.pending_command, v.sequence)
                self.assertEqual(v._receive_timing.report()["phaseNs"][phase]["maxNs"], 186_679_200)
                self.assertEqual(h.command_writes, [])

    def test_json_parse_delay_is_separate_from_snapshot_io(self):
        h = self.harness()
        v = h.driver
        h.now = v.last_request + 102_512_700
        _, response = memory.fixture(h.now, v.pending_nonce)
        h.publish(response)
        loads = d.json.loads

        def parse(raw, **kwargs):
            h.now += 186_679_200
            return loads(raw, **kwargs)

        with patch.object(d.json, "loads", parse), self.assertRaises(d.REFUSAL):
            v.receive_snapshot()
        v._receive_timing.freeze(v.pending_command, v.sequence)
        report = v._receive_timing.report()
        self.assertEqual(report["phaseNs"]["snapshot_parse"]["maxNs"], 186_679_200)
        self.assertEqual(report["phaseNs"]["snapshot_read"]["maxNs"], 0)
        self.assertIsNone(d._snapshot_json_timing.get())
        self.assertEqual(h.command_writes, [])

    def test_many_polls_are_bounded_with_no_per_poll_artifacts(self):
        h = self.harness()
        v = h.driver
        del h.files[v.snapshot_path]
        initial = copy.deepcopy((v.artifacts.saved, v.artifacts.events))
        for _ in range(1000):
            h.now += 10_000
            v.pump()
        self.assertEqual((v.artifacts.saved, v.artifacts.events), initial)
        trace = v._receive_timing
        trace.freeze(v.pending_command, v.sequence)
        report = trace.report()
        self.assertEqual(len(report["spans"]), d.MAX_RECEIVE_TIMING_SPANS)
        self.assertEqual(report["droppedSpans"], report["spanCount"] - d.MAX_RECEIVE_TIMING_SPANS)
        self.assertEqual(report["spans"][0]["phase"], "before_first_poll")
        self.assertEqual(sum(row["totalNs"] for row in report["phaseNs"].values()), h.now-v.last_request)
        self.assertLess(len(d.json_bytes(report)), 8192)
        before = copy.deepcopy(report)
        h.now += MS
        v.pump()
        self.assertEqual(trace.report(), before)

    def test_success_has_unchanged_artifacts_failure_emits_after_cleanup_only(self):
        h = self.harness()
        v = h.driver
        _, response = memory.fixture(h.now, v.pending_nonce)
        h.publish(response)
        received = v.receive_snapshot()
        self.assertIsNotNone(received)
        v.close()
        self.assertEqual(set(v.artifacts.saved), {"snapshot-001.json"})
        self.assertEqual(v.artifacts.events, [])

        v.pending_nonce = 1
        h.now = v.last_request + 251 * MS
        with self.assertRaises(d.REFUSAL):
            v.receive_snapshot()
        self.assertEqual(v.release_after_failure(), "not attempted: no settled owned held controller")
        self.assertNotIn("receive-timing-failure.json", v.artifacts.saved)
        v.close()
        self.assertIn("receive-timing-failure.json", v.artifacts.saved)
        self.assertEqual(h.command_writes, [])

    def test_ack_read_and_poll_gap_are_retained_without_new_input(self):
        h = self.harness()
        v = h.driver
        v.monitor = False
        read_state = v.read_state

        def slow_state():
            if v.pending_command and v.commands:
                h.now += 20 * MS
            return read_state()

        v.read_state = slow_state
        v.send(["grip l 0"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertFalse(v.pending_command)
        self.assertNotIn("receive-timing-failure.json", v.artifacts.saved)
        trace = v._receive_timing
        trace.freeze(v.pending_command, v.sequence)
        report = trace.report()
        self.assertEqual(report["phaseNs"]["ack_state_read"]["maxNs"], 20 * MS)
        self.assertGreaterEqual(report["phaseNs"]["ack_gap"]["totalNs"], MS)
        self.assertIn("ack_read", report["phaseNs"])
        self.assertIn("pump", report["phaseNs"])


    def test_compact_report_and_artifact_failure_cannot_replace_original_refusal(self):
        h = self.harness()
        v = h.driver
        names = ("before_first_poll", "between_calls", "receive", "request_read", "failure_exists",
                 "failure_read", "failure_parse", "snapshot_exists", "snapshot_read", "snapshot_parse",
                 "pump", "command", "ack_gap", "ack_state_read", "ack_command_read", "ack_exists", "ack_read")
        trace = v._receive_timing = d.ReceiveTiming(999_999_999_999_999, 999_999_999_999_999,
                                                   18_446_744_073_709_551_615, h.now)
        for index, name in enumerate(names * 3):
            trace.phase(name, 999_999_999_999_999 + (index+1) * MS)
        h.now = trace.at + MS
        trace.error(PermissionError("x" * 1000))
        trace.freeze(True, 512)
        self.assertLess(len(d.json_bytes(trace.report())), 8192)
        with patch.object(v.artifacts, "save", side_effect=d.REFUSAL("existing artifact cap")):
            v.close()  # Best-effort diagnostics must not mask the existing error.
        self.assertEqual(h.command_writes, [])


if __name__ == "__main__":
    unittest.main()
