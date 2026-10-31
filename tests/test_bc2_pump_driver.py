"""Deterministic in-memory pump driver checks; no game/process/native effects."""
import copy
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


d = load("pump_driver_subject", ROOT / "tools/drive_bc2_pump_simulation.py")
memory = load("pump_driver_memory", ROOT / "tests/test_bc2_feed_driver_observation.py")
fixture = load("pump_driver_geometry", ROOT / "tests/test_bc2_pump_simulation_plan.py")
memory.d = d.transport
MS, NS = 1_000_000, d.NS
REFUSALS = (d.REFUSAL, d.planner.Refusal)


def snapshot(now, nonce=1, pumping=False):
    value = fixture.refresh(fixture.snapshot(pumping), now, now//MS)
    value["nonce"] = nonce
    value["observed_ns"] = now
    value["input"]["floorRelative"] = False
    return None, value


def ready_snapshot(before, plan, now):
    s = fixture.refresh(before, now, now//MS)
    sp, pump = s["shellPump"], s["shellPump"]["pump"]
    cycle = copy.deepcopy(plan["baseline"]["pumpLease"])
    release = dict(cycle=cycle, request=600, inputSequence=now//MS-1,
                   observedNs=now-40*MS, deadlineNs=now+20*MS)
    pump.update(lastSettled=dict(release=release, sequence=now//MS,
        observedNs=now-30*MS, deadlineNs=now+30*MS, nativeReady=True, unchangedAmmunition=True),
        acknowledgedNs=now-20*MS, debt=False, blocksFire=False, phase=7,
        supportHolding=True, supportToken=203)
    pump["native"].update(phase=5, blocksFire=False, held=None)
    sp["leftClaim"] = fixture.claim(s, 0, 2, cycle["item"], dict(id=2, generation=14), 203, 200)
    s["weapon_actions_blocked"] = False
    return s


def inserted_snapshot(held, plan, now):
    s = fixture.refresh(held, now, now//MS)
    sh, item = s["shellPump"]["shell"], plan["baseline"]["shellItem"]
    sh.update(held=False, ammoOwnsHand=False, item=None)
    s["shellPump"]["leftClaim"] = None
    sh["lifetimeCounters"].update(submitted=plan["baseline"]["shellCounters"]["submitted"]+1,
                                   completed=plan["baseline"]["shellCounters"]["completed"]+1)
    for counts in (sh["reserve"], sh["heldLease"]):
        counts.update(loaded=plan["baseline"]["ammo"]["loaded"]+1,
                      reserve=plan["baseline"]["ammo"]["reserve"]-1)
    sh["lastSubmitted"] = dict(request=800, operation=3, submittedNs=now-40*MS,
        heldLease={**copy.deepcopy(held["shellPump"]["shell"]["heldLease"]),
                   "observedNs": now-50*MS, "deadlineNs": now+20*MS},
        reservation=dict(request=800, cycle=700, seat=900, item=item["key"], claim=item["claim"]["token"]))
    sh["lastAcknowledged"] = dict(identity=copy.deepcopy(sh["identity"]), cycle=700, request=800,
        operation=3, status=1, sampleSequence=now//MS, serverInvocation=910,
        observedNs=now-30*MS, deadlineNs=now+50*MS, verified=True, acknowledgedNs=now-20*MS)
    return s


def harness(test, pumping=False):
    memory.fixture = lambda now, nonce=1: snapshot(now, nonce, pumping)
    h = memory.Harness(test)
    v = h.driver
    v.__class__ = d.PumpDriver
    v.args = SimpleNamespace(step_ms=40)
    v.active_plan, v.demand_reference, v.demand_started = None, None, None
    v.demand_finished, v.gate_receipts, v.release_authorized = True, [], False
    v.plans, v.requested_stage, v.pickup_first = [], "PumpCycle", False
    v.owner = SimpleNamespace(check=lambda: None)
    v.manifest = dict(source_sha256="synthetic-source", version="synthetic")
    v.link = dict(supervisor=dict(pid=2))
    v.folder = v.run / "session-folder"
    v.session_path, v.state_path = v.folder / "session.json", v.run / "state.json"
    v.capture_plan = dict(maximum_compositions=1)
    h.json_files[v.session_path] = dict(host=v.host, supervisor=v.link["supervisor"],
        source_sha256="synthetic-source", version="synthetic", state="running")
    h.files[v.session_path] = b"synthetic"
    h.state.update(sessionRunning=True, actionsAttached=True, paceMode="free",
        head=dict(valid=True), handR=dict(valid=True),
        handL=dict(valid=True, followsHead=False, pos=[0, 0, 0], ypr=[0, 0, 0]))
    h.json_files[v.state_path], h.files[v.state_path] = h.state, b"synthetic"
    v.initial_sim = copy.deepcopy(h.state)
    v.read_state = lambda: d.transport.Driver.read_state(v)
    v.started = h.now
    # Extend the memory simulator to commit actual pose/grip and increasing frames.
    def sleep(seconds):
        pending, frame, ended = h.command_due, h.state["frame"], h.state["endFrames"]
        memory.Harness.sleep(h, seconds)
        if pending is not None and h.command_due is None:
            batch = h.files[v.command_path].decode().splitlines()
            for line in batch:
                if line.startswith("grip l "):
                    h.state["controls"]["gripL"] = float(line.split()[-1])
                elif line.startswith("hand l grip pose "):
                    values = [float(x) for x in line.split()[4:]]
                    h.state["handL"].update(pos=values[:3], ypr=values[3:], followsHead=False)
            h.state.update(frame=frame+2, endFrames=ended+2)
            h.files[v.ack_path] = (f"seq={h.state['cmdSeq']} frame={frame+1} error=-\n"
                                     f"last={' | '.join(batch)}\n").encode()
            if h.corrupt_ack:
                h.corrupt_ack()
    h.corrupt_ack = None
    test.enterContext(patch.object(d.time, "sleep", sleep))
    return h


def admit_current(h, pumping=False):
    v = h.driver
    _, s = snapshot(h.now, v.pending_nonce or h.now, pumping)
    v.pending_nonce, v.last_request = None, h.now
    v.accept_snapshot(s, d.transport.json_bytes(s), h.now)
    return s


def plan_current(h, pumping=False):
    v = h.driver
    s = admit_current(h, pumping)
    v.reference = s
    v.active_plan = d.planner.plan(s, "PumpCycle" if pumping else "ShellPickup", h.now,
                                 s["nonce"], 40*MS)
    return v.active_plan


def gate_state(h, plan, gate_id):
    gate = next(g for g in plan["receiptGates"] if g["id"] == gate_id)
    target = plan["schedule"][gate["afterSample"]]
    h.state["controls"]["gripL"] = 1.
    h.state["handL"].update(pos=target["positionMeters"], ypr=target["yawPitchRollDegrees"], followsHead=False)
    h.driver.last_commit = h.now
    return gate, target


class PumpDriverTests(unittest.TestCase):
    def test_complete_pump_schedule_stops_at_every_actual_gate_then_releases(self):
        h = harness(self, True)
        v = h.driver
        v.stage_deadline, v.motion_started = v.deadline, None
        h.phase = None
        base_send = v.send
        def send(lines, expected=None, **kwargs):
            result = base_send(lines, expected=expected, **kwargs)
            if expected:
                h.phase = expected["phase"]
            return result
        v.send = send
        def response(now, nonce=1):
            plan, before = v.active_plan, v.reference
            if plan is None or h.phase in (None, "neutral", "approach", "neutral_at_contact"):
                return snapshot(now, nonce, True)
            if h.phase == "front_dwell":
                s = ready_snapshot(before, plan, now)
            else:
                travel = before["shellPump"]["profile"]["pump"]["strokeMeters"] if h.phase in ("rear", "rear_dwell", "return") else 0
                s = fixture.pump_endpoint(before, travel, now, now//MS)
            s["nonce"], s["observed_ns"] = nonce, now
            return None, s
        memory.fixture = response
        plan = v.stage("PumpCycle")
        self.assertEqual([r["gate"] for r in v.gate_receipts], ["pump_acquired", "pump_rear", "pump_ready"])
        self.assertFalse(v.held_left)
        self.assertFalse(v.pending_command)
        self.assertGreater(v.plans[-1]["actualDurationNs"], plan["durationNs"])
        self.assertLess(h.now, v.hard_deadline)
        self.assertLess(v.commands, 512)
        self.assertTrue(all(b[0]-a[0] >= 25*MS for a, b in zip(h.command_writes, h.command_writes[1:])))

    def test_continuous_pickup_capture_and_exact_insert_round_receipt(self):
        h = harness(self)
        v = h.driver
        v.stage_deadline, v.motion_started = v.deadline, None
        h.phase, h.pickup, h.held = None, None, None
        base_send = v.send
        def send(lines, expected=None, **kwargs):
            result = base_send(lines, expected=expected, **kwargs)
            if expected:
                h.phase = expected["phase"]
            return result
        v.send = send
        def response(now, nonce=1):
            plan = v.active_plan
            if plan is None or (plan["stage"] == "ShellPickup" and h.phase != "acquire_shell"):
                return snapshot(now, nonce)
            if plan["stage"] == "ShellPickup":
                s = fixture.refresh(fixture.held_after_pickup(v.reference, plan, now), now, now//MS)
                h.held = copy.deepcopy(s)
            elif h.phase == "seat_dwell":
                s = inserted_snapshot(h.held, plan, now)
            else:
                s = fixture.refresh(h.held, now, now//MS)
                if h.phase in ("alignment", "insert"):
                    sp = s["shellPump"]
                    sp["shell"]["geometry"] = dict(available=True, captured=False, seated=False, phase=1,
                        owner=copy.deepcopy(sp["input"]["owner"]), item=copy.deepcopy(sp["shell"]["item"]["key"]),
                        cycle=700, sourceSequence=now//MS-1, currentSequence=now//MS,
                        observedNs=now-MS, deadlineNs=now+60*MS, progress=0., alignment=1.)
            s["nonce"], s["observed_ns"] = nonce, now
            return None, s
        memory.fixture = response
        pickup = v.stage("ShellPickup")
        self.assertTrue(v.held_left)
        pickup_end = len(h.command_writes)
        v.stage("ShellInsert", pickup)
        self.assertEqual([r["gate"] for r in v.gate_receipts], ["shell_held", "shell_captured", "shell_inserted"])
        self.assertFalse(v.held_left)
        self.assertLess(v.commands, 512)
        self.assertLess(h.now, v.hard_deadline)
        self.assertTrue(all(b"grip l 0" not in raw for _, raw in h.command_writes[pickup_end:-1]))
        self.assertTrue(h.command_writes[-1][1].startswith(b"grip l 0\n"))

    def test_ordinary_left_batch_and_actual_committed_pose(self):
        h = harness(self)
        plan = plan_current(h)
        row = plan["schedule"][0]
        h.driver.send(row["commands"], expected=row)
        self.assertFalse(h.driver.pending_command)
        self.assertEqual(h.driver.sequence, 10+len(row["commands"])+1)
        self.assertEqual(h.state["lastCmd"], h.driver.sentinel)
        self.assertEqual(h.state["lastCmdError"], "")
        self.assertGreater(h.state["frame"], 100)
        self.assertEqual(h.state["handL"]["pos"], row["positionMeters"])
        self.assertEqual(len(h.command_writes), 1)

    def test_only_bounded_ordinary_left_input(self):
        for line in ("grip r 0", "trigger l 1", "btn b down", "shot x", "hand l follow on",
                     "hand l aim pose nan 0 0 0 0 0", "hand l grip pose 11 0 0 0 0 0",
                     "grip l 1\ntrigger r 1"):
            with self.subTest(line=line), self.assertRaises((d.REFUSAL, ValueError)):
                d.ordinary_left([line])
        d.ordinary_left(["hand l follow off", "hand l aimtrim 0 0", "hand l grip pose 0 0 0 0 0 0",
                         "hand l aim pose 0 0 0 0 0 0", "grip l 1"])

    def test_initial_capture_pending_is_never_admitted_and_reobserves_without_commands(self):
        h = harness(self, True)
        v = h.driver
        v.reference, v.latest_snapshot, v.demand_finished = None, None, False
        _, pending = snapshot(h.now, 1, True)
        pending["shellPump"] = dict(available=False, reason="capture_pending_or_not_requested")
        original = copy.deepcopy(pending)
        h.publish(pending)
        actual, _, _ = v.fresh_available()
        self.assertTrue(actual["shellPump"]["available"])
        self.assertTrue(v.demand_finished)
        self.assertEqual(v.demand_reference, original)
        self.assertEqual(h.command_writes, [])
        self.assertTrue(any(e["type"] == "pump_capture_pending" for e in v.artifacts.events))
        self.assertLess(h.now-v.demand_started, NS)

    def test_persistent_pending_demand_is_bounded_without_renewing_source(self):
        h = harness(self, True)
        v = h.driver
        v.reference, v.latest_snapshot, v.demand_finished = None, None, False
        def pending(now, nonce=1):
            _, s = snapshot(now, nonce, True)
            s["shellPump"] = dict(available=False, reason="capture_pending_or_not_requested")
            return None, s
        memory.fixture = pending
        h.publish(pending(h.now, 1)[1])
        with self.assertRaises(REFUSALS):
            v.fresh_available()
        self.assertEqual(h.command_writes, [])
        self.assertIsNone(v.latest_snapshot)
        self.assertLess(h.now-v.demand_started, NS+200*MS)

    def test_pending_cannot_hide_changed_owner_or_wrong_unavailable_reason(self):
        for changed in ("owner", "reason"):
            with self.subTest(changed=changed):
                h = harness(self, True)
                v = h.driver
                v.demand_finished, v.demand_started = False, h.now
                _, s = snapshot(h.now, 2, True)
                s["shellPump"] = dict(available=False, reason="capture_pending_or_not_requested")
                v.demand_reference = copy.deepcopy(s)
                if changed == "owner":
                    s["owner"]["weapon"] += 1
                else:
                    s["shellPump"]["reason"] = "source_mismatched_or_expired"
                with self.assertRaises(REFUSALS):
                    v.admit_snapshot(s, h.now, 2)
                self.assertEqual(h.command_writes, [])

    def test_each_gate_requires_new_actual_snapshot_and_holds_pose(self):
        h = harness(self, True)
        v, plan = h.driver, plan_current(h, True)
        gate, target = gate_state(h, plan, "pump_acquired")
        baseline = copy.deepcopy(v.reference)
        memory.fixture = lambda now, nonce=1: (None, {**fixture.pump_endpoint(baseline, 0, now, now//MS), "nonce": nonce})
        receipt = v.wait_gate(plan, gate, target)
        self.assertEqual(receipt["gate"], "pump_acquired")
        self.assertFalse(receipt["nativeAcceptance"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.state["controls"]["gripL"], 1.)
        self.assertGreater(v.latest_snapshot["observed_ns"], v.last_commit)
        self.assertEqual(plan["originalLeases"]["snapshot"], baseline["valid_until_ns"])

    def test_gate_retries_actual_missing_evaluation_without_timed_success(self):
        h = harness(self, True)
        v, plan = h.driver, plan_current(h, True)
        gate, target = gate_state(h, plan, "pump_rear")
        gate = {**gate, "timeoutNs": 230*MS}
        with self.assertRaises(REFUSALS):
            v.wait_gate(plan, gate, target)
        self.assertEqual(v.gate_receipts, [])
        self.assertEqual(h.command_writes, [])
        self.assertTrue(v.held_left)
        self.assertTrue(any(e["type"] == "pump_gate_pending" for e in v.artifacts.events))

    def test_changed_native_cycle_owner_profile_or_basis_aborts_gate(self):
        for changed in ("cycle", "owner", "profile", "basis", "right"):
            with self.subTest(changed=changed):
                h = harness(self, True)
                v, plan = h.driver, plan_current(h, True)
                gate, target = gate_state(h, plan, "pump_acquired")
                baseline = copy.deepcopy(v.reference)
                def changed_snapshot(now, nonce=1):
                    s = fixture.pump_endpoint(baseline, 0, now, now//MS)
                    s["nonce"] = nonce
                    if changed == "cycle":
                        s["shellPump"]["pump"]["native"]["cycle"] += 1
                    elif changed == "owner":
                        s["owner"]["weapon"] += 1
                    elif changed == "profile":
                        s["shellPump"]["profile"]["shell"]["travelMeters"] += .01
                    elif changed == "basis":
                        s["mapping"]["trackingBodyWorldMeters"][3][0] += .001
                    else:
                        s["input"]["hands"][1]["squeeze"] = .9
                    return None, s
                memory.fixture = changed_snapshot
                with self.assertRaises(REFUSALS):
                    v.wait_gate(plan, gate, target)
                self.assertEqual(h.command_writes, [])
                self.assertEqual(v.gate_receipts, [])

    def test_expired_snapshots_refresh_without_renewal_then_refuse(self):
        h = harness(self, True)
        v = h.driver
        original = copy.deepcopy(v.reference)
        h.response_stale = True
        with self.assertRaises(REFUSALS):
            v.fresh_available()
        self.assertEqual(v.reference, original)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.snapshot_refresh_requests, 2)

    def test_actual_ready_and_support_gate_is_required_for_normal_release(self):
        h = harness(self, True)
        v, plan = h.driver, plan_current(h, True)
        gate, target = gate_state(h, plan, "pump_ready")
        baseline = copy.deepcopy(v.reference)
        memory.fixture = lambda now, nonce=1: (None, {**ready_snapshot(baseline, plan, now), "nonce": nonce})
        v.wait_gate(plan, gate, target)
        v.release_authorized = True
        v.send(["grip l 0"])
        self.assertFalse(v.held_left)
        self.assertEqual(len(h.command_writes), 1)
        self.assertTrue(h.command_writes[0][1].startswith(b"grip l 0\n"))

    def test_unobserved_normal_release_is_refused_but_owned_failure_cleanup_works(self):
        h = harness(self, True)
        v = h.driver
        plan_current(h, True)
        v.release_authorized = True
        with self.assertRaisesRegex(d.REFUSAL, "requires actual"):
            v.send(["grip l 0"])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.release_after_failure(), "ordinary owned controller release acknowledged")
        self.assertFalse(v.held_left)
        self.assertTrue(v.artifacts.events[-1]["cleanup"])

    def test_cmdseq_sentinel_error_frame_and_batch_ack_are_verified(self):
        for changed in ("cmdseq", "sentinel", "error", "frame", "batch"):
            with self.subTest(changed=changed):
                h = harness(self)
                v, plan = h.driver, plan_current(h)
                row = plan["schedule"][0]
                def corrupt():
                    if changed == "cmdseq":
                        h.state["cmdSeq"] += 1
                    elif changed == "sentinel":
                        h.state["lastCmd"] = "other-writer"
                    elif changed == "error":
                        h.state["lastCmdError"] = "bad command"
                    elif changed == "frame":
                        h.state.update(frame=100, endFrames=99)
                    else:
                        h.files[v.ack_path] = h.files[v.ack_path].replace(b"last=", b"last=foreign ")
                h.corrupt_ack = corrupt
                with self.assertRaises(REFUSALS):
                    v.send(row["commands"], expected=row)
                self.assertTrue(v.pending_command)
                self.assertEqual(len(h.command_writes), 1)

    def test_changed_writer_session_and_stop_prevent_motion_and_cleanup(self):
        for changed in ("file", "cmdseq", "sentinel", "session", "stop"):
            with self.subTest(changed=changed):
                h = harness(self)
                v, plan = h.driver, plan_current(h)
                row = plan["schedule"][0]
                if changed == "file":
                    h.files[v.command_path] = b"foreign\n"
                elif changed == "cmdseq":
                    h.state["cmdSeq"] += 1
                elif changed == "sentinel":
                    h.state["lastCmd"] = "foreign"
                elif changed == "session":
                    h.json_files[v.session_path]["host"] = dict(pid=99)
                else:
                    h.files[v.folder / "stop.request"] = b"1"
                with self.assertRaises(REFUSALS):
                    v.send(row["commands"], expected=row)
                try:
                    self.assertIn("not attempted", v.release_after_failure())
                except REFUSALS:
                    pass
                self.assertEqual(h.command_writes, [])

    def test_ten_second_and_command_limits_are_unchanged(self):
        h = harness(self)
        v, plan = h.driver, plan_current(h)
        row = plan["schedule"][0]
        v.commands = 512
        with self.assertRaisesRegex(d.REFUSAL, "512-command"):
            v.send(row["commands"], expected=row)
        v.commands = 0
        v.deadline = h.now
        with self.assertRaisesRegex(d.REFUSAL, "ten-second"):
            v.send(row["commands"], expected=row)
        self.assertEqual(h.command_writes, [])

    def test_cli_requires_continuous_pickup_and_supported_transport_period(self):
        with patch.object(d, "PumpDriver") as driver, patch("sys.stdout", new_callable=io.StringIO):
            for args in (("--stage", "ShellInsert"), ("--stage", "PumpCycle", "--pickup-first"),
                         ("--stage", "PumpCycle", "--step-ms", "20")):
                self.assertEqual(d.main(["--run", "memory", *args]), 2)
            driver.assert_not_called()


if __name__ == "__main__":
    unittest.main()
