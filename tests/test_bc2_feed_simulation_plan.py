"""Synthetic geometry tests, not native or headset evidence.

Run: python tests/test_bc2_feed_simulation_plan.py --profiles EXPORT.json
or set BC2_FEED_SIMULATION_PROFILES and invoke unittest discovery. The exporter
must be linked to production and include supplyConfig/bodyFrameProbe.
"""
import argparse
import copy
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import sys
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("feed_planner", ROOT / "tools/plan_bc2_feed_simulation.py")
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)
PROFILE_PATH = None
NOW = 1_000_000_000


# Retained feedback221 SeatFeed samples; the surrounding fixtures remain synthetic.
OBSERVED_RIGHT_POSE = dict(position=[0.200000003, 1.29999995, -0.349999994],
    orientation=[0.206157163, -0.239314288, 0.0520769954, 0.947373152])


def observed_right_pose(snapshot, changed=False):
    for key in ("grip", "aim"):
        pose = copy.deepcopy(OBSERVED_RIGHT_POSE)
        if changed:
            pose["orientation"][1:3] = [-0.239314303, 0.0520769916]
        snapshot["input"]["hands"][1][key] = pose


def float32_steps(value, count):
    bits = struct.unpack("<I", struct.pack("<f", value))[0]
    return struct.unpack("<f", struct.pack("<I", bits + count))[0]


def frame(position, angles):
    return p.from_pose({"position": position, "orientation": p.ypr_quaternion(angles)})


def terminal(pose):
    result = pose["weaponFromFeedRoot"]
    for child in pose["parentFromChainChild"]:
        result = p.multiply(child, result)
    return result


def emitted_wrist(snapshot, command):
    absolute = p.from_pose({"position": command["positionMeters"],
                            "orientation": p.ypr_quaternion(command["yawPitchRollDegrees"])})
    mapping = snapshot["mapping"]
    return p.multiply(p.multiply(mapping["anatomicalWristToGrip"],
                                p.multiply(absolute, p.inverse(p.from_pose(snapshot["input"]["referenceHead"])))),
                      mapping["trackingBodyWorldMeters"])


def set_wrist(snapshot, world):
    mapping, inp = snapshot["mapping"], snapshot["input"]
    mapping["rawLeftWristWorldMeters"] = world
    absolute = p.multiply(p.multiply(p.multiply(p.inverse(mapping["anatomicalWristToGrip"]), world),
                                     p.inverse(mapping["trackingBodyWorldMeters"])), p.from_pose(inp["referenceHead"]))
    inp["hands"][0]["grip"] = p.to_pose(absolute)


def synthetic_snapshot(row, stage="OpenCover", now=NOW, sequence=100):
    """Invent owner/leases/claims explicitly; production export supplies geometry."""
    owner = dict(player=11, soldier=12, weak=13, weapon=14, actorGeneration=15, equipGeneration=16, space=17)
    physical = 18
    hand_owner = dict(actor=(13 << 32) | 12, actorGeneration=15, equipGeneration=physical, space=17)
    evidence = dict(owner=hand_owner, sequence=sequence, observedNs=now-10_000_000,
                    deadlineNs=now+70_000_000, nowNs=now, focused=True,
                    tracked=[True, True], released=[True, False])
    reference = p.to_pose(frame([.43, 1.5, -.27], [33, -9, 7]))
    head = p.to_pose(frame([.56, 1.57, -.43], [71, -22, 13]))
    right = p.to_pose(frame([.71, 1.2, -.62], [-25, 8, 19]))
    hand = dict(grip=right, aim=right, gripTracked=True, aimTracked=True, squeeze=0.)
    inp = dict(generation=sequence, spaceGeneration=17, trackingBasisGeneration=19,
               predictedNs=now, focused=True, headValid=True, worldUnitsPerMeter=3.7,
               referenceHead=reference, head=head, hands=[copy.deepcopy(hand), copy.deepcopy(hand)])
    w = frame([2.3, 1.7, -3.1], [21, -12, 8])
    b = frame([1.2, -.4, 2.8], [-37, 17, 11])
    k = frame([0, 0, 0], [-17, 13, -8])
    carry = frame([-1.3, .6, 2.4], [12, 6, -11])
    g = row["geometry"]
    phase = {"OpenCover": "Resting", "DetachContainer": "PouchDetachSource",
             "SupplyPickup": "PouchWithdrawn", "AttachContainer": "PouchWithdrawn",
             "SeatFeed": "PouchAttachedFeedUnseated", "CloseCover": "FeedSeated"}[stage]
    geom = g["poses"][phase]
    chain = [geom["weaponFromFeedRoot"]]
    for child in geom["parentFromChainChild"]:
        chain.append(p.multiply(child, chain[-1]))
    angle = 0 if stage == "OpenCover" else g["coverTravelRadians"]
    feed = dict(owner=owner, registrationKey=row["key"], rigFingerprint=row["rigFingerprint"],
                weapon=dict(id=14, generation=physical), lifetime=20, revision=21,
                supply=dict(resource=dict(id=22, generation=23), container=dict(id=14, generation=23)),
                operation={"OpenCover": 0, "DetachContainer": 2, "SupplyPickup": 4,
                           "AttachContainer": 5, "SeatFeed": 5, "CloseCover": 6}[stage],
                targetKind=0, blocksFire=stage != "OpenCover", chainCount=g["chainCount"], input=copy.deepcopy(evidence),
                mechanismInput=None, mechanismWristWorldMeters=None, mechanismClaim=None, supplyObject=None,
                coverRadians=angle, weaponFromCoverHinge=p.multiply(p.along(p.identity(), angle), g["weaponFromHinge"]),
                weaponFromPouch=geom["weaponFromPouch"], weaponFromChain=chain,
                carriedPartsMask=0, pouchCarryWeaponWorldMeters=carry, chainCarryWeaponWorldMeters=carry,
                originalWeaponWorldMeters=w)
    motion = row["operations"]["AttachContainer" if stage == "SupplyPickup" else stage]["motion"]
    if motion["kind"] == "Hinge":
        local = p.multiply(motion["motionFromContact"], motion["weaponFromMotion"])
    else:
        source = motion["placement"]["weaponFromSource"]
        local = p.multiply(motion["placement"]["sourceFromGrip"], source)
        if stage == "SeatFeed":
            feed["carriedPartsMask"] = 1 << (g["chainCount"]+1)
            feed["weaponFromChain"][-1] = p.multiply(p.multiply(source, w), p.inverse(carry))
        else:
            feed["carriedPartsMask"] = 1
            feed["weaponFromPouch"] = p.multiply(p.multiply(source, w), p.inverse(carry))
    # Preserve an actual acquired wrist offset; acquired orientation is permitted.
    local = copy.deepcopy(local)
    local[3][0] += .006
    snapshot = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, nonce=sequence,
                    observed_ns=now-1_000_000, valid_until_ns=now+60_000_000,
                    native_deadline_ns=now+65_000_000, weapon_actions_blocked=stage != "OpenCover",
                    physical_equipment_generation=physical, shot_input_generation=sequence,
                    owner=owner, equipment=dict(weapon=14, data=22, asset=row["asset"]),
                    rig=dict(soldier=12, weak=13, count=128, skeleton=24, pose=25, fingerprint=row["rigFingerprint"]),
                    selected=dict(configurationPathVerified=True, sequence=sequence, observedNs=now-10_000_000,
                                  deadlineNs=now+70_000_000, configurationPath=row["configurationPath"], inventory=26,
                                  matchedMeshPath=row["meshPath"], meshes=[dict(path=row["meshPath"])]),
                    input=inp, input_evidence=evidence, feed=feed,
                    mapping=dict(weaponWorldMeters=w, trackingBodyWorldMeters=b, anatomicalWristToGrip=k))
    set_wrist(snapshot, p.multiply(local, w))
    return snapshot


def add_synthetic_custody(profiles, row, snapshot):
    feed, inp, config = snapshot["feed"], snapshot["input"], profiles["supplyConfig"]
    item = dict(id=config["itemNamespace"], generation=27)
    token = dict(id=28, owner=feed["input"]["owner"], hand=0, kind=5, item=item,
                 contact=config["pouch"], prerequisiteClaim=0)
    claim = dict(token=token, inputSequence=inp["generation"], deadlineNs=feed["input"]["deadlineNs"])
    feed["input"]["released"] = [False, False]
    snapshot["input_evidence"]["released"] = [False, False]
    inp["hands"][0]["squeeze"] = 1.
    feed.update(operation=5, targetKind=2, mechanismClaim=claim, mechanismInput=copy.deepcopy(feed["input"]),
                mechanismWristWorldMeters=copy.deepcopy(snapshot["mapping"]["rawLeftWristWorldMeters"]))
    feed["supplyObject"] = dict(identity=dict(owner=token["owner"], weapon=feed["weapon"],
        profile=dict(id=row["key"], generation=row["mechanism"]["revision"]),
        pool=dict(id=26, generation=23), trackingEpoch=17), item=item, claim=copy.deepcopy(claim), family=1, units=30)


def add_synthetic_feed_claim(row, snapshot, stage):
    feed = snapshot["feed"]
    part = {"OpenCover": 0, "DetachContainer": 2, "SeatFeed": 3, "CloseCover": 0}[stage]
    contact = dict(id=(p.integer(row["key"])+part-1) % (2**64-1)+1, generation=feed["lifetime"])
    token = dict(id=28, owner=copy.deepcopy(feed["input"]["owner"]), hand=0, kind=4,
                 item=copy.deepcopy(feed["weapon"]), contact=contact, prerequisiteClaim=27)
    feed["input"]["released"] = [False, False]
    snapshot["input_evidence"]["released"] = [False, False]
    snapshot["input"]["hands"][0]["squeeze"] = 1.
    feed.update(operation=p.STAGES[stage], targetKind=1 if stage in ("OpenCover", "CloseCover") else 2,
                mechanismInput=copy.deepcopy(feed["input"]),
                mechanismClaim=dict(token=token, inputSequence=feed["input"]["sequence"],
                                    deadlineNs=feed["input"]["deadlineNs"]),
                mechanismWristWorldMeters=copy.deepcopy(snapshot["mapping"]["rawLeftWristWorldMeters"]))


class PreservedPosePrecisionTests(unittest.TestCase):
    def test_recorded_one_step_and_equivalent_sign_preserve_pose_without_mutation(self):
        before = copy.deepcopy(OBSERVED_RIGHT_POSE)
        after = copy.deepcopy(before)
        after["orientation"][1:3] = [-0.239314303, 0.0520769916]
        original = copy.deepcopy((before, after))
        self.assertTrue(p.preserved_pose_equal(before, after))
        self.assertEqual((before, after), original)
        after["orientation"] = [-v for v in after["orientation"]]
        self.assertTrue(p.preserved_pose_equal(before, after))

    def test_component_step_limit_applies_at_axes_signs_and_general_orientation(self):
        for orientation in ([0., 0., 0., 1.], [.5, -.5, .5, -.5], OBSERVED_RIGHT_POSE["orientation"]):
            for component in range(4):
                before = dict(position=[0., 0., 0.], orientation=list(orientation))
                for steps in (1, 2):
                    with self.subTest(orientation=orientation, component=component, steps=steps):
                        after = copy.deepcopy(before)
                        after["orientation"][component] = float32_steps(orientation[component], steps)
                        self.assertEqual(p.preserved_pose_equal(before, after), steps == 1)

    def test_translation_rotation_and_metadata_changes_are_not_representation_noise(self):
        before = copy.deepcopy(OBSERVED_RIGHT_POSE)
        for field in ("translation", "rotation", "metadata"):
            with self.subTest(field=field):
                after = copy.deepcopy(before)
                if field == "translation":
                    after["position"][0] = math.nextafter(after["position"][0], math.inf)
                elif field == "rotation":
                    after["orientation"] = p.quat_mul(after["orientation"], [0., math.sin(1e-5), 0., math.cos(1e-5)])
                else:
                    after["generation"] = 1
                self.assertFalse(p.preserved_pose_equal(before, after))

    def test_invalid_pose_is_refused_even_when_both_samples_match(self):
        for field, value in (("orientation", [0., 0., 0., 0.]), ("orientation", [0., 0., 0., 2.]),
                             ("orientation", [0., 0., 1.]), ("orientation", [math.nan, 0., 0., 1.]),
                             ("orientation", [math.inf, 0., 0., 1.]), ("orientation", [False, 0., 0., 1.]),
                             ("position", [0., math.nan, 0.]), ("position", [0., math.inf, 0.])):
            with self.subTest(field=field, value=value):
                bad = copy.deepcopy(OBSERVED_RIGHT_POSE)
                bad[field] = value
                with self.assertRaises(p.Refusal):
                    p.preserved_pose_equal(bad, bad)


class FeedSimulationPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = PROFILE_PATH or os.environ.get("BC2_FEED_SIMULATION_PROFILES")
        if not path:
            raise RuntimeError("provide --profiles or BC2_FEED_SIMULATION_PROFILES; no substitute registry fixture")
        cls.profiles, _ = p.load_json(path, 8*1024*1024)
        if len(cls.profiles["profiles"]) != 24 or "bodyFrameProbe" not in cls.profiles.get("supplyConfig", {}):
            raise RuntimeError("rebuild production exporter with supply geometry probe")

    def assert_pose(self, a, b, distance=.0005, angle=.005):
        d, r = p.pose_error(a, b)
        self.assertLessEqual(d, distance)
        self.assertLessEqual(r, angle)

    def cover_probe_case(self, row):
        # All ownership/receipt fields below are invented fixtures. Only the
        # registered geometry/policy comes from the unchanged production export.
        before = synthetic_snapshot(row)
        local = p.multiply(before["mapping"]["rawLeftWristWorldMeters"],
                           p.inverse(before["mapping"]["weaponWorldMeters"]))
        natural = p.multiply(p.along(p.identity(), math.radians(20)), local)
        set_wrist(before, p.multiply(natural, before["mapping"]["weaponWorldMeters"]))
        before["feedObservation"] = dict(observations="independent_atomics_not_native_proof",
            submit_attempts=1, receipts_accepted=1, latest_tick=dict(pending=False))
        plan = p.plan(self.profiles, before, "OpenCover", NOW, before["nonce"], held_cover_probe=True)
        acquire = next(t for t in plan["schedule"] if t["phase"] == "acquire")
        acquired = synthetic_snapshot(row, now=NOW+100_000_000, sequence=102)
        acquired["feed"]["revision"] = 22
        set_wrist(acquired, emitted_wrist(before, acquire))
        add_synthetic_feed_claim(row, acquired, "OpenCover")
        # Native contact presentation constrains translation, retaining the
        # actual acquisition orientation (Bc2PhysicalFeed::GripTarget).
        bound = p.multiply(acquired["mapping"]["rawLeftWristWorldMeters"],
                           p.inverse(acquired["mapping"]["weaponWorldMeters"]))
        motion = row["operations"]["OpenCover"]["motion"]
        bound[3] = p.multiply(motion["motionFromContact"], motion["weaponFromMotion"])[3]
        acquired["feed"]["mechanismWristWorldMeters"] = p.multiply(bound, acquired["mapping"]["weaponWorldMeters"])
        claim = p.held_feed(self.profiles, row, acquired, NOW+100_000_000, plan, NOW+80_000_000)
        anchor = p.cover_wrist_in_hinge(acquired)
        def held(target, now, sequence):
            snapshot = synthetic_snapshot(row, now=now, sequence=sequence)
            set_wrist(snapshot, emitted_wrist(before, target))
            add_synthetic_feed_claim(row, snapshot, "OpenCover")
            feed = snapshot["feed"]
            feed["revision"] = sequence
            feed["coverRadians"] = row["geometry"]["coverTravelRadians"]
            feed["weaponFromCoverHinge"] = p.multiply(p.along(p.identity(), feed["coverRadians"]),
                                                     row["geometry"]["weaponFromHinge"])
            feed["mechanismWristWorldMeters"] = p.multiply(p.multiply(anchor, feed["weaponFromCoverHinge"]),
                                                          feed["originalWeaponWorldMeters"])
            snapshot["feedObservation"] = dict(observations="independent_atomics_not_native_proof",
                submit_attempts=2, receipts_accepted=2, latest_tick=dict(pending=False))
            return snapshot
        return before, plan, claim, anchor, held

    def test_held_cover_probe_natural_acquisition_and_one_bounded_comparison_all24(self):
        for row in self.profiles["profiles"]:
            with self.subTest(asset=row["asset"], key=row["key"]):
                before, plan, claim, anchor, held = self.cover_probe_case(row)
                self.assert_schedule(plan, before)
                self.assertEqual(plan["pickup"], "actual observed offset")
                probe = [t for t in plan["schedule"] if t["phase"].startswith("held_probe_")]
                named = {t["phase"]: t for t in probe if t["phase"] != "held_probe_transition"}
                self.assertEqual(list(named), ["held_probe_"+n for n in ("control", "positive", "negative", "restore")])
                control = emitted_wrist(before, named["held_probe_control"])
                for name in ("positive", "negative"):
                    distance, angle = p.pose_error(control, emitted_wrist(before, named["held_probe_"+name]))
                    self.assertAlmostEqual(distance, .03, places=6)
                    self.assertAlmostEqual(angle, math.radians(20), places=6)
                self.assert_pose(control, emitted_wrist(before, named["held_probe_restore"]), 1e-8, 1e-7)
                previous = None
                for index, target in enumerate(probe):
                    now = NOW+200_000_000+index*20_000_000
                    snapshot = held(target, now, 104+index)
                    previous = p.held_cover_observation(self.profiles, row, snapshot, now, plan, target,
                        now-20_000_000, claim, anchor, before["feedObservation"], previous)
                    self.assertFalse(previous["nativeReceiptProof"])
                    if target["phase"] == "held_probe_positive":
                        with self.assertRaisesRegex(p.Refusal, "actual raw contact"):
                            p.held_feed(self.profiles, row, snapshot, now, plan, now-20_000_000)

    def test_held_cover_probe_rejects_custody_pose_and_source_loss_despite_completed_action(self):
        row = self.profiles["profiles"][0]
        before, plan, claim, anchor, held = self.cover_probe_case(row)
        control = next(t for t in plan["schedule"] if t["phase"] == "held_probe_control")
        target = next(t for t in plan["schedule"] if t["phase"] == "held_probe_positive")
        first = held(control, NOW+200_000_000, 104)
        previous = p.held_cover_observation(self.profiles, row, first, NOW+200_000_000, plan, control,
            NOW+180_000_000, claim, anchor, before["feedObservation"])
        base = held(target, NOW+240_000_000, 106)
        faults = {
            "lost_claim": lambda s: s["feed"].update(mechanismClaim=None),
            "different_token": lambda s: s["feed"]["mechanismClaim"]["token"].update(id=29),
            "closed_lid": lambda s: s["feed"].update(coverRadians=0, weaponFromCoverHinge=row["geometry"]["weaponFromHinge"]),
            "wrist_detached": lambda s: s["feed"]["mechanismWristWorldMeters"][3].__setitem__(0, 4),
            "old_command": lambda s: s["feed"]["mechanismInput"].update(observedNs=NOW),
            "expired_claim": lambda s: s["feed"]["mechanismClaim"].update(deadlineNs=NOW),
            "other_part": lambda s: s["feed"]["mechanismClaim"]["token"]["contact"].update(id=1),
            "other_owner": lambda s: s["feed"]["mechanismClaim"]["token"]["owner"].update(actor=1),
            "other_lifetime": lambda s: s["feed"].update(lifetime=99),
            "different_hand_input": lambda s: s["input"]["hands"][0]["grip"]["position"].__setitem__(0, 8),
            "no_receipt": lambda s: s["feedObservation"].update(receipts_accepted=1),
            "second_action": lambda s: s["feedObservation"].update(submit_attempts=3, receipts_accepted=3),
            "pending": lambda s: s["feedObservation"]["latest_tick"].update(pending=True),
        }
        for name, fault in faults.items():
            with self.subTest(fault=name):
                snapshot = copy.deepcopy(base)
                fault(snapshot)
                with self.assertRaises(p.Refusal):
                    p.held_cover_observation(self.profiles, row, snapshot, NOW+240_000_000, plan, target,
                        NOW+220_000_000, claim, anchor, before["feedObservation"], previous)

    def test_driver_held_cover_probe_stops_on_joint_observation_failure(self):
        spec = importlib.util.spec_from_file_location("feed_probe_driver", ROOT / "tools/drive_bc2_feed_simulation.py")
        driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(driver)
        row = self.profiles["profiles"][0]
        before, _, _, anchor, held = self.cover_probe_case(row)
        for fault in (None, "claim", "lid"):
            with self.subTest(fault=fault):
                clock, sent = [NOW], []
                instance = driver.Driver.__new__(driver.Driver)
                instance.profiles, instance.args = self.profiles, argparse.Namespace(step_ms=20, held_cover_probe=True)
                instance.reference, instance.snapshot_index = None, 1
                instance.plans, instance.commands, instance.last_commit = [], 0, None
                instance.deadline, instance.last_write = NOW+10_000_000_000, 0
                instance.artifacts = mock.Mock()
                def fresh():
                    instance.monitor = False  # Match the production fresh() handoff.
                    clock[0] += 20_000_000
                    if not sent:
                        return before, b"synthetic", NOW
                    target = sent[-1]
                    snapshot = held(target, clock[0], 102+len(sent))
                    if target["phase"] == "acquire":
                        snapshot["feed"]["coverRadians"] = 0
                        snapshot["feed"]["weaponFromCoverHinge"] = row["geometry"]["weaponFromHinge"]
                        snapshot["feed"]["mechanismWristWorldMeters"] = p.multiply(
                            p.multiply(anchor, row["geometry"]["weaponFromHinge"]), snapshot["feed"]["originalWeaponWorldMeters"])
                    if target["phase"] == "held_probe_positive":
                        if fault == "claim":
                            snapshot["feed"]["mechanismClaim"] = None
                        elif fault == "lid":
                            snapshot["feed"].update(coverRadians=0, weaponFromCoverHinge=row["geometry"]["weaponFromHinge"])
                    return snapshot, b"synthetic", clock[0]
                def send(lines, expected=None, source_deadline=None, final_release=False):
                    self.assertTrue(instance.monitor)
                    if source_deadline is not None:
                        self.assertLess(clock[0], source_deadline)
                    sent.append(expected)
                    instance.last_write = instance.last_commit = clock[0]
                instance.fresh, instance.send = fresh, send
                instance.pause_until = lambda deadline: clock.__setitem__(0, max(clock[0], deadline))
                instance.accept_snapshot = mock.Mock()
                instance.observe_stage_receipt = mock.Mock()
                with mock.patch.object(driver.time, "perf_counter_ns", side_effect=lambda: clock[0]):
                    if fault:
                        with self.assertRaises(driver.REFUSAL):
                            instance.stage("OpenCover")
                        self.assertEqual(sent[-1]["phase"], "held_probe_positive")
                        instance.observe_stage_receipt.assert_not_called()
                    else:
                        instance.stage("OpenCover")
                        self.assertEqual(sent[-1]["phase"], "release")
                        instance.observe_stage_receipt.assert_called_once()
                        phases = [v["phase"] for v in instance.plans[-1]["heldCoverObservations"]]
                        self.assertIn("held_probe_restore", phases)

    def assert_schedule(self, result, snapshot):
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["executionAuthorized"])
        self.assertEqual(result["preserve"]["referenceHead"], snapshot["input"]["referenceHead"])
        policy, commands = result["policy"], result["schedule"]
        self.assertLessEqual(result["sourceValidUntilNs"], snapshot["valid_until_ns"])
        self.assertLessEqual(commands[-1]["atNs"], policy["maximumGestureNs"])
        previous_wrist = snapshot["mapping"]["rawLeftWristWorldMeters"]
        previous_grip = p.from_pose(snapshot["input"]["hands"][0]["grip"])
        for command in commands:
            wrist = emitted_wrist(snapshot, command)
            grip = p.from_pose(dict(position=command["positionMeters"], orientation=p.ypr_quaternion(command["yawPitchRollDegrees"])))
            for a, b in ((previous_wrist, wrist), (previous_grip, grip)):
                distance, angle = p.pose_error(a, b)
                self.assertLessEqual(distance, policy["maxStepMeters"])
                self.assertLessEqual(angle, policy["maxStepRadians"])
            previous_wrist, previous_grip = wrist, grip

    def test_all24_five_stages_from_exported_production_geometry(self):
        for row in self.profiles["profiles"]:
            for stage in p.STAGES:
                with self.subTest(asset=row["asset"], stage=stage):
                    snap = synthetic_snapshot(row, stage)
                    if stage == "AttachContainer":
                        add_synthetic_custody(self.profiles, row, snap)
                    result = p.plan(self.profiles, snap, stage, NOW, snap["nonce"])
                    self.assert_schedule(result, snap)
                    motion, w = row["operations"][stage]["motion"], snap["mapping"]["weaponWorldMeters"]
                    start = p.multiply(snap["mapping"]["rawLeftWristWorldMeters"], p.inverse(w))
                    if motion["kind"] == "Hinge":
                        expected = p.multiply(p.along(p.multiply(start, p.inverse(motion["weaponFromMotion"])), motion["travel"]), motion["weaponFromMotion"])
                    else:
                        expected = p.multiply(p.multiply(start, p.inverse(motion["placement"]["weaponFromSource"])), motion["placement"]["weaponFromDestination"])
                    self.assert_pose(emitted_wrist(snap, result["schedule"][-1]), p.multiply(expected, w))
                    self.assertGreaterEqual(result["verification"]["endpointHeldNs"], motion["policy"]["endpointDwellNs"])
                    if stage == "AttachContainer":
                        self.assertEqual(result["schedule"][0]["phase"], "continue_held")
                        self.assertTrue(all(c["grip"] == 1 for c in result["schedule"][:-1]))

    def test_production_export_entry_and_destination_contract(self):
        # These are named authored pose relationships in BuildBc2FeedMotion,
        # not expectations obtained by asking the planner for its own targets.
        for row in self.profiles["profiles"]:
            g, ops = row["geometry"], row["operations"]
            for stage, source, destination in (("DetachContainer", "PouchDetachSource", "PouchWithdrawn"),
                                                ("AttachContainer", "PouchWithdrawn", "PouchAttachedFeedUnseated")):
                placement = ops[stage]["motion"]["placement"]
                self.assertEqual(placement["weaponFromSource"], g["poses"][source]["weaponFromPouch"])
                self.assertEqual(placement["weaponFromDestination"], g["poses"][destination]["weaponFromPouch"])
                self.assertEqual(placement["sourceFromGrip"], g["poses"][source]["sourceFromWrist"])
            self.assert_pose(ops["SeatFeed"]["motion"]["placement"]["weaponFromDestination"], terminal(g["poses"]["FeedSeated"]))
            self.assertEqual(ops["OpenCover"]["motion"]["motionFromContact"], g["hingeFromOpeningContact"])
        config = self.profiles["supplyConfig"]
        self.assertEqual(config["source"], "Bc2FeedSupplyConfig / BodyAnchorHandPose")
        self.assertTrue(config["productionContactCentersVerified"])
        probe = config["bodyFrameProbe"]
        self.assertTrue(probe["synthetic"])
        mapped = p.multiply(p.from_pose(probe["grip"]), p.inverse(p.supply_body_frame(probe)))
        self.assert_pose(mapped, probe["productionBodyFromHand"], .00001, .0001)

    def test_all24_chest_and_alternate_pickup_then_fresh_held_attachment(self):
        for row in self.profiles["profiles"]:
            for contact in ("chest", "alternate"):
                with self.subTest(asset=row["asset"], contact=contact):
                    before = synthetic_snapshot(row, "SupplyPickup")
                    pickup = p.plan_supply_pickup(self.profiles, before, NOW, before["nonce"], contact=contact)
                    self.assert_schedule(pickup, before)
                    schedule = pickup["schedule"]
                    self.assertEqual([c["phase"] for c in schedule[-2:]], ["neutral_at_contact", "acquire"])
                    self.assertTrue(all(c["grip"] == 0 for c in schedule[:-1]))
                    self.assertEqual(schedule[-1]["grip"], 1)
                    self.assertTrue(pickup["requiresPostPickupReobservation"])
                    # Independent forward coordinate check: original XR grip
                    # relative to head position and upright reference yaw.
                    ref = before["input"]["referenceHead"]["orientation"]
                    x, y, z, w = ref
                    yaw = math.atan2(2*(x*z+w*y), 1-2*(x*x+y*y))
                    expected_body = self.profiles["supplyConfig"][contact]["centerMeters"]
                    dx, dy, dz = [a-b for a, b in zip(schedule[-1]["positionMeters"], before["input"]["head"]["position"])]
                    observed_body = [math.cos(yaw)*dx-math.sin(yaw)*dz, dy,
                                     -(math.sin(yaw)*dx+math.cos(yaw)*dz)]
                    self.assertLess(math.dist(expected_body, observed_body), .00001)
                    after = synthetic_snapshot(row, "AttachContainer", NOW+100_000_000, sequence=102)
                    after["feed"]["revision"] += 2
                    # Actual pickup source follows production chestNeutral's
                    # inverse(PouchWithdrawn.sourceFromWrist) * raw local wrist.
                    wrist = emitted_wrist(before, schedule[-1])
                    set_wrist(after, wrist)
                    source = p.multiply(p.inverse(row["geometry"]["poses"]["PouchWithdrawn"]["sourceFromWrist"]),
                                        p.multiply(wrist, p.inverse(after["mapping"]["weaponWorldMeters"])))
                    after["feed"]["pouchCarryWeaponWorldMeters"] = after["mapping"]["weaponWorldMeters"]
                    after["feed"]["weaponFromPouch"] = source
                    add_synthetic_custody(self.profiles, row, after)
                    attached = p.plan(self.profiles, after, "AttachContainer", NOW+100_000_000,
                                      after["nonce"], pickup_plan=pickup)
                    self.assert_schedule(attached, after)
                    self.assertTrue(all(c["grip"] == 1 for c in attached["schedule"][:-1]))
                    offset = p.multiply(p.multiply(wrist, p.inverse(after["mapping"]["weaponWorldMeters"])), p.inverse(source))
                    end = p.multiply(p.multiply(offset, row["operations"]["AttachContainer"]["motion"]["placement"]["weaponFromDestination"]),
                                     after["mapping"]["weaponWorldMeters"])
                    self.assert_pose(emitted_wrist(after, attached["schedule"][-1]), end)

    def test_idle_and_completed_close_bootstrap_without_presentation_eligibility(self):
        row = self.profiles["profiles"][0]
        for operation in (0, 7):
            snap = synthetic_snapshot(row)
            snap["feed"]["operation"] = operation
            result = p.plan(self.profiles, snap, "OpenCover", NOW, snap["nonce"])
            self.assertEqual(result["schedule"][0]["phase"], "neutral")
            self.assertFalse(result["executionAuthorized"])
        snap["feed"]["coverRadians"] = .1
        with self.assertRaises(p.Refusal):
            p.plan(self.profiles, snap, "OpenCover", NOW, snap["nonce"])

    def test_all24_fresh_feed_claim_before_mechanism_motion(self):
        for row in self.profiles["profiles"]:
            for stage in ("OpenCover", "DetachContainer", "SeatFeed", "CloseCover"):
                with self.subTest(asset=row["asset"], stage=stage):
                    before = synthetic_snapshot(row, stage)
                    plan = p.plan(self.profiles, before, stage, NOW, before["nonce"])
                    self.assertTrue(plan["requiresPostAcquisitionReobservation"])
                    after = synthetic_snapshot(row, stage, NOW+100_000_000, sequence=102)
                    after["feed"]["revision"] += 2
                    add_synthetic_feed_claim(row, after, stage)
                    original = copy.deepcopy(after)
                    claim = p.held_feed(self.profiles, row, after, NOW+100_000_000, plan, NOW+80_000_000)
                    self.assertEqual(claim, after["feed"]["mechanismClaim"])
                    self.assertEqual(after, original, "observation must not renew evidence or claims")
                    # A current poll can be newer than the admitted raw source;
                    # processing nowNs is not original packet identity.
                    after["input"]["generation"] = after["shot_input_generation"] = 104
                    after["input_evidence"]["sequence"] = 104
                    after["input_evidence"]["observedNs"] += 1_000_000
                    after["feed"]["mechanismInput"]["nowNs"] -= 1_000_000
                    self.assertEqual(p.held_feed(self.profiles, row, after, NOW+100_000_000,
                                                 plan, NOW+80_000_000), claim)

    def test_feed_acquisition_refuses_support_stale_or_unbound_claim(self):
        row = self.profiles["profiles"][0]
        before = synthetic_snapshot(row, "SeatFeed")
        plan = p.plan(self.profiles, before, "SeatFeed", NOW, before["nonce"])
        base = synthetic_snapshot(row, "SeatFeed", NOW+100_000_000, sequence=102)
        base["feed"]["revision"] += 2
        add_synthetic_feed_claim(row, base, "SeatFeed")
        faults = [lambda s: s["feed"].update(mechanismClaim=None),
                  lambda s: s["feed"]["mechanismClaim"]["token"].update(kind=2),
                  lambda s: s["feed"]["mechanismClaim"]["token"].update(kind=5),
                  lambda s: s["feed"]["mechanismClaim"]["token"].update(prerequisiteClaim=0),
                  lambda s: s["feed"]["mechanismClaim"]["token"]["contact"].update(id=999),
                  lambda s: s["feed"]["mechanismClaim"]["token"]["item"].update(generation=999),
                  lambda s: s["feed"]["mechanismClaim"]["token"]["owner"].update(space=999),
                  lambda s: s["feed"]["mechanismClaim"].update(inputSequence=100),
                  lambda s: s["feed"]["mechanismClaim"].update(deadlineNs=NOW+100_000_000),
                  lambda s: s["feed"]["mechanismInput"].update(released=[True, False]),
                  lambda s: s["feed"].update(operation=5),
                  lambda s: s["feed"].update(targetKind=0),
                  lambda s: s["feed"].update(mechanismWristWorldMeters=None),
                  lambda s: s["feed"].update(revision=plan["sourceFeedRevision"]),
                  lambda s: s.update(nonce=plan["snapshotNonce"]),
                  lambda s: s["input"]["hands"][0].update(squeeze=0),
                  lambda s: s["feed"]["mechanismWristWorldMeters"][3].__setitem__(0, 100)]
        for fault in faults:
            snap = copy.deepcopy(base)
            fault(snap)
            with self.assertRaises(p.Refusal):
                p.held_feed(self.profiles, row, snap, NOW+100_000_000, plan, NOW+80_000_000)
        with self.assertRaisesRegex(p.Refusal, "post-squeeze"):
            p.held_feed(self.profiles, row, base, NOW+100_000_000, plan, NOW+95_000_000)

    def test_recorded_pose_precision_passes_feed_and_supply_custody_with_exact_gates(self):
        row = self.profiles["profiles"][0]
        for stage in ("SeatFeed", "SupplyPickup"):
            before = synthetic_snapshot(row, stage)
            observed_right_pose(before)
            plan = p.plan(self.profiles, before, stage, NOW, before["nonce"])
            after = synthetic_snapshot(row, "AttachContainer" if stage == "SupplyPickup" else stage,
                                       NOW+100_000_000, sequence=102)
            observed_right_pose(after, changed=True)
            after["feed"]["revision"] += 2
            if stage == "SupplyPickup":
                add_synthetic_custody(self.profiles, row, after)
                check = lambda s: p.held_supply(self.profiles, row, s, NOW+100_000_000, plan)
                expected = after["feed"]["supplyObject"]
            else:
                add_synthetic_feed_claim(row, after, stage)
                check = lambda s: p.held_feed(self.profiles, row, s, NOW+100_000_000, plan, NOW+80_000_000)
                expected = after["feed"]["mechanismClaim"]
            original = copy.deepcopy((plan, after))
            self.assertEqual(check(after), expected)
            self.assertEqual((plan, after), original)
            faults = [lambda s: s["input"]["hands"][1]["grip"]["position"].__setitem__(0, .200000004),
                      lambda s: s["input"]["hands"][1]["grip"]["orientation"].__setitem__(1,
                          float32_steps(OBSERVED_RIGHT_POSE["orientation"][1], 2)),
                      lambda s: s["input"].update(trackingBasisGeneration=999),
                      lambda s: s["owner"].update(weapon=999),
                      lambda s: s["feed"].update(lifetime=999)]
            for index, fault in enumerate(faults):
                with self.subTest(stage=stage, fault=index):
                    bad = copy.deepcopy(after)
                    fault(bad)
                    with self.assertRaises(p.Refusal):
                        check(bad)

    def test_driver_does_not_move_without_reobserved_feed_claim(self):
        spec = importlib.util.spec_from_file_location("feed_driver", ROOT / "tools/drive_bc2_feed_simulation.py")
        driver = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(driver)
        row = self.profiles["profiles"][0]
        for owned in (False, True):
            with self.subTest(owned=owned):
                clock, sent = [NOW], []
                instance = driver.Driver.__new__(driver.Driver)
                instance.profiles, instance.args = self.profiles, argparse.Namespace(step_ms=20)
                instance.reference, instance.snapshot_index = None, 1
                instance.plans, instance.commands, instance.last_commit = [], 0, None
                instance.deadline, instance.last_write = NOW+10_000_000_000, 0
                instance.artifacts = mock.Mock()
                def fresh():
                    clock[0] += 20_000_000
                    snap = synthetic_snapshot(row, "SeatFeed", clock[0], sequence=100+len(sent))
                    if sent:
                        snap["feed"]["revision"] += 2
                        if owned:
                            add_synthetic_feed_claim(row, snap, "SeatFeed")
                    return snap, b"synthetic", clock[0]
                def pause(deadline):
                    clock[0] = max(clock[0], deadline)
                def send(lines, expected=None, source_deadline=None, final_release=False):
                    if source_deadline is not None:
                        self.assertLess(clock[0], source_deadline)
                    sent.append(expected["phase"])
                    instance.last_write = instance.last_commit = clock[0]
                instance.fresh, instance.pause_until, instance.send = fresh, pause, send
                instance.accept_snapshot = mock.Mock()
                instance.observe_stage_receipt = mock.Mock()
                with mock.patch.object(driver.time, "perf_counter_ns", side_effect=lambda: clock[0]):
                    if owned:
                        instance.stage("SeatFeed")
                        self.assertIn("move", sent)
                        instance.observe_stage_receipt.assert_called_once()
                    else:
                        with self.assertRaisesRegex(driver.REFUSAL, "owned Feed mechanism claim"):
                            instance.stage("SeatFeed")
                        self.assertEqual(sent[-1], "acquire")
                        self.assertNotIn("move", sent)
                        instance.observe_stage_receipt.assert_not_called()

    def test_stale_and_mismatched_sources_refuse(self):
        row = self.profiles["profiles"][0]
        base = synthetic_snapshot(row)
        faults = [lambda s: s.update(valid_until_ns=NOW), lambda s: s.update(native_deadline_ns=NOW),
                  lambda s: s.update(weapon_actions_blocked=True),
                  lambda s: s.update(nonce=999), lambda s: s["owner"].update(weapon=999),
                  lambda s: s["selected"].update(matchedMeshPath="wrong"),
                  lambda s: s["rig"].update(fingerprint=999), lambda s: s["input"].update(generation=999),
                  lambda s: s["feed"]["input"].update(deadlineNs=NOW),
                  lambda s: s["mapping"]["anatomicalWristToGrip"][3].__setitem__(0, .01)]
        for fault in faults:
            snap = copy.deepcopy(base)
            fault(snap)
            with self.assertRaises(p.Refusal):
                p.plan(self.profiles, snap, "OpenCover", NOW, 100)
        snap = synthetic_snapshot(row, "AttachContainer")
        with self.assertRaises(p.Refusal):
            p.plan(self.profiles, snap, "AttachContainer", NOW, 100)
        add_synthetic_custody(self.profiles, row, snap)
        for fault in (lambda s: s["feed"]["supplyObject"]["identity"]["profile"].update(id=999),
                      lambda s: s["feed"]["supplyObject"]["identity"]["pool"].update(generation=999),
                      lambda s: s["feed"]["mechanismClaim"].update(deadlineNs=NOW),
                      lambda s: s["input"]["hands"][0].update(squeeze=0)):
            bad = copy.deepcopy(snap)
            fault(bad)
            with self.assertRaises(p.Refusal):
                p.plan(self.profiles, bad, "AttachContainer", NOW, 100)
        pickup = p.plan_supply_pickup(self.profiles, synthetic_snapshot(row, "SupplyPickup"), NOW, 100)
        with self.assertRaises(p.Refusal):
            p.plan(self.profiles, snap, "AttachContainer", NOW, 100, pickup_plan=pickup)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profiles", required=True)
    args, remaining = parser.parse_known_args()
    PROFILE_PATH = args.profiles
    unittest.main(argv=[sys.argv[0], *remaining])
