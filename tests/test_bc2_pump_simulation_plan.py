"""Synthetic geometry/receipt refusal tests, not production/native acceptance."""
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


p = module("pump_planner", ROOT / "tools/plan_bc2_pump_simulation.py")
f = p.frame
# Reuse only the existing explicitly synthetic arbitrary-reference input fixture.
fixture = module("sight_fixture", ROOT / "tests/test_bc2_sight_simulation_plan.py")
NOW = 1_000_000_000


def claim(snapshot, hand, kind, item, contact, identifier, prerequisite=0):
    e = snapshot["input_evidence"]
    return dict(token=dict(id=identifier, owner=copy.deepcopy(e["owner"]), hand=hand, kind=kind,
                           item=copy.deepcopy(item), contact=contact, prerequisiteClaim=prerequisite),
                inputSequence=e["sequence"], deadlineNs=e["deadlineNs"])


def set_local(s, local):
    m = s["mapping"]
    world = f.multiply(local, m["weaponWorldMeters"])
    m["rawLeftWristWorldMeters"] = world
    absolute = f.multiply(f.multiply(f.multiply(f.inverse(m["anatomicalWristToGrip"]), world),
                                     f.inverse(m["trackingBodyWorldMeters"])), f.from_pose(s["input"]["referenceHead"]))
    s["input"]["hands"][0]["grip"] = f.to_pose(absolute)
    s["input"]["hands"][0]["aim"] = f.to_pose(absolute)
    if "shellPump" in s:
        s["shellPump"]["shell"]["raw"]["rawWristWorldMeters"] = copy.deepcopy(world)


def snapshot(pumping=False, variant=0):
    s = fixture.snapshot()
    del s["sight"]
    # Keep a physically reachable body/weapon spacing while retaining unrelated
    # rotations, nonzero XR reference, head motion and nonunit world scale.
    s["mapping"]["trackingBodyWorldMeters"][3][:3] = s["mapping"]["weaponWorldMeters"][3][:3]
    s["equipment"]["asset"] = "synthetic-pump-"+str(variant)
    s["selected"]["configurationPath"] = "synthetic/exact/"+str(variant)
    evidence = s["input_evidence"]
    weapon = dict(id=14, generation=18)
    native = dict(owner=copy.deepcopy(s["owner"]), firing=[51, 52, 53], serverPlayer=54, serverSoldier=55, serverItem=56)
    # Distinct synthetic rotations, lengths and signed axes ensure no SPAS pose
    # or default pouch can accidentally satisfy these independent endpoint tests.
    insertion = dict(id=1000+variant, revision=2, approach=3, orientation=1,
        travelMeters=.105+variant*.009, postCaptureTravelMeters=.02, seatDwellNs=60_000_000,
        alignmentNs=120_000_000, captureDistanceMeters=.06, maxStepMeters=.04,
        maxStepRadians=1.047, maxSampleGapNs=100_000_000, maxGuidedNs=5_000_000_000,
        travelDirection=[.6, 0, .8], itemFromHand=fixture.matrix([.03, -.02, .04], [17, -13, 29]),
        itemFromInsertion=fixture.matrix([-.02, .01, -.03], [-11, 23, -7]),
        weaponFromEntry=fixture.matrix([.07+variant*.013, -.03, -.24], [-19, 31, 7]))
    pump_profile = dict(id=1000+variant, revision=2, rearDirection=-1 if variant % 2 else 1,
        strokeMeters=.09+variant*.011, axis=[0, .6, .8],
        closedWrist=fixture.matrix([.04+variant*.018, -.05, -.4], [23, -17, 11]))
    raw = dict(valid=True, current=True, ownerMatches=True, rigMatches=True,
               rigFingerprint=s["rig"]["fingerprint"], input=copy.deepcopy(evidence))
    shell_raw = {**copy.deepcopy(raw), "profileMatches": True,
                 "weaponWorldMeters": copy.deepcopy(s["mapping"]["weaponWorldMeters"]),
                 "rawWristWorldMeters": copy.deepcopy(s["mapping"]["rawLeftWristWorldMeters"])}
    reserve = dict(identity=native, sequence=80, observedNs=NOW-10_000_000, deadlineNs=NOW+70_000_000,
                   loaded=3, reserve=17, capacity=6, verified=True, fresh=True, allThreeIdle=True, reloadInputReady=True)
    shell = dict(ticked=True, active=False, held=False, nativeHolding=False, pending=False, reloadHeld=False,
        ammoOwnsHand=False, cycle=0, identity=native, lifetimeCounters=dict(acquired=2, submitted=2, completed=2),
        raw=shell_raw, pouch=dict(frame=1, centerMeters=[-.21, -.47, .13], radiusMeters=.14), reserve=reserve,
        heldLease=None, item=None, pendingRequest=None, lastSubmitted=None, lastAcknowledged=None, geometry=dict(available=False))
    pump = dict(ticked=True, phase=0, recoveryPhase=0, debt=False, blocksFire=False, ownsHand=False,
        companionReturning=False, raw={**raw, "mappingValid": True, "contact": copy.deepcopy(pump_profile["closedWrist"])},
        held=None, pending=None, recovery=None, recoveryPending=None, mechanismClaim=None, target=None,
        recoveryTarget=None, lastSettled=None, lastRecovered=None, acknowledgedNs=0,
        supportReturnPending=False, supportReturnsLifetime=0, supportHolding=False, supportToken=0, native=None,
        gesture=dict(available=False))
    sp = dict(available=True, observationOnly=True, retainedEventsAreHistorical=True, perBranchAmmunitionAvailable=False,
        input=copy.deepcopy(evidence), profile=dict(registrationId=40+variant, rigFingerprint=s["rig"]["fingerprint"],
        configurationPath=s["selected"]["configurationPath"], shell=insertion, pump=pump_profile),
        gunClaim=claim(s, 1, 1, weapon, dict(id=1, generation=14), 200), leftClaim=None, shell=shell, pump=pump)
    s["shellPump"] = sp
    set_local(s, pump_profile["closedWrist"])
    if pumping:
        lease = dict(owner=copy.deepcopy(evidence["owner"]), item=weapon,
            mechanism=dict(id=pump_profile["id"], generation=18), cycle=400, shot=500, sequence=80,
            observedNs=NOW-10_000_000, deadlineNs=NOW+70_000_000, held=True, fresh=True, advancing=None)
        pump.update(phase=1, debt=True, blocksFire=True, held=copy.deepcopy(lease), native=dict(identity=native,
            ownerMatchesSnapshot=True, countsFreshnessAvailable=False, phase=3, failure=0, cycle=400, shot=500,
            blocksFire=True, heldSuspended=False, held=copy.deepcopy(lease), ready=None))
        s["weapon_actions_blocked"] = True
    return s


def refresh(s, now, generation):
    """Invent a *new synthetic* callback snapshot, never an execution utility."""
    s = copy.deepcopy(s)
    def visit(value):
        if isinstance(value, dict):
            for k in value:
                if k in ("sequence", "inputSequence") and isinstance(value[k], int):
                    value[k] = generation
                elif k in ("observedNs", "nowNs"):
                    value[k] = now-1_000_000 if k == "observedNs" else now
                elif k == "deadlineNs":
                    value[k] = now+70_000_000
                else:
                    visit(value[k])
        elif isinstance(value, list):
            for child in value:
                visit(child)
    visit(s)
    s.update(nonce=generation, observed_ns=now-1_000_000, valid_until_ns=now+60_000_000,
             native_deadline_ns=now+65_000_000, shot_input_generation=generation)
    s["input"].update(generation=generation, predictedNs=now)
    return s


def held_after_pickup(before, plan, now=NOW+200_000_000):
    s = refresh(before, now, 110)
    sp, shell = s["shellPump"], s["shellPump"]["shell"]
    shell.update(active=True, held=True, nativeHolding=True, ammoOwnsHand=True, cycle=700)
    shell["lifetimeCounters"]["acquired"] += 1
    shell["heldLease"] = {k: copy.deepcopy(shell["reserve"][k]) for k in
        ("identity", "sequence", "observedNs", "deadlineNs", "loaded", "reserve", "capacity", "fresh")}
    shell["heldLease"].update(cycle=700, nativeBindingVerified=True, allThreeHeld=True)
    item_key = dict(id=300, generation=30)
    c = claim(s, 0, 5, item_key, dict(id=9, generation=1), 201)
    shell["item"] = dict(key=item_key, claim=c, units=1, profile=dict(id=sp["profile"]["shell"]["id"], generation=2))
    sp["leftClaim"] = copy.deepcopy(c)
    s["weapon_actions_blocked"] = True
    s["input"]["hands"][0]["squeeze"] = 1.
    for e in (s["input_evidence"], sp["input"], shell["raw"]["input"]):
        e["released"][0] = False
    end = plan["schedule"][-1]
    absolute = f.from_pose(dict(position=end["positionMeters"], orientation=f.ypr_quaternion(end["yawPitchRollDegrees"])))
    mapping = s["mapping"]
    world = f.multiply(f.multiply(mapping["anatomicalWristToGrip"], f.multiply(absolute, f.inverse(f.from_pose(s["input"]["referenceHead"])))),
                       mapping["trackingBodyWorldMeters"])
    set_local(s, f.multiply(world, f.inverse(mapping["weaponWorldMeters"])))
    return s


def pump_endpoint(before, travel, now, generation):
    s = refresh(before, now, generation)
    sp, pump = s["shellPump"], s["shellPump"]["pump"]
    # The physical starting lease deliberately stays historical/expired.
    pump["held"] = copy.deepcopy(before["shellPump"]["pump"]["held"])
    pump["held"]["fresh"] = False
    lease = pump["native"]["held"]
    c = claim(s, 0, 4, lease["item"], lease["mechanism"], 202, 200)
    sp["leftClaim"] = copy.deepcopy(c)
    pump.update(mechanismClaim=c, ownsHand=True, phase=4 if travel else 3,
                target=dict(profile=sp["profile"]["pump"]["id"], revision=2, inputSequence=generation,
                            observedNs=now-1_000_000, deadlineNs=now+70_000_000, travelMeters=travel))
    pump["gesture"] = dict(available=True, evaluated=True, gate=0, failure=0, recovery=False,
        input=generation, rawInput=generation, phase=pump["phase"], travelMeters=travel)
    s["input"]["hands"][0]["squeeze"] = 1.
    for e in (s["input_evidence"], sp["input"], pump["raw"]["input"]):
        e["released"][0] = False
    return s


class PumpSimulationPlanTests(unittest.TestCase):
    def assert_schedule(self, result, s):
        self.assertFalse(result["executionAuthorized"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertLessEqual(result["sourceValidUntilNs"], s["valid_until_ns"])
        self.assertEqual(result["sourceWeaponActionsBlocked"], s["weapon_actions_blocked"])
        self.assertEqual(result["preserve"]["rightHand"], s["input"]["hands"][1])
        self.assertLess(result["verification"]["maximumStepMeters"], .015)
        self.assertLess(result["verification"]["maximumStepRadians"], .06)
        self.assertTrue(all(g["stop"] and g["requiresFreshSnapshot"] for g in result["receiptGates"]))
        self.assertLessEqual(len(result["schedule"]), p.MAX_SAMPLES)
        for row in result["schedule"]:
            self.assertTrue(all(c.startswith(("hand l ", "grip l ")) for c in row["commands"]))

    def test_distinct_exact_profiles_pump_geometry_and_immutable_block(self):
        for variant in range(3):
            with self.subTest(variant=variant):
                s = snapshot(True, variant)
                original = copy.deepcopy(s)
                result = p.plan(s, "PumpCycle", NOW, 100)
                self.assertEqual(s, original)
                self.assert_schedule(result, s)
                c = s["shellPump"]["profile"]["pump"]
                rear_row = result["schedule"][next(g["afterSample"] for g in result["receiptGates"] if g["id"] == "pump_rear")]
                local = fixture.emitted_local(s, rear_row)
                expected = [x+a*c["rearDirection"]*c["strokeMeters"] for x, a in zip(c["closedWrist"][3][:3], c["axis"])]
                self.assertLess(math.dist(local[3][:3], expected), .00001)
                self.assertLess(f.pose_error(fixture.emitted_local(s, result["schedule"][-1]), c["closedWrist"])[0], .00001)
                phases = [r["phase"] for r in result["schedule"]]
                self.assertGreaterEqual(phases.count("rear")*20_000_000, 900_000_000)
                self.assertGreaterEqual(phases.count("return")*20_000_000, 900_000_000)

    def test_pickup_uses_actual_body_contact_and_fresh_custody_bridge(self):
        s = snapshot()
        pickup = p.plan(s, "ShellPickup", NOW, 100)
        self.assert_schedule(pickup, s)
        end = pickup["schedule"][-1]
        absolute = f.from_pose(dict(position=end["positionMeters"], orientation=f.ypr_quaternion(end["yawPitchRollDegrees"])))
        body = f.multiply(absolute, f.inverse(f.supply_body_frame(s["input"])))
        self.assertLess(math.dist(body[3][:3], s["shellPump"]["shell"]["pouch"]["centerMeters"]), .00001)
        self.assertTrue(all(r["grip"] == 0 for r in pickup["schedule"][:-1]))
        held = held_after_pickup(s, pickup)
        result = p.plan(held, "ShellInsert", NOW+200_000_000, 110, pickup_plan=pickup)
        self.assert_schedule(result, held)
        self.assertTrue(all(r["grip"] == 1 for r in result["schedule"]))
        profile = held["shellPump"]["profile"]["shell"]
        local = fixture.emitted_local(held, result["schedule"][-1])
        # Independent inverse geometry: anatomical hand -> insertion rail.
        # Compare the actual shell pose against the authored translated entry.
        shell_pose = f.multiply(f.inverse(profile["itemFromHand"]), local)
        expected = f.identity()
        expected[3][:3] = [x*profile["travelMeters"] for x in profile["travelDirection"]]
        shell_pose = f.multiply(f.multiply(profile["itemFromInsertion"], shell_pose), f.inverse(profile["weaponFromEntry"]))
        self.assertLess(f.pose_error(shell_pose, expected)[0], .00001)
        self.assertGreaterEqual(sum(r["phase"] == "insert" for r in result["schedule"])*20_000_000, 800_000_000)

    def test_original_frame_callers_are_unchanged(self):
        clear = snapshot()
        f.admit_frame(clear, NOW, 100)
        blocked = snapshot(True)
        with self.assertRaises(p.Refusal):
            f.admit_frame(blocked, NOW, 100)
        blocked["feed"] = dict(blocksFire=True)
        f.admit_frame(blocked, NOW, 100)  # Existing feed behavior remains.
        with self.assertRaises(p.Refusal):
            p.plan(blocked, "PumpCycle", NOW, 100)

    def test_no_historical_frame_or_synthetic_lease_renewal(self):
        s = snapshot(True)
        with self.assertRaises(p.Refusal):
            p.plan(s, "PumpCycle", NOW+80_000_000, 100)
        with self.assertRaises(p.Refusal):
            p.plan(s, "PumpCycle", NOW, 99)
        s["valid_until_ns"] = NOW+200_000_000
        with self.assertRaises(p.Refusal):
            p.plan(s, "PumpCycle", NOW, 100)

    def test_foreign_or_unknown_block_is_refused(self):
        for mutation in ("native_owner", "shot", "profile", "recovery", "native_phase"):
            with self.subTest(mutation=mutation):
                s = snapshot(True)
                pump = s["shellPump"]["pump"]
                if mutation == "native_owner":
                    pump["native"]["identity"]["owner"]["equipGeneration"] += 1
                elif mutation == "shot":
                    pump["native"]["shot"] += 1
                elif mutation == "profile":
                    s["shellPump"]["profile"]["configurationPath"] = "other"
                elif mutation == "recovery":
                    pump["recovery"] = dict(grantDetailAvailable=False)
                else:
                    pump["native"]["phase"] = 0
                with self.assertRaises(p.Refusal):
                    p.plan(s, "PumpCycle", NOW, 100)

    def test_unrelated_controls_and_mapping_are_refused(self):
        for field in ("trigger", "stickX", "stickY", "held", "touched"):
            s = snapshot()
            s["input"]["hands"][1][field] = 1
            with self.assertRaises(p.Refusal):
                p.plan(s, "ShellPickup", NOW, 100)
        s = snapshot()
        s["mapping"]["anatomicalWristToGrip"][3][0] += .1
        with self.assertRaises(p.Refusal):
            p.plan(s, "ShellPickup", NOW, 100)

    def test_insert_refuses_missing_stale_or_fake_custody(self):
        s = snapshot()
        pickup = p.plan(s, "ShellPickup", NOW, 100)
        held = held_after_pickup(s, pickup)
        with self.assertRaises(p.Refusal):
            p.plan(held, "ShellInsert", NOW+200_000_000, 110)
        with self.assertRaises(p.Refusal):
            p.check_gate(pickup, s, "shell_held", NOW, 100)
        for mutation in ("native", "item", "claim", "reserve", "count", "basis"):
            bad = copy.deepcopy(held)
            sh = bad["shellPump"]["shell"]
            if mutation == "native":
                sh["heldLease"]["allThreeHeld"] = False
            elif mutation == "item":
                sh["item"]["units"] = 2
            elif mutation == "claim":
                bad["shellPump"]["leftClaim"]["token"]["owner"]["space"] += 1
            elif mutation == "reserve":
                sh["reserve"]["loaded"] += 1
            elif mutation == "count":
                sh["lifetimeCounters"]["acquired"] += 1
            else:
                bad["input"]["head"]["position"][0] += .1
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.plan(bad, "ShellInsert", NOW+200_000_000, 110, pickup_plan=pickup)

    def test_rear_gate_needs_actual_evaluated_debt_and_retains_old_lease(self):
        s = snapshot(True)
        result = p.plan(s, "PumpCycle", NOW, 100)
        now = NOW+1_000_000_000
        stroke = s["shellPump"]["profile"]["pump"]["strokeMeters"]
        rear = pump_endpoint(s, stroke, now, 120)
        self.assertTrue(p.check_gate(result, rear, "pump_rear", now, 120)["observed"])
        self.assertLess(rear["shellPump"]["pump"]["held"]["deadlineNs"], now)
        for mutation in ("phase", "gate", "lease", "claim"):
            bad = copy.deepcopy(rear)
            pump = bad["shellPump"]["pump"]
            if mutation == "phase":
                pump["gesture"]["phase"] = 3
            elif mutation == "gate":
                pump["gesture"]["evaluated"] = False
            elif mutation == "lease":
                pump["native"]["held"]["deadlineNs"] = now
            else:
                pump["mechanismClaim"]["token"]["prerequisiteClaim"] += 1
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.check_gate(result, bad, "pump_rear", now, 120)

    def test_ready_gate_requires_new_exact_native_ready_and_support(self):
        s = snapshot(True)
        result = p.plan(s, "PumpCycle", NOW, 100)
        now = NOW+2_000_000_000
        ready = refresh(s, now, 130)
        pump, sp = ready["shellPump"]["pump"], ready["shellPump"]
        cycle = copy.deepcopy(result["baseline"]["pumpLease"])
        release = dict(cycle=cycle, request=600, inputSequence=125, observedNs=now-40_000_000, deadlineNs=now+20_000_000)
        pump.update(lastSettled=dict(release=release, sequence=126, observedNs=now-30_000_000,
            deadlineNs=now+30_000_000, nativeReady=True, unchangedAmmunition=True), acknowledgedNs=now-20_000_000,
            debt=False, blocksFire=False, phase=7, supportHolding=True, supportToken=203)
        pump["native"].update(phase=5, blocksFire=False, held=None)
        sp["leftClaim"] = claim(ready, 0, 2, cycle["item"], dict(id=2, generation=14), 203, 200)
        ready["weapon_actions_blocked"] = False
        self.assertTrue(p.check_gate(result, ready, "pump_ready", now, 130)["observed"])
        for mutation in ("ack", "historical", "shot", "support", "ammo"):
            bad = copy.deepcopy(ready)
            bp = bad["shellPump"]["pump"]
            if mutation == "ack":
                bp["lastSettled"]["nativeReady"] = False
            elif mutation == "historical":
                bp["lastSettled"]["release"]["observedNs"] = NOW-1
            elif mutation == "shot":
                bp["lastSettled"]["release"]["cycle"]["shot"] += 1
            elif mutation == "support":
                bp["supportReturnPending"] = True
            else:
                bad["shellPump"]["shell"]["reserve"]["loaded"] += 1
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.check_gate(result, bad, "pump_ready", now, 130)

    def test_shell_capture_phase_persists_after_single_packet_event(self):
        s = snapshot()
        pickup = p.plan(s, "ShellPickup", NOW, 100)
        held = held_after_pickup(s, pickup)
        result = p.plan(held, "ShellInsert", NOW+200_000_000, 110, pickup_plan=pickup)
        now = NOW+1_000_000_000
        captured = refresh(held, now, 120)
        sp = captured["shellPump"]
        sp["shell"]["geometry"] = dict(available=True, captured=False, seated=False, phase=1,
            owner=copy.deepcopy(sp["input"]["owner"]), item=copy.deepcopy(sp["shell"]["item"]["key"]), cycle=700,
            sourceSequence=119, currentSequence=120, observedNs=now-2_000_000, deadlineNs=now+60_000_000,
            progress=0., alignment=1.)
        self.assertTrue(p.check_gate(result, captured, "shell_captured", now, 120)["observed"])
        sp["shell"]["geometry"]["phase"] = 0
        with self.assertRaises(p.Refusal):
            p.check_gate(result, captured, "shell_captured", now, 120)

    def test_shell_receipt_is_not_final_ammo_or_lifetime_counter(self):
        s = snapshot()
        pickup = p.plan(s, "ShellPickup", NOW, 100)
        held = held_after_pickup(s, pickup)
        result = p.plan(held, "ShellInsert", NOW+200_000_000, 110, pickup_plan=pickup)
        now = NOW+2_000_000_000
        done = refresh(held, now, 140)
        sh = done["shellPump"]["shell"]
        item = copy.deepcopy(sh["item"])
        sh.update(held=False, ammoOwnsHand=False, item=None)
        done["shellPump"]["leftClaim"] = None
        sh["lifetimeCounters"].update(submitted=3, completed=3)
        for counts in (sh["reserve"], sh["heldLease"]):
            counts.update(loaded=4, reserve=16)
        # Native hold still blocks actions while the spent item is cleared;
        # read-only receipt admission must not require an invented held item.
        sh["lastSubmitted"] = dict(request=800, operation=3, submittedNs=now-40_000_000,
            heldLease={**copy.deepcopy(held["shellPump"]["shell"]["heldLease"]),
                       "observedNs": now-50_000_000, "deadlineNs": now+20_000_000},
            reservation=dict(request=800, cycle=700, seat=900, item=item["key"], claim=item["claim"]["token"]))
        sh["lastAcknowledged"] = dict(identity=copy.deepcopy(sh["identity"]), cycle=700, request=800,
            operation=3, status=1, sampleSequence=130, serverInvocation=910,
            observedNs=now-30_000_000, deadlineNs=now+50_000_000, verified=True, acknowledgedNs=now-20_000_000)
        self.assertTrue(p.check_gate(result, done, "shell_inserted", now, 140)["observed"])
        for mutation in ("request", "cycle", "time", "verified"):
            bad = copy.deepcopy(done)
            ack = bad["shellPump"]["shell"]["lastAcknowledged"]
            if mutation == "request":
                ack["request"] += 1
            elif mutation == "cycle":
                ack["cycle"] += 1
            elif mutation == "time":
                ack["acknowledgedNs"] = NOW-1
            else:
                ack["verified"] = False
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.check_gate(result, bad, "shell_inserted", now, 140)

    def test_invalid_profile_raw_and_period_refused(self):
        for mutation in ("axis", "raw", "frame", "full", "period"):
            s = snapshot(mutation in ("axis", "raw"))
            stage, period = "PumpCycle" if mutation in ("axis", "raw") else "ShellPickup", 20_000_000
            if mutation == "axis":
                s["shellPump"]["profile"]["pump"]["axis"] = [1, 1, 1]
            elif mutation == "raw":
                s["shellPump"]["pump"]["raw"]["current"] = False
            elif mutation == "frame":
                s["shellPump"]["shell"]["pouch"]["frame"] = 0
            elif mutation == "full":
                s["shellPump"]["shell"]["reserve"]["loaded"] = 6
            else:
                period = 100_000_000
            with self.subTest(mutation=mutation), self.assertRaises(p.Refusal):
                p.plan(s, stage, NOW, 100, period)


if __name__ == "__main__":
    unittest.main()
