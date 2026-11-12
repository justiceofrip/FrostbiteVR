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
        self.constructor_main("feed", "OpenCover")

    @unittest.skipUnless(os.name == "nt", "startup receipts require local Windows drive paths")
    def test_fixed_reload_constructor_allocates_one_bounded_sequence_clock(self):
        self.constructor_main("fixed", "Reload")

    def constructor_main(self, family, stage):
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
                    patch.object(m, "sim_state", side_effect=lambda path, deadline: json.loads(path.read_bytes())), \
                    patch.object(m, "ObservationWorker", return_value=observer) as observer_factory, \
                    patch.object(m.ctypes, "WinDLL", side_effect=AssertionError("unexpected native access")), \
                    patch("sys.stdout", stdout):
                code = m.main(["--run", str(run), "--profiles", str(profiles), "--family", family,
                    "--stage", stage, "--root-authorized-ordinary-input"])
            result = json.loads(stdout.getvalue())
            self.assertEqual(code, 2)
            self.assertEqual(result["error"], "fresh typed stimulus boundary absent within original budget")
            self.assertEqual(result["status"], "refused")
            self.assertEqual(result["durationNs"], m.RECEIVE_NS)
            self.assertEqual(result["commands"], 0)
            self.assertEqual(result["plans"], [])
            self.assertEqual(result["boundaries"], {})
            self.assertEqual(result["pendingBoundary"], dict(family=family, stage=stage,
                id="fixed_capture_readiness" if family == "fixed" else "stage_admission",
                startedNs=1_000_000_000, deadlineNs=1_000_000_000+m.RECEIVE_NS,
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
            duration = m.invocation_duration_ns(family, stage)
            requests = (duration + m.PERIOD_NS - 1) // m.PERIOD_NS
            observer_factory.assert_called_once_with(folder,
                clock.now - m.RECEIVE_NS + duration - m.CLEANUP_RESERVE_NS, maximum_requests=requests)
            observer.close.assert_called_once_with()
            artifacts = Path(result["artifactDirectory"])
            self.assertEqual(json.loads((artifacts / "result.json").read_text()), result)
            self.assertEqual({path.name for path in artifacts.iterdir()},
                             {"invocation.json", "profile-source.json", "initial-state.json", "result.json"})
            invocation = json.loads((artifacts / "invocation.json").read_bytes())
            self.assertEqual(invocation["maximumDurationNs"], duration)
            self.assertEqual(invocation["maximumObservationRequests"], requests)
            self.assertEqual(invocation["maximumDecodedObservationBytes"], 4*m.MAX_BYTES)

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

    def test_journal_scalar_reservations_bound_json_without_serializing(self):
        values = ["ordinary ASCII metadata", 'quote" and backslash\\',
                  "".join(map(chr, range(128))), "\u00e9\u6f22\U0001f680", "\ud800",
                  None, True, False, 0, -1, 2**2048, -(2**2048), 1.7976931348623157e308,
                  {"source": {"nonce": "1794510692106244900", "validUntilNs": 636008731406300},
                   "commands": ["hand l grip pose -0.123456789 1.234567890 0.000000000"]}]
        values.extend(map(chr, range(128)))
        for value in values:
            with self.subTest(value=repr(value)[:80]):
                encoded = m.transport.json_bytes(value).rstrip(b"\n")
                with patch.object(m.json, "dumps", side_effect=AssertionError("live serialization")):
                    reserved = m.DeferredArtifacts.bound(value)
                self.assertGreaterEqual(reserved, len(encoded))
        self.assertEqual(m.DeferredArtifacts.bound("ordinary ASCII metadata"), 25)
        artifacts = Artifacts()
        artifacts.save("within.json", "x" * (m.MAX_BYTES - 2))
        self.assertEqual(artifacts.metadata_bytes, m.MAX_BYTES)
        with self.assertRaisesRegex(m.Refusal, "diagnostic metadata budget"):
            artifacts.event({"type": "genuine_overflow"})
        self.assertEqual(len(artifacts.records), 1)

    @unittest.skipUnless(os.environ.get("BC2_RELOAD_METADATA_REPLAY"), "retained full-Reload metadata optional")
    def test_retained_five_stage_reload_metadata_at_transport_limits(self):
        # Read-only archive accounting, never a replay of native actions or a
        # completed Reload verdict. The retained failure remains a failure.
        run = Path(os.environ["BC2_RELOAD_METADATA_REPLAY"])
        original = (run / "result.json").read_bytes()
        result = json.loads(original)
        self.assertEqual(result["error"], "diagnostic metadata budget")
        self.assertFalse(result["stimulusCompleted"])
        self.assertEqual([p["stage"] for p in result["plans"]],
                         ["Open", "EjectSpent", "Pickup", "Insert", "Close"])
        self.assertTrue(all(p["boundaryChecksPassed"] for p in result["plans"][:4]))
        artifacts, snapshots = Artifacts(), []
        for path in sorted(run.glob("*.json")):
            if path.name != "result.json":
                artifacts.save(path.name, json.loads(path.read_bytes()))
        for path in sorted(run.glob("observation-*.json.gz")):
            raw = m.gzip.decompress(path.read_bytes())
            artifacts.snapshot(path.name, raw)
            snapshots.append(json.loads(raw))
        events = [json.loads(row) for row in (run / "events.jsonl").read_bytes().splitlines()]
        for event in events:
            artifacts.event(event)

        source = result["plans"][-1]["source"]
        snapshot = next(s for s in snapshots if str(s["nonce"]) == str(source["nonce"]))
        close = fixtures.g.build_stimulus("fixed", None, snapshot, source["evaluatedAtNs"], "Close",
            step_ms=artifacts.saved["stimulus-04.json"]["stepNs"] // 1_000_000)
        self.assertEqual(len(close["schedule"]), result["plans"][-1]["samples"])
        clock = Clock()
        clock.now = source["evaluatedAtNs"]
        driver = ScheduleDriver(clock)
        driver.artifacts, driver.fixed_reload = artifacts, True
        driver.plans = copy.deepcopy(result["plans"][:4])
        save = artifacts.save
        class ArchiveOnly(Exception):
            pass
        def stop_after_archive(name, value, final=False):
            save(name, value, final)
            raise ArchiveOnly()
        with patch.object(artifacts, "save", side_effect=stop_after_archive), patch.object(m, "time", clock):
            with self.assertRaises(ArchiveOnly):
                driver.execute(close)
        self.assertEqual(driver.sent, [])
        self.assertEqual(artifacts.saved["stimulus-05.json"]["source"], close["source"])
        self.assertNotIn("stimulusCompleted", driver.plans[-1])

        # A batch consumes at least two of the existing 512 commands. Reserve
        # the largest retained event shape for every possible batch and all
        # 100 observations, plus four boundary summaries per stage. This is
        # deliberately larger than the actual five-stage command schedule.
        for kind, count in (("command_request", m.MAX_COMMANDS // 2),
                            ("command_commit", m.MAX_COMMANDS // 2),
                            ("native_observation", m.MAX_OBSERVATIONS),
                            ("fresh_stimulus_boundary", 5 * 4)):
            rows = [e for e in events if e["type"] == kind]
            largest = max(rows, key=m.DeferredArtifacts.bound)
            for _ in range(count - len(rows)):
                artifacts.event(copy.deepcopy(largest))
        self.assertLess(artifacts.metadata_bytes, m.MAX_BYTES)
        encoded = sum(len(m.transport.json_bytes(value)) for kind, _, value, _ in artifacts.records
                      if kind != "snapshot")
        self.assertLess(encoded, artifacts.metadata_bytes)
        disk_bytes = encoded + sum(len(m.gzip.compress(value, mtime=0))
                                  for kind, _, value, _ in artifacts.records if kind == "snapshot")
        self.assertLess(disk_bytes, m.MAX_BYTES - 12288)
        remaining = m.MAX_BYTES - artifacts.metadata_bytes
        with self.assertRaisesRegex(m.Refusal, "diagnostic metadata budget"):
            artifacts.save("overflow.json", "x" * remaining)
        artifacts.flush()
        self.assertIn("stimulus-05.json", artifacts.persisted)
        self.assertEqual((run / "result.json").read_bytes(), original)

    @unittest.skipUnless(os.environ.get("BC2_RELOAD_DEADLINE_REPLAY"), "retained full-Reload deadline optional")
    def test_retained_global_deadline_failure_and_release_fit_unchanged_archive_caps(self):
        # Inspect immutable saved bytes only. Neither the failed result nor its
        # already acknowledged cleanup release becomes a successful live run.
        run = Path(os.environ["BC2_RELOAD_DEADLINE_REPLAY"])
        original = (run / "result.json").read_bytes()
        result = json.loads(original)
        self.assertEqual(result["error"], "original acknowledgement/read deadline exceeded")
        self.assertFalse(result["stimulusCompleted"])
        self.assertTrue(result["boundaries"]["fixed_step_complete"]["observed"])
        events = [json.loads(row) for row in (run / "events.jsonl").read_bytes().splitlines()]
        failure = next(e for e in events if e["type"] == "command_publication_trace" and e["failure"])
        self.assertEqual(failure["publicationOutcome"], "not_published")
        self.assertEqual(failure["failure"]["phase"], "initial.session_read_decode")
        self.assertIsNone(failure["originalAckDeadlineNs"])
        self.assertEqual(failure["originalHardDeadlineNs"]-failure["originalActionDeadlineNs"], m.CLEANUP_RESERVE_NS)
        self.assertGreaterEqual(failure["finishedNs"], failure["originalActionDeadlineNs"])
        self.assertLess(failure["finishedNs"], failure["originalHardDeadlineNs"])
        artifacts = Artifacts()
        plans = [json.loads(p.read_bytes()) for p in sorted(run.glob("stimulus-*.json"))]
        self.assertEqual([p["stage"] for p in plans], ["Open", "EjectSpent", "Pickup", "Insert", "Close"])
        self.assertEqual(sum(p["durationNs"] for p in plans), 5_440_000_000)
        for path in run.glob("*.json"):
            artifacts.save(path.name, json.loads(path.read_bytes()))
        for path in run.glob("observation-*.json.gz"):
            artifacts.snapshot(path.name, m.gzip.decompress(path.read_bytes()))
        for event in events:
            artifacts.event(event)
        # Reserve even a duplicate release pair beyond the saved complete
        # cleanup pair; the relevant9.7s episode still fits the original caps.
        released = next(e for e in events if e["type"] == "command_commit" and e["cleanup"])
        artifacts.event(released)
        artifacts.event(next(e for e in events if e["type"] == "command_request"
                             and e["targetSequence"] == released["sequence"]))
        disk_bytes = sum(len(m.gzip.compress(value, mtime=0)) if kind == "snapshot"
                         else len(m.transport.json_bytes(value)) for kind, _, value, _ in artifacts.records)
        self.assertLess(disk_bytes, m.MAX_BYTES-12288)
        self.assertLess(artifacts.raw_bytes, 4*m.MAX_BYTES)
        self.assertLess(artifacts.metadata_bytes, m.MAX_BYTES)
        artifacts.raw_bytes = 4*m.MAX_BYTES
        with self.assertRaisesRegex(m.Refusal, "decoded observation budget"):
            artifacts.snapshot("genuine-overflow.gz", b"x")
        self.assertEqual((run / "result.json").read_bytes(), original)

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
                worker.maximum_requests = m.MAX_OBSERVATIONS
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

    def test_full_reload_observer_keeps_request_deadlines_and_finite_cap(self):
        for stop_at_deadline in (False, True):
            with self.subTest(stop_at_deadline=stop_at_deadline):
                clock = Clock()
                maximum = (m.FIXED_RELOAD_NS + m.PERIOD_NS - 1) // m.PERIOD_NS
                worker = m.ObservationWorker.__new__(m.ObservationWorker)
                worker.maximum_requests = maximum
                worker.hard_deadline = clock.now + (m.FIXED_RELOAD_NS-m.CLEANUP_RESERVE_NS
                                                  if stop_at_deadline else 2*m.FIXED_RELOAD_NS)
                worker.request, worker.snapshot = Mock(), Mock()
                worker.request.exists.return_value = False
                worker.snapshot.exists.side_effect = [False] + [True]*maximum
                worker.fatal = worker.terminal = None
                worker.stop_event = types.SimpleNamespace(is_set=lambda: False, wait=clock.sleep)
                emitted, request = [], [b""]
                worker.emit = emitted.append
                def response(*args):
                    snapshot = dict(nonce=request[0].decode().strip())
                    return snapshot, json.dumps(snapshot).encode()
                with patch.object(m, "time", clock), \
                        patch.object(m.transport, "atomic", side_effect=lambda path, raw: request.__setitem__(0, raw)), \
                        patch.object(m.transport, "read_bytes", side_effect=lambda *args: request[0]), \
                        patch.object(m.transport, "read_json", side_effect=response):
                    worker._run()
                snapshots = [item for item in emitted if item["kind"] == "snapshot"]
                self.assertGreater(len(snapshots), m.MAX_OBSERVATIONS)
                self.assertLessEqual(len(snapshots), maximum)
                self.assertIsNone(worker.fatal)
                self.assertTrue(all(item["requestNs"] < worker.hard_deadline for item in snapshots))
                self.assertTrue(all(item["deadlineNs"] == min(worker.hard_deadline,
                    item["requestNs"]+m.RECEIVE_NS) for item in snapshots))
                if stop_at_deadline:
                    self.assertGreaterEqual(clock.now, worker.hard_deadline)
                    self.assertLess(len(snapshots), maximum)
                else:
                    self.assertEqual(len(snapshots), maximum)
                    self.assertEqual(emitted[-1]["reason"], "bounded observer request cap")

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
                                  publish_observations=True, publication_deadlines=[],
                                  command=b"note previous\n", ack=b"", ready_ns=0,
                                  state_read_ns=40_000_000, ack_read_ns=0, session_read_ns=0,
                                  session_read_after_publication=False,
                                  unready_polls=unready_polls, state=state, previous=state,
                                  alter_state=lambda x: x, alter_ack=lambda x: x, alter_command=lambda x: x)

        def publish(path, raw, *, deadline_ns=None, validate=None):
            self.assertEqual(path, driver.command_path)
            if validate is not None:
                h.publication_deadline = deadline_ns
                trace = driver._publication_trace
                h.publication_deadlines.append((trace.requested_ns, deadline_ns, trace.ack_deadline))
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
            if h.publish_observations:
                driver.observer.inbox.put(dict(kind="snapshot", index=len(h.published), snapshot=snapshot,
                    raw=json.dumps(snapshot).encode(), requestNs=clock.now,
                    receivedNs=clock.now + 35_000_000, deadlineNs=clock.now + m.RECEIVE_NS))

        def read_state(path, until, trace=None):
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
            driver.dispose_publication_owner()
            h.owner_record = driver.owner_bytes(pending)
            h.owners.append(dict(pending=pending, sequence=driver.sequence, sentinel=driver.sentinel,
                                 commandSha256=driver.command_hash, atNs=clock.now))
            if pending:
                driver._publication_owner = MemoryOwner(h.owner_record)

        class MemoryOwner:
            def __init__(self, raw):
                self.raw, self.stream = raw, self
                self.temporary = driver.run / "live-driver-owner.json.tmp"

            def validate(self, expected_bytes, deadline_ns):
                # Model sealed bytes and the current pathname identity together.
                # Actual Windows metadata custody is covered separately.
                if clock.now >= deadline_ns:
                    raise m.transport.ReadDeadlineExceeded("original acknowledgement/read deadline exceeded")
                m.require(self.stream is not None and self.raw == expected_bytes == h.owner_record,
                          "command owner record replaced before publication")
                return self.raw

            def close(self):
                self.stream = None

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
                self.assertIsNone(driver._ack_trace)
                self.assertEqual(getattr(driver, "_ack_failures", []), [])

    def test_failed_ack_trace_is_bounded_deferred_and_does_not_replay_input(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            driver, requested = h.driver, clock.now
            h.alter_ack = lambda raw: b""
            with self.assertRaisesRegex(m.Refusal, "original command acknowledgement/commit deadline exceeded"):
                driver.send(sample(0, "acquire", 1, .1))
            self.assertEqual(clock.now, requested + m.ACK_NS)
            self.assertIsNone(driver.last_commit)
            self.assertFalse(driver.unsafe_owner)
            self.assertTrue(driver.pending_command)
            self.assertIsNone(driver._ack_trace)
            trace = driver._ack_failures[0]
            report = trace.report()
            self.assertEqual(report["originalDeadlineNs"], requested + m.ACK_NS)
            self.assertEqual(report["requestedNs"], requested)
            self.assertEqual(report["publishedNs"], driver.last_write)
            self.assertGreater(report["pollCount"], m.MAX_ACK_TRACE_POLLS)
            self.assertEqual(report["retainedPolls"], m.MAX_ACK_TRACE_POLLS)
            self.assertEqual(report["droppedPolls"], report["pollCount"] - m.MAX_ACK_TRACE_POLLS)
            self.assertEqual(report["polls"][0]["index"], 1)
            self.assertEqual(report["polls"][-1]["index"], report["pollCount"])
            self.assertEqual(report["lastState"]["cmdSeq"], driver.sequence)
            self.assertEqual(report["lastAck"]["parsed"], False)
            self.assertEqual(report["pendingCommand"]["commandSha256"], m.transport.digest(h.published[0]))
            self.assertNotIn("ack_read_failure", [x["type"] for x in driver.artifacts.events])
            self.assertEqual(driver.artifacts.persisted, {})
            self.assertEqual(len(h.published), 1)

            # Original input remains refused. Only its acknowledged held grip
            # may settle for the existing ordinary release cleanup.
            h.alter_ack = lambda raw: raw
            self.assertEqual(driver.cleanup(), "ordinary owned left-grip release acknowledged")
            self.assertEqual(len(h.published), 2)
            self.assertEqual(h.published[-1].decode().splitlines()[:-1], ["grip l 0"])
            self.assertEqual(trace.report(), report)
            self.assertEqual(len(driver._ack_failures), 1)
            driver.lock = None
            driver.observer.close = Mock(return_value=True)
            self.assertTrue(driver.close())
            driver.defer_ack_failures(True)
            self.assertEqual(driver.artifacts.persisted, {})
            events = [x for x in driver.artifacts.events if x["type"] == "ack_read_failure"]
            self.assertEqual(len(events), 1)
            self.assertEqual(events[0], dict(report, observerStopped=True))
            self.assertEqual(driver._ack_failures, [])
            driver.artifacts.flush()

    def test_late_state_read_trace_preserves_original_expiry_and_phase(self):
        clock = Clock()
        with self.ack_driver(clock) as h:
            h.state_read_ns = m.ACK_NS
            requested = clock.now
            with self.assertRaisesRegex(m.ReadDeadline, "original simulator state read deadline exceeded"):
                h.driver.send(sample(0, "acquire", 1, .1))
            report = h.driver._ack_failures[0].report()
            self.assertEqual(report["originalDeadlineNs"], requested + m.ACK_NS)
            self.assertEqual(report["failure"]["atNs"], requested + m.ACK_NS)
            self.assertEqual(report["pollCount"], 1)
            self.assertEqual(report["phaseNs"]["state_setup"], m.ACK_NS)
            self.assertEqual(report["polls"][0]["failedPhase"], "state_validation")
            self.assertEqual(report["lastState"]["cmdSeq"], h.driver.sequence)
            self.assertIsNone(report["lastAck"])
            self.assertIsNone(h.driver.last_commit)
            self.assertFalse(h.driver.unsafe_owner)
            self.assertEqual(len(h.published), 1)

    @unittest.skipUnless(os.name == "nt", "Win32 share-delete read diagnostics")
    def test_windows_state_trace_retains_transient_winerrors_and_exact_bytes_result(self):
        clock = Clock()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "state.json"
            expected = dict(cmdSeq=278, lastCmd="note owned", frame=9560, endFrames=9559)
            path.write_bytes(json.dumps(expected).encode())
            api = m.ctypes.WinDLL("kernel32", use_last_error=True)
            api.CreateFileW.argtypes = [m.wintypes.LPCWSTR, m.wintypes.DWORD, m.wintypes.DWORD,
                m.wintypes.LPVOID, m.wintypes.DWORD, m.wintypes.DWORD, m.wintypes.HANDLE]
            api.CreateFileW.restype = m.wintypes.HANDLE
            attempts = []
            def create(*args):
                attempts.append(args)
                clock.now += 2_000_000
                return m.wintypes.HANDLE(-1).value if len(attempts) <= 3 else api.CreateFileW(*args)
            fake = types.SimpleNamespace(CreateFileW=Mock(side_effect=create), CloseHandle=api.CloseHandle)
            trace = m.AckReadTrace(clock.now, clock.now, clock.now + m.ACK_NS, dict(sequence=278))
            with patch.object(m, "time", clock), patch.object(m.ctypes, "WinDLL", return_value=fake), \
                    patch.object(m.ctypes, "get_last_error", side_effect=[2, 32, 33]):
                trace.begin_poll()
                observed = m.sim_state(path, trace.deadline_ns, trace)
                trace.end_poll()
            self.assertEqual(observed, expected)
            self.assertEqual(clock.now, trace.requested_ns + 11_000_000)
            report = trace.report()
            self.assertEqual(report["openAttempts"], 4)
            self.assertEqual(report["winErrors"], {"2": 1, "32": 1, "33": 1})
            self.assertEqual(report["phaseNs"]["state_open"], 8_000_000)
            self.assertEqual(report["phaseNs"]["state_retry_wait"], 3_000_000)
            self.assertTrue({"state_read", "state_decode", "state_close"}.issubset(report["phaseNs"]))
            self.assertTrue(all(args[2] == 7 for args in attempts))

    @unittest.skipUnless(os.name == "nt", "Win32 share-delete read diagnostics")
    def test_windows_missing_state_retries_stop_at_original_deadline(self):
        clock = Clock()
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "unpublished-state.json"
            deadline = clock.now + 5_000_000
            trace = m.AckReadTrace(clock.now, clock.now, deadline, dict(sequence=278))
            with patch.object(m, "time", clock):
                trace.begin_poll()
                try:
                    m.sim_state(missing, deadline, trace)
                except m.ReadDeadline as error:
                    trace.end_poll(error)
                    trace.failed(error)
                else:
                    self.fail("missing state must not renew the original read deadline")
            report = trace.report()
            self.assertEqual(clock.now, deadline)
            self.assertEqual(report["openAttempts"], 5)
            self.assertEqual(report["winErrors"], {"2": 5})
            self.assertEqual(report["phaseNs"]["state_retry_wait"], 5_000_000)
            self.assertEqual(report["failure"]["kind"], "ReadDeadline")
            self.assertIsNone(report["lastState"])

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

    @contextmanager
    def fixed_capture_driver(self, clock, deliveries):
        # Real guard, ownership, observation admission, geometry, dispatch and
        # command transport; only external process/file publications are modeled.
        with self.ack_driver(clock) as h:
            d = h.driver
            d.geometry, d.mapping, d.stimulus, d.profiles = fixtures.g, None, None, None
            d.target_observed = d.neutral_motion_supported = False
            d.args = types.SimpleNamespace(family="fixed", stage="Reload", target="secondary",
                                          step_ms=40, pickup_first=False)
            d.boundaries, d.plans = {}, []
            pending = iter(deliveries)
            next_delivery = next(pending, None)
            read_state = m.sim_state
            def publish_observations(path, until, trace=None):
                nonlocal next_delivery
                state = read_state(path, until, trace)
                while next_delivery is not None and clock.now >= next_delivery["drainedNs"]:
                    d.observer.inbox.put({k: v for k, v in next_delivery.items() if k != "drainedNs"})
                    next_delivery = next(pending, None)
                return state
            with patch.object(m, "sim_state", side_effect=publish_observations):
                yield h

    def fixed_capture_deliveries(self, start):
        ready = fixtures.xf.refresh(fixtures.complete(fixtures.xf.snapshot(kind=3)), start+300_000_000, 102)
        pending = fixtures.xf.refresh(ready, start+160_000_000, 101)
        pending["fixedContainer"] = dict(schema=fixtures.g.fixed.OBSERVATION_SCHEMA,
                                         available=False, reason="capture_pending_or_not_requested")
        result = []
        for index, (snapshot, received, requested) in enumerate(((pending, start+160_000_000, start),
                                                               (ready, start+300_000_000, start+161_000_000))):
            result.append(dict(kind="snapshot", index=index, snapshot=snapshot,
                raw=json.dumps(snapshot).encode(), requestNs=requested, deadlineNs=requested+m.RECEIVE_NS,
                receivedNs=received, ownershipCheckedNs=received, drainedNs=received,
                requestNonce=int(snapshot["nonce"]), requestOwned=True, responseOwned=True))
        return result

    def test_cold_fixed_capture_reaches_real_reload_plan_and_first_command(self):
        clock = Clock()
        start = clock.now
        with self.fixed_capture_driver(clock, self.fixed_capture_deliveries(start)) as h:
            d = h.driver
            fresh = d.fresh
            class FirstBoundary(Exception):
                pass
            def stop_at_boundary(**kwargs):
                if kwargs.get("boundary") is not None:
                    raise FirstBoundary()
                result = fresh(**kwargs)
                # Later native publications are deliberately absent in this
                # test. Never invent a completed mechanical boundary.
                h.publish_observations = False
                return result
            d.fresh = stop_at_boundary
            with self.assertRaises(FirstBoundary):
                d.run_stimulus()
            self.assertGreater(clock.now, start+m.RECEIVE_NS)
            self.assertEqual(d.plans[0]["stage"], "Open")
            self.assertEqual(d.mapping["nonce"], 102)
            self.assertTrue(h.published)
            self.assertEqual(h.publication_deadlines[0][1], d.initial_mapping_deadline)
            for requested, published_until, ack_until in h.publication_deadlines:
                self.assertEqual(ack_until, requested+m.ACK_NS)
                self.assertLessEqual(published_until, ack_until)
            self.assertEqual(d.last_commit, d.last_write+h.state_read_ns)
            self.assertFalse(d.pending_command)
            self.assertFalse(d.plans[0].get("stimulusCompleted", False))
            self.assertEqual(d.boundaries, {})
            event = next(e for e in d.artifacts.events if e["type"] == "boundary_pending")
            self.assertEqual(event["id"], "fixed_capture_readiness")
            self.assertEqual(event["deadlineNs"], start+160_000_000+m.RECEIVE_NS)
            ready = next(e for e in d.artifacts.events if e["type"] == "fixed_capture_ready")
            self.assertEqual(ready["finishedNs"], start+300_000_000)

    def retained_close_deliveries(self, start):
        deliveries = self.fixed_capture_deliveries(start)
        ready = deliveries[1]["snapshot"]
        ready["fixedContainer"]["tracking"].update(step=3, lifetime=1, revision=60260, blocksFire=True)
        ready["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1, reserve=6)
        # Sanitized normal85/04 f117 observation000 shape. Accepted native gate
        # rows are compact publications: they contain no retained step or view.
        for index, delivery in enumerate(deliveries):
            s, now = delivery["snapshot"], delivery["receivedNs"]
            s["weapon_actions_blocked"] = True
            binding = copy.deepcopy(ready["fixedContainer"]["view"]["binding"])
            binding.update(observedNs=now-2_000_000, deadlineNs=now+70_000_000)
            source = s["input_evidence"]
            proof = dict(binding=binding, handOwner=copy.deepcopy(source["owner"]), lifetime=1,
                         revision=60253 if index == 0 else 60260, inputSequence=source["sequence"],
                         observedNs=source["observedNs"], deadlineNs=source["deadlineNs"],
                         blocksFire=True, closedVerified=False)
            window = dict(windowEpoch=4, windowBeginNs=now-1_000_000, windowEndNs=now+m.NS)
            s["fixedGateAdmission"] = dict(schema="fvr.bc2.fixed-gate-observation.v1", available=True,
                **window, latest=dict(**window, refusal="accepted", beginNs=now-500_000,
                                     endNs=now-400_000, publication=proof))
            for name in ("feedGateAdmission", "shellPump", "magazine", "bolt"):
                s[name] = dict(available=False)
            gun = copy.deepcopy(ready["fixedContainer"]["tracking"]["gun"])
            gun.update(inputSequence=source["sequence"], deadlineNs=source["deadlineNs"])
            s["bodyHolster"] = dict(available=True, nativeOwner=copy.deepcopy(s["owner"]),
                inputMatchesSnapshot=True, blockWeaponActions=False, leftClaim=None,
                physicalGun=copy.deepcopy(gun["token"]["item"]), rightClaim=gun)
            delivery["raw"] = json.dumps(s).encode()
        return deliveries

    def test_retained_close_capture_reaches_typed_close_before_first_input(self):
        for stage in ("Close", "Reload"):
            with self.subTest(stage=stage):
                clock = Clock()
                deliveries = self.retained_close_deliveries(clock.now)
                original = copy.deepcopy(deliveries)
                with self.fixed_capture_driver(clock, deliveries) as h:
                    d = h.driver
                    d.args.stage = stage
                    fresh = d.fresh
                    class FirstBoundary(Exception):
                        pass
                    def stop_at_boundary(**kwargs):
                        if kwargs.get("boundary") is not None:
                            raise FirstBoundary()
                        result = fresh(**kwargs)
                        self.assertEqual(h.published, [])
                        self.assertEqual(result[2]["stage"], "Close")
                        self.assertEqual(result[0]["fixedContainer"]["tracking"]["step"], 3)
                        h.publish_observations = False
                        return result
                    d.fresh = stop_at_boundary
                    with self.assertRaises(FirstBoundary):
                        d.run_stimulus()
                    self.assertTrue(h.published)
                    self.assertEqual(d.plans[0]["stage"], "Close")
                    self.assertEqual(d.mapping["nonce"], 102)
                    self.assertEqual(h.publication_deadlines[0][1], d.initial_mapping_deadline)
                    self.assertEqual(d.initial_mapping_deadline,
                                     fixtures.g.source_deadline(deliveries[1]["snapshot"], "fixed"))
                    self.assertEqual(d.boundaries, {})
                    self.assertEqual(deliveries, original)

    def test_retained_close_capture_refusals_never_authorize_input(self):
        faults = ("owner", "context", "input", "expired", "unaccepted", "missing_gate", "lifetime",
                  "closed", "feed", "holster", "gun", "stage", "followup_owner", "followup_lifetime",
                  "followup_context", "followup_gate_lifetime", "followup_step", "followup_pending", "followup_raw", "followup_block",
                  "followup_absent", "no_followup")
        for fault in faults:
            with self.subTest(fault=fault):
                clock = Clock()
                deliveries = self.retained_close_deliveries(clock.now)
                first, second = [row["snapshot"] for row in deliveries]
                proof = first["fixedGateAdmission"]["latest"]["publication"]
                x = second["fixedContainer"]
                if fault == "owner": proof["binding"]["owner"]["weapon"] += 1
                elif fault == "context": proof["binding"]["context"]["resource"]["actorGeneration"] += 1
                elif fault == "input": proof["inputSequence"] -= 1
                elif fault == "expired": proof["deadlineNs"] = deliveries[0]["receivedNs"]
                elif fault == "unaccepted": first["fixedGateAdmission"]["latest"]["refusal"] = "resource_lock"
                elif fault == "missing_gate": first.pop("fixedGateAdmission")
                elif fault == "lifetime": proof["lifetime"] = 0
                elif fault == "closed": proof["closedVerified"] = True
                elif fault == "feed": first["feedGateAdmission"]["available"] = True
                elif fault == "holster": first["bodyHolster"]["blockWeaponActions"] = True
                elif fault == "gun": first["bodyHolster"]["rightClaim"]["token"]["item"]["generation"] += 1
                elif fault == "followup_owner": second["owner"]["weapon"] += 1
                elif fault == "followup_lifetime": x["tracking"]["lifetime"] += 1
                elif fault == "followup_context":
                    x["view"]["binding"]["context"]["resource"]["weaponGeneration"] += 1
                    x["view"]["snapshot"]["context"] = copy.deepcopy(x["view"]["binding"]["context"])
                elif fault == "followup_gate_lifetime":
                    second["fixedGateAdmission"]["latest"]["publication"]["lifetime"] += 1
                elif fault == "followup_step": x["tracking"]["step"] = 2
                elif fault == "followup_pending": x["tracking"]["pending"] = True
                elif fault == "followup_raw": x["raw"]["admittedAtTick"] = False
                elif fault == "followup_block": second["bodyHolster"]["blockWeaponActions"] = True
                elif fault == "followup_absent": second["fixedContainer"] = copy.deepcopy(first["fixedContainer"])
                elif fault == "no_followup": deliveries.pop()
                for row in deliveries:
                    row["raw"] = json.dumps(row["snapshot"]).encode()
                with self.fixed_capture_driver(clock, deliveries) as h:
                    h.driver.args.stage = "Open" if fault == "stage" else "Reload"
                    with self.assertRaises((m.Refusal, fixtures.g.fixed.Refusal)):
                        h.driver.run_stimulus()
                    self.assertEqual(h.published, [])
                    self.assertEqual(h.driver.commands, 0)
                    self.assertEqual(h.driver.plans, [])
                    self.assertLessEqual(clock.now, 1_410_000_000)
                    if fault == "no_followup":
                        self.assertTrue(h.driver.observation_incomplete)
                        self.assertEqual(h.driver.pending_boundary["deadlineNs"], 1_410_000_000)

    @unittest.skipUnless(os.environ.get("BC2_FIXED_CLOSE_REPLAY"), "pinned normal85/04 evidence required")
    def test_actual_retained_close_waits_passively_and_original_complete_copy_plans_close(self):
        root = Path(os.environ["BC2_FIXED_CLOSE_REPLAY"])
        close = root / "live-driver-f1171c7020354f1e9e8a2902ccd1ddf8"
        reload = root / "live-driver-8f7c83785cbe402eb087bfb721b58694"
        paths = ((close / "observation-000.json.gz", "a2167ed624acf521123413c9d5ee7636b65132ff4820fb4813d008fb444b8dc5"),
                 (reload / "observation-060.json.gz", "bbb1578e6afe0ea32bba7af8add258f0f9ff91b94ea729030a59f8ae83b70e2f"))
        snapshots = []
        for path, digest in paths:
            raw = path.read_bytes()
            self.assertEqual(m.transport.digest(raw), digest)
            snapshots.append(json.loads(m.gzip.decompress(raw)))
        pending, complete = snapshots
        original = json.loads((close / "result.json").read_bytes())
        received = next(json.loads(line) for line in (close / "events.jsonl").read_bytes().splitlines()
                        if json.loads(line).get("type") == "native_observation")
        self.assertEqual(original["commands"], 0)
        self.assertEqual(original["error"], "weapon actions blocked without an admitted retained mechanism")
        clock = Clock()
        clock.now = original["pendingBoundary"]["startedNs"]
        delivery = {k: received[k] for k in ("index", "requestNs", "deadlineNs", "receivedNs",
            "ownershipCheckedNs", "requestNonce", "requestOwned", "responseOwned", "drainedNs")}
        delivery.update(kind="snapshot", snapshot=pending, raw=m.gzip.decompress(paths[0][0].read_bytes()))
        with self.fixed_capture_driver(clock, [delivery]) as h:
            h.driver.args.stage = "Close"
            with self.assertRaisesRegex(m.Refusal, "fresh typed stimulus boundary absent within original budget"):
                h.driver.run_stimulus()
            self.assertEqual(h.published, [])
            self.assertEqual(h.driver.commands, 0)
            self.assertEqual(h.driver.plans, [])
            self.assertTrue(h.driver.observation_incomplete)
            event = next(e for e in h.driver.artifacts.events if e["type"] == "boundary_pending")
            self.assertEqual(event["reason"], "initial fixed-container capture pending")
            self.assertEqual(event["deadlineNs"], received["receivedNs"]+m.RECEIVE_NS)
        # The older complete recording is checked at its own original time.
        # It is never substituted as a fresh follow-up to the later Close run.
        evaluated = next(json.loads(line)["drainedNs"] for line in (reload / "events.jsonl").read_bytes().splitlines()
                         if json.loads(line).get("index") == 60)
        plan = fixtures.g.build_stimulus("fixed", None, complete, evaluated, "Close", step_ms=40)
        proof = pending["fixedGateAdmission"]["latest"]["publication"]
        self.assertEqual(complete["owner"], pending["owner"])
        self.assertEqual(complete["fixedContainer"]["tracking"]["lifetime"], proof["lifetime"])
        self.assertEqual(plan["stage"], "Close")
        self.assertFalse(plan["nativeAcceptance"])

    def test_fixed_capture_followup_preserves_gates_and_never_retries_absence(self):
        for fault, message in (("owner", "owner/equipment/rig changed"),
                               ("basis", "reference/head/right-controller input changed"),
                               ("absent", "still absent after original follow-up"),
                               ("input", "did not advance native input"),
                               ("request", "competing snapshot request writer"),
                               ("source", "original budget"),
                               ("late", "original budget")):
            with self.subTest(fault=fault):
                clock = Clock()
                deliveries = self.fixed_capture_deliveries(clock.now)
                second = deliveries[1]
                if fault == "owner":
                    second["snapshot"]["owner"]["weapon"] += 1
                elif fault == "basis":
                    second["snapshot"]["input"]["hands"][1]["grip"]["position"][0] += .1
                elif fault == "absent":
                    second["snapshot"]["fixedContainer"] = copy.deepcopy(deliveries[0]["snapshot"]["fixedContainer"])
                elif fault == "input":
                    second["snapshot"]["input_evidence"]["sequence"] = 101
                elif fault == "request":
                    second["requestOwned"] = False
                elif fault == "source":
                    second["snapshot"]["valid_until_ns"] = second["receivedNs"]
                else:
                    second["drainedNs"] = clock.now+500_000_000
                second["raw"] = json.dumps(second["snapshot"]).encode()
                with self.fixed_capture_driver(clock, deliveries) as h:
                    with self.assertRaisesRegex(m.Refusal, message):
                        h.driver.run_stimulus()
                    self.assertEqual(h.published, [])
                    self.assertEqual(h.driver.commands, 0)
                    self.assertLessEqual(clock.now, 1_410_000_000)

    def test_fixed_capture_cannot_extend_admission_after_complete_copy(self):
        clock = Clock()
        deliveries = self.fixed_capture_deliveries(clock.now)
        with self.fixed_capture_driver(clock, deliveries) as h:
            original_build = fixtures.g.build_stimulus
            def slow_build(*args, **kwargs):
                plan = original_build(*args, **kwargs)
                clock.now = plan["source"]["validUntilNs"]
                return plan
            with patch.object(fixtures.g, "build_stimulus", side_effect=slow_build), \
                    self.assertRaisesRegex(m.Refusal, "initial boundary validation consumed original deadline"):
                h.driver.run_stimulus()
            self.assertEqual(h.published, [])
            self.assertEqual(h.driver.pending_boundary["id"], "stage_admission")
            self.assertEqual(h.driver.pending_boundary["deadlineNs"], 1_300_000_000+m.RECEIVE_NS)

    @unittest.skipUnless(os.environ.get("BC2_FIXED_CAPTURE_REPLAY"), "pinned normal84/02 evidence required")
    def test_actual_normal84_02_capture_refusal_replay_does_not_claim_reload(self):
        root = Path(os.environ["BC2_FIXED_CAPTURE_REPLAY"])
        pinned = {"observation-000.json.gz": "16a272e4c9cf801f62b3a88f6015231c099db116c9e03afb6589e87070943afa",
                  "events.jsonl": "1abf8620cfce3dc33be499698b380c81c340c77d9597b8919f80cfc50e5307a4",
                  "result.json": "250c486ac04f0bc706063d5501696f775ff62212dab28bc30b9f5e9f59d54c47"}
        raw = {name: (root/name).read_bytes() for name in pinned}
        for name, digest in pinned.items():
            self.assertEqual(m.transport.digest(raw[name]), digest)
        snapshot_raw = m.gzip.decompress(raw["observation-000.json.gz"])
        snapshot = json.loads(snapshot_raw)
        events = [json.loads(line) for line in raw["events.jsonl"].splitlines()]
        original = json.loads(raw["result.json"])
        received, failure = events[0], events[-1]
        self.assertEqual((original["status"], original["commands"]), ("refused", 0))
        self.assertEqual(original["error"], "original acknowledgement/read deadline exceeded")
        for explicit_original_budget in (True, False):
            with self.subTest(explicit_original_budget=explicit_original_budget):
                class ReplayClock(Clock):
                    def sleep(self, seconds):
                        end = self.now + int(seconds*m.NS)
                        for checkpoint in (received["drainedNs"], failure["startedNs"]):
                            if self.now < checkpoint < end:
                                end = checkpoint
                        self.now = end
                clock = ReplayClock()
                clock.now = original["pendingBoundary"]["startedNs"]
                delivery = {k: received[k] for k in ("index", "requestNs", "deadlineNs", "receivedNs",
                    "ownershipCheckedNs", "requestNonce", "requestOwned", "responseOwned", "drainedNs")}
                delivery.update(kind="snapshot", snapshot=snapshot, raw=snapshot_raw)
                with self.fixed_capture_driver(clock, [delivery]) as h:
                    read = m.transport.read_bytes
                    def measured_read(path, maximum):
                        if path == h.driver.session_path and clock.now == failure["startedNs"]:
                            clock.now = failure["finishedNs"]
                        return read(path, maximum)
                    with patch.object(m.transport, "read_bytes", side_effect=measured_read):
                        with self.assertRaises(m.Refusal) as caught:
                            h.driver.fresh(prepare=True, fixed_reload=True,
                                deadline_ns=failure["deadlineNs"] if explicit_original_budget else None)
                    if explicit_original_budget:
                        self.assertEqual(str(caught.exception), original["error"])
                        self.assertEqual(clock.now, failure["finishedNs"])
                    else:
                        self.assertEqual(str(caught.exception), "fresh typed stimulus boundary absent within original budget")
                        self.assertGreater(clock.now, failure["finishedNs"])
                        self.assertLessEqual(clock.now, received["receivedNs"]+m.RECEIVE_NS+1_000_000)
                    self.assertEqual(h.driver.pending_boundary["id"], "fixed_capture_readiness")
                    self.assertEqual(h.driver.commands, 0)
                    self.assertEqual(h.published, [])
                    self.assertEqual(h.driver.plans, [])
                    self.assertTrue(h.driver.observation_incomplete)
                    self.assertEqual(h.driver.artifacts.saved["observation-000.json.gz"], snapshot_raw)

    def test_real_geometry_contracts_cross_transport_interface(self):
        for family, source, stage in (
            ("pump", fixtures.complete(fixtures.pf.snapshot(pumping=True)), "PumpCycle"),
            ("fixed", fixtures.complete(fixtures.xf.snapshot(kind=3)), "Open"),
            ("sight", fixtures.complete(fixtures.sf.snapshot()), "SightFlip")):
            with self.subTest(family=family):
                stimulus = fixtures.g.build_stimulus(family, None, source, fixtures.NOW, stage)
                m.validate_stimulus(stimulus)

    def fixed_completion_snapshot(self, driver, boundary, now, sequence):
        source, geometry = driver.mapping, driver.stimulus["geometryEvidence"]
        if boundary == "fixed_inserted":
            snapshot = fixtures.xf.completed(source, geometry, now)
            proof = driver.boundaries["fixed_aligned"]["gate"]
            aligned = proof["completionPrerequisite"]
            snapshot["fixedContainer"]["tracking"]["revision"] = aligned["revision"] + 1
            snapshot["fixedContainer"]["raw"]["admittedAtTick"] = False
            intent = snapshot["fixedContainer"]["lastCompletion"]["outcome"]["request"]["intent"]
            intent["observedNs"] = max(intent["observedNs"], proof["observedNs"] + 1)
            return snapshot
        if boundary == "fixed_held":
            source = fixtures.xf.held_after_pickup(source, geometry, now)
        snapshot = fixtures.xf.refresh(source, now, sequence)
        fixed = snapshot["fixedContainer"]
        tracking, raw = fixed["tracking"], fixed["raw"]
        snapshot["input"]["hands"][0]["squeeze"] = 1.
        for evidence in (snapshot["input_evidence"], fixed["input"], raw["input"]):
            evidence["released"][0] = False
        tracking["grip"] = raw["grip"] = True
        if boundary == "fixed_acquired":
            contact = fixed["profile"]["geometry"][tracking["step"]]["contact"]["role"]
            tracking["hand"] = fixtures.xf.claim(raw["input"], 0, 4, tracking["gun"]["token"]["item"],
                dict(id=contact, generation=fixed["profile"]["revision"]), 201, tracking["gun"]["token"]["id"])
            fixed["leftClaim"] = copy.deepcopy(tracking["hand"])
            tracking["target"] = copy.deepcopy(raw["weaponFromWrist"])
        elif boundary == "fixed_step_complete":
            tracking["step"] = 2 if driver.args.stage == "Open" else 4
            tracking.update(hand=None, target=None, carried=False, carriedSupply=None)
            tracking["revision"] = driver.boundaries["fixed_acquired"]["gate"]["completionPrerequisite"]["revision"] + 1
            fixed["leftClaim"] = None
            raw["admittedAtTick"] = False
            tracking["blocksFire"] = snapshot["weapon_actions_blocked"] = tracking["step"] != 4
        elif boundary == "fixed_aligned":
            destination = fixed["profile"]["geometry"][2]["motion"]["placement"]["weaponFromDestination"]
            tracking["target"] = copy.deepcopy(destination)
            tracking["carriedSupply"] = copy.deepcopy(destination)
        return snapshot

    @contextmanager
    def completion_driver(self, clock, stage, *, pickup_first=False, maximum_gesture_ns=None):
        # Full declared fixed plans, real fresh-boundary validation and ordinary
        # send/ownership/ACK paths; only native observations are synthetic.
        clock.now = fixtures.NOW
        source = fixtures.complete(fixtures.xf.snapshot(kind=3))
        if maximum_gesture_ns is not None:
            for geometry in source["fixedContainer"]["profile"]["geometry"]:
                if geometry is not None:
                    geometry["motion"]["policy"]["maximumGestureNs"] = maximum_gesture_ns
        if stage in ("Pickup", "Insert", "Close"):
            source["fixedContainer"]["tracking"]["step"] = 3 if stage == "Close" else 2
            source["fixedContainer"]["tracking"]["blocksFire"] = source["weapon_actions_blocked"] = True
        if stage == "Close":
            source["fixedContainer"]["view"]["snapshot"]["counts"].update(loaded=1, reserve=6)
        with self.ack_driver(clock, archive_ns=0) as h:
            driver = h.driver
            driver.args = types.SimpleNamespace(family="fixed", stage=stage, target="secondary",
                                               pickup_first=pickup_first, step_ms=40)
            driver.geometry, driver.profiles = fixtures.g, None
            driver.plans, driver.boundaries = [], {}
            driver.target_observed = False
            driver.observations = Mock()  # Replace the ACK fixture's untyped observations below.
            h.boundary_fault = lambda snapshot, boundary: None
            h.observation_sequence = 100
            guard = driver.guard

            def fresh(*, boundary=None, after_ns=0, pickup=None, prepare=False):
                def observe(deadline_ns=None):
                    guard(deadline_ns=deadline_ns)
                    clock.now += 2_000_000
                    h.observation_sequence += 1
                    if boundary is None:
                        current = source if pickup is None else fixtures.xf.held_after_pickup(
                            driver.mapping, pickup["geometryEvidence"], clock.now)
                        current = fixtures.xf.refresh(current, clock.now, h.observation_sequence)
                    else:
                        current = self.fixed_completion_snapshot(driver, boundary["id"], clock.now, h.observation_sequence)
                        h.boundary_fault(current, boundary["id"])
                    driver.observed = [dict(snapshot=current, current=True)]
                with patch.object(driver, "guard", side_effect=observe):
                    return m.ReloadStimulus.fresh(driver, boundary=boundary, after_ns=after_ns,
                                                 pickup=pickup, prepare=prepare)

            driver.fresh = fresh
            yield h

    def test_full_fixed_open_and_close_release_only_after_verified_completion(self):
        for stage in ("Open", "Close"):
            with self.subTest(stage=stage), self.completion_driver(Clock(), stage) as h:
                driver = h.driver
                driver.run_stimulus()
                declared = driver.artifacts.saved["stimulus-01.json"]
                self.assertEqual(declared["finishGrip"], 1)
                self.assertEqual([b["id"] for b in declared["boundaries"]], ["fixed_acquired", "fixed_step_complete"])
                self.assertEqual(h.published[-1].decode().splitlines(), ["grip l 0", driver.sentinel])
                self.assertEqual(sum(raw.startswith(b"grip l 0\n") for raw in h.published), 1)
                events = driver.artifacts.events
                completed = next(i for i, e in enumerate(events) if e["type"] == "fresh_stimulus_boundary"
                                 and e["id"] == "fixed_step_complete")
                released = next(i for i, e in enumerate(events) if e["type"] == "command_request"
                                and e["commands"][0] == "grip l 0")
                self.assertLess(completed, released)
                self.assertFalse(events[-1]["cleanup"])
                self.assertFalse(driver.held_left or driver.pending_command)
                self.assertTrue(driver.plans[-1]["stimulusCompleted"])
                self.assertTrue(driver.plans[-1]["boundaryChecksPassed"])
                self.assertTrue(driver.boundaries["fixed_step_complete"]["completionOnly"])
                self.assertTrue(driver.completion_only_observed)
                with self.assertRaisesRegex(m.Refusal, "completion-only observation"):
                    driver.send(driver.stimulus["schedule"][-1])
                self.assertEqual(h.owners[-1]["pending"], False)
                self.assertEqual(h.owners[-1]["commandSha256"], m.transport.digest(h.published[-1]))

    @contextmanager
    def five_stage_clock_driver(self, clock, fault=None, *, legacy_budget=False):
        # Actual normal86 plan sizes/timing, with synthetic poses and native
        # boundaries. Dispatch, schedule, neutral motion, command ownership and
        # ACK deadlines execute together; this is not a native receipt replay.
        stages = [("Open", 23, 17), ("EjectSpent", 10, None), ("Pickup", 35, 33),
                  ("Insert", 34, None), ("Close", 39, 33)]
        with self.ack_driver(clock, archive_ns=0) as h:
            d, started = h.driver, clock.now
            d.args = types.SimpleNamespace(family="fixed", stage="Reload", target="secondary", pickup_first=False)
            duration = m.MAX_STAGE_NS if legacy_budget else m.invocation_duration_ns("fixed", "Reload")
            d.hard_deadline = started + duration
            d.deadline = d.stage_deadline = d.hard_deadline - m.CLEANUP_RESERVE_NS
            d.started, d.plans, d.boundaries = started, [], {}
            d.target_observed = False
            d.neutral_motion_supported = True
            d.geometry = types.SimpleNamespace(source_deadline=lambda s, family: s["validUntilNs"],
                                              neutral_motion=lambda p: p["testMotion"])
            d.observations = Mock()
            h.publish_observations = False
            h.state_read_ns = 100_000_000
            h.state["handR"].update(pos=[.2, 1.3, -.35], ypr=[0., 0., 0.], followsHead=False)
            d.initial_sim = copy.deepcopy(h.state)
            d.state = copy.deepcopy(h.state)
            h.stage_deadlines = []

            def gate(identifier, index, held):
                return dict(id=identifier, afterSample=index, holdGrip=held, requiresFreshSnapshot=True,
                            timeoutNs=2*m.NS if identifier != "fixed_tilt_current" else m.RECEIVE_NS)

            def fresh(*, boundary=None, after_ns=0, pickup=None, prepare=False, **kwargs):
                until = min(d.deadline, d.stage_deadline,
                            clock.now + (boundary["timeoutNs"] if boundary else m.RECEIVE_NS))
                clock.sleep(.15)  # Fresh response at the existing100ms request cadence.
                d.bounds()
                m.require(clock.now < until, "original fresh boundary deadline")
                snapshot = dict(nonce=str(clock.now), validUntilNs=clock.now+60_000_000,
                                fixedContainer=dict(profile=dict(kind=3)))
                if boundary is not None:
                    h.stage_deadlines.append(d.stage_deadline)
                    if fault == "cap" and d.args.stage == "Close" and boundary["id"] == "fixed_acquired":
                        d.artifacts.raw_bytes = 4*m.MAX_BYTES
                        d.artifacts.snapshot("genuine-overflow.gz", b"x")
                    if fault in ("stage", "overall") and d.args.stage == "Close":
                        clock.now = d.stage_deadline if fault == "stage" else d.deadline
                        d.bounds()
                    ejected = d.args.stage == "EjectSpent" and d.stimulus["tiltExpectedSample"]["atNs"] == 360_000_000
                    if boundary["id"] != "neutral_motion_contact":
                        d.boundaries[boundary["id"]] = dict(gate=dict(caseEjected=ejected))
                    if kwargs.get("neutral_readiness") is not None:
                        d.mapping = snapshot
                    return snapshot, clock.now
                stage, count, neutral_end = stages[len(d.plans)]
                continued = stage == "Insert"
                rows = []
                origin = h.state["handR" if stage == "EjectSpent" else "handL"]["pos"][0]
                for index in range(count):
                    held = continued and index < count-1 or neutral_end is not None and index > neutral_end
                    row = sample(index*40_000_000, "move", int(held), origin+index*.001)
                    if stage == "EjectSpent":
                        row.update(hand="r", aimPositionMeters=row["positionMeters"],
                                   aimYawPitchRollDegrees=row["yawPitchRollDegrees"])
                        row["commands"] = [line.replace("hand l ", "hand r ") for line in row["commands"]]
                    rows.append(row)
                if stage == "EjectSpent":
                    rows[0]["commands"].insert(0, "hand r follow off")
                    gates = [gate("fixed_tilt_current", 0, False), gate("fixed_case_ejected", count-1, False)]
                elif stage == "Insert":
                    gates = [gate("fixed_aligned", count-3, True), gate("fixed_inserted", count-1, False)]
                else:
                    rows[0]["commands"] = m.LEFT_POSE_SETUP + rows[0]["commands"]
                    gates = [gate("fixed_held", count-1, True)] if stage == "Pickup" else [
                        gate("fixed_acquired", neutral_end+1, True), gate("fixed_step_complete", count-1, True)]
                motion = None if neutral_end is None else dict(firstSample=0, lastSample=neutral_end,
                    durationNs=neutral_end*40_000_000, durationMs=neutral_end*40)
                value = dict(schema="bc2vr.reload-stimulus.v1", family="fixed", stage=stage,
                    nativeAcceptance=False, headsetAcceptance=False, continuesHeldCustody=continued,
                    source=dict(validUntilNs=snapshot["validUntilNs"]), durationNs=rows[-1]["atNs"],
                    budget=dict(totalNs=10*m.NS, maximumGestureNs=10*m.NS, maximumCommands=512, gestureStartSample=0),
                    schedule=rows, boundaries=gates, finishGrip=rows[-1]["grip"], testMotion=motion,
                    geometryEvidence=dict(schedule=copy.deepcopy(rows), selection={}), mapping=dict(mechanismSelection={}))
                if fault == "source" and stage == "Close":
                    value["source"]["validUntilNs"] = clock.now
                return snapshot, clock.now, value

            original_publish = m.transport.atomic.side_effect
            def publish(path, raw, **kwargs):
                original_publish(path, raw, **kwargs)
                for line in raw.decode().splitlines():
                    if line.startswith("hand r grip pose "):
                        pose = list(map(float, line.split()[4:]))
                        h.state["handR"].update(pos=pose[:3], ypr=pose[3:])

            def state_read(state):
                if getattr(d, "outstanding_motion", None) and not d.pending_command:
                    row = d.stimulus["schedule"][d.stimulus["testMotion"]["lastSample"]]
                    h.state["handL"].update(pos=row["positionMeters"], ypr=row["yawPitchRollDegrees"])
                    h.state["frame"] += 1
                    h.state["endFrames"] += 1
                    state = copy.deepcopy(h.state)
                return state

            d.fresh, h.alter_state = fresh, state_read
            d.motion_guard = lambda until: d.bounds()
            if fault == "ack":
                h.alter_ack = lambda raw: b"unacknowledged\n" if d.args.stage == "Close" else raw
            lease = types.SimpleNamespace(close=Mock())
            with patch.object(m, "MotionInputLease", return_value=lease), \
                    patch.object(m.transport, "atomic", side_effect=publish):
                yield h

    def test_five_stage_reload_fits_aggregate_clock_with_real_ack_delays(self):
        clock = Clock()
        with self.five_stage_clock_driver(clock) as h:
            d = h.driver
            original = d.deadline, d.hard_deadline
            d.run_stimulus()
            self.assertEqual([p["stage"] for p in d.plans], ["Open", "EjectSpent", "Pickup", "Insert", "Close"])
            self.assertTrue(all(p["stimulusCompleted"] and p["boundaryChecksPassed"] for p in d.plans))
            self.assertGreater(clock.now-d.started, 10*m.NS)
            self.assertLess(clock.now, d.deadline)
            self.assertEqual((d.deadline, d.hard_deadline), original)
            self.assertFalse(d.held_left or d.pending_command)
            self.assertLess(d.commands, m.MAX_COMMANDS)
            self.assertTrue(all(ack == requested+m.ACK_NS for requested, _, ack in h.publication_deadlines))
            self.assertTrue(all(publication <= ack for _, publication, ack in h.publication_deadlines))
            d.artifacts.flush()  # The existing journal ceilings still fit.

    def test_five_stage_reload_preserves_original_expiries_and_refusal(self):
        for fault in ("source", "ack", "stage", "overall", "legacy", "cap"):
            clock = Clock()
            with self.subTest(fault=fault), self.five_stage_clock_driver(clock, fault, legacy_budget=fault == "legacy") as h:
                d = h.driver
                original = d.deadline, d.hard_deadline
                with self.assertRaisesRegex(m.Refusal, "deadline|expired|decoded observation budget"):
                    d.run_stimulus()
                self.assertEqual((d.deadline, d.hard_deadline), original)
                self.assertFalse(d.plans[-1].get("stimulusCompleted", False))
                self.assertFalse(any(e.get("cleanup") for e in d.artifacts.events))
                if fault == "cap":
                    self.assertEqual(d.cleanup(), "ordinary owned left-grip release acknowledged")
                    self.assertFalse(d.held_left or d.pending_command)
                    self.assertEqual((d.deadline, d.hard_deadline), original)

    def test_fixed_completion_pending_unverified_or_pre_ack_cannot_release(self):
        for stage in ("Open", "Close"):
            for fault in ("pending", "ammunition", "pre_ack", "identity", "stale_raw", "absent_acquisition"):
                with self.subTest(stage=stage, fault=fault), self.completion_driver(Clock(), stage) as h:
                    driver = h.driver
                    def change(snapshot, boundary):
                        if boundary != "fixed_step_complete":
                            return
                        if fault == "pending":
                            snapshot["fixedContainer"]["tracking"]["step"] = 0 if stage == "Open" else 3
                            snapshot["fixedContainer"]["tracking"]["blocksFire"] = snapshot["weapon_actions_blocked"] = True
                        elif fault == "ammunition":
                            snapshot["fixedContainer"]["view"]["snapshot"]["counts"]["reserve"] -= 1
                        elif fault == "pre_ack":
                            snapshot["input_evidence"]["observedNs"] = driver.last_commit - 1
                        elif fault == "identity":
                            snapshot["fixedContainer"]["view"]["identity"]["serverItem"] += 1
                        elif fault == "stale_raw":
                            snapshot["fixedContainer"]["raw"]["input"]["deadlineNs"] = snapshot["observed_ns"]
                        else:
                            driver.boundaries.pop("fixed_acquired", None)
                    h.boundary_fault = change
                    with self.assertRaises((m.Refusal, fixtures.g.GeometryRefusal)):
                        driver.run_stimulus()
                    self.assertTrue(driver.held_left)
                    self.assertFalse(any(raw.startswith(b"grip l 0\n") for raw in h.published))
                    self.assertNotIn("fixed_step_complete", driver.boundaries)
                    self.assertNotIn("stimulusCompleted", driver.plans[-1])
                    self.assertEqual(driver.pending_boundary["id"], "fixed_step_complete")

    def test_fixed_completion_release_ack_failure_cannot_pass_or_renew_deadline(self):
        for fault in ("ack", "deadline"):
            clock = Clock()
            with self.subTest(fault=fault), self.completion_driver(clock, "Open", maximum_gesture_ns=6*m.NS) as h:
                driver = h.driver
                def change(snapshot, boundary):
                    if boundary == "fixed_step_complete":
                        h.completion_deadline = driver.stage_deadline
                def ack(raw):
                    if not h.command.startswith(b"grip l 0\n"):
                        return raw
                    if fault == "deadline":
                        clock.now = h.completion_deadline
                        return raw
                    return b"unacknowledged\n"
                h.boundary_fault, h.alter_ack = change, ack
                with self.assertRaisesRegex(m.Refusal, "deadline"):
                    driver.run_stimulus()
                self.assertIn("fixed_step_complete", driver.boundaries)
                self.assertTrue(driver.held_left and driver.pending_command)
                self.assertNotIn("stimulusCompleted", driver.plans[-1])
                self.assertEqual(driver.stage_deadline, h.completion_deadline)
                self.assertLess(driver.stage_deadline, driver.deadline)
                self.assertEqual(sum(raw.startswith(b"grip l 0\n") for raw in h.published), 1)
                self.assertFalse(driver._ack_failures[-1].report()["cleanup"])

    def test_fixed_pickup_insert_keeps_custody_until_declared_destination_release(self):
        with self.completion_driver(Clock(), "Insert", pickup_first=True) as h:
            driver = h.driver
            driver.run_stimulus()
            pickup, insertion = (driver.artifacts.saved[f"stimulus-{index:02}.json"] for index in (1, 2))
            self.assertEqual((pickup["finishGrip"], insertion["finishGrip"]), (1, 0))
            self.assertTrue(insertion["continuesHeldCustody"])
            self.assertEqual([p["stage"] for p in driver.plans], ["Pickup", "Insert"])
            self.assertTrue(all(p["stimulusCompleted"] and p["boundaryChecksPassed"] for p in driver.plans))
            self.assertTrue(driver.boundaries["fixed_inserted"]["completionOnly"])
            self.assertFalse(any(raw.startswith(b"grip l 0\n") for raw in h.published))
            grips = [int(line[-1]) for raw in h.published for line in raw.decode().splitlines()
                     if line.startswith("grip l ")]
            self.assertTrue(all(grips[grips.index(1):-1]))
            self.assertEqual(grips[-1], 0)
            self.assertFalse(driver.held_left or driver.pending_command)

    def test_standalone_pickup_still_requires_pair_and_released_feed_is_unchanged(self):
        for family, stage in (("feed", "SupplyPickup"), ("pump", "ShellPickup"), ("fixed", "Pickup"),
                              ("feed", "OpenCover")):
            clock = Clock()
            driver = ScheduleDriver(clock)
            driver.args.family, driver.args.stage, driver.args.pickup_first = family, stage, False
            value = plan(clock)
            value.update(family=family, stage=stage)
            if stage != "OpenCover":
                value["schedule"].pop()
                value.update(finishGrip=1, durationNs=value["schedule"][-1]["atNs"])
            driver.fresh = Mock(return_value=({}, clock.now, value))
            with self.subTest(stage=stage), patch.object(m, "time", clock):
                if stage == "OpenCover":
                    driver.run_stimulus()
                    self.assertEqual(driver.sent[-1][0]["phase"], "release")
                    self.assertFalse(driver.held_left)
                else:
                    with self.assertRaisesRegex(m.Refusal, "paired workflow required"):
                        driver.run_stimulus()
                    self.assertTrue(driver.held_left)
                self.assertFalse(any(sent[2] or sent[3] for sent in driver.sent))

    def test_declared_pump_cycle_uses_same_completion_release(self):
        clock = Clock()
        clock.now = fixtures.NOW
        driver = ScheduleDriver(clock)
        driver.args.family, driver.args.stage = "pump", "PumpCycle"
        source = fixtures.complete(fixtures.pf.snapshot(pumping=True))
        value = fixtures.g.build_stimulus("pump", None, source, clock.now, "PumpCycle")
        self.assertEqual(value["finishGrip"], 1)
        self.assertEqual(value["boundaries"][-1]["id"], "pump_ready")
        with patch.object(m, "time", clock):
            driver.execute(value)
        self.assertEqual(set(driver.boundaries), {"pump_acquired", "pump_rear", "pump_ready"})
        self.assertTrue(driver.sent[-1][3])
        self.assertFalse(driver.sent[-1][2] or driver.held_left)

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
