"""ROOT-RUN-ONLY ordinary XR Resume helper for an already owned live run.

python -B tools/resume_bc2_simulation.py --run G:/.../run01
At most three unchanged Resume-ray clicks and 35 seconds including cleanup.
Simulator acknowledgements prove input delivery, never native widget acceptance.
Success requires fresh native world_calls progression. No launch or capture.
"""
import argparse
import importlib.util
import json
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
            maximumNativeAgeNs=MAX_AGE_NS, retryDelayNs=RETRY_NS, pose=RESUME_POSE,
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
        elif self.ever_ready and not current["menuReady"]:
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
        require(lines in ([RESUME_POSE, "trigger r 0"], ["trigger r 1"]),
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
                        self.event("neutral-ray", [RESUME_POSE, "trigger r 0"])
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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True, help="exact already-live runner output directory")
    parser.add_argument("--timeout-seconds", type=int, default=35)
    parser.add_argument("--max-clicks", type=int, default=3)
    args = parser.parse_args(argv)
    started = time.perf_counter_ns()
    args.overall_deadline_ns = started + min(args.timeout_seconds, 35) * NS
    driver = None
    result = dict(schema="bc2vr.resume-driver.v1", status="refused", nativeWidgetAcceptance=False,
                  headsetAcceptance=False, evidence="native world counter progression; simulator receipts are input delivery only")
    code = 2
    carry = None
    identity = None
    native_tail = None
    episodes = []
    try:
        for _ in range(4):
            require(time.perf_counter_ns() + 2 * NS < args.overall_deadline_ns,
                    "overall Resume observation budget exhausted without world progression")
            driver = ResumeDriver.__new__(ResumeDriver)
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
                result["status"] = "world_progressed"
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
                    driver.artifacts.save("resume-result.json", result, final=True)
                except (REFUSAL, OSError) as write_error:
                    result["resultWriteError"], code = str(write_error), 2
            driver.close()
    print(json.dumps(result, separators=(",", ":"), allow_nan=False))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
