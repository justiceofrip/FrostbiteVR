"""Completion-only sight observation regressions; never native acceptance."""
import copy
import gzip
import importlib.util
import json
import os
from pathlib import Path
import queue
import types
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("sight_completion_fixtures", ROOT / "tests/test_bc2_reload_stimulus.py")
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)
m, g = f.m, f.fixtures.g
REFUSALS = (m.Refusal, g.GeometryRefusal)


def fixture(secondary=False, subtree=False):
    source = f.fixtures.complete(f.fixtures.sf.snapshot(secondary=secondary, subtree=subtree, primary_slot=0))
    target = "primary" if secondary else "secondary"
    plan = g.build_stimulus("sight", None, source, f.fixtures.NOW, "SightFlip", target=target)
    after = copy.deepcopy(source)
    family = source["sight"]["family"]
    after["nonce"] += 1
    after["observed_ns"] = after["input_evidence"]["observedNs"] = f.fixtures.NOW + 20_000_000
    after["input_evidence"]["nowNs"] = after["observed_ns"]
    after["input_evidence"]["released"][0] = False
    after["input"]["hands"][0]["squeeze"] = 1.
    after["owner"].update(weapon=family["targetWeapon"], equipGeneration=source["owner"]["equipGeneration"] + 1)
    after["equipment"].update(weapon=family["targetWeapon"], asset=family[target + "Asset"])
    after["selected"].update(selectedSlot=family["targetSlot"], configurationPath=family[target + "Configuration"])
    after["sight"].update(status="geometry_unavailable", contact=None, preferredByDistance=None)
    after["sight"]["family"].update(selectedSecondary=not secondary, targetWeapon=source["owner"]["weapon"],
        targetSlot=source["selected"]["selectedSlot"], targetAction=33 if secondary else family["primaryAction"])
    return source, plan, after


def driver_for(source, plan, clock):
    d = m.ReloadStimulus.__new__(m.ReloadStimulus)
    d.args = types.SimpleNamespace(family="sight", stage="SightFlip", target=plan["target"])
    d.geometry, d.mapping, d.stimulus = g, source, plan
    d.profiles = None
    d.boundaries = {"sight_contact": dict(observed=True, selectionState="source")}
    d.target_observed = d.completion_only_observed = d.observation_incomplete = False
    d.observation_failure = None
    d.held_left = True
    d.artifacts, d.observed = f.Artifacts(), []
    d.observer = types.SimpleNamespace(inbox=queue.Queue(), fatal=None)
    d.decoded_snapshot_bytes = d.commands = 0
    d.last_write, d.last_commit = clock.now - 25_000_000, clock.now - 20_000_000
    d.deadline = d.stage_deadline = d.hard_deadline = clock.now + 200_000_000
    d.guard = lambda deadline_ns=None: d.observations(deadline_ns)
    return d


def enqueue(d, snapshot, clock, *, received=None, owned=True):
    d.observer.inbox.put(dict(kind="snapshot", index=0, snapshot=snapshot, raw=json.dumps(snapshot).encode(),
        requestNs=clock.now - 20_000_000, receivedNs=clock.now if received is None else received,
        ownershipCheckedNs=clock.now if received is None else received, requestOwned=owned,
        responseOwned=True, deadlineNs=clock.now + m.RECEIVE_NS))


class SightTransitionCompletionTests(unittest.TestCase):
    def setup_case(self, **kwargs):
        source, plan, after = fixture(**kwargs)
        clock = f.Clock()
        clock.now += 30_000_000
        return driver_for(source, plan, clock), after, clock

    def boundary(self, d):
        return next(b for b in d.stimulus["boundaries"] if b["id"] == "sight_selected")

    def test_both_modes_and_assemblies_complete_without_inventing_geometry(self):
        for secondary in (False, True):
            for subtree in (False, True):
                with self.subTest(secondary=secondary, subtree=subtree):
                    d, after, clock = self.setup_case(secondary=secondary, subtree=subtree)
                    original = copy.deepcopy(after)
                    enqueue(d, after, clock)
                    with patch.object(m, "time", clock):
                        d.fresh(boundary=self.boundary(d), after_ns=d.last_write)
                    summary = d.boundaries["sight_selected"]
                    self.assertTrue(d.target_observed and d.completion_only_observed)
                    self.assertTrue(summary["completionOnly"] and summary["sightGeometryUnavailable"])
                    self.assertFalse(summary["nativeReceiptProof"] or summary["nativeAcceptance"] or summary["headsetAcceptance"])
                    self.assertEqual(after, original)

    def test_incomplete_family_or_wrong_reciprocal_owner_never_completes(self):
        mutations = [
            lambda s: s["sight"].update(family=None),
            lambda s: s["sight"]["family"].update(targetWeapon=999),
            lambda s: s["sight"]["family"].update(targetSlot=9),
            lambda s: s["sight"]["family"].update(targetAction=36),
            lambda s: s["sight"]["family"].update(physicalItem=99),
            lambda s: s["sight"]["family"].update(persistentId="other-item"),
            lambda s: s["selected"].update(selectedSlot=9),
            lambda s: s["owner"].update(weapon=99),
            lambda s: s["owner"].update(actorGeneration=99),
            lambda s: s.update(physical_equipment_generation=99),
            lambda s: s["input"].update(trackingBasisGeneration=99),
            lambda s: s["input"]["hands"][1].update(squeeze=0.),
            lambda s: s["sight"].update(generation=99),
            lambda s: s["sight"].update(deadlineNs=s["native_deadline_ns"] + 1),
        ]
        for mutate in mutations:
            d, after, clock = self.setup_case()
            mutate(after)
            enqueue(d, after, clock)
            with patch.object(m, "time", clock), self.assertRaises(REFUSALS + (TypeError,)):
                d.fresh(boundary=self.boundary(d), after_ns=d.last_write)
            self.assertNotIn("sight_selected", d.boundaries)

    def test_absent_geometry_on_source_is_not_pending_or_success(self):
        d, after, clock = self.setup_case()
        after = copy.deepcopy(d.mapping)
        after["sight"].update(status="geometry_unavailable", contact=None, preferredByDistance=None)
        with self.assertRaises(REFUSALS):
            d.sight_completion_mapping(after)

    def test_completion_requires_original_contact_and_held_input(self):
        for change in (lambda d: d.boundaries.clear(), lambda d: setattr(d, "held_left", False)):
            d, after, clock = self.setup_case()
            change(d)
            with self.assertRaises(m.Refusal):
                d.sight_completion_mapping(after)
        d, after, clock = self.setup_case()
        with self.assertRaisesRegex(m.Refusal, "completion-only"):
            d.sight_completion_boundary(after, "sight_contact", clock.now)
        with self.assertRaises(g.GeometryRefusal):
            g.build_stimulus("sight", None, after, clock.now, "SightFlip", target="primary")

    def test_boundary_retains_fresh_frame_trigger_and_command_checks(self):
        mutations = [
            lambda s: s.update(valid=False),
            lambda s: s["input"].update(focused=False),
            lambda s: s["input"]["hands"][0].update(trigger=1.),
            lambda s: s["selected"].update(configurationPathVerified=False),
            lambda s: s["input_evidence"].update(observedNs=f.fixtures.NOW),
            lambda s: s.update(nonce=100),
        ]
        for mutate in mutations:
            d, after, clock = self.setup_case()
            mutate(after)
            with self.assertRaises(REFUSALS):
                d.sight_completion_boundary(after, "sight_selected", clock.now)
        d, after, clock = self.setup_case()
        with self.assertRaises(REFUSALS):
            d.sight_completion_boundary(after, "sight_selected", g.source_deadline(after, "sight"))

    def test_expired_and_foreign_observations_never_supply_boundary(self):
        for foreign in (False, True):
            d, after, clock = self.setup_case()
            if not foreign:
                clock.now = g.source_deadline(after, "sight")
            original_deadline = d.stage_deadline
            enqueue(d, after, clock, owned=not foreign)
            with patch.object(m, "time", clock), self.assertRaises(m.Refusal):
                d.fresh(boundary=self.boundary(d), after_ns=d.last_write)
            self.assertEqual(d.stage_deadline, original_deadline)
            self.assertNotIn("sight_selected", d.boundaries)
            self.assertTrue(d.observation_incomplete)

    def test_completion_discovered_during_pause_cannot_publish_old_pose(self):
        for during_pause in (False, True):
            d, after, clock = self.setup_case()
            d.first_sample_pending = False
            d.completion_only_observed = not during_pause
            d.pause_until = lambda until: setattr(d, "completion_only_observed", True)
            d.read_state = Mock()
            sample = next(s for s in d.stimulus["schedule"] if s["phase"] == "acquire")
            with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, "another controller pose"):
                d.send(sample)
            d.read_state.assert_not_called()
            self.assertEqual(d.commands, 0)

    @unittest.skipUnless(os.environ.get("BC2_SIGHT_TRANSITION_EVIDENCE"), "retained actual recording is opt-in")
    def test_actual_normal83_03_snapshot_027_completes_original_boundary_only(self):
        folder = Path(os.environ["BC2_SIGHT_TRANSITION_EVIDENCE"])
        load = lambda i: json.loads(gzip.decompress((folder / f"observation-{i:03}.json.gz").read_bytes()))
        source, after = load(0), load(27)
        plan = json.loads((folder / "stimulus-01.json").read_bytes())
        events = [json.loads(line) for line in (folder / "events.jsonl").read_text().splitlines()]
        result = json.loads((folder / "result.json").read_bytes())
        event = next(e for e in events if e["type"] == "native_observation" and e["index"] == 27)
        clock = f.Clock()
        clock.now = event["drainedNs"]
        d = driver_for(source, plan, clock)
        d.boundaries = copy.deepcopy(result["boundaries"])
        command = [e for e in events if e["type"] == "command_commit" and not e["cleanup"]][-1]
        d.last_write, d.last_commit = command["writtenNs"], command["committedNs"]
        d.stage_deadline = result["pendingBoundary"]["deadlineNs"]
        d.deadline = d.hard_deadline = d.stage_deadline
        original = copy.deepcopy(after)
        d.observer.inbox.put(dict(kind="snapshot", snapshot=after,
            raw=gzip.decompress((folder / "observation-027.json.gz").read_bytes()),
            **{k: event[k] for k in ("index", "requestNs", "deadlineNs", "receivedNs", "ownershipCheckedNs",
                                     "requestOwned", "requestNonce", "responseOwned")}))
        with self.assertRaises(g.GeometryRefusal):
            g.same_mapping(source, after, "sight", "SightFlip", target="secondary")
        with patch.object(m, "time", clock):
            d.fresh(boundary=self.boundary(d), after_ns=d.last_write)
        self.assertLess(clock.now, result["pendingBoundary"]["deadlineNs"])
        self.assertEqual(d.boundaries["sight_selected"]["snapshotNonce"], after["nonce"])
        self.assertTrue(d.completion_only_observed)
        self.assertFalse(d.boundaries["sight_selected"]["nativeAcceptance"])
        self.assertEqual(after, original)
        self.assertTrue(after["fixedContainer"]["available"])
        self.assertFalse(after["fixedContainer"]["viewFresh"])
        with self.assertRaises(g.GeometryRefusal):
            g.admit_boundary("fixed", None, after, clock.now, "Open")


if __name__ == "__main__":
    unittest.main()
