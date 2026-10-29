"""BC2 window helper. Show/placement/capture never activate; input requires --activate."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import time

GAME_CLASS = "Battlefield: Bad Company 2"


def _desktop_libraries():
    """Bind lazily so package preflight and mocked contracts need no Win32."""
    user32 = c.WinDLL("user32", use_last_error=True)
    kernel32 = c.WinDLL("kernel32", use_last_error=True)
    for library, name, args, result in (
        (user32, "GetProcessWindowStation", [], w.HANDLE),
        (user32, "GetThreadDesktop", [w.DWORD], w.HANDLE),
        (user32, "OpenInputDesktop", [w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
        (user32, "CloseDesktop", [w.HANDLE], w.BOOL),
        (user32, "GetUserObjectInformationW",
         [w.HANDLE, c.c_int, c.c_void_p, w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
        (user32, "GetForegroundWindow", [], w.HWND),
        (user32, "GetWindowThreadProcessId", [w.HWND, c.POINTER(w.DWORD)], w.DWORD),
        (kernel32, "GetCurrentProcessId", [], w.DWORD),
        (kernel32, "GetCurrentThreadId", [], w.DWORD),
        (kernel32, "ProcessIdToSessionId", [w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
        (kernel32, "WTSGetActiveConsoleSessionId", [], w.DWORD),
    ):
        function = getattr(library, name)
        function.argtypes = args
        function.restype = result
    return user32, kernel32


def desktop_context(user32=None, kernel32=None, last_error=None):
    """Take one bounded, read-only caller-desktop snapshot, without a game.

    UOI_IO verifies actual input ownership; desktop names alone are insufficient.
    This does not switch desktops or grant future foreground/input permission.
    """
    result = {"schema": "fvr.bc2.desktop_context", "schema_version": 1,
              "observed_unix_ns": time.time_ns(), "read_only": True,
              "process_opened": False, "window_or_input_actions": False,
              "process_id": None, "thread_id": None, "session_id": None,
              "active_console_session_id": None, "window_station": None,
              "thread_desktop": None, "input_desktop": None,
              "thread_desktop_receives_input": None,
              "input_desktop_receives_input": None,
              "active_input_desktop_verified": False,
              "foreground": {"hwnd": None, "pid": None, "thread_id": None},
              "errors": []}
    if user32 is None or kernel32 is None:
        try:
            user32, kernel32 = _desktop_libraries()
        except (AttributeError, OSError) as error:
            result["errors"].append({"query": "load_win32_libraries", "message": str(error)})
            return result
    if last_error is None:
        last_error = c.get_last_error

    def failed(query):
        result["errors"].append({"query": query, "win32_error": last_error()})

    def name(handle, label):
        # Fixed upper bound; a longer/unavailable name fails admission.
        value = c.create_unicode_buffer(512)
        needed = w.DWORD()
        if not user32.GetUserObjectInformationW(handle, 2, value, c.sizeof(value), c.byref(needed)):
            failed("GetUserObjectInformationW(UOI_NAME):" + label)
            return None
        if not value.value:
            result["errors"].append({"query": "GetUserObjectInformationW(UOI_NAME):" + label,
                                     "message": "Empty object name"})
            return None
        return value.value

    def receives_input(handle, label):
        value = w.BOOL()
        needed = w.DWORD()
        if not user32.GetUserObjectInformationW(handle, 6, c.byref(value), c.sizeof(value), c.byref(needed)):
            failed("GetUserObjectInformationW(UOI_IO):" + label)
            return None
        return bool(value.value)

    result["process_id"] = int(kernel32.GetCurrentProcessId())
    result["thread_id"] = int(kernel32.GetCurrentThreadId())
    session = w.DWORD()
    if kernel32.ProcessIdToSessionId(result["process_id"], c.byref(session)):
        result["session_id"] = session.value
    else:
        failed("ProcessIdToSessionId")
    console = int(kernel32.WTSGetActiveConsoleSessionId())
    result["active_console_session_id"] = None if console == 0xffffffff else console
    station = user32.GetProcessWindowStation()
    if station:
        result["window_station"] = name(station, "window_station")
    else:
        failed("GetProcessWindowStation")
    thread_desktop = user32.GetThreadDesktop(result["thread_id"])
    if thread_desktop:
        result["thread_desktop"] = name(thread_desktop, "thread_desktop")
        result["thread_desktop_receives_input"] = receives_input(thread_desktop, "thread_desktop")
    else:
        failed("GetThreadDesktop")
    input_desktop = user32.OpenInputDesktop(0, False, 0x0001)  # DESKTOP_READOBJECTS only.
    if input_desktop:
        try:
            result["input_desktop"] = name(input_desktop, "input_desktop")
            result["input_desktop_receives_input"] = receives_input(input_desktop, "input_desktop")
        finally:
            # GetProcessWindowStation/GetThreadDesktop return borrowed handles.
            if not user32.CloseDesktop(input_desktop):
                failed("CloseDesktop")
    else:
        failed("OpenInputDesktop")
    foreground = int(user32.GetForegroundWindow() or 0)
    result["foreground"]["hwnd"] = foreground
    if foreground:  # A NULL foreground is an observation, not a GetLastError failure.
        owner = w.DWORD()
        thread = user32.GetWindowThreadProcessId(foreground, c.byref(owner))
        if thread:
            result["foreground"].update(pid=owner.value, thread_id=int(thread))
        else:
            failed("GetWindowThreadProcessId:foreground")
    result["active_input_desktop_verified"] = bool(
        not result["errors"] and result["window_station"]
        and result["thread_desktop"] and result["input_desktop"]
        and result["thread_desktop"].casefold() == result["input_desktop"].casefold()
        and result["thread_desktop_receives_input"] is True
        and result["input_desktop_receives_input"] is True)
    return result


def require_input_desktop(context):
    if not context["active_input_desktop_verified"]:
        raise RuntimeError("Caller desktop could not be verified as the active input desktop; "
                           "inspect --desktop-context. Context: " + json.dumps(context))


def capture_window_image(bounds, grab, get_metrics):
    """Crop desktop pixels using the OS virtual origin, including left monitors.

    Capture without bbox and validate the layout before cropping explicitly.
    Both bbox and full-desktop GDI captures can intermittently return black;
    this coordinate check alone does not establish valid game presentation.
    The full desktop stays in memory; callers save only the requested window.
    """
    virtual = tuple(get_metrics(i) for i in (76, 77, 78, 79))
    desktop = grab(all_screens=True)
    if virtual != tuple(get_metrics(i) for i in (76, 77, 78, 79)):
        raise RuntimeError("Display layout changed during window capture")
    x, y, width, height = virtual
    if width <= 0 or height <= 0 or desktop.size != (width, height):
        raise RuntimeError("Desktop capture size does not match virtual display")
    left, top, right, bottom = bounds
    crop = (left-x, top-y, right-x, bottom-y)
    if not (0 <= crop[0] < crop[2] <= width and 0 <= crop[1] < crop[3] <= height):
        raise RuntimeError("Window is outside the captured virtual desktop")
    return desktop.crop(crop), {"method": "virtual_desktop_crop",
                                "virtual_bounds": list(virtual),
                                "window_bounds": list(bounds), "crop": list(crop)}


def capture_window_burst(owner, grab, get_metrics, snapshot, pause=time.sleep):
    """Select a navigable frame without hiding inconclusive black captures."""
    selected = None
    selected_index = None
    attempts = []
    virtual_bounds = None
    keys = ("hwnd", "pid", "class", "rect", "dpi", "visible", "minimized")
    def verify_owner():
        current = snapshot()
        if any(current[key] != owner[key] for key in keys):
            raise RuntimeError("BC2 window identity or geometry changed during capture")
    for index in range(6):
        if index:
            pause(.05)
        verify_owner()
        started = time.perf_counter_ns()
        captured, metadata = capture_window_image(owner["rect"], grab, get_metrics)
        if virtual_bounds is not None and metadata["virtual_bounds"] != virtual_bounds:
            raise RuntimeError("Display layout changed between capture attempts")
        virtual_bounds = metadata["virtual_bounds"]
        verify_owner()
        rgb = captured.convert("RGB")
        histogram = rgb.histogram()
        pixels = rgb.width * rgb.height
        nonzero = sum(pixels-histogram[channel*256] for channel in range(3))
        grayscale = rgb.convert("L").histogram()
        attempts.append({"index": index, "started_ns": started,
                         "elapsed_ns": time.perf_counter_ns()-started,
                         "nonzero_channels": nonzero, "total_channels": pixels*3,
                         "grayscale_nonzero_pixels": pixels-grayscale[0],
                         "grayscale_mean": sum(i*n for i, n in enumerate(grayscale))/pixels,
                         "all_black": nonzero == 0})
        if nonzero:
            selected = captured
            selected_index = index
    if selected is None:
        selected = captured
        selected_index = len(attempts)-1
    metadata.update({"dpi": owner["dpi"], "attempts": attempts,
                     "selected_attempt": selected_index,
                     "selection": "last_nonblack" if any(not x["all_black"] for x in attempts) else "last_inconclusive",
                     "capture_inconclusive": all(x["all_black"] for x in attempts),
                     "presentation_health_assessed": False})
    return selected, metadata


def recovery_plan(*, visible, minimized, show, place, activate=False):
    """Never turn a passive visibility repair into an unminimize operation."""
    if minimized and not activate:
        return {"show": False, "place": False, "skip": "user_minimized"}
    return {"show": bool(show and not visible and not activate),
            "place": bool(place), "skip": None}


def select_window(windows, pid, requested=None):
    """Use exact process/class identity; cleanup never switches to another HWND."""
    matches = [v for v in windows
               if v["pid"] == pid and v["class"] == GAME_CLASS
               and (v["minimized"] or (v["width"] >= 800 and v["height"] >= 500))]
    if requested is not None:
        matches = [v for v in matches if v["hwnd"] == requested]
    if len(matches) != 1:
        raise RuntimeError("Expected one original BC2 window with matching process/class")
    return matches[0]["hwnd"]


def report_path(value, report_root=None):
    out = Path(value).resolve()
    root = Path(report_root) if report_root is not None else Path(__file__).resolve().parents[1] / "reports"
    if not root.is_absolute() or out == root.resolve() or not out.is_relative_to(root.resolve()):
        raise ValueError("Capture/report must stay inside the explicit report root or package reports")
    return out


def preflight():
    # Exercise the actual local import without opening a process, enumerating
    # windows or requiring a running game. Screenshot capture is optional.
    import read_bc2
    expected_module = Path(__file__).resolve().with_name('read_bc2.py')
    if Path(read_bc2.__file__).resolve() != expected_module:
        raise RuntimeError('Window recovery dependency is not package-local')
    config = Path(__file__).resolve().parents[1] / 'config/local.json'
    expected = read_bc2.configured_executable(config) if config.is_file() else None
    return {'helper_ready': True, 'dependency': 'tools/read_bc2.py',
            'game_configured': expected is not None, 'expected_executable': expected,
            'process_opened': False, 'window_or_input_actions': False,
            'optional_screenshot_dependency': 'Pillow (only for --capture)'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pid", type=int)
    ap.add_argument("--preflight", action="store_true")
    ap.add_argument("--desktop-context", action="store_true")
    ap.add_argument("--hwnd", type=lambda text: int(text, 0))
    ap.add_argument("--key", choices=["escape", "enter", "e", "r", "1", "2"])
    ap.add_argument("--click", type=int, nargs=2)
    ap.add_argument("--capture")
    ap.add_argument("--report")
    ap.add_argument("--report-root")
    ap.add_argument("--activate", action="store_true")
    ap.add_argument("--left-monitor", action="store_true")
    ap.add_argument("--show", action="store_true")
    a = ap.parse_args()
    window_arguments = (a.pid is not None, a.hwnd is not None, a.key, a.click, a.capture,
                        a.report, a.report_root, a.activate, a.left_monitor, a.show)
    if a.preflight:
        if a.desktop_context or any(window_arguments):
            ap.error('--preflight must be used alone')
        print(json.dumps(preflight()))
        return
    if a.desktop_context:
        if any(window_arguments):
            ap.error('--desktop-context must be used alone')
        print(json.dumps(desktop_context()))
        return
    if not a.pid:
        ap.error('--pid is required for window operations')
    if (a.key or a.click) and not a.activate:
        ap.error("Keyboard/mouse control requires explicit --activate")
    output = report_path(a.report, a.report_root) if a.report else None
    capture = report_path(a.capture, a.report_root) if a.capture else None
    context = desktop_context()
    require_input_desktop(context)
    from read_bc2 import Process
    p = Process(a.pid)
    try:
        u = c.WinDLL("user32", use_last_error=True)
        u.SetProcessDpiAwarenessContext.argtypes = [c.c_void_p]
        u.SetProcessDpiAwarenessContext(c.c_void_p(-4))
        u.IsWindow.argtypes = u.IsIconic.argtypes = u.IsWindowVisible.argtypes = [w.HWND]
        u.GetForegroundWindow.restype = w.HWND
        u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        u.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
        u.GetDpiForWindow.argtypes = [w.HWND]
        u.GetDpiForWindow.restype = w.UINT
        u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        u.ShowWindow.argtypes = [w.HWND, c.c_int]
        u.SetForegroundWindow.argtypes = [w.HWND]
        u.GetWindowLongW.argtypes = [w.HWND, c.c_int]
        u.GetWindowLongW.restype = w.LONG
        u.SetWindowLongW.argtypes = [w.HWND, c.c_int, w.LONG]
        u.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
        u.keybd_event.argtypes = [w.BYTE, w.BYTE, w.DWORD, c.c_size_t]
        u.mouse_event.argtypes = [w.DWORD, w.DWORD, w.DWORD, w.DWORD, c.c_size_t]
        u.SetCursorPos.argtypes = [c.c_int, c.c_int]
        u.GetCursorPos.argtypes = [c.POINTER(w.POINT)]
        u.WindowFromPoint.argtypes = [w.POINT]
        u.WindowFromPoint.restype = w.HWND

        def snapshot(hwnd):
            pid = w.DWORD()
            thread = u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            rect = w.RECT()
            if not u.GetWindowRect(hwnd, c.byref(rect)):
                raise OSError(c.get_last_error(), "Read BC2 window bounds")
            name = c.create_unicode_buffer(256)
            if not u.GetClassNameW(hwnd, name, len(name)):
                raise OSError(c.get_last_error(), "Read BC2 window class")
            return {"hwnd": int(hwnd), "pid": pid.value, "thread": thread,
                    "class": name.value, "visible": bool(u.IsWindowVisible(hwnd)),
                    "minimized": bool(u.IsIconic(hwnd)),
                    "rect": [rect.left, rect.top, rect.right, rect.bottom],
                    "width": rect.right-rect.left, "height": rect.bottom-rect.top,
                    "dpi": u.GetDpiForWindow(hwnd),
                    "foreground": int(u.GetForegroundWindow() or 0)}

        windows = []
        @c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
        def each(hwnd, unused):
            pid = w.DWORD()
            u.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if pid.value == a.pid:
                try:
                    windows.append(snapshot(hwnd))
                except OSError:
                    pass  # A concurrently destroyed window cannot be selected.
            return True
        u.EnumWindows(each, 0)
        hwnd = select_window(windows, a.pid, a.hwnd)

        def owns():
            # Original process handle remains open; an exited/reused PID cannot
            # pass its memory read plus current exact HWND/class checks.
            if p.read(p.base, 2) != b"MZ":
                raise RuntimeError("Original BC2 process is no longer readable")
            state = snapshot(hwnd)
            if state["pid"] != a.pid or state["class"] != GAME_CLASS:
                raise RuntimeError("BC2 no longer owns the original window")
            return state

        before = owns()
        plan = recovery_plan(visible=before["visible"], minimized=before["minimized"],
                             show=a.show or a.left_monitor, place=a.left_monitor, activate=a.activate)
        actions = []
        if plan["show"]:
            if owns()["minimized"]:
                plan["skip"] = "user_minimized"
            else:
                u.ShowWindow(hwnd, 8)  # SW_SHOWNOACTIVATE; never SetForegroundWindow.
                actions.append("show_without_activation")
        if plan["place"]:
            monitors = []
            @c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
            def monitor(handle, dc, rect, param):
                r = rect.contents
                monitors.append((r.left, r.top, r.right, r.bottom))
                return True
            u.EnumDisplayMonitors(None, None, monitor, 0)
            if not monitors:
                raise RuntimeError("No desktop monitors found")
            left, top, right, bottom = min(monitors, key=lambda r: r[0])
            # Respect minimization that occurred after the initial snapshot.
            if owns()["minimized"] and not a.activate:
                plan["skip"] = "user_minimized"
            else:
                style = u.GetWindowLongW(hwnd, -16)
                u.SetWindowLongW(hwnd, -16, style & ~0x00c40000)
                # No SWP_SHOWWINDOW: showing is explicit and independently guarded.
                if not u.SetWindowPos(hwnd, None, left, top, right-left, bottom-top, 0x4 | 0x10 | 0x20):
                    raise OSError(c.get_last_error(), "Nonactivating placement failed")
                actions.append("place_left_without_activation")
        if a.activate:
            owns()
            u.ShowWindow(hwnd, 9)
            u.SetForegroundWindow(hwnd)
            time.sleep(.25)
            actions.append("explicit_activation")

        def check_foreground():
            owns()
            if u.GetForegroundWindow() != hwnd:
                raise RuntimeError("BC2 no longer owns foreground")
        if a.activate:
            check_foreground()
        if a.key:
            vk, scan = {"escape": (0x1b, 1), "enter": (0x0d, 0x1c), "e": (0x45, 0x12),
                        "r": (0x52, 0x13), "1": (0x31, 0x02), "2": (0x32, 0x03)}[a.key]
            u.keybd_event(vk, scan, 0, 0)
            try:
                time.sleep(.15)
            finally:
                u.keybd_event(vk, scan, 2, 0)
            time.sleep(.4)
        if a.click:
            check_foreground()
            current = owns()
            x, y = a.click
            if not (0 <= x < current["width"] and 0 <= y < current["height"]):
                raise ValueError("Click outside game window")
            target = (current["rect"][0]+x, current["rect"][1]+y)
            if not u.SetCursorPos(*target):
                raise OSError(c.get_last_error(), "Cannot position BC2 cursor")
            # Allow the native UI to consume cursor motion before the click.
            time.sleep(.12)
            check_foreground()
            if owns()["rect"] != current["rect"]:
                raise RuntimeError("BC2 window moved before the requested click")
            cursor = w.POINT()
            if not u.GetCursorPos(c.byref(cursor)) or (cursor.x, cursor.y) != target:
                raise RuntimeError("Cursor moved before the requested BC2 click")
            if u.WindowFromPoint(cursor) != hwnd:
                raise RuntimeError("BC2 no longer owns the requested click position")
            u.mouse_event(2, 0, 0, 0, 0)
            try:
                time.sleep(.08)
            finally:
                u.mouse_event(4, 0, 0, 0, 0)
            time.sleep(.4)
        capture_metadata = None
        if capture:
            if a.activate:
                check_foreground()
            from PIL import ImageGrab
            capture_owner = owns()
            captured, capture_metadata = capture_window_burst(
                capture_owner, ImageGrab.grab, u.GetSystemMetrics, owns)
            capture_after = owns()
            if capture_after["rect"] != capture_owner["rect"] or capture_after["dpi"] != capture_owner["dpi"]:
                raise RuntimeError("BC2 window geometry changed during capture")
            capture_metadata["dpi"] = capture_owner["dpi"]
            captured.save(capture)
        after = owns()
        result = {"schema": "fvr.bc2.window_recovery", "schema_version": 1,
                  "desktop_context": context,
                  "before": before, "after": after, "actions": actions, "skip": plan["skip"],
                  "foreground_unchanged": before["foreground"] == after["foreground"],
                  "activation_requested": a.activate}
        if capture_metadata is not None:
            result["capture"] = capture_metadata
        if output:
            output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(result))
    finally:
        p.close()


if __name__ == "__main__":
    main()
