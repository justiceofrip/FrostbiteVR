"""Assembled shared entry/readiness checks; synthetic evidence, no native claims.

Only snapshot delivery, host acknowledgement, artifacts and clocks are replaced.
The public entry, mapping, original frame admission, readiness, reach/selection,
command validation, withdrawal and owned release execute their production paths.
"""
import copy
from contextlib import redirect_stdout
from dataclasses import replace
import importlib.util
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bc2_draw_admission as policy
import bc2_draw_readiness_driver as adapter
import drive_bc2_holster_prepared as entry

spec = importlib.util.spec_from_file_location("held_swap_fixture", ROOT / "tests/test_bc2_draw_admission.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
core, transport = entry.core, entry.transport
NOW, MS = fixture.NOW, fixture.MS
WEAPONS = (fixture.SOURCE, fixture.TARGET)


def snapshot(now, nonce, selected=0, lifetime=1, shoulders=(2, 1), pose=None, grip=0):
    s = fixture.snapshot(now, nonce)
    h, d = s["bodyHolster"], s["bodyInventory"]
    d.update(nativeCount=2, nativeBindingGeneration=10 + lifetime)
    for index, row in enumerate(d["slots"]):
        row["native"]["slot"] = index
        row["assignment"]["slot"] = shoulders[index]
    row = d["slots"][selected]
    n, c = row["native"], row["configuration"]
    s["owner"].update(weapon=n["weapon"], equipGeneration=lifetime)
    s["equipment"] = dict(asset=c["asset"], weapon=n["weapon"], data=n["data"], persistence=n["persistence"])
    s["physical_equipment_generation"] = lifetime + 1
    s["selected"].update(configurationPath=c["configurationPath"],
        configurationPathPointer=c["configurationPathPointer"], soleConfiguredArray=c["soleConfiguredArray"],
        selectedSlot=n["slot"], matchedMeshPath="mesh-" + str(selected), meshes=[dict(path="mesh-" + str(selected))])
    for evidence in (s["input_evidence"], h["inputEvidence"], d["inputEvidence"]):
        evidence["owner"]["equipGeneration"] = lifetime + 1
    h.update(nativeOwner=copy.deepcopy(s["owner"]), physicalGun=dict(id=n["weapon"], generation=lifetime+1),
        selectedSlot=copy.deepcopy(row["assignment"]), inventoryEvaluation=0, neutralTrackedInput=not grip)
    h["rightClaim"]["token"].update(id=lifetime, owner=copy.deepcopy(h["inputEvidence"]["owner"]),
                                   item=copy.deepcopy(h["physicalGun"]))
    for inp in (s["input"], h["input"]):
        inp["hands"][1].update(held=grip, squeeze=grip)
        if pose is not None:
            inp["hands"][1].update(grip=copy.deepcopy(pose), aim=copy.deepcopy(pose))
    d.update(selectedOwner=copy.deepcopy(s["owner"]), physicalOwner=copy.deepcopy(h["inputEvidence"]["owner"]),
             physicalSelected=copy.deepcopy(h["selectedSlot"]["item"]))
    for index, row in enumerate(d["slots"]):
        row["configuration"].update(owner=copy.deepcopy(s["owner"]), selectedSlot=selected,
                                     kind="selected" if index == selected else "carried")
    return s


class Artifacts:
    directory = Path("synthetic-held-swap")

    def __init__(self):
        self.saved, self.events = {}, []

    def save(self, name, value, **kwargs):
        self.saved[name] = copy.deepcopy(value)

    def event(self, value):
        self.events.append(copy.deepcopy(value))


class Harness:
    def __init__(self, source=0, shoulders=(2, 1), fault=None):
        self.now, self.source, self.shoulders, self.fault = NOW, source, shoulders, fault
        self.driver, self.sent, self.observed, self.pending_counts = None, [], [], []
        self.requests = self.target_polls = 0
        self.budget = adapter.AdmissionBudget(policy, NOW-100*MS, NOW+20*policy.NS)
        self.budget.prime_attempt(NOW-50*MS)
        self.composed = adapter.make_driver(entry.HolsterDriver, core, budget=self.budget, clock=lambda: self.now)
        harness = self

        class OfflineDriver(self.composed):
            def __init__(self, args):
                harness.initialize(self, args)

            def read_state(self):
                return copy.deepcopy(self.state)

            def ownership(self, state):
                core.require(state["pid"] == 101 and state["runtime"] == "xr-sim"
                             and state["cmdSeq"] == self.sequence and state["lastCmd"] == self.sentinel,
                             "synthetic acknowledged host owner changed")

            def fresh(self):
                self.request_snapshot()
                for _ in range(4):
                    received = self.receive_snapshot()
                    if received is not None:
                        self.accept_snapshot(*received)
                        return received
                raise AssertionError("bounded synthetic receive did not complete")

            def close(self):
                harness.closed = True

        self.driver_class = OfflineDriver

    def initialize(self, d, args):
        self.driver = d
        d.args, d.artifacts = args, Artifacts()
        args.stow_slot = args.target_slot = None
        d.expected = dict(asset=args.target_asset, configuration=args.target_config, slot=None)
        d.phase, d.press_ns = "baseline", 0
        d.baseline = d.target_baseline = d.last_native = d.latest_snapshot = d.publication_source = None
        d.slot_binding = d.stow_ack = d.stow_cohort_binding = d.held_swap_ack = None
        d.holster_hold_started = d.holster_hold_anchor = d.holster_hold_state = d.holster_hold_command = None
        d.holster_replacements = 0
        d.snapshot_refresh_started = d.motion_started = d.reference = None
        d.pending_command = d.draw_armed = d.held_right = d.held_left = d.held_trigger = d.unsafe_owner = False
        d.held_buttons = set()
        d.sequence, d.commands, d.sentinel, d.command_hash = 42, 0, "owned", "synthetic-hash"
        d.last_write, d.last_commit = 0, None
        d.started, d.deadline = NOW-20*MS, NOW+9*policy.NS
        d.stage_deadline, d.hard_deadline = d.deadline, NOW+10*policy.NS
        d.right_pose = dict(position=[.25, 1.15, -.35], orientation=[0, 0, 0, 1])
        d.state = fixture.host()
        d.state.update(cmdSeq=d.sequence, lastCmd=d.sentinel)
        d.state["handR"].update(pos=copy.deepcopy(d.right_pose["position"]), ypr=[0, 0, 0], followsHead=False)
        d._readiness_ack = d.command_identity()
        d._readiness_ready = d._readiness_record = None
        self.budget.attach_draw(d.started, d.deadline)

    def request(self, d):
        self.now += 10*MS
        self.requests += 1
        d.last_request, d.last_request_published = self.now-5*MS, self.now-4*MS
        d.pending_nonce = self.requests

    def receive(self, d):
        selected, lifetime = self.source, 1
        if d.draw_armed:
            self.target_polls += 1
            if self.target_polls <= 2:
                self.pending_counts.append(len(self.sent))
            else:
                selected, lifetime = 1-self.source, 2
        s = snapshot(self.now, self.requests, selected, lifetime, self.shoulders, d.right_pose, int(d.held_right))
        if self.fault:
            self.fault(self, d, s)
        if s.get("valid") is False:
            core.require(d.defer_invalid_snapshot(s, self.now), "synthetic invalid response was not deferred")
            return None
        d.pending_nonce = None
        self.observed.append(copy.deepcopy(s))
        return s, b"synthetic full response", self.now

    def send(self, d, lines, expected=None, source_deadline=None, cleanup=False):
        d.validate_publication(lines, expected, cleanup)
        if source_deadline is not None:
            core.require(self.now < source_deadline, "original source expired")
        self.sent.append(dict(lines=lines[:], expected=copy.deepcopy(expected), cleanup=cleanup,
                              phase=d.phase, snapshot=copy.deepcopy(d.publication_source)))
        d.pending_batch = lines[:] + ["ack"]
        d.commands += len(lines)+1
        d.sequence += len(lines)+1
        d.last_write, d.last_commit = self.now+MS, self.now+2*MS
        self.now = d.last_commit
        d.sentinel, d.command_hash = "ack-" + str(d.sequence), "hash-" + str(d.sequence)
        d.state.update(cmdSeq=d.sequence, lastCmd=d.sentinel)
        if expected is not None:
            d.state["handR"].update(pos=copy.deepcopy(expected["pose"]["position"]),
                ypr=core.frame.quaternion_ypr(expected["pose"]["orientation"]), followsHead=False)
            d.state["controls"]["gripR"] = expected["grip"]
            d.check_target(d.state, expected)
        else:
            core.require(lines == ["grip r 0"] and cleanup, "synthetic cleanup exceeded owned release")
            d.state["controls"]["gripR"] = 0
        d.held_right = bool(d.state["controls"]["gripR"])

    def run(self, held_swap=True, observed_binding=None):
        source, target = WEAPONS[self.source], WEAPONS[1-self.source]
        argv = ["--run", "synthetic-unused", "--stow-asset", source[0], "--stow-config", source[1],
                "--target-asset", target[0], "--target-config", target[1]]
        if held_swap:
            argv.append("--held-swap")
        output = io.StringIO()
        with patch.object(entry, "HolsterDriver", self.driver_class), \
             patch.object(transport.Driver, "request_snapshot", lambda d: self.request(d)), \
             patch.object(transport.Driver, "receive_snapshot", lambda d: self.receive(d)), \
             patch.object(transport.Driver, "send", lambda d, *a, **kw: self.send(d, *a, **kw)), \
             patch.object(core.time, "perf_counter_ns", side_effect=lambda: self.now), redirect_stdout(output):
            code = entry.main(argv, observed_binding=observed_binding)
        return code, json.loads(output.getvalue())


class HeldSwapTests(unittest.TestCase):
    def test_full_prepared_entry_both_directions_and_fresh_physical_slots(self):
        for source, shoulders in ((0, (2, 1)), (1, (1, 2))):
            with self.subTest(source=WEAPONS[source][0], shoulders=shoulders):
                h = Harness(source, shoulders)
                code, result = h.run()
                self.assertEqual((code, result.get("error")), (0, None))
                self.assertEqual(result["status"], "observed_held_swap_and_expected_target_released")
                d, binding = h.driver, result["slotBinding"]
                self.assertEqual(binding["source"]["native"]["slot"], source)
                self.assertEqual(binding["target"]["native"]["slot"], 1-source)
                self.assertEqual(binding["target"]["assignment"]["slot"], shoulders[1-source])
                self.assertEqual(binding["target"]["asset"], WEAPONS[1-source][0])
                self.assertTrue(all(r["status"] == "Ready" for r in h.budget.decisions))
                self.assertTrue(h.budget.decisions)
                presses = [row for row in h.sent if row["phase"] == "draw_wait"]
                self.assertEqual(len(presses), 1)
                self.assertEqual(presses[0]["expected"]["grip"], 1)
                self.assertLess(core.frame.pose_error(core.frame.from_pose(presses[0]["expected"]["pose"]),
                    core.frame.from_pose(core.socket_pose(presses[0]["snapshot"], shoulders[1-source])))[0], .002)
                self.assertTrue(all(row["expected"]["grip"] == 0 for row in h.sent if row["phase"] == "held_reach"))
                withdrawal = [row for row in h.sent if row["phase"] == "withdraw"]
                self.assertGreater(len(withdrawal), 1)
                self.assertTrue(all(row["expected"]["grip"] == 1 for row in withdrawal[:-1]))
                self.assertEqual(withdrawal[-1]["expected"]["grip"], 0)
                self.assertEqual(h.pending_counts[0], h.pending_counts[1])
                self.assertTrue(all(s["bodyHolster"]["actualEmpty"] is False for s in h.observed))
                self.assertEqual(d.target_baseline["owner"]["equipGeneration"], 2)
                self.assertFalse(d.held_right or d.pending_command)
                self.assertEqual(d.state["controls"], fixture.host()["controls"])
                self.assertIsNone(d.stow_ack)
                self.assertNotIn("observed-empty-held.json", d.artifacts.saved)
                self.assertTrue(h.closed)
                self.assertFalse(result["nativeAcceptance"] or result["headsetAcceptance"])

    def test_prime_evaluation_zero_requires_explicit_mode_and_auto_true(self):
        for mode, automatic, wanted in ((True, True, policy.Status.READY),
                                       (False, True, policy.Status.REFUSE),
                                       (True, False, policy.Status.REFUSE)):
            s = snapshot(NOW, 1)
            s["bodyHolster"]["allowAutomaticGunHold"] = automatic
            context = replace(fixture.context(s, phase="prime"), held_swap=mode)
            budget = adapter.AdmissionBudget(policy, NOW-100*MS, NOW+20*policy.NS)
            budget.prime_attempt(NOW-10*MS)
            decision = budget.admit_prime(s, NOW, context,
                lambda value, now: core.resolve_slots(value, now, *WEAPONS[0], *WEAPONS[1]))
            self.assertEqual(decision.status, wanted)

    def test_default_does_not_admit_held_only_evaluation(self):
        h = Harness()
        code, result = h.run(held_swap=False)
        self.assertEqual(code, 2)
        self.assertFalse(result["heldSwap"])
        self.assertEqual(h.sent, [])

    def test_wrong_or_stale_or_unknown_source_never_commands(self):
        def change(path, value):
            def fault(h, d, s):
                obj = s
                for key in path[:-1]:
                    obj = obj[key]
                obj[path[-1]] = value
            return fault
        cases = (
            change(("bodyHolster", "allowAutomaticGunHold"), False),
            change(("bodyHolster", "inventoryEvaluation"), 2),
            change(("bodyHolster", "reloadBusy"), True),
            change(("bodyHolster", "cancel"), True),
            change(("bodyInventory", "deadlineNs"), NOW),
            change(("bodyInventory", "slots", 1, "configuration", "available"), False),
            change(("bodyInventory", "slots", 1, "configuration", "configurationPath"), "wrong/config"),
            change(("bodyHolster", "rightClaim", "deadlineNs"), NOW),
            change(("bodyInventory", "available"), False),
            change(("owner", "actorGeneration"), 9))
        for index, fault in enumerate(cases):
            with self.subTest(case=index):
                h = Harness(fault=fault)
                code, result = h.run()
                self.assertEqual(code, 2)
                self.assertEqual(result["status"], "refused")
                self.assertEqual(h.sent, [])

    def test_wrong_target_or_old_lifetime_releases_owned_grip(self):
        for invalid in ("configuration", "lifetime"):
            def fault(h, d, s):
                if h.target_polls > 2:
                    if invalid == "configuration":
                        s["selected"]["configurationPath"] = "wrong/target"
                    else:
                        s["owner"]["equipGeneration"] = 1
                        s["bodyHolster"]["nativeOwner"] = copy.deepcopy(s["owner"])
            h = Harness(fault=fault)
            code, result = h.run()
            self.assertEqual(code, 2)
            self.assertEqual(result["release"], "ordinary owned controller release acknowledged")
            self.assertEqual(h.sent[-1]["lines"], ["grip r 0"])
            self.assertTrue(h.sent[-1]["cleanup"])
            self.assertFalse(h.driver.held_right)
            self.assertFalse(any(row["phase"] == "withdraw" for row in h.sent))

    def test_pending_source_cannot_authorize_another_command(self):
        checked = []
        def fault(h, d, s):
            if h.target_polls == 1:
                with self.assertRaisesRegex(core.REFUSAL, "observation only"):
                    d.command(d.last_native, d.right_pose, 1)
                checked.append(True)
        h = Harness(fault=fault)
        code, result = h.run()
        self.assertEqual((code, result.get("error")), (0, None))
        self.assertEqual(checked, [True])

    def test_publication_gap_requires_exact_acknowledged_held_rearm(self):
        for tamper in (False, True):
            def fault(h, d, s):
                if h.target_polls == 1:
                    if tamper:
                        d.held_swap_ack["identity"] = ("foreign",)
                    s.clear()
                    s.update(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=False,
                        nonce=h.requests, observed_ns=h.now, invalid_reason="no_shot_publication",
                        bodyHolster=dict(available=False, reason="snapshot_unavailable"))
            h = Harness(fault=fault)
            code, result = h.run()
            if tamper:
                self.assertEqual(code, 2)
                self.assertIn("acknowledged held swap rearm", result["error"])
                self.assertEqual(h.sent[-1]["lines"], ["grip r 0"])
            else:
                self.assertEqual((code, result.get("error")), (0, None))
                self.assertEqual(h.budget.observations, 2)
                self.assertTrue(any(row["type"] == "holster_draw_publication_gap" for row in h.driver.artifacts.events))

    def test_prior_leg_binding_cannot_replace_fresh_opposite_leg_mapping(self):
        old = core.resolve_slots(snapshot(NOW, 1), NOW, *WEAPONS[0], *WEAPONS[1])
        h = Harness(source=1, shoulders=(1, 2))
        code, result = h.run(observed_binding=old)
        self.assertEqual(code, 2)
        self.assertEqual(h.sent, [])
        self.assertIn("binding changed", result["error"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
