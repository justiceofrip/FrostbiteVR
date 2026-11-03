"""ROOT-RUN-ONLY bounded actual-game xr-sim command driver (Windows, stdlib).

The runner must already own a live session and have published startup readiness.
Example: python -B tools/drive_bc2_feed_simulation.py --run G:/.../run01
  --profiles G:/.../run01/profiles.json --stage OpenCover
Use --stage AttachContainer --pickup-first for a continuous chest pickup/attach.
--capture NAME is explicit and consumes the runner's pre-admitted capture budget.
--menu-batches FILE accepts [{"commands":[...],"holdMs":0}] for bounded right-hand
menu input only. No process launch, desktop input, native writes or clock rewrites.
Plans and simulator acknowledgements never establish native reload acceptance.
"""

import argparse
import ctypes
from ctypes import wintypes
import datetime
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import stat
import sys
import time
import uuid

sys.dont_write_bytecode = True
_planner_spec = importlib.util.spec_from_file_location(
    "bc2_feed_simulation_planner", Path(__file__).with_name("plan_bc2_feed_simulation.py"))
planner = importlib.util.module_from_spec(_planner_spec)
_planner_spec.loader.exec_module(planner)

NS = 1_000_000_000
MAX_BYTES = 1024 * 1024
MAX_COMMANDS = 512
SNAPSHOT_PERIOD = 100_000_000
SNAPSHOT_TIMEOUT = 250_000_000
SNAPSHOT_REFRESH_BUDGET = 500_000_000
MAX_SNAPSHOT_REFRESHES = 2
REFUSAL = planner.Refusal
require = planner.require
integer = planner.integer


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def local_path(value):
    path = Path(value)
    require(path.is_absolute() and len(path.drive) == 2 and path.drive[1] == ":"
            and ".." not in path.parts, "absolute local drive path required")
    # Reject links/junctions in every existing ancestor. WOF is an ordinary
    # compressed file, not a path redirect, and is allowed by repository policy.
    for part in (path, *path.parents):
        try:
            info = part.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode), "symbolic link refused: " + str(part))
        if getattr(info, "st_file_attributes", 0) & 0x400:
            require(getattr(info, "st_reparse_tag", 0) == 0x80000017,
                    "redirecting reparse point refused: " + str(part))
    return path


def same_path(a, b):
    return os.path.normcase(os.path.normpath(str(a))) == os.path.normcase(os.path.normpath(str(b)))


def child(root, value):
    path = local_path(value)
    require(path.is_relative_to(root), "artifact must be inside exact run directory")
    return path


def open_snapshot(path):
    """Share replacement with the native publisher; the open handle stays immutable."""
    if os.name != "nt":
        return path.open("rb")
    import msvcrt
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    # OPEN_EXISTING / OPEN_REPARSE_POINT never creates or follows the final link.
    handle = api.CreateFileW(str(path), 0x80000000, 0x1 | 0x2 | 0x4,
                             None, 3, 0x00200000, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        api.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, "rb")


def read_bytes(path, maximum):
    local_path(path)
    # Windows may briefly deny a reader while the native writer atomically
    # replaces its snapshot. Bound the retry; original source leases are still
    # checked after the read and are never renewed by this wait.
    until = time.perf_counter_ns() + 40_000_000
    while True:
        try:
            with (open_snapshot(path) if path.name == "mechanism-snapshot.json" else path.open("rb")) as stream:
                require(os.fstat(stream.fileno()).st_size <= maximum, "oversized file: " + str(path))
                data = stream.read(maximum + 1)
            break
        except (PermissionError, FileNotFoundError):
            if time.perf_counter_ns() >= until:
                raise
            time.sleep(.001)
    require(len(data) <= maximum, "file grew beyond bound: " + str(path))
    return data


def read_json(path, maximum=128 * 1024):
    data = read_bytes(path, maximum)
    def invalid(value):
        raise REFUSAL("invalid JSON constant: " + value)
    return json.loads(data.decode("utf-8-sig"), parse_constant=invalid), data


def atomic(path, data, *, deadline_ns=None, validate=None):
    local_path(path)
    require(len(data) <= MAX_BYTES, "oversized atomic write")
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    published = False
    try:
        with temporary.open("xb") as stream:
            stream.write(data)
            stream.flush()
        local_path(path)
        if deadline_ns is None:
            require(validate is None, "unbounded publication guard")
            os.replace(temporary, path)
        else:
            require(callable(validate), "command publication requires its ownership guard")
            for _ in range(1200):
                require(time.perf_counter_ns() < deadline_ns, "original command publication deadline exceeded")
                validate()
                require(time.perf_counter_ns() < deadline_ns, "original command publication deadline exceeded")
                try:
                    os.replace(temporary, path)
                    break
                except OSError as error:
                    if getattr(error, "winerror", None) not in (5, 32, 33):
                        raise
                    remaining = deadline_ns - time.perf_counter_ns()
                    if remaining <= 0:
                        raise REFUSAL("original command publication deadline exceeded") from error
                    time.sleep(min(.001, remaining / NS))
            else:
                raise REFUSAL("bounded command publication iteration limit")
        published = True
    finally:
        if not published and temporary.exists():
            temporary.unlink()


def publish_command(path, data, deadline_ns, validate):
    """Publish one unchanged batch; a refused replace has not sent input.

    A Windows reader can temporarily deny replacement despite the simulator's
    delete-sharing handle. Retry only that uncommitted filesystem operation,
    within the caller's original budget and with ownership checked each time.
    Never retry a successful replace or create a new command identity.
    """
    atomic(path, data, deadline_ns=deadline_ns, validate=validate)


def check_pin(pin, maximum=128 * 1024 * 1024):
    path = local_path(pin["path"])
    require(path.stat().st_size == integer(pin["bytes"]) <= maximum, "pinned file size changed")
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for _ in range(maximum // 65536 + 1):
            block = stream.read(65536)
            if not block:
                break
            hasher.update(block)
        else:
            raise REFUSAL("pinned file exceeds read bound")
    require(hasher.hexdigest() == pin["sha256"], "pinned file hash changed: " + str(path))
    return path


def start_ticks(value):
    match = re.fullmatch(r"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)(?:\.(\d{1,7}))?(Z|\+00:00)", value)
    require(match is not None, "process start_utc must be exact UTC timestamp")
    stamp = datetime.datetime.strptime(match[1], "%Y-%m-%dT%H:%M:%S")
    seconds = (stamp - datetime.datetime(1601, 1, 1)).days * 86400
    seconds += stamp.hour * 3600 + stamp.minute * 60 + stamp.second
    return seconds * 10_000_000 + int((match[2] or "").ljust(7, "0"))


class WindowsOwner:
    """Retain the exact process handle; never discover or control processes by name."""
    def __init__(self, expected):
        self.expected = expected
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenProcess.restype = wintypes.HANDLE
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        self.api.WaitForSingleObject.restype = wintypes.DWORD
        self.api.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        self.api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                       wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.handle = self.api.OpenProcess(0x1000 | 0x100000, False, integer(expected["pid"]))
        require(bool(self.handle), "cannot retain exact host process handle")
        try:
            self.check()
            times = [wintypes.FILETIME() for _ in range(4)]
            require(self.api.GetProcessTimes(self.handle, *(ctypes.byref(t) for t in times)),
                    "cannot read host creation time")
            actual = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            require(actual == start_ticks(expected["start_utc"]), "host PID creation time changed")
            name = ctypes.create_unicode_buffer(32768)
            size = wintypes.DWORD(len(name))
            require(self.api.QueryFullProcessImageNameW(self.handle, 0, name, ctypes.byref(size)),
                    "cannot read exact host image path")
            require(same_path(local_path(name.value), local_path(expected["path"])), "host image path changed")
        except BaseException:
            self.close()
            raise

    def check(self):
        require(self.api.WaitForSingleObject(self.handle, 0) == 258, "owned host exited or handle failed")

    def close(self):
        if self.handle:
            self.api.CloseHandle(self.handle)
            self.handle = None


class Artifacts:
    def __init__(self, run):
        self.directory = run / ("live-driver-" + uuid.uuid4().hex)
        self.directory.mkdir()
        self.sizes = {}

    def save(self, name, data, final=False):
        if not isinstance(data, bytes):
            data = json_bytes(data)
        # Leave room for the small session-local request/command/owner files as
        # well as the refusal result. Capture pixels use the runner's own budget.
        limit = MAX_BYTES - (4096 if final else 12288)
        require(sum(self.sizes.values()) - self.sizes.get(name, 0) + len(data) <= limit,
                "driver artifact budget would exceed 1 MiB")
        atomic(self.directory / name, data)
        self.sizes[name] = len(data)

    def event(self, value, cleanup=False):
        data = json_bytes(value)
        name = "events.jsonl"
        require(sum(self.sizes.values()) + len(data) <= MAX_BYTES - (8192 if cleanup else 12288),
                "driver artifact budget exhausted")
        with (self.directory / name).open("ab") as stream:
            stream.write(data)
        self.sizes[name] = self.sizes.get(name, 0) + len(data)


def sim_summary(state):
    return {key: state[key] for key in ("pid", "runtime", "cmdSeq", "lastCmd", "lastCmdError",
            "errors", "frame", "endFrames", "captureSeq", "sessionState", "head", "handL", "handR", "controls")}


def stable_snapshot(before, after):
    for key in ("owner", "equipment", "physical_equipment_generation", "rig"):
        require(before[key] == after[key], "native owner/equipment/rig changed: " + key)
    for key in ("configurationPath", "configurationPathPointer", "inventory", "selectedSlot",
                "soleConfiguredArray", "matchedMeshPath", "meshes"):
        require(before["selected"][key] == after["selected"][key], "selected configuration changed: " + key)
    for key in ("spaceGeneration", "trackingBasisGeneration", "worldUnitsPerMeter", "floorRelative"):
        require(before["input"][key] == after["input"][key], "tracking reference changed: " + key)
    poses = [(before["input"][key], after["input"][key]) for key in ("referenceHead", "head")]
    poses += [(before["input"]["hands"][1][key], after["input"]["hands"][1][key]) for key in ("grip", "aim")]
    require(all(a == b for a, b in poses), "reference/head/right-controller input changed")
    # The accepted inverse uses W * inverse(B). Common actor/world movement
    # cancels; separately freezing absolute W and B would reject that invariant.
    relative = [planner.multiply(s["mapping"]["weaponWorldMeters"],
                                 planner.inverse(s["mapping"]["trackingBodyWorldMeters"]))
                for s in (before, after)]
    matrices = [(relative[0], relative[1]),
                (before["mapping"]["anatomicalWristToGrip"], after["mapping"]["anatomicalWristToGrip"])]
    require(all(p <= .002 and a <= .005 for p, a in (planner.pose_error(x, y) for x, y in matrices)),
            "reference/head/right-hand/weapon/body basis changed")


class Driver:
    def admit_snapshot(self, snapshot, now, nonce):
        """Mechanism-specific admission; transport and original leases stay shared."""
        return planner.admit(self.profiles, snapshot, now, nonce)

    def stable_snapshot(self, before, after):
        return stable_snapshot(before, after)

    def validate_publication(self, lines, expected, cleanup):
        """Optional final validation after polling, immediately before publishing."""
        pass

    def __init__(self, args):
        require(os.name == "nt", "live driver requires Windows")
        self.args, self.run = args, local_path(args.run)
        self.owner = None
        self.lock = None
        self.artifacts = None
        self.started = time.perf_counter_ns()
        self.hard_deadline = self.started + 10 * NS
        self.deadline = self.hard_deadline - 300_000_000
        self.commands = 0
        self.last_write = 0
        self.last_commit = None
        self.motion_started = None
        self.held_left = False
        self.held_trigger = False
        self.held_right = False
        self.held_buttons = set()
        self.pending_command = False
        self.unsafe_owner = False
        self.monitor = False
        self.pending_nonce = None
        self.last_request = 0
        self.snapshot_index = 0
        self.reference = None
        self.latest_snapshot = None
        self.snapshot_refresh_started = None
        self.snapshot_refresh_requests = 0
        self.snapshot_readiness = None
        self.neutral_snapshot_approach = False
        self.gap = NS
        self.stage_deadline = self.deadline
        self.plans = []
        self.manifest, _ = read_json(self.run / "run-manifest.json")
        require(self.manifest["schema"] == "fvr.xr-game-run.v1"
                and same_path(self.manifest["output_directory"], self.run), "run manifest mismatch")
        self.link, _ = read_json(self.run / "session-link.json")
        self.startup, _ = read_json(self.run / "sim-startup.json")
        require(self.link["root_commands_ready"] is True and self.startup["root_commands_ready"] is True
                and self.startup["status"] == "ready", "runner startup is not ready for root commands")
        self.host = self.link["host"]
        require(self.startup["host"] == self.host, "startup host identity differs from session link")
        self.folder = local_path(self.link["folder"])
        self.state_path = self.run / "sim" / "state.json"
        self.command_path = self.run / "sim" / "command.txt"
        self.ack_path = self.run / "sim" / "ack.txt"
        for key, expected in (("sim_state", self.state_path), ("command", self.command_path),
                              ("session", self.folder / "session.json"), ("options", self.folder / "options.json"),
                              ("stop_request", self.folder / "stop.request"), ("startup_receipt", self.run / "sim-startup.json")):
            require(same_path(local_path(self.link[key]), expected), "linked path differs: " + key)
        self.session_path = self.folder / "session.json"
        self.request_path = self.folder / "mechanism-snapshot-request.txt"
        self.snapshot_path = self.folder / "mechanism-snapshot.json"
        self.owner = WindowsOwner(self.host)
        self.acquire_lock()
        self.artifacts = Artifacts(self.run)
        self.artifacts.save("invocation.json", {"stage": args.stage, "pickup_first": args.pickup_first,
                            "capture": args.capture, "menu_batches": str(args.menu_batches) if args.menu_batches else None,
                            "host": self.host, "startedNs": self.started, "maximumDurationNs": 10 * NS,
                            "maximumCommands": MAX_COMMANDS, "maximumArtifactBytes": MAX_BYTES,
                            "driverSha256": digest(Path(__file__).read_bytes()),
                            "plannerSha256": digest(Path(planner.__file__).read_bytes())})
        options, _ = read_json(self.folder / "options.json")
        require(options["source_sha256"] == self.manifest["source_sha256"]
                and options["version"] == self.manifest["version"] and options["activate_window"] is False,
                "session options do not match run source/version")
        for key in ("runtime_manifest", "runtime_library", "host_executable"):
            pinned = check_pin(self.manifest[key])
            if key == "runtime_manifest":
                require(same_path(options["xr_manifest"], pinned), "session runtime manifest differs")
            if key == "host_executable":
                require(same_path(self.host["path"], pinned), "pinned host executable differs")
        self.capture_plan = self.manifest["capture_plan"]
        require(self.capture_plan["periodic"] is False and self.capture_plan["layers"] is False
                and 1 <= integer(self.capture_plan["maximum_compositions"]) <= 12
                and 64 <= integer(self.capture_plan["width"]) <= 1024
                and 64 <= integer(self.capture_plan["height"]) <= 768, "invalid pre-admitted capture budget")
        self.state = self.read_state()
        self.capture_sequence = integer(self.state["captureSeq"])
        self.command_hash = digest(read_bytes(self.command_path, 4096))
        self.sequence = integer(self.state["cmdSeq"])
        self.sentinel = self.state["lastCmd"]
        owner_path = self.run / "live-driver-owner.json"
        if owner_path.exists():
            previous, _ = read_json(owner_path, 8192)
            require(previous["host"] == self.host and previous["pending"] is False
                    and previous["commandSha256"] == self.command_hash and previous["sequence"] == self.sequence
                    and previous["sentinel"] == self.sentinel, "another or unsettled command writer owns this session")
        else:
            require(self.sequence == self.startup["target_sequence"]
                    and self.command_hash == self.startup["command_file"]["sha256"]
                    and self.sentinel == self.startup["settled_state"]["lastCmd"], "startup command ownership changed")
        self.initial_sim = self.state
        self.artifacts.save("initial-state.json", self.state)
        self.profiles = None
        if args.stage:
            require(self.link["mechanism_snapshots"]["requested"] is True
                    and read_bytes(self.folder / "mechanism-snapshot.enable", 2) == b"1\n",
                    "session did not opt in to mechanism snapshots before native startup")
            path = child(self.run, args.profiles)
            self.profiles, raw = read_json(path, 8 * 1024 * 1024)
            self.artifacts.save("profile-source.json", {"path": str(path), "sha256": digest(raw), "bytes": len(raw)})

    def acquire_lock(self):
        api = self.owner.api
        api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                   wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        api.CreateFileW.restype = wintypes.HANDLE
        path = local_path(self.run / "live-driver.lock")
        self.lock = api.CreateFileW(str(path), 0x80000000 | 0x40000000, 0, None, 4, 0x80, None)
        require(self.lock not in (None, ctypes.c_void_p(-1).value), "another root command driver holds this run")

    def close(self):
        if getattr(self, "owner", None):
            if self.lock not in (None, ctypes.c_void_p(-1).value):
                self.owner.api.CloseHandle(self.lock)
                self.lock = None
            self.owner.close()

    def read_state(self):
        self.owner.check()
        session, _ = read_json(self.session_path)
        require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                and session["source_sha256"] == self.manifest["source_sha256"]
                and session["version"] == self.manifest["version"] and session["state"] == "running",
                "linked native session is no longer running with the exact owners")
        require(not (self.folder / "stop.request").exists(), "normal session stop was requested")
        state, _ = read_json(self.state_path, 16384)
        require(integer(state["pid"]) == integer(self.host["pid"]) and state["runtime"] == "xr-sim",
                "simulator PID/runtime identity differs")
        require(integer(state["errors"]) == 0 and state["lastCmdError"] == "", "simulator reports an error")
        require(state["sessionRunning"] is True and state["actionsAttached"] is True
                and state["sessionState"] == "FOCUSED" and state["paceMode"] == "free"
                and state["head"]["valid"] and state["handL"]["valid"] and state["handR"]["valid"],
                "simulator session/focus/tracking changed")
        require(integer(state["captureSeq"]) <= integer(self.capture_plan["maximum_compositions"]),
                "simulator exceeded runner capture budget")
        return state

    def ownership(self, state, expected=None):
        sequence, sentinel, command_hash = expected or (self.sequence, self.sentinel, self.command_hash)
        try:
            require(digest(read_bytes(self.command_path, 4096)) == command_hash,
                    "command file replaced by another writer")
            require(integer(state["cmdSeq"]) == sequence and state["lastCmd"] == sentinel,
                    "command sequence/sentinel belongs to another writer")
        except BaseException:
            self.unsafe_owner = True
            raise

    def owner_bytes(self, pending):
        return json_bytes({"host": self.host,
               "pending": pending, "sequence": self.sequence, "sentinel": self.sentinel,
               "commandSha256": self.command_hash, "artifactDirectory": str(self.artifacts.directory)})

    def save_owner(self, pending):
        atomic(self.run / "live-driver-owner.json", self.owner_bytes(pending))

    def bounds(self, now):
        require(now < self.deadline, "ten-second driver deadline reached")
        require(now < self.stage_deadline, "actual stage exceeded maximumGestureNs")
        readiness = getattr(self, "snapshot_readiness", None)
        if readiness is not None:
            require(now < readiness["deadlineNs"], "original neutral snapshot observation deadline reached")
        if self.snapshot_refresh_started is not None:
            require(now - self.snapshot_refresh_started < SNAPSHOT_REFRESH_BUDGET,
                    "expired snapshot refresh budget exhausted")
        # A held pose still produces a new real XR packet every runtime frame.
        # Command acknowledgement cadence is not the native input-packet gap;
        # fresh snapshot leases and the normal recognizer enforce that lifetime.

    def request_snapshot(self):
        now = time.perf_counter_ns()
        require(self.pending_nonce is None, "snapshot request already pending")
        existing = 0
        if self.request_path.exists():
            raw = read_bytes(self.request_path, 22)
            require(re.fullmatch(rb"[1-9][0-9]{0,19}\r?\n?", raw) is not None, "unexpected snapshot request writer")
            existing = int(raw)
        if self.snapshot_path.exists():
            previous, _ = read_json(self.snapshot_path, planner.SNAPSHOT_LIMIT)
            existing = max(existing, integer(previous["nonce"]))
        nonce = max(time.time_ns(), existing + 1)
        require(0 < nonce < 2**64, "snapshot nonce overflow")
        raw = (str(nonce) + "\n").encode("ascii")
        atomic(self.request_path, raw)
        published = time.perf_counter_ns()
        self.snapshot_index += 1
        self.pending_nonce, self.last_request, self.request_bytes = nonce, now, raw
        self.last_request_published = published
        self.artifacts.save(f"request-{self.snapshot_index:03}.txt", raw)
        self.artifacts.save(f"request-{self.snapshot_index:03}-timing.json", {
            "nonce": nonce, "clock": "perf_counter_ns", "requestStartedNs": now,
            "requestPublishedNs": published, "responseDeadlineNs": now + SNAPSHOT_TIMEOUT,
            "budgetNs": SNAPSHOT_TIMEOUT})

    def snapshot_failure(self):
        path = self.snapshot_path.with_name("mechanism-snapshot-failure.json")
        if not path.exists():
            return None
        raw = read_bytes(path, 1024)
        # The native worker retains this CREATE_NEW handle and only rewrites it
        # on terminal failure. A partial in-place update is never an outcome.
        if not raw.endswith(b"\n"):
            return None
        try:
            value = json.loads(raw)
        except (UnicodeError, json.JSONDecodeError):
            return None
        require(isinstance(value, dict) and value.get("schema") == "fvr.bc2.mechanism-snapshot-failure.v1",
                "unexpected snapshot failure record")
        nonce = integer(value["nonce"])
        if nonce != integer(value["nonce_end"]) or nonce != integer(value["publication"]["nonce"]):
            return None
        require(nonce <= self.pending_nonce, "snapshot failure belongs to a later competing request")
        if nonce != self.pending_nonce:
            return None
        require(value["publication"]["status"] == "failed", "nonterminal snapshot failure record")
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}-failure.json", raw)
        return value["publication"]

    def snapshot_readiness_scope(self):
        """Only idle observation or an explicitly declared pre-gesture approach."""
        if self.motion_started is None:
            return "idle"
        if getattr(self, "neutral_snapshot_approach", False):
            return "neutral_approach"
        return None

    def check_snapshot_readiness_owner(self):
        readiness = self.snapshot_readiness
        require(self.snapshot_readiness_scope() == readiness["scope"]
                and not (self.pending_command or self.unsafe_owner or self.held_left
                         or self.held_right or self.held_trigger or self.held_buttons),
                "neutral snapshot observation scope/held/pending authority changed")
        self.bounds(time.perf_counter_ns())
        require((self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit)
                == readiness["commandIdentity"], "neutral snapshot command identity changed")
        require(self.last_write == 0 or (self.last_commit is not None and self.last_commit >= self.last_write),
                "neutral snapshot observation requires a settled command")
        state = self.read_state()
        self.ownership(state)
        require(integer(state["captureSeq"]) == self.capture_sequence,
                "unrequested capture during neutral snapshot observation")
        require(all(state[key] == readiness["state"][key] for key in ("head", "handL", "handR", "controls")),
                "neutral snapshot simulator input changed")
        require(state["controls"] and all(
            all(component == 0 for component in value) if isinstance(value, list) else value == 0
            for value in state["controls"].values()), "neutral snapshot observation contains active input")
        self.bounds(time.perf_counter_ns())

    def check_snapshot_readiness_response(self, snapshot, now):
        readiness = self.snapshot_readiness
        self.check_snapshot_readiness_owner()
        require(self.last_request <= integer(snapshot["observed_ns"]) <= now,
                "neutral snapshot response predates its request or is from the future")
        # Sight's selected-target adapter permits an intentional configuration
        # transition. Neutral observation has no such authority: retain the
        # exact source owner/configuration, then run the adapter's extra gates.
        stable_snapshot(readiness["reference"], snapshot)
        self.stable_snapshot(readiness["reference"], snapshot)
        require(all(hand["squeeze"] == hand["trigger"] == hand["held"]
                    == hand["stickX"] == hand["stickY"] == 0 for hand in snapshot["input"]["hands"]),
                "neutral snapshot response contains active input")

    def snapshot_polls(self, ordinary_limit):
        # The old iteration guard remains outside observation loss. A neutral
        # wait consumes its captured original stage/driver deadline, including
        # shorter initial capture-demand windows; it gets no new timeout clock.
        count = 0
        while count < ordinary_limit or getattr(self, "snapshot_readiness", None) is not None:
            self.bounds(time.perf_counter_ns())
            yield count
            count += 1

    def receive_late_snapshot(self):
        self.check_snapshot_readiness_owner()
        if not self.snapshot_path.exists():
            return None
        snapshot, raw = read_json(self.snapshot_path, planner.SNAPSHOT_LIMIT)
        nonce = integer(snapshot["nonce"])
        require(nonce <= self.pending_nonce, "snapshot response belongs to a later competing request")
        if nonce != self.pending_nonce:
            return None
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}-late.json", raw)
        require(snapshot.get("valid") is True, "native snapshot refused: " + str(snapshot.get("invalid_reason")))
        self.check_snapshot_readiness_response(snapshot, time.perf_counter_ns())
        # Validate the original observation only to reject identity/lease faults.
        # A timed-out response NEVER becomes current command authority, even if
        # it happens to contain an unexpired source when finally read.
        self.admit_snapshot(snapshot, integer(snapshot["observed_ns"]), nonce)
        readiness = self.snapshot_readiness
        require(readiness["freshRequests"] < MAX_SNAPSHOT_REFRESHES,
                "neutral snapshot reacquisition request budget exhausted")
        self.check_snapshot_readiness_owner()
        readiness["freshRequests"] += 1
        readiness["awaitingLate"] = False
        readiness["discardedInputGeneration"] = integer(snapshot["shot_input_generation"])
        self.artifacts.event(dict(type="snapshot_late_response_discarded", nonce=nonce,
            observedNs=snapshot["observed_ns"], sourceDeadlineNs=self.snapshot_source_deadline(snapshot),
            evaluatedNs=time.perf_counter_ns(), observationDeadlineNs=readiness["deadlineNs"],
            freshRequest=readiness["freshRequests"], commandAuthority=False))
        require(read_bytes(self.request_path, 22) == self.request_bytes,
                "snapshot request changed by another writer")
        self.pending_nonce = None
        self.request_snapshot()
        return None

    def snapshot_timeout(self, now, response=None):
        """Preserve a late/stale response as refused evidence, never as admission."""
        read_error = None
        if response is None and self.snapshot_path.exists():
            try:
                response = read_json(self.snapshot_path, planner.SNAPSHOT_LIMIT)
            except (OSError, ValueError, REFUSAL) as exc:
                read_error = str(exc)
        if response is not None:
            snapshot, raw = response
            self.artifacts.save(f"snapshot-{self.snapshot_index:03}-at-timeout.json", raw)
            self.last_observed_response_nonce = snapshot.get("nonce") if isinstance(snapshot, dict) else None
        event = {"type": "snapshot_response_timeout", "nonce": self.pending_nonce,
                 "clock": "perf_counter_ns", "requestStartedNs": self.last_request,
                 "requestPublishedNs": getattr(self, "last_request_published", None),
                 "timeoutEvaluatedNs": now, "elapsedNs": now - self.last_request,
                 "responseDeadlineNs": self.last_request + SNAPSHOT_TIMEOUT,
                 "budgetNs": SNAPSHOT_TIMEOUT,
                 "lastObservedResponseNonce": getattr(self, "last_observed_response_nonce", None),
                 "diagnosticReadError": read_error}
        self.artifacts.event(event)
        scope = self.snapshot_readiness_scope()
        reference = self.reference if self.reference is not None else self.latest_snapshot
        if (scope is not None and reference is not None and self.snapshot_refresh_started is None
                and not (self.pending_command or self.unsafe_owner or self.held_left
                         or self.held_right or self.held_trigger or self.held_buttons)):
            if getattr(self, "snapshot_readiness", None) is None:
                self.snapshot_readiness = dict(scope=scope, startedNs=now,
                    deadlineNs=min(self.deadline, self.stage_deadline), reference=reference,
                    commandIdentity=(self.sequence, self.sentinel, self.command_hash, self.last_write, self.last_commit),
                    state=self.state, freshRequests=0, awaitingLate=True)
            self.check_snapshot_readiness_owner()
            self.snapshot_readiness["awaitingLate"] = True
            self.latest_snapshot = None
            self.artifacts.event(dict(type="snapshot_neutral_observation_hold", nonce=self.pending_nonce,
                evaluatedNs=now, observationDeadlineNs=self.snapshot_readiness["deadlineNs"],
                originalResponseDeadlineNs=self.last_request + SNAPSHOT_TIMEOUT,
                scope=scope, commandAuthority=False))
            return None
        raise REFUSAL("snapshot response not received within request budget: "
                      f"nonce={self.pending_nonce} elapsed_ns={now - self.last_request} "
                      f"budget_ns={SNAPSHOT_TIMEOUT}")

    def snapshot_source_deadline(self, snapshot):
        """Adapters may include shorter original mechanism evidence leases."""
        return min(integer(snapshot["valid_until_ns"]),
                   integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)

    def defer_invalid_snapshot(self, snapshot, now):
        """Optional observation-only hold of this exact consumed response.

        The caller has checked writer, failure, nonce and original response
        deadline, and retained its original bytes. Returning True cannot admit
        the invalid frame; an adapter must explicitly request a fresh response.
        """
        return False

    def receive_snapshot(self):
        if self.pending_nonce is None:
            return None
        require(read_bytes(self.request_path, 22) == self.request_bytes, "snapshot request changed by another writer")
        failure = self.snapshot_failure()
        now = time.perf_counter_ns()
        if failure is not None:
            self.artifacts.event({"type": "snapshot_publication_failure", "nonce": self.pending_nonce,
                                  "receivedNs": now, "requestStartedNs": self.last_request,
                                  "requestPublishedNs": getattr(self, "last_request_published", None),
                                  "publication": failure})
            raise REFUSAL("native snapshot publication failed: "
                          f"nonce={self.pending_nonce} stage={failure['stage']} "
                          f"win32_error={failure['win32_error']} attempts={failure['attempts']}")
        readiness = getattr(self, "snapshot_readiness", None)
        if readiness is not None:
            self.check_snapshot_readiness_owner()
            if readiness["awaitingLate"]:
                return self.receive_late_snapshot()
        if now - self.last_request > SNAPSHOT_TIMEOUT:
            return self.snapshot_timeout(now)
        if not self.snapshot_path.exists():
            return None
        snapshot, raw = read_json(self.snapshot_path, planner.SNAPSHOT_LIMIT)
        nonce = integer(snapshot["nonce"])
        self.last_observed_response_nonce = nonce
        now = time.perf_counter_ns()
        if now - self.last_request > SNAPSHOT_TIMEOUT:
            return self.snapshot_timeout(now, (snapshot, raw))
        require(nonce <= self.pending_nonce, "snapshot response belongs to a later competing request")
        if nonce != self.pending_nonce:
            return None
        # Preserve every matching response, including expired and invalid ones.
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}.json", raw)
        if snapshot.get("valid") is not True:
            if self.defer_invalid_snapshot(snapshot, now):
                return None
            raise REFUSAL("native snapshot refused: " + str(snapshot.get("invalid_reason")))
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        evaluated = time.perf_counter_ns()
        if evaluated - self.last_request > SNAPSHOT_TIMEOUT:
            return self.snapshot_timeout(evaluated, (snapshot, raw))
        source_deadline = self.snapshot_source_deadline(snapshot)
        if evaluated >= source_deadline:
            # Validate the expired diagnostic at its recorded native publication
            # time only to distinguish delayed delivery from invalid admission.
            # It is never accepted as current evidence or used for a command;
            # no source/claim timestamp, owner, or deadline is changed.
            if readiness is not None:
                self.check_snapshot_readiness_response(snapshot, evaluated)
            self.admit_snapshot(snapshot, integer(snapshot["observed_ns"]), nonce)
            if self.snapshot_refresh_started is None:
                self.snapshot_refresh_started = evaluated
                self.snapshot_refresh_requests = 0
            self.bounds(time.perf_counter_ns())
            if readiness is not None:
                require(readiness["freshRequests"] < MAX_SNAPSHOT_REFRESHES,
                        "neutral snapshot reacquisition request budget exhausted")
                self.check_snapshot_readiness_owner()
                readiness["freshRequests"] += 1
            else:
                require(self.snapshot_refresh_requests < MAX_SNAPSHOT_REFRESHES,
                        "expired snapshot refresh request budget exhausted")
            self.snapshot_refresh_requests += 1
            self.artifacts.event({"type": "snapshot_refresh_hold", "nonce": snapshot["nonce"],
                                  "evaluatedNs": evaluated, "sourceDeadlineNs": source_deadline,
                                  "refreshRequest": self.snapshot_refresh_requests,
                                  "lastCommandWrittenNs": self.last_write})
            self.pending_nonce = None
            self.request_snapshot()
            return None
        self.pending_nonce = None
        return snapshot, raw, evaluated

    def accept_snapshot(self, snapshot, raw, now):
        self.admit_snapshot(snapshot, now, integer(snapshot["nonce"]))
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        if getattr(self, "snapshot_readiness", None) is not None:
            readiness = self.snapshot_readiness
            require(not readiness["awaitingLate"], "late snapshot cannot supply command authority")
            self.check_snapshot_readiness_response(snapshot, now)
            require(integer(snapshot["shot_input_generation"]) > readiness["discardedInputGeneration"]
                    and integer(snapshot["input_evidence"]["observedNs"]) >= self.last_write,
                    "neutral snapshot reacquisition requires new input after the settled command")
            evaluated = time.perf_counter_ns()
            require(evaluated - self.last_request <= SNAPSHOT_TIMEOUT,
                    "neutral snapshot reacquisition exceeded original response deadline")
            require(evaluated < self.snapshot_source_deadline(snapshot),
                    "neutral snapshot reacquisition consumed original source lease")
            self.artifacts.event(dict(type="snapshot_neutral_observation_resumed", nonce=snapshot["nonce"],
                evaluatedNs=now, heldNs=now-readiness["startedNs"],
                observationDeadlineNs=readiness["deadlineNs"], freshRequests=readiness["freshRequests"]))
            self.snapshot_readiness = None
        self.latest_snapshot = snapshot
        if self.snapshot_refresh_started is not None:
            self.bounds(time.perf_counter_ns())
            self.artifacts.event({"type": "snapshot_refresh_resumed", "nonce": snapshot["nonce"],
                                  "evaluatedNs": now,
                                  "heldNs": now - self.snapshot_refresh_started,
                                  "refreshRequests": self.snapshot_refresh_requests})
            self.snapshot_refresh_started = None
            self.snapshot_refresh_requests = 0

    def pump(self):
        now = time.perf_counter_ns()
        self.bounds(now)
        if self.monitor:
            received = self.receive_snapshot()
            if received:
                self.accept_snapshot(*received)
            if self.pending_nonce is None and now - self.last_request >= SNAPSHOT_PERIOD:
                self.request_snapshot()

    def pause_until(self, deadline):
        for _ in range(6000):
            self.pump()
            # Holding sends no command: xr-sim keeps the last pose/grip and
            # produces its ordinary XR frames. Do not advance the trajectory
            # until a replacement passes current-time native admission.
            if (time.perf_counter_ns() >= deadline and self.snapshot_refresh_started is None
                    and getattr(self, "snapshot_readiness", None) is None):
                return
            time.sleep(.001)
        raise REFUSAL("bounded polling iteration limit")

    def fresh(self):
        self.monitor = False
        # Preserve an already pending publication, including any refusal, then
        # request the fresh source used to plan the next stage.
        if self.pending_nonce is not None:
            for _ in self.snapshot_polls(800):
                self.bounds(time.perf_counter_ns())
                received = self.receive_snapshot()
                if received:
                    self.accept_snapshot(*received)
                    break
                time.sleep(.001)
            require(self.pending_nonce is None, "pending snapshot did not complete")
        self.pause_until(self.last_request + SNAPSHOT_PERIOD)
        self.request_snapshot()
        for _ in self.snapshot_polls(800):
            self.bounds(time.perf_counter_ns())
            received = self.receive_snapshot()
            if received:
                self.accept_snapshot(*received)
                return received
            time.sleep(.001)
        raise REFUSAL("fresh snapshot request iteration bound")

    def send(self, lines, expected=None, capture=None, cleanup=False, source_deadline=None,
             final_release=False):
        require(0 < len(lines) <= 8 and all(isinstance(x, str) and "\n" not in x and "\r" not in x for x in lines),
                "invalid command batch")
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget exhausted")
        if not cleanup:
            self.pause_until(max(self.last_write + 25_000_000, time.perf_counter_ns()))
        else:
            remaining = self.last_write + 25_000_000 - time.perf_counter_ns()
            if remaining > 0:
                time.sleep(remaining / NS)
        before = self.read_state()
        self.ownership(before)
        require(integer(before["captureSeq"]) == self.capture_sequence, "unrequested capture occurred")
        if self.reference is not None and not cleanup:
            for key in ("head", "handR"):
                require(before[key] == self.initial_sim[key], "simulator head/right-hand basis changed")
        sentinel = "note root-feed-" + uuid.uuid4().hex
        batch = lines + [sentinel]
        require(len(" | ".join(batch).encode("ascii")) < 508, "batch exceeds complete xr-sim acknowledgement bound")
        target = self.sequence + len(batch)
        raw = ("\n".join(batch) + "\n").encode("ascii")
        self.validate_publication(lines, expected, cleanup)
        require(getattr(self, "snapshot_readiness", None) is None,
                "command publication forbidden during neutral snapshot observation")
        written = time.perf_counter_ns()
        if final_release:
            require(not cleanup and capture is None and expected is not None
                    and expected["phase"] == "release" and expected["grip"] == 0
                    and lines == expected["commands"] and len(lines) == 2
                    and lines[0] == "hand l grip pose " + " ".join(
                        f"{v:.9f}" for v in expected["positionMeters"] + expected["yawPitchRollDegrees"])
                    and lines[1] == "grip l 0" and self.monitor
                    and self.motion_started is not None and self.held_left
                    and not (self.pending_command or self.unsafe_owner or self.held_right
                             or self.held_trigger or self.held_buttons),
                    "observation handoff requires the exact planned final ordinary release")
            self.admit_snapshot(self.latest_snapshot, written, integer(self.latest_snapshot["nonce"]))
            self.stable_snapshot(self.reference, self.latest_snapshot)
        if source_deadline is not None:
            require(written < source_deadline, "original source/claim lease expired before command publication")
        if self.last_commit is not None and not cleanup:
            self.bounds(written)
        self.artifacts.event({"type": "command_request", "atNs": written, "beforeSequence": self.sequence,
                              "targetSequence": target, "commands": batch}, cleanup=cleanup)
        until = min(self.deadline, self.stage_deadline,
                    written + (250_000_000 if self.motion_started is not None else NS))
        if cleanup:
            until = min(self.hard_deadline, written + 250_000_000)
        publication_deadline = min(until, source_deadline) if source_deadline is not None else until
        previous_owner = self.sequence, self.sentinel, self.command_hash
        self.sequence, self.sentinel, self.command_hash = target, sentinel, digest(raw)
        self.pending_command = True
        self.pending_batch, self.pending_before = batch, before
        pending_owner = self.owner_bytes(True)
        try:
            self.save_owner(True)
        except BaseException:
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            raise
        def validate_publication_owner():
            if self.last_commit is not None and not cleanup:
                self.bounds(time.perf_counter_ns())
            try:
                require(read_bytes(self.run / "live-driver-owner.json", 8192) == pending_owner,
                        "command owner record replaced before publication")
                current = self.read_state()
                self.ownership(current, expected=previous_owner)
                require(integer(current["captureSeq"]) == self.capture_sequence, "unrequested capture before publication")
                if self.reference is not None and not cleanup:
                    require(all(current[key] == self.initial_sim[key] for key in ("head", "handR")),
                            "simulator head/right-hand basis changed before publication")
            except BaseException:
                self.unsafe_owner = True
                raise
        try:
            publish_command(self.command_path, raw, publication_deadline, validate_publication_owner)
        except BaseException as error:
            # Atomic replacement never committed. No target sequence or held
            # input is ours to settle; cleanup retains the prior exact command.
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            if not self.unsafe_owner:
                try:
                    require(read_bytes(self.run / "live-driver-owner.json", 8192) == pending_owner,
                            "unpublished owner record replaced")
                    self.ownership(self.read_state())
                    self.save_owner(False)
                except BaseException as owner_error:
                    self.unsafe_owner = True
                    raise REFUSAL(f"{error}; unpublished owner restoration refused: {owner_error}") from error
            raise
        published = time.perf_counter_ns()
        if final_release:
            # Release may retire the feed presentation before xr-sim's commit
            # is observed. Only this successfully published final plan batch
            # stops command admission. Keep its pending snapshot/leases intact
            # for the read-only observer after the exact bounded acknowledgement.
            self.monitor = False
        self.last_write = published
        self.commands += len(batch)
        ack, settled = None, None
        for _ in range(1200):
            if not cleanup:
                self.pump()
            require(time.perf_counter_ns() < until, "command acknowledgement/commit deadline exceeded")
            state = self.read_state()
            require(digest(read_bytes(self.command_path, 4096)) == self.command_hash, "concurrent command writer")
            seq = integer(state["cmdSeq"])
            require(seq in (target - len(batch), target), "unexpected command sequence advancement")
            if seq == target:
                require(state["lastCmd"] == sentinel, "unique command sentinel mismatch")
                if self.ack_path.exists():
                    ack_raw = read_bytes(self.ack_path, 4096).decode("utf-8")
                    match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", ack_raw)
                    if match and int(match[1]) == target:
                        require(match[3] == " | ".join(batch), "acknowledgement batch text mismatch")
                        ack = (int(match[2]), ack_raw)
                if ack and integer(state["frame"]) > ack[0] and integer(state["endFrames"]) > integer(before["endFrames"]):
                    if expected:
                        self.check_target(state, expected)
                    if capture is None:
                        require(integer(state["captureSeq"]) == self.capture_sequence, "unexpected capture during command")
                    elif integer(state["captureSeq"]) != self.capture_sequence + 1:
                        require(integer(state["captureSeq"]) <= self.capture_sequence + 1,
                                "capture sequence advanced beyond the single requested image")
                        time.sleep(.001)
                        continue
                    settled = state
                    break
            time.sleep(.001)
        require(settled is not None, "bounded acknowledgement iteration limit")
        committed = time.perf_counter_ns()
        if not cleanup:
            self.bounds(committed)
        self.last_commit = committed
        self.pending_command = False
        self.capture_sequence = integer(settled["captureSeq"])
        self.state = settled
        self.save_owner(False)
        self.artifacts.event({"type": "command_commit", "requestedNs": written, "writtenNs": published, "committedNs": committed,
                              "ack": ack[1], "state": sim_summary(settled), "cleanup": cleanup}, cleanup=cleanup)
        self.remember_controls(lines)
        return settled

    def remember_controls(self, lines):
        for line in lines:
            if line.startswith("grip l "):
                self.held_left = float(line.split()[-1]) > 0
            if line.startswith("trigger r "):
                self.held_trigger = float(line.split()[-1]) > 0
            if line.startswith("grip r "):
                self.held_right = float(line.split()[-1]) > 0
            if line.startswith("btn "):
                _, button, action = line.split()
                if action == "down":
                    self.held_buttons.add(button)
                else:
                    self.held_buttons.discard(button)

    @staticmethod
    def check_target(state, target):
        require(abs(float(state["controls"]["gripL"]) - target["grip"]) < .001, "left grip did not commit")
        require(state["handL"]["followsHead"] is False, "left hand unexpectedly follows head")
        require(math.dist(state["handL"]["pos"], target["positionMeters"]) <= .0002,
                "left controller position did not commit")
        actual = planner.ypr_quaternion(state["handL"]["ypr"])
        expected = planner.ypr_quaternion(target["yawPitchRollDegrees"])
        a = planner.from_pose({"position": [0, 0, 0], "orientation": actual})
        b = planner.from_pose({"position": [0, 0, 0], "orientation": expected})
        require(planner.pose_error(a, b)[1] <= .001, "left controller orientation did not commit")

    def fresh_stage_ready(self, name):
        received = self.fresh()
        if received[0]["feed"] is not None or name == "AttachContainer":
            return received
        # A valid body/rig observation can precede the native feed's idle view
        # during draw/equip animation. Stay released until ordinary native
        # publication arms neutral; a missed squeeze cannot acquire later while
        # held. These observations never extend a source or gesture deadline.
        baseline = received[0]
        started = time.perf_counter_ns()
        for _ in range(100):
            snapshot, raw, evaluated = received
            self.bounds(time.perf_counter_ns())
            require(self.motion_started is None and not self.pending_command and not self.unsafe_owner
                    and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons),
                    "feed readiness requires neutral input before stage motion")
            state = self.read_state()
            self.ownership(state)
            require(all(state[key] == self.state[key] for key in ("head", "handL", "handR", "controls")),
                    "feed readiness simulator input changed")
            self.stable_snapshot(baseline, snapshot)
            require(snapshot["input_evidence"]["released"][0] is True
                    and all(hand["squeeze"] == 0 and hand["trigger"] == 0 and hand["held"] == 0
                            and hand["stickX"] == 0 and hand["stickY"] == 0 for hand in snapshot["input"]["hands"]),
                    "feed readiness contains held input")
            require(all(p <= .0002 and a <= .001 for p, a in
                        (planner.pose_error(planner.from_pose(baseline["input"]["hands"][0][key]),
                                            planner.from_pose(snapshot["input"]["hands"][0][key]))
                         for key in ("grip", "aim"))), "feed readiness left-controller pose changed")
            feed = snapshot["feed"]
            if feed is not None:
                require(feed["input"]["released"][0] is True and feed["mechanismClaim"] is None
                        and feed["mechanismInput"] is None and feed["supplyObject"] is None,
                        "feed readiness requires a fresh unclaimed neutral publication")
                self.artifacts.event({"type": "feed_stage_ready", "stage": name, "nonce": snapshot["nonce"],
                                      "waitedNs": time.perf_counter_ns() - started,
                                      "registrationKey": feed["registrationKey"], "nativeReceiptProof": False})
                return received
            self.artifacts.event({"type": "feed_stage_readiness_wait", "stage": name,
                                  "nonce": snapshot["nonce"], "evaluatedNs": evaluated,
                                  "reason": "native_feed_not_published"})
            received = self.fresh()
        raise REFUSAL("feed stage readiness observation iteration bound")

    def stage(self, name, pickup=None):
        snapshot, raw, evaluated = self.fresh_stage_ready(name)
        row = planner.admit(self.profiles, snapshot, evaluated, integer(snapshot["nonce"]))
        policy = row["operations"]["AttachContainer" if name == "SupplyPickup" else name]["motion"]["policy"]
        step = self.args.step_ms * 1_000_000 if self.args.step_ms else min(60_000_000, integer(policy["maxInputGapNs"]) // 2)
        # No file/tool/model round trip intervenes between fresh publication and
        # planning. This is the real local QPC clock used by native snapshots.
        evaluated = time.perf_counter_ns()
        if name == "SupplyPickup":
            plan = planner.plan_supply_pickup(self.profiles, snapshot, evaluated, integer(snapshot["nonce"]), step)
        else:
            plan = planner.plan(self.profiles, snapshot, name, evaluated, integer(snapshot["nonce"]), step,
                                **({"pickup_plan": pickup} if pickup is not None else {}))
        require(time.perf_counter_ns() < min(integer(snapshot["valid_until_ns"]),
                integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD), "planning consumed original source lease")
        if self.reference is not None:
            self.stable_snapshot(self.reference, snapshot)
        self.reference = snapshot
        self.latest_snapshot = snapshot
        self.artifacts.save(f"snapshot-{self.snapshot_index:03}.json", raw)
        self.artifacts.save(f"plan-{len(self.plans) + 1:02}-{name}.json", plan)
        self.plans.append({"stage": name, "nonce": snapshot["nonce"], "samples": len(plan["schedule"]),
                           "nominalDurationNs": plan["durationNs"], "stepNs": step,
                           "feedObservationBefore": snapshot.get("feedObservation")})
        self.gap = integer(plan["policy"]["maxInputGapNs"])
        maximum = min(10 * NS, integer(plan["policy"]["maximumGestureNs"]))
        require(0 < len(plan["schedule"]) <= MAX_COMMANDS and integer(plan["durationNs"]) <= maximum,
                "plan exceeds bounded stage budget")
        require(self.commands + sum(len(target["commands"]) + 1 for target in plan["schedule"]) + 2 <= MAX_COMMANDS,
                "complete plan and ordinary release exceed remaining command budget")
        start = time.perf_counter_ns()
        self.motion_started = start
        self.stage_deadline = min(self.deadline, start + maximum)
        self.monitor = True
        last_time = -1
        acquisition_deadline = None
        for target in plan["schedule"]:
            at = integer(target["atNs"])
            require(at > last_time and at <= maximum, "nonmonotonic or oversized plan")
            last_time = at
            # Hold each pose for the planner's tested period even when a previous
            # acknowledgement took longer. Never catch up by compressing dwell.
            due = start + at if self.last_commit is None else max(start + at, self.last_write + step)
            self.pause_until(due)
            source_deadline = min(integer(plan["sourceValidUntilNs"]),
                                  integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)
            self.send(target["commands"], expected=target,
                      source_deadline=source_deadline if target is plan["schedule"][0] else acquisition_deadline,
                      final_release=name != "SupplyPickup" and target is plan["schedule"][-1])
            acquisition_deadline = None
            if target["phase"] == "acquire" and plan["requiresPostAcquisitionReobservation"]:
                acquisition_deadline = self.observe_acquisition(row, plan, self.last_write)
        self.plans[-1]["actualDurationNs"] = time.perf_counter_ns() - start
        self.motion_started = None
        self.stage_deadline = self.deadline
        if name == "SupplyPickup":
            fresh, fresh_raw, fresh_now = self.fresh()
            self.accept_snapshot(fresh, fresh_raw, fresh_now)
            held = planner.held_supply(self.profiles, row, fresh, time.perf_counter_ns(), plan)
            self.plans[-1]["postPickupHeldSupply"] = held
            self.plans[-1]["postStageNonce"] = fresh["nonce"]
            self.plans[-1]["feedObservationAfter"] = fresh.get("feedObservation")
        else:
            self.observe_stage_receipt(snapshot)
        return plan

    def observe_acquisition(self, row, plan, command_written_ns):
        # Hold the committed pose and squeeze while ordinary XR frames continue.
        # No move, synthetic input or extended deadline may stand in for a claim.
        self.plans[-1]["acquisitionCommandWrittenNs"] = command_written_ns
        snapshot, raw, evaluated = self.fresh()
        self.accept_snapshot(snapshot, raw, evaluated)
        self.plans[-1]["postAcquisitionNonce"] = snapshot["nonce"]
        claim = planner.held_feed(self.profiles, row, snapshot, time.perf_counter_ns(), plan, command_written_ns)
        self.plans[-1]["postAcquisitionClaim"] = claim
        self.artifacts.event({"type": "feed_acquisition_observed", "stage": plan["stage"],
                              "nonce": snapshot["nonce"], "claim": claim,
                              "commandWrittenNs": command_written_ns, "nativeReceiptProof": False})
        self.monitor = True
        return min(integer(snapshot["valid_until_ns"]), integer(claim["deadlineNs"]),
                   integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)

    def observe_stage_receipt(self, before):
        """A bounded diagnostic gate, never a replacement for native trace review."""
        baseline = before["feedObservation"]
        require(baseline["observations"] == "independent_atomics_not_native_proof", "unknown feed observation contract")
        deadline = min(self.deadline, time.perf_counter_ns() + 600_000_000)
        retained = self.latest_snapshot
        for _ in range(4):
            require(time.perf_counter_ns() < deadline, "post-stage receipt observation timed out")
            snapshot, raw, evaluated = self.fresh_postrelease_observation(deadline)
            self.check_postrelease_observation(retained, snapshot, evaluated)
            observation = snapshot["feedObservation"]
            require(observation["observations"] == "independent_atomics_not_native_proof", "feed observation contract changed")
            if snapshot["feed"] is None:
                # This is a refused command source, not an admitted mechanism.
                # Retain it and spend only the existing observation budget.
                self.artifacts.event({"type": "postrelease_feed_unpublished", "nonce": snapshot["nonce"],
                                      "evaluatedNs": evaluated, "observationDeadlineNs": deadline,
                                      "lastCommandWrittenNs": self.last_write, "nativeReceiptProof": False})
                continue
            self.accept_snapshot(snapshot, raw, evaluated)
            self.plans[-1]["feedObservationAfter"] = observation
            self.plans[-1]["postStageNonce"] = snapshot["nonce"]
            attempts = integer(observation["submit_attempts"]) - integer(baseline["submit_attempts"])
            accepted = integer(observation["receipts_accepted"]) - integer(baseline["receipts_accepted"])
            require(attempts >= 0 and accepted >= 0, "feed observation counters regressed")
            if accepted > 0 and attempts > 0 and observation["latest_tick"]["pending"] is False:
                self.plans[-1]["receiptCounterAdvanced"] = True
                self.plans[-1]["nativeReceiptProof"] = False
                return
        raise REFUSAL("ordinary stage has no observed accepted-receipt counter advancement; retain native trace")

    def fresh_postrelease_observation(self, deadline):
        """Read only; never turn a missing retained presentation into command admission."""
        self.monitor = False
        for _ in range(800):
            now = time.perf_counter_ns()
            require(now < deadline, "post-stage receipt observation timed out")
            self.bounds(now)
            if self.pending_nonce is None and now - self.last_request >= SNAPSHOT_PERIOD:
                self.request_snapshot()
            received = self.receive_snapshot()
            if received:
                require(time.perf_counter_ns() < deadline, "post-stage receipt observation timed out")
                return received
            time.sleep(.001)
        raise REFUSAL("post-stage receipt observation iteration bound")

    def check_postrelease_observation(self, retained, snapshot, now):
        require(self.motion_started is None and not self.pending_command and not self.unsafe_owner
                and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons)
                and self.last_commit is not None and self.last_commit >= self.last_write
                and self.pending_batch[-2:] == ["grip l 0", self.sentinel],
                "post-stage observation requires acknowledged ordinary release without held motion/input")
        state = self.read_state()
        self.ownership(state)
        require(all(state[key] == self.state[key] for key in ("head", "handL", "handR", "controls")),
                "post-release simulator input changed")
        self.stable_snapshot(retained, snapshot)
        source = retained["feed"]
        require(source is not None, "post-release observation lacks retained feed identity")
        require(snapshot["input_evidence"]["released"][0] is True
                and integer(snapshot["input_evidence"]["observedNs"]) >= self.last_write
                and all(hand["squeeze"] == 0 and hand["trigger"] == 0 and hand["held"] == 0
                        and hand["stickX"] == 0 and hand["stickY"] == 0 for hand in snapshot["input"]["hands"]),
                "post-release observation contains held or preceding input")
        left = planner.from_pose({"position": state["handL"]["pos"],
                                  "orientation": planner.ypr_quaternion(state["handL"]["ypr"])})
        require(all(p <= .0002 and a <= .001 for p, a in
                    (planner.pose_error(left, planner.from_pose(snapshot["input"]["hands"][0][key]))
                     for key in ("grip", "aim"))), "post-release left-controller pose changed")
        feed = snapshot["feed"]
        if feed is None:
            require(source["blocksFire"] is True and snapshot["weapon_actions_blocked"] is True
                    and snapshot["feedObservation"]["latest_tick"]["claim"] is False,
                    "missing post-release feed is not an unclaimed retained Fire block")
            # The existing common frame hook validates original leases/owners;
            # it supplies no feed, claim, success, or command-capable snapshot.
            planner.admit_frame(snapshot, now, integer(snapshot["nonce"]),
                                mechanism_admission=lambda original, evaluated: original is snapshot and evaluated == now)
        else:
            require(all(feed[key] == source[key] for key in
                        ("owner", "weapon", "registrationKey", "rigFingerprint", "lifetime")),
                    "post-release retained feed identity changed")

    def capture(self, name):
        require(re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,47}", name) is not None, "invalid capture tag")
        require(self.capture_sequence < integer(self.capture_plan["maximum_compositions"]), "capture budget exhausted")
        capture_path = self.run / "sim" / "capture" / (name + ".json")
        require(not capture_path.exists(), "capture tag already exists")
        self.send(["shot " + name], capture=name)
        for _ in range(6000):
            self.pump()
            if capture_path.exists():
                value, _ = read_json(capture_path, 65536)
                require(value["width"] == self.capture_plan["width"] and value["height"] == self.capture_plan["height"],
                        "actual capture dimensions differ from runner budget")
                self.artifacts.save("capture.json", {"path": str(capture_path), "captureSeq": self.capture_sequence,
                                                    "width": value["width"], "height": value["height"]})
                return
            time.sleep(.001)
        raise REFUSAL("capture metadata iteration bound")

    def menu(self, path):
        batches, raw = read_json(child(self.run, path), 16384)
        require(isinstance(batches, list) and 1 <= len(batches) <= 32, "menu batch count exceeds bound")
        self.artifacts.save("menu-request.json", raw)
        requested_trigger, requested_grip, requested_buttons = 0., 0., set()
        for batch in batches:
            lines = batch["commands"]
            require(isinstance(lines, list) and 1 <= len(lines) <= 5, "menu command count exceeds bound")
            hold = integer(batch.get("holdMs", 0))
            require(0 <= hold <= 1500, "menu hold exceeds 1500ms")
            for line in lines:
                require(isinstance(line, str), "menu command must be text")
                if line in ("hand r follow off", "hand r aimtrim 0 0"):
                    continue
                match = re.fullmatch(r"(?:trigger r|grip r) (0(?:\.\d+)?|1(?:\.0+)?)", line)
                if match:
                    if line.startswith("trigger"):
                        requested_trigger = float(match[1])
                    else:
                        requested_grip = float(match[1])
                    continue
                if re.fullmatch(r"btn (?:a|b|x|y|menu) (?:down|up)", line):
                    _, button, action = line.split()
                    if action == "down":
                        requested_buttons.add(button)
                    else:
                        requested_buttons.discard(button)
                    continue
                require(line.startswith("hand r grip pose "), "menu mode permits right pose/input only")
                numbers = line.split()[4:]
                require(len(numbers) == 6, "menu pose needs six numbers")
                values = [float(v) for v in numbers]
                require(all(math.isfinite(v) for v in values) and all(abs(v) <= 10 for v in values[:3])
                        and all(abs(v) <= 360 for v in values[3:]), "menu pose outside bounded range")
        require(requested_trigger == 0 and requested_grip == 0 and not requested_buttons,
                "menu input sequence must finish with ordinary button/trigger/grip release")
        require(sum(len(batch["commands"]) + 1 for batch in batches) + 9 <= MAX_COMMANDS,
                "menu command and cleanup budget exceeded")
        for batch in batches:
            self.send(batch["commands"])
            # A menu hold is not a feed gesture; runtime continues submitting the
            # unchanged controller pose. No fresh-input claims are synthesized.
            self.pause_until(time.perf_counter_ns() + integer(batch.get("holdMs", 0)) * 1_000_000)

    def settle_pending_for_release(self):
        """Observe only our exact pending batch before deciding release ownership."""
        until = min(self.hard_deadline - 150_000_000, time.perf_counter_ns() + 100_000_000)
        for _ in range(100):
            if time.perf_counter_ns() >= until:
                return
            state = self.read_state()
            require(digest(read_bytes(self.command_path, 4096)) == self.command_hash,
                    "pending command replaced; ordinary release ownership lost")
            require(integer(state["cmdSeq"]) in (self.sequence - len(self.pending_batch), self.sequence),
                    "pending command sequence changed")
            if integer(state["cmdSeq"]) == self.sequence and self.ack_path.exists():
                self.ownership(state)
                raw = read_bytes(self.ack_path, 4096).decode("utf-8")
                match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", raw)
                if match and int(match[1]) == self.sequence and match[3] == " | ".join(self.pending_batch):
                    if integer(state["frame"]) > int(match[2]) and integer(state["endFrames"]) > integer(self.pending_before["endFrames"]):
                        self.pending_command = False
                        self.remember_controls(self.pending_batch)
                        self.save_owner(False)
                        self.artifacts.event({"type": "pending_command_settled_before_release", "ack": raw,
                                              "state": sim_summary(state)}, cleanup=True)
                        return
            time.sleep(.001)

    def release_after_failure(self):
        self.monitor = False
        if self.pending_command and not self.unsafe_owner:
            self.settle_pending_for_release()
        if self.unsafe_owner or self.pending_command or not (self.held_left or self.held_trigger or self.held_right or self.held_buttons):
            return "not attempted: no settled owned held controller"
        if time.perf_counter_ns() + 250_000_000 >= self.hard_deadline:
            return "not attempted: bounded driver deadline"
        lines = (["grip l 0"] if self.held_left else []) + (["trigger r 0"] if self.held_trigger else [])
        lines += (["grip r 0"] if self.held_right else []) + ["btn " + key + " up" for key in sorted(self.held_buttons)]
        self.send(lines, cleanup=True)
        return "ordinary owned controller release acknowledged"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="exact live runner output directory")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--stage", choices=[*planner.STAGES, "SupplyPickup"])
    mode.add_argument("--capture", help="one explicit named composition within runner budget")
    mode.add_argument("--menu-batches", type=Path, help="bounded JSON batches inside exact run directory")
    parser.add_argument("--profiles", type=Path, help="production profile export copied inside exact run directory")
    parser.add_argument("--pickup-first", action="store_true", help="continuous fresh chest pickup then held AttachContainer")
    parser.add_argument("--step-ms", type=int, default=0, help="0 chooses min(60ms,maxInputGapNs/2) from production policy")
    args = parser.parse_args(argv)
    driver = None
    result = {"schema": "bc2vr.live-feed-driver.v1", "status": "refused", "nativeAcceptance": False,
              "headsetAcceptance": False, "evidence": "actual simulator commands; native receipts require separate review"}
    try:
        require(not args.stage or args.profiles is not None, "stage requires --profiles inside run")
        require(not args.pickup_first or args.stage == "AttachContainer", "--pickup-first requires AttachContainer")
        require(0 <= args.step_ms <= 100, "invalid command period")
        # Keep a partially initialized instance available for handle cleanup and
        # a refusal artifact if admission fails after creating the output folder.
        driver = Driver.__new__(Driver)
        driver.__init__(args)
        if args.stage:
            pickup = driver.stage("SupplyPickup") if args.pickup_first else None
            # While awaiting the fresh post-pickup source, the unchanged held
            # pose continues producing genuine XR packets each host frame.
            # Keep command timestamps intact; native admission owns input leases.
            driver.stage(args.stage, pickup)
        elif args.capture:
            driver.capture(args.capture)
        else:
            driver.menu(args.menu_batches)
        result["status"] = "commands_acknowledged"
        return_code = 0
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except Exception as release_error:
                result["release"] = "refused: " + str(release_error)
        return_code = 2
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update({"durationNs": time.perf_counter_ns() - driver.started,
                               "commands": driver.commands, "plans": driver.plans,
                               "leftGripHeld": driver.held_left, "pendingCommand": driver.pending_command,
                               "captureSequence": getattr(driver, "capture_sequence", None),
                               "artifactDirectory": str(driver.artifacts.directory)})
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except (REFUSAL, OSError) as error:
                    result["resultWriteError"] = str(error)
                    return_code = 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return return_code


if __name__ == "__main__":
    raise SystemExit(main())
