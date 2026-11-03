"""Portable regressions for ordinary input transport, not native reload proofs."""
from contextlib import ExitStack, contextmanager
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import queue
import shutil
import tempfile
import types
import unittest
from unittest.mock import Mock, patch
import uuid

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

    def time_ns(self):
        return 1_793_720_000_000_000_000 + self.now

    def sleep(self, seconds):
        self.now += int(seconds * m.NS)


class Artifacts(m.DeferredArtifacts):
    def __init__(self):
        self.events, self.saved, self.persisted = [], {}, {}
        sink = types.SimpleNamespace(directory=Path("offline-artifacts"),
            save=lambda name, value, final=False: self.persisted.update({name: value}),
            event=lambda value, cleanup=False: None)
        super().__init__(sink)

    def event(self, event, cleanup=False):
        super().event(event, cleanup)
        self.events.append(event)

    def save(self, name, value, final=False):
        super().save(name, value, final)
        self.saved[name] = value

    def snapshot(self, name, raw):
        super().snapshot(name, raw)
        self.saved[name] = raw


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
    @unittest.skipUnless(os.name == "nt", "startup receipts require local Windows drive paths")
    def test_real_constructor_and_main_reach_fresh_boundary_without_input(self):
        # Keep construction, dispatch, receipts, ownership and fresh() real.
        # Only the process/Win32 and observer boundaries are offline substitutes.
        clock = Clock()
        with ExitStack() as cleanup:
            # Inherit scratch ACLs; Python's Windows mode-0700 temp directories
            # can exclude the restricted token used by the installed runtime.
            scratch = Path(tempfile.gettempdir()).resolve()
            run = (scratch / ("bc2-stimulus-startup-" + uuid.uuid4().hex)).resolve()
            run.mkdir()
            def remove_fixture():
                self.assertEqual(run.resolve().parent, scratch)
                shutil.rmtree(run)
            cleanup.callback(remove_fixture)
            folder, sim = run / "session", run / "sim"
            folder.mkdir()
            sim.mkdir()

            def save(path, value):
                path.write_bytes(m.transport.json_bytes(value))

            def pin(name):
                path = run / name
                raw = b"offline startup fixture; never executable\n"
                path.write_bytes(raw)
                return dict(path=str(path), bytes=len(raw), sha256=m.transport.digest(raw))

            pins = {key: pin(key) for key in ("runtime_manifest", "runtime_library", "host_executable")}
            host = dict(pid=123, path=pins["host_executable"]["path"], start_utc="2026-11-02T00:00:00Z")
            supervisor = dict(pid=456)
            identity = dict(source_sha256="offline-source", version="offline")
            command = b"note offline settled startup\n"
            command_path = sim / "command.txt"
            command_path.write_bytes(command)
            state = dict(pid=host["pid"], runtime="xr-sim", errors=0, lastCmdError="",
                sessionRunning=True, actionsAttached=True, sessionState="FOCUSED", paceMode="free",
                head=dict(valid=True), handL=dict(valid=True), handR=dict(valid=True),
                captureSeq=0, cmdSeq=1, lastCmd="note offline settled startup", controls=copy.deepcopy(CONTROLS))
            save(sim / "state.json", state)
            save(run / "run-manifest.json", dict(schema="fvr.xr-game-run.v1", output_directory=str(run),
                capture_plan=dict(maximum_compositions=0), **identity, **pins))
            save(run / "session-link.json", dict(root_commands_ready=True, host=host, supervisor=supervisor,
                folder=str(folder), sim_state=str(sim / "state.json"), command=str(command_path),
                session=str(folder / "session.json"), options=str(folder / "options.json"),
                stop_request=str(folder / "stop.request"), startup_receipt=str(run / "sim-startup.json"),
                mechanism_snapshots=dict(requested=True)))
            save(run / "sim-startup.json", dict(root_commands_ready=True, status="ready", host=host,
                target_sequence=state["cmdSeq"], command_file=dict(sha256=m.transport.digest(command)),
                settled_state=dict(lastCmd=state["lastCmd"])))
            save(folder / "session.json", dict(host=host, supervisor=supervisor, state="running", **identity))
            save(folder / "options.json", dict(activate_window=False,
                xr_manifest=pins["runtime_manifest"]["path"], **identity))
            (folder / "mechanism-snapshot.enable").write_bytes(b"1\n")
            profiles = run / "profiles.json"
            save(profiles, dict(profiles=[]))
            owner = Mock()
            owner.api.CreateFileW.return_value = 1
            observer = types.SimpleNamespace(fatal=None, inbox=queue.Queue(), close=Mock(return_value=True))
            stdout = io.StringIO()
            with patch.object(m, "time", clock), \
                    patch.object(m.transport, "WindowsOwner", return_value=owner) as owner_factory, \
                    patch.object(m, "sim_state", side_effect=lambda path, deadline: m.transport.read_json(path)[0]), \
                    patch.object(m, "ObservationWorker", return_value=observer) as observer_factory, \
                    patch.object(m.ctypes, "WinDLL", side_effect=AssertionError("unexpected native access")), \
                    patch("sys.stdout", stdout):
                code = m.main(["--run", str(run), "--profiles", str(profiles), "--family", "feed",
                    "--stage", "OpenCover", "--root-authorized-ordinary-input"])
            result = json.loads(stdout.getvalue())
            self.assertEqual(code, 2)
            self.assertEqual(result["error"], "fresh typed stimulus boundary absent within original budget")
            self.assertEqual(result["status"], "refused")
            self.assertEqual(result["durationNs"], m.RECEIVE_NS)
            self.assertEqual(result["commands"], 0)
            self.assertEqual(result["plans"], [])
            self.assertEqual(result["boundaries"], {})
            self.assertEqual(result["pendingBoundary"], dict(family="feed", stage="OpenCover",
                id="stage_admission", startedNs=1_000_000_000, deadlineNs=1_000_000_000+m.RECEIVE_NS,
                lastPending=None))
            self.assertTrue(result["observationIncomplete"])
            self.assertTrue(result["observerStopped"])
            for key in ("pendingCommand", "leftGripHeld", "stimulusCompleted", "boundaryChecksPassed",
                        "nativeAcceptance", "headsetAcceptance"):
                self.assertFalse(result[key], key)
            self.assertEqual(result["release"], "not attempted: no settled owned held left grip")
            self.assertEqual(command_path.read_bytes(), command)
            for path in (sim / "ack.txt", run / "live-driver-owner.json",
                         folder / "mechanism-snapshot-request.txt"):
                self.assertFalse(path.exists(), path)
            owner_factory.assert_called_once_with(host)
            self.assertEqual(owner.api.CreateFileW.call_args.args[0], str(run / "live-driver.lock"))
            self.assertGreater(owner.check.call_count, 1)
            owner.api.CloseHandle.assert_called_once_with(1)
            owner.close.assert_called_once_with()
            observer_factory.assert_called_once_with(folder, clock.now - m.RECEIVE_NS + 10*m.NS - 400_000_000)
            observer.close.assert_called_once_with()
            artifacts = Path(result["artifactDirectory"])
            self.assertEqual(json.loads((artifacts / "result.json").read_text()), result)
            self.assertEqual({path.name for path in artifacts.iterdir()},
                             {"invocation.json", "profile-source.json", "initial-state.json", "result.json"})

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

    def test_stimulus_serialization_waits_until_all_input_decisions_end(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        value = plan(clock)
        original_deadline = value["source"]["validUntilNs"]
        driver.artifacts.sink.save = lambda *args: clock.sleep(1)
        with patch.object(m, "time", clock):
            driver.execute(value)
            self.assertLess(clock.now, original_deadline)
            self.assertTrue(driver.sent)
            driver.artifacts.flush()
            self.assertGreater(clock.now, original_deadline)
        self.assertEqual(value["source"]["validUntilNs"], original_deadline)

    def test_delayed_prepublication_state_read_cannot_publish_expired_mapping(self):
        clock = Clock()
        driver = ScheduleDriver(clock)
        driver.first_sample_pending = True
        driver.initial_mapping_deadline = clock.now + 5_000_000
        driver.pause_until = lambda until: None
        driver.read_state = lambda *args, **kwargs: clock.sleep(.010)
        driver.ownership, driver.save_owner = Mock(), Mock()
        driver.command_path = object()
        with patch.object(m, "time", clock), patch.object(m.transport, "atomic") as publish, \
                self.assertRaisesRegex(m.Refusal, "initial mapping expired before first publication"):
            m.ReloadStimulus.send(driver, plan(clock)["schedule"][0])
        publish.assert_not_called()
        driver.save_owner.assert_not_called()
        self.assertEqual(driver.commands, 0)
        self.assertEqual(driver.artifacts.events, [])

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

    def queue_observations(self, driver, clock, count=8, lease_ns=100_000_000):
        for index in range(count):
            snapshot = dict(valid=True, nonce=str(index+1), observed_ns=clock.now,
                validUntilNs=clock.now+lease_ns, input_evidence=dict(observedNs=clock.now))
            driver.observer.inbox.put(dict(kind="snapshot", index=index, snapshot=snapshot,
                raw=json.dumps(snapshot).encode(), requestNs=clock.now-10_000_000,
                receivedNs=clock.now, ownershipCheckedNs=clock.now, requestOwned=True,
                deadlineNs=clock.now+m.RECEIVE_NS))

    def test_all_families_admit_fresh_queued_boundary_without_debug_serialization(self):
        cases = (("feed", "DetachContainer", "feed_released"), ("pump", "PumpCycle", "pump_complete"),
                 ("fixed", "Open", "fixed_complete"), ("sight", "SightFlip", "sight_selected"))
        for family, stage, boundary_id in cases:
            for prepare in (False, True):
                with self.subTest(family=family, prepare=prepare):
                    clock = Clock()
                    with self.ack_driver(clock, archive_ns=m.NS) as h:
                        d = h.driver
                        d.args.family, d.args.stage, d.args.step_ms = family, stage, None
                        d.boundaries, d.stimulus, d.profiles = {}, {}, {}
                        d.last_commit = clock.now
                        d.geometry.BoundaryPending = fixtures.g.BoundaryPending
                        d.geometry.source_deadline = lambda s, f: s["validUntilNs"]
                        d.geometry.same_mapping = lambda *a, **k: clock.sleep(.001)
                        d.geometry.check_boundary = lambda *a, **k: dict(observed=True, nativeAcceptance=False)
                        d.geometry.build_stimulus = lambda *a, **k: plan(clock)
                        self.queue_observations(d, clock, lease_ns=20_000_000)
                        before = clock.now
                        with patch.object(m.gzip, "compress", side_effect=AssertionError("live compression")), \
                                patch.object(m.transport, "json_bytes", wraps=m.transport.json_bytes) as serialize:
                            if prepare:
                                result = d.fresh(prepare=True)
                            else:
                                result = d.fresh(boundary=dict(id=boundary_id, timeoutNs=m.RECEIVE_NS))
                            # The only JSON serialization is the fixture's session read.
                            self.assertTrue(all(call.args[0].get("state") == "running" for call in serialize.call_args_list))
                        self.assertEqual(result[0]["nonce"], "8")
                        self.assertIsNone(d.pending_boundary)
                        self.assertEqual(clock.now, before+8_000_000)
                        self.assertEqual(d.artifacts.persisted, {})
                        self.assertEqual(len(d.observed), 8)
                        self.assertEqual(len(h.published), 0)
                        d.artifacts.flush()
                        self.assertGreater(clock.now, result[0]["validUntilNs"])
                        self.assertEqual(len(d.artifacts.persisted), 8)

    def test_slow_queued_validation_retains_all_raw_without_late_boundary_or_retry(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            d = h.driver
            d.boundaries, d.stimulus, d.profiles = {}, {}, {}
            d.last_commit = clock.now
            d.geometry.source_deadline = lambda s, f: s["validUntilNs"]
            d.geometry.same_mapping = lambda *a, **k: clock.sleep(.3)
            d.geometry.check_boundary = Mock()
            self.queue_observations(d, clock)
            with self.assertRaisesRegex(m.ReadDeadline, "original observation validation deadline"):
                d.fresh(boundary=dict(id="feed_released", timeoutNs=m.RECEIVE_NS))
            self.assertEqual(d.boundaries, {})
            d.geometry.check_boundary.assert_not_called()
            self.assertEqual(len(d.artifacts.saved), 8)
            self.assertTrue(d.observation_incomplete)
            self.assertTrue(d.observer.inbox.empty())
            failure = next(e for e in d.artifacts.events if e["type"] == "guard_failure")
            self.assertEqual(failure["phase"], "observation validation")
            self.assertEqual(failure["deadlineNs"], 1_000_000_000+m.RECEIVE_NS)
            with self.assertRaisesRegex(m.Refusal, "previous native observation failure"):
                d.send(sample(0, "move", 1, .1))
            self.assertEqual(h.published, [])
            d.artifacts.flush()
            self.assertEqual(len(d.artifacts.persisted), 8)

    def test_boundary_deadline_reaches_state_and_ownership_reads_with_phase(self):
        for phase in ("session/simulator state", "command ownership"):
            with self.subTest(phase=phase):
                clock = Clock()
                with self.ack_driver(clock) as h:
                    d = h.driver
                    d.boundaries = {}
                    if phase == "session/simulator state":
                        h.session_read_ns = m.RECEIVE_NS
                    else:
                        def slow_hash(path, maximum, deadline):
                            clock.now += m.RECEIVE_NS
                            raise m.ReadDeadline("original acknowledgement/read deadline exceeded")
                        original = m.deadline_bytes
                        def read(path, maximum, deadline):
                            return slow_hash(path, maximum, deadline) if path == d.command_path else original(path, maximum, deadline)
                    with ExitStack() as patches:
                        if phase == "command ownership":
                            patches.enter_context(patch.object(m, "deadline_bytes", side_effect=read))
                        with self.assertRaises(m.ReadDeadline):
                            d.fresh(boundary=dict(id="feed_released", timeoutNs=m.RECEIVE_NS))
                    self.assertTrue(d.observation_incomplete)
                    self.assertFalse(d.unsafe_owner)
                    self.assertEqual(d.boundaries, {})
                    self.assertEqual(h.published, [])
                    failure = next(e for e in d.artifacts.events if e["type"] == "guard_failure")
                    self.assertEqual(failure["phase"], phase)
                    self.assertEqual(failure["deadlineNs"], 1_000_000_000+m.RECEIVE_NS)

    def test_pending_boundary_reason_survives_normal_reads_exhausting_original_clock(self):
        # Actual240 ended a pending pickup wait during a 1.5916ms state guard,
        # with only 1.5315ms left. Keep that read error AND what was awaited.
        cases = (("feed", "SupplyPickup", "feed_supply_held"), ("pump", "PumpCycle", "pump_complete"),
                 ("fixed", "Open", "fixed_complete"), ("sight", "SightFlip", "sight_selected"))
        for family, stage, boundary_id in cases:
            for prepare in (False, True):
                with self.subTest(family=family, prepare=prepare):
                    clock = Clock()
                    with self.ack_driver(clock) as h:
                        d = h.driver
                        d.args.family, d.args.stage, d.args.step_ms = family, stage, None
                        d.boundaries, d.stimulus, d.profiles = {}, {}, {}
                        d.last_commit = clock.now
                        d.geometry.BoundaryPending = fixtures.g.BoundaryPending
                        d.geometry.source_deadline = lambda s, f: s["validUntilNs"]
                        pending = fixtures.g.BoundaryPending("required native evidence not yet observed")
                        validator = Mock(side_effect=pending)
                        d.geometry.check_boundary = d.geometry.build_stimulus = validator
                        self.queue_observations(d, clock, count=1)
                        read_state = m.sim_state
                        def measured_read(path, until):
                            state = read_state(path, until)
                            clock.now += 1_591_600
                            return state
                        with patch.object(m, "sim_state", side_effect=measured_read), \
                                self.assertRaisesRegex(m.ReadDeadline, "original simulator state read deadline exceeded"):
                            if prepare:
                                d.fresh(prepare=True)
                            else:
                                d.fresh(boundary=dict(id=boundary_id, timeoutNs=m.RECEIVE_NS))
                        validator.assert_called_once()
                        expected = dict(family=family, stage=stage,
                            id="stage_admission" if prepare else boundary_id,
                            startedNs=1_000_000_000, deadlineNs=1_000_000_000+m.RECEIVE_NS,
                            lastPending=dict(nonce="1", reason=str(pending)))
                        self.assertEqual(d.pending_boundary, expected)
                        failure = next(e for e in d.artifacts.events if e["type"] == "guard_failure")
                        self.assertEqual(failure["phase"], "session/simulator state")
                        self.assertEqual(failure["deadlineNs"], expected["deadlineNs"])
                        self.assertLess(failure["startedNs"], failure["deadlineNs"])
                        self.assertGreaterEqual(failure["finishedNs"], failure["deadlineNs"])
                        self.assertTrue(d.observation_incomplete)
                        self.assertFalse(d.unsafe_owner)
                        self.assertEqual(d.boundaries, {})
                        self.assertEqual(d.cleanup(), "not attempted: no settled owned held left grip")
                        self.assertEqual(d.pending_boundary, expected)
                        self.assertEqual(h.published, [])
                        self.assertIn("observation-000.json.gz", d.artifacts.saved)

    def test_observer_fatal_preserves_queued_raw_and_first_failure(self):
        clock = Clock()
        driver = self.observation_driver()
        self.queue_observations(driver, clock)
        driver.observer.fatal = "competing snapshot request writer"
        driver.observer.terminal = dict(kind="gap", reason=driver.observer.fatal)
        with self.assertRaisesRegex(m.Refusal, "competing snapshot request writer"):
            driver.observations()
        self.assertEqual(len(driver.artifacts.saved), 8)
        self.assertEqual(driver.observed, [])
        self.assertTrue(driver.observation_incomplete)
        first = driver.observation_failure
        with self.assertRaisesRegex(m.Refusal, "previous native observation failure"):
            driver.observations()
        self.assertEqual(driver.observation_failure, first)
        self.assertEqual(sum(e["type"] == "observation_gap" for e in driver.artifacts.events), 1)

    def test_journal_raw_metadata_and_record_limits_survive_deferred_flush(self):
        for limit in ("raw", "metadata", "records"):
            with self.subTest(limit=limit):
                artifacts = Artifacts()
                artifacts.event(dict(type="original_gap"))
                if limit == "raw":
                    artifacts.raw_bytes = 4*m.MAX_BYTES
                    append = lambda: artifacts.snapshot("oversize.gz", b"x")
                elif limit == "metadata":
                    artifacts.metadata_bytes = m.MAX_BYTES
                    append = lambda: artifacts.event(dict(type="overflow"))
                else:
                    artifacts.records += [("event", None, {}, False)] * (m.MAX_DIAGNOSTIC_RECORDS-1)
                    append = lambda: artifacts.event(dict(type="overflow"))
                with self.assertRaises(m.Refusal):
                    append()
                self.assertEqual(artifacts.events, [dict(type="original_gap")])
                self.assertEqual(artifacts.persisted, {})

    def test_flush_failure_keeps_first_error_and_continues_preserving_later_raw(self):
        artifacts = Artifacts()
        artifacts.snapshot("one.gz", b"first")
        artifacts.snapshot("two.gz", b"second")
        calls = []
        def write(name, value, final=False):
            calls.append(name)
            if name == "one.gz":
                raise OSError("original disk failure")
            artifacts.persisted[name] = value
        artifacts.sink.save = write
        with self.assertRaisesRegex(m.Refusal, "original disk failure"):
            artifacts.flush()
        self.assertEqual(calls, ["one.gz", "two.gz"])
        self.assertEqual(m.gzip.decompress(artifacts.persisted["two.gz"]), b"second")
        artifacts.save("result.json", dict(error="original refusal"), final=True)
        self.assertEqual(artifacts.persisted["result.json"]["error"], "original refusal")

    def test_worker_late_or_foreign_request_read_retains_original_raw_and_clock(self):
        for failure in ("late_read", "foreign_during_read", "foreign_between_requests", "foreign_response"):
            with self.subTest(failure=failure):
                clock = Clock()
                worker = m.ObservationWorker.__new__(m.ObservationWorker)
                worker.request = Mock()
                worker.request.exists.return_value = False
                worker.snapshot = Mock()
                worker.snapshot.exists.side_effect = [False, True]
                worker.inbox = queue.Queue(maxsize=8)
                worker.fatal = worker.terminal = None
                worker.hard_deadline = clock.now+10*m.NS
                h = types.SimpleNamespace(stopped=False, raw=b"", publishes=[])
                def wait(seconds):
                    clock.sleep(seconds)
                    h.raw = b"foreign request\n"
                worker.stop_event = types.SimpleNamespace(is_set=lambda: h.stopped, wait=wait)
                def publish(path, raw):
                    self.assertIs(path, worker.request)
                    h.raw = raw
                    h.publishes.append(raw)
                def read_json(path, maximum):
                    snapshot = dict(valid=True, nonce=h.raw.decode().strip(), observed_ns=clock.now)
                    if failure == "foreign_response":
                        snapshot["nonce"] = str(int(snapshot["nonce"])+1)
                    if failure == "late_read":
                        clock.now += m.RECEIVE_NS
                    if failure == "foreign_during_read":
                        h.raw = b"foreign request\n"
                    h.stopped = failure != "foreign_between_requests"
                    return snapshot, json.dumps(snapshot).encode()
                with patch.object(m, "time", clock), patch.object(m.transport, "atomic", side_effect=publish), \
                        patch.object(m.transport, "read_bytes", side_effect=lambda *a: h.raw), \
                        patch.object(m.transport, "read_json", side_effect=read_json):
                    worker._run()
                self.assertEqual(len(h.publishes), 1)
                item = worker.inbox.get_nowait()
                self.assertEqual(item["requestNs"], 1_000_000_000)
                self.assertEqual(item["deadlineNs"], 1_000_000_000+m.RECEIVE_NS)
                self.assertEqual(json.loads(item["raw"])["nonce"], item["snapshot"]["nonce"])
                if failure == "late_read":
                    self.assertGreaterEqual(item["receivedNs"], item["deadlineNs"])
                    self.assertIsNone(worker.fatal)
                else:
                    self.assertIn("competing snapshot", worker.fatal)
                    self.assertIsNotNone(worker.terminal)
                    self.assertEqual(item["requestOwned"], failure != "foreign_during_read")
                    self.assertEqual(item["responseOwned"], failure != "foreign_response")

    def test_queued_or_receive_expiry_never_supplies_fresh_boundary(self):
        for expiry in ("queued", "received"):
            with self.subTest(expiry=expiry):
                clock = Clock()
                with self.ack_driver(clock) as h:
                    d = h.driver
                    d.boundaries, d.stimulus, d.profiles = {}, {}, {}
                    d.last_commit = clock.now
                    d.geometry.source_deadline = lambda s, f: s["validUntilNs"]
                    d.geometry.check_boundary = Mock()
                    self.queue_observations(d, clock, count=1, lease_ns=1_000_000)
                    if expiry == "queued":
                        clock.now += 2_000_000
                    else:
                        item = d.observer.inbox.get_nowait()
                        item["receivedNs"] = item["deadlineNs"]
                        d.observer.inbox.put_nowait(item)
                    with self.assertRaisesRegex(m.Refusal, "fresh typed stimulus boundary absent"):
                        d.fresh(boundary=dict(id="feed_released", timeoutNs=m.RECEIVE_NS))
                    d.geometry.check_boundary.assert_not_called()
                    self.assertEqual(d.boundaries, {})
                    self.assertEqual(h.published, [])
                    self.assertEqual(d.observed[0]["snapshot"]["observed_ns"], 1_000_000_000)
                    self.assertEqual(len(d.artifacts.saved), 1)

    def test_worker_inbox_limit_retains_explicit_unadmitted_record_diagnostic(self):
        worker = m.ObservationWorker.__new__(m.ObservationWorker)
        worker.inbox, worker.terminal = queue.Queue(maxsize=8), None
        for index in range(8):
            worker.emit(dict(kind="gap", nonce=index+1))
        with self.assertRaisesRegex(m.Refusal, "bounded observation inbox exhausted"):
            worker.emit(dict(kind="snapshot", index=8, snapshot=dict(nonce="9"), raw=b"raw"))
        self.assertEqual(worker.inbox.qsize(), 8)
        self.assertEqual(worker.terminal["nonce"], "9")
        self.assertEqual(worker.terminal["unadmittedRawBytes"], 3)

    def test_shutdown_drains_failed_queue_and_keeps_lock_if_worker_not_joined(self):
        for joined in (False, True):
            with self.subTest(joined=joined):
                clock = Clock()
                driver = self.observation_driver()
                self.queue_observations(driver, clock)
                driver.observer.close = Mock(return_value=joined)
                driver.owner, driver.lock = Mock(), 7
                driver.observation_failure = "original owner failure"
                driver.geometry.same_mapping = Mock(side_effect=AssertionError("validation replay"))
                self.assertEqual(driver.close(), joined)
                self.assertEqual(driver.observation_failure, "original owner failure")
                self.assertEqual(len(driver.artifacts.saved), 8)
                self.assertTrue(driver.observer.inbox.empty())
                driver.geometry.same_mapping.assert_not_called()
                self.assertEqual(driver.owner.api.CloseHandle.call_count, int(joined))
                self.assertEqual(driver.owner.close.call_count, int(joined))
                self.assertEqual(driver.lock, None if joined else 7)

    def test_source_lease_is_rechecked_after_required_owner_publication(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            d = h.driver
            d.first_sample_pending = True
            d.initial_mapping_deadline = clock.now+10_000_000
            original = d.sequence, d.sentinel, d.command_hash
            save_owner = d.save_owner
            def slow_owner(pending):
                save_owner(pending)
                if pending:
                    clock.now += 20_000_000
            d.save_owner = slow_owner
            with self.assertRaisesRegex(m.Refusal, "initial mapping expired during owner publication"):
                d.send(sample(0, "neutral", 0, .1))
            self.assertEqual(h.published, [])
            self.assertEqual((d.sequence, d.sentinel, d.command_hash), original)
            self.assertEqual([row["pending"] for row in h.owners], [True, False])
            self.assertFalse(d.pending_command)
            self.assertIsNone(d.last_commit)
            self.assertEqual(d.commands, 0)

    @contextmanager
    def ack_driver(self, clock, *, unready_polls=0, archive_ns=300_000_000, mapping_error=False):
        # Real send/read_state/ownership/ACK/target/observation paths; substitute
        # only process identity, files, sim-state publication and archive latency.
        driver = self.observation_driver(mapping_error)
        driver.owner = Mock()
        driver.host = dict(pid=123)
        driver.link = dict(supervisor=dict(pid=456))
        driver.manifest = dict(source_sha256="offline", version="offline")
        driver.run = Path("offline-stimulus-run")
        driver.folder = Path("offline-stimulus-session")
        driver.session_path, driver.state_path = driver.folder / "session.json", driver.folder / "state.json"
        driver.command_path = driver.folder / "command.txt"
        driver.ack_path = Mock()
        driver.ack_path.exists.return_value = True
        driver.deadline = driver.stage_deadline = clock.now + 9_600_000_000
        driver.hard_deadline = clock.now + 10*m.NS
        driver.commands = driver.last_write = driver.capture_sequence = 0
        driver.sequence, driver.sentinel = 1, "note previous"
        driver.command_hash = m.transport.digest(b"note previous\n")
        driver.pending_command = driver.unsafe_owner = driver.held_left = False
        driver.first_sample_pending = driver.pose_setup_established = False
        driver.last_commit = driver.observation_failure = None
        driver.geometry.source_deadline = lambda *args: driver.deadline
        state = dict(pid=123, runtime="xr-sim", errors=0, lastCmdError="", sessionRunning=True,
                     actionsAttached=True, sessionState="FOCUSED", paceMode="free", captureSeq=0,
                     cmdSeq=1, lastCmd=driver.sentinel, frame=100, endFrames=100,
                     head=dict(valid=True), handR=dict(valid=True),
                     handL=dict(valid=True, followsHead=False, pos=[0., 1.3, -.35], ypr=[0., 0., 0.]),
                     controls=copy.deepcopy(CONTROLS))
        driver.initial_sim = copy.deepcopy(state)
        session = dict(host=driver.host, supervisor=driver.link["supervisor"], state="running", **driver.manifest)
        h = types.SimpleNamespace(driver=driver, published=[], owners=[], state_deadlines=[],
                                  command=b"note previous\n", ack=b"", ready_ns=0,
                                  state_read_ns=40_000_000, ack_read_ns=0, session_read_ns=0,
                                  session_read_after_publication=False,
                                  unready_polls=unready_polls, state=state, previous=state,
                                  alter_state=lambda x: x, alter_ack=lambda x: x, alter_command=lambda x: x)

        def publish(path, raw, *, deadline_ns=None, validate=None):
            self.assertEqual(path, driver.command_path)
            if validate is not None:
                h.publication_deadline = deadline_ns
                validate()
                m.require(clock.now < deadline_ns, "original command publication deadline exceeded")
            h.published.append(raw)
            h.command, h.previous = raw, h.state
            h.state = copy.deepcopy(h.state)
            lines = raw.decode("ascii").splitlines()
            h.state.update(cmdSeq=driver.sequence, lastCmd=driver.sentinel,
                           frame=h.state["frame"] + 2, endFrames=h.state["endFrames"] + 2)
            for line in lines:
                if line.startswith("grip l "):
                    h.state["controls"]["gripL"] = float(line.split()[-1])
                if line.startswith("hand l grip pose "):
                    pose = list(map(float, line.split()[4:]))
                    h.state["handL"].update(pos=pose[:3], ypr=pose[3:])
            h.ack = (f"seq={driver.sequence} frame={h.state['frame'] - 1} error=-\n"
                     f"last={' | '.join(lines)}\n").encode()
            h.ready_ns = clock.now + h.state_read_ns
            snapshot = dict(valid=True, nonce=str(len(h.published)), observed_ns=clock.now + 30_000_000)
            driver.observer.inbox.put(dict(kind="snapshot", index=len(h.published), snapshot=snapshot,
                raw=json.dumps(snapshot).encode(), requestNs=clock.now,
                receivedNs=clock.now + 35_000_000, deadlineNs=clock.now + m.RECEIVE_NS))

        def read_state(path, until):
            self.assertEqual(path, driver.state_path)
            h.state_deadlines.append(until)
            if driver.pending_command and h.state["cmdSeq"] == driver.sequence:
                if h.unready_polls:
                    h.unready_polls -= 1
                    return copy.deepcopy(h.previous)
                clock.now = max(clock.now, h.ready_ns)
            return h.alter_state(copy.deepcopy(h.state))

        def read(path, maximum):
            if path == driver.command_path:
                return h.alter_command(h.command) if driver.pending_command and h.state["cmdSeq"] == driver.sequence else h.command
            if path == driver.run / "live-driver-owner.json":
                return h.owner_record
            if path == driver.ack_path:
                clock.now += h.ack_read_ns
                return h.alter_ack(h.ack)
            self.assertEqual(path, driver.session_path)
            if not h.session_read_after_publication or h.published:
                clock.now += h.session_read_ns
            return m.transport.json_bytes(session)

        def save_owner(pending):
            h.owner_record = driver.owner_bytes(pending)
            h.owners.append(dict(pending=pending, sequence=driver.sequence, sentinel=driver.sentinel,
                                 commandSha256=driver.command_hash, atNs=clock.now))

        save = driver.artifacts.sink.save
        def archive(name, value, final=False):
            clock.now += archive_ns
            save(name, value, final)
        driver.save_owner, driver.artifacts.sink.save = save_owner, archive
        with patch.object(m, "time", clock), patch.object(m, "sim_state", side_effect=read_state), \
                patch.object(m.transport, "read_bytes", side_effect=read), \
                patch.object(m.transport, "atomic", side_effect=publish):
            yield h

    def test_unpublished_command_failure_restores_prior_owner_without_pending_ack(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            driver = h.driver
            previous = driver.sequence, driver.sentinel, driver.command_hash
            def fail(path, raw, **options):
                options["validate"]()
                raise OSError("unpublished replacement failure")
            with patch.object(m.transport, "atomic", side_effect=fail), self.assertRaisesRegex(OSError, "unpublished"):
                driver.send(sample(0, "acquire", 1, .1))
            self.assertEqual((driver.sequence, driver.sentinel, driver.command_hash), previous)
            self.assertFalse(driver.pending_command)
            self.assertFalse(driver.unsafe_owner)
            self.assertFalse(driver.held_left)
            self.assertEqual(driver.commands, 0)
            self.assertEqual(driver.last_write, 0)
            self.assertEqual(h.published, [])
            self.assertEqual([x["pending"] for x in h.owners], [True, False])
            self.assertEqual(driver.cleanup(), "not attempted: no settled owned held left grip")

    def test_unpublished_owner_interference_is_not_overwritten_by_rollback(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            def fail(path, raw, **options):
                h.owner_record = b"different pending owner"
                options["validate"]()
                self.fail("changed owner must refuse before replacement")
            with patch.object(m.transport, "atomic", side_effect=fail), \
                    self.assertRaisesRegex(m.Refusal, "owner record replaced"):
                h.driver.send(sample(0, "acquire", 1, .1))
            self.assertTrue(h.driver.unsafe_owner)
            self.assertFalse(h.driver.pending_command)
            self.assertEqual(h.owner_record, b"different pending owner")
            self.assertEqual([x["pending"] for x in h.owners], [True])
            self.assertEqual(h.published, [])

    def test_owner_and_publication_delay_do_not_restart_original_ack_deadline(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            requested = clock.now
            save, publish = h.driver.save_owner, m.transport.atomic.side_effect
            def delayed_owner(pending):
                save(pending)
                if pending:
                    clock.now += 30_000_000
            def delayed_publish(path, raw, **options):
                clock.now += 30_000_000
                publish(path, raw, **options)
            h.driver.save_owner = delayed_owner
            h.ack_read_ns = 180_000_000
            with patch.object(m.transport, "atomic", side_effect=delayed_publish), self.assertRaises(m.Refusal):
                h.driver.send(sample(0, "acquire", 1, .1))
            self.assertEqual(h.publication_deadline, requested + m.ACK_NS)
            self.assertEqual(h.driver.last_write, requested + 60_000_000)
            self.assertTrue(h.driver.pending_command)
            self.assertEqual(len(h.published), 1)

    def test_timely_ack_precedes_slow_archive_even_after_unready_poll(self):
        for unready_polls in (0, 1):
            with self.subTest(unready_polls=unready_polls):
                clock = Clock()
                with self.ack_driver(clock, unready_polls=unready_polls) as h:
                    h.driver.send(sample(0, "acquire", 1, .1))
                    self.assertLess(clock.now, h.driver.last_write + m.ACK_NS)
                    self.assertEqual(h.driver.artifacts.persisted, {})
                    h.driver.artifacts.flush()
                driver = h.driver
                self.assertEqual(driver.last_commit, driver.last_write + 40_000_000)
                self.assertGreater(clock.now, driver.last_write + m.ACK_NS)
                self.assertFalse(driver.pending_command)
                self.assertTrue(driver.held_left)
                self.assertFalse(driver.unsafe_owner)
                self.assertEqual(len(h.published), 1)
                self.assertEqual([owner["pending"] for owner in h.owners], [True, False])
                self.assertEqual(h.owners[-1]["atNs"], driver.last_commit)
                self.assertEqual(h.owners[-1]["commandSha256"], m.transport.digest(h.published[-1]))
                self.assertEqual(h.state_deadlines[-1], driver.last_write + m.ACK_NS)
                events = driver.artifacts.events
                commit = next(row for row in events if row["type"] == "command_commit")
                self.assertEqual(commit["committedNs"], driver.last_commit)
                self.assertLess(events.index(commit), next(i for i, row in enumerate(events) if row["type"] == "native_observation"))
                self.assertIn("observation-001.json.gz", driver.artifacts.saved)
                self.assertEqual(len(driver.observed), 1)
                self.assertTrue(driver.observer.inbox.empty())

    def test_original_ack_deadline_rejects_late_session_state_or_ack_read(self):
        for late_read in ("session_read_ns", "state_read_ns", "ack_read_ns"):
            with self.subTest(late_read=late_read):
                clock = Clock()
                with self.ack_driver(clock) as h:
                    setattr(h, late_read, m.ACK_NS)
                    # Exercise the ACK read after publication, not the new
                    # ownership check before the command has reached disk.
                    h.session_read_after_publication = True
                    with self.assertRaisesRegex(m.Refusal, "deadline"):
                        h.driver.send(sample(0, "acquire", 1, .1))
                    self.assertIsNone(h.driver.last_commit)
                    self.assertTrue(h.driver.pending_command)
                    self.assertFalse(h.driver.unsafe_owner)
                    self.assertEqual([owner["pending"] for owner in h.owners], [True])
                    self.assertEqual(len(h.published), 1)
                    self.assertNotIn("command_commit", [row["type"] for row in h.driver.artifacts.events])
                    self.assertIn("observation-001.json.gz", h.driver.artifacts.saved)
                    self.assertTrue(h.driver.observer.inbox.empty())

    def test_unready_ack_timeout_preserves_evidence_without_commit_or_replay(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            h.alter_ack = lambda raw: b""
            with self.assertRaisesRegex(m.Refusal, "original command acknowledgement/commit deadline exceeded"):
                h.driver.send(sample(0, "acquire", 1, .1))
            self.assertIsNone(h.driver.last_commit)
            self.assertTrue(h.driver.pending_command)
            self.assertTrue(h.owners[-1]["pending"])
            self.assertEqual(len(h.published), 1)
            self.assertIn("observation-001.json.gz", h.driver.artifacts.saved)
            self.assertTrue(h.driver.observer.inbox.empty())

    def test_ack_refusal_and_observation_failure_are_both_retained(self):
        clock = Clock()
        with self.ack_driver(clock, mapping_error=True) as h:
            h.state_read_ns = m.ACK_NS
            with self.assertRaisesRegex(m.Refusal, "deadline.*post-refusal observation failure: native owner changed"):
                h.driver.send(sample(0, "acquire", 1, .1))
            self.assertIsNone(h.driver.last_commit)
            self.assertTrue(h.driver.pending_command)
            self.assertEqual(h.driver.observation_failure, "native owner changed")
            self.assertIn("observation-001.json.gz", h.driver.artifacts.saved)
            self.assertEqual(len(h.published), 1)

    def test_exact_command_ack_frame_and_target_guards_survive_deferred_archive(self):
        for fault in ("hash", "sentinel", "ack_text", "frame", "target"):
            with self.subTest(fault=fault):
                clock = Clock()
                with self.ack_driver(clock) as h:
                    if fault == "hash":
                        h.alter_command = lambda raw: b"foreign command\n"
                    elif fault == "ack_text":
                        h.alter_ack = lambda raw: raw.replace(b"last=", b"last=foreign ")
                    else:
                        def change(state):
                            if h.driver.pending_command and h.published:
                                if fault == "sentinel":
                                    state["lastCmd"] = "note foreign"
                                elif fault == "frame":
                                    state["frame"] -= 1
                                else:
                                    state["handL"]["pos"][0] += .1
                            return state
                        h.alter_state = change
                    with self.assertRaises(m.Refusal):
                        h.driver.send(sample(0, "acquire", 1, .1))
                    self.assertIsNone(h.driver.last_commit)
                    self.assertTrue(h.driver.pending_command)
                    self.assertEqual(len(h.published), 1)
                    self.assertEqual(h.driver.unsafe_owner, fault in ("hash", "sentinel", "ack_text"))

    def test_post_ack_observation_failure_blocks_next_input_but_retains_owned_cleanup(self):
        clock = Clock()
        with self.ack_driver(clock, mapping_error=True) as h:
            driver = h.driver
            with self.assertRaisesRegex(m.Refusal, "native owner changed"):
                driver.send(sample(0, "acquire", 1, .1))
            self.assertEqual(driver.last_commit, driver.last_write + 40_000_000)
            self.assertFalse(driver.pending_command)
            self.assertTrue(driver.held_left)
            self.assertFalse(h.owners[-1]["pending"])
            self.assertIn("observation-001.json.gz", driver.artifacts.saved)
            with self.assertRaisesRegex(m.Refusal, "previous native observation failure"):
                driver.send(sample(1, "move", 1, .2))
            self.assertEqual(len(h.published), 1)
            self.assertEqual(driver.cleanup(), "ordinary owned left-grip release acknowledged")
            self.assertEqual(h.published[-1].decode().splitlines()[:-1], ["grip l 0"])
            self.assertEqual(len(h.published), 2)
            self.assertFalse(driver.pending_command)
            self.assertFalse(driver.held_left)
            self.assertFalse(h.owners[-1]["pending"])
            self.assertEqual(h.owners[-1]["sequence"], driver.state["cmdSeq"])
            self.assertEqual(h.owners[-1]["sentinel"], driver.state["lastCmd"])
            self.assertEqual(h.owners[-1]["commandSha256"], m.transport.digest(h.published[-1]))

    def test_deferred_archival_cannot_consume_or_extend_original_stage_deadline(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            h.driver.stage_deadline = clock.now + m.ACK_NS
            h.driver.send(sample(0, "acquire", 1, .1))
            self.assertLess(clock.now, h.driver.stage_deadline)
            original = h.driver.stage_deadline
            h.driver.artifacts.flush()
            with self.assertRaisesRegex(m.Refusal, "original stimulus/gesture deadline"):
                h.driver.bounds()
            self.assertEqual(h.driver.stage_deadline, original)
            self.assertEqual(h.driver.last_commit, h.driver.last_write + 40_000_000)
            self.assertFalse(h.driver.pending_command)
            self.assertTrue(h.driver.held_left)
            self.assertFalse(h.owners[-1]["pending"])
            self.assertEqual(len(h.published), 1)

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
        driver.last_commit = clock.now
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
        def guard(deadline_ns=None):
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
        self.assertIsNone(driver.pending_boundary)
        self.assertTrue(driver.observation_incomplete)
        event = driver.artifacts.events[0]
        self.assertEqual((event["type"], event["id"]), ("boundary_pending", "stage_admission"))
        self.assertEqual(event["deadlineNs"], 1_000_000_000 + m.RECEIVE_NS)

    def test_run_reuses_plan_from_same_fresh_stage_admission(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["ready"])
        driver.args.pickup_first, driver.args.step_ms = False, 40
        driver.held_left = False
        value = plan(clock)
        def build(family, profiles, snapshot, now, stage, **kwargs):
            driver.geometry.admit_boundary(family, profiles, snapshot, now, stage)
            value["source"] = dict(nonce=snapshot["nonce"], evaluatedAtNs=now,
                                   validUntilNs=snapshot["deadlineNs"])
            return value
        driver.geometry.build_stimulus = Mock(side_effect=build)
        driver.execute = Mock()
        with patch.object(m, "time", clock):
            driver.run_stimulus()
        self.assertEqual(calls, ["1"])
        driver.geometry.build_stimulus.assert_called_once()
        driver.execute.assert_called_once()
        self.assertIs(driver.execute.call_args.args[0], value)
        self.assertEqual(driver.mapping["nonce"], value["source"]["nonce"])

    def test_delayed_plan_preparation_keeps_original_boundary_deadline(self):
        clock = Clock()
        driver, _ = self.boundary_driver(clock, ["ready"])
        driver.args.step_ms = 40
        value = plan(clock)
        def build(*args, **kwargs):
            clock.sleep(.1)  # Exactly exhaust the admitted snapshot's original lease.
            return value
        driver.geometry.build_stimulus = Mock(side_effect=build)
        with patch.object(m, "time", clock), \
                self.assertRaisesRegex(m.Refusal, "initial boundary validation consumed original deadline"):
            driver.fresh(prepare=True)
        driver.geometry.build_stimulus.assert_called_once()
        self.assertEqual(driver.observed[0]["snapshot"]["deadlineNs"], clock.now)

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
        self.assertIsNone(driver.pending_boundary)

    def test_boundary_refusal_is_not_retried_as_pending(self):
        clock = Clock()
        driver, calls = self.boundary_driver(clock, ["refused", "ready"])
        with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, "owner changed"):
            driver.fresh(boundary=dict(id="feed_acquired", timeoutNs=250_000_000))
        self.assertEqual(calls, ["2"])

    def test_late_successful_validation_cannot_record_a_passed_boundary(self):
        clock = Clock()
        driver, _ = self.boundary_driver(clock, ["ready"])
        def delayed_check(*args, **kwargs):
            clock.sleep(.3)
            return dict(observed=True, nativeAcceptance=False)
        driver.geometry.check_boundary = delayed_check
        with patch.object(m, "time", clock), self.assertRaisesRegex(m.Refusal, "boundary validation consumed original deadline"):
            driver.fresh(boundary=dict(id="feed_acquired", timeoutNs=m.RECEIVE_NS))
        self.assertEqual(driver.boundaries, {})
        self.assertNotIn("fresh_stimulus_boundary", [event["type"] for event in driver.artifacts.events])

    def test_release_boundary_requires_input_after_ack_not_command_write(self):
        # Actual195: release was written before native held packet7078, but the
        # host acknowledged it only at frame7081. The later packet7085 released.
        written, committed = 191321678511200, 191321752470600
        for case in ("before_ack", "serialized_after_ack", "held_after_ack"):
            with self.subTest(case=case):
                clock = Clock()
                clock.now = committed
                driver, _ = self.boundary_driver(clock, ["ready"])
                driver.last_write, driver.last_commit = written, committed
                driver.args.stage = "DetachContainer"
                held = dict(valid=True, nonce="19", observed_ns=191321712252500,
                    deadlineNs=191321787029600,
                    input_evidence=dict(sequence=7078, observedNs=191321687029600, released=[False, False]))
                released = dict(valid=True, nonce="20", observed_ns=191321820545600,
                    deadlineNs=191321909928600,
                    input_evidence=dict(sequence=7085, observedNs=191321809928600, released=[True, False]))
                if case != "before_ack":
                    held["observed_ns"] = committed + 1
                if case == "held_after_ack":
                    held["input_evidence"]["observedNs"] = committed + 1
                pending = iter(((held, 191321760000000), (released, 191321824000000)))
                def guard(deadline_ns=None):
                    snapshot, clock.now = next(pending)
                    driver.observed.append(dict(snapshot=snapshot, current=True))
                driver.guard = guard
                checked = []
                def check(stimulus, snapshot, name, now, **kwargs):
                    checked.append(snapshot["input_evidence"]["sequence"])
                    self.assertEqual(kwargs["command_written_ns"], written)
                    m.require(snapshot["input_evidence"]["released"][0], "feed was not freshly released")
                    return dict(observed=True, nativeAcceptance=False)
                driver.geometry.check_boundary = check
                boundary = dict(id="feed_released", timeoutNs=600_000_000)
                with patch.object(m, "time", clock):
                    if case == "held_after_ack":
                        with self.assertRaisesRegex(m.Refusal, "feed was not freshly released"):
                            driver.fresh(boundary=boundary, after_ns=written)
                        self.assertEqual(checked, [7078])
                        self.assertNotIn("feed_released", driver.boundaries)
                    else:
                        snapshot, _ = driver.fresh(boundary=boundary, after_ns=written)
                        self.assertIs(snapshot, released)
                        self.assertEqual(checked, [7085])
                        self.assertFalse(driver.boundaries["feed_released"]["nativeAcceptance"])
                self.assertLess(clock.now, committed + boundary["timeoutNs"])

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
