"""ROOT-RUN-ONLY ordinary XR Resume helper for an already owned live run.

python -B tools/resume_bc2_simulation.py --run G:/.../run01
At most three unchanged Resume-ray clicks and 35 seconds including cleanup.
Simulator acknowledgements prove input delivery, never native widget acceptance.
Success requires fresh native world_calls progression. No launch or capture.
--single-player-capture sim/capture/NAME.json instead selects the reviewed Main
menu's Single Player tab once, then returns released for ROOT capture/review.
--single-player-hover-only aims without clicking. Neither mode invokes Resume.
--dropdown-resume-capture sim/capture/NAME.json selects the reviewed open Single
Player dropdown's Resume Game item once, then waits for native world progression.
ROOT must review both current dropdown images before invoking this option.
"""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location(
    "bc2_resume_transport", Path(__file__).with_name("drive_bc2_feed_simulation.py"))
transport = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(transport)
require, integer, REFUSAL = transport.require, transport.integer, transport.REFUSAL
NS = transport.NS
TAIL_BYTES = 512 * 1024
MAX_AGE_NS = 1_500_000_000
POLL_SECONDS = .05
RETRY_NS = 4 * NS
RELEASE_RESERVE_NS = 500_000_000
# Exact feedback65/67 full-quad Resume pose; no inferred coordinate correction.
RESUME_POSE = "hand r grip pose 0.2 1.3 -0.35 -28.353614376 24.553389864 0"


class MenuTransition(Exception):
    """Input stops permanently; native world observation may continue."""


class NativeTail:
    """Read only a bounded complete tail; never accept a replaced/truncated log."""
    def __init__(self, path):
        self.path, self.identity, self.size = path, None, 0

    def read(self):
        transport.local_path(self.path)
        with self.path.open("rb") as stream:
            info = os.fstat(stream.fileno())
            identity = (info.st_dev, info.st_ino)
            require(self.identity in (None, identity), "native telemetry file identity changed")
            require(info.st_size >= self.size, "native telemetry file truncated")
            self.identity, self.size = identity, info.st_size
            offset = max(0, info.st_size - TAIL_BYTES)
            stream.seek(offset)
            raw = stream.read(min(info.st_size, TAIL_BYTES))
        lines = raw.split(b"\n")[:-1]  # A partially written last row is never evidence.
        if offset:
            lines = lines[1:]
        for line in reversed(lines):
            try:
                row = json.loads(line)
            except (ValueError, UnicodeError):
                continue
            if isinstance(row, dict) and "world_calls" in row:
                return row
        raise REFUSAL("bounded native tail has no complete world telemetry row")


def observation(row, now):
    menu = row["menu_connection"]
    stamp = integer(row["selected_knife"]["sample_ns"])
    require(0 <= now - stamp <= MAX_AGE_NS, "native telemetry is stale or future-dated")
    require(isinstance(menu["ready"], bool), "native menu readiness is not boolean")
    result = dict(sampleNs=stamp, elapsedMs=integer(row["elapsed_ms"]),
                  worldCalls=integer(row["world_calls"]), menuReady=menu["ready"],
                  menuInputs=integer(menu["input_samples"]), menuPublished=integer(menu["published"]),
                  menuGraphicsFailures=integer(menu["graphics_failures"]))
    require(all(result[key] >= 0 for key in
                ("sampleNs", "elapsedMs", "worldCalls", "menuInputs", "menuPublished", "menuGraphicsFailures")),
            "negative native telemetry counter")
    return result


class ResumeDriver(transport.Driver):
    pose = RESUME_POSE
    reviewed_menu_required = False

    def neutral_commands(self):
        return [self.pose, "trigger r 0"]

    def __init__(self, args):
        require(5 <= args.timeout_seconds <= 35, "Resume timeout must be 5..35 seconds")
        require(1 <= args.max_clicks <= 3, "Resume permits at most three clicks")
        self.latest = None
        self.ever_ready = False
        self.input_stopped = None
        self.world_progressed = False
        self.clicks = 0
        self.last_click_sample = -1
        self.pre_event = None
        self.neutral_sample = None
        self.next_click = 0
        args.stage, args.profiles, args.capture, args.menu_batches = None, None, None, None
        args.pickup_first, args.step_ms = False, 0
        super().__init__(args)
        # Never widen the installed transport's ten-second instance lifetime.
        self.hard_deadline = min(self.hard_deadline, args.overall_deadline_ns)
        self.deadline = min(self.deadline, self.hard_deadline - RELEASE_RESERVE_NS)
        self.stage_deadline = self.deadline
        self.tail = NativeTail(self.folder / "session-telemetry.jsonl")
        self.artifacts.save("resume-invocation.json", dict(maximumDurationNs=args.timeout_seconds * NS,
            maximumClicks=args.max_clicks, maximumNativeTailBytes=TAIL_BYTES,
            maximumNativeAgeNs=MAX_AGE_NS, retryDelayNs=RETRY_NS, pose=self.pose,
            driverSha256=transport.digest(Path(__file__).read_bytes()),
            transportSha256=transport.digest(Path(transport.__file__).read_bytes()),
            nativeWidgetAcceptance=False, headsetAcceptance=False))
        self.bounds(time.perf_counter_ns())

    def bounds(self, now):
        require(now < self.deadline, "Resume observation deadline reached")
        require(self.clicks <= self.args.max_clicks, "Resume click budget exceeded")

    def observe(self):
        self.bounds(time.perf_counter_ns())
        state = self.read_state()  # Exact process/session/focus/tracking identities.
        self.ownership(state)     # Exact command generation and exclusive writer.
        current = observation(self.tail.read(), time.perf_counter_ns())
        before = self.latest
        if before is not None:
            for key in ("sampleNs", "elapsedMs", "worldCalls", "menuInputs", "menuPublished"):
                require(current[key] >= before[key], "native telemetry regressed: " + key)
            if current["sampleNs"] == before["sampleNs"]:
                require(current == before, "native telemetry changed within one sample")
        if current["worldCalls"] > 0:
            self.input_stopped = "world observed"
            if before and current["sampleNs"] > before["sampleNs"] and current["worldCalls"] > before["worldCalls"]:
                self.world_progressed = True
        elif (self.ever_ready or self.reviewed_menu_required) and not current["menuReady"]:
            # Ready is gated by native Menu display. False is NOT proof of loading,
            # but is sufficient to suppress every further press, even if it recovers.
            self.input_stopped = "menu readiness lost; possible loading"
        if current["menuReady"]:
            require(current["menuGraphicsFailures"] == 0, "native menu graphics failed")
            self.ever_ready = True
        self.latest = current
        if before != current:
            self.artifacts.event(dict(type="resume_native_observation", observedNs=time.perf_counter_ns(),
                                      native=current, inputStopped=self.input_stopped))
        return current

    def validate_publication(self, lines, expected, cleanup):
        if lines == ["trigger r 0"] and self.held_trigger:
            # Release only our settled press, even if loading/world began after down.
            require(time.perf_counter_ns() + 250_000_000 < self.hard_deadline,
                    "insufficient bounded time for owned trigger release")
            return
        require(not cleanup, "Resume cleanup requires a settled owned trigger")
        require(lines in (self.neutral_commands(), ["trigger r 1"]),
                "Resume permits only its fixed ray and ordinary trigger edges")
        current = self.observe()  # Last check inside send, immediately before publication.
        if self.input_stopped:
            raise MenuTransition(self.input_stopped)
        require(current["menuReady"], "fresh native menu readiness required")
        require(self.pre_event is not None and current["sampleNs"] >= self.pre_event["sampleNs"],
                "missing fresh pre-event native receipt")
        if lines == ["trigger r 1"]:
            require(self.clicks < self.args.max_clicks, "Resume click budget exhausted")
            require(current["sampleNs"] > self.last_click_sample,
                    "Resume retry lacks a fresh native sample")
            require(time.perf_counter_ns() + NS < self.deadline,
                    "insufficient bounded time for a complete click")

    def event(self, name, lines):
        self.pre_event = self.observe()
        self.artifacts.event(dict(type="resume_event_before", event=name, native=self.pre_event,
                                  nativeWidgetAcceptance=False))
        self.send(lines)
        self.artifacts.event(dict(type="resume_event_acknowledged", event=name,
                                  commandSequence=self.sequence, committedNs=self.last_commit,
                                  nativeWidgetAcceptance=False))

    def resume(self):
        # Do not change caller-held inputs or let a pre-existing trigger become a shot.
        controls = self.state["controls"]
        require(all(float(controls[key]) == 0 for key in ("trigR", "trigL", "gripR", "gripL")),
                "Resume requires neutral caller trigger/grip controls")
        require(not any(controls[key] for key in ("a", "b", "x", "y", "menu", "clickL", "clickR"))
                and controls["stickL"] == controls["stickR"] == [0, 0],
                "Resume requires neutral caller buttons/sticks")
        while True:
            if time.perf_counter_ns() + NS >= self.deadline:
                require(not self.held_trigger and not self.pending_command,
                        "cannot hand off an unsettled or held Resume command")
                return False  # Re-admit exact ownership in another bounded episode.
            current = self.observe()
            if self.world_progressed:
                return True
            if (not self.input_stopped and current["menuReady"]
                    and self.clicks < self.args.max_clicks and time.perf_counter_ns() >= self.next_click):
                try:
                    if self.neutral_sample is None:
                        self.event("neutral-ray", self.neutral_commands())
                        self.neutral_sample = self.last_commit
                    elif (current["sampleNs"] > max(self.neutral_sample, self.last_click_sample)
                          and current["menuInputs"] > self.pre_event["menuInputs"]
                          and current["menuPublished"] > self.pre_event["menuPublished"]):
                        self.event("trigger-down", ["trigger r 1"])
                        self.clicks += 1
                        self.last_click_sample = self.pre_event["sampleNs"]
                        # Preserve feedback67's bounded 200ms press, ending early
                        # on observed transition. No further down follows a transition.
                        hold_until = self.last_commit + 200_000_000
                        while time.perf_counter_ns() < hold_until:
                            self.observe()
                            if self.input_stopped:
                                break
                            remaining = hold_until - time.perf_counter_ns()
                            if remaining > 0:
                                time.sleep(min(POLL_SECONDS, remaining / NS))
                        self.event("trigger-up", ["trigger r 0"])
                        self.next_click = self.last_commit + RETRY_NS
                except MenuTransition:
                    pass
            time.sleep(POLL_SECONDS)


def reviewed_menu_geometry(capture, pixel):
    """Invert a reviewed actual452 left-eye pixel through its full menu quad.

    MenuPointer.h uses a yaw-only head anchor, -1.5 m depth and 2.2 m width.
    OpenXrMenu submits the complete raster; native DispatchMenuRasterPointer
    performs viewport conversion. Do not remove the raster's black letterbox.
    Only this reviewed initial head/panel/FOV configuration is supported.
    """
    require(capture["sessionState"] == "FOCUSED"
            and capture["head"]["pos"] == [0, 1.6, 0]
            and capture["head"]["quat"] == [0, 0, 0, 1], "unsupported captured head frame")
    panel = capture["layers"][0]
    require(panel["i"] == 0 and panel["type"] == "quad" and panel["space"] == "stage"
            and panel["pose"] == [0, 1.6, -1.5] and panel["sizeM"] == [2.2, 1.65],
            "unsupported menu panel; a fresh reviewed full-raster capture is required")
    require(64 <= integer(capture["width"]) <= 1024
            and 64 <= integer(capture["height"]) <= 768
            and capture["width"] * 3 == capture["height"] * 4, "unsupported capture aspect")
    left = capture["views"][0]
    require(left["eye"] == "left" and left["pos"] == [-.0315, 1.6, 0]
            and left["fovDeg"] == dict(l=-54.431, r=42.972, u=48.701, d=-48.701),
            "unsupported captured eye projection")
    fov = {k: math.tan(math.radians(v)) for k, v in left["fovDeg"].items()}
    # Pixel centers; retain the whole 4:3 surface including native letterbox.
    sx, sy = (pixel[0] + .5) / 640, (pixel[1] + .5) / 480
    x = left["pos"][0] + 1.5 * (fov["l"] + sx * (fov["r"] - fov["l"]))
    y = left["pos"][1] + 1.5 * (fov["u"] - sy * (fov["u"] - fov["d"]))
    dx, dy, dz = x - .2, y - 1.3, -1.5 + .35
    yaw = math.degrees(math.atan2(-dx, -dz))
    pitch = math.degrees(math.atan2(dy, math.hypot(dx, dz)))
    return dict(pose=f"hand r grip pose 0.2 1.3 -0.35 {yaw:.9f} {pitch:.9f} 0",
                targetMeters=[x, y, -1.5], rasterUv=[.5 + x / 2.2, .5 - (y - 1.6) / 1.65],
                referencePixel=list(pixel), referenceImageSize=[640, 480], nativeWidgetAcceptance=False)


def single_player_geometry(capture):
    """Main tab: left (354,174), corroborated by right (214,174)."""
    return reviewed_menu_geometry(capture, (354, 174))


def dropdown_resume_geometry(capture):
    """Actual452 open dropdown, second item RESUME GAME; both views reviewed."""
    require(len(capture["views"]) == 2, "dropdown capture requires both eye views")
    right = capture["views"][1]
    require(right["eye"] == "right" and right["pos"] == [.0315, 1.6, 0]
            and right["fovDeg"] == dict(l=-42.972, r=54.431, u=48.701, d=-48.701),
            "unsupported captured right eye projection")
    geometry = reviewed_menu_geometry(capture, (349, 186))
    geometry["referenceRightPixel"] = [209, 186]
    return geometry


class ReviewedMenuDriver(ResumeDriver):
    """Share capture admission, full-quad pointing and owned-release guards."""
    def __init__(self, args):
        run = transport.local_path(args.run)
        path = transport.child(run, run / getattr(args, self.capture_argument))
        require(path.parent == run / "sim" / "capture" and path.suffix == ".json",
                "menu capture must belong to this exact run's sim/capture directory")
        self.menu_capture, raw = transport.read_json(path, 65536)
        require(self.menu_capture["name"] == path.stem, "capture name/path mismatch")
        geometry = self.geometry(self.menu_capture)
        self.pose = geometry["pose"]
        super().__init__(args)
        self.validate_capture_age()
        require(integer(self.startup["settled_state"]["frame"]) <= integer(self.menu_capture["frameIndex"])
                <= integer(self.state["frame"]), "menu capture frame is outside the current run")
        require(self.menu_capture["width"] == self.capture_plan["width"]
                and self.menu_capture["height"] == self.capture_plan["height"], "capture budget dimensions changed")
        self.artifacts.save(self.calibration_name, dict(geometry, capturePath=str(path),
            captureSha256=transport.digest(raw), hoverOnly=args.single_player_hover_only,
            evidence=self.capture_evidence))

    def neutral_commands(self):
        return ["hand r follow off", "hand r aimtrim 0 0", self.pose, "trigger r 0"]

    def validate_capture_age(self):
        require(0 <= time.perf_counter_ns() - integer(self.menu_capture["displayTimeNs"]) <= 60 * NS,
                "menu capture is stale or future-dated; ROOT must capture/review both current menu views")

    def wait_initial_menu_readiness(self):
        """Passively admit an initial sample, using this driver's original budget."""
        require(self.commands == self.clicks == 0 and not self.pending_command
                and not self.held_trigger and not self.unsafe_owner,
                "initial menu readiness wait requires untouched owned input")
        while True:
            current = self.observe()  # Retains owner, stop, freshness and monotonic checks.
            require(not self.input_stopped, "menu changed before initial input; review required")
            require(current["menuGraphicsFailures"] == 0, "native menu graphics failed")
            self.validate_capture_age()
            now = time.perf_counter_ns()
            self.bounds(now)
            if current["menuReady"]:
                return
            # A fresh false initial sample authorizes observation only. Never
            # catch a refusal, renew the driver/capture lease, or publish input.
            time.sleep(min(POLL_SECONDS, (self.deadline - now) / NS))

    def validate_publication(self, lines, expected, cleanup):
        super().validate_publication(lines, expected, cleanup)
        if lines == ["trigger r 0"]:
            return  # Preserve owned release even after head/menu/world changes.
        state = self.read_state()
        self.ownership(state)
        require(state["head"]["pos"] == self.menu_capture["head"]["pos"]
                and state["head"]["ypr"] == [0, 0, 0]
                and state["handR"]["pos"] == [.2, 1.3, -.35]
                and state["handR"]["followsHead"] is False, "captured pointing frame changed")


class SinglePlayerDriver(ReviewedMenuDriver):
    """One ordinary tab click; use the existing owner, native tail and transport."""
    capture_argument = "single_player_capture"
    geometry = staticmethod(single_player_geometry)
    calibration_name = "single-player-calibration.json"
    capture_evidence = "actual452 reviewed Main tab; projected pixel is geometry, not widget acceptance"

    def resume(self):
        controls = self.state["controls"]
        require(all(float(controls[k]) == 0 for k in ("trigR", "trigL", "gripR", "gripL"))
                and not any(controls[k] for k in ("a", "b", "x", "y", "menu", "clickL", "clickR"))
                and controls["stickL"] == controls["stickR"] == [0, 0], "tab selection requires neutral input")
        self.wait_initial_menu_readiness()
        self.event("single-player-neutral-ray", self.neutral_commands())
        neutral_ns, before = self.last_commit, self.pre_event
        while True:
            current = self.observe()
            require(not self.input_stopped, "menu changed before tab selection; review required")
            if (current["sampleNs"] > neutral_ns and current["menuInputs"] > before["menuInputs"]
                    and current["menuPublished"] > before["menuPublished"]):
                break
            time.sleep(POLL_SECONDS)
        if self.args.single_player_hover_only:
            return True
        self.event("single-player-trigger-down", ["trigger r 1"])
        self.clicks += 1
        hold_until = self.last_commit + 200_000_000
        while time.perf_counter_ns() < hold_until:
            self.observe()
            if self.input_stopped:
                break
            remaining = hold_until - time.perf_counter_ns()
            if remaining > 0:
                time.sleep(min(POLL_SECONDS, remaining / NS))
        self.event("single-player-trigger-up", ["trigger r 0"])
        require(not self.pending_command and not self.held_trigger, "tab release did not settle")
        # A tab transition can retire menu readiness. The owned press/release
        # was delivered; its destination is still for ROOT's next image review.
        return True  # Input delivery only. ROOT captures/reviews before a separate Resume invocation.


class DropdownResumeDriver(ReviewedMenuDriver):
    """One reviewed dropdown click; inherit bounded native-world observation."""
    capture_argument = "dropdown_resume_capture"
    geometry = staticmethod(dropdown_resume_geometry)
    calibration_name = "dropdown-resume-calibration.json"
    capture_evidence = "actual452 reviewed RESUME GAME dropdown item; geometry is not widget acceptance"
    # The reviewed dropdown is already open. Any readiness loss, including the
    # first live observation, retires its authority to admit new input forever.
    reviewed_menu_required = True

    def __init__(self, args):
        args.max_clicks = 1
        super().__init__(args)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="exact already-live runner output directory")
    parser.add_argument("--timeout-seconds", type=int, default=35)
    parser.add_argument("--max-clicks", type=int, default=3)
    capture_mode = parser.add_mutually_exclusive_group()
    capture_mode.add_argument("--single-player-capture", type=Path,
                        help="reviewed current Main menu capture JSON inside this run; one tab click only")
    capture_mode.add_argument("--dropdown-resume-capture", type=Path,
                        help="current open Single Player dropdown JSON; ROOT must review both images; one Resume click")
    parser.add_argument("--single-player-hover-only", action="store_true",
                        help="with --single-player-capture, leave a neutral ray for ROOT capture/review")
    args = parser.parse_args(argv)
    if args.single_player_capture:
        args.max_clicks, args.timeout_seconds = 1, min(args.timeout_seconds, 10)
    elif args.dropdown_resume_capture:
        args.max_clicks = 1
    started = time.perf_counter_ns()
    args.overall_deadline_ns = started + min(args.timeout_seconds, 35) * NS
    driver = None
    result = dict(schema="bc2vr.resume-driver.v1", status="refused", nativeWidgetAcceptance=False,
                  headsetAcceptance=False, evidence="native world counter progression; simulator receipts are input delivery only")
    if args.single_player_capture:
        result.update(schema="bc2vr.single-player-selection.v1",
                      evidence="one ordinary tab selection/hover; ROOT must capture/review before Resume")
    elif args.dropdown_resume_capture:
        result.update(schema="bc2vr.dropdown-resume.v1",
                      evidence="one reviewed dropdown Resume click; success requires native world counter progression")
    code = 2
    carry = None
    identity = None
    native_tail = None
    episodes = []
    try:
        require(not args.single_player_hover_only or args.single_player_capture,
                "hover-only requires --single-player-capture")
        driver_type = (SinglePlayerDriver if args.single_player_capture else
                       DropdownResumeDriver if args.dropdown_resume_capture else ResumeDriver)
        for _ in range(1 if args.single_player_capture else 4):
            require(time.perf_counter_ns() + 2 * NS < args.overall_deadline_ns,
                    "overall Resume observation budget exhausted without world progression")
            driver = driver_type.__new__(driver_type)
            driver.__init__(args)
            current_identity = dict(manifest=driver.manifest, link=driver.link, startup=driver.startup)
            require(identity is None or current_identity == identity,
                    "run/process/session generation changed across Resume episodes")
            identity = current_identity
            if native_tail is not None:
                require(driver.tail.path == native_tail.path, "native telemetry path changed across episodes")
                driver.tail = native_tail  # Preserve file identity and size regression guards.
            if carry:
                for key, value in carry.items():
                    setattr(driver, key, value)
            episodes.append(str(driver.artifacts.directory))
            completed = driver.resume()
            if completed:
                result["status"] = (("hover_ready_for_review" if args.single_player_hover_only
                                     else "single_player_click_delivered")
                                    if args.single_player_capture else "world_progressed")
                code = 0
                break
            carry = {key: getattr(driver, key) for key in (
                "latest", "pre_event", "ever_ready", "input_stopped", "world_progressed", "clicks",
                "last_click_sample", "neutral_sample", "next_click")}
            native_tail = driver.tail
            driver.artifacts.save("resume-episode.json", dict(status="observation_handoff", state=carry,
                durationNs=time.perf_counter_ns() - driver.started, pendingCommand=driver.pending_command,
                triggerHeld=driver.held_trigger, nativeWidgetAcceptance=False), final=True)
            driver.close()
        require(code == 0, "bounded Resume episodes exhausted without world progression")
    except (Exception, KeyboardInterrupt) as error:
        result["error"] = str(error)
        if driver is not None and getattr(driver, "artifacts", None):
            try:
                result["release"] = driver.release_after_failure()
            except Exception as cleanup_error:
                result["release"] = "refused: " + str(cleanup_error)
    finally:
        if driver is not None:
            if getattr(driver, "artifacts", None):
                result.update(durationNs=time.perf_counter_ns() - started, clicks=driver.clicks,
                    commands=driver.commands, pendingCommand=driver.pending_command,
                    triggerHeld=driver.held_trigger, native=driver.latest,
                    inputStopped=driver.input_stopped, artifactDirectory=str(driver.artifacts.directory),
                    episodeArtifactDirectories=episodes)
                try:
                    name = ("single-player-result.json" if args.single_player_capture else
                            "dropdown-resume-result.json" if args.dropdown_resume_capture else "resume-result.json")
                    driver.artifacts.save(name, result, final=True)
                except (REFUSAL, OSError) as write_error:
                    result["resultWriteError"], code = str(write_error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
