"""Synthetic transformed geometry/receipt refusal cases; no native acceptance."""
import copy
import importlib.util
import math
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


p = module("fixed_planner_test", ROOT / "tools/plan_bc2_fixed_container_simulation.py")
d = module("fixed_driver_test", ROOT / "tools/drive_bc2_fixed_container_simulation.py")
fixture = module("fixed_sight_fixture", ROOT / "tests/test_bc2_sight_simulation_plan.py")
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
        hand=None, target=None, carriedSupply=None, pouchContact=False, grip=False)
    raw = dict(valid=True, current=True, admittedAtTick=True, ownerMatches=True, rigMatches=True, profileMatches=True,
        rigFingerprint=profile["rigFingerprint"], input=copy.deepcopy(source), weaponFromWrist=f.identity(),
        weaponWorldMeters=copy.deepcopy(s["mapping"]["weaponWorldMeters"]), grip=False, pouchContact=False)
    s["fixedContainer"] = dict(schema=p.OBSERVATION_SCHEMA, available=True, observationOnly=True, retainedEventsAreHistorical=True,
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


if __name__ == "__main__":
    unittest.main()
