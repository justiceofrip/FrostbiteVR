"""Root-only bounded ordinary XR reload stimulus; never a native success verdict.

Unlike drive_bc2_feed_simulation, diagnostic reception is not motion authority.
Fresh typed mapping/custody boundaries bracket ordinary left-controller input;
the installed mod independently validates every real input frame and operation.
This runner starts no process, captures no image and changes no native state.
"""
import argparse
import ctypes
from ctypes import wintypes
import gzip
import importlib.util
import json
import math
import os
from pathlib import Path
import queue
import re
import sys
import threading
import time
import uuid

sys.dont_write_bytecode = True


def module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + ".py"))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


transport = module("drive_bc2_feed_simulation")
require, integer, Refusal = transport.require, transport.integer, transport.REFUSAL
NS = 1_000_000_000
RECEIVE_NS = 250_000_000
PERIOD_NS = 100_000_000
ACK_NS = 250_000_000
WRITE_GAP_NS = 25_000_000
MAX_COMMANDS = 512
MAX_BYTES = 1024 * 1024
MAX_OBSERVATIONS = 100  # 10s / original100ms request period; not a renewed clock.
LEFT_POSE_SETUP = ["hand l follow off", "hand l aimtrim 0 0"]


def ordinary_commands(sample, allow_setup=False):
    """Reconstruct an allowlisted request, never execute planner command text."""
    require(isinstance(sample, dict) and not isinstance(sample.get("grip"), bool)
            and sample.get("grip") in (0, 1), "invalid left grip sample")
    position, angles = sample.get("positionMeters"), sample.get("yawPitchRollDegrees")
    require(isinstance(position, list) and isinstance(angles, list) and len(position) == len(angles) == 3,
            "left pose must have three position and angle components")
    require(all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)
                for v in position + angles), "non-finite pose")
    require(all(abs(v) <= 10 for v in position) and all(abs(v) <= 360 for v in angles),
            "pose outside bounded ordinary input envelope")
    coordinates = " ".join(f"{v:.9f}" for v in position + angles)
    supplied = sample.get("commands")
    require(isinstance(supplied, list), "missing ordinary command rows")
    setup = supplied[:2] == LEFT_POSE_SETUP
    require(not setup or allow_setup, "left pose setup only allowed on first sample")
    tail = supplied[2:] if setup else supplied
    require(len(tail) in (2, 3), "unexpected ordinary command count")
    lines = ([*LEFT_POSE_SETUP] if setup else []) + ["hand l grip pose " + coordinates]
    if len(tail) == 3:
        lines.append("hand l aim pose " + coordinates)
    lines.append("grip l " + str(sample["grip"]))
    require(sample.get("commands") == lines, "stimulus contains an unallowlisted or inconsistent command")
    return lines


def neutral_controls(controls, allow_left_grip=False):
    buttons = {"a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR"}
    axes, sticks = {"trigL", "trigR", "gripL", "gripR"}, {"stickL", "stickR"}
    if not isinstance(controls, dict) or set(controls) != buttons | axes | sticks:
        return False
    if not all(isinstance(controls[k], bool) and controls[k] is False for k in buttons):
        return False
    finite = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)
    if not all(finite(controls[k]) and 0 <= controls[k] <= 1
               and (k == "gripL" and allow_left_grip or controls[k] == 0) for k in axes):
        return False
    return all(isinstance(controls[k], list) and len(controls[k]) == 2
               and all(finite(v) and v == 0 for v in controls[k]) for k in sticks)


def validate_stimulus(value, continuation=False):
    require(value.get("schema") == "bc2vr.reload-stimulus.v1", "separate stimulus contract required")
    require(value.get("nativeAcceptance") is False and value.get("headsetAcceptance") is False,
            "stimulus must not claim native/headset acceptance")
    budget, schedule = value["budget"], value["schedule"]
    require(0 < integer(budget["totalNs"]) <= 10 * NS
            and 0 < integer(budget["maximumGestureNs"]) <= 10 * NS
            and 0 < integer(budget["maximumCommands"]) <= MAX_COMMANDS,
            "stimulus exceeds original time/command bounds")
    require(isinstance(schedule, list) and 1 <= len(schedule) <= MAX_COMMANDS
            and 0 <= integer(budget["gestureStartSample"]) < len(schedule), "invalid stimulus schedule")
    previous = -1
    require(value.get("continuesHeldCustody", False) is continuation, "held continuation contract differs")
    require((schedule[0]["commands"][:2] == LEFT_POSE_SETUP) is (not continuation),
            "initial pose setup required; held continuation must preserve established configuration")
    require(schedule[0]["grip"] == (1 if continuation else 0), "initial/continued grip state differs")
    for index, sample in enumerate(schedule):
        at = integer(sample["atNs"])
        require(previous < at <= integer(budget["totalNs"]), "nonmonotonic/oversized stimulus timing")
        ordinary_commands(sample, index == 0)
        previous = at
    require(sum(len(ordinary_commands(s, i == 0)) + 1 for i, s in enumerate(schedule)) + 2 <= MAX_COMMANDS,
            "stimulus and bounded release exceed command budget")
    require(integer(value["durationNs"]) == schedule[-1]["atNs"], "stimulus duration differs")
    ids = set()
    for boundary in value["boundaries"]:
        require(boundary["id"] not in ids and 0 <= integer(boundary["afterSample"]) < len(schedule)
                and boundary["requiresFreshSnapshot"] is True
                and 0 < integer(boundary["timeoutNs"]) <= integer(budget["totalNs"])
                and 0 <= integer(boundary.get("minimumObservationSpanNs", 0)) <= integer(boundary["timeoutNs"]),
                "invalid fresh stimulus boundary")
        ids.add(boundary["id"])
    return value


def sim_state(path, deadline_ns):
    """Immutable share-delete read; only transient2/32/33 inside original clock."""
    if os.name != "nt":
        return transport.read_json(path, 16384)[0]
    import msvcrt
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    transport.local_path(path)
    while True:
        require(time.perf_counter_ns() < deadline_ns, "simulator state read deadline")
        handle = api.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x00200000, None)
        if handle == wintypes.HANDLE(-1).value:
            error = ctypes.get_last_error()
            if error not in (2, 32, 33):
                raise ctypes.WinError(error)
            time.sleep(.001)
            continue
        try:
            fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
        except BaseException:
            api.CloseHandle(handle)
            raise
        with os.fdopen(fd, "rb") as stream:
            require(os.fstat(stream.fileno()).st_size <= 16384, "oversized simulator state")
            raw = stream.read(16385)
        require(len(raw) <= 16384, "simulator state grew")
        return json.loads(raw.decode("utf-8-sig"), parse_constant=lambda x: (_ for _ in ()).throw(Refusal(x)))


def publish_owner(driver, data):
    """Reviewed157 owner-only replace, one normal-access Win32 call, no retry."""
    require(os.name == "nt" and driver.lock not in (None, wintypes.HANDLE(-1).value),
            "owner publication requires exclusive live-driver.lock")
    path = transport.local_path(driver.run / "live-driver-owner.json")
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    require(isinstance(data, bytes) and len(data) <= 8192, "oversized owner record")
    class RenameInfo(ctypes.Structure):
        _fields_ = [("Flags", wintypes.DWORD), ("RootDirectory", wintypes.HANDLE),
                    ("FileNameLength", wintypes.DWORD), ("FileName", wintypes.WCHAR * 1)]
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    api.SetFileInformationByHandle.argtypes = (wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD)
    api.SetFileInformationByHandle.restype = wintypes.BOOL
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
        transport.local_path(temporary)
        transport.local_path(path)
        handle = api.CreateFileW(str(temporary), 0x00010000, 0, None, 3, 0x00200000, None)
        if handle in (None, wintypes.HANDLE(-1).value):
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            name = str(path).encode("utf-16-le")
            size = max(ctypes.sizeof(RenameInfo), RenameInfo.FileName.offset + len(name) + 2)
            buffer = ctypes.create_string_buffer(size)
            info = RenameInfo.from_buffer(buffer)
            info.Flags, info.RootDirectory, info.FileNameLength = 3, None, len(name)
            ctypes.memmove(ctypes.addressof(buffer) + RenameInfo.FileName.offset, name, len(name))
            if not api.SetFileInformationByHandle(handle, 22, buffer, size):
                raise ctypes.WinError(ctypes.get_last_error())
        finally:
            api.CloseHandle(handle)
    finally:
        if temporary.exists():
            temporary.unlink()


class ObservationWorker:
    """One bounded asynchronous read-only native observer; never publishes input."""
    def __init__(self, folder, hard_deadline):
        self.request = folder / "mechanism-snapshot-request.txt"
        self.snapshot = folder / "mechanism-snapshot.json"
        self.hard_deadline = hard_deadline
        self.inbox = queue.Queue(maxsize=8)
        self.stop_event = threading.Event()
        self.fatal = None
        self.thread = threading.Thread(target=self._run, name="bc2-stimulus-observer", daemon=True)
        self.thread.start()

    def emit(self, value):
        try:
            self.inbox.put_nowait(value)
        except queue.Full:
            raise Refusal("bounded observation inbox exhausted")

    def _run(self):
        try:
            previous = 0
            if self.request.exists():
                raw = transport.read_bytes(self.request, 22)
                require(re.fullmatch(rb"[1-9][0-9]{0,19}\r?\n?", raw) is not None, "invalid existing snapshot request")
                previous = int(raw)
            if self.snapshot.exists():
                previous = max(previous, integer(transport.read_json(self.snapshot, transport.planner.SNAPSHOT_LIMIT)[0]["nonce"]))
            for index in range(MAX_OBSERVATIONS):
                if self.stop_event.is_set() or time.perf_counter_ns() >= self.hard_deadline:
                    return
                started = time.perf_counter_ns()
                nonce = max(time.time_ns(), previous + 1)
                require(nonce < 2**64, "snapshot nonce overflow")
                request = (str(nonce) + "\n").encode("ascii")
                transport.atomic(self.request, request)
                deadline = min(self.hard_deadline, started + RECEIVE_NS)
                delivered = False
                while not self.stop_event.is_set() and time.perf_counter_ns() < deadline:
                    require(transport.read_bytes(self.request, 22) == request, "competing snapshot request writer")
                    if self.snapshot.exists():
                        snapshot, raw = transport.read_json(self.snapshot, transport.planner.SNAPSHOT_LIMIT)
                        actual = integer(snapshot["nonce"])
                        require(actual <= nonce, "competing snapshot response")
                        if actual == nonce:
                            self.emit({"kind": "snapshot", "index": index, "requestNs": started,
                                       "deadlineNs": deadline, "receivedNs": time.perf_counter_ns(),
                                       "snapshot": snapshot, "raw": raw})
                            delivered = True
                            break
                    self.stop_event.wait(.001)
                if not delivered and not self.stop_event.is_set():
                    self.emit({"kind": "gap", "reason": "original250ms response deadline", "requestNs": started,
                               "deadlineNs": deadline, "nonce": nonce})
                previous = nonce
                remaining = started + PERIOD_NS - time.perf_counter_ns()
                if remaining > 0:
                    self.stop_event.wait(remaining / NS)
            self.emit({"kind": "gap", "reason": "bounded observer request cap"})
        except BaseException as error:
            self.fatal = str(error)

    def close(self):
        self.stop_event.set()
        self.thread.join(.08)
        return not self.thread.is_alive()


class ReloadStimulus:
    acquire_lock = transport.Driver.acquire_lock
    ownership = transport.Driver.ownership
    check_target = staticmethod(transport.Driver.check_target)

    def __init__(self, args):
        require(os.name == "nt", "ordinary XR stimulus requires Windows")
        self.args, self.run = args, transport.local_path(args.run)
        self.owner = self.lock = self.artifacts = self.observer = None
        self.started = time.perf_counter_ns()
        self.hard_deadline = self.started + 10 * NS
        self.deadline = self.hard_deadline - 400_000_000
        self.stage_deadline = self.deadline
        self.commands = self.last_write = 0
        self.last_commit = None
        self.pending_command = self.unsafe_owner = self.held_left = False
        self.mapping = self.stimulus = None
        self.target_observed = False
        self.first_sample_pending = False
        self.pose_setup_established = False
        self.boundaries, self.plans, self.observed = {}, [], []
        self.observation_incomplete = False
        self.decoded_snapshot_bytes = 0
        self.geometry = module("bc2_reload_stimulus_geometry")
        self.manifest, _ = transport.read_json(self.run / "run-manifest.json")
        self.link, _ = transport.read_json(self.run / "session-link.json")
        self.startup, _ = transport.read_json(self.run / "sim-startup.json")
        require(self.manifest["schema"] == "fvr.xr-game-run.v1"
                and transport.same_path(self.manifest["output_directory"], self.run), "run manifest mismatch")
        require(self.link["root_commands_ready"] is True and self.startup["root_commands_ready"] is True
                and self.startup["status"] == "ready", "runner startup not ready")
        self.host = self.link["host"]
        require(self.startup["host"] == self.host, "startup host identity differs")
        self.folder = transport.local_path(self.link["folder"])
        self.state_path, self.command_path, self.ack_path = [self.run / "sim" / x for x in ("state.json", "command.txt", "ack.txt")]
        self.session_path = self.folder / "session.json"
        for key, expected in (("sim_state", self.state_path), ("command", self.command_path), ("session", self.session_path),
                              ("options", self.folder / "options.json"), ("stop_request", self.folder / "stop.request"),
                              ("startup_receipt", self.run / "sim-startup.json")):
            require(transport.same_path(transport.local_path(self.link[key]), expected), "linked path differs: " + key)
        self.owner = transport.WindowsOwner(self.host)
        self.acquire_lock()
        self.artifacts = transport.Artifacts(self.run)
        self.artifacts.save("invocation.json", {"schema": "bc2vr.reload-stimulus-invocation.v1", "host": self.host,
            "family": args.family, "stage": args.stage, "rootAuthorizedOrdinaryInput": True,
            "maximumDurationNs": 10 * NS, "maximumCommands": MAX_COMMANDS, "maximumArtifactBytes": MAX_BYTES,
            "maximumObservationRequests": MAX_OBSERVATIONS, "maximumDecodedObservationBytes": 4 * MAX_BYTES,
            "driverSha256": transport.digest(Path(__file__).read_bytes()),
            "geometrySha256": transport.digest(Path(self.geometry.__file__).read_bytes())})
        options, _ = transport.read_json(self.folder / "options.json")
        require(options["source_sha256"] == self.manifest["source_sha256"] and options["version"] == self.manifest["version"]
                and options["activate_window"] is False, "session version/source/options changed")
        for key in ("runtime_manifest", "runtime_library", "host_executable"):
            pinned = transport.check_pin(self.manifest[key])
            if key == "runtime_manifest":
                require(transport.same_path(options["xr_manifest"], pinned), "runtime manifest differs")
            if key == "host_executable":
                require(transport.same_path(self.host["path"], pinned), "host executable differs")
        self.capture_plan = self.manifest["capture_plan"]
        self.state = self.read_state()
        self.capture_sequence = integer(self.state["captureSeq"])
        self.command_hash = transport.digest(transport.read_bytes(self.command_path, 4096))
        self.sequence, self.sentinel = integer(self.state["cmdSeq"]), self.state["lastCmd"]
        owner_path = self.run / "live-driver-owner.json"
        if owner_path.exists():
            previous, _ = transport.read_json(owner_path, 8192)
            require(previous["host"] == self.host and previous["pending"] is False
                    and previous["commandSha256"] == self.command_hash and previous["sequence"] == self.sequence
                    and previous["sentinel"] == self.sentinel, "unsettled/foreign command owner")
        else:
            require(self.sequence == self.startup["target_sequence"] and self.command_hash == self.startup["command_file"]["sha256"]
                    and self.sentinel == self.startup["settled_state"]["lastCmd"], "startup ownership changed")
        self.initial_sim = self.state
        require(neutral_controls(self.state["controls"]), "initial controls must all be neutral")
        require(self.link["mechanism_snapshots"]["requested"] is True
                and transport.read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n", "mechanism observer not opted in")
        profiles = transport.child(self.run, args.profiles)
        self.profiles, raw = transport.read_json(profiles, 8 * MAX_BYTES)
        self.artifacts.save("profile-source.json", {"path": str(profiles), "sha256": transport.digest(raw), "bytes": len(raw)})
        self.artifacts.save("initial-state.json", self.state)
        self.observer = ObservationWorker(self.folder, self.deadline)

    def bounds(self):
        require(time.perf_counter_ns() < min(self.deadline, self.stage_deadline), "original stimulus/gesture deadline")

    def read_state(self, cleanup=False):
        self.owner.check()
        session, _ = transport.read_json(self.session_path)
        require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                and session["source_sha256"] == self.manifest["source_sha256"]
                and session["version"] == self.manifest["version"] and session["state"] == "running",
                "native session/owner/source changed")
        require(not (self.folder / "stop.request").exists(), "normal stop requested")
        state = sim_state(self.state_path, self.hard_deadline if cleanup else min(self.deadline, self.stage_deadline))
        require(integer(state["pid"]) == integer(self.host["pid"]) and state["runtime"] == "xr-sim"
                and integer(state["errors"]) == 0 and state["lastCmdError"] == "", "simulator identity/error changed")
        require(state["sessionRunning"] is True and state["actionsAttached"] is True
                and state["sessionState"] == "FOCUSED" and state["paceMode"] == "free"
                and all(state[key]["valid"] is True for key in ("head", "handL", "handR")), "focus/tracking/session changed")
        if hasattr(self, "initial_sim"):
            require(all(state[key] == self.initial_sim[key] for key in ("head", "handR")), "head/right-hand basis changed")
            require(neutral_controls(state["controls"], True), "unrequested controller input")
            require(integer(state["captureSeq"]) == self.capture_sequence, "unrequested capture")
        return state

    def save_owner(self, pending):
        publish_owner(self, transport.json_bytes({"host": self.host, "pending": pending, "sequence": self.sequence,
            "sentinel": self.sentinel, "commandSha256": self.command_hash, "artifactDirectory": str(self.artifacts.directory)}))

    def observations(self):
        if self.observer is None:
            return
        require(self.observer.fatal is None, "native observer ownership/read failure: " + str(self.observer.fatal))
        for _ in range(8):
            try:
                item = self.observer.inbox.get_nowait()
            except queue.Empty:
                break
            if item["kind"] == "gap":
                self.observation_incomplete = True
                self.artifacts.event({"type": "observation_gap", **item})
                continue
            raw, snapshot = item.pop("raw"), item["snapshot"]
            self.decoded_snapshot_bytes += len(raw)
            require(self.decoded_snapshot_bytes <= 4 * MAX_BYTES, "decoded observation budget")
            self.artifacts.save(f"observation-{item['index']:03}.json.gz", gzip.compress(raw, mtime=0))
            item["current"] = (snapshot.get("valid") is True and item["receivedNs"] < item["deadlineNs"]
                               and item["receivedNs"] < self.geometry.source_deadline(snapshot, self.args.family))
            if not item["current"]:
                self.observation_incomplete = True
            # Expiry never renews this record. A valid later mapping still makes
            # observed owner/reference drift actionable even if delivery is late.
            if self.mapping is not None and snapshot.get("valid") is True:
                require(integer(snapshot["observed_ns"]) >= integer(self.mapping["observed_ns"]), "observation time regressed")
                transition = self.geometry.same_mapping(self.mapping, snapshot, self.args.family, self.args.stage, target=self.args.target)
                if transition == "target":
                    self.target_observed = True
            self.observed.append(item)
            self.observed = self.observed[-8:]
            self.artifacts.event({"type": "native_observation", "index": item["index"], "nonce": snapshot.get("nonce"),
                "requestNs": item["requestNs"], "receivedNs": item["receivedNs"], "currentAtReceive": item["current"],
                "valid": snapshot.get("valid"), "invalidReason": snapshot.get("invalid_reason"), "nativeAcceptance": False})

    def guard(self):
        self.bounds()
        state = self.read_state()
        if not self.pending_command:
            self.ownership(state)
        self.observations()
        return state

    def pause_until(self, until):
        while time.perf_counter_ns() < until:
            self.guard()
            time.sleep(.001)
        self.guard()

    def fresh(self, *, boundary=None, after_ns=0, pickup=None, prepare=False):
        require(not prepare or boundary is None, "stimulus preparation requires initial stage admission")
        timeout = integer(boundary["timeoutNs"]) if boundary else RECEIVE_NS
        until = min(self.deadline, self.stage_deadline, time.perf_counter_ns() + timeout)
        used = set()
        while time.perf_counter_ns() < until:
            self.guard()
            if time.perf_counter_ns() >= until:
                self.observation_incomplete = True
                raise Refusal("original fresh boundary deadline consumed by observation")
            for item in reversed(self.observed):
                snapshot = item["snapshot"]
                key = integer(snapshot["nonce"])
                if key in used or snapshot.get("valid") is not True or integer(snapshot["observed_ns"]) < after_ns:
                    continue
                used.add(key)
                now = time.perf_counter_ns()
                if not item["current"] or now >= self.geometry.source_deadline(snapshot, self.args.family):
                    continue
                if boundary is not None:
                    require(self.last_commit is not None, "fresh boundary requires settled command")
                    # Publishing the command file precedes host application. A
                    # snapshot after that write can still contain held input
                    # from before the acknowledged release. Keep the original
                    # budget and require input observed after the settled ACK.
                    if integer(snapshot["input_evidence"]["observedNs"]) < max(self.last_write, self.last_commit):
                        continue
                if boundary is None:
                    try:
                        if prepare:
                            # Building includes typed stage admission. Keep its
                            # plan local to this fresh snapshot and original lease.
                            plan = self.geometry.build_stimulus(self.args.family, self.profiles, snapshot, now, self.args.stage,
                                step_ms=self.args.step_ms, pickup=pickup, target=self.args.target)
                        else:
                            self.geometry.admit_boundary(self.args.family, self.profiles, snapshot, now, self.args.stage,
                                                         pickup=pickup, target=self.args.target)
                    except self.geometry.BoundaryPending as pending:
                        self.observation_incomplete = True
                        self.artifacts.event({"type": "boundary_pending", "id": "stage_admission", "nonce": snapshot["nonce"],
                                              "reason": str(pending), "deadlineNs": until})
                        continue
                    require(time.perf_counter_ns() < min(until, self.geometry.source_deadline(snapshot, self.args.family)),
                            "initial boundary validation consumed original deadline")
                    return (snapshot, now, plan) if prepare else (snapshot, now)
                try:
                    summary = self.geometry.check_boundary(self.stimulus, snapshot, boundary["id"], now,
                        command_written_ns=self.last_write, prior=self.boundaries, profiles=self.profiles)
                except self.geometry.BoundaryPending as pending:
                    self.artifacts.event({"type": "boundary_pending", "id": boundary["id"], "nonce": snapshot["nonce"],
                                          "reason": str(pending), "deadlineNs": until})
                    continue
                self.boundaries[boundary["id"]] = summary
                self.artifacts.event({"type": "fresh_stimulus_boundary", "id": boundary["id"], "summary": summary})
                require(time.perf_counter_ns() < min(until, self.geometry.source_deadline(snapshot, self.args.family)),
                        "boundary validation consumed original deadline")
                return snapshot, now
            time.sleep(.001)
        self.observation_incomplete = True
        raise Refusal("fresh typed stimulus boundary absent within original budget")

    def send(self, sample=None, *, cleanup=False, release_only=False):
        require(not release_only or self.held_left, "ordinary release requires held left input")
        lines = ["grip l 0"] if cleanup or release_only else ordinary_commands(sample, self.first_sample_pending)
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget")
        if cleanup:
            require(time.perf_counter_ns() + ACK_NS < self.hard_deadline, "cleanup reserve exhausted")
            while time.perf_counter_ns() < self.last_write + WRITE_GAP_NS:
                time.sleep(.001)
        else:
            self.pause_until(max(self.last_write + WRITE_GAP_NS, time.perf_counter_ns()))
        before = self.read_state(cleanup)
        self.ownership(before)
        sentinel = "note root-stimulus-" + uuid.uuid4().hex
        batch = lines + [sentinel]
        require(len(" | ".join(batch).encode("ascii")) < 508, "acknowledgement text overflow")
        raw, target = ("\n".join(batch) + "\n").encode("ascii"), self.sequence + len(batch)
        written = time.perf_counter_ns()
        if not cleanup:
            self.bounds()
            if self.first_sample_pending:
                require(written < self.initial_mapping_deadline, "initial mapping expired before first publication")
        self.artifacts.event({"type": "command_request", "atNs": written, "beforeSequence": self.sequence,
            "targetSequence": target, "commands": batch, "ordinaryInputOnly": True}, cleanup=cleanup)
        previous_owner = self.sequence, self.sentinel, self.command_hash
        self.sequence, self.sentinel, self.command_hash = target, sentinel, transport.digest(raw)
        self.pending_command = True
        self.pending_batch, self.pending_before = batch, before
        try:
            self.save_owner(True)
        except BaseException:
            # The command publisher has not been called. Retain the exact
            # previously owned command so failure cleanup can verify it afresh;
            # preserve the owner-file failure, never retry the failed action.
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            raise
        transport.atomic(self.command_path, raw)
        if not cleanup:
            self.first_sample_pending = False
        self.last_write = written
        self.commands += len(batch)
        until = min(self.hard_deadline if cleanup else min(self.deadline, self.stage_deadline), written + ACK_NS)
        while time.perf_counter_ns() < until:
            state = self.read_state(cleanup)
            if not cleanup:
                self.observations()
            if self._acknowledged(state, batch, before):
                require(time.perf_counter_ns() < until, "original acknowledgement deadline consumed by state read")
                if sample is not None:
                    self.check_target(state, sample)
                else:
                    require(state["controls"]["gripL"] == 0, "cleanup grip did not commit")
                self.last_commit = time.perf_counter_ns()
                self.pending_command = False
                self.state = state
                self.held_left = False if cleanup or release_only else sample["grip"] == 1
                if lines[:2] == LEFT_POSE_SETUP:
                    self.pose_setup_established = True
                self.save_owner(False)
                self.artifacts.event({"type": "command_commit", "writtenNs": written, "committedNs": self.last_commit,
                    "sequence": self.sequence, "frame": state["frame"], "endFrames": state["endFrames"],
                    "handL": state["handL"], "gripL": state["controls"]["gripL"], "cleanup": cleanup}, cleanup=cleanup)
                return
            time.sleep(.001)
        raise Refusal("original command acknowledgement/commit deadline exceeded")

    def _acknowledged(self, state, batch, before):
        try:
            require(transport.digest(transport.read_bytes(self.command_path, 4096)) == self.command_hash, "competing command writer")
            require(integer(state["cmdSeq"]) in (self.sequence - len(batch), self.sequence), "unexpected command sequence")
            if integer(state["cmdSeq"]) != self.sequence:
                return False
            require(state["lastCmd"] == self.sentinel, "command sentinel changed")
            if not self.ack_path.exists():
                return False
            ack = transport.read_bytes(self.ack_path, 4096).decode("utf-8")
            match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", ack)
            if not match or int(match[1]) != self.sequence:
                return False
            require(match[3] == " | ".join(batch), "acknowledgement batch differs")
            return integer(state["frame"]) > int(match[2]) and integer(state["endFrames"]) > integer(before["endFrames"])
        except BaseException:
            self.unsafe_owner = True
            raise

    def execute(self, stimulus):
        continuation = self.held_left
        require(not continuation or self.pose_setup_established, "held continuation lacks established left pose configuration")
        self.stimulus = validate_stimulus(stimulus, continuation)
        require(self.commands + sum(len(ordinary_commands(s, i == 0)) + 1 for i, s in enumerate(stimulus["schedule"])) + 2 <= MAX_COMMANDS,
                "complete next stimulus and cleanup exceed remaining command budget")
        self.boundaries = {}
        self.plans.append({"family": stimulus["family"], "stage": stimulus["stage"], "source": stimulus["source"],
                           "samples": len(stimulus["schedule"]), "nativeAcceptance": False})
        self.artifacts.save(f"stimulus-{len(self.plans):02}.json", stimulus)
        start = time.perf_counter_ns()
        require(start < integer(stimulus["source"]["validUntilNs"]), "initial mapping expired before ordinary input")
        self.initial_mapping_deadline = integer(stimulus["source"]["validUntilNs"])
        self.first_sample_pending = True
        budget = stimulus["budget"]
        self.stage_deadline = min(self.deadline, start + integer(budget["totalNs"]))
        gesture_start = integer(budget["gestureStartSample"])
        anchor_at, anchor_commit, previous, previous_at = None, None, None, 0
        for index, sample in enumerate(stimulus["schedule"]):
            if index == gesture_start:
                self.stage_deadline = min(self.stage_deadline, time.perf_counter_ns() + integer(budget["maximumGestureNs"]))
            at = integer(sample["atNs"])
            same_pose = previous is not None and all(sample[k] == previous[k] for k in ("positionMeters", "yawPitchRollDegrees"))
            if same_pose and anchor_commit is not None:
                # A delayed final-moving ACK cannot consume passive endpoint dwell.
                due = anchor_commit + at - anchor_at
            else:
                due = max(start + at, self.last_write + (at - previous_at) if previous is not None else start)
            self.pause_until(due)
            if self.target_observed and stimulus["family"] == "sight":
                require(self.held_left, "sight target changed before requested held gesture")
                boundary = next(g for g in stimulus["boundaries"] if g["id"] == "sight_selected")
                self.fresh(boundary=boundary, after_ns=self.last_write)
                self.send(release_only=True)
                self.artifacts.event({"type": "expected_selection_ends_stimulus", "nextSampleOmitted": index,
                    "reason": "Fresh exact family target observed; no further old-rig pose commands.", "nativeAcceptance": False})
                break
            if previous is not None and same_pose and sample["grip"] == previous["grip"]:
                self.artifacts.event({"type": "passive_pose_dwell", "sample": index, "untilNs": due,
                                      "retainedCommandSequence": self.sequence})
            else:
                self.send(sample)
                anchor_at, anchor_commit = at, self.last_commit
            for boundary in stimulus["boundaries"]:
                if boundary["afterSample"] == index:
                    require(bool(boundary["holdGrip"]) == self.held_left, "boundary grip differs")
                    span = integer(boundary.get("minimumObservationSpanNs", 0))
                    self.pause_until(self.last_commit + span)
                    self.fresh(boundary=boundary, after_ns=self.last_write)
            previous, previous_at = sample, at
        require(self.held_left is bool(stimulus["finishGrip"]), "unexpected final grip")
        self.stage_deadline = self.deadline
        self.plans[-1].update(stimulusCompleted=True, boundaryChecksPassed=len(self.boundaries) == len(stimulus["boundaries"]))
        return stimulus

    def run_stimulus(self):
        pickup = None
        original_stage = self.args.stage
        if self.args.pickup_first:
            self.args.stage = "SupplyPickup" if self.args.family == "feed" else "ShellPickup" if self.args.family == "pump" else "Pickup"
        snapshot, _, plan = self.fresh(prepare=True)
        self.mapping = snapshot
        self.execute(plan)
        if self.args.pickup_first:
            pickup = plan
            require(self.held_left, "fresh pickup must preserve actual held custody")
            self.args.stage = original_stage
            snapshot, _, plan = self.fresh(pickup=pickup, prepare=True)
            grip, aim = (transport.planner.from_pose(snapshot["input"]["hands"][0][k]) for k in ("grip", "aim"))
            position_error, angle_error = transport.planner.pose_error(grip, aim)
            require(position_error <= .0002 and angle_error <= .001, "held continuation changed established aim trim")
            self.mapping = snapshot
            self.execute(plan)
        require(not self.held_left, "standalone stimulus may not leave held custody; paired workflow required")

    def cleanup(self):
        if self.pending_command and not self.unsafe_owner:
            until = min(self.hard_deadline - ACK_NS - WRITE_GAP_NS, time.perf_counter_ns() + 100_000_000)
            while time.perf_counter_ns() < until:
                state = self.read_state(True)
                if self._acknowledged(state, self.pending_batch, self.pending_before):
                    self.pending_command = False
                    self.held_left = "grip l 1" in self.pending_batch
                    self.state = state
                    self.save_owner(False)
                    break
                time.sleep(.001)
        if self.pending_command or self.unsafe_owner or not self.held_left:
            return "not attempted: no settled owned held left grip"
        self.send(cleanup=True)
        return "ordinary owned left-grip release acknowledged"

    def close(self):
        joined = self.observer is None or self.observer.close()
        if joined and self.owner is not None:
            if self.lock not in (None, wintypes.HANDLE(-1).value):
                self.owner.api.CloseHandle(self.lock)
                self.lock = None
            self.owner.close()
        # A still-returning observer retains the OS lock until process exit;
        # a subsequent driver cannot overlap its final read/request publication.
        return joined


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--profiles", type=Path, required=True)
    parser.add_argument("--family", choices=("feed", "pump", "fixed", "sight"), required=True)
    parser.add_argument("--stage", required=True)
    parser.add_argument("--step-ms", type=int)
    parser.add_argument("--target", choices=("primary", "secondary"), default="secondary")
    parser.add_argument("--pickup-first", action="store_true", help="continuous fresh pickup then AttachContainer/ShellInsert/Insert")
    parser.add_argument("--root-authorized-ordinary-input", action="store_true")
    args = parser.parse_args(argv)
    driver = None
    result = {"schema": "bc2vr.reload-stimulus-result.v1", "status": "refused", "stimulusCompleted": False,
              "boundaryChecksPassed": False, "nativeAcceptance": False, "headsetAcceptance": False}
    code = 2
    try:
        require(args.root_authorized_ordinary_input, "root ordinary-input authorization required")
        require(args.step_ms is None or 17 <= args.step_ms <= 100, "invalid sample period")
        require(not args.pickup_first or (args.family, args.stage) in (("feed", "AttachContainer"),
                ("pump", "ShellInsert"), ("fixed", "Insert")), "pickup-first needs its matching insertion stage")
        driver = ReloadStimulus.__new__(ReloadStimulus)
        driver.__init__(args)
        driver.run_stimulus()
        result.update(status="ordinary_stimulus_completed", stimulusCompleted=True, boundaryChecksPassed=True)
        code = 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                try:
                    result["release"] = driver.cleanup()
                except Exception as error:
                    result["release"] = "refused: " + str(error)
                    code = 2
                result.update(durationNs=time.perf_counter_ns() - driver.started, commands=driver.commands,
                    observationIncomplete=driver.observation_incomplete, leftGripHeld=driver.held_left,
                    pendingCommand=driver.pending_command, plans=driver.plans, boundaries=driver.boundaries,
                    artifactDirectory=str(driver.artifacts.directory))
            try:
                result["observerStopped"] = driver.close()
                if not result["observerStopped"]:
                    code = 2
                    result["status"] = "observer_shutdown_incomplete"
                if getattr(driver, "artifacts", None):
                    driver.artifacts.save("result.json", result, final=True)
            except Exception as error:
                result["closeError"] = str(error)
                code = 2
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
