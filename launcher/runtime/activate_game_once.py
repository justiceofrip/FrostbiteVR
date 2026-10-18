"""One explicitly authorized BC2 focus request, with no generated keyboard/mouse input.

Uses documented AttachThreadInput/BringWindowToTop/SetForegroundWindow APIs.
Windows may still refuse the request; the receipt records that outcome, never
claims a bypass, and the runner must not attach the mod after a failed result.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import sys
import time

GAME_CLASS = "Battlefield: Bad Company 2"


def activate_once(api, hwnd, verify, report, pause=time.sleep):
    """Pure injectable sequencing: at most one focus burst; only passive samples after it."""
    report.update(activation_attempts=0, operations=[], attachments=[], detachments=[],
                  attachments_detached=True, os_keyboard_or_mouse_input_sent=False,
                  focus_retries=0, passed=False)
    target = verify()
    initial = api.foreground()
    report["before"] = {"target": target, "foreground": initial}
    if initial["hwnd"] == hwnd:
        report.update(passed=True, outcome="already_foreground", samples=[initial])
        return
    if target["hung"] or initial.get("hung"):
        raise RuntimeError("An involved window is unresponsive; no input queues were attached")
    caller = api.current_thread()
    api.ensure_message_queue()
    report["caller_thread"] = caller
    report["conditions"] = api.conditions(initial["thread"])
    attached = []
    try:
        # Create only current->foreground and current->target edges, deduplicated.
        for thread in dict.fromkeys((initial["thread"], target["thread"])):
            if not thread or thread == caller:
                continue
            result = api.attach(caller, thread, True)
            report["attachments"].append({"from": caller, "to": thread, **result})
            if not result["ok"]:
                raise RuntimeError("AttachThreadInput refused; no activation call was sent")
            attached.append(thread)
            report["attachments_detached"] = False
        verify()
        current = api.foreground()
        if current["hwnd"] not in (initial["hwnd"], hwnd):
            raise RuntimeError("Foreground changed during setup; cancelled instead of stealing it again")
        report["activation_attempts"] = 1
        if not target["visible"] or target["minimized"]:
            report["operations"].append(api.call("ShowWindowAsync", hwnd, 9))
        report["operations"].append(api.call("BringWindowToTop", hwnd))
        report["operations"].append(api.call("SetForegroundWindow", hwnd))
        report["foreground_after_request"] = api.foreground()
        # Focus and active-window state may only be changed for this exact BC2 HWND.
        report["operations"].append(api.call("SetActiveWindow", hwnd))
        report["operations"].append(api.call("SetFocus", hwnd))
    finally:
        failed = []
        for thread in reversed(attached):
            try:
                result = api.attach(caller, thread, False)
            except Exception as error:
                result = {"ok": False, "error": str(error)}
            report["detachments"].append({"from": caller, "to": thread, **result})
            if not result["ok"]:
                failed.append(thread)
        report["attachments_detached"] = not failed
        if failed:
            raise RuntimeError("Input-thread detachment was not verified; helper must exit")
    samples = []
    for index in range(4):
        if index:
            pause(.1)
        verify()
        samples.append(api.foreground())
    report["samples"] = samples
    stable = all(row["hwnd"] == hwnd for row in samples[-3:])
    report["passed"] = stable
    if stable:
        report["outcome"] = "activated_and_stable"
    elif any(row["hwnd"] == hwnd for row in samples) or report["foreground_after_request"]["hwnd"] == hwnd:
        report["outcome"] = "activated_then_lost_foreground"
    else:
        report["outcome"] = "foreground_request_not_observed"
    if not stable:
        raise RuntimeError(report["outcome"] + "; no second activation attempt was made")


class Win32:
    def __init__(self):
        self.u = c.WinDLL("user32", use_last_error=True)
        self.k = c.WinDLL("kernel32", use_last_error=True)
        self.u.GetForegroundWindow.restype = w.HWND
        self.u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        self.u.GetWindowThreadProcessId.restype = w.DWORD
        self.u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        self.u.IsWindow.argtypes = self.u.IsWindowVisible.argtypes = self.u.IsIconic.argtypes = self.u.IsHungAppWindow.argtypes = [w.HWND]
        self.u.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
        self.u.AttachThreadInput.restype = w.BOOL
        self.u.ShowWindowAsync.argtypes = [w.HWND, c.c_int]
        self.u.ShowWindowAsync.restype = w.BOOL
        self.u.BringWindowToTop.argtypes = self.u.SetForegroundWindow.argtypes = [w.HWND]
        self.u.BringWindowToTop.restype = self.u.SetForegroundWindow.restype = w.BOOL
        self.u.SetActiveWindow.argtypes = self.u.SetFocus.argtypes = [w.HWND]
        self.u.SetActiveWindow.restype = self.u.SetFocus.restype = w.HWND
        self.u.PeekMessageW.argtypes = [c.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]
        self.k.GetCurrentThreadId.restype = w.DWORD
        self.u.SystemParametersInfoW.argtypes = [w.UINT, w.UINT, c.c_void_p, w.UINT]

    def snapshot(self, hwnd):
        pid = w.DWORD()
        thread = self.u.GetWindowThreadProcessId(hwnd, c.byref(pid)) if hwnd else 0
        name = c.create_unicode_buffer(256)
        if hwnd:
            self.u.GetClassNameW(hwnd, name, len(name))
        return {"hwnd": int(hwnd or 0), "pid": pid.value, "thread": thread,
                "class": name.value, "visible": bool(hwnd and self.u.IsWindowVisible(hwnd)),
                "minimized": bool(hwnd and self.u.IsIconic(hwnd)),
                "hung": bool(hwnd and self.u.IsHungAppWindow(hwnd))}

    def foreground(self):
        return self.snapshot(self.u.GetForegroundWindow())

    def current_thread(self):
        return self.k.GetCurrentThreadId()

    def ensure_message_queue(self):
        message = w.MSG()
        self.u.PeekMessageW(c.byref(message), None, 0, 0, 0)  # PM_NOREMOVE: never dispatch or consume input.

    def attach(self, source, target, enable):
        c.set_last_error(0)
        ok = self.u.AttachThreadInput(source, target, enable)
        return {"ok": bool(ok), "last_error": c.get_last_error()}

    def call(self, name, *args):
        c.set_last_error(0)
        result = getattr(self.u, name)(*args)
        return {"api": name, "result": int(result or 0), "last_error": c.get_last_error()}

    def conditions(self, foreground_thread):
        class GUIINFO(c.Structure):
            _fields_ = [("cbSize", w.DWORD), ("flags", w.DWORD),
                        ("hwndActive", w.HWND), ("hwndFocus", w.HWND),
                        ("hwndCapture", w.HWND), ("hwndMenuOwner", w.HWND),
                        ("hwndMoveSize", w.HWND), ("hwndCaret", w.HWND), ("rcCaret", w.RECT)]
        self.u.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(GUIINFO)]
        state = GUIINFO()
        state.cbSize = c.sizeof(state)
        c.set_last_error(0)
        ok = bool(foreground_thread and self.u.GetGUIThreadInfo(foreground_thread, c.byref(state)))
        info_error = c.get_last_error()
        timeout = w.UINT()
        timeout_ok = self.u.SystemParametersInfoW(0x2000, 0, c.byref(timeout), 0)  # GET, never change policy.
        return {"foreground_gui_info_read": ok, "gui_info_error": info_error,
                "foreground_gui_flags": state.flags, "foreground_menu_owner": int(state.hwndMenuOwner or 0),
                "foreground_focus": int(state.hwndFocus or 0),
                "foreground_lock_timeout_ms": timeout.value if timeout_ok else None,
                "system_focus_policy_modified": False,
                "note": "AttachThreadInput temporarily shares input state and resets key state; no keyboard or mouse events are generated."}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--creation-filetime", type=int, required=True)
    parser.add_argument("--hwnd", type=lambda x: int(x, 0), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--allow-initial-activation", action="store_true")
    args = parser.parse_args()
    if not args.allow_initial_activation:
        parser.error("Requires explicit --allow-initial-activation; no implicit focus changes")
    output = args.output.resolve()
    if output.exists():
        raise ValueError("Preserve existing activation evidence")
    exe = args.exe.resolve()
    result = {"schema": "fvr.bc2.initial-activation416.v1", "passed": False,
              "pid": args.pid, "creation_filetime": str(args.creation_filetime), "hwnd": args.hwnd,
              "explicit_initial_activation": True, "os_keyboard_or_mouse_input_sent": False,
              "helper_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    process = None
    started = time.monotonic()
    try:
        if hashlib.sha256(exe.read_bytes()).hexdigest() != args.expected_sha256:
            raise RuntimeError("Installed BC2 executable changed")
        from read_bc2 import Process
        from runtime_process import creation_filetime
        process = Process(args.pid, expected_path=exe)
        api = Win32()

        def verify():
            if creation_filetime(process) != args.creation_filetime or process.read(process.base, 2) != b"MZ":
                raise RuntimeError("Original BC2 process identity/liveness changed")
            current = api.snapshot(args.hwnd)
            if current["pid"] != args.pid or current["class"] != GAME_CLASS or not current["thread"]:
                raise RuntimeError("Exact original HWND no longer belongs to BC2")
            return current

        activate_once(api, args.hwnd, verify, result)
    except Exception as error:
        result["error"] = str(error)
        result["passed"] = False
    finally:
        if process:
            process.close()
        result["elapsed_seconds"] = round(time.monotonic() - started, 4)
        with output.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
