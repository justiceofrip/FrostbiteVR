"""Assembled synthetic vehicle CLI/transport tests; no native/game acceptance.

Real shared initialization, file publication owner HANDLE, command replacement,
ACK parsing, later-frame commitment and cleanup use temporary files. Only host
liveness and synthetic simulator/native publishers replace external processes.
Set TEMP/TMP to G: scratch when running on the development workstation.
"""
import copy
from contextlib import ExitStack, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


t = load("vehicle_test_transport", ROOT / "tools/drive_bc2_feed_simulation.py")
v = load("vehicle_test_adapter", ROOT / "tools/bc2_vehicle_stimulus.py")


def snapshot(now, nonce, generation, role="driver", state=None):
    pose = dict(position=[0, 1.6, 0], orientation=[0, 0, 0, 1])
    hands = []
    for side, key in enumerate(("handL", "handR")):
        hand_pose = (dict(position=state[key]["pos"][:], orientation=t.planner.ypr_quaternion(state[key]["ypr"]))
                     if state else copy.deepcopy(pose))
        hands.append(dict(grip=hand_pose, aim=copy.deepcopy(hand_pose), gripTracked=True, aimTracked=True,
                          active=127, held=0, touchActive=1, touched=0, stickX=0, stickY=0, trigger=0, squeeze=0))
    actions = (0, 1, 6, 5)
    axes = []
    for index, (semantic, source, concept) in enumerate(v.AXES):
        available = index < 2 if role == "driver" else index >= 2 if role == "gunner" else True
        axes.append(dict(semantic=semantic, available=available,
            reason=None if available else "consumer_unavailable",
            mapping=dict(action=actions[index], concept=concept, consumer=501 if index < 2 else 502,
                         source=source) if available else None))
    owner = dict(zip(v.OWNER, range(101, 101 + len(v.OWNER))))
    # Mirror the frozen1310 JSON field shape, including explicit unavailable
    # exit/seat readiness. Root firearm fields deliberately remain absent.
    obs = dict(observationOnly=True, available=True, reason=None,
        gatherSequence=generation, sampledNs=now-1_000_000,
        inputObservedNs=now-2_000_000, inputDeadlineNs=now+98_000_000, validUntilNs=now+70_000_000,
        owner=owner, routeFingerprint=8877, slot=0, allowedRead=True,
        allowedMask=3 if role == "driver" else 96 if role == "gunner" else 99,
        nativePlaying=True, routesVerified=True, profileVerified=True, consumersVerified=True, role=role,
        consumers=dict(driver=501 if role != "gunner" else 0, camera=502, weaponCount=0 if role == "driver" else 1),
        neutralRearm=dict(controlsActive=True, armedLeft=True, armedRight=True,
                          seatButtonKnown=False, seatButtonReason="button_rearm_unavailable"),
        consumedControls=dict(throttle=0, steer=0, lookYaw=0, lookPitch=0, exitHeld=False),
        capabilities=dict(fire=False, axes=axes,
            exit=dict(available=False, reason="exit_binding_unavailable", action=16, concept=36,
                      routeVerified=True, permissionRead=True, permitted=True, bindingVerified=False,
                      bindingReason="exit_binding_unavailable"),
            seatCycle=dict(available=True, action=17, concept=42, codeVerified=True, routeVerified=True,
                permissionRead=True, permitted=True, nativeFlagsClear=True, requestReady=False,
                requestReason="button_rearm_unavailable", bindingVerified=True, bindingReason=None,
                unboundConcept=True, binding={})), playerPermissionMask=96,
        input=dict(generation=generation, spaceGeneration=4, trackingBasisGeneration=5, predictedNs=now+5_000_000,
            focused=True, headValid=True, floorRelative=True, worldUnitsPerMeter=1,
            referenceHead=copy.deepcopy(pose), head=copy.deepcopy(pose), hands=hands,
            menu=dict(owner=1, sequence=generation, epoch=7, deadlineNs=now+70_000_000)))
    return dict(schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=str(nonce), valid=False,
                invalid_reason="no_shot_publication", observed_ns=now, vehicleObservation=obs)


class AdmissionTests(unittest.TestCase):
    def test_frozen_driver_gunner_and_combined_axis_schema(self):
        for role in ("driver", "gunner", "driver_gunner"):
            s = snapshot(2_000_000_000, 33, 21, role)
            for requested in (("driver", "gunner") if role == "driver_gunner" else (role,)):
                self.assertIs(v.admit(t, s, 2_001_000_000, 33, requested), s["vehicleObservation"])

    def test_vehicle_sample_can_follow_outer_clock_without_clock_rewrite(self):
        s = snapshot(2_000_000_000, 33, 21)
        s["observed_ns"] = "1998000000"
        original = copy.deepcopy(s)
        v.admit(t, s, 2_001_000_000, 33, "driver")
        self.assertEqual(s, original)

    def test_closed_request_schema_and_budget(self):
        cases = [dict(role="driver", stick=[0, .5], holdMs=300),
                 dict(role="driver", stick=[0, .5], holdMs=501),
                 dict(role="driver", stick=[0, .1], holdMs=400),
                 dict(role="driver", stick=[0, float("nan")], holdMs=400),
                 dict(role="driver", stick=[0, 1.1], holdMs=400),
                 dict(role="exit", stick=[0, .5], holdMs=400),
                 dict(role="driver", stick=[0, .5], holdMs=400, action="fire")]
        for row in cases:
            with self.subTest(row=row), self.assertRaises(t.REFUSAL):
                v.plan(row, t.require)


class Clock:
    def __init__(self):
        self.now = 2_000_000_000

    def tick(self):
        self.now += 10_000
        return self.now

    def sleep(self, seconds):
        self.now += int(seconds * t.NS)


@unittest.skipUnless(sys.platform == "win32", "shared publication HANDLE path requires Windows")
class AssembledVehicleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="vehicle-offline-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.run, self.folder = self.root / "run", self.root / "session"
        (self.run / "sim").mkdir(parents=True)
        self.folder.mkdir()
        self.clock, self.driver = Clock(), None
        self.version, self.source = "0.1.synthetic", "a" * 64
        for name in ("host.exe", "xrsim.dll", "runtime.json"):
            (self.root / name).write_bytes(b"synthetic-only")
        pin = lambda name: dict(path=str(self.root / name), bytes=14,
                               sha256=t.digest((self.root / name).read_bytes()))
        self.host = dict(pid=999, path=str(self.root / "host.exe"), start_utc="2026-01-01T00:00:00Z")
        capture = dict(periodic=False, layers=False, maximum_compositions=3, width=640, height=480,
            eyes=2, image_streams=2, bytes_per_pixel=4, metadata_bytes_per_composition=65536,
            raw_bytes=3*640*480*16+3*65536)
        raw = 512*1024*1024+capture["raw_bytes"]
        limits = dict(host_seconds=300, outer_seconds=420, extended_observation=False, tape_max_frames=30000,
            raw_budget_bytes=raw, raw_budget_limit_bytes=660*1024*1024, planned_bytes=raw+128*1024*1024,
            native_maximum_raw_images=8, sim_compositions=3, sim_capture_width=640, sim_capture_height=480)
        self.manifest = dict(schema="fvr.xr-game-run.v1", output_directory=str(self.run),
            version=self.version, source_sha256=self.source, runtime_manifest=pin("runtime.json"),
            runtime_library=pin("xrsim.dll"), host_executable=pin("host.exe"), capture_plan=capture, limits=limits)
        self.write(self.run / "run-manifest.json", self.manifest)
        controls = {k: False for k in ("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
        controls.update(trigL=0, trigR=0, gripL=0, gripR=0, stickL=[0, 0], stickR=[0, 0])
        self.state = dict(pid=999, runtime="xr-sim", errors=0, lastCmdError="", sessionRunning=True,
            actionsAttached=True, sessionState="FOCUSED", paceMode="free", captureSeq=0, cmdSeq=1,
            lastCmd="note startup", frame=1, endFrames=1, controls=controls,
            head=dict(valid=True, pos=[0, 1.6, 0], ypr=[0, 0, 0]),
            handL=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]),
            handR=dict(valid=True, followsHead=False, pos=[.2, 1.4, .1], ypr=[0, 0, 0]))
        self.write(self.run / "sim/state.json", self.state)
        (self.run / "sim/command.txt").write_bytes(b"note startup\n")
        self.write(self.run / "sim-startup.json", dict(root_commands_ready=True, status="ready", host=self.host,
            target_sequence=1, command_file=dict(sha256=t.digest(b"note startup\n")), settled_state=self.state))
        self.link = dict(root_commands_ready=True, host=self.host, supervisor=dict(pid=111), folder=str(self.folder),
            sim_state=str(self.run / "sim/state.json"), command=str(self.run / "sim/command.txt"),
            session=str(self.folder / "session.json"), options=str(self.folder / "options.json"),
            stop_request=str(self.folder / "stop.request"), startup_receipt=str(self.run / "sim-startup.json"),
            mechanism_snapshots=dict(requested=True), capture_plan=capture)
        self.write(self.run / "session-link.json", self.link)
        self.session = dict(host=self.host, supervisor=self.link["supervisor"], source_sha256=self.source,
                            version=self.version, state="running")
        self.write(self.folder / "session.json", self.session)
        self.write(self.folder / "options.json", dict(source_sha256=self.source, version=self.version,
            activate_window=False, xr_manifest=str(self.root / "runtime.json")))
        (self.folder / "mechanism-snapshot.enable").write_bytes(b"1\n")
        self.request = dict(role="driver", stick=[.4, .5], holdMs=400)
        self.calls, self.samples, self.requests, self.owner_records, self.windows = [], [], [], [], []
        self.snapshot_mutation = self.state_mutation = None
        self.no_ack = self.no_later_frame = self.foreign_ack = False
        self.ack_delay = self.publication_delay = 0
        self.no_response = False
        self.response_delay = lambda driver: 0
        self.response_age = 0
        self.omit_response_from = None
        self.before_read = None
        self.cleanup_failure = False
        self.after_press = None

    def write(self, path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def argv(self):
        self.write(self.run / "vehicle.json", self.request)
        return ["--run", str(self.run), "--vehicle-stimulus", str(self.run / "vehicle.json"),
                "--vehicle-version", self.version, "--vehicle-source-sha256", self.source,
                "--root-authorized-ordinary-input"]

    def assembled(self, argv=None):
        current, pending_ack, pending_snapshot = b"note startup\n", None, None
        original_init, original_request = t.Driver.__init__, t.Driver.request_snapshot
        original_publish, original_send = t.publish_command, t.Driver.send
        original_read = t.read_bytes
        test = self

        def simulate():
            nonlocal current, pending_ack
            path = self.run / "sim/command.txt"
            raw = path.read_bytes()
            if raw != current:
                current = raw
                batch = raw.decode("ascii").splitlines()
                if batch[:-1]:
                    self.calls.append(batch[:-1])
                    self.owner_records.append(json.loads((self.run / "live-driver-owner.json").read_text()))
                for line in batch[:-1]:
                    words = line.split()
                    if words[0] != "stick" or words[1] not in ("l", "r") or len(words) != 4:
                        raise AssertionError("unreviewed synthetic vehicle command: " + line)
                    self.state["controls"]["stickL" if words[1] == "l" else "stickR"] = list(map(float, words[2:]))
                self.state.update(cmdSeq=self.state["cmdSeq"]+len(batch), lastCmd=batch[-1], frame=self.state["frame"]+1)
                pending_ack = (self.clock.now+self.ack_delay, batch, self.state["frame"])
            if pending_ack and self.clock.now >= pending_ack[0] and not self.no_ack:
                _, batch, frame = pending_ack
                joined = "foreign" if self.foreign_ack else " | ".join(batch)
                (self.run / "sim/ack.txt").write_text(
                    f"seq={self.state['cmdSeq']} frame={frame} error=-\nlast={joined}\n", encoding="utf-8")
                if not self.no_later_frame:
                    self.state.update(frame=frame+1, endFrames=self.state["endFrames"]+1)
                pending_ack = None
            if self.state_mutation and self.calls:
                self.state_mutation(self.state, self.driver)
            self.write(self.run / "sim/state.json", self.state)

        class Owner:
            def __init__(self, expected):
                self.api = SimpleNamespace(CloseHandle=lambda handle: None)
            def check(self):
                simulate()
            def close(self):
                pass

        def initialize(driver, args):
            self.driver = driver
            original_init(driver, args)

        def request(driver):
            nonlocal pending_snapshot
            original_request(driver)
            self.requests.append(dict(nonce=driver.pending_nonce, input=copy.deepcopy(self.state["controls"]),
                startedNs=driver.last_request, actionDeadlineNs=driver.action_deadline,
                pendingCommand=driver.pending_command, motionStartedNs=driver.motion_started))
            if self.no_response or (self.omit_response_from is not None and driver.snapshot_index >= self.omit_response_from):
                return
            pending_snapshot = self.clock.now + self.response_delay(driver)
            if pending_snapshot <= self.clock.now:
                publish_snapshot(driver)

        def publish_snapshot(driver):
            nonlocal pending_snapshot
            pending_snapshot = None
            now = self.clock.tick() - (self.response_age(driver) if callable(self.response_age) else self.response_age)
            value = snapshot(now, driver.pending_nonce, driver.snapshot_index*100, self.request["role"], self.state)
            if self.snapshot_mutation:
                self.snapshot_mutation(value, driver)
            self.samples.append(copy.deepcopy(value))
            self.write(driver.snapshot_path, value)

        def read(path, *args):
            if self.before_read and self.driver is not None:
                self.before_read(path, self.driver)
            if pending_snapshot is not None and self.clock.now >= pending_snapshot:
                publish_snapshot(self.driver)
            return original_read(path, *args)

        def publish(path, raw, deadline_ns, validate):
            self.windows.append(dict(until=deadline_ns, pulse=driver.action_deadline,
                                     release=driver.releasing, cleanup=driver.cleanup_mode))
            self.clock.now += self.publication_delay(driver) if callable(self.publication_delay) else self.publication_delay
            if self.cleanup_failure and driver.cleanup_mode:
                raise OSError("synthetic zero publication failed")
            return original_publish(path, raw, deadline_ns, validate)

        def send(driver, *args, **kwargs):
            result = original_send(driver, *args, **kwargs)
            if self.after_press and any(driver.stick):
                self.after_press(driver)
            return result

        with ExitStack() as stack:
            stack.enter_context(patch.object(t.time, "perf_counter_ns", self.clock.tick))
            stack.enter_context(patch.object(t.time, "sleep", self.clock.sleep))
            stack.enter_context(patch.object(t, "WindowsOwner", Owner))
            stack.enter_context(patch.object(t.Driver, "acquire_lock", lambda driver: None))
            stack.enter_context(patch.object(t.Driver, "__init__", initialize))
            stack.enter_context(patch.object(t.Driver, "request_snapshot", request))
            stack.enter_context(patch.object(t, "read_bytes", read))
            # Bind after driver construction through the test object.
            def publication(*args):
                nonlocal driver
                driver = self.driver
                return publish(*args)
            driver = None
            stack.enter_context(patch.object(t, "publish_command", publication))
            stack.enter_context(patch.object(t.Driver, "send", send))
            output = stack.enter_context(redirect_stdout(io.StringIO()))
            code = t.main(self.argv() if argv is None else argv)
        return code, json.loads(output.getvalue().strip().splitlines()[-1])

    def test_driver_pulse_ack_and_only_owned_left_zero(self):
        code, result = self.assembled()
        self.assertEqual(code, 0, result)
        self.assertEqual(self.calls, [["stick l 0.4 0.5"], ["stick l 0 0"]])
        self.assertEqual(len(self.requests), 2)
        self.assertTrue(all(v.neutral(r["input"]) for r in self.requests))
        self.assertTrue(all(r["pending"] for r in self.owner_records))
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertFalse(owner["pending"])
        self.assertTrue(owner["vehicleStimulus"]["neutralCommitted"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["headsetAcceptance"])
        episode = result["vehicleStimulus"]
        self.assertLess(episode["pulseCommittedNs"], episode["neutralCommittedNs"])
        self.assertLess(episode["neutralCommittedNs"], episode["originalDeadlineNs"])
        self.assertEqual(result["commands"], 4)

    def test_gunner_pulse_ack_and_only_owned_right_zero(self):
        self.request = dict(role="gunner", stick=[-.4, .3], holdMs=400)
        code, result = self.assembled()
        self.assertEqual(code, 0, result)
        self.assertEqual(self.calls, [["stick r -0.4 0.3"], ["stick r 0 0"]])
        self.assertEqual(self.state["controls"]["stickL"], [0, 0])

    def test_readiness_refusals_before_any_input(self):
        cases = {
            "onfoot": lambda s: s["vehicleObservation"]["owner"].update(controlled=s["vehicleObservation"]["owner"]["soldier"]),
            "unknown_role": lambda s: s["vehicleObservation"].update(role="unclassified"),
            "unknown_consumer": lambda s: s["vehicleObservation"]["consumers"].update(driver=0),
            "route_unverified": lambda s: s["vehicleObservation"].update(routesVerified=False),
            "native_permission": lambda s: s["vehicleObservation"].update(allowedMask=0),
            "no_rearm": lambda s: s["vehicleObservation"]["neutralRearm"].update(armedLeft=False),
            "consumed_active": lambda s: s["vehicleObservation"]["consumedControls"].update(throttle=.5),
            "untracked": lambda s: s["vehicleObservation"]["input"]["hands"][1].update(aimTracked=False),
            "focus": lambda s: s["vehicleObservation"]["input"].update(focused=False),
            "menu": lambda s: s["vehicleObservation"]["input"]["menu"].update(owner=2),
            "expired": lambda s: s["vehicleObservation"].update(validUntilNs=s["observed_ns"]-1),
            "old_root": lambda s: s.update(observed_ns=1),
            "unavailable": lambda s: s["vehicleObservation"].update(available=False, reason="gather_pending", input=None),
            "firearm_only": lambda s: (s.update(valid=True), s.pop("vehicleObservation")),
            "native_nonzero": lambda s: s["vehicleObservation"]["input"]["hands"][0].update(stickX=.5),
            "future_source": lambda s: s["vehicleObservation"].update(sampledNs=s["observed_ns"]+100_000_000),
            "wrong_consumer": lambda s: s["vehicleObservation"]["capabilities"]["axes"][0]["mapping"].update(consumer=0),
        }
        for name, mutation in cases.items():
            with self.subTest(name=name):
                self.snapshot_mutation = lambda s, d: mutation(s)
                self.calls.clear()
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertEqual(self.calls, [])
                self.assertEqual(self.driver.snapshot_index, 1)

    def test_same_seat_route_and_input_continuity_before_input(self):
        cases = {
            "vehicle": lambda x: x["owner"].update(controlled=999),
            "entry": lambda x: x["owner"].update(entry=999),
            "actor": lambda x: x["owner"].update(actorGeneration=999),
            "seat": lambda x: x["owner"].update(seatGeneration=999),
            "input_epoch": lambda x: x["owner"].update(inputEpoch=999),
            "movement_epoch": lambda x: x["owner"].update(movementEpoch=999),
            "route": lambda x: x.update(routeFingerprint=999),
            "reuse_input": lambda x: x["input"].update(generation=100),
            "reuse_gather": lambda x: x.update(gatherSequence=100),
            "menu_epoch": lambda x: x["input"]["menu"].update(epoch=999),
        }
        for name, mutation in cases.items():
            with self.subTest(name=name):
                self.snapshot_mutation = lambda s, d: mutation(s["vehicleObservation"]) if d.snapshot_index == 2 else None
                self.calls.clear()
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertEqual(self.calls, [])
                self.assertEqual(self.driver.snapshot_index, 2)

    def test_head_aim_mapping_does_not_authorize_right_stick(self):
        self.request = dict(role="gunner", stick=[.4, 0], holdMs=400)
        self.snapshot_mutation = lambda s, d: s["vehicleObservation"]["capabilities"]["axes"][2]["mapping"].update(source="head_aim_yaw")
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [])

    def test_authorization_and_source_pins_before_input(self):
        for field in ("authorization", "version", "source"):
            with self.subTest(field=field):
                argv = self.argv()
                if field == "authorization":
                    argv.remove("--root-authorized-ordinary-input")
                else:
                    argv[argv.index("--vehicle-version" if field == "version" else "--vehicle-source-sha256")+1] = "b"*64
                code, result = self.assembled(argv)
                self.assertEqual(code, 2, result)
                self.assertEqual(self.calls, [])

    def test_source_expiry_during_publication_never_retries(self):
        self.publication_delay = 100_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.windows), 1)
        self.assertEqual(len(self.requests), 2)

    def test_source_expiry_after_receive_admission_never_refreshes(self):
        original_init = t.Driver.__init__
        admitted = []

        def initialize(driver, args):
            original_init(driver, args)
            original_usable = driver.snapshot_usable

            def usable(value, now):
                result = original_usable(value, now)
                if driver.snapshot_index == 2:
                    self.assertTrue(result)
                    until = t.integer(value["vehicleObservation"]["validUntilNs"])
                    self.assertLess(self.clock.now, until)
                    admitted.append(copy.deepcopy(value))
                    # Cross the lease only after actual vehicle admission and
                    # before inherited receive samples its evaluation clock.
                    self.clock.now = until
                return result

            driver.snapshot_usable = usable

        with patch.object(t.Driver, "__init__", initialize):
            code, result = self.assembled()

        self.assertEqual(len(admitted), 1)
        self.assertEqual(code, 2, result)
        self.assertIn("expired/invalid original vehicle input/menu clocks", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(self.windows, [])
        self.assertEqual(len(self.requests), 2)
        self.assertEqual(self.driver.snapshot_index, 2)
        self.assertEqual(self.driver.pending_nonce, self.requests[-1]["nonce"])
        self.assertIsNone(self.driver.snapshot_refresh_started)
        self.assertEqual(self.driver.snapshot_refresh_requests, 0)
        self.assertEqual(json.loads((self.driver.artifacts.directory / "snapshot-002.json").read_text()),
                         admitted[0])
        self.assertEqual(admitted[0], self.samples[1])
        self.assertEqual(result["commands"], 0)
        self.assertFalse(result["pendingCommand"])
        self.assertFalse(result["normalStopRequired"])
        self.assertTrue(self.driver.release_attempted)
        self.assertEqual(result["release"], "no owned vehicle stick remains active")
        self.assertEqual(result["vehicleStick"], [0, 0])
        self.assertIsNone(result["pendingVehicleStick"])
        owner = json.loads((self.run / "live-driver-owner.json").read_text())
        self.assertFalse(owner["pending"])
        self.assertTrue(owner["vehicleStimulus"]["neutralCommitted"])

    def test_pending_input_timeout_cannot_claim_or_send_neutral(self):
        self.no_ack = True
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [["stick l 0.4 0.5"]])
        self.assertTrue(result["pendingCommand"])
        self.assertTrue(result["normalStopRequired"])
        self.assertEqual(result["pendingVehicleStick"], [.4, .5])
        self.assertTrue(json.loads((self.run / "live-driver-owner.json").read_text())["pending"])

    def test_ack_requires_later_committed_frame(self):
        self.no_later_frame = True
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertTrue(result["pendingCommand"])
        self.assertEqual(len(self.calls), 1)

    def test_late_ack_settles_only_for_cleanup_and_does_not_retry_pulse(self):
        self.ack_delay = 120_000_000
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [["stick l 0.4 0.5"], ["stick l 0 0"]])
        self.assertFalse(result["pendingCommand"])
        self.assertEqual(result["vehicleStick"], [0, 0])
        self.assertIn("zero ACK", result["release"])
        self.assertNotIn("vehicleStimulus", result)

    def test_focus_loss_after_pulse_still_releases_only_owned_stick(self):
        self.state_mutation = lambda s, d: s.update(sessionState="VISIBLE")
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [["stick l 0.4 0.5"], ["stick l 0 0"]])
        self.assertEqual(result["vehicleStick"], [0, 0])

    def test_native_owner_change_refusal_keeps_command_cleanup_ownership(self):
        def owner_changed(driver):
            changed = copy.deepcopy(driver.latest_snapshot)
            changed["vehicleObservation"]["owner"]["seatGeneration"] += 1
            driver.admit_snapshot(changed, self.clock.tick(), t.integer(changed["nonce"]))
        self.after_press = owner_changed
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertIn("owner/seat/route", result["error"])
        self.assertEqual(self.calls, [["stick l 0.4 0.5"], ["stick l 0 0"]])
        self.assertEqual(result["vehicleStick"], [0, 0])

    def test_session_owner_change_forbids_even_cleanup(self):
        def changed(driver):
            self.session["source_sha256"] = "b"*64
            self.write(self.folder / "session.json", self.session)
            raise t.REFUSAL("synthetic owner changed")
        self.after_press = changed
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])

    def test_foreign_command_owner_or_ack_never_gains_cleanup_authority(self):
        self.foreign_ack = True
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(len(self.calls), 1)
        self.assertTrue(result["normalStopRequired"])
        self.assertTrue(result["pendingCommand"])

    def test_cleanup_failure_is_retained_and_never_retried(self):
        self.state_mutation = lambda s, d: s.update(sessionState="VISIBLE")
        self.cleanup_failure = True
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertTrue(result["normalStopRequired"])
        count = len(self.calls)
        self.assertIn("not retried", self.driver.release_after_failure())
        self.assertEqual(len(self.calls), count)

    def test_same_driver_cannot_issue_second_pulse(self):
        code, result = self.assembled()
        self.assertEqual(code, 0, result)
        with self.assertRaisesRegex(t.REFUSAL, "may not be retried"):
            self.driver.stimulate(self.run / "vehicle.json")
        self.assertEqual(len(self.calls), 2)

    def test_no_snapshot_response_does_not_retry_or_send_input(self):
        self.no_response = True
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.requests), 1)

    def test_either_initial_late_response_is_discarded_for_fresh_successor(self):
        for delayed in (1, 2):
            with self.subTest(delayed=delayed):
                if delayed == 2:
                    self.setUp()
                self.response_delay = lambda d: 375_000_000 if d.snapshot_index == delayed else 0
                code, result = self.assembled()
                self.assertEqual(code, 0, result)
                self.assertEqual(len(self.requests), 3)
                self.assertEqual(self.calls, [["stick l 0.4 0.5"], ["stick l 0 0"]])
                self.assertEqual(result["vehicleStimulus"]["admissionNonce"], self.samples[2]["nonce"])
                self.assertNotEqual(result["vehicleStimulus"]["admissionNonce"], self.samples[delayed-1]["nonce"])
                self.assertTrue(self.driver.attempted)
                self.assertTrue(self.driver.initial_observation_waited)
                self.assertIsNone(self.driver.initial_discarded_snapshot)
                artifacts = Path(result["artifactDirectory"])
                self.assertEqual(json.loads((artifacts / f"snapshot-{delayed:03}-late.json").read_text()),
                                 self.samples[delayed-1])
                if delayed == 2:
                    self.assertEqual(self.driver.vehicle_reference, self.samples[0])
                    self.assertEqual(json.loads((artifacts / "snapshot-002-at-timeout.json").read_text()),
                                     self.samples[0])
                events = [json.loads(line) for line in (artifacts / "events.jsonl").read_text().splitlines()]
                hold = next(e for e in events if e["type"] == "vehicle_initial_observation_hold")
                self.assertEqual(hold["originalResponseDeadlineNs"] - hold["requestStartedNs"], 250_000_000)
                self.assertEqual(hold["observationDeadlineNs"] - hold["requestStartedNs"], 500_000_000)
                self.assertFalse(hold["commandAuthority"])
                self.assertEqual(self.driver.hard_deadline - self.driver.started, 10*t.NS)
                self.assertTrue(all(v.neutral(r["input"]) and r["actionDeadlineNs"] is None
                    and r["motionStartedNs"] is None and not r["pendingCommand"] for r in self.requests))
                episode = result["vehicleStimulus"]
                self.assertEqual(episode["originalDeadlineNs"] - episode["startedNs"], 400_000_000)
                self.assertLess(episode["neutralCommittedNs"], episode["originalDeadlineNs"])
                self.assertFalse(result["nativeAcceptance"])

    def test_late_expired_source_is_only_discarded_and_successor_uses_original_clocks(self):
        self.response_delay = lambda d: 375_000_000 if d.snapshot_index == 2 else 0
        self.response_age = lambda driver: 100_000_000 if driver.snapshot_index == 2 else 0
        code, result = self.assembled()
        self.assertEqual(code, 0, result)
        late = self.samples[1]
        self.assertLess(late["vehicleObservation"]["validUntilNs"], self.requests[2]["startedNs"])
        self.assertEqual(result["vehicleStimulus"]["admissionNonce"], self.samples[2]["nonce"])
        artifacts = Path(result["artifactDirectory"])
        self.assertEqual(json.loads((artifacts / "snapshot-002-late.json").read_text()), late)

    def test_initial_wait_never_renews_invocation_or_stage_deadline(self):
        for field in ("deadline", "stage_deadline"):
            with self.subTest(field=field):
                if field == "stage_deadline":
                    self.setUp()
                self.omit_response_from = 2
                def shorten(path, driver):
                    if driver.snapshot_index == 2:
                        setattr(driver, field, min(getattr(driver, field), driver.last_request + 300_000_000))
                self.before_read = shorten
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertIn("deadline", result["error"])
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.requests), 2)
                self.assertLess(self.clock.now - self.requests[1]["startedNs"], 310_000_000)

    def test_second_preflight_without_response_stops_at_original_observation_bound(self):
        self.omit_response_from = 2
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertIn("initial vehicle observation deadline", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.requests), 2)
        self.assertLess(self.clock.now - self.requests[1]["startedNs"], 510_000_000)

    def test_late_successor_timeout_does_not_wait_again(self):
        self.response_delay = lambda d: 375_000_000 if d.snapshot_index >= 2 else 0
        code, result = self.assembled()
        self.assertEqual(code, 2, result)
        self.assertIn("snapshot response not received within request budget", result["error"])
        self.assertEqual(self.calls, [])
        self.assertEqual(len(self.requests), 3)
        self.assertLess(self.clock.now - self.requests[2]["startedNs"], 260_000_000)

    def test_late_and_successor_snapshots_preserve_exact_vehicle_identity_and_input(self):
        faults = {
            "owner": lambda s: s["vehicleObservation"]["owner"].update(seatGeneration=999),
            "role": lambda s: s["vehicleObservation"].update(role="driver_gunner"),
            "source": lambda s: s["vehicleObservation"]["capabilities"]["axes"][0]["mapping"].update(source="head_aim_yaw"),
            "pose": lambda s: s["vehicleObservation"]["input"]["head"]["position"].__setitem__(0, 1),
            "stick": lambda s: s["vehicleObservation"]["input"]["hands"][0].update(stickY=.5),
            "future": lambda s: s.update(observed_ns=s["observed_ns"]+t.NS),
            "stale": lambda s: s["vehicleObservation"].update(validUntilNs=s["observed_ns"]-1),
            "gather": lambda s: s["vehicleObservation"].update(gatherSequence=100),
            "input": lambda s: s["vehicleObservation"]["input"].update(generation=100),
        }
        for index in (2, 3):
            for fault, corrupt in faults.items():
                with self.subTest(index=index, fault=fault):
                    self.setUp()
                    self.response_delay = lambda d: 375_000_000 if d.snapshot_index == 2 else 0
                    self.snapshot_mutation = lambda s, d: corrupt(s) if d.snapshot_index == index else None
                    code, result = self.assembled()
                    self.assertEqual(code, 2, result)
                    self.assertEqual(self.calls, [])
                    self.assertEqual(len(self.requests), index)

    def test_successor_must_advance_beyond_discarded_input_not_only_first_sample(self):
        for field in ("gatherSequence", "generation", "inputObservedNs"):
            with self.subTest(field=field):
                self.setUp()
                self.response_delay = lambda d: 375_000_000 if d.snapshot_index == 2 else 0
                def reuse(sample, driver):
                    if driver.snapshot_index != 3:
                        return
                    new, old = sample["vehicleObservation"], self.samples[1]["vehicleObservation"]
                    if field == "generation":
                        new["input"][field] = old["input"][field]
                    else:
                        new[field] = old[field]
                        if field == "inputObservedNs":
                            new["inputDeadlineNs"] = old["inputDeadlineNs"]
                            new["validUntilNs"] = min(new["inputDeadlineNs"], new["input"]["menu"]["deadlineNs"])
                self.snapshot_mutation = reuse
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertIn("reused vehicle Gather/input observation", result["error"])
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.requests), 3)

    def test_initial_wait_refuses_external_source_owner_control_and_pose_changes(self):
        for fault in ("source", "command", "owner_record", "request", "control", "pose"):
            with self.subTest(fault=fault):
                self.setUp()
                self.response_delay = lambda d: 375_000_000 if d.snapshot_index == 2 else 0
                altered = []
                def change(path, driver):
                    if not driver.initial_observation_waited or altered:
                        return
                    altered.append(True)
                    if fault == "source":
                        self.session["source_sha256"] = "b"*64
                        self.write(self.folder / "session.json", self.session)
                    elif fault == "command":
                        (self.run / "sim/command.txt").write_bytes(b"note foreign-writer\n")
                    elif fault == "owner_record":
                        self.write(self.run / "live-driver-owner.json", dict(pending=True))
                    elif fault == "request":
                        driver.request_path.write_bytes(b"9\n")
                    elif fault == "control":
                        self.state["controls"]["trigR"] = .5
                    else:
                        self.state["head"]["pos"][0] = 1
                self.before_read = change
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.requests), 2)

    def test_held_pending_or_attempted_action_cannot_enter_observation_wait(self):
        faults = dict(held_left=True, held_right=True, held_trigger=True, held_buttons={"x"},
            pending_command=True, pending_stick=[.4, .5], pending_target=3,
            action_deadline=9_000_000_000, motion_started=2_000_000_000,
            last_write=2_000_000_000, commands=1, stick=[.4, .5])
        for field, value in faults.items():
            with self.subTest(field=field):
                self.setUp()
                self.response_delay = lambda d: 375_000_000 if d.snapshot_index == 2 else 0
                altered = []
                def change(path, driver):
                    if driver.snapshot_index == 2 and not altered:
                        altered.append(True)
                        setattr(driver, field, value)
                self.before_read = change
                code, result = self.assembled()
                self.assertEqual(code, 2, result)
                self.assertIn("snapshot response not received within request budget", result["error"])
                self.assertFalse(self.driver.initial_observation_waited)
                self.assertEqual(self.calls, [])
                self.assertEqual(len(self.requests), 2)

    def test_menu_batches_still_refuse_vehicle_stick_commands(self):
        self.write(self.run / "menu.json", [dict(commands=["stick l 0 .5"], holdMs=0)])
        code, result = self.assembled(["--run", str(self.run), "--menu-batches", str(self.run / "menu.json")])
        self.assertEqual(code, 2, result)
        self.assertIn("menu mode permits right pose/input only", result["error"])
        self.assertEqual(self.calls, [])


if __name__ == "__main__":
    unittest.main()
