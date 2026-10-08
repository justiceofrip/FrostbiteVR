"""BC2 window helper. Show/placement/capture never activate; input requires --activate."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import time

GAME_CLASS = "Battlefield: Bad Company 2"


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
    if a.preflight:
        if any((a.pid, a.hwnd, a.key, a.click, a.capture, a.report, a.report_root, a.activate, a.left_monitor, a.show)):
            ap.error('--preflight must be used alone')
        print(json.dumps(preflight()))
        return
    if not a.pid:
        ap.error('--pid is required for window operations')
    if (a.key or a.click) and not a.activate:
        ap.error("Keyboard/mouse control requires explicit --activate")
    output = report_path(a.report, a.report_root) if a.report else None
    capture = report_path(a.capture, a.report_root) if a.capture else None
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
