"""Deterministic real driver transport/admission; no native/headset acceptance.

Set BC2_FEED_SIMULATION_PROFILES to the production export, then run unittest
discovery. Native state timings below come from feedback76/xr-game04; synthetic
snapshots model only the absent/present publication boundary, not native state.
"""
import copy
import os
import unittest
from unittest.mock import patch

from test_bc2_feed_driver_observation import Harness, MS, NS, d
from test_bc2_feed_simulation_plan import synthetic_snapshot


# Saved first driver snapshot, last state-12 changes, and all-copy idle return.
FIRST_OBSERVATION_NS = 53305757072100
STATE12_NS = (53306595447200, 53306604553300, 53306593406100)
IDLE_NS = (53308593884700, 53308603635700, 53308597121100)


class FeedStageReadinessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get("BC2_FEED_SIMULATION_PROFILES")
        if not path:
            raise RuntimeError("provide BC2_FEED_SIMULATION_PROFILES; no substitute registry fixture")
        cls.profiles, _ = d.planner.load_json(path, 8 * 1024 * 1024)
        if len(cls.profiles["profiles"]) != 24:
            raise RuntimeError("need all 24 exported production registrations")

    def harness(self, row=None, ready_at=max(IDLE_NS), mutate=None):
        h = Harness(self)
        v = h.driver
        row = row or self.profiles["profiles"][0]
        h.now = FIRST_OBSERVATION_NS
        h.response_after = 20 * MS
        v.profiles = self.profiles
        v.reference, v.latest_snapshot = None, None
        v.pending_nonce, v.monitor, v.motion_started = None, False, None
        v.held_left = v.held_right = v.held_trigger = False
        v.held_buttons = set()
        v.last_request = h.now - d.SNAPSHOT_PERIOD
        v.last_write, v.last_commit = 0, None
        v.deadline = v.stage_deadline = h.now + 9700 * MS
        v.hard_deadline = h.now + 10 * NS
        v.plans = []
        h.state["controls"] = dict(gripL=0.)
        v.state = copy.deepcopy(h.state)
        v.initial_sim = copy.deepcopy(h.state)
        h.responses_seen = []
        publish = h.publish

        def response(transport):
            now = transport["observed_ns"]
            sequence = 100 + (now - FIRST_OBSERVATION_NS) // (16 * MS)
            snapshot = synthetic_snapshot(row, now=now, sequence=sequence)
            snapshot["input"]["floorRelative"] = True
            snapshot["nonce"] = transport["nonce"]
            snapshot["selected"].update(configurationPathPointer=31, selectedSlot=1, soleConfiguredArray=32)
            for hand in snapshot["input"]["hands"]:
                hand.update(trigger=0., held=0, stickX=0., stickY=0.)
                hand["aim"] = copy.deepcopy(hand["grip"])
            if ready_at is None or now < ready_at:
                snapshot["feed"] = None
            if mutate:
                mutate(snapshot, len(h.responses_seen))
            h.responses_seen.append(copy.deepcopy(snapshot))
            publish(snapshot)

        h.publish = response
        return h

    def assert_command_free(self, h, original_deadlines):
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.commands, 0)
        self.assertEqual(h.driver.last_write, 0)
        self.assertEqual((h.driver.deadline, h.driver.stage_deadline), original_deadlines)

    def test_all24_wait_through_actual_state12_timing_then_plan_from_fresh_neutral(self):
        for row in self.profiles["profiles"]:
            with self.subTest(asset=row["asset"], key=row["key"]):
                h = self.harness(row)
                v = h.driver
                deadlines = v.deadline, v.stage_deadline
                snapshot, raw, evaluated = v.fresh_stage_ready("OpenCover")
                self.assertGreaterEqual(h.now, max(IDLE_NS))
                self.assertLess(h.now - max(IDLE_NS), d.SNAPSHOT_PERIOD + 20 * MS)
                self.assertTrue(any(min(STATE12_NS) <= s["observed_ns"] < max(IDLE_NS)
                                    and s["feed"] is None for s in h.responses_seen))
                self.assertTrue(all(s["feed"] is None for s in h.responses_seen[:-1]))
                self.assertEqual(snapshot["feed"]["registrationKey"], row["key"])
                self.assertIsNone(snapshot["feed"]["mechanismClaim"])
                self.assertTrue(snapshot["feed"]["input"]["released"][0])
                self.assertLess(evaluated, snapshot["feed"]["input"]["deadlineNs"])
                self.assertEqual(raw, d.json_bytes(snapshot))
                plan = d.planner.plan(self.profiles, snapshot, "OpenCover", evaluated, int(snapshot["nonce"]), 40 * MS)
                self.assertEqual(plan["sourceFeedLifetime"], snapshot["feed"]["lifetime"])
                self.assertEqual(plan["sourceInputSequence"], snapshot["input"]["generation"])
                self.assertEqual(plan["sourceValidUntilNs"], snapshot["valid_until_ns"])
                requests = [value["requestStartedNs"] for name, value in v.artifacts.saved.items()
                            if name.endswith("-timing.json")]
                self.assertTrue(all(b - a >= d.SNAPSHOT_PERIOD for a, b in zip(requests, requests[1:])))
                self.assert_command_free(h, deadlines)

    def test_stage_cannot_plan_or_publish_before_ready(self):
        h = self.harness()
        v = h.driver
        v.args = type("Args", (), {"step_ms": 40})()
        real_plan = d.planner.plan

        def checked_plan(*args, **kwargs):
            self.assertGreaterEqual(h.now, max(IDLE_NS))
            self.assertIsNotNone(args[1]["feed"])
            return real_plan(*args, **kwargs)

        class StopBeforeCommand(Exception):
            pass

        def stop(*args, **kwargs):
            self.assertGreaterEqual(h.now, max(IDLE_NS))
            raise StopBeforeCommand()

        with patch.object(d.planner, "plan", checked_plan), patch.object(v, "send", stop):
            with self.assertRaises(StopBeforeCommand):
                v.stage("OpenCover")
        self.assertEqual(h.command_writes, [])
        self.assertIsNotNone(v.plans[0]["nonce"])

    def test_perpetual_nonidle_uses_existing_global_deadline_without_commands(self):
        h = self.harness(ready_at=None)
        v = h.driver
        deadlines = v.deadline, v.stage_deadline
        with self.assertRaisesRegex(d.REFUSAL, "ten-second driver deadline"):
            v.fresh_stage_ready("OpenCover")
        self.assertGreaterEqual(h.now, v.deadline)
        self.assertLess(h.now, v.hard_deadline)
        self.assert_command_free(h, deadlines)

    def test_changed_owner_is_refused_immediately_not_waited_into_readiness(self):
        def changed(snapshot, index):
            if index:
                snapshot["owner"]["player"] += 1
        h = self.harness(ready_at=None, mutate=changed)
        deadlines = h.driver.deadline, h.driver.stage_deadline
        with self.assertRaisesRegex(d.REFUSAL, "native owner/equipment/rig changed"):
            h.driver.fresh_stage_ready("OpenCover")
        self.assertLess(h.now - FIRST_OBSERVATION_NS, 200 * MS)
        self.assert_command_free(h, deadlines)

    def test_unowned_or_held_input_and_left_pose_drift_cannot_wait(self):
        for kind in ("held", "left_pose", "simulator_writer"):
            with self.subTest(kind=kind):
                def changed(snapshot, index):
                    if not index:
                        return
                    if kind == "held":
                        snapshot["input_evidence"]["released"][0] = False
                        snapshot["input"]["hands"][0]["squeeze"] = 1.
                    elif kind == "left_pose":
                        snapshot["input"]["hands"][0]["grip"]["position"][0] += .01
                    else:
                        h.state["cmdSeq"] += 1
                h = self.harness(ready_at=None, mutate=changed)
                deadlines = h.driver.deadline, h.driver.stage_deadline
                with self.assertRaises(d.REFUSAL):
                    h.driver.fresh_stage_ready("OpenCover")
                self.assert_command_free(h, deadlines)

    def test_stale_or_wrong_registration_ready_publication_cannot_unblock(self):
        for kind in ("stale", "registration", "claimed"):
            with self.subTest(kind=kind):
                def changed(snapshot, index):
                    if snapshot["feed"] is None:
                        return
                    if kind == "stale":
                        snapshot["feed"]["input"]["deadlineNs"] = FIRST_OBSERVATION_NS
                    elif kind == "registration":
                        snapshot["feed"]["registrationKey"] = 1
                    else:
                        snapshot["feed"]["mechanismClaim"] = {"syntheticConflictingClaim": True}
                h = self.harness(ready_at=FIRST_OBSERVATION_NS + 300 * MS, mutate=changed)
                deadlines = h.driver.deadline, h.driver.stage_deadline
                with self.assertRaises(d.REFUSAL):
                    h.driver.fresh_stage_ready("OpenCover")
                self.assert_command_free(h, deadlines)


if __name__ == "__main__":
    unittest.main()
