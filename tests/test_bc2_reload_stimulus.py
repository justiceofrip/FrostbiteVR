"""Portable regressions for ordinary input transport, not native reload proofs."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import queue
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("reload_stimulus", ROOT / "tools/drive_bc2_reload_stimulus.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
fixture_spec = importlib.util.spec_from_file_location("stimulus_geometry_fixtures", ROOT / "tests/test_bc2_reload_stimulus_geometry.py")
fixtures = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixtures)

# Sanitized actual170 xr-sim state shape: its sticks are arrays, never scalars.
CONTROLS = dict.fromkeys(("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR"), False)
CONTROLS.update(trigL=0.0, trigR=0.0, gripL=0.0, gripR=0.0, stickL=[0.0, 0.0], stickR=[0.0, 0.0])


class Clock:
    def __init__(self):
        self.now = 1_000_000_000

    def perf_counter_ns(self):
        return self.now

    def sleep(self, seconds):
        self.now += int(seconds * m.NS)


class Artifacts:
    def __init__(self):
        self.events, self.saved = [], {}

    def event(self, event, cleanup=False):
        self.events.append(event)

    def save(self, name, value, final=False):
        self.saved[name] = value


def sample(at, phase, grip, x):
    pose = [x, 1.3, -.35]
    angles = [0., 0., 0.]
    coordinates = " ".join(f"{v:.9f}" for v in pose + angles)
    return dict(atNs=at, phase=phase, grip=grip, positionMeters=pose, yawPitchRollDegrees=angles,
                commands=["hand l grip pose " + coordinates, "hand l aim pose " + coordinates, "grip l " + str(grip)])


def plan(clock):
    value = dict(schema="bc2vr.reload-stimulus.v1", nativeAcceptance=False, headsetAcceptance=False,
                family="feed", stage="OpenCover", source=dict(nonce="1", validUntilNs=clock.now + m.NS),
                budget=dict(totalNs=10*m.NS, maximumGestureNs=m.NS, maximumCommands=512, gestureStartSample=0),
                durationNs=160_000_000, boundaries=[], finishGrip=0,
                schedule=[sample(0, "neutral", 0, 0.), sample(40_000_000, "acquire", 1, 0.),
                          sample(80_000_000, "move", 1, .1), sample(120_000_000, "endpoint_dwell", 1, .1),
                          sample(160_000_000, "release", 0, .1)])
    value["schedule"][0]["commands"] = m.LEFT_POSE_SETUP + value["schedule"][0]["commands"]
    return value


class ScheduleDriver(m.ReloadStimulus):
    def __init__(self, clock):
        self.clock = clock
        self.args = types.SimpleNamespace(family="feed", stage="OpenCover", target="secondary")
        self.started = clock.now
        self.deadline = self.hard_deadline = clock.now + 10*m.NS
        self.stage_deadline = self.deadline
        self.commands, self.sequence, self.last_write = 0, 0, 0
        self.last_commit = None
        self.plans, self.boundaries, self.sent, self.waits = [], {}, [], []
        self.artifacts = Artifacts()
        self.held_left = self.target_observed = self.observation_incomplete = False
        self.on_send = None

    def guard(self):
        self.bounds()

    def pause_until(self, until):
        self.clock.now = max(self.clock.now, until)
        self.waits.append(until)
        self.guard()

    def send(self, sample=None, *, cleanup=False, release_only=False):
        self.sent.append((copy.deepcopy(sample), self.clock.now, cleanup, release_only))
        self.last_write = self.clock.now
        self.clock.now += 60_000_000  # Real166/159-style delayed frame acknowledgement.
        self.last_commit = self.clock.now
        self.held_left = not (cleanup or release_only) and sample["grip"] == 1
        self.commands += 2 if cleanup or release_only else 4
        self.sequence += 4
        if self.on_send:
            self.on_send(self, sample)

    def fresh(self, *, boundary=None, after_ns=0, pickup=None):
        self.boundaries[boundary["id"]] = {"observed": True, "nativeAcceptance": False}
        return {}, self.clock.now


class StimulusTests(unittest.TestCase):
    def test_actual170_neutral_control_shape_and_owned_grip(self):
        self.assertTrue(m.neutral_controls(CONTROLS))
        held = copy.deepcopy(CONTROLS)
        held["gripL"] = 1.
        self.assertFalse(m.neutral_controls(held))
        self.assertTrue(m.neutral_controls(held, True))

    def test_nonzero_or_unknown_controller_shape_refused(self):
        for key, value in (("stickL", [0., .1]), ("stickR", []), ("stickR", 0),
                           ("gripR", 1.), ("trigL", True), ("a", 0), ("unknown", 0),
                           ("stickL", [0., float("nan")])):
            with self.subTest(key=key, value=value):
                controls = copy.deepcopy(CONTROLS)
                controls[key] = value
                self.assertFalse(m.neutral_controls(controls, True))

    def test_unallowlisted_command_and_nonfinite_pose_refused(self):
        for change in (lambda x: x["commands"].append("trigger r 1"),
                       lambda x: x["positionMeters"].__setitem__(0, float("nan")),
                       lambda x: x.update(grip=True)):
            row = sample(0, "neutral", 0, 0.)
            change(row)
            with self.assertRaises(m.Refusal):
                m.ordinary_commands(row)

    def test_pose_setup_only_first_sample_and_both_allowed_pose_forms(self):
        row = sample(0, "neutral", 0, 0.)
        row["commands"] = m.LEFT_POSE_SETUP + row["commands"]
        self.assertEqual(m.ordinary_commands(row, True), row["commands"])
        with self.assertRaises(m.Refusal):
            m.ordinary_commands(row)
        row["commands"].pop(3)  # Runtime derives aim from grip with zero trim.
        self.assertEqual(m.ordinary_commands(row, True), row["commands"])

    def test_legacy_geometry_is_not_execution_permission(self):
        with self.assertRaises(m.Refusal):
            m.validate_stimulus(dict(schema="bc2vr.feed-simulation-plan.v1", executionAuthorized=False))

    def test_native_acceptance_and_bad_schedule_refused(self):
        clock = Clock()
        for change in (lambda x: x.update(nativeAcceptance=True),
                       lambda x: x["budget"].update(maximumGestureNs=11*m.NS),
                       lambda x: x["schedule"][2].update(atNs=0)):
            value = plan(clock)
            change(value)
            with self.assertRaises(m.Refusal):
                m.validate_stimulus(value)

    def test_endpoint_dwell_starts_at_final_move_ack_no_pose_republication(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        with patch.object(m, "time", clock):
            driver.execute(plan(clock))
        self.assertEqual([s[0]["phase"] for s in driver.sent], ["neutral", "acquire", "move", "release"])
        move, release = driver.sent[2], driver.sent[3]
        self.assertEqual(release[1] - (move[1] + 60_000_000), 80_000_000)
        self.assertFalse(driver.held_left)
        self.assertTrue(driver.plans[-1]["stimulusCompleted"])

    def test_missing_observation_does_not_shorten_held_stimulus(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        driver.on_send = lambda d, s: setattr(d, "observation_incomplete", True)
        with patch.object(m, "time", clock):
            driver.execute(plan(clock))
        self.assertTrue(driver.observation_incomplete)
        self.assertEqual(driver.sent[-1][0]["phase"], "release")
        self.assertFalse(driver.plans[-1]["nativeAcceptance"])

    def test_original_deadline_still_rejects_delayed_gesture(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        value = plan(clock)
        value["budget"]["maximumGestureNs"] = 200_000_000
        with patch.object(m, "time", clock), self.assertRaises(m.Refusal):
            driver.execute(value)
        self.assertNotIn("release", [s[0]["phase"] for s in driver.sent])

    def test_remaining_command_budget_checked_before_next_stimulus(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        driver.commands = 500
        with patch.object(m, "time", clock), self.assertRaises(m.Refusal):
            driver.execute(plan(clock))
        self.assertEqual(driver.sent, [])

    def test_exact_sight_transition_stops_old_geometry_and_releases_only(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        value = plan(clock)
        value.update(family="sight", stage="SightFlip", boundaries=[dict(id="sight_selected", afterSample=3,
            holdGrip=1, timeoutNs=250_000_000, minimumObservationSpanNs=0, requiresFreshSnapshot=True)])
        driver.on_send = lambda d, s: setattr(d, "target_observed", bool(s and s["phase"] == "move"))
        with patch.object(m, "time", clock):
            driver.execute(value)
        self.assertEqual(len(driver.sent), 4)
        self.assertIsNone(driver.sent[-1][0])
        self.assertTrue(driver.sent[-1][3])
        self.assertFalse(driver.held_left)

    def observation_driver(self, mapping_error=False):
        driver = m.ReloadStimulus.__new__(m.ReloadStimulus)
        driver.observer = types.SimpleNamespace(fatal=None, inbox=queue.Queue())
        driver.artifacts, driver.observed = Artifacts(), []
        driver.args = types.SimpleNamespace(family="feed", stage="OpenCover", target="secondary")
        driver.decoded_snapshot_bytes = 0
        driver.observation_incomplete = False
        driver.mapping = dict(valid=True, observed_ns=1)
        def mapping(*args, **kwargs):
            if mapping_error:
                raise m.Refusal("native owner changed")
            return "source"
        driver.geometry = types.SimpleNamespace(source_deadline=lambda *args: 100,
                                                same_mapping=mapping)
        return driver

    def test_actual170_invalid_snapshot_is_gap_not_motion_authority(self):
        driver = self.observation_driver()
        value = dict(valid=False, nonce="2", invalid_reason="selected_configuration_or_rig_missing_changed_or_expired")
        driver.observer.inbox.put(dict(kind="snapshot", index=0, snapshot=value, raw=json.dumps(value).encode(),
                                      requestNs=1, receivedNs=2, deadlineNs=250))
        driver.observations()
        self.assertTrue(driver.observation_incomplete)
        self.assertFalse(driver.observed[0]["current"])

    def test_actual166_style_late_record_keeps_original_timestamp(self):
        driver = self.observation_driver()
        value = dict(valid=True, nonce="2", observed_ns=80)
        driver.observer.inbox.put(dict(kind="snapshot", index=0, snapshot=value, raw=json.dumps(value).encode(),
                                      requestNs=1, receivedNs=120, deadlineNs=250))
        driver.observations()
        self.assertTrue(driver.observation_incomplete)
        self.assertEqual(driver.observed[0]["snapshot"]["observed_ns"], 80)
        self.assertFalse(driver.observed[0]["current"])

    def test_observed_native_owner_drift_stops_input(self):
        driver = self.observation_driver(True)
        value = dict(valid=True, nonce="2", observed_ns=80)
        driver.observer.inbox.put(dict(kind="snapshot", index=0, snapshot=value, raw=json.dumps(value).encode(),
                                      requestNs=1, receivedNs=90, deadlineNs=250))
        with self.assertRaisesRegex(m.Refusal, "owner changed"):
            driver.observations()

    def test_empty_async_inbox_never_waits_for_publication(self):
        driver = self.observation_driver()
        driver.observations()
        self.assertEqual(driver.observed, [])

    def test_competing_command_writer_blocks_cleanup_authority(self):
        driver = m.ReloadStimulus.__new__(m.ReloadStimulus)
        driver.command_path = object()
        driver.command_hash = "wrong"
        driver.unsafe_owner = False
        with patch.object(m.transport, "read_bytes", return_value=b"competing input"), self.assertRaises(m.Refusal):
            driver._acknowledged({}, [], {})
        self.assertTrue(driver.unsafe_owner)

    def test_pending_command_ack_settles_before_grip_only_cleanup(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        driver.pending_command, driver.unsafe_owner = True, False
        driver.pending_batch, driver.pending_before = ["grip l 1", "note test"], {}
        driver.read_state = lambda *args: {}
        driver._acknowledged = lambda *args: True
        driver.save_owner = lambda pending: None
        with patch.object(m, "time", clock):
            result = driver.cleanup()
        self.assertEqual(result, "ordinary owned left-grip release acknowledged")
        self.assertFalse(driver.pending_command)
        self.assertFalse(driver.held_left)
        self.assertEqual(driver.sent, [(None, 1_000_000_000, True, False)])

    def test_no_cleanup_input_after_owner_loss(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        driver.pending_command, driver.unsafe_owner, driver.held_left = False, True, True
        with patch.object(m, "time", clock):
            driver.cleanup()
        self.assertEqual(driver.sent, [])

    def boundary_driver(self, clock, verdicts, advance=10_000_000):
        driver = m.ReloadStimulus.__new__(m.ReloadStimulus)
        driver.deadline = driver.stage_deadline = clock.now + 10*m.NS
        driver.last_write = clock.now
        driver.observed, driver.boundaries = [], {}
        driver.observation_incomplete = False
        driver.stimulus, driver.profiles, driver.artifacts = {}, {}, Artifacts()
        driver.args = types.SimpleNamespace(family="feed", stage="OpenCover", target="secondary")
        class Pending(Exception):
            pass
        calls = []
        def check(stimulus, snapshot, name, now, **kwargs):
            calls.append(snapshot["nonce"])
            verdict = verdicts[min(len(calls)-1, len(verdicts)-1)]
            if verdict == "pending":
                raise Pending("native claim not yet observed")
            if verdict == "refused":
                raise m.Refusal("native owner changed")
            return {"observed": True, "nativeAcceptance": False}
        driver.geometry = types.SimpleNamespace(BoundaryPending=Pending, check_boundary=check,
            admit_boundary=lambda family, profiles, snapshot, now, stage, **kwargs: check(None, snapshot, stage, now),
            source_deadline=lambda snapshot, family: snapshot["deadlineNs"])
        def guard():
            clock.now += advance
            index = len(driver.observed)
            snapshot = dict(valid=True, nonce=str(index+1), observed_ns=clock.now,
                deadlineNs=clock.now+100_000_000,
                input_evidence=dict(observedNs=driver.last_write-1 if index == 0 else clock.now))
            driver.observed.append(dict(snapshot=snapshot, current=True))
        driver.guard = guard
        return driver, calls

    def test_initial_boundary_waits_only_typed_pending_before_any_input(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["pending", "ready"])
        with patch.object(m, "time", clock):
            snapshot, _ = driver.fresh()
        self.assertEqual(calls, ["1", "2"])
        self.assertEqual(snapshot["nonce"], "2")
        self.assertTrue(driver.observation_incomplete)
        event = driver.artifacts.events[0]
        self.assertEqual((event["type"], event["id"]), ("boundary_pending", "stage_admission"))
        self.assertEqual(event["deadlineNs"], 1_000_000_000 + m.RECEIVE_NS)

    def test_initial_boundary_pending_never_renews_budget_or_retries_refusal(self):
        for verdict, message, expected_calls in (("pending", "deadline", ["1", "2"]),
                                                  ("refused", "owner changed", ["1"])):
            with self.subTest(verdict=verdict):
                clock = Clock()
                driver, calls = self.boundary_driver(clock, [verdict], advance=100_000_000)
                with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, message):
                    driver.fresh()
                self.assertEqual(calls, expected_calls)
                self.assertLess(clock.now, 1_400_000_000)

    def test_boundary_skips_precommand_input_then_only_waits_typed_pending(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["pending", "ready"])
        boundary = dict(id="feed_acquired", timeoutNs=250_000_000)
        with patch.object(m, "time", clock):
            driver.fresh(boundary=boundary, after_ns=driver.last_write)
        self.assertEqual(calls, ["2", "3"])
        self.assertTrue(driver.boundaries["feed_acquired"]["observed"])

    def test_boundary_refusal_is_not_retried_as_pending(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["refused", "ready"])
        with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, "owner changed"):
            driver.fresh(boundary=dict(id="feed_acquired", timeoutNs=250_000_000))
        self.assertEqual(calls, ["2"])

    def test_boundary_wait_does_not_restart_its_original_deadline(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["pending"], advance=100_000_000)
        with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, "deadline"):
            driver.fresh(boundary=dict(id="feed_acquired", timeoutNs=250_000_000))
        self.assertEqual(calls, ["2"])
        self.assertTrue(driver.observation_incomplete)
        self.assertLess(clock.now, 1_400_000_000)

    def test_real_geometry_contracts_cross_transport_interface(self):
        for family, source, stage in (
            ("pump", fixtures.complete(fixtures.pf.snapshot(pumping=True)), "PumpCycle"),
            ("fixed", fixtures.complete(fixtures.xf.snapshot(kind=3)), "Open"),
            ("sight", fixtures.complete(fixtures.sf.snapshot()), "SightFlip")):
            with self.subTest(family=family):
                stimulus = fixtures.g.build_stimulus(family, None, source, fixtures.NOW, stage)
                m.validate_stimulus(stimulus)

    def test_held_continuation_reuses_pose_configuration(self):
        source = fixtures.complete(fixtures.pf.snapshot())
        pickup = fixtures.g.build_stimulus("pump", None, source, fixtures.NOW, "ShellPickup")
        held = fixtures.complete(fixtures.pf.held_after_pickup(source, pickup["geometryEvidence"]))
        insertion = fixtures.g.build_stimulus("pump", None, held, fixtures.NOW+200_000_000, "ShellInsert", pickup=pickup)
        m.validate_stimulus(insertion, True)
        bad = copy.deepcopy(insertion)
        bad["schedule"][0]["commands"] = m.LEFT_POSE_SETUP + bad["schedule"][0]["commands"]
        with self.assertRaises(m.Refusal):
            m.validate_stimulus(bad, True)

    @unittest.skipUnless(os.environ.get("BC2_FEED_SIMULATION_PROFILES"), "production feed profile export optional")
    def test_all_feed_profiles_cross_transport_command_budget(self):
        profiles = json.loads(Path(os.environ["BC2_FEED_SIMULATION_PROFILES"]).read_text(encoding="utf-8-sig"))
        for row in profiles["profiles"]:
            for stage in ("OpenCover", "DetachContainer", "SeatFeed", "CloseCover", "SupplyPickup"):
                with self.subTest(asset=row["asset"], stage=stage):
                    source = fixtures.complete(fixtures.ff.synthetic_snapshot(row, stage))
                    stimulus = fixtures.g.build_stimulus("feed", profiles, source, fixtures.NOW, stage)
                    m.validate_stimulus(stimulus)


if __name__ == "__main__":
    unittest.main()
