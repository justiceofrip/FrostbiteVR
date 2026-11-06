"""Synthetic bounded sight transport checks, without process/game/native effects.

Run: python -B -m unittest discover -s tests -p test_bc2_sight_driver.py
The existing feed harness supplies in-memory files, simulator and QPC clock.
"""
import copy
import gzip
import importlib.util
import json
import os
from pathlib import Path
import random
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


s = load("sight_driver_test_subject", ROOT / "tools/drive_bc2_sight_simulation.py")
memory = load("sight_driver_memory_transport", ROOT / "tests/test_bc2_feed_driver_observation.py")
memory.d = s.transport
original_fixture = memory.fixture
MS, NS = 1_000_000, s.NS


def fixture(now, nonce=1):
    profiles, snapshot = original_fixture(now, nonce)
    snapshot["equipment"]["persistence"] = 20
    for hand in snapshot["input"]["hands"]:
        hand["trigger"] = 0.
    snapshot["sight"] = dict(status="available", family=dict(observedMs=100, physicalItem=20,
        selectedSecondary=False, inventory=14, targetWeapon=40, targetSlot=2, targetAction=33, primaryAction=36,
        primaryAsset="test1", primaryConfiguration="config1", secondaryAsset="test2",
        secondaryConfiguration="config2", persistentId="synthetic-physical-item"))
    return profiles, snapshot


memory.fixture = fixture


def transition(snapshot):
    after = copy.deepcopy(snapshot)
    after["owner"].update(weapon=40, equipGeneration=60)
    after["equipment"].update(weapon=40, data=90, asset="test2")
    after["rig"].update(fingerprint=2, count=20, skeleton=110, pose=120)
    after["selected"].update(configurationPath="config2", configurationPathPointer=130,
                              selectedSlot=2, soleConfiguredArray=150, matchedMeshPath="mesh2",
                              meshes=[dict(path="mesh2")])
    after["sight"].update(status="geometry_unavailable", contact=None,
                          native=dict(valid=True, physicalItem=20))
    after["sight"]["family"].update(selectedSecondary=True, targetWeapon=4,
        targetSlot=snapshot["selected"]["selectedSlot"], targetAction=snapshot["sight"]["family"]["primaryAction"])
    return after


def harness(testcase, ack_ms=5):
    h = memory.Harness(testcase, ack_ms)
    v = h.driver
    v.__class__ = s.SightDriver
    v.args = SimpleNamespace(target="secondary", step_ms=20)
    v.sight_plan = dict(expectedTransition=dict(sourceMode="primary", targetMode="secondary",
        sourceWeapon=4, targetWeapon=40, targetSlot=2, targetAction=33, primaryAction=36, asset="test2",
        configurationPath="config2", inventory=14, physicalItem=20, persistentId="synthetic-physical-item"),
        policy=copy.deepcopy(s.sight.POLICY))
    v.transition_observation, v.release_authorized = None, False
    v.selection_wait_armed, v.selection_hold_until, v.selection_hold_requests = False, None, 0
    return h


def admit_current(h, target=False):
    v = h.driver
    _, snapshot = fixture(h.now, v.pending_nonce or 2)
    if target:
        snapshot = transition(snapshot)
    v.pending_nonce = None
    v.last_request = h.now
    v.accept_snapshot(snapshot, s.transport.json_bytes(snapshot), h.now)
    return snapshot


def expire_during_publication_checks(h):
    read_state, reads = h.driver.read_state, 0

    def delayed_read():
        nonlocal reads
        reads += 1
        if reads == 2:
            h.now = s.source_deadline(h.driver.latest_snapshot)
        return read_state()

    h.driver.read_state = delayed_read


def selection_unavailable(h, armed=True, reason=s.SELECTION_UNAVAILABLE):
    v = h.driver
    admit_current(h)
    v.selection_wait_armed = armed
    v.request_snapshot()
    snapshot = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=v.pending_nonce,
                    valid=False, invalid_reason=reason, observed_ns=h.now)
    h.publish(snapshot)
    return snapshot


def stage_harness(testcase, approach_samples=32, move_samples=4, ack_ms=80, select_target=True):
    """Exercise the complete stage with real guarded transport and a synthetic plan."""
    h = harness(testcase, ack_ms)
    v = h.driver
    v.args.step_ms = 30
    v.plans, v.held_left = [], False
    h.state["controls"]["gripL"] = 0.
    h.state["handL"] = dict(pos=[0., 0., 0.], ypr=[0., 0., 0.], followsHead=False)
    v.initial_sim = copy.deepcopy(h.state)
    phases = ["neutral"] + ["approach"] * approach_samples + ["neutral_at_contact", "acquire"]
    phases += ["move"] * move_samples + ["detent_dwell"] * 3 + ["await_acknowledgement", "release"]
    acquire = phases.index("acquire")
    rows = []
    for index, phase in enumerate(phases):
        grip = int(index >= acquire and phase != "release")
        row = dict(atNs=index * 30 * MS, phase=phase, grip=grip,
                   positionMeters=[0., 0., 0.], yawPitchRollDegrees=[0., 0., 0.],
                   commands=["hand l grip pose 0 0 0 0 0 0", f"grip l {grip}"])
        if phase == "acquire":
            row["requiresFreshSightPreference"] = True
        if phase == "await_acknowledgement":
            row["maximumWaitNs"] = s.sight.POLICY["ackTimeoutNs"]
        if phase == "release":
            row["requiresObservedFamilyTransition"] = True
        rows.append(row)
    plan = dict(expectedTransition=copy.deepcopy(v.sight_plan["expectedTransition"]),
                durationNs=rows[-1]["atNs"], gestureNs=rows[-1]["atNs"] - rows[acquire]["atNs"],
                policy=copy.deepcopy(s.sight.POLICY), schedule=rows)
    h.selected_target = False

    def observed(now, nonce=1):
        profiles, snapshot = fixture(now, nonce)
        return profiles, transition(snapshot) if h.selected_target else snapshot

    original_sleep = h.sleep

    def committed_sleep(seconds):
        pending = h.command_due is not None
        original_sleep(seconds)
        if pending and h.command_due is None:
            # The shared harness models one commit. Advance frames and actual
            # controls for this multi-command stage, retaining real ack checks.
            h.state.update(frame=100 + v.sequence, endFrames=99 + v.sequence)
            lines = h.files[v.command_path].decode("ascii").splitlines()
            for line in lines:
                if line.startswith("grip l "):
                    h.state["controls"]["gripL"] = float(line.split()[-1])
            if select_target and len(h.command_writes) == phases.index("await_acknowledgement") + 1:
                h.selected_target = True

    testcase.enterContext(patch.object(memory, "fixture", observed))
    testcase.enterContext(patch.object(s.time, "sleep", committed_sleep))
    testcase.enterContext(patch.object(s.sight, "plan", return_value=plan))
    testcase.enterContext(patch.object(s.sight, "admit", return_value=dict(preferredByDistance=True)))
    return h, plan


def first_detent_gap(testcase, recovery="target", different_endpoint=False, ack_ms=80, during_endpoint=False):
    """Publish the actual274-shaped missing frame during the first dwell ack."""
    h, plan = stage_harness(testcase, approach_samples=1, select_target=False)
    v = h.driver
    detent = next(i for i, row in enumerate(plan["schedule"]) if row["phase"] == "detent_dwell")
    if different_endpoint:
        plan["schedule"][detent]["commands"][0] = "hand l grip pose .01 0 0 0 0 0"
    h.gap = None
    gap_command = detent if not during_endpoint else detent - 1
    observed = memory.fixture

    def publish_during_dwell(path, raw, **options):
        h.write(path, raw, **options)
        if path == v.command_path and len(h.command_writes) == gap_command + 1:
            h.command_due = h.now + ack_ms * MS
            if v.pending_nonce is None:
                v.request_snapshot()
            h.response_due = h.now + MS

    def response(now, nonce=1):
        profiles, snapshot = observed(now, nonce)
        if len(h.command_writes) == gap_command + 1 and h.gap is None:
            h.gap = dict(now=now, pending=v.pending_command, armed=v.selection_wait_armed,
                         writes=len(h.command_writes), stage_deadline=v.stage_deadline,
                         driver_deadline=v.deadline, request=v.last_request)
            return profiles, dict(schema=snapshot["schema"], nonce=nonce, observed_ns=now,
                                  valid=False, invalid_reason=s.SELECTION_UNAVAILABLE)
        if h.gap is not None and recovery != "source":
            snapshot = transition(snapshot)
            if recovery == "wrong_target":
                snapshot["owner"]["weapon"] = 99
            elif recovery == "wrong_actor":
                snapshot["owner"]["actorGeneration"] = 99
        return profiles, snapshot

    testcase.enterContext(patch.object(s.transport, "atomic", publish_during_dwell))
    testcase.enterContext(patch.object(memory, "fixture", response))
    return h, plan


class SightDriverTests(unittest.TestCase):
    def test_selection_response_replaced_after_read_is_consumed_once_before_hold(self):
        h = harness(self)
        v = h.driver
        admit_current(h)
        v.selection_wait_armed = True
        v.request_snapshot()
        pending = v.pending_nonce
        missing = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=pending,
                       valid=False, invalid_reason=s.SELECTION_UNAVAILABLE, observed_ns=h.now)
        read, replaced, reads = h.read_json, False, 0

        def replace_after_read(path, maximum):
            nonlocal replaced, reads
            value = read(path, maximum)
            if path == v.snapshot_path:
                reads += 1
                if not replaced:
                    replaced = True
                    h.publish(missing)
            return value

        with patch.object(s.transport, "read_json", replace_after_read):
            # First poll consumed the previous nonce. The old override reread
            # here and the base rejected the newly published response without
            # giving the sight hold a chance to examine those exact bytes.
            self.assertIsNone(v.receive_snapshot())
            self.assertEqual(reads, 1)
            self.assertEqual(v.pending_nonce, pending)
            self.assertEqual(v.selection_hold_requests, 0)
            self.assertIsNone(v.receive_snapshot())
        self.assertEqual(v.selection_hold_requests, 1)
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], s.transport.json_bytes(missing))
        self.assertIsNone(v.transition_observation)
        self.assertEqual(h.command_writes, [])
        with patch.object(memory, "fixture", lambda now, nonce=1: (fixture(now, nonce)[0], transition(fixture(now, nonce)[1]))):
            with self.assertRaises(s.SelectionObserved):
                v.send(["hand l grip pose 0 0 0 0 0 0", "grip l 1"])
        self.assertEqual(h.command_writes, [])
        v.release_authorized = True
        v.send(["grip l 0"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertFalse(v.held_left)

    def test_selection_recovery_rejects_wrong_item_configuration_physical_root_and_rig(self):
        for section, key, value in (("equipment", "persistence", 999),
                                    ("selected", "configurationPath", "unrelated"),
                                    (None, "physical_equipment_generation", 999),
                                    ("rig", "soldier", 999), ("rig", "pose", 0)):
            with self.subTest(section=section, key=key):
                h = harness(self)
                v = h.driver
                selection_unavailable(h)

                def wrong_target(now, nonce=1):
                    profiles, snapshot = fixture(now, nonce)
                    snapshot = transition(snapshot)
                    (snapshot if section is None else snapshot[section])[key] = value
                    return profiles, snapshot

                with patch.object(memory, "fixture", wrong_target), self.assertRaises((s.REFUSAL, s.sight.Refusal)):
                    v.send(["grip l 1"])
                self.assertEqual(h.command_writes, [])
                self.assertIsNone(v.transition_observation)
                self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
                self.assertEqual(len(h.command_writes), 1)
                self.assertFalse(v.held_left)

    @unittest.skipUnless(os.environ.get("BC2_SIGHT_TRANSITION_REPLAY"), "optional retained actual312 response replay")
    def test_actual312_069_to_070_interleaving_enters_existing_hold_without_motion(self):
        # Exact retained bytes/clocks. This demonstrates response handling, not
        # a uniquely recorded live interleaving or a future native target frame.
        folder = Path(os.environ["BC2_SIGHT_TRANSITION_REPLAY"])
        pins = {
            "snapshot-069.json.gz": "6ef50d90092c0ee58f6667bde10a82be36b294a8dd4e597724a0941da2069e9c",
            "snapshot-070.json.gz": "2f3aa7741999828e2ec53b7feb449cee3a5bdd3cc4b7d212b77b3743c398353a",
            "sight-plan.json": "3b12c04abcc5c19e0c503c152832845aef85b877fc58dbc9072c67d9e98d5d95",
            "events.jsonl": "2d9816474c20ac829136c7b20183acd29d4cff9b146655a615d696a6291f76d3",
            "result.json": "f87f0ccb98fa25165b292fe64dd97aa83b5ff4ab3e6ed2ea560b055c3414a0da",
            "request-070-timing.json": "f9fbc6d3e71599c7e1384ac1bdefb6387658266839531a801a1fe5139675a04f",
        }
        blobs = {name: (folder / name).read_bytes() for name in pins}
        self.assertEqual({name: s.transport.digest(blob) for name, blob in blobs.items()}, pins)
        old_raw, missing_raw = [gzip.decompress(blobs[name]) for name in ("snapshot-069.json.gz", "snapshot-070.json.gz")]
        old, missing = json.loads(old_raw), json.loads(missing_raw)
        plan, result, timing = [json.loads(blobs[name]) for name in ("sight-plan.json", "result.json", "request-070-timing.json")]
        events = [json.loads(line) for line in blobs["events.jsonl"].splitlines()]
        requests = [e for e in events if e["type"] == "command_request" and e["commands"][:-1] != ["grip l 0"]]
        commits = [e for e in events if e["type"] == "command_commit" and not e["cleanup"]]
        self.assertEqual(len(requests), len(commits))
        self.assertEqual(len(commits), 72)
        self.assertEqual([e["commands"][:-1] for e in requests], [r["commands"] for r in plan["schedule"][:72]])
        self.assertEqual(plan["schedule"][70]["phase"], "move")
        self.assertEqual(plan["schedule"][71]["phase"], "detent_dwell")
        self.assertEqual(plan["schedule"][70]["commands"], plan["schedule"][71]["commands"])
        self.assertLess(commits[70]["committedNs"], timing["requestStartedNs"])
        self.assertLess(commits[71]["committedNs"], missing["observed_ns"])
        h = harness(self)
        v = h.driver
        h.now = 270019557403300  # retained native publication completed, inside original250ms
        h.state = copy.deepcopy(commits[-1]["state"])
        v.initial_sim = copy.deepcopy(h.state)
        v.reference = v.latest_snapshot = old
        v.sight_plan, v.selection_wait_armed, v.held_left = plan, True, True
        v.stage_deadline = result["plans"][0]["gestureDeadlineNs"]
        v.deadline = result["plans"][0]["approachDeadlineNs"]
        v.last_request, v.pending_nonce, v.snapshot_index = timing["requestStartedNs"], int(missing["nonce"]), 70
        v.last_write, v.last_commit = commits[-1]["writtenNs"], commits[-1]["committedNs"]
        v.sequence, v.sentinel, v.capture_sequence = h.state["cmdSeq"], h.state["lastCmd"], h.state["captureSeq"]
        v.request_bytes = (str(v.pending_nonce) + "\n").encode()
        h.files[v.request_path] = v.request_bytes
        h.files[v.command_path] = ("\n".join(requests[-1]["commands"]) + "\n").encode()
        v.command_hash = s.transport.digest(h.files[v.command_path])
        h.json_files[v.snapshot_path], h.files[v.snapshot_path] = old, old_raw
        read, replaced = h.read_json, False

        def replace_after_read(path, maximum):
            nonlocal replaced
            value = read(path, maximum)
            if path == v.snapshot_path and not replaced:
                replaced = True
                h.json_files[v.snapshot_path], h.files[v.snapshot_path] = missing, missing_raw
            return value

        with patch.object(s.transport, "read_json", replace_after_read):
            self.assertIsNone(v.receive_snapshot())
            self.assertEqual(v.selection_hold_requests, 0)
            self.assertIsNone(v.receive_snapshot())
        self.assertEqual(v.selection_hold_requests, 1)
        self.assertEqual(v.artifacts.saved["snapshot-070.json"], missing_raw)
        self.assertEqual(v.selection_hold_until, min(v.stage_deadline, v.deadline,
            timing["requestStartedNs"] + plan["policy"]["ackTimeoutNs"], h.now + s.transport.SNAPSHOT_REFRESH_BUDGET))
        self.assertEqual(h.command_writes, [])
        self.assertIsNone(v.transition_observation)

    def test_slow_neutral_approach_does_not_consume_acquisition_budget_or_skip_commands(self):
        h, plan = stage_harness(self)
        v = h.driver
        v.stage()
        timing = v.plans[-1]
        self.assertGreater(timing["approachDurationNs"], plan["policy"]["gestureTimeoutNs"])
        commits = [e for e in v.artifacts.events if e["type"] == "command_commit"]
        acquire_index = next(i for i, row in enumerate(plan["schedule"]) if row["phase"] == "acquire")
        acquired = commits[acquire_index]
        self.assertLessEqual(timing["gestureBudgetStartedNs"], acquired["writtenNs"])
        self.assertLess(acquired["writtenNs"], acquired["committedNs"])
        self.assertEqual(timing["gestureDeadlineNs"],
                         timing["gestureBudgetStartedNs"] + plan["policy"]["gestureTimeoutNs"])
        actual = [raw.decode("ascii").splitlines()[:-1] for _, raw in h.command_writes]
        self.assertEqual(actual, [r["commands"] for r in plan["schedule"][:-1]] + [["grip l 0"]])
        self.assertTrue(v.transition_observation)
        self.assertFalse(v.held_left)
        self.assertEqual(v.stage_deadline, v.deadline)

    def test_stage_observes_selection_gap_after_held_detent_then_releases(self):
        h, _ = stage_harness(self)
        v = h.driver
        observed = memory.fixture
        gap_writes = []

        def gap_once(now, nonce=1):
            profiles, snapshot = observed(now, nonce)
            if h.selected_target and not gap_writes:
                gap_writes.append(len(h.command_writes))
                snapshot = dict(schema=snapshot["schema"], nonce=nonce, observed_ns=now,
                                valid=False, invalid_reason=s.SELECTION_UNAVAILABLE)
            return profiles, snapshot

        with patch.object(memory, "fixture", gap_once):
            v.stage()
        self.assertTrue(v.selection_wait_armed)
        self.assertEqual(v.selection_hold_requests, 1)
        self.assertEqual(len(h.command_writes), gap_writes[0] + 1)
        self.assertEqual(h.command_writes[-1][1].decode().splitlines()[:-1], ["grip l 0"])
        self.assertTrue(v.plans[-1]["selectedTransitionObserved"])
        self.assertTrue(v.plans[-1]["ordinaryLeftReleaseAcknowledged"])
        self.assertFalse(v.held_left)

    def test_acknowledged_endpoint_recovers_gap_during_first_dwell_ack(self):
        h, plan = first_detent_gap(self)
        v = h.driver
        v.stage()
        self.assertTrue(h.gap["pending"])
        self.assertTrue(h.gap["armed"])
        self.assertEqual(v.selection_hold_requests, 1)
        self.assertEqual(len(h.command_writes), h.gap["writes"] + 1)
        self.assertEqual(h.command_writes[-1][1].decode().splitlines()[:-1], ["grip l 0"])
        commits = [e for e in v.artifacts.events if e["type"] == "command_commit"]
        endpoint, dwell = commits[-3:-1]
        self.assertLess(endpoint["committedNs"], h.gap["now"])
        self.assertLess(h.gap["now"], dwell["committedNs"])
        self.assertEqual(v.plans[-1]["gestureDeadlineNs"], h.gap["stage_deadline"])
        self.assertEqual(v.deadline, h.gap["driver_deadline"])
        self.assertEqual(v.selection_hold_until, min(h.gap["stage_deadline"], h.gap["driver_deadline"],
            h.gap["request"] + plan["policy"]["ackTimeoutNs"],
            h.gap["now"] + s.transport.SNAPSHOT_REFRESH_BUDGET))
        self.assertTrue(v.plans[-1]["selectedTransitionObserved"])
        self.assertFalse(v.held_left)

    def test_different_endpoint_does_not_arm_before_first_dwell_ack(self):
        h, _ = first_detent_gap(self, different_endpoint=True)
        with self.assertRaisesRegex(s.REFUSAL, "native snapshot refused"):
            h.driver.stage()
        self.assertTrue(h.gap["pending"])
        self.assertFalse(h.gap["armed"])
        self.assertEqual(h.driver.selection_hold_requests, 0)
        self.assertIsNone(h.driver.transition_observation)

    def test_unacknowledged_endpoint_does_not_arm_selection_hold(self):
        h, _ = first_detent_gap(self, during_endpoint=True)
        with self.assertRaisesRegex(s.REFUSAL, "native snapshot refused"):
            h.driver.stage()
        self.assertTrue(h.gap["pending"])
        self.assertFalse(h.gap["armed"])
        self.assertEqual(h.driver.selection_hold_requests, 0)
        self.assertIsNone(h.driver.transition_observation)

    def assert_first_dwell_gap_rejects(self, recovery):
        h, _ = first_detent_gap(self, recovery=recovery)
        with self.assertRaises(s.REFUSAL):
            h.driver.stage()
        self.assertEqual(h.driver.selection_hold_requests, 1)
        self.assertEqual(len(h.command_writes), h.gap["writes"])
        self.assertIsNone(h.driver.transition_observation)

    def test_first_dwell_gap_rejects_contradictory_target(self):
        self.assert_first_dwell_gap_rejects("wrong_target")

    def test_first_dwell_gap_rejects_contradictory_actor(self):
        self.assert_first_dwell_gap_rejects("wrong_actor")

    def test_first_dwell_gap_keeps_original_command_ack_deadline(self):
        h, _ = first_detent_gap(self, ack_ms=250)
        with self.assertRaisesRegex(s.REFUSAL, "acknowledgement/commit deadline"):
            h.driver.stage()
        self.assertEqual(h.driver.selection_hold_requests, 1)
        self.assertEqual(len(h.command_writes), h.gap["writes"])
        self.assertEqual(h.driver.stage_deadline, h.gap["stage_deadline"])

    def test_first_dwell_gap_preserves_exclusive_command_ownership(self):
        h, _ = first_detent_gap(self)

        def interfere():
            if h.gap is not None:
                h.files[h.driver.command_path] = b"unowned replacement\n"

        h.on_sleep = interfere
        with self.assertRaisesRegex(s.REFUSAL, "concurrent command writer"):
            h.driver.stage()
        self.assertEqual(len(h.command_writes), h.gap["writes"])
        self.assertIsNone(h.driver.transition_observation)
        with self.assertRaisesRegex(s.REFUSAL, "pending command replaced; ordinary release ownership lost"):
            h.driver.release_after_failure()
        self.assertEqual(len(h.command_writes), h.gap["writes"])

    def test_first_dwell_gap_never_resumes_motion_from_source_only_recovery(self):
        h, _ = first_detent_gap(self, recovery="source")
        with self.assertRaisesRegex(s.REFUSAL, "selection publication hold deadline"):
            h.driver.stage()
        self.assertEqual(len(h.command_writes), h.gap["writes"])
        self.assertIsNone(h.driver.transition_observation)
        self.assertEqual(h.driver.stage_deadline, h.gap["stage_deadline"])
        self.assertEqual(h.driver.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertFalse(h.driver.held_left)

    def test_neutral_approach_retains_global_watchdog(self):
        h, _ = stage_harness(self)
        h.driver.deadline = h.now + 600 * MS
        with self.assertRaisesRegex(s.REFUSAL, "driver deadline"):
            h.driver.stage()
        self.assertIsNone(h.driver.plans[-1]["gestureBudgetStartedNs"])
        self.assertFalse(h.driver.held_left)
        self.assertFalse(any(b"grip l 1" in raw for _, raw in h.command_writes))

    def test_neutral_approach_retains_250ms_command_ack_bound(self):
        h, _ = stage_harness(self, ack_ms=250)
        with self.assertRaisesRegex(s.REFUSAL, "acknowledgement/commit deadline"):
            h.driver.stage()
        self.assertIsNone(h.driver.plans[-1]["gestureBudgetStartedNs"])
        self.assertEqual(len(h.command_writes), 1)

    def test_acquisition_budget_is_not_restarted_by_slow_motion_or_fresh_snapshots(self):
        h, plan = stage_harness(self, approach_samples=1, move_samples=20, ack_ms=180, select_target=False)
        v = h.driver
        with self.assertRaisesRegex(s.REFUSAL, "maximumGestureNs"):
            v.stage()
        timing = v.plans[-1]
        acquired = next(e for e in v.artifacts.events
                        if e["type"] == "command_request" and "grip l 1" in e["commands"])
        self.assertLessEqual(timing["gestureBudgetStartedNs"], acquired["atNs"])
        self.assertEqual(v.stage_deadline,
                         timing["gestureBudgetStartedNs"] + plan["policy"]["gestureTimeoutNs"])
        self.assertGreater(v.snapshot_index, 4)
        self.assertIsNone(v.transition_observation)

    def test_same_source_and_exact_target_allow_only_family_selection_change(self):
        h = harness(self)
        v = h.driver
        self.assertEqual(s.selection_state(v.reference, v.reference, v.sight_plan["expectedTransition"]), "source")
        target = transition(v.reference)
        self.assertEqual(s.selection_state(v.reference, target, v.sight_plan["expectedTransition"]), "target")
        self.assertNotEqual(target["rig"], v.reference["rig"])
        self.assertIsNone(target["sight"]["contact"])

    def test_wrong_target_family_actor_space_or_right_input_is_refused(self):
        changes = [
            ("owner", "weapon", 41), ("owner", "actorGeneration", 99), ("owner", "space", 99),
            ("equipment", "persistence", 21), ("equipment", "asset", "unrelated"),
            ("selected", "configurationPath", "another-config"), ("selected", "selectedSlot", 3),
            ("selected", "inventory", 99), ("input", "trackingBasisGeneration", 99),
        ]
        h = harness(self)
        v = h.driver
        for section, key, value in changes:
            with self.subTest(section=section, key=key):
                target = transition(v.reference)
                target[section][key] = value
                with self.assertRaises(s.REFUSAL):
                    s.selection_state(v.reference, target, v.sight_plan["expectedTransition"])
        for key, value in (("persistentId", "other"), ("targetWeapon", 99), ("targetAction", 33),
                           ("primaryConfiguration", "other"), ("selectedSecondary", False)):
            with self.subTest(family_key=key):
                target = transition(v.reference)
                target["sight"]["family"][key] = value
                with self.assertRaises(s.REFUSAL):
                    s.selection_state(v.reference, target, v.sight_plan["expectedTransition"])
        target = transition(v.reference)
        target["input"]["hands"][1]["grip"]["position"][0] = .01
        with self.assertRaisesRegex(s.REFUSAL, "right controller"):
            s.selection_state(v.reference, target, v.sight_plan["expectedTransition"])

    def test_reverse_transition_requires_primary_slot_and_native_inverse_action(self):
        h = harness(self)
        primary = copy.deepcopy(h.driver.reference)
        secondary = transition(primary)
        expected = dict(sourceMode="secondary", targetMode="primary", sourceWeapon=40,
                        targetWeapon=4, targetSlot=1, targetAction=36, primaryAction=36, asset="test1",
                        configurationPath="config1", inventory=14, physicalItem=20,
                        persistentId="synthetic-physical-item")
        self.assertEqual(s.selection_state(secondary, primary, expected), "target")
        primary["sight"]["family"]["targetAction"] = 36
        with self.assertRaisesRegex(s.REFUSAL, "inverse target"):
            s.selection_state(secondary, primary, expected)

    def test_slot_zero_route_roundtrip_and_changed_route_refusals(self):
        h = harness(self)
        primary = copy.deepcopy(h.driver.reference)
        primary["selected"]["selectedSlot"] = 0
        primary["sight"]["family"]["primaryAction"] = 25
        expected = dict(h.driver.sight_plan["expectedTransition"], primaryAction=25)
        secondary = transition(primary)
        self.assertEqual(s.selection_state(primary, secondary, expected), "target")
        reverse = dict(expected, sourceMode="secondary", targetMode="primary", sourceWeapon=40,
                       targetWeapon=4, targetSlot=0, targetAction=25, asset="test1", configurationPath="config1")
        self.assertEqual(s.selection_state(secondary, primary, reverse), "target")
        for value in (None, 7, 33, 36, 38):
            changed = copy.deepcopy(secondary)
            if value is None:
                del changed["sight"]["family"]["primaryAction"]
            else:
                changed["sight"]["family"]["primaryAction"] = value
            with self.subTest(value=value), self.assertRaisesRegex(s.REFUSAL, "primary route"):
                s.selection_state(primary, changed, expected)
        for source in (primary, expected):
            changed = copy.deepcopy(source)
            if source is primary:
                del changed["sight"]["family"]["primaryAction"]
                args = (changed, secondary, expected)
            else:
                del changed["primaryAction"]
                args = (primary, secondary, changed)
            with self.assertRaisesRegex(s.REFUSAL, "primary route"):
                s.selection_state(*args)

    def test_expired_target_is_retained_but_does_not_authorize_release(self):
        h = harness(self)
        v = h.driver
        target = transition(v.reference)
        h.publish(target)
        self.assertIsNone(v.receive_snapshot())
        self.assertIsNone(v.transition_observation)
        self.assertEqual(v.latest_snapshot, v.reference)
        self.assertEqual(v.snapshot_refresh_requests, 1)
        self.assertIn("snapshot-001.json", v.artifacts.saved)
        self.assertEqual(h.command_writes, [])

    def test_selection_publication_gap_holds_even_fresh_source_until_exact_target(self):
        h = harness(self)
        v = h.driver
        missing = selection_unavailable(h)
        original = copy.deepcopy(v.reference)
        stage_deadline, driver_deadline, last_write = v.stage_deadline, v.deadline, v.last_write
        responses = 0

        def observed(now, nonce=1):
            nonlocal responses
            responses += 1
            profiles, snapshot = fixture(now, nonce)
            return profiles, transition(snapshot) if responses > 1 else snapshot

        with patch.object(memory, "fixture", observed):
            with self.assertRaises(s.SelectionObserved):
                v.send(["hand l grip pose 0 0 0 0 0 0", "grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.last_write, last_write)
        self.assertGreaterEqual(responses, 2)
        self.assertEqual(v.reference, original)
        self.assertEqual((v.stage_deadline, v.deadline), (stage_deadline, driver_deadline))
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], s.transport.json_bytes(missing))
        self.assertEqual(v.selection_hold_requests, 1)
        self.assertFalse(v.transition_observation["nativeAcceptance"])
        v.release_authorized = True
        v.send(["grip l 0"])
        self.assertFalse(v.held_left)
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(h.command_writes[0][1].decode().splitlines()[:-1], ["grip l 0"])

    def test_selection_gap_requires_acknowledged_detent_and_exact_reason(self):
        for armed, reason in ((False, s.SELECTION_UNAVAILABLE), (True, "native_owner_changed")):
            with self.subTest(armed=armed, reason=reason):
                h = harness(self)
                selection_unavailable(h, armed, reason)
                with self.assertRaisesRegex(s.REFUSAL, "native snapshot refused"):
                    h.driver.send(["grip l 1"])
                self.assertEqual(h.driver.selection_hold_requests, 0)
                self.assertEqual(h.command_writes, [])

    def test_selection_gap_has_fixed_budget_and_does_not_resume_source_motion(self):
        h = harness(self)
        v = h.driver
        selection_unavailable(h)
        started = h.now
        with self.assertRaisesRegex(s.REFUSAL, "selection publication hold deadline"):
            v.send(["grip l 1"])
        self.assertEqual(v.selection_hold_until, started + s.transport.SNAPSHOT_REFRESH_BUDGET)
        self.assertIsNone(v.transition_observation)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertFalse(v.held_left)

    def test_repeated_selection_gaps_exhaust_original_refresh_count(self):
        h = harness(self)
        v = h.driver
        selection_unavailable(h)

        def unavailable(now, nonce=1):
            return {}, dict(schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=nonce,
                            valid=False, invalid_reason=s.SELECTION_UNAVAILABLE, observed_ns=now)

        with patch.object(memory, "fixture", unavailable):
            with self.assertRaisesRegex(s.REFUSAL, "selection publication refresh request budget"):
                v.send(["grip l 1"])
        self.assertEqual(v.selection_hold_requests, s.transport.MAX_SNAPSHOT_REFRESHES)
        self.assertEqual(h.command_writes, [])

    def test_selection_gap_recovery_rechecks_native_family_and_input(self):
        for change in ("native", "family", "actor", "input"):
            with self.subTest(change=change):
                h = harness(self)
                v = h.driver
                selection_unavailable(h)

                def changed(now, nonce=1):
                    profiles, snapshot = fixture(now, nonce)
                    snapshot = transition(snapshot)
                    if change == "native":
                        snapshot["sight"]["native"]["valid"] = False
                    elif change == "family":
                        snapshot["sight"]["family"]["physicalItem"] = 99
                    elif change == "actor":
                        snapshot["owner"]["actorGeneration"] = 99
                    else:
                        snapshot["input_evidence"]["tracked"] = [False, True]
                    return profiles, snapshot

                with patch.object(memory, "fixture", changed):
                    with self.assertRaises((s.REFUSAL, s.sight.Refusal)):
                        v.send(["grip l 1"])
                self.assertIsNone(v.transition_observation)
                self.assertEqual(h.command_writes, [])

    def test_expired_target_after_selection_gap_never_authorizes_a_command(self):
        h = harness(self)
        v = h.driver
        selection_unavailable(h)
        responses = 0

        def observed(now, nonce=1):
            nonlocal responses
            responses += 1
            profiles, snapshot = fixture(now - (120 * MS if responses == 1 else 0), nonce)
            return profiles, transition(snapshot)

        with patch.object(memory, "fixture", observed):
            with self.assertRaises(s.SelectionObserved):
                v.send(["grip l 1"])
        self.assertEqual(responses, 2)
        self.assertEqual(h.command_writes, [])
        self.assertIn("snapshot-003.json", v.artifacts.saved)
        self.assertEqual(v.transition_observation["nonce"], v.latest_snapshot["nonce"])
        self.assertLess(h.now, s.source_deadline(v.latest_snapshot))

    def test_selection_gap_does_not_extend_gesture_or_request_deadlines(self):
        for bound in ("gesture", "request"):
            with self.subTest(bound=bound):
                h = harness(self)
                v = h.driver
                selection_unavailable(h)
                if bound == "gesture":
                    v.stage_deadline = h.now + 20 * MS
                    message = "maximumGestureNs"
                else:
                    h.response_after = None
                    message = "response not received within request budget"
                with self.assertRaisesRegex(s.REFUSAL, message):
                    v.send(["grip l 1"])
                self.assertEqual(h.command_writes, [])

    def test_selection_gap_during_pending_command_keeps_original_ack_deadline(self):
        h = harness(self, ack_ms=250)
        v = h.driver
        admit_current(h)
        v.selection_wait_armed = True
        v.request_snapshot()
        responses = 0

        def observed(now, nonce=1):
            nonlocal responses
            responses += 1
            profiles, snapshot = fixture(now, nonce)
            if responses == 1:
                snapshot.update(valid=False, invalid_reason=s.SELECTION_UNAVAILABLE)
            else:
                snapshot = transition(snapshot)
            return profiles, snapshot

        with patch.object(memory, "fixture", observed):
            with self.assertRaisesRegex(s.REFUSAL, "acknowledgement/commit deadline"):
                v.send(["grip l 1"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(v.selection_hold_requests, 1)

    def test_fresh_selection_stops_old_mapping_pose_publication(self):
        h = harness(self)
        v = h.driver
        admit_current(h, target=True)
        with self.assertRaises(s.SelectionObserved):
            v.send(["hand l grip pose 0 0 0 0 0 0", "grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertFalse(v.transition_observation["nativeAcceptance"])
        self.assertFalse(v.transition_observation["requestAcknowledgementAvailable"])
        self.assertIn("selected-transition.json", v.artifacts.saved)

    def test_exact_fresh_target_allows_grip_only_release(self):
        h = harness(self)
        v = h.driver
        admit_current(h, target=True)
        v.release_authorized = True
        v.send(["grip l 0"])
        self.assertFalse(v.held_left)
        self.assertFalse(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(h.command_writes[0][1].decode().splitlines()[0], "grip l 0")
        self.assertNotIn("pose", h.command_writes[0][1].decode())

    def test_source_cannot_authorize_planned_release_or_revert_after_target(self):
        h = harness(self)
        v = h.driver
        admit_current(h)
        with self.assertRaisesRegex(s.REFUSAL, "lacks observed-family"):
            v.send(["grip l 0"], expected=dict(phase="release"))
        self.assertEqual(h.command_writes, [])
        admit_current(h, target=True)
        with self.assertRaisesRegex(s.REFUSAL, "reverted"):
            admit_current(h)
        self.assertEqual(h.command_writes, [])

    def test_stale_source_refresh_retains_lease_and_waits_before_command(self):
        h = harness(self)
        v = h.driver
        original = copy.deepcopy(v.reference)
        started = h.now
        v.send(["grip l 1"])
        self.assertGreaterEqual(h.command_writes[0][0], started + 40 * MS)
        self.assertEqual(v.reference, original)
        self.assertLess(h.command_writes[0][0], s.source_deadline(v.latest_snapshot))
        self.assertEqual(len(h.command_writes), 1)

    def test_source_expiring_in_transport_wait_refreshes_before_publication(self):
        h = harness(self)
        v = h.driver
        original = copy.deepcopy(admit_current(h))
        reference, stage_deadline = copy.deepcopy(v.reference), v.stage_deadline
        started = h.now
        expire_during_publication_checks(h)
        v.send(["grip l 1"])
        self.assertEqual(len(h.command_writes), 1)
        written = h.command_writes[0][0]
        self.assertGreaterEqual(written, started + s.transport.SNAPSHOT_PERIOD + h.response_after)
        self.assertLess(written, s.source_deadline(v.latest_snapshot))
        self.assertGreater(v.latest_snapshot["nonce"], original["nonce"])
        self.assertEqual(v.snapshot_index, 2)  # Consume the next request, without an extra fresh() cycle.
        self.assertEqual(v.reference, reference)
        self.assertEqual(v.stage_deadline, stage_deadline)
        self.assertEqual(original["valid_until_ns"], started + 80 * MS)

    def test_publication_refresh_refuses_new_native_frame_with_stale_input(self):
        h = harness(self)
        v = h.driver
        original = copy.deepcopy(admit_current(h))
        expire_during_publication_checks(h)

        def stale_input(now, nonce=1):
            profiles, snapshot = fixture(now, nonce)
            snapshot["input_evidence"] = copy.deepcopy(original["input_evidence"])
            return profiles, snapshot

        with patch.object(memory, "fixture", stale_input):
            with self.assertRaisesRegex(s.sight.Refusal, "stale or invalid original hand evidence lease"):
                v.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.latest_snapshot, original)
        self.assertIn("snapshot-002.json", v.artifacts.saved)

    def test_publication_refresh_does_not_extend_original_plan_deadline(self):
        h = harness(self)
        v = h.driver
        original = copy.deepcopy(admit_current(h))
        expire_during_publication_checks(h)
        with self.assertRaisesRegex(s.REFUSAL, "original source/claim lease expired"):
            v.send(["grip l 1"], source_deadline=s.source_deadline(original))
        self.assertEqual(h.command_writes, [])
        self.assertGreater(v.latest_snapshot["nonce"], original["nonce"])
        self.assertEqual(original["valid_until_ns"], original["observed_ns"] + 80 * MS)

    def test_publication_refresh_rechecks_writer_after_wait(self):
        h = harness(self)
        v = h.driver
        admit_current(h)
        expire_during_publication_checks(h)
        h.on_sleep = lambda: h.files.update({v.command_path: b"another writer\n"})
        with self.assertRaisesRegex(s.REFUSAL, "command file replaced"):
            v.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertTrue(v.unsafe_owner)

    def test_acquisition_requires_fresh_sight_preference(self):
        h = harness(self)
        v = h.driver
        admit_current(h)
        with patch.object(s.sight, "admit", return_value=dict(preferredByDistance=False)):
            with self.assertRaisesRegex(s.REFUSAL, "does not beat support"):
                v.send(["grip l 1"], expected=dict(requiresFreshSightPreference=True))
        self.assertEqual(h.command_writes, [])

    def test_unobserved_transition_times_out_then_only_owned_cleanup_releases(self):
        h = harness(self)
        v = h.driver
        admit_current(h)
        v.monitor = False
        with self.assertRaisesRegex(s.REFUSAL, "within bounded wait"):
            v.await_transition(20 * MS)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertFalse(v.held_left)
        self.assertTrue(v.artifacts.events[-1]["cleanup"])

    def test_writer_or_capture_change_prevents_motion_and_release(self):
        for changed in ("writer", "capture"):
            with self.subTest(changed=changed):
                h = harness(self)
                v = h.driver
                admit_current(h)
                if changed == "writer":
                    h.files[v.command_path] = b"unowned\n"
                else:
                    h.state["captureSeq"] = 1
                with self.assertRaises(s.REFUSAL):
                    v.send(["grip l 1"])
                self.assertEqual(h.command_writes, [])
                if changed == "writer":
                    self.assertTrue(v.unsafe_owner)
                    self.assertIn("not attempted", v.release_after_failure())

    def test_only_bounded_ordinary_left_commands_are_admitted(self):
        for line in ("grip r 1", "trigger l 1", "btn b down", "shot test", "hand l follow on",
                     "hand l grip pose nan 0 0 0 0 0", "hand l grip pose 11 0 0 0 0 0",
                     "grip l 1\ntrigger r 1"):
            with self.subTest(command=line), self.assertRaises((s.REFUSAL, ValueError)):
                s.ordinary_left([line])
        s.ordinary_left(["hand l follow off", "hand l aimtrim 0 0", "hand l grip pose 0 0 0 0 0 0", "grip l 1"])


class SightArtifactTests(unittest.TestCase):
    def store(self, compressed=True):
        directory = Path(self.enterContext(tempfile.TemporaryDirectory(dir=ROOT / "reports")))
        store = s.transport.Artifacts(directory)
        return s.SightArtifacts(store) if compressed else store

    def response(self, size, seed=0):
        # Incompressible diagnostic values plus repeated JSON text exercise a
        # stored size near the measured 3.5-4.5 KiB, rather than an all-zero blob.
        raw = s.transport.json_bytes(dict(valid=True, diagnostic=random.Random(seed).randbytes(3500).hex()))
        self.assertLessEqual(len(raw), size)
        return raw[:-1] + b" " * (size - len(raw)) + b"\n"

    def test_measured_exhaustion_retains_all_bytes_through_100_responses(self):
        plain, compressed = self.store(False), self.store()
        # Actual40: 57 responses / 902,600 bytes plus 119,158 other bytes;
        # the next measured-maximum-size response crosses the ordinary cap.
        sizes = [10033] + [15939] * 55 + [15922]
        self.assertEqual(sum(sizes), 902600)
        other = b"x" * 119158
        for store in (plain, compressed):
            store.save("other-evidence.bin", other)
        for index, size in enumerate(sizes, 1):
            raw = self.response(size, index)
            name = f"snapshot-{index:03}.json"
            plain.save(name, raw)
            compressed.save(name, raw)
            self.assertEqual(gzip.decompress((compressed.directory / (name + ".gz")).read_bytes()), raw)
        self.assertEqual(sum(plain.sizes.values()), 1021758)
        with self.assertRaisesRegex(s.REFUSAL, "1 MiB"):
            plain.save("snapshot-058.json", self.response(16097, 58))
        for index in range(58, 101):
            raw = self.response(16097, index)
            name = f"snapshot-{index:03}.json"
            compressed.save(name, raw)
            self.assertEqual(gzip.decompress((compressed.directory / (name + ".gz")).read_bytes()), raw)
        self.assertEqual(len(compressed.snapshot_sizes), 100)
        self.assertLess(sum(compressed.sizes.values()), s.transport.MAX_BYTES - 12288)
        self.assertEqual((compressed.directory / "other-evidence.bin").read_bytes(), other)
        self.assertEqual(s.transport.MAX_BYTES, 1024 * 1024)

    def test_refused_expired_timeout_bytes_retained_and_observed_contract_unchanged(self):
        store = self.store()
        for name in ("snapshot-001.json", "snapshot-002-failure.json", "snapshot-003-at-timeout.json"):
            raw = b'{"valid":false,"invalid_reason":"synthetic_expired"}\r\n'
            store.save(name, raw)
            self.assertEqual(gzip.decompress((store.directory / (name + ".gz")).read_bytes()), raw)
            self.assertFalse((store.directory / name).exists())
        observed = b'{"full":"snapshot-observed input"}\n'
        store.save("snapshot-observed.json", observed)
        self.assertEqual((store.directory / "snapshot-observed.json").read_bytes(), observed)
        self.assertNotIn("snapshot-observed.json", store.snapshot_sizes)

    def test_decoded_cap_counts_replacements_once_and_refuses_before_write(self):
        store = self.store()
        raw = b"x" * s.sight.frame.SNAPSHOT_LIMIT
        self.assertEqual(s.MAX_SIGHT_SNAPSHOT_BYTES, 4 * 1024 * 1024)
        count = s.MAX_SIGHT_SNAPSHOT_BYTES // len(raw)
        self.assertEqual(count, 64)
        for index in range(count):
            store.save(f"snapshot-{index:03}.json", raw)
        store.save("snapshot-000.json", raw)
        sizes = store.sizes.copy()
        with self.assertRaisesRegex(s.REFUSAL, "4 MiB"):
            store.save("snapshot-128.json", b"x")
        with self.assertRaisesRegex(s.REFUSAL, "oversized sight snapshot"):
            store.save("snapshot-000.json", raw + b"x")
        self.assertEqual(store.sizes, sizes)
        self.assertEqual(sum(store.snapshot_sizes.values()), s.MAX_SIGHT_SNAPSHOT_BYTES)
        self.assertFalse((store.directory / "snapshot-128.json.gz").exists())

    def test_disk_cap_and_cleanup_result_reserves_remain_enforced(self):
        store = self.store()
        store.save("other-evidence.bin", b"x" * (s.transport.MAX_BYTES - 12288))
        sizes = store.sizes.copy()
        with self.assertRaisesRegex(s.REFUSAL, "1 MiB"):
            store.save("snapshot-001.json", b"{}\n")
        self.assertEqual(store.sizes, sizes)
        self.assertEqual(store.snapshot_sizes, {})
        self.assertFalse((store.directory / "snapshot-001.json.gz").exists())
        with self.assertRaisesRegex(s.REFUSAL, "artifact budget exhausted"):
            store.event(dict(type="ordinary"))
        store.event(dict(type="cleanup"), cleanup=True)
        with self.assertRaisesRegex(s.REFUSAL, "artifact budget exhausted"):
            store.event(dict(padding="x" * 4096), cleanup=True)
        store.save("result.json", dict(status="refused"), final=True)
        with self.assertRaisesRegex(s.REFUSAL, "1 MiB"):
            store.save("too-large-result.json", b"x" * 8192, final=True)
        self.assertLessEqual(sum(store.sizes.values()), s.transport.MAX_BYTES - 4096)


if __name__ == "__main__":
    unittest.main()
