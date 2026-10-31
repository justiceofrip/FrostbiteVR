"""Deterministic synthetic sight/XR geometry and refusal tests; no native evidence."""
import copy
import importlib.util
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sight_planner", ROOT / "tools/plan_bc2_sight_simulation.py")
p = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(p)
f = p.frame
NOW = 1_000_000_000


def matrix(position, angles):
    return f.from_pose(dict(position=position, orientation=f.ypr_quaternion(angles)))


def snapshot(secondary=False, subtree=False, primary_slot=1):
    """Explicitly invented owners/leases and transformed geometry, never live evidence."""
    owner = dict(player=11, soldier=12, weak=13, weapon=14, actorGeneration=15, equipGeneration=16, space=17)
    reference = f.to_pose(matrix([.43, 1.5, -.27], [33, -9, 7]))
    right = f.to_pose(matrix([.71, 1.2, -.62], [-25, 8, 19]))
    hand = dict(grip=right, aim=right, gripTracked=True, aimTracked=True, active=6, held=0,
                trigger=0., squeeze=0., touchActive=0, touched=0, stickX=0., stickY=0.)
    inp = dict(generation=100, spaceGeneration=17, trackingBasisGeneration=19,
               predictedNs=NOW, focused=True, headValid=True, worldUnitsPerMeter=3.7,
               referenceHead=reference, head=f.to_pose(matrix([.56, 1.57, -.43], [71, -22, 13])),
               hands=[copy.deepcopy(hand), copy.deepcopy(hand)])
    inp["hands"][1]["squeeze"] = 1.
    evidence = dict(owner=dict(actor=(13 << 32) | 12, actorGeneration=15, equipGeneration=18, space=17),
                    sequence=100, observedNs=NOW-10_000_000, deadlineNs=NOW+70_000_000, nowNs=NOW,
                    focused=True, tracked=[True, True], released=[True, False])
    primary = "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped"
    secondary_config = "Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped"
    primary_action = 25 if primary_slot == 0 else 36
    family = dict(observedMs=500, physicalItem=44, selectedSecondary=secondary, inventory=26, primaryAction=primary_action,
                  targetWeapon=34, targetSlot=primary_slot if secondary else 2, targetAction=primary_action if secondary else 33,
                  primaryAsset="XM8_sp_s", primaryConfiguration=primary, secondaryAsset="40mmgl",
                  secondaryConfiguration=secondary_config, persistentId="synthetic-xm8-family")
    w = matrix([2.3, 1.7, -3.1], [21, -12, 8])
    b = matrix([1.2, -.4, 2.8], [-37, 17, 11])
    k = matrix([0, 0, 0], [-17, 13, -8])
    rear = matrix([.03, .11, -.19], [27, -19, 31])
    front = matrix([-.02, .13, -.39], [-13, 21, -9])
    axis = rear[0][:3]
    pivot = rear[3][:3]
    grasp = p.point([0, 0, -.09], rear)
    raw = matrix([-.08, .17, -.28], [-11, 29, 13])
    palm = [.021, -.013, .067]  # Nonzero bind-derived mechanism point.
    hand_point = p.point(palm, raw)
    distance = math.dist(hand_point, grasp)
    wrist = f.multiply(raw, w)
    absolute = f.multiply(f.multiply(f.multiply(f.inverse(k), wrist), f.inverse(b)), f.from_pose(reference))
    inp["hands"][0]["grip"] = f.to_pose(absolute)
    contact = dict(valid=True, generation=100, deadlineQpc=1234, deadlineNs=NOW+65_000_000,
                   previewValid=True, contactDistanceMeters=distance, pivotMeters=pivot, axis=axis,
                   handLocalMeters=hand_point, graspPointMeters=grasp, sightLocalMeters=rear,
                   frontLocalMeters=None if subtree else front, rawHandLocalFrameMeters=raw,
                   rawPalmPointWristMeters=palm, handLocalFrameMeters=copy.deepcopy(raw), palmPointWristMeters=palm)
    sight = dict(status="available", generation=100, deadlineQpc=1234, deadlineNs=NOW+65_000_000,
                 family=family, adapter=dict(assembly="subtree" if subtree else "opposed_leaves", root="jntWpn_1",
                                            rear="jntWpn_9", front=None if subtree else "jntWpn_11", travelRadians=math.pi/2),
                 support=dict(valid=True, distanceMeters=.3), preferredByDistance=distance <= .08,
                 contact=contact, native=dict(valid=True, physicalItem=44, rear=rear,
                                            front=None if subtree else front, rawHand=raw))
    return dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, nonce=100,
                observed_ns=NOW-1_000_000, valid_until_ns=NOW+60_000_000,
                native_deadline_ns=NOW+65_000_000, native_deadline_qpc=1234,
                weapon_actions_blocked=False, physical_equipment_generation=18, shot_input_generation=100,
                owner=owner, equipment=dict(weapon=14, data=22, persistence=44, asset="40mmgl" if secondary else "XM8_sp_s"),
                rig=dict(soldier=12, weak=13, count=128, skeleton=24, pose=25, fingerprint=0xa7f219a1426216ab),
                selected=dict(configurationPathVerified=True, sequence=100, observedNs=NOW-10_000_000,
                              deadlineNs=NOW+70_000_000, configurationPath=secondary_config if secondary else primary,
                              configurationPathPointer=27, inventory=26, selectedSlot=2 if secondary else primary_slot, soleConfiguredArray=28,
                              matchedMeshPath="synthetic/mesh", meshes=[dict(path="synthetic/mesh", address=29, typeInfo=30, namePointer=31)]),
                input=inp, input_evidence=evidence, feed=None, sight=sight,
                mapping=dict(weaponWorldMeters=w, trackingBodyWorldMeters=b, rawLeftWristWorldMeters=wrist, anatomicalWristToGrip=k))


def emitted_local(source, command):
    mapping, inp = source["mapping"], source["input"]
    absolute = f.from_pose(dict(position=command["positionMeters"], orientation=f.ypr_quaternion(command["yawPitchRollDegrees"])))
    wrist = f.multiply(f.multiply(mapping["anatomicalWristToGrip"],
                                f.multiply(absolute, f.inverse(f.from_pose(inp["referenceHead"])))), mapping["trackingBodyWorldMeters"])
    return f.multiply(wrist, f.inverse(mapping["weaponWorldMeters"]))


class SightSimulationPlanTests(unittest.TestCase):
    def test_bind_point_hinge_roundtrip_and_controller_only_schedule(self):
        source = snapshot()
        original = copy.deepcopy(source)
        plan = p.plan(source, NOW, 100)
        self.assertEqual(source, original)
        self.assertIsNone(source["feed"])
        self.assertFalse(plan["nativeAcceptance"])
        self.assertFalse(plan["executionAuthorized"])
        self.assertEqual(plan["sourceValidUntilNs"], source["valid_until_ns"])
        self.assertEqual(plan["preserve"]["rightHand"], source["input"]["hands"][1])
        self.assertEqual(plan["preserve"]["head"], source["input"]["head"])
        self.assertEqual(plan["expectedTransition"]["configurationPath"], source["sight"]["family"]["secondaryConfiguration"])
        self.assertTrue(plan["expectedTransition"]["nativeAcceptanceRequiresExactRequestAcknowledgement"])
        acquire = next(s for s in plan["schedule"] if s["phase"] == "acquire")
        release = plan["schedule"][-1]
        self.assertTrue(acquire["requiresFreshSightPreference"])
        self.assertTrue(release["requiresObservedFamilyTransition"])
        self.assertEqual(release["grip"], 0)
        c = source["sight"]["contact"]
        actual = p.point(c["rawPalmPointWristMeters"], emitted_local(source, acquire))
        self.assertLess(math.dist(actual, c["graspPointMeters"]), 1e-7)
        self.assertGreater(math.dist(emitted_local(source, acquire)[3][:3], actual), .05)
        # Independent Cartesian axis-angle evaluation of the final hand point.
        axis, pivot, grasp = c["axis"], c["pivotMeters"], c["graspPointMeters"]
        r = [g-v for g, v in zip(grasp, pivot)]
        cross = [axis[1]*r[2]-axis[2]*r[1], axis[2]*r[0]-axis[0]*r[2], axis[0]*r[1]-axis[1]*r[0]]
        dot, angle = sum(a*v for a, v in zip(axis, r)), plan["geometry"]["signedTravelRadians"]
        expected = [v+q*math.cos(angle)+c*math.sin(angle)+a*dot*(1-math.cos(angle))
                    for v, q, c, a in zip(pivot, r, cross, axis)]
        endpoint = p.point(c["rawPalmPointWristMeters"], emitted_local(source, release))
        self.assertLess(math.dist(endpoint, expected), 1e-7)
        self.assertGreaterEqual(plan["verification"]["detentHeldNs"], 70_000_000)
        self.assertLess(plan["verification"]["maximumStepRadians"], .175)
        for command in plan["schedule"]:
            self.assertTrue(all(line.startswith(("hand l ", "grip l ")) for line in command["commands"]))
        self.assertGreater(plan["durationNs"], source["valid_until_ns"]-NOW)  # Schedule never pretends to renew lease.

    def test_reverse_and_subtree_geometry(self):
        source = snapshot(secondary=True, subtree=True)
        plan = p.plan(source, NOW, 100, target="primary")
        self.assertLess(plan["geometry"]["signedTravelRadians"], -.4)
        self.assertIsNone(plan["geometry"]["frontLocalMeters"])
        self.assertEqual(plan["expectedTransition"]["targetAction"], 36)
        self.assertEqual(plan["expectedTransition"]["asset"], "XM8_sp_s")

    def test_both_primary_slots_use_observed_reciprocal_action(self):
        for primary_slot, primary_action in ((0, 25), (1, 36)):
            for secondary in (False, True):
                with self.subTest(primary_slot=primary_slot, secondary=secondary):
                    source = snapshot(secondary=secondary, primary_slot=primary_slot)
                    plan = p.plan(source, NOW, 100, target="primary" if secondary else "secondary")
                    self.assertEqual(plan["expectedTransition"]["primaryAction"], primary_action)
                    self.assertEqual(plan["expectedTransition"]["targetAction"], primary_action if secondary else 33)
                    self.assertEqual(plan["sourceValidUntilNs"], source["valid_until_ns"])

    def test_primary_action_cannot_be_missing_or_borrowed_from_other_slot(self):
        for value in (None, 7, 33, 38, 36):
            source = snapshot(secondary=True, primary_slot=0)
            if value is None:
                del source["sight"]["family"]["primaryAction"]
            else:
                source["sight"]["family"]["primaryAction"] = value
            with self.subTest(value=value), self.assertRaises(p.Refusal):
                p.plan(source, NOW, 100, target="primary")

    def test_stale_identity_and_geometry_refusals(self):
        mutations = {
            "expired_snapshot": lambda s: s.update(valid_until_ns=NOW),
            "wrong_nonce": lambda s: s.update(nonce=99),
            "wrong_owner": lambda s: s["owner"].update(weapon=15),
            "wrong_space": lambda s: s["input"].update(spaceGeneration=18),
            "expired_input": lambda s: s["input_evidence"].update(deadlineNs=NOW),
            "extended_lease": lambda s: s.update(valid_until_ns=NOW+80_000_000),
            "wrong_config": lambda s: s["selected"].update(configurationPath="unrelated/config"),
            "wrong_family_item": lambda s: s["sight"]["family"].update(physicalItem=45),
            "wrong_inventory": lambda s: s["sight"]["family"].update(inventory=27),
            "same_target": lambda s: s["sight"]["family"].update(targetWeapon=14),
            "same_slot": lambda s: s["sight"]["family"].update(targetSlot=1),
            "wrong_action": lambda s: s["sight"]["family"].update(targetAction=36),
            "wrong_generation": lambda s: s["sight"]["contact"].update(generation=101),
            "wrong_deadline": lambda s: s["sight"]["contact"].update(deadlineNs=NOW+64_000_000),
            "wrong_axis": lambda s: s["sight"]["contact"].update(axis=[0, 2, 0]),
            "wrong_palm": lambda s: s["sight"]["contact"].update(rawPalmPointWristMeters=[0, 0, 0]),
            "missing_front": lambda s: s["sight"]["contact"].update(frontLocalMeters=None),
            "wrong_native_item": lambda s: s["sight"]["native"].update(physicalItem=45),
            "unavailable": lambda s: s["sight"].update(status="geometry_unavailable"),
            "inactive_squeeze": lambda s: s["input"]["hands"][0].update(active=2),
            "firing": lambda s: s["input"]["hands"][1].update(trigger=1),
            "blocked": lambda s: s.update(weapon_actions_blocked=True),
        }
        for label, mutate in mutations.items():
            with self.subTest(label=label):
                source = snapshot()
                mutate(source)
                with self.assertRaises(p.Refusal):
                    p.plan(source, NOW, 100)

    def test_calibration_and_preference_are_not_fabricated(self):
        source = snapshot()
        source["mapping"]["anatomicalWristToGrip"] = f.identity()
        with self.assertRaisesRegex(p.Refusal, "binding"):
            p.plan(source, NOW, 100)
        source = snapshot()
        source["sight"]["preferredByDistance"] = not source["sight"]["preferredByDistance"]
        with self.assertRaisesRegex(p.Refusal, "preference"):
            p.plan(source, NOW, 100)
        with self.assertRaisesRegex(p.Refusal, "already selected"):
            p.plan(snapshot(secondary=True), NOW, 100)
        with self.assertRaisesRegex(p.Refusal, "command period"):
            p.plan(snapshot(), NOW, 100, step_ns=16_000_000)


if __name__ == "__main__":
    unittest.main()
