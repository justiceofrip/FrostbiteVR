"""Passive reference gaps are not ownership changes or fresh native authority."""
import copy
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import queue
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


f = load("reference_driver_fixtures", ROOT / "tests/test_bc2_reload_stimulus.py")
m, g, xf, NOW = f.m, f.fixtures.g, f.fixtures.xf, f.fixtures.NOW


def linked_source():
    source = f.fixtures.complete(xf.snapshot(3))
    x = source["fixedContainer"]
    x["physicalFamily"] = dict(owner=copy.deepcopy(source["owner"]), observedNs=NOW-200_000_000,
        deadlineNs=NOW+300_000_000, current=True, physicalItem=source["equipment"]["persistence"],
        inventory=source["selected"]["inventory"], selectedSecondary=True, targetWeapon=15, targetSlot=0,
        action=25, primaryAction=25, configurationPath=source["selected"]["configurationPath"],
        asset=source["equipment"]["asset"], persistentId="sp_xm8_s")
    x["tracking"]["gun"]["token"]["item"]["id"] = source["equipment"]["persistence"]
    return source


def missing_reference(source, *, default_tracking=True):
    gap = copy.deepcopy(source)
    x = gap["fixedContainer"]
    x["physicalFamily"] = None
    if default_tracking:
        x["tracking"].update(enabled=False, current=False, lifetime=0, revision=0, hand=None)
        x["tracking"]["gun"]["deadlineNs"] = 0
    return gap


def driver_for(source, clock):
    d = m.ReloadStimulus.__new__(m.ReloadStimulus)
    d.args = types.SimpleNamespace(family="fixed", stage="Open", target="secondary", step_ms=40)
    d.geometry, d.mapping, d.profiles = g, source, None
    d.observer = types.SimpleNamespace(fatal=None, inbox=queue.Queue())
    d.artifacts, d.observed, d.boundaries = f.Artifacts(), [], {}
    d.decoded_snapshot_bytes = 0
    d.observation_failure = None
    d.observation_incomplete = d.target_observed = d.completion_only_observed = False
    d.held_left = d.pending_command = False
    d.commands = 0
    d.deadline = d.stage_deadline = d.hard_deadline = clock.now + m.NS
    d.last_write = d.last_commit = clock.now - 2_000_000
    d.stimulus = g.build_stimulus("fixed", None, source, NOW, "Open")
    def guard(deadline_ns=None):
        d.bounds()
        d.observations(deadline_ns)
    d.guard = guard
    return d


def enqueue(d, snapshot, now, index=0):
    d.observer.inbox.put(dict(kind="snapshot", index=index, snapshot=snapshot,
        raw=json.dumps(snapshot).encode(), requestNs=now-1_000_000,
        receivedNs=now, ownershipCheckedNs=now, requestOwned=True, responseOwned=True,
        deadlineNs=now+m.RECEIVE_NS))


class ReferenceObservationTests(unittest.TestCase):
    def test_missing_reference_is_incomplete_without_mutating_source_or_plan(self):
        source = linked_source()
        for default_tracking in (True, False):
            with self.subTest(default_tracking=default_tracking):
                gap = missing_reference(source, default_tracking=default_tracking)
                before = copy.deepcopy((source, gap))
                self.assertEqual(g.same_mapping(source, gap, "fixed", "Open"), "incomplete")
                self.assertEqual((source, gap), before)
                with self.assertRaises(g.GeometryRefusal):
                    g._same(g._mapping(source, "fixed"), g._mapping(gap, "fixed"))
                with self.assertRaises(g.GeometryRefusal):
                    g.build_stimulus("fixed", None, gap, NOW, "Open")

    def test_observer_gap_cannot_be_current_or_replace_active_mapping(self):
        source, clock = linked_source(), f.Clock()
        d = driver_for(source, clock)
        # Even an otherwise current publication cannot activate a missing reference.
        gap = missing_reference(source, default_tracking=False)
        enqueue(d, gap, clock.now)
        original = copy.deepcopy((source, d.stimulus))
        with patch.object(m, "time", clock):
            d.observations()
        self.assertIs(d.mapping, source)
        self.assertEqual((source, d.stimulus), original)
        self.assertFalse(d.observed[-1]["current"])
        self.assertTrue(d.observation_incomplete)
        self.assertTrue(d.artifacts.events[-1]["mappingReferenceMissing"])
        self.assertFalse(d.target_observed or d.completion_only_observed)
        self.assertEqual(d.commands, 0)

    def test_missing_reference_cannot_complete_or_renew_original_boundary(self):
        for boundary in (None, dict(id="fixed_acquired", timeoutNs=20_000_000),
                         dict(id="fixed_step_complete", timeoutNs=20_000_000)):
            with self.subTest(boundary=boundary):
                source, clock = linked_source(), f.Clock()
                d = driver_for(source, clock)
                d.stage_deadline = clock.now + 20_000_000
                original_deadline, original_mapping = d.stage_deadline, copy.deepcopy(source)
                enqueue(d, missing_reference(source, default_tracking=False), clock.now)
                with patch.object(m, "time", clock), self.assertRaises(m.Refusal):
                    d.fresh(boundary=boundary, prepare=boundary is None)
                self.assertEqual(clock.now, original_deadline)
                self.assertEqual(d.stage_deadline, original_deadline)
                self.assertEqual(d.mapping, original_mapping)
                self.assertEqual(d.boundaries, {})
                self.assertFalse(d.completion_only_observed)
                self.assertEqual(d.commands, 0)

    def test_exact_reference_recovery_can_supply_only_verified_current_acquisition(self):
        source, clock = linked_source(), f.Clock()
        d = driver_for(source, clock)
        original_deadline = d.stage_deadline
        clock.now += 10_000_000
        enqueue(d, missing_reference(xf.refresh(source, clock.now, 109)), clock.now)
        with patch.object(m, "time", clock):
            d.observations()
        recovery = xf.physical_acquired(source, now=clock.now+10_000_000, sequence=110)
        clock.now += 10_000_000
        enqueue(d, recovery, clock.now, 1)
        with patch.object(m, "time", clock):
            observed, _ = d.fresh(boundary=dict(id="fixed_acquired", timeoutNs=20_000_000))
        self.assertIs(observed, recovery)
        self.assertIs(d.mapping, source)
        self.assertEqual(d.stage_deadline, original_deadline)
        self.assertTrue(d.boundaries["fixed_acquired"]["observed"])
        self.assertFalse(d.boundaries["fixed_acquired"]["nativeAcceptance"])
        self.assertFalse(d.completion_only_observed)
        self.assertEqual(d.commands, 0)

    def test_contradictory_reference_after_gap_stops_first_failure(self):
        for field, changed in (("physicalItem", 999), ("targetWeapon", 999), ("selectedSecondary", False),
                               ("persistentId", "other"), ("inventory", 999)):
            with self.subTest(field=field):
                source, clock = linked_source(), f.Clock()
                d = driver_for(source, clock)
                enqueue(d, missing_reference(source), clock.now)
                with patch.object(m, "time", clock):
                    d.observations()
                bad = copy.deepcopy(source)
                bad["fixedContainer"]["physicalFamily"][field] = changed
                enqueue(d, bad, clock.now, 1)
                with patch.object(m, "time", clock), self.assertRaises(g.GeometryRefusal):
                    d.observations()
                self.assertIsNotNone(d.observation_failure)
                with self.assertRaisesRegex(m.Refusal, "previous native observation failure"):
                    d.send(f.sample(0, "move", 1, .1))
                self.assertEqual(d.commands, 0)

    def test_missing_reference_preserves_every_independent_mapping_guard(self):
        changes = (lambda s: s["owner"].update(weapon=999),
                   lambda s: s["equipment"].update(data=999),
                   lambda s: s["rig"].update(fingerprint=999),
                   lambda s: s["selected"].update(selectedSlot=8),
                   lambda s: s["fixedContainer"]["profile"].update(revision=999),
                   lambda s: s["input"]["hands"][1].update(squeeze=0.),
                   lambda s: s["input"].update(spaceGeneration=999),
                   lambda s: s["mapping"]["weaponWorldMeters"][3].__setitem__(0, 99.),
                   lambda s: s["mapping"]["anatomicalWristToGrip"][3].__setitem__(0, .1))
        for index, mutate in enumerate(changes):
            with self.subTest(index=index):
                source = linked_source()
                gap = missing_reference(source)
                mutate(gap)
                with self.assertRaises(g.GeometryRefusal):
                    g.same_mapping(source, gap, "fixed", "Open")

    def test_recovered_root_lifetime_binding_native_identity_drift_refuses_boundary(self):
        changes = (lambda x: x["tracking"]["gun"]["token"].update(id=999),
                   lambda x: x["tracking"].update(lifetime=999),
                   lambda x: x["view"]["binding"].update(switching=999),
                   lambda x: x["view"]["identity"].update(serverItem=999))
        for index, mutate in enumerate(changes):
            with self.subTest(index=index):
                source = linked_source()
                stimulus = g.build_stimulus("fixed", None, source, NOW, "Open")
                recovered = xf.physical_acquired(source)
                mutate(recovered["fixedContainer"])
                with self.assertRaises(g.GeometryRefusal):
                    g.check_boundary(stimulus, recovered, "fixed_acquired", NOW+200_000_000)

    @unittest.skipUnless(os.environ.get("BC2_RELOAD_REFERENCE_REPLAY"), "pinned actual332 artifacts required")
    def test_pinned_actual332_first_difference_is_reference_gap(self):
        folder = Path(os.environ["BC2_RELOAD_REFERENCE_REPLAY"])
        pins = json.loads(Path(os.environ["BC2_RELOAD_REFERENCE_PINS"]).read_text())["evidence"]
        snapshots = []
        for i in range(7):
            name = f"observation-{i:03}.json.gz"
            raw = (folder/name).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), pins[name]["sha256"])
            snapshots.append(json.loads(gzip.decompress(raw)))
        source, gap = snapshots[0], snapshots[6]
        original = copy.deepcopy(snapshots)
        for value in snapshots[:6]:
            self.assertEqual(g.same_mapping(source, value, "fixed", "Open"), "source")
        self.assertEqual(g.same_mapping(source, gap, "fixed", "Open"), "incomplete")
        self.assertEqual(g.source_deadline(gap, "fixed"), 0)
        self.assertIsNone(gap["fixedContainer"]["physicalFamily"])
        self.assertEqual(snapshots, original)
        with self.assertRaises(g.GeometryRefusal):
            g.admit_observation("fixed", None, gap, gap["observed_ns"])


if __name__ == "__main__":
    unittest.main()
