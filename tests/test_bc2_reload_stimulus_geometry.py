"""Synthetic geometry/boundary tests; no native or headset success is implied."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


g = load("stimulus_geometry_test", ROOT / "tools/bc2_reload_stimulus_geometry.py")
ff = load("stimulus_feed_fixture", ROOT / "tests/test_bc2_feed_simulation_plan.py")
pf = load("stimulus_pump_fixture", ROOT / "tests/test_bc2_pump_simulation_plan.py")
xf = load("stimulus_fixed_fixture", ROOT / "tests/test_bc2_fixed_container_simulation_plan.py")
sf = load("stimulus_sight_fixture", ROOT / "tests/test_bc2_sight_simulation_plan.py")
NOW = sf.NOW


def complete(s):
    """Add explicitly invented identity fields absent from older math fixtures."""
    s["input"].setdefault("floorRelative", False)
    s["equipment"].setdefault("persistence", 44)
    for k, v in dict(configurationPathPointer=27, selectedSlot=0, soleConfiguredArray=28).items():
        s["selected"].setdefault(k, v)
    for m in s["selected"]["meshes"]:
        for k, v in dict(address=29, typeInfo=30, namePointer=31).items():
            m.setdefault(k, v)
    for h in s["input"]["hands"]:
        for k, v in dict(trigger=0., held=0, stickX=0., stickY=0., active=6, touchActive=0, touched=0).items():
            h.setdefault(k, v)
    s.setdefault("feedObservation", dict(observations="independent_atomics_not_native_proof",
        submit_attempts=0, submit_refusals=0, receipts_accepted=0, gate_refusals=1,
        latest_tick=dict(gate_failed=False, submit_refused=False, pending=False)))
    return s


class StimulusGeometryTests(unittest.TestCase):
    def contract(self, family, snapshot, stage, **kwargs):
        old = copy.deepcopy(snapshot)
        plan = g.build_stimulus(family, None, snapshot, NOW, stage, **kwargs)
        self.assertEqual(snapshot, old)
        self.assertEqual(plan["schema"], g.SCHEMA)
        self.assertNotIn("executionAuthorized", plan)
        self.assertFalse(plan["geometryEvidence"]["executionAuthorized"])
        self.assertTrue(plan["geometryEvidence"]["offlineOnly"])
        self.assertFalse(plan["nativeAcceptance"])
        self.assertNotIn("profiles", plan)
        self.assertLess(len(json.dumps(plan)), 256*1024)
        self.assertLessEqual(plan["budget"]["maximumGestureNs"], 10_000_000_000)
        self.assertFalse(plan["continuesHeldCustody"])
        for index, row in enumerate(plan["schedule"]):
            commands = row["commands"]
            if index == 0:
                self.assertEqual(commands[:2], g.LEFT_POSE_SETUP)
                commands = commands[2:]
            self.assertEqual(len(commands), 2 if family in ("feed", "sight") else 3)
            self.assertTrue(commands[0].startswith("hand l grip pose "))
            if family in ("pump", "fixed"):
                self.assertTrue(commands[1].startswith("hand l aim pose "))
            self.assertIn(commands[-1], ("grip l 0", "grip l 1"))
        return plan

    def test_pump_variants_share_geometry_adapter_and_native_gates(self):
        paths = []
        for variant in range(3):
            s = complete(pf.snapshot(pumping=True, variant=variant))
            plan = self.contract("pump", s, "PumpCycle")
            self.assertEqual([b["id"] for b in plan["boundaries"]], ["pump_acquired", "pump_rear", "pump_ready"])
            paths.append(plan["schedule"])
        self.assertNotEqual(paths[0], paths[1])

    def test_shell_pickup_to_insert_requires_fresh_actual_custody(self):
        s = complete(pf.snapshot())
        pickup = self.contract("pump", s, "ShellPickup")
        with self.assertRaises(g.GeometryRefusal):
            g.build_stimulus("pump", None, s, NOW, "ShellInsert", pickup=pickup)
        held = complete(pf.held_after_pickup(s, pickup["geometryEvidence"]))
        plan = g.build_stimulus("pump", None, held, NOW+200_000_000, "ShellInsert", pickup=pickup)
        self.assertEqual(plan["schedule"][0]["phase"], "continue_held_shell")
        self.assertTrue(plan["continuesHeldCustody"])
        self.assertTrue(plan["schedule"][0]["commands"][0].startswith("hand l grip pose "))
        self.assertIn("shell_captured", [b["id"] for b in plan["boundaries"]])
        self.assertIn("shell_inserted", [b["id"] for b in plan["boundaries"]])

    def test_fixed_signed_hinge_and_linear_use_current_profile(self):
        for motion in ("Hinge", "Linear"):
            s = complete(xf.snapshot(kind=3, motion_kind=motion))
            p = self.contract("fixed", s, "Open")
            self.assertEqual([b["id"] for b in p["boundaries"]], ["fixed_acquired", "fixed_step_complete"])
            self.assertEqual(p["geometryEvidence"]["policy"], s["fixedContainer"]["profile"]["geometry"][0]["motion"]["policy"])

    def test_fixed_pickup_insert_retains_exact_receipt_gate(self):
        s = complete(xf.snapshot())
        pickup = self.contract("fixed", s, "Pickup")
        held = complete(xf.held_after_pickup(s, pickup["geometryEvidence"]))
        insertion = g.build_stimulus("fixed", None, held, NOW+200_000_000, "Insert", pickup=pickup)
        final = complete(xf.completed(held, insertion["geometryEvidence"]))
        out = g.check_boundary(insertion, final, "fixed_inserted", NOW+500_000_000)
        self.assertTrue(out["observed"])
        self.assertFalse(out["nativeAcceptance"])
        changed = copy.deepcopy(final)
        changed["fixedContainer"]["lastCompletion"]["outcome"]["receipt"]["copiesVerified"] = False
        with self.assertRaises(g.GeometryRefusal):
            g.check_boundary(insertion, changed, "fixed_inserted", NOW+500_000_000)

    def test_sight_both_assemblies_and_modes_use_observed_family(self):
        for subtree in (False, True):
            for secondary in (False, True):
                s = complete(sf.snapshot(secondary=secondary, subtree=subtree))
                p = self.contract("sight", s, "SightFlip", target="primary" if secondary else "secondary")
                self.assertEqual(p["stepNs"], 20_000_000)
                self.assertEqual([b["id"] for b in p["boundaries"]], ["sight_contact", "sight_selected"])
                start = p["budget"]["gestureStartSample"]
                self.assertEqual(p["schedule"][start]["phase"], "acquire")
                self.assertGreater(start, 0)

    def test_sight_transition_requires_exact_target_and_inverse_family(self):
        source = complete(sf.snapshot(primary_slot=0))
        after = copy.deepcopy(source)
        after["sight"]["family"].update(selectedSecondary=True, targetWeapon=14, targetSlot=0, targetAction=25)
        after["owner"]["weapon"] = after["equipment"]["weapon"] = 34
        after["owner"]["equipGeneration"] += 1
        after["equipment"]["asset"] = "40mmgl"
        after["selected"]["configurationPath"] = source["sight"]["family"]["secondaryConfiguration"]
        after["selected"]["selectedSlot"] = 2
        self.assertEqual(g.same_mapping(source, after, "sight", "SightFlip"), "target")
        after["sight"]["family"]["targetWeapon"] = 99
        with self.assertRaises(g.GeometryRefusal):
            g.same_mapping(source, after, "sight", "SightFlip")

    def test_expired_or_invalid_observation_never_supplies_boundary(self):
        for family, s, stage in (("sight", complete(sf.snapshot()), "SightFlip"),
                                  ("pump", complete(pf.snapshot()), "ShellPickup"),
                                  ("fixed", complete(xf.snapshot()), "Pickup")):
            with self.subTest(family=family):
                with self.assertRaises(g.GeometryRefusal):
                    g.build_stimulus(family, None, s, s["valid_until_ns"], stage)
                bad = copy.deepcopy(s)
                bad["valid"] = False
                bad["invalid_reason"] = "selected_configuration_or_rig_missing_changed_or_expired"
                with self.assertRaises(g.GeometryRefusal):
                    g.same_mapping(s, bad, family, stage)

    def test_native_source_deadline_is_not_retimed(self):
        s = complete(xf.snapshot())
        s["fixedContainer"]["physicalFamily"] = dict(deadlineNs=NOW+5)
        self.assertEqual(g.source_deadline(s, "fixed"), NOW+5)
        self.assertEqual(s["fixedContainer"]["physicalFamily"]["deadlineNs"], NOW+5)

    def test_owner_and_relative_weapon_drift_refuse(self):
        s = complete(pf.snapshot())
        for field in ("owner", "basis"):
            changed = copy.deepcopy(s)
            if field == "owner":
                changed["owner"]["weapon"] += 1
            else:
                changed["mapping"]["weaponWorldMeters"][3][0] += .1
            with self.assertRaises(g.GeometryRefusal):
                g.same_mapping(s, changed, "pump", "ShellPickup")

    def test_right_packet_metadata_does_not_masquerade_as_control_change(self):
        s = complete(pf.snapshot())
        changed = copy.deepcopy(s)
        changed["input"]["hands"][1]["observedNs"] = NOW+1
        self.assertEqual(g.same_mapping(s, changed, "pump", "ShellPickup"), "source")
        changed["input"]["hands"][1]["squeeze"] = 0
        with self.assertRaises(g.GeometryRefusal):
            g.same_mapping(s, changed, "pump", "ShellPickup")

    def test_pending_native_claim_waits_without_weakening_changed_ammo(self):
        s = complete(xf.snapshot())
        plan = g.build_stimulus("fixed", None, s, NOW, "Pickup")
        later = complete(xf.refresh(s, NOW+20_000_000, 101))
        later["fixedContainer"]["tracking"]["revision"] += 1
        with self.assertRaises(g.BoundaryPending):
            g.check_boundary(plan, later, "fixed_held", NOW+20_000_000)
        later["fixedContainer"]["view"]["snapshot"]["counts"]["reserve"] -= 1
        with self.assertRaises(g.GeometryRefusal) as caught:
            g.check_boundary(plan, later, "fixed_held", NOW+20_000_000)
        self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def test_old_command_text_cannot_inject_another_control(self):
        s = complete(sf.snapshot())
        original = g.sight.plan(s, NOW, s["nonce"])
        original["schedule"][0]["commands"] = ["trigger r 1"]
        with mock.patch.object(g.sight, "plan", return_value=original):
            plan = g.build_stimulus("sight", None, s, NOW, "SightFlip")
        self.assertNotIn("trigger r 1", [c for row in plan["schedule"] for c in row["commands"]])
        self.assertEqual(plan["geometryEvidence"]["schedule"][0]["commands"], ["trigger r 1"])

    def test_unsupported_family_stage_or_absent_profile_refuses(self):
        for family, stage in (("rifle", "Reload"), ("pump", "Reload"), ("fixed", "PumpCycle")):
            with self.assertRaises(g.GeometryRefusal):
                g.build_stimulus(family, None, complete(pf.snapshot()), NOW, stage)
        s = complete(pf.snapshot())
        s["shellPump"]["profile"] = None
        with self.assertRaises(g.GeometryRefusal):
            g.build_stimulus("pump", None, s, NOW, "ShellPickup")


@unittest.skipUnless(os.environ.get("BC2_FEED_SIMULATION_PROFILES"), "production 24-profile export required")
class FeedRegistryStimulusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profiles = json.loads(Path(os.environ["BC2_FEED_SIMULATION_PROFILES"]).read_text(encoding="utf-8-sig"))

    def test_build_reuses_admitted_feed_trajectory_at_requested_step(self):
        snapshot = complete(ff.synthetic_snapshot(self.profiles["profiles"][0]))
        expected = g.feed.plan(self.profiles, snapshot, "OpenCover", NOW, int(snapshot["nonce"]), 40_000_000)
        with mock.patch.object(g.feed, "plan", wraps=g.feed.plan) as trajectory:
            plan = g.build_stimulus("feed", self.profiles, snapshot, NOW, "OpenCover", step_ms=40)
        trajectory.assert_called_once()
        self.assertEqual(plan["geometryEvidence"], expected)
        self.assertEqual(plan["source"]["validUntilNs"],
                         min(g.source_deadline(snapshot, "feed"), expected["sourceValidUntilNs"]))
        self.assertFalse(plan["geometryEvidence"]["executionAuthorized"])
        self.assertTrue(plan["geometryEvidence"]["offlineOnly"])

    def test_absent_feed_waits_only_after_valid_generic_admission(self):
        s = complete(ff.synthetic_snapshot(self.profiles["profiles"][0]))
        s["feed"] = None
        with self.assertRaises(g.BoundaryPending):
            g.admit_boundary("feed", self.profiles, s, NOW, "OpenCover")
        for changed in (dict(s, weapon_actions_blocked=True), {k: v for k, v in s.items() if k != "feed"}):
            with self.assertRaises(g.GeometryRefusal) as caught:
                g.admit_boundary("feed", self.profiles, changed, NOW, "OpenCover")
            self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def test_absent_feed_boundary_keeps_mapping_and_refusal_checks(self):
        row = self.profiles["profiles"][0]
        before = complete(ff.synthetic_snapshot(row))
        plan = g.build_stimulus("feed", self.profiles, before, NOW, "OpenCover")
        later = complete(ff.synthetic_snapshot(row, now=NOW+20_000_000, sequence=101))
        later["feed"] = None
        with self.assertRaises(g.BoundaryPending):
            g.check_boundary(plan, later, "feed_contact", NOW+20_000_000, profiles=self.profiles)
        for field in ("gate_refusals", "submit_refusals", "submit_attempts"):
            changed = copy.deepcopy(later)
            changed["feedObservation"][field] += 1
            with self.assertRaises(g.GeometryRefusal) as caught:
                g.check_boundary(plan, changed, "feed_contact", NOW+20_000_000, profiles=self.profiles)
            self.assertNotIsInstance(caught.exception, g.BoundaryPending)
        changed = copy.deepcopy(later)
        changed["owner"]["weapon"] += 1
        with self.assertRaises(g.GeometryRefusal) as caught:
            g.check_boundary(plan, changed, "feed_contact", NOW+20_000_000, profiles=self.profiles)
        self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def test_absent_feed_after_receipt_is_pending_not_completion(self):
        row = self.profiles["profiles"][0]
        before = complete(ff.synthetic_snapshot(row))
        plan = g.build_stimulus("feed", self.profiles, before, NOW, "OpenCover")
        later = complete(ff.synthetic_snapshot(row, now=NOW+20_000_000, sequence=101))
        later["feed"] = None
        later["feedObservation"]["submit_attempts"] += 1
        later["feedObservation"]["receipts_accepted"] += 1
        prior = {"feed_acquired": {"counters": g._counters(before)}}
        with self.assertRaises(g.BoundaryPending):
            g.check_boundary(plan, later, "feed_released", NOW+20_000_000, profiles=self.profiles, prior=prior)
        later["feedObservation"]["submit_attempts"] += 1
        with self.assertRaises(g.GeometryRefusal) as caught:
            g.check_boundary(plan, later, "feed_released", NOW+20_000_000, profiles=self.profiles, prior=prior)
        self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def released_feed(self):
        row = self.profiles["profiles"][0]
        before = complete(ff.synthetic_snapshot(row, "DetachContainer"))
        plan = g.build_stimulus("feed", self.profiles, before, NOW, "DetachContainer")
        later = complete(ff.synthetic_snapshot(row, "AttachContainer", NOW+20_000_000, sequence=101))
        later["feedObservation"].update(submit_attempts=1, receipts_accepted=1, gate_refusals=2)
        prior = {"feed_acquired": {"counters": g._counters(before), "lifetime": before["feed"]["lifetime"]}}
        return plan, later, prior

    def test_feed_release_records_recovered_historical_gate_delta_without_acceptance(self):
        plan, later, prior = self.released_feed()
        original = copy.deepcopy(later)
        result = g.check_boundary(plan, later, "feed_released", NOW+20_000_000,
            command_written_ns=NOW+1_000_000, profiles=self.profiles, prior=prior)
        self.assertEqual(later, original)
        self.assertEqual(result["unresolvedGateRefusalDelta"], 1)
        self.assertIn("unresolved", result["evidenceLimitations"][0])
        self.assertTrue(result["receiptCounterAdvanced"])
        for field in ("nativeReceiptProof", "nativeAcceptance", "headsetAcceptance"):
            self.assertFalse(result[field])

    def test_feed_release_historical_gate_delta_with_absent_feed_waits_for_fresh_release(self):
        plan, later, prior = self.released_feed()
        absent = copy.deepcopy(later)
        absent["feed"] = None
        absent["weapon_actions_blocked"] = False  # Generic frame admission still applies.
        with self.assertRaises(g.BoundaryPending):
            g.check_boundary(plan, absent, "feed_released", NOW+20_000_000,
                profiles=self.profiles, prior=prior)
        absent["feedObservation"]["gate_refusals"] = 0
        with self.assertRaisesRegex(g.GeometryRefusal, "gate refusal counter regressed"):
            g.check_boundary(plan, absent, "feed_released", NOW+20_000_000,
                profiles=self.profiles, prior=prior)
        result = g.check_boundary(plan, later, "feed_released", NOW+20_000_000,
            command_written_ns=NOW+1_000_000, profiles=self.profiles, prior=prior)
        self.assertEqual(result["unresolvedGateRefusalDelta"], 1)
        self.assertFalse(result["nativeReceiptProof"])

    def test_feed_release_rejects_historical_gate_counter_regression(self):
        plan, later, prior = self.released_feed()
        later["feedObservation"]["gate_refusals"] = 0
        with self.assertRaisesRegex(g.GeometryRefusal, "gate refusal counter regressed"):
            g.check_boundary(plan, later, "feed_released", NOW+20_000_000,
                profiles=self.profiles, prior=prior)

    def test_feed_release_historical_gate_delta_keeps_current_and_submission_refusals(self):
        for field in ("gate_failed", "submit_refused", "submit_refusals"):
            with self.subTest(field=field):
                plan, later, prior = self.released_feed()
                if field == "submit_refusals":
                    later["feedObservation"][field] += 1
                else:
                    later["feedObservation"]["latest_tick"][field] = True
                with self.assertRaises(g.GeometryRefusal) as caught:
                    g.check_boundary(plan, later, "feed_released", NOW+20_000_000,
                        profiles=self.profiles, prior=prior)
                self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def test_feed_release_historical_gate_delta_still_waits_for_transaction_completion(self):
        for field in ("pending", "receipts_accepted"):
            with self.subTest(field=field):
                plan, later, prior = self.released_feed()
                if field == "pending":
                    later["feedObservation"]["latest_tick"][field] = True
                else:
                    later["feedObservation"][field] = 0
                with self.assertRaises(g.BoundaryPending):
                    g.check_boundary(plan, later, "feed_released", NOW+20_000_000,
                        profiles=self.profiles, prior=prior)

    def test_all_24_measured_feed_registrations_use_same_pipeline(self):
        self.assertEqual(len(self.profiles["profiles"]), 24)
        for row in self.profiles["profiles"]:
            for stage in ("OpenCover", "DetachContainer", "SeatFeed", "CloseCover"):
                with self.subTest(asset=row["asset"], stage=stage):
                    s = complete(ff.synthetic_snapshot(row, stage))
                    plan = g.build_stimulus("feed", self.profiles, s, NOW, stage)
                    self.assertEqual(plan["geometryEvidence"]["selection"]["key"], row["key"])
                    self.assertEqual([b["id"] for b in plan["boundaries"]], ["feed_contact", "feed_acquired", "feed_released"])
                    self.assertNotIn("profiles", plan)

    def test_feed_acquisition_needs_new_claim_and_rejects_new_refusal(self):
        row = self.profiles["profiles"][0]
        s = complete(ff.synthetic_snapshot(row))
        plan = g.build_stimulus("feed", self.profiles, s, NOW, "OpenCover")
        later = complete(ff.synthetic_snapshot(row, now=NOW+20_000_000, sequence=101))
        contact = g.check_boundary(plan, later, "feed_contact", NOW+20_000_000, profiles=self.profiles)
        self.assertTrue(contact["stageRevalidated"])
        acquired = complete(ff.synthetic_snapshot(row, now=NOW+40_000_000, sequence=102))
        acquired["feed"]["revision"] += 1
        ff.add_synthetic_feed_claim(row, acquired, "OpenCover")
        result = g.check_boundary(plan, acquired, "feed_acquired", NOW+40_000_000,
            command_written_ns=NOW+25_000_000, prior={"feed_contact": contact}, profiles=self.profiles)
        self.assertIn("claim", result)
        acquired["feedObservation"]["gate_refusals"] += 1
        with self.assertRaises(g.GeometryRefusal):
            g.check_boundary(plan, acquired, "feed_acquired", NOW+40_000_000,
                command_written_ns=NOW+25_000_000, prior={"feed_contact": contact}, profiles=self.profiles)

    def test_feed_contact_rechecks_native_state_and_geometry_before_squeeze(self):
        row = self.profiles["profiles"][0]
        before = complete(ff.synthetic_snapshot(row, "DetachContainer"))
        plan = g.build_stimulus("feed", self.profiles, before, NOW, "DetachContainer")
        later = complete(ff.synthetic_snapshot(row, "DetachContainer", NOW+20_000_000, sequence=101))
        self.assertTrue(g.check_boundary(plan, later, "feed_contact", NOW+20_000_000,
                                        profiles=self.profiles)["stageRevalidated"])
        for field in ("lifetime", "operation", "targetKind", "blocksFire", "coverRadians", "carriedPartsMask", "supply", "pouch"):
            with self.subTest(field=field):
                changed = copy.deepcopy(later)
                if field == "supply":
                    changed["feed"][field]["resource"]["generation"] += 1
                elif field == "pouch":
                    changed["feed"]["weaponFromPouch"][3][0] += .02
                elif field == "blocksFire":
                    changed["feed"][field] = False
                else:
                    changed["feed"][field] += 1
                with self.assertRaises(g.GeometryRefusal) as caught:
                    g.check_boundary(plan, changed, "feed_contact", NOW+20_000_000, profiles=self.profiles)
                self.assertNotIsInstance(caught.exception, g.BoundaryPending)

    def test_feed_admission_requires_retained_closed_stage(self):
        row = self.profiles["profiles"][0]
        missing = complete(ff.synthetic_snapshot(row))
        missing["feed"] = None
        with self.assertRaises(g.GeometryRefusal):
            g.admit_boundary("feed", self.profiles, missing, NOW, "OpenCover")
        open_cover = complete(ff.synthetic_snapshot(row, "CloseCover"))
        open_cover["feed"]["operation"] = 0
        with self.assertRaises(g.GeometryRefusal):
            g.admit_boundary("feed", self.profiles, open_cover, NOW, "OpenCover")

    def test_all24_pickup_to_attach_requires_fresh_relocated_custody(self):
        for row in self.profiles["profiles"]:
            with self.subTest(asset=row["asset"]):
                before = complete(ff.synthetic_snapshot(row, "SupplyPickup"))
                # Preserve arbitrary rotations but use reachable body/weapon
                # spacing; the original algebra fixture intentionally spans6m.
                before["mapping"]["trackingBodyWorldMeters"][3][:3] = before["mapping"]["weaponWorldMeters"][3][:3]
                ff.set_wrist(before, before["mapping"]["rawLeftWristWorldMeters"])
                pickup = g.build_stimulus("feed", self.profiles, before, NOW, "SupplyPickup")
                after = complete(ff.synthetic_snapshot(row, "AttachContainer", NOW+100_000_000, sequence=102))
                after["mapping"]["trackingBodyWorldMeters"] = copy.deepcopy(before["mapping"]["trackingBodyWorldMeters"])
                after["feed"]["revision"] += 2
                wrist = ff.emitted_wrist(before, pickup["schedule"][-1])
                ff.set_wrist(after, wrist)
                source = g.feed.multiply(g.feed.inverse(row["geometry"]["poses"]["PouchWithdrawn"]["sourceFromWrist"]),
                    g.feed.multiply(wrist, g.feed.inverse(after["mapping"]["weaponWorldMeters"])))
                after["feed"]["pouchCarryWeaponWorldMeters"] = after["mapping"]["weaponWorldMeters"]
                after["feed"]["weaponFromPouch"] = source
                ff.add_synthetic_custody(self.profiles, row, after)
                with self.assertRaises(g.GeometryRefusal):
                    g.build_stimulus("feed", self.profiles, after, NOW+100_000_000, "AttachContainer")
                attached = g.build_stimulus("feed", self.profiles, after, NOW+100_000_000, "AttachContainer", pickup=pickup)
                self.assertEqual(attached["schedule"][0]["phase"], "continue_held")
                self.assertTrue(attached["continuesHeldCustody"])
                self.assertTrue(attached["schedule"][0]["commands"][0].startswith("hand l grip pose "))
                self.assertEqual(attached["finishGrip"], 0)


if __name__ == "__main__":
    unittest.main()
