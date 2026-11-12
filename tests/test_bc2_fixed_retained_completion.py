"""Completed physical endpoints retain custody until ordinary release."""
import copy
import gzip
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


g = load("retained_completion_geometry", ROOT / "tools/bc2_reload_stimulus_geometry.py")
fixtures = load("retained_completion_fixtures", ROOT / "tests/test_bc2_reload_stimulus_geometry.py")
transport = load("retained_completion_transport", ROOT / "tests/test_bc2_reload_stimulus.py")
xf, NOW = fixtures.xf, fixtures.NOW


def retained(acquired, stage, now=NOW+500_000_000):
    value = xf.refresh(acquired, now, 120)
    x, t = value["fixedContainer"], value["fixedContainer"]["tracking"]
    t.update(step=2 if stage == "Open" else 4, targetStep=g.fixed.STEP[stage])
    t["blocksFire"] = value["weapon_actions_blocked"] = stage == "Open"
    x["raw"]["admittedAtTick"] = True
    return value


class RetainedCompletionTests(unittest.TestCase):
    def pipeline(self, stage="Open", motion="Hinge"):
        source = fixtures.complete(xf.snapshot(3, motion))
        if stage == "Close":
            source["fixedContainer"]["tracking"].update(step=3, blocksFire=True)
            source["weapon_actions_blocked"] = True
            source["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1, reserve=6)
        stimulus = g.build_stimulus("fixed", None, source, NOW, stage)
        acquired = xf.physical_acquired(source)
        proof = g.check_boundary(stimulus, acquired, "fixed_acquired", NOW+200_000_000)
        return stimulus, acquired, {"fixed_acquired": proof}, retained(acquired, stage)

    def test_open_and_close_accept_exact_retained_endpoint_for_both_motion_families(self):
        for stage in ("Open", "Close"):
            for motion in ("Hinge", "Linear"):
                with self.subTest(stage=stage, motion=motion):
                    stimulus, _, prior, done = self.pipeline(stage, motion)
                    before = copy.deepcopy((stimulus, prior, done))
                    result = g.check_boundary(stimulus, done, "fixed_step_complete", NOW+500_000_000, prior=prior)
                    self.assertTrue(result["completionOnly"])
                    self.assertFalse(result["nativeAcceptance"])
                    self.assertEqual((stimulus, prior, done), before)

    def test_already_released_completion_remains_supported(self):
        for stage in ("Open", "Close"):
            stimulus, acquired, prior, _ = self.pipeline(stage)
            done = xf.physical_completed(acquired, stage)
            result = g.check_boundary(stimulus, done, "fixed_step_complete", NOW+500_000_000, prior=prior)
            self.assertTrue(result["completionOnly"])

    def test_retained_completion_refuses_changed_custody_and_unfinished_native_step(self):
        faults = {
            "claim": lambda s: s["fixedContainer"]["tracking"]["hand"]["token"].update(id=999),
            "left_claim": lambda s: s["fixedContainer"].update(leftClaim=None),
            "missing_hand": lambda s: s["fixedContainer"]["tracking"].update(hand=None),
            "target": lambda s: s["fixedContainer"]["tracking"].update(target=None),
            "target_step": lambda s: s["fixedContainer"]["tracking"].update(targetStep=2),
            "step": lambda s: s["fixedContainer"]["tracking"].update(step=0),
            "raw": lambda s: s["fixedContainer"]["raw"].update(admittedAtTick=False),
            "raw_grip": lambda s: s["fixedContainer"]["raw"].update(grip=False),
            "grip": lambda s: s["fixedContainer"]["tracking"].update(grip=False),
            "squeeze": lambda s: s["input"]["hands"][0].update(squeeze=0),
            "released": lambda s: s["fixedContainer"]["raw"]["input"]["released"].__setitem__(0, True),
            "claim_expiry": lambda s: s["fixedContainer"]["tracking"]["hand"].update(deadlineNs=NOW+500_000_000),
            "ammo": lambda s: s["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1),
            "pending": lambda s: s["fixedContainer"]["tracking"].update(pending=True),
            "carried": lambda s: s["fixedContainer"]["tracking"].update(carried=True),
            "native_identity": lambda s: s["fixedContainer"]["view"]["identity"].update(serverItem=999),
            "lifetime": lambda s: s["fixedContainer"]["tracking"].update(lifetime=999),
        }
        stimulus, _, prior, done = self.pipeline()
        for label, mutate in faults.items():
            bad = copy.deepcopy(done)
            mutate(bad)
            with self.subTest(fault=label), self.assertRaises(g.GeometryRefusal):
                g.check_boundary(stimulus, bad, "fixed_step_complete", NOW+500_000_000, prior=prior)
        for missing in (None, {}):
            with self.subTest(prior=missing), self.assertRaises(g.GeometryRefusal):
                g.check_boundary(stimulus, done, "fixed_step_complete", NOW+500_000_000, prior=missing)

    def test_close_still_requires_cleared_fire_interlock(self):
        stimulus, _, prior, done = self.pipeline("Close")
        done["fixedContainer"]["tracking"]["blocksFire"] = done["weapon_actions_blocked"] = True
        with self.assertRaisesRegex(g.GeometryRefusal, "closure did not clear"):
            g.check_boundary(stimulus, done, "fixed_step_complete", NOW+500_000_000, prior=prior)

    def test_driver_releases_once_after_retained_completion_and_allows_no_next_pose(self):
        case = transport.StimulusTests()
        original = case.fixed_completion_snapshot
        def observe(driver, boundary, now, sequence):
            value = original(driver, boundary, now, sequence)
            if boundary == "fixed_step_complete":
                x, t = value["fixedContainer"], value["fixedContainer"]["tracking"]
                proof = driver.boundaries["fixed_acquired"]["gate"]
                t["hand"] = dict(token=copy.deepcopy(proof["claimToken"]),
                    inputSequence=x["raw"]["input"]["sequence"], deadlineNs=x["raw"]["input"]["deadlineNs"])
                x["leftClaim"] = copy.deepcopy(t["hand"])
                t["target"] = copy.deepcopy(x["raw"]["weaponFromWrist"])
                t["targetStep"] = g.fixed.STEP[driver.args.stage]
                x["raw"]["admittedAtTick"] = True
            return value
        with patch.object(case, "fixed_completion_snapshot", side_effect=observe), \
             patch.object(transport.fixtures.g, "fixed", g.fixed):
            for stage in ("Open", "Close"):
                with self.subTest(stage=stage), case.completion_driver(transport.Clock(), stage) as h:
                    driver = h.driver
                    driver.run_stimulus()
                    self.assertTrue(driver.boundaries["fixed_step_complete"]["completionOnly"])
                    self.assertTrue(driver.plans[-1]["stimulusCompleted"])
                    self.assertEqual(sum(raw.startswith(b"grip l 0\n") for raw in h.published), 1)
                    self.assertFalse(driver.held_left or driver.pending_command)
                    events = driver.artifacts.events
                    completed = next(i for i, e in enumerate(events) if e["type"] == "fresh_stimulus_boundary"
                                     and e["id"] == "fixed_step_complete")
                    released = next(i for i, e in enumerate(events) if e["type"] == "command_request"
                                    and e["commands"][0] == "grip l 0")
                    self.assertLess(completed, released)
                    with self.assertRaisesRegex(transport.m.Refusal, "completion-only observation"):
                        driver.send(driver.stimulus["schedule"][-1])

    @unittest.skipUnless(os.environ.get("BC2_FIXED_RETAINED_COMPLETION_REPLAY"), "retained actual83/03 observations required")
    def test_actual83_03_completed_open_keeps_original_claim_and_unchanged_ammo(self):
        folder = Path(os.environ["BC2_FIXED_RETAINED_COMPLETION_REPLAY"])
        pins = {
            "stimulus-01.json": "b7457603a487ffae049f3952f06f8cb99be885579f16713ab288923cbbf2de11",
            "observation-008.json.gz": "60e5cb6e10981218fa5bff53aeacff30ae6e61de8c83184f5ac56def4ffe0691",
            "observation-012.json.gz": "64de01df0d8fcdf7543e2f1af121a116a22c58784e0a3e9486cbde36785cfa6a",
        }
        for name, digest in pins.items():
            self.assertEqual(hashlib.sha256((folder / name).read_bytes()).hexdigest(), digest)
        def observed(index):
            return json.loads(gzip.decompress((folder / f"observation-{index:03}.json.gz").read_bytes()))
        stimulus = json.loads((folder / "stimulus-01.json").read_bytes())
        acquired, done = observed(8), observed(12)
        before = copy.deepcopy((stimulus, acquired, done))
        proof = g.check_boundary(stimulus, acquired, "fixed_acquired", 607351155831400)
        self.assertEqual(proof["gate"]["claimToken"]["id"], 5)
        self.assertEqual(done["fixedContainer"]["tracking"]["step"], 2)
        self.assertEqual(done["fixedContainer"]["tracking"]["hand"]["token"], proof["gate"]["claimToken"])
        result = g.check_boundary(stimulus, done, "fixed_step_complete", 607351591568100,
                                  prior={"fixed_acquired": proof})
        self.assertTrue(result["completionOnly"])
        self.assertEqual(done["fixedContainer"]["view"]["snapshot"]["counts"], dict(loaded=0, reserve=7, capacity=1))
        self.assertEqual((stimulus, acquired, done), before)


if __name__ == "__main__":
    unittest.main()
