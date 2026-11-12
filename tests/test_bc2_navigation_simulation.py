"""Assembled synthetic navigation CLI; no game, live session or native evidence.

Real shared construction, publication owner HANDLE, atomic command publication,
ACK parsing, later-frame commitment and release run against temporary files.
Only host liveness/lock and the simulator/native publishers are substituted.
"""
import copy
import gzip
from contextlib import ExitStack, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fixtures = load("navigation_native_fixture", ROOT / "tests/test_bc2_holster_shared_entry.py")


class Clock:
    def __init__(self):
        self.now = 2_000_000_000

    def tick(self):
        self.now += 10_000
        return self.now

    def sleep(self, seconds):
        self.now += int(seconds * 1_000_000_000)


@unittest.skipUnless(sys.platform == "win32", "shared publication HANDLE path requires Windows")
class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="navigation-offline-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ("drive_bc2_navigation_simulation.py", "drive_bc2_feed_simulation.py", "plan_bc2_feed_simulation.py"):
            target = self.root / "tools" / name
            target.parent.mkdir(exist_ok=True)
            shutil.copyfile(ROOT / "tools" / name, target)
        self.m = load("navigation_" + self.root.name, self.root / "tools/drive_bc2_navigation_simulation.py")
        self.clock = Clock()
        self.run, self.folder = self.root / "run", self.root / "session"
        (self.run / "sim").mkdir(parents=True)
        self.folder.mkdir()
        self.version, self.source = "0.1.999", "a" * 64
        for name in ("host.exe", "xrsim.dll", "runtime.json"):
            (self.root / name).write_bytes(b"synthetic-only")
        pin = lambda name: dict(path=str(self.root / name), bytes=(self.root / name).stat().st_size,
                                sha256=self.m.t.digest((self.root / name).read_bytes()))
        self.host = dict(pid=999, path=str(self.root / "host.exe"), start_utc="2026-01-01T00:00:00Z")
        self.manifest = dict(schema="fvr.xr-game-run.v1", output_directory=str(self.run),
            version=self.version, source_sha256=self.source, runtime_manifest=pin("runtime.json"),
            runtime_library=pin("xrsim.dll"), host_executable=pin("host.exe"),
            capture_plan=dict(periodic=False, layers=False, maximum_compositions=3, width=640, height=480))
        self.write(self.run / "run-manifest.json", self.manifest)
        controls = {key: False for key in ("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
        controls.update(trigL=0, trigR=0, gripL=0, gripR=0, stickL=[0, 0], stickR=[0, 0])
        self.state = dict(pid=999, runtime="xr-sim", errors=0, lastCmdError="", sessionRunning=True,
            actionsAttached=True, sessionState="FOCUSED", paceMode="free", captureSeq=0, cmdSeq=1,
            lastCmd="note startup", frame=1, endFrames=1, controls=controls,
            head=dict(valid=True, pos=[0, 1.6, 0], ypr=[0, 0, 0]),
            handL=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]),
            handR=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]))
        self.initial_state = copy.deepcopy(self.state)
        self.write(self.run / "sim/state.json", self.state)
        (self.run / "sim/command.txt").write_bytes(b"note startup\n")
        self.write(self.run / "sim-startup.json", dict(root_commands_ready=True, status="ready", host=self.host,
            target_sequence=1, command_file=dict(sha256=self.m.t.digest(b"note startup\n")), settled_state=self.state))
        self.link = dict(root_commands_ready=True, host=self.host, supervisor=dict(pid=111), folder=str(self.folder),
            sim_state=str(self.run / "sim/state.json"), command=str(self.run / "sim/command.txt"),
            session=str(self.folder / "session.json"), options=str(self.folder / "options.json"),
            stop_request=str(self.folder / "stop.request"), startup_receipt=str(self.run / "sim-startup.json"),
            mechanism_snapshots=dict(requested=True))
        self.write(self.run / "session-link.json", self.link)
        self.session = dict(host=self.host, supervisor=self.link["supervisor"], source_sha256=self.source,
                            version=self.version, state="running")
        self.write(self.folder / "session.json", self.session)
        self.write(self.folder / "options.json", dict(source_sha256=self.source, version=self.version,
            activate_window=False, xr_manifest=str(self.root / "runtime.json")))
        (self.folder / "mechanism-snapshot.enable").write_bytes(b"1\n")
        self.rows = [dict(left=[0, .5], right=[0, 0], holdMs=180, lookDegrees=[10, 0])]
        self.calls, self.samples, self.owner_records = [], [], []
        self.driver = None
        self.snapshot_mutation = None
        self.state_mutation = None
        self.ack_delay = 0
        self.suppress_ack = False
        self.cleanup_failure = False
        self.publication_delay = 0
        self.competing_writer = False
        self.omit_later_frame = False
        self.held_owner_records = []
        self.bad_ack_once = False

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def argv(self):
        self.write(self.run / "navigation.json", self.rows)
        return ["--run", str(self.run), "--navigation", str(self.run / "navigation.json"),
                "--version", self.version, "--source-sha256", self.source,
                "--root-authorized-ordinary-navigation"]

    def snapshot(self, driver):
        now = self.clock.tick()
        s = fixtures.outer_snapshot(now, driver.pending_nonce, now // 10_000, "held")
        s["input"]["menu"] = dict(owner=1, sequence=now // 10_000, epoch=7, deadlineNs=now+70_000_000)
        s["input"]["head"] = dict(position=self.state["head"]["pos"][:],
            orientation=self.m.p.ypr_quaternion(self.state["head"]["ypr"]))
        for index, hand in enumerate(("handL", "handR")):
            pose = dict(position=self.state[hand]["pos"][:],
                        orientation=self.m.p.ypr_quaternion(self.state[hand]["ypr"]))
            s["input"]["hands"][index].update(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose),
                held=0, squeeze=0, trigger=0, stickX=self.state["controls"]["stickL" if index == 0 else "stickR"][0],
                stickY=self.state["controls"]["stickL" if index == 0 else "stickR"][1])
        if self.snapshot_mutation:
            self.snapshot_mutation(s, driver)
        self.samples.append(copy.deepcopy(s))
        if any(any(v) for v in driver.sticks):
            self.held_owner_records.append(json.loads(driver.owner_bytes(False)))
        self.write(driver.snapshot_path, s)

    def assembled(self, args=None):
        m, current, pending_ack = self.m, b"note startup\n", None
        original_request = m.t.Driver.request_snapshot
        original_init = m.NavigationDriver.__init__
        original_publish = m.t.publish_command
        original_read = m.t.read_bytes
        test = self

        def simulate():
            nonlocal current, pending_ack
            path = self.run / "sim/command.txt"
            raw = path.read_bytes()
            if raw != current:
                current = raw
                batch = raw.decode("ascii").splitlines()
                if batch[:-1]:
                    self.calls.append(batch[:-1])
                    self.owner_records.append(json.loads((self.run / "live-driver-owner.json").read_text()))
                for line in batch[:-1]:
                    words = line.split()
                    if words[0] == "stick":
                        self.state["controls"]["stickL" if words[1] == "l" else "stickR"] = list(map(float, words[2:]))
                    elif words[:2] == ["head", "pose"]:
                        numbers = list(map(float, words[2:]))
                        self.state["head"].update(pos=numbers[:3], ypr=numbers[3:])
                    else:
                        raise AssertionError("unexpected simulator input: " + line)
                self.state.update(cmdSeq=self.state["cmdSeq"]+len(batch), lastCmd=batch[-1],
                                  frame=self.state["frame"]+1)
                pending_ack = (self.clock.now+self.ack_delay, batch, self.state["frame"])
            if pending_ack and self.clock.now >= pending_ack[0] and not self.suppress_ack:
                _, batch, frame = pending_ack
                (self.run / "sim/ack.txt").write_text(
                    f"seq={self.state['cmdSeq']} frame={frame} error=-\nlast={' | '.join(batch)}\n", encoding="utf-8")
                if not self.omit_later_frame:
                    self.state.update(frame=frame+1, endFrames=self.state["endFrames"]+1)
                pending_ack = None
            if self.state_mutation and self.calls:
                self.state_mutation(self.state, self.driver)
            self.write(self.run / "sim/state.json", self.state)

        class Owner:
            def __init__(self, expected):
                self.api = SimpleNamespace(CloseHandle=lambda handle: None)
            def check(self):
                simulate()
            def close(self):
                pass

        def initialize(driver, args):
            self.driver = driver
            original_init(driver, args)

        def request(driver):
            original_request(driver)
            self.snapshot(driver)

        def publish(path, raw, deadline_ns, validate):
            self.clock.now += self.publication_delay
            if self.cleanup_failure and self.driver.cleanup_mode:
                raise OSError("synthetic zero publication failure")
            if self.competing_writer and self.calls:
                path.write_bytes(b"note competing-writer\n")
            return original_publish(path, raw, deadline_ns, validate)

        def read(path, maximum):
            raw = original_read(path, maximum)
            if self.bad_ack_once and path.name == "ack.txt":
                self.bad_ack_once = False
                raw = raw.split(b"last=")[0] + b"last=foreign batch\n"
            return raw

        with ExitStack() as stack:
            stack.enter_context(patch.object(m.time, "perf_counter_ns", self.clock.tick))
            stack.enter_context(patch.object(m.time, "sleep", self.clock.sleep))
            stack.enter_context(patch.object(m.t, "WindowsOwner", Owner))
            stack.enter_context(patch.object(m.t.Driver, "acquire_lock", lambda driver: None))
            stack.enter_context(patch.object(m.NavigationDriver, "__init__", initialize))
            stack.enter_context(patch.object(m.t.Driver, "request_snapshot", request))
            stack.enter_context(patch.object(m.t, "publish_command", publish))
            stack.enter_context(patch.object(m.t, "read_bytes", read))
            output = stack.enter_context(redirect_stdout(io.StringIO()))
            code = m.main(self.argv() if args is None else args)
        return code, json.loads(output.getvalue().strip().splitlines()[-1])

    def test_real_assembled_head_stick_zero_and_second_episode(self):
        self.rows.append(dict(left=[.25, 0], right=[.1, 0], holdMs=160))
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(len(self.calls), 4)
        self.assertEqual(self.calls[1], self.m.ZERO)
        self.assertEqual(self.calls[3], self.m.ZERO)
        self.assertTrue(all(record["pending"] for record in self.owner_records))
        self.assertTrue(self.held_owner_records)
        self.assertTrue(all(record["pending"] and not record["navigation"]["neutralCommitted"]
                            for record in self.held_owner_records))
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertFalse(owner["pending"])
        self.assertTrue(owner["navigation"]["neutralCommitted"])
        self.assertEqual(self.state["head"]["ypr"], [10, 0, 0])
        for key in ("handL", "handR"):
            self.assertEqual(self.state[key], self.initial_state[key])
        self.assertEqual(self.state["head"]["pos"], self.initial_state["head"]["pos"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["objectiveEntry"])
        self.assertEqual(len(result["episodes"]), 2)

    def test_explicit_root_and_reviewed_batch_required(self):
        args = self.argv()[:-1]
        code, result = self.assembled(args)
        self.assertEqual(code, 2)
        self.assertIn("explicit ROOT", result["error"])
        self.assertEqual(self.calls, [])
        with self.assertRaises(self.m.Refusal):
            self.m.plan([dict(left=[0, 1], right=[0, 0], holdMs=501)])
        with self.assertRaises(self.m.Refusal):
            self.m.plan([dict(left=[0, 1], right=[0, 0], holdMs=150, commands=["trigger r 1"])])

    def test_delayed_ack_consumes_original_pulse_and_explicit_zero(self):
        self.ack_delay = 35_000_000
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertLess(result["episodes"][0]["neutralCommittedNs"]-result["episodes"][0]["startedNs"],
                        self.rows[0]["holdMs"]*1_000_000)

    def test_actor_loss_releases_owned_sticks_and_refuses_next_episode(self):
        self.rows.append(dict(left=[1, 0], right=[0, 0], holdMs=150))
        def mutation(s, driver):
            if self.calls:
                s["owner"]["actorGeneration"] += 1
                s["input_evidence"]["owner"]["actorGeneration"] += 1
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("lifetime changed", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertEqual(len(self.calls), 2)
        self.assertFalse(result["normalStopRequired"])

    def test_source_expiry_fails_closed_without_restamping(self):
        def mutation(s, driver):
            if self.calls:
                s["valid_until_ns"] = s["input_evidence"]["deadlineNs"] = self.clock.now - 1
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertTrue(any(s["valid_until_ns"] < s["observed_ns"] for s in self.samples))
        self.assertFalse(result["nativeAcceptance"])

    def test_focus_loss_preserves_release_ownership(self):
        self.state_mutation = lambda state, driver: state.update(sessionState="VISIBLE")
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("focus/tracking lost", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_tracking_loss_preserves_release_ownership(self):
        self.state_mutation = lambda state, driver: state["handR"].update(valid=False)
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls[-1], self.m.ZERO)
        self.assertFalse(result["normalStopRequired"])

    def test_original_publication_lease_expires_before_write(self):
        self.publication_delay = 120_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertIn("deadline", result["error"])

    def test_missing_ack_preserves_pending_custody_and_requests_stop(self):
        self.suppress_ack = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("normal Stop", result["nextAction"])
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertTrue(owner["pending"])

    def test_cleanup_failure_never_claims_neutral_or_retries(self):
        self.state_mutation = lambda state, driver: state.update(sessionState="VISIBLE")
        self.cleanup_failure = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("synthetic zero publication failure", result["releaseError"])

    def test_competing_writer_means_no_further_input(self):
        self.competing_writer = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertIn("ownership", result["releaseError"])

    def test_source_version_mismatch_refuses_before_input(self):
        args = self.argv()
        args[args.index("--version")+1] = "0.1.998"
        code, result = self.assembled(args)
        self.assertEqual(code, 2)
        self.assertEqual(self.calls, [])
        self.assertIn("source/version", result["error"])

    def test_non_neutral_entry_cannot_release_someone_elses_sticks(self):
        self.state["controls"]["stickL"] = [0, 1]
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("neutral entry", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(self.state["controls"]["stickL"], [0, 1])

    def test_menu_loss_releases_without_claiming_playing(self):
        def mutation(s, driver):
            if self.calls:
                s["input"]["menu"]["owner"] = 3
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("Playing", result["error"])
        self.assertEqual(self.calls[-1], self.m.ZERO)

    def test_session_source_loss_forbids_further_input_and_requires_stop(self):
        def mutation(state, driver):
            self.session["source_sha256"] = "b"*64
            self.write(self.folder / "session.json", self.session)
        self.state_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_owner_record_replacement_forbids_zero(self):
        def mutation(s, driver):
            if self.calls:
                self.write(self.run / "live-driver-owner.json", dict(host=self.host, pending=False, foreign=True))
        self.snapshot_mutation = mutation
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_ack_requires_later_committed_frame(self):
        self.omit_later_frame = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])

    def test_late_ack_does_not_renew_pulse_deadline(self):
        self.ack_delay = 190_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertRegex(result["error"], "deadline|maximumGestureNs")
        self.assertEqual(result["episodes"], [])
        self.assertEqual(self.calls[-1], self.m.ZERO)

    def test_observed_ack_ambiguity_stays_revoked_after_file_recovers(self):
        self.bad_ack_once = True
        code, result = self.assembled()
        self.assertEqual(code, 2)
        self.assertIn("acknowledgement batch text mismatch", result["error"])
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_four_pulses_retain_original_large_responses_within_shared_disk_bound(self):
        self.rows = [dict(left=[0, .25], right=[0, 0], holdMs=400) for _ in range(4)]
        self.snapshot_mutation = lambda s, driver: s.update(observationPadding="x"*24_000)
        code, result = self.assembled()
        self.assertEqual((code, result.get("error")), (0, None))
        paths = sorted(Path(result["artifactDirectory"]).glob("snapshot-*.json.gz"))
        restored = [gzip.decompress(path.read_bytes()) for path in paths]
        self.assertGreater(sum(map(len, restored)), self.m.t.MAX_BYTES)
        self.assertLess(sum(path.stat().st_size for path in Path(result["artifactDirectory"]).iterdir()), self.m.t.MAX_BYTES)
        self.assertEqual([json.loads(raw) for raw in restored], self.samples)
        self.assertEqual(len(self.calls), 8)


if __name__ == "__main__":
    unittest.main()
