"""Synthetic recorder/consumer contracts; no native or headset acceptance."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def load(path):
    spec = importlib.util.spec_from_file_location("fire_clearance_recording_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


recorder = load(ROOT / "tools/record_bc2_fire_clearance.py")


def write(path, value):
    path.write_text(json.dumps(value, allow_nan=False), encoding="utf-8")


class FireClearanceRecordingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.tools = self.root / "tools"
        self.tools.mkdir()
        for name in recorder.SOURCES:
            shutil.copyfile(ROOT / "tools" / name, self.tools / name)
        self.record = load(self.tools / "record_bc2_fire_clearance.py")
        self.run = self.root / "run"
        (self.run / "sim/capture").mkdir(parents=True)
        native = self.root / "native"
        native.mkdir()
        self.capture_path = self.run / "sim/capture/pair.json"
        self.output = self.run / "clearance.json"
        self.host = dict(pid=4242, started="synthetic-host")
        self.link = dict(host=self.host, supervisor=dict(pid=4343), folder=str(native),
                         session=str(self.root / "session.json"), stop_request=str(self.root / "stop"))
        self.manifest = dict(source_sha256="a" * 64, version="synthetic-test")
        self.session = dict(state="running", host=self.host, supervisor=self.link["supervisor"],
                            **self.manifest)
        controls = {key: False for key in ("a", "b", "x", "y", "menu", "clickL", "clickR", "restL", "restR")}
        controls.update(trigL=0, trigR=0, gripL=0, gripR=1, stickL=[0, 0], stickR=[0, 0])
        hand = dict(valid=True, pos=[0., 1., 0.], ypr=[0., 0., 0.], followsHead=False)
        self.state = dict(pid=4242, runtime="xr-sim", sessionRunning=True, sessionState="FOCUSED",
            lastCmdError="", errors=0, lastCapture=str(self.capture_path.with_suffix("")), frame=20,
            head=copy.deepcopy(hand), handL=copy.deepcopy(hand), handR=copy.deepcopy(hand),
            controls=controls, captureSeq=3, cmdSeq=7, lastCmd="owned-sentinel")
        self.capture = dict(name="pair", sessionState="FOCUSED", frameIndex=19, displayTimeNs=1,
            head=copy.deepcopy(hand), controls=copy.deepcopy(controls), views=[dict(eye="left"), dict(eye="right")],
            **{key: dict(valid=True, grip=hand["pos"], aimYpr=hand["ypr"]) for key in ("handL", "handR")})
        pose = dict(position=[0., 1., 0.], orientation=[0., 0., 0., 1.])
        self.snapshot = dict(schema="fvr.bc2.live-mechanism-snapshot.v1", valid=True, observed_ns=1,
            owner=dict(player=1, soldier=2, weapon=3, actorGeneration=4, equipGeneration=5, space=6),
            selected=dict(configurationPathVerified=True, configurationPath="synthetic/magazine"),
            rig=dict(fingerprint=10, pose=11), input=dict(referenceHead=copy.deepcopy(pose),
            hands=[dict(aim=copy.deepcopy(pose)), dict(aim=copy.deepcopy(pose))]))
        self.command = self.run / "sim/command.txt"
        self.command.write_bytes(b"synthetic settled command\n")
        self.writer = dict(host=self.host, pending=False, sequence=7, sentinel="owned-sentinel",
                           commandSha256=self.record.pin(self.command)["sha256"])
        self.save_fixture()
        for eye in ("left", "right"):
            # Bytes are intentionally not decoded: explicit human review, never pixel inference.
            self.capture_path.with_name("pair_" + eye + ".png").write_bytes(("synthetic " + eye).encode())
        self.argv = ["--run", str(self.run), "--capture", str(self.capture_path), "--output", str(self.output),
            "--capture-sha256", self.record.pin(self.capture_path)["sha256"],
            "--left-sha256", self.record.pin(self.capture_path.with_name("pair_left.png"))["sha256"],
            "--right-sha256", self.record.pin(self.capture_path.with_name("pair_right.png"))["sha256"],
            "--root-reviewed-both-eyes-clear-shot"]

    def save_fixture(self):
        for path, value in ((self.run / "session-link.json", self.link),
                            (self.run / "run-manifest.json", self.manifest),
                            (Path(self.link["session"]), self.session),
                            (self.run / "sim/state.json", self.state), (self.capture_path, self.capture),
                            (Path(self.link["folder"]) / "mechanism-snapshot.json", self.snapshot),
                            (self.run / "live-driver-owner.json", self.writer)):
            write(path, value)

    def invoke(self, argv=None):
        with mock.patch("sys.stdout", new_callable=io.StringIO):
            return self.record.main(self.argv if argv is None else argv)

    def refused(self, reason, argv=None):
        with self.assertRaisesRegex((RuntimeError, ValueError), reason):
            self.invoke(argv)
        self.assertFalse(self.output.exists())

    def consumer(self):
        return self.record.load_driver({name: self.record.pin(self.tools / name) for name in self.record.SOURCES})

    def test_complete_cli_without_reports_and_current_consumer(self):
        self.assertFalse((self.root / "reports").exists())
        before = {p: p.read_bytes() for p in self.root.rglob("*") if p.is_file()}
        result = subprocess.run([sys.executable, "-B", str(self.tools / "record_bc2_fire_clearance.py"), *self.argv],
                                cwd=self.root, text=True, capture_output=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        receipt = json.loads(result.stdout)
        self.assertEqual(receipt["status"], "explicit_root_review_recorded")
        self.assertFalse(receipt["commandAuthority"])
        clearance, raw = self.consumer().transport.read_json(self.output, 16384)
        self.assertLessEqual(len(raw), 16384)
        now = time.perf_counter_ns()
        self.assertEqual(self.consumer().geometry.fire_clearance(clearance, self.snapshot, self.state,
                         self.host, now, now + 1_000_000_000), 100_000_000)
        provenance = clearance["provenance"]
        self.assertEqual(provenance["observedNs"], 1)
        self.assertEqual(provenance["originalCaptureDisplayTimeNs"], 1)
        self.assertTrue(provenance["noDeadlineCreatedOrRenewed"])
        self.assertTrue(provenance["retainedObservationOnly"])
        self.assertTrue(provenance["freshNativeAdmissionRequiredByActionDriver"])
        self.assertFalse(provenance["nativeAcceptance"])
        self.assertFalse(provenance["headsetAcceptance"])
        self.assertEqual(set(provenance["sources"]), set(recorder.SOURCES))
        for item in provenance["sources"].values():
            self.assertEqual(Path(item["path"]).parent, self.tools)
            self.assertEqual(item, self.record.pin(item["path"]))
        for path, data in before.items():
            self.assertEqual(path.read_bytes(), data, str(path))
        self.assertEqual({p for p in self.root.rglob("*") if p.is_file()}, set(before) | {self.output})

    def test_all_shared_family_clearances_and_original_action_deadline(self):
        geometry = self.consumer().geometry
        for family in ("magazine", "bolt", "fixed"):
            with self.subTest(family=family):
                self.snapshot["selected"]["configurationPath"] = "synthetic/" + family
                self.save_fixture()
                argv = list(self.argv)
                output = self.run / (family + ".json")
                argv[argv.index("--output") + 1] = str(output)
                self.assertEqual(self.invoke(argv), 0)
                clearance = self.record.read(output)
                now = time.perf_counter_ns()
                self.assertEqual(geometry.fire_clearance(clearance, self.snapshot, self.state,
                                 self.host, now, now + 100), 100_000_000)
                with self.assertRaisesRegex(ValueError, "original bounded action episode expired"):
                    geometry.fire_clearance(clearance, self.snapshot, self.state, self.host, now, now)

    def test_explicit_both_eye_decision_and_all_hashes_are_required(self):
        for flag in ("--root-reviewed-both-eyes-clear-shot", "--capture-sha256", "--left-sha256", "--right-sha256"):
            with self.subTest(flag=flag), mock.patch("sys.stderr", new_callable=io.StringIO):
                argv = list(self.argv)
                index = argv.index(flag)
                del argv[index:index + (1 if flag.endswith("clear-shot") else 2)]
                with self.assertRaises(SystemExit) as error:
                    self.invoke(argv)
                self.assertEqual(error.exception.code, 2)
                self.assertFalse(self.output.exists())

    def test_metadata_and_each_eye_hash_must_match(self):
        for kind in ("capture", "left", "right"):
            with self.subTest(kind=kind):
                argv = list(self.argv)
                argv[argv.index("--" + kind + "-sha256") + 1] = "0" * 64
                self.refused("metadata changed|image changed", argv)

    def test_changed_eye_metadata_and_pose_are_refused(self):
        original_capture, original_state = copy.deepcopy(self.capture), copy.deepcopy(self.state)
        for field in ("views", "head", "handL", "handR"):
            with self.subTest(field=field):
                self.capture, self.state = copy.deepcopy(original_capture), copy.deepcopy(original_state)
                if field == "views":
                    self.capture["views"][1]["eye"] = "left"
                else:
                    self.state[field]["pos"][0] += .01
                self.save_fixture()
                argv = list(self.argv)
                argv[argv.index("--capture-sha256") + 1] = self.record.pin(self.capture_path)["sha256"]
                self.refused("stereo metadata|differs from|current hand differs", argv)

    def test_settled_command_custody_is_required(self):
        original = copy.deepcopy(self.writer)
        for field, bad in (("host", dict(pid=99)), ("pending", True), ("sequence", 6),
                           ("sentinel", "other"), ("commandSha256", "0" * 64)):
            with self.subTest(field=field):
                self.writer = dict(copy.deepcopy(original), **{field: bad})
                self.save_fixture()
                self.refused("command custody")

    def test_session_identity_and_retirement_are_required(self):
        original = copy.deepcopy(self.session)
        for field, bad in (("host", dict(pid=99)), ("supervisor", dict(pid=99)),
                           ("state", "retired"), ("source_sha256", "b" * 64), ("version", "other")):
            with self.subTest(field=field):
                self.session = dict(copy.deepcopy(original), **{field: bad})
                self.save_fixture()
                self.refused("linked running session identity")
        self.session = original
        self.save_fixture()
        write(self.run / "result.json", {})
        self.refused("stopped or retired")

    def test_current_controls_must_be_neutral_except_settled_grips(self):
        original = copy.deepcopy(self.state["controls"])
        for key, value in (("trigR", 1), ("trigL", 1), ("a", True), ("stickL", [.2, 0]), ("gripR", .5)):
            with self.subTest(key=key):
                self.state["controls"] = dict(copy.deepcopy(original), **{key: value})
                self.capture["controls"] = copy.deepcopy(self.state["controls"])
                self.save_fixture()
                argv = list(self.argv)
                argv[argv.index("--capture-sha256") + 1] = self.record.pin(self.capture_path)["sha256"]
                self.refused("trigger must be released|unowned button|partially active", argv)

    def test_native_observation_must_be_verified_and_pose_valid(self):
        original = copy.deepcopy(self.snapshot)
        for change in (lambda s: s.update(valid=False), lambda s: s.update(observed_ns=0),
                       lambda s: s.update(observed_ns=10**30),
                       lambda s: s["selected"].update(configurationPathVerified=False),
                       lambda s: s["input"]["referenceHead"].update(orientation=[0, 0, 0, 0])):
            self.snapshot = copy.deepcopy(original)
            change(self.snapshot)
            self.save_fixture()
            self.refused("actual verified native|unnormalized OpenXR pose")

    def test_current_consumer_refuses_changed_owner_config_rig_and_aim(self):
        self.invoke()
        clearance, geometry = self.record.read(self.output), self.consumer().geometry
        mutations = (
            lambda s: s["owner"].update(weapon=99), lambda s: s["rig"].update(fingerprint=99),
            lambda s: s["selected"].update(configurationPath="other"),
            lambda s: s["input"]["referenceHead"]["position"].__setitem__(0, .01),
            lambda s: s["input"]["hands"][1]["aim"]["position"].__setitem__(0, .01))
        now = time.perf_counter_ns()
        for change in mutations:
            changed = copy.deepcopy(self.snapshot)
            change(changed)
            with self.assertRaisesRegex(ValueError, "owner/visible corridor|aim/head/reference changed"):
                geometry.fire_clearance(clearance, changed, self.state, self.host, now, now + 100)

    def test_final_reread_refuses_image_pose_custody_command_and_session_changes(self):
        original = self.record.unchanged_review
        image_path = self.capture_path.with_name("pair_left.png")
        image_bytes, command_bytes = image_path.read_bytes(), self.command.read_bytes()
        changes = (
            lambda: image_path.write_bytes(b"changed"),
            lambda: write(self.run / "sim/state.json", dict(self.state, handL=dict(self.state["handL"], pos=[1, 2, 3]))),
            lambda: write(self.run / "live-driver-owner.json", dict(self.writer, pending=True)),
            lambda: self.command.write_bytes(b"changed command"),
            lambda: write(Path(self.link["session"]), dict(self.session, state="retired")),
            lambda: write(self.run / "session-link.json", dict(self.link, host=dict(pid=999))),
        )
        for change in changes:
            self.save_fixture()
            image_path.write_bytes(image_bytes)
            self.command.write_bytes(command_bytes)
            def changed_review(*args):
                change()
                return original(*args)
            with mock.patch.object(self.record, "unchanged_review", side_effect=changed_review):
                self.refused("reviewed pair changed|changed during binding|linked running session identity")

    def test_final_source_pin_refuses_change(self):
        original = self.record.unchanged_review
        def changed_review(*args):
            with (self.tools / "plan_bc2_feed_simulation.py").open("ab") as stream:
                stream.write(b"\n# changed during review\n")
            return original(*args)
        with mock.patch.object(self.record, "unchanged_review", side_effect=changed_review):
            self.refused("reviewed source changed during binding")

    def test_output_is_fresh_exclusive_run_root_json(self):
        for output in (self.root / "outside.json", self.run / "sim/nested.json", self.run / "wrong.txt"):
            argv = list(self.argv)
            argv[argv.index("--output") + 1] = str(output)
            self.refused("inside exact run|new run-root JSON", argv)
        self.output.write_bytes(b"keep existing")
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.invoke()
        self.assertEqual(self.output.read_bytes(), b"keep existing")

    def test_output_created_during_review_is_never_overwritten(self):
        original = self.record.unchanged_review
        def changed_review(*args):
            self.output.write_bytes(b"concurrent output")
            return original(*args)
        with mock.patch.object(self.record, "unchanged_review", side_effect=changed_review):
            with self.assertRaises(FileExistsError):
                self.invoke()
        self.assertEqual(self.output.read_bytes(), b"concurrent output")


if __name__ == "__main__":
    unittest.main()
