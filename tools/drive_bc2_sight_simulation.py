"""ROOT-RUN-ONLY bounded ordinary left-controller sight driver (Windows).

Use --run EXACT_LIVE_RUN --target primary|secondary. The existing root runner
must own the process, simulator command channel and startup snapshot opt-in.
Exact observed family selection gates ordinary release. Native sight request,
commit and action acknowledgement still require separate native trace review.
No process launch, capture, right input, native writes or fabricated registration.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True


def load_sibling(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


transport = load_sibling("bc2_sight_guarded_transport", "drive_bc2_feed_simulation.py")
sight = load_sibling("bc2_sight_live_planner", "plan_bc2_sight_simulation.py")
require, integer, REFUSAL = transport.require, transport.integer, transport.REFUSAL
NS = transport.NS


class SelectionObserved(Exception):
    """Stop publishing old-configuration poses once the exact target is selected."""


class SourceExpired(REFUSAL):
    """Retry admission only while the base transport has not published a command."""


def source_deadline(snapshot):
    return min(integer(snapshot["valid_until_ns"]),
               integer(snapshot["input_evidence"]["observedNs"]) + transport.SNAPSHOT_PERIOD)


def ordinary_left(lines):
    require(isinstance(lines, list) and 1 <= len(lines) <= 4, "sight requires a bounded ordinary left batch")
    for line in lines:
        require(isinstance(line, str), "sight command must be text")
        if line in ("hand l follow off", "hand l aimtrim 0 0", "grip l 0", "grip l 1"):
            continue
        require(line.startswith("hand l grip pose "), "sight permits ordinary left pose/grip only")
        numbers = line.split()[4:]
        require(len(numbers) == 6, "sight pose requires six numbers")
        values = [float(v) for v in numbers]
        require(all(math.isfinite(v) for v in values)
                and all(abs(v) <= 10 for v in values[:3])
                and all(abs(v) <= 360 for v in values[3:]), "sight pose exceeds bounded controller range")


def selection_state(before, after, expected):
    """Validate a same-family source or exact native-selected target observation.

    This does not infer request IDs, action dispatch, sight commit or native ack.
    Target equipment/rig may change, but actor, physical family and space may not.
    """
    old_family = before["sight"]["family"]
    family = after.get("sight", {}).get("family")
    require(isinstance(family, dict), "live selected sight family disappeared")
    primary_action = integer(family.get("primaryAction", 0))
    require(primary_action in (25, 36)
            and primary_action == integer(old_family.get("primaryAction", 0))
            and primary_action == integer(expected.get("primaryAction", 0)), "native sight primary route changed or missing")
    for key in ("physicalItem", "inventory", "persistentId", "primaryAsset", "primaryConfiguration",
                "secondaryAsset", "secondaryConfiguration"):
        require(family[key] == old_family[key], "native sight family changed: " + key)
    require(integer(family["observedMs"]) >= integer(old_family["observedMs"]), "native sight family observation regressed")
    require(isinstance(family["selectedSecondary"], bool), "native selected sight mode is not boolean")
    mode = "secondary" if family["selectedSecondary"] else "primary"
    eq, selected = after["equipment"], after["selected"]
    require(integer(eq["persistence"]) == integer(expected["physicalItem"])
            and integer(selected["inventory"]) == integer(expected["inventory"]), "selected physical item/inventory changed")
    require(eq["asset"] == family[mode + "Asset"]
            and selected["configurationPath"] == family[mode + "Configuration"], "selected configuration is outside observed sight family")
    for key in ("player", "soldier", "weak", "actorGeneration", "space"):
        require(before["owner"][key] == after["owner"][key], "sight actor/space changed: " + key)
    require(before["physical_equipment_generation"] == after["physical_equipment_generation"],
            "sight physical equipment generation changed")
    for key in ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative", "referenceHead", "head"):
        require(before["input"][key] == after["input"][key], "sight tracking/head reference changed: " + key)
    require(before["input"]["hands"][1] == after["input"]["hands"][1], "sight right controller input changed")
    require(all(float(h["trigger"]) == 0 for h in after["input"]["hands"]), "sight trigger input is no longer neutral")
    if mode == expected["sourceMode"]:
        transport.stable_snapshot(before, after)
        require(all(family[k] == expected[k] for k in ("targetWeapon", "targetSlot", "targetAction")),
                "native opposite sight target changed")
        return "source"
    require(mode == expected["targetMode"]
            and integer(after["owner"]["weapon"]) == integer(eq["weapon"]) == integer(expected["targetWeapon"])
            and integer(selected["selectedSlot"]) == integer(expected["targetSlot"])
            and eq["asset"] == expected["asset"]
            and selected["configurationPath"] == expected["configurationPath"], "native selected target does not match exact planned sight transition")
    # After selection, the live family describes the inverse native target.
    require(integer(family["targetWeapon"]) == integer(expected["sourceWeapon"])
            and integer(family["targetSlot"]) == integer(before["selected"]["selectedSlot"])
            and integer(family["targetAction"]) == (primary_action if mode == "secondary" else 33),
            "native selected family inverse target does not match original source")
    return "target"


class SightDriver(transport.Driver):
    def __init__(self, args):
        self.sight_plan = None
        self.transition_observation = None
        self.release_authorized = False
        # Feed transport setup remains identical; sight owns its explicit opt-in
        # below and does not invent a feed profile or enable snapshots at runtime.
        args.stage, args.profiles, args.capture, args.menu_batches, args.pickup_first = None, None, None, None, False
        super().__init__(args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "session did not opt in to mechanism snapshots before native startup")
        self.artifacts.save("sight-invocation.json", dict(target=args.target, stepMs=args.step_ms,
                            driverSha256=transport.digest(Path(__file__).read_bytes()),
                            plannerSha256=transport.digest(Path(sight.__file__).read_bytes()),
                            transportSha256=transport.digest(Path(transport.__file__).read_bytes()),
                            requestAcknowledgementAvailable=False, nativeAcceptance=False))

    def admit_snapshot(self, snapshot, now, nonce):
        sight.frame.admit_frame(snapshot, now, nonce)
        require(all(float(h["trigger"]) == 0 for h in snapshot["input"]["hands"]),
                "release both triggers before sight manipulation")

    def stable_snapshot(self, before, after):
        if self.sight_plan is None:
            return transport.stable_snapshot(before, after)
        state = selection_state(before, after, self.sight_plan["expectedTransition"])
        require(self.transition_observation is None or state == "target", "native selection reverted after observed sight transition")

    def accept_snapshot(self, snapshot, raw, now):
        super().accept_snapshot(snapshot, raw, now)
        if self.reference is None or self.sight_plan is None:
            return
        if selection_state(self.reference, snapshot, self.sight_plan["expectedTransition"]) == "target":
            if self.transition_observation is None:
                self.transition_observation = dict(nonce=snapshot["nonce"], observedNs=snapshot["observed_ns"],
                    evaluatedNs=now, owner=snapshot["owner"], selected=snapshot["selected"],
                    family=snapshot["sight"]["family"],
                    expectedTargetAction=self.sight_plan["expectedTransition"]["targetAction"],
                    requestAcknowledgementAvailable=False, nativeAcceptance=False)
                self.artifacts.save("selected-transition.json", self.transition_observation)

    def current_source(self):
        if self.latest_snapshot is None:
            self.fresh()
        self.monitor = True
        # As in pump motion, consume the pending publication through the shared
        # polling path. fresh() drains it and requests again for stage starts;
        # repeating that on a motion lease boundary wastes current authority.
        for _ in range(800):
            self.pump()
            if self.latest_snapshot is not None and time.perf_counter_ns() < source_deadline(self.latest_snapshot):
                break
            time.sleep(.001)
        require(time.perf_counter_ns() < source_deadline(self.latest_snapshot), "current sight snapshot lease expired")

    def validate_publication(self, lines, expected, cleanup):
        ordinary_left(lines)
        if cleanup:
            require(lines == ["grip l 0"], "sight cleanup permits only ordinary owned left release")
            return
        if self.latest_snapshot is None or time.perf_counter_ns() >= source_deadline(self.latest_snapshot):
            raise SourceExpired("original sight source lease expired before command publication")
        if self.transition_observation is not None and not self.release_authorized:
            raise SelectionObserved()
        if self.release_authorized:
            require(lines == ["grip l 0"] and self.transition_observation is not None,
                    "normal sight release requires exact observed native selected family")
        if expected and expected.get("phase") == "release":
            require(self.release_authorized, "planned sight release lacks observed-family acknowledgement")
        if expected and expected.get("requiresFreshSightPreference"):
            current = self.latest_snapshot
            admitted = sight.admit(current, time.perf_counter_ns(), integer(current["nonce"]), self.args.target)
            require(admitted["preferredByDistance"] is True, "fresh sight contact does not beat support before acquisition")

    def send(self, lines, expected=None, capture=None, cleanup=False, source_deadline=None):
        ordinary_left(lines)
        require(capture is None, "sight driver does not request captures")
        if cleanup:
            return super().send(lines, expected=expected, cleanup=True, source_deadline=source_deadline)
        for _ in range(800):
            self.current_source()
            state = self.read_state()
            require({k: v for k, v in state["controls"].items() if k != "gripL"}
                    == {k: v for k, v in self.initial_sim["controls"].items() if k != "gripL"},
                    "unrelated simulator controls changed during sight operation")
            try:
                return super().send(lines, expected=expected, source_deadline=source_deadline)
            except SourceExpired:
                # The base wait/state reads can consume the admitted lease.
                # Nothing was published: obtain current native evidence, then
                # repeat every ownership check. Keep any original plan deadline.
                continue
        raise REFUSAL("sight publication refresh iteration bound")

    def await_transition(self, maximum):
        until = min(self.stage_deadline, self.deadline, time.perf_counter_ns() + integer(maximum))
        for _ in range(2000):
            require(time.perf_counter_ns() < until, "exact selected sight-family transition was not observed within bounded wait")
            self.pump()
            if self.transition_observation is not None:
                self.current_source()
                return
            time.sleep(.001)
        raise REFUSAL("sight selected-family observation iteration bound")

    def stage(self):
        snapshot, raw, _ = self.fresh()
        evaluated = time.perf_counter_ns()
        plan = sight.plan(snapshot, evaluated, integer(snapshot["nonce"]), self.args.step_ms * 1_000_000, self.args.target)
        require(time.perf_counter_ns() < source_deadline(snapshot), "planning consumed original sight source lease")
        self.sight_plan, self.reference, self.latest_snapshot = plan, snapshot, snapshot
        self.artifacts.save("sight-plan.json", plan)
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}.json", raw)
        schedule, policy = plan["schedule"], plan["policy"]
        require(0 < len(schedule) <= transport.MAX_COMMANDS
                and self.commands + sum(len(row["commands"]) + 1 for row in schedule) + 2 <= transport.MAX_COMMANDS,
                "sight plan and ordinary release exceed bounded command budget")
        require(schedule[-1].get("requiresObservedFamilyTransition") is True
                and schedule[-1]["phase"] == "release", "sight plan lacks conditional observed-family release")
        require(integer(plan["durationNs"]) < integer(policy["gestureTimeoutNs"]), "sight plan exceeds gesture budget")
        self.plans.append(dict(stage="SightFlip", target=self.args.target, nonce=snapshot["nonce"],
                               samples=len(schedule), nominalDurationNs=plan["durationNs"], nativeAcceptance=False))
        start = time.perf_counter_ns()
        self.motion_started, self.stage_deadline = start, min(self.deadline, start + integer(policy["gestureTimeoutNs"]))
        self.monitor = True
        last_at = -1
        for target in schedule:
            at = integer(target["atNs"])
            require(last_at < at <= integer(plan["durationNs"]), "nonmonotonic sight trajectory")
            last_at = at
            if target["phase"] == "release":
                break
            if self.transition_observation is not None:
                break
            due = max(start + at, self.last_write + self.args.step_ms * 1_000_000)
            self.pause_until(due)
            if target.get("requiresFreshSightPreference"):
                self.fresh()
                self.monitor = True
            try:
                self.send(target["commands"], expected=target,
                          source_deadline=source_deadline(snapshot) if target is schedule[0] else None)
            except SelectionObserved:
                break
            if target["phase"] == "await_acknowledgement":
                self.await_transition(target["maximumWaitNs"])
                break
        self.await_transition(policy["ackTimeoutNs"])
        self.release_authorized = True
        # Selection may change the weapon mapping. Ordinary grip release uses no
        # stale primary/secondary pose and performs no native operation itself.
        self.send(["grip l 0"])
        self.plans[-1].update(actualDurationNs=time.perf_counter_ns() - start,
                              selectedTransitionObserved=True, requestAcknowledgementAvailable=False,
                              ordinaryLeftReleaseAcknowledged=True)
        self.motion_started, self.stage_deadline = None, self.deadline


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="exact live root runner output directory")
    parser.add_argument("--target", choices=("primary", "secondary"), required=True)
    parser.add_argument("--step-ms", type=int, default=20)
    args = parser.parse_args(argv)
    driver = None
    result = dict(schema="bc2vr.live-sight-driver.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, requestAcknowledgementAvailable=False,
                  evidence="ordinary simulator commands; exact selected family observed separately from native request/commit proof")
    code = 2
    try:
        require(17 <= args.step_ms <= 125, "invalid sight command period")
        driver = SightDriver.__new__(SightDriver)
        driver.__init__(args)
        driver.stage()
        result["status"] = "selected_transition_observed_and_left_released"
        code = 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except Exception as release_error:
                result["release"] = "refused: " + str(release_error)
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update(durationNs=time.perf_counter_ns() - driver.started,
                    commands=driver.commands, plans=driver.plans, leftGripHeld=driver.held_left,
                    pendingCommand=driver.pending_command, selectedTransitionObserved=driver.transition_observation is not None,
                    captureSequence=getattr(driver, "capture_sequence", None), artifactDirectory=str(driver.artifacts.directory))
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"], code = str(error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
