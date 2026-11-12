"""Assembled ordinary-input fixtures; never native or headset acceptance."""
import copy
import importlib.util
import json
import math
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


rf = load("spent_case_transport_fixture", "tests/test_bc2_reload_stimulus.py")
m, xf, g = rf.m, rf.fixtures.xf, rf.fixtures.g
p, f, NOW = g.fixed, g.feed, rf.fixtures.NOW


def snapshot(kind=3, spent=True):
    s = rf.fixtures.complete(xf.snapshot(kind))
    t = s["fixedContainer"]["tracking"]
    t.update(step=1 if kind == 3 and spent else 0 if kind == 3 else 2,
             spentCasePresent=spent, spentCaseEpoch=7 if spent else 0,
             spentCasePoseWeaponMeters=xf.fixture.matrix([.035, -.02, -.3], [41, -22, 13]),
             spentCasePolicy=dict(modOwned=True, outwardAxis=[0, 0, 1], maximumWorldUpDot=-.5,
                                  dwellNs=100_000_000, maxInputGapNs=100_000_000))
    t["blocksFire"] = s["weapon_actions_blocked"] = spent and kind == 3
    return s


def observed(source, row, now, sequence, *, ejected=False):
    s = xf.refresh(source, now, sequence)
    grip = g.controller_pose(row)
    hand = s["input"]["hands"][1]
    hand["grip"] = f.to_pose(grip)
    hand["aim"] = dict(position=row["aimPositionMeters"], orientation=f.ypr_quaternion(row["aimYawPitchRollDegrees"]))
    body, reference = (f.rigid(s["mapping"]["trackingBodyWorldMeters"]), f.from_pose(s["input"]["referenceHead"]))
    right_world = f.multiply(f.multiply(grip, f.inverse(reference)), body)
    w = f.multiply(p.tilt_mapping(source)["weaponFromRight"], right_world)
    s["mapping"]["weaponWorldMeters"] = copy.deepcopy(w)
    x = s["fixedContainer"]
    x["raw"]["weaponWorldMeters"] = copy.deepcopy(w)
    x["raw"]["weaponFromWrist"] = f.multiply(s["mapping"]["rawLeftWristWorldMeters"], f.inverse(w))
    if ejected:
        x["tracking"].update(step=2, spentCasePresent=False,
            spentCaseEjectedNs=x["raw"]["input"]["observedNs"],
            spentCaseEjectionWorldMeters=f.multiply(x["tracking"]["spentCasePoseWeaponMeters"], w))
    return s


class SpentCaseTests(unittest.TestCase):
    def test_measured_case_axis_drives_bounded_right_tilt_without_ammo_commands(self):
        source = snapshot()
        original = copy.deepcopy(source)
        plan = g.build_stimulus("fixed", None, source, NOW, "EjectSpent")
        m.validate_stimulus(plan)
        self.assertEqual(source, original)
        last = observed(source, plan["schedule"][-1], NOW+200_000_000, 120)
        case = f.multiply(last["fixedContainer"]["tracking"]["spentCasePoseWeaponMeters"], last["mapping"]["weaponWorldMeters"])
        self.assertLessEqual(case[2][1], -.6499)
        for a, b in zip(plan["schedule"], plan["schedule"][1:]):
            distance, angle = f.pose_error(g.controller_pose(a), g.controller_pose(b))
            self.assertLess(distance, .00001)
            self.assertLess(angle, plan["policy"]["maxStepRadians"])
        for index, row in enumerate(plan["schedule"]):
            self.assertEqual(m.ordinary_commands(row, index == 0), row["commands"])
            self.assertEqual(row["grip"], 0)
            self.assertFalse(any("trigger" in line or "teleport" in line for line in row["commands"]))

    def test_open_debt_and_initial_empty_choose_distinct_actual_next_steps(self):
        for spent in (True, False):
            source = snapshot(spent=spent)
            source["fixedContainer"]["tracking"]["step"] = 0
            plan = p.plan(source, "Open", NOW, 100)
            held = xf.physical_acquired(source)
            proof = p.check_gate(plan, held, "fixed_acquired", NOW+200_000_000, 110)
            done = xf.physical_completed(held, "Open")
            self.assertEqual(done["fixedContainer"]["tracking"]["step"], 1 if spent else 2)
            self.assertTrue(p.check_gate(plan, done, "fixed_step_complete", NOW+500_000_000, 120, acquired=proof)["observed"])
            wrong = copy.deepcopy(done)
            wrong["fixedContainer"]["tracking"]["step"] = 2 if spent else 1
            with self.assertRaises(p.Refusal):
                p.check_gate(plan, wrong, "fixed_step_complete", NOW+500_000_000, 120, acquired=proof)
        muzzle = snapshot(4, False)
        self.assertEqual(p.plan(muzzle, "Pickup", NOW, 100)["stage"], "Pickup")
        for stage in ("Open", "EjectSpent", "Close"):
            muzzle["fixedContainer"]["tracking"]["step"] = p.STEP[stage]
            with self.assertRaises(p.Refusal):
                p.plan(muzzle, stage, NOW, 100)

    def test_missing_case_publication_never_means_open_or_ejection_success(self):
        for name in ("spentCasePresent", "spentCaseEpoch", "spentCasePoseWeaponMeters", "spentCasePolicy"):
            source = snapshot()
            del source["fixedContainer"]["tracking"][name]
            with self.assertRaisesRegex(g.GeometryRefusal, "missing published"):
                g.build_stimulus("fixed", None, source, NOW, "EjectSpent")
        source = snapshot(spent=False)
        del source["fixedContainer"]["tracking"]["spentCasePresent"]
        with self.assertRaisesRegex(p.Refusal, "missing published"):
            p.plan(source, "Open", NOW, 100)

    def test_only_new_same_case_native_observation_completes_without_ammo_delta(self):
        source = snapshot()
        plan = p.plan(source, "EjectSpent", NOW, 100)
        done = observed(source, plan["schedule"][-1], NOW+300_000_000, 130, ejected=True)
        proof = p.check_gate(plan, done, "fixed_case_ejected", NOW+300_000_000, 130)
        self.assertTrue(proof["caseEjected"])
        self.assertFalse(proof["nativeAcceptance"] or proof["nativeAmmunitionTransaction"])
        mutations = {
            "ack_only": lambda s: s["fixedContainer"]["tracking"].update(step=1, spentCasePresent=True, spentCaseEjectedNs=0, spentCaseEjectionWorldMeters=None),
            "old_case": lambda s: s["fixedContainer"]["tracking"].update(spentCaseEpoch=6),
            "old_raw": lambda s: s["fixedContainer"]["raw"].update(input=copy.deepcopy(source["fixedContainer"]["raw"]["input"])),
            "ammo": lambda s: s["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1, reserve=6),
            "left": lambda s: s["input"]["hands"][0].update(squeeze=1.),
            "event": lambda s: s["fixedContainer"]["tracking"].update(spentCaseEjectedNs=NOW-10_000_000),
            "up": lambda s: s["fixedContainer"]["tracking"].update(spentCaseEjectionWorldMeters=f.identity()),
            "owner": lambda s: s["fixedContainer"]["view"]["binding"]["owner"].update(weapon=99),
            "raw_admission": lambda s: s["fixedContainer"]["raw"].update(admittedAtTick=False),
            "weapon_drift": lambda s: s["mapping"]["weaponWorldMeters"][3].__setitem__(0, 999.),
        }
        for name, change in mutations.items():
            bad = copy.deepcopy(done)
            change(bad)
            with self.subTest(name=name), self.assertRaises(p.Refusal):
                p.check_gate(plan, bad, "fixed_case_ejected", NOW+300_000_000, 130)

    def assembled(self, *, acknowledge_only=False, mutate=None, reload=False, kind=3, spent=True, opened=False):
        """Real entry, geometry, owner/send/ACK, observer and gate dependency path."""
        clock, fixture = rf.Clock(), rf.StimulusTests()
        source = snapshot(kind, spent)
        if reload:
            source["fixedContainer"]["tracking"]["step"] = (1 if opened else 0) if kind == 3 else 2
        with fixture.ack_driver(clock, archive_ns=0) as h:
            d = h.driver
            d.args = SimpleNamespace(family="fixed", stage="Reload" if reload else "EjectSpent",
                                     target="secondary", step_ms=40, pickup_first=False)
            d.geometry, d.mapping, d.profiles = g, None, None
            d.plans, d.boundaries, d.stimulus = [], {}, None
            d.started = clock.now
            d.target_observed = False
            right = f.to_pose(f.from_pose(source["input"]["hands"][1]["grip"]))
            h.state["handR"].update(pos=right["position"], ypr=f.quaternion_ypr(right["orientation"]), followsHead=False)
            d.initial_sim = copy.deepcopy(h.state)
            d.state = copy.deepcopy(h.state)
            old_publish, old_read = m.transport.atomic.side_effect, m.sim_state.side_effect
            sensor = SimpleNamespace(last=0, sequence=100, downward=None, ejected=False,
                                     source=source, plan=None, row=None, released=False, latest=source,
                                     physical_done=False, completion=None)

            def publish(path, raw, **kw):
                old_publish(path, raw, **kw)
                for line in raw.decode().splitlines():
                    if line.startswith("hand r grip pose "):
                        values = list(map(float, line.split()[4:]))
                        h.state["handR"].update(pos=values[:3], ypr=values[3:], followsHead=False)
                lines = raw.decode().splitlines()[:-1]
                if d.stimulus is not None:
                    sensor.row = next((r for r in d.stimulus["schedule"] if r["commands"] == lines), sensor.row)
                sensor.released = lines == ["grip l 0"]
                while not d.observer.inbox.empty():
                    d.observer.inbox.get_nowait()

            def read(path, until, trace=None):
                state = old_read(path, until, trace)
                if clock.now-sensor.last >= 20_000_000:
                    sensor.last, sensor.sequence = clock.now, sensor.sequence+1
                    if d.stimulus is not sensor.plan:
                        sensor.plan, sensor.source = d.stimulus, copy.deepcopy(sensor.latest)
                        sensor.row, sensor.released, sensor.physical_done, sensor.completion = None, False, False, None
                    row = getattr(d, "settled_right_sample", None)
                    if d.pending_command and h.state["cmdSeq"] == d.sequence:
                        row = getattr(d, "pending_right_sample", row)
                    current_source = sensor.source
                    stage = d.stimulus["stage"] if d.stimulus else None
                    if stage in (None, "EjectSpent"):
                        s = xf.refresh(current_source, clock.now, sensor.sequence) if row is None else observed(current_source, row, clock.now, sensor.sequence)
                    else:
                        s = xf.refresh(current_source, clock.now, sensor.sequence)
                        current_row = sensor.row
                        if current_row is not None:
                            if stage in ("Open", "Close") and current_row["grip"]:
                                s = xf.physical_acquired(current_source, clock.now, sensor.sequence)
                                if current_row is d.stimulus["schedule"][-1]:
                                    sensor.physical_done = True
                            elif stage == "Pickup" and current_row["grip"]:
                                s = xf.refresh(xf.held_after_pickup(current_source, d.stimulus["geometryEvidence"], clock.now), clock.now, sensor.sequence)
                            xf.set_local(s, xf.fixture.emitted_local(s, current_row))
                            if stage == "Insert":
                                if current_row["grip"]:
                                    placement = s["fixedContainer"]["profile"]["geometry"][2]["motion"]["placement"]
                                    actual = f.multiply(f.inverse(placement["sourceFromGrip"]), s["fixedContainer"]["raw"]["weaponFromWrist"])
                                    s["fixedContainer"]["tracking"].update(target=actual, carriedSupply=actual)
                                else:
                                    if sensor.completion is None:
                                        s = xf.completed(current_source, d.stimulus["geometryEvidence"], clock.now)
                                        event = s["fixedContainer"]["lastCompletion"]
                                        out, request = event["outcome"], event["outcome"]["request"]
                                        request["sourceProof"].update(inputSequence=sensor.sequence, constructedNs=clock.now-20_000_000)
                                        request["intent"]["observedNs"] = clock.now-25_000_000
                                        request["preparedCommand"]["requestedNs"] = clock.now-20_000_000
                                        out["receipt"]["command"]["requestedNs"] = clock.now-20_000_000
                                        out["receipt"].update(beganNs=clock.now-15_000_000, completedNs=clock.now-10_000_000)
                                        out["resolvedNs"] = clock.now-8_000_000
                                        event["postSnapshot"].update(sequence=sensor.sequence, observedNs=clock.now-5_000_000)
                                        event["acceptedNs"] = clock.now-2_000_000
                                        sensor.completion = copy.deepcopy(event)
                                    else:
                                        s = xf.completed(current_source, d.stimulus["geometryEvidence"], clock.now)
                                    s = xf.refresh(s, clock.now, sensor.sequence)
                                    s["fixedContainer"]["lastCompletion"] = copy.deepcopy(sensor.completion)
                                    s["fixedContainer"]["view"]["snapshot"]["sequence"] = sensor.sequence
                            if sensor.physical_done:
                                t = s["fixedContainer"]["tracking"]
                                t.update(step=(1 if t["spentCasePresent"] else 2) if stage == "Open" else 4,
                                         targetStep=0 if stage == "Open" else 3)
                                t["blocksFire"] = s["weapon_actions_blocked"] = stage == "Open"
                            if sensor.released:
                                x = s["fixedContainer"]
                                x["tracking"].update(hand=None, target=None, grip=False)
                                x["leftClaim"], x["raw"]["grip"] = None, False
                                s["input"]["hands"][0]["squeeze"] = 0.
                                for e in (s["input_evidence"], x["input"], x["raw"]["input"]):
                                    e["released"][0] = True
                            else:
                                s["input"]["hands"][0]["squeeze"] = float(current_row["grip"])
                    case_world = f.multiply(s["fixedContainer"]["tracking"]["spentCasePoseWeaponMeters"], s["mapping"]["weaponWorldMeters"])
                    if stage == "EjectSpent" and case_world[2][1] <= -.5:
                        sensor.downward = sensor.downward or clock.now
                    else:
                        sensor.downward = None
                    if row is not None and sensor.downward and clock.now-sensor.downward >= 100_000_000 and not acknowledge_only:
                        sensor.ejected = True
                    if sensor.ejected and stage == "EjectSpent":
                        s = observed(current_source, row, clock.now, sensor.sequence, ejected=True)
                    s["fixedContainer"]["tracking"]["revision"] = sensor.sequence
                    if mutate is not None:
                        mutate(s, d, sensor)
                    sensor.latest = copy.deepcopy(s)
                    raw = json.dumps(s).encode()
                    d.observer.inbox.put(dict(kind="snapshot", index=sensor.sequence, snapshot=s, raw=raw,
                        requestNs=clock.now-2_000_000, receivedNs=clock.now, deadlineNs=clock.now+m.RECEIVE_NS))
                return state

            with patch.object(m.transport, "atomic", side_effect=publish), patch.object(m, "sim_state", side_effect=read):
                original_deadlines = d.deadline, d.hard_deadline
                error = None
                try:
                    d.run_stimulus()
                except Exception as caught:
                    error = caught
                self.assertEqual((d.deadline, d.hard_deadline), original_deadlines)
                self.assertLess(d.commands, m.MAX_COMMANDS)
                self.assertFalse(d.held_left, str(error))
                self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
            return d, h, error

    def test_shared_reload_dispatches_observed_case_and_muzzle_paths(self):
        for kind, spent, opened, stages in ((3, True, False, ["Open", "EjectSpent", "Pickup", "Insert", "Close"]),
                                    (3, True, True, ["EjectSpent", "Pickup", "Insert", "Close"]),
                                    (3, False, False, ["Open", "Pickup", "Insert", "Close"]),
                                    (4, False, False, ["Pickup", "Insert"])):
            with self.subTest(kind=kind, spent=spent, opened=opened):
                d, h, error = self.assembled(reload=True, kind=kind, spent=spent, opened=opened)
                self.assertIsNone(error, str(error))
                self.assertEqual([item["stage"] for item in d.plans], stages)
                self.assertTrue(all(item["stimulusCompleted"] for item in d.plans))
                self.assertFalse(d.pending_command or d.held_left)
                self.assertLess(d.artifacts.metadata_bytes, m.MAX_BYTES)
                for index in range(1, len(stages)+1):
                    saved = d.artifacts.saved[f"stimulus-{index:02}.json"]
                    self.assertEqual(saved["geometryEvidence"]["scheduleJsonPointer"], "/schedule")
                    self.assertTrue(saved["schedule"])
                reconstructed = copy.deepcopy(saved["schedule"])
                for index, commands in saved["geometryEvidence"]["scheduleCommandOverrides"].items():
                    reconstructed[int(index)]["commands"] = commands
                self.assertEqual(d.stimulus["geometryEvidence"]["schedule"], reconstructed)

    def test_legacy_left_only_reload_refuses_observed_case_debt_before_pickup(self):
        for opened in (False, True):
            source = snapshot()
            source["fixedContainer"]["tracking"]["step"] = 1 if opened else 0
            driver = xf.d.FixedContainerDriver.__new__(xf.d.FixedContainerDriver)
            driver.latest_snapshot = source
            driver.fresh_available = lambda: (source, b"", NOW)
            calls = []
            def stage(name, **kwargs):
                calls.append(name)
                source["fixedContainer"]["tracking"]["step"] = 1
            driver.stage = stage
            with self.assertRaisesRegex(xf.d.transport.REFUSAL, "shared drive_bc2_reload_stimulus"):
                driver.reload()
            self.assertEqual(calls, [] if opened else ["Open"])

    def test_assembled_entry_requires_actual_case_transition_after_command_ack(self):
        d, h, error = self.assembled()
        self.assertIsNone(error, str(error))
        self.assertTrue(h.published)
        self.assertTrue(d.boundaries["fixed_case_ejected"]["gate"]["caseEjected"])
        self.assertFalse(d.boundaries["fixed_case_ejected"]["nativeAcceptance"])
        self.assertTrue(d.plans[0]["stimulusCompleted"])
        failed, ack, error = self.assembled(acknowledge_only=True)
        self.assertIsNotNone(error)
        self.assertIn("fresh typed stimulus boundary absent", str(error))
        self.assertTrue(ack.published)
        self.assertNotIn("fixed_case_ejected", failed.boundaries)
        self.assertNotIn("stimulusCompleted", failed.plans[0])

    def test_assembled_freshness_identity_and_case_loss_stop_further_pose_commands(self):
        faults = {
            "old_raw": lambda s: s["fixedContainer"]["raw"].update(input=copy.deepcopy(snapshot()["fixedContainer"]["raw"]["input"])),
            "raw_not_admitted": lambda s: s["fixedContainer"]["raw"].update(admittedAtTick=False),
            "owner": lambda s: s["fixedContainer"]["raw"].update(ownerMatches=False),
            "missing_case": lambda s: s["fixedContainer"]["tracking"].pop("spentCaseEpoch"),
            "foreign_ammo": lambda s: s["fixedContainer"]["view"]["snapshot"]["counts"].update(reserve=6),
        }
        for name, fault in faults.items():
            def mutate(s, driver, sensor):
                if driver.commands:
                    fault(s)
            with self.subTest(name=name):
                d, h, error = self.assembled(mutate=mutate)
                self.assertIsNotNone(error)
                self.assertEqual(len(h.published), 1)
                self.assertNotIn("fixed_case_ejected", d.boundaries)
                self.assertEqual(d.capture_sequence, 0)

    def test_right_command_allowlist_cannot_authorize_another_stage_or_trigger(self):
        plan = g.build_stimulus("fixed", None, snapshot(), NOW, "EjectSpent")
        row = copy.deepcopy(plan["schedule"][0])
        row["commands"].append("trigger r 1")
        with self.assertRaises(m.Refusal):
            m.ordinary_commands(row, True)
        plan["stage"] = "Open"
        with self.assertRaisesRegex(m.Refusal, "right pose is restricted"):
            m.validate_stimulus(plan)


if __name__ == "__main__":
    unittest.main()
