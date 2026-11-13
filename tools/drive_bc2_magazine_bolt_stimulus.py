"""ROOT-only ordinary magazine replacement and bolt continuation.

Uses the installed normal observer and canonical session/command/ACK transport.
Modes compose across settled owner records: prepare-hold; prep-shot-bolt; magazine; shot-bolt; bolt.
No process launch, fixture, ammo write, resource grant or acceptance verdict.
"""
import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("magbolt_geometry", Path(__file__).with_name("bc2_magazine_bolt_stimulus.py"))
geometry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)
ordinary = geometry.shared("drive_bc2_reload_stimulus")
transport = ordinary.transport
frame = geometry.frame
require, integer, Refusal = geometry.require, geometry.integer, geometry.Refusal
NS = geometry.NS


class Artifacts(ordinary.DeferredArtifacts):
    def save(self, name, value, final=False):
        if name.startswith("snapshot-") and name.endswith(".json") and isinstance(value, bytes):
            return self.snapshot(name+".gz", value)
        return super().save(name, value, final)


def controls_allowed(controls):
    neutral = copy.deepcopy(controls)
    for k in ("gripL", "gripR", "trigR"):
        value = neutral[k]
        require(isinstance(value, (float, int)) and not isinstance(value, bool)
                and value in (0, 1), "unexpected partially active ordinary control")
        neutral[k] = 0
    require(ordinary.neutral_controls(neutral), "unowned button/stick/left trigger is active")


class MagazineBoltDriver(transport.Driver):
    def __init__(self, args):
        # Canonical constructor pins host/runtime/session, takes the same OS
        # writer lock, and admits only a settled previous command-owner record.
        args.stage, args.pickup_first, args.capture, args.menu_batches = None, False, None, None
        super().__init__(args)
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "normal session did not enable immutable mechanism snapshots at startup")
        self.prior_owner, _ = transport.read_json(self.run / "live-driver-owner.json", 8192) if (
            self.run / "live-driver-owner.json").exists() else (None, None)
        self.artifacts = Artifacts(self.artifacts)
        self.family = "magazine" if args.mode == "magazine" else "bolt"
        self.anchor = None
        self.preserved = None
        self.configured_hands = set()
        self.boundaries = []
        self.debt = None
        self.gun_root = None
        self.preparation = None
        self.preparation_mode = args.mode == "prep-shot-bolt"
        self.motion_reference = None
        self.handoff_guard = None
        self.handoff_phase = None
        self.neutral_handoff_completed = False
        self.bolt_cycle_completed = False
        self.post_magazine_capture = None
        self.bootstrap_phase = None
        self.bootstrap_source = None
        self.bootstrap_incomplete = False
        self.bootstrap_attempted = False
        self.hold_prepared = False
        self.geometry_evidence = None
        controls_allowed(self.state["controls"])
        require(self.state["controls"]["trigR"] == 0, "trigger must be released at invocation")
        # Settled writer ownership authorizes continuation of actual controls;
        # these values never manufacture a native GunHold or held ammunition.
        self.held_left = bool(self.state["controls"]["gripL"])
        self.held_right = bool(self.state["controls"]["gripR"])
        self.monitor = False
        self.artifacts.save("adapter.json", dict(schema="bc2vr.magazine-bolt-adapter.v1",
            mode=args.mode, ordinaryInputOnly=True, nativeAcceptance=False, headsetAcceptance=False,
            maximumDurationNs=10*NS, maximumCommands=transport.MAX_COMMANDS,
            driverSha256=transport.digest(Path(__file__).read_bytes()),
            geometrySha256=transport.digest(Path(geometry.__file__).read_bytes())))

    def read_state(self):
        state = super().read_state()
        if hasattr(self, "initial_sim"):
            require(state["head"] == self.initial_sim["head"], "head changed outside the ordinary adapter")
            controls_allowed(state["controls"])
        return state

    def save_receive_timing_failure(self):
        # Base close normally writes a final artifact. This adapter defers all
        # serialization until input/ownership has ended, including failures.
        trace = getattr(self, "_receive_timing", None)
        if trace is not None and trace.frozen:
            self._receive_timing = None
            self.artifacts.save("receive-timing-failure.json", trace.report())

    def admit_snapshot(self, snapshot, now, nonce):
        if getattr(self, "bootstrap_phase", None) is not None:
            require(integer(snapshot["nonce"]) == nonce, "held entry snapshot nonce differs")
            geometry.held_entry(snapshot, now,
                self.bootstrap_source if self.bootstrap_phase == "claim" else None,
                self.last_write if self.bootstrap_phase == "claim" else 0)
        else:
            geometry.admit(snapshot, now, nonce, self.family)
        current = geometry.owner_key(snapshot)
        if self.anchor is not None:
            require(current == self.anchor, "exact selected native/rig owner changed")
            inp = snapshot["input"]
            require(all(inp[k] == self.preserved[k] for k in
                        ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative"))
                    and all(frame.preserved_pose_equal(inp[k], self.preserved[k]) for k in ("head", "referenceHead")),
                    "tracking/body reference changed during ordinary gesture")
        return True

    def snapshot_source_deadline(self, snapshot):
        deadline = min(super().snapshot_source_deadline(snapshot), geometry.source_deadline(snapshot, self.family))
        if getattr(self, "bootstrap_phase", None) is not None:
            deadline = min(deadline, geometry.held_entry_deadline(snapshot))
        if getattr(self, "preparation_mode", False):
            deadline = min(deadline, geometry.source_deadline(snapshot, "magazine"))
        return deadline

    def validate_publication(self, lines, expected, cleanup):
        require(expected is not None, "ordinary command requires a typed target")
        require(lines == expected["commands"], "command text differs from typed ordinary target")
        state = self.read_state()
        self.ownership(state)
        require(all(state[k] == self.state[k] for k in ("head", "handL", "handR", "controls")),
                "settled ordinary inputs changed outside this writer")
        if not cleanup:
            require(self.latest_snapshot is not None, "ordinary input requires a current mapped boundary")
            now = time.perf_counter_ns()
            self.admit_snapshot(self.latest_snapshot, now, integer(self.latest_snapshot["nonce"]))
            require(now < self.snapshot_source_deadline(self.latest_snapshot),
                    "original source/claim lease expired before ordinary publication")
            if getattr(self, "bootstrap_phase", None) is not None:
                require(self.bootstrap_phase == "press" and lines == ["grip r 1"]
                        and ordinary.neutral_controls(state["controls"]),
                        "held entry permits only one ordinary right grip from neutral input")
            if expected.get("sample") is not None and self.motion_reference is not None:
                geometry.movement_continuation(self.motion_reference, self.latest_snapshot,
                                               now, self.family, self.debt)
            if getattr(self, "handoff_phase", None) is not None:
                control = "grip l 0" if self.handoff_phase == "support" else "grip r 0"
                require(lines == [control], "neutral handoff permits only its owned release")
                self.handoff_snapshot(self.latest_snapshot, now, self.handoff_phase)
            if expected.get("capture") is not None:
                require(lines == ["shot " + expected["capture"]]
                        and all(expected[k] == state[k] for k in ("head", "handL", "handR", "controls")),
                        "magazine capture must preserve the exact settled input")
                self.admit_magazine_capture(self.latest_snapshot, now)
        if expected.get("fire"):
            if getattr(self, "preparation_mode", False):
                current = geometry.preparation_baseline(self.latest_snapshot, time.perf_counter_ns())
                require(all(current[k] == self.preparation[k] for k in current
                            if k not in ("startedNs", "sequence")),
                        "preparation full-magazine/owner/shot baseline changed before publication")
            geometry.fire_clearance(self.fire_permission, self.latest_snapshot, state, self.host,
                                    time.perf_counter_ns(), self.deadline)

    @staticmethod
    def check_target(state, target):
        if "head" in target:
            require(state["head"] == target["head"], "ordinary command moved the head")
        if "sample" in target:
            s = target["sample"]
            h, grip = ("handL", "gripL") if s["hand"] == 0 else ("handR", "gripR")
            require(state["handR" if s["hand"] == 0 else "handL"] == target["otherHand"],
                    "other hand moved during one-hand ordinary input")
            require(state[h]["followsHead"] is False and math.dist(state[h]["pos"], s["positionMeters"]) <= .0002,
                    "controller position/follow configuration did not acknowledge")
            actual = frame.from_pose(dict(position=[0, 0, 0], orientation=frame.ypr_quaternion(state[h]["ypr"])))
            intended = frame.from_pose(dict(position=[0, 0, 0], orientation=frame.ypr_quaternion(s["yawPitchRollDegrees"])))
            require(frame.pose_error(actual, intended)[1] <= .001 and state["controls"][grip] == s["grip"],
                    "controller orientation/grip did not acknowledge")
        else:
            require(all(state[k] == target[k] for k in ("handL", "handR")), "control-only command moved a hand")
        require(state["controls"] == target["controls"], "ordinary target controls did not acknowledge exactly")

    def command(self, *, sample=None, control=None, value=None, source=None, cleanup=False, fire=False):
        require((sample is None) != (control is None), "one typed ordinary command action required")
        target = dict(controls=copy.deepcopy(self.state["controls"]), head=copy.deepcopy(self.state["head"]), fire=fire)
        if sample is not None:
            hand = integer(sample["hand"])
            require(hand in (0, 1) and sample["grip"] in (0, 1), "invalid hand sample")
            side = "l" if hand == 0 else "r"
            values = sample["positionMeters"] + sample["yawPitchRollDegrees"]
            require(len(values) == 6 and all(math.isfinite(frame.finite(v)) for v in values)
                    and all(abs(v) <= 10 for v in values[:3]) and all(abs(v) <= 360 for v in values[3:]),
                    "unbounded controller sample")
            numbers = " ".join(f"{v:.9f}" for v in values)
            lines = [f"hand {side} grip pose {numbers}", f"hand {side} aim pose {numbers}", f"grip {side} {sample['grip']}"]
            if hand not in self.configured_hands:
                current = self.latest_snapshot["input"]["hands"][hand]
                require(frame.pose_error(frame.from_pose(current["grip"]), frame.from_pose(current["aim"]))[1] <= .001,
                        "first hand setup requires already neutral aim trim")
                lines = [f"hand {side} follow off", f"hand {side} aimtrim 0 0"] + lines
            target.update(sample=sample, otherHand=copy.deepcopy(self.state["handR" if hand == 0 else "handL"]))
            target["controls"]["gripL" if hand == 0 else "gripR"] = sample["grip"]
        else:
            require(control in ("grip l", "grip r", "trigger r") and value in (0, 1), "unsupported ordinary control")
            lines = [f"{control} {value}"]
            target.update({k: copy.deepcopy(self.state[k]) for k in ("handL", "handR")})
            target["controls"][{"grip l": "gripL", "grip r": "gripR", "trigger r": "trigR"}[control]] = value
        target["commands"] = lines
        # Keep the canonical command publisher, exact text ACK, original write
        # spacing, writer identity and 512-command/10-second/250ms ACK limits.
        self.motion_started = self.motion_started or time.perf_counter_ns()
        self.send(lines, expected=target, cleanup=cleanup, source_deadline=source)
        if sample is not None:
            self.configured_hands.add(sample["hand"])

    def boundary(self, name, predicate, timeout=750_000_000):
        until = min(self.deadline, self.stage_deadline, time.perf_counter_ns()+timeout)
        original_stage = self.stage_deadline
        self.stage_deadline = until
        last = None
        try:
            while time.perf_counter_ns() < until:
                try:
                    # Unavailable captures are observations only. fresh() still
                    # enforces each original response/source and writer bound;
                    # no command or family admission follows a pending capture.
                    s, _, now = self.fresh()
                    require(integer(s["input_evidence"]["observedNs"]) >= self.last_write,
                            "boundary contact predates acknowledged input")
                    if self.anchor is None:
                        self.anchor = geometry.owner_key(s)
                        self.preserved = copy.deepcopy(s["input"])
                    result = predicate(s, now)
                    if name in ("ordinary_right_gun_ready", "prepared_hold", "partial_settled", "before_preparation_shot"):
                        self.geometry_evidence = geometry.magazine_geometry_evidence(s)
                except geometry.Pending as pending:
                    last = str(pending)
                    continue
                require(time.perf_counter_ns() < min(until, self.snapshot_source_deadline(s)),
                        "boundary validation consumed its original clock")
                record = dict(name=name, nonce=s["nonce"], inputSequence=s["input_evidence"]["sequence"],
                              observedNs=s["input_evidence"]["observedNs"], evaluatedNs=now,
                              afterCommandNs=self.last_write, result=result)
                self.boundaries.append(record)
                self.artifacts.event(dict(type="fresh_magazine_bolt_boundary", **record))
                return s, now, result
            raise Refusal(name + " evidence absent: " + str(last))
        finally:
            self.stage_deadline = original_stage

    def move(self, snapshot, now, hand, local, grip, maximum_pos, maximum_angle, *, family=None):
        family = family or self.family
        target = geometry.control(snapshot, family, hand, local, now)
        self.move_control(snapshot, hand, target, grip, maximum_pos, maximum_angle)

    def move_control(self, snapshot, hand, target, grip, maximum_pos, maximum_angle):
        state = self.state["handL" if hand == 0 else "handR"]
        start = frame.from_pose(dict(position=state["pos"], orientation=frame.ypr_quaternion(state["ypr"])))
        path = geometry.subdivide(start, target, maximum_pos, maximum_angle)
        samples = [geometry.pose_sample(hand, pose, grip) for pose in path]
        previous = start
        for sample in samples:
            emitted = frame.from_pose(dict(position=sample["positionMeters"],
                orientation=frame.ypr_quaternion(sample["yawPitchRollDegrees"])))
            require(all(v <= cap for v, cap in zip(frame.pose_error(previous, emitted),
                                                   (maximum_pos, maximum_angle))),
                    "host pose precision exceeds the mechanism step bound")
            previous = emitted
        require(self.commands + len(path)*4 + 16 <= transport.MAX_COMMANDS,
                "planned movement plus ordinary cleanup exceeds remaining command budget")
        require(self.motion_reference is None, "nested ordinary motion is unsupported")
        self.motion_reference = copy.deepcopy(snapshot)
        try:
            for sample in samples:
                # Reuse a still-live source, but never extrapolate its authority
                # past the original deadline. Waiting/requesting sends no input;
                # fresh() retains the original action/stage/response budgets.
                self.pause_until(max(self.last_write + 25_000_000, time.perf_counter_ns()))
                if time.perf_counter_ns() >= self.snapshot_source_deadline(snapshot):
                    snapshot, _, now = self.fresh()
                    require(integer(snapshot["input_evidence"]["observedNs"]) >= self.last_write,
                            "movement renewal predates acknowledged input")
                else:
                    now = time.perf_counter_ns()
                geometry.movement_continuation(self.motion_reference, snapshot, now, self.family, self.debt)
                self.latest_snapshot = snapshot
                source = self.snapshot_source_deadline(snapshot)
                self.command(sample=sample, source=source)
        finally:
            self.motion_reference = None

    def dwell(self, duration):
        require(0 <= integer(duration) <= NS, "unbounded endpoint dwell")
        self.pause_until(self.last_commit + integer(duration))

    @staticmethod
    def ordinary_gun_ready(snapshot, now):
        x = geometry.magazine(snapshot, now, raw=True, idle=True)
        b = geometry.bolt(snapshot, now, raw=True)
        gun = geometry.claim(x, 1, 1, now, x["weapon"])
        require(gun == geometry.claim(b, 1, 1, now, x["weapon"])
                and integer(gun["prerequisiteClaim"]) == 0
                and x["leftClaim"] is None and b["leftClaim"] is None,
                "ordinary right gun family custody differs")
        return copy.deepcopy(gun)

    def ensure_gun_hold(self):
        if self.held_right:
            return
        require(not getattr(self, "bootstrap_attempted", False), "ordinary grip bootstrap already attempted")
        self.bootstrap_attempted = True
        original_stage = self.stage_deadline
        self.stage_deadline = min(self.deadline, original_stage, time.perf_counter_ns()+750_000_000)
        self.bootstrap_phase = "press"
        try:
            s, _, _ = self.boundary("ordinary_held_neutral", lambda s, n: geometry.held_entry(s, n)["physicalGun"])
            self.bootstrap_source = copy.deepcopy(s)
            self.bootstrap_incomplete = True
            self.command(control="grip r", value=1, source=self.snapshot_source_deadline(s))
            self.bootstrap_phase = "claim"

            def held(s, n):
                h = geometry.held_entry(s, n, self.bootstrap_source, self.last_write)
                geometry.wait_for(h["rightClaim"] is not None, "ordinary right GunHold receipt pending")
                return copy.deepcopy(h["rightClaim"]["token"])

            _, _, gun = self.boundary("ordinary_right_gun_held", held)
            # Leave root/body-only observation before full family admission.
            self.bootstrap_phase = None
            _, _, admitted = self.boundary("ordinary_right_gun_ready", self.ordinary_gun_ready)
            require(admitted == gun, "ordinary family admission replaced the observed GunHold")
            self.bootstrap_incomplete = False
        finally:
            self.bootstrap_phase = None
            self.stage_deadline = original_stage

    def run_prepare_hold(self):
        self.ensure_gun_hold()
        self.boundary("prepared_hold", self.ordinary_gun_ready)
        self.hold_prepared = True

    def run_magazine(self):
        self.ensure_gun_hold()
        # Support release is an ordinary edge; no claim clearing or reset.
        if self.held_left:
            self.boundary("before_support_release", lambda s, n: geometry.claim(geometry.magazine(s, n), 0, 2, n))
            self.command(control="grip l", value=0, source=self.snapshot_source_deadline(self.latest_snapshot))
        s, now, base = self.boundary("partial_settled", geometry.baseline)
        p, insertion = base["profile"], base["profile"]["insertion"]
        maximum_pos = min(frame.finite(p["maxPullStepMeters"]), frame.finite(insertion["maxStepMeters"]))
        maximum_angle = frame.finite(insertion["maxStepRadians"])
        removal = geometry.magazine_path(s, "remove")
        self.move(s, now, 0, removal[0], 0, maximum_pos, maximum_angle)
        s, now, _ = self.boundary("removal_contact", lambda s, n: geometry.magazine(s, n, raw=True)["raw"]["current"])
        self.command(control="grip l", value=1, source=self.snapshot_source_deadline(s))
        def acquired(s, n):
            x = geometry.magazine(s, n, raw=True)
            geometry.wait_for(x["leftClaim"] is not None, "original removal claim pending")
            t = geometry.claim(x, 0, 4, n, x["weapon"])
            require(t["prerequisiteClaim"] == base["gun"]["id"]
                    and geometry.claim(x, 1, 1, n) == base["gun"], "original removal gun custody changed")
            return t
        s, now, removal_claim = self.boundary("original_grabbed", acquired)
        for local in removal[1:]:
            self.move(s, now, 0, local, 1, maximum_pos, maximum_angle)
            s, now, _ = self.boundary("original_pull_continuation", lambda a, n: acquired(a, n))
        empty = dict(base["counts"], loaded=0)
        def removed(s, n):
            x = geometry.magazine(s, n, idle=True)
            geometry.wait_for(integer(x["phase"]) == 3, "original physically removed-held pending")
            require(geometry.claim(x, 0, 4, n) == removal_claim, "removed original claim changed")
            r = geometry.receipt(s, n, base, 0, base["counts"], empty)
            require(r["original"] is not None and r["original"]["rounds"] == base["counts"]["loaded"]
                    and r["original"]["owner"] == r["receipt"]["command"]["context"]["resource"],
                    "removed original rounds/owner were not retained")
            return r
        s, now, removed_receipt = self.boundary("removed_native_receipt", removed)
        self.command(control="grip l", value=0, source=self.snapshot_source_deadline(s))
        s, now, _ = self.boundary("original_discarded", lambda s, n: geometry.discarded(s, n, base, removed_receipt))
        x = geometry.magazine(s, now, raw=True, idle=True)
        # The normal chest supply consumes BodyAnchorHandPose, not raw wrist.
        body = frame.supply_body_frame(s["input"])
        current = frame.from_pose(s["input"]["hands"][0]["grip"])
        local = frame.multiply(current, frame.inverse(body))
        local[3][:3] = [frame.finite(v) for v in x["pouch"]["centerMeters"]]
        self.move_control(s, 0, frame.multiply(local, body), 0, maximum_pos, maximum_angle)
        s, now, _ = self.boundary("chest_contact", lambda s, n: geometry.magazine(s, n, raw=True)["pouch"])
        self.command(control="grip l", value=1, source=self.snapshot_source_deadline(s))
        s, now, carried = self.boundary("actual_replacement_pickup", lambda s, n: geometry.replacement(s, n, base))
        path = geometry.magazine_path(s, "insert")
        # Chest-to-entry motion must first establish an actual outside sample;
        # placing a replacement directly inside the rail cannot arm insertion.
        self.move(s, now, 0, geometry.insertion_outside(p), 1, maximum_pos, maximum_angle)
        s, now, _ = self.boundary("replacement_outside_entry", lambda s, n: geometry.replacement(s, n, base, carried))
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+integer(insertion["maxGuidedNs"]))
        for index, local in enumerate(path):
            self.move(s, now, 0, local, 1, maximum_pos, maximum_angle)
            if index == 0:
                self.dwell(integer(insertion["alignmentNs"]))
            if index < len(path)-1:
                s, now, _ = self.boundary("same_replacement_continuation", lambda s, n: geometry.replacement(s, n, base, carried))
        self.dwell(integer(insertion["seatDwellNs"]))
        refill_base = dict(base, request=integer(removed_receipt["request"]))
        after = dict(empty, loaded=empty["capacity"], reserve=empty["reserve"]-empty["capacity"])
        def inserted(s, n):
            x = geometry.magazine(s, n, idle=True)
            geometry.wait_for(x["resource"]["wellEmpty"] is False, "replacement seat/native commit pending")
            r = geometry.receipt(s, n, refill_base, 2, empty, after)
            require(integer(x["seatedRequest"]) in (0, integer(r["request"]))
                    and (integer(x["seatedRequest"]) != 0 or (integer(x["phase"]) == 0
                         and x["carriedReplacement"] is None and x["reservation"] is None))
                    and x["resource"]["original"] == removed_receipt["original"]
                    and integer(x["resource"]["originalState"]) == 2,
                    "seat is not this refill or lost the discarded original")
            return r
        s, now, _ = self.boundary("replacement_native_receipt", inserted)
        self.command(control="grip l", value=0, source=self.snapshot_source_deadline(s))
        self.stage_deadline = self.deadline
        self.boundary("magazine_released", lambda s, n: self.magazine_released(s, n, base, after))

    @staticmethod
    def magazine_released(s, n, base, after):
        x = geometry.magazine(s, n, idle=True)
        geometry.wait_for(x["leftClaim"] is None and x["blocksWeaponActions"] is False,
                          "normal magazine release still pending")
        require(geometry.claim(x, 1, 1, n) == base["gun"] and counts_of(x) == after,
                "release changed gun custody/ammunition")
        return dict(released=True, retainedNativeReceipt=True)

    def admit_magazine_capture(self, snapshot, now):
        require(self.args.mode == "magazine" and self.boundaries
                and self.boundaries[-1]["name"] == "magazine_released"
                and not (self.pending_command or self.unsafe_owner or self.held_left or self.held_trigger),
                "capture requires the settled magazine release")
        base = next(b["result"] for b in self.boundaries if b["name"] == "partial_settled")
        closure = next(b["result"] for b in self.boundaries if b["name"] == "replacement_native_receipt")
        x = geometry.magazine(snapshot, now, idle=True)
        require(x["resource"]["identity"] == closure["identity"]
                and x["resource"]["receipt"] == closure["receipt"]
                and x["resource"]["receiptJoined"] is True,
                "magazine capture native refill changed")
        self.magazine_released(snapshot, now, base, closure["receipt"]["after"])

    def capture_after_magazine(self, name):
        require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}", name) is not None, "invalid capture tag")
        require(self.capture_sequence < integer(self.capture_plan["maximum_compositions"]), "capture budget exhausted")
        path = self.run / "sim" / "capture" / (name + ".json")
        require(not path.exists(), "capture tag already exists")
        snapshot, _, now = self.fresh()
        self.admit_magazine_capture(snapshot, now)
        target = {k: copy.deepcopy(self.state[k]) for k in ("head", "handL", "handR", "controls")}
        target.update(commands=["shot " + name], capture=name)
        # The canonical publisher retains this magazine artifact's ownership,
        # original action/source/ACK clocks and pre-admitted capture budget.
        self.send(target["commands"], expected=target, capture=name,
                  source_deadline=self.snapshot_source_deadline(snapshot))
        for _ in range(6000):
            self.pump()
            if path.exists():
                value, _ = transport.read_json(path, 65536)
                require(value["name"] == name and value["width"] == self.capture_plan["width"]
                        and value["height"] == self.capture_plan["height"]
                        and sorted(v["eye"] for v in value["views"]) == ["left", "right"],
                        "actual stereo capture differs from the requested budgeted pair")
                state = self.read_state()
                self.ownership(state)
                self.check_target(state, target)
                require(integer(state["captureSeq"]) == self.capture_sequence
                        and transport.same_path(state["lastCapture"], path.with_suffix("")),
                        "current capture identity changed")
                self.post_magazine_capture = dict(path=str(path), captureSeq=self.capture_sequence,
                                                 width=value["width"], height=value["height"])
                self.artifacts.save("capture.json", self.post_magazine_capture)
                # Keep magazine_released as the terminal native boundary.
                return
            time.sleep(.001)
        raise Refusal("capture metadata iteration bound")

    def run_preparation_shot(self):
        require(self.args.fire_clearance is not None and self.args.magazine_result is None,
                "prep-shot-bolt requires ROOT corridor review and does not consume a magazine result")
        self.ensure_gun_hold()
        s, now, self.preparation = self.boundary("before_preparation_shot", geometry.preparation_baseline)
        # One attempt per exact host/owner/native shot identity, including a
        # refused clearance or uncertain publication. This is evidence only.
        key = dict(host=self.host, owner=self.preparation["owner"], identity=self.preparation["identity"],
                   cycle=self.preparation["cycle"], shot=self.preparation["shot"])
        encoded = json.dumps(key, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
        attempt = self.run / ("magazine-preparation-attempt-"+transport.digest(encoded)+".json")
        with attempt.open("xb") as stream:
            stream.write(transport.json_bytes(dict(schema="bc2vr.magazine-preparation-attempt.v1",
                key=key, preparationOnly=True, artifactDirectory=str(self.artifacts.directory))))
        self.fire_permission, _ = transport.read_json(self.args.fire_clearance, 16384)
        for pin in self.fire_permission["captures"]:
            require(transport.child(self.run, pin["path"]) == transport.local_path(pin["path"]),
                    "ROOT reviewed capture is outside the linked run")
            transport.check_pin(pin)
        pulse = geometry.fire_clearance(self.fire_permission, s, self.state, self.host,
                                        time.perf_counter_ns(), self.deadline)
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+pulse)
        self.command(control="trigger r", value=1, source=self.snapshot_source_deadline(s), fire=True)
        self.preparation["pressedNs"] = self.last_write
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+250_000_000)
        self.command(control="trigger r", value=0)
        require(self.last_write-self.preparation["pressedNs"] <= pulse,
                "single preparation trigger pulse exceeded100ms; released; no retry")
        self.stage_deadline = self.deadline
        s, now, self.debt = self.boundary("preparation_shot_created_owed_bolt", geometry.owed)
        self.boundary("preparation_shot_counts", lambda s, n:
                      geometry.preparation_debt(s, n, self.preparation, self.debt))

    def finish_preparation(self):
        self.boundary("preparation_completed", lambda s, n:
                      geometry.preparation_complete(s, n, self.preparation, self.debt))

    def run_shot(self):
        require(self.args.magazine_result is not None and self.args.fire_clearance is not None,
                "shot-bolt requires the immediately preceding magazine result and ROOT corridor review")
        result_path = transport.child(self.run, self.args.magazine_result)
        require(self.prior_owner is not None and transport.same_path(self.prior_owner["artifactDirectory"], result_path.parent),
                "shot must immediately follow the settled owned magazine adapter")
        closed, closed_raw = transport.read_json(result_path, transport.MAX_BYTES)
        require(closed["schema"] == "bc2vr.magazine-bolt-stimulus-result.v1"
                and closed["status"] == "ordinary_stimulus_completed" and closed["stimulusCompleted"] is True
                and closed["pendingCommand"] is False and "error" not in closed,
                "preceding magazine stimulus did not complete cleanly")
        closure = next(b for b in closed["boundaries"] if b["name"] == "replacement_native_receipt")["result"]
        require(closed["boundaries"][-1]["name"] == "magazine_released", "preceding magazine grip release missing")
        self.fire_permission, _ = transport.read_json(self.args.fire_clearance, 16384)
        for pin in self.fire_permission["captures"]:
            require(transport.child(self.run, pin["path"]) == transport.local_path(pin["path"]),
                    "ROOT reviewed capture is outside the linked run")
            transport.check_pin(pin)
        # One invocation attempt for this exact native refill request, including
        # refusal. File is run-local evidence, not a command or ammo mutation.
        attempt = self.run / ("magazine-shot-attempt-"+str(integer(closure["request"]))+".json")
        with attempt.open("xb") as stream:
            stream.write(transport.json_bytes(dict(host=self.host, request=closure["request"],
                closeSha256=transport.digest(closed_raw), artifactDirectory=str(self.artifacts.directory))))
        def before(s, n):
            b = geometry.bolt(s, n)
            require(b["held"] is None and b["recovery"] is None and b["blocksFire"] is False
                    and s["weapon_actions_blocked"] is False, "ordinary shot starts only from native ready")
            m = geometry.magazine(s, n, idle=True)
            require(m["resource"]["identity"] == closure["identity"]
                    and m["resource"]["receipt"] == closure["receipt"]
                    and m["resource"]["receiptJoined"] is True
                    and counts_of(m) == closure["receipt"]["after"], "post-refill native closure changed")
            return dict(identity=b["native"]["identity"], shot=b["native"]["shot"])
        s, now, initial = self.boundary("before_ordinary_shot", before)
        pulse = geometry.fire_clearance(self.fire_permission, s, self.state, self.host, time.perf_counter_ns(), self.deadline)
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+pulse)
        self.command(control="trigger r", value=1, source=self.snapshot_source_deadline(s), fire=True)
        pressed = self.last_write
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+250_000_000)
        self.command(control="trigger r", value=0)
        require(self.last_write-pressed <= pulse, "single trigger pulse exceeded100ms; released; no retry")
        self.stage_deadline = self.deadline
        s, now, self.debt = self.boundary("shot_created_owed_bolt", geometry.owed)
        require(self.debt["identity"] == initial["identity"]
                and integer(self.debt["debt"]["shot"]) > integer(initial["shot"]),
                "ordinary trigger did not produce a new same-owner bolt debt")

    def run_bolt(self):
        if self.debt is None:
            s, now, self.debt = self.boundary("existing_owed_bolt", geometry.owed)
        else:
            s, now, _ = self.boundary("same_owed_bolt", lambda s, n: geometry.bolt(s, n, self.debt)["custody"])
        b = geometry.bolt(s, now, self.debt)
        references = b.get("originalGripReferences")
        require(references is not None and references["geometryOnly"] is True
                and references["sourceInputClockAvailable"] is False,
                "normal retained support/return grip geometry unavailable")
        refs = [frame.rigid(v) for v in references["wristInWeapon"]]
        p = b["calibration"]
        maximum_pos, maximum_angle = frame.finite(p["maxStepMeters"]), .08
        # The rotation subdivision is a command resolution; endpoint/geometry
        # acceptance still uses the observed production calibration.
        self.stage_deadline = min(self.deadline, time.perf_counter_ns()+geometry.bolt_cycle_budget(p))
        if not (b["custodyCurrent"] and b["gunClaim"] and integer(b["gunClaim"]["token"]["hand"]) == 0):
            require(b["leftClaim"] is None or integer(b["leftClaim"]["token"]["kind"]) == 2,
                    "another object occupies the support hand")
            if b["leftClaim"] is None:
                self.move(s, now, 0, refs[0], 0, maximum_pos, maximum_angle)
                s, now, _ = self.boundary("support_approach", lambda s, n: geometry.bolt(s, n, self.debt)["raw"]["current"])
                self.command(control="grip l", value=1, source=self.snapshot_source_deadline(s))
            def supported(s, n):
                x = geometry.bolt(s, n, self.debt)
                geometry.wait_for(x["leftClaim"] is not None, "actual support claim pending")
                left, right = geometry.claim(x, 0, 2, n), geometry.claim(x, 1, 1, n)
                require(left["prerequisiteClaim"] == right["id"] and left["item"] == right["item"],
                        "support does not depend on exact departing gun")
                return dict(left=left, right=right)
            s, now, _ = self.boundary("support_owned", supported)
            # Capture a real right press before release if normal native hold
            # existed with the controller physically released.
            if not self.held_right:
                self.command(control="grip r", value=1, source=self.snapshot_source_deadline(s))
                s, now, _ = self.boundary("departing_gun_pressed", supported)
            self.command(control="grip r", value=0, source=self.snapshot_source_deadline(s))
        s, now, self.gun_root = self.boundary("left_gun_custody", lambda s, n: geometry.custody(s, n, self.debt, 0))
        b = geometry.bolt(s, now, self.debt)
        require(integer(b["mechanismPhase"]) in (0, 1), "continuation is not at original owed stroke start")
        self.move(s, now, 1, geometry.bolt_pose(p, 0, 0), 0, maximum_pos, maximum_angle)
        s, now, _ = self.boundary("bolt_closed_contact", lambda s, n: geometry.custody(s, n, self.debt, 0, self.gun_root))
        self.command(control="grip r", value=1, source=self.snapshot_source_deadline(s))
        def mechanism(s, n):
            x = geometry.bolt(s, n, self.debt)
            geometry.custody(s, n, self.debt, 0, self.gun_root)
            geometry.wait_for(x["mechanismClaim"] is not None, "actual mechanism acquisition pending")
            t = geometry.claim(x, 1, 4, n)
            require(x["mechanismClaim"]["token"] == t and t["prerequisiteClaim"] == self.gun_root["id"],
                    "mechanism claim is not bound to retained left gun root")
            return t
        s, now, mech = self.boundary("bolt_mechanism_owned", mechanism)
        endpoints = [(0, p["unlockRadians"], 3), (p["strokeMeters"], p["unlockRadians"], 4),
                     (0, p["unlockRadians"], 5), (0, 0, 6)]
        for travel, angle, phase in endpoints:
            self.move(s, now, 1, geometry.bolt_pose(p, travel, angle), 1, maximum_pos, maximum_angle)
            self.dwell(integer(p["endpointDwellNs"]))
            def advanced(s, n, phase=phase):
                x = geometry.bolt(s, n, self.debt)
                geometry.custody(s, n, self.debt, 0, self.gun_root)
                geometry.wait_for(integer(x["mechanismPhase"]) >= phase, "owed bolt endpoint still pending")
                require(integer(x["mechanismPhase"]) <= 7, "bolt entered cancellation/reload pause")
                if phase < 6:
                    require(mechanism(s, n) == mech, "mechanism claim changed mid-stroke")
                return dict(phase=x["mechanismPhase"], target=x["target"])
            s, now, _ = self.boundary("bolt_endpoint_"+str(phase), advanced)
        s, now, ready_receipt = self.boundary("same_debt_native_ready", lambda s, n: geometry.ready(s, n, self.debt))
        self.debt["readyReceipt"] = copy.deepcopy(ready_receipt)
        self.command(control="grip r", value=0, source=self.snapshot_source_deadline(s))
        s, now, _ = self.boundary("return_neutral", lambda s, n: geometry.custody(s, n, self.debt, 0, self.gun_root))
        self.move(s, now, 1, refs[1], 0, maximum_pos, maximum_angle)
        s, now, _ = self.boundary("right_return_contact", lambda s, n: geometry.custody(s, n, self.debt, 0, self.gun_root))
        self.command(control="grip r", value=1, source=self.snapshot_source_deadline(s))
        def returned(s, n):
            root = geometry.custody(s, n, self.debt, 1)
            x = geometry.bolt(s, n, self.debt)
            left = geometry.claim(x, 0, 2, n)
            require(left["prerequisiteClaim"] == root["id"] and x["blocksFire"] is False,
                    "normal support/gun return has not settled")
            return dict(gun=root, support=left, ready=geometry.ready(s, n, self.debt))
        self.boundary("right_gun_and_support_returned", returned)
        self.bolt_cycle_completed = True
        self.stage_deadline = self.deadline

    def handoff_snapshot(self, snapshot, now, phase):
        guard = self.handoff_guard
        require(guard is not None and geometry.owner_key(snapshot) == guard["owner"],
                "neutral handoff equipment owner changed")
        b = geometry.bolt(snapshot, now, raw=False)
        native = b["native"]
        require(native["identity"] == self.debt["identity"]
                and integer(native["cycle"]) == integer(self.debt["debt"]["cycle"])
                and integer(native["shot"]) == integer(self.debt["debt"]["shot"])
                and geometry.counts(native["counts"]) == self.debt["counts"]
                and integer(native["phase"]) == 5
                and b["held"] is None and b["recovery"] is None and native["held"] is None
                and b["mechanismClaim"] is None and b["blocksFire"] is False
                and snapshot["weapon_actions_blocked"] is False,
                "neutral handoff requires the completed unchanged bolt")
        if phase == "support":
            require(geometry.claim(b, 0, 2, now, guard["gun"]["item"]) == guard["support"],
                    "neutral handoff support custody changed")
        else:
            if b["leftClaim"] is not None:
                require(b["leftClaim"]["token"] == guard["support"],
                        "neutral handoff left custody changed")
                raise geometry.Pending("neutral handoff support release pending")
            geometry.wait_for(b["input"]["released"][0] is True,
                              "neutral handoff left input release pending")
        if phase != "neutral":
            require(geometry.claim(b, 1, 1, now, guard["gun"]["item"]) == guard["gun"],
                    "neutral handoff right gun custody changed")
        return dict(phase=phase, sameCompletedDebt=True)

    def run_neutral_handoff(self):
        # This is an optional successful-operation transition, never failure
        # cleanup. Preserve the original invocation and source/ACK deadlines.
        require(self.family == "bolt" and self.debt is not None
                and self.debt.get("readyReceipt") is not None
                and not (self.pending_command or self.unsafe_owner),
                "neutral handoff requires a settled completed bolt invocation")
        returned = next((b["result"] for b in reversed(self.boundaries)
                         if b["name"] == "right_gun_and_support_returned"), None)
        require(returned is not None and returned["support"]["prerequisiteClaim"] == returned["gun"]["id"],
                "neutral handoff lacks this invocation's returned gun/support")
        holster = geometry.shared("bc2_holster_core")
        self.handoff_guard = dict(owner=copy.deepcopy(self.anchor),
                                  gun=returned["gun"], support=returned["support"])
        try:
            s, _, _ = self.boundary("before_neutral_handoff", lambda s, n:
                                    self.handoff_snapshot(s, n, "support"))
            self.handoff_phase = "support"
            self.command(control="grip l", value=0, source=self.snapshot_source_deadline(s))
            self.handoff_phase = None
            s, _, _ = self.boundary("neutral_handoff_support_released", lambda s, n:
                                    self.handoff_snapshot(s, n, "gun"))
            self.handoff_phase = "gun"
            self.command(control="grip r", value=0, source=self.snapshot_source_deadline(s))
            self.handoff_phase = None

            def neutral(s, n):
                self.handoff_snapshot(s, n, "neutral")
                h = holster.holster_proof(s, n)
                holster.OrdinaryHolsterDriver.require_held(self, s)
                geometry.wait_for(h["neutralTrackedInput"] is True and holster.neutral(s["input"]),
                                  "neutral handoff fresh neutral input pending")
                require(h["physicalGun"] == returned["gun"]["item"]
                        and ordinary.neutral_controls(self.state["controls"])
                        and integer(h["inputEvidence"]["observedNs"]) >= self.last_write
                        and not (self.pending_command or self.unsafe_owner),
                        "neutral handoff lacks acknowledged same-gun neutral observation")
                return dict(neutral=True, gun=h["rightClaim"]["token"],
                            completedDebtRetained=True, holsterEntryObserved=True)

            self.boundary("neutral_handoff_completed", neutral)
            self.neutral_handoff_completed = True
        finally:
            self.handoff_phase = None

    def cleanup_owned(self):
        if self.pending_command or self.unsafe_owner:
            return "retained: unsettled command or writer ownership; no speculative cleanup"
        if self.held_trigger:
            self.command(control="trigger r", value=0, cleanup=True)
        if getattr(self, "bootstrap_incomplete", False) and self.held_right:
            # Only our acknowledged warmup squeeze is releasable here. The
            # canonical publisher still rejoins exact writer/state/ACK before
            # cleanup, and pending/foreign ownership above forbids speculation.
            self.command(control="grip r", value=0, cleanup=True)
            return "owned incomplete grip bootstrap released"
        # A release can discard an original magazine and its rounds, return a
        # replacement, or drop a gun. Only the admitted transaction path may
        # request those edges. Unknown or existing custody survives a refusal.
        return "owned trigger released; existing grips, ammunition custody and owed debt retained"


def counts_of(x):
    return geometry.counts(x["resource"]["snapshot"]["counts"])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--mode", choices=("prepare-hold", "magazine", "prep-shot-bolt", "shot-bolt", "bolt"), required=True)
    parser.add_argument("--fire-clearance", type=Path)
    parser.add_argument("--magazine-result", type=Path)
    parser.add_argument("--capture-after-magazine", help="one budgeted stereo capture under the completed magazine writer")
    parser.add_argument("--neutral-handoff", action="store_true",
                        help="after a completed bolt return, release owned grips and observe neutral holster entry")
    parser.add_argument("--root-authorized-ordinary-input", action="store_true")
    args = parser.parse_args(argv)
    result = dict(schema="bc2vr.magazine-bolt-stimulus-result.v1", status="refused",
                  stimulusCompleted=False, nativeAcceptance=False, headsetAcceptance=False,
                  shotAllThreeCopiesVerified=False, mode=args.mode,
                  boltCycleCompleted=False,
                  neutralHandoffRequested=args.neutral_handoff, neutralHandoffCompleted=False,
                  preparationOnly=args.mode == "prep-shot-bolt", preparationCompleted=False,
                  commands=0, pendingCommand=False, boundaries=[], retainedDebt=None,
                  postMagazineCapture=None, holdPrepared=False, retainedGunRoot=None, geometryEvidence=None)
    driver, code = None, 2
    try:
        require(args.root_authorized_ordinary_input, "ROOT ordinary-input authorization required")
        require(not args.neutral_handoff or args.mode not in ("magazine", "prepare-hold"),
                "neutral handoff requires a bolt completion mode")
        require(not args.capture_after_magazine or args.mode == "magazine",
                "post-magazine capture requires magazine mode")
        require(args.mode != "prepare-hold" or (args.fire_clearance is None and args.magazine_result is None),
                "prepare-hold only establishes ordinary custody before a separate capture/review")
        # Parse and hash immutable source review before the constructor starts
        # the action clock or any original native observation lease exists.
        geometry.magazine_geometry_sources()
        driver = MagazineBoltDriver.__new__(MagazineBoltDriver)
        driver.__init__(args)
        if args.mode == "prepare-hold":
            driver.run_prepare_hold()
        elif args.mode == "magazine":
            driver.run_magazine()
            if args.capture_after_magazine:
                driver.capture_after_magazine(args.capture_after_magazine)
        else:
            if args.mode == "shot-bolt":
                driver.run_shot()
            elif args.mode == "prep-shot-bolt":
                driver.run_preparation_shot()
            driver.run_bolt()
            if args.mode == "prep-shot-bolt":
                driver.finish_preparation()
            if args.neutral_handoff:
                driver.run_neutral_handoff()
        result.update(status="ordinary_hold_prepared" if args.mode == "prepare-hold" else
                      "ordinary_preparation_completed" if args.mode == "prep-shot-bolt"
                      else "ordinary_stimulus_completed", stimulusCompleted=True,
                      preparationCompleted=args.mode == "prep-shot-bolt")
        code = 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
    finally:
        if driver is not None:
            try:
                if hasattr(driver, "family"):
                    result["cleanup"] = driver.cleanup_owned()
            except Exception as error:
                result["cleanupError"] = str(error)
                code = 2
            result.update(commands=getattr(driver, "commands", 0),
                          pendingCommand=getattr(driver, "pending_command", False),
                          boundaries=getattr(driver, "boundaries", []),
                          retainedDebt=getattr(driver, "debt", None),
                          boltCycleCompleted=getattr(driver, "bolt_cycle_completed", False),
                          neutralHandoffCompleted=getattr(driver, "neutral_handoff_completed", False),
                          postMagazineCapture=getattr(driver, "post_magazine_capture", None),
                          holdPrepared=getattr(driver, "hold_prepared", False),
                          geometryEvidence=getattr(driver, "geometry_evidence", None),
                          retainedGunRoot=getattr(driver, "gun_root", None))
            try:
                driver.close()
            except Exception as error:
                result["closeError"] = str(error)
                code = 2
            artifacts = getattr(driver, "artifacts", None)
            if artifacts is not None:
                try:
                    if isinstance(artifacts, ordinary.DeferredArtifacts):
                        artifacts.flush()
                    result["artifactDirectory"] = str(artifacts.directory)
                    if code:
                        result.update(status="refused", stimulusCompleted=False, preparationCompleted=False)
                    artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["archiveError"] = str(error)
                    code = 2
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
