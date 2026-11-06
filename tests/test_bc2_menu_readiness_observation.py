"""Deterministic initial menu admission; synthetic recovery is not a native pass."""
import copy
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    "menu_readiness_transport_fixture", Path(__file__).with_name("test_bc2_resume_driver.py"))
base = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(base)
r, d, NS = base.r, base.d, base.NS


class InitialMenuReadinessTests(unittest.TestCase):
    def harness(self, schedule=None):
        h = base.SinglePlayerSelectionTests.harness(self, schedule)
        h.v.menu_capture["displayTimeNs"] = h.now
        # Exercise real read_state/ownership/send, replacing only OS/filesystem
        # and simulator acknowledgements. No live process or input is used.
        h.owner_checks = 0
        def owner_check():
            h.owner_checks += 1
            d.require(h.owner_alive, "synthetic exact process owner exited")
        h.owner_alive = True
        h.v.owner = SimpleNamespace(check=owner_check)
        h.v.folder = Path("memory-session")
        h.v.session_path = h.v.folder / "session.json"
        h.v.state_path = h.v.run / "sim/state.json"
        h.v.link = dict(supervisor=dict(pid=2, start_time="synthetic"))
        h.v.manifest = dict(source_sha256="synthetic", version="synthetic")
        h.v.capture_plan = dict(maximum_compositions=5)
        h.session = dict(host=h.v.host, supervisor=h.v.link["supervisor"],
                         source_sha256="synthetic", version="synthetic", state="running")
        h.state.update(sessionRunning=True, actionsAttached=True, paceMode="free")
        for name in ("head", "handL", "handR"):
            h.state[name]["valid"] = True
        def read_json(path, maximum=None):
            return copy.deepcopy({h.v.session_path: h.session, h.v.state_path: h.state}[path]), b""
        self.enterContext(patch.object(d, "read_json", read_json))
        h.v.read_state = d.Driver.read_state.__get__(h.v)
        return h

    def assert_untouched(self, h):
        self.assertEqual(h.writes, [])
        self.assertEqual(h.v.commands, 0)
        self.assertEqual(h.v.clicks, 0)
        self.assertFalse(h.v.pending_command or h.v.held_trigger)

    def test_saved_initial_false_then_synthetic_fresh_true_delivers_exactly_one_click(self):
        h = self.harness()
        # Saved actual452/fixed65/xr-game07 initial row and invocation clock.
        h.now = h.v.started = 105658027361600
        h.v.hard_deadline = h.now + 10 * NS
        h.v.deadline = h.v.stage_deadline = h.v.hard_deadline - r.RELEASE_RESERVE_NS
        h.v.menu_capture["displayTimeNs"] = 105655652988041
        def native():
            elapsed = (h.now - h.v.started) // NS
            return dict(selected_knife=dict(sample_ns=105657752749700 + elapsed * NS),
                        elapsed_ms=13265 + elapsed * 1000, world_calls=0,
                        menu_connection=dict(ready=elapsed >= 1, input_samples=2255 + elapsed * 30,
                                             published=340 + elapsed * 20, graphics_failures=0))
        h.v.tail.read = native
        deadlines = h.v.hard_deadline, h.v.deadline, h.v.stage_deadline
        self.assertTrue(h.v.resume())
        self.assertGreaterEqual(h.writes[0][0], h.v.started + NS)
        self.assertEqual(len(h.writes), 3)
        self.assertEqual(len(h.downs()), 1)
        self.assertEqual(h.v.clicks, 1)
        self.assertEqual(h.v.commands, 9)  # Existing neutral/down/up batches and their sentinels.
        self.assertEqual(h.writes[-1][1][0], "trigger r 0")
        self.assertFalse(h.v.pending_command or h.v.held_trigger)
        self.assertEqual(deadlines, (h.v.hard_deadline, h.v.deadline, h.v.stage_deadline))
        first = h.v.artifacts.events[0]["native"]
        self.assertEqual(first, dict(sampleNs=105657752749700, elapsedMs=13265, worldCalls=0,
                                     menuReady=False, menuInputs=2255, menuPublished=340,
                                     menuGraphicsFailures=0))
        self.assertGreater(h.owner_checks, 20)

    def test_never_ready_exhausts_original_deadline_without_renewal_or_commands(self):
        h = self.harness(lambda elapsed: (0, False))
        # Constructor/admission work has already consumed part of the same budget.
        h.now += 3 * NS
        deadlines = h.v.hard_deadline, h.v.deadline, h.v.stage_deadline
        with self.assertRaisesRegex(r.REFUSAL, "Resume observation deadline reached"):
            h.v.resume()
        self.assertEqual(h.now, deadlines[1])
        self.assertLess(h.now - h.v.started, 10 * NS)
        self.assertEqual(deadlines, (h.v.hard_deadline, h.v.deadline, h.v.stage_deadline))
        self.assert_untouched(h)

    def test_wait_cannot_extend_reviewed_capture_age(self):
        h = self.harness(lambda elapsed: (0, False))
        h.v.menu_capture["displayTimeNs"] = h.now - 60 * NS + 50_000_000
        with self.assertRaisesRegex(r.REFUSAL, "menu capture is stale"):
            h.v.resume()
        self.assertEqual(h.now - h.v.started, 100_000_000)
        self.assert_untouched(h)

    def test_ready_observation_crossing_capture_age_cannot_authorize_input(self):
        h = self.harness()
        h.v.menu_capture["displayTimeNs"] = h.now - 60 * NS + 10_000_000
        def native():
            h.now += 20_000_000
            return h.native()
        h.v.tail.read = native
        with self.assertRaisesRegex(r.REFUSAL, "menu capture is stale"):
            h.v.resume()
        self.assert_untouched(h)

    def test_ready_observation_crossing_original_deadline_cannot_authorize_input(self):
        h = self.harness()
        def native():
            h.now = h.v.deadline
            return h.native()
        h.v.tail.read = native
        with self.assertRaisesRegex(r.REFUSAL, "Resume observation deadline reached"):
            h.v.resume()
        self.assertEqual(h.now, h.v.started + 10 * NS - r.RELEASE_RESERVE_NS)
        self.assert_untouched(h)

    def test_world_appearing_during_initial_wait_is_terminal(self):
        h = self.harness(lambda elapsed: (elapsed, elapsed > 0))
        with self.assertRaisesRegex(r.REFUSAL, "menu changed before initial input"):
            h.v.resume()
        self.assertEqual(h.v.input_stopped, "world observed")
        self.assert_untouched(h)

    def test_graphics_failure_on_not_ready_row_is_terminal(self):
        h = self.harness(lambda elapsed: (0, False))
        row = h.native()
        row["menu_connection"]["graphics_failures"] = 1
        h.v.tail.read = lambda: row
        with self.assertRaisesRegex(r.REFUSAL, "native menu graphics failed"):
            h.v.resume()
        self.assertEqual(h.now, h.v.started)
        self.assert_untouched(h)

    def test_command_owner_change_during_wait_is_terminal(self):
        h = self.harness(lambda elapsed: (0, elapsed > 0))
        h.on_sleep = lambda: h.files.update({h.v.command_path: b"different writer\n"})
        with self.assertRaisesRegex(r.REFUSAL, "another writer"):
            h.v.resume()
        self.assertTrue(h.v.unsafe_owner)
        self.assert_untouched(h)

    def test_process_session_stop_focus_and_native_errors_remain_terminal(self):
        cases = (
            (lambda h: setattr(h, "owner_alive", False), "process owner exited"),
            (lambda h: h.session.update(state="stopped"), "exact owners"),
            (lambda h: h.files.update({h.v.folder / "stop.request": b"1"}), "stop was requested"),
            (lambda h: h.state.update(sessionState="VISIBLE"), "session/focus/tracking"),
            (lambda h: h.state.update(errors=1), "simulator reports an error"),
        )
        for mutate, error in cases:
            with self.subTest(error=error):
                h = self.harness(lambda elapsed: (0, elapsed > 0))
                h.on_sleep = lambda: mutate(h)
                with self.assertRaisesRegex(r.REFUSAL, error):
                    h.v.resume()
                self.assertEqual(h.now - h.v.started, 50_000_000)
                self.assert_untouched(h)

    def test_unchanged_false_sample_becoming_stale_is_refused(self):
        h = self.harness(lambda elapsed: (0, False))
        row = h.native()
        h.v.tail.read = lambda: row
        with self.assertRaisesRegex(r.REFUSAL, "native telemetry is stale"):
            h.v.resume()
        self.assertEqual(h.now - h.v.started, r.MAX_AGE_NS + 50_000_000)
        self.assert_untouched(h)

    def test_readiness_change_inside_same_sample_is_not_fresh_authority(self):
        h = self.harness(lambda elapsed: (0, False))
        row = h.native()
        h.v.tail.read = lambda: copy.deepcopy(row)
        h.on_sleep = lambda: row["menu_connection"].update(ready=True)
        with self.assertRaisesRegex(r.REFUSAL, "changed within one sample"):
            h.v.resume()
        self.assert_untouched(h)

    def test_native_counter_regression_remains_terminal(self):
        h = self.harness(lambda elapsed: (0, False))
        row = h.native()
        h.v.tail.read = lambda: copy.deepcopy(row)
        def regress():
            row["selected_knife"]["sample_ns"] = h.now
            row["menu_connection"].update(ready=True, published=99)
        h.on_sleep = regress
        with self.assertRaisesRegex(r.REFUSAL, "regressed: menuPublished"):
            h.v.resume()
        self.assert_untouched(h)

    def test_readiness_loss_after_admission_before_publication_is_not_retried(self):
        h = self.harness(lambda elapsed: (0, False))
        row, reads = h.native(), []
        def native():
            reads.append(h.now)
            current = copy.deepcopy(row)
            current["selected_knife"]["sample_ns"] = h.now
            current["menu_connection"]["ready"] = len(reads) == 2
            if len(reads) > 2:
                # A distinct sample loses readiness before any neutral command.
                h.now += 1
                current["selected_knife"]["sample_ns"] = h.now
            return current
        h.v.tail.read = native
        with self.assertRaises(r.MenuTransition):
            h.v.resume()
        self.assertEqual(h.v.input_stopped, "menu readiness lost; possible loading")
        self.assert_untouched(h)


if __name__ == "__main__":
    unittest.main()
