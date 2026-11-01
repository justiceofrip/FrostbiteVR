"""Synthetic driver transport tests; no process, native, or headset evidence.

Run: python -B -m unittest discover -s tests -p test_bc2_feed_driver_observation.py
All filesystem, simulator, and clock effects are held in memory.
"""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("feed_driver", ROOT / "tools/drive_bc2_feed_simulation.py")
d = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(d)
NS = d.NS
MS = 1_000_000


def fixture(now, nonce=1):
    """Minimal synthetic identities exercise real admission, not production geometry."""
    rows = [dict(key=n, asset=f"test{n}", configurationPath=f"config{n}",
                 meshPath=f"mesh{n}", rigFingerprint=n) for n in range(1, 25)]
    owner = dict(player=1, soldier=2, weak=3, weapon=4, actorGeneration=5, equipGeneration=6, space=7)
    pose = dict(position=[0, 0, 0], orientation=[0, 0, 0, 1])
    hand = dict(grip=pose, aim=pose, squeeze=1., gripTracked=True, aimTracked=True)
    snapshot = dict(
        schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=nonce, valid=True,
        observed_ns=now, valid_until_ns=now + 80 * MS, native_deadline_ns=now + 80 * MS,
        weapon_actions_blocked=False, physical_equipment_generation=6, shot_input_generation=8,
        owner=owner, equipment=dict(weapon=4, data=9, asset="test1"),
        rig=dict(soldier=2, weak=3, count=10, skeleton=11, pose=12, fingerprint=1),
        selected=dict(configurationPathVerified=True, sequence=8, observedNs=now - MS,
                      deadlineNs=now + 90 * MS, configurationPath="config1", configurationPathPointer=13,
                      inventory=14, selectedSlot=1, soleConfiguredArray=15, matchedMeshPath="mesh1",
                      meshes=[dict(path="mesh1")]),
        input_evidence=dict(owner=dict(actor=(3 << 32) | 2, actorGeneration=5, equipGeneration=6, space=7),
                            sequence=8, observedNs=now - MS, deadlineNs=now + 90 * MS, nowNs=now,
                            focused=True, tracked=[True, True], released=[False, False]),
        input=dict(generation=8, spaceGeneration=7, trackingBasisGeneration=16, predictedNs=now,
                   focused=True, headValid=True, worldUnitsPerMeter=1, floorRelative=True,
                   referenceHead=pose, head=pose, hands=[copy.deepcopy(hand), copy.deepcopy(hand)]),
        mapping=dict(weaponWorldMeters=d.planner.identity(), trackingBodyWorldMeters=d.planner.identity(),
                     anatomicalWristToGrip=d.planner.identity()), feed=None)
    return dict(schema="bc2vr.feed-simulation-profiles.v1", profiles=rows), snapshot


class MemoryArtifacts:
    def __init__(self):
        self.saved, self.events = {}, []
        self.directory = Path("memory-artifacts")

    def save(self, name, raw, final=False):
        self.saved[name] = copy.deepcopy(raw)

    def event(self, event, cleanup=False):
        self.events.append(copy.deepcopy(event))


class Harness:
    def __init__(self, testcase, ack_ms=5):
        self.now, self.ack_ms = NS + 120 * MS, ack_ms
        self.files, self.json_files, self.command_writes = {}, {}, []
        self.response_after, self.response_stale, self.on_sleep = 40 * MS, False, None
        self.response_due, self.command_due = None, None
        v = self.driver = d.Driver.__new__(d.Driver)
        v.profiles, original = fixture(NS)
        v.reference = copy.deepcopy(original)
        v.latest_snapshot = copy.deepcopy(original)
        v.artifacts = MemoryArtifacts()
        v.run = Path("memory-run")
        v.host = dict(pid=1, start_time="synthetic")
        v.command_path, v.ack_path = v.run / "command", v.run / "ack"
        v.request_path, v.snapshot_path = v.run / "request", v.run / "snapshot"
        v.pending_nonce, v.snapshot_index = 1, 1
        v.request_bytes, v.last_request = b"1\n", NS + 20 * MS
        v.snapshot_refresh_started, v.snapshot_refresh_requests = None, 0
        v.deadline, v.hard_deadline, v.stage_deadline = self.now + 9 * NS, self.now + 10 * NS, self.now + 2 * NS
        v.motion_started, v.monitor = self.now - 100 * MS, True
        v.last_write, v.last_commit, v.gap = self.now - 60 * MS, self.now - 50 * MS, 80 * MS
        v.commands, v.capture_sequence = 0, 0
        v.sequence, v.sentinel, v.pending_command, v.unsafe_owner = 10, "initial", False, False
        v.held_left, v.held_right, v.held_trigger, v.held_buttons = True, False, False, set()
        self.state = dict(pid=1, runtime="xr-sim", cmdSeq=10, lastCmd="initial", lastCmdError="", errors=0,
                          frame=100, endFrames=99, captureSeq=0, sessionState="FOCUSED", head={}, handL={}, handR={},
                          controls=dict(gripL=1.))
        v.initial_sim = copy.deepcopy(self.state)
        self.files[v.command_path] = b"initial\n"
        self.files[v.request_path] = v.request_bytes
        self.publish(original)
        v.command_hash = d.digest(self.files[v.command_path])
        v.read_state = lambda: copy.deepcopy(self.state)
        for target, replacement in (
            ("perf_counter_ns", lambda: self.now), ("time_ns", lambda: 10 * NS + self.now), ("sleep", self.sleep)
        ):
            testcase.enterContext(patch.object(d.time, target, replacement))
        testcase.enterContext(patch.object(Path, "exists", lambda path: path in self.files))
        testcase.enterContext(patch.object(d, "read_bytes", lambda path, maximum: self.files[path]))
        testcase.enterContext(patch.object(d, "read_json", self.read_json))
        testcase.enterContext(patch.object(d, "atomic", self.write))

    def read_json(self, path, maximum=128 * 1024):
        return copy.deepcopy(self.json_files[path]), self.files[path]

    def publish(self, snapshot):
        self.json_files[self.driver.snapshot_path] = copy.deepcopy(snapshot)
        self.files[self.driver.snapshot_path] = d.json_bytes(snapshot)

    def write(self, path, raw):
        self.files[path] = raw
        if path == self.driver.request_path:
            self.response_due = None if self.response_after is None else self.now + self.response_after
        if path == self.driver.command_path:
            self.command_writes.append((self.now, raw))
            self.command_due = self.now + self.ack_ms * MS

    def sleep(self, seconds):
        self.now += round(seconds * NS)
        if self.on_sleep:
            self.on_sleep()
        if self.response_due is not None and self.now >= self.response_due:
            observed = self.now - (120 * MS if self.response_stale else 0)
            _, snapshot = fixture(observed, int(self.files[self.driver.request_path]))
            self.publish(snapshot)
            self.response_due = None
        if self.command_due is not None and self.now >= self.command_due:
            batch = self.files[self.driver.command_path].decode("ascii").splitlines()
            for command in batch:
                if command.startswith("grip l "):
                    self.state["controls"]["gripL"] = float(command.split()[-1])
                elif command.startswith("hand l grip pose "):
                    pose = [float(value) for value in command.split()[4:]]
                    self.state["handL"].update(pos=pose[:3], ypr=pose[3:])
            self.state.update(cmdSeq=self.state["cmdSeq"] + len(batch), lastCmd=batch[-1], frame=103, endFrames=102)
            self.files[self.driver.ack_path] = (
                f"seq={self.state['cmdSeq']} frame=101 error=-\nlast={' | '.join(batch)}\n").encode("ascii")
            self.command_due = None


class FeedDriverObservationTests(unittest.TestCase):
    def test_expired_response_holds_pose_without_commands_then_fresh_resumes(self):
        h = Harness(self)
        v, original = h.driver, copy.deepcopy(h.driver.reference)
        raw = h.files[v.snapshot_path]
        started, original_write, original_stage = h.now, v.last_write, v.stage_deadline

        def held():
            if not h.command_writes:
                self.assertEqual(v.last_write, original_write)
                self.assertTrue(v.held_left)
                self.assertEqual(v.commands, 0)

        h.on_sleep = held
        v.send(["grip l 1"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertGreaterEqual(h.command_writes[0][0], started + 40 * MS)
        self.assertEqual(v.artifacts.saved["snapshot-001.json"], raw)
        self.assertEqual(v.reference, original)
        self.assertGreater(v.latest_snapshot["nonce"], original["nonce"])
        self.assertEqual(v.stage_deadline, original_stage)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual([e["type"] for e in v.artifacts.events],
                         ["snapshot_refresh_hold", "snapshot_refresh_resumed", "command_request", "command_commit"])

    def test_expired_changed_identity_is_refused_without_refresh_or_command(self):
        for section, key, value in (("owner", "player", 99), ("selected", "selectedSlot", 2),
                                    ("input", "trackingBasisGeneration", 99)):
            with self.subTest(section=section):
                h = Harness(self)
                changed = copy.deepcopy(h.driver.reference)
                changed[section][key] = value
                h.publish(changed)
                with self.assertRaises(d.REFUSAL):
                    h.driver.send(["grip l 1"])
                self.assertEqual(h.driver.snapshot_index, 1)
                self.assertEqual(h.command_writes, [])
                self.assertIn("snapshot-001.json", h.driver.artifacts.saved)

    def test_invalid_native_publication_is_retained_and_not_retried(self):
        h = Harness(self)
        changed = copy.deepcopy(h.driver.reference)
        changed.update(valid=False, invalid_reason="native_owner_changed")
        h.publish(changed)
        with self.assertRaisesRegex(d.REFUSAL, "native snapshot refused"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 1)
        self.assertIn("snapshot-001.json", h.driver.artifacts.saved)

    def test_expired_but_inadmissible_source_is_not_retried(self):
        h = Harness(self)
        changed = copy.deepcopy(h.driver.reference)
        changed["input_evidence"]["tracked"] = [False, True]
        h.publish(changed)
        with self.assertRaisesRegex(d.REFUSAL, "untracked or unfocused"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 1)

    def test_repeated_expired_responses_exhaust_refresh_count(self):
        h = Harness(self)
        h.response_stale = True
        with self.assertRaisesRegex(d.REFUSAL, "refresh request budget exhausted"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 3)
        self.assertIn("snapshot-003.json", h.driver.artifacts.saved)

    def test_fresh_replacement_must_pass_current_native_admission(self):
        h = Harness(self)
        v = h.driver
        self.assertIsNone(v.receive_snapshot())
        _, replacement = fixture(h.now, v.pending_nonce)
        replacement["input_evidence"]["tracked"] = [False, True]
        h.publish(replacement)
        with self.assertRaisesRegex(d.REFUSAL, "untracked or unfocused"):
            v.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertIsNotNone(v.snapshot_refresh_started)
        self.assertEqual(v.latest_snapshot, v.reference)

    def test_refresh_keeps_elapsed_budget_and_gesture_watchdog(self):
        for bound, message in (("refresh", "refresh budget exhausted"),
                               ("stage", "maximumGestureNs"), ("watchdog", "driver deadline")):
            with self.subTest(bound=bound):
                h = Harness(self)
                v = h.driver
                self.assertIsNone(v.receive_snapshot())
                h.response_after, h.response_due = None, None
                if bound == "refresh":
                    h.now += d.SNAPSHOT_REFRESH_BUDGET
                elif bound == "stage":
                    v.stage_deadline = h.now + 10 * MS
                    h.now = v.stage_deadline
                else:
                    v.deadline = h.now + 10 * MS
                    h.now = v.deadline
                with self.assertRaisesRegex(d.REFUSAL, message):
                    v.pause_until(h.now)
                self.assertEqual(h.command_writes, [])

    def test_monitor_uses_100ms_request_period_not_each_command(self):
        h = Harness(self)
        v = h.driver
        v.pending_nonce = None
        v.last_request = h.now
        for _ in range(3):
            h.now += 25 * MS
            v.pump()
        self.assertEqual(v.snapshot_index, 1)
        h.now += 25 * MS
        v.pump()
        self.assertEqual(v.snapshot_index, 2)

    def test_held_pose_command_gap_is_not_native_input_gap(self):
        h = Harness(self, ack_ms=150)
        v = h.driver
        v.monitor = False
        v.last_write = v.last_commit = h.now - 200 * MS
        v.send(["grip l 1"])
        self.assertFalse(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)

    def test_ack_limits_remain_250ms_in_motion_and_one_second_otherwise(self):
        for moving, delay, succeeds in ((True, 249, True), (True, 250, False),
                                        (False, 999, True), (False, 1000, False)):
            with self.subTest(moving=moving, delay=delay):
                h = Harness(self, ack_ms=delay)
                v = h.driver
                v.monitor = False
                v.motion_started = h.now if moving else None
                if succeeds:
                    v.send(["grip l 1"])
                    self.assertFalse(v.pending_command)
                else:
                    with self.assertRaisesRegex(d.REFUSAL, "acknowledgement/commit deadline"):
                        v.send(["grip l 1"])


if __name__ == "__main__":
    unittest.main()
