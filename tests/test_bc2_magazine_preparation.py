"""Deterministic preparation contracts; invented live evidence grants no acceptance.

Runs against production helpers and command/publication code. The geometry case
reads the actual authored M95 header; its native join is a synthetic assertion.
"""
import copy
from contextlib import contextmanager, ExitStack
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = next(p for p in Path(__file__).resolve().parents
                  if (p / "tests/test_bc2_magazine_bolt_stimulus.py").is_file())


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    # Some shared fixtures import one another; isolate their dependency lookup
    # to this checkout even under embedded Python's empty script search path.
    with mock.patch.object(sys, "path", [str(ROOT / "tests"), *sys.path]):
        spec.loader.exec_module(module)
    return module


d = load("preparation_driver", ROOT / "tools/drive_bc2_magazine_bolt_stimulus.py")
old = load("preparation_fixture", REPOSITORY / "tests/test_bc2_magazine_bolt_stimulus.py")
g, f, NOW = d.geometry, d.frame, old.NOW


def fixture():
    s = old.fixture()
    x, b = s["magazine"], s["bolt"]
    x.update(originalPhysical=None, pendingRequest=None, submittedSeat=None)
    x["raw"]["nativeMagazineAttached"] = True
    x["resource"]["command"] = None
    x["resource"]["snapshot"]["counts"]["loaded"] = 7
    b["native"].update(phase=0, failure=0, heldSuspended=False, blocksFire=False)
    return s


def postshot(s, base):
    s = copy.deepcopy(s)
    old.owed(s)
    counts = dict(base["counts"], loaded=base["counts"]["loaded"]-1)
    b, r = s["bolt"], s["magazine"]["resource"]
    b["native"]["counts"] = copy.deepcopy(counts)
    for held in (b["held"], b["native"]["held"]):
        held["advancing"]["counts"] = copy.deepcopy(counts)
    r["snapshot"].update(counts=counts, sequence=base["sequence"]+1, observedNs=NOW+1)
    return s


def permission(s, state, folder):
    captures = []
    for eye in ("left", "right"):
        path = folder / (eye+".png")
        path.write_bytes(eye.encode("ascii"))
        captures.append(dict(eye=eye, path=str(path), bytes=path.stat().st_size,
                             sha256=d.transport.digest(path.read_bytes())))
    return dict(schema="bc2vr.root-fire-clearance.v1", rootReviewedVisibleCorridor=True,
        host={"pid": 1}, owner=copy.deepcopy(s["owner"]), configurationPath=s["selected"]["configurationPath"],
        rig=s["rig"], reviewedNs=1, referenceHead=s["input"]["referenceHead"],
        rightAim=s["input"]["hands"][1]["aim"], captures=captures, **copy.deepcopy(state))


@contextmanager
def driver_fixture(folder, mutation=None, clearance_mutation=None):
    """Real boundaries/publication/ACK/cleanup, with synthetic runtime IO."""
    timed = load("magazine_preparation_timed_fixture", ROOT / "tests/test_bc2_timed_trigger.py")
    with ExitStack() as stack:
        stack.enter_context(mock.patch.object(timed, "d", d.transport))
        stack.enter_context(mock.patch.object(timed.transport_tests, "d", d.transport))
        runtime = timed.RuntimeHarness(SimpleNamespace(enterContext=stack.enter_context))
        runtime.now = runtime.origin = NOW
        driver = runtime.driver
        driver.__class__ = d.MagazineBoltDriver
        del driver.validate_publication
        s = fixture()
        if mutation:
            mutation(s)
        runtime.state["controls"]["gripR"] = 1
        driver.held_right = True  # Matches the existing native GunHold and settled input.
        driver.args = SimpleNamespace(fire_clearance=folder / "clearance.json", magazine_result=None,
                                      mode="prep-shot-bolt")
        state = {key: copy.deepcopy(runtime.state[key]) for key in ("head", "handL", "handR", "controls")}
        allowed = permission(s, state, folder)
        allowed["host"] = copy.deepcopy(driver.host)
        if clearance_mutation:
            clearance_mutation(allowed)
        raw = d.transport.json_bytes(allowed)
        driver.args.fire_clearance.write_bytes(raw)
        runtime.json_files[driver.args.fire_clearance] = allowed
        runtime.files[driver.args.fire_clearance] = raw
        driver.run, driver.artifacts.directory = folder, folder / "artifacts"
        driver.initial_sim = copy.deepcopy(runtime.state)
        driver.family, driver.preparation_mode = "bolt", True
        driver.anchor = driver.preserved = driver.motion_reference = None
        driver.preparation = driver.debt = None
        driver.boundaries = []
        driver.last_write = NOW-25_000_000

        def fresh():
            current = copy.deepcopy(s) if driver.preparation is None else postshot(s, driver.preparation)
            # A new producer observation follows natural timer release; original
            # source/claim deadlines remain unchanged throughout this short pulse.
            sequence = 21+driver.commands
            current["input_evidence"].update(sequence=sequence, observedNs=runtime.now, nowNs=runtime.now)
            current.update(observed_ns=runtime.now, shot_input_generation=sequence)
            current["input"].update(generation=sequence, predictedNs=runtime.now)
            current["selected"].update(sequence=sequence, observedNs=runtime.now)
            for family in ("magazine", "bolt"):
                current[family]["capturedNs"] = runtime.now
                current[family]["rightClaim"]["inputSequence"] = sequence
            driver.admit_snapshot(current, runtime.now, current["nonce"])
            driver.latest_snapshot = current
            return current, b"", runtime.now

        driver.fresh = fresh
        yield driver, s, runtime


class PreparationTests(unittest.TestCase):
    def test_production_authored_geometry_preflight_caps_native_ceiling(self):
        text = (REPOSITORY / "src/games/bc2/Bc2M95BoltGeometry.h").read_text(encoding="utf-8")
        s = fixture()
        p = s["bolt"]["calibration"]
        for key, symbol in (("strokeMeters", "M95BoltStroke"), ("unlockRadians", "M95BoltUnlock")):
            p[key] = float(re.search(symbol+r"=([-.0-9]+)f", text)[1])
        for key, symbol in (("wristFromPart", "M95AuthoredPartFromWrist"),
                            ("closedContact", "M95AuthoredReferencePart")):
            fields = re.search(symbol+r"=([^;]+);", text)[1]
            values = [float(v) for v in re.findall(r"([-+]?[0-9]+(?:\.[0-9]*)?(?:e[-+]?\d+)?)f", fields)]
            self.assertEqual(len(values), 16)
            p[key] = [values[n:n+4] for n in range(0, 16, 4)]
        p["axis"] = [0, 0, 1]
        for key in ("endpointDwellNs", "maximumCycleNs"):
            p[key] = int(re.search(r"p\."+key+r"=(\d+)", text)[1])
        p["maxStepMeters"] = float(re.search(r"p\.maxStepMeters=([.0-9]+)", text)[1])
        self.assertEqual(p["maximumCycleNs"], 30*g.NS)
        self.assertEqual(g.bolt_cycle_budget(p), 10*g.NS)
        self.assertEqual(g.preparation_baseline(s, NOW)["counts"]["loaded"], 7)
        for travel, angle in ((0, 0), (p["strokeMeters"], p["unlockRadians"])):
            f.rigid(g.bolt_pose(p, travel, angle))
        # Shared admission uses the exact observed configuration, not an alias.
        for name in ("M24", "GOL", "SV98", "reviewed-other"):
            variant = "exact/"+name
            s["selected"]["configurationPath"] = variant
            s["magazine"]["profile"]["configurationPath"] = variant
            s["bolt"]["calibration"]["configurationPath"] = variant
            self.assertEqual(g.preparation_baseline(s, NOW)["owner"]["selection"]["configurationPath"], variant)

    def test_full_counts_capacity_reserve_and_observation_must_be_valid(self):
        mutations = (lambda s: s["magazine"]["resource"]["snapshot"]["counts"].update(loaded=6),
                     lambda s: s["magazine"]["resource"]["snapshot"]["counts"].update(loaded=1, capacity=1),
                     lambda s: s["magazine"]["resource"]["snapshot"]["counts"].update(reserve=6),
                     lambda s: s["magazine"]["resource"]["snapshot"].update(coherentIdleVerified=False),
                     lambda s: s["magazine"]["resource"]["snapshot"].update(deadlineNs=NOW))
        for mutation in mutations:
            s = fixture()
            mutation(s)
            with self.assertRaises(g.Refusal):
                g.preparation_baseline(s, NOW)

    def test_owner_profile_native_copies_and_raw_clock_cannot_be_borrowed(self):
        mutations = (lambda s: s["bolt"]["native"].update(identity=dict(s["bolt"]["native"]["identity"], serverItem=999)),
                     lambda s: s["magazine"]["profile"].update(configurationPath="foreign"),
                     lambda s: s["bolt"]["calibration"].update(nativeJoined=False),
                     lambda s: s["bolt"]["calibration"].update(authoredReviewed=False),
                     lambda s: s["magazine"]["raw"].update(current=False),
                     lambda s: s["magazine"]["raw"].update(originalHandEvidence=dict(s["input_evidence"], sequence=20)),
                     lambda s: s["magazine"]["resource"]["identity"].update(firing=[101, 101, 103]))
        for mutation in mutations:
            s = fixture()
            mutation(s)
            with self.assertRaises(g.Refusal):
                g.preparation_baseline(s, NOW)

    def test_held_ammunition_pending_transfer_and_cycle_are_refused(self):
        def mismatched_original(s):
            old.attached_original(s)
            s["magazine"]["originalPhysical"]["rounds"] -= 1
        mutations = (lambda s: s["magazine"]["resource"].update(originalState=1),
                     mismatched_original,
                     lambda s: s["magazine"].update(carriedReplacement={}),
                     lambda s: s["magazine"].update(reservation={}),
                     lambda s: s["magazine"].update(pendingRequest={}),
                     lambda s: s["magazine"].update(submittedSeat={}),
                     lambda s: s["magazine"].update(requestPending=True),
                     lambda s: s["magazine"]["resource"].update(requestState=5),
                     lambda s: s["magazine"]["resource"].update(command={}),
                     lambda s: s["bolt"]["native"].update(phase=1),
                     lambda s: s["bolt"]["native"].update(ready={}),
                     lambda s: s["bolt"]["native"].update(heldSuspended=True),
                     lambda s: s["bolt"].update(recovery={}),
                     lambda s: s["bolt"].update(reloadPause={}),
                     lambda s: old.owed(s))
        for mutation in mutations:
            s = fixture()
            mutation(s)
            with self.assertRaises(g.Refusal):
                g.preparation_baseline(s, NOW)

    def test_left_support_requires_exact_dependency(self):
        s = fixture()
        s["input_evidence"]["released"][0] = False
        token = dict(s["magazine"]["rightClaim"]["token"], id=92, hand=0, kind=2, prerequisiteClaim=91)
        claim = dict(token=token, inputSequence=21, deadlineNs=NOW+70_000_000)
        s["magazine"]["leftClaim"] = s["bolt"]["leftClaim"] = claim
        g.preparation_baseline(s, NOW)
        token["prerequisiteClaim"] = 99
        with self.assertRaisesRegex(g.Refusal, "another gun"):
            g.preparation_baseline(s, NOW)

    def test_actual_guarded_debt_and_coherent_counts_are_required(self):
        s = fixture()
        base = g.preparation_baseline(s, NOW)
        base["pressedNs"] = NOW
        s = postshot(s, base)
        debt = g.owed(s, NOW+2)
        result = g.preparation_debt(s, NOW+2, base, debt)
        self.assertTrue(result["coherentThreeCopyCounts"])
        self.assertFalse(result["shotAllThreeCopiesVerified"])
        for field, value in (("guardEpoch", None), ("countsFreshnessAvailable", False), ("updates", [1, 2])):
            changed = copy.deepcopy(debt)
            changed[field] = value
            with self.assertRaises(g.Refusal):
                g.preparation_debt(s, NOW+2, base, changed)
        for path, value in (("reserve", 29), ("loaded", 5), ("capacity", 8)):
            changed = copy.deepcopy(s)
            changed["magazine"]["resource"]["snapshot"]["counts"][path] = value
            with self.assertRaisesRegex(g.Refusal, "exactly one round"):
                g.preparation_debt(changed, NOW+2, base, debt)
        for field in ("shot", "cycle"):
            previous = dict(base, **{field: debt["debt"][field]})
            with self.assertRaisesRegex(g.Refusal, "new same-owner"):
                g.preparation_debt(s, NOW+2, previous, debt)

    def test_postshot_freshness_transaction_and_owner_are_pinned(self):
        s = fixture()
        base = g.preparation_baseline(s, NOW)
        base["pressedNs"] = NOW
        s = postshot(s, base)
        debt = g.owed(s, NOW+2)
        for change in (lambda s: s["magazine"]["resource"].update(request=11),
                       lambda s: s["magazine"]["resource"]["snapshot"].update(observedNs=NOW-1),
                       lambda s: s["magazine"]["resource"]["snapshot"].update(sequence=base["sequence"]),
                       lambda s: s["selected"].update(selectedSlot=1)):
            changed = copy.deepcopy(s)
            change(changed)
            with self.assertRaises(g.Refusal):
                g.preparation_debt(changed, NOW+2, base, debt)

    def test_completion_requires_same_debt_ready_return_and_coherent_unchanged_counts(self):
        s = fixture()
        base = g.preparation_baseline(s, NOW)
        base["pressedNs"] = NOW
        s = postshot(s, base)
        debt = g.owed(s, NOW+2)
        b = s["bolt"]
        event = dict(release=dict(cycle=copy.deepcopy(b["held"]), request=51, inputSequence=21,
                                 observedNs=NOW+3, deadlineNs=NOW+50_000_000),
                     sequence=52, observedNs=NOW+4, deadlineNs=NOW+50_000_000,
                     nativeReady=True, unchangedAmmunition=True)
        debt["readyReceipt"] = event
        b.update(held=None, custodyCurrent=True, custody=3, blocksFire=False, gunClaim=b["rightClaim"])
        b["native"].update(held=None, phase=5)
        s["input_evidence"]["released"][0] = False
        support = dict(b["rightClaim"]["token"], id=92, hand=0, kind=2, prerequisiteClaim=91)
        b["leftClaim"] = dict(token=support, inputSequence=21, deadlineNs=NOW+70_000_000)
        s["magazine"]["leftClaim"] = copy.deepcopy(b["leftClaim"])
        result = g.preparation_complete(s, NOW+5, base, debt)
        self.assertTrue(result["preparationOnly"])
        self.assertEqual(result["counts"], dict(loaded=6, reserve=30, capacity=7))
        for mutation in (lambda s: s["magazine"]["resource"]["snapshot"]["counts"].update(reserve=29),
                         lambda s: s["magazine"]["resource"]["snapshot"].update(coherentIdleVerified=False),
                         lambda s: s["bolt"]["native"].update(cycle=99),
                         lambda s: s["bolt"]["leftClaim"]["token"].update(prerequisiteClaim=99)):
            changed = copy.deepcopy(s)
            mutation(changed)
            with self.assertRaises(g.Refusal):
                g.preparation_complete(changed, NOW+5, base, debt)

    def test_one_pulse_uses_production_command_publication_and_actual_debt(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder)) as (driver, s, runtime):
            driver.run_preparation_shot()
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertEqual(driver.commands, 2)
            self.assertFalse(driver.held_trigger)
            self.assertTrue(driver.held_right)
            self.assertIsNone(driver.timed_trigger)
            proof = driver.shot_pulse["pulseTransport"]
            self.assertEqual(proof["expectedNeutralControls"]["gripR"], 1)
            self.assertEqual(proof["runtimeSha256"], d.transport.TIMED_TRIGGER_RUNTIME_SHA256)
            self.assertIsNone(driver.shot_pulse["releaseWrittenNs"])
            self.assertEqual(driver.debt["counts"], dict(loaded=6, reserve=30, capacity=7))
            self.assertEqual([b["name"] for b in driver.boundaries],
                             ["before_preparation_shot", "preparation_shot_created_owed_bolt", "preparation_shot_counts"])
            self.assertEqual(len(list(Path(folder).glob("magazine-preparation-attempt-*.json"))), 1)
            self.assertEqual(list(Path(folder).glob("magazine-shot-attempt-*.json")), [])

    def test_nonfull_preflight_sends_no_input(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder),
                mutation=lambda s: s["magazine"]["resource"]["snapshot"]["counts"].update(loaded=6)) as (driver, s, runtime):
            with self.assertRaisesRegex(g.Refusal, "full magazine"):
                driver.run_preparation_shot()
            self.assertEqual(runtime.batches(), [])

    def test_refused_clearance_consumes_attempt_without_input_or_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            with driver_fixture(folder,
                    clearance_mutation=lambda p: p.update(rootReviewedVisibleCorridor=False)) as (driver, s, runtime):
                with self.assertRaisesRegex(g.Refusal, "corridor"):
                    driver.run_preparation_shot()
                self.assertEqual(runtime.batches(), [])
            with driver_fixture(folder) as (driver2, _, runtime2):
                with self.assertRaises(FileExistsError):
                    driver2.run_preparation_shot()
                self.assertEqual(runtime2.batches(), [])

    def test_changed_capture_pin_and_owner_clearance_refuse_before_press(self):
        for mutation in (lambda p: p["captures"][0].update(sha256="0"*64),
                         lambda p: p["owner"].update(weapon=999),
                         lambda p: p["controls"].update(gripR=0)):
            with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder),
                    clearance_mutation=mutation) as (driver, s, runtime):
                with self.assertRaises((g.Refusal, d.transport.REFUSAL)):
                    driver.run_preparation_shot()
                self.assertEqual(runtime.batches(), [])

    def test_expired_source_after_pin_work_refuses_no_input(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder)) as (driver, s, runtime):
            original = d.transport.check_pin
            def slow_pin(pin):
                original(pin)
                runtime.now += 40_000_000
            with mock.patch.object(d.transport, "check_pin", side_effect=slow_pin):
                with self.assertRaises(g.Refusal):
                    driver.run_preparation_shot()
            self.assertEqual(runtime.batches(), [])

    def test_preparation_source_includes_shorter_magazine_lease(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder)) as (driver, s, _):
            s["magazine"]["resource"]["binding"]["deadlineNs"] = NOW+3
            self.assertEqual(driver.snapshot_source_deadline(s), NOW+3)

    def test_uncertain_shot_stops_after_single_release_and_retains_debt(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder)) as (driver, s, runtime):
            original = driver.boundary
            def boundary(name, predicate):
                if name == "preparation_shot_counts":
                    raise g.Refusal("uncertain three-copy shot")
                return original(name, predicate)
            driver.boundary = boundary
            with self.assertRaisesRegex(g.Refusal, "uncertain"):
                driver.run_preparation_shot()
            self.assertIsNotNone(driver.debt)
            self.assertFalse(driver.held_trigger)
            self.assertTrue(driver.held_right)
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertIn("owed debt retained", driver.cleanup_owned())
            self.assertIsNotNone(driver.debt)
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])

    def test_missing_timed_release_refuses_then_cleans_up_without_debt_or_retry(self):
        with tempfile.TemporaryDirectory() as folder, driver_fixture(Path(folder)) as (driver, s, runtime):
            runtime.timer_ms = 400
            with self.assertRaises(d.transport.REFUSAL):
                driver.run_preparation_shot()
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND]])
            self.assertTrue(driver.held_trigger)
            self.assertIsNotNone(driver.timed_trigger)
            self.assertIn("runtime timer custody retained", driver.cleanup_owned())
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND], ["trigger r 0"]])
            self.assertFalse(driver.held_trigger)
            self.assertTrue(driver.held_right)
            self.assertIsNotNone(driver.timed_trigger)
            self.assertIsNone(driver.debt)
            with self.assertRaisesRegex(d.transport.REFUSAL, "already used"):
                driver.single_trigger_pulse()
            self.assertEqual(runtime.batches(), [[d.transport.TIMED_TRIGGER_COMMAND], ["trigger r 0"]])

    def test_cli_composes_bolt_then_final_check_and_distinct_preparation_result(self):
        calls = []
        class Fake:
            def __init__(self, args):
                self.args = args
            def run_preparation_shot(self):
                calls.append("prep")
            def run_bolt(self):
                calls.append("bolt")
            def finish_preparation(self):
                calls.append("complete")
            def close(self):
                pass
        out = io.StringIO()
        with mock.patch.object(d, "MagazineBoltDriver", Fake), mock.patch("sys.stdout", out):
            code = d.main(["--run", ".", "--mode", "prep-shot-bolt", "--root-authorized-ordinary-input"])
        self.assertEqual(code, 0)
        self.assertEqual(calls, ["prep", "bolt", "complete"])
        result = json.loads(out.getvalue())
        self.assertEqual(result["status"], "ordinary_preparation_completed")
        self.assertTrue(result["preparationOnly"] and result["preparationCompleted"])
        self.assertFalse(result["shotAllThreeCopiesVerified"] or result["nativeAcceptance"])

    def test_existing_shot_still_requires_immediate_completed_magazine_receipt(self):
        driver = d.MagazineBoltDriver.__new__(d.MagazineBoltDriver)
        driver.args = SimpleNamespace(magazine_result=None, fire_clearance=Path("clearance.json"))
        with self.assertRaisesRegex(g.Refusal, "immediately preceding magazine result"):
            driver.run_shot()


if __name__ == "__main__":
    unittest.main()
