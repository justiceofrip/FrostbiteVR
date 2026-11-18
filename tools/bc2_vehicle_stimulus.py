"""One bounded vehicle pulse adapter for drive_bc2_feed_simulation.py.

--vehicle-stimulus FILE: {"role":"driver","stick":[0.4,0.5],"holdMs":400}
or {"role":"gunner","stick":[0.4,0],"holdMs":400}. Driver uses left stick;
gunner uses right stick. holdMs (301..500) includes both acknowledgements and
a 300 ms reserve for numeric zero. At most one pulse, no automatic retry.
Optional diagnostic: {"mode":"sustained","role":"driver","stick":[0.4,0.5],
"positiveHoldMs":2000}. This budgets 1..2000 ms for positive publication/ACK
and holding, PLUS the same 300 ms zero/ACK reserve. It continuously requires
fresh same-owner native observations; an authority gap cancels the hold.
Neither mode guarantees a native hold duration or authorizes a retry.
Requires --root-authorized-ordinary-input, --vehicle-version and
--vehicle-source-sha256. No fire, exit, seat switch, pose, or button commands.
Simulator ACKs are not evidence of native vehicle movement or headset behavior.
"""
import copy
import json
import math
from pathlib import Path

PERIOD = 25_000_000
COMMAND_WINDOW = 250_000_000
INITIAL_OBSERVATION_WINDOW = 500_000_000
# Pending-only requests are at least one ordinary pump period apart. Exactly
# 19 fresh starts can fit strictly inside the original 500 ms window; the
# response and source leases remain independent and are never extended.
MAX_PENDING_REACQUISITIONS = (INITIAL_OBSERVATION_WINDOW - 1) // PERIOD
# BeginGather revokes a pending copy; Current reports Changed when the same
# publication turns over after a complete copy. Both withhold input entirely.
PENDING_REASONS = ("gather_pending", "observation_changed")
RELEASE_RESERVE = COMMAND_WINDOW + 2 * PERIOD
OWNER = ("manager", "player", "soldier", "weak", "controlled", "entry", "router", "cache",
         "actorGeneration", "seatGeneration", "inputEpoch", "movementEpoch")
AXES = (("throttle", "left_stick_y", 0), ("steer", "left_stick_x", 1),
        ("lookYaw", "right_stick_x", 8), ("lookPitch", "right_stick_y", 7))
PENDING_COHORT = ("owner", "routeFingerprint", "slot", "allowedMask", "allowedRead",
                  "nativePlaying", "routesVerified", "profileVerified", "consumersVerified")
PENDING_FIELDS = set(PENDING_COHORT) | {"observationOnly", "available", "reason", "gatherSequence",
    "sampledNs", "inputObservedNs", "inputDeadlineNs", "validUntilNs", "input", "capabilities", "neutralRearm"}


def neutral(controls):
    return bool(controls) and all(all(x == 0 for x in v) if isinstance(v, list) else v == 0
                                  for v in controls.values())


def plan(value, require):
    require(isinstance(value, dict), "vehicle request must be an object")
    sustained = value.get("mode") == "sustained"
    require(set(value) == ({"mode", "role", "stick", "positiveHoldMs"} if sustained
                          else {"role", "stick", "holdMs"}),
            "vehicle request needs role/stick/holdMs or explicit sustained mode/role/stick/positiveHoldMs")
    require(value["role"] in ("driver", "gunner"), "vehicle role must be driver or gunner")
    stick = value["stick"]
    require(isinstance(stick, list) and len(stick) == 2
            and all(type(x) in (int, float) and math.isfinite(x) and abs(x) <= 1 for x in stick)
            and any(abs(x) > .18 for x in stick), "vehicle stick needs a bounded nonzero axis beyond deadzone")
    if sustained:
        require(type(value["positiveHoldMs"]) is int and 1 <= value["positiveHoldMs"] <= 2000,
                "sustained vehicle positive budget must be 1..2000 ms plus full zero/ACK reserve")
    else:
        require(type(value["holdMs"]) is int and 301 <= value["holdMs"] <= 500,
                "vehicle pulse must be 301..500 ms including full zero/ACK reserve")
    return copy.deepcopy(value)


def axis(value):
    """VehicleControllerActions' existing .18 deadzone; no native override."""
    return 0 if abs(value) <= .18 else math.copysign((abs(value) - .18) / .82, value)


def axis_matches(actual, expected):
    # Native float/9-digit JSON readback only. Neutral remains exact zero.
    return actual == 0 if expected == 0 else abs(actual - expected) <= 1e-7


def hand_basis(hands, side):
    result = copy.deepcopy(hands)
    result[side]["stickX"] = result[side]["stickY"] = 0
    return result


def admit(t, snapshot, now, nonce, role, stick=None):
    """Join the actual passive serializer, including its firearm-invalid branch."""
    r, n, p = t.require, t.integer, t.planner
    r(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1"
      and type(snapshot["valid"]) is bool and n(snapshot["nonce"]) == nonce and nonce > 0,
      "vehicle snapshot schema/nonce mismatch")
    r(0 < n(snapshot["observed_ns"]) <= now, "vehicle snapshot publication clock invalid")
    v = snapshot.get("vehicleObservation")
    r(isinstance(v, dict) and v.get("observationOnly") is True and v.get("available") is True
      and v.get("reason") is None, "native vehicle observation unavailable")
    o, inp = v["owner"], v["input"]
    r(all(n(o[k]) > 0 for k in OWNER) and n(o["controlled"]) != n(o["soldier"]),
      "vehicle seated actor/entry identity unavailable")
    r(v["nativePlaying"] is True and v["allowedRead"] is True and v["routesVerified"] is True
      and v["profileVerified"] is True and v["consumersVerified"] is True
      and n(v["routeFingerprint"]) > 0 and n(v["slot"]) >= 0
      and v["role"] in (role, "driver_gunner"), "vehicle role/route/consumer/Playing proof unavailable")
    allowed = n(v["allowedMask"])
    r(0 <= allowed <= 0xffffffff, "invalid vehicle permission mask")
    menu = inp["menu"]
    observed, sampled, deadline, until = (n(v[k]) for k in
        ("inputObservedNs", "sampledNs", "inputDeadlineNs", "validUntilNs"))
    # The worker reads vehicle after recording observed_ns. A new Gather may
    # therefore be newer than that outer clock; neither clock is rewritten.
    r(n(v["gatherSequence"]) > 0 and 0 < observed <= sampled <= now < until
      and 0 < deadline - observed <= t.SNAPSHOT_PERIOD
      and until == min(deadline, n(menu["deadlineNs"])), "expired/invalid original vehicle input/menu clocks")
    r(n(menu["owner"]) == 1 and n(menu["sequence"]) > 0 and n(menu["epoch"]) > 0
      and now < n(menu["deadlineNs"]) <= now + t.SNAPSHOT_PERIOD,
      "fresh vehicle Playing menu ownership unavailable")
    r(all(n(inp[k]) > 0 for k in ("generation", "spaceGeneration", "trackingBasisGeneration", "predictedNs"))
      and inp["focused"] is True and inp["headValid"] is True, "vehicle focus/head/input generation unavailable")
    r(type(inp["floorRelative"]) is bool and .01 <= p.finite(inp["worldUnitsPerMeter"]) <= 1000,
      "invalid vehicle tracking basis")
    p.from_pose(inp["head"])
    p.from_pose(inp["referenceHead"])
    r(isinstance(inp["hands"], list) and len(inp["hands"]) == 2, "vehicle needs two tracked hands")
    side = 0 if role == "driver" else 1
    for index, hand in enumerate(inp["hands"]):
        expected = stick if stick is not None and index == side else [0, 0]
        r(hand["gripTracked"] is True and hand["aimTracked"] is True
          and all(n(hand[k]) >= 0 for k in ("active", "held", "touchActive", "touched"))
          and all(p.finite(hand[k]) == 0 for k in ("held", "trigger", "squeeze"))
          and all(axis_matches(p.finite(hand[k]), x) for k, x in zip(("stickX", "stickY"), expected)),
          "vehicle requires tracked controllers with exactly owned stick and otherwise neutral input")
        p.from_pose(hand["grip"])
        p.from_pose(hand["aim"])
    r(n(inp["hands"][side]["active"]) & 1, "requested vehicle stick action is inactive")
    rearm, consumed = v["neutralRearm"], v["consumedControls"]
    expected_axes = [0, 0, 0, 0]
    if stick is not None:
        expected_axes[side * 2:side * 2 + 2] = [axis(x) for x in (stick[::-1] if side == 0 else stick)]
    r(rearm["controlsActive"] is True and rearm["armedLeft" if side == 0 else "armedRight"] is True
      and all(axis_matches(p.finite(consumed[k]), x) for k, x in
              zip(("throttle", "steer", "lookYaw", "lookPitch"), expected_axes))
      and consumed["exitHeld"] is False, "vehicle neutral rearm/exact consumed axes unavailable")
    capabilities = v["capabilities"]
    r(capabilities["fire"] is False and isinstance(capabilities["axes"], list)
      and len(capabilities["axes"]) == 4, "unknown vehicle axis capability schema")
    consumer = n(v["consumers"]["driver" if side == 0 else "camera"])
    r(consumer > 0 and (side == 0 or n(v["consumers"]["weaponCount"]) > 0),
      "requested vehicle axis consumer unknown")
    for index in (range(2) if side == 0 else range(2, 4)):
        capability, (semantic, source, concept) = capabilities["axes"][index], AXES[index]
        mapping = capability["mapping"]
        r(capability["semantic"] == semantic and capability["available"] is True and capability["reason"] is None
          and isinstance(mapping, dict) and mapping["source"] == source
          and n(mapping["concept"]) == concept and n(mapping["consumer"]) == consumer
          and 0 <= n(mapping["action"]) <= 10 and allowed & (1 << n(mapping["action"])),
          "requested vehicle axis mapping/consumer/permission unavailable")
    return v


def driver_class(t):
    """Bind to the existing loaded shared driver; do not create another transport."""
    if isinstance(t, dict):
        namespace = t
        class SharedNamespace:
            def __getattr__(self, key):
                return namespace[key]
        t = SharedNamespace()
    require, n = t.require, t.integer

    class VehicleDriver(t.Driver):
        def __init__(self, args):
            self.vehicle_reference = None
            self.vehicle_request = None
            self.stick = [0, 0]
            self.pending_stick = None
            self.pending_target = None
            self.release_attempted = False
            self.releasing = self.cleanup_mode = self.entry_admitted = False
            self.normal_stop_required = False
            self.action_deadline = None
            self.authorized_lines = None
            self.attempted = False
            self.initial_observation_waited = False
            self.initial_discarded_snapshot = None
            self.pending_observation = None
            self.pending_observation_requests = 0
            self.sustained_monitor = False
            self.sustained_reference = None
            self.sustained_observations = 0
            super().__init__(args)
            require(self.manifest["version"] == args.vehicle_version
                    and self.manifest["source_sha256"] == args.vehicle_source_sha256,
                    "ROOT-reviewed vehicle source/version differs from current session")
            require(self.link["mechanism_snapshots"]["requested"] is True
                    and t.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                    "vehicle observation must be enabled before native startup")
            self.baseline_sim = copy.deepcopy(self.state)
            self.check_input(self.state)
            require(neutral(self.state["controls"]), "vehicle requires neutral simulator entry")
            require(all(self.state[k]["followsHead"] is False for k in ("handL", "handR")),
                    "vehicle controllers must already have head-follow disabled")
            owner_path = self.run / "live-driver-owner.json"
            self.vehicle_owner_record = t.read_bytes(owner_path, 8192) if owner_path.exists() else None
            self.entry_admitted = True

        def read_state(self):
            # Gameplay/focus loss can still release an exactly owned stick.
            # Host/session/command ownership loss never grants cleanup authority.
            self.owner.check()
            session, _ = t.read_json(self.session_path)
            require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                    and session["source_sha256"] == self.manifest["source_sha256"]
                    and session["version"] == self.manifest["version"] and session["state"] == "running",
                    "vehicle session ownership/source/version changed")
            require(not (self.folder / "stop.request").exists(), "normal session stop requested")
            state, _ = t.read_until(self.hard_deadline, t.read_json, self.state_path, 16384)
            require(n(state["pid"]) == n(self.host["pid"]) and state["runtime"] == "xr-sim"
                    and n(state["errors"]) == 0 and state["lastCmdError"] == "",
                    "vehicle simulator identity/error changed")
            require(n(state["captureSeq"]) <= n(self.capture_plan["maximum_compositions"]),
                    "vehicle capture budget changed")
            return state

        def check_input(self, state):
            require(state["sessionRunning"] is True and state["actionsAttached"] is True
                    and state["sessionState"] == "FOCUSED" and state["paceMode"] == "free"
                    and all(state[k]["valid"] is True for k in ("head", "handL", "handR")),
                    "vehicle simulator focus/tracking lost")
            self.check_controls(state)
            require(all(state[k] == self.baseline_sim[k] for k in ("head", "handL", "handR")),
                    "vehicle simulator tracked poses changed")

        def check_controls(self, state):
            key = self.stick_key() if self.vehicle_request else None
            require(set(state["controls"]) == set(self.baseline_sim["controls"])
                    and all(value == self.baseline_sim["controls"][k]
                            for k, value in state["controls"].items() if k != key),
                    "unowned vehicle controller input changed")
            if key is not None:
                allowed = [self.stick]
                if self.pending_command and self.pending_stick is not None:
                    allowed.append(self.pending_stick)
                require(state["controls"][key] in allowed, "owned vehicle stick differs from exact command")

        def stick_key(self):
            return "stickL" if self.vehicle_request["role"] == "driver" else "stickR"

        def line(self, values):
            return "stick " + ("l" if self.vehicle_request["role"] == "driver" else "r") + " " + " ".join(map(str, values))

        def owner_bytes(self, pending):
            value = json.loads(super().owner_bytes(pending))
            value["pending"] = pending or self.pending_stick is not None or any(self.stick)
            value["vehicleStimulus"] = dict(stick=self.stick, pendingStick=self.pending_stick,
                hand=self.stick_key() if self.vehicle_request else None, neutralCommitted=not value["pending"])
            return t.json_bytes(value)

        def check_owner_record(self):
            if not hasattr(self, "vehicle_owner_record"):
                return
            path = self.run / "live-driver-owner.json"
            try:
                require((t.read_bytes(path, 8192) if path.exists() else None) == self.vehicle_owner_record,
                        "vehicle command owner record changed")
            except t.ReadDeadlineExceeded:
                raise
            except BaseException:
                self.unsafe_owner = True
                raise

        def save_owner(self, pending, **kwargs):
            self.check_owner_record()
            if not pending and not self.pending_command and self.pending_stick is not None:
                state = self.read_state()
                self.ownership(state)
                if self.sequence == self.pending_target:
                    require(state["controls"][self.stick_key()] == self.pending_stick,
                            "vehicle ACK does not prove requested stick")
                    self.stick = self.pending_stick[:]
                else:
                    require(self.sequence == self.pending_target - 2
                            and state["controls"][self.stick_key()] == self.stick,
                            "unpublished vehicle command cannot restore prior owner")
                self.pending_stick = self.pending_target = None
                self.state = state
            result = super().save_owner(pending, **kwargs)
            self.vehicle_owner_record = self.owner_bytes(pending)
            return result

        def snapshot_readiness_scope(self):
            return None

        def neutral_preflight(self):
            # attempted marks method entry, not input publication.
            return not (not self.entry_admitted or self.vehicle_request is None
                    or self.motion_started is not None
                    or self.last_write != 0 or self.commands != 0
                    or self.action_deadline is not None or self.pending_command
                    or self.pending_stick is not None or self.pending_target is not None
                    or any(self.stick) or self.unsafe_owner or self.cleanup_mode
                    or self.releasing or self.release_attempted
                    or self.held_left or self.held_right or self.held_trigger or self.held_buttons
                    or self.snapshot_refresh_started is not None)

        def snapshot_timeout(self, now, response=None):
            # Pending-Gather and late-response recovery share one allowance;
            # neither can chain into the other or retry an attempted pulse.
            if self.initial_observation_waited or not self.neutral_preflight():
                return super().snapshot_timeout(now, response)
            self.initial_observation_waited = True
            wait_deadline = min(self.deadline, self.stage_deadline,
                                self.last_request + INITIAL_OBSERVATION_WINDOW)
            identity = (self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit)
            trace = getattr(self, "_receive_timing", None)
            if trace is not None and not trace.frozen:
                trace.timeout_evaluated_ns = now
            if response is not None:
                self.artifacts.save(f"snapshot-{self.snapshot_index:03}-at-timeout.json", response[1])
            timeout_response_saved = response is not None
            self.artifacts.event(dict(type="vehicle_initial_observation_hold",
                nonce=self.pending_nonce, requestStartedNs=self.last_request,
                originalResponseDeadlineNs=self.last_request + t.SNAPSHOT_TIMEOUT,
                observationDeadlineNs=wait_deadline, evaluatedNs=now, commandAuthority=False))

            def check_neutral_owner():
                self.bounds(t.time.perf_counter_ns())
                self.check_owner_record()
                state = t.read_until(wait_deadline, self.read_state)
                self.ownership(state)
                self.check_input(state)
                require((self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit) == identity
                        and n(state["captureSeq"]) == self.capture_sequence and neutral(state["controls"]),
                        "initial vehicle observation lost neutral command ownership")
                require(t.read_until(wait_deadline, t.read_bytes, self.request_path, 22) == self.request_bytes,
                        "snapshot request changed by another writer")

            while t.time.perf_counter_ns() < wait_deadline:
                check_neutral_owner()
                require(t.read_until(wait_deadline, self.snapshot_failure) is None,
                        "native snapshot publication failed during initial vehicle observation")
                if self.snapshot_path.exists():
                    snapshot, raw = t.read_until(wait_deadline, self.receive_snapshot_json)
                    if not timeout_response_saved:
                        self.artifacts.save(f"snapshot-{self.snapshot_index:03}-at-timeout.json", raw)
                        timeout_response_saved = True
                    nonce = n(snapshot["nonce"])
                    self.last_observed_response_nonce = nonce
                    require(nonce <= self.pending_nonce,
                            "snapshot response belongs to a later competing request")
                    if nonce == self.pending_nonce:
                        self.artifacts.save(f"snapshot-{self.snapshot_index:03}-late.json", raw)
                        observed = n(snapshot["observed_ns"])
                        # Vehicle sampling may follow the outer serializer clock.
                        # Validate original clocks only as discarded evidence;
                        # current-time admission remains mandatory for a successor.
                        sampled = max(observed, n(snapshot["vehicleObservation"]["sampledNs"]))
                        require(self.last_request <= observed <= sampled <= t.time.perf_counter_ns(),
                                "initial vehicle response predates request or is from the future")
                        self.check_vehicle_snapshot(snapshot, sampled, nonce)
                        check_neutral_owner()
                        require(t.time.perf_counter_ns() < wait_deadline,
                                "initial vehicle observation deadline reached")
                        self.artifacts.event(dict(type="vehicle_initial_late_response_discarded",
                            nonce=nonce, observedNs=observed, observationDeadlineNs=wait_deadline,
                            commandAuthority=False))
                        require(t.time.perf_counter_ns() < wait_deadline,
                                "initial vehicle observation deadline reached")
                        self.initial_discarded_snapshot = copy.deepcopy(snapshot)
                        self.latest_snapshot = None
                        self.pending_nonce = None
                        self.request_snapshot()
                        return None
                t.time.sleep(.001)
            raise t.REFUSAL("initial vehicle observation deadline reached without a matching response")

        def accept_snapshot(self, snapshot, raw, now):
            result = super().accept_snapshot(snapshot, raw, now)
            self.initial_discarded_snapshot = None
            if self.sustained_monitor:
                self.sustained_reference = copy.deepcopy(snapshot)
                self.sustained_observations += 1
                self.artifacts.event(dict(type="vehicle_sustained_observation", nonce=snapshot["nonce"],
                    evaluatedNs=now, sourceDeadlineNs=self.snapshot_source_deadline(snapshot)))
            return result

        def snapshot_usable(self, snapshot, now):
            if self.pending_observation is not None:
                self.check_pending_observation_owner()
            value = snapshot.get("vehicleObservation")
            if isinstance(value, dict) and value.get("available") is False and value.get("reason") in PENDING_REASONS:
                return False
            # Every other refusal still fails before shared receive's optional
            # expired-source refresh. No unavailable sample grants authority.
            self.admit_snapshot(snapshot, now, n(snapshot["nonce"]))
            return True

        def check_pending_cohort(self, snapshot, before, *, available):
            value, old = snapshot["vehicleObservation"], before["vehicleObservation"]
            require(n(snapshot["nonce"]) > n(before["nonce"]), "reused vehicle snapshot nonce")
            require(all(value[k] == old[k] for k in PENDING_COHORT),
                    "pending vehicle owner/seat/route/permission cohort changed")
            require(n(value["gatherSequence"]) >= n(old["gatherSequence"])
                    and n(value["inputObservedNs"]) >= n(old["inputObservedNs"])
                    and n(value["sampledNs"]) >= n(old["sampledNs"]),
                    "pending vehicle Gather/input observation regressed")
            if available:
                require(n(value["gatherSequence"]) > n(old["gatherSequence"])
                        and n(value["inputObservedNs"]) > n(old["inputObservedNs"]),
                        "reused pending vehicle Gather/input observation")
            elif n(value["gatherSequence"]) == n(old["gatherSequence"]):
                require(all(value[k] == old[k] for k in
                    ("sampledNs", "inputObservedNs", "inputDeadlineNs", "validUntilNs")),
                    "original pending vehicle clocks changed")

        def check_pending_observation_owner(self):
            observation = self.pending_observation
            self.bounds(t.time.perf_counter_ns())
            self.check_owner_record()
            state = t.read_until(observation["deadlineNs"], self.read_state)
            self.ownership(state)
            self.check_input(state)
            require(n(state["captureSeq"]) == self.capture_sequence and neutral(state["controls"]),
                    "pending vehicle observation lost neutral command ownership")
            require(t.read_until(observation["deadlineNs"], t.read_bytes, self.request_path, 22) == self.request_bytes,
                    "snapshot request changed by another writer")
            self.bounds(t.time.perf_counter_ns())

        def defer_invalid_snapshot(self, snapshot, now):
            # The native serializer withholds role, consumers and all input on
            # revocation. Retained labels constrain a successor, never admit it.
            require(self.neutral_preflight() and (self.pending_observation is not None
                    or not self.initial_observation_waited), "pending vehicle recovery requires untouched neutral preflight")
            value = snapshot["vehicleObservation"]
            require(set(value) == PENDING_FIELDS and value["observationOnly"] is True
                    and value["available"] is False and value["reason"] in PENDING_REASONS
                    and all(value[k] is None for k in ("input", "capabilities", "neutralRearm")),
                    "unsupported pending vehicle observation shape")
            require(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1"
                    and type(snapshot["valid"]) is bool and n(snapshot["nonce"]) == self.pending_nonce
                    and self.last_request <= n(snapshot["observed_ns"]) <= now,
                    "pending vehicle snapshot schema/nonce/publication clock invalid")
            owner = value["owner"]
            require(set(owner) == set(OWNER) and all(n(owner[k]) > 0 for k in OWNER)
                    and n(owner["controlled"]) != n(owner["soldier"])
                    and all(value[k] is True for k in PENDING_COHORT[4:])
                    and n(value["routeFingerprint"]) > 0 and n(value["slot"]) >= 0
                    and 0 < n(value["allowedMask"]) <= 0xffffffff,
                    "pending vehicle seated owner/route/consumer proof unavailable")
            observed, sampled, deadline, until = (n(value[k]) for k in
                ("inputObservedNs", "sampledNs", "inputDeadlineNs", "validUntilNs"))
            require(n(value["gatherSequence"]) > 0 and 0 < observed <= sampled <= now < until <= deadline
                    and 0 < deadline - observed <= t.SNAPSHOT_PERIOD,
                    "expired/invalid original pending vehicle clocks")
            if self.vehicle_reference is not None:
                self.check_pending_cohort(snapshot, self.vehicle_reference, available=False)
            if self.pending_observation is None:
                self.initial_observation_waited = True
                self.pending_observation = dict(deadlineNs=min(self.deadline, self.stage_deadline,
                    self.last_request + INITIAL_OBSERVATION_WINDOW),
                    commandIdentity=(self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit),
                    reference=copy.deepcopy(snapshot))
            else:
                self.check_pending_cohort(snapshot, self.pending_observation["reference"], available=False)
            self.check_pending_observation_owner()
            # Rapid pending publications must not burn a small count budget or
            # create a tight request loop. Pace against the preceding request,
            # with the same original deadline and neutral ownership throughout.
            next_request = self.last_request + PERIOD
            while t.time.perf_counter_ns() < next_request:
                self.check_pending_observation_owner()
                remaining = min(next_request, self.pending_observation["deadlineNs"]) - t.time.perf_counter_ns()
                if remaining > 0:
                    t.time.sleep(min(.001, remaining / t.NS))
            self.check_pending_observation_owner()
            evaluated = t.time.perf_counter_ns()
            require(evaluated < until, "expired/invalid original pending vehicle clocks")
            require(evaluated - self.last_request <= t.SNAPSHOT_TIMEOUT,
                    "pending vehicle response exceeded original response deadline")
            require(self.pending_observation_requests < MAX_PENDING_REACQUISITIONS,
                    "pending vehicle reacquisition request budget exhausted")
            self.pending_observation_requests += 1
            self.pending_observation["reference"] = copy.deepcopy(snapshot)
            self.artifacts.event(dict(type="vehicle_gather_pending_discarded", nonce=snapshot["nonce"],
                reason=value["reason"],
                commandAuthority=False, observationDeadlineNs=self.pending_observation["deadlineNs"],
                minimumRequestIntervalNs=PERIOD, reacquisitionNotBeforeNs=next_request,
                freshRequest=self.pending_observation_requests, maximumFreshRequests=MAX_PENDING_REACQUISITIONS))
            self.latest_snapshot = None
            self.pending_nonce = None
            self.request_snapshot()
            self.bounds(t.time.perf_counter_ns())
            return True

        def admit_snapshot(self, snapshot, now, nonce):
            # Shared receive may probe an expired response at observed_ns before
            # refreshing it. Vehicle admission always uses the current clock so
            # that historical probe refuses instead of authorizing a retry.
            return self.check_vehicle_snapshot(snapshot, t.time.perf_counter_ns(), nonce)

        def check_vehicle_snapshot(self, snapshot, now, nonce):
            held = self.stick if self.sustained_monitor else None
            value = admit(t, snapshot, now, nonce, self.vehicle_request["role"], held)
            require(n(snapshot["observed_ns"]) >= self.last_request,
                    "vehicle snapshot predates its nonce request")
            if self.pending_observation is not None:
                self.bounds(t.time.perf_counter_ns())
                self.check_pending_cohort(snapshot, self.pending_observation["reference"], available=True)
            for before in (self.vehicle_reference, self.initial_discarded_snapshot, self.sustained_reference):
                if before is None:
                    continue
                require(n(snapshot["nonce"]) > n(before["nonce"]), "reused vehicle snapshot nonce")
                old = before["vehicleObservation"]
                if self.vehicle_request.get("mode") == "sustained":
                    require(all(value[k] == old[k] for k in PENDING_COHORT +
                                ("playerPermissionMask", "neutralRearm", "capabilities")),
                            "sustained vehicle owner/permission cohort changed")
                require(all(value[k] == old[k] for k in ("owner", "routeFingerprint", "slot", "role", "consumers"))
                        and value["capabilities"]["axes"] == old["capabilities"]["axes"],
                        "vehicle owner/seat/route/consumer changed")
                require(n(value["gatherSequence"]) > n(old["gatherSequence"])
                        and n(value["input"]["generation"]) > n(old["input"]["generation"])
                        and n(value["inputObservedNs"]) > n(old["inputObservedNs"]),
                        "reused vehicle Gather/input observation")
                inp, prev = value["input"], old["input"]
                hands, previous_hands = inp["hands"], prev["hands"]
                if held is not None:
                    side = 0 if self.vehicle_request["role"] == "driver" else 1
                    hands, previous_hands = hand_basis(hands, side), hand_basis(previous_hands, side)
                require(all(inp[k] == prev[k] for k in ("spaceGeneration", "trackingBasisGeneration",
                            "worldUnitsPerMeter", "floorRelative", "referenceHead", "head"))
                        and hands == previous_hands
                        and inp["menu"]["epoch"] == prev["menu"]["epoch"],
                        "vehicle input/tracking/menu basis changed")
                if n(inp["menu"]["sequence"]) == n(prev["menu"]["sequence"]):
                    require(n(inp["menu"]["deadlineNs"]) == n(prev["menu"]["deadlineNs"]),
                            "original vehicle menu clock changed")
                else:
                    require(n(inp["menu"]["sequence"]) > n(prev["menu"]["sequence"]),
                            "vehicle menu sequence regressed")

        def snapshot_source_deadline(self, snapshot):
            v = snapshot["vehicleObservation"]
            return min(n(v["validUntilNs"]), n(v["inputDeadlineNs"]), n(v["input"]["menu"]["deadlineNs"]))

        def pump(self):
            until = min(self.deadline, self.stage_deadline)
            if self.sustained_monitor and not (self.releasing or self.cleanup_mode):
                # A successor must arrive before the PREVIOUS lease expires.
                # Nested reads cannot use its newer clock to bridge a gap.
                until = min(until, self.snapshot_source_deadline(self.latest_snapshot))
            return t.read_until(until, self.pump_owned)

        def pump_owned(self):
            self.bounds(t.time.perf_counter_ns())
            state = self.read_state()
            self.check_owner_record()
            if self.pending_command:
                try:
                    require(t.digest(t.read_bytes(self.command_path, 4096)) == self.command_hash
                            and n(state["cmdSeq"]) in (self.sequence - len(self.pending_batch), self.sequence),
                            "vehicle pending command ownership changed")
                except t.ReadDeadlineExceeded:
                    raise
                except BaseException:
                    self.unsafe_owner = True
                    raise
            else:
                self.ownership(state)
            if not self.releasing:
                self.check_input(state)
            if self.sustained_monitor and not (self.releasing or self.cleanup_mode):
                received = self.receive_snapshot()
                if received:
                    self.accept_snapshot(*received)
                now = t.time.perf_counter_ns()
                self.bounds(now)
                if self.pending_nonce is None and now - self.last_request >= PERIOD:
                    self.request_snapshot()

        def bounds(self, now):
            super().bounds(now)
            if self.sustained_monitor and not (self.releasing or self.cleanup_mode):
                require(now < self.snapshot_source_deadline(self.latest_snapshot),
                        "sustained vehicle original source lease expired")
            if self.pending_observation is not None:
                require(now < self.pending_observation["deadlineNs"], "original pending vehicle observation deadline reached")
                require(self.neutral_preflight() and
                        (self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit)
                        == self.pending_observation["commandIdentity"],
                        "pending vehicle recovery requires untouched neutral preflight")
            if self.releasing and not self.cleanup_mode and not self.pending_command:
                require(now + COMMAND_WINDOW <= self.action_deadline,
                        "vehicle zero lost its full original publication/ACK reserve")

        def validate_publication(self, lines, expected, cleanup):
            require(lines == self.authorized_lines and len(lines) == 1
                    and (not cleanup or self.release_attempted), "unreviewed vehicle command")
            if self.releasing:
                require(lines == [self.line([0, 0])], "vehicle release may only zero its owned stick")
            else:
                require(self.pending_nonce is None, "vehicle observation request remains pending")
                self.check_input(self.read_state())
                self.admit_snapshot(self.latest_snapshot, t.time.perf_counter_ns(), n(self.latest_snapshot["nonce"]))

        def read_publication_owner(self, expected_bytes, deadline_ns):
            raw = super().read_publication_owner(expected_bytes, deadline_ns)
            if not self.releasing:
                self.check_input(self.read_state())
                self.admit_snapshot(self.latest_snapshot, t.time.perf_counter_ns(), n(self.latest_snapshot["nonce"]))
            return raw

        def publish_stick(self, values, *, release=False, cleanup=False):
            require(not self.pending_command and not self.unsafe_owner, "vehicle publication ownership unavailable")
            self.releasing = release or cleanup
            if self.releasing:
                require(values == [0, 0] and any(self.stick), "vehicle neutral release is not owned")
            self.authorized_lines = [self.line(values)]
            self.pending_stick, self.pending_target = values[:], self.sequence + 2
            try:
                super().send(self.authorized_lines, cleanup=cleanup,
                    source_deadline=None if self.releasing else self.snapshot_source_deadline(self.latest_snapshot))
                require(self.pending_stick is None and self.stick == values, "vehicle stick custody unsettled")
            except BaseException as error:
                if any(s in str(error) for s in ("concurrent command writer", "unique command sentinel mismatch",
                        "unexpected command sequence advancement", "acknowledgement batch text mismatch",
                        "command owner record replaced")):
                    self.unsafe_owner = True
                raise
            finally:
                self.authorized_lines = None
                self.releasing = False

        def stimulate(self, path):
            require(not self.attempted, "vehicle pulse may not be retried")
            self.attempted = True
            value, raw = t.read_json(t.child(self.run, path), 4096)
            self.vehicle_request = plan(value, require)
            sustained = self.vehicle_request.get("mode") == "sustained"
            self.artifacts.save("vehicle-request.json", raw)
            self.artifacts.save("vehicle-policy.json", dict(adapterSha256=t.digest(Path(__file__).read_bytes()),
                sourceSha256=self.manifest["source_sha256"], version=self.manifest["version"],
                maximumPulses=1, neutralReserveNs=RELEASE_RESERVE, nativeAcceptance=False))
            self.fresh()
            self.vehicle_reference = copy.deepcopy(self.latest_snapshot)
            self.fresh()
            if self.pending_observation is not None:
                self.check_pending_observation_owner()
                self.pending_observation = None
            started = t.time.perf_counter_ns()
            admission = copy.deepcopy(self.latest_snapshot)
            previous_stage = self.stage_deadline
            budget = (self.vehicle_request["positiveHoldMs"] * 1_000_000 + RELEASE_RESERVE if sustained
                      else self.vehicle_request["holdMs"] * 1_000_000)
            self.action_deadline = min(previous_stage, self.deadline - RELEASE_RESERVE,
                started + budget)
            active_until = self.action_deadline - RELEASE_RESERVE
            require(started < active_until, "vehicle pulse has no active budget")
            self.stage_deadline, self.motion_started = active_until, started
            if sustained:
                self.stage_deadline = min(active_until, self.snapshot_source_deadline(self.latest_snapshot))
            self.publish_stick(self.vehicle_request["stick"])
            press_committed = self.last_commit
            self.stage_deadline = self.action_deadline
            if sustained:
                self.sustained_reference = copy.deepcopy(self.latest_snapshot)
                self.sustained_monitor = True
            while (now := t.time.perf_counter_ns()) < active_until:
                # The optional positive budget is a cap, including the final
                # read. An overrun cancels through the owned cleanup path.
                t.read_until(min(now + PERIOD, active_until if sustained else active_until + PERIOD), self.pump)
                remaining = active_until - t.time.perf_counter_ns()
                if remaining > 0:
                    t.time.sleep(min(.001, remaining / t.NS))
            self.publish_stick([0, 0], release=True)
            require(neutral(self.state["controls"]), "vehicle zero ACK lacks committed neutral controls")
            report = dict(request=self.vehicle_request, admissionNonce=admission["nonce"],
                owner=admission["vehicleObservation"]["owner"],
                routeFingerprint=admission["vehicleObservation"]["routeFingerprint"],
                startedNs=started, originalDeadlineNs=self.action_deadline,
                pulseCommittedNs=press_committed, neutralCommittedNs=self.last_commit,
                commandsAcknowledged=True, nativeAcceptance=False, headsetAcceptance=False)
            if sustained:
                report.update(positiveBudgetNs=active_until-started, neutralReserveNs=RELEASE_RESERVE,
                    freshObservations=self.sustained_observations, lastObservationNonce=self.latest_snapshot["nonce"])
                self.sustained_monitor = False
            self.action_deadline, self.stage_deadline = None, previous_stage
            self.artifacts.save("vehicle-stimulus.json", report)
            return report

        def release_after_failure(self):
            if self.release_attempted:
                return "not retried: previous vehicle cleanup attempt retained"
            self.release_attempted = True
            if not self.entry_admitted:
                return "not attempted: neutral vehicle entry not admitted"
            self.cleanup_mode, self.monitor = True, False
            trace = getattr(self, "_receive_timing", None)
            if trace is not None:
                trace.freeze(self.pending_command, self.sequence)
            try:
                require(not self.unsafe_owner, "vehicle command ownership lost")
                self.check_owner_record()
                if self.pending_command:
                    self.settle_pending_for_release()
                require(not self.pending_command, "pending vehicle input could not be settled")
                state = self.read_state()
                self.ownership(state)
                self.check_controls(state)
                # Discard only an unpublished proposal, after exact old state
                # has been rejoined. Never derive ownership from arbitrary input.
                self.pending_stick = self.pending_target = None
                if not any(self.stick):
                    self.save_owner(False)
                    return "no owned vehicle stick remains active"
                require(t.time.perf_counter_ns() + COMMAND_WINDOW < self.hard_deadline,
                        "vehicle cleanup reserve exhausted")
                self.publish_stick([0, 0], cleanup=True)
                return "owned vehicle stick zero ACK and later committed frame verified"
            except BaseException:
                self.normal_stop_required = True
                raise

    return VehicleDriver
