"""Offline clock/transport tests only; no game, process, or headset evidence."""
import copy
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("resume_driver", ROOT / "tools/resume_bc2_simulation.py")
r = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(r)
d, NS = r.transport, r.NS


class Artifacts:
    def __init__(self):
        self.events = []
        self.directory = Path("memory-artifacts")

    def event(self, row, cleanup=False):
        self.events.append(copy.deepcopy(row))


class Harness:
    """Use the real inherited send/ack/ownership/cleanup protocol in memory."""
    def __init__(self, test, schedule=None):
        self.now = 10 * NS
        self.writes = []
        self.pending = None
        self.schedule = schedule or (lambda elapsed: (0, True))
        self.on_sleep = None
        self.v = v = r.ResumeDriver.__new__(r.ResumeDriver)
        v.args = SimpleNamespace(max_clicks=3)
        v.started, v.hard_deadline = self.now, self.now + 35 * NS
        v.deadline = v.hard_deadline - r.RELEASE_RESERVE_NS
        v.stage_deadline = v.deadline
        v.latest, v.pre_event = None, None
        v.ever_ready, v.input_stopped, v.world_progressed = False, None, False
        v.clicks, v.last_click_sample = 0, -1
        v.neutral_sample, v.next_click = None, 0
        v.run = Path("memory")
        v.host = dict(pid=1, start_time="synthetic")
        v.command_path, v.ack_path = v.run / "command", v.run / "ack"
        v.command_hash = d.digest(b"initial\n")
        v.sequence, v.sentinel, v.capture_sequence = 0, "initial", 0
        v.commands, v.last_write, v.last_commit = 0, 0, None
        v.pending_command, v.unsafe_owner, v.monitor = False, False, False
        v.snapshot_refresh_started = None
        v.motion_started, v.reference = None, None
        v.held_left, v.held_right, v.held_trigger, v.held_buttons = False, False, False, set()
        v.artifacts = Artifacts()
        controls = dict(trigR=0., trigL=0., gripR=0., gripL=0., a=False, b=False,
                        x=False, y=False, menu=False, clickL=False, clickR=False,
                        stickL=[0, 0], stickR=[0, 0])
        self.state = dict(pid=1, runtime="xr-sim", cmdSeq=0, lastCmd="initial", lastCmdError="",
                          errors=0, frame=100, endFrames=99, captureSeq=0, sessionState="FOCUSED",
                          head={}, handL={}, handR={}, controls=controls)
        v.state = self.state
        v.read_state = lambda: copy.deepcopy(self.state)
        v.tail = SimpleNamespace(read=self.native)
        self.files = {v.command_path: b"initial\n"}
        for name, fn in (("perf_counter_ns", lambda: self.now), ("sleep", self.sleep)):
            test.enterContext(patch.object(r.time, name, fn))
        test.enterContext(patch.object(d, "read_bytes", lambda path, maximum: self.files[path]))
        test.enterContext(patch.object(d, "atomic", self.write))
        test.enterContext(patch.object(Path, "exists", lambda path: path in self.files))

    def native(self):
        elapsed = (self.now - self.v.started) // NS
        world, ready = self.schedule(elapsed)
        return dict(selected_knife=dict(sample_ns=self.v.started + elapsed * NS), elapsed_ms=elapsed * 1000,
                    world_calls=world, menu_connection=dict(ready=ready, input_samples=100 + elapsed * 30,
                    published=100 + elapsed * 20, graphics_failures=0))

    def write(self, path, raw, *, deadline_ns=None, validate=None):
        # Keep real owner serialization and the real pre-publication guard;
        # only replace the filesystem write and simulator acknowledgement.
        if validate is not None:
            d.require(self.now < deadline_ns, "original command publication deadline exceeded")
            validate()
            d.require(self.now < deadline_ns, "original command publication deadline exceeded")
        self.files[path] = raw
        if path == self.v.command_path:
            self.writes.append((self.now, raw.decode("ascii").splitlines()))
            self.pending = self.now + 5_000_000

    def sleep(self, seconds):
        assert seconds >= 0, "negative polling sleep"
        self.now += round(seconds * NS)
        if self.on_sleep:
            self.on_sleep()
        if self.pending is not None and self.now >= self.pending:
            batch = self.files[self.v.command_path].decode("ascii").splitlines()
            self.state.update(cmdSeq=self.state["cmdSeq"] + len(batch), lastCmd=batch[-1],
                              frame=self.state["frame"] + 3, endFrames=self.state["endFrames"] + 3)
            self.files[self.v.ack_path] = (
                f"seq={self.state['cmdSeq']} frame={self.state['frame'] - 1} error=-\n"
                f"last={' | '.join(batch)}\n").encode("ascii")
            self.pending = None

    def downs(self):
        return [stamp for stamp, batch in self.writes if "trigger r 1" in batch]


class ResumeDriverTests(unittest.TestCase):
    def test_delayed_world_requires_native_progress_and_preserves_event_receipts(self):
        h = Harness(self, lambda elapsed: (max(0, elapsed - 10) * 100, elapsed < 7))
        h.v.resume()
        self.assertTrue(h.v.world_progressed)
        self.assertGreaterEqual(h.now - h.v.started, 11 * NS)
        self.assertEqual(len(h.downs()), 2)
        self.assertFalse(h.v.held_trigger)
        events = h.v.artifacts.events
        self.assertEqual(sum(e["type"] == "resume_event_acknowledged" for e in events), 5)
        downs = [e for e in events if e["type"] == "resume_event_before" and e["event"] == "trigger-down"]
        self.assertLess(downs[0]["native"]["sampleNs"], downs[1]["native"]["sampleNs"])
        self.assertTrue(all(e.get("nativeWidgetAcceptance") is False for e in events if "event" in e))

    def test_initial_loading_never_sends_input(self):
        h = Harness(self, lambda elapsed: (0, False))
        self.assertFalse(h.v.resume())
        self.assertEqual(h.writes, [])
        self.assertLessEqual(h.now - h.v.started, 35 * NS)

    def test_initial_world_waits_for_progress_without_even_aiming(self):
        h = Harness(self, lambda elapsed: (100 + elapsed, False))
        h.v.resume()
        self.assertEqual(h.writes, [])
        self.assertTrue(h.v.world_progressed)

    def test_loading_latches_no_more_presses_even_if_ready_returns(self):
        h = Harness(self, lambda elapsed: (0, elapsed != 2))
        self.assertFalse(h.v.resume())
        self.assertEqual(len(h.downs()), 1)
        self.assertEqual(h.v.input_stopped, "menu readiness lost; possible loading")

    def test_acknowledged_clicks_without_world_fail_at_three_within_budget(self):
        h = Harness(self)
        self.assertFalse(h.v.resume())
        self.assertEqual(len(h.downs()), 3)
        self.assertFalse(h.v.world_progressed)
        self.assertFalse(h.v.held_trigger)
        self.assertLessEqual(h.now - h.v.started, 35 * NS)

    def test_episode_handoff_preserves_original_transport_lifetime_and_retry_state(self):
        h = Harness(self)
        h.v.hard_deadline = h.v.started + 10 * NS
        h.v.deadline = h.v.hard_deadline - r.RELEASE_RESERVE_NS
        h.v.stage_deadline = h.v.deadline
        self.assertFalse(h.v.resume())
        self.assertLess(h.now - h.v.started, 10 * NS)
        self.assertEqual(len(h.downs()), 2)
        self.assertFalse(h.v.held_trigger)
        self.assertFalse(h.v.pending_command)
        self.assertGreater(h.v.next_click, h.now)

    def test_final_publication_check_catches_transition_after_pre_event(self):
        h = Harness(self)
        h.v.pre_event = h.v.observe()
        h.v.last_write = h.now
        h.on_sleep = lambda: setattr(h, "schedule", lambda elapsed: (10, False))
        # Force a new native sample during the transport's publication spacing.
        h.v.tail.read = lambda: dict(selected_knife=dict(sample_ns=h.now), elapsed_ms=(h.now - h.v.started) // 1_000_000,
            world_calls=10, menu_connection=dict(ready=False, input_samples=100, published=100, graphics_failures=0))
        with self.assertRaises(r.MenuTransition):
            h.v.send(["trigger r 1"])
        self.assertEqual(h.writes, [])

    def test_world_during_down_ack_allows_only_owned_release(self):
        h = Harness(self)
        original = h.native
        def native():
            row = original()
            if h.downs() and h.now >= h.downs()[0] + 5_000_000:
                row["selected_knife"]["sample_ns"] = h.now
                row["world_calls"], row["menu_connection"]["ready"] = 1 + len(h.writes), False
            return row
        h.v.tail.read = native
        h.v.resume()
        self.assertEqual(len(h.downs()), 1)
        self.assertEqual(h.writes[-1][1][0], "trigger r 0")
        self.assertFalse(h.v.held_trigger)

    def test_slow_observation_crossing_hold_deadline_never_sleeps_negative(self):
        h = Harness(self, lambda elapsed: (max(0, elapsed - 1), elapsed < 2))
        original = h.v.observe
        def slow_observation():
            if h.v.held_trigger:
                h.now += 250_000_000
            return original()
        h.v.observe = slow_observation
        self.assertTrue(h.v.resume())
        self.assertFalse(h.v.held_trigger)

    def test_main_refuses_changed_session_identity_between_episodes(self):
        clock, created, resumed = [10 * NS], [], []
        def initialize(v, args):
            v.started = clock[0]
            v.manifest, v.startup = {}, {}
            v.link = dict(host=len(created))  # Synthetic replacement owner on episode two.
            v.tail = SimpleNamespace(path=Path("memory"))
            v.artifacts = SimpleNamespace(directory=Path("memory-artifacts"), save=lambda *a, **k: None)
            v.latest, v.pre_event, v.input_stopped = None, None, None
            v.ever_ready, v.world_progressed = False, False
            v.clicks, v.last_click_sample, v.next_click, v.commands = 0, -1, 0, 0
            v.neutral_sample = None
            v.pending_command, v.held_trigger = False, False
            created.append(v)
        def resume(v):
            resumed.append(v)
            clock[0] += 8 * NS
            return False
        output = io.StringIO()
        with patch.object(r.ResumeDriver, "__init__", initialize), \
             patch.object(r.ResumeDriver, "resume", resume), \
             patch.object(r.ResumeDriver, "close"), \
             patch.object(r.ResumeDriver, "release_after_failure", return_value="no held input"), \
             patch.object(r.time, "perf_counter_ns", lambda: clock[0]), \
             patch.object(r.sys, "stdout", output):
            self.assertEqual(r.main(["--run", "G:/memory"]), 2)
        self.assertEqual(len(resumed), 1)
        self.assertIn("generation changed across Resume episodes", output.getvalue())

    def test_cleanup_refuses_lost_command_ownership_and_expired_budget(self):
        h = Harness(self)
        h.v.held_trigger = True
        h.files[h.v.command_path] = b"another writer\n"
        with self.assertRaisesRegex(r.REFUSAL, "another writer"):
            h.v.release_after_failure()
        self.assertEqual(h.writes, [])
        self.assertTrue(h.v.unsafe_owner)
        h.v.unsafe_owner = False
        h.now = h.v.hard_deadline - 200_000_000
        self.assertEqual(h.v.release_after_failure(), "not attempted: bounded driver deadline")
        self.assertEqual(h.writes, [])

    def test_cleanup_releases_only_settled_owned_trigger(self):
        h = Harness(self)
        h.v.held_trigger = True
        self.assertEqual(h.v.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertEqual(h.writes[0][1][0], "trigger r 0")
        self.assertFalse(h.v.held_trigger)

    def test_stale_native_sample_never_authorizes_click(self):
        h = Harness(self)
        row = h.native()
        row["selected_knife"]["sample_ns"] -= 2 * NS
        h.v.tail.read = lambda: row
        with self.assertRaisesRegex(r.REFUSAL, "stale"):
            h.v.resume()
        self.assertEqual(h.writes, [])

    def test_native_generation_regression_is_refused(self):
        h = Harness(self)
        h.now += NS
        h.v.observe()
        h.v.tail.read = lambda: dict(selected_knife=dict(sample_ns=h.now), elapsed_ms=0,
            world_calls=0, menu_connection=dict(ready=True, input_samples=1, published=1, graphics_failures=0))
        with self.assertRaisesRegex(r.REFUSAL, "regressed"):
            h.v.observe()

    def test_tail_ignores_partial_last_row_and_refuses_replacement(self):
        raw = b'{"world_calls":7}\n{"world_calls":900}'
        stream = io.BytesIO(raw)
        stream.fileno = lambda: 9
        tail = r.NativeTail(Path("memory"))
        with patch.object(d, "local_path"), patch.object(Path, "open", return_value=stream), \
             patch.object(r.os, "fstat", return_value=SimpleNamespace(st_dev=1, st_ino=2, st_size=len(raw))):
            self.assertEqual(tail.read(), dict(world_calls=7))
        stream = io.BytesIO(raw)
        stream.fileno = lambda: 9
        with patch.object(d, "local_path"), patch.object(Path, "open", return_value=stream), \
             patch.object(r.os, "fstat", return_value=SimpleNamespace(st_dev=1, st_ino=3, st_size=len(raw))):
            with self.assertRaisesRegex(r.REFUSAL, "identity changed"):
                tail.read()


if __name__ == "__main__":
    unittest.main()
