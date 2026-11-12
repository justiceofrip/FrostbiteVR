"""Offline assembled neutral-motion checks; no native/gameplay acceptance."""
from contextlib import contextmanager
import copy
import ctypes
import importlib.util
import json
import math
import os
from pathlib import Path
import types
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("neutral_motion_fixtures", ROOT / "tests/test_bc2_reload_stimulus.py")
df = importlib.util.module_from_spec(spec)
spec.loader.exec_module(df)
m, f, g = df.m, df.fixtures, df.fixtures.g


def plans():
    for family, stage, source in (
            ("pump", "ShellPickup", f.pf.snapshot()),
            ("fixed", "Open", f.xf.snapshot(kind=3)),
            ("sight", "SightFlip", f.sf.snapshot())):
        yield g.build_stimulus(family, None, f.complete(source), f.NOW, stage)
    if os.environ.get("BC2_FEED_SIMULATION_PROFILES"):
        profiles = json.loads(Path(os.environ["BC2_FEED_SIMULATION_PROFILES"]).read_text(encoding="utf-8-sig"))
        for row in profiles["profiles"]:
            yield g.build_stimulus("feed", profiles, f.complete(f.ff.synthetic_snapshot(row, "SupplyPickup")),
                                   f.NOW, "SupplyPickup")


class NeutralGeometryTests(unittest.TestCase):
    def test_registered_family_prefixes_preserve_path_budgets_and_native_contract(self):
        eligible = set()
        for plan in plans():
            original = copy.deepcopy(plan)
            motion = g.neutral_motion(plan)
            self.assertEqual(original, plan)
            if motion is None:
                continue
            eligible.add(plan["family"])
            end, rows = motion["lastSample"], plan["schedule"]
            self.assertTrue(all(row["grip"] == 0 for row in rows[:end+1]))
            self.assertIn(rows[end+1]["phase"], ("acquire", "acquire_shell"))
            self.assertLessEqual(motion["peakStepMeters"], plan["policy"].get("maxStepMeters", .025))
            self.assertLessEqual(motion["peakStepRadians"], plan["policy"]["maxStepRadians"])
            self.assertLessEqual(motion["durationNs"]+rows[-1]["atNs"]-rows[end]["atNs"], plan["budget"]["totalNs"])
            self.assertFalse(plan["nativeAcceptance"])
            self.assertFalse(plan["geometryEvidence"]["executionAuthorized"])
        self.assertTrue({"pump", "fixed", "sight"} <= eligible)
        if os.environ.get("BC2_FEED_SIMULATION_PROFILES"):
            self.assertIn("feed", eligible)

    def test_curved_unheld_path_and_held_continuation_never_compact(self):
        plan = next(p for p in plans() if p["family"] == "sight")
        for mutate in (lambda p: p["schedule"][2]["positionMeters"].__setitem__(0, p["schedule"][2]["positionMeters"][0]+.01),
                       lambda p: p.update(continuesHeldCustody=True),
                       lambda p: p["schedule"][2].update(grip=1),
                       lambda p: p["boundaries"].append(dict(afterSample=2))):
            changed = copy.deepcopy(plan)
            mutate(changed)
            self.assertIsNone(g.neutral_motion(changed))

    def test_smoothstep_peak_uses_existing_slack_and_refuses_insufficient_budget(self):
        plan = next(plans())
        motion = g.neutral_motion(plan)
        self.assertGreater(motion["durationNs"], motion["originalPrefixNs"])
        plan["budget"]["maximumGestureNs"] = plan["durationNs"]
        self.assertIsNone(g.neutral_motion(plan))

    def test_nearly_parallel_quaternion_peak_retains_pinned_nlerp_bound(self):
        angle, count, step = .05, 16, 20_000_000
        rows = [df.sample(i*step, "neutral" if i == 0 else "approach", 0, 0.) for i in range(count+1)]
        for i, row in enumerate(rows):
            row["yawPitchRollDegrees"] = [math.degrees(angle*i/count), 0., 0.]
        rows += [copy.deepcopy(rows[-1]), copy.deepcopy(rows[-1])]
        rows[-2].update(atNs=(count+1)*step, phase="neutral_at_contact")
        rows[-1].update(atNs=(count+2)*step, phase="acquire", grip=1)
        first, last = g.controller_pose(rows[0]), g.controller_pose(rows[-2])
        interval = 1/(count+1)
        peak = 1.5*interval-.5*interval**3
        exact = g.feed.pose_error(g.feed.blend(first, last, (1-peak)/2), g.feed.blend(first, last, (1+peak)/2))[1]
        self.assertGreater(exact, angle*peak)
        cap = (exact+angle*peak)/2
        plan = dict(family="fixed", schedule=rows, boundaries=[], stepNs=step,
                    policy=dict(maxStepMeters=.025, maxStepRadians=cap),
                    budget=dict(totalNs=m.NS, maximumGestureNs=m.NS, gestureStartSample=0))
        motion = g.neutral_motion(plan)
        self.assertGreater(motion["durationNs"], motion["originalPrefixNs"])
        self.assertLessEqual(motion["peakStepRadians"], cap)

    def test_zero_motion_preserves_existing_contact_and_held_curve_path(self):
        plan = g.build_stimulus("pump", None, f.complete(f.pf.snapshot(pumping=True)), f.NOW, "PumpCycle")
        self.assertIsNone(g.neutral_motion(plan))
        self.assertIn("rear", [row["phase"] for row in plan["schedule"]])

    def test_hand_to_parser_allowlist_rejects_duration_grip_and_injected_commands(self):
        sample = df.sample(0, "neutral", 0, 0.)
        self.assertTrue(m.hand_to_command(sample, 0).endswith(" 0"))
        for duration in (-1, 10001, 1.5, True):
            with self.assertRaises(m.Refusal):
                m.hand_to_command(sample, duration)
        sample["commands"].append("trigger l 1")
        with self.assertRaises(m.Refusal):
            m.hand_to_command(sample, 10)

    def test_real_pump_fixed_fresh_contact_and_native_refusals(self):
        for family, stage, fixture, source in (("pump", "ShellPickup", f.pf, f.pf.snapshot()),
                                             ("fixed", "Open", f.xf, f.xf.snapshot(kind=3))):
            with self.subTest(family=family):
                source = f.complete(source)
                plan = g.build_stimulus(family, None, source, f.NOW, stage)
                end = plan["schedule"][g.neutral_motion(plan)["lastSample"]]
                now = f.NOW+2_000_000_000
                snapshot = fixture.refresh(source, now, 201)
                fixture.set_local(snapshot, f.sf.emitted_local(source, end))
                result = g.check_neutral_contact(plan, snapshot, now)
                self.assertTrue(result["observed"])
                self.assertFalse(result["nativeAcceptance"])
                changes = [lambda s: s["input"]["hands"][0].update(squeeze=1.),
                           lambda s: s["owner"].update(weapon=99),
                           lambda s: s.update(valid_until_ns=now),
                           lambda s: s["input"]["hands"][0]["grip"]["position"].__setitem__(0,
                               s["input"]["hands"][0]["grip"]["position"][0]+.01)]
                if family == "fixed":
                    changes += [lambda s: s["fixedContainer"]["tracking"].update(lifetime=999),
                                lambda s: s["fixedContainer"]["view"]["snapshot"]["counts"].update(reserve=999)]
                for change in changes:
                    bad = copy.deepcopy(snapshot)
                    change(bad)
                    with self.assertRaises(g.GeometryRefusal):
                        g.check_neutral_contact(plan, bad, now)

    def test_all_registered_feed_stages_and_pump_variants_share_compiler(self):
        path = os.environ.get("BC2_FEED_SIMULATION_PROFILES")
        if not path:
            self.skipTest("registered measured feed profile export")
        profiles = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        eligible = 0
        for row in profiles["profiles"]:
            for stage in ("OpenCover", "DetachContainer", "SeatFeed", "CloseCover", "SupplyPickup"):
                plan = g.build_stimulus("feed", profiles, f.complete(f.ff.synthetic_snapshot(row, stage)), f.NOW, stage)
                original = copy.deepcopy(plan)
                motion = g.neutral_motion(plan)
                self.assertEqual(plan, original)
                if motion is not None:
                    eligible += 1
                    self.assertLessEqual(motion["peakStepMeters"], plan["policy"]["maxStepMeters"])
        self.assertGreater(eligible, 0)
        for variant in range(3):
            for stage in ("ShellPickup", "PumpCycle"):
                plan = g.build_stimulus("pump", None, f.complete(f.pf.snapshot(variant=variant, pumping=stage == "PumpCycle")), f.NOW, stage)
                if stage == "ShellPickup":
                    self.assertIsNotNone(g.neutral_motion(plan))
                else:
                    self.assertIsNone(g.neutral_motion(plan))  # Already at contact.
        for kind in (3, 4):
            for motion_kind in ("Hinge", "Linear"):
                plan = g.build_stimulus("fixed", None, f.complete(f.xf.snapshot(kind=kind, motion_kind=motion_kind)), f.NOW,
                                       "Open" if kind == 3 else "Pickup")
                self.assertIsNotNone(g.neutral_motion(plan))

    def test_retained_actual05_prefix_compiles_without_retiming_evidence(self):
        path = os.environ.get("BC2_NEUTRAL_MOTION_REPLAY")
        if not path:
            self.skipTest("optional retained actual05 stimulus")
        plan = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        before = copy.deepcopy(plan)
        motion = g.neutral_motion(plan)
        self.assertEqual((motion["lastSample"], motion["durationMs"]), (65, 1300))
        self.assertEqual(plan, before)


class MemoryLease:
    def __init__(self, driver, until):
        self.streams = [object(), object()]
        self.closed = False

    def close(self):
        self.streams = []
        self.closed = True

    def check(self, until):
        pass


class NeutralTransportTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows retained file custody")
    def test_windows_motion_lease_seals_payload_and_detects_posix_name_replacement(self):
        api = ctypes.WinDLL("kernel32", use_last_error=True)
        api.CreateFileW.argtypes = (m.wintypes.LPCWSTR, m.wintypes.DWORD, m.wintypes.DWORD,
                                   ctypes.c_void_p, m.wintypes.DWORD, m.wintypes.DWORD, m.wintypes.HANDLE)
        api.CreateFileW.restype = m.wintypes.HANDLE
        api.CloseHandle.argtypes = (m.wintypes.HANDLE,)
        with tempfile.TemporaryDirectory(prefix="bc2-neutral-custody-", dir=os.environ["TEMP"]) as directory:
            run = Path(directory)
            command, owner = b"note offline custody test\n", b'{"pending":true}\n'
            command_path, owner_path = run/"command.txt", run/"live-driver-owner.json"
            command_path.write_bytes(command)
            owner_path.write_bytes(owner)
            driver = types.SimpleNamespace(owner=types.SimpleNamespace(api=api), run=run,
                command_path=command_path, command_hash=m.transport.digest(command), owner_bytes=lambda pending: owner)
            driver.lock = api.CreateFileW(str(run/"live-driver.lock"), 0xC0000000, 0, None, 4, 0x80, None)
            lease = m.MotionInputLease(driver, m.time.perf_counter_ns()+m.NS)
            try:
                self.assertEqual(command_path.read_bytes(), command)
                for path in (command_path, owner_path):
                    with self.assertRaises(OSError):
                        path.write_bytes(b"foreign")
                # The same production POSIX publication path may replace a
                # read-sealed pathname. It must either refuse or be detected.
                try:
                    m.publish_owner(driver, b'{"foreign":true}\n')
                except OSError:
                    self.assertEqual(owner_path.read_bytes(), owner)
                else:
                    lease.next_check = 0
                    with self.assertRaisesRegex(m.Refusal, "pathname ownership changed"):
                        lease.check(m.time.perf_counter_ns()+m.NS)
            finally:
                lease.close()
                api.CloseHandle(driver.lock)
            command_path.write_bytes(b"released")

    def test_metadata_identity_is_bounded_to_observer_cadence_and_detects_replacement(self):
        clock = df.Clock()
        lease = m.MotionInputLease.__new__(m.MotionInputLease)
        lease.next_check = clock.now+m.PERIOD_NS
        lease.entries = [(Path("command.txt"), (1, b"command")), (Path("owner.json"), (1, b"owner"))]
        lease.api = types.SimpleNamespace(CreateFileW=Mock(return_value=99), CloseHandle=Mock())
        lease.identity = Mock(side_effect=[(1, b"command"), (1, b"owner"), (2, b"foreign")])
        with patch.object(m, "time", clock), patch.object(m.transport, "time", clock):
            lease.check(clock.now+m.NS)
            self.assertFalse(lease.api.CreateFileW.called)
            clock.sleep(.1)
            lease.check(clock.now+m.NS)
            self.assertEqual(lease.api.CreateFileW.call_count, 2)
            clock.sleep(.099)
            lease.check(clock.now+m.NS)
            self.assertEqual(lease.api.CreateFileW.call_count, 2)
            clock.sleep(.001)
            with self.assertRaisesRegex(m.Refusal, "pathname ownership changed"):
                lease.check(clock.now+m.NS)
            self.assertEqual(lease.api.CloseHandle.call_count, 3)
        for call in lease.api.CreateFileW.call_args_list:
            self.assertEqual(call.args[1:3], (0, 7))

    @contextmanager
    def runtime(self, plan=None):
        """Real send/ACK/read/ownership + a deterministic runtime frame model."""
        clock = df.Clock()
        clock.now = f.NOW
        if plan is None:
            plan = next(p for p in plans() if p["family"] == "sight")
        with df.StimulusTests().ack_driver(clock, archive_ns=0) as h:
            d = h.driver
            d.stimulus, d.geometry = plan, g
            d.args = types.SimpleNamespace(family=plan["family"], stage=plan["stage"], target="secondary")
            d.profiles, d.plans, d.boundaries = None, [], {}
            d.first_sample_pending, d.neutral_motion_supported = True, True
            d.initial_mapping_deadline = plan["source"]["validUntilNs"]
            d.supervisor_owner, d.lock = Mock(), 12345
            d.motion_lease, d.outstanding_motion = None, None
            d.target_observed = False
            d.observations = Mock()  # Native evidence is independently injected at the boundary below.
            h.state["handL"].update(pos=plan["schedule"][0]["positionMeters"], ypr=plan["schedule"][0]["yawPitchRollDegrees"])
            h.active, h.frames, h.endpoint_calls, h.boundary_calls = None, [], [], []
            h.freeze_frames = False
            h.contact_error = None
            publish, read_state = m.transport.atomic.side_effect, m.sim_state.side_effect

            def atomic(path, raw, **kwargs):
                publish(path, raw, **kwargs)
                for line in raw.decode().splitlines():
                    if line.startswith("hand l to grip "):
                        values = line.split()[4:]
                        h.active = dict(start=clock.now, duration=int(values[6])*1_000_000,
                            source=g.controller_pose(dict(positionMeters=h.state["handL"]["pos"], yawPitchRollDegrees=h.state["handL"]["ypr"])),
                            target=g.controller_pose(dict(positionMeters=list(map(float, values[:3])), yawPitchRollDegrees=list(map(float, values[3:6])))))

            def frame(path, until, trace=None):
                result = read_state(path, until, trace)
                if h.active is not None and not h.freeze_frames:
                    a = h.active
                    t = min(1., (clock.now-a["start"])/a["duration"]) if a["duration"] else 1.
                    pose = g.feed.to_pose(g.feed.blend(a["source"], a["target"], t*t*(3-2*t)))
                    h.state["handL"].update(pos=pose["position"], ypr=g.feed.quaternion_ypr(pose["orientation"]))
                    h.state["frame"] += 1
                    h.state["endFrames"] += 1
                    result = copy.deepcopy(h.state)
                    h.frames.append((clock.now, t, copy.deepcopy(result["handL"])))
                return result

            def fresh(*, boundary, after_ns=0, deadline_ns=None, **kwargs):
                self.assertLess(clock.now, deadline_ns)
                self.assertEqual(h.state["controls"]["gripL"], 0)
                self.assertIsNotNone(d.outstanding_motion)
                m.ReloadStimulus.check_target(h.state, plan["schedule"][g.neutral_motion(plan)["lastSample"]])
                h.boundary_calls.append((boundary["id"], after_ns, deadline_ns))
                if h.contact_error:
                    raise m.Refusal(h.contact_error)
                if boundary["id"] != "neutral_motion_contact":
                    d.boundaries[boundary["id"]] = dict(observed=True)
                # Boundary data has no native claim: this harness tests the
                # assembled transport; real geometry admission is tested below.
                snapshot = dict(nonce=1001)
                return snapshot, clock.now

            d.fresh = fresh
            with patch.object(m.transport, "atomic", side_effect=atomic), patch.object(m, "sim_state", side_effect=frame), \
                    patch.object(m, "MotionInputLease", MemoryLease), patch.object(g, "source_deadline", side_effect=lambda *a: clock.now+70_000_000):
                yield clock, h

    def test_assembled_all_eligible_family_send_interpolate_endpoint_then_acquire(self):
        representatives = {}
        for plan in plans():
            if g.neutral_motion(plan):
                representatives.setdefault(plan["family"], plan)
        for family, plan in representatives.items():
            with self.subTest(family=family), self.runtime(plan) as (clock, h):
                d, original_deadline = h.driver, h.driver.stage_deadline
                motion = g.neutral_motion(plan)
                d.execute_neutral_motion(motion)
                self.assertEqual(len(h.published), 1)
                raw = h.published[0].decode()
                self.assertEqual(raw.count("hand l to grip "), 1)
                self.assertNotIn("to aim", raw)
                self.assertNotIn("grip l 1", raw)
                self.assertTrue(any(0 < t < 1 for _, t, _ in h.frames))
                self.assertIsNone(d.outstanding_motion)
                self.assertEqual(d.stage_deadline, original_deadline)
                self.assertTrue(d.first_sample_pending)
                self.assertEqual(len(h.boundary_calls), 1)
                d.send(plan["schedule"][motion["lastSample"]+1])
                self.assertTrue(d.held_left)
                self.assertIn("grip l 1", h.published[-1].decode())

    def test_wait_has_no_payload_reads_or_repeated_setup(self):
        with self.runtime() as (clock, h):
            d = h.driver
            d.motion_lease = MemoryLease(d, d.deadline)
            with patch.object(d, "read_state", side_effect=AssertionError("synchronous payload read")), \
                    patch.object(d, "ownership", side_effect=AssertionError("command payload read")):
                for _ in range(10):
                    d.motion_guard(d.deadline)
                    clock.sleep(.01)
            self.assertEqual(d.owner.check.call_count, 10)
            self.assertEqual(d.supervisor_owner.check.call_count, 10)

    @contextmanager
    def readiness_runtime(self, *, change=None, late=False, limit=None):
        source = f.complete(f.xf.snapshot(kind=3))
        plan = g.build_stimulus("fixed", None, source, f.NOW, "Open")
        source_deadline = g.source_deadline
        with self.runtime(plan) as (clock, h):
            d = h.driver
            d.mapping = source
            d.observations = types.MethodType(m.ReloadStimulus.observations, d)
            h.publish_observations = False
            h.deliveries = []
            frame = m.sim_state.side_effect

            def read(path, until, trace=None):
                result = frame(path, until, trace)
                if h.deliveries and clock.now >= h.deliveries[0]["receivedNs"]:
                    d.observer.inbox.put(h.deliveries.pop(0))
                if change and hasattr(h, "settled_ns") and clock.now >= h.settled_ns+100_000_000:
                    change(d, h, result)
                return result

            def fresh(**kwargs):
                h.settled_ns = clock.now
                if limit is not None:
                    setattr(d, limit, clock.now+200_000_000)
                endpoint = plan["schedule"][g.neutral_motion(plan)["lastSample"]]
                # The first native copy precedes settlement. The final copy
                # arrives after both the old endpoint limit and a fresh250ms
                # wait, but within its own unchanged250ms response budget.
                for nonce, observed, received, requested in (
                        (201, -1, 20_000_000, -50_000_000),
                        (202, 280_000_000, 290_000_000, 39_000_000 if late else 90_000_000)):
                    snapshot = f.xf.refresh(source, clock.now+observed, nonce)
                    f.xf.set_local(snapshot, f.sf.emitted_local(source, endpoint))
                    h.deliveries.append(dict(kind="snapshot", index=nonce, snapshot=snapshot,
                        raw=json.dumps(snapshot).encode(), requestNs=clock.now+requested,
                        deadlineNs=clock.now+requested+m.RECEIVE_NS, receivedNs=clock.now+received,
                        ownershipCheckedNs=clock.now+received, requestOwned=True, responseOwned=True))
                return m.ReloadStimulus.fresh(d, **kwargs)

            d.fresh = fresh
            with patch.object(m, "sim_state", side_effect=read), \
                    patch.object(g, "source_deadline", side_effect=source_deadline):
                yield clock, h

    def test_settled_neutral_endpoint_waits_for_fresh_evidence_before_acquisition(self):
        with self.readiness_runtime() as (clock, h):
            d = h.driver
            limits = d.deadline, d.stage_deadline
            motion = g.neutral_motion(d.stimulus)
            d.execute_neutral_motion(motion)
            self.assertEqual(len(h.published), 1)
            self.assertFalse(d.held_left)
            event = next(e for e in d.artifacts.events if e["type"] == "neutral_motion_endpoint")
            self.assertGreater(clock.now, event["originalDeadlineNs"])
            self.assertEqual(int(event["contactNonce"]), 202)
            self.assertEqual((d.deadline, d.stage_deadline), limits)
            self.assertEqual(d.initial_mapping_deadline, g.source_deadline(d.observed[-1]["snapshot"], "fixed"))
            d.send(d.stimulus["schedule"][motion["lastSample"]+1])
            self.assertTrue(d.held_left)
            self.assertEqual(len(h.published), 2)

    def test_neutral_readiness_refuses_changed_owner_pose_or_command(self):
        changes = {
            "owner": (lambda d, h, state: state.update(cmdSeq=d.sequence+1), "another writer"),
            "pose": (lambda d, h, state: state["handL"]["pos"].__setitem__(0, 99.), "simulator input changed"),
            "command": (lambda d, h, state: setattr(d, "last_write", d.last_write+1), "command identity changed"),
        }
        for name, (change, reason) in changes.items():
            with self.subTest(change=name), self.readiness_runtime(change=change) as (clock, h):
                d = h.driver
                with self.assertRaisesRegex(m.Refusal, reason):
                    d.execute_neutral_motion(g.neutral_motion(d.stimulus))
                self.assertFalse(d.held_left)
                self.assertIsNotNone(d.outstanding_motion)
                self.assertEqual(len(h.published), 1)

    def test_neutral_readiness_preserves_response_and_stage_driver_deadlines(self):
        for limit, late in (("stage_deadline", False), ("deadline", False), ("stage_deadline", True)):
            with self.subTest(limit=limit, late=late), self.readiness_runtime(late=late,
                    limit=None if late else limit) as (clock, h):
                d = h.driver
                if late:
                    d.stage_deadline = clock.now+2*m.NS
                with self.assertRaisesRegex(m.Refusal, "deadline|original budget"):
                    d.execute_neutral_motion(g.neutral_motion(d.stimulus))
                self.assertFalse(d.held_left)
                self.assertEqual(len(h.published), 1)
                self.assertIsNotNone(d.outstanding_motion)
                if late:
                    self.assertFalse(d.observed[-1]["current"])
                    self.assertEqual(d.observed[-1]["deadlineNs"], h.settled_ns+289_000_000)
                else:
                    self.assertEqual(clock.now, getattr(d, limit))

    def test_motion_owner_loss_blocks_cancellation_without_clearing_uncertainty(self):
        with self.runtime() as (clock, h):
            d = h.driver
            d.motion_lease = MemoryLease(d, d.deadline)
            d.outstanding_motion = dict(status="interpolating")
            d.motion_lease.check = Mock(side_effect=m.Refusal("motion input pathname ownership changed"))
            with self.assertRaisesRegex(m.Refusal, "pathname ownership changed"):
                d.motion_guard(d.deadline)
            self.assertTrue(d.unsafe_owner)
            with self.assertRaisesRegex(m.Refusal, "ownership uncertainty"):
                d.cleanup()
            self.assertFalse(h.published)
            self.assertIsNotNone(d.outstanding_motion)

    def test_normal_stop_prevents_more_motion_but_allows_owned_cancellation(self):
        with self.runtime() as (clock, h):
            d = h.driver
            h.contact_error = "stop before acquire"
            with self.assertRaises(m.Refusal):
                d.execute_neutral_motion(g.neutral_motion(d.stimulus))
            original_exists = Path.exists
            def exists(path):
                return True if path.name == "stop.request" else original_exists(path)
            with patch.object(Path, "exists", exists):
                d.motion_lease = MemoryLease(d, d.deadline)
                with self.assertRaisesRegex(m.Refusal, "normal stop requested"):
                    d.motion_guard(d.deadline)
                self.assertIn("cancellation acknowledged", d.cleanup())
            self.assertIsNone(d.outstanding_motion)

    def test_execute_skips_old_neutral_rows_but_keeps_acquisition_boundary(self):
        plan = next(plans())  # Pump ShellPickup ends at owned shell acquisition.
        with self.runtime(plan) as (clock, h):
            d = h.driver
            contact = d.fresh
            def fresh(*, boundary, **kwargs):
                if boundary["id"] == "neutral_motion_contact":
                    return contact(boundary=boundary, **kwargs)
                self.assertEqual(boundary["id"], "shell_held")
                self.assertTrue(d.held_left)
                d.boundaries[boundary["id"]] = dict(observed=True)
                return dict(nonce=1002), clock.now
            d.fresh = fresh
            d.execute(plan)
            self.assertEqual(len(h.published), 2)
            self.assertEqual(sum(raw.count(b"hand l to grip ") for raw in h.published), 1)
            self.assertTrue(d.held_left)
            self.assertTrue(d.plans[-1]["boundaryChecksPassed"])

    def test_failed_contact_cancels_neutral_motion_without_held_grip(self):
        with self.runtime() as (clock, h):
            d = h.driver
            h.contact_error = "fresh contact rejected"
            with self.assertRaisesRegex(m.Refusal, "fresh contact rejected"):
                d.execute_neutral_motion(g.neutral_motion(d.stimulus))
            self.assertFalse(d.held_left)
            self.assertIsNotNone(d.outstanding_motion)
            self.assertTrue(json.loads(h.owner_record)["pending"])
            self.assertIn("cancellation acknowledged", d.cleanup())
            lines = h.published[-1].decode().splitlines()
            self.assertTrue(lines[0].startswith("hand l to grip ") and lines[0].endswith(" 0"))
            self.assertEqual(lines[1], "grip l 0")
            self.assertIsNone(d.outstanding_motion)
            self.assertFalse(json.loads(h.owner_record)["pending"])

    def test_stalled_frames_never_complete_or_acquire(self):
        with self.runtime() as (clock, h):
            d = h.driver
            guard = d.motion_guard
            def freeze(until):
                h.freeze_frames = True
                guard(until)
            d.motion_guard = freeze
            with self.assertRaises(m.Refusal):
                d.execute_neutral_motion(g.neutral_motion(d.stimulus))
            self.assertFalse(h.boundary_calls)
            self.assertIsNotNone(d.outstanding_motion)
            self.assertEqual(len(h.published), 1)

    def test_expired_source_publishes_nothing(self):
        with self.runtime() as (clock, h):
            h.driver.initial_mapping_deadline = clock.now
            with self.assertRaisesRegex(m.Refusal, "initial mapping expired"):
                h.driver.execute_neutral_motion(g.neutral_motion(h.driver.stimulus))
            self.assertFalse(h.published)
            self.assertIsNone(h.driver.outstanding_motion)

    def test_late_ack_retains_motion_and_original_deadline(self):
        with self.runtime() as (clock, h):
            h.state_read_ns = m.ACK_NS+1
            d = h.driver
            with self.assertRaises(m.Refusal):
                d.execute_neutral_motion(g.neutral_motion(d.stimulus))
            self.assertTrue(d.pending_command)
            self.assertIsNotNone(d.outstanding_motion)
            self.assertEqual(len(h.published), 1)
            self.assertEqual(d._ack_failures[0].deadline_ns-d._ack_failures[0].requested_ns, m.ACK_NS)

    def test_cancellation_failure_keeps_owner_motion_uncertainty(self):
        with self.runtime() as (clock, h):
            d = h.driver
            h.contact_error = "contact refused"
            with self.assertRaises(m.Refusal):
                d.execute_neutral_motion(g.neutral_motion(d.stimulus))
            h.state_read_ns = m.ACK_NS+1
            with self.assertRaises(m.Refusal):
                d.cleanup()
            self.assertIsNotNone(d.outstanding_motion)
            self.assertTrue(d.pending_command)
            self.assertTrue(json.loads(h.owner_record)["outstandingMotion"])


if __name__ == "__main__":
    unittest.main()
