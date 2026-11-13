"""Synthetic driver transport tests; no process, native, or headset evidence.

Run: python -B -m unittest discover -s tests -p test_bc2_feed_driver_observation.py
Driver sessions, simulator and clocks are held in memory. Atomic replacement
tests use temporary files with deterministic replace failures.
"""
import copy
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("feed_driver", ROOT / "tools/drive_bc2_feed_simulation.py")
d = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(d)
NS = d.NS
MS = 1_000_000


def fixture(now, nonce=1):
    """Minimal synthetic identities exercise real admission, not production geometry."""
    rows = [dict(key=n, asset=f"test{n}", configurationPath=f"config{n}",
                 meshPath=f"mesh{n}", rigFingerprint=n) for n in range(1, 25)]
    owner = dict(player=1, soldier=2, weak=3, weapon=4, actorGeneration=5, equipGeneration=6, space=7)
    pose = dict(position=[0, 0, 0], orientation=[0, 0, 0, 1])
    hand = dict(grip=pose, aim=pose, squeeze=1., gripTracked=True, aimTracked=True)
    snapshot = dict(
        schema="fvr.bc2.live-mechanism-snapshot.v1", nonce=nonce, valid=True,
        observed_ns=now, valid_until_ns=now + 80 * MS, native_deadline_ns=now + 80 * MS,
        weapon_actions_blocked=False, physical_equipment_generation=6, shot_input_generation=8,
        owner=owner, equipment=dict(weapon=4, data=9, asset="test1"),
        rig=dict(soldier=2, weak=3, count=10, skeleton=11, pose=12, fingerprint=1),
        selected=dict(configurationPathVerified=True, sequence=8, observedNs=now - MS,
                      deadlineNs=now + 90 * MS, configurationPath="config1", configurationPathPointer=13,
                      inventory=14, selectedSlot=1, soleConfiguredArray=15, matchedMeshPath="mesh1",
                      meshes=[dict(path="mesh1")]),
        input_evidence=dict(owner=dict(actor=(3 << 32) | 2, actorGeneration=5, equipGeneration=6, space=7),
                            sequence=8, observedNs=now - MS, deadlineNs=now + 90 * MS, nowNs=now,
                            focused=True, tracked=[True, True], released=[False, False]),
        input=dict(generation=8, spaceGeneration=7, trackingBasisGeneration=16, predictedNs=now,
                   focused=True, headValid=True, worldUnitsPerMeter=1, floorRelative=True,
                   referenceHead=pose, head=pose, hands=[copy.deepcopy(hand), copy.deepcopy(hand)]),
        mapping=dict(weaponWorldMeters=d.planner.identity(), trackingBodyWorldMeters=d.planner.identity(),
                     anatomicalWristToGrip=d.planner.identity()), feed=None)
    return dict(schema="bc2vr.feed-simulation-profiles.v1", profiles=rows), snapshot


class MemoryArtifacts:
    def __init__(self):
        self.saved, self.events = {}, []
        self.directory = Path("memory-artifacts")

    def save(self, name, raw, final=False):
        self.saved[name] = copy.deepcopy(raw)

    def event(self, event, cleanup=False):
        self.events.append(copy.deepcopy(event))


class CaptureAdmissionTests(unittest.TestCase):
    @staticmethod
    def manifest(mode="standard", count=12, width=1024, height=768):
        campaign, extended = mode == "campaign", mode == "extended"
        sim_bytes = count * width * height * 16 + count * 65536
        raw_bytes = 512 * 1024 * 1024 + sim_bytes
        host, outer = (1200, 1320) if campaign else (420, 540) if extended else (300, 420)
        return dict(
            limits=dict(observation_mode=mode, campaign_observation=campaign, extended_observation=extended,
                        host_seconds=host, outer_seconds=outer, cleanup_reserve_seconds=120 if campaign else 60,
                        tape_max_frames=90000 if campaign else 30000, raw_budget_bytes=raw_bytes,
                        raw_budget_limit_bytes=660 * 1024 * 1024, planned_bytes=raw_bytes + 128 * 1024 * 1024,
                        native_maximum_raw_images=8, sim_compositions=count, sim_capture_width=width,
                        sim_capture_height=height),
            capture_plan=dict(maximum_compositions=count, width=width, height=height, eyes=2, image_streams=2,
                              bytes_per_pixel=4, metadata_bytes_per_composition=65536, raw_bytes=sim_bytes,
                              layers=False, periodic=False))

    def test_standard_and_extended_existing_manifests_remain_admitted(self):
        for mode in ("standard", "extended"):
            for legacy in (False, True):
                with self.subTest(mode=mode, legacy=legacy):
                    manifest = self.manifest(mode)
                    if legacy:
                        for key in ("observation_mode", "campaign_observation", "cleanup_reserve_seconds"):
                            del manifest["limits"][key]
                    self.assertIs(d.admit_capture_plan(manifest), manifest["capture_plan"])

    def test_campaign_thirty_captures_fit_the_unchanged_raw_ceiling(self):
        manifest = self.manifest("campaign", 30, 640, 480)
        self.assertIs(d.admit_capture_plan(manifest), manifest["capture_plan"])
        self.assertEqual(manifest["limits"]["raw_budget_bytes"], 654.5 * 1024 * 1024)

    def test_count_and_dimensions_cannot_bypass_mode_or_raw_ceiling(self):
        for mode, count, width, height in (
                ("standard", 13, 640, 480), ("extended", 13, 640, 480),
                ("campaign", 31, 64, 64), ("campaign", 30, 1024, 768),
                ("campaign", 0, 640, 480), ("campaign", 1, 63, 480),
                ("campaign", 1, 640, 63), ("campaign", 1, 1025, 480),
                ("campaign", 1, 640, 769)):
            with self.subTest(mode=mode, count=count, width=width, height=height), self.assertRaises(d.REFUSAL):
                d.admit_capture_plan(self.manifest(mode, count, width, height))

    def test_campaign_requires_exact_manifest_opt_in_and_profile(self):
        for key, value in (("campaign_observation", False), ("campaign_observation", "true"),
                           ("campaign_observation", 1), ("extended_observation", True),
                           ("observation_mode", "extended"), ("cleanup_reserve_seconds", 60),
                           ("host_seconds", 1201), ("outer_seconds", 1321),
                           ("outer_seconds", 1319), ("tape_max_frames", 30000)):
            with self.subTest(key=key, value=value), self.assertRaises(d.REFUSAL):
                manifest = self.manifest("campaign", 30, 640, 480)
                manifest["limits"][key] = value
                d.admit_capture_plan(manifest)
        for key in ("campaign_observation", "observation_mode", "cleanup_reserve_seconds"):
            with self.subTest(missing=key), self.assertRaises(d.REFUSAL):
                manifest = self.manifest("campaign", 30, 640, 480)
                del manifest["limits"][key]
                d.admit_capture_plan(manifest)

    def test_mode_label_alone_and_larger_default_tape_cap_are_refused(self):
        for key, value in (("observation_mode", "campaign"), ("tape_max_frames", 90000)):
            with self.subTest(key=key), self.assertRaises(d.REFUSAL):
                manifest = self.manifest()
                manifest["limits"][key] = value
                d.admit_capture_plan(manifest)

    def test_capture_accounting_and_limits_must_match_recomputed_bytes(self):
        for section, key, value in (
                ("capture_plan", "eyes", 1), ("capture_plan", "image_streams", 1),
                ("capture_plan", "bytes_per_pixel", 3), ("capture_plan", "metadata_bytes_per_composition", 0),
                ("capture_plan", "raw_bytes", 1), ("capture_plan", "layers", True),
                ("capture_plan", "periodic", True), ("limits", "raw_budget_bytes", 1),
                ("limits", "raw_budget_limit_bytes", 661 * 1024 * 1024),
                ("limits", "planned_bytes", 1), ("limits", "native_maximum_raw_images", 0),
                ("limits", "sim_compositions", 12), ("limits", "sim_capture_width", 1024),
                ("limits", "sim_capture_height", 768)):
            with self.subTest(section=section, key=key), self.assertRaises(d.REFUSAL):
                manifest = self.manifest("campaign", 30, 640, 480)
                manifest[section][key] = value
                d.admit_capture_plan(manifest)

    def test_last_campaign_capture_consumes_single_slot_and_then_refuses(self):
        v = d.Driver.__new__(d.Driver)
        v.capture_plan = d.admit_capture_plan(self.manifest("campaign", 30, 640, 480))
        v.capture_sequence, v.run, v.artifacts = 29, Path("memory-run"), MemoryArtifacts()
        commands = []
        def send(lines, capture):
            commands.append((lines, capture))
            v.capture_sequence += 1
        v.send, v.pump = send, lambda: None
        with patch.object(Path, "exists", side_effect=lambda: bool(commands)), \
                patch.object(d, "read_json", return_value=({"width": 640, "height": 480}, b"")):
            v.capture("last")
            with self.assertRaisesRegex(d.REFUSAL, "capture budget exhausted"):
                v.capture("beyond")
        self.assertEqual(commands, [(["shot last"], "last")])
        self.assertEqual(v.artifacts.saved["capture.json"]["captureSeq"], 30)


class MemoryPendingOwner:
    """Fake only the OS custody boundary; byte/owner validation stays real."""
    def __init__(self, driver, path, raw):
        self.path = self.temporary = path
        self.stream = self
        self.raw = raw
        d.atomic(path, raw)

    def read(self):
        return d.read_bytes(self.path, 8192)

    def validate(self, expected_bytes, deadline_ns):
        d.require(self.stream is not None and expected_bytes == self.raw,
                  "command owner record replaced before publication")
        # The in-memory read models current pathname identity; production
        # validates metadata against the sealed HANDLE without rereading bytes.
        observed = d.read_until(deadline_ns, self.read)
        d.require(observed == self.raw, "command owner record replaced before publication")
        return self.raw

    def close(self):
        self.stream = None


class Harness:
    def __init__(self, testcase, ack_ms=5, *, real_pending_owner=False):
        self.now, self.ack_ms = NS + 120 * MS, ack_ms
        self.files, self.json_files, self.command_writes = {}, {}, []
        self.response_after, self.response_stale, self.on_sleep = 40 * MS, False, None
        self.response_due, self.command_due = None, None
        v = self.driver = d.Driver.__new__(d.Driver)
        v.profiles, original = fixture(NS)
        v.reference = copy.deepcopy(original)
        v.latest_snapshot = copy.deepcopy(original)
        v.artifacts = MemoryArtifacts()
        v.run = Path("memory-run")
        v.host = dict(pid=1, start_time="synthetic")
        v.command_path, v.ack_path = v.run / "command", v.run / "ack"
        v.request_path, v.snapshot_path = v.run / "request", v.run / "snapshot"
        v.pending_nonce, v.snapshot_index = 1, 1
        v.request_bytes, v.last_request = b"1\n", NS + 20 * MS
        v.snapshot_refresh_started, v.snapshot_refresh_requests = None, 0
        v.deadline, v.hard_deadline, v.stage_deadline = self.now + 9 * NS, self.now + 10 * NS, self.now + 2 * NS
        v.motion_started, v.monitor = self.now - 100 * MS, True
        v.last_write, v.last_commit, v.gap = self.now - 60 * MS, self.now - 50 * MS, 80 * MS
        v.commands, v.capture_sequence = 0, 0
        v.sequence, v.sentinel, v.pending_command, v.unsafe_owner = 10, "initial", False, False
        v.held_left, v.held_right, v.held_trigger, v.held_buttons = True, False, False, set()
        self.state = dict(pid=1, runtime="xr-sim", cmdSeq=10, lastCmd="initial", lastCmdError="", errors=0,
                          frame=100, endFrames=99, captureSeq=0, sessionState="FOCUSED", head={}, handL={}, handR={},
                          controls=dict(gripL=1.))
        v.initial_sim = copy.deepcopy(self.state)
        self.files[v.command_path] = b"initial\n"
        self.files[v.request_path] = v.request_bytes
        self.publish(original)
        v.command_hash = d.digest(self.files[v.command_path])
        v.read_state = lambda: copy.deepcopy(self.state)
        for target, replacement in (
            ("perf_counter_ns", lambda: self.now), ("time_ns", lambda: 10 * NS + self.now), ("sleep", self.sleep)
        ):
            testcase.enterContext(patch.object(d.time, target, replacement))
        testcase.enterContext(patch.object(Path, "exists", lambda path: path in self.files))
        testcase.enterContext(patch.object(d, "read_bytes", lambda path, maximum: self.files[path]))
        testcase.enterContext(patch.object(d, "read_json", self.read_json))
        testcase.enterContext(patch.object(d, "atomic", self.write))
        if not real_pending_owner:
            testcase.enterContext(patch.object(d, "PendingOwner", MemoryPendingOwner))

    def read_json(self, path, maximum=128 * 1024):
        return copy.deepcopy(self.json_files[path]), self.files[path]

    def publish(self, snapshot):
        self.json_files[self.driver.snapshot_path] = copy.deepcopy(snapshot)
        self.files[self.driver.snapshot_path] = d.json_bytes(snapshot)

    def write(self, path, raw, *, deadline_ns=None, validate=None):
        if validate is not None:
            self.driver_publication_deadline = deadline_ns
            validate()
            d.require(self.now < deadline_ns, "original command publication deadline exceeded")
        self.files[path] = raw
        if path == self.driver.request_path:
            self.response_due = None if self.response_after is None else self.now + self.response_after
        if path == self.driver.command_path:
            self.command_writes.append((self.now, raw))
            self.command_due = self.now + self.ack_ms * MS

    def sleep(self, seconds):
        self.now += round(seconds * NS)
        if self.on_sleep:
            self.on_sleep()
        if self.response_due is not None and self.now >= self.response_due:
            observed = self.now - (120 * MS if self.response_stale else 0)
            _, snapshot = fixture(observed, int(self.files[self.driver.request_path]))
            self.publish(snapshot)
            self.response_due = None
        if self.command_due is not None and self.now >= self.command_due:
            batch = self.files[self.driver.command_path].decode("ascii").splitlines()
            for command in batch:
                if command.startswith("grip l "):
                    self.state["controls"]["gripL"] = float(command.split()[-1])
                elif command.startswith("hand l grip pose "):
                    pose = [float(value) for value in command.split()[4:]]
                    self.state["handL"].update(pos=pose[:3], ypr=pose[3:])
            self.state.update(cmdSeq=self.state["cmdSeq"] + len(batch), lastCmd=batch[-1], frame=103, endFrames=102)
            self.files[self.driver.ack_path] = (
                f"seq={self.state['cmdSeq']} frame=101 error=-\nlast={' | '.join(batch)}\n").encode("ascii")
            self.command_due = None


class FeedDriverObservationTests(unittest.TestCase):
    def test_prepublication_state_stall_is_observed_without_extra_operations(self):
        h = Harness(self)
        v = h.driver
        v.monitor = False
        original_read_state = v.read_state
        original_owner = v.sequence, v.sentinel, v.command_hash, v.last_write, v.commands
        source_deadline = h.now + 75 * MS

        def delayed_state():
            h.now += 95 * MS
            return original_read_state()

        with patch.object(v, "read_state", side_effect=delayed_state) as states, \
                patch.object(v, "pump", wraps=v.pump) as pumps, \
                patch.object(d, "read_bytes", wraps=d.read_bytes) as reads, \
                patch.object(d.time, "sleep", wraps=h.sleep) as sleeps:
            with self.assertRaisesRegex(d.REFUSAL, "source/claim lease expired"):
                v.send(["grip l 1"], source_deadline=source_deadline)
        states.assert_called_once_with()
        pumps.assert_called_once_with()
        reads.assert_called_once_with(v.command_path, 4096)
        sleeps.assert_not_called()
        self.assertEqual((v.sequence, v.sentinel, v.command_hash, v.last_write, v.commands), original_owner)
        self.assertFalse(v.pending_command)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.artifacts.events, [])
        v._receive_timing.freeze(v.pending_command, v.sequence)
        v.save_receive_timing_failure()
        timing = v.artifacts.saved["receive-timing-failure.json"]
        self.assertEqual(timing["phaseNs"]["command_state_read"]["totalNs"], 95 * MS)
        self.assertIn("command_pause", timing["phaseNs"])
        self.assertIn("command_ownership", timing["phaseNs"])
        self.assertEqual(timing["transientReadRetries"], {})

    def test_transient_read_retries_follow_phase_without_extra_reads_or_sleep(self):
        v = d.Driver.__new__(d.Driver)
        now, attempts, sleeps = [NS], [], []
        original_open = Path.open
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "command.txt"
            path.write_bytes(b"owned\n")

            def open_stream(target, *args, **kwargs):
                attempts.append(target)
                if len(attempts) in (1, 2, 4, 6):
                    raise FileNotFoundError("controlled transient read refusal")
                return original_open(target, *args, **kwargs)

            def sleep(seconds):
                sleeps.append(seconds)
                now[0] += round(seconds * NS)

            with patch.object(d, "local_path", side_effect=lambda value: value), \
                    patch.object(Path, "open", open_stream), \
                    patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                    patch.object(d.time, "sleep", side_effect=sleep):
                with v.receive_timing_scope("command_pause"):
                    self.assertEqual(d.read_bytes(path, 4096), b"owned\n")
                    self.assertEqual(v.receive_timing_call("command_state_read", d.read_bytes, path, 4096), b"owned\n")
                self.assertIsNone(d._receive_read_timing.get())
                v._receive_timing.freeze(False, 1)
                before = copy.deepcopy(v._receive_timing.report())
                with v.receive_timing_scope("cleanup_read"):
                    self.assertEqual(d.read_bytes(path, 4096), b"owned\n")
                self.assertEqual(v._receive_timing.report(), before)
                self.assertIsNone(d._receive_read_timing.get())
        self.assertEqual(attempts, [path] * 7)
        self.assertEqual(sleeps, [.001] * 4)
        self.assertEqual(before["transientReadRetries"], {"command_pause": 2, "command_state_read": 1})
        self.assertEqual(before["phaseNs"]["command_pause"]["totalNs"], 2 * MS)
        self.assertEqual(before["phaseNs"]["command_state_read"]["totalNs"], MS)

    def test_unpublished_read_expiry_rejoins_owner_for_release_only(self):
        for phase in ("before_state_read", "command_read"):
            with self.subTest(phase=phase):
                h = Harness(self)
                v = h.driver
                v.monitor = False
                source_deadline = h.now + 5 * MS
                deadlines = v.deadline, v.stage_deadline, v.hard_deadline
                original = v.sequence, v.sentinel, v.command_hash, v.last_write, v.commands
                delayed = False
                def read(path, maximum):
                    nonlocal delayed
                    target = v.run / "live-driver-owner.json" if phase == "before_state_read" else v.command_path
                    if v.pending_command and path == target and not delayed:
                        delayed = True
                        h.now = source_deadline
                        if phase == "command_read":
                            return d.read_until(source_deadline, lambda: h.files[path])
                    return h.files[path]
                with patch.object(d, "read_bytes", read), self.assertRaisesRegex(
                        d.ReadDeadlineExceeded, "original acknowledgement/read deadline"):
                    v.send(["grip l 1"], source_deadline=source_deadline)
                self.assertFalse(v.pending_command or v.unsafe_owner)
                self.assertEqual(h.command_writes, [])
                self.assertEqual((v.sequence, v.sentinel, v.command_hash, v.last_write, v.commands), original)
                self.assertEqual(h.files[v.run / "live-driver-owner.json"], v.owner_bytes(False))
                self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
                self.assertEqual(len(h.command_writes), 1)
                self.assertEqual(h.command_writes[0][1].decode().splitlines()[:-1], ["grip l 0"])
                self.assertFalse(v.held_left)
                self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline), deadlines)

    def test_read_expiry_recovery_rejects_changed_writer_or_input(self):
        for changed in ("owner", "command", "sequence", "sentinel", "controls", "head", "handL", "handR", "capture"):
            with self.subTest(changed=changed):
                h = Harness(self)
                v = h.driver
                v.monitor = False
                source_deadline = h.now + 5 * MS
                owner = v.run / "live-driver-owner.json"
                delayed = False
                def read(path, maximum):
                    nonlocal delayed
                    raw = h.files[path]
                    if path == owner and v.pending_command and not delayed:
                        delayed = True
                        h.now = source_deadline
                        if changed == "owner":
                            h.files[owner] = b"other owner"
                        elif changed == "command":
                            h.files[v.command_path] = b"other command"
                        elif changed == "sequence":
                            h.state["cmdSeq"] += 1
                        elif changed == "sentinel":
                            h.state["lastCmd"] = "note other writer"
                        elif changed == "controls":
                            h.state["controls"]["gripL"] = 0
                        elif changed == "capture":
                            h.state["captureSeq"] += 1
                        else:
                            h.state[changed] = {"changed": True}
                    return raw
                with patch.object(d, "read_bytes", read), self.assertRaisesRegex(d.REFUSAL, "owner restoration refused"):
                    v.send(["grip l 1"], source_deadline=source_deadline)
                self.assertTrue(v.unsafe_owner)
                self.assertFalse(v.pending_command)
                self.assertEqual(v.release_after_failure(), "not attempted: no settled owned held controller")
                self.assertEqual(h.command_writes, [])

    def test_read_expiry_recovery_preserves_action_and_cleanup_deadlines(self):
        h = Harness(self)
        v = h.driver
        v.monitor = False
        source_deadline = h.now + 5 * MS
        delayed = False
        def read(path, maximum):
            nonlocal delayed
            if v.pending_command and path == v.run / "live-driver-owner.json" and not delayed:
                delayed = True
                h.now = source_deadline
            return h.files[path]
        with patch.object(d, "read_bytes", read), self.assertRaises(d.ReadDeadlineExceeded):
            v.send(["grip l 1"], source_deadline=source_deadline)
        self.assertFalse(v.unsafe_owner)
        with self.assertRaisesRegex(d.REFUSAL, "source/claim lease expired"):
            v.send(["grip l 1"], source_deadline=source_deadline)
        v.hard_deadline = h.now + 250 * MS
        self.assertEqual(v.release_after_failure(), "not attempted: bounded driver deadline")
        self.assertEqual(h.command_writes, [])


    def test_unpublished_failure_restores_previous_command_without_false_pending(self):
        h = Harness(self)
        v = h.driver
        v.monitor = False
        original = v.sequence, v.sentinel, v.command_hash
        original_write, original_commands = v.last_write, v.commands
        def fail_command(path, raw, **options):
            if path == v.command_path:
                options["validate"]()
                raise OSError("unpublished transport failure")
            h.write(path, raw, **options)
        with patch.object(d, "atomic", side_effect=fail_command), self.assertRaisesRegex(OSError, "unpublished"):
            v.send(["grip l 1"])
        self.assertEqual((v.sequence, v.sentinel, v.command_hash), original)
        self.assertEqual((v.last_write, v.commands), (original_write, original_commands))
        self.assertFalse(v.pending_command)
        self.assertFalse(v.unsafe_owner)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.files[v.run / "live-driver-owner.json"], v.owner_bytes(False))
        self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertEqual(len(h.command_writes), 1)
        self.assertTrue(h.command_writes[0][1].startswith(b"grip l 0\n"))
        self.assertFalse(v.held_left)

    def test_prepublication_interference_does_not_restore_over_another_owner(self):
        h = Harness(self)
        v = h.driver
        v.monitor = False
        owner_path = v.run / "live-driver-owner.json"
        def interfere(path, raw, **options):
            if path == v.command_path:
                h.files[owner_path] = b"different owner"
                options["validate"]()
                self.fail("changed owner must stop before replacement")
            h.write(path, raw, **options)
        with patch.object(d, "atomic", side_effect=interfere), self.assertRaisesRegex(d.REFUSAL, "owner record replaced"):
            v.send(["grip l 1"])
        self.assertEqual(h.files[owner_path], b"different owner")
        self.assertTrue(v.unsafe_owner)
        self.assertFalse(v.pending_command)
        self.assertEqual(h.command_writes, [])

    def test_publication_delay_consumes_original_ack_budget_and_updates_write_spacing(self):
        h = Harness(self, ack_ms=220)
        v = h.driver
        v.monitor = False
        requested = h.now
        def delayed(path, raw, **options):
            if path == v.command_path:
                h.now += 40 * MS
            h.write(path, raw, **options)
        with patch.object(d, "atomic", side_effect=delayed), self.assertRaisesRegex(d.REFUSAL, "acknowledgement/commit deadline"):
            v.send(["grip l 1"])
        self.assertEqual(h.driver_publication_deadline, requested + 250 * MS)
        self.assertEqual(v.last_write, requested + 40 * MS)
        self.assertTrue(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)

    def test_expired_response_holds_pose_without_commands_then_fresh_resumes(self):
        h = Harness(self)
        v, original = h.driver, copy.deepcopy(h.driver.reference)
        raw = h.files[v.snapshot_path]
        started, original_write, original_stage = h.now, v.last_write, v.stage_deadline

        def held():
            if not h.command_writes:
                self.assertEqual(v.last_write, original_write)
                self.assertTrue(v.held_left)
                self.assertEqual(v.commands, 0)

        h.on_sleep = held
        v.send(["grip l 1"])
        self.assertEqual(len(h.command_writes), 1)
        self.assertGreaterEqual(h.command_writes[0][0], started + 40 * MS)
        self.assertEqual(v.artifacts.saved["snapshot-001.json"], raw)
        self.assertEqual(v.reference, original)
        self.assertGreater(v.latest_snapshot["nonce"], original["nonce"])
        self.assertEqual(v.stage_deadline, original_stage)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual([e["type"] for e in v.artifacts.events],
                         ["snapshot_refresh_hold", "snapshot_refresh_resumed", "command_request", "command_commit"])

    def test_expired_changed_identity_is_refused_without_refresh_or_command(self):
        for section, key, value in (("owner", "player", 99), ("selected", "selectedSlot", 2),
                                    ("input", "trackingBasisGeneration", 99)):
            with self.subTest(section=section):
                h = Harness(self)
                changed = copy.deepcopy(h.driver.reference)
                changed[section][key] = value
                h.publish(changed)
                with self.assertRaises(d.REFUSAL):
                    h.driver.send(["grip l 1"])
                self.assertEqual(h.driver.snapshot_index, 1)
                self.assertEqual(h.command_writes, [])
                self.assertIn("snapshot-001.json", h.driver.artifacts.saved)

    def test_invalid_native_publication_is_retained_and_not_retried(self):
        h = Harness(self)
        changed = copy.deepcopy(h.driver.reference)
        changed.update(valid=False, invalid_reason="native_owner_changed")
        h.publish(changed)
        with self.assertRaisesRegex(d.REFUSAL, "native snapshot refused"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 1)
        self.assertIn("snapshot-001.json", h.driver.artifacts.saved)

    def test_navigation_observation_never_admits_an_invalid_weapon_snapshot(self):
        h = Harness(self)
        changed = copy.deepcopy(h.driver.reference)
        changed.update(valid=False, invalid_reason="no_shot_publication",
                       navigationObservation=dict(observationOnly=True, available=True, reason=None))
        h.publish(changed)
        with self.assertRaisesRegex(d.REFUSAL, "native snapshot refused: no_shot_publication"):
            h.driver.send(["grip l 1"])
        self.assertFalse(h.driver.snapshot_usable(changed, h.now))
        self.assertFalse(changed["valid"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 1)
        self.assertIn("snapshot-001.json", h.driver.artifacts.saved)

    def test_expired_but_inadmissible_source_is_not_retried(self):
        h = Harness(self)
        changed = copy.deepcopy(h.driver.reference)
        changed["input_evidence"]["tracked"] = [False, True]
        h.publish(changed)
        with self.assertRaisesRegex(d.REFUSAL, "untracked or unfocused"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 1)

    def test_repeated_expired_responses_exhaust_refresh_count(self):
        h = Harness(self)
        h.response_stale = True
        with self.assertRaisesRegex(d.REFUSAL, "refresh request budget exhausted"):
            h.driver.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.snapshot_index, 3)
        self.assertIn("snapshot-003.json", h.driver.artifacts.saved)

    def test_fresh_replacement_must_pass_current_native_admission(self):
        h = Harness(self)
        v = h.driver
        self.assertIsNone(v.receive_snapshot())
        _, replacement = fixture(h.now, v.pending_nonce)
        replacement["input_evidence"]["tracked"] = [False, True]
        h.publish(replacement)
        with self.assertRaisesRegex(d.REFUSAL, "untracked or unfocused"):
            v.send(["grip l 1"])
        self.assertEqual(h.command_writes, [])
        self.assertIsNotNone(v.snapshot_refresh_started)
        self.assertEqual(v.latest_snapshot, v.reference)

    def test_refresh_keeps_elapsed_budget_and_gesture_watchdog(self):
        for bound, message in (("refresh", "refresh budget exhausted"),
                               ("stage", "maximumGestureNs"), ("watchdog", "driver deadline")):
            with self.subTest(bound=bound):
                h = Harness(self)
                v = h.driver
                self.assertIsNone(v.receive_snapshot())
                h.response_after, h.response_due = None, None
                if bound == "refresh":
                    h.now += d.SNAPSHOT_REFRESH_BUDGET
                elif bound == "stage":
                    v.stage_deadline = h.now + 10 * MS
                    h.now = v.stage_deadline
                else:
                    v.deadline = h.now + 10 * MS
                    h.now = v.deadline
                with self.assertRaisesRegex(d.REFUSAL, message):
                    v.pause_until(h.now)
                self.assertEqual(h.command_writes, [])

    def test_monitor_uses_100ms_request_period_not_each_command(self):
        h = Harness(self)
        v = h.driver
        v.pending_nonce = None
        v.last_request = h.now
        for _ in range(3):
            h.now += 25 * MS
            v.pump()
        self.assertEqual(v.snapshot_index, 1)
        h.now += 25 * MS
        v.pump()
        self.assertEqual(v.snapshot_index, 2)

    def test_held_pose_command_gap_is_not_native_input_gap(self):
        h = Harness(self, ack_ms=150)
        v = h.driver
        v.monitor = False
        v.last_write = v.last_commit = h.now - 200 * MS
        v.send(["grip l 1"])
        self.assertFalse(v.pending_command)
        self.assertEqual(len(h.command_writes), 1)

    def test_ack_limits_remain_250ms_in_motion_and_one_second_otherwise(self):
        for moving, delay, succeeds in ((True, 249, True), (True, 250, False),
                                        (False, 999, True), (False, 1000, False)):
            with self.subTest(moving=moving, delay=delay):
                h = Harness(self, ack_ms=delay)
                v = h.driver
                v.monitor = False
                v.motion_started = h.now if moving else None
                if succeeds:
                    v.send(["grip l 1"])
                    self.assertFalse(v.pending_command)
                else:
                    with self.assertRaisesRegex(d.REFUSAL, "acknowledgement/commit deadline"):
                        v.send(["grip l 1"])


class AtomicCommandPublicationTests(unittest.TestCase):
    @staticmethod
    def denied(code):
        error = PermissionError("controlled Windows replacement refusal")
        error.winerror = code
        return error

    def test_transient_windows_refusal_reuses_one_temp_and_identical_batch(self):
        for code in (5, 32, 33):
            with self.subTest(code=code), tempfile.TemporaryDirectory() as folder:
                target = Path(folder) / "command.txt"
                target.write_bytes(b"previous\n")
                raw = b"grip l 1\nnote same-command-id\n"
                now, attempts, guards = [NS], [], []
                replace = os.replace
                def attempt(source, destination):
                    attempts.append((source, source.read_bytes(), destination.read_bytes()))
                    if len(attempts) < 3:
                        raise self.denied(code)
                    replace(source, destination)
                def sleep(seconds):
                    now[0] += round(seconds * NS)
                with patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                        patch.object(d.time, "sleep", side_effect=sleep), patch.object(d.os, "replace", side_effect=attempt):
                    d.publish_command(target, raw, NS + 5 * MS, lambda: guards.append(now[0]))
                self.assertEqual(target.read_bytes(), raw)
                self.assertEqual(len(attempts), 3)
                self.assertEqual(len({row[0] for row in attempts}), 1)
                self.assertTrue(all(row[1:] == (raw, b"previous\n") for row in attempts))
                self.assertEqual(guards, [NS, NS + MS, NS + 2 * MS])
                self.assertEqual(list(Path(folder).iterdir()), [target])

    def test_persistent_refusal_stops_at_original_deadline_without_publication(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "command.txt"
            target.write_bytes(b"previous")
            now = [NS]
            def sleep(seconds):
                now[0] += round(seconds * NS)
            with patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                    patch.object(d.time, "sleep", side_effect=sleep), \
                    patch.object(d.os, "replace", side_effect=self.denied(5)) as replace, \
                    self.assertRaisesRegex(d.REFUSAL, "original command publication deadline"):
                d.publish_command(target, b"new", NS + 3 * MS, lambda: None)
            self.assertEqual(now[0], NS + 3 * MS)
            self.assertEqual(replace.call_count, 3)
            self.assertEqual(target.read_bytes(), b"previous")
            self.assertEqual(list(Path(folder).iterdir()), [target])

    def test_nonsharing_error_is_not_retried(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "command.txt"
            target.write_bytes(b"previous")
            with patch.object(d.time, "perf_counter_ns", return_value=NS), \
                    patch.object(d.time, "sleep") as sleep, \
                    patch.object(d.os, "replace", side_effect=self.denied(87)) as replace, \
                    self.assertRaises(OSError):
                d.publish_command(target, b"new", NS + MS, lambda: None)
            replace.assert_called_once()
            sleep.assert_not_called()
            self.assertEqual(target.read_bytes(), b"previous")

    def test_changed_predecessor_between_attempts_stops_before_replace(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "command.txt"
            target.write_bytes(b"previous")
            now, checks = [NS], []
            def validate():
                checks.append(now[0])
                if len(checks) == 2:
                    target.write_bytes(b"another writer")
                d.require(target.read_bytes() == b"previous", "predecessor changed")
            def sleep(seconds):
                now[0] += round(seconds * NS)
            with patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                    patch.object(d.time, "sleep", side_effect=sleep), \
                    patch.object(d.os, "replace", side_effect=self.denied(5)) as replace, \
                    self.assertRaisesRegex(d.REFUSAL, "predecessor changed"):
                d.publish_command(target, b"new", NS + 5 * MS, validate)
            replace.assert_called_once()
            self.assertEqual(target.read_bytes(), b"another writer")

    def test_guard_time_cannot_move_publication_past_its_original_deadline(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "command.txt"
            target.write_bytes(b"previous")
            now = [NS]
            def validate():
                now[0] += 3 * MS
            with patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                    patch.object(d.os, "replace") as replace, \
                    self.assertRaisesRegex(d.REFUSAL, "original command publication deadline"):
                d.publish_command(target, b"new", NS + 3 * MS, validate)
            replace.assert_not_called()
            self.assertEqual(target.read_bytes(), b"previous")

    def test_successful_replace_is_never_retried_or_reclassified_unpublished(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "command.txt"
            target.write_bytes(b"previous")
            now, replace = [NS], os.replace
            def slow_success(source, destination):
                replace(source, destination)
                now[0] += 5 * MS
            with patch.object(d.time, "perf_counter_ns", side_effect=lambda: now[0]), \
                    patch.object(d.os, "replace", side_effect=slow_success) as call:
                d.publish_command(target, b"new", NS + MS, lambda: None)
            call.assert_called_once()
            self.assertEqual(target.read_bytes(), b"new")


if __name__ == "__main__":
    unittest.main()
