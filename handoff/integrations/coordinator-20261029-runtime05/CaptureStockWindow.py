"""One HWND-specific WGC frame. Root must admit storage and enforce a 12s watchdog.

This observes Windows capture output, not a raw game swapchain or physical display.
WGC may resize its internal GPU pool before our callback; saved bytes alone are capped.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import importlib
import json
from pathlib import Path
import re
import sys
import threading
import time

REPORTS = Path(r"G:\Unity\VRMMO\FrostbiteVR\reports")
LIB = REPORTS / "coordinator-20261029-runtime05/wgc-tool/lib"
PROVENANCE = LIB.parent / "provenance.json"
EXE = Path(r"G:\New folder (2)\Battlefield Bad Company 2\BFBC2Game.exe")
EXE_SHA = "3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258"
WINDOW_CLASS = "Battlefield: Bad Company 2"
MAX_WIDTH, MAX_HEIGHT = 1030, 797
MAX_RAW, MAX_PNG = 3283640, 4 * 1024 * 1024
CAPTURE_SECONDS, MAX_RECEIPT_AGE = 5, 90


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def read_json(path):
    require(path.stat().st_size <= 1024 * 1024, "Oversized receipt")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def integer(value):
    require(type(value) is int and value >= 0, "Expected nonnegative integer")
    return value


def rect(value):
    result = {key: value[key] for key in ("left", "top", "right", "bottom")}
    require(all(type(v) is int for v in result.values()), "Malformed rectangle")
    return result


def canonical(path, exists=True):
    resolved = Path(path).resolve(strict=exists)
    require(resolved.is_relative_to(REPORTS.resolve(strict=True)), "Path outside canonical reports")
    return resolved


def encode_bounded_bgra(buffer, width, height, np, cv2):
    """Pure image validation/encoding, independently usable with synthetic arrays."""
    require(type(width) is int and type(height) is int, "Noninteger frame dimensions")
    require(0 < width <= MAX_WIDTH and 0 < height <= MAX_HEIGHT, "Frame exceeds admitted dimensions")
    require(width * height * 4 <= MAX_RAW, "Frame exceeds admitted raw bytes")
    require(buffer.shape == (height, width, 4) and buffer.dtype == np.uint8, "Expected BGRA8 array")
    pixels = np.ascontiguousarray(buffer)
    raw = pixels.tobytes()
    require(len(raw) == width * height * 4, "Unexpected packed frame length")
    okay, encoded = cv2.imencode(".png", pixels[:, :, :3])
    require(okay and 0 < encoded.nbytes <= MAX_PNG, "PNG encoding failed or exceeds 4MiB")
    png = encoded.tobytes()
    channels = pixels.reshape(-1, 4)
    stats = {
        "pixels": width * height, "raw_format": "BGRA8 tightly packed", "png_format": "RGB8 (alpha omitted)",
        "bgra_min": channels.min(axis=0).tolist(), "bgra_max": channels.max(axis=0).tolist(),
        "bgra_sum": channels.sum(axis=0, dtype=np.uint64).tolist(),
        "black_rgb": int(np.count_nonzero(np.all(channels[:, :3] == 0, axis=1))),
        "near_black_rgb_le4": int(np.count_nonzero(np.all(channels[:, :3] <= 4, axis=1))),
        "opaque_alpha": int(np.count_nonzero(channels[:, 3] == 255)),
    }
    return raw, png, stats


class NativeIdentity:
    """Read-only process queries and HWND observation; retains the original handle."""
    def __init__(self, identity, window):
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.u = C.WinDLL("user32", use_last_error=True)
        signatures = [
            (self.k, "OpenProcess", [W.DWORD, W.BOOL, W.DWORD], W.HANDLE),
            (self.k, "CloseHandle", [W.HANDLE], W.BOOL),
            (self.k, "GetProcessId", [W.HANDLE], W.DWORD),
            (self.k, "GetProcessTimes", [W.HANDLE] + [C.POINTER(W.FILETIME)] * 4, W.BOOL),
            (self.k, "QueryFullProcessImageNameW", [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)], W.BOOL),
            (self.k, "WaitForSingleObject", [W.HANDLE, W.DWORD], W.DWORD),
            (self.k, "GetTickCount64", [], C.c_ulonglong),
            (self.u, "IsWindow", [W.HWND], W.BOOL),
            (self.u, "IsWindowVisible", [W.HWND], W.BOOL),
            (self.u, "IsIconic", [W.HWND], W.BOOL),
            (self.u, "GetWindowThreadProcessId", [W.HWND, C.POINTER(W.DWORD)], W.DWORD),
            (self.u, "GetClassNameW", [W.HWND, W.LPWSTR, C.c_int], C.c_int),
            (self.u, "GetWindowRect", [W.HWND, C.POINTER(W.RECT)], W.BOOL),
            (self.u, "GetClientRect", [W.HWND, C.POINTER(W.RECT)], W.BOOL),
            (self.u, "ClientToScreen", [W.HWND, C.POINTER(W.POINT)], W.BOOL),
            (self.u, "SetThreadDpiAwarenessContext", [C.c_void_p], C.c_void_p),
        ]
        for dll, name, args, result in signatures:
            function = getattr(dll, name)
            function.argtypes, function.restype = args, result
        self.identity, self.window = identity, window
        self.pid, self.hwnd = integer(identity["pid"]), integer(window["hwnd"])
        require(self.pid > 0 and self.hwnd > 0, "Invalid process/window identity")
        self.handle = self.k.OpenProcess(0x1000 | 0x100000, False, self.pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())

    def observe(self):
        previous = self.u.SetThreadDpiAwarenessContext(C.c_void_p(-4))
        if not previous:
            raise C.WinError(C.get_last_error())
        original_error = None
        try:
            return self._observe_physical()
        except BaseException as error:
            original_error = error
            raise
        finally:
            if not self.u.SetThreadDpiAwarenessContext(previous):
                restore_error = C.WinError(C.get_last_error())
                if original_error is not None:
                    original_error.add_note("DPI context restoration also failed: " + str(restore_error))
                else:
                    raise restore_error

    def _observe_physical(self):
        require(self.k.WaitForSingleObject(self.handle, 0) == 258, "Original process is no longer running")
        require(self.k.GetProcessId(self.handle) == self.pid, "Process handle identity changed")
        times = [W.FILETIME() for _ in range(4)]
        require(self.k.GetProcessTimes(self.handle, *(C.byref(v) for v in times)), "GetProcessTimes failed")
        birth = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
        require(str(birth) == str(self.identity["creation_filetime"]), "Process birth differs")
        name, size = C.create_unicode_buffer(32768), W.DWORD(32768)
        require(self.k.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(size)), "Process path query failed")
        require(Path(name.value).resolve(strict=True) == EXE.resolve(strict=True), "Live executable path differs")
        digest = sha(EXE)
        require(digest == EXE_SHA, "Live executable file hash differs")
        owner = W.DWORD()
        thread = self.u.GetWindowThreadProcessId(self.hwnd, C.byref(owner))
        require(self.u.IsWindow(self.hwnd) and thread and owner.value == self.pid, "HWND owner differs")
        klass = C.create_unicode_buffer(256)
        require(self.u.GetClassNameW(self.hwnd, klass, 256) > 0 and klass.value == WINDOW_CLASS, "HWND class differs")
        require(self.u.IsWindowVisible(self.hwnd) and not self.u.IsIconic(self.hwnd), "Window hidden or minimized")
        bounds = {}
        for key, method in (("rect", self.u.GetWindowRect), ("client_rect", self.u.GetClientRect)):
            item = W.RECT()
            require(method(self.hwnd, C.byref(item)), "Window rectangle query failed")
            bounds[key] = {k: getattr(item, k) for k in ("left", "top", "right", "bottom")}
            require(bounds[key] == rect(self.window[key]), "Window geometry changed since observation")
        outer, client = bounds["rect"], bounds["client_rect"]
        require((outer["right"] - outer["left"], outer["bottom"] - outer["top"]) == (1030, 797), "Unexpected outer size")
        require((client["right"] - client["left"], client["bottom"] - client["top"]) == (1024, 768), "Unexpected client size")
        point = W.POINT(0, 0)
        require(self.u.ClientToScreen(self.hwnd, C.byref(point)), "Client origin query failed")
        require(self.k.WaitForSingleObject(self.handle, 0) == 258, "Process exited during observation")
        return dict(pid=self.pid, creation_filetime=str(birth), path=name.value, sha256=digest,
                    hwnd=self.hwnd, window_thread=thread, window_class=klass.value, visible=True,
                    minimized=False, coordinate_space="physical_pixels_per_monitor_v2",
                    client_origin={"x": point.x, "y": point.y},
                    observed_tick_ms=self.k.GetTickCount64(), **bounds)

    def close(self):
        if self.handle:
            require(self.k.CloseHandle(self.handle), "CloseHandle failed")
            self.handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene-directory", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--storage-receipt", required=True)
    parser.add_argument("--observation", default="window-before-wgc.json")
    args = parser.parse_args()
    scene = canonical(args.scene_directory)
    output = canonical(args.output, exists=False)
    require(scene.is_dir() and output.parent == scene and not output.exists(), "Output must be a fresh direct scene child")
    require(re.fullmatch(r"window-[A-Za-z0-9_-]+\.json", args.observation), "Invalid observation basename")
    storage_path = canonical(args.storage_receipt)
    require(storage_path.parent == scene, "Storage receipt must belong to this scene")
    admission = read_json(storage_path)
    age = (datetime.now(timezone.utc) - datetime.fromisoformat(admission["checked_utc"].replace("Z", "+00:00"))).total_seconds()
    require(admission.get("schema") == "fvr.storage-admission.v1" and admission.get("operation") == "Capture", "Wrong storage admission")
    require(0 <= age <= MAX_RECEIPT_AGE and canonical(admission["output_path"], False) == output, "Stale or differently targeted storage admission")
    require(integer(admission["raw_capture_bytes"]) >= 8 * 1024 * 1024 and integer(admission["planned_bytes"]) >= 16 * 1024 * 1024, "Insufficient storage admission")
    output.mkdir()
    report = dict(schema="fvr.runtime05.wgc-one-frame.v1", success=False, errors=[], observations={},
                  native_attached=False, input_sent=False, window_actions=False, physical_visibility_proven=False,
                  raw_swapchain_proven=False, native_acceptance=False, capture_deadline_seconds=CAPTURE_SECONDS,
                  outer_watchdog_required_seconds=12, saved_raw_limit=MAX_RAW, saved_png_limit=MAX_PNG,
                  gpu_allocation_limit_enforced=False, gpu_resize_before_callback_possible=True,
                  storage_receipt=str(storage_path), storage_receipt_sha256=sha(storage_path),
                  callbacks=0, stop_requested=False, capture_thread_joined=False)
    identity_handle = control = None
    images = {}
    ready, claimed, journal_lock = threading.Event(), threading.Lock(), threading.Lock()
    journal = (output / "progress.jsonl").open("x", encoding="utf-8")

    def event(phase, error=None):
        entry = {"phase": phase, "utc": datetime.now(timezone.utc).isoformat()}
        if error is not None:
            entry["error"] = error
        with journal_lock:
            journal.write(json.dumps(entry) + "\n")
            journal.flush()

    def failure(phase, error):
        entry = {"phase": phase, "type": type(error).__name__, "message": str(error)[:4096]}
        if getattr(error, "__notes__", None):
            entry["additional_failures"] = list(error.__notes__)
        report["errors"].append(entry)
        event(phase, entry)

    try:
        identity = read_json(scene / "process.json")
        observation_path = canonical(scene / args.observation)
        observed = read_json(observation_path)
        require(identity.get("root_launched") is True and identity.get("stock_launch") is True and identity.get("mod_injected") is False, "Expected root-launched stock process")
        require(Path(identity["path"]).resolve(strict=True) == EXE.resolve(strict=True) and identity["sha256"].lower() == EXE_SHA, "Process receipt executable differs")
        require(observed.get("schema") == "fvr.runtime02.window-observation.v1" and observed.get("observation_only") is True, "Wrong window observation")
        require(observed["pid"] == identity["pid"] and str(observed["creation_filetime"]) == str(identity["creation_filetime"]), "Observation process identity differs")
        window = observed["window"]
        require(window["pid"] == identity["pid"] and str(window["creation_filetime"]) == str(identity["creation_filetime"]), "Nested window identity differs")
        identity_handle = NativeIdentity(identity, window)
        require(0 <= identity_handle.k.GetTickCount64() - integer(observed["observed_tick_ms"]) <= MAX_RECEIPT_AGE * 1000, "Window observation is stale")
        report["observation_receipt"] = {"path": str(observation_path), "sha256": sha(observation_path)}
        report["provision_receipt"] = {"path": str(PROVENANCE), "sha256": sha(PROVENANCE)}
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(LIB.resolve(strict=True)))
        np, cv2, wgc = [importlib.import_module(name) for name in ("numpy", "cv2", "windows_capture")]
        for module in (np, cv2, wgc):
            require(Path(module.__file__).resolve(strict=True).is_relative_to(LIB.resolve(strict=True)), "Dependency loaded outside isolated library")
        report["observations"]["before"] = identity_handle.observe()
        capture = wgc.WindowsCapture(window_hwnd=identity_handle.hwnd, cursor_capture=False, draw_border=None)

        @capture.event
        def on_frame_arrived(frame, capture_control):
            with claimed:
                report["callbacks"] += 1
                first = report["callbacks"] == 1
            if not first:
                capture_control.stop()
                return
            try:
                event("callback_entered")
                report["observations"]["callback"] = identity_handle.observe()
                raw, png, stats = encode_bounded_bgra(frame.frame_buffer, frame.width, frame.height, np, cv2)
                report["frame"] = dict(width=frame.width, height=frame.height, timespan=int(frame.timespan), **stats)
                images.update(raw=raw, png=png)
            except Exception as error:
                failure("callback", error)
            finally:
                report["stop_requested"] = True
                try:
                    capture_control.stop()
                    event("callback_stop_requested")
                except Exception as error:
                    failure("callback_stop", error)
                ready.set()

        @capture.event
        def on_closed():
            failure("window_closed", RuntimeError("Capture item closed"))
            ready.set()

        event("capture_start_requested")
        started = time.monotonic()
        control = capture.start_free_threaded()
        completed = ready.wait(max(0, CAPTURE_SECONDS - (time.monotonic() - started)))
        require(completed and time.monotonic() - started <= CAPTURE_SECONDS, "No completed callback within five seconds")
    except Exception as error:
        failure("capture_or_preflight", error)
    finally:
        if control is not None:
            event("capture_stop_join_requested")
            report["stop_requested"] = True
            try:
                control.stop()
                report["capture_thread_joined"] = True
                event("capture_stop_join_completed")
            except Exception as error:
                failure("capture_stop_join", error)
        if identity_handle is not None:
            try:
                report["observations"]["after"] = identity_handle.observe()
            except Exception as error:
                failure("after_identity", error)
            try:
                identity_handle.close()
            except Exception as error:
                failure("process_handle_close", error)
        try:
            if not report["errors"]:
                require(report["callbacks"] == 1 and report["capture_thread_joined"] and set(images) == {"raw", "png"}, "Incomplete one-frame capture")
                report["files"] = []
                for key, name in (("raw", "one.raw"), ("png", "one.png")):
                    data = images[key]
                    with (output / name).open("xb") as stream:
                        require(stream.write(data) == len(data), "Short image write")
                    report["files"].append({"name": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
                report["success"] = True
        except Exception as error:
            failure("artifact_write", error)
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        event("result_write_requested")
        with (output / "result.json").open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2)
        journal.close()
    print(json.dumps({"success": report["success"], "result": str(output / "result.json")}))
    return 0 if report["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
