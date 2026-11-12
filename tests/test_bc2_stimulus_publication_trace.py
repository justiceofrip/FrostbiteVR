"""Offline publication timing/custody checks; no native gameplay acceptance."""
from contextlib import contextmanager
import importlib.util
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import Mock, patch
import uuid

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("publication_trace_fixtures", ROOT / "tests/test_bc2_reload_stimulus.py")
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
m = fixtures.m
ATOMIC, REPLACE = m.transport.atomic, m.transport.os.replace
MS = 1_000_000


class PublicationTraceTests(unittest.TestCase):
    @contextmanager
    def publication(self, *, source_ns=35 * MS):
        clock = fixtures.Clock()
        # Use inherited scratch ACLs, as the canonical stimulus fixture does.
        scratch = Path(tempfile.gettempdir()).resolve()
        directory = scratch / ("bc2-publication-trace-" + uuid.uuid4().hex)
        directory.mkdir()
        try:
            with fixtures.StimulusTests().ack_driver(clock, archive_ns=0) as h:
                driver = h.driver
                driver.command_path = directory / "command.txt"
                driver.command_path.write_bytes(h.command)
                driver.first_sample_pending = True
                driver.initial_mapping_deadline = clock.now + source_ns
                h.state_read_ns = 0
                h.clock, h.directory, h.rename_attempts = clock, directory, 0
                h.before_rename = lambda: None
                h.after_rename = lambda: None
                consume = m.transport.atomic.side_effect

                def replace(temporary, destination):
                    self.assertEqual(destination, driver.command_path)
                    h.rename_attempts += 1
                    h.before_rename()
                    raw = temporary.read_bytes()
                    REPLACE(temporary, destination)
                    consume(destination, raw)  # Offline simulator consumes one real replacement.
                    h.after_rename()

                driver.artifacts.sink.event = Mock()
                driver.artifacts.sink.save = Mock(wraps=driver.artifacts.sink.save)
                with patch.object(m.transport, "atomic", ATOMIC), \
                        patch.object(m.transport, "time", clock), \
                        patch.object(m.transport.os, "replace", side_effect=replace):
                    yield h
        finally:
            self.assertEqual(directory.resolve().parent, scratch)
            shutil.rmtree(directory)

    def assert_deferred(self, driver):
        driver.artifacts.sink.save.assert_not_called()
        driver.artifacts.sink.event.assert_not_called()
        self.assertNotIn("command_publication_trace", [event["type"] for event in driver.artifacts.events])
        self.assertIsNone(driver._publication_trace)

    def assert_unpublished(self, h, previous):
        d = h.driver
        self.assertEqual((d.sequence, d.sentinel, d.command_hash), previous)
        self.assertFalse(d.pending_command)
        self.assertFalse(d.unsafe_owner)
        self.assertFalse(d.held_left)
        self.assertEqual(d.commands, 0)
        self.assertEqual(h.published, [])
        self.assertEqual(d.command_path.read_bytes(), b"note previous\n")
        self.assertEqual(list(h.directory.iterdir()), [d.command_path])
        self.assertEqual([row["pending"] for row in h.owners], [True, False])
        self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
        self.assert_deferred(d)

    def test_slow_owner_session_state_and_command_reads_locate_consumed_lease(self):
        cases = (("owner_check", "validation.owner_check", "validation.session_read_decode"),
                 ("owner_record", "validation.pending_owner_read", "validation.pending_owner_read"),
                 ("session", "validation.session_read_decode", "validation.session_read_decode"),
                 ("state", "validation.state_setup", "validation.state_validation"),
                 ("command", "validation.previous_command_read_validation", "validation.previous_command_read_validation"))
        for operation, delayed_phase, failed_phase in cases:
            with self.subTest(operation=operation), self.publication() as h:
                d, clock = h.driver, h.clock
                previous = d.sequence, d.sentinel, d.command_hash
                requested, delayed = clock.now, False
                read, state = m.transport.read_bytes.side_effect, m.sim_state.side_effect
                validate_owner = d.validate_pending_owner

                def stall():
                    nonlocal delayed
                    if not delayed and d._publication_trace.context == "validation":
                        delayed = True
                        clock.now += 40 * MS

                def read_bytes(path, maximum):
                    target = {"session": d.session_path, "command": d.command_path}.get(operation)
                    if path == target:
                        stall()
                    return read(path, maximum)

                def read_state(*args):
                    if operation == "state":
                        stall()
                    return state(*args)

                def validate_pending_owner(expected_bytes, deadline_ns):
                    # Owner payload reads are gone; stall the sealed metadata
                    # validation that now occupies the same publication phase.
                    if operation == "owner_record":
                        stall()
                    return validate_owner(expected_bytes, deadline_ns)

                d.owner.check.side_effect = stall if operation == "owner_check" else None
                with patch.object(m.transport, "read_bytes", side_effect=read_bytes), \
                        patch.object(m, "sim_state", side_effect=read_state), \
                        patch.object(d, "validate_pending_owner", side_effect=validate_pending_owner), \
                        self.assertRaises(m.ReadDeadline):
                    d.send(fixtures.sample(0, "neutral", 0, .1))
                self.assert_unpublished(h, previous)
                report = d._publication_failures[0].report()
                self.assertEqual(report["publicationOutcome"], "not_published")
                self.assertEqual(report["originalPublicationDeadlineNs"], requested + 35 * MS)
                self.assertEqual(report["originalAckDeadlineNs"], requested + m.ACK_NS)
                self.assertEqual(report["sourceDeadlineNs"], d.initial_mapping_deadline)
                self.assertEqual(report["originalActionDeadlineNs"], d.stage_deadline)
                self.assertEqual(report["failure"]["kind"], "ReadDeadline")
                self.assertEqual(report["failure"]["phase"], failed_phase)
                self.assertEqual(report["failure"]["atNs"], requested + 40 * MS)
                self.assertEqual(report["phaseNs"][delayed_phase], 40 * MS)
                self.assertEqual(report["validationCalls"], 1)
                self.assertEqual(h.rename_attempts, 0)
                self.assertEqual(report["paths"]["command_path"], str(d.command_path))

    def test_slow_pending_owner_save_is_separate_from_command_preparation(self):
        with self.publication() as h:
            d, save = h.driver, h.driver.save_owner
            previous = d.sequence, d.sentinel, d.command_hash

            def slow_save(pending):
                save(pending)
                if pending:
                    h.clock.now += 40 * MS

            d.save_owner = slow_save
            with self.assertRaisesRegex(m.Refusal, "initial mapping expired during owner publication"):
                d.send(fixtures.sample(0, "neutral", 0, .1))
            self.assert_unpublished(h, previous)
            report = d._publication_failures[0].report()
            self.assertEqual(report["phaseNs"]["initial.pending_owner_save"], 40 * MS)
            self.assertEqual(report["failure"]["phase"], "initial.publication_bounds")
            self.assertNotIn("transport.command_preparation", report["phaseNs"])
            self.assertEqual(h.rename_attempts, 0)

    def test_owner_refusal_is_preserved_before_slow_temporary_cleanup(self):
        with self.publication() as h:
            d, requested = h.driver, h.clock.now
            unlink = Path.unlink

            def changed_owner():
                if d._publication_trace.context == "validation":
                    raise m.Refusal("offline process owner changed")

            def slow_unlink(path, *args, **kwargs):
                if path.suffix == ".tmp":
                    h.clock.now += 40 * MS
                return unlink(path, *args, **kwargs)

            d.owner.check.side_effect = changed_owner
            with patch.object(Path, "unlink", slow_unlink), \
                    self.assertRaisesRegex(m.Refusal, "offline process owner changed"):
                d.send(fixtures.sample(0, "neutral", 0, .1))
            report = d._publication_failures[0].report()
            self.assertEqual(report["failure"]["phase"], "validation.owner_check")
            self.assertEqual(report["failure"]["atNs"], requested)
            self.assertEqual(report["phaseNs"]["validation.exception_unwind"], 40 * MS)
            self.assertEqual(report["publicationOutcome"], "not_published")
            self.assertTrue(d.unsafe_owner)
            self.assertFalse(d.pending_command)
            self.assertEqual([row["pending"] for row in h.owners], [True])
            self.assertEqual(d.command_path.read_bytes(), b"note previous\n")
            self.assertEqual(h.rename_attempts, 0)
            self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
            self.assert_deferred(d)

    def test_slow_temporary_preparation_refuses_before_validation_or_rename(self):
        with self.publication() as h:
            d, local_path = h.driver, m.transport.local_path
            previous = d.sequence, d.sentinel, d.command_hash
            checks = 0

            def slow_preparation(path):
                nonlocal checks
                if path == d.command_path:
                    checks += 1
                    # The second existing destination check follows temp write/flush.
                    if checks == 2:
                        h.clock.now += 40 * MS
                return local_path(path)

            with patch.object(m.transport, "local_path", side_effect=slow_preparation), \
                    self.assertRaisesRegex(m.Refusal, "original command publication deadline exceeded"):
                d.send(fixtures.sample(0, "neutral", 0, .1))
            self.assert_unpublished(h, previous)
            report = d._publication_failures[0].report()
            self.assertEqual(report["phaseNs"]["transport.command_preparation"], 40 * MS)
            self.assertEqual(report["failure"]["phase"], "transport.command_preparation")
            self.assertEqual(report["validationCalls"], 0)
            self.assertEqual(h.rename_attempts, 0)

    def test_slow_refused_rename_preserves_original_error_and_prior_owner(self):
        with self.publication() as h:
            d = h.driver
            previous = d.sequence, d.sentinel, d.command_hash

            def refuse():
                h.clock.now += 40 * MS
                raise OSError("offline slow rename refused")

            h.before_rename = refuse
            with self.assertRaisesRegex(OSError, "offline slow rename refused"):
                d.send(fixtures.sample(0, "neutral", 0, .1))
            self.assert_unpublished(h, previous)
            report = d._publication_failures[0].report()
            self.assertEqual(report["failure"]["kind"], "OSError")
            self.assertEqual(report["failure"]["phase"], "transport.replace_or_retry")
            self.assertEqual(report["phaseNs"]["transport.replace_or_retry"], 40 * MS)
            self.assertEqual(report["publicationOutcome"], "not_published")
            self.assertEqual(h.rename_attempts, 1)

    def test_uncertain_replace_never_claims_no_publication_or_replays_input(self):
        with self.publication() as h:
            d = h.driver

            def uncertain():
                raise OSError("offline interruption after replacement")

            h.after_rename = uncertain
            with self.assertRaisesRegex(m.Refusal, "unpublished owner restoration refused"):
                d.send(fixtures.sample(0, "neutral", 0, .1))
            report = d._publication_failures[0].report()
            self.assertEqual(report["publicationOutcome"], "publication_uncertain")
            self.assertEqual(report["failure"]["kind"], "OSError")
            self.assertEqual(report["failure"]["phase"], "transport.replace_or_retry")
            self.assertEqual(report["terminalFailure"]["kind"], "Refusal")
            self.assertIsNone(report["publishedNs"])
            self.assertTrue(d.unsafe_owner)
            self.assertFalse(d.pending_command)
            self.assertEqual(d.commands, 0)  # Existing counter requires successful publisher return.
            self.assertEqual(len(h.published), 1)
            self.assertEqual(h.rename_attempts, 1)
            self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
            self.assertEqual(h.rename_attempts, 1)
            self.assert_deferred(d)

    def test_settled_slow_rename_keeps_bytes_deadlines_and_defers_evidence(self):
        with self.publication() as h:
            d, requested = h.driver, h.clock.now
            h.before_rename = lambda: setattr(h.clock, "now", h.clock.now + 10 * MS)
            ordinary = fixtures.sample(0, "neutral", 0, .1)
            ordinary["commands"] = m.LEFT_POSE_SETUP + ordinary["commands"]
            with patch.object(m.PublicationTrace, "report", side_effect=AssertionError("report inside send")):
                d.send(ordinary)
            self.assert_deferred(d)
            report = d._last_publication_trace.report()
            self.assertEqual(report["publicationOutcome"], "settled")
            self.assertIsNone(report["failure"])
            self.assertEqual(report["publishedNs"], requested + 10 * MS)
            self.assertEqual(report["committedNs"], requested + 10 * MS)
            self.assertEqual(report["originalPublicationDeadlineNs"], requested + 35 * MS)
            self.assertEqual(report["originalAckDeadlineNs"], requested + m.ACK_NS)
            self.assertEqual(report["phaseNs"]["transport.replace_or_retry"], 10 * MS)
            self.assertEqual(h.published, [("\n".join(ordinary["commands"] + [d.sentinel]) + "\n").encode("ascii")])
            self.assertEqual(d.command_path.read_bytes(), h.published[0])
            self.assertEqual([row["pending"] for row in h.owners], [True, False])
            self.assertFalse(d.pending_command)
            self.assertFalse(d.unsafe_owner)
            self.assertFalse(d.held_left)
            self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
            d.lock = None
            d.observer.close = Mock(return_value=True)
            self.assertTrue(d.close())
            d.defer_publication_traces(True)
            d.artifacts.sink.event.assert_not_called()
            events = [event for event in d.artifacts.events if event["type"] == "command_publication_trace"]
            self.assertEqual(events, [dict(report, observerStopped=True)])
            d.artifacts.flush()
            self.assertEqual(sum(call.args[0]["type"] == "command_publication_trace"
                                 for call in d.artifacts.sink.event.call_args_list), 1)

    def test_existing_replace_retries_have_bounded_spans_and_one_command(self):
        with self.publication(source_ns=m.NS) as h:
            d, requested = h.driver, h.clock.now

            def sharing_violation():
                if h.rename_attempts <= 80:
                    error = OSError("offline sharing violation")
                    error.winerror = 32
                    raise error

            h.before_rename = sharing_violation
            d.send(fixtures.sample(0, "neutral", 0, .1))
            report = d._last_publication_trace.report()
            self.assertEqual(report["publicationOutcome"], "settled")
            self.assertEqual(report["validationCalls"], 81)
            self.assertEqual(h.rename_attempts, 81)
            self.assertEqual(len(h.published), 1)
            self.assertEqual(report["phaseNs"]["transport.replace_or_retry"], 80 * MS)
            self.assertEqual(report["retainedSpans"], m.MAX_PUBLICATION_TRACE_SPANS)
            self.assertGreater(report["droppedSpans"], 0)
            self.assertEqual(report["spans"][0]["phase"], "admission.checks")
            self.assertEqual(report["originalAckDeadlineNs"], requested + m.ACK_NS)
            self.assertEqual(report["originalPublicationDeadlineNs"], requested + m.ACK_NS)
            self.assertEqual(list(h.directory.iterdir()), [d.command_path])
            self.assert_deferred(d)

    def test_ack_failure_remains_published_unsettled_until_existing_cleanup(self):
        with self.publication(source_ns=m.NS) as h:
            d = h.driver
            h.alter_ack = lambda raw: b""
            with self.assertRaisesRegex(m.Refusal, "original command acknowledgement/commit deadline exceeded"):
                d.send(fixtures.sample(0, "acquire", 1, .1))
            report = d._publication_failures[0].report()
            self.assertEqual(report["publicationOutcome"], "published_unsettled")
            self.assertIsNotNone(report["publishedNs"])
            self.assertIsNone(report["committedNs"])
            self.assertTrue(d.pending_command)
            self.assertFalse(d.unsafe_owner)
            self.assertEqual(len(h.published), 1)
            self.assert_deferred(d)
            h.alter_ack = lambda raw: raw
            self.assertEqual(d.cleanup(), "ordinary owned left-grip release acknowledged")
            self.assertEqual(len(h.published), 2)
            self.assertEqual(h.published[1].decode().splitlines()[:-1], ["grip l 0"])
            self.assertEqual(d._publication_failures[0].report(), report)
            self.assertEqual(d._last_publication_trace.report()["publicationOutcome"], "settled")
            self.assertTrue(d._last_publication_trace.report()["cleanup"])
            self.assertIsNone(d._last_publication_trace.report()["sourceDeadlineNs"])

    def test_retained_failure_count_is_bounded_and_flush_emits_only_once(self):
        with self.publication() as h:
            d = h.driver
            d.observation_failure = "offline existing observation refusal"
            for _ in range(10):
                with self.assertRaisesRegex(m.Refusal, "previous native observation failure"):
                    d.send(fixtures.sample(0, "neutral", 0, .1))
            self.assertEqual(len(d._publication_failures), 2)
            self.assertEqual(h.rename_attempts, 0)
            self.assert_deferred(d)
            d.defer_publication_traces(False)
            d.defer_publication_traces(False)
            events = [event for event in d.artifacts.events if event["type"] == "command_publication_trace"]
            self.assertEqual(len(events), 2)
            self.assertTrue(all(event["publicationOutcome"] == "not_published" for event in events))
            self.assertEqual(d._publication_failures, [])


if __name__ == "__main__":
    unittest.main()
