"""Synthetic transformed geometry/receipt refusal cases; no native acceptance."""
import copy
import hashlib
import json
import os
import importlib.util
import math
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


p = module("fixed_planner_test", ROOT / "tools/plan_bc2_fixed_container_simulation.py")
d = module("fixed_driver_test", ROOT / "tools/drive_bc2_fixed_container_simulation.py")
fixture = module("fixed_sight_fixture", ROOT / "tests/test_bc2_sight_simulation_plan.py")
precision = module("fixed_pose_precision_fixture", ROOT / "tests/test_bc2_feed_simulation_plan.py")
memory = module("fixed_transport_memory", ROOT / "tests/test_bc2_feed_driver_observation.py")
memory.d = d.transport
f, NOW = p.frame, 1_000_000_000


def claim(source, hand, kind, item, contact, identifier, prerequisite=0):
    return dict(token=dict(id=identifier, owner=copy.deepcopy(source["owner"]), hand=hand, kind=kind,
                           item=copy.deepcopy(item), contact=contact, prerequisiteClaim=prerequisite),
                inputSequence=source["sequence"], deadlineNs=source["deadlineNs"])


def set_local(s, local):
    m = s["mapping"]
    world = f.multiply(local, m["weaponWorldMeters"])
    absolute = f.multiply(f.multiply(f.multiply(f.inverse(m["anatomicalWristToGrip"]), world),
                         f.inverse(m["trackingBodyWorldMeters"])), f.from_pose(s["input"]["referenceHead"]))
    m["rawLeftWristWorldMeters"] = world
    s["input"]["hands"][0]["grip"] = f.to_pose(absolute)
    s["input"]["hands"][0]["aim"] = f.to_pose(absolute)
    if "fixedContainer" in s:
        s["fixedContainer"]["raw"]["weaponFromWrist"] = copy.deepcopy(local)


def move_world(source, transform):
    """Synthetic common native motion; controller input and source leases stay original."""
    s = copy.deepcopy(source)
    for field in ("weaponWorldMeters", "trackingBodyWorldMeters", "rawLeftWristWorldMeters"):
        s["mapping"][field] = f.multiply(s["mapping"][field], transform)
    raw = s["fixedContainer"]["raw"]
    raw["weaponWorldMeters"] = f.multiply(raw["weaponWorldMeters"], transform)
    return s


def snapshot(kind=4, motion_kind="Hinge"):
    s = fixture.snapshot(secondary=True)
    del s["sight"]
    s["input"]["floorRelative"] = False
    s["mapping"]["trackingBodyWorldMeters"][3][:3] = s["mapping"]["weaponWorldMeters"][3][:3]
    source, selected, owner = s["input_evidence"], s["selected"], s["owner"]
    policy = dict(completeFraction=.92, releaseFraction=.25, pathToleranceMeters=.012,
        orientationToleranceRadians=.35, maxStepMeters=.06, maxStepRadians=.5,
        endpointDwellNs=60_000_000, maximumGestureNs=10_000_000_000, maxInputGapNs=100_000_000)
    placement = dict(weaponFromSource=fixture.matrix([-.12, -.1, -.21], [13, -19, 21]),
        weaponFromDestination=fixture.matrix([.04, -.09, -.41], [-17, 23, -9]),
        sourceFromGrip=fixture.matrix([.012, -.024, .036], [7, -13, 19]))
    def geometry(index, kind):
        return dict(contact=dict(role=700+index, radiusMeters=.035, maxAngleRadians=.6, orientation=0),
            motion=dict(kind=kind, measured=True, returnToStart=False, travel=.4 if kind == "Hinge" else .11,
                policy=copy.deepcopy(policy), weaponFromMotion=fixture.matrix([.05, -.12, -.3], [31, -17, 11]),
                motionFromContact=fixture.matrix([.18, .02, -.03], [-13, 21, 29]), placement=copy.deepcopy(placement)))
    profile = dict(id=900, revision=2, kind=kind, ammunitionKind=44, capacity=1, roundsPerInsertion=1,
        maxAgeNs=100_000_000, configurationPath=selected["configurationPath"], meshPath=selected["matchedMeshPath"],
        rigFingerprint=s["rig"]["fingerprint"], geometry=[geometry(0, motion_kind), None,
                                                       geometry(2, "FreePlacement"), geometry(3, motion_kind)])
    context = dict(resource=dict(actor=owner["soldier"], actorGeneration=owner["actorGeneration"],
        weapon=owner["weapon"], weaponGeneration=99), equipGeneration=owner["equipGeneration"], space=owner["space"])
    binding = dict(owner=copy.deepcopy(owner), context=context, inventory=selected["inventory"], switching=33,
        data=s["equipment"]["data"], persistence=s["equipment"]["persistence"], observedNs=NOW-10_000_000, deadlineNs=NOW+70_000_000)
    ammunition = dict(context=copy.deepcopy(context), sequence=80, observedNs=NOW-10_000_000,
        deadlineNs=NOW+70_000_000, counts=dict(loaded=0, reserve=7, capacity=1), coherentIdleVerified=True)
    t = dict(enabled=True, current=True, blocksFire=False, carried=False, pending=False, step=0 if kind == 3 else 2,
        lifetime=400, revision=10, gun=claim(source, 1, 1, dict(id=14, generation=18), dict(id=1, generation=14), 200),
        hand=None, target=None, carriedSupply=None, pouchContact=False, grip=False,
        spentCasePresent=False, spentCaseEpoch=0, spentCaseEjectedNs=0, spentCaseEjectionWorldMeters=None)
    raw = dict(valid=True, current=True, admittedAtTick=True, ownerMatches=True, rigMatches=True, profileMatches=True,
        rigFingerprint=profile["rigFingerprint"], input=copy.deepcopy(source), weaponFromWrist=f.identity(),
        weaponWorldMeters=copy.deepcopy(s["mapping"]["weaponWorldMeters"]), grip=False, pouchContact=False)
    s["fixedContainer"] = dict(schema=p.OBSERVATION_SCHEMA, available=True, observationOnly=True, retainedEventsAreHistorical=True,
        owner=copy.deepcopy(owner),
        input=copy.deepcopy(source), profile=profile, tracking=t, raw=raw, leftClaim=None,
        pouch=dict(frame=1, centerMeters=[-.19, -.31, .13], radiusMeters=.14),
        supply=dict(resource=dict(id=300, generation=400), container=dict(id=0, generation=0)),
        viewFresh=True, view=dict(binding=binding, snapshot=ammunition, phase=1, command=None, request=0, requestState=0,
                  identity=dict(owner=copy.deepcopy(owner), firing=[51, 52, 53], serverPlayer=54, serverSoldier=55, serverItem=56),
                  terminal=None, receipt=None, wellEmpty=False), pendingRequest=None, lastCompletion=None)
    set_local(s, fixture.matrix([.02, -.11, -.24], [13, -19, 21]))
    return s


def refresh(s, now, sequence):
    """Invent a new synthetic native callback, never a live execution utility."""
    s = copy.deepcopy(s)
    def visit(value):
        if isinstance(value, dict):
            for key in value:
                if key in ("sequence", "inputSequence"):
                    value[key] = sequence
                elif key == "observedNs":
                    value[key] = now-1_000_000
                elif key == "deadlineNs":
                    value[key] = now+70_000_000
                elif key == "nowNs":
                    value[key] = now
                else:
                    visit(value[key])
        elif isinstance(value, list):
            for child in value:
                visit(child)
    visit(s)
    s.update(nonce=sequence, observed_ns=now-1_000_000, valid_until_ns=now+60_000_000,
             native_deadline_ns=now+65_000_000, shot_input_generation=sequence)
    s["input"].update(generation=sequence, predictedNs=now)
    s["fixedContainer"]["tracking"]["revision"] += 1
    return s


def held_after_pickup(before, plan, now=NOW+200_000_000):
    s = refresh(before, now, 110)
    x, end = s["fixedContainer"], plan["schedule"][-1]
    set_local(s, fixture.emitted_local(s, end))
    t, raw = x["tracking"], x["raw"]
    s["input"]["hands"][0]["squeeze"] = 1.
    s["weapon_actions_blocked"] = t["blocksFire"] = True
    t.update(carried=True, grip=True, pouchContact=True)
    for e in (s["input_evidence"], x["input"], raw["input"]):
        e["released"][0] = False
    # The raw packet and left claim intentionally precede current safety input.
    raw["input"].update(sequence=109, observedNs=now-2_000_000, deadlineNs=now+68_000_000)
    raw.update(grip=True, pouchContact=True)
    placement = x["profile"]["geometry"][2]["motion"]["placement"]
    t["carriedSupply"] = f.multiply(f.inverse(placement["sourceFromGrip"]), raw["weaponFromWrist"])
    t["target"] = copy.deepcopy(t["carriedSupply"])
    t["hand"] = claim(raw["input"], 0, 5, x["supply"]["resource"], dict(id=702, generation=2), 201)
    x["leftClaim"] = copy.deepcopy(t["hand"])
    return s


def physical_acquired(before, now=NOW+200_000_000, sequence=110):
    s = refresh(before, now, sequence)
    x, t = s["fixedContainer"], s["fixedContainer"]["tracking"]
    raw, profile = x["raw"], x["profile"]
    s["input"]["hands"][0]["squeeze"] = 1.
    s["weapon_actions_blocked"] = t["blocksFire"] = True
    t["grip"] = raw["grip"] = True
    for evidence in (s["input_evidence"], x["input"], raw["input"]):
        evidence["released"][0] = False
    t["hand"] = claim(raw["input"], 0, 4, t["gun"]["token"]["item"],
        dict(id=profile["geometry"][t["step"]]["contact"]["role"], generation=profile["revision"]),
        201, t["gun"]["token"]["id"])
    x["leftClaim"] = copy.deepcopy(t["hand"])
    t["target"] = copy.deepcopy(raw["weaponFromWrist"])
    return s


def physical_completed(acquired, stage, now=NOW+500_000_000, sequence=120):
    # Actual304 observation015: valid/current raw input survives PhysicalStep,
    # while Release clears its claim, target and admittedAtTick gesture bit.
    s = refresh(acquired, now, sequence)
    x, t = s["fixedContainer"], s["fixedContainer"]["tracking"]
    t.update(step=(1 if t["spentCasePresent"] else 2) if stage == "Open" else 4,
             hand=None, target=None, carried=False, carriedSupply=None, pending=False)
    t["blocksFire"] = s["weapon_actions_blocked"] = t["step"] != 4
    x["leftClaim"] = None
    x["raw"]["admittedAtTick"] = False
    return s


def aligned_for_insert(before, now=NOW+300_000_000, sequence=115):
    s = refresh(before, now, sequence)
    x = s["fixedContainer"]
    destination = x["profile"]["geometry"][2]["motion"]["placement"]["weaponFromDestination"]
    x["tracking"]["target"] = copy.deepcopy(destination)
    x["tracking"]["carriedSupply"] = copy.deepcopy(destination)
    return s


def completed(before, plan, now=NOW+500_000_000):
    s = refresh(before, now, 120)
    x, t = s["fixedContainer"], s["fixedContainer"]["tracking"]
    set_local(s, fixture.emitted_local(s, plan["schedule"][-1]))
    t.update(step=3 if x["profile"]["kind"] == 3 else 4, carried=False, hand=None, target=None, carriedSupply=None, grip=False)
    t["blocksFire"] = s["weapon_actions_blocked"] = t["step"] != 4
    x["leftClaim"], x["raw"]["grip"] = None, False
    s["input"]["hands"][0]["squeeze"] = 0.
    b = plan["baseline"]
    view = x["view"]
    after = dict(loaded=1, reserve=b["counts"]["reserve"]-1, capacity=1)
    view["snapshot"]["counts"] = after
    command = dict(context=copy.deepcopy(view["binding"]["context"]), id=901, sourceSequence=b["ammoSequence"],
        requestedNs=now-40_000_000, admissionDeadlineNs=now+30_000_000, deadlineNs=now+30_000_000,
        operation=4, before=b["counts"], after=after, original=None, mutationRequired=True)
    intent = dict(context=command["context"], id=902, observedNs=now-45_000_000, deadlineNs=now+30_000_000,
                  operation=4, original=None, rounds=1)
    binding = copy.deepcopy(view["binding"])
    binding.update(observedNs=now-50_000_000, deadlineNs=now+30_000_000)
    request = dict(binding=binding, intent=intent, discard=False, preparedCommand=command,
        sourceProof=dict(constructedNs=now-40_000_000, inputSequence=119, snapshotSequence=b["ammoSequence"],
                         snapshotObservedNs=now-50_000_000, snapshotDeadlineNs=now+30_000_000))
    receipt = dict(command=copy.deepcopy(command), authorityInvocation=77, beganNs=now-30_000_000,
        completedNs=now-20_000_000, before=copy.deepcopy(b["counts"]), after=copy.deepcopy(after), authorityVerified=True, copiesVerified=True)
    post = copy.deepcopy(view["snapshot"])
    post.update(sequence=119, observedNs=now-10_000_000, deadlineNs=now+60_000_000)
    x["lastCompletion"] = dict(lifetime=t["lifetime"], supply=copy.deepcopy(x["supply"]),
        outcome=dict(request=request, receipt=receipt, state=3, resolvedNs=now-15_000_000, nativeDispatched=True, actorRetirement=None),
        postSnapshot=post, acceptedNs=now-5_000_000)
    return s


class FixedContainerSimulationPlanTests(unittest.TestCase):
    def test_linked_physical_custody_preserves_native_ammunition_identity(self):
        s = snapshot(3)
        x = s["fixedContainer"]
        x["physicalFamily"] = dict(owner=copy.deepcopy(s["owner"]), observedNs=NOW-200_000_000,
            deadlineNs=NOW+300_000_000, current=True, physicalItem=s["equipment"]["persistence"],
            inventory=s["selected"]["inventory"], selectedSecondary=True, targetWeapon=15, targetSlot=0,
            action=25, primaryAction=25, configurationPath=s["selected"]["configurationPath"],
            asset=s["equipment"]["asset"], persistentId="sp_xm8_s")
        x["tracking"]["gun"]["token"]["item"]["id"] = s["equipment"]["persistence"]
        plan = p.plan(s, "Open", NOW, 100)
        self.assertEqual(plan["baseline"]["binding"], p.binding_key(x["view"]["binding"]))
        self.assertEqual(plan["baseline"]["nativeIdentity"]["owner"]["weapon"], s["owner"]["weapon"])
        self.assertEqual(plan["selection"]["physicalFamily"]["physicalItem"], s["equipment"]["persistence"])
        self.assertIn(x["physicalFamily"]["deadlineNs"], plan["originalLeases"])
        # Resolver renewal is allowed without changing the verified relationship.
        renewed = copy.deepcopy(s)
        renewed["fixedContainer"]["physicalFamily"]["observedNs"] += 10_000_000
        renewed["fixedContainer"]["physicalFamily"]["deadlineNs"] += 10_000_000
        self.assertEqual(p.selection(s), p.selection(renewed))
        for field, value in (("current", False), ("selectedSecondary", False), ("inventory", 1),
                             ("physicalItem", s["owner"]["weapon"]), ("configurationPath", "wrong"),
                             ("asset", "wrong"), ("deadlineNs", NOW), ("observedNs", NOW+1),
                             ("targetWeapon", s["owner"]["weapon"]), ("primaryAction", 36)):
            with self.subTest(field=field):
                bad = copy.deepcopy(s)
                bad["fixedContainer"]["physicalFamily"][field] = value
                with self.assertRaises(p.Refusal):
                    p.plan(bad, "Open", NOW, 100)
        for mutate in (lambda value: value.pop("physicalFamily"),
                       lambda value: value["physicalFamily"]["owner"].update(weapon=99)):
            bad = copy.deepcopy(s)
            mutate(bad["fixedContainer"])
            with self.assertRaises(p.Refusal):
                p.plan(bad, "Open", NOW, 100)

    def pipeline(self, kind=4):
        s = snapshot(kind)
        s["fixedContainer"]["tracking"]["step"] = 2
        pickup = p.plan(s, "Pickup", NOW, 100)
        held = held_after_pickup(s, pickup)
        insertion = p.plan(held, "Insert", NOW+200_000_000, 110, pickup_plan=pickup)
        return s, pickup, held, insertion

    def test_signed_hinge_and_linear_use_exact_measured_bases(self):
        for kind in ("Hinge", "Linear"):
            for sign in (-1, 1):
                s = snapshot(3, kind)
                motion = s["fixedContainer"]["profile"]["geometry"][0]["motion"]
                motion["travel"] *= sign
                original = copy.deepcopy(s)
                plan = p.plan(s, "Open", NOW, 100)
                self.assertEqual(s, original)
                local = copy.deepcopy(motion["motionFromContact"])
                if kind == "Hinge":
                    local = f.along(local, motion["travel"])
                else:
                    local[3][0] += motion["travel"]
                expected = f.multiply(local, motion["weaponFromMotion"])
                self.assertLess(f.pose_error(fixture.emitted_local(s, plan["schedule"][-1]), expected)[0], .00001)
                self.assertEqual([g["id"] for g in plan["receiptGates"]], ["fixed_acquired", "fixed_step_complete"])

    def physical_pipeline(self, stage="Open", motion="Hinge"):
        source = snapshot(3, motion)
        if stage == "Close":
            source["fixedContainer"]["tracking"].update(step=3, blocksFire=True)
            source["weapon_actions_blocked"] = True
            source["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1, reserve=6)
        plan = p.plan(source, stage, NOW, 100)
        acquired = physical_acquired(source)
        proof = p.check_gate(plan, acquired, "fixed_acquired", NOW+200_000_000, 110)
        return plan, acquired, proof, physical_completed(acquired, stage)

    def test_released_physical_step_observation_has_no_gesture_authority(self):
        for stage in ("Open", "Close"):
            for motion in ("Hinge", "Linear"):
                with self.subTest(stage=stage, motion=motion):
                    plan, acquired, proof, done = self.physical_pipeline(stage, motion)
                    original = copy.deepcopy((plan, acquired, proof, done))
                    result = p.check_gate(plan, done, "fixed_step_complete", NOW+500_000_000, 120, acquired=proof)
                    self.assertTrue(result["observed"] and result["completionOnly"])
                    self.assertIsNone(result["claimToken"])
                    self.assertFalse(result["nativeAcceptance"])
                    self.assertEqual((plan, acquired, proof, done), original)
                    for action in (lambda: p.admit(done, NOW+500_000_000, 120),
                                   lambda: p.reobserve(plan, done, NOW+500_000_000, 120),
                                   lambda: p.plan(done, "Pickup", NOW+500_000_000, 120),
                                   lambda: p.held(done, NOW+500_000_000, 4)):
                        with self.assertRaisesRegex(p.Refusal, "original raw"):
                            action()
                    unadmitted = copy.deepcopy(acquired)
                    unadmitted["fixedContainer"]["raw"]["admittedAtTick"] = False
                    with self.assertRaises(p.Refusal):
                        p.check_gate(plan, unadmitted, "fixed_acquired", NOW+200_000_000, 110)

    def test_physical_completion_requires_original_current_identity_and_acquisition(self):
        plan, acquired, proof, done = self.physical_pipeline()
        faults = {
            "expired_raw": lambda s: s["fixedContainer"]["raw"]["input"].update(deadlineNs=NOW+500_000_000),
            "missing_raw": lambda s: s["fixedContainer"]["raw"].update(valid=False),
            "noncurrent_raw": lambda s: s["fixedContainer"]["raw"].update(current=False),
            "owner": lambda s: s["fixedContainer"]["raw"].update(ownerMatches=False),
            "rig": lambda s: s["fixedContainer"]["raw"].update(rigMatches=False),
            "profile": lambda s: s["fixedContainer"]["raw"].update(profileMatches=False),
            "root": lambda s: s["fixedContainer"]["tracking"]["gun"]["token"].update(id=999),
            "native_identity": lambda s: s["fixedContainer"]["view"]["identity"].update(serverItem=999),
            "lifetime": lambda s: s["fixedContainer"]["tracking"].update(lifetime=999),
            "revision": lambda s: s["fixedContainer"]["tracking"].update(revision=acquired["fixedContainer"]["tracking"]["revision"]),
            "old_input": lambda s: s["input"].update(generation=110),
            "not_completed": lambda s: s["fixedContainer"]["tracking"].update(step=0),
            "competing_claim": lambda s: s["fixedContainer"].update(leftClaim=acquired["fixedContainer"]["leftClaim"]),
            "carried": lambda s: s["fixedContainer"]["tracking"].update(carried=True),
            "target": lambda s: s["fixedContainer"]["tracking"].update(target=f.identity()),
            "pending": lambda s: s["fixedContainer"]["tracking"].update(pending=True),
            "ammo": lambda s: s["fixedContainer"]["view"]["snapshot"]["counts"].update(reserve=6),
            "incoherent": lambda s: s["fixedContainer"]["view"]["snapshot"].update(coherentIdleVerified=False),
            "mapping": lambda s: s["input"].update(trackingBasisGeneration=999),
        }
        for name, fault in faults.items():
            with self.subTest(fault=name):
                bad = copy.deepcopy(done)
                fault(bad)
                with self.assertRaises(p.Refusal):
                    p.check_gate(plan, bad, "fixed_step_complete", NOW+500_000_000, 120, acquired=proof)
        wrong = copy.deepcopy(proof)
        wrong["completionPrerequisite"]["planSourceNonce"] = 999
        for prior in (None, {}, wrong):
            with self.subTest(prior=prior), self.assertRaises(p.Refusal):
                p.check_gate(plan, done, "fixed_step_complete", NOW+500_000_000, 120, acquired=prior)

    def test_actual_body_pickup_original_raw_claim_and_continuous_insert(self):
        s, pickup, held, insertion = self.pipeline()
        end = pickup["schedule"][-1]
        absolute = f.from_pose(dict(position=end["positionMeters"], orientation=f.ypr_quaternion(end["yawPitchRollDegrees"])))
        body = f.multiply(absolute, f.inverse(f.supply_body_frame(s["input"])))
        self.assertLess(math.dist(body[3][:3], s["fixedContainer"]["pouch"]["centerMeters"]), .00001)
        self.assertEqual(p.check_gate(pickup, held, "fixed_held", NOW+200_000_000, 110)["gate"], "fixed_held")
        self.assertTrue(all(row["grip"] == 1 for row in insertion["schedule"][:-1]))
        self.assertEqual(insertion["schedule"][-1]["grip"], 0)
        self.assertEqual(insertion["receiptGates"][0]["minimumObservationSpanNs"], 60_000_000)
        self.assertEqual(insertion["receiptGates"][-1]["id"], "fixed_inserted")

    def test_pickup_continuation_preserves_float32_pose_precision_and_exact_identity(self):
        source = snapshot()
        source["fixedContainer"]["tracking"]["step"] = 2
        precision.observed_right_pose(source)
        pickup = p.plan(source, "Pickup", NOW, 100)
        held = held_after_pickup(source, pickup)
        precision.observed_right_pose(held, changed=True)
        original = copy.deepcopy((pickup, held))
        self.assertTrue(p.check_gate(pickup, held, "fixed_held", NOW+200_000_000, 110)["observed"])
        insertion = p.plan(held, "Insert", NOW+200_000_000, 110, pickup_plan=pickup)
        self.assertEqual(insertion["stage"], "Insert")
        self.assertEqual((pickup, held), original)
        faults = [lambda s: s["input"]["hands"][1]["grip"]["position"].__setitem__(0, .200000004),
                  lambda s: s["input"]["hands"][1]["aim"]["orientation"].__setitem__(1,
                      precision.float32_steps(precision.OBSERVED_RIGHT_POSE["orientation"][1], 2)),
                  lambda s: s["input"]["hands"][1].update(squeeze=0),
                  lambda s: s["input"].update(trackingBasisGeneration=999),
                  lambda s: s["owner"].update(weapon=999)]
        for index, fault in enumerate(faults):
            with self.subTest(fault=index):
                bad = copy.deepcopy(held)
                fault(bad)
                with self.assertRaises(p.Refusal):
                    p.check_gate(pickup, bad, "fixed_held", NOW+200_000_000, 110)

    def test_common_world_motion_preserves_commands_pickup_and_insertion_receipts(self):
        s, pickup, held, _ = self.pipeline()
        original = copy.deepcopy(held)
        moved = move_world(held, fixture.matrix([3.2, -1.1, 2.4], [37, -23, 19]))
        self.assertEqual(held, original)
        self.assertEqual(moved["input"], held["input"])
        self.assertEqual(moved["fixedContainer"]["raw"]["input"], held["fixedContainer"]["raw"]["input"])
        self.assertNotEqual(s["mapping"]["weaponWorldMeters"], moved["mapping"]["weaponWorldMeters"])
        self.assertNotEqual(s["mapping"]["trackingBodyWorldMeters"], moved["mapping"]["trackingBodyWorldMeters"])
        # The already emitted controller schedule still reaches the same local
        # geometry after a substantial common translation and rotation.
        for row in pickup["schedule"]:
            position, angle = f.pose_error(fixture.emitted_local(s, row), fixture.emitted_local(moved, row))
            self.assertLess(position, 1e-7)
            self.assertLess(angle, 1e-6)
        driver = object.__new__(d.FixedContainerDriver)
        driver.stable_snapshot(s, moved)
        self.assertTrue(p.check_gate(pickup, moved, "fixed_held", NOW+200_000_000, 110)["observed"])
        insertion = p.plan(moved, "Insert", NOW+200_000_000, 110, pickup_plan=pickup)
        done = move_world(completed(moved, insertion), fixture.matrix([-1.7, .8, 2.1], [-29, 17, -11]))
        driver.stable_snapshot(moved, done)
        self.assertTrue(p.check_gate(insertion, done, "fixed_inserted", NOW+500_000_000, 120)["observed"])

    def test_relative_weapon_or_body_drift_uses_shared_transport_limits(self):
        s, pickup, held, _ = self.pipeline()
        driver = object.__new__(d.FixedContainerDriver)
        for field in ("weaponWorldMeters", "trackingBodyWorldMeters"):
            for distance, radians, accepted in ((.0019, 0, True), (.0021, 0, False),
                                               (0, .0049, True), (0, .0051, False)):
                with self.subTest(field=field, distance=distance, radians=radians):
                    changed = copy.deepcopy(held)
                    # Rotate in place so the angular boundary is exercised
                    # independently of the translational boundary.
                    matrix = changed["mapping"][field]
                    turned = f.multiply(matrix, fixture.matrix([0, 0, 0], [math.degrees(radians), 0, 0]))
                    turned[3][:3] = [matrix[3][0]+distance, *matrix[3][1:3]]
                    changed["mapping"][field] = turned
                    set_local(changed, changed["fixedContainer"]["raw"]["weaponFromWrist"])
                    changed["fixedContainer"]["raw"]["weaponWorldMeters"] = copy.deepcopy(changed["mapping"]["weaponWorldMeters"])
                    if accepted:
                        driver.stable_snapshot(s, changed)
                        self.assertTrue(p.check_gate(pickup, changed, "fixed_held", NOW+200_000_000, 110)["observed"])
                    else:
                        with self.assertRaises(d.transport.REFUSAL):
                            driver.stable_snapshot(s, changed)
                        with self.assertRaisesRegex(p.Refusal, "relative weapon/body basis changed"):
                            p.check_gate(pickup, changed, "fixed_held", NOW+200_000_000, 110)

    def test_common_world_motion_keeps_generations_controls_and_original_leases_pinned(self):
        s, pickup, held, _ = self.pipeline()
        moved = move_world(held, fixture.matrix([2, -1, 3], [31, -17, 13]))
        driver = object.__new__(d.FixedContainerDriver)
        for mutation in ("trackingBasisGeneration", "spaceGeneration", "physical", "worldUnitsPerMeter",
                         "floorRelative", "referenceHead", "head", "right", "calibration"):
            bad = copy.deepcopy(moved)
            if mutation == "physical": bad["physical_equipment_generation"] += 1
            elif mutation == "floorRelative": bad["input"][mutation] = not bad["input"][mutation]
            elif mutation in ("referenceHead", "head"): bad["input"][mutation]["position"][0] += .001
            elif mutation == "right": bad["input"]["hands"][1]["squeeze"] = .9
            elif mutation == "calibration": bad["mapping"]["anatomicalWristToGrip"][3][0] += .001
            else: bad["input"][mutation] += 1
            with self.subTest(mutation=mutation):
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    driver.stable_snapshot(s, bad)
                with self.assertRaises(p.Refusal):
                    p.check_gate(pickup, bad, "fixed_held", NOW+200_000_000, 110)
        bad = copy.deepcopy(moved)
        bad["fixedContainer"]["raw"]["input"]["deadlineNs"] = NOW+200_000_000
        with self.assertRaises(p.Refusal):
            p.check_gate(pickup, bad, "fixed_held", NOW+200_000_000, 110)

    def test_insertion_requires_new_actual_pickup_and_same_supply_claim(self):
        s, pickup, held, _ = self.pipeline()
        with self.assertRaises(p.Refusal):
            p.plan(s, "Insert", NOW, 100)
        for mutation in ("missing", "owner", "deadline", "raw_sequence", "item", "lifetime", "competing"):
            bad = copy.deepcopy(held)
            x = bad["fixedContainer"]
            if mutation == "missing": x["tracking"]["hand"] = None
            elif mutation == "owner": x["tracking"]["hand"]["token"]["owner"]["space"] += 1
            elif mutation == "deadline": x["tracking"]["hand"]["deadlineNs"] = NOW
            elif mutation == "raw_sequence": x["raw"]["input"]["sequence"] -= 1
            elif mutation == "item": x["supply"]["resource"]["generation"] += 1
            elif mutation == "lifetime": x["tracking"]["lifetime"] += 1
            else: x["leftClaim"] = None
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.plan(bad, "Insert", NOW+200_000_000, 110, pickup_plan=pickup)

    def test_receipt_requires_exact_authority_copies_post_snapshot_and_counts(self):
        for kind in (3, 4):
            _, _, held, insertion = self.pipeline(kind)
            done = completed(held, insertion)
            self.assertTrue(p.check_gate(insertion, done, "fixed_inserted", NOW+500_000_000, 120)["observed"])
            for mutation in ("missing", "copies", "authority", "command", "owner", "supply", "stale_request", "post_time", "post_count", "dispatch"):
                bad = copy.deepcopy(done)
                x = bad["fixedContainer"]
                event = x["lastCompletion"]
                if mutation == "missing": x["lastCompletion"] = None
                elif mutation == "copies": event["outcome"]["receipt"]["copiesVerified"] = False
                elif mutation == "authority": event["outcome"]["receipt"]["authorityInvocation"] = 0
                elif mutation == "command": event["outcome"]["receipt"]["command"]["id"] += 1
                elif mutation == "owner": event["outcome"]["request"]["binding"]["owner"]["space"] += 1
                elif mutation == "supply": event["supply"]["resource"]["id"] += 1
                elif mutation == "stale_request": event["outcome"]["request"]["intent"]["id"] = 0
                elif mutation == "post_time": event["postSnapshot"]["observedNs"] = NOW
                elif mutation == "post_count": event["postSnapshot"]["counts"]["loaded"] = 0
                else: event["outcome"]["nativeDispatched"] = False
                with self.subTest(kind=kind, mutation=mutation), self.assertRaises(p.Refusal):
                    p.check_gate(insertion, bad, "fixed_inserted", NOW+500_000_000, 120)

    def test_counts_alone_never_finish_insertion(self):
        _, _, held, insertion = self.pipeline()
        done = completed(held, insertion)
        done["fixedContainer"]["lastCompletion"] = None
        with self.assertRaises(p.Refusal):
            p.check_gate(insertion, done, "fixed_inserted", NOW+500_000_000, 120)

    def test_insert_receipt_after_claim_release_requires_same_plan_alignment(self):
        for kind in (3, 4):
            with self.subTest(kind=kind):
                _, _, held, insertion = self.pipeline(kind)
                aligned = aligned_for_insert(held)
                proof = p.check_gate(insertion, aligned, "fixed_aligned", NOW+300_000_000, 115)
                done = completed(aligned, insertion)
                done["fixedContainer"]["raw"]["admittedAtTick"] = False
                original = copy.deepcopy((insertion, proof, done))
                result = p.check_gate(insertion, done, "fixed_inserted", NOW+500_000_000, 120, acquired=proof)
                self.assertTrue(result["completionOnly"])
                self.assertIsNone(result["claimToken"])
                self.assertEqual((insertion, proof, done), original)
                for fault in ("no_alignment", "receipt", "stale", "identity", "old_request", "not_consumed"):
                    bad = copy.deepcopy(done)
                    prior = proof
                    if fault == "no_alignment": prior = None
                    elif fault == "receipt": bad["fixedContainer"]["lastCompletion"]["outcome"]["receipt"]["copiesVerified"] = False
                    elif fault == "stale": bad["fixedContainer"]["raw"]["input"]["deadlineNs"] = NOW+500_000_000
                    elif fault == "identity": bad["fixedContainer"]["view"]["identity"]["serverItem"] += 1
                    elif fault == "old_request": bad["fixedContainer"]["lastCompletion"]["outcome"]["request"]["sourceProof"]["inputSequence"] = 115
                    else: bad["fixedContainer"]["tracking"]["carried"] = True
                    with self.subTest(fault=fault), self.assertRaises(p.Refusal):
                        p.check_gate(insertion, bad, "fixed_inserted", NOW+500_000_000, 120, acquired=prior)
                with self.assertRaises(p.Refusal):
                    p.admit(done, NOW+500_000_000, 120)

    def test_native_transaction_is_observable_but_blocks_new_motion(self):
        _, _, held, insertion = self.pipeline()
        pending = completed(held, insertion)
        x = pending["fixedContainer"]
        request = x["lastCompletion"]["outcome"]["request"]
        x.update(pendingRequest=request, lastCompletion=None)
        x["tracking"].update(step=2, pending=True, blocksFire=True)
        pending["weapon_actions_blocked"] = True
        x["view"].update(phase=3, command=request["preparedCommand"])
        p.admit(pending, NOW+500_000_000, 120)
        with self.assertRaises(p.Refusal):
            p.plan(pending, "Pickup", NOW+500_000_000, 120)
        x["pendingRequest"]["preparedCommand"]["operation"] = 2
        with self.assertRaises(p.Refusal):
            p.admit(pending, NOW+500_000_000, 120)

    def test_source_selection_policy_and_unrelated_controls_fail_closed(self):
        for mutation in ("missing", "expired", "configuration", "mesh", "raw", "full", "trigger", "kind", "gap", "competing"):
            s = snapshot()
            x = s["fixedContainer"]
            if mutation == "missing": del s["fixedContainer"]
            elif mutation == "expired": x["view"]["binding"]["deadlineNs"] = NOW
            elif mutation == "configuration": x["profile"]["configurationPath"] = "other"
            elif mutation == "mesh": x["profile"]["meshPath"] = "other"
            elif mutation == "raw": x["raw"]["current"] = False
            elif mutation == "full": x["view"]["snapshot"]["counts"]["loaded"] = 1
            elif mutation == "trigger": s["input"]["hands"][1]["trigger"] = 1
            elif mutation == "kind": x["profile"]["kind"] = 1
            elif mutation == "gap": x["profile"]["geometry"][2]["motion"]["policy"]["maxInputGapNs"] = 40_000_000
            else: x["leftClaim"] = copy.deepcopy(x["tracking"]["gun"])
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.plan(s, "Pickup", NOW, 100)


class FixedContainerReadinessTests(unittest.TestCase):
    def test_reload_uses_admitted_readiness_before_requesting_another_observation(self):
        # Actual37 had a ready snapshot002, then the redundant pre-Open request
        # returned disabled tracking or missing raw contact. Neither run sent a
        # command. Exercise that sequence through real planning/publication gates.
        class FirstPublication(Exception):
            pass

        for missing in ("tracking", "raw"):
            with self.subTest(missing=missing):
                ready = snapshot(3)
                invalid = refresh(ready, NOW+100_000_000, 101)
                x = invalid["fixedContainer"]
                if missing == "tracking":
                    x["tracking"].update(enabled=False, current=False)
                else:
                    x["raw"].update(valid=False, current=False, admittedAtTick=False,
                                    ownerMatches=False, rigMatches=False, profileMatches=False)
                v = object.__new__(d.FixedContainerDriver)
                v.args = SimpleNamespace(step_ms=40)
                v.active_plan, v.reference, v.latest_snapshot = None, None, None
                v.commands, v.plans, v.gate_receipts = 0, [], []
                v.demand_finished, v.held_left, v.pending_command = True, False, False
                v.deadline, v.stage_deadline = NOW+9*d.NS, NOW+9*d.NS
                v.artifacts = memory.MemoryArtifacts()
                requests, publications = [], []

                def fresh():
                    s = ready if not requests else invalid
                    requests.append(s)
                    d.planner.admit(s, NOW if s is ready else NOW+100_000_000, int(s["nonce"]))
                    v.latest_snapshot = s
                    return s, d.transport.json_bytes(s), NOW

                def send(lines, expected=None, source_deadline=None):
                    v.validate_publication(lines, expected, False)
                    self.assertEqual(source_deadline, v.snapshot_source_deadline(ready))
                    publications.append(lines)
                    raise FirstPublication()

                with patch.object(d.time, "perf_counter_ns", return_value=NOW), \
                     patch.object(v, "fresh_available", side_effect=fresh), \
                     patch.object(v, "pause_until"), patch.object(v, "send", side_effect=send):
                    with self.assertRaises(FirstPublication):
                        v.reload()
                self.assertEqual(len(requests), 1)
                self.assertEqual(len(publications), 1)
                self.assertEqual(v.plans[0]["stage"], "Open")
                self.assertIs(v.reference, ready)
                # The later unavailable observation remains a refusal even
                # after a plan exists; no invalid held source becomes current.
                with self.assertRaises(d.planner.Refusal):
                    v.admit_snapshot(invalid, NOW+100_000_000, 101)


class FixedContainerDeadlineTests(unittest.TestCase):
    """In-memory delivery delays retain original leases and command ownership."""
    MS = 1_000_000

    def response(self, now, nonce=1, kind=3, family_remaining=300_000_000):
        s = refresh(snapshot(kind), now, now//self.MS)
        s.update(nonce=nonce, observed_ns=now)
        x = s["fixedContainer"]
        x["physicalFamily"] = dict(owner=copy.deepcopy(s["owner"]),
            observedNs=now+family_remaining-500_000_000, deadlineNs=now+family_remaining,
            current=True, physicalItem=s["equipment"]["persistence"],
            inventory=s["selected"]["inventory"], selectedSecondary=True, targetWeapon=15, targetSlot=0,
            action=25, primaryAction=25, configurationPath=s["selected"]["configurationPath"],
            asset=s["equipment"]["asset"], persistentId="sp_xm8_s")
        x["tracking"]["gun"]["token"]["item"]["id"] = s["equipment"]["persistence"]
        return None, s

    def harness(self, kind=3):
        self.enterContext(patch.object(memory, "fixture", lambda now, nonce=1: self.response(now, nonce, kind)))
        h = memory.Harness(self)
        v = h.driver
        v.__class__ = d.FixedContainerDriver
        v.active_plan, v.demand_reference, v.demand_started = None, None, None
        v.demand_finished, v.gate_receipts, v.release_authorized = True, [], False
        v.acquired_claim = None
        _, delayed = self.response(h.now-20*self.MS, v.pending_nonce, kind, 10*self.MS)
        h.publish(delayed)
        return h, delayed

    def test_family_expired_in_delivery_holds_then_admits_real_replacement(self):
        for kind in (3, 4):
            with self.subTest(kind=kind):
                h, delayed = self.harness(kind)
                v = h.driver
                original, raw = copy.deepcopy(v.reference), h.files[v.snapshot_path]
                stage_deadline, last_write, started = v.stage_deadline, v.last_write, h.now
                self.assertLess(h.now, d.shared.source_deadline(delayed))
                self.assertGreaterEqual(h.now, v.snapshot_source_deadline(delayed))
                def held():
                    self.assertEqual(h.command_writes, [])
                    self.assertEqual(v.last_write, last_write)
                    self.assertTrue(v.held_left)
                h.on_sleep = held
                v.current_source()
                self.assertGreaterEqual(h.now, started+40*self.MS)
                self.assertEqual(v.artifacts.saved["snapshot-001.json"], raw)
                self.assertEqual(v.reference, original)
                self.assertEqual(v.stage_deadline, stage_deadline)
                self.assertGreater(v.latest_snapshot["nonce"], delayed["nonce"])
                self.assertEqual(v.latest_snapshot["fixedContainer"]["physicalFamily"]["deadlineNs"]
                                 - v.latest_snapshot["fixedContainer"]["physicalFamily"]["observedNs"], 500*self.MS)
                self.assertIsNone(v.snapshot_refresh_started)
                self.assertEqual([e["type"] for e in v.artifacts.events],
                                 ["snapshot_refresh_hold", "snapshot_refresh_resumed"])
                self.assertEqual(v.artifacts.events[0]["sourceDeadlineNs"],
                                 delayed["fixedContainer"]["physicalFamily"]["deadlineNs"])
                self.assertEqual(h.command_writes, [])

    def test_accepted_family_expiry_holds_until_new_admitted_source(self):
        h, _ = self.harness()
        v = h.driver
        _, current = self.response(h.now, 2, family_remaining=10*self.MS)
        v.pending_nonce, v.last_request = None, h.now
        v.accept_snapshot(current, d.transport.json_bytes(current), h.now)
        h.now += 11*self.MS
        self.assertLess(h.now, d.shared.source_deadline(current))
        v.current_source()
        self.assertGreater(v.latest_snapshot["nonce"], current["nonce"])
        self.assertLess(h.now, v.snapshot_source_deadline(v.latest_snapshot))
        self.assertEqual(current["fixedContainer"]["physicalFamily"]["deadlineNs"],
                         current["observed_ns"]+10*self.MS)
        self.assertEqual(h.command_writes, [])

    def test_expired_family_with_invalid_identity_is_not_refreshed(self):
        for mutation in ("owner", "item", "current", "unbounded"):
            with self.subTest(mutation=mutation):
                h, delayed = self.harness()
                v = h.driver
                proof = delayed["fixedContainer"]["physicalFamily"]
                if mutation == "owner": proof["owner"]["weapon"] += 1
                elif mutation == "item": proof["physicalItem"] += 1
                elif mutation == "current": proof["current"] = False
                else: proof["observedNs"] -= 1
                # No prior selection makes historical native admission, rather
                # than the stable-selection check, reject the invalid proof.
                v.reference = None
                h.publish(delayed)
                with self.assertRaisesRegex(d.planner.Refusal, "physical family"):
                    v.receive_snapshot()
                self.assertIsNone(v.snapshot_refresh_started)
                self.assertEqual(v.snapshot_index, 1)
                self.assertIn("snapshot-001.json", v.artifacts.saved)
                self.assertEqual(h.command_writes, [])

    def test_family_refresh_retains_request_count_and_elapsed_bounds(self):
        for bound in ("requests", "response", "refresh", "stage", "driver"):
            with self.subTest(bound=bound):
                h, _ = self.harness()
                v = h.driver
                self.assertIsNone(v.receive_snapshot())
                if bound == "requests":
                    h.response_stale = True
                    message = "refresh request budget exhausted"
                elif bound == "response":
                    h.response_due = None
                    message = "within request budget"
                else:
                    h.response_due = None
                    if bound == "refresh":
                        h.now += d.transport.SNAPSHOT_REFRESH_BUDGET
                        message = "refresh budget exhausted"
                    elif bound == "stage":
                        v.stage_deadline = h.now
                        message = "maximumGestureNs"
                    else:
                        v.deadline = h.now
                        message = "driver deadline"
                with self.assertRaisesRegex(d.transport.REFUSAL, message):
                    v.pause_until(h.now)
                if bound == "requests":
                    self.assertEqual(v.snapshot_refresh_requests, 2)
                    self.assertIn("snapshot-003.json", v.artifacts.saved)
                self.assertEqual(h.command_writes, [])

    def test_publication_refuses_family_that_expired_after_current_source(self):
        h, delayed = self.harness()
        v = h.driver
        v.latest_snapshot, v.active_plan = delayed, {"schedule": []}
        with self.assertRaisesRegex(d.transport.REFUSAL, "source expired before publication"):
            v.validate_publication(["grip l 1"], None, False)
        self.assertEqual(h.command_writes, [])

    def test_source_deadline_retains_fixed_plan_leases_and_pending_capture(self):
        v = object.__new__(d.FixedContainerDriver)
        for kind, stage in ((3, "Open"), (4, "Pickup")):
            _, s = self.response(NOW, 100, kind, 10*self.MS)
            plan = p.plan(s, stage, NOW, 100)
            self.assertEqual(v.snapshot_source_deadline(s), plan["sourceValidUntilNs"])
        s = snapshot()
        x = s["fixedContainer"]
        for lease in (x["input"], x["raw"]["input"], x["view"]["snapshot"],
                      x["view"]["binding"], x["tracking"]["gun"]):
            original = lease["deadlineNs"]
            lease["deadlineNs"] = NOW+5*self.MS
            self.assertEqual(v.snapshot_source_deadline(s), NOW+5*self.MS)
            lease["deadlineNs"] = original
        x["tracking"]["hand"] = {"deadlineNs": NOW+4*self.MS}
        self.assertEqual(v.snapshot_source_deadline(s), NOW+4*self.MS)
        s["fixedContainer"] = dict(available=False, reason="capture_pending_or_not_requested")
        self.assertEqual(v.snapshot_source_deadline(s), d.shared.source_deadline(s))


class SelectedAmmoDriverTests(unittest.TestCase):
    """Fresh selected identity can precede its idle three-path ammo publication."""
    MS = 1_000_000

    class Driver(d.SelectedAmmoDriver):
        def assert_selected_family(self, s):
            family = s['sight']['family']
            d.require(family['selectedSecondary'] is True
                      and family['physicalItem'] == s['equipment']['persistence']
                      and family['inventory'] == s['selected']['inventory']
                      and family['secondaryConfiguration'] == s['selected']['configurationPath']
                      and family['secondaryAsset'] == s['equipment']['asset']
                      and s['selected']['selectedSlot'] == 2, 'synthetic selected family mismatch')

    def response(self, now, nonce=1):
        _, s = FixedContainerDeadlineTests().response(now, nonce)
        s['sight'] = fixture.snapshot(secondary=True, primary_slot=0)['sight']
        s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 1
        # The prefire observation does not require active empty-loader tracking.
        s['fixedContainer']['tracking'].update(enabled=False, current=False)
        s['fixedContainer']['raw'].update(valid=False, current=False, admittedAtTick=False)
        s['fixedContainer']['physicalFamily'] = None
        return None, s

    @staticmethod
    def missing(s):
        s['fixedContainer'].update(view=None, viewFresh=False)
        return s

    def harness(self, missing=True, right_squeeze=1.):
        future = {'missing': False, 'mutate': None}

        def response(now, nonce=1):
            _, s = self.response(now, nonce)
            s['input']['hands'][1]['squeeze'] = right_squeeze
            if future['missing']:
                self.missing(s)
            if future['mutate'] is not None:
                future['mutate'](s)
            return None, s

        self.enterContext(patch.object(memory, 'fixture', response))
        h = memory.Harness(self)
        v = h.driver
        v.__class__ = self.Driver
        v.active_plan, v.demand_reference, v.demand_started = None, None, None
        v.demand_finished, v.reference, v.latest_snapshot = False, None, None
        v.held_left, v.monitor, v.motion_started = False, False, None
        v.last_request = h.now
        h.state['controls']['gripL'] = 0.
        _, initial = self.response(h.now)
        initial['input']['hands'][1]['squeeze'] = right_squeeze
        if missing:
            self.missing(initial)
        h.publish(initial)
        return h, future, initial

    def test_first_available_missing_view_waits_then_uses_new_exact_ammo_without_input(self):
        h, _, initial = self.harness()
        v, raw = h.driver, d.transport.json_bytes(initial)
        deadlines = v.deadline, v.hard_deadline, v.stage_deadline
        start = h.now
        result, _, _ = v.fresh_available()
        self.assertEqual(v.demand_started, start)
        self.assertTrue(v.demand_finished)
        self.assertIs(v.latest_snapshot, result)
        self.assertEqual(result['fixedContainer']['view']['snapshot']['counts'],
                         dict(loaded=1, reserve=7, capacity=1))
        self.assertEqual(v.artifacts.saved['snapshot-001.json'], raw)
        self.assertEqual(d.transport.json_bytes(v.demand_reference), raw)
        self.assertGreater(result['observed_ns'], initial['observed_ns'])
        self.assertEqual((v.deadline, v.hard_deadline, v.stage_deadline), deadlines)
        self.assertEqual([e['type'] for e in v.artifacts.events], ['pump_capture_pending'])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.commands, 0)

    def test_absent_view_never_renews_original_one_second_demand(self):
        h, future, _ = self.harness()
        future['missing'] = True
        v, start = h.driver, h.now
        deadline = v.stage_deadline
        with self.assertRaisesRegex(d.transport.REFUSAL, 'maximumGestureNs|capture demand exceeded one second'):
            v.fresh_available()
        self.assertEqual(v.demand_started, start)
        self.assertEqual(v.stage_deadline, deadline)
        self.assertLessEqual(h.now-start, d.NS+self.MS)
        self.assertIsNone(v.latest_snapshot)
        self.assertFalse(v.demand_finished)
        self.assertGreater(len(v.artifacts.saved), 1)
        self.assertEqual(h.command_writes, [])

    def test_present_invalid_ammo_and_missing_view_contradictions_are_not_pending(self):
        mutations = (
            lambda s: s['fixedContainer'].update(viewFresh=False),
            lambda s: s['fixedContainer']['view']['binding']['owner'].update(weapon=999),
            lambda s: s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns']),
            lambda s: s['fixedContainer'].update(view=None, viewFresh=True),
            lambda s: (self.missing(s), s['fixedContainer']['tracking'].update(pending=True)),
            lambda s: (self.missing(s), s['fixedContainer'].update(pendingRequest={})),
            lambda s: (self.missing(s), s['fixedContainer']['owner'].update(weapon=999)),
            lambda s: (self.missing(s), s['fixedContainer']['input'].update(sequence=999)),
            lambda s: (self.missing(s), s['input']['hands'][0].update(squeeze=1.)),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                h, _, initial = self.harness(missing=False)
                mutation(initial)
                h.publish(initial)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertFalse(h.driver.artifacts.events)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_pending_wait_refuses_changed_selection_family_or_owner(self):
        mutations = (
            lambda s: s['sight']['family'].update(persistentId='different-family'),
            lambda s: s['equipment'].update(persistence=999),
            lambda s: s['selected'].update(selectedSlot=0),
            lambda s: s['rig'].update(pose=999),
            lambda s: s['owner'].update(weapon=999),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                h, future, _ = self.harness()
                future['mutate'] = mutation
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertEqual(len(h.driver.artifacts.events), 1)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_request_timeout_and_shorter_outer_deadline_remain_binding(self):
        for limit in ('response', 'outer'):
            with self.subTest(limit=limit):
                h, future, _ = self.harness()
                v, start = h.driver, h.now
                if limit == 'response':
                    h.response_after = None
                    message = 'within request budget'
                else:
                    future['missing'] = True
                    v.deadline = h.now+150*self.MS
                    message = 'driver deadline'
                with self.assertRaisesRegex(d.transport.REFUSAL, message):
                    v.fresh_available()
                self.assertEqual(v.demand_started, start)
                self.assertLess(h.now-start, d.NS)
                self.assertEqual(h.command_writes, [])

    def test_after_first_ammo_binding_change_or_loss_is_not_a_new_capture_demand(self):
        for missing in (False, True):
            with self.subTest(missing=missing):
                h, future, _ = self.harness()
                v = h.driver
                ready, _, _ = v.fresh_available()
                v.reference = ready
                future['missing'] = missing
                if not missing:
                    future['mutate'] = lambda s: s['fixedContainer']['view']['binding'].update(switching=999)
                with self.assertRaisesRegex(d.transport.REFUSAL, 'owner/binding changed'):
                    v.fresh_available()
                self.assertTrue(v.demand_finished)
                self.assertEqual(len(v.artifacts.events), 1)
                self.assertEqual(h.command_writes, [])

    def shot_harness(self):
        # Recorded319 starts with neutral controllers and retains its GunHold.
        h, future, _ = self.harness(right_squeeze=0.)
        v = h.driver
        # The shared single-command harness publishes fixed frame numbers.
        # Model successive simulator commits for this press/release pair, so
        # the real transport can require a later completed frame for each ACK.
        advance = h.sleep
        h.state['controls']['trigR'] = 0.

        def advance_commands(seconds):
            previous_sequence = h.state['cmdSeq']
            previous_frame, previous_end = h.state['frame'], h.state['endFrames']
            advance(seconds)
            if h.state['cmdSeq'] != previous_sequence:
                batch = h.files[v.command_path].decode('ascii').splitlines()
                acknowledged_frame = previous_frame+1
                h.state.update(frame=acknowledged_frame+2, endFrames=previous_end+3)
                for command in batch:
                    if command.startswith('trigger r '):
                        h.state['controls']['trigR'] = float(command.split()[-1])
                h.files[v.ack_path] = (
                    f"seq={h.state['cmdSeq']} frame={acknowledged_frame} error=-\n"
                    f"last={' | '.join(batch)}\n").encode('ascii')

        self.enterContext(patch.object(d.transport.time, 'sleep', advance_commands))
        ready, _, _ = v.fresh_available()
        v.reference = ready
        v.send(['trigger r 1'])
        v.pause_until(h.now+180*self.MS)
        v.send(['trigger r 0'])
        v.pause_until(h.now+500*self.MS)
        # This post-shot observation may start after the initial diagnostic
        # capture expired; it must not reset that original demand clock.
        h.now = max(h.now, v.demand_started+d.NS+self.MS)
        self.assertEqual(v.commands, 4)
        self.assertFalse(v.held_trigger)
        return h, future, ready

    @staticmethod
    def capture_missing(s):
        s['fixedContainer'] = dict(schema=p.OBSERVATION_SCHEMA, available=False,
                                   reason='capture_pending_or_not_requested')

    @staticmethod
    def native_view_missing(s):
        # Actual79/08: Tick returned before tracking when the ammo view expired.
        SelectedAmmoDriverTests.missing(s)
        s['fixedContainer']['tracking'] = dict(
            enabled=False, current=False, blocksFire=False, carried=False,
            pending=False, step=0, targetStep=0, lifetime=0, revision=0,
            gun=dict(token=dict(id=0, owner=dict(actor=0, actorGeneration=0,
                equipGeneration=0, space=0), hand=0, kind=0,
                item=dict(id=0, generation=0), contact=dict(id=0, generation=0),
                prerequisiteClaim=0), deadlineNs=0, inputSequence=0),
            hand=None, target=None, carriedSupply=None, pouchContact=False,
            grip=False, gripPressed=False)

    def test_owned_post_shot_capture_absence_waits_for_same_native_empty_ammo_without_input(self):
        for missing in (self.capture_missing, self.missing, self.native_view_missing):
            with self.subTest(missing=missing):
                h, future, ready = self.shot_harness()
                v = h.driver
                original = copy.deepcopy(ready)
                clocks = v.deadline, v.hard_deadline, v.stage_deadline, v.demand_started
                requests = 0

                def transition(s):
                    nonlocal requests
                    requests += 1
                    if requests == 1:
                        missing(s)
                    else:
                        s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0

                future['mutate'] = transition
                before_commands = list(h.command_writes)
                result, _, _ = v.fresh_available()
                self.assertEqual(result['fixedContainer']['view']['snapshot']['counts'],
                                 dict(loaded=0, reserve=7, capacity=1))
                self.assertEqual(v.reference, original)
                self.assertEqual((v.deadline, v.hard_deadline, v.stage_deadline, v.demand_started), clocks)
                self.assertEqual(h.command_writes, before_commands)
                self.assertEqual(requests, 2)
                self.assertEqual(len([e for e in v.artifacts.events
                    if e['type'] == 'selected_ammo_post_shot_unpublished']), 1)
                self.assertFalse(v.post_shot_observing)
                with self.assertRaisesRegex(d.transport.REFUSAL, 'already attempted'):
                    v.fresh_available()

    def test_native_missing_view_never_accepts_partial_tracking_or_an_empty_published_claim(self):
        for fault in ('pending', 'root', 'left_claim', 'released', 'published'):
            with self.subTest(fault=fault):
                h, future, ready = self.shot_harness()
                def response(s):
                    original_view = s['fixedContainer']['view']
                    self.native_view_missing(s)
                    x = s['fixedContainer']
                    if fault == 'pending':
                        x['tracking']['pending'] = True
                    elif fault == 'root':
                        x['tracking']['gun']['token']['id'] = 2
                    elif fault == 'left_claim':
                        x['leftClaim'] = {}
                    elif fault == 'released':
                        x['input']['released'] = [True, True]
                    else:
                        x.update(view=original_view, viewFresh=True)
                future['mutate'] = response
                commands = list(h.command_writes)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertIs(h.driver.reference, ready)
                self.assertEqual(h.command_writes, commands)

    def test_post_shot_capture_wait_rejects_changed_owner_config_root_and_native_identity(self):
        mutations = (
            lambda s: s['owner'].update(weapon=999),
            lambda s: s['equipment'].update(persistence=999),
            lambda s: s.update(physical_equipment_generation=999),
            lambda s: s['selected'].update(configurationPath='unrelated'),
            lambda s: s['rig'].update(pose=999),
            lambda s: s['fixedContainer']['tracking']['gun']['token'].update(id=999),
            lambda s: s['fixedContainer']['tracking']['gun'].update(deadlineNs=s['observed_ns']),
            lambda s: s['fixedContainer']['view']['binding'].update(switching=999),
            lambda s: s['fixedContainer']['view']['identity'].update(serverItem=999),
            lambda s: s['fixedContainer']['view'].update(command={}),
            lambda s: s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns']),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                h, future, ready = self.shot_harness()
                calls = 0

                def response(s):
                    nonlocal calls
                    calls += 1
                    if calls == 1:
                        self.capture_missing(s)
                    else:
                        s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                        mutation(s)

                future['mutate'] = response
                before_commands = list(h.command_writes)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertIs(h.driver.reference, ready)
                self.assertEqual(h.command_writes, before_commands)

    def test_post_shot_absence_does_not_hide_unrelated_capture_or_transaction(self):
        mutations = (
            lambda s: s['fixedContainer'].update(reason='source_mismatched_or_expired'),
            lambda s: s['fixedContainer'].update(schema='unrelated'),
            lambda s: s.update(weapon_actions_blocked=True),
            lambda s: s.update(feed={}),
            lambda s: s['input']['hands'][0].update(squeeze=1.),
            lambda s: s['input_evidence'].update(observedNs=0),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                h, future, _ = self.shot_harness()
                future['mutate'] = lambda s: (self.capture_missing(s), mutation(s))
                before_commands = list(h.command_writes)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, before_commands)
                self.assertFalse(any(e['type'] == 'selected_ammo_post_shot_unpublished'
                                     for e in h.driver.artifacts.events))

    def test_post_shot_wait_preserves_request_cap_original_deadline_and_exclusive_input(self):
        for fault in ('requests', 'deadline', 'writer', 'capture', 'pending'):
            with self.subTest(fault=fault):
                h, future, _ = self.shot_harness()
                v = h.driver
                future['mutate'] = self.capture_missing
                if fault == 'deadline':
                    v.stage_deadline = h.now+25*self.MS
                elif fault == 'writer':
                    h.files[v.command_path] = b'unrelated writer\n'
                elif fault == 'capture':
                    h.state['captureSeq'] += 1
                elif fault == 'pending':
                    v.pending_command = True
                clocks = v.deadline, v.stage_deadline, v.demand_started
                start_index = v.snapshot_index
                before_commands = list(h.command_writes)
                with self.assertRaises(d.transport.REFUSAL):
                    v.fresh_available()
                self.assertEqual((v.deadline, v.stage_deadline, v.demand_started), clocks)
                self.assertLessEqual(v.snapshot_index-start_index, 1+d.transport.MAX_SNAPSHOT_REFRESHES)
                self.assertEqual(h.command_writes, before_commands)

    @unittest.skipUnless(os.environ.get('BC2_SELECTED_AMMO_REPLAY'), 'optional retained actual319 post-shot replay')
    def test_actual319_missing_post_shot_frame_stays_observation_only_until_original_deadline(self):
        folder = Path(os.environ['BC2_SELECTED_AMMO_REPLAY'])
        pins = {'snapshot-003.json': '65ac6676239c92836b4a54e86daeb81765657c8995904a41bd6886b6d29340fc', 'snapshot-004.json': '2ae48332ecfda48b53f1d0b6be1fa541de55a87171f19d4207abdbb9f06de263', 'events.jsonl': '80fa8b5c1eb2fa2bd7eecfdf42b49c4ee20d1b69eeb71e94ea88823c97712916', 'request-004-timing.json': 'cff5cc38146d01fd8231ed26ffe447a82a5236226ca197e3f37b1a56adde1e9c'}
        blobs = {name:(folder/name).read_bytes() for name in pins}
        self.assertEqual({name:hashlib.sha256(blob).hexdigest() for name,blob in blobs.items()}, pins)
        before, absent = [json.loads(blobs[name]) for name in ('snapshot-003.json','snapshot-004.json')]
        events = [json.loads(line) for line in blobs['events.jsonl'].splitlines()]
        commits = [row for row in events if row['type'] == 'command_commit']
        requests = [row for row in events if row['type'] == 'command_request']
        self.assertEqual([row['commands'][0] for row in requests], ['trigger r 1','trigger r 0'])
        self.assertEqual(len(commits), 2)
        h, _, _ = self.harness(missing=False)
        v = h.driver
        h.now = absent['observed_ns']+self.MS
        h.state = copy.deepcopy(commits[-1]['state'])
        v.state = copy.deepcopy(h.state)
        v.initial_sim = copy.deepcopy(h.state)
        v.reference = v.latest_snapshot = before
        v.demand_finished, v.commands = True, 4
        v.demand_started = before['observed_ns']
        v.deadline, v.hard_deadline = h.now+d.NS, h.now+2*d.NS
        v.stage_deadline = h.now+100*self.MS
        original_deadline = v.stage_deadline
        v.last_write, v.last_commit = commits[-1]['writtenNs'], commits[-1]['committedNs']
        v.sequence, v.sentinel, v.capture_sequence = h.state['cmdSeq'],h.state['lastCmd'],h.state['captureSeq']
        v.pending_batch = requests[-1]['commands']
        h.files[v.command_path] = ('\n'.join(v.pending_batch)+'\n').encode()
        v.command_hash = d.transport.digest(h.files[v.command_path])
        timing = json.loads(blobs['request-004-timing.json'])
        v.last_request, v.pending_nonce, v.snapshot_index = timing['requestStartedNs'],int(absent['nonce']),4
        v.request_bytes = (str(v.pending_nonce)+'\n').encode()
        h.files[v.request_path] = v.request_bytes
        h.publish(absent)
        h.files[v.snapshot_path] = blobs['snapshot-004.json']
        h.response_after = None
        with self.assertRaisesRegex(d.transport.REFUSAL, 'maximumGestureNs|post-stage receipt observation timed out'):
            v.fresh_available()
        self.assertEqual(v.stage_deadline, original_deadline)
        self.assertIsNone(v.latest_snapshot)
        self.assertIs(v.reference, before)
        self.assertEqual(v.artifacts.saved['snapshot-004.json'], blobs['snapshot-004.json'])
        held = [e for e in v.artifacts.events if e['type'] == 'selected_ammo_post_shot_unpublished']
        self.assertEqual(len(held), 1)
        self.assertFalse(held[0]['commandAuthority'])
        self.assertEqual(h.command_writes, [])


    def test_trigger_publication_still_requires_current_loaded_one(self):
        for state in ('ready', 'empty', 'missing', 'expired'):
            with self.subTest(state=state):
                h, _, _ = self.harness()
                v = h.driver
                ready, _, _ = v.fresh_available()
                v.reference = ready
                # Native JSON serializes the 64-bit request nonce as a string.
                ready['nonce'] = str(ready['nonce'])
                if state == 'empty':
                    ready['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                elif state == 'missing':
                    self.missing(ready)
                elif state == 'expired':
                    h.now = v.snapshot_source_deadline(ready)+self.MS
                if state == 'ready':
                    v.validate_publication(['trigger r 1'], None, False)
                else:
                    with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                        v.validate_publication(['trigger r 1'], None, False)
                self.assertEqual(h.command_writes, [])


class SelectedAmmoInitialBasisTests(unittest.TestCase):
    """Passive diagnosis cannot freeze geometry or replace an active source."""
    MS = SelectedAmmoDriverTests.MS
    Driver = SelectedAmmoDriverTests.Driver
    response = SelectedAmmoDriverTests.response
    missing = staticmethod(SelectedAmmoDriverTests.missing)
    harness = SelectedAmmoDriverTests.harness
    shot_harness = SelectedAmmoDriverTests.shot_harness

    @unittest.skipUnless(os.environ.get('BC2_SELECTED_BASIS_REPLAY'), 'optional pinned actual329 replay')
    def test_actual329_packets_stay_passive_with_original_bytes_and_no_action_source(self):
        folder = Path(os.environ['BC2_SELECTED_BASIS_REPLAY'])
        pins = {'snapshot-001.json': 'c6e9ad61b56e9c106e251fe503132f8937af36b8511ac7eed749bfd249f1b821', 'snapshot-002.json': '9cb718abe9ac783a265decbbd0557169038e322ee6b34e54a1b8b983f574769b', 'request-001-timing.json': '12e52b511b0e09f9e104ac82acbf9774cbf3dcc1d6d3cfac79f63b7eb1b086ea', 'request-002-timing.json': '47f4731fff971b8ca2900856574c572a690282b0485123fea1130653ac379d1d'}
        blobs = {name: (folder / name).read_bytes() for name in pins}
        self.assertEqual({name: hashlib.sha256(raw).hexdigest() for name, raw in blobs.items()}, pins)
        snapshots = [json.loads(blobs[name]) for name in ('snapshot-001.json', 'snapshot-002.json')]
        h, _, _ = self.harness()
        v = h.driver
        v.demand_started = snapshots[0]['observed_ns']-100*self.MS
        for index, snapshot in enumerate(snapshots, 1):
            h.now = snapshot['observed_ns']
            timing = json.loads(blobs[f'request-{index:03}-timing.json'])
            v.last_request = timing['requestStartedNs']
            v.pending_nonce, v.snapshot_index = int(snapshot['nonce']), index
            v.request_bytes = (str(v.pending_nonce)+'\n').encode()
            h.files[v.request_path] = v.request_bytes
            h.publish(snapshot)
            raw = blobs[f'snapshot-{index:03}.json']
            h.files[v.snapshot_path] = raw
            received = v.receive_snapshot()
            self.assertEqual(received[1], raw)
            with self.assertRaisesRegex(d.shared.CapturePending, 'initial selected ammunition publication pending'):
                v.accept_snapshot(*received)
            self.assertEqual(v.artifacts.saved[f'snapshot-{index:03}.json'], raw)
            self.assertIsNone(v.reference)
            self.assertIsNone(v.latest_snapshot)
            self.assertFalse(v.demand_finished)
        self.assertEqual(v.demand_reference, snapshots[0])
        with self.assertRaisesRegex(d.transport.REFUSAL, 'basis changed'):
            d.transport.stable_snapshot(*snapshots)
        self.assertEqual(v.commands, 0)
        self.assertEqual(h.command_writes, [])

    def test_first_complete_current_ammo_activates_its_own_basis_then_freezes_it(self):
        h, future, diagnostic = self.harness()
        v = h.driver
        deadlines = v.deadline, v.stage_deadline, v.hard_deadline
        future['mutate'] = lambda s: s['mapping']['weaponWorldMeters'][3].__setitem__(0, s['mapping']['weaponWorldMeters'][3][0]+.01)
        source, _, _ = v.fresh_available()
        self.assertEqual(v.demand_reference, diagnostic)
        self.assertIs(v.reference, source)
        self.assertIs(v.latest_snapshot, source)
        self.assertTrue(v.demand_finished)
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline), deadlines)
        activations = [e for e in v.artifacts.events if e['type'] == 'selected_ammo_action_reference_activated']
        self.assertEqual(len(activations), 1)
        self.assertEqual(activations[0]['commands'], 0)
        self.assertEqual(activations[0]['diagnosticNonce'], diagnostic['nonce'])
        self.assertEqual(activations[0]['sourceNonce'], source['nonce'])
        self.assertAlmostEqual(activations[0]['diagnosticDistanceMeters'], .01)
        changed = copy.deepcopy(source)
        changed['mapping']['weaponWorldMeters'][3][0] += .003
        with self.assertRaisesRegex(d.transport.REFUSAL, 'basis changed'):
            v.admit_snapshot(changed, h.now, int(changed['nonce']))
        self.assertIs(v.reference, source)
        self.assertEqual(h.command_writes, [])

    def test_passive_basis_motion_never_hides_identity_input_family_or_calibration_changes(self):
        mutations = (
            ('owner', lambda s: s['owner'].update(weapon=999)),
            ('equipment', lambda s: s['equipment'].update(persistence=999)),
            ('rig', lambda s: s['rig'].update(pose=999)),
            ('configuration', lambda s: s['selected'].update(configurationPath='changed')),
            ('sight', lambda s: s['sight']['family'].update(persistentId='changed')),
            ('head', lambda s: s['input']['head']['position'].__setitem__(0, .003)),
            ('right', lambda s: s['input']['hands'][1]['grip']['position'].__setitem__(0, .003)),
            ('left', lambda s: s['input']['hands'][0]['grip']['position'].__setitem__(0, .003)),
            ('calibration', lambda s: s['mapping']['anatomicalWristToGrip'][3].__setitem__(0, .01)),
            ('wrist_mapping', lambda s: s['mapping']['rawLeftWristWorldMeters'][3].__setitem__(0, 99.)),
            ('nonrigid_weapon', lambda s: s['mapping']['weaponWorldMeters'][0].__setitem__(0, 2.)),
            ('pending_ammo', lambda s: s['fixedContainer'].update(pendingRequest={})),
            ('expired_ammo', lambda s: s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns'])),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                h, future, _ = self.harness()
                def update(s):
                    s['mapping']['weaponWorldMeters'][3][0] += .01
                    mutate(s)
                future['mutate'] = update
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.reference)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_passive_movement_keeps_initial_response_outer_and_source_deadlines(self):
        for mode in ('initial', 'response', 'outer', 'source'):
            with self.subTest(mode=mode):
                h, future, _ = self.harness()
                v, started = h.driver, h.now
                future['missing'] = True
                future['mutate'] = lambda s: s['mapping']['weaponWorldMeters'][3].__setitem__(0, s['mapping']['weaponWorldMeters'][3][0]+.01)
                if mode == 'response':
                    h.response_after = None
                if mode == 'outer':
                    v.deadline = h.now+150*self.MS
                if mode == 'source':
                    future['missing'] = False
                    future['mutate'] = lambda s: s.update(native_deadline_ns=s['observed_ns'])
                original_stage = v.stage_deadline
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    v.fresh_available()
                self.assertEqual(v.demand_started, started)
                self.assertEqual(v.stage_deadline, original_stage)
                self.assertLessEqual(h.now-started, d.NS+self.MS)
                self.assertIsNone(v.reference)
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_activation_refuses_owned_input_and_unsettled_writer_states(self):
        changes = (
            ('commands', 1), ('pending_command', True), ('unsafe_owner', True),
            ('held_left', True), ('held_right', True), ('held_trigger', True),
            ('held_buttons', {'a'}), ('motion_started', 1), ('active_plan', {}),
        )
        for field, value in changes:
            with self.subTest(field=field):
                h, _, _ = self.harness()
                v = h.driver
                v.demand_started = h.now
                setattr(v, field, value)
                _, complete = self.response(h.now)
                with self.assertRaises(d.transport.REFUSAL):
                    v.accept_snapshot(complete, d.transport.json_bytes(complete), h.now)
                self.assertIsNone(v.reference)
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_active_post_shot_missing_view_keeps_original_basis_refusal(self):
        h, future, reference = self.shot_harness()
        v = h.driver
        future['missing'] = True
        future['mutate'] = lambda s: s['mapping']['weaponWorldMeters'][3].__setitem__(0, s['mapping']['weaponWorldMeters'][3][0]+.003)
        before = list(h.command_writes)
        with self.assertRaisesRegex(d.transport.REFUSAL, 'basis changed'):
            v.fresh_available()
        self.assertIs(v.reference, reference)
        self.assertEqual(h.command_writes, before)
        self.assertEqual(v.commands, 4)



class SelectedAmmoCapturePendingBasisTests(unittest.TestCase):
    """Both unpublished shapes retain diagnostic identity without action authority."""
    MS = SelectedAmmoDriverTests.MS
    Driver = SelectedAmmoDriverTests.Driver
    response = SelectedAmmoDriverTests.response
    missing = staticmethod(SelectedAmmoDriverTests.missing)
    capture_missing = staticmethod(SelectedAmmoDriverTests.capture_missing)
    harness = SelectedAmmoDriverTests.harness
    shot_harness = SelectedAmmoDriverTests.shot_harness

    @staticmethod
    def move_weapon(s, distance=.006):
        s['mapping']['weaponWorldMeters'][3][0] += distance

    def test_both_initial_absences_preserve_bytes_until_full_current_source_activates(self):
        for initial_shape in ('view', 'capture'):
            with self.subTest(initial_shape=initial_shape):
                h, future, initial = self.harness()
                v = h.driver
                if initial_shape == 'capture':
                    self.capture_missing(initial)
                    h.publish(initial)
                initial_raw = d.transport.json_bytes(initial)
                pending_raw = []
                requests = 0
                deadlines = v.deadline, v.hard_deadline, v.stage_deadline
                started = h.now

                def transition(s):
                    nonlocal requests
                    requests += 1
                    self.assertIsNone(v.reference)
                    self.assertIsNone(v.latest_snapshot)
                    self.assertEqual(v.commands, 0)
                    self.assertFalse(v.demand_finished)
                    self.assertEqual(v.demand_reference, initial)
                    self.move_weapon(s, .006 if requests == 1 else .008)
                    if requests == 1:
                        self.capture_missing(s)
                        pending_raw.append(d.transport.json_bytes(s))

                future['mutate'] = transition
                source, _, _ = v.fresh_available()
                self.assertEqual(requests, 2)
                self.assertEqual(v.artifacts.saved['snapshot-001.json'], initial_raw)
                self.assertEqual(v.artifacts.saved['snapshot-002.json'], pending_raw[0])
                self.assertEqual(d.transport.json_bytes(v.demand_reference), initial_raw)
                self.assertIs(v.reference, source)
                self.assertIs(v.latest_snapshot, source)
                self.assertTrue(v.demand_finished)
                self.assertEqual(source['fixedContainer']['view']['snapshot']['counts']['loaded'], 1)
                self.assertEqual(v.demand_started, started)
                self.assertEqual((v.deadline, v.hard_deadline, v.stage_deadline), deadlines)
                self.assertEqual(len([e for e in v.artifacts.events if e['type'] == 'pump_capture_pending']), 2)
                self.assertEqual(h.command_writes, [])

    def test_capture_pending_drift_never_hides_invalid_identity_input_mapping_or_absence(self):
        mutations = (
            ('owner', lambda s: s['owner'].update(weapon=999)),
            ('equipment', lambda s: s['equipment'].update(persistence=999)),
            ('rig', lambda s: s['rig'].update(pose=999)),
            ('configuration', lambda s: s['selected'].update(configurationPath='changed')),
            ('sight', lambda s: s['sight']['family'].update(persistentId='changed')),
            ('head', lambda s: s['input']['head']['position'].__setitem__(0, .003)),
            ('right', lambda s: s['input']['hands'][1]['grip']['position'].__setitem__(0, .003)),
            ('left', lambda s: s['input']['hands'][0]['grip']['position'].__setitem__(0, .003)),
            ('calibration', lambda s: s['mapping']['anatomicalWristToGrip'][3].__setitem__(0, .01)),
            ('wrist_mapping', lambda s: s['mapping']['rawLeftWristWorldMeters'][3].__setitem__(0, 99.)),
            ('nonrigid_weapon', lambda s: s['mapping']['weaponWorldMeters'][0].__setitem__(0, 2.)),
            ('generation', lambda s: s['input'].update(generation=999)),
            ('active_trigger', lambda s: s['input']['hands'][1].update(trigger=1.)),
            ('held_input', lambda s: s['input']['hands'][0].update(held=1)),
            ('interlock', lambda s: s.update(weapon_actions_blocked=True)),
            ('feed', lambda s: s.update(feed={'blocksFire': False})),
            ('reason', lambda s: s['fixedContainer'].update(reason='unrecognized')),
            ('schema', lambda s: s['fixedContainer'].update(schema='unknown')),
            ('view', lambda s: s['fixedContainer'].update(
                view=self.response(s['observed_ns'], int(s['nonce']))[1]['fixedContainer']['view'])),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                h, future, initial = self.harness()
                v = h.driver
                refused_raw = []

                def update(s):
                    self.move_weapon(s)
                    self.capture_missing(s)
                    mutate(s)
                    refused_raw.append(d.transport.json_bytes(s))

                future['mutate'] = update
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    v.fresh_available()
                self.assertEqual(v.artifacts.saved['snapshot-002.json'], refused_raw[0])
                self.assertEqual(v.demand_reference, initial)
                self.assertEqual(len(v.artifacts.events), 1)
                self.assertIsNone(v.reference)
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_first_capture_pending_refuses_action_authority_and_unknown_or_blocked_states(self):
        fields = (
            ('commands', 1), ('pending_command', True), ('unsafe_owner', True),
            ('held_left', True), ('held_right', True), ('held_trigger', True),
            ('held_buttons', {'a'}), ('motion_started', 1), ('active_plan', {}),
            ('demand_finished', True), ('reference', {}), ('latest_snapshot', {}),
        )
        for field, value in fields:
            with self.subTest(field=field):
                h, _, initial = self.harness()
                v = h.driver
                self.capture_missing(initial)
                v.demand_started = h.now
                setattr(v, field, value)
                with self.assertRaises(d.transport.REFUSAL):
                    v.admit_snapshot(initial, h.now, int(initial['nonce']))
                self.assertIsNone(v.demand_reference)
                self.assertEqual(h.command_writes, [])
        for label, mutation in (
            ('reason', lambda s: s['fixedContainer'].update(reason='unrecognized')),
            ('interlock', lambda s: s.update(weapon_actions_blocked=True)),
            ('invalid', lambda s: s.update(valid=False, invalid_reason='unrelated native block')),
        ):
            with self.subTest(label=label):
                h, _, initial = self.harness()
                self.capture_missing(initial)
                mutation(initial)
                h.publish(initial)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertEqual(h.driver.artifacts.saved['snapshot-001.json'], d.transport.json_bytes(initial))
                self.assertFalse(h.driver.artifacts.events)
                self.assertIsNone(h.driver.demand_reference)
                self.assertIsNone(h.driver.reference)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_capture_pending_movement_retains_original_demand_request_outer_and_source_deadlines(self):
        for mode in ('initial', 'response', 'outer', 'source'):
            with self.subTest(mode=mode):
                h, future, initial = self.harness()
                v, started = h.driver, h.now
                self.capture_missing(initial)
                h.publish(initial)

                def pending(s):
                    self.move_weapon(s)
                    self.capture_missing(s)
                    if mode == 'source':
                        s['native_deadline_ns'] = s['observed_ns']

                future['mutate'] = pending
                if mode == 'response':
                    h.response_after = None
                if mode == 'outer':
                    v.deadline = h.now+150*self.MS
                deadlines = v.deadline, v.hard_deadline, v.stage_deadline
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    v.fresh_available()
                self.assertEqual(v.demand_started, started)
                self.assertEqual((v.deadline, v.hard_deadline, v.stage_deadline), deadlines)
                self.assertLessEqual(h.now-started, d.NS+self.MS)
                self.assertIsNone(v.reference)
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_parent_pending_dispatch_keeps_original_block_admission_and_full_basis_gate(self):
        h, future, initial = self.harness()
        v = h.driver
        v.__class__ = d.FixedContainerDriver
        self.capture_missing(initial)
        initial['weapon_actions_blocked'] = True
        h.publish(initial)

        def pending(s):
            self.move_weapon(s)
            self.capture_missing(s)
            s['weapon_actions_blocked'] = True

        future['mutate'] = pending
        with self.assertRaisesRegex(d.transport.REFUSAL, 'basis changed'):
            v.fresh_available()
        self.assertEqual(v.demand_reference, initial)
        self.assertEqual(len(v.artifacts.events), 1)
        self.assertIsNone(v.reference)
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(h.command_writes, [])

    def test_activated_basis_is_retained_for_post_shot_capture_absence_and_drift_refusal(self):
        for drift in (0., .003):
            with self.subTest(drift=drift):
                original_harness = self.harness

                def shifted_harness(*args, **kwargs):
                    h, future, initial = original_harness(*args, **kwargs)
                    future['mutate'] = lambda s: self.move_weapon(s, .01)
                    return h, future, initial

                with patch.object(self, 'harness', side_effect=shifted_harness):
                    h, future, reference = self.shot_harness()
                v = h.driver
                self.assertNotEqual(v.demand_reference['mapping'], reference['mapping'])
                clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
                before_commands = list(h.command_writes)
                requests = 0

                def transition(s):
                    nonlocal requests
                    requests += 1
                    self.move_weapon(s, .01+drift)
                    if requests == 1:
                        self.capture_missing(s)
                    else:
                        s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0

                future['mutate'] = transition
                if drift:
                    with self.assertRaisesRegex(d.transport.REFUSAL, 'basis changed'):
                        v.fresh_available()
                    self.assertEqual(requests, 1)
                else:
                    result, _, _ = v.fresh_available()
                    self.assertEqual(result['fixedContainer']['view']['snapshot']['counts']['loaded'], 0)
                    self.assertEqual(requests, 2)
                self.assertIs(v.reference, reference)
                self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
                self.assertEqual(h.command_writes, before_commands)
                self.assertEqual(v.commands, 4)



if __name__ == "__main__":
    unittest.main()
