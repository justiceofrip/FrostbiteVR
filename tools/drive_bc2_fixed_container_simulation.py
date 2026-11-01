"""ROOT-RUN-ONLY single-round fixed-container adapter for an admitted XR session.

--run EXACT_RUN --stage Reload performs the observed opening (if needed), actual
reserve pickup, continuous insertion, receipt, and closure. No launch, firing,
capture, native writes, imported custody or independently owned command channel.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


shared = load("fixed_shared_pump", "drive_bc2_pump_simulation.py")
transport = shared.transport
planner = load("fixed_live_planner", "plan_bc2_fixed_container_simulation.py")
require, integer, NS = transport.require, transport.integer, transport.NS


class FixedContainerDriver(shared.PumpDriver):
    """Reuse pump polling/sending and the ONE feed transport lock/owner/expiry."""
    def __init__(self, args):
        self.requested_stage = args.stage
        self.active_plan, self.demand_reference = None, None
        self.demand_started, self.demand_finished = None, False
        self.gate_receipts, self.release_authorized = [], False
        self.acquired_claim = None
        args.stage, args.profiles, args.capture, args.menu_batches, args.pickup_first = None, None, None, None, False
        transport.Driver.__init__(self, args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "session did not request mechanism snapshots before native startup")
        self.artifacts.save("fixed-container-invocation.json", dict(stage=self.requested_stage, stepMs=args.step_ms,
            driverSha256=transport.digest(Path(__file__).read_bytes()), plannerSha256=transport.digest(Path(planner.__file__).read_bytes()),
            sharedPumpSha256=transport.digest(Path(shared.__file__).read_bytes()), transportSha256=transport.digest(Path(transport.__file__).read_bytes()),
            nativeAcceptance=False, headsetAcceptance=False))

    def admit_snapshot(self, snapshot, now, nonce):
        x = snapshot.get("fixedContainer")
        if isinstance(x, dict) and x.get("available") is False:
            require(self.active_plan is None and self.commands == 0 and not self.demand_finished
                    and self.demand_started is not None and time.perf_counter_ns()-self.demand_started < NS
                    and x.get("reason") == "capture_pending_or_not_requested" and snapshot.get("feed") is None,
                    "fixed-container unavailable outside initial bounded capture demand")
            def pending(original, evaluated):
                raise shared.CapturePending("initial fixed-container capture pending")
            try:
                planner.frame.admit_frame(snapshot, now, nonce, mechanism_admission=pending)
            except shared.CapturePending:
                pass
            if self.demand_reference is None:
                self.demand_reference = snapshot
            else:
                self.stable_snapshot(self.demand_reference, snapshot)
            raise shared.CapturePending("initial fixed-container capture pending")
        return planner.admit(snapshot, now, nonce)

    def stable_snapshot(self, before, after):
        transport.stable_snapshot(before, after)
        planner.require_preserved(planner.preserved(before), planner.preserved(after))
        if before.get("fixedContainer", {}).get("available") is True:
            require(planner.selection(before) == planner.selection(after), "exact fixed-container profile changed")

    def snapshot_source_deadline(self, snapshot):
        deadlines = [super().snapshot_source_deadline(snapshot)]
        x = snapshot.get("fixedContainer")
        if isinstance(x, dict) and x.get("available") is True:
            # The same original leases already bound the fixed-container plan.
            # In particular, a current-at-publication family can expire before
            # delivery while the newer frame/input leases remain current.
            deadlines.extend(integer(value) for value in (
                snapshot["native_deadline_ns"], x["input"]["deadlineNs"],
                x["raw"]["input"]["deadlineNs"], x["view"]["snapshot"]["deadlineNs"],
                x["view"]["binding"]["deadlineNs"], x["tracking"]["gun"]["deadlineNs"]))
            if x.get("physicalFamily") is not None:
                deadlines.append(integer(x["physicalFamily"]["deadlineNs"]))
            if x["tracking"]["hand"] is not None:
                deadlines.append(integer(x["tracking"]["hand"]["deadlineNs"]))
        return min(deadlines)

    def current_source(self):
        if self.latest_snapshot is None:
            self.fresh_available()
        self.monitor = True
        # Hold the committed pose while the shared transport requests and
        # admits a replacement; retain its request/refresh/stage bounds.
        for _ in range(800):
            self.pump()
            if self.latest_snapshot is not None and time.perf_counter_ns() < self.snapshot_source_deadline(self.latest_snapshot):
                return
            time.sleep(.001)
        raise transport.REFUSAL("original fixed-container source expired")

    def accept_snapshot(self, snapshot, raw, now):
        transport.Driver.accept_snapshot(self, snapshot, raw, now)
        if self.demand_reference is not None:
            self.stable_snapshot(self.demand_reference, snapshot)
        if self.active_plan is not None:
            b, x = self.active_plan["baseline"], snapshot["fixedContainer"]
            require(x["tracking"]["lifetime"] == b["lifetime"] and x["supply"] == b["supply"]
                    and planner.binding_key(x["view"]["binding"]) == b["binding"]
                    and x["view"]["identity"] == b["nativeIdentity"], "retained native loader lifetime changed")
            pending = x["pendingRequest"]
            if pending is not None:
                require(self.active_plan["stage"] == "Insert"
                        and pending["preparedCommand"]["before"] == b["counts"]
                        and integer(pending["intent"]["id"]) > integer(b["lastCompletedRequest"]),
                        "unrelated pending transaction during fixed-container motion")

    def validate_publication(self, lines, expected, cleanup):
        shared.ordinary_left(lines)
        if cleanup:
            require(lines == ["grip l 0"], "cleanup permits only owned ordinary left release")
            return
        require(self.demand_finished and self.active_plan is not None and self.latest_snapshot is not None,
                "unadmitted observation cannot authorize input")
        current, now = self.latest_snapshot, time.perf_counter_ns()
        require(now < self.snapshot_source_deadline(current), "original source expired before publication")
        self.admit_snapshot(current, now, integer(current["nonce"]))
        planner.ammo(current, now, idle=True)
        self.stable_snapshot(self.reference, current)
        if self.release_authorized:
            require(lines == ["grip l 0"] and self.gate_receipts
                    and self.gate_receipts[-1]["gate"] == "fixed_step_complete", "ordinary release lacks actual physical completion")
            planner.check_gate(self.active_plan, current, "fixed_step_complete", now, integer(current["nonce"]))
        else:
            require(expected is not None and expected in self.active_plan["schedule"] and lines == expected["commands"],
                    "command differs from admitted fixed-container sample")
            if self.active_plan["stage"] == "Insert":
                planner.held(current, now, 5)
                require(current["fixedContainer"]["tracking"]["hand"]["token"] == self.active_plan["baseline"]["claim"],
                        "insertion lost original acquired custody")
                if expected["grip"] == 0:
                    require(self.gate_receipts and self.gate_receipts[-1]["gate"] == "fixed_aligned"
                            and self.gate_receipts[-1]["heldObservationSpanNs"] >= integer(self.active_plan["policy"]["endpointDwellNs"]),
                            "insertion release lacks actual continuously held endpoint dwell")
                    planner.check_gate(self.active_plan, current, "fixed_aligned", now, integer(current["nonce"]))
            elif expected["phase"] == "move":
                token = planner.held(current, now, 4)
                require(self.acquired_claim is not None and token == self.acquired_claim,
                        "mechanical motion lost the exact observed acquisition claim")

    def wait_gate(self, plan, gate, target):
        require(gate["stop"] is True and gate["requiresFreshSnapshot"] is True
                and gate["countsTowardGestureBudget"] is True and gate["requireContinuousFreshInput"] is True
                and gate["holdGrip"] == target["grip"], "invalid mandatory fixed-container gate")
        until = min(self.deadline, self.stage_deadline, time.perf_counter_ns()+integer(gate["timeoutNs"]))
        commit, first, last_error = self.last_commit, None, "no post-command observation"
        for _ in range(32):
            require(time.perf_counter_ns() < until, "fixed-container gate timed out: "+gate["id"]+": "+last_error)
            state = self.read_state()
            self.ownership(state)
            self.check_target(state, target)
            previous_deadline, self.stage_deadline = self.stage_deadline, until
            try:
                snapshot, _, _ = self.fresh_available()
                now = time.perf_counter_ns()
                require(now < until and integer(snapshot["observed_ns"]) > commit,
                        "gate must observe original native state after endpoint command commit")
                require(now-integer(snapshot["input_evidence"]["observedNs"]) < integer(gate["maxInputGapNs"]),
                        "gate input exceeds native maximum input gap")
                try:
                    receipt = planner.check_gate(plan, snapshot, gate["id"], now, integer(snapshot["nonce"]))
                    planner.require(integer(receipt["inputObservedNs"]) > commit, "gate raw input predates endpoint command commit")
                except planner.Refusal as error:
                    first, last_error = None, str(error)
                    self.artifacts.event(dict(type="fixed_gate_pending", gate=gate["id"], nonce=snapshot["nonce"], error=last_error))
                    continue
                observed = integer(receipt["inputObservedNs"])
                if first is None:
                    first = observed
                span = observed-first
                self.artifacts.event(dict(type="fixed_gate_sample", heldObservationSpanNs=span, **receipt))
                if span < integer(gate["minimumObservationSpanNs"]):
                    continue
                receipt["heldObservationSpanNs"] = span
                if gate["id"] == "fixed_acquired":
                    self.acquired_claim = receipt["claimToken"]
                self.gate_receipts.append(receipt)
                return receipt
            finally:
                self.stage_deadline, self.monitor = previous_deadline, True
        raise transport.REFUSAL("fixed-container gate iteration bound: "+gate["id"]+": "+last_error)

    def stage(self, name, pickup=None, initial_snapshot=None):
        if initial_snapshot is None:
            snapshot, _, _ = self.fresh_available()
        else:
            # Reload already obtained an admitted source to choose its first
            # stage. Plan from that exact source while its original leases are
            # current; requesting again can discard readiness before any input.
            require(self.active_plan is None and self.commands == 0 and self.demand_finished
                    and not self.held_left and not self.pending_command
                    and initial_snapshot is self.latest_snapshot,
                    "initial stage source is not the untouched admitted readiness observation")
            snapshot = initial_snapshot
        plan = planner.plan(snapshot, name, time.perf_counter_ns(), integer(snapshot["nonce"]), self.args.step_ms*1_000_000, pickup)
        require(time.perf_counter_ns() < min(integer(plan["sourceValidUntilNs"]), self.snapshot_source_deadline(snapshot)),
                "planning consumed original source lease")
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        self.reference, self.active_plan, self.release_authorized = snapshot, plan, False
        self.acquired_claim = None
        schedule = plan["schedule"]
        maximum = integer(plan["policy"]["maximumGestureNs"])
        require(0 < integer(plan["durationNs"]) < maximum <= 10*NS
                and self.commands+sum(len(row["commands"])+1 for row in schedule)+2 <= transport.MAX_COMMANDS,
                "fixed-container plan and ordinary release exceed shared budget")
        gates = {integer(g["afterSample"]): g for g in plan["receiptGates"]}
        require(len(gates) == len(plan["receiptGates"]) and max(gates) == len(schedule)-1,
                "fixed-container schedule lacks unique final receipt gate")
        self.artifacts.save(f"fixed-container-plan-{len(self.plans)+1:02}-{name}.json", plan)
        self.plans.append(dict(stage=name, nonce=snapshot["nonce"], samples=len(schedule), nominalDurationNs=plan["durationNs"]))
        start = time.perf_counter_ns()
        self.motion_started, self.stage_deadline, self.monitor = start, min(self.deadline, start+maximum), True
        previous_at = -1
        for index, target in enumerate(schedule):
            at = integer(target["atNs"])
            require(previous_at < at <= integer(plan["durationNs"]), "nonmonotonic fixed-container schedule")
            due = start+at if index == 0 else max(start+at, self.last_commit+at-previous_at)
            self.pause_until(due)
            self.send(target["commands"], expected=target,
                      source_deadline=integer(plan["sourceValidUntilNs"]) if index == 0 else None)
            previous_at = at
            if index in gates:
                self.wait_gate(plan, gates[index], target)
        if name in ("Open", "Close"):
            self.release_authorized = True
            self.send(["grip l 0"])
        self.plans[-1].update(actualDurationNs=time.perf_counter_ns()-start, ordinaryLeftReleaseAcknowledged=name != "Pickup")
        self.motion_started, self.stage_deadline = None, self.deadline
        return plan

    def reload(self):
        snapshot, _, _ = self.fresh_available()
        step = integer(snapshot["fixedContainer"]["tracking"]["step"])
        require(step in (0, 2), "reload starts only from actual unopened/insert-ready empty loader")
        if step == 0:
            self.stage("Open", initial_snapshot=snapshot)
        pickup = self.stage("Pickup", initial_snapshot=snapshot if step == 2 else None)
        self.stage("Insert", pickup)
        if integer(self.latest_snapshot["fixedContainer"]["profile"]["kind"]) == 3:
            self.stage("Close")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--stage", choices=("Reload", "Open", "Insert", "Close"), required=True)
    parser.add_argument("--step-ms", type=int, default=40)
    args = parser.parse_args(argv)
    driver, code = None, 2
    result = dict(schema="bc2vr.live-fixed-container-driver.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, requiresIndependentAllThreePathAudit=True)
    try:
        require(25 <= args.step_ms <= 50, "shared transport command period must be 25-50 ms")
        driver = FixedContainerDriver.__new__(FixedContainerDriver)
        driver.__init__(args)
        if driver.requested_stage == "Reload":
            driver.reload()
        else:
            pickup = driver.stage("Pickup") if driver.requested_stage == "Insert" else None
            driver.stage(driver.requested_stage, pickup)
        result["status"], code = "actual_receipt_gates_observed_and_left_released", 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
    finally:
        if driver is not None:
            # This shared cleanup runs for every exit, including successful runs.
            # It settles only this writer's pending batch and ordinary owned grip.
            if getattr(driver, "artifacts", None):
                try:
                    result["release"] = driver.release_after_failure()
                except Exception as error:
                    result["release"], code = "refused: "+str(error), 2
                if driver.held_left or driver.pending_command:
                    result["status"], code = "refused_unsettled_release", 2
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
