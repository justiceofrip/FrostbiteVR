"""Root-only bounded ordinary XR reload stimulus; never a native success verdict.

Unlike drive_bc2_feed_simulation, diagnostic reception is not motion authority.
Fresh typed mapping/custody boundaries bracket ordinary left-controller input;
the installed mod independently validates every real input frame and operation.
This runner starts no process, captures no image and changes no native state.
"""
import argparse
import copy
from collections import deque
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
MAX_DIAGNOSTIC_RECORDS = 2048
MAX_ACK_TRACE_POLLS = 32  # First poll plus a bounded tail; no per-poll archival.
MAX_PUBLICATION_TRACE_SPANS = 32  # First span plus a bounded tail across replace attempts.
LEFT_POSE_SETUP = ["hand l follow off", "hand l aimtrim 0 0"]
# Verified ca14f5b1 xr-sim build: one smoothstep grip motion per hand; aim is
# derived from grip plus trim. Unknown binaries retain the ordinary pose path.
NEUTRAL_MOTION_RUNTIME_SHA256 = "2c5eb913be5d4a6b86ed38e308b085985cd86e7454c149e91d5d629c3259d20a"


class ReadDeadline(Refusal):
    """A late read is not evidence that command ownership changed."""


class PublicationTrace:
    """Passive, bounded send timings; reports are materialized after cleanup.

    The transport's validation callback brackets preparation and replacement.
    Those intervals include transport checks, temporary-file cleanup and any
    replace retry wait; they are not timings of individual filesystem calls.
    No trace value authorizes input, a retry, ownership or a renewed deadline.
    """
    def __init__(self, driver, cleanup):
        self.started_ns = time.perf_counter_ns()
        self.cleanup, self.context = cleanup, "admission"
        self.requested_ns = self.published_ns = self.committed_ns = self.finished_ns = None
        self.action_deadline = self.source_deadline = self.ack_deadline = self.publication_deadline = None
        self.hard_deadline = getattr(driver, "hard_deadline", None)
        self.identity = None
        self.outcome = "not_published"
        self.failure = self.terminal_failure = None
        self.first = self.current = None
        self.tail = deque(maxlen=MAX_PUBLICATION_TRACE_SPANS - 1)
        self.spans = self.validations = self.open_attempts = 0
        self.phase_ns, self.winerrors = {}, {}
        self.paths = {key: str(getattr(driver, key)) for key in
                      ("session_path", "state_path", "command_path") if hasattr(driver, key)}
        if hasattr(driver, "run"):
            self.paths["owner_path"] = str(driver.run / "live-driver-owner.json")
        self.phase("checks")

    def phase(self, name):
        now = time.perf_counter_ns()
        if self.current is not None:
            row = self.current
            row["finishedNs"] = now
            row["durationNs"] = now - row["startedNs"]
            self.phase_ns[row["phase"]] = self.phase_ns.get(row["phase"], 0) + row["durationNs"]
        self.spans += 1
        self.current = dict(phase=self.context + "." + name, startedNs=now)
        if self.first is None:
            self.first = self.current
        else:
            self.tail.append(self.current)

    def attempted_open(self, error=0):
        self.open_attempts += 1
        if error:
            key = str(error)
            self.winerrors[key] = self.winerrors.get(key, 0) + 1

    def observed_state(self, state):
        pass  # State payloads already have their existing guarded evidence path.

    def exception(self, error):
        return dict(atNs=time.perf_counter_ns(), kind=type(error).__name__,
                    error=str(error)[:512], phase=self.current["phase"],
                    errno=getattr(error, "errno", None), winError=getattr(error, "winerror", None))

    def failed(self, error):
        if self.failure is None:
            self.failure = self.exception(error)
            self.phase("exception_unwind")

    def finish(self, error=None):
        if error is not None:
            self.terminal_failure = self.exception(error)
            self.failed(error)
        self.phase("finished")
        self.finished_ns = self.current["startedNs"]

    def report(self):
        rows = [self.first] + list(self.tail)
        return dict(type="command_publication_trace", startedNs=self.started_ns,
                    requestedNs=self.requested_ns, publishedNs=self.published_ns,
                    committedNs=self.committed_ns, finishedNs=self.finished_ns,
                    originalActionDeadlineNs=self.action_deadline, sourceDeadlineNs=self.source_deadline,
                    originalHardDeadlineNs=self.hard_deadline,
                    originalAckDeadlineNs=self.ack_deadline, originalPublicationDeadlineNs=self.publication_deadline,
                    command=self.identity, publicationOutcome=self.outcome, cleanup=self.cleanup,
                    failure=self.failure, terminalFailure=self.terminal_failure, paths=self.paths,
                    validationCalls=self.validations, openAttempts=self.open_attempts, winErrors=self.winerrors,
                    phaseNs=self.phase_ns, spanCount=self.spans, retainedSpans=len(rows),
                    droppedSpans=self.spans - len(rows), spans=rows,
                    transportTiming="callback intervals include preparation/replace checks, retry wait and temporary cleanup")


class AckReadTrace:
    """Passive timings only: retain failed ACK polls, never extend their clock.

    The first poll survives tail eviction so publication-to-first-read delay is
    distinguishable from the last stalled read. Counters include evicted polls.
    Nothing here serializes, touches disk, or contributes to an input decision.
    """
    def __init__(self, requested_ns, published_ns, deadline_ns, identity, cleanup=False):
        self.requested_ns, self.published_ns, self.deadline_ns = requested_ns, published_ns, deadline_ns
        self.identity, self.cleanup = identity, cleanup
        self.first, self.current = None, None
        self.tail = deque(maxlen=MAX_ACK_TRACE_POLLS - 1)
        self.polls = self.attempts = 0
        self.winerrors, self.phase_ns = {}, {}
        self.last_state = self.last_ack = self.failure = None

    def begin_poll(self):
        self.polls += 1
        self.current = dict(index=self.polls, startedNs=time.perf_counter_ns(), phaseNs={},
                            openAttempts=0, winErrors={})
        if self.first is None:
            self.first = self.current
        else:
            self.tail.append(self.current)

    def phase(self, name):
        row = self.current
        if row is None:
            return
        now = time.perf_counter_ns()
        if "phase" in row:
            previous, elapsed = row["phase"], now - row["phaseStartedNs"]
            row["phaseNs"][previous] = row["phaseNs"].get(previous, 0) + elapsed
            self.phase_ns[previous] = self.phase_ns.get(previous, 0) + elapsed
        row["phase"], row["phaseStartedNs"] = name, now

    def attempted_open(self, error=0):
        self.attempts += 1
        self.current["openAttempts"] += 1
        if error:
            key = str(error)
            self.winerrors[key] = self.winerrors.get(key, 0) + 1
            errors = self.current["winErrors"]
            errors[key] = errors.get(key, 0) + 1

    def observed_state(self, state):
        # Record raw observations before validation without coercing malformed
        # values or retaining arbitrary state payloads in the diagnostic journal.
        self.last_state = {key: value[:512] if isinstance(value, str) else value
            for key in ("cmdSeq", "lastCmd", "frame", "endFrames")
            if isinstance(value := state.get(key), (str, int, float, bool, type(None)))} if isinstance(state, dict) else None
        if self.current is not None:
            self.current["state"] = self.last_state

    def observed_ack(self, match, raw):
        self.last_ack = dict(atNs=time.perf_counter_ns(), parsed=match is not None,
                             text=raw[:512])
        if match is not None:
            self.last_ack.update(sequence=int(match[1]), frame=int(match[2]))
        if self.current is not None:
            self.current["ack"] = self.last_ack

    def end_poll(self, error=None):
        if self.current is None:
            return
        if error is not None:
            self.current["failedPhase"] = self.current.get("phase")
        self.phase("finished")
        self.current["finishedNs"] = time.perf_counter_ns()
        if error is not None:
            self.current["error"] = str(error)[:512]
        self.current = None

    def failed(self, error):
        self.failure = dict(atNs=time.perf_counter_ns(), kind=type(error).__name__, error=str(error)[:512])

    def report(self):
        rows = ([] if self.first is None else [self.first]) + list(self.tail)
        return dict(type="ack_read_failure", requestedNs=self.requested_ns, publishedNs=self.published_ns,
                    originalDeadlineNs=self.deadline_ns, pendingCommand=self.identity, cleanup=self.cleanup,
                    failure=self.failure, pollCount=self.polls, retainedPolls=len(rows),
                    droppedPolls=self.polls - len(rows), openAttempts=self.attempts,
                    winErrors=self.winerrors, phaseNs=self.phase_ns, lastState=self.last_state,
                    lastAck=self.last_ack, polls=rows)


def deadline_bytes(path, maximum, deadline_ns):
    def check():
        if deadline_ns is not None and time.perf_counter_ns() >= deadline_ns:
            raise ReadDeadline("original acknowledgement/read deadline exceeded")
    check()
    raw = transport.read_bytes(path, maximum)
    check()
    return raw


class DeferredArtifacts:
    """Bounded diagnostics retained in memory until all input decisions end.

    Only the driver thread owns this journal. No serializer, compressor or
    artifact writer runs inside an acquired source, boundary, stage or ACK lease.
    The existing disk writer still enforces its 1 MiB cap, including final reserve.
    """
    def __init__(self, sink):
        self.sink, self.directory = sink, sink.directory
        self.records = []
        self.metadata_bytes = self.raw_bytes = 0
        self.finished = False

    @staticmethod
    def bound(value, depth=0):
        # Conservative JSON byte reservation, without rendering the diagnostic.
        require(depth < 64, "diagnostic nesting budget")
        if isinstance(value, (bytes, str)):
            return len(value) * (1 if isinstance(value, bytes) else 6) + 2
        if value is None or isinstance(value, bool):
            return 5
        if isinstance(value, (int, float)):
            return 64
        if isinstance(value, dict):
            return 2 + sum(DeferredArtifacts.bound(k, depth+1) + DeferredArtifacts.bound(v, depth+1) + 2
                           for k, v in value.items())
        require(isinstance(value, (list, tuple)), "unsupported diagnostic value")
        return 2 + sum(DeferredArtifacts.bound(v, depth+1) + 1 for v in value)

    def append(self, kind, name, value, flag=False):
        require(not self.finished, "diagnostic journal already finalized")
        require(len(self.records) < MAX_DIAGNOSTIC_RECORDS, "bounded diagnostic journal exhausted")
        size = len(value) if kind == "snapshot" else self.bound(value)
        if kind == "snapshot":
            require(self.raw_bytes + size <= 4 * MAX_BYTES, "decoded observation budget")
            self.raw_bytes += size
        else:
            require(self.metadata_bytes + size <= MAX_BYTES, "diagnostic metadata budget")
            self.metadata_bytes += size
        self.records.append((kind, name, value, flag))

    def save(self, name, value, final=False):
        if final:
            require(self.finished, "final result requires completed diagnostic flush")
            return self.sink.save(name, value, final=True)
        self.append("save", name, value)

    def snapshot(self, name, raw):
        self.append("snapshot", name, raw)

    def event(self, value, cleanup=False):
        self.append("event", None, value, cleanup)

    def flush(self):
        require(not self.finished, "diagnostic journal already finalized")
        self.finished = True
        failure = None
        for kind, name, value, flag in self.records:
            try:
                if kind == "event":
                    self.sink.event(value, cleanup=flag)
                else:
                    self.sink.save(name, gzip.compress(value, mtime=0) if kind == "snapshot" else value)
            except BaseException as error:
                if failure is None:
                    failure = f"{kind} {name or 'events.jsonl'}: {error}"
        self.records.clear()
        if failure is not None:
            raise Refusal("diagnostic flush failed: " + failure)


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
    if sample.get("hand") == "r":
        require(sample["grip"] == 0, "spent-case right tilt requires neutral left grip")
        aim = sample.get("aimPositionMeters", []) + sample.get("aimYawPitchRollDegrees", [])
        require(len(aim) == 6 and all(type(v) in (int, float) and math.isfinite(v) for v in aim)
                and all(abs(v) <= 10 for v in aim[:3]) and all(abs(v) <= 360 for v in aim[3:]),
                "invalid bounded right aim pose")
        setup = supplied[:1] == ["hand r follow off"]
        require(not setup or allow_setup, "right pose setup only allowed on first sample")
        lines = (["hand r follow off"] if setup else []) + ["hand r grip pose " + coordinates,
                 "hand r aim pose " + " ".join(f"{v:.9f}" for v in aim), "grip l 0"]
        require(supplied == lines, "unallowlisted spent-case right command")
        return lines
    require(sample.get("hand", "l") == "l", "unsupported ordinary hand")
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


def hand_to_command(sample, duration_ms):
    ordinary_commands(sample, True)
    require(type(duration_ms) is int and 0 <= duration_ms <= 10_000 and sample["grip"] == 0,
            "unbounded/non-neutral interpolation")
    values = sample["positionMeters"] + sample["yawPitchRollDegrees"]
    return "hand l to grip " + " ".join(f"{v:.9f}" for v in values) + " " + str(duration_ms)


class MotionInputLease:
    """Seal driver-owned publications during motion, never runtime state files.

    A read HANDLE with only FILE_SHARE_READ prevents in-place data writes.
    Windows POSIX rename can still replace a pathname: cached metadata-only
    identity checks cover that at the observer's100ms cadence. Payloads are
    verified once, not on every XR frame. Runtime publications remain unlocked.
    """
    def __init__(self, driver, deadline_ns):
        import msvcrt
        self.streams, self.entries = [], []
        self.api = driver.owner.api
        class FileIdInfo(ctypes.Structure):
            _fields_ = [("VolumeSerialNumber", ctypes.c_ulonglong), ("FileId", ctypes.c_ubyte * 16)]
        self.api.GetFileInformationByHandleEx.argtypes = (wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD)
        self.api.GetFileInformationByHandleEx.restype = wintypes.BOOL
        self.info_type = FileIdInfo
        self.next_check = time.perf_counter_ns() + PERIOD_NS
        try:
            for path, expected in ((driver.command_path, driver.command_hash),
                    (driver.run / "live-driver-owner.json", transport.digest(driver.owner_bytes(False)))):
                transport.require_read_time(deadline_ns)
                handle = driver.owner.api.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x00200000, None)
                require(handle not in (None, wintypes.HANDLE(-1).value), "cannot seal motion input ownership")
                try:
                    fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
                except BaseException:
                    driver.owner.api.CloseHandle(handle)
                    raise
                stream = os.fdopen(fd, "rb")
                self.streams.append(stream)
                require(transport.digest(stream.read(8193)) == expected, "motion input ownership changed")
                self.entries.append((path, self.identity(handle)))
                transport.require_read_time(deadline_ns)
        except BaseException:
            self.close()
            raise

    def identity(self, handle):
        info = self.info_type()
        if not self.api.GetFileInformationByHandleEx(handle, 18, ctypes.byref(info), ctypes.sizeof(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        return info.VolumeSerialNumber, bytes(info.FileId)

    def check(self, until):
        if time.perf_counter_ns() < self.next_check:
            return
        for path, expected in self.entries:
            transport.require_read_time(until)
            # Access0 is metadata only. No path canonicalization, library setup,
            # session JSON or command payload read occurs in this wait check.
            handle = self.api.CreateFileW(str(path), 0, 7, None, 3, 0x00200000, None)
            require(handle not in (None, wintypes.HANDLE(-1).value), "motion input pathname disappeared")
            try:
                require(self.identity(handle) == expected, "motion input pathname ownership changed")
            finally:
                self.api.CloseHandle(handle)
            transport.require_read_time(until)
        self.next_check = time.perf_counter_ns() + PERIOD_NS

    def close(self):
        for stream in self.streams:
            stream.close()
        self.streams = []
        self.entries = []


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
    tilt = value.get("family") == "fixed" and value.get("stage") == "EjectSpent"
    require(all((s.get("hand") == "r") is tilt for s in schedule), "right pose is restricted to spent-case tilt")
    require(not tilt or not continuation and schedule[0]["commands"][:1] == ["hand r follow off"],
            "spent-case tilt requires initial right setup and neutral left input")
    require(tilt or (schedule[0]["commands"][:2] == LEFT_POSE_SETUP) is (not continuation),
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


def sim_state(path, deadline_ns, trace=None):
    """Immutable share-delete read; only transient2/32/33 inside original clock."""
    if os.name != "nt":
        if trace is not None:
            trace.phase("portable_state_read")
        return transport.read_json(path, 16384)[0]
    import msvcrt
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    transport.local_path(path)
    while True:
        if time.perf_counter_ns() >= deadline_ns:
            raise ReadDeadline("original simulator state read deadline exceeded")
        if trace is not None:
            trace.phase("state_open")
        handle = api.CreateFileW(str(path), 0x80000000, 7, None, 3, 0x00200000, None)
        if handle == wintypes.HANDLE(-1).value:
            error = ctypes.get_last_error()
            if trace is not None:
                trace.attempted_open(error)
            if error not in (2, 32, 33):
                raise ctypes.WinError(error)
            if trace is not None:
                trace.phase("state_retry_wait")
            time.sleep(.001)
            continue
        if trace is not None:
            trace.attempted_open()
            trace.phase("state_handle")
        try:
            fd = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
        except BaseException:
            api.CloseHandle(handle)
            raise
        with os.fdopen(fd, "rb") as stream:
            if trace is not None:
                trace.phase("state_read")
            require(os.fstat(stream.fileno()).st_size <= 16384, "oversized simulator state")
            raw = stream.read(16385)
            if trace is not None:
                trace.phase("state_close")
        if trace is not None:
            trace.phase("state_decode")
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
        self.terminal = None
        self.thread = threading.Thread(target=self._run, name="bc2-stimulus-observer", daemon=True)
        self.thread.start()

    def emit(self, value):
        try:
            self.inbox.put_nowait(value)
        except queue.Full:
            self.terminal = {"kind": "gap", "reason": "bounded observation inbox exhausted",
                "index": value.get("index"), "nonce": value.get("snapshot", {}).get("nonce", value.get("nonce")),
                "unadmittedRawBytes": len(value.get("raw", b"")), "atNs": time.perf_counter_ns()}
            raise Refusal("bounded observation inbox exhausted")

    def _run(self):
        try:
            previous = 0
            last_request = None
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
                if last_request is not None:
                    require(transport.read_bytes(self.request, 22) == last_request, "competing snapshot request writer")
                if self.stop_event.is_set() or time.perf_counter_ns() >= self.hard_deadline:
                    return
                transport.atomic(self.request, request)
                last_request = request
                deadline = min(self.hard_deadline, started + RECEIVE_NS)
                delivered = False
                while not self.stop_event.is_set() and time.perf_counter_ns() < deadline:
                    require(transport.read_bytes(self.request, 22) == request, "competing snapshot request writer")
                    if self.snapshot.exists():
                        snapshot, raw = transport.read_json(self.snapshot, transport.planner.SNAPSHOT_LIMIT)
                        actual = integer(snapshot["nonce"])
                        if actual >= nonce:
                            # The read can return after its lease or after a
                            # foreign writer replaced our request. Retain the
                            # bytes either way, but never grant late authority.
                            received = time.perf_counter_ns()
                            owned = transport.read_bytes(self.request, 22) == request
                            self.emit({"kind": "snapshot", "index": index, "requestNs": started,
                                       "deadlineNs": deadline, "receivedNs": received,
                                       "ownershipCheckedNs": time.perf_counter_ns(), "requestOwned": owned,
                                       "requestNonce": nonce, "responseOwned": actual == nonce,
                                       "snapshot": snapshot, "raw": raw})
                            require(owned, "competing snapshot request writer after response read")
                            require(actual == nonce, "competing snapshot response")
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
            if self.terminal is None:
                self.terminal = {"kind": "gap", "reason": "native observer ownership/read failure",
                                 "error": self.fatal, "atNs": time.perf_counter_ns()}

    def close(self):
        self.stop_event.set()
        self.thread.join(.08)
        return not self.thread.is_alive()


class ReloadStimulus:
    acquire_lock = transport.Driver.acquire_lock
    @staticmethod
    def check_target(state, target):
        if target.get("hand") != "r":
            return transport.Driver.check_target(state, target)
        require(state["controls"]["gripL"] == 0 and state["handR"]["followsHead"] is False,
                "spent-case right pose/neutral left input did not commit")
        a = transport.planner.from_pose(dict(position=state["handR"]["pos"],
            orientation=transport.planner.ypr_quaternion(state["handR"]["ypr"])))
        b = transport.planner.from_pose(dict(position=target["positionMeters"],
            orientation=transport.planner.ypr_quaternion(target["yawPitchRollDegrees"])))
        distance, angle = transport.planner.pose_error(a, b)
        require(distance <= .0002 and angle <= .001, "spent-case right controller pose did not commit")
    dispose_prepared_command = transport.Driver.dispose_prepared_command
    dispose_publication_owner = transport.Driver.dispose_publication_owner

    def __init__(self, args):
        require(os.name == "nt", "ordinary XR stimulus requires Windows")
        self.args, self.run = args, transport.local_path(args.run)
        self.owner = self.lock = self.artifacts = self.observer = None
        self.supervisor_owner = self.motion_lease = None
        self.outstanding_motion = None
        self.neutral_motion_supported = False
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
        self.pending_boundary = None
        self.observation_incomplete = False
        self.observation_failure = None
        self.diagnostic_failure = None
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
        self.artifacts = DeferredArtifacts(transport.Artifacts(self.run))
        self.artifacts.save("invocation.json", {"schema": "bc2vr.reload-stimulus-invocation.v1", "host": self.host,
            "family": args.family, "stage": args.stage, "rootAuthorizedOrdinaryInput": True,
            "maximumDurationNs": 10 * NS, "maximumCommands": MAX_COMMANDS, "maximumArtifactBytes": MAX_BYTES,
            "maximumObservationRequests": MAX_OBSERVATIONS, "maximumDecodedObservationBytes": 4 * MAX_BYTES,
            "maximumDiagnosticMetadataBytes": MAX_BYTES, "maximumDiagnosticRecords": MAX_DIAGNOSTIC_RECORDS,
            "diagnosticFlush": "after owned cleanup and observer stop; no live decisions afterward",
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
        self.neutral_motion_supported = self.manifest["runtime_library"]["sha256"] == NEUTRAL_MOTION_RUNTIME_SHA256
        if self.neutral_motion_supported:
            self.supervisor_owner = transport.WindowsOwner(self.link["supervisor"])
        self.capture_plan = self.manifest["capture_plan"]
        self.state = self.read_state()
        self.capture_sequence = integer(self.state["captureSeq"])
        self.command_hash = transport.digest(transport.read_bytes(self.command_path, 4096))
        self.sequence, self.sentinel = integer(self.state["cmdSeq"]), self.state["lastCmd"]
        owner_path = self.run / "live-driver-owner.json"
        if owner_path.exists():
            previous, _ = transport.read_json(owner_path, 8192)
            require(previous["host"] == self.host and previous["pending"] is False
                    and not previous.get("outstandingMotion")
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

    def read_state(self, cleanup=False, deadline_ns=None):
        until = self.hard_deadline if cleanup else min(self.deadline, self.stage_deadline)
        if deadline_ns is not None:
            until = min(until, deadline_ns)
        trace = getattr(self, "_ack_trace", None) or getattr(self, "_publication_trace", None)
        if trace is not None:
            trace.phase("owner_check")
        self.owner.check()
        if trace is not None:
            trace.phase("session_read_decode")
        if deadline_ns is None:
            session, _ = transport.read_json(self.session_path)
        else:
            raw = deadline_bytes(self.session_path, 128 * 1024, until)
            session = json.loads(raw.decode("utf-8-sig"), parse_constant=lambda x: (_ for _ in ()).throw(Refusal(x)))
        if trace is not None:
            trace.phase("session_validation")
        require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                and session["source_sha256"] == self.manifest["source_sha256"]
                and session["version"] == self.manifest["version"] and session["state"] == "running",
                "native session/owner/source changed")
        # A stop request forbids new stimulus, but must not prevent replacing
        # our already-running neutral motion while the exact session is still
        # live/focused. All owner/state/ACK checks remain required for cleanup.
        require(not (self.folder / "stop.request").exists()
                or cleanup and bool(getattr(self, "outstanding_motion", None)), "normal stop requested")
        if trace is not None:
            trace.phase("state_setup")
        state = sim_state(self.state_path, until, trace) if trace is not None else sim_state(self.state_path, until)
        if trace is not None:
            trace.observed_state(state)
            trace.phase("state_validation")
        if time.perf_counter_ns() >= until:
            raise ReadDeadline("original simulator state read deadline exceeded")
        require(integer(state["pid"]) == integer(self.host["pid"]) and state["runtime"] == "xr-sim"
                and integer(state["errors"]) == 0 and state["lastCmdError"] == "", "simulator identity/error changed")
        require(state["sessionRunning"] is True and state["actionsAttached"] is True
                and state["sessionState"] == "FOCUSED" and state["paceMode"] == "free"
                and all(state[key]["valid"] is True for key in ("head", "handL", "handR")), "focus/tracking/session changed")
        if hasattr(self, "initial_sim"):
            tilt = self.args.family == "fixed" and self.args.stage == "EjectSpent"
            untouched = getattr(self, "tilt_left_state", self.initial_sim["handL"]) if tilt else getattr(self, "right_rest_state", self.initial_sim["handR"])
            require(state["head"] == self.initial_sim["head"] and state["handL" if tilt else "handR"] == untouched,
                    "head/untouched-hand basis changed")
            if tilt:
                applying = self.pending_command and integer(state["cmdSeq"]) == self.sequence
                require(state["controls"]["gripL"] == 0, "spent-case left input is not neutral")
                # xr-sim parse_command_file publishes the new cmdSeq with the
                # previous frame snapshot before control_apply_pending commits
                # the pose. _wait_ack checks this target only after the exact
                # ACK's frame boundary; receipt alone is not pose commitment.
                if not applying:
                    target = getattr(self, "settled_right_sample", None)
                    if target is None:
                        require(state["handR"] == self.initial_sim["handR"], "unrequested right-hand pose change")
                    else:
                        self.check_target(state, target)
            require(neutral_controls(state["controls"], True), "unrequested controller input")
            require(integer(state["captureSeq"]) == self.capture_sequence, "unrequested capture")
        return state

    def owner_bytes(self, pending):
        # Older shared drivers understand `pending`, not outstandingMotion.
        # A parsed interpolation remains unsettled input ownership until its
        # endpoint or explicit duration-zero replacement has been established.
        return transport.json_bytes({"host": self.host, "pending": pending or bool(getattr(self, "outstanding_motion", None)), "sequence": self.sequence,
            "sentinel": self.sentinel, "commandSha256": self.command_hash, "artifactDirectory": str(self.artifacts.directory),
            "outstandingMotion": getattr(self, "outstanding_motion", None)})

    def save_owner(self, pending):
        require(os.name == "nt" and self.lock not in (None, wintypes.HANDLE(-1).value),
                "owner publication requires exclusive live-driver.lock")
        transport.Driver.save_owner(self, pending, settled_writer=lambda path, raw: publish_owner(self, raw))

    def validate_pending_owner(self, expected_bytes, deadline_ns):
        record = getattr(self, "_publication_owner", None)
        require(record is not None, "pending owner HANDLE custody unavailable")
        try:
            return record.validate(expected_bytes, deadline_ns)
        except transport.ReadDeadlineExceeded as error:
            raise ReadDeadline(str(error)) from error

    def ownership(self, state, deadline_ns=None, expected=None):
        sequence, sentinel, command_hash = expected or (self.sequence, self.sentinel, self.command_hash)
        trace = getattr(self, "_publication_trace", None)
        if trace is not None:
            trace.phase("previous_command_read_validation")
        try:
            require(transport.digest(deadline_bytes(self.command_path, 4096, deadline_ns)) == command_hash,
                    "command file replaced by another writer")
            require(integer(state["cmdSeq"]) == sequence and state["lastCmd"] == sentinel,
                    "command sequence/sentinel belongs to another writer")
        except ReadDeadline:
            raise
        except BaseException:
            self.unsafe_owner = True
            raise

    def observations(self, deadline_ns=None):
        require(getattr(self, "observation_failure", None) is None, "previous native observation failure")
        try:
            self._observations(deadline_ns=deadline_ns)
        except BaseException as error:
            self.observation_failure = str(error)
            self.observation_incomplete = True
            raise

    def sight_completion_mapping(self, snapshot):
        """Observe the exact selected family without requiring a new grasp.

        A native selection can retain its family while publishing no contact
        geometry. This path can only end an already contacted, held gesture;
        it never admits acquisition or another pose on the selected weapon.
        """
        require(self.args.family == "sight" and self.args.stage == "SightFlip"
                and self.stimulus is not None and self.stimulus["family"] == "sight"
                and (self.held_left or self.target_observed)
                and self.boundaries.get("sight_contact", {}).get("selectionState") == "source",
                "sight completion requires the original contacted held gesture")
        sight = snapshot["sight"]
        require(snapshot.get("valid") is True and sight.get("status") == "geometry_unavailable"
                and sight.get("contact") is None and sight.get("preferredByDistance") is None,
                "sight completion requires explicit absent contact geometry")
        require(integer(sight["generation"]) == integer(snapshot["input"]["generation"])
                and integer(sight["deadlineQpc"]) == integer(snapshot["native_deadline_qpc"])
                and integer(sight["deadlineNs"]) == integer(snapshot["native_deadline_ns"]),
                "sight completion publication/input lease mismatch")
        # Common body mapping plus the ORIGINAL observed family, without
        # changing status or supplying any replacement contact/rig evidence.
        mapped = self.geometry._mapping(snapshot, "feed")
        mapped["family"] = sight["family"]
        transition = self.geometry._sight_state(self.stimulus["mapping"], mapped, self.args.target)
        require(transition == "target", "absent sight geometry is not an exact target transition")
        return transition

    def sight_completion_boundary(self, snapshot, boundary_id, now):
        require(boundary_id == "sight_selected", "absent sight geometry is completion-only")
        self.sight_completion_mapping(snapshot)
        nonce = integer(snapshot["nonce"])
        require(now < self.geometry.source_deadline(snapshot, "sight"), "stimulus boundary source expired")
        self.geometry.feed.admit_frame(snapshot, now, nonce)
        require(all(float(h["trigger"]) == 0 for h in snapshot["input"]["hands"]), "sight trigger is active")
        require(nonce != integer(self.stimulus["source"]["nonce"])
                and integer(snapshot["observed_ns"]) > integer(self.stimulus["source"]["evaluatedAtNs"]),
                "boundary needs a new original snapshot")
        require(self.last_commit is not None and 0 < self.last_write
                and max(self.last_write, self.last_commit) <= integer(snapshot["input_evidence"]["observedNs"]),
                "sight completion predates its settled ordinary command")
        return dict(boundary=boundary_id, observed=True, snapshotNonce=snapshot["nonce"],
                    observedNs=snapshot["observed_ns"], sourceValidUntilNs=self.geometry.source_deadline(snapshot, "sight"),
                    selectionState="target", completionOnly=True, sightGeometryUnavailable=True,
                    nativeReceiptProof=False, nativeAcceptance=False, headsetAcceptance=False)

    def _observations(self, *, deadline_ns=None, diagnostic_only=False):
        if self.observer is None:
            return
        batch = []
        for _ in range(8):
            try:
                item = self.observer.inbox.get_nowait()
            except queue.Empty:
                break
            if item["kind"] == "gap":
                self.observation_incomplete = True
                self.artifacts.event({"type": "observation_gap", **item})
                continue
            raw, snapshot = item["raw"], item["snapshot"]
            self.decoded_snapshot_bytes += len(raw)
            require(self.decoded_snapshot_bytes <= 4 * MAX_BYTES, "decoded observation budget")
            self.artifacts.snapshot(f"observation-{item['index']:03}.json.gz", raw)
            item = {k: v for k, v in item.items() if k != "raw"}
            batch.append(item)
        # Preserve the complete bounded batch before identity/clock decisions.
        # A refusal in one item cannot discard later queued failure evidence.
        events = []
        for item in batch:
            snapshot = item["snapshot"]
            event = {"type": "native_observation", "index": item["index"], "nonce": snapshot.get("nonce"),
                "requestNs": item["requestNs"], "deadlineNs": item["deadlineNs"], "receivedNs": item["receivedNs"],
                "ownershipCheckedNs": item.get("ownershipCheckedNs", item["receivedNs"]),
                "requestOwned": item.get("requestOwned", True), "requestNonce": item.get("requestNonce"),
                "responseOwned": item.get("responseOwned", True), "drainedNs": time.perf_counter_ns(),
                "diagnosticOnly": diagnostic_only, "currentAtReceive": None, "valid": snapshot.get("valid"),
                "invalidReason": snapshot.get("invalid_reason"), "nativeAcceptance": False}
            self.artifacts.event(event)
            events.append(event)
        terminal = getattr(self.observer, "terminal", None)
        if terminal is not None:
            self.artifacts.event({"type": "observation_gap", **terminal})
            self.observer.terminal = None
        if diagnostic_only:
            return
        require(self.observer.fatal is None, "native observer ownership/read failure: " + str(self.observer.fatal))
        for item, event in zip(batch, events):
            snapshot = item["snapshot"]
            require(item.get("requestOwned", True), "competing snapshot request writer after response read")
            require(item.get("responseOwned", True), "competing snapshot response")
            if deadline_ns is not None and time.perf_counter_ns() >= deadline_ns:
                raise ReadDeadline("original observation validation deadline exceeded")
            item["current"] = (snapshot.get("valid") is True and item["receivedNs"] < item["deadlineNs"]
                               and item.get("requestOwned", True)
                               and item.get("ownershipCheckedNs", item["receivedNs"]) < item["deadlineNs"]
                               and item["receivedNs"] < self.geometry.source_deadline(snapshot, self.args.family))
            event["currentAtReceive"] = item["current"]
            if not item["current"]:
                self.observation_incomplete = True
            # Expiry never renews this record. A valid later mapping still makes
            # observed owner/reference drift actionable even if delivery is late.
            if self.mapping is not None and snapshot.get("valid") is True:
                require(integer(snapshot["observed_ns"]) >= integer(self.mapping["observed_ns"]), "observation time regressed")
                if self.args.family == "sight" and snapshot.get("sight", {}).get("status") == "geometry_unavailable":
                    transition = self.sight_completion_mapping(snapshot)
                    self.completion_only_observed = True
                    event["sightCompletionOnly"] = True
                else:
                    transition = self.geometry.same_mapping(self.mapping, snapshot, self.args.family, self.args.stage, target=self.args.target)
                if transition == "incomplete":
                    item["current"] = event["currentAtReceive"] = False
                    event["mappingReferenceMissing"] = True
                    self.observation_incomplete = True
                if transition == "target":
                    self.target_observed = True
            self.observed.append(item)
            self.observed = self.observed[-8:]
            if deadline_ns is not None and time.perf_counter_ns() >= deadline_ns:
                raise ReadDeadline("original observation validation deadline exceeded")

    def guard(self, deadline_ns=None):
        self.bounds()
        until = min(self.deadline, self.stage_deadline, deadline_ns if deadline_ns is not None else self.deadline)
        started, phase = time.perf_counter_ns(), "session/simulator state"
        try:
            state = self.read_state(deadline_ns=until)
            phase = "command ownership"
            if not self.pending_command:
                self.ownership(state, until)
            phase = "observation validation"
            self.observations(until)
        except BaseException as error:
            try:
                self.artifacts.event({"type": "guard_failure", "phase": phase, "startedNs": started,
                    "finishedNs": time.perf_counter_ns(), "deadlineNs": until, "error": str(error)})
            except BaseException as diagnostic_error:
                self.diagnostic_failure = str(diagnostic_error)
            raise
        return state

    def pause_until(self, until):
        while time.perf_counter_ns() < until:
            self.guard()
            time.sleep(.001)
        self.guard()

    def motion_guard(self, until):
        """No synchronous session/state/command payload reads during the sweep."""
        require(time.perf_counter_ns() < min(until, self.deadline, self.stage_deadline),
                "original neutral motion deadline")
        require(self.motion_lease is not None and self.motion_lease.streams
                and self.lock not in (None, wintypes.HANDLE(-1).value)
                and not self.pending_command and not self.unsafe_owner,
                "neutral motion lost exclusive input custody")
        self.owner.check()
        self.supervisor_owner.check()
        require(not (self.folder / "stop.request").exists(), "normal stop requested")
        try:
            self.motion_lease.check(min(until, self.deadline, self.stage_deadline))
        except transport.ReadDeadlineExceeded:
            raise
        except BaseException:
            self.unsafe_owner = True
            raise
        self.observations(min(until, self.deadline, self.stage_deadline))
        self.bounds()

    def execute_neutral_motion(self, motion):
        rows = self.stimulus["schedule"]
        end = motion["lastSample"]
        self.send(rows[0], motion=motion)
        # ACK proves parsing and a subsequent frame, not arrival. Waiting from
        # its committed timestamp is conservative about the runtime timer start.
        finish = self.last_commit + motion["durationNs"]
        until = min(self.deadline, self.stage_deadline, finish + RECEIVE_NS)
        require(finish < until, "neutral interpolation exceeds original stage budget")
        self.motion_lease = MotionInputLease(self, until)
        try:
            while time.perf_counter_ns() < finish:
                self.motion_guard(until)
                time.sleep(min(.01, max(0, finish-time.perf_counter_ns())/NS))
            self.motion_guard(until)
            first = self.guard(deadline_ns=until)
            # Require a new committed frame after the elapsed-duration check.
            # A stalled runtime or a stale endpoint file cannot finish motion.
            while True:
                self.motion_guard(until)
                time.sleep(.01)
                state = self.guard(deadline_ns=until)
                if integer(state["frame"]) > integer(first["frame"]) and integer(state["endFrames"]) > integer(first["endFrames"]):
                    break
            self.check_target(state, rows[end])
            self.state, self.last_commit = state, time.perf_counter_ns()
            boundary = next((g for g in self.stimulus["boundaries"] if g["afterSample"] == end), None)
            if boundary is None:
                boundary = dict(id="neutral_motion_contact", timeoutNs=RECEIVE_NS)
            snapshot, _ = self.fresh(boundary=boundary, after_ns=self.last_commit, deadline_ns=until)
            self.artifacts.event(dict(type="neutral_motion_endpoint", motion=motion,
                frame=state["frame"], endFrames=state["endFrames"], endpointNs=self.last_commit,
                contactNonce=snapshot["nonce"], originalDeadlineNs=until, nativeAcceptance=False))
            # Acquisition must still publish inside this contact's original
            # source lifetime; completion does not renew a source or stage.
            self.initial_mapping_deadline = self.geometry.source_deadline(snapshot, self.args.family)
            self.first_sample_pending = True
        finally:
            self.motion_lease.close()
            self.motion_lease = None
        self.outstanding_motion = None
        self.save_owner(False)
        return rows[end]

    def fresh(self, *, boundary=None, after_ns=0, pickup=None, prepare=False, deadline_ns=None, fixed_reload=False):
        require(not prepare or boundary is None, "stimulus preparation requires initial stage admission")
        timeout = integer(boundary["timeoutNs"]) if boundary else RECEIVE_NS
        started = time.perf_counter_ns()
        until = min(self.deadline, self.stage_deadline, started + timeout)
        if deadline_ns is not None:
            until = min(until, deadline_ns)
        # Retain what was still awaited if a final guard consumes this clock.
        # This is diagnostic context, never an additional admission condition.
        self.pending_boundary = {"family": self.args.family, "stage": self.args.stage,
            "id": boundary["id"] if boundary else "stage_admission", "startedNs": started,
            "deadlineNs": until, "lastPending": None}
        used = set()
        capture_reference = None
        while time.perf_counter_ns() < until:
            try:
                self.guard(deadline_ns=until)
            except ReadDeadline:
                self.observation_incomplete = True
                raise
            if time.perf_counter_ns() >= until:
                self.observation_incomplete = True
                raise Refusal("original fresh boundary deadline consumed by guard")
            for item in reversed(self.observed):
                snapshot = item["snapshot"]
                key = integer(snapshot["nonce"])
                if key in used or snapshot.get("valid") is not True or integer(snapshot["observed_ns"]) < after_ns:
                    continue
                used.add(key)
                now = time.perf_counter_ns()
                if not item["current"] or now >= self.geometry.source_deadline(snapshot, self.args.family):
                    continue
                if fixed_reload and self.last_commit is not None:
                    raw = snapshot.get("fixedContainer", {}).get("raw", {}).get("input", {})
                    if (integer(snapshot["input_evidence"]["observedNs"]) < self.last_commit
                            or integer(raw.get("observedNs", 0)) < self.last_commit):
                        continue
                if boundary is not None:
                    require(self.last_commit is not None, "fresh boundary requires settled command")
                    # Publishing the command file precedes host application. A
                    # snapshot after that write can still contain held input
                    # from before the acknowledged release. Keep the original
                    # budget and require input observed after the settled ACK.
                    if integer(snapshot["input_evidence"]["observedNs"]) < max(self.last_write, self.last_commit):
                        continue
                    if self.args.family == "fixed" and self.args.stage == "EjectSpent":
                        raw = snapshot.get("fixedContainer", {}).get("raw", {}).get("input", {})
                        if integer(raw.get("observedNs", 0)) < max(self.last_write, self.last_commit):
                            continue
                        prior_raw = self.boundaries.get("fixed_tilt_current", {}).get("gate", {})
                        if (integer(raw.get("sequence", 0)) <= integer(prior_raw.get("rawInputSequence", 0))
                                or integer(raw.get("observedNs", 0)) <= integer(prior_raw.get("inputObservedNs", 0))):
                            continue
                if boundary is None:
                    try:
                        if capture_reference is not None:
                            transport.stable_snapshot(capture_reference, snapshot)
                            self.geometry.fixed.require_preserved(
                                self.geometry.fixed.preserved(capture_reference),
                                self.geometry.fixed.preserved(snapshot))
                        x = snapshot.get("fixedContainer")
                        if (self.args.family == "fixed" and isinstance(x, dict)
                                and x.get("available") is False
                                and x.get("reason") == "capture_pending_or_not_requested"):
                            # A request arms subsequent Gameplay diagnostic copies.
                            # An absent first copy is never a plan or an ammo view;
                            # retain only identity inside this existing admission clock.
                            require(self.commands == 0 and self.mapping is None and pickup is None
                                    and not self.held_left and not self.pending_command and not self.unsafe_owner,
                                    "fixed capture pending outside initial unheld observation")
                            frame = self.geometry.fixed.frame
                            frame.admit_frame(snapshot, now, key)
                            require(x.get("schema") == self.geometry.fixed.OBSERVATION_SCHEMA
                                    and x.get("view") is None and snapshot.get("feed") is None
                                    and snapshot["weapon_actions_blocked"] is False
                                    and snapshot["input_evidence"]["released"] == [True, False]
                                    and snapshot["input"]["hands"][0]["squeeze"] == 0
                                    and all(frame.finite(h["trigger"]) == frame.finite(h["stickX"])
                                            == frame.finite(h["stickY"]) == 0 and integer(h["held"]) == 0
                                            for h in snapshot["input"]["hands"]),
                                    "fixed capture absence contradicts neutral initial state")
                            if capture_reference is None:
                                capture_reference = snapshot
                            raise self.geometry.BoundaryPending("initial fixed-container capture pending")
                        if prepare:
                            # Building includes typed stage admission. Keep its
                            # plan local to this fresh snapshot and original lease.
                            stage = self.args.stage
                            if fixed_reload:
                                require(self.args.family == "fixed", "fixed reload dispatch requires fixed family")
                                step = integer(snapshot["fixedContainer"]["tracking"]["step"])
                                require(step in (0, 1, 2, 3), "fixed Reload requires an observed unfinished loader")
                                stage = "Insert" if pickup is not None else {0: "Open", 1: "EjectSpent", 2: "Pickup", 3: "Close"}[step]
                            plan = self.geometry.build_stimulus(self.args.family, self.profiles, snapshot, now, stage,
                                step_ms=self.args.step_ms, pickup=pickup, target=self.args.target)
                        else:
                            self.geometry.admit_boundary(self.args.family, self.profiles, snapshot, now, self.args.stage,
                                                         pickup=pickup, target=self.args.target)
                    except self.geometry.BoundaryPending as pending:
                        self.observation_incomplete = True
                        self.pending_boundary["lastPending"] = {"nonce": snapshot["nonce"], "reason": str(pending)}
                        self.artifacts.event({"type": "boundary_pending", "id": "stage_admission", "nonce": snapshot["nonce"],
                                              "reason": str(pending), "deadlineNs": until})
                        continue
                    require(time.perf_counter_ns() < min(until, self.geometry.source_deadline(snapshot, self.args.family)),
                            "initial boundary validation consumed original deadline")
                    self.pending_boundary = None
                    return (snapshot, now, plan) if prepare else (snapshot, now)
                try:
                    if boundary["id"] == "neutral_motion_contact":
                        summary = self.geometry.check_neutral_contact(self.stimulus, snapshot, now, profiles=self.profiles)
                    elif self.args.family == "sight" and snapshot.get("sight", {}).get("status") == "geometry_unavailable":
                        summary = self.sight_completion_boundary(snapshot, boundary["id"], now)
                    else:
                        summary = self.geometry.check_boundary(self.stimulus, snapshot, boundary["id"], now,
                            command_written_ns=self.last_write, prior=self.boundaries, profiles=self.profiles)
                except self.geometry.BoundaryPending as pending:
                    self.pending_boundary["lastPending"] = {"nonce": snapshot["nonce"], "reason": str(pending)}
                    self.artifacts.event({"type": "boundary_pending", "id": boundary["id"], "nonce": snapshot["nonce"],
                                          "reason": str(pending), "deadlineNs": until})
                    continue
                require(time.perf_counter_ns() < min(until, self.geometry.source_deadline(snapshot, self.args.family)),
                        "boundary validation consumed original deadline")
                if summary.get("completionOnly") is True:
                    self.completion_only_observed = True
                if boundary["id"] != "neutral_motion_contact":
                    self.boundaries[boundary["id"]] = summary
                self.artifacts.event({"type": "fresh_stimulus_boundary", "id": boundary["id"], "summary": summary})
                self.pending_boundary = None
                return snapshot, now
            time.sleep(.001)
        self.observation_incomplete = True
        raise Refusal("fresh typed stimulus boundary absent within original budget")

    def send(self, sample=None, *, cleanup=False, release_only=False, motion=None, cancel_motion=False):
        trace = self._publication_trace = PublicationTrace(self, cleanup)
        try:
            self._send(sample, cleanup=cleanup, release_only=release_only, motion=motion, cancel_motion=cancel_motion)
        except BaseException as error:
            trace.finish(error)
            if not hasattr(self, "_publication_failures"):
                self._publication_failures = []
            # Original input and its ordinary cleanup release; never retain an
            # unbounded history or write evidence while command input is live.
            if len(self._publication_failures) < 2:
                self._publication_failures.append(trace)
            raise
        else:
            trace.finish()
            self._last_publication_trace = trace
        finally:
            self.dispose_publication_owner()
            self._publication_trace = None

    def _send(self, sample=None, *, cleanup=False, release_only=False, motion=None, cancel_motion=False):
        publication = self._publication_trace
        require(cleanup or getattr(self, "observation_failure", None) is None, "previous native observation failure")
        require(cleanup or release_only or not getattr(self, "completion_only_observed", False),
                "completion-only observation cannot authorize another controller pose")
        require(not release_only or self.held_left, "ordinary release requires held left input")
        require(not getattr(self, "outstanding_motion", None) or cancel_motion,
                "outstanding neutral motion requires endpoint settlement or explicit cancellation")
        require(not cancel_motion or cleanup and self.outstanding_motion and not self.held_left,
                "cancellation requires owned neutral motion")
        require(motion is None or not cleanup and not release_only and not self.held_left
                and self.first_sample_pending and getattr(self, "neutral_motion_supported", False),
                "interpolation requires fresh unheld supported source")
        lines = ["grip l 0"] if cleanup or release_only else ordinary_commands(sample, self.first_sample_pending)
        if sample is not None and sample.get("hand") == "r":
            require(self.args.family == "fixed" and self.args.stage == "EjectSpent" and not cleanup and not release_only
                    and not self.held_left and motion is None, "right input requires the admitted spent-case stage")
            self.pending_right_sample = sample
        if motion is not None:
            # Exported aim derives from grip + the existing zero trim. There is
            # one runtime slot per hand, so never queue a second to-aim command.
            lines = [line for line in lines if not line.startswith("hand l aim pose ")]
            lines.append(hand_to_command(self.stimulus["schedule"][motion["lastSample"]], motion["durationMs"]))
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget")
        publication.phase("write_gap")
        if cleanup:
            require(time.perf_counter_ns() + ACK_NS < self.hard_deadline, "cleanup reserve exhausted")
            while time.perf_counter_ns() < self.last_write + WRITE_GAP_NS:
                time.sleep(.001)
        else:
            self.pause_until(max(self.last_write + WRITE_GAP_NS, time.perf_counter_ns()))
        require(cleanup or release_only or not getattr(self, "completion_only_observed", False),
                "completion-only observation cannot authorize another controller pose")
        action_deadline = self.hard_deadline if cleanup else min(self.deadline, self.stage_deadline)
        publication.action_deadline = action_deadline
        publication.source_deadline = self.initial_mapping_deadline if not cleanup and self.first_sample_pending else None
        publication.context = "initial"
        publication.phase("state_read")
        before = self.read_state(cleanup, action_deadline)
        self.ownership(before, action_deadline)
        if motion is not None:
            # The admitted native source and current simulator agree before
            # staging that same starting pose. follow-off is part of setup.
            source_state = {**before, "handL": {**before["handL"], "followsHead": False}}
            self.check_target(source_state, sample)
        if cancel_motion:
            values = before["handL"]["pos"] + before["handL"]["ypr"]
            coordinates = " ".join(f"{v:.9f}" for v in values)
            sample = dict(positionMeters=values[:3], yawPitchRollDegrees=values[3:], grip=0,
                          commands=["hand l grip pose " + coordinates, "grip l 0"])
            lines = [hand_to_command(sample, 0), "grip l 0"]
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget")
        publication.phase("command_identity")
        sentinel = "note root-stimulus-" + uuid.uuid4().hex
        batch = lines + [sentinel]
        require(len(" | ".join(batch).encode("ascii")) < 508, "acknowledgement text overflow")
        raw, target = ("\n".join(batch) + "\n").encode("ascii"), self.sequence + len(batch)
        written = time.perf_counter_ns()
        publication.requested_ns = written
        publication.phase("request_bounds")
        if not cleanup:
            self.bounds()
            if self.first_sample_pending:
                require(written < self.initial_mapping_deadline, "initial mapping expired before first publication")
        publication.phase("request_journal")
        self.artifacts.event({"type": "command_request", "atNs": written, "beforeSequence": self.sequence,
            "targetSequence": target, "commands": batch, "ordinaryInputOnly": True}, cleanup=cleanup)
        previous_owner = self.sequence, self.sentinel, self.command_hash
        previous_motion = getattr(self, "outstanding_motion", None)
        if motion is not None:
            self.outstanding_motion = dict(**motion, requestedNs=written, sequence=target, status="publication_pending")
        self.sequence, self.sentinel, self.command_hash = target, sentinel, transport.digest(raw)
        self.pending_command = True
        self.pending_batch, self.pending_before = batch, before
        publication.phase("pending_owner_encode")
        pending_owner = self.owner_bytes(True)
        until = min(action_deadline, written + ACK_NS)
        publication_deadline = min(until, self.initial_mapping_deadline) if not cleanup and self.first_sample_pending else until
        publication.ack_deadline, publication.publication_deadline = until, publication_deadline
        publication.identity = dict(sequence=target, sentinel=sentinel, commandSha256=self.command_hash)
        publication.phase("pending_owner_save")
        try:
            self.save_owner(True)
        except BaseException:
            # The command publisher has not been called. Retain the exact
            # previously owned command so failure cleanup can verify it afresh;
            # preserve the owner-file failure, never retry the failed action.
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            self.outstanding_motion = previous_motion
            raise
        def validate_publication_owner():
            # Re-entry means the transport's previous replace was refused.
            publication.outcome = "not_published"
            publication.validations += 1
            publication.context = "validation"
            publication.phase("bounds")
            try:
                if not cleanup:
                    self.bounds()
            except BaseException as error:
                publication.failed(error)
                raise
            try:
                publication.phase("pending_owner_read")
                require(self.validate_pending_owner(pending_owner, publication_deadline) == pending_owner,
                        "command owner record replaced before publication")
                self.ownership(self.read_state(cleanup, publication_deadline), publication_deadline, expected=previous_owner)
            except ReadDeadline as error:
                publication.failed(error)
                raise
            except BaseException as error:
                publication.failed(error)
                self.unsafe_owner = True
                raise
            publication.context = "transport"
            publication.phase("replace_or_retry")
            publication.outcome = "publication_uncertain"
        try:
            # Required owner I/O and transient sharing consume the same
            # original source/ACK budget as the eventual command publication.
            publication.phase("publication_bounds")
            require(time.perf_counter_ns() < action_deadline, "original prepublication deadline exceeded")
            if not cleanup and self.first_sample_pending:
                require(time.perf_counter_ns() < self.initial_mapping_deadline,
                        "initial mapping expired during owner publication")
            publication.context = "transport"
            publication.phase("command_preparation")
            transport.publish_command(self.command_path, raw, publication_deadline, validate_publication_owner)
        except BaseException as error:
            publication.failed(error)
            publication.context = "restoration"
            publication.phase("previous_identity")
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            if not self.unsafe_owner:
                try:
                    publication.phase("pending_owner_read")
                    require(self.validate_pending_owner(pending_owner, self.hard_deadline) == pending_owner,
                            "unpublished owner record replaced")
                    self.ownership(self.read_state(True, self.hard_deadline), self.hard_deadline)
                    publication.outcome = "not_published"
                    self.outstanding_motion = previous_motion
                    publication.phase("previous_owner_save")
                    self.save_owner(False)
                except BaseException as owner_error:
                    self.unsafe_owner = True
                    raise Refusal(f"{error}; unpublished owner restoration refused: {owner_error}") from error
            raise
        published = time.perf_counter_ns()
        if motion is not None:
            self.outstanding_motion.update(publishedNs=published, status="published_unsettled")
        publication.published_ns, publication.outcome = published, "published_unsettled"
        publication.context = "settlement"
        publication.phase("ack_wait")
        if not cleanup:
            self.first_sample_pending = False
        self.last_write = published
        self.commands += len(batch)
        self.dispose_publication_owner()
        trace = AckReadTrace(written, published, until, dict(sequence=target, sentinel=sentinel,
                            commandSha256=self.command_hash, commands=batch), cleanup)
        self._ack_trace = trace
        try:
            state, committed = self._wait_ack(batch, before, None if motion is not None else sample, cleanup, until)
        except BaseException as error:
            trace.failed(error)
            if not hasattr(self, "_ack_failures"):
                self._ack_failures = []
            # At most the failed input and its failed ordinary cleanup release.
            if len(self._ack_failures) < 2:
                self._ack_failures.append(trace)
            self._ack_trace = None
            if not cleanup:
                # Preserve queued evidence after the ACK decision, without
                # settling/replaying a late command or masking its refusal.
                try:
                    self.observations()
                except BaseException as observation_error:
                    raise Refusal(f"{error}; post-refusal observation failure: {observation_error}") from error
            raise
        finally:
            self._ack_trace = None
        self.last_commit = committed
        publication.committed_ns, publication.outcome = committed, "settled"
        publication.phase("owner_save")
        self.pending_command = False
        self.state = state
        self.held_left = False if cleanup or release_only else sample["grip"] == 1
        if sample is not None and sample.get("hand") == "r":
            self.settled_right_sample = sample
        if cancel_motion:
            self.outstanding_motion = None
        elif motion is not None:
            self.outstanding_motion.update(committedNs=committed, status="interpolating")
        if lines[:2] == LEFT_POSE_SETUP:
            self.pose_setup_established = True
        self.save_owner(False)
        publication.phase("post_commit")
        self.artifacts.event({"type": "command_commit", "requestedNs": written, "writtenNs": published, "committedNs": self.last_commit,
            "sequence": self.sequence, "frame": state["frame"], "endFrames": state["endFrames"],
            "handL": state["handL"], "gripL": state["controls"]["gripL"], "cleanup": cleanup}, cleanup=cleanup)
        if not cleanup:
            # Settled input/owner state survives an observer refusal so
            # only normal owned grip release remains available.
            self.observations()
            self.bounds()

    def _wait_ack(self, batch, before, sample, cleanup, until):
        # No observation drain/archive anywhere in this polling loop: it
        # must not consume the original budget before a later ready poll.
        while time.perf_counter_ns() < until:
            trace = getattr(self, "_ack_trace", None)
            if trace is not None:
                trace.begin_poll()
            try:
                state = self.read_state(cleanup, until)
                if self._acknowledged(state, batch, before, until):
                    if trace is not None:
                        trace.phase("target_validation")
                    if sample is not None:
                        self.check_target(state, sample)
                    else:
                        require(state["controls"]["gripL"] == 0, "cleanup grip did not commit")
                        if getattr(self, "outstanding_motion", None):
                            require(state["handL"]["followsHead"] is False, "neutral motion unexpectedly follows head")
                    committed = time.perf_counter_ns()
                    require(committed < until, "original command acknowledgement/commit deadline exceeded")
                    return state, committed
            except BaseException as error:
                if trace is not None:
                    trace.end_poll(error)
                raise
            finally:
                if trace is not None:
                    trace.end_poll()
            time.sleep(.001)
        raise Refusal("original command acknowledgement/commit deadline exceeded")

    def _acknowledged(self, state, batch, before, deadline_ns=None):
        trace = getattr(self, "_ack_trace", None)
        try:
            if trace is not None:
                trace.phase("command_read_validation")
            require(transport.digest(deadline_bytes(self.command_path, 4096, deadline_ns)) == self.command_hash, "competing command writer")
            require(integer(state["cmdSeq"]) in (self.sequence - len(batch), self.sequence), "unexpected command sequence")
            if integer(state["cmdSeq"]) != self.sequence:
                return False
            require(state["lastCmd"] == self.sentinel, "command sentinel changed")
            if trace is not None:
                trace.phase("ack_exists")
            if not self.ack_path.exists():
                return False
            if trace is not None:
                trace.phase("ack_read_decode")
            ack = deadline_bytes(self.ack_path, 4096, deadline_ns).decode("utf-8")
            if trace is not None:
                trace.phase("ack_validation")
            match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", ack)
            if trace is not None:
                trace.observed_ack(match, ack)
            if not match or int(match[1]) != self.sequence:
                return False
            require(match[3] == " | ".join(batch), "acknowledgement batch differs")
            return integer(state["frame"]) > int(match[2]) and integer(state["endFrames"]) > integer(before["endFrames"])
        except ReadDeadline:
            raise
        except BaseException:
            self.unsafe_owner = True
            raise

    def execute(self, stimulus):
        continuation = self.held_left
        require(not continuation or self.pose_setup_established, "held continuation lacks established left pose configuration")
        self.stimulus = validate_stimulus(stimulus, continuation)
        if stimulus["stage"] == "EjectSpent":
            self.tilt_left_state = copy.deepcopy(self.state["handL"])
        require(self.commands + sum(len(ordinary_commands(s, i == 0)) + 1 for i, s in enumerate(stimulus["schedule"])) + 2 <= MAX_COMMANDS,
                "complete next stimulus and cleanup exceed remaining command budget")
        self.boundaries = {}
        self.completion_only_observed = False
        self.plans.append({"family": stimulus["family"], "stage": stimulus["stage"], "source": stimulus["source"],
                           "samples": len(stimulus["schedule"]), "nativeAcceptance": False})
        artifact = stimulus
        if getattr(self, "fixed_reload", False):
            # Five stages retain the same journal ceiling. Store their exact
            # shared schedule once, with an explicit reference for offline
            # reconstruction, instead of archiving identical command rows or
            # exact owner/profile selections twice.
            original_rows = stimulus["geometryEvidence"]["schedule"]
            require([{k: v for k, v in row.items() if k != "commands"} for row in original_rows]
                    == [{k: v for k, v in row.items() if k != "commands"} for row in stimulus["schedule"]],
                    "fixed Reload geometry/stimulus schedules diverged")
            artifact = copy.deepcopy(stimulus)
            artifact["geometryEvidence"].pop("schedule")
            artifact["geometryEvidence"]["scheduleJsonPointer"] = "/schedule"
            artifact["geometryEvidence"]["scheduleCommandOverrides"] = {
                str(i): row["commands"] for i, row in enumerate(original_rows)
                if row["commands"] != stimulus["schedule"][i]["commands"]}
            require(artifact["geometryEvidence"]["selection"] == artifact["mapping"]["mechanismSelection"],
                    "fixed Reload geometry/stimulus selections diverged")
            artifact["geometryEvidence"].pop("selection")
            artifact["geometryEvidence"]["selectionJsonPointer"] = "/mapping/mechanismSelection"
        self.artifacts.save(f"stimulus-{len(self.plans):02}.json", artifact)
        start = time.perf_counter_ns()
        require(start < integer(stimulus["source"]["validUntilNs"]), "initial mapping expired before ordinary input")
        self.initial_mapping_deadline = integer(stimulus["source"]["validUntilNs"])
        self.first_sample_pending = True
        budget = stimulus["budget"]
        self.stage_deadline = min(self.deadline, start + integer(budget["totalNs"]))
        gesture_start = integer(budget["gestureStartSample"])
        motion = self.geometry.neutral_motion(stimulus) if getattr(self, "neutral_motion_supported", False) else None
        if motion is not None:
            self.plans[-1]["neutralMotion"] = motion
        anchor_at, anchor_commit, previous, previous_at = None, None, None, 0
        for index, sample in enumerate(stimulus["schedule"]):
            if motion is not None and 0 < index <= motion["lastSample"]:
                continue
            if index == gesture_start:
                self.stage_deadline = min(self.stage_deadline, time.perf_counter_ns() + integer(budget["maximumGestureNs"]))
            if motion is not None and index == 0:
                previous = self.execute_neutral_motion(motion)
                previous_at = anchor_at = integer(previous["atNs"])
                anchor_commit = self.last_commit
                continue
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
                if stimulus["stage"] == "EjectSpent":
                    stimulus["tiltExpectedSample"] = sample
                    # Each pose retains the last freshly admitted raw packet's
                    # original lease through owner checks and publication.
                    self.first_sample_pending = True
                self.send(sample)
                anchor_at, anchor_commit = at, self.last_commit
            if stimulus["stage"] == "EjectSpent":
                boundary = next(g for g in stimulus["boundaries"] if g["id"] == "fixed_tilt_current")
                tilt_snapshot, _ = self.fresh(boundary=boundary, after_ns=self.last_write)
                self.initial_mapping_deadline = self.geometry.source_deadline(tilt_snapshot, "fixed")
                current = self.boundaries["fixed_tilt_current"]
                if current["gate"]["caseEjected"]:
                    # Same-case Insert/epoch/event was observed, so stop poses.
                    self.boundaries["fixed_case_ejected"] = dict(current, boundary="fixed_case_ejected")
                    break
            for boundary in stimulus["boundaries"]:
                if boundary["afterSample"] == index:
                    if stimulus["stage"] == "EjectSpent" and boundary["id"] == "fixed_tilt_current":
                        continue
                    require(bool(boundary["holdGrip"]) == self.held_left, "boundary grip differs")
                    span = integer(boundary.get("minimumObservationSpanNs", 0))
                    self.pause_until(self.last_commit + span)
                    self.fresh(boundary=boundary, after_ns=self.last_write)
            previous, previous_at = sample, at
        require(self.held_left is bool(stimulus["finishGrip"]), "unexpected final grip")
        # Complete every fresh boundary before releasing a held mechanism grip.
        # Reserve pickups keep their custody for the paired insertion instead.
        if stimulus["finishGrip"] == 1 and stimulus["stage"] not in ("SupplyPickup", "ShellPickup", "Pickup"):
            self.send(release_only=True)
        self.stage_deadline = self.deadline
        if stimulus["stage"] == "EjectSpent":
            self.right_rest_state = copy.deepcopy(self.state["handR"])
        self.plans[-1].update(stimulusCompleted=True, boundaryChecksPassed=len(self.boundaries) == len(stimulus["boundaries"]))
        return stimulus

    def run_stimulus(self):
        if self.args.family == "fixed" and self.args.stage == "Reload":
            return self.run_fixed_reload()
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

    def run_fixed_reload(self):
        """One shared owner/observer/invocation clock for every retained stage."""
        self.fixed_reload = True
        pickup, previous = None, None
        for _ in range(5):
            snapshot, _, plan = self.fresh(prepare=True, pickup=pickup, fixed_reload=True,
                                           after_ns=self.last_commit or 0)
            stage = plan["stage"]
            allowed = {None: ("Open", "EjectSpent", "Pickup"), "Open": ("EjectSpent", "Pickup"),
                       "EjectSpent": ("Pickup",), "Pickup": ("Insert",), "Insert": ("Close",)}
            require(stage in allowed.get(previous, ()), "fixed Reload observed an unexpected stage transition")
            self.args.stage, self.mapping = stage, snapshot
            self.execute(plan)
            if stage == "Close" or stage == "Insert" and integer(snapshot["fixedContainer"]["profile"]["kind"]) == 4:
                require(not self.held_left, "completed fixed Reload retained left custody")
                return
            pickup = plan if stage == "Pickup" else None
            previous = stage
        raise Refusal("fixed Reload exceeded bounded stage count")

    def cleanup(self):
        if getattr(self, "motion_lease", None) is not None:
            self.motion_lease.close()
            self.motion_lease = None
        if self.pending_command and not self.unsafe_owner:
            until = min(self.hard_deadline - ACK_NS - WRITE_GAP_NS, time.perf_counter_ns() + 100_000_000)
            while time.perf_counter_ns() < until:
                state = self.read_state(True, until)
                if self._acknowledged(state, self.pending_batch, self.pending_before, until):
                    if self.args.family == "fixed" and self.args.stage == "EjectSpent":
                        self.check_target(state, self.pending_right_sample)
                    require(time.perf_counter_ns() < until, "original pending cleanup deadline exceeded")
                    self.pending_command = False
                    if self.args.family == "fixed" and self.args.stage == "EjectSpent":
                        self.settled_right_sample = self.pending_right_sample
                    self.held_left = "grip l 1" in self.pending_batch
                    self.state = state
                    self.save_owner(False)
                    break
                time.sleep(.001)
        if getattr(self, "outstanding_motion", None):
            require(not self.pending_command and not self.unsafe_owner,
                    "neutral motion cancellation unsettled: retained command ownership uncertainty")
            self.send(cleanup=True, cancel_motion=True)
            return "ordinary owned neutral motion duration-zero cancellation acknowledged"
        if self.pending_command or self.unsafe_owner or not self.held_left:
            return "not attempted: no settled owned held left grip"
        self.send(cleanup=True)
        return "ordinary owned left-grip release acknowledged"

    def close(self):
        if getattr(self, "motion_lease", None) is not None:
            self.motion_lease.close()
            self.motion_lease = None
        self.dispose_publication_owner()
        for record in tuple(getattr(self, "_prepared_cleanup_pending", ())):
            self.dispose_prepared_command(record)
        joined = self.observer is None or self.observer.close()
        try:
            if getattr(self, "artifacts", None) is not None:
                # No further action is permitted. Preserve queued evidence even
                # after a prior refusal; otherwise retain final identity failures.
                prior_failure = self.observation_failure
                if not joined:
                    self.observation_incomplete = True
                try:
                    if self.observation_failure is None:
                        self.observations()
                    else:
                        self._observations(diagnostic_only=True)
                    if not joined:
                        self.artifacts.event({"type": "observation_gap", "reason": "observer shutdown incomplete",
                                              "atNs": time.perf_counter_ns()})
                except Exception as error:
                    if prior_failure is not None or self.observation_failure is None:
                        self.diagnostic_failure = str(error)
                if self.observer is not None and self.observer.fatal is not None:
                    self.observation_incomplete = True
                    if self.observation_failure is None:
                        self.observation_failure = "native observer ownership/read failure: " + self.observer.fatal
        finally:
            if joined and self.owner is not None:
                if self.lock not in (None, wintypes.HANDLE(-1).value):
                    self.owner.api.CloseHandle(self.lock)
                    self.lock = None
                self.owner.close()
                if getattr(self, "supervisor_owner", None) is not None:
                    self.supervisor_owner.close()
        # A still-returning observer retains the OS lock until process exit;
        # a subsequent driver cannot overlap its final read/request publication.
        return joined

    def defer_ack_failures(self, observer_stopped):
        # Called by main only after owned cleanup and observer shutdown. Keep
        # serialization/budget accounting out of every pending-command poll.
        for trace in getattr(self, "_ack_failures", ()):
            value = trace.report()
            value["observerStopped"] = observer_stopped
            self.artifacts.event(value, cleanup=trace.cleanup)
        self._ack_failures = []

    def defer_publication_traces(self, observer_stopped):
        # As with ACK diagnostics, main calls this after cleanup and shutdown.
        traces = list(getattr(self, "_publication_failures", ()))
        last = getattr(self, "_last_publication_trace", None)
        if last is not None:
            traces.append(last)
        for trace in traces:
            value = trace.report()
            value["observerStopped"] = observer_stopped
            self.artifacts.event(value, cleanup=trace.cleanup)
        self._publication_failures, self._last_publication_trace = [], None
        errors = getattr(self, "_prepared_cleanup_errors", None)
        if errors:
            self.artifacts.save("prepared-command-cleanup-failure.json", dict(
                schema="bc2vr.prepared-command-cleanup-failure.v1", errors=list(errors),
                retainedTemporaryCount=len(getattr(self, "_prepared_cleanup_pending", ()))))


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
                    outstandingMotion=getattr(driver, "outstanding_motion", None),
                    pendingBoundary=driver.pending_boundary,
                    artifactDirectory=str(driver.artifacts.directory))
            try:
                result["observerStopped"] = driver.close()
                if not result["observerStopped"]:
                    code = 2
                    result["status"] = "observer_shutdown_incomplete"
            except Exception as error:
                result["closeError"] = str(error)
                code = 2
            if getattr(driver, "artifacts", None):
                result.update(observationIncomplete=driver.observation_incomplete,
                              observationFailure=driver.observation_failure,
                              diagnosticFailure=driver.diagnostic_failure)
                if driver.observation_failure is not None:
                    result.setdefault("error", driver.observation_failure)
                    code = 2
                if driver.diagnostic_failure is not None:
                    result.setdefault("error", driver.diagnostic_failure)
                    code = 2
                flush_started = time.perf_counter_ns()
                try:
                    driver.defer_ack_failures(result.get("observerStopped", False))
                    driver.defer_publication_traces(result.get("observerStopped", False))
                    driver.artifacts.flush()
                except Exception as error:
                    result["archiveError"] = str(error)
                    code = 2
                result["archiveDurationNs"] = time.perf_counter_ns() - flush_started
                if code != 0 and result["status"] == "ordinary_stimulus_completed":
                    result["status"] = "refused"
                try:
                    driver.artifacts.save("result.json", result, final=True)
                except Exception as error:
                    result["resultWriteError"] = str(error)
                    code = 2
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
