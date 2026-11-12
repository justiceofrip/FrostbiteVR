"""Record ROOT's explicit current stereo review for magazine/bolt/fixed firing.

Requires the exact metadata and BOTH image hashes plus the operator's clear-shot
decision. This records a review; it never inspects pixels, issues commands,
creates a deadline or renews native evidence. The action driver independently
admits fresh native state under its original action clock.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
SOURCES = (
    "record_bc2_fire_clearance.py", "drive_bc2_magazine_bolt_stimulus.py",
    "bc2_magazine_bolt_stimulus.py", "drive_bc2_reload_stimulus.py",
    "drive_bc2_feed_simulation.py", "plan_bc2_feed_simulation.py",
)


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def read(path):
    with Path(path).open("rb") as stream:
        raw = stream.read(4 * 1024 * 1024 + 1)
    require(len(raw) <= 4 * 1024 * 1024, "bounded JSON required")
    return json.loads(raw.decode("utf-8-sig"))


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())


def load_driver(sources):
    path = HERE / "drive_bc2_magazine_bolt_stimulus.py"
    spec = importlib.util.spec_from_file_location("fire_clearance_driver", path)
    driver = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(driver)
    for module in (driver, driver.geometry, driver.ordinary, driver.transport,
                   driver.geometry.frame, driver.transport.planner):
        path = Path(module.__file__).resolve(strict=True)
        require(path.parent == HERE and path.name in sources
                and pin(path) == sources[path.name], "sibling source changed or escaped tools")
    return driver


# The three stereo-binding helpers retain the reviewed continuation's checks.
# They have no report imports and confer no input authority.
def close_numbers(a, b, tolerance):
    return len(a) == len(b) == 3 and all(
        isinstance(x, (float, int)) and not isinstance(x, bool) and math.isfinite(x)
        and isinstance(y, (float, int)) and not isinstance(y, bool) and math.isfinite(y)
        and abs(x-y) <= tolerance for x, y in zip(a, b))


def review(run, metadata_path, digest, transport, host):
    metadata_path = transport.child(run, metadata_path)
    require(metadata_path.parent == run / 'sim/capture' and metadata_path.suffix == '.json',
            'review must identify this run\'s actual capture metadata')
    require(pin(metadata_path)['sha256'] == digest, 'reviewed stereo metadata changed')
    capture = read(metadata_path)
    state, _ = transport.read_json(run / 'sim/state.json')
    require(state['pid'] == host['pid'] and state['runtime'] == 'xr-sim'
            and state['sessionRunning'] is True and state['sessionState'] == 'FOCUSED'
            and state['lastCmdError'] == '' and state['errors'] == 0,
            'current owned simulator must be running and focused')
    require(transport.same_path(state['lastCapture'], metadata_path.with_suffix(''))
            and capture['name'] == metadata_path.stem and capture['sessionState'] == 'FOCUSED'
            and 0 < capture['frameIndex'] <= state['frame'], 'current capture identity differs')
    require(state['head']['valid'] and state['handR']['valid']
            and capture['handR']['valid']
            and close_numbers(state['head']['pos'], capture['head']['pos'], 0.00011)
            and close_numbers(state['head']['ypr'], capture['head']['ypr'], 0.011)
            and close_numbers(state['handR']['pos'], capture['handR']['grip'], 0.00011)
            and close_numbers(state['handR']['ypr'], capture['handR']['aimYpr'], 0.011),
            'head/right aim differs from ROOT-reviewed current pair')
    for key, value in capture['controls'].items():
        require(key in state['controls'] and state['controls'][key] == value,
                'controls differ from ROOT-reviewed current pair')
    images = [pin(metadata_path.with_name(metadata_path.stem+'_'+eye+'.png'))
              for eye in ('left', 'right')]
    return dict(metadata=pin(metadata_path), images=images, host=host,
                head=state['head'], handR=state['handR'], captureSeq=state['captureSeq'])


def unchanged_review(run, record, transport):
    for item in [record['metadata'], *record['images']]:
        require(pin(item['path']) == item, 'reviewed pair changed')
    state, _ = transport.read_json(run / 'sim/state.json')
    require(state['pid'] == record['host']['pid'] and state['sessionRunning'] is True
            and state['sessionState'] == 'FOCUSED' and state['head'] == record['head']
            and state['handR'] == record['handR'] and state['captureSeq'] == record['captureSeq'],
            'head/right aim/session or reviewed capture changed during continuation')


def running_session(run, link, manifest):
    require(not (run / "result.json").exists() and not Path(link["stop_request"]).exists(),
            "session stopped or retired")
    session = read(link["session"])
    require(session["state"] == "running" and session["host"] == link["host"]
            and session["supervisor"] == link["supervisor"]
            and session["source_sha256"] == manifest["source_sha256"]
            and session["version"] == manifest["version"], "linked running session identity changed")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("run", "capture", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    for key in ("capture", "left", "right"):
        parser.add_argument("--" + key + "-sha256", required=True)
    parser.add_argument("--root-reviewed-both-eyes-clear-shot", action="store_true", required=True)
    args = parser.parse_args(argv)
    reviewed_ns = time.perf_counter_ns()
    sources = {name: pin(HERE / name) for name in SOURCES}
    require(all(Path(row["path"]).parent == HERE for row in sources.values()),
            "source provenance must contain sibling tools only")
    driver = load_driver(sources)
    transport, geometry = driver.transport, driver.geometry
    run = transport.local_path(args.run)
    output = transport.child(run, args.output)
    require(output.parent == run and output.suffix == ".json", "new run-root JSON output required")
    require(not output.exists(), "clearance output already exists")
    link, manifest = read(run / "session-link.json"), read(run / "run-manifest.json")
    host = link["host"]
    running_session(run, link, manifest)
    reviewed = review(run, args.capture, args.capture_sha256, transport, host)
    state, capture = read(run / "sim/state.json"), read(args.capture)
    require(sorted(v["eye"] for v in capture["views"]) == ["left", "right"], "stereo metadata required")
    require(state["head"] == reviewed["head"] and state["handR"] == reviewed["handR"]
            and state["captureSeq"] == reviewed["captureSeq"], "state changed while binding review")
    require(0 < transport.integer(capture["displayTimeNs"]) <= reviewed_ns, "original capture clock invalid")
    for side in ("L", "R"):
        hand, rendered = state["hand" + side], capture["hand" + side]
        require(hand["valid"] and rendered["valid"]
                and close_numbers(hand["pos"], rendered["grip"], .00011)
                and close_numbers(hand["ypr"], rendered["aimYpr"], .011),
                "current hand differs from reviewed images")
    driver.controls_allowed(state["controls"])
    require(state["controls"]["trigR"] == 0, "trigger must be released")
    require(all(state["controls"][k] == v for k, v in capture["controls"].items()),
            "current controls differ from captured controls")
    images = []
    for eye, expected in (("left", args.left_sha256), ("right", args.right_sha256)):
        row = pin(Path(reviewed["metadata"]["path"]).with_name(capture["name"] + "_" + eye + ".png"))
        require(row["sha256"] == expected, "explicit reviewed " + eye + " image changed")
        images.append(dict(row, eye=eye))
    native_path = transport.local_path(link["folder"]) / "mechanism-snapshot.json"
    with transport.open_snapshot(native_path) as stream:
        raw = stream.read(4 * 1024 * 1024 + 1)
    require(len(raw) <= 4 * 1024 * 1024, "native observation exceeds bound")
    snapshot = json.loads(raw.decode("utf-8-sig"))
    observed_ns = transport.integer(snapshot["observed_ns"])
    require(snapshot["schema"] == "fvr.bc2.live-mechanism-snapshot.v1"
            and snapshot["valid"] is True and 0 < observed_ns <= time.perf_counter_ns()
            and snapshot["selected"]["configurationPathVerified"] is True,
            "actual verified native producer observation required")
    for pose in (snapshot["input"]["referenceHead"], snapshot["input"]["hands"][1]["aim"]):
        geometry.frame.from_pose(pose)
    writer_path, command_path = run / "live-driver-owner.json", run / "sim/command.txt"
    writer_pin, writer, command_pin = pin(writer_path), read(writer_path), pin(command_path)
    require(writer["host"] == host and writer["pending"] is False
            and writer["sequence"] == state["cmdSeq"] and writer["sentinel"] == state["lastCmd"]
            and writer["commandSha256"] == command_pin["sha256"],
            "current command custody is unsettled or different")
    value = dict(schema="bc2vr.root-fire-clearance.v1", rootReviewedVisibleCorridor=True,
        host=host, owner=snapshot["owner"], configurationPath=snapshot["selected"]["configurationPath"],
        rig=snapshot["rig"], reviewedNs=reviewed_ns, referenceHead=snapshot["input"]["referenceHead"],
        rightAim=snapshot["input"]["hands"][1]["aim"], captures=images,
        **{key: state[key] for key in ("head", "handL", "handR", "controls")})
    value["provenance"] = dict(metadata=reviewed["metadata"],
        originalCaptureDisplayTimeNs=capture["displayTimeNs"],
        observation=dict(path=str(native_path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest()),
        observedNs=observed_ns, writer=writer_pin, sources=sources,
        noDeadlineCreatedOrRenewed=True, retainedObservationOnly=True,
        nativeAcceptance=False, headsetAcceptance=False, freshNativeAdmissionRequiredByActionDriver=True)
    unchanged_review(run, reviewed, transport)
    final = read(run / "sim/state.json")
    require(all(final[k] == state[k] for k in
                ("pid", "head", "handL", "handR", "controls", "cmdSeq", "captureSeq", "lastCmd"))
            and pin(writer_path) == writer_pin and pin(command_path) == command_pin,
            "pose, controls or command owner changed during binding")
    require(read(run / "session-link.json") == link and read(run / "run-manifest.json") == manifest,
            "linked session or manifest changed during binding")
    running_session(run, link, manifest)
    for row in sources.values():
        require(pin(row["path"]) == row, "reviewed source changed during binding")
    raw = (json.dumps(value, indent=2, allow_nan=False) + "\n").encode("utf-8")
    require(len(raw) <= 16384, "consumer16KiB bound exceeded")
    with output.open("xb") as stream:
        stream.write(raw)
    print(json.dumps(dict(status="explicit_root_review_recorded", clearance=pin(output),
                         commandAuthority=False, nativeAcceptance=False, headsetAcceptance=False)))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (IndexError, KeyError, OSError, RuntimeError, TypeError, ValueError) as error:
        print(json.dumps(dict(status="refused", error=str(error), commandAuthority=False)), file=sys.stderr)
        raise SystemExit(2)
