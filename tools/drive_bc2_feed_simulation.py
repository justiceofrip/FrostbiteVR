"""ROOT-RUN-ONLY bounded actual-game xr-sim command driver (Windows, stdlib).

The runner must already own a live session and have published startup readiness.
Example: python -B tools/drive_bc2_feed_simulation.py --run G:/.../run01
  --profiles G:/.../run01/profiles.json --stage OpenCover
Use --stage AttachContainer --pickup-first for a continuous chest pickup/attach.
--post-reload-shot requires a completed result from drive_bc2_reload_stimulus.py
(feed CloseCover or supported fixed reload), not this driver's --stage result.
--capture NAME is explicit and consumes the runner's pre-admitted capture budget.
--menu-batches FILE accepts [{"commands":[...],"holdMs":0}] for bounded right-hand
menu input only. No process launch, desktop input, native writes or clock rewrites.
Plans and simulator acknowledgements never establish native reload acceptance.
"""

import argparse
from collections import deque
from contextlib import contextmanager
from contextvars import ContextVar
from functools import wraps
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
MAX_RECEIVE_TIMING_SPANS = 24
_snapshot_json_timing = ContextVar('snapshot_json_timing', default=None)
_read_deadline_ns = ContextVar('read_deadline_ns', default=None)
_owner_read_timing = ContextVar('owner_read_timing', default=None)
_receive_read_timing = ContextVar('receive_read_timing', default=None)
REFUSAL = planner.Refusal
require = planner.require
integer = planner.integer


def digest(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, separators=(",", ":"), allow_nan=False) + "\n").encode("utf-8")


def admit_capture_plan(manifest):
    """Recompute the runner's raw reserve; larger counts require its campaign opt-in."""
    limits, plan = manifest.get("limits"), manifest.get("capture_plan")
    require(isinstance(limits, dict) and isinstance(plan, dict), "missing pre-admitted capture budget")
    campaign, extended = limits.get("campaign_observation", False), limits.get("extended_observation", False)
    require(type(campaign) is bool and type(extended) is bool and not (campaign and extended),
            "invalid observation opt-in")
    mode = "campaign" if campaign else "extended" if extended else "standard"
    # Older standard/extended manifests have no mode or cleanup field. Campaign
    # requires both explicit fields; a count or mode label alone grants nothing.
    require(limits.get("observation_mode", "extended" if extended else "standard") == mode,
            "observation mode/opt-in mismatch")
    reserve, tape_cap = (120, 90000) if campaign else (60, 30000)
    host_max, outer_max = (1200, 1320) if campaign else (420, 540) if extended else (300, 420)
    host, outer = integer(limits.get("host_seconds")), integer(limits.get("outer_seconds"))
    require(15 <= host <= host_max and 90 <= outer <= outer_max and outer >= host + reserve
            and integer(limits.get("cleanup_reserve_seconds", 60)) == reserve
            and integer(limits.get("tape_max_frames")) == tape_cap, "invalid observation limits")
    count, width, height = (integer(plan.get(key)) for key in ("maximum_compositions", "width", "height"))
    require(plan.get("periodic") is False and plan.get("layers") is False
            and 1 <= count <= (30 if campaign else 12)
            and 64 <= width <= 1024 and 64 <= height <= 768,
            "invalid pre-admitted capture budget")
    require(integer(plan.get("eyes")) == 2 and integer(plan.get("image_streams")) == 2
            and integer(plan.get("bytes_per_pixel")) == 4
            and integer(plan.get("metadata_bytes_per_composition")) == 65536,
            "capture stream/overhead budget differs")
    sim_bytes = count * width * height * 4 * 2 * 2 + count * 65536
    raw_bytes = 512 * 1024 * 1024 + sim_bytes
    require(integer(plan.get("raw_bytes")) == sim_bytes
            and integer(limits.get("raw_budget_bytes")) == raw_bytes
            and integer(limits.get("raw_budget_limit_bytes")) == 660 * 1024 * 1024
            and raw_bytes <= 660 * 1024 * 1024
            and integer(limits.get("planned_bytes")) == raw_bytes + 128 * 1024 * 1024
            and integer(limits.get("native_maximum_raw_images")) == 8
            and integer(limits.get("sim_compositions")) == count
            and integer(limits.get("sim_capture_width")) == width
            and integer(limits.get("sim_capture_height")) == height,
            "capture raw/storage budget differs or exceeds ceiling")
    return plan


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


def open_snapshot(path, *, share_write=True):
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
    handle = api.CreateFileW(str(path), 0x80000000, 0x1 | 0x4 | (0x2 if share_write else 0),
                             None, 3, 0x00200000, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, os.O_RDONLY | os.O_BINARY)
    except BaseException:
        api.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, "rb")


class ReadDeadlineExceeded(REFUSAL):
    """Read budget exhausted without itself establishing an owner mismatch."""


def require_read_time(deadline_ns):
    if time.perf_counter_ns() >= deadline_ns:
        raise ReadDeadlineExceeded("original acknowledgement/read deadline exceeded")


def read_until(deadline_ns, function, *args):
    """Apply an existing absolute budget to nested reads, including overrides."""
    outer = _read_deadline_ns.get()
    until = min(deadline_ns, outer) if outer is not None else deadline_ns
    require_read_time(until)
    token = _read_deadline_ns.set(until)
    try:
        result = function(*args)
        require_read_time(until)
        return result
    finally:
        _read_deadline_ns.reset(token)


def read_bytes(path, maximum):
    # Only the final publication owner read enables these bounded in-memory
    # phases. Retain every path/file check and the original read behavior.
    trace = _owner_read_timing.get()
    if trace is not None:
        trace.phase("publish_owner_path_check")
    local_path(path)
    # xr-sim closes state.json.tmp before replacement. Share DELETE, never
    # in-place writes; retain WinError so ACL denial (5) fails immediately.
    # As in the runner's bounded state policy, only transient 2/32/33 retry.
    simulator_state = path.name == "state.json"
    until = time.perf_counter_ns() + (250_000_000 if simulator_state else 40_000_000)
    deadline = _read_deadline_ns.get()
    if deadline is not None:
        until = min(until, deadline)
    for attempt in range(250):
        require_read_time(until)
        try:
            if trace is not None:
                trace.phase("publish_owner_open")
            stream = (open_snapshot(path, share_write=False) if simulator_state else
                      open_snapshot(path) if path.name == "mechanism-snapshot.json" else path.open("rb"))
            with stream:
                try:
                    if trace is not None:
                        trace.phase("publish_owner_size_check")
                    require(os.fstat(stream.fileno()).st_size <= maximum, "oversized file: " + str(path))
                    if trace is not None:
                        trace.phase("publish_owner_read_bytes")
                    data = stream.read(maximum + 1)
                except BaseException as error:
                    if trace is not None:
                        trace.error(error)
                    raise
                finally:
                    if trace is not None:
                        trace.phase("publish_owner_close")
            break
        except OSError as error:
            if trace is not None:
                trace.error(error)
            transient = (getattr(error, "winerror", None) in (2, 32, 33) if simulator_state and os.name == "nt"
                         else isinstance(error, (PermissionError, FileNotFoundError)))
            remaining = until - time.perf_counter_ns()
            if not transient or remaining <= 0 or attempt == 249:
                raise
            if trace is not None:
                trace.phase("publish_owner_retry_wait")
            receive_trace = _receive_read_timing.get()
            if receive_trace is not None:
                receive_trace.read_retry()
            time.sleep(min(.001, remaining / NS))
    if trace is not None:
        trace.phase("publish_owner_read")
    if deadline is not None:
        require_read_time(deadline)
    require(len(data) <= maximum, "file grew beyond bound: " + str(path))
    return data


def read_json(path, maximum=128 * 1024):
    data = read_bytes(path, maximum)
    trace = _snapshot_json_timing.get()
    if trace is not None:
        trace.phase("snapshot_parse")
    def invalid(value):
        raise REFUSAL("invalid JSON constant: " + value)
    return json.loads(data.decode("utf-8-sig"), parse_constant=invalid), data


def replace_command(temporary, path, deadline_ns, validate, replace=None):
    for _ in range(1200):
        require(time.perf_counter_ns() < deadline_ns, "original command publication deadline exceeded")
        validate()
        require(time.perf_counter_ns() < deadline_ns, "original command publication deadline exceeded")
        try:
            (os.replace if replace is None else replace)(temporary, path)
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
            replace_command(temporary, path, deadline_ns, validate)
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



def open_prepared_command(path, *, readable=False):
    """Own a new temporary exclusively until the final publication attempt."""
    require(os.name == "nt", "disk command preparation requires Windows handle custody")
    import msvcrt
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    api.CreateFileW.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = (wintypes.HANDLE,)
    # CREATE_NEW plus share=0: neither replace nor mutate the owned file while
    # waiting for fresh native evidence. No command path is published here.
    access = 0x40000000 | 0x00010000 | (0x80000000 if readable else 0)
    handle = api.CreateFileW(str(path), access, 0, None, 1, 0x80, None)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        descriptor = msvcrt.open_osfhandle(handle, (os.O_RDWR if readable else os.O_WRONLY) | os.O_BINARY)
    except BaseException:
        api.CloseHandle(handle)
        raise
    return os.fdopen(descriptor, "w+b" if readable else "wb")


def prepared_file_api(stream):
    import msvcrt
    api = ctypes.WinDLL("kernel32", use_last_error=True)
    api.SetFileInformationByHandle.argtypes = (wintypes.HANDLE, ctypes.c_int,
                                               ctypes.c_void_p, wintypes.DWORD)
    api.SetFileInformationByHandle.restype = wintypes.BOOL
    return api, msvcrt.get_osfhandle(stream.fileno())


def prepare_command_rename(stream, path):
    """Build FILE_RENAME_INFO before the fresh source; retain the exact HANDLE.

    The SDK union is BOOLEAN ReplaceIfExists / DWORD Flags, followed by the
    naturally aligned HANDLE, DWORD byte length and variable UTF-16 name.
    FileRenameInfo (3), not FileRenameInfoEx, uses ReplaceIfExists.
    """
    class RenameFlags(ctypes.Union):
        _fields_ = [("ReplaceIfExists", wintypes.BYTE), ("Flags", wintypes.DWORD)]
    class RenameInfo(ctypes.Structure):
        _fields_ = [("Flags", RenameFlags), ("RootDirectory", wintypes.HANDLE),
                    ("FileNameLength", wintypes.DWORD), ("FileName", wintypes.WCHAR * 1)]
    require(ctypes.sizeof(wintypes.WCHAR) == 2
            and RenameInfo.FileName.offset == (20 if ctypes.sizeof(wintypes.HANDLE) == 8 else 12),
            "unsupported Windows FILE_RENAME_INFO layout")
    encoded = str(path).encode("utf-16-le")
    buffer = ctypes.create_string_buffer(max(ctypes.sizeof(RenameInfo),
                                            RenameInfo.FileName.offset + len(encoded) + 2))
    info = RenameInfo.from_buffer(buffer)
    info.Flags.ReplaceIfExists, info.RootDirectory = 1, None
    info.FileNameLength = len(encoded)
    ctypes.memmove(ctypes.addressof(buffer) + RenameInfo.FileName.offset, encoded, len(encoded))
    api, handle = prepared_file_api(stream)
    def rename(source, destination):
        require(source is stream and destination == path, "prepared rename destination changed")
        if not api.SetFileInformationByHandle(handle, 3, buffer, len(buffer)):
            raise ctypes.WinError(ctypes.get_last_error())
    return rename


def delete_prepared_command(stream):
    # Delete the owned FILE_OBJECT while its exclusive handle is still held.
    # Never close and then unlink a pathname that could have been replaced.
    api, handle = prepared_file_api(stream)
    disposition = wintypes.BYTE(1)  # FILE_DISPOSITION_INFO.DeleteFile BOOLEAN
    if not api.SetFileInformationByHandle(handle, 4, ctypes.byref(disposition), ctypes.sizeof(disposition)):
        raise ctypes.WinError(ctypes.get_last_error())


class SealedOwnerStream:
    """Retain file custody without exposing payload reads or further writes."""
    def __init__(self, stream):
        self._stream = stream

    def fileno(self):
        return self._stream.fileno()

    def close(self):
        self._stream.close()


def owner_file_identity(api, handle):
    class FileIdInfo(ctypes.Structure):
        _fields_ = [("VolumeSerialNumber", ctypes.c_ulonglong), ("FileId", ctypes.c_ubyte * 16)]
    api.GetFileInformationByHandleEx.argtypes = (wintypes.HANDLE, ctypes.c_int,
                                                 ctypes.c_void_p, wintypes.DWORD)
    api.GetFileInformationByHandleEx.restype = wintypes.BOOL
    info = FileIdInfo()
    if not api.GetFileInformationByHandleEx(handle, 18, ctypes.byref(info), ctypes.sizeof(info)):
        raise ctypes.WinError(ctypes.get_last_error())
    return info.VolumeSerialNumber, bytes(info.FileId)


class PendingOwner:
    """Keep the exact published owner record immutable until command handoff."""
    def __init__(self, driver, path, raw):
        self.path = local_path(path)
        require(isinstance(raw, bytes) and len(raw) <= 8192, "oversized command owner record")
        self.temporary = self.path.with_name(self.path.name + "." + uuid.uuid4().hex + ".tmp")
        self.stream, self.owned = None, False
        self._raw = self._identity = self._path_handle = None
        try:
            self.stream = open_prepared_command(self.temporary, readable=True)
            self.owned = True
            require(self.stream.write(raw) == len(raw), "incomplete command owner write")
            self.stream.flush()
            self.stream.seek(0)
            require(self.stream.read(8193) == raw, "command owner write verification failed")
            self._raw = raw
            self.stream = SealedOwnerStream(self.stream)
            self._api, handle = prepared_file_api(self.stream)
            self._identity = owner_file_identity(self._api, handle)
            rename = prepare_command_rename(self.stream, self.path)
            local_path(self.path)
            rename(self.stream, self.path)
            self.owned = False
        except BaseException:
            driver.dispose_prepared_command(self)
            raise

    def validate(self, expected_bytes, deadline_ns):
        """Join sealed bytes, the live HANDLE, and the current pathname identity.

        Metadata-only pathname opens coexist with exclusive data custody. A
        retained name alone is insufficient proof against file replacement.
        Every check consumes the caller's original budget; no payload is reread.
        """
        def checked(function, *args):
            require_read_time(deadline_ns)
            value = function(*args)
            require_read_time(deadline_ns)
            return value
        require_read_time(deadline_ns)
        require(self.stream is not None and self._raw is not None and self._identity is not None
                and self._path_handle is None, "pending owner HANDLE custody unavailable")
        require(expected_bytes == self._raw, "command owner record replaced before publication")
        checked(local_path, self.path)
        api, handle = prepared_file_api(self.stream)
        require(checked(owner_file_identity, api, handle) == self._identity,
                "pending owner HANDLE identity changed")
        api.GetFinalPathNameByHandleW.argtypes = (wintypes.HANDLE, wintypes.LPWSTR,
                                                wintypes.DWORD, wintypes.DWORD)
        api.GetFinalPathNameByHandleW.restype = wintypes.DWORD
        name = ctypes.create_unicode_buffer(32768)
        length = checked(api.GetFinalPathNameByHandleW, handle, name, len(name), 0)
        if not length:
            raise ctypes.WinError(ctypes.get_last_error())
        require(length < len(name) and name.value.startswith("\\\\?\\")
                and same_path(name.value[4:], self.path), "pending owner HANDLE path changed")
        api.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                   ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
        api.CreateFileW.restype = wintypes.HANDLE
        api.CloseHandle.argtypes = (wintypes.HANDLE,)
        api.CloseHandle.restype = wintypes.BOOL
        require_read_time(deadline_ns)
        # Access=0 requests metadata only; share READ|WRITE|DELETE accommodates
        # the retained data HANDLE. OPEN_REPARSE_POINT never follows a final link.
        path_handle = api.CreateFileW(str(self.path), 0, 7, None, 3, 0x00200000, None)
        if path_handle in (None, wintypes.HANDLE(-1).value):
            require_read_time(deadline_ns)
            raise ctypes.WinError(ctypes.get_last_error())
        self._path_handle = path_handle
        try:
            require_read_time(deadline_ns)
            require(checked(owner_file_identity, api, path_handle) == self._identity,
                    "pending owner pathname identity changed")
        finally:
            # Close even after expiry. Retain a failed close for bounded driver
            # disposal; never overwrite it with another validation HANDLE.
            self.close_path_handle()
        require_read_time(deadline_ns)
        return self._raw

    def close_path_handle(self):
        if self._path_handle is not None:
            self._api.CloseHandle.argtypes = (wintypes.HANDLE,)
            self._api.CloseHandle.restype = wintypes.BOOL
            if not self._api.CloseHandle(self._path_handle):
                raise ctypes.WinError(ctypes.get_last_error())
            self._path_handle = None

    def close(self):
        self.close_path_handle()
        if self.owned:
            delete_prepared_command(self.stream)
            self.owned = False
        if self.stream is not None:
            self.stream.close()
            self.stream = None
        self._raw = self._identity = None


class PreparedCommand:
    """Immutable command bytes, not an input source or publication permission."""
    def __init__(self, driver, lines):
        self.path = local_path(driver.command_path)
        self.lines = tuple(lines)
        self.previous = driver.sequence, driver.sentinel, driver.command_hash
        self.command_count = driver.commands
        self.sentinel = "note root-feed-" + uuid.uuid4().hex
        self.batch = list(self.lines) + [self.sentinel]
        require(len(" | ".join(self.batch).encode("ascii")) < 508,
                "batch exceeds complete xr-sim acknowledgement bound")
        self.raw = ("\n".join(self.batch) + "\n").encode("ascii")
        self.target = driver.sequence + len(self.batch)
        self.hash = digest(self.raw)
        self.temporary = self.path.with_name(self.path.name + "." + uuid.uuid4().hex + ".tmp")
        self.stream, self.owned, self.consumed = None, False, False
        try:
            self.stream = open_prepared_command(self.temporary)
            self.owned = True
            self.stream.write(self.raw)
            self.stream.flush()
            self.rename = prepare_command_rename(self.stream, self.path)
        except BaseException:
            # Retain the original creation/write refusal if disposal also fails.
            driver.dispose_prepared_command(self)
            raise

    def matches(self, driver, lines):
        require(not self.consumed and self.lines == tuple(lines)
                and self.path == driver.command_path and self.command_count == driver.commands
                and self.previous == (driver.sequence, driver.sentinel, driver.command_hash),
                "prepared command does not match exact owned next batch")

    def publish(self, path, raw, deadline_ns, validate):
        require(not self.consumed and self.owned and path == self.path and raw == self.raw,
                "prepared command bytes/path were changed or consumed")
        self.consumed = True
        # The exact CREATE_NEW FILE_OBJECT stays exclusive through validation,
        # the unchanged deadline checks and the handle-based atomic rename.
        local_path(path)
        local_path(self.temporary)
        replace_command(self.stream, path, deadline_ns, validate, self.rename)
        self.owned = False  # committed: subsequent close failure must not roll back

    def close(self):
        if self.owned:
            delete_prepared_command(self.stream)
            self.owned = False
        if self.stream is not None:
            self.stream.close()
            self.stream = None



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


def feed_artifact_limit(args, manifest):
    if args.stage == "AttachContainer" and args.pickup_first:
        # Actual102 exhausted 1 MiB after 34 full responses in the continuous
        # pickup/attach route. Its two stages share 2 MiB from the runner's
        # existing 128 MiB diagnostic reserve, checked by admit_capture_plan.
        # The runner's aggregate watchdog and every input/time bound remain.
        admit_capture_plan(manifest)
        return 2 * MAX_BYTES
    return MAX_BYTES


class Artifacts:
    # Existing specialized stores retain the default without copying a budget.
    maximum_bytes = MAX_BYTES

    def __init__(self, run, maximum_bytes=MAX_BYTES):
        require(type(maximum_bytes) is int and maximum_bytes in (MAX_BYTES, 2 * MAX_BYTES),
                "unsupported driver artifact allowance")
        self.maximum_bytes = maximum_bytes
        self.directory = run / ("live-driver-" + uuid.uuid4().hex)
        self.directory.mkdir()
        self.sizes = {}

    def save(self, name, data, final=False):
        if not isinstance(data, bytes):
            data = json_bytes(data)
        # Leave room for the small session-local request/command/owner files as
        # well as the refusal result. Capture pixels use the runner's own budget.
        limit = self.maximum_bytes - (4096 if final else 12288)
        require(sum(self.sizes.values()) - self.sizes.get(name, 0) + len(data) <= limit,
                f"driver artifact budget would exceed {self.maximum_bytes // MAX_BYTES} MiB")
        atomic(self.directory / name, data)
        self.sizes[name] = len(data)

    def event(self, value, cleanup=False):
        data = json_bytes(value)
        name = "events.jsonl"
        require(sum(self.sizes.values()) + len(data) <= self.maximum_bytes - (8192 if cleanup else 12288),
                "driver artifact budget exhausted")
        with (self.directory / name).open("ab") as stream:
            stream.write(data)
        self.sizes[name] = self.sizes.get(name, 0) + len(data)


def sim_summary(state):
    return {key: state[key] for key in ("pid", "runtime", "cmdSeq", "lastCmd", "lastCmdError",
            "errors", "frame", "endFrames", "captureSeq", "sessionState", "head", "handL", "handR", "controls")}


def stable_snapshot_identity(before, after):
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


def stable_snapshot(before, after):
    stable_snapshot_identity(before, after)
    # The accepted inverse uses W * inverse(B). Common actor/world movement
    # cancels; separately freezing absolute W and B would reject that invariant.
    relative = [planner.multiply(s["mapping"]["weaponWorldMeters"],
                                 planner.inverse(s["mapping"]["trackingBodyWorldMeters"]))
                for s in (before, after)]
    matrices = [(relative[0], relative[1]),
                (before["mapping"]["anatomicalWristToGrip"], after["mapping"]["anatomicalWristToGrip"])]
    require(all(p <= .002 and a <= .005 for p, a in (planner.pose_error(x, y) for x, y in matrices)),
            "reference/head/right-hand/weapon/body basis changed")


class ReceiveTiming:
    """Passive receive spans, using diagnostic267's first-plus-bounded-tail design.

    Detailed AckReadTrace remains in drive_bc2_reload_stimulus.py unchanged.
    These spans locate shared Driver receive/prepublication/ACK gaps; they grant no authority.
    Counters/maxima include evicted spans. No JSON or disk I/O occurs here.
    """
    def __init__(self, requested_ns, published_ns, nonce, now):
        self.name, self.at = "before_first_poll", requested_ns
        self.frozen = False
        self.reset(requested_ns, published_ns, nonce, now, initial=True)

    def reset(self, requested_ns, published_ns, nonce, now, initial=False):
        self.requested_ns, self.published_ns, self.nonce = requested_ns, published_ns, nonce
        self.first, self.tail = None, deque(maxlen=MAX_RECEIVE_TIMING_SPANS - 1)
        self.count, self.phase_ns, self.last_error = 0, {}, None
        self.transient_read_retries = {}
        self.last_nonce = None
        self.timeout_evaluated_ns = None
        self.publication_deadline_ns = None
        if not initial:
            self.at = now

    def phase(self, name, now=None):
        previous = self.name
        if self.frozen:
            return previous
        now = time.perf_counter_ns() if now is None else now
        elapsed = now - self.at
        row = dict(phase=previous, startedNs=self.at, finishedNs=now)
        self.count += 1
        if self.first is None:
            self.first = row
        else:
            self.tail.append(row)
        summary = self.phase_ns.setdefault(previous, dict(count=0, totalNs=0, maxNs=-1))
        summary["count"] += 1
        summary["totalNs"] += elapsed
        if elapsed > summary["maxNs"]:
            summary.update(maxNs=elapsed, maxStartedNs=self.at, maxFinishedNs=now)
        self.name, self.at = name, now
        return previous

    def read_retry(self):
        # Aggregate only; no per-read rows, paths, timestamps or additional I/O.
        if not self.frozen:
            self.transient_read_retries[self.name] = self.transient_read_retries.get(self.name, 0) + 1

    def error(self, error):
        if not self.frozen and self.last_error is None:
            # First phase error since this request; it may be a tolerated partial
            # sidecar. The existing result keeps the terminal refusal separately.
            self.last_error = dict(phase=self.name, atNs=time.perf_counter_ns(), kind=type(error).__name__,
                                   error=str(error)[:256], winerror=getattr(error, "winerror", None))

    def freeze(self, pending_command, sequence):
        if not self.frozen:
            self.phase("failed")
            self.pending_command, self.sequence = pending_command, sequence
            self.frozen = True

    def report(self):
        rows = ([] if self.first is None else [self.first]) + list(self.tail)
        return dict(schema="bc2vr.receive-timing-failure.v1", clock="perf_counter_ns",
                    nonce=self.nonce, requestStartedNs=self.requested_ns,
                    requestPublishedNs=self.published_ns,
                    originalResponseDeadlineNs=self.requested_ns + SNAPSHOT_TIMEOUT,
                    originalPublicationDeadlineNs=self.publication_deadline_ns,
                    timeoutEvaluatedNs=self.timeout_evaluated_ns,
                    frozenNs=self.at, pendingCommand=self.pending_command, sequence=self.sequence,
                    lastObservedResponseNonce=self.last_nonce, firstPhaseError=self.last_error,
                    spanCount=self.count, droppedSpans=self.count-len(rows),
                    phaseNs=self.phase_ns, transientReadRetries=self.transient_read_retries, spans=rows)


def receive_timed(name):
    def decorate(function):
        @wraps(function)
        def call(self, *args, **kwargs):
            with self.receive_timing_scope(name):
                return function(self, *args, **kwargs)
        return call
    return decorate


def fixed_post_reload_planner():
    # Load only for a fixed terminal stage; ordinary feed execution is unchanged.
    global _fixed_post_reload_planner
    if "_fixed_post_reload_planner" not in globals():
        spec = importlib.util.spec_from_file_location(
            "bc2_post_reload_fixed", Path(__file__).with_name("plan_bc2_fixed_container_simulation.py"))
        _fixed_post_reload_planner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(_fixed_post_reload_planner)
    return _fixed_post_reload_planner


def fixed_post_reload_closure(plan, closed):
    fixed = fixed_post_reload_planner()
    geometry = plan["geometryEvidence"]
    baseline, selection = geometry["baseline"], geometry["selection"]
    profile, family = selection["profile"], selection["physicalFamily"]
    insertion = integer(profile["kind"]) == 4
    stage = "Insert" if insertion else "Close"
    acquisition_gate = "fixed_aligned" if insertion else "fixed_acquired"
    completion_gate = "fixed_inserted" if insertion else "fixed_step_complete"
    require(geometry["schema"] == fixed.SCHEMA and geometry["stage"] == plan["stage"] == stage
            and plan["mapping"]["mechanismSelection"] == selection
            and all(plan["mapping"][k] == selection[k] for k in ("owner", "equipment", "rig"))
            and plan["mapping"]["physical_equipment_generation"] == selection["physical"]
            and integer(profile["kind"]) in (3, 4) and integer(profile["capacity"]) == 1
            and isinstance(family, dict) and family["selectedSecondary"] is True
            and baseline["counts"]["capacity"] == 1
            and baseline["counts"]["loaded"] == (0 if insertion else 1)
            and (integer(baseline["counts"]["reserve"]) > 0 if insertion else
                 integer(baseline["lastCompletedRequest"]) > 0),
            "fixed shot requires a completed single-round selected-secondary reload")
    acquired = closed["boundaries"][acquisition_gate]["gate"]
    complete = closed["boundaries"][completion_gate]["gate"]
    proof = acquired["completionPrerequisite"]
    require(acquired["gate"] == acquisition_gate and acquired["observed"] is True
            and complete["gate"] == completion_gate and complete["observed"] is True
            and complete["completionOnly"] is True
            and proof["stage"] == stage and proof["planSourceNonce"] == geometry["snapshotNonce"]
            and proof["planEvaluatedAtNs"] == geometry["evaluatedAtNs"]
            and proof["lifetime"] == baseline["lifetime"] and proof["gunToken"] == baseline["gun"]
            and acquired["inputObservedNs"] <= acquired["observedNs"] < acquired["sourceValidUntilNs"]
            and geometry["evaluatedAtNs"] < acquired["observedNs"] < complete["observedNs"]
            < complete["sourceValidUntilNs"], "fixed terminal stage lacks its own acquired/completed boundary")
    return dict(family="fixed", mapping=plan["mapping"], lifetime=baseline["lifetime"],
                fixedPlan=geometry, acquired=acquired, completed=complete)


def fixed_post_reload_counts(closure):
    geometry = closure["fixedPlan"]
    counts = geometry["baseline"]["counts"]
    return (dict(counts, loaded=1, reserve=counts["reserve"]-1)
            if geometry["stage"] == "Insert" else counts)


def post_reload_terminal_plan(path, closed):
    """Bind the original final stimulus of the completed same-owner result."""
    plans = closed["plans"]
    stages = [(p["family"], p["stage"]) for p in plans]
    if stages == [("feed", "CloseCover")]:
        combined = False
    else:
        require(1 <= len(plans) <= 5 and all(family == "fixed" for family, _ in stages),
                "post-reload requires a completed closing stimulus")
        names = tuple(stage for _, stage in stages)
        require(names in (("Close",), ("Insert",), ("Pickup", "Insert"),
                          ("Pickup", "Insert", "Close"), ("EjectSpent", "Pickup", "Insert", "Close"),
                          ("Open", "Pickup", "Insert", "Close"),
                          ("Open", "EjectSpent", "Pickup", "Insert", "Close")),
                "post-reload fixed stages do not end in a completed reload")
        combined = len(plans) > 1
    first, previous_time = None, None
    for index, summary in enumerate(plans, 1):
        plan, raw = read_json(path.parent / f"stimulus-{index:02}.json", MAX_BYTES)
        require(plan["schema"] == "bc2vr.reload-stimulus.v1"
                and (plan["family"], plan["stage"]) == stages[index-1],
                "original closing plan differs from its completed result")
        if plan["family"] == "fixed" and "selectionJsonPointer" in plan["geometryEvidence"]:
            geometry = plan["geometryEvidence"]
            require(geometry["selectionJsonPointer"] == "/mapping/mechanismSelection" and "selection" not in geometry,
                    "fixed reload selection reference is invalid or ambiguous")
            # Resolve only this exact local reference; preserve the original raw
            # artifact hash and all original proof/lease fields.
            plan = dict(plan, geometryEvidence=dict(geometry, selection=plan["mapping"]["mechanismSelection"]))
        if combined:
            geometry, mapping = plan["geometryEvidence"], plan["mapping"]
            baseline = geometry["baseline"]
            require(summary["stimulusCompleted"] is True and summary["boundaryChecksPassed"] is True
                    and summary["source"] == plan["source"]
                    and geometry["stage"] == plan["stage"]
                    and geometry["snapshotNonce"] == plan["source"]["nonce"]
                    and geometry["evaluatedAtNs"] == plan["source"]["evaluatedAtNs"]
                    and mapping["mechanismSelection"] == geometry["selection"],
                    "combined reload stage/source did not complete")
            current_time = integer(plan["source"]["evaluatedAtNs"])
            require(current_time < integer(plan["source"]["validUntilNs"])
                    and (previous_time is None or previous_time < current_time),
                    "combined reload original stage observations are not ordered/valid")
            identity = ({k: mapping[k] for k in ("owner", "equipment", "rig", "physical_equipment_generation",
                                                "selected", "mechanismSelection")},
                        {k: baseline[k] for k in ("lifetime", "supply", "binding", "nativeIdentity", "gun")})
            require(first is None or identity == first, "combined reload changed its exact owner or ammunition lineage")
            first, previous_time = identity, current_time
    return plan, raw


def fixed_post_reload_ready(snapshot, now, closure, *, neutral, after_shot):
    fixed = fixed_post_reload_planner()
    geometry, acquired = closure["fixedPlan"], closure["acquired"]
    baseline = geometry["baseline"]
    insertion = geometry["stage"] == "Insert"
    completed_counts = fixed_post_reload_counts(closure)
    try:
        if not after_shot:
            # The saved terminal acquisition is historical context only. Every
            # command decision consumes a new original current observation.
            fixed.check_gate(geometry, snapshot, "fixed_inserted" if insertion else "fixed_step_complete", now,
                             integer(snapshot["nonce"]), acquired=acquired)
        else:
            # A fired empty breech may owe Open. This is selected-ammunition
            # observation only and cannot authorize another trigger or pose.
            planner.admit_frame(snapshot, now, integer(snapshot["nonce"]),
                mechanism_admission=lambda s, n: s is snapshot and n == now)
        x = snapshot["fixedContainer"]
        require(x["schema"] == fixed.OBSERVATION_SCHEMA and x["available"] is True
                and x["observationOnly"] is True and x["owner"] == snapshot["owner"]
                and all(x["input"][k] == snapshot["input_evidence"][k] for k in
                        ("owner", "sequence", "observedNs", "deadlineNs", "focused", "tracked", "released")),
                "fixed post-shot current observation identity/input changed")
        fixed.require_reobserved(geometry, snapshot)
        t, view = x["tracking"], fixed.ammo(snapshot, now, idle=True)
        gun = fixed.claim(t["gun"], x["input"], now, 1, 1)
        require(gun == baseline["gun"] and gun["item"] == fixed.physical_item(snapshot, now)
                and t["enabled"] is True and t["current"] is True
                and t["step"] in ((2 if insertion else 0, 4) if after_shot else (4,))
                and snapshot.get("feed") is None and snapshot["weapon_actions_blocked"] == t["blocksFire"]
                and (after_shot or t["blocksFire"] is False)
                and t["hand"] is None and x["leftClaim"] is None and t["target"] is None
                and t["carried"] is False and t["carriedSupply"] is None,
                "fixed post-shot root/custody or retained step changed")
        expected = dict(completed_counts, loaded=0 if after_shot else 1)
        require(view["snapshot"]["counts"] == expected,
                "fixed post-shot chamber/reserve differs from the completed reload")
        event = x["lastCompletion"]
        out = event["outcome"]
        request, receipt = out["request"], out["receipt"]
        fixed.request_proof(request, view)
        require(event["lifetime"] == baseline["lifetime"] and event["supply"] == baseline["supply"]
                and out["state"] == 3 and out["nativeDispatched"] is True and out["actorRetirement"] is None
                and (integer(request["intent"]["id"]) > integer(baseline["lastCompletedRequest"]) if insertion else
                     request["intent"]["id"] == baseline["lastCompletedRequest"])
                and request["preparedCommand"] == receipt["command"]
                and receipt["authorityVerified"] is True and receipt["copiesVerified"] is True
                and integer(receipt["authorityInvocation"]) > 0
                and receipt["after"] == request["preparedCommand"]["after"]
                    == event["postSnapshot"]["counts"] == completed_counts
                and receipt["before"] == request["preparedCommand"]["before"]
                and (not insertion or receipt["before"] == baseline["counts"])
                and event["postSnapshot"]["coherentIdleVerified"] is True
                and event["postSnapshot"]["context"] == view["binding"]["context"]
                and integer(request["preparedCommand"]["requestedNs"]) <= integer(receipt["beganNs"])
                    <= integer(receipt["completedNs"]) <= integer(out["resolvedNs"])
                    <= integer(event["acceptedNs"]) <= integer(closure["completed"]["observedNs"] if insertion else geometry["evaluatedAtNs"])
                and (not insertion or integer(acquired["observedNs"]) < integer(request["preparedCommand"]["requestedNs"]))
                and integer(receipt["completedNs"]) <= integer(event["postSnapshot"]["observedNs"])
                    <= integer(event["acceptedNs"]) < integer(event["postSnapshot"]["deadlineNs"])
                and integer(event["postSnapshot"]["sequence"]) > integer(request["preparedCommand"]["sourceSequence"]),
                "fixed post-shot lost its exact completed insertion receipt")
        require(integer(snapshot["observed_ns"]) > integer(closure["completed"]["observedNs"]),
                "fixed shot requires a new observation after reload completion")
        if neutral:
            require(x["input"]["released"][0] is True and snapshot["input"]["hands"][0]["squeeze"] == 0
                    and all(h[k] == 0 for h in snapshot["input"]["hands"]
                            for k in ("trigger", "held", "stickX", "stickY")),
                    "fixed post-shot neutral release not observed")
    except fixed.Refusal as error:
        raise REFUSAL(str(error)) from error


def post_reload_ready(profiles, snapshot, now, closure, *, neutral, after_shot=False):
    """Current completed reload/custody gate; counts never prove a shot."""
    if closure.get("family") == "fixed":
        return fixed_post_reload_ready(snapshot, now, closure, neutral=neutral, after_shot=after_shot)
    planner.admit(profiles, snapshot, now, integer(snapshot["nonce"]))
    mapping = closure["mapping"]
    require(all(snapshot[k] == mapping[k] for k in
                ("owner", "equipment", "rig", "physical_equipment_generation"))
            and all(snapshot["selected"][k] == v for k, v in mapping["selected"].items()),
            "post-reload owner/configuration changed")
    f = snapshot["feed"]
    if f is not None:
        # This is retained presentation geometry, not the native closed state.
        # Applied(CloseCover) records Closed without snapping the measured angle.
        planner.finite(f["coverRadians"])
    require(f is not None and f["lifetime"] == closure["lifetime"]
            and f["blocksFire"] is False and snapshot["weapon_actions_blocked"] is False
            and integer(f["operation"]) == planner.STAGES["CloseCover"]
            and integer(f["carriedPartsMask"]) == 0 and f["supplyObject"] is None,
            "post-reload requires closed cover and exact empty carried custody")
    tick = snapshot["feedObservation"]["latest_tick"]
    require(all(tick[k] is False for k in ("pending", "gate_failed", "submit_refused")),
            "post-reload native operation pending/refused")
    if neutral:
        require(f["mechanismClaim"] is None and f["mechanismInput"] is None
                and f["input"]["released"][0] is True
                and snapshot["input_evidence"]["released"][0] is True
                and all(h[k] == 0 for h in snapshot["input"]["hands"]
                        for k in ("squeeze", "trigger", "held", "stickX", "stickY")),
                "post-reload neutral rearm/claim release not observed")


def post_reload_prepress_neutral(row, flow, boundary):
    """Join one restored neutral lock-contention guard to its original callback."""
    require(flow is not None and boundary is not None
            and boundary["schema"] == "bc2vr.feed-post-reload-pulse.v1"
            and integer(row["kind"]) == 0 and integer(row["depth"]) == 1
            and integer(row["end_ns"]) < integer(boundary["pressWrittenNs"]),
            "changed feed input is not a prepress neutral callback")
    guard, before, after = row["feed_input"], row["before"], row["after"]
    require(guard["original"] == [0, 0]
            and guard["effective"] == guard["before_restore"] == [1, 0]
            and all(type(v) is int for k in ("original", "effective", "before_restore") for v in guard[k])
            and before["current"] == before["next"] == after["current"] == after["next"] == 2
            and (before["loaded"], before["reserve"]) == (after["loaded"], after["reserve"]),
            "prepress feed inhibition changed Fire, readiness or ammunition")
    trace = flow.get("feed_guard_trace", {})
    require(trace.get("schema") == "bc2.feed-guard-trace.v1"
            and trace.get("enabled") == 1 and trace.get("rows_available") == 1,
            "prepress feed inhibition lacks a drained guard trace")
    matches = []
    for candidate in [*(r for ring in ("changes", "counts", "refills")
                         for r in trace.get(ring, {}).get("rows", [])), *trace.get("last", [])]:
        if candidate.get("invocation") == row["native_invocation"] and candidate not in matches:
            matches.append(candidate)
    require(len(matches) == 1, "prepress feed inhibition lacks one exact trace join")
    joined = matches[0]
    require(integer(joined["serial"]) > 0 and integer(joined["invocation"]) > 0
            and all(joined[k] == row[k] for k in ("thread", "depth", "caller", "context"))
            and joined["firing"] == before["firing"] == after["firing"]
            and joined["branch"] == before["branch"] == after["branch"]
            and row["begin_ns"] <= integer(joined["begin_ns"]) <= integer(joined["end_ns"]) <= row["end_ns"],
            "prepress feed trace belongs to another callback")
    owner = boundary["owner"]
    require(all(joined["owner"][k] == before[k] == after[k] == owner[v] for k, v in
                (("player", "player"), ("soldier", "soldier"), ("weak", "weak"), ("weapon", "weapon"),
                 ("actor_generation", "actorGeneration"), ("equip_generation", "equipGeneration"), ("space", "space")))
            and boundary["closure"]["mapping"]["owner"] == owner
            and joined["config"]["weapon_data"] == boundary["closure"]["mapping"]["equipment"]["data"]
            and integer(joined["lease_revision"]) > 0
            and 0 < integer(joined["lease_sequence"]) == before["snapshot_sequence"] == after["snapshot_sequence"]
            and 0 < integer(joined["lease_observed_ns"]) <= row["begin_ns"]
            and row["end_ns"] < integer(joined["lease_deadline_ns"]),
            "prepress feed trace lost its owner/configuration/source lease")
    require(all(integer(joined[k]) == value for k, value in
                (("evaluated", 4095), ("passed", 4095), ("lease", 1), ("retained_scope_fallback", 0),
                 ("lease_current_before", 1), ("lease_current_after", 1), ("lookup", 128),
                 ("retained_lookup", 0), ("gate_lifetime", 0), ("gate_revision", 0),
                 ("run_called", 1), ("run_admitted", 1), ("native_entered", 1), ("native_returned", 1),
                 ("observation_stage", 0), ("client_failure", 0), ("server_failure", 0),
                 ("input_known", 15), ("count_changed", 0), ("gap_count_changed", 0), ("refill_candidate", 0)))
            and joined["guard"] == guard
            and joined["input"] == [[0, 0], [1, 0], [1, 0], [0, 0]]
            and all(type(v) is int for phase in joined["input"] for v in phase),
            "prepress feed trace does not prove neutral lock contention and exact restoration")
    for name, native in (("before", before), ("after", after)):
        sample = joined[name]
        require(integer(sample["read"]) == integer(sample["identity_matches"]) == 1
                and sample["identity"] == joined["before"]["identity"]
                and len(sample["identity"]) == 3 and all(integer(v) > 0 for v in sample["identity"])
                and sample["identity"][1:] == [joined["config"]["firing_data"], joined["config"]["ammo_address"]]
                and sample["state"] == [native[k] for k in ("current", "previous", "next")]
                and sample["loaded"] == joined["guard_loaded"] == native["loaded"]
                and sample["reserve"] == native["reserve"] and sample["flags_a8"] == native["flags_a8"],
                "prepress feed trace changed firing identity, state or ammunition")
    return dict(recordId=row["id"], traceSerial=joined["serial"], nativeInvocation=row["native_invocation"],
                branch=before["branch"], beginNs=row["begin_ns"], endNs=row["end_ns"],
                beforePressNs=boundary["pressWrittenNs"] - row["end_ns"],
                owner=joined["owner"], reason="exactly restored prepress neutral lock-contention inhibition")


def post_reload_input_unmodified(row, flow=None, boundary=None):
    """Keep Fire unchanged; admit only separately proved prepress neutral inhibition."""
    require(row["transfer_path"] is None and row["hold_requested"] is False and row["hold_applied"] is False,
            "native shot boundary contains reload/hold overrides")
    # Native guard bool members are streamed as JSON integers, unlike hold flags.
    for name in ("fixed_input", "pistol_input", "cycle_guard"):
        require(all(type(row[name][k]) is int and row[name][k] == 0
                    for k in ("requested", "applied", "restored", "unexpected_write")),
                "native shot boundary contains input overrides")
    guard = row["feed_input"]
    flags = tuple(guard[k] for k in ("requested", "applied", "restored", "unexpected_write"))
    require(all(type(v) is int for v in flags), "invalid native feed guard flags")
    if flags == (0, 0, 0, 0):
        return
    require(flags == (1, 1, 1, 0),
            "native feed guard changed input or did not restore exactly")
    for name in ("context_before", "context_after"):
        context = row[name]
        require(isinstance(context, dict) and context["decoded_valid"] is True,
                "native feed guard lacks copied input")
        raw = context["bytes"]
        require(len(raw) == 0x30 and all(type(v) is int and 0 <= v <= 255 for v in raw),
                "native feed guard lacks exact input bytes")
        words = [sum(raw[offset+i] << (8*i) for i in range(4)) for offset in (0x28, 0x2c)]
        require(guard["original"] == words and words[0] & 255 == 0
                and words[1] in (0, 1) and context["input_flags"] == words[1],
                "native feed guard changed or inhibited ordinary Fire input")
    if not guard["original"] == guard["effective"] == guard["before_restore"]:
        return post_reload_prepress_neutral(row, flow, boundary)


def audit_post_reload_shot(flow, boundary):
    """Positive retained native Fire evidence; never claims exhaustive capture."""
    preparation = boundary["schema"] == "bc2vr.fixed-preparation-pulse.v1"
    fixed_shot = preparation or boundary["schema"] == "bc2vr.fixed-post-reload-pulse.v1"
    if preparation:
        require(boundary["preparationOnly"] is True and "closure" not in boundary
                and "closeResultSha256" not in boundary, "preparation cannot claim reload completion")
    require(boundary["schema"] in ("bc2vr.feed-post-reload-pulse.v1", "bc2vr.fixed-post-reload-pulse.v1", "bc2vr.fixed-preparation-pulse.v1")
            and boundary["status"] == "native_shot_observation_pending"
            and boundary["nativeAcceptance"] is False, "missing completed pulse boundary")
    start, end = (integer(boundary[k]) for k in ("pressWrittenNs", "releaseCommittedNs"))
    begin = integer(boundary["preparationObservedNs"] if preparation else boundary["neutralWrittenNs"])
    finish = integer(boundary["observationEndNs"])
    pulse = boundary.get("pulseTransport")
    if pulse is None:
        release = integer(boundary["releaseWrittenNs"])
        require(start < release <= end and release - start <= 100_000_000
                and end - release <= 250_000_000, "post-reload pulse outside original bound")
    else:
        state = pulse["neutralState"]
        controls = pulse["expectedNeutralControls"]
        press_started, press_deadline, press_committed, driver_deadline = (
            integer(pulse[k]) for k in
            ("pressStartedNs", "pressDeadlineNs", "pressCommittedNs", "driverDeadlineNs"))
        require(pulse["backend"] == "runtime_timed" and pulse["command"] == TIMED_TRIGGER_COMMAND
                and pulse["runtimeSha256"] == TIMED_TRIGGER_RUNTIME_SHA256
                and pulse["configuredMilliseconds"] == 50 and boundary["releaseWrittenNs"] is None
                and pulse["physicalDurationNs"] is None and pulse["nativeDeliveryProved"] is False
                and 0 < press_started <= integer(pulse["publicationStartedNs"]) <= start < press_committed
                and press_committed < press_deadline == min(driver_deadline, press_started + 100_000_000)
                and press_committed <= end == integer(pulse["neutralObservedNs"])
                and end < integer(pulse["observationDeadlineNs"])
                    == min(driver_deadline, press_committed + 250_000_000)
                and integer(state["frame"]) >= integer(pulse["ackFrame"]) + 2
                and integer(state["frame"]) > integer(pulse["beforeFrame"])
                and integer(state["endFrames"]) > integer(pulse["beforeEndFrames"])
                and state["pid"] == boundary["host"]["pid"] and state["runtime"] == "xr-sim"
                and state["sessionState"] == "FOCUSED" and state["errors"] == 0 and state["lastCmdError"] == ""
                and state["controls"] == controls and controls.get("trigR") == 0
                and all(all(v == 0 for v in value) if isinstance(value, list) else value == 0
                        for value in controls.values()), "runtime pulse lacks bounded fresh neutral evidence")
    require(0 < begin < start < end < finish and finish - end <= NS,
            "post-reload pulse/observation outside original bound")
    require(flow["started"] is True and flow["drained"] is True
            and flow["server_binding_verified"] is True and integer(flow["in_flight"]) == 0,
            "native shot observation absent: drained native report required")
    # Record IDs are allocated after owner and record-lock gates; contiguous
    # retained IDs cannot localize global losses or prove exhaustive capture.
    drops = {k: integer(flow[k]) for k in
             ("dropped", "record_lock_drops", "record_begin_lock_drops", "record_end_lock_drops", "owner_lock_drops")}
    require(all(v >= 0 for v in drops.values()), "invalid native drop counters")
    fixed_baseline = (boundary["preparation"] if preparation else
                      boundary["closure"]["fixedPlan"]["baseline"] if fixed_shot else None)
    if fixed_shot:
        completed_counts = fixed_baseline["counts"] if preparation else fixed_post_reload_counts(boundary["closure"])
        require((preparation or boundary["closure"]["family"] == "fixed")
                and boundary["beforeCounts"] == completed_counts
                and boundary["afterCounts"] == dict(completed_counts, loaded=0)
                and boundary["beforeCounts"]["loaded"] == boundary["beforeCounts"]["capacity"] == 1,
                "fixed pulse lost its completed reload ammunition boundary")
    source = boundary["owner"]
    owner = {key: source[value] for key, value in
             (("player", "player"), ("soldier", "soldier"), ("weak", "weak"), ("weapon", "weapon"),
              ("actor_generation", "actorGeneration"), ("equip_generation", "equipGeneration"), ("space", "space"))}
    copies, seen, window, neutral_guards = {n: [] for n in range(3)}, set(), [], []
    for r in flow["records"]:
        if not (begin <= integer(r["begin_ns"]) <= finish):
            continue
        window.append(r)
        b = r.get("before")
        if not isinstance(b, dict) or not all(b.get(k) == v for k, v in owner.items()):
            continue
        a = r.get("after")
        require(r["finished"] is True and r["identity_retained"] is True and isinstance(a, dict)
                and all(a.get(k) == v for k, v in owner.items())
                and integer(r["begin_ns"]) <= integer(r["end_ns"]) <= finish,
                "native shot observation has an unfinished/changed owner callback")
        neutral_guard = post_reload_input_unmodified(r, flow, boundary)
        if neutral_guard is not None:
            neutral_guards.append(neutral_guard)
        require(integer(r["id"]) > 0 and integer(r["id"]) not in seen,
                "native shot record duplicated or invalid")
        seen.add(integer(r["id"]))
        if integer(r["kind"]) != 0 or integer(r["depth"]) != 1:
            require((b["loaded"], b["reserve"]) == (a["loaded"], a["reserve"]),
                    "native count changed outside an original top-level Update")
            continue
        n = integer(b["branch"])
        require(n in copies and a["branch"] == n and integer(b["firing"]) > 0
                and b["firing"] == a["firing"],
                "native shot firing identity/record duplicated or changed")
        if n == 2:
            require(all(integer(b[k]) > 0 and b[k] == a[k]
                        for k in ("server_player", "server_soldier", "server_item")),
                    "authoritative server shot owner absent")
        copies[n].append(r)
    events, counts, firing, tokens, bounds = [], [], [], [], []
    for n, rows in copies.items():
        rows.sort(key=lambda r: r["begin_ns"])
        pre = [r for r in rows if r["end_ns"] < start
               and r["after"]["current"] == r["after"]["next"] == 2]
        post = [r for r in rows if end <= r["begin_ns"]
                and r["after"]["current"] == r["after"]["next"]
                and (r["after"]["loaded"] == 0 if fixed_shot else r["after"]["current"] == 2)]
        changed = [r for r in rows if (r["before"]["loaded"], r["before"]["reserve"])
                   != (r["after"]["loaded"], r["after"]["reserve"])]
        require(pre and post and changed, "native shot observation absent on copy " + str(n))
        old, new, shot = pre[-1]["after"], post[-1]["after"], changed[0]["before"]
        if fixed_shot:
            identity = fixed_baseline["nativeIdentity"]
            require(boundary["owner"] == identity["owner"]
                    and shot["firing"] == identity["firing"][n]
                    and (old["loaded"], new["loaded"], old["reserve"])
                        == (1, 0, completed_counts["reserve"]),
                    "native shot does not belong to the reloaded fixed ammunition")
            if n == 2:
                require(all(shot[k] == identity[v] for k, v in
                            (("server_player", "serverPlayer"), ("server_soldier", "serverSoldier"),
                             ("server_item", "serverItem"))), "fixed server ammunition association changed")
        count, previous_end, order = integer(old["loaded"]), begin, []
        require(count > 0 and integer(old["reserve"]) >= 0, "invalid pre-shot ammunition")
        for r in rows:
            b, a = r["before"], r["after"]
            require(previous_end <= r["begin_ns"] and b["firing"] == shot["firing"]
                    and integer(b["loaded"]) == count and b["reserve"] == a["reserve"] == old["reserve"],
                    "native observed firing identity/count chain changed")
            previous_end = r["end_ns"]
            if b["loaded"] == a["loaded"]:
                continue
            x, y = r["context_before"], r["context_after"]
            # Release acknowledgement may arrive later; retained native
            # Fire events still have the original 100ms pulse window.
            require(start <= r["begin_ns"] <= r["end_ns"] <= min(end, start + 100_000_000)
                    and 0 <= integer(a["loaded"]) == count - 1,
                    "native observed change is not one loaded round during the pulse")
            require(b["current"] in (2, 9) and b["next"] == b["current"] and a["current"] == a["next"] == 9
                    and isinstance(x, dict) and isinstance(y, dict)
                    and x["decoded_valid"] is True and y["decoded_valid"] is True
                    and x["input_flags"] == y["input_flags"] == 1
                    and 0 <= integer(x["raw_1c"]) <= 0xffffffff and x["raw_1c"] == y["raw_1c"],
                    "native decrement lacks an original Fire event")
            count = integer(a["loaded"])
            order.append(x["raw_1c"])
            events.append(dict(branch=n, recordId=r["id"], token=x["raw_1c"],
                               loadedBefore=b["loaded"], loadedAfter=a["loaded"],
                               beginNs=r["begin_ns"], endNs=r["end_ns"]))
        require(count == new["loaded"] and len(set(order)) == len(order),
                "native observed burst has duplicate tokens or unexplained final ammunition")
        if n == 2:
            require(all(s[k] == shot[k] for r in rows for s in (r["before"], r["after"])
                        for k in ("server_player", "server_soldier", "server_item")),
                    "native server association changed")
        counts.append((old["loaded"], new["loaded"], old["reserve"]))
        firing.append(shot["firing"])
        tokens.append(order)
        bounds.append(dict(branch=n, firstRecordId=rows[0]["id"], lastRecordId=rows[-1]["id"],
                           preReadyRecordId=pre[-1]["id"], postReadyRecordId=post[-1]["id"],
                           beginNs=rows[0]["begin_ns"], endNs=rows[-1]["end_ns"]))
    require(len(set(counts)) == 1 and len(set(firing)) == 3 and tokens[0] == tokens[1] == tokens[2],
            "native copies disagree on the observed Fire events")
    for guard in neutral_guards:
        trace_owner = guard.pop("owner")
        server = copies[2][0]["before"]
        require(trace_owner["firing"] == firing
                and all(trace_owner[k] == server[k] for k in ("server_player", "server_soldier", "server_item")),
                "prepress feed trace lost the three-copy native owner association")
    require(all(all(s.get(k) == v for k, v in owner.items())
                for r in window for s in (r.get("before"), r.get("after"))
                if isinstance(s, dict) and s.get("firing") in firing),
            "native selected firing copy changed owner inside the observation")
    return dict(status="native_fixed_preparation_shot_observed" if preparation else "native_post_reload_shot_observed",
                **({"preparationOnly": True, "reloadCompleted": False} if preparation else {}), nativeShotObserved=True,
                loadedBefore=counts[0][0], loadedAfter=counts[0][1], reserve=counts[0][2],
                roundsObserved=len(tokens[0]), events=events, observedBounds=bounds,
                prepressNeutralInhibitions=neutral_guards,
                completeWindow=False, nativeDropCounters=drops,
                nativeAcceptance=False, headsetAcceptance=False,
                evidence="Matched same-owner native Fire events and connected retained ammunition on all three copies; "
                         "capture completeness and full reload acceptance are not established. "
                         "Native drop counters are session-global and cannot localize missing callbacks. "
                         "Any admitted prepress neutral inhibition has an exact joined restoration; "
                         "guard trace samples remain sequential diagnostic observations.")


TIMED_TRIGGER_RUNTIME_SHA256 = "2c5eb913be5d4a6b86ed38e308b085985cd86e7454c149e91d5d629c3259d20a"
TIMED_TRIGGER_COMMAND = "trigger r pull 50"


class Driver:
    @contextmanager
    def receive_timing_scope(self, name):
        trace = getattr(self, "_receive_timing", None)
        if trace is None:
            now = time.perf_counter_ns()
            trace = self._receive_timing = ReceiveTiming(
                getattr(self, "last_request", now) or now,
                getattr(self, "last_request_published", None),
                getattr(self, "pending_nonce", None), now)
        previous = trace.phase(name)
        token = _receive_read_timing.set(trace)
        try:
            yield trace
        except BaseException as error:
            trace.error(error)
            raise
        finally:
            _receive_read_timing.reset(token)
            trace.phase("between_calls" if previous == "before_first_poll" else previous)

    def receive_timing_call(self, name, function, *args):
        with self.receive_timing_scope(name):
            return function(*args)

    def receive_snapshot_json(self):
        with self.receive_timing_scope("snapshot_read") as trace:
            token = _snapshot_json_timing.set(trace)
            try:
                result = read_json(self.snapshot_path, planner.SNAPSHOT_LIMIT)
                value = result[0].get("nonce") if isinstance(result[0], dict) else None
                trace.last_nonce = str(value)[:20] if isinstance(value, (int, str)) else None
                return result
            finally:
                _snapshot_json_timing.reset(token)

    def read_publication_owner(self, expected_bytes, deadline_ns):
        with self.receive_timing_scope("publish_owner_read") as trace:
            token = _owner_read_timing.set(trace)
            try:
                record = getattr(self, "_publication_owner", None)
                require(record is not None, "pending owner HANDLE custody unavailable")
                return record.validate(expected_bytes, deadline_ns)
            finally:
                _owner_read_timing.reset(token)

    def save_receive_timing_failure(self):
        trace = getattr(self, "_receive_timing", None)
        if trace is not None and trace.frozen:
            # Called after failure cleanup/owner closure; preserve the original
            # refusal even if the existing artifact bound or filesystem refuses.
            self._receive_timing = None
            try:
                self.artifacts.save("receive-timing-failure.json", trace.report(), final=True)
            except Exception:
                pass

    def admit_snapshot(self, snapshot, now, nonce):
        """Mechanism-specific admission; transport and original leases stay shared."""
        closure = getattr(self, "post_reload_closure", None)
        if closure is not None and closure.get("family") == "fixed":
            return post_reload_ready(self.profiles, snapshot, now, closure, neutral=False,
                                     after_shot=getattr(self, "post_reload_phase", None) == "observe")
        return planner.admit(self.profiles, snapshot, now, nonce)

    def stable_snapshot(self, before, after):
        return stable_snapshot(before, after)

    def validate_publication(self, lines, expected, cleanup):
        """Optional final validation after polling, immediately before publishing."""
        phase = getattr(self, "post_reload_phase", None)
        if phase is None:
            return
        if lines == ["trigger r 0"]:
            require(self.held_trigger and not self.pending_command, "post-reload trigger release is not owned")
            return
        require(phase == "press" and lines == ["trigger r 1"] and not cleanup
                and not self.held_trigger and not self.pending_command, "post-reload permits one trigger press only")
        post_reload_ready(self.profiles, self.latest_snapshot, time.perf_counter_ns(),
                          self.post_reload_closure, neutral=True)

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
        self.timed_trigger = None
        self.timed_trigger_supported = False
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
        self.artifacts = Artifacts(self.run, feed_artifact_limit(args, self.manifest))
        self.artifacts.save("invocation.json", {"stage": args.stage, "pickup_first": args.pickup_first,
                            "pickup_approach": getattr(args, "pickup_approach", False),
                            "capture": args.capture, "menu_batches": str(args.menu_batches) if args.menu_batches else None,
                            "host": self.host, "startedNs": self.started, "maximumDurationNs": 10 * NS,
                             "maximumCommands": MAX_COMMANDS, "maximumArtifactBytes": self.artifacts.maximum_bytes,
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
        # Capability belongs only to this checked runtime, never a version/name.
        self.timed_trigger_supported = self.manifest["runtime_library"]["sha256"] == TIMED_TRIGGER_RUNTIME_SHA256
        self.capture_plan = admit_capture_plan(self.manifest)
        require(self.link.get("capture_plan") == self.capture_plan, "session link capture plan differs")
        self.state = self.read_state()
        self.capture_sequence = integer(self.state["captureSeq"])
        self.command_hash = digest(read_bytes(self.command_path, 4096))
        self.sequence = integer(self.state["cmdSeq"])
        self.sentinel = self.state["lastCmd"]
        owner_path = self.run / "live-driver-owner.json"
        if owner_path.exists():
            previous, _ = read_json(owner_path, 8192)
            require(previous["host"] == self.host and previous["pending"] is False
                    and previous.get("timedTrigger") is None
                    and previous["commandSha256"] == self.command_hash and previous["sequence"] == self.sequence
                    and previous["sentinel"] == self.sentinel, "another or unsettled command writer owns this session")
        else:
            require(self.sequence == self.startup["target_sequence"]
                    and self.command_hash == self.startup["command_file"]["sha256"]
                    and self.sentinel == self.startup["settled_state"]["lastCmd"], "startup command ownership changed")
        self.initial_sim = self.state
        self.artifacts.save("initial-state.json", self.state)
        self.profiles = None
        if args.stage or getattr(args, "post_reload_shot", None):
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
        self.dispose_publication_owner()
        if getattr(self, "owner", None):
            if self.lock not in (None, ctypes.c_void_p(-1).value):
                self.owner.api.CloseHandle(self.lock)
                self.lock = None
            self.owner.close()
        self.save_receive_timing_failure()
        # Preserve failed-episode timing and retire the process handles even if
        # deleting an unpublished prepared temporary itself fails.
        self.discard_prepared_command()
        for prepared in tuple(getattr(self, "_prepared_cleanup_pending", [])):
            self.dispose_prepared_command(prepared)
        self.save_prepared_cleanup_failure()

    def read_state(self):
        self.owner.check()
        session, _ = read_json(self.session_path)
        require(session["host"] == self.host and session["supervisor"] == self.link["supervisor"]
                and session["source_sha256"] == self.manifest["source_sha256"]
                and session["version"] == self.manifest["version"] and session["state"] == "running",
                "linked native session is no longer running with the exact owners")
        require(not (self.folder / "stop.request").exists(), "normal session stop was requested")
        state, _ = read_until(self.hard_deadline, read_json, self.state_path, 16384)
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
        except ReadDeadlineExceeded:
            raise
        except BaseException:
            self.unsafe_owner = True
            raise

    def owner_bytes(self, pending):
        timer = getattr(self, "timed_trigger", None)
        value = {"host": self.host,
               # Older readers only understand pending. They must also refuse
               # a settled batch whose runtime timer has not been retired.
               "pending": pending or timer is not None, "sequence": self.sequence, "sentinel": self.sentinel,
               "commandSha256": self.command_hash, "artifactDirectory": str(self.artifacts.directory)}
        if timer is not None:
            value["timedTrigger"] = timer
        return json_bytes(value)

    def dispose_publication_owner(self):
        record = getattr(self, "_publication_owner", None)
        self.dispose_prepared_command(record)
        if record is not None and record.stream is None:
            self._publication_owner = None

    def save_owner(self, pending, *, settled_writer=None):
        self.dispose_publication_owner()
        require(getattr(self, "_publication_owner", None) is None,
                "pending owner HANDLE did not close")
        if pending:
            self._publication_owner = PendingOwner(self, self.run / "live-driver-owner.json",
                                                   self.owner_bytes(True))
        else:
            (settled_writer or atomic)(self.run / "live-driver-owner.json", self.owner_bytes(False))

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
        trace = getattr(self, "_receive_timing", None)
        if trace is None:
            self._receive_timing = ReceiveTiming(now, published, nonce, published)
        elif not trace.frozen:
            trace.reset(now, published, nonce, published)
        self.artifacts.save(f"request-{self.snapshot_index:03}.txt", raw)
        self.artifacts.save(f"request-{self.snapshot_index:03}-timing.json", {
            "nonce": nonce, "clock": "perf_counter_ns", "requestStartedNs": now,
            "requestPublishedNs": published, "responseDeadlineNs": now + SNAPSHOT_TIMEOUT,
            "budgetNs": SNAPSHOT_TIMEOUT})

    def snapshot_failure(self):
        path = self.snapshot_path.with_name("mechanism-snapshot-failure.json")
        if not self.receive_timing_call("failure_exists", path.exists):
            return None
        raw = self.receive_timing_call("failure_read", read_bytes, path, 1024)
        # The native worker retains this CREATE_NEW handle and only rewrites it
        # on terminal failure. A partial in-place update is never an outcome.
        if not raw.endswith(b"\n"):
            return None
        try:
            value = self.receive_timing_call("failure_parse", json.loads, raw)
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
        if not self.receive_timing_call("snapshot_exists", self.snapshot_path.exists):
            return None
        snapshot, raw = self.receive_snapshot_json()
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
        require(self.receive_timing_call("request_read", read_bytes, self.request_path, 22) == self.request_bytes,
                "snapshot request changed by another writer")
        self.pending_nonce = None
        self.request_snapshot()
        return None

    def snapshot_timeout(self, now, response=None):
        """Preserve a late/stale response as refused evidence, never as admission."""
        trace = getattr(self, "_receive_timing", None)
        if trace is not None and not trace.frozen:
            # Reuse the original decision sample to distinguish subsequent
            # diagnostic-only reads from work that consumed the response lease.
            trace.timeout_evaluated_ns = now
        read_error = None
        if response is None and self.receive_timing_call("snapshot_exists", self.snapshot_path.exists):
            try:
                response = self.receive_snapshot_json()
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
        deadline = min(integer(snapshot["valid_until_ns"]),
                       integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)
        if (getattr(self, "post_reload_closure", None) or {}).get("family") == "fixed":
            x = snapshot["fixedContainer"]
            deadline = min(deadline, integer(snapshot["native_deadline_ns"]),
                integer(x["input"]["deadlineNs"]), integer(x["physicalFamily"]["deadlineNs"]),
                integer(x["view"]["binding"]["deadlineNs"]), integer(x["view"]["snapshot"]["deadlineNs"]),
                integer(x["tracking"]["gun"]["deadlineNs"]))
            if getattr(self, "post_reload_phase", None) != "observe":
                deadline = min(deadline, integer(x["raw"]["input"]["deadlineNs"]))
        return deadline

    def defer_invalid_snapshot(self, snapshot, now):
        """Optional observation-only hold of this exact consumed response.

        The caller has checked writer, failure, nonce and original response
        deadline, and retained its original bytes. Returning True cannot admit
        the invalid frame; an adapter must explicitly request a fresh response.
        """
        return False

    def snapshot_usable(self, snapshot, now):
        """Default weapon admission stays strict; independent observers opt in."""
        return snapshot.get("valid") is True

    @receive_timed("receive")
    def receive_snapshot(self):
        if self.pending_nonce is None:
            return None
        require(self.receive_timing_call("request_read", read_bytes, self.request_path, 22)
                == self.request_bytes, "snapshot request changed by another writer")
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
        if not self.receive_timing_call("snapshot_exists", self.snapshot_path.exists):
            return None
        snapshot, raw = self.receive_snapshot_json()
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
        if not self.snapshot_usable(snapshot, now):
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

    @receive_timed("pump")
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

    def prepare_next_command(self, lines):
        require(getattr(self, "_prepared_command", None) is None
                and not getattr(self, "_prepared_cleanup_pending", [])
                and not self.pending_command and not self.unsafe_owner,
                "command preparation requires unambiguous current ownership")
        require(0 < len(lines) <= 8 and all(isinstance(x, str) and "\n" not in x and "\r" not in x for x in lines),
                "invalid command batch")
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget exhausted")
        self.bounds(time.perf_counter_ns())
        self._prepared_command = PreparedCommand(self, lines)

    def dispose_prepared_command(self, prepared):
        if prepared is None:
            return
        try:
            prepared.close()
        except BaseException as error:
            # Independent bounded diagnostics: never feed cleanup into the first
            # command-phase error, and retain custody for a later close attempt.
            errors = getattr(self, "_prepared_cleanup_errors", None)
            if errors is None:
                self._prepared_cleanup_errors = errors = deque(maxlen=16)
            errors.append(dict(atNs=time.perf_counter_ns(), kind=type(error).__name__,
                               error=str(error)[:256], temporary=str(prepared.temporary),
                               winerror=getattr(error, "winerror", None)))
            pending = getattr(self, "_prepared_cleanup_pending", None)
            if pending is None:
                self._prepared_cleanup_pending = pending = []
            if prepared not in pending:
                pending.append(prepared)
            return
        pending = getattr(self, "_prepared_cleanup_pending", [])
        if prepared in pending:
            pending.remove(prepared)

    def discard_prepared_command(self):
        prepared = getattr(self, "_prepared_command", None)
        self._prepared_command = None
        self.dispose_prepared_command(prepared)

    def save_prepared_cleanup_failure(self):
        errors = getattr(self, "_prepared_cleanup_errors", None)
        if errors:
            try:
                self.artifacts.save("prepared-command-cleanup-failure.json", dict(
                    schema="bc2vr.prepared-command-cleanup-failure.v1", errors=list(errors),
                    retainedTemporaryCount=len(getattr(self, "_prepared_cleanup_pending", []))), final=True)
            except Exception:
                pass

    @receive_timed("command")
    def send(self, lines, expected=None, capture=None, cleanup=False, source_deadline=None,
             final_release=False):
        prepared = getattr(self, "_prepared_command", None)
        self._prepared_command = None  # one attempt only, even on refusal
        if cleanup:
            # Failure to dispose cannot suppress the normal owned release.
            self.dispose_prepared_command(prepared)
            prepared = None
        try:
            return self._send(lines, expected, capture, cleanup, source_deadline, final_release, prepared)
        finally:
            self.dispose_publication_owner()
            self.dispose_prepared_command(prepared)

    def _send(self, lines, expected, capture, cleanup, source_deadline, final_release, prepared):
        require(0 < len(lines) <= 8 and all(isinstance(x, str) and "\n" not in x and "\r" not in x for x in lines),
                "invalid command batch")
        require(getattr(self, "timed_trigger", None) is None or lines == ["trigger r 0"],
                "runtime trigger timer remains in custody; no new input permitted")
        timed = getattr(self, "_timed_trigger_arm", False)
        require(not timed or (lines == ["trigger r 1"] and not cleanup and prepared is None
                and (expected is None or getattr(self, "_timed_trigger_retained_grips", None) is not None)
                and capture is None and not final_release
                and self.timed_trigger_supported), "invalid timed trigger publication")
        require(all(not line.startswith("trigger r pull") for line in lines),
                "timed trigger requires the shared admitted single-shot path")
        require(self.commands + len(lines) + 1 <= MAX_COMMANDS, "512-command budget exhausted")
        if not cleanup:
            self.receive_timing_call("command_pause", self.pause_until,
                                     max(self.last_write + 25_000_000, time.perf_counter_ns()))
        else:
            remaining = self.last_write + 25_000_000 - time.perf_counter_ns()
            if remaining > 0:
                time.sleep(remaining / NS)
        before = self.receive_timing_call("command_state_read", self.read_state)
        self.receive_timing_call("command_ownership", self.ownership, before)
        require(integer(before["captureSeq"]) == self.capture_sequence, "unrequested capture occurred")
        if self.reference is not None and not cleanup:
            for key in ("head", "handR"):
                require(before[key] == self.initial_sim[key], "simulator head/right-hand basis changed")
        if prepared is not None:
            prepared.matches(self, lines)
            sentinel, batch, target, raw = prepared.sentinel, prepared.batch, prepared.target, prepared.raw
        else:
            sentinel = "note root-feed-" + uuid.uuid4().hex
            batch = ([TIMED_TRIGGER_COMMAND] if timed else lines) + [sentinel]
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
        self.receive_timing_call("publish_request_event", self.artifacts.event,
            {"type": "command_request", "atNs": written, "beforeSequence": self.sequence,
             "targetSequence": target, "commands": batch}, cleanup)
        until = min(self.deadline, self.stage_deadline,
                    written + (250_000_000 if self.motion_started is not None else NS))
        if cleanup:
            until = min(self.hard_deadline, written + 250_000_000)
        publication_deadline = min(until, source_deadline) if source_deadline is not None else until
        if not self._receive_timing.frozen:
            self._receive_timing.publication_deadline_ns = publication_deadline
        previous_owner = self.sequence, self.sentinel, self.command_hash
        previous_timer = dict(self.timed_trigger) if getattr(self, "timed_trigger", None) is not None else None
        if timed:
            retained = getattr(self, "_timed_trigger_retained_grips", None) or {}
            self.check_timed_trigger_baseline(before["controls"], retained)
            if expected is not None:
                require(expected.get("commands") == lines and expected.get("fire") is True
                        and all(expected.get(key) == before[key] for key in ("head", "handL", "handR"))
                        and expected.get("controls") == dict(before["controls"], trigR=1),
                        "retained-grip trigger target differs from exact settled input")
            self.timed_trigger = dict(command=TIMED_TRIGGER_COMMAND, configuredMilliseconds=50,
                runtimeSha256=TIMED_TRIGGER_RUNTIME_SHA256, beforeFrame=integer(before["frame"]),
                beforeEndFrames=integer(before["endFrames"]), neutralControls=before["controls"],
                basis={key: before[key] for key in ("head", "handL", "handR")},
                sequence=target, sentinel=sentinel, commandSha256=digest(raw),
                publicationStartedNs=written, ackFrame=None, numericRelease=False)
            if retained:
                self.timed_trigger["retainedGrips"] = dict(retained)
        elif lines == ["trigger r 0"] and getattr(self, "timed_trigger", None) is not None:
            self.timed_trigger["numericRelease"] = True
        self.sequence, self.sentinel, self.command_hash = target, sentinel, prepared.hash if prepared else digest(raw)
        self.pending_command = True
        self.pending_batch, self.pending_before = batch, before
        pending_owner = self.owner_bytes(True)
        try:
            self.receive_timing_call("publish_owner_save", self.save_owner, True)
        except BaseException:
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            self.timed_trigger = previous_timer
            raise
        def validate_publication_owner():
            if self.last_commit is not None and not cleanup:
                self.bounds(time.perf_counter_ns())
            try:
                require(self.read_publication_owner(pending_owner, publication_deadline) == pending_owner,
                        "command owner record replaced before publication")
                current = self.receive_timing_call("publish_state_read", read_until,
                    publication_deadline, self.read_state)
                self.receive_timing_call("publish_command_read", self.ownership, current, previous_owner)
                require(integer(current["captureSeq"]) == self.capture_sequence, "unrequested capture before publication")
                if timed:
                    require(all(current[key] == before[key] for key in ("head", "handL", "handR", "controls")),
                            "runtime trigger input changed before publication")
                if self.reference is not None and not cleanup:
                    require(all(current[key] == self.initial_sim[key] for key in ("head", "handR")),
                            "simulator head/right-hand basis changed before publication")
            except ReadDeadlineExceeded:
                # Expiry refuses this publication, but is not an observed owner
                # change. The unpublished rollback below must independently
                # rejoin the exact previous writer and unchanged input before
                # existing bounded release-only cleanup can become eligible.
                raise
            except BaseException:
                self.unsafe_owner = True
                raise
        try:
            self.receive_timing_call("publish_file", prepared.publish if prepared else publish_command,
                self.command_path, raw, publication_deadline, validate_publication_owner)
        except BaseException as error:
            # Atomic replacement never committed. No target sequence or held
            # input is ours to settle; cleanup retains the prior exact command.
            self.sequence, self.sentinel, self.command_hash = previous_owner
            self.pending_command = False
            self.timed_trigger = previous_timer
            if not self.unsafe_owner:
                try:
                    require(self.read_publication_owner(pending_owner, self.hard_deadline) == pending_owner,
                            "unpublished owner record replaced")
                    restored = self.read_state()
                    self.ownership(restored)
                    require(all(restored[key] == before[key]
                                for key in ("head", "handL", "handR", "controls", "captureSeq")),
                            "unpublished simulator input changed before owner restoration")
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
        self.dispose_publication_owner()
        # Rename has committed and pending ownership is retained before closing
        # the exclusive handle lets the simulator consume the command. A close
        # failure is separate cleanup evidence, never an unpublished rollback.
        self.dispose_prepared_command(prepared)
        trace = getattr(self, "_receive_timing", None)
        if trace is not None:
            trace.phase("ack_gap", published)
        # A neutral approach has already published its only authorized pose.
        # Settle that exact batch before observing a late snapshot: an OS read
        # may consume the response window while the independent original ACK
        # deadline remains live. No held input, capture or new command gets this
        # ordering, and no deadline or native source lease is extended.
        neutral_approach_ack = (
            not cleanup and capture is None and not final_release and self.monitor
            and self.snapshot_readiness_scope() == "neutral_approach"
            and not (self.unsafe_owner or self.held_left or self.held_right
                     or self.held_trigger or self.held_buttons)
            and expected is not None
            and expected.get("phase") in ("neutral", "approach", "neutral_at_contact")
            and expected.get("grip") == 0 and lines == expected.get("commands")
            and len(lines) == 2 and lines[1] == "grip l 0"
            and lines[0] == "hand l grip pose " + " ".join(
                f"{v:.9f}" for v in expected["positionMeters"] + expected["yawPitchRollDegrees"])
            and before["controls"] and all(
                all(component == 0 for component in value) if isinstance(value, list) else value == 0
                for value in before["controls"].values()))
        ack, settled = None, None
        for _ in range(1200):
            if neutral_approach_ack:
                self.bounds(time.perf_counter_ns())
            elif not cleanup:
                self.pump()
            require(time.perf_counter_ns() < until, "command acknowledgement/commit deadline exceeded")
            state = self.receive_timing_call("ack_state_read", read_until, until, self.read_state)
            require(digest(self.receive_timing_call("ack_command_read", read_until, until, read_bytes, self.command_path, 4096))
                    == self.command_hash, "concurrent command writer")
            seq = integer(state["cmdSeq"])
            require(seq in (target - len(batch), target), "unexpected command sequence advancement")
            if seq == target:
                require(state["lastCmd"] == sentinel, "unique command sentinel mismatch")
                if self.receive_timing_call("ack_exists", self.ack_path.exists):
                    ack_raw = self.receive_timing_call("ack_read", read_until, until, read_bytes, self.ack_path, 4096).decode("utf-8")
                    match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", ack_raw)
                    if match and int(match[1]) == target:
                        require(match[3] == " | ".join(batch), "acknowledgement batch text mismatch")
                        ack = (int(match[2]), ack_raw)
                if ack and integer(state["frame"]) > ack[0] and integer(state["endFrames"]) > integer(before["endFrames"]):
                    if timed:
                        self.check_timed_trigger_state(state)
                    elif expected:
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
        require(committed < until, "command acknowledgement/commit deadline exceeded")
        if trace is not None:
            trace.phase("command", committed)
        if not cleanup:
            self.bounds(committed)
        self.last_commit = committed
        self.pending_command = False
        self.capture_sequence = integer(settled["captureSeq"])
        self.state = settled
        if timed:
            self.timed_trigger["ackFrame"] = ack[0]
        elif lines == ["trigger r 0"] and getattr(self, "timed_trigger", None) is not None:
            # Numeric zero does not cancel xr-sim's hold timer. Retain its
            # owner record even if this ordinary emergency release commits.
            self.timed_trigger["numericRelease"] = True
        self.save_owner(False)
        self.artifacts.event({"type": "command_commit", "requestedNs": written, "writtenNs": published, "committedNs": committed,
                              "ack": ack[1], "state": sim_summary(settled), "cleanup": cleanup}, cleanup=cleanup)
        self.remember_controls(lines)
        if neutral_approach_ack:
            # Pending ownership is now settled within its unchanged deadline.
            # Existing neutral observation discards late/stale responses and
            # reacquires current native evidence before any following command.
            self.pump()
        return settled

    def remember_controls(self, lines):
        for line in lines:
            if line.startswith("grip l "):
                self.held_left = float(line.split()[-1]) > 0
            if line.startswith("trigger r "):
                self.held_trigger = line == TIMED_TRIGGER_COMMAND or float(line.split()[-1]) > 0
            if line.startswith("grip r "):
                self.held_right = float(line.split()[-1]) > 0
            if line.startswith("btn "):
                _, button, action = line.split()
                if action == "down":
                    self.held_buttons.add(button)
                else:
                    self.held_buttons.discard(button)

    @staticmethod
    def timed_trigger_baseline_matches(controls, retained):
        return (isinstance(retained, dict) and set(retained) <= {"gripL", "gripR"}
                and all(type(value) is int and value == 1 for value in retained.values())
                and controls.get("trigR") == 0
                and all(controls.get(key) == value for key, value in retained.items())
                and all(all(part == 0 for part in value) if isinstance(value, list) else value == 0
                        for key, value in controls.items() if key not in retained))

    def check_timed_trigger_baseline(self, controls, retained):
        require(self.timed_trigger_baseline_matches(controls, retained),
                "timed trigger requires exact neutral controls except authorized retained full grips")
        require(not (self.pending_command or self.unsafe_owner or self.held_trigger or self.held_buttons)
                and self.held_left == ("gripL" in retained) and self.held_right == ("gripR" in retained),
                "runtime trigger retained grips do not match settled owned controls")

    def check_timed_trigger_state(self, state):
        timer = self.timed_trigger
        require(integer(state["captureSeq"]) == self.capture_sequence
                and all(state[k] == v for k, v in timer["basis"].items()),
                "runtime trigger observation changed pose/capture")
        controls = dict(state["controls"])
        require(controls.get("trigR") in (0, 1), "runtime trigger control differs")
        controls["trigR"] = 0
        require(controls == timer["neutralControls"], "runtime trigger observation changed other controls")
        retained = timer.get("retainedGrips", {})
        require(self.held_left == ("gripL" in retained) and self.held_right == ("gripR" in retained)
                and not self.held_buttons, "runtime trigger retained grip ownership changed")

    def observe_timed_trigger_neutral(self):
        """Observe natural expiry, without a zero command masking timer custody.

        State snapshots do not expose the timer or every high sample. Exact
        neutral controls in a newer committed frame prove settled input on the
        pinned runtime only when no intervening write could have forced zero.
        Native Fire delivery and physical pulse duration remain separate facts.
        """
        timer = self.timed_trigger
        require(timer is not None and timer["ackFrame"] is not None
                and timer["numericRelease"] is False and not self.pending_command
                and not self.unsafe_owner, "runtime trigger expiry is unsettled")
        for _ in range(1000):
            self.bounds(time.perf_counter_ns())
            state = read_until(self.stage_deadline, self.read_state)
            self.ownership(state)
            observed = time.perf_counter_ns()
            self.bounds(observed)
            self.check_timed_trigger_state(state)
            if (state["controls"]["trigR"] == 0
                    # Parsing can race the frame increment after pending input
                    # was committed. ACK+1 may still be the old neutral frame.
                    and integer(state["frame"]) >= timer["ackFrame"] + 2
                    and integer(state["frame"]) > timer["beforeFrame"]
                    and integer(state["endFrames"]) > timer["beforeEndFrames"]):
                proof = dict(backend="runtime_timed", command=timer["command"],
                    runtimeSha256=timer["runtimeSha256"], configuredMilliseconds=50,
                    publicationStartedNs=timer["publicationStartedNs"],
                    pressStartedNs=timer["pressStartedNs"], pressDeadlineNs=timer["pressDeadlineNs"],
                    pressCommittedNs=timer["pressCommittedNs"], driverDeadlineNs=timer["driverDeadlineNs"],
                    observationDeadlineNs=self.stage_deadline, ackFrame=timer["ackFrame"],
                    beforeFrame=timer["beforeFrame"], beforeEndFrames=timer["beforeEndFrames"],
                    neutralObservedNs=observed, neutralState=sim_summary(state),
                    expectedNeutralControls=timer["neutralControls"],
                    physicalDurationNs=None, nativeDeliveryProved=False)
                self.state = state
                self.held_trigger = False
                self.timed_trigger = None
                try:
                    self.save_owner(False)
                except BaseException:
                    self.timed_trigger = timer
                    self.held_trigger = True
                    raise
                self.artifacts.event(dict(type="timed_trigger_neutral_observed", **proof))
                return proof
            time.sleep(.001)
        raise REFUSAL("runtime trigger neutral observation iteration bound")

    def single_trigger_pulse(self, pulse_ns=100_000_000, source_deadline=None, *, retained_grips=None, expected=None):
        """One shot; explicitly retained grips require the pinned runtime timer.

        The default neutral route keeps its existing numeric fallback. A caller
        retaining grips supplies its typed firing target and native admission;
        this transport preserves exactly those already-owned controls.
        """
        require(pulse_ns == 100_000_000 and not getattr(self, "_single_trigger_used", False)
                and getattr(self, "timed_trigger", None) is None,
                "single trigger attempt already used or unsupported original pulse bound")
        self._single_trigger_used = True  # refusal cannot grant another attempt
        require(not (self.pending_command or self.unsafe_owner), "single trigger ownership is unsettled")
        require(expected is None if retained_grips is None else
                getattr(self, "timed_trigger_supported", False) and expected is not None,
                "retained-grip shot requires the pinned runtime timer and typed target")
        if retained_grips is not None:
            self.check_timed_trigger_baseline(self.state["controls"], retained_grips)
        press_started, driver_deadline = time.perf_counter_ns(), self.deadline
        press_deadline = self.stage_deadline = min(driver_deadline, press_started + pulse_ns)
        self._timed_trigger_arm = getattr(self, "timed_trigger_supported", False)
        self._timed_trigger_retained_grips = dict(retained_grips) if retained_grips is not None else None
        try:
            options = {"source_deadline": source_deadline} if source_deadline is not None else {}
            if expected is not None:
                options["expected"] = expected
            self.send(["trigger r 1"], **options)
        finally:
            self._timed_trigger_arm = False
            self._timed_trigger_retained_grips = None
        press = self.last_write
        if getattr(self, "post_reload_phase", None) is not None:
            self.post_reload_phase = "release"
        if getattr(self, "timed_trigger_supported", False):
            # The original DOWN stage bounds publication and settled ACK, not
            # observation of release. Preserve the original separate 250ms UP
            # observation phase, anchored to settled DOWN without renewing any
            # source lease or global clock. Timer50ms is not physical duration.
            self.timed_trigger.update(pressStartedNs=press_started, pressDeadlineNs=press_deadline,
                pressCommittedNs=self.last_commit, driverDeadlineNs=driver_deadline)
            self.stage_deadline = min(driver_deadline, self.last_commit + 250_000_000)
            proof = self.observe_timed_trigger_neutral()
            return dict(pressWrittenNs=press, releaseWrittenNs=None,
                        releaseCommittedNs=proof["neutralObservedNs"], pulseTransport=proof)
        # Ordinary UP remains mandatory even after a late DOWN acknowledgement.
        self.stage_deadline = min(self.deadline, time.perf_counter_ns() + 250_000_000)
        self.send(["trigger r 0"])
        require(self.last_write - press <= pulse_ns,
                "single trigger pulse publication exceeded 100ms; released; no retry")
        return dict(pressWrittenNs=press, releaseWrittenNs=self.last_write,
                    releaseCommittedNs=self.last_commit)

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
            plan = planner.plan_supply_pickup(self.profiles, snapshot, evaluated, integer(snapshot["nonce"]), step,
                                               pickup_approach=getattr(self.args, "pickup_approach", False))
        else:
            plan = planner.plan(self.profiles, snapshot, name, evaluated, integer(snapshot["nonce"]), step,
                                held_cover_probe=getattr(self.args, "held_cover_probe", False),
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
        approach_deadline = None
        for target in plan["schedule"]:
            at = integer(target["atNs"])
            require(at > last_time and at <= maximum, "nonmonotonic or oversized plan")
            last_time = at
            # Hold each pose for the planner's tested period even when a previous
            # acknowledgement took longer. Never catch up by compressing dwell.
            due = start + at if self.last_commit is None else max(start + at, self.last_write + step)
            self.pause_until(due)
            if target["phase"] == "supply_press":
                # Bound actual publication/acknowledgement time, not just the
                # nominal plan. Never renew this deadline on later samples.
                require(approach_deadline is None and name == "SupplyPickup", "duplicate or invalid supply press")
                approach_deadline = time.perf_counter_ns() + integer(plan["supplyApproach"]["windowNs"])
                self.stage_deadline = min(self.stage_deadline, approach_deadline)
                self.plans[-1]["supplyApproachDeadlineNs"] = approach_deadline
            source_deadline = min(integer(plan["sourceValidUntilNs"]),
                                  integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)
            self.send(target["commands"], expected=target,
                      source_deadline=source_deadline if target is plan["schedule"][0] else acquisition_deadline,
                      final_release=name != "SupplyPickup" and target is plan["schedule"][-1])
            acquisition_deadline = None
            if approach_deadline is not None:
                require(time.perf_counter_ns() < approach_deadline, "actual supply reach exceeded original grip-approach window")
            if target["phase"] == "acquire" and plan["requiresPostAcquisitionReobservation"]:
                acquisition_deadline = self.observe_acquisition(row, plan, self.last_write)
            elif target["phase"].startswith("held_probe_"):
                acquisition_deadline = self.observe_held_cover(row, plan, target, snapshot)
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
        if plan.get("heldCoverProbe") is not None:
            self.plans[-1]["postAcquisitionHingeFromWrist"] = planner.cover_wrist_in_hinge(snapshot)
        self.artifacts.event({"type": "feed_acquisition_observed", "stage": plan["stage"],
                              "nonce": snapshot["nonce"], "claim": claim,
                              "commandWrittenNs": command_written_ns, "nativeReceiptProof": False})
        self.monitor = True
        return min(integer(snapshot["valid_until_ns"]), integer(claim["deadlineNs"]),
                   integer(snapshot["input_evidence"]["observedNs"]) + SNAPSHOT_PERIOD)

    def observe_held_cover(self, row, plan, target, before):
        snapshot, raw, evaluated = self.fresh()
        self.accept_snapshot(snapshot, raw, evaluated)
        record = self.plans[-1]
        observations = record.setdefault("heldCoverObservations", [])
        observed = planner.held_cover_observation(
            self.profiles, row, snapshot, time.perf_counter_ns(), plan, target, self.last_write,
            record["postAcquisitionClaim"], record["postAcquisitionHingeFromWrist"],
            before["feedObservation"], observations[-1] if observations else None)
        observations.append(observed)
        self.artifacts.event(dict(type="held_cover_probe_observed", **observed))
        self.monitor = True
        return min(integer(snapshot["valid_until_ns"]), integer(observed["claim"]["deadlineNs"]),
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

    def post_reload_shot(self, result_path):
        # Reuse the exact preceding completed closing artifact, then consume
        # one run-local attempt even if admission or delivery fails.
        path = child(self.run, result_path)
        previous, _ = read_json(self.run / "live-driver-owner.json", 8192)
        require(same_path(previous["artifactDirectory"], path.parent),
                "post-reload shot must immediately follow the owned closing stimulus")
        closed, raw = read_json(path)
        require(closed["schema"] == "bc2vr.reload-stimulus-result.v1"
                and closed["status"] == "ordinary_stimulus_completed"
                and closed["stimulusCompleted"] is True and closed["boundaryChecksPassed"] is True
                and closed["observerStopped"] is True and closed["observationIncomplete"] is False
                and closed.get("error") is None and closed.get("observationFailure") is None
                and closed.get("diagnosticFailure") is None
                and closed["leftGripHeld"] is False and closed["pendingCommand"] is False,
                "post-reload requires a completed released bc2vr.reload-stimulus-result.v1 "
                "from drive_bc2_reload_stimulus.py; this driver's --stage result is not accepted")
        plan, plan_raw = post_reload_terminal_plan(path, closed)
        if plan["family"] == "fixed":
            closure = fixed_post_reload_closure(plan, closed)
        else:
            require(closed["boundaries"]["feed_released"]["receiptCounterAdvanced"] is True,
                    "original CloseCover boundary missing")
            closure = dict(mapping=plan["mapping"], lifetime=closed["boundaries"]["feed_released"]["lifetime"])
        with (self.run / "post-reload-shot-attempt.json").open("xb") as stream:
            stream.write(json_bytes(dict(host=self.host, closeResult=str(path), closeSha256=digest(raw),
                                         artifactDirectory=str(self.artifacts.directory))))
        self.artifacts.save("close-cover-binding.json", dict(path=str(path), sha256=digest(raw),
                                                           planSha256=digest(plan_raw), closure=closure))
        self.post_reload_closure = closure
        before, _, now = self.fresh()
        post_reload_ready(self.profiles, before, now, closure, neutral=False)
        self.reference = before
        require(not self.pending_command and not self.unsafe_owner
                and not (self.held_left or self.held_right or self.held_trigger or self.held_buttons),
                "post-reload input ownership not settled")
        # Ordinary release can retire a retained mechanism claim. No pose,
        # claim, ammunition, or hand-angle change is fabricated.
        self.send(["grip l 0", "trigger r 0"])
        neutral_written = self.last_write
        ready, _, now = self.fresh()
        post_reload_ready(self.profiles, ready, now, closure, neutral=True)
        require(integer(ready["input_evidence"]["observedNs"]) >= neutral_written
                and integer(ready["shot_input_generation"]) > integer(before["shot_input_generation"]),
                "neutral rearm needs a new original input packet")
        self.post_reload_closure = closure
        self.post_reload_phase = "press"
        pulse = self.single_trigger_pulse(source_deadline=self.snapshot_source_deadline(ready))
        committed = pulse["releaseCommittedNs"]
        self.post_reload_phase = "observe"
        self.stage_deadline = min(self.deadline, committed + 600_000_000)
        self.pause_until(committed + 100_000_000)
        after, _, now = self.fresh()
        post_reload_ready(self.profiles, after, now, closure, neutral=True, after_shot=True)
        require(integer(after["input_evidence"]["observedNs"]) >= committed
                and integer(after["shot_input_generation"]) > integer(ready["shot_input_generation"]),
                "post-shot release not observed in a new original packet")
        schema = "bc2vr.fixed-post-reload-pulse.v1" if closure.get("family") == "fixed" else "bc2vr.feed-post-reload-pulse.v1"
        boundary = dict(schema=schema, status="native_shot_observation_pending",
                        owner=ready["owner"], closure=closure, host=self.host,
                        sourceSha256=self.manifest["source_sha256"], closeResultSha256=digest(raw),
                        neutralWrittenNs=neutral_written, **pulse, observationEndNs=after["observed_ns"],
                        nativeAcceptance=False, headsetAcceptance=False, nativeShotObserved=False)
        if closure.get("family") == "fixed":
            boundary.update(beforeCounts=ready["fixedContainer"]["view"]["snapshot"]["counts"],
                            afterCounts=after["fixedContainer"]["view"]["snapshot"]["counts"])
        self.artifacts.save("post-reload-shot.json", boundary)
        return boundary

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

    def retire_timed_trigger_during_cleanup(self, state, ack_raw, ack_frame, until):
        """Retain pre-zero natural expiry without reviving the failed action.

        Only the exact pending timed batch can supply this cleanup observation.
        The original press/ACK failure stays failed; no source or action clock
        is renewed, and a numeric zero can never supply natural-expiry proof.
        """
        timer = getattr(self, "timed_trigger", None)
        if not (timer is not None and getattr(self, "timed_trigger_supported", False)
                and getattr(self, "_single_trigger_used", False)
                and not (self.pending_command or self.unsafe_owner)
                and timer.get("command") == TIMED_TRIGGER_COMMAND
                and timer.get("runtimeSha256") == TIMED_TRIGGER_RUNTIME_SHA256
                and timer.get("configuredMilliseconds") == 50
                and timer.get("numericRelease") is False
                and timer.get("sequence") == self.sequence
                and timer.get("sentinel") == self.sentinel
                and timer.get("commandSha256") == self.command_hash
                and self.pending_batch == [TIMED_TRIGGER_COMMAND, self.sentinel]
                and timer.get("ackFrame") == ack_frame
                and timer.get("beforeFrame") == integer(self.pending_before["frame"])
                and timer.get("beforeEndFrames") == integer(self.pending_before["endFrames"])
                and timer.get("neutralControls") == self.pending_before["controls"]
                and timer.get("basis") == {key: self.pending_before[key]
                    for key in ("head", "handL", "handR")}
                and integer(self.pending_before["cmdSeq"]) + 2 == self.sequence
                and integer(state["frame"]) >= ack_frame + 2
                and integer(state["frame"]) > timer["beforeFrame"]
                and integer(state["endFrames"]) > timer["beforeEndFrames"]
                and integer(state["captureSeq"]) == self.capture_sequence
                and all(state[key] == value for key, value in timer["basis"].items())
                and state["controls"] == timer["neutralControls"]
                and self.timed_trigger_baseline_matches(state["controls"], timer.get("retainedGrips", {}))
                and self.held_left == ("gripL" in timer.get("retainedGrips", {}))
                and self.held_right == ("gripR" in timer.get("retainedGrips", {}))
                and not self.held_buttons):
            return
        # The prior save retained timer custody. Rejoin that exact current
        # owner record before clearing it, within the existing cleanup budget.
        owner = read_until(until, read_bytes, self.run / "live-driver-owner.json", 8192)
        if owner != self.owner_bytes(False):
            self.unsafe_owner = True
            raise REFUSAL("runtime timer cleanup owner record replaced")
        read_until(until, self.ownership, state)
        observed = time.perf_counter_ns()
        require(0 < timer["publicationStartedNs"] <= self.last_write <= observed < until,
                "runtime timer cleanup observation exceeded original bound")
        proof = dict(type="timed_trigger_cleanup_neutral_observed", host=self.host,
            timer=dict(timer), ack=ack_raw, neutralState=sim_summary(state),
            neutralObservedNs=observed, cleanupDeadlineNs=until,
            originalActionRemainsRefused=True, commandAuthority=False,
            nativeAcceptance=False, headsetAcceptance=False)
        self.artifacts.event(proof, cleanup=True)
        require_read_time(until)
        self.timed_trigger, self.held_trigger = None, False
        try:
            self.save_owner(False)
        except BaseException:
            self.timed_trigger, self.held_trigger = timer, True
            raise
        self.timed_trigger_cleanup = proof
        self.state = state

    def settle_pending_for_release(self):
        """Observe only our exact pending batch before deciding release ownership."""
        until = min(self.hard_deadline - 150_000_000, time.perf_counter_ns() + 100_000_000)
        for _ in range(100):
            if time.perf_counter_ns() >= until:
                return
            state = read_until(until, self.read_state)
            require(digest(read_until(until, read_bytes, self.command_path, 4096)) == self.command_hash,
                    "pending command replaced; ordinary release ownership lost")
            require(integer(state["cmdSeq"]) in (self.sequence - len(self.pending_batch), self.sequence),
                    "pending command sequence changed")
            if integer(state["cmdSeq"]) == self.sequence and self.ack_path.exists():
                self.ownership(state)
                raw = read_until(until, read_bytes, self.ack_path, 4096).decode("utf-8")
                match = re.fullmatch(r"seq=(\d+) frame=(\d+) error=-\r?\nlast=([^\r\n]*)\r?\n", raw)
                if match and int(match[1]) == self.sequence and match[3] == " | ".join(self.pending_batch):
                    if integer(state["frame"]) > int(match[2]) and integer(state["endFrames"]) > integer(self.pending_before["endFrames"]):
                        require(time.perf_counter_ns() < until, "original acknowledgement/read deadline exceeded")
                        if getattr(self, "timed_trigger", None) is not None:
                            owner = read_until(until, read_bytes, self.run / "live-driver-owner.json", 8192)
                            if owner != self.owner_bytes(True):
                                self.unsafe_owner = True
                                raise REFUSAL("pending runtime timer owner record replaced")
                        self.pending_command = False
                        if getattr(self, "timed_trigger", None) is not None:
                            if TIMED_TRIGGER_COMMAND in self.pending_batch:
                                require(self.timed_trigger.get("ackFrame") in (None, int(match[2])),
                                        "runtime timer acknowledgement frame changed")
                                self.timed_trigger["ackFrame"] = int(match[2])
                            if "trigger r 0" in self.pending_batch:
                                self.timed_trigger["numericRelease"] = True
                        self.remember_controls(self.pending_batch)
                        self.save_owner(False)
                        self.artifacts.event({"type": "pending_command_settled_before_release", "ack": raw,
                                              "state": sim_summary(state)}, cleanup=True)
                        self.retire_timed_trigger_during_cleanup(state, raw, int(match[2]), until)
                        return
            time.sleep(.001)

    def release_after_failure(self):
        trace = getattr(self, "_receive_timing", None)
        if trace is not None:
            trace.freeze(self.pending_command, self.sequence)
        self.monitor = False
        if self.pending_command and not self.unsafe_owner:
            self.settle_pending_for_release()
        completed_timer = getattr(self, "timed_trigger_cleanup", None)
        if (completed_timer is not None and self.sequence == completed_timer["timer"]["sequence"]
                and not (self.pending_command or self.unsafe_owner or self.held_left or self.held_right
                         or self.held_trigger or self.held_buttons or getattr(self, "timed_trigger", None))):
            return "runtime timer natural neutral observed during cleanup; original action remains refused"
        timer = getattr(self, "timed_trigger", None)
        if timer is not None and not self.pending_command and not self.unsafe_owner:
            if timer["numericRelease"] and not self.held_trigger:
                return "ordinary zero already acknowledged; runtime timer custody retained; future input refused"
            # Timer custody survives an archive/owner-save failure after ACK,
            # even if remember_controls was never reached.
            self.held_trigger = True
        if self.unsafe_owner or self.pending_command or not (self.held_left or self.held_trigger or self.held_right or self.held_buttons):
            return "not attempted: no settled owned held controller"
        if time.perf_counter_ns() + 250_000_000 >= self.hard_deadline:
            return "not attempted: bounded driver deadline"
        lines = (["grip l 0"] if self.held_left else []) + (["trigger r 0"] if self.held_trigger else [])
        lines += (["grip r 0"] if self.held_right else []) + ["btn " + key + " up" for key in sorted(self.held_buttons)]
        self.send(lines, cleanup=True)
        if getattr(self, "timed_trigger", None) is not None:
            return "ordinary zero acknowledged; runtime timer custody retained; future input refused"
        return "ordinary owned controller release acknowledged"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="exact live runner output directory")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--stage", choices=[*planner.STAGES, "SupplyPickup"])
    mode.add_argument("--capture", help="one explicit named composition within runner budget")
    mode.add_argument("--menu-batches", type=Path, help="bounded JSON batches inside exact run directory")
    mode.add_argument("--post-reload-shot", type=Path,
                      help="preceding completed result.json from drive_bc2_reload_stimulus.py "
                           "(feed CloseCover or supported fixed reload); this driver's --stage result is not accepted; "
                           "one pulse, native proof pending")
    mode.add_argument("--audit-post-reload-shot", type=Path, help="saved post-reload-shot.json; read-only drained native audit")
    parser.add_argument("--root-authorized-ordinary-input", action="store_true")
    parser.add_argument("--profiles", type=Path, help="production profile export copied inside exact run directory")
    parser.add_argument("--pickup-first", action="store_true", help="continuous fresh chest pickup then held AttachContainer")
    parser.add_argument("--pickup-approach", action="store_true",
                        help="SupplyPickup or --pickup-first only: real outside neutral/press then held reach")
    parser.add_argument("--held-cover-probe", action="store_true",
                        help="OpenCover only: held 3cm/20deg CONTROL/+EPS/-EPS/RESTORE with joint custody/lid observations")
    parser.add_argument("--step-ms", type=int, default=0, help="0 chooses min(60ms,maxInputGapNs/2) from production policy")
    args = parser.parse_args(argv)
    driver = None
    result = {"schema": "bc2vr.live-feed-driver.v1", "status": "refused", "nativeAcceptance": False,
              "headsetAcceptance": False, "evidence": "actual simulator commands; native receipts require separate review"}
    try:
        require(not args.pickup_approach or args.stage == "SupplyPickup" or
                (args.stage == "AttachContainer" and args.pickup_first),
                "--pickup-approach requires SupplyPickup or AttachContainer --pickup-first")
        require(not (args.stage or args.post_reload_shot) or args.profiles is not None, "stage/shot requires --profiles inside run")
        require(not args.post_reload_shot or args.root_authorized_ordinary_input, "root ordinary-input authorization required")
        if args.audit_post_reload_shot:
            run = local_path(args.run)
            boundary_path = child(run, args.audit_post_reload_shot)
            boundary, _ = read_json(boundary_path)
            manifest, _ = read_json(run / "run-manifest.json")
            link, _ = read_json(run / "session-link.json")
            attempt, _ = read_json(run / "post-reload-shot-attempt.json")
            require(boundary["host"] == link["host"] == attempt["host"]
                    and boundary["sourceSha256"] == manifest["source_sha256"]
                    and boundary["closeResultSha256"] == attempt["closeSha256"]
                    and same_path(boundary_path.parent, attempt["artifactDirectory"]),
                    "post-reload boundary belongs to another run/attempt")
            retirement, _ = read_json(run / "native-retirement.json")
            require(retirement["hooks_disabled"] is True and retirement["reload_flow_drained"] is True
                    and retirement["pre_activation_refusal"] is False,
                    "native shot observation absent: await ordinary native retirement")
            report_path = check_pin(retirement["native_report"], 160 * 1024 * 1024)
            require(same_path(report_path, local_path(link["folder"]) / "native-trace.json"),
                    "retired native report belongs to another session")
            native, _ = read_json(report_path, 160 * 1024 * 1024)
            result.update(audit_post_reload_shot(native["gameplay"]["reload_flow"], boundary))
            print(json.dumps(result, separators=(",", ":"), allow_nan=False))
            return 0
        require(not args.pickup_first or args.stage == "AttachContainer", "--pickup-first requires AttachContainer")
        require(not args.held_cover_probe or args.stage == "OpenCover", "--held-cover-probe requires OpenCover")
        require(0 <= args.step_ms <= 100, "invalid command period")
        # Keep a partially initialized instance available for handle cleanup and
        # a refusal artifact if admission fails after creating the output folder.
        driver = Driver.__new__(Driver)
        driver.__init__(args)
        if args.post_reload_shot:
            result["postReloadShot"] = driver.post_reload_shot(args.post_reload_shot)
        elif args.stage:
            pickup = driver.stage("SupplyPickup") if args.pickup_first else None
            # While awaiting the fresh post-pickup source, the unchanged held
            # pose continues producing genuine XR packets each host frame.
            # Keep command timestamps intact; native admission owns input leases.
            driver.stage(args.stage, pickup)
        elif args.capture:
            driver.capture(args.capture)
        else:
            driver.menu(args.menu_batches)
        result["status"] = "native_shot_observation_pending" if args.post_reload_shot else "commands_acknowledged"
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
                               "triggerHeld": driver.held_trigger,
                               "timedTriggerPending": getattr(driver, "timed_trigger", None) is not None,
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
