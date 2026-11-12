"""ROOT-only bounded ordinary on-foot navigation through the shared xr-sim driver.

--navigation FILE (inside --run) is a reviewed JSON list, e.g.
[{"left":[0,0.5],"right":[0,0],"holdMs":150,"lookDegrees":[10,0]}].
lookDegrees is a relative yaw/pitch change; head position, roll and controller
poses remain unchanged. holdMs bounds the entire pulse, including both ACKs;
normal zero publication reserves the last 80 ms. Each of at most four pulses ends with acknowledged
numeric zeros for both sticks. There are no buttons, triggers or hand claims.
No launch, capture, objective-entry, native movement or headset claim is made.
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
PERIOD = 25_000_000


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
    require(isinstance(value, list) and 1 <= len(value) <= 4, "one to four reviewed pulses required")
    for row in value:
        require(isinstance(row, dict) and set(row) <= {"left", "right", "holdMs", "lookDegrees"}
                and {"left", "right", "holdMs"} <= set(row), "unknown or missing navigation fields")
        vector(row["left"], 2, 1)
        vector(row["right"], 2, 1)
        require(type(row["holdMs"]) is int and 100 <= row["holdMs"] <= 500,
                "pulse must be 100..500 ms, including command and zero acknowledgement")
        if "lookDegrees" in row:
            look = vector(row["lookDegrees"], 2, 45)
            require(abs(look[1]) <= 30, "pitch step exceeds 30 degrees")
    return value


def neutral_controls(controls):
    return bool(controls) and all(
        all(x == 0 for x in value) if isinstance(value, list) else value == 0
        for value in controls.values())


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
        self.entry_admitted = False
        self.sticks = [[0, 0], [0, 0]]
        self.pending_sticks = None
        self.release_attempted = False
        self.cleanup_mode = False
        self.action_deadline = None
        self.authorized_lines = None
        self.command_fault = False
        self.normal_stop_required = False
        self.episodes = []
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
            sourceSha256=args.source_sha256, nativeAcceptance=False, objectiveEntry=False,
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
                             for k, v in state["controls"].items() if k not in ("stickL", "stickR")),
                         "non-navigation controller input changed")
        if not self.pending_command:
            need_observation(state["head"] == self.expected_head
                             and [state["controls"][k] for k in ("stickL", "stickR")] == self.sticks,
                             "settled navigation input changed")

    def owner_bytes(self, pending):
        value = json.loads(super().owner_bytes(pending))
        # Existing shared drivers understand pending and therefore cannot take
        # over a settled but still-held navigation pulse or an unverified zero.
        value["pending"] = pending or self.pending_sticks is not None or any(any(v) for v in self.sticks)
        value["navigation"] = dict(sticks=self.sticks, pendingSticks=self.pending_sticks,
                                   neutralCommitted=not value["pending"])
        return t.json_bytes(value)

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
            self.sticks = copy.deepcopy(self.pending_sticks)
            self.pending_sticks = None
            self.state = state
        result = super().save_owner(pending, **kwargs)
        self.navigation_owner_record = self.owner_bytes(pending)
        return result

    def snapshot_readiness_scope(self):
        # Observation is never a license to extend a held pulse or its lease.
        return None

    def snapshot_source_deadline(self, snapshot):
        deadline = min(super().snapshot_source_deadline(snapshot),
                       t.integer(snapshot["native_deadline_ns"]),
                       t.integer(snapshot["input"]["menu"]["deadlineNs"]))
        if (snapshot["weapon_actions_blocked"] is True
                and (snapshot.get("bodyHolster") or {}).get("actualEmpty") is True):
            deadline = min(deadline, t.integer(snapshot["bodyHolster"]["validUntilNs"]))
        return deadline

    def admit_snapshot(self, snapshot, now, nonce):
        p.admit_frame(snapshot, now, nonce, mechanism_admission=empty_movement_admission)
        inp = snapshot["input"]
        menu = inp["menu"]
        # Gameplay=1 is the native menu owner. Valid body/shot publication is
        # revoked by Bc2Gameplay's !owner.foot and !nativePlaying branches.
        # The original actor/rig/frame lease is the on-foot evidence; no new
        # synthetic onFoot/Playing field or hand claim is manufactured here.
        need_observation(menu["owner"] == 1 and t.integer(menu["sequence"]) > 0
                         and t.integer(menu["epoch"]) > 0
                         and now < t.integer(menu["deadlineNs"]) <= now + 100_000_000,
                         "fresh native Playing menu ownership unavailable")
        need_observation(all(h["held"] == h["squeeze"] == h["trigger"] == 0 for h in inp["hands"]),
                         "navigation requires neutral native buttons/triggers/grips")
        before = self.navigation_reference
        if before is not None:
            need_observation(snapshot["owner"] == before["owner"]
                             and snapshot["physical_equipment_generation"] == before["physical_equipment_generation"],
                             "navigation actor/equipment lifetime changed")
            need_observation(all(inp[k] == before["input"][k] for k in
                                 ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative", "referenceHead"))
                             and menu["epoch"] == before["input"]["menu"]["epoch"]
                             and inp["head"]["position"] == before["input"]["head"]["position"],
                             "navigation tracking/head/menu epoch changed")
            need_observation(all(p.preserved_pose_equal(h[k], old[k])
                                 for h, old in zip(inp["hands"], before["input"]["hands"])
                                 for k in ("grip", "aim")), "native controller pose changed")
        require(now < self.snapshot_source_deadline(snapshot), "original navigation source lease expired")

    def pump(self):
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
        self.check_observation_state(state)
        if self.monitor:
            received = self.receive_snapshot()
            if received:
                self.accept_snapshot(*received)
            if self.latest_snapshot is not None:
                self.admit_snapshot(self.latest_snapshot, time.perf_counter_ns(),
                                    t.integer(self.latest_snapshot["nonce"]))
            if self.pending_nonce is None and now - self.last_request >= PERIOD:
                self.request_snapshot()

    def validate_publication(self, lines, expected, cleanup):
        require(lines == (ZERO if cleanup else self.authorized_lines), "unreviewed navigation command")
        require(not cleanup or self.release_attempted, "unowned navigation release")
        if not cleanup:
            self.check_observation_state(self.read_state())
            self.admit_snapshot(self.latest_snapshot, time.perf_counter_ns(),
                                t.integer(self.latest_snapshot["nonce"]))

    def read_publication_owner(self, expected_bytes, deadline_ns):
        raw = super().read_publication_owner(expected_bytes, deadline_ns)
        if not self.cleanup_mode:
            # Repeat readiness immediately at every shared atomic publication
            # attempt. These failures do not alone establish writer ambiguity.
            self.check_observation_state(self.read_state())
            self.admit_snapshot(self.latest_snapshot, time.perf_counter_ns(),
                                t.integer(self.latest_snapshot["nonce"]))
        return raw

    def publish_navigation(self, lines, sticks, *, cleanup=False):
        self.authorized_lines = lines[:]
        self.pending_sticks = copy.deepcopy(sticks)
        try:
            result = super().send(lines, cleanup=cleanup,
                source_deadline=None if cleanup else self.snapshot_source_deadline(self.latest_snapshot))
            require(self.pending_sticks is None and self.sticks == sticks,
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

    def navigate(self, rows):
        self.fresh()
        self.navigation_reference = copy.deepcopy(self.latest_snapshot)
        require(all(h["stickX"] == h["stickY"] == 0 for h in self.latest_snapshot["input"]["hands"]),
                "native entry sticks are not neutral")
        self.monitor = True
        for row in rows:
            require(self.pending_sticks is None and not self.pending_command and not any(any(v) for v in self.sticks),
                    "previous navigation episode lacks committed neutral")
            # Obtain a new original source before each episode, never restamp.
            self.fresh()
            self.monitor = True
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
            started = time.perf_counter_ns()
            previous_stage_deadline = self.stage_deadline
            self.action_deadline = min(self.deadline-300_000_000, started + row["holdMs"]*1_000_000)
            self.stage_deadline = min(previous_stage_deadline, self.action_deadline)
            self.motion_started = started
            self.publish_navigation(lines, sticks)
            self.expected_head = self.next_head
            # Include ACK latency in the original pulse clock. Stop normal
            # holding before the deadline so numeric-zero publication has its
            # own existing shared acknowledgement budget, without renewal.
            while time.perf_counter_ns() + 80_000_000 < self.action_deadline:
                self.pump()
                time.sleep(.001)
            self.publish_navigation(ZERO, [[0, 0], [0, 0]])
            require(neutral_controls(self.state["controls"]), "zero ACK lacks neutral committed controls")
            self.action_deadline = None
            self.stage_deadline = previous_stage_deadline
            self.episodes.append(dict(startedNs=started, neutralCommittedNs=self.last_commit,
                                      commandsAcknowledged=True, nativeMovement=False, objectiveEntry=False))
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
            self.sticks = [state["controls"][k][:] for k in ("stickL", "stickR")]
            self.pending_sticks = None
            if self.last_write == 0 and not any(any(v) for v in self.sticks):
                self.save_owner(False)
                return "no navigation input published"
            require(time.perf_counter_ns()+250_000_000 < self.hard_deadline,
                    "bounded release reserve exhausted")
            self.publish_navigation(ZERO, [[0, 0], [0, 0]], cleanup=True)
            require(neutral_controls(self.state["controls"]), "release ACK lacks neutral committed controls")
            return "ordinary zero-stick ACK and neutral committed state verified"
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
                  headsetAcceptance=False, objectiveEntry=False, normalStopRequired=False)
    try:
        require(args.root_authorized_ordinary_navigation, "explicit ROOT ordinary-navigation authorization required")
        require(re.fullmatch(r"[a-f0-9]{64}", args.source_sha256) is not None, "invalid reviewed source SHA256")
        rows, raw = t.read_json(t.child(t.local_path(args.run), args.navigation), 16384)
        plan(rows)
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
