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
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("feed_planner", ROOT / "tools/plan_bc2_feed_simulation.py")
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)
PROFILE_PATH = None
NOW = 1_000_000_000


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
                targetKind=0, chainCount=g["chainCount"], input=copy.deepcopy(evidence),
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
                    native_deadline_ns=now+65_000_000, weapon_actions_blocked=False,
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

    def test_stale_and_mismatched_sources_refuse(self):
        row = self.profiles["profiles"][0]
        base = synthetic_snapshot(row)
        faults = [lambda s: s.update(valid_until_ns=NOW), lambda s: s.update(native_deadline_ns=NOW),
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
