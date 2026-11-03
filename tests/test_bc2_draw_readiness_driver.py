"""Offline adapter checks; no live requests, clocks, commands or native claims."""
import copy
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import bc2_draw_admission as policy
import bc2_draw_readiness_driver as adapter

_spec = importlib.util.spec_from_file_location("adapter_policy_fixture", ROOT / "tests/test_bc2_draw_admission.py")
fixture = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(fixture)
NOW, MS = fixture.NOW, fixture.MS


class InjectedOriginal:
    @staticmethod
    def admit_snapshot(driver, snapshot, now, nonce):
        policy.require(snapshot["nonce"] == nonce, "original request differs")
        fixture.validate_current(snapshot, now)

    @staticmethod
    def require_held(driver, snapshot):
        policy.require(snapshot["bodyHolster"]["allowAutomaticGunHold"] is True,
                       "original Held gate refused unknown eligibility")


binding = SimpleNamespace(
    resolve_slots=lambda s, n, *selectors: fixture.admit(s, n),
    selected_matches=fixture.selected_matches, REFUSAL=policy.Refusal, integer=int,
    transport=SimpleNamespace(SNAPSHOT_TIMEOUT=policy.REQUEST_NS),
    original=SimpleNamespace(HolsterDriver=InjectedOriginal,
        frame=SimpleNamespace(ypr_quaternion=lambda angles: [0, 0, 0, 1])))


class Events:
    def __init__(self):
        self.rows = []

    def event(self, row):
        self.rows.append(copy.deepcopy(row))


class Harness:
    """Transport substitute with portable injected evidence validators."""
    def __init__(self, args):
        self.args = args
        self.started, self.deadline = NOW-MS, NOW+9000*MS
        self.holster_hold_started = self.snapshot_refresh_started = None
        self.artifacts = Events()
        self.requests, self.sent, self.accepted = 0, [], []
        self.queue = []
        self.state = fixture.host()
        pose = fixture.snapshot()["input"]["hands"][1]["grip"]
        self.state["handR"].update(pos=pose["position"], ypr=[0, 0, 0], followsHead=False)
        self.right_pose = copy.deepcopy(pose)
        self.sequence = 27
        self.last_write, self.last_commit = 0, None
        self.pending_command = self.held_right = self.draw_armed = False
        self.phase = "baseline"
        self.slot_binding = self.stow_ack = self.baseline = self.last_native = None
        self.latest_snapshot = self.publication_source = None
        self.target_baseline = None
        self.press_ns = 0
        self.expected = dict(asset=args.target_asset, configuration=args.target_config, slot=2)

    def bounds(self, now):
        policy.require(now < self.deadline, "original draw deadline")

    def command_identity(self):
        return self.sequence, "ack", "hash", self.last_write, self.last_commit, len(self.sent)

    def read_state(self):
        return copy.deepcopy(self.state)

    def ownership(self, state):
        policy.require(state["pid"] == 101, "original host owner changed")

    def request_snapshot(self):
        self.requests += 1

    def receive_snapshot(self):
        value = self.queue.pop(0)
        if callable(value):
            return value(self)
        self.last_request = value["observed_ns"]-MS
        self.last_request_published = self.last_request+MS//2
        return value, b"retained raw response", value["observed_ns"]

    def accept_snapshot(self, snapshot, raw, now):
        self.accepted.append(snapshot)
        self.latest_snapshot = self.last_native = snapshot
        self.holster_hold_started = self.snapshot_refresh_started = None

    def current(self):
        # This ordering models the real92.receive ->80.accept/current ->94 Held
        # seam: unknown evidence must never reach the unchanged Held assertion.
        self.request_snapshot()
        for _ in range(4):
            value = self.receive_snapshot()
            if value is not None:
                self.accept_snapshot(*value)
                s, _, now = value
                binding.original.HolsterDriver.require_held(self, s)
                if self.slot_binding is None:
                    self.slot_binding = fixture.admit(s, now)
                return s, now
        raise AssertionError("adapter did not finish within replacement budget")

    def validate_publication(self, lines, expected, cleanup):
        if not cleanup:
            policy.require(self.publication_source is self.latest_snapshot,
                           "original latest source check")

    def command(self, snapshot, pose, grip):
        self.publication_source = snapshot
        self.validate_publication(["grip r " + str(grip)], dict(pose=pose, grip=grip), False)
        self.sent.append((snapshot, copy.deepcopy(pose), grip))
        self.last_write, self.last_commit = snapshot["observed_ns"]+MS, snapshot["observed_ns"]+2*MS
        self.sequence += 5
        self.held_right = bool(grip)
        self.right_pose = copy.deepcopy(pose)

    def release_after_failure(self):
        self.validate_publication(["grip r 0"], None, True)
        return "inherited ordinary cleanup"


class AdapterTests(unittest.TestCase):
    def setup_driver(self, primes=2, command=True):
        self.now = NOW
        budget = adapter.AdmissionBudget(policy, NOW-policy.NS, NOW+29*policy.NS)
        for _ in range(primes):
            budget.prime_attempt(NOW-10*MS)
        cls = adapter.make_driver(Harness, binding, budget=budget, clock=lambda: self.now)
        args = SimpleNamespace(stow_asset=fixture.SOURCE[0], stow_config=fixture.SOURCE[1],
            target_asset=fixture.TARGET[0], target_config=fixture.TARGET[1], observed_binding=None)
        driver = cls(args)
        driver.queue = [fixture.snapshot()]
        initial, _ = driver.current()
        driver.baseline = initial
        driver.phase = "stow_reach"
        if command:
            driver.command(initial, driver.right_pose, 0)
        return driver, budget

    def sample(self, millis, nonce, unknown=False):
        self.now = NOW+millis*MS
        return fixture.snapshot(self.now, nonce, unknown)

    def test_real_order_baseline_then_reach_current_before_first_command(self):
        d, b = self.setup_driver(command=False)
        initial_identity = d.command_identity()
        self.assertEqual(d.last_write, 0)
        self.assertIsNone(d.last_commit)
        s = self.sample(20, 2)
        d.queue = [s]
        reached, _ = d.current()
        self.assertIs(reached, s)
        self.assertEqual(d.command_identity(), initial_identity)
        self.assertEqual(d.sent, [])
        self.assertEqual(b.decisions[-1]["status"], "Ready")
        d.command(reached, d.right_pose, 0)
        self.assertEqual(len(d.sent), 1)
        self.assertGreater(d.last_commit, d.last_write)

    def test_unknown_never_reaches_original_held_gate_or_command(self):
        d, b = self.setup_driver()
        unknown = self.sample(20, 2, True)
        raw = copy.deepcopy(unknown)
        ready = self.sample(40, 3)
        d.queue = [unknown, ready]
        # evaluate each request at its actual injected time
        def receive(driver):
            self.now = ready["observed_ns"]
            driver.queue.insert(0, ready)
            return Harness.receive_snapshot(driver)
        self.now = unknown["observed_ns"]
        d.queue = [unknown, receive]
        observed, _ = d.current()
        self.assertIs(observed, ready)
        self.assertEqual(unknown, raw)
        self.assertNotIn(unknown, d.accepted)
        self.assertEqual(len(d.sent), 1)
        self.assertEqual(b.observations, 3)  # twoPrime + one replacement
        self.assertEqual([r["status"] for r in b.decisions], ["Ready", "ObserveOnly", "Ready"])
        d.command(ready, d.right_pose, 0)
        self.assertEqual(len(d.sent), 2)
        with self.assertRaisesRegex(ValueError, "latest fresh Ready"):
            d.command(ready, d.right_pose, 0)

    def test_missing_current_mapping_stays_unknown_until_complete_ready(self):
        d, b = self.setup_driver()
        unknown = self.sample(20, 2, True)
        unknown["bodyInventory"] = dict(available=False, observationOnly=True,
            reason="display_missing", sampledNs=self.now)
        original = copy.deepcopy(unknown)
        d.queue = [unknown]
        self.assertIsNone(d.receive_snapshot())
        self.assertEqual(b.decisions[-1]["status"], "ObserveOnly")
        self.assertEqual(unknown, original)
        self.assertIsNone(d.publication_source)
        self.assertIsNone(d.latest_snapshot)
        self.assertEqual(len(d.sent), 1)
        ready = self.sample(40, 3)
        d.queue = [ready]
        received = d.receive_snapshot()
        self.assertIs(received[0], ready)
        d.accept_snapshot(*received)
        self.assertEqual(b.decisions[-1]["status"], "Ready")

    def test_nested_missing_and_refresh_do_not_restart_hold_or_replacements(self):
        d, b = self.setup_driver()
        s = self.sample(20, 2, True)
        d.queue = [s]
        self.assertIsNone(d.receive_snapshot())
        anchor = b.hold_started
        d.holster_hold_started = NOW+30*MS
        self.now = NOW+30*MS
        d.request_snapshot()
        self.assertEqual(b.hold_started, anchor)
        self.assertEqual(b.replacements, 2)
        d.snapshot_refresh_started = NOW+40*MS
        self.now = NOW+40*MS
        with self.assertRaisesRegex(ValueError, "two replacement"):
            d.request_snapshot()
        self.assertEqual(b.observations, 4)
        self.assertEqual(len(d.sent), 1)
        self.assertEqual(d.release_after_failure(), "inherited ordinary cleanup")

    def test_nested_older_hold_keeps_original_500ms_deadline(self):
        d, b = self.setup_driver()
        d.holster_hold_started = NOW+10*MS
        self.now = NOW+20*MS
        d.request_snapshot()
        self.assertEqual(b.hold_started, NOW+10*MS)
        d.holster_hold_started = None
        d.snapshot_refresh_started = NOW+400*MS
        self.now = NOW+510*MS
        with self.assertRaisesRegex(ValueError, "500ms"):
            d.request_snapshot()
        self.assertEqual(b.replacements, 1)

    def test_prime_budget_is_shared_but_ordinary_observations_do_not_charge(self):
        d, b = self.setup_driver(primes=20)
        for nonce in range(2, 25):
            s = self.sample(20+nonce*3, nonce)
            d.queue = [s]
            d.current()
        self.assertEqual(b.observations, 20)
        s = self.sample(120, 25, True)
        d.queue = [s]
        with self.assertRaisesRegex(ValueError, "budget|20"):
            d.current()
        self.assertEqual(len(d.sent), 1)

    def test_mutated_ready_and_changed_ack_are_refused_without_command(self):
        d, b = self.setup_driver()
        s = self.sample(20, 2)
        d.queue = [s]
        d.current()
        s["bodyHolster"]["inventoryEvaluation"] = 0
        with self.assertRaisesRegex(ValueError, "unchanged latest fresh Ready"):
            d.command(s, d.right_pose, 0)
        d.sequence += 1
        d.queue = [self.sample(40, 3, True)]
        with self.assertRaisesRegex(ValueError, "ownership changed"):
            d.current()
        self.assertEqual(len(d.sent), 1)

    def test_factory_preserves_actual_empty_cohort_and_cleanup_methods(self):
        path = ROOT / "reports/coordinator-20261102-feedback100/holster-transition/drive_bc2_holster_transition100.py"
        if not path.is_file():
            self.skipTest("optional archived100 integration helper is not in a clean checkout")
        spec = importlib.util.spec_from_file_location("adapter_original100_test", path)
        actual = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(actual)
        b = adapter.AdmissionBudget(policy, NOW-policy.NS, NOW+29*policy.NS)
        cls = adapter.make_driver(actual.HolsterDriver, actual.frozen, budget=b)
        for name in ("run_holster", "current", "require_empty", "require_held", "reach",
                     "wait_state", "release_after_failure", "admit_snapshot"):
            self.assertIs(getattr(cls, name), getattr(actual.HolsterDriver, name), name)
        self.assertEqual(b.summary()["decisions"], [])

    def test_optional_prime_helper_carries_original_count_and_one_draw_clock(self):
        b = adapter.AdmissionBudget(policy, NOW-policy.NS, NOW+29*policy.NS)
        b.prime_attempt(NOW-MS)
        s = fixture.snapshot()
        result = b.admit_prime(s, NOW, fixture.context(s, used=1, phase="prime"), fixture.admit)
        self.assertEqual(result.status, policy.Status.READY, result.reason)
        self.assertEqual(b.observations, 1)
        b.attach_draw(NOW+MS, NOW+9000*MS)
        self.assertEqual(b.summary()["originalAdmissionStartedNs"], NOW-policy.NS)
        with self.assertRaisesRegex(ValueError, "already attached"):
            b.attach_draw(NOW+2*MS, NOW+9001*MS)
        with self.assertRaisesRegex(ValueError, "cannot restart"):
            b.prime_attempt(NOW+2*MS)


if __name__ == "__main__":
    unittest.main()
