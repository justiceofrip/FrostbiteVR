"""Deterministic real driver transport/admission; no native/headset acceptance.

Set BC2_FEED_SIMULATION_PROFILES to the production export, then run unittest
discovery. Native state timings below come from feedback76/xr-game04; synthetic
snapshots model only the absent/present publication boundary, not native state.
"""
import copy
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as observation_tests
from test_bc2_feed_driver_observation import Harness, MS, NS, d
from test_bc2_feed_simulation_plan import synthetic_snapshot, add_synthetic_custody, set_wrist, emitted_wrist


# Saved first driver snapshot, last state-12 changes, and all-copy idle return.
FIRST_OBSERVATION_NS = 53305757072100
STATE12_NS = (53306595447200, 53306604553300, 53306593406100)
IDLE_NS = (53308593884700, 53308603635700, 53308597121100)


class FeedArtifactBudgetTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.run = Path(temporary.name)
        paths = patch.object(d, "local_path", side_effect=Path)
        paths.start()
        self.addCleanup(paths.stop)

    def test_only_combined_pickup_attach_uses_admitted_two_mib(self):
        manifest = observation_tests.CaptureAdmissionTests.manifest("extended", 8, 640, 480)
        for stage, pickup in (("AttachContainer", True), ("AttachContainer", False),
                              ("SupplyPickup", False), ("OpenCover", False), (None, False)):
            for approach in (False, True):
                args = SimpleNamespace(stage=stage, pickup_first=pickup, pickup_approach=approach)
                expected = 2 * d.MAX_BYTES if stage == "AttachContainer" and pickup else d.MAX_BYTES
                self.assertEqual(d.feed_artifact_limit(args, manifest), expected)
        args = SimpleNamespace(stage="AttachContainer", pickup_first=True)
        for planned in (manifest["limits"]["planned_bytes"] - 1,
                        manifest["limits"]["planned_bytes"] + 1):
            invalid = copy.deepcopy(manifest)
            invalid["limits"]["planned_bytes"] = planned
            with self.assertRaisesRegex(d.REFUSAL, "capture raw/storage budget"):
                d.feed_artifact_limit(args, invalid)
        self.assertEqual(d.MAX_BYTES, 1024 * 1024)

    def test_actual102_byte_shape_retains_raw_evidence_through_combined_route(self):
        # Actual102/01: these 34 responses totaled 912,258 bytes. Other evidence
        # before the failed write was 107,667 bytes; cleanup/timing/result later
        # added 11,865 bytes. These are synthetic byte payloads, not native proof.
        sizes = [27812, 28295, 28297, 28288, 28301, 29123, 29141, 29129, 29138,
                 27983, 29145, 26275, 26176, 26189, 25055, 26184, 26191, 26160,
                 26156, 26185, 26196, 26191, 25050, 26173, 26185, 26182, 26167,
                 25040, 26152, 26167, 26162, 26167, 26161, 25042]
        self.assertEqual(sum(sizes), 912258)
        plain, combined = d.Artifacts(self.run), d.Artifacts(self.run, 2 * d.MAX_BYTES)
        for store in (plain, combined):
            store.save("measured-other-evidence.bin", b"x" * 107667)
            for index, size in enumerate(sizes, 1):
                store.save(f"snapshot-{index:03}.json", b" " * size)
            self.assertEqual(sum(store.sizes.values()), 1019925)
        with self.assertRaisesRegex(d.REFUSAL, "exceed 1 MiB"):
            plain.save("snapshot-035.json", b" " * max(sizes))
        self.assertFalse((plain.directory / "snapshot-035.json").exists())
        # Budget one maximum-observed response per each of the 26+36 planned
        # samples, plus their request/ACK metadata and all failed diagnostics.
        for index in range(35, 63):
            combined.save(f"snapshot-{index:03}.json", b" " * max(sizes))
            combined.event(dict(type="continued_command_evidence", padding="x" * 1500))
        for suffix in ("failure", "at-timeout", "late"):
            raw = b'{"valid":false,"invalid_reason":"synthetic_expired"}\r\n'
            combined.save(f"snapshot-063-{suffix}.json", raw)
            self.assertEqual((combined.directory / f"snapshot-063-{suffix}.json").read_bytes(), raw)
        for index in range(1, 63):
            size = sizes[index - 1] if index <= len(sizes) else max(sizes)
            self.assertEqual((combined.directory / f"snapshot-{index:03}.json").read_bytes(), b" " * size)
        combined.save("receive-timing-failure.json", b" " * 5450, final=True)
        combined.event(dict(type="ordinary_release_acknowledged"), cleanup=True)
        combined.save("result.json", b" " * 4408, final=True)
        self.assertLess(sum(combined.sizes.values()), 2 * d.MAX_BYTES - 12288)

    def test_exact_normal_cleanup_and_final_limits_refuse_before_write(self):
        for maximum in (d.MAX_BYTES, 2 * d.MAX_BYTES):
            store = d.Artifacts(self.run, maximum)
            normal_limit = maximum - 12288
            # The per-file atomic limit stays 1 MiB even for the combined route.
            store.save("first.bin", b"x" * min(d.MAX_BYTES, normal_limit))
            if normal_limit > d.MAX_BYTES:
                store.save("second.bin", b"x" * (normal_limit - d.MAX_BYTES))
            previous = store.sizes.copy()
            with self.assertRaisesRegex(d.REFUSAL, "artifact budget would exceed"):
                store.save("over-normal.bin", b"x")
            with self.assertRaisesRegex(d.REFUSAL, "artifact budget exhausted"):
                store.event(dict(type="ordinary"))
            self.assertEqual(store.sizes, previous)
            cleanup = dict(padding="x" * (4096 - len(d.json_bytes(dict(padding="")))))
            store.event(cleanup, cleanup=True)
            self.assertEqual(sum(store.sizes.values()), maximum - 8192)
            with self.assertRaisesRegex(d.REFUSAL, "artifact budget exhausted"):
                store.event({}, cleanup=True)
            store.save("result.json", b"x" * 4096, final=True)
            self.assertEqual(sum(store.sizes.values()), maximum - 4096)
            with self.assertRaisesRegex(d.REFUSAL, "artifact budget would exceed"):
                store.save("over-final.bin", b"x", final=True)
            self.assertFalse((store.directory / "over-normal.bin").exists())
            self.assertFalse((store.directory / "over-final.bin").exists())


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

    def test_real_stage_dispatches_opt_in_supply_approach_and_checks_actual_window(self):
        # Real planner + Driver.stage with an explicit command transport stub.
        # Synthetic post-pickup custody is not native acceptance.
        for late in (False, True):
            with self.subTest(late_ack=late):
                h = self.harness(ready_at=FIRST_OBSERVATION_NS)
                v, row = h.driver, self.profiles["profiles"][0]
                v.args = type("Args", (), {"step_ms": 40, "pickup_approach": True})()
                before = synthetic_snapshot(row, "SupplyPickup", now=h.now)
                sent = []
                def pause(due): h.now = max(h.now, due)
                def send(lines, expected=None, source_deadline=None, **kwargs):
                    self.assertEqual(lines, expected["commands"])
                    if source_deadline is not None: self.assertLess(h.now, source_deadline)
                    if expected["phase"] == "supply_press":
                        self.assertLessEqual(v.stage_deadline, h.now + self.profiles["supplyConfig"]["gripApproach"]["windowNs"])
                    sent.append(copy.deepcopy(expected))
                    v.last_write = h.now
                    h.now += 650 * MS if late and expected["phase"] == "supply_press" else 1 * MS
                    v.last_commit = h.now
                def fresh():
                    after = synthetic_snapshot(row, "AttachContainer", now=h.now, sequence=102)
                    after["feed"]["revision"] += 1
                    set_wrist(after, emitted_wrist(before, sent[-1]))
                    add_synthetic_custody(self.profiles, row, after)
                    return after, d.json_bytes(after), h.now
                deadlines = v.deadline, v.hard_deadline
                with patch.object(v, "fresh_stage_ready", return_value=(before,d.json_bytes(before),h.now)), \
                     patch.object(v, "pause_until", pause), patch.object(v, "send", send), \
                     patch.object(v, "fresh", fresh), patch.object(v, "accept_snapshot"):
                    if late:
                        with self.assertRaisesRegex(d.REFUSAL, "original grip-approach window"):
                            v.stage("SupplyPickup")
                        self.assertEqual(sent[-1]["phase"], "supply_press")
                    else:
                        plan = v.stage("SupplyPickup")
                        self.assertEqual(sent, plan["schedule"])
                        self.assertTrue(v.plans[-1]["postPickupHeldSupply"])
                        self.assertLess(v.plans[-1]["actualDurationNs"], 10*NS)
                self.assertEqual((v.deadline,v.hard_deadline), deadlines)

    def test_pickup_approach_rejects_invalid_cli_modes_before_opening_driver(self):
        for mode in (("--capture","unused"),("--stage","OpenCover"),("--stage","AttachContainer")):
            with self.subTest(mode=mode), patch.object(d.Driver,"__init__") as opened:
                self.assertEqual(d.main(["--run","G:/unused","--profiles","G:/unused/profiles.json",
                                         "--pickup-approach",*mode]),2)
                opened.assert_not_called()

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
