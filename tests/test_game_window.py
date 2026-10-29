import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest import mock
try:
    from PIL import Image
except ImportError:
    Image = None

spec = importlib.util.spec_from_file_location("game_window", Path(__file__).parents[1]/"tools"/"game_window.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DesktopContextTests(unittest.TestCase):
    def fixture(self, *, desktop="Default", thread_input=True, input_available=True,
                failed_query=None, close_ok=True, foreground=0x100000099, owner_ok=True):
        # Only the opened input handle (33) is owned; 11/22 are borrowed.
        names = {11: "WinSta0", 22: desktop, 33: "Default"}

        def object_info(handle, query, buffer, size, needed):
            if (handle, query) == failed_query:
                return False
            if query == 2:
                buffer.value = names[handle]
            elif query == 6:
                module.c.cast(buffer, module.c.POINTER(module.w.BOOL)).contents.value = (
                    thread_input if handle == 22 else True)
            else:
                self.fail("Unexpected user-object query")
            return True

        def session(pid, output):
            module.c.cast(output, module.c.POINTER(module.w.DWORD)).contents.value = 1
            return True

        def owner(hwnd, output):
            module.c.cast(output, module.c.POINTER(module.w.DWORD)).contents.value = 53772
            return 42 if owner_ok else 0

        user32 = SimpleNamespace(
            GetProcessWindowStation=mock.Mock(return_value=11),
            GetThreadDesktop=mock.Mock(return_value=22),
            OpenInputDesktop=mock.Mock(return_value=33 if input_available else 0),
            CloseDesktop=mock.Mock(return_value=close_ok),
            GetUserObjectInformationW=mock.Mock(side_effect=object_info),
            GetForegroundWindow=mock.Mock(return_value=foreground),
            GetWindowThreadProcessId=mock.Mock(side_effect=owner))
        kernel32 = SimpleNamespace(
            GetCurrentProcessId=mock.Mock(return_value=1234),
            GetCurrentThreadId=mock.Mock(return_value=5678),
            ProcessIdToSessionId=mock.Mock(side_effect=session),
            WTSGetActiveConsoleSessionId=mock.Mock(return_value=1))
        return user32, kernel32

    def observe(self, **changes):
        user32, kernel32 = self.fixture(**changes)
        return module.desktop_context(user32, kernel32, lambda: 5), user32

    def test_active_desktop_records_identity_and_closes_only_owned_handle(self):
        context, user32 = self.observe()
        module.require_input_desktop(context)
        self.assertTrue(context["active_input_desktop_verified"])
        self.assertEqual((context["window_station"], context["thread_desktop"], context["input_desktop"]),
                         ("WinSta0", "Default", "Default"))
        self.assertEqual((context["process_id"], context["thread_id"], context["session_id"]),
                         (1234, 5678, 1))
        self.assertEqual(context["foreground"], {"hwnd": 0x100000099, "pid": 53772, "thread_id": 42})
        self.assertEqual(context["errors"], [])
        user32.OpenInputDesktop.assert_called_once_with(0, False, 0x0001)
        user32.CloseDesktop.assert_called_once_with(33)

    def test_sandbox_desktop_mismatch_is_distinct_from_null_foreground(self):
        context, user32 = self.observe(desktop="CodexSandboxDesktop-test", thread_input=False, foreground=0)
        self.assertEqual(context["foreground"], {"hwnd": 0, "pid": None, "thread_id": None})
        self.assertEqual(context["errors"], [])
        self.assertFalse(context["active_input_desktop_verified"])
        with self.assertRaisesRegex(RuntimeError, "active input desktop"):
            module.require_input_desktop(context)
        user32.GetWindowThreadProcessId.assert_not_called()
        user32.CloseDesktop.assert_called_once_with(33)

    def test_same_desktop_name_does_not_replace_input_ownership(self):
        context, _ = self.observe(thread_input=False)
        self.assertEqual(context["thread_desktop"], context["input_desktop"])
        self.assertFalse(context["active_input_desktop_verified"])

    def test_unavailable_input_desktop_refuses_without_closing_borrowed_handles(self):
        context, user32 = self.observe(input_available=False)
        self.assertEqual(context["errors"], [{"query": "OpenInputDesktop", "win32_error": 5}])
        self.assertIsNone(context["input_desktop"])
        with self.assertRaisesRegex(RuntimeError, "OpenInputDesktop"):
            module.require_input_desktop(context)
        user32.CloseDesktop.assert_not_called()

    def test_failed_name_or_input_query_is_reported_and_owned_handle_closed(self):
        for handle, query, label in ((22, 2, "UOI_NAME):thread_desktop"),
                                     (22, 6, "UOI_IO):thread_desktop"),
                                     (33, 2, "UOI_NAME):input_desktop"),
                                     (33, 6, "UOI_IO):input_desktop")):
            with self.subTest(handle=handle, query=query):
                context, user32 = self.observe(failed_query=(handle, query))
                self.assertFalse(context["active_input_desktop_verified"])
                self.assertEqual(context["errors"], [
                    {"query": "GetUserObjectInformationW(" + label, "win32_error": 5}])
                user32.CloseDesktop.assert_called_once_with(33)

    def test_close_failure_is_visible_and_blocks_admission(self):
        context, user32 = self.observe(close_ok=False)
        self.assertFalse(context["active_input_desktop_verified"])
        self.assertEqual(context["errors"], [{"query": "CloseDesktop", "win32_error": 5}])
        user32.CloseDesktop.assert_called_once_with(33)

    def test_foreground_owner_failure_is_not_an_invented_owner(self):
        context, user32 = self.observe(owner_ok=False)
        self.assertEqual(context["foreground"], {"hwnd": 0x100000099, "pid": None, "thread_id": None})
        self.assertEqual(context["errors"], [
            {"query": "GetWindowThreadProcessId:foreground", "win32_error": 5}])
        user32.CloseDesktop.assert_called_once_with(33)

    def test_platform_unavailability_is_a_diagnostic_result(self):
        with mock.patch.object(module, "_desktop_libraries", side_effect=OSError("Win32 unavailable")):
            context = module.desktop_context()
        self.assertFalse(context["active_input_desktop_verified"])
        self.assertEqual(context["errors"], [
            {"query": "load_win32_libraries", "message": "Win32 unavailable"}])

    def test_diagnostic_cli_needs_no_game_or_capture_dependency(self):
        user32, kernel32 = self.fixture()
        original_import = __import__

        def guarded_import(name, *args, **kwargs):
            if name in ("read_bc2", "PIL", "PIL.ImageGrab"):
                self.fail("Desktop diagnostic imported a game/capture dependency")
            return original_import(name, *args, **kwargs)

        with mock.patch("sys.argv", ["game_window.py", "--desktop-context"]), \
                mock.patch.object(module, "_desktop_libraries", return_value=(user32, kernel32)), \
                mock.patch.object(module.c, "get_last_error", return_value=5, create=True), \
                mock.patch("builtins.__import__", side_effect=guarded_import), \
                mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            module.main()
        context = json.loads(output.getvalue())
        self.assertTrue(context["active_input_desktop_verified"])
        self.assertFalse(context["process_opened"])
        self.assertFalse(context["window_or_input_actions"])
        user32.CloseDesktop.assert_called_once_with(33)

    def test_preflight_does_not_query_desktop_context(self):
        with mock.patch("sys.argv", ["game_window.py", "--preflight"]), \
                mock.patch.object(module, "preflight", return_value={"helper_ready": True}), \
                mock.patch.object(module, "desktop_context", side_effect=AssertionError("OS query")), \
                mock.patch("sys.stdout", new_callable=io.StringIO) as output:
            module.main()
        self.assertEqual(json.loads(output.getvalue()), {"helper_ready": True})

    def test_refused_desktop_stops_every_window_mode_before_opening_process(self):
        process = mock.Mock(side_effect=AssertionError("Process opened before desktop admission"))
        for options in ([], ["--show"], ["--left-monitor"], ["--activate"],
                        ["--activate", "--key", "enter"],
                        ["--capture", str(Path(__file__).resolve().parents[1]/"reports"/"unused.png")]):
            for changes in ({"input_available": False}, {"desktop": "Sandbox", "thread_input": False}):
                with self.subTest(options=options, changes=changes):
                    context, _ = self.observe(**changes)
                    with mock.patch("sys.argv", ["game_window.py", "--pid", "4", *options]), \
                            mock.patch.object(module, "desktop_context", return_value=context), \
                            mock.patch.dict("sys.modules", {"read_bc2": SimpleNamespace(Process=process)}), \
                            self.assertRaisesRegex(RuntimeError, "active input desktop"):
                        module.main()
        process.assert_not_called()

    def test_verified_desktop_reaches_existing_process_admission(self):
        context, _ = self.observe()
        process = mock.Mock(side_effect=RuntimeError("Existing process admission reached"))
        with mock.patch("sys.argv", ["game_window.py", "--pid", "4"]), \
                mock.patch.object(module, "desktop_context", return_value=context), \
                mock.patch.dict("sys.modules", {"read_bc2": SimpleNamespace(Process=process)}), \
                self.assertRaisesRegex(RuntimeError, "Existing process admission reached"):
            module.main()
        process.assert_called_once_with(4)


class WindowRecoveryTests(unittest.TestCase):
    def plan(self, **changes):
        args = dict(visible=False, minimized=False, show=True, place=False)
        args.update(changes)
        return module.recovery_plan(**args)

    def test_hidden_original_shows_without_placement(self):
        self.assertEqual(self.plan(), {"show": True, "place": False, "skip": None})

    def test_visible_window_has_no_redundant_show(self):
        self.assertFalse(self.plan(visible=True)["show"])

    def test_user_minimization_survives_show_and_left_placement(self):
        for visible in (False, True):
            self.assertEqual(self.plan(visible=visible, minimized=True, place=True),
                             {"show": False, "place": False, "skip": "user_minimized"})

    def test_only_explicit_activation_may_restore_minimized_placement(self):
        p = self.plan(minimized=True, place=True, activate=True)
        self.assertTrue(p["place"])
        self.assertFalse(p["show"])

    def test_readonly_does_not_request_show(self):
        self.assertEqual(self.plan(show=False), {"show": False, "place": False, "skip": None})

    def window(self, **changes):
        v = dict(pid=4, hwnd=99, **{"class": module.GAME_CLASS}, width=1920, height=1080, minimized=False)
        v.update(changes)
        return v

    def test_original_hidden_window_does_not_need_visible_style(self):
        self.assertEqual(module.select_window([self.window(visible=False)], 4, 99), 99)

    def test_cleanup_cannot_switch_to_replacement_window(self):
        with self.assertRaises(RuntimeError):
            module.select_window([self.window(hwnd=100)], 4, 99)

    def test_foreign_process_and_ghost_class_rejected(self):
        for v in (self.window(pid=8), self.window(**{"class": "Ghost"})):
            with self.assertRaises(RuntimeError):
                module.select_window([v], 4)

    def test_ambiguous_original_windows_rejected(self):
        with self.assertRaises(RuntimeError):
            module.select_window([self.window(), self.window(hwnd=100)], 4)

    def test_minimized_original_is_found_at_small_bounds(self):
        self.assertEqual(module.select_window([self.window(minimized=True, width=100, height=20)], 4, 99), 99)

    def test_capture_scope_is_reports_only(self):
        root = Path(__file__).resolve().parents[1]
        self.assertEqual(module.report_path(root/"reports"/"window-test.json"), root/"reports"/"window-test.json")
        with self.assertRaises(ValueError):
            module.report_path(root/"tools"/"bad.json")

    def capture_fixture(self, bounds, virtual=(-1920, 0, 3840, 1080), changed=None, image_size=None):
        class Desktop:
            size = image_size or virtual[2:]
            def crop(self, box):
                return ("captured_pixels", box)
        calls = []
        reads = []
        def grab(**kwargs):
            calls.append(kwargs)
            # The regression requires full-desktop capture: the previous bbox
            # call on this machine produced black padding for the left monitor.
            self.assertEqual(kwargs, {"all_screens": True})
            return Desktop()
        def metrics(index):
            position = (76, 77, 78, 79).index(index)
            source = changed if changed is not None and len(reads) >= 4 else virtual
            reads.append(index)
            return source[position]
        result = module.capture_window_image(bounds, grab, metrics)
        self.assertEqual(len(calls), 1)
        return result

    def test_left_monitor_capture_uses_virtual_origin_without_bbox(self):
        image, metadata = self.capture_fixture((-1920, 0, 0, 1080))
        self.assertEqual(image, ("captured_pixels", (0, 0, 1920, 1080)))
        self.assertEqual(metadata["virtual_bounds"], [-1920, 0, 3840, 1080])

    def test_primary_monitor_retains_positive_pixel_offset(self):
        image, _ = self.capture_fixture((0, 0, 1920, 1080))
        self.assertEqual(image, ("captured_pixels", (1920, 0, 3840, 1080)))

    def test_monitor_above_primary_uses_negative_y_origin(self):
        image, _ = self.capture_fixture((0, -1080, 1920, 0), (0, -1080, 1920, 2160))
        self.assertEqual(image, ("captured_pixels", (0, 0, 1920, 1080)))

    def test_display_change_or_wrong_capture_size_is_not_black_evidence(self):
        with self.assertRaisesRegex(RuntimeError, "layout changed"):
            self.capture_fixture((-1920, 0, 0, 1080), changed=(0, 0, 1920, 1080))
        with self.assertRaisesRegex(RuntimeError, "size"):
            self.capture_fixture((-1920, 0, 0, 1080), image_size=(1920, 1080))

    def test_offscreen_window_rejects_instead_of_silent_black_padding(self):
        with self.assertRaisesRegex(RuntimeError, "outside"):
            self.capture_fixture((-1930, 0, -10, 1080))


@unittest.skipIf(Image is None, "Optional Pillow capture dependency unavailable")
class CaptureBurstTests(unittest.TestCase):
    def fixture(self, colors, change_owner=None, change_metrics=None):
        owner = dict(hwnd=99, pid=4, **{"class": module.GAME_CLASS},
                     rect=[-2, 0, 0, 2], dpi=96, visible=True, minimized=False)
        state = {"grabs": 0, "snapshots": 0, "metrics": 0, "waits": []}
        def grab(**kwargs):
            self.assertEqual(kwargs, {"all_screens": True})
            # Right monitor pixels must never make a black game crop look valid.
            desktop = Image.new("RGB", (4, 2), (0, 255, 0))
            desktop.paste(colors[state["grabs"]], (0, 0, 2, 2))
            state["grabs"] += 1
            return desktop
        def snapshot():
            state["snapshots"] += 1
            current = {**owner, "rect": list(owner["rect"])}
            if change_owner:
                change_owner(state, current)
            return current
        def metrics(index):
            state["metrics"] += 1
            bounds = [-2, 0, 4, 2]
            if change_metrics:
                change_metrics(state, bounds)
            return bounds[(76, 77, 78, 79).index(index)]
        return owner, grab, metrics, snapshot, state

    def test_alternating_actual_frames_keep_last_color_and_black_evidence(self):
        args = self.fixture([(0, 0, 0), (255, 0, 0), (0, 0, 0),
                             (0, 0, 0), (0, 0, 255), (0, 0, 0)])
        owner, grab, metrics, snapshot, state = args
        image, report = module.capture_window_burst(owner, grab, metrics, snapshot, state["waits"].append)
        self.assertEqual(image.size, (2, 2))
        self.assertEqual(image.tobytes(), bytes([0, 0, 255])*4)
        self.assertEqual(report["selected_attempt"], 4)
        self.assertEqual([x["all_black"] for x in report["attempts"]], [True, False, True, True, False, True])
        self.assertFalse(report["capture_inconclusive"])
        self.assertFalse(report["presentation_health_assessed"])
        self.assertEqual(state["waits"], [.05]*5)

    def test_all_black_retains_last_frame_and_explicit_inconclusive(self):
        owner, grab, metrics, snapshot, state = self.fixture([(0, 0, 0)]*6)
        image, report = module.capture_window_burst(owner, grab, metrics, snapshot, lambda _: None)
        self.assertEqual(image.getbbox(), None)
        self.assertEqual(report["selected_attempt"], 5)
        self.assertTrue(report["capture_inconclusive"])
        self.assertEqual(report["selection"], "last_inconclusive")
        self.assertEqual([x["nonzero_channels"] for x in report["attempts"]], [0]*6)
        self.assertEqual(state["grabs"], 6)

    def test_color_below_grayscale_rounding_still_counts_as_nonblack(self):
        owner, grab, metrics, snapshot, _ = self.fixture([(0, 0, 1)]*6)
        _, report = module.capture_window_burst(owner, grab, metrics, snapshot, lambda _: None)
        self.assertFalse(report["capture_inconclusive"])
        self.assertEqual(report["attempts"][0]["nonzero_channels"], 4)
        self.assertEqual(report["attempts"][0]["grayscale_nonzero_pixels"], 0)

    def test_owner_replacement_or_geometry_change_aborts_whole_burst(self):
        for field, value in (("hwnd", 100), ("pid", 5), ("dpi", 120),
                             ("rect", [0, 0, 2, 2]), ("minimized", True)):
            def change(state, current):
                if state["snapshots"] == 4:
                    current[field] = value
            owner, grab, metrics, snapshot, _ = self.fixture([(255, 0, 0)]*6, change_owner=change)
            with self.subTest(field=field), self.assertRaisesRegex(RuntimeError, "identity or geometry"):
                module.capture_window_burst(owner, grab, metrics, snapshot, lambda _: None)

    def test_display_change_during_later_frame_never_returns_old_selection(self):
        def change(state, bounds):
            if state["metrics"] > 12:
                bounds[0] = 0
        owner, grab, metrics, snapshot, _ = self.fixture([(255, 0, 0)]*6, change_metrics=change)
        with self.assertRaisesRegex(RuntimeError, "layout changed"):
            module.capture_window_burst(owner, grab, metrics, snapshot, lambda _: None)


if __name__ == "__main__":
    unittest.main()
