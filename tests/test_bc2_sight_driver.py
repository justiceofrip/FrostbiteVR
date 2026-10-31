"""Synthetic bounded sight transport checks, without process/game/native effects.

Run: python -B -m unittest discover -s tests -p test_bc2_sight_driver.py
The existing feed harness supplies in-memory files, simulator and QPC clock.
"""
import copy
import importlib.util
from pathlib import Path
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
        selectedSecondary=False, inventory=14, targetWeapon=40, targetSlot=2, targetAction=33,
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
    after["sight"].update(status="geometry_unavailable", contact=None)
    after["sight"]["family"].update(selectedSecondary=True, targetWeapon=4, targetSlot=1, targetAction=36)
    return after


def harness(testcase, ack_ms=5):
    h = memory.Harness(testcase, ack_ms)
    v = h.driver
    v.__class__ = s.SightDriver
    v.args = SimpleNamespace(target="secondary", step_ms=20)
    v.sight_plan = dict(expectedTransition=dict(sourceMode="primary", targetMode="secondary",
        sourceWeapon=4, targetWeapon=40, targetSlot=2, targetAction=33, asset="test2",
        configurationPath="config2", inventory=14, physicalItem=20, persistentId="synthetic-physical-item"))
    v.transition_observation, v.release_authorized = None, False
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


class SightDriverTests(unittest.TestCase):
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
                        targetWeapon=4, targetSlot=1, targetAction=36, asset="test1",
                        configurationPath="config1", inventory=14, physicalItem=20,
                        persistentId="synthetic-physical-item")
        self.assertEqual(s.selection_state(secondary, primary, expected), "target")
        primary["sight"]["family"]["targetAction"] = 36
        with self.assertRaisesRegex(s.REFUSAL, "inverse target"):
            s.selection_state(secondary, primary, expected)

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


if __name__ == "__main__":
    unittest.main()
