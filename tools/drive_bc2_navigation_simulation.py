"""ROOT-only bounded ordinary on-foot navigation through the shared xr-sim driver.

--navigation FILE (inside --run) is a reviewed JSON list, e.g.
[{"left":[0,0.5],"right":[0,0],"holdMs":400,"lookDegrees":[10,0]}].
lookDegrees is a relative yaw/pitch change; head position, roll and controller
poses remain unchanged. holdMs allocates the row's aggregate pulse budget,
including both ACKs per pulse; fresh admission also spends the invocation time.
normal zero reserves the shared 250 ms publication/ACK window plus two bounded
25 ms pump/preparation intervals. Pulses without an active budget are refused
before input. Each of at most four rows expands into pulses of at most 500 ms,
up to sixteen pulses total. Every pulse obtains new original native admission
and ends with acknowledged numeric zeros for both sticks. Longer row budgets
include a separate zero reserve per pulse; they do not promise continuous input.
Native diagnostics are requested only at neutral boundaries. During a pulse,
the driver checks live simulator ownership/focus/tracking; ordinary native input
freshness and Playing gates remain responsible for each consumed XR packet.
An explicit passive navigation observation can admit on-foot input even without
a weapon publication. Its original actor/route, input and menu leases remain
independent of weapon validity. Only a transient gather_pending observation may
request a fresh response while ownership is proven neutral; it admits no input.
Rows of 501..601 ms cannot fit two active pulses with full zero reserves and
are refused before any input, as are rows of 300 ms or less.
Optional action "jump" presses right Primary (A), while "use" presses left
Primary (X) and requires both sticks neutral. Only a row's first subpulse presses
its action; its ordinary button-up shares the acknowledged neutral release.
The shared ten-second invocation limit remains. There are no triggers or hand
claims. No launch, capture, objective-entry, vehicle-entry, native action/movement
or headset claim is made. A button ACK cannot authorize a new owner or seat.
"""
import argparse
import copy
import gzip
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location(
    "bc2_navigation_transport", Path(__file__).with_name("drive_bc2_feed_simulation.py"))
t = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(t)
p, require, Refusal = t.planner, t.require, t.REFUSAL
_spec = importlib.util.spec_from_file_location(
    "bc2_navigation_holster", Path(__file__).with_name("bc2_holster_core.py"))
holster = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(holster)
ZERO = ["stick l 0 0", "stick r 0 0"]
# OpenXrInput binds right/left Primary to A/X; ControllerInput maps them to
# Jump/Use. Keep this a closed semantic list, never an arbitrary button escape.
ACTION_BUTTONS = {"jump": "a", "use": "x"}
PERIOD = 25_000_000
# Driver._send gives a moving command 250 ms from its request timestamp for
# publication and ACK together. Keep that complete window inside holdMs.
COMMAND_WINDOW = 250_000_000
RELEASE_RESERVE = COMMAND_WINDOW + 2 * PERIOD
NAVIGATION_OWNER = ("manager", "player", "soldier", "weak", "controlled", "entry",
                    "router", "cache", "actorGeneration", "inputEpoch", "movementEpoch")


class ObservationLost(Refusal):
    """Loss of gameplay readiness does not itself revoke command-file ownership."""


def need_observation(condition, message):
    if not condition:
        raise ObservationLost(message)


def vector(value, count, cap):
    require(isinstance(value, list) and len(value) == count
            and all(type(x) in (int, float) and math.isfinite(x) and abs(x) <= cap for x in value),
            "navigation vector outside reviewed bound")
    return value[:]


def plan(value):
    require(isinstance(value, list) and 1 <= len(value) <= 4, "one to four reviewed rows required")
    for row in value:
        require(isinstance(row, dict) and set(row) <= {"left", "right", "holdMs", "lookDegrees", "action"}
                and {"left", "right", "holdMs"} <= set(row), "unknown or missing navigation fields")
        vector(row["left"], 2, 1)
        vector(row["right"], 2, 1)
        require(type(row["holdMs"]) is int and 100 <= row["holdMs"] <= 2000,
                "row must be 100..2000 ms, including command and zero acknowledgement")
        if "action" in row:
            require(isinstance(row["action"], str) and row["action"] in ACTION_BUTTONS,
                    "navigation action must be jump or use")
            require(row["action"] != "use" or not any(row["left"] + row["right"]),
                    "Use requires both sticks neutral")
        if "lookDegrees" in row:
            look = vector(row["lookDegrees"], 2, 45)
            require(abs(look[1]) <= 30, "pitch step exceeds 30 degrees")
    return value


def pulse_schedule(rows):
    schedule = []
    for row_index, row in enumerate(rows):
        count = (row["holdMs"] + 499) // 500
        duration, extra = divmod(row["holdMs"], count)
        # Preserve the requested aggregate bound; never round a short tail up
        # or publish one before discovering that its zero reserve cannot fit.
        require(duration * 1_000_000 > RELEASE_RESERVE,
                "pulse has no active budget after full zero publication/ACK reserve")
        for index in range(count):
            pulse = dict(rowIndex=row_index, subpulseIndex=index, subpulseCount=count,
                         requestedRowMs=row["holdMs"], holdMs=duration + (index < extra),
                         left=row["left"][:], right=row["right"][:])
            if index == 0 and "lookDegrees" in row:
                pulse["lookDegrees"] = row["lookDegrees"][:]
            if index == 0 and "action" in row:
                pulse["action"] = row["action"]
            schedule.append(pulse)
    return schedule


def neutral_controls(controls):
    return bool(controls) and all(
        all(x == 0 for x in value) if isinstance(value, list) else value == 0
        for value in controls.values())


def canonical_head(head):
    """Normalize cyclic yaw only; every other head field still compares exactly."""
    head = copy.deepcopy(head)
    # Remainder avoids rounding away small yaw changes at the wrap boundary.
    yaw = math.remainder(p.finite(head["ypr"][0]), 360)
    head["ypr"][0] = 180 if yaw == -180 else yaw
    return head


def empty_movement_admission(snapshot, now):
    """Observe current genuine Empty; confer no weapon or mechanism authority."""
    h = snapshot.get("bodyHolster")
    if not isinstance(h, dict) or h.get("actualEmpty") is not True:
        return False
    # This joins the original independent holster, visibility and suppression
    # leases to the current actor/physical item. A Fire block alone is no proof.
    h = holster.holster_proof(snapshot, now)
    need_observation(all(hand["gripTracked"] is True and hand["aimTracked"] is True
                         and hand["held"] == hand["squeeze"] == hand["trigger"] == 0
                         for hand in h["input"]["hands"]),
                     "Empty navigation requires neutral tracked buttons/triggers/grips")
    # neutralTrackedInput includes sticks, so it correctly becomes false during
    # movement. holster_proof still verifies that flag against consumed input.
    return True


def navigation_input(snapshot):
    """Use the exact native navigation frame without constructing a weapon frame."""
    return (snapshot["navigationObservation"]["input"] if "navigationObservation" in snapshot
            else snapshot["input"])


def navigation_gather_pending(snapshot):
    nav = snapshot.get("navigationObservation")
    return (isinstance(nav, dict) and nav.get("observationOnly") is True
            and nav.get("available") is False and nav.get("reason") == "gather_pending"
            and "input" in nav and nav["input"] is None)


def admit_navigation_observation(snapshot, now, nonce):
    require(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1"
            and type(snapshot["valid"]) is bool, "snapshot admission refused")
    require(t.integer(snapshot["nonce"]) == nonce and nonce > 0, "snapshot request nonce mismatch")
    nav = snapshot.get("navigationObservation")
    need_observation(isinstance(nav, dict) and nav.get("observationOnly") is True
                     and nav.get("available") is True and nav.get("reason") is None,
                     "native navigation unavailable: " + str(nav.get("reason") if isinstance(nav, dict) else None))
    owner, inp = nav["owner"], nav["input"]
    need_observation(all(t.integer(owner[k]) > 0 for k in NAVIGATION_OWNER)
                     and t.integer(owner["controlled"]) == t.integer(owner["soldier"]),
                     "native navigation actor/control route unavailable")
    allowed_mask = t.integer(nav["allowedMask"])
    need_observation(nav["onFoot"] is True and nav["nativePlaying"] is True
                     and nav["allowedRead"] is True and 0 <= allowed_mask <= 0xffffffff
                     and allowed_mask & 0x103 == 0x103,
                     "native navigation on-foot Playing input unavailable")
    observed, deadline = t.integer(nav["inputObservedNs"]), t.integer(nav["inputDeadlineNs"])
    sampled, valid_until = t.integer(nav["sampledNs"]), t.integer(nav["validUntilNs"])
    menu_deadline = t.integer(inp["menu"]["deadlineNs"])
    need_observation(t.integer(nav["gatherSequence"]) > 0
                     and 0 < observed <= sampled <= t.integer(snapshot["observed_ns"]) <= now < valid_until
                     and 0 < deadline-observed <= t.SNAPSHOT_PERIOD
                     and valid_until == min(deadline, menu_deadline),
                     "stale or invalid original navigation source/menu lease")
    need_observation(t.integer(inp["generation"]) > 0 and t.integer(inp["spaceGeneration"]) > 0
                     and t.integer(inp["trackingBasisGeneration"]) > 0 and t.integer(inp["predictedNs"]) > 0
                     and inp["focused"] is True and inp["headValid"] is True,
                     "native navigation focus/head/tracking unavailable")
    # predictedNs belongs to XR's clock; source/menu authority uses native QPC.
    need_observation(t.integer(inp["menu"]["owner"]) == 1, "fresh native Playing menu ownership unavailable")
    require(type(inp["floorRelative"]) is bool and .01 <= p.finite(inp["worldUnitsPerMeter"]) <= 1000,
            "invalid navigation tracking units/basis")
    p.from_pose(inp["head"])
    p.from_pose(inp["referenceHead"])
    need_observation(isinstance(inp["hands"], list) and len(inp["hands"]) == 2
                     and all(h["gripTracked"] is True and h["aimTracked"] is True for h in inp["hands"]),
                     "native navigation controller tracking unavailable")
    for hand in inp["hands"]:
        p.from_pose(hand["grip"])
        p.from_pose(hand["aim"])
        require(all(t.integer(hand[k]) >= 0 for k in ("active", "held", "touchActive", "touched"))
                and all(-1 <= p.finite(hand[k]) <= 1 for k in ("stickX", "stickY"))
                and all(0 <= p.finite(hand[k]) <= 1 for k in ("trigger", "squeeze")),
                "invalid navigation controller input")
    return nav


class NavigationArtifacts(t.Artifacts):
    """Lossless native responses within the unchanged shared 1 MiB disk cap."""
    def __init__(self, existing):
        self.directory, self.sizes = existing.directory, existing.sizes
        self.decoded_sizes = {}

    def save(self, name, data, final=False):
        if re.fullmatch(r"snapshot-\d{3,}(?:-(?:failure|at-timeout|late))?\.json", name):
            raw = data if isinstance(data, bytes) else t.json_bytes(data)
            require(len(raw) <= p.SNAPSHOT_LIMIT, "oversized navigation snapshot")
            require(sum(self.decoded_sizes.values())-self.decoded_sizes.get(name, 0)+len(raw) <= 8*1024*1024,
                    "navigation decoded snapshot budget exhausted")
            super().save(name+".gz", gzip.compress(raw, compresslevel=1, mtime=0), final=final)
            self.decoded_sizes[name] = len(raw)
        else:
            super().save(name, data, final=final)


class NavigationDriver(t.Driver):
    def __init__(self, args):
        self.navigation_reference = None
        self.previous_navigation = None
        self.entry_admitted = False
        self.sticks = [[0, 0], [0, 0]]
        self.pending_sticks = None
        self.action_button = None
        self.pending_action_button = None
        self.release_attempted = False
        self.cleanup_mode = False
        self.releasing_sticks = False
        self.action_deadline = None
        self.authorized_lines = None
        self.command_fault = False
        self.normal_stop_required = False
        self.episodes = []
        self.navigation_schedule = []
        args.stage = args.profiles = args.capture = args.menu_batches = None
        args.pickup_first = False
        super().__init__(args)
        self.artifacts = NavigationArtifacts(self.artifacts)
        require(self.manifest["version"] == args.version
                and self.manifest["source_sha256"] == args.source_sha256,
                "ROOT-reviewed source/version differs from exact live session")
        require(self.link["mechanism_snapshots"]["requested"] is True
                and t.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                "native snapshots must be enabled before startup")
        need_observation(neutral_controls(self.state["controls"]), "navigation requires neutral entry")
        for hand in ("handL", "handR"):
            need_observation(self.state[hand]["followsHead"] is False,
                             "controller head-follow must already be off")
        self.baseline_sim = copy.deepcopy(self.state)
        self.expected_head = copy.deepcopy(self.state["head"])
        self.check_observation_state(self.state)
        self.entry_admitted = True
        owner_path = self.run / "live-driver-owner.json"
        self.navigation_owner_record = t.read_bytes(owner_path, 8192) if owner_path.exists() else None
        self.artifacts.save("navigation-invocation.json", dict(
            driverSha256=t.digest(Path(__file__).read_bytes()),
            transportSha256=t.digest(Path(t.__file__).read_bytes()),
            plannerSha256=t.digest(Path(p.__file__).read_bytes()),
            holsterPolicySha256=t.digest(Path(holster.__file__).read_bytes()),
            rootAuthorizedOrdinaryNavigation=True, version=args.version,
            sourceSha256=args.source_sha256, nativeAcceptance=False, objectiveEntry=False, vehicleEntry=False,
            snapshotPolicy="neutral-boundaries-only", freshNativeAdmissionPerPulse=True,
            snapshotArtifactEncoding="gzip-original-bytes", maximumDecodedSnapshotBytes=8*1024*1024))

    def read_state(self):
        # Identity/session checks remain mandatory for release. Focus/tracking
        # are separate observation gates, so their loss cannot strand an owned
        # stick solely by being misclassified as a competing command writer.
        self.owner.check()
        session, _ = t.read_json(self.session_path)
        require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                and session["source_sha256"] == self.manifest["source_sha256"]
                and session["version"] == self.manifest["version"] and session["state"] == "running",
                "linked session ownership/source/version changed")
        require(not (self.folder / "stop.request").exists(), "normal session Stop already requested")
        state, _ = t.read_until(self.hard_deadline, t.read_json, self.state_path, 16384)
        require(t.integer(state["pid"]) == t.integer(self.host["pid"]) and state["runtime"] == "xr-sim"
                and t.integer(state["errors"]) == 0 and state["lastCmdError"] == "",
                "simulator identity/error changed")
        require(t.integer(state["captureSeq"]) <= t.integer(self.capture_plan["maximum_compositions"]),
                "capture budget changed")
        return state

    def ownership(self, state, expected=None):
        try:
            return super().ownership(state, expected)
        except t.ReadDeadlineExceeded:
            raise
        except BaseException:
            self.command_fault = True
            raise

    def check_observation_state(self, state):
        need_observation(state["sessionRunning"] is True and state["actionsAttached"] is True
                         and state["sessionState"] == "FOCUSED" and state["paceMode"] == "free"
                         and all(state[k]["valid"] is True for k in ("head", "handL", "handR")),
                         "navigation session/focus/tracking lost")
        if not hasattr(self, "baseline_sim"):
            return
        need_observation(all(state[k] == self.baseline_sim[k] for k in ("handL", "handR"))
                         and state["head"]["pos"] == self.baseline_sim["head"]["pos"],
                         "navigation head position/controller poses changed")
        need_observation(all(v == self.baseline_sim["controls"][k]
                             for k, v in state["controls"].items()
                             if k not in ("stickL", "stickR", *ACTION_BUTTONS.values())),
                         "non-navigation controller input changed")
        allowed_buttons = [self.action_button]
        if self.pending_command and self.pending_sticks is not None:
            allowed_buttons.append(self.pending_action_button)
        need_observation(any(self.buttons_match(state, button) for button in allowed_buttons),
                         "navigation action button state changed")
        if not self.pending_command:
            need_observation(canonical_head(state["head"]) == canonical_head(self.expected_head)
                             and [state["controls"][k] for k in ("stickL", "stickR")] == self.sticks,
                             "settled navigation input changed")

    def owner_bytes(self, pending):
        value = json.loads(super().owner_bytes(pending))
        # Existing shared drivers understand pending and therefore cannot take
        # over a settled but still-held navigation pulse or an unverified zero.
        value["pending"] = (pending or self.pending_sticks is not None
                            or any(any(v) for v in self.sticks) or self.action_button is not None)
        value["navigation"] = dict(sticks=self.sticks, pendingSticks=self.pending_sticks,
                                   actionButton=self.action_button, pendingActionButton=self.pending_action_button,
                                   neutralCommitted=not value["pending"])
        return t.json_bytes(value)

    @staticmethod
    def buttons_match(state, button):
        return all(state["controls"].get(key) == (key == button) for key in ACTION_BUTTONS.values())

    def neutral_lines(self):
        return ZERO + (["btn " + self.action_button + " up"] if self.action_button else [])

    def check_owner_record(self):
        if not hasattr(self, "navigation_owner_record"):
            return
        path = self.run / "live-driver-owner.json"
        try:
            require((t.read_bytes(path, 8192) if path.exists() else None) == self.navigation_owner_record,
                    "navigation command owner record changed")
        except t.ReadDeadlineExceeded:
            raise
        except BaseException:
            self.command_fault = self.unsafe_owner = True
            raise

    def save_owner(self, pending, **kwargs):
        self.check_owner_record()
        if not pending and not self.pending_command and self.pending_sticks is not None:
            # Shared send installs its committed state before this call. A
            # mismatch leaves custody pending even if archival later fails.
            state = self.read_state()
            self.ownership(state)
            require([state["controls"][k] for k in ("stickL", "stickR")] == self.pending_sticks,
                    "acknowledged stick state differs from requested values")
            require(self.buttons_match(state, self.pending_action_button),
                    "acknowledged action button state differs from requested values")
            self.sticks = copy.deepcopy(self.pending_sticks)
            self.action_button = self.pending_action_button
            self.pending_sticks = None
            self.pending_action_button = None
            self.state = state
        result = super().save_owner(pending, **kwargs)
        self.navigation_owner_record = self.owner_bytes(pending)
        return result

    def snapshot_readiness_scope(self):
        # Observation is never a license to extend a held pulse or its lease.
        return None

    def snapshot_usable(self, snapshot, now):
        if "navigationObservation" in snapshot:
            if navigation_gather_pending(snapshot):
                # BeginGather revokes the old publication before its successor
                # is ready. The shared receiver must discard these bytes and
                # run the neutral refresh gate, never admit their old identity.
                return False
            # An explicit native refusal cannot be hidden by a still-valid old
            # firearm publication. Only this adapter admits the independent proof.
            # Validate original publication, then let the shared receive path
            # refresh expired delivery without admitting stale command evidence.
            self.admit_snapshot(snapshot, t.integer(snapshot["observed_ns"]),
                                t.integer(snapshot["nonce"]))
            return self.new_navigation_input(snapshot["navigationObservation"])
        return super().snapshot_usable(snapshot, now)

    def new_navigation_input(self, current):
        before = self.previous_navigation
        return before is None or (
            t.integer(current["gatherSequence"]) > t.integer(before["gatherSequence"])
            and t.integer(current["input"]["generation"]) > t.integer(before["input"]["generation"])
            and t.integer(current["inputObservedNs"]) >= self.last_write)

    def defer_invalid_snapshot(self, snapshot, now):
        if "navigationObservation" not in snapshot:
            return super().defer_invalid_snapshot(snapshot, now)
        # The shared receiver retained and checked this exact response, and
        # checked writer, nonce and the original response deadline. Neither a
        # pending gather nor input predating a neutral ACK supplies authority.
        pending = navigation_gather_pending(snapshot)
        if pending:
            require(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1"
                    and type(snapshot["valid"]) is bool
                    and 0 < t.integer(snapshot["observed_ns"]) <= now,
                    "pending navigation snapshot admission refused")
            nav = snapshot["navigationObservation"]
            owner = nav["owner"]
            identity = [t.integer(owner[key]) for key in NAVIGATION_OWNER]
            before = self.navigation_reference
            if before is not None:
                need_observation("navigationObservation" in before, "navigation observation route changed")
                need_observation(owner == before["navigationObservation"]["owner"],
                                 "navigation actor/control route lifetime changed")
            # Before the first publication, native serialization has an empty
            # identity. It cannot establish a reference: only a later fully
            # admitted fresh frame can do that. A retained identity still may
            # not contradict the on-foot/native gates.
            uninitialized = before is None and all(value == 0 for value in identity)
            need_observation(uninitialized or (all(value > 0 for value in identity)
                             and owner["controlled"] == owner["soldier"]
                             and nav["onFoot"] is True and nav["nativePlaying"] is True
                             and nav["allowedRead"] is True
                             and 0 <= t.integer(nav["allowedMask"]) <= 0xffffffff
                             and t.integer(nav["allowedMask"]) & 0x103 == 0x103),
                             "pending navigation actor/control route unavailable")
        else:
            require(self.previous_navigation is not None
                    and not self.new_navigation_input(snapshot["navigationObservation"]),
                    "navigation input refresh requires an unready original sample")
        require(self.entry_admitted and self.action_deadline is None and self.pending_sticks is None
                and not self.pending_command and not any(any(v) for v in self.sticks)
                and self.action_button is None and self.pending_action_button is None
                and not self.releasing_sticks and not self.cleanup_mode
                and not self.command_fault and not self.unsafe_owner,
                "navigation input refresh requires settled neutral ownership")
        self.pump()  # Rejoin live command owner, neutral controls, focus and tracking.
        if self.snapshot_refresh_started is None:
            self.snapshot_refresh_started = now
            self.snapshot_refresh_requests = 0
        self.bounds(time.perf_counter_ns())
        need_observation(self.snapshot_refresh_requests < t.MAX_SNAPSHOT_REFRESHES,
                         "navigation pulse requires a new original native input sample: refresh budget exhausted")
        self.snapshot_refresh_requests += 1
        self.artifacts.event(dict(type="snapshot_refresh_hold", nonce=snapshot["nonce"],
            evaluatedNs=now, sourceDeadlineNs=None if pending else self.snapshot_source_deadline(snapshot),
            refreshRequest=self.snapshot_refresh_requests, lastCommandWrittenNs=self.last_write,
            reason="navigation_gather_pending" if pending else "navigation_input_not_new", commandAuthority=False))
        self.pending_nonce = None
        self.latest_snapshot = None
        self.pause_without_snapshot_until(self.last_request + t.SNAPSHOT_PERIOD)
        self.request_snapshot()
        return True

    def snapshot_source_deadline(self, snapshot):
        if "navigationObservation" in snapshot:
            nav = snapshot["navigationObservation"]
            return min(t.integer(nav["validUntilNs"]), t.integer(nav["inputDeadlineNs"]),
                       t.integer(nav["inputObservedNs"]) + t.SNAPSHOT_PERIOD,
                       t.integer(nav["input"]["menu"]["deadlineNs"]))
        deadline = min(super().snapshot_source_deadline(snapshot),
                       t.integer(snapshot["native_deadline_ns"]),
                       t.integer(snapshot["input"]["menu"]["deadlineNs"]))
        if (snapshot["weapon_actions_blocked"] is True
                and (snapshot.get("bodyHolster") or {}).get("actualEmpty") is True):
            deadline = min(deadline, t.integer(snapshot["bodyHolster"]["validUntilNs"]))
        return deadline

    def admit_snapshot(self, snapshot, now, nonce):
        native = "navigationObservation" in snapshot
        if native:
            admit_navigation_observation(snapshot, now, nonce)
        else:
            p.admit_frame(snapshot, now, nonce, mechanism_admission=empty_movement_admission)
        inp = navigation_input(snapshot)
        menu = inp["menu"]
        # Gameplay=1 is the native menu owner. The independent observation
        # supplies explicit native on-foot/Playing proof. Legacy snapshots use
        # their original body/shot lease, revoked at those same native gates.
        need_observation(menu["owner"] == 1 and t.integer(menu["sequence"]) > 0
                         and t.integer(menu["epoch"]) > 0
                         and now < t.integer(menu["deadlineNs"]) <= now + 100_000_000,
                         "fresh native Playing menu ownership unavailable")
        need_observation(all(h["held"] == h["squeeze"] == h["trigger"] == 0 for h in inp["hands"]),
                         "navigation requires neutral native buttons/triggers/grips")
        before = self.navigation_reference
        if before is not None:
            need_observation(native == ("navigationObservation" in before), "navigation observation route changed")
            if native:
                need_observation(snapshot["navigationObservation"]["owner"] == before["navigationObservation"]["owner"],
                                 "navigation actor/control route lifetime changed")
            else:
                need_observation(snapshot["owner"] == before["owner"]
                                 and snapshot["physical_equipment_generation"] == before["physical_equipment_generation"],
                                 "navigation actor/equipment lifetime changed")
            previous_input = navigation_input(before)
            need_observation(all(inp[k] == previous_input[k] for k in
                                 ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative", "referenceHead"))
                             and menu["epoch"] == previous_input["menu"]["epoch"]
                             and inp["head"]["position"] == previous_input["head"]["position"],
                             "navigation tracking/head/menu epoch changed")
            need_observation(all(p.preserved_pose_equal(h[k], old[k])
                                 for h, old in zip(inp["hands"], previous_input["hands"])
                                 for k in ("grip", "aim")), "native controller pose changed")
        require(now < self.snapshot_source_deadline(snapshot), "original navigation source lease expired")

    def bounds(self, now):
        super().bounds(now)
        if self.releasing_sticks and not self.cleanup_mode and not self.pending_command:
            # Shared _send calls this with its exact command-request timestamp,
            # before any owner/command write. Preparation may not clip the full
            # normal zero window, even if an OS read or scheduler stalls.
            require(now + COMMAND_WINDOW <= self.action_deadline,
                    "insufficient original pulse budget for full zero publication/ACK window")

    def pump(self):
        deadline = min(self.deadline, self.stage_deadline)
        if self.action_deadline is not None:
            deadline = min(deadline, self.action_deadline)
        return t.read_until(deadline, self._pump)

    def _pump(self):
        now = time.perf_counter_ns()
        self.bounds(now)
        if self.action_deadline is not None:
            need_observation(now < self.action_deadline, "original navigation pulse deadline exceeded")
        state = self.read_state()
        self.check_owner_record()
        if self.pending_command:
            try:
                require(t.digest(t.read_bytes(self.command_path, 4096)) == self.command_hash,
                        "concurrent navigation command writer")
                require(t.integer(state["cmdSeq"]) in (self.sequence-len(self.pending_batch), self.sequence),
                        "navigation pending sequence changed")
            except t.ReadDeadlineExceeded:
                raise
            except BaseException:
                self.command_fault = self.unsafe_owner = True
                raise
        else:
            self.ownership(state)
        if not self.releasing_sticks:
            self.check_observation_state(state)
        # No speculative native diagnostic work belongs in the command/ACK or
        # hold path. A request started here can spend its entire response budget
        # while normal zero is committing, then poison fresh() for the next
        # pulse. XR produces fresh packets while held; native GatherInput keeps
        # its independent original input deadline and Playing/tracking gates.
        # The next pulse explicitly requests a new source after committed zero.

    def validate_publication(self, lines, expected, cleanup):
        require(lines == (self.neutral_lines() if cleanup else self.authorized_lines),
                "unreviewed navigation command")
        require(not cleanup or self.release_attempted, "unowned navigation release")
        require(not self.releasing_sticks or lines == self.neutral_lines(),
                "release must contain only numeric stick zeros and the exact owned button up")
        if not cleanup and not self.releasing_sticks:
            require(self.pending_nonce is None, "navigation publication requires completed native admission")
            self.check_observation_state(self.receive_timing_call("navigation_publication_state_read", self.read_state))
            self.admit_snapshot(self.latest_snapshot, time.perf_counter_ns(),
                                t.integer(self.latest_snapshot["nonce"]))

    def pause_until(self, deadline):
        if not self.releasing_sticks:
            return t.read_until(min(self.deadline, self.stage_deadline), super().pause_until, deadline)
        if not self.cleanup_mode:
            return t.read_until(self.action_deadline - COMMAND_WINDOW, self.pause_without_snapshot_until, deadline)
        return self.pause_without_snapshot_until(deadline)

    def pause_without_snapshot_until(self, deadline):
        # Exact owned zeros and neutral refresh spacing need no diagnostic.
        # Pump preserves ownership and all existing stage/invocation bounds.
        for _ in range(6000):
            self.pump()
            if time.perf_counter_ns() >= deadline:
                return
            time.sleep(.001)
        raise Refusal("bounded release polling iteration limit")

    def read_publication_owner(self, expected_bytes, deadline_ns):
        raw = super().read_publication_owner(expected_bytes, deadline_ns)
        if not self.cleanup_mode and not self.releasing_sticks:
            # Repeat readiness immediately at every shared atomic publication
            # attempt. These failures do not alone establish writer ambiguity.
            self.check_observation_state(self.read_state())
            self.admit_snapshot(self.latest_snapshot, time.perf_counter_ns(),
                                t.integer(self.latest_snapshot["nonce"]))
        return raw

    def publish_navigation(self, lines, sticks, *, button=None, cleanup=False, release=False):
        require(not self.releasing_sticks, "nested navigation release")
        require(button is None or button in ACTION_BUTTONS.values(), "unreviewed navigation button")
        if release or cleanup:
            require(lines == self.neutral_lines() and sticks == [[0, 0], [0, 0]]
                    and button is None and self.entry_admitted
                    and not self.pending_command and not self.command_fault and not self.unsafe_owner,
                    "navigation release requires exact settled command ownership")
        self.releasing_sticks = release or cleanup
        self.authorized_lines = lines[:]
        self.pending_sticks = copy.deepcopy(sticks)
        self.pending_action_button = button
        try:
            result = super().send(lines, cleanup=cleanup,
                source_deadline=None if self.releasing_sticks else self.snapshot_source_deadline(self.latest_snapshot))
            require(self.pending_sticks is None and self.sticks == sticks and self.action_button == button,
                    "navigation custody did not settle")
            return result
        except BaseException as error:
            # Shared ACK guards can observe ambiguity after our last pump.
            # Remember that observed loss even if a later file looks restored.
            if any(text in str(error) for text in ("concurrent command writer", "unique command sentinel mismatch",
                    "unexpected command sequence advancement", "acknowledgement batch text mismatch",
                    "command owner record replaced")):
                self.command_fault = self.unsafe_owner = True
            # Before publication a target may have been proposed without any
            # side effect. Retain it until cleanup rejoins exact command state.
            raise
        finally:
            self.authorized_lines = None
            self.releasing_sticks = False

    def navigate(self, rows):
        self.navigation_schedule = pulse_schedule(rows)
        self.artifacts.save("navigation-plan.json", self.navigation_schedule)
        self.fresh()
        self.navigation_reference = copy.deepcopy(self.latest_snapshot)
        self.previous_navigation = self.latest_snapshot.get("navigationObservation")
        require(all(h["stickX"] == h["stickY"] == 0 for h in navigation_input(self.latest_snapshot)["hands"]),
                "native entry sticks are not neutral")
        for row in self.navigation_schedule:
            require(self.pending_sticks is None and not self.pending_command
                    and not any(any(v) for v in self.sticks) and self.action_button is None,
                    "previous navigation episode lacks committed neutral")
            # Obtain a new original source before each episode, never restamp.
            self.fresh()
            if self.previous_navigation is not None:
                current = self.latest_snapshot["navigationObservation"]
                need_observation(self.new_navigation_input(current),
                                 "navigation pulse requires a new original native input sample")
                self.previous_navigation = current
            require(all(h["stickX"] == h["stickY"] == 0 for h in navigation_input(self.latest_snapshot)["hands"]),
                    "native continuation sticks are not neutral")
            admission_nonce = self.latest_snapshot["nonce"]
            lines = []
            if "lookDegrees" in row:
                yaw, pitch, roll = self.expected_head["ypr"]
                yaw += row["lookDegrees"][0]
                pitch += row["lookDegrees"][1]
                require(abs(yaw) <= 360 and abs(pitch) <= 80, "head orientation exceeds navigation bound")
                self.next_head = copy.deepcopy(self.expected_head)
                self.next_head["ypr"] = [yaw, pitch, roll]
                values = self.next_head["pos"] + self.next_head["ypr"]
                lines.append("head pose " + " ".join(str(v) for v in values))
            else:
                self.next_head = copy.deepcopy(self.expected_head)
            sticks = [row["left"], row["right"]]
            lines += ["stick " + hand + " " + " ".join(str(v) for v in values)
                      for hand, values in zip(("l", "r"), sticks)]
            button = ACTION_BUTTONS.get(row.get("action"))
            if button is not None:
                lines.append("btn " + button + " down")
            started = time.perf_counter_ns()
            previous_stage_deadline = self.stage_deadline
            self.action_deadline = min(previous_stage_deadline, self.deadline-300_000_000,
                                       started + row["holdMs"]*1_000_000)
            active_deadline = self.action_deadline - RELEASE_RESERVE
            require(started < active_deadline, "pulse has no active budget after full zero publication/ACK reserve")
            # An initial publication/ACK or diagnostic refresh cannot consume
            # the reserved zero window. A late initial ACK stays a failure and
            # uses the existing exact-owner cleanup path.
            self.stage_deadline = active_deadline
            self.motion_started = started
            self.publish_navigation(lines, sticks, button=button)
            command_committed = self.last_commit
            self.expected_head = self.next_head
            self.stage_deadline = self.action_deadline
            # A last hold pump gets at most one 25 ms interval; release
            # preparation gets the next. Neither can spend the 250 ms shared
            # command window or wait out a pending diagnostic refresh.
            while (now := time.perf_counter_ns()) < active_deadline:
                t.read_until(min(now + PERIOD, active_deadline + PERIOD), self.pump)
                remaining = active_deadline - time.perf_counter_ns()
                if remaining > 0:
                    time.sleep(min(.001, remaining / t.NS))
            # Exact owned zeros retire this pulse; they do not need a new
            # native observation. Keep the original normal pulse/ACK bounds
            # (cleanup=False), including when a diagnostic is still pending.
            active_finished = time.perf_counter_ns()
            self.publish_navigation(self.neutral_lines(), [[0, 0], [0, 0]], release=True)
            require(neutral_controls(self.state["controls"]), "zero ACK lacks neutral committed controls")
            self.action_deadline = None
            self.stage_deadline = previous_stage_deadline
            self.episodes.append(dict(rowIndex=row["rowIndex"], subpulseIndex=row["subpulseIndex"],
                                      subpulseCount=row["subpulseCount"], plannedDurationMs=row["holdMs"],
                                      plannedActiveMs=row["holdMs"]-RELEASE_RESERVE//1_000_000,
                                      admissionNonce=admission_nonce, startedNs=started,
                                      commandCommittedNs=command_committed, neutralCommittedNs=self.last_commit,
                                      acknowledgedActiveNs=active_finished-command_committed,
                                      completedElapsedNs=self.last_commit-started,
                                      action=row.get("action"), commandsAcknowledged=True,
                                      nativeAction=False, nativeMovement=False, objectiveEntry=False,
                                      vehicleEntry=False))
        self.monitor = False
        self.save_owner(False)

    def release_after_failure(self):
        if self.release_attempted:
            return "not retried: normal Stop required"
        self.release_attempted = True
        if not self.entry_admitted:
            return "not attempted: neutral navigation entry was not admitted"
        self.cleanup_mode = True
        self.monitor = False
        self.action_deadline = None
        self.snapshot_readiness = None
        trace = getattr(self, "_receive_timing", None)
        if trace is not None:
            trace.freeze(self.pending_command, self.sequence)
        try:
            require(not self.command_fault, "command ownership was lost")
            self.check_owner_record()
            if self.pending_command:
                # This read-only settlement does not extend the failed action;
                # it proves the exact command before a release-only attempt.
                self.settle_pending_for_release()
            require(not self.pending_command, "pending navigation command could not be settled")
            state = self.read_state()
            self.ownership(state)
            self.unsafe_owner = False  # exact ownership independently rejoined
            require(self.buttons_match(state, self.action_button),
                    "owned navigation action button differs from settled state")
            self.sticks = [state["controls"][k][:] for k in ("stickL", "stickR")]
            self.pending_sticks = None
            self.pending_action_button = None
            if self.last_write == 0 and not any(any(v) for v in self.sticks) and self.action_button is None:
                self.save_owner(False)
                return "no navigation input published"
            require(time.perf_counter_ns()+250_000_000 < self.hard_deadline,
                    "bounded release reserve exhausted")
            self.publish_navigation(self.neutral_lines(), [[0, 0], [0, 0]], cleanup=True)
            require(neutral_controls(self.state["controls"]), "release ACK lacks neutral committed controls")
            return "ordinary navigation release ACK and neutral committed state verified"
        except BaseException:
            self.normal_stop_required = True
            raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--navigation", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-sha256", required=True)
    parser.add_argument("--root-authorized-ordinary-navigation", action="store_true")
    args = parser.parse_args(argv)
    driver, code = None, 2
    result = dict(schema="bc2vr.ordinary-navigation.v1", status="refused", nativeAcceptance=False,
                  headsetAcceptance=False, objectiveEntry=False, vehicleEntry=False, normalStopRequired=False)
    try:
        require(args.root_authorized_ordinary_navigation, "explicit ROOT ordinary-navigation authorization required")
        require(re.fullmatch(r"[a-f0-9]{64}", args.source_sha256) is not None, "invalid reviewed source SHA256")
        rows, raw = t.read_json(t.child(t.local_path(args.run), args.navigation), 16384)
        plan(rows)
        result["requestedDurationMs"] = sum(row["holdMs"] for row in rows)
        driver = NavigationDriver.__new__(NavigationDriver)
        driver.__init__(args)
        driver.artifacts.save("navigation-request.json", raw)
        driver.navigate(rows)
        result["status"], code = "navigation_commands_acknowledged_and_neutral", 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except (Exception, KeyboardInterrupt) as release_error:
                result["releaseError"] = str(release_error)
                result["normalStopRequired"] = True
                result["nextAction"] = "ROOT must request the existing session's normal Stop; no further input"
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update(episodes=driver.episodes, pendingCommand=driver.pending_command,
                              sticks=driver.sticks, pendingSticks=driver.pending_sticks,
                              actionButton=driver.action_button, pendingActionButton=driver.pending_action_button,
                              plannedSubpulses=len(driver.navigation_schedule),
                              plannedDurationMs=sum(row["holdMs"] for row in driver.navigation_schedule),
                              completedElapsedNs=sum(row["completedElapsedNs"] for row in driver.episodes),
                              acknowledgedActiveNs=sum(row["acknowledgedActiveNs"] for row in driver.episodes),
                              artifactDirectory=str(driver.artifacts.directory))
                try:
                    driver.artifacts.save("navigation-result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"], code = str(error), 2
            try:
                driver.close()
            except Exception as error:
                result["closeError"], code = str(error), 2
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
