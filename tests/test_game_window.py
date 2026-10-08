import importlib.util
from pathlib import Path
import unittest
try:
    from PIL import Image
except ImportError:
    Image = None

spec = importlib.util.spec_from_file_location("game_window", Path(__file__).parents[1]/"tools"/"game_window.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


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
