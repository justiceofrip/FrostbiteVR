"""ROOT-RUN-ONLY ordinary XR pump and continuous shell pickup/insertion driver.

Use an already admitted live root runner: --run EXACT_RUN --stage PumpCycle,
or --stage ShellInsert --pickup-first. No process launch, native writes, firing,
capture, imported pickup receipts, or fabricated observations. Independent
all-three-native-path audit and headset acceptance remain separate.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


transport = load("pump_guarded_transport", "drive_bc2_feed_simulation.py")
planner = load("pump_actual_planner", "plan_bc2_pump_simulation.py")
require, integer, REFUSAL = transport.require, transport.integer, transport.REFUSAL
NS = transport.NS


class CapturePending(Exception):
    """A diagnostic was retained but was never admitted for commands."""


def source_deadline(snapshot):
    return min(integer(snapshot["valid_until_ns"]),
               integer(snapshot["input_evidence"]["observedNs"]) + transport.SNAPSHOT_PERIOD)


def ordinary_left(lines):
    require(isinstance(lines, list) and 1 <= len(lines) <= 5, "pump requires a bounded ordinary left batch")
    for line in lines:
        require(isinstance(line, str) and "\n" not in line and "\r" not in line, "invalid ordinary command")
        if line in ("hand l follow off", "hand l aimtrim 0 0", "grip l 0", "grip l 1"):
            continue
        require(line.startswith(("hand l grip pose ", "hand l aim pose ")), "pump permits ordinary left pose/grip only")
        values = [float(v) for v in line.split()[4:]]
        require(len(values) == 6 and all(math.isfinite(v) for v in values)
                and all(abs(v) <= 10 for v in values[:3]) and all(abs(v) <= 360 for v in values[3:]),
                "pump pose exceeds bounded controller range")


class PumpDriver(transport.Driver):
    def __init__(self, args):
        self.requested_stage, self.pickup_first = args.stage, args.pickup_first
        self.active_plan, self.demand_reference = None, None
        self.demand_started, self.demand_finished = None, False
        self.gate_receipts, self.release_authorized = [], False
        # Reuse all root session, process, command writer and source pin checks.
        args.stage, args.profiles, args.capture, args.menu_batches, args.pickup_first = None, None, None, None, False
        super().__init__(args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "session did not opt in to mechanism snapshots before native startup")
        self.artifacts.save("pump-invocation.json", dict(stage=self.requested_stage,
            pickupFirst=self.pickup_first, stepMs=args.step_ms, driverSha256=transport.digest(Path(__file__).read_bytes()),
            plannerSha256=transport.digest(Path(planner.__file__).read_bytes()),
            transportSha256=transport.digest(Path(transport.__file__).read_bytes()), nativeAcceptance=False))

    def admit_snapshot(self, snapshot, now, nonce):
        sp = snapshot.get("shellPump")
        if isinstance(sp, dict) and sp.get("available") is False:
            require(not self.demand_finished and self.active_plan is None and self.commands == 0
                    and self.demand_started is not None and time.perf_counter_ns() - self.demand_started < NS
                    and sp.get("reason") == "capture_pending_or_not_requested",
                    "shell/pump observation unavailable outside initial bounded capture demand")
            require(snapshot.get("feed") is None, "unrelated feed custody during pump capture demand")
            # Generic frame checks run first. The blocked-frame callback RAISES;
            # it never admits an unknown debt or manufactures a mechanism lease.
            def pending(original, evaluated):
                raise CapturePending("initial shell/pump capture pending")
            try:
                planner.frame.admit_frame(snapshot, now, nonce, mechanism_admission=pending)
            except CapturePending:
                pass
            if self.demand_reference is not None:
                self.stable_snapshot(self.demand_reference, snapshot)
            else:
                self.demand_reference = snapshot
            raise CapturePending("initial shell/pump capture pending")
        planner.admit(snapshot, now, nonce)
        planner.reserve(snapshot, now)

    def stable_snapshot(self, before, after):
        transport.stable_snapshot(before, after)
        require(planner.preserved(before) == planner.preserved(after), "pump head/right/basis changed; replan")
        if before.get("shellPump", {}).get("available") is True:
            require(planner.selection(before) == planner.selection(after), "exact pump selection/profile changed")

    def accept_snapshot(self, snapshot, raw, now):
        super().accept_snapshot(snapshot, raw, now)
        if self.demand_reference is not None:
            self.stable_snapshot(self.demand_reference, snapshot)
        if self.active_plan is not None:
            baseline, sp = self.active_plan["baseline"], snapshot["shellPump"]
            require(sp["shell"]["reserve"]["identity"] == baseline["nativeIdentity"], "native pump/shell identity changed")
            if self.active_plan["stage"] == "PumpCycle":
                native = sp["pump"]["native"]
                require(native is not None and native["identity"] == baseline["nativeIdentity"]
                        and all(native[k] == baseline["pumpLease"][k] for k in ("cycle", "shot")),
                        "native pump cycle/shot changed")
            elif self.active_plan["stage"] == "ShellInsert":
                require(sp["shell"]["cycle"] == baseline["shellCycle"], "native shell cycle changed")
                item = sp["shell"]["item"]
                if item is not None:
                    require(all(item[k] == baseline["shellItem"][k] for k in ("key", "units", "profile"))
                            and item["claim"]["token"] == baseline["shellItem"]["claim"]["token"],
                            "owned shell item/claim changed during insertion")

    def fresh_available(self):
        if self.demand_started is None:
            self.demand_started = time.perf_counter_ns()
        while True:
            previous_deadline = self.stage_deadline
            if not self.demand_finished:
                self.stage_deadline = min(previous_deadline, self.demand_started + NS)
            try:
                result = self.fresh()
                self.demand_finished = True
                return result
            except CapturePending as error:
                # The matching original diagnostic was already saved by base
                # receive_snapshot. No clock, input or claim is rewritten.
                self.pending_nonce = None
                self.artifacts.event(dict(type="pump_capture_pending", error=str(error),
                                          elapsedNs=time.perf_counter_ns()-self.demand_started))
                self.ownership(self.read_state())
                require(time.perf_counter_ns()-self.demand_started < NS, "initial pump capture demand exceeded one second")
            finally:
                self.stage_deadline = previous_deadline

    def current_source(self):
        if self.latest_snapshot is None:
            self.fresh_available()
        self.monitor = True
        # Consume the next current publication through the shared polling path.
        # fresh() deliberately drains THEN requests again for stage/gate starts;
        # doing that on every motion lease boundary would discard useful fresh
        # authority and spend an unnecessary second request period holding.
        for _ in range(800):
            self.pump()
            if self.latest_snapshot is not None and time.perf_counter_ns() < source_deadline(self.latest_snapshot):
                break
            time.sleep(.001)
        require(time.perf_counter_ns() < source_deadline(self.latest_snapshot), "original pump source expired")

    def validate_publication(self, lines, expected, cleanup):
        ordinary_left(lines)
        if cleanup:
            require(lines == ["grip l 0"], "pump cleanup permits only ordinary owned left release")
            return
        require(self.demand_finished and self.latest_snapshot is not None,
                "unadmitted capture demand cannot authorize commands")
        current, now = self.latest_snapshot, time.perf_counter_ns()
        require(now < source_deadline(current), "original pump source expired before publication")
        self.admit_snapshot(current, now, integer(current["nonce"]))
        if self.reference is not None:
            self.stable_snapshot(self.reference, current)
        if self.release_authorized:
            require(lines == ["grip l 0"] and self.gate_receipts
                    and self.gate_receipts[-1]["gate"] in ("pump_ready", "shell_inserted"),
                    "normal release requires actual Ready/support or InsertRound receipt")
            planner.check_gate(self.active_plan, current, self.gate_receipts[-1]["gate"], now, integer(current["nonce"]))
        else:
            require(expected is not None and self.active_plan is not None
                    and lines == expected["commands"] and expected in self.active_plan["schedule"],
                    "motion must be an exact admitted plan sample")
            if self.active_plan["stage"] == "ShellInsert":
                require(expected["grip"] == 1, "shell custody must remain continuously held")

    def send(self, lines, expected=None, capture=None, cleanup=False, source_deadline=None):
        ordinary_left(lines)
        require(capture is None, "pump driver does not request captures")
        if not cleanup:
            self.pause_until(max(self.last_write + 25_000_000, time.perf_counter_ns()))
            self.current_source()
            state = self.read_state()
            require({k: v for k, v in state["controls"].items() if k != "gripL"}
                    == {k: v for k, v in self.initial_sim["controls"].items() if k != "gripL"},
                    "unrelated simulator controls changed during pump/shell operation")
        return super().send(lines, expected=expected, cleanup=cleanup, source_deadline=source_deadline)

    def wait_gate(self, plan, gate, target):
        require(gate["stop"] is True and gate["requiresFreshSnapshot"] is True
                and gate["countsTowardGestureBudget"] is True and gate["requireContinuousFreshInput"] is True
                and gate["holdGrip"] == target["grip"] == 1, "invalid mandatory pump receipt gate")
        until = min(self.deadline, self.stage_deadline, time.perf_counter_ns() + integer(gate["timeoutNs"]))
        minimum_observed = self.last_commit
        last_error = "no fresh post-command snapshot"
        for _ in range(32):
            require(time.perf_counter_ns() < until, "actual receipt gate timed out: " + gate["id"] + ": " + last_error)
            # Freeze the committed endpoint and squeeze. xr-sim keeps producing
            # ordinary real XR packets; requests never publish pose commands.
            state = self.read_state()
            self.ownership(state)
            self.check_target(state, target)
            previous_deadline = self.stage_deadline
            self.stage_deadline = until
            try:
                snapshot, _, _ = self.fresh_available()
                now = time.perf_counter_ns()
                require(now < until, "actual receipt gate observation exceeded deadline")
                require(integer(snapshot["observed_ns"]) > minimum_observed,
                        "gate needs a real snapshot after endpoint command commit")
                require(now-integer(snapshot["input_evidence"]["observedNs"]) < integer(gate["maxInputGapNs"]),
                        "gate input lease exceeds native maximum input gap")
                try:
                    receipt = planner.check_gate(plan, snapshot, gate["id"], now, integer(snapshot["nonce"]))
                except planner.Refusal as error:
                    last_error = str(error)
                    self.artifacts.event(dict(type="pump_gate_pending", gate=gate["id"], nonce=snapshot["nonce"], error=last_error))
                    continue
                self.gate_receipts.append(receipt)
                self.artifacts.event(dict(type="pump_gate_observed", **receipt))
                return receipt
            finally:
                self.stage_deadline = previous_deadline
                self.monitor = True
        raise REFUSAL("actual receipt gate iteration bound: " + gate["id"] + ": " + last_error)

    def stage(self, name, pickup=None):
        snapshot, _, _ = self.fresh_available()
        evaluated = time.perf_counter_ns()
        step = self.args.step_ms * 1_000_000
        require(step >= 25_000_000, "shared transport cannot execute a period below 25 ms")
        plan = planner.plan(snapshot, name, evaluated, integer(snapshot["nonce"]), step, pickup_plan=pickup)
        require(time.perf_counter_ns() < min(integer(plan["sourceValidUntilNs"]), source_deadline(snapshot)),
                "planning consumed original pump source lease")
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        self.reference, self.active_plan = snapshot, plan
        self.release_authorized = False
        schedule, policy = plan["schedule"], plan["policy"]
        maximum = min(integer(policy["maximumGestureNs"]), 10*NS)
        require(0 < len(schedule) <= transport.MAX_COMMANDS and integer(plan["durationNs"]) < maximum,
                "pump plan exceeds bounded stage budget")
        require(self.commands + sum(len(t["commands"])+1 for t in schedule) + 2 <= transport.MAX_COMMANDS,
                "pump plan and ordinary release exceed remaining 512-command budget")
        gates = {integer(g["afterSample"]): g for g in plan["receiptGates"]}
        require(len(gates) == len(plan["receiptGates"]) and max(gates) == len(schedule)-1,
                "pump plan lacks unique receipt gates and final receipt")
        self.artifacts.save(f"pump-plan-{len(self.plans)+1:02}-{name}.json", plan)
        self.plans.append(dict(stage=name, nonce=snapshot["nonce"], samples=len(schedule),
                               nominalDurationNs=plan["durationNs"], stepNs=step))
        start = time.perf_counter_ns()
        self.motion_started, self.stage_deadline = start, min(self.deadline, start+maximum)
        self.monitor, last_at, previous_at = True, -1, 0
        for index, target in enumerate(schedule):
            at = integer(target["atNs"])
            require(last_at < at <= integer(plan["durationNs"]), "nonmonotonic pump trajectory")
            # Add every gate's actual wait to elapsed gesture time. Subsequent
            # sample dwell is never compressed to catch up with a nominal clock.
            due = start+at if index == 0 else max(start+at, self.last_commit + at-previous_at)
            self.pause_until(due)
            self.send(target["commands"], expected=target,
                      source_deadline=integer(plan["sourceValidUntilNs"]) if index == 0 else None)
            last_at, previous_at = at, at
            if index in gates:
                self.wait_gate(plan, gates[index], target)
                # Keep the following dwell measured from actual accepted gate.
                self.pause_until(time.perf_counter_ns() + (step if index+1 < len(schedule) else 0))
        if name != "ShellPickup":
            self.release_authorized = True
            self.send(["grip l 0"])
        self.plans[-1].update(actualDurationNs=time.perf_counter_ns()-start,
                              ordinaryLeftReleaseAcknowledged=name != "ShellPickup")
        self.motion_started, self.stage_deadline = None, self.deadline
        return plan


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--stage", choices=("PumpCycle", "ShellInsert"), required=True)
    parser.add_argument("--pickup-first", action="store_true", help="continuous actual pickup then fresh held ShellInsert")
    parser.add_argument("--step-ms", type=int, default=40, help="default 40; shared transport requires >=25 ms; native policy may be tighter")
    args = parser.parse_args(argv)
    driver, code = None, 2
    result = dict(schema="bc2vr.live-pump-driver.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, requiresIndependentAllThreePathAudit=True)
    try:
        require(25 <= args.step_ms <= 50, "unsupported command period: shared transport requires >=25 ms (default 40)")
        require(args.pickup_first == (args.stage == "ShellInsert"), "ShellInsert requires --pickup-first; PumpCycle forbids it")
        driver = PumpDriver.__new__(PumpDriver)
        driver.__init__(args)
        pickup = driver.stage("ShellPickup") if driver.pickup_first else None
        driver.stage(driver.requested_stage, pickup)
        result["status"], code = "actual_receipt_gates_observed_and_left_released", 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except Exception as cleanup_error:
                result["release"] = "refused: " + str(cleanup_error)
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update(durationNs=time.perf_counter_ns()-driver.started, commands=driver.commands,
                    plans=driver.plans, gateReceipts=driver.gate_receipts, leftGripHeld=driver.held_left,
                    pendingCommand=driver.pending_command, artifactDirectory=str(driver.artifacts.directory))
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"], code = str(error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
