"""One bounded vehicle pulse adapter for drive_bc2_feed_simulation.py.

--vehicle-stimulus FILE: {"role":"driver","stick":[0.4,0.5],"holdMs":400}
or {"role":"gunner","stick":[0.4,0],"holdMs":400}. Driver uses left stick;
gunner uses right stick. holdMs (301..500) includes both acknowledgements and
a 300 ms reserve for numeric zero. At most one pulse, no automatic retry.
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
RELEASE_RESERVE = COMMAND_WINDOW + 2 * PERIOD
OWNER = ("manager", "player", "soldier", "weak", "controlled", "entry", "router", "cache",
         "actorGeneration", "seatGeneration", "inputEpoch", "movementEpoch")
AXES = (("throttle", "left_stick_y", 0), ("steer", "left_stick_x", 1),
        ("lookYaw", "right_stick_x", 8), ("lookPitch", "right_stick_y", 7))


def neutral(controls):
    return bool(controls) and all(all(x == 0 for x in v) if isinstance(v, list) else v == 0
                                  for v in controls.values())


def plan(value, require):
    require(isinstance(value, dict) and set(value) == {"role", "stick", "holdMs"},
            "vehicle request needs exactly role, stick and holdMs; no fire/exit/seat actions")
    require(value["role"] in ("driver", "gunner"), "vehicle role must be driver or gunner")
    stick = value["stick"]
    require(isinstance(stick, list) and len(stick) == 2
            and all(type(x) in (int, float) and math.isfinite(x) and abs(x) <= 1 for x in stick)
            and any(abs(x) > .18 for x in stick), "vehicle stick needs a bounded nonzero axis beyond deadzone")
    require(type(value["holdMs"]) is int and 301 <= value["holdMs"] <= 500,
            "vehicle pulse must be 301..500 ms including full zero/ACK reserve")
    return copy.deepcopy(value)


def admit(t, snapshot, now, nonce, role):
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
    for hand in inp["hands"]:
        r(hand["gripTracked"] is True and hand["aimTracked"] is True
          and all(n(hand[k]) >= 0 for k in ("active", "held", "touchActive", "touched"))
          and all(p.finite(hand[k]) == 0 for k in ("held", "stickX", "stickY", "trigger", "squeeze")),
          "vehicle requires consumed neutral tracked controllers")
        p.from_pose(hand["grip"])
        p.from_pose(hand["aim"])
    side = 0 if role == "driver" else 1
    r(n(inp["hands"][side]["active"]) & 1, "requested vehicle stick action is inactive")
    rearm, consumed = v["neutralRearm"], v["consumedControls"]
    r(rearm["controlsActive"] is True and rearm["armedLeft" if side == 0 else "armedRight"] is True
      and all(p.finite(consumed[k]) == 0 for k in ("throttle", "steer", "lookYaw", "lookPitch"))
      and consumed["exitHeld"] is False, "vehicle neutral rearm/consumed zero unavailable")
    capabilities = v["capabilities"]
    r(capabilities["fire"] is False and isinstance(capabilities["axes"], list)
      and len(capabilities["axes"]) == 4, "unknown vehicle axis capability schema")
    consumer = n(v["consumers"]["driver" if side == 0 else "camera"])
    r(consumer > 0 and (side == 0 or n(v["consumers"]["weaponCount"]) > 0),
      "requested vehicle axis consumer unknown")
    for index in (range(2) if side == 0 else range(2, 4)):
        axis, (semantic, source, concept) = capabilities["axes"][index], AXES[index]
        mapping = axis["mapping"]
        r(axis["semantic"] == semantic and axis["available"] is True and axis["reason"] is None
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

        def snapshot_timeout(self, now, response=None):
            # Either neutral preflight may wait once for its original response.
            # attempted marks method entry, not input publication; all motion
            # and custody gates below must still be untouched.
            if (not self.entry_admitted or self.vehicle_request is None
                    or self.initial_observation_waited or self.motion_started is not None
                    or self.last_write != 0 or self.commands != 0
                    or self.action_deadline is not None or self.pending_command
                    or self.pending_stick is not None or self.pending_target is not None
                    or any(self.stick) or self.unsafe_owner or self.cleanup_mode
                    or self.releasing or self.release_attempted
                    or self.held_left or self.held_right or self.held_trigger or self.held_buttons
                    or self.snapshot_refresh_started is not None):
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
            return result

        def snapshot_usable(self, snapshot, now):
            # Expired/missing/refused input fails here, before shared receive's
            # optional refresh branch. This mode never automatically retries.
            self.admit_snapshot(snapshot, now, n(snapshot["nonce"]))
            return True

        def admit_snapshot(self, snapshot, now, nonce):
            # Shared receive may probe an expired response at observed_ns before
            # refreshing it. Vehicle admission always uses the current clock so
            # that historical probe refuses instead of authorizing a retry.
            return self.check_vehicle_snapshot(snapshot, t.time.perf_counter_ns(), nonce)

        def check_vehicle_snapshot(self, snapshot, now, nonce):
            value = admit(t, snapshot, now, nonce, self.vehicle_request["role"])
            require(n(snapshot["observed_ns"]) >= self.last_request,
                    "vehicle snapshot predates its nonce request")
            for before in (self.vehicle_reference, self.initial_discarded_snapshot):
                if before is None:
                    continue
                require(n(snapshot["nonce"]) > n(before["nonce"]), "reused vehicle snapshot nonce")
                old = before["vehicleObservation"]
                require(all(value[k] == old[k] for k in ("owner", "routeFingerprint", "slot", "role", "consumers"))
                        and value["capabilities"]["axes"] == old["capabilities"]["axes"],
                        "vehicle owner/seat/route/consumer changed")
                require(n(value["gatherSequence"]) > n(old["gatherSequence"])
                        and n(value["input"]["generation"]) > n(old["input"]["generation"])
                        and n(value["inputObservedNs"]) > n(old["inputObservedNs"]),
                        "reused vehicle Gather/input observation")
                inp, prev = value["input"], old["input"]
                require(all(inp[k] == prev[k] for k in ("spaceGeneration", "trackingBasisGeneration",
                            "worldUnitsPerMeter", "floorRelative", "referenceHead", "head", "hands"))
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

        def bounds(self, now):
            super().bounds(now)
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
            self.artifacts.save("vehicle-request.json", raw)
            self.artifacts.save("vehicle-policy.json", dict(adapterSha256=t.digest(Path(__file__).read_bytes()),
                sourceSha256=self.manifest["source_sha256"], version=self.manifest["version"],
                maximumPulses=1, neutralReserveNs=RELEASE_RESERVE, nativeAcceptance=False))
            self.fresh()
            self.vehicle_reference = copy.deepcopy(self.latest_snapshot)
            self.fresh()
            started = t.time.perf_counter_ns()
            previous_stage = self.stage_deadline
            self.action_deadline = min(previous_stage, self.deadline - RELEASE_RESERVE,
                started + self.vehicle_request["holdMs"] * 1_000_000)
            active_until = self.action_deadline - RELEASE_RESERVE
            require(started < active_until, "vehicle pulse has no active budget")
            self.stage_deadline, self.motion_started = active_until, started
            self.publish_stick(self.vehicle_request["stick"])
            press_committed = self.last_commit
            self.stage_deadline = self.action_deadline
            while (now := t.time.perf_counter_ns()) < active_until:
                t.read_until(min(now + PERIOD, active_until + PERIOD), self.pump)
                remaining = active_until - t.time.perf_counter_ns()
                if remaining > 0:
                    t.time.sleep(min(.001, remaining / t.NS))
            self.publish_stick([0, 0], release=True)
            require(neutral(self.state["controls"]), "vehicle zero ACK lacks committed neutral controls")
            report = dict(request=self.vehicle_request, admissionNonce=self.latest_snapshot["nonce"],
                owner=self.latest_snapshot["vehicleObservation"]["owner"],
                routeFingerprint=self.latest_snapshot["vehicleObservation"]["routeFingerprint"],
                startedNs=started, originalDeadlineNs=self.action_deadline,
                pulseCommittedNs=press_committed, neutralCommittedNs=self.last_commit,
                commandsAcknowledged=True, nativeAcceptance=False, headsetAcceptance=False)
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
