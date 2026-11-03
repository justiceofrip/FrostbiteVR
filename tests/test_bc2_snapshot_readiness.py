"""Deterministic neutral publication-loss recovery; no actual-game acceptance.

Run: python -B -m unittest discover -s tests -p test_bc2_snapshot_readiness.py
"""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


memory = load("neutral_snapshot_memory", "tests/test_bc2_feed_driver_observation.py")
d = memory.d
original_fixture = memory.fixture
MS, NS = 1_000_000, d.NS


def neutral_fixture(now, nonce=1, source=original_fixture):
    profiles, snapshot = source(now, nonce)
    generation = now // MS
    snapshot["shot_input_generation"] = generation
    snapshot["input"]["generation"] = generation
    snapshot["input_evidence"].update(sequence=generation, released=[True, True])
    snapshot["selected"]["sequence"] = generation
    for hand in snapshot["input"]["hands"]:
        hand.update(squeeze=0., trigger=0., held=0, stickX=0., stickY=0.)
    return profiles, snapshot


def harness(testcase, approach=False):
    testcase.enterContext(patch.object(memory, "fixture", neutral_fixture))
    return neutral_harness(memory.Harness(testcase), approach)


def neutral_harness(h, approach=False):
    v = h.driver
    v.motion_started = h.now if approach else None
    v.neutral_snapshot_approach = approach
    v.snapshot_readiness = None
    v.held_left = False
    h.state["controls"] = dict(gripL=0., gripR=0., trigL=0., trigR=0., stickL=[0., 0.], stickR=[0., 0.])
    v.state = copy.deepcopy(h.state)
    v.initial_sim = copy.deepcopy(h.state)
    v.pending_nonce, v.request_bytes = 2, b"2\n"
    h.files[v.request_path] = v.request_bytes
    v.last_request_published = v.last_request + 2 * MS
    return h


def timeout(h):
    h.now = h.driver.last_request + d.SNAPSHOT_TIMEOUT + 1
    return h.driver.receive_snapshot()


def late(h, *, observed=None, alter=None, factory=neutral_fixture):
    v = h.driver
    _, response = factory(v.last_request + 98 * MS if observed is None else observed, v.pending_nonce)
    if alter:
        alter(response)
    h.publish(response)
    return response


def expired_fresh(h, *, alter=None, factory=neutral_fixture):
    v = h.driver
    _, response = factory(v.last_request + MS, v.pending_nonce)
    if alter:
        alter(response)
    h.now = v.snapshot_source_deadline(response)
    h.publish(response)
    return response


class NeutralSnapshotReadinessTests(unittest.TestCase):
    def test_actual280_shaped_delay_discards_late_source_then_reacquires_without_input(self):
        h = harness(self, approach=True)
        v = h.driver
        original = (v.last_request, v.last_request_published, v.pending_nonce, v.request_bytes,
                    v.stage_deadline, v.deadline, v.last_write, v.last_commit)
        started = v.last_request
        delivered = False
        response = None

        def deliver():
            nonlocal delivered, response
            self.assertEqual(h.command_writes, [])
            if not delivered:
                self.assertEqual((v.last_request, v.pending_nonce, v.request_bytes),
                                 (original[0], original[2], original[3]))
                if h.now >= started + 1_162_338_600:
                    response = late(h, observed=started + 98_582_600)
                    delivered = True

        h.on_sleep = deliver
        timeout(h)
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(v.snapshot_readiness["scope"], "neutral_approach")
        v.pause_until(h.now)
        self.assertTrue(delivered)
        self.assertIsNone(v.snapshot_readiness)
        self.assertGreater(int(v.latest_snapshot["nonce"]), original[2])
        self.assertLess(h.now, v.snapshot_source_deadline(v.latest_snapshot))
        self.assertGreater(v.latest_snapshot["shot_input_generation"], response["shot_input_generation"])
        self.assertEqual((v.stage_deadline, v.deadline, v.last_write, v.last_commit), original[4:])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.commands, 0)
        saved = v.artifacts.saved["snapshot-001-late.json"]
        self.assertEqual(saved, d.json_bytes(response))
        event = next(e for e in v.artifacts.events if e["type"] == "snapshot_response_timeout")
        self.assertEqual(event["responseDeadlineNs"], started + 250 * MS)
        self.assertEqual(event["lastObservedResponseNonce"], 1)
        resumed = v.artifacts.events[-1]
        self.assertEqual(resumed["type"], "snapshot_neutral_observation_resumed")
        self.assertEqual(resumed["observationDeadlineNs"], original[4])

    def test_fresh_drains_same_pending_request_beyond_old_iteration_guard(self):
        h = harness(self)
        v = h.driver
        started = v.last_request
        delivered = False

        def deliver():
            nonlocal delivered
            if not delivered and h.now >= started + 1_162_338_600:
                late(h)
                delivered = True

        h.on_sleep = deliver
        received = v.fresh()
        self.assertTrue(delivered)
        self.assertIsNone(v.snapshot_readiness)
        self.assertEqual(received[0], v.latest_snapshot)
        self.assertEqual(h.command_writes, [])

    def test_new_neutral_command_waits_for_fresh_reacquisition(self):
        h = harness(self)
        v = h.driver
        timeout(h)
        late(h)
        v.send(["grip l 0"])
        resumed = next(e for e in v.artifacts.events if e["type"] == "snapshot_neutral_observation_resumed")
        self.assertEqual(len(h.command_writes), 1)
        self.assertGreaterEqual(h.command_writes[0][0], resumed["evaluatedNs"])
        self.assertFalse(v.pending_command)

    def test_timeout_never_recovers_held_pending_unsafe_or_active_gesture(self):
        for key, value in (("held_left", True), ("held_right", True), ("held_trigger", True),
                           ("held_buttons", {"a"}), ("pending_command", True), ("unsafe_owner", True),
                           ("motion_started", NS)):
            with self.subTest(key=key):
                h = harness(self)
                setattr(h.driver, key, value)
                with self.assertRaisesRegex(d.REFUSAL, "response not received within request budget"):
                    timeout(h)
                self.assertIsNone(h.driver.snapshot_readiness)
                self.assertEqual(h.driver.pending_nonce, 2)
                self.assertEqual(h.command_writes, [])

    def test_timeout_without_existing_owner_observation_is_not_recovery(self):
        h = harness(self)
        h.driver.reference = h.driver.latest_snapshot = None
        with self.assertRaisesRegex(d.REFUSAL, "response not received within request budget"):
            timeout(h)

    def test_unsettled_command_cannot_enter_observation_and_no_command_startup_can(self):
        for committed in (None, NS):
            with self.subTest(committed=committed):
                h = harness(self)
                h.driver.last_commit = committed
                with self.assertRaisesRegex(d.REFUSAL, "requires a settled command"):
                    timeout(h)
                self.assertEqual(h.command_writes, [])
        h = harness(self)
        h.driver.last_write, h.driver.last_commit = 0, None
        self.assertIsNone(timeout(h))
        self.assertIsNotNone(h.driver.snapshot_readiness)
        self.assertEqual(h.command_writes, [])

    def test_neutral_simulator_controls_and_exact_owner_are_required(self):
        for change, error in ((lambda h: h.state["controls"].update(trigR=.1), "simulator input changed"),
                              (lambda h: h.state.update(lastCmd="another writer"), "another writer"),
                              (lambda h: h.state.update(captureSeq=1), "unrequested capture")):
            with self.subTest(error=error):
                h = harness(self)
                change(h)
                with self.assertRaisesRegex(d.REFUSAL, error):
                    timeout(h)
                self.assertEqual(h.command_writes, [])

    def test_initial_nonzero_input_does_not_gain_neutral_scope(self):
        h = harness(self)
        h.state["controls"]["gripR"] = .2
        h.driver.state = copy.deepcopy(h.state)
        with self.assertRaisesRegex(d.REFUSAL, "contains active input"):
            timeout(h)

    def test_scope_exit_and_preacquisition_cannot_resume_observation(self):
        for key, value in (("neutral_snapshot_approach", False), ("motion_started", None),
                           ("held_left", True), ("pending_command", True)):
            with self.subTest(key=key):
                h = harness(self, approach=True)
                timeout(h)
                setattr(h.driver, key, value)
                late(h)
                with self.assertRaisesRegex(d.REFUSAL, "scope/held/pending authority changed"):
                    h.driver.receive_snapshot()
                self.assertEqual(h.driver.pending_nonce, 2)
                self.assertEqual(h.command_writes, [])

    def test_original_shorter_initial_demand_deadline_is_never_restarted(self):
        h = harness(self)
        v = h.driver
        v.stage_deadline = v.last_request + 300 * MS
        original = v.stage_deadline
        timeout(h)
        self.assertEqual(v.snapshot_readiness["deadlineNs"], original)
        v.stage_deadline = v.deadline  # An outer scope restoring its deadline cannot extend this hold.
        h.now = original
        late(h)
        with self.assertRaisesRegex(d.REFUSAL, "original neutral snapshot observation deadline"):
            v.receive_snapshot()
        self.assertEqual(v.pending_nonce, 2)
        self.assertEqual(h.command_writes, [])

    def test_no_wait_after_original_driver_deadline(self):
        h = harness(self)
        v = h.driver
        v.deadline = v.last_request + 270 * MS
        timeout(h)
        h.now = v.deadline
        with self.assertRaisesRegex(d.REFUSAL, "driver deadline"):
            v.pump()
        self.assertEqual(h.command_writes, [])

    def test_late_wrong_owner_invalid_lease_and_active_input_are_terminal(self):
        for change, error in ((lambda s: s["owner"].update(weapon=999), "owner/equipment/rig changed"),
                              (lambda s: s.update(valid_until_ns=s["observed_ns"]), "stale snapshot"),
                              (lambda s: s["input"]["hands"][0].update(squeeze=1), "contains active input"),
                              (lambda s: s.update(valid=False, invalid_reason="owner missing"), "owner missing")):
            with self.subTest(error=error):
                h = harness(self)
                timeout(h)
                late(h, alter=change)
                with self.assertRaisesRegex(d.REFUSAL, error):
                    h.driver.receive_snapshot()
                self.assertEqual(h.driver.pending_nonce, 2)
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_future_nonce_and_competing_request_cannot_be_superseded(self):
        for competing_request in (False, True):
            with self.subTest(competing_request=competing_request):
                h = harness(self)
                timeout(h)
                late(h, alter=None if competing_request else lambda s: s.update(nonce=3))
                if competing_request:
                    h.files[h.driver.request_path] = b"3\n"
                with self.assertRaisesRegex(d.REFUSAL, "another writer|later competing request"):
                    h.driver.receive_snapshot()
                self.assertEqual(h.driver.pending_nonce, 2)
                self.assertEqual(h.command_writes, [])

    def test_fresh_response_still_requires_same_owner_and_new_input(self):
        for owner_fault in (True, False):
            with self.subTest(owner_fault=owner_fault):
                h = harness(self)
                v = h.driver
                timeout(h)
                previous = late(h)
                v.receive_snapshot()
                h.now += 40 * MS
                _, response = neutral_fixture(h.now, v.pending_nonce)
                if owner_fault:
                    response["owner"]["weapon"] = 999
                else:
                    generation = previous["shot_input_generation"]
                    response["shot_input_generation"] = response["input"]["generation"] = generation
                    response["input_evidence"]["sequence"] = response["selected"]["sequence"] = generation
                h.publish(response)
                with self.assertRaisesRegex(d.REFUSAL, "owner/equipment/rig changed|requires new input"):
                    v.pump()
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, [])

    def test_even_unexpired_late_response_is_discarded_and_original_request_kept(self):
        h = harness(self)
        v = h.driver
        timeout(h)
        response = late(h, observed=h.now)
        self.assertLess(h.now, response["valid_until_ns"])
        old_request = v.last_request, v.request_bytes
        v.receive_snapshot()
        self.assertIsNone(v.latest_snapshot)
        self.assertGreater(v.pending_nonce, 2)
        self.assertEqual(v.artifacts.saved["snapshot-001-late.json"], d.json_bytes(response))
        timeout_event = next(e for e in v.artifacts.events if e["type"] == "snapshot_response_timeout")
        self.assertEqual(timeout_event["requestStartedNs"], old_request[0])
        self.assertEqual(h.command_writes, [])

    def test_repeated_late_requests_cannot_renew_original_observation_budget(self):
        h = harness(self)
        v = h.driver
        deadline = v.stage_deadline
        for _ in range(d.MAX_SNAPSHOT_REFRESHES):
            timeout(h)
            late(h)
            v.receive_snapshot()
            self.assertEqual(v.snapshot_readiness["deadlineNs"], deadline)
            self.assertIsNone(v.latest_snapshot)
        timeout(h)
        late(h)
        with self.assertRaisesRegex(d.REFUSAL, "reacquisition request budget exhausted"):
            v.receive_snapshot()
        self.assertEqual(h.command_writes, [])

    def test_late_original_then_expired_fresh_reacquires_within_two_total_requests(self):
        h = harness(self, approach=True)
        v = h.driver
        original = v.stage_deadline, v.deadline, v.last_write, v.last_commit
        timeout(h)
        late(h)
        v.receive_snapshot()
        expired = expired_fresh(h)
        self.assertIsNone(v.receive_snapshot())
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(v.snapshot_readiness["freshRequests"], 2)
        self.assertEqual(v.snapshot_readiness["deadlineNs"], min(original[:2]))
        self.assertEqual(v.snapshot_refresh_started, h.now)
        self.assertEqual(v.snapshot_refresh_requests, 1)
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], d.json_bytes(expired))
        h.now += 40 * MS
        _, response = neutral_fixture(h.now, v.pending_nonce)
        h.publish(response)
        v.pump()
        self.assertIsNone(v.snapshot_readiness)
        self.assertEqual(v.latest_snapshot, response)
        self.assertLess(h.now, v.snapshot_source_deadline(response))
        self.assertEqual(v.snapshot_index, 3)
        resumed = next(e for e in v.artifacts.events if e["type"] == "snapshot_neutral_observation_resumed")
        self.assertEqual(resumed["freshRequests"], 2)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual((v.stage_deadline, v.deadline, v.last_write, v.last_commit), original)
        self.assertEqual(h.command_writes, [])

    def test_second_expired_fresh_refuses_before_third_reacquisition_request(self):
        h = harness(self)
        v = h.driver
        timeout(h)
        late(h)
        v.receive_snapshot()
        expired_fresh(h)
        v.receive_snapshot()
        request = v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index
        refresh_started = v.snapshot_refresh_started
        response = expired_fresh(h)
        with self.assertRaisesRegex(d.REFUSAL, "neutral snapshot reacquisition request budget exhausted"):
            v.receive_snapshot()
        self.assertEqual((v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index), request)
        self.assertEqual(h.files[v.request_path], request[2])
        self.assertEqual(v.snapshot_index, 3)
        self.assertEqual(v.snapshot_readiness["freshRequests"], 2)
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(v.snapshot_refresh_started, refresh_started)
        self.assertEqual(v.snapshot_refresh_requests, 1)
        self.assertEqual(v.artifacts.saved["snapshot-003.json"], d.json_bytes(response))
        self.assertEqual(h.command_writes, [])

    def test_mixed_reacquisition_keeps_original_expired_source_refresh_budget(self):
        h = harness(self)
        v = h.driver
        timeout(h)
        late(h)
        v.receive_snapshot()
        expired_fresh(h)
        v.receive_snapshot()
        request = v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index
        h.now = v.snapshot_refresh_started + d.SNAPSHOT_REFRESH_BUDGET
        self.assertLess(h.now, v.snapshot_readiness["deadlineNs"])
        with self.assertRaisesRegex(d.REFUSAL, "expired snapshot refresh budget exhausted"):
            v.pump()
        self.assertEqual((v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index), request)
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(h.command_writes, [])

    def test_expired_fresh_with_active_input_or_changed_identity_cannot_request_again(self):
        for change, error in ((lambda s: s["input"]["hands"][0].update(squeeze=1), "contains active input"),
                              (lambda s: s["selected"].update(configurationPath="config2"), "selected configuration changed")):
            with self.subTest(error=error):
                h = harness(self)
                v = h.driver
                timeout(h)
                late(h)
                v.receive_snapshot()
                request = v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index
                response = expired_fresh(h, alter=change)
                with self.assertRaisesRegex(d.REFUSAL, error):
                    v.receive_snapshot()
                self.assertEqual((v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index), request)
                self.assertEqual(h.files[v.request_path], request[2])
                self.assertEqual(v.snapshot_readiness["freshRequests"], 1)
                self.assertIsNone(v.latest_snapshot)
                self.assertIsNone(v.snapshot_refresh_started)
                self.assertEqual(v.artifacts.saved["snapshot-002.json"], d.json_bytes(response))
                self.assertEqual(h.command_writes, [])

    def test_expired_fresh_sight_target_transition_cannot_escape_neutral_identity(self):
        subject = load("neutral_sight_expired_response", "tests/test_bc2_sight_driver.py")

        def fixture(now, nonce=1):
            return neutral_fixture(now, nonce, source=subject.fixture)

        self.enterContext(patch.object(subject.memory, "fixture", fixture))
        h = neutral_harness(subject.harness(self), approach=True)
        v = h.driver
        timeout(h)
        late(h, factory=fixture)
        v.receive_snapshot()
        request = v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index
        response = expired_fresh(h, factory=fixture)
        response = subject.transition(response)
        h.publish(response)
        # This is an intentional, admissible Sight adapter transition, but no
        # selection authority exists during the shared neutral observation hold.
        v.stable_snapshot(v.reference, response)
        v.admit_snapshot(response, response["observed_ns"], v.pending_nonce)
        with self.assertRaisesRegex(subject.s.transport.REFUSAL, "owner/equipment/rig changed"):
            v.receive_snapshot()
        self.assertEqual((v.pending_nonce, v.last_request, v.request_bytes, v.snapshot_index), request)
        self.assertEqual(h.files[v.request_path], request[2])
        self.assertEqual(v.snapshot_readiness["freshRequests"], 1)
        self.assertIsNone(v.latest_snapshot)
        self.assertIsNone(v.transition_observation)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], subject.s.transport.json_bytes(response))
        self.assertEqual(h.command_writes, [])

    def test_sight_scope_is_removed_before_acquisition_and_at_completion(self):
        subject = load("neutral_sight_stage", "tests/test_bc2_sight_driver.py")
        h, _ = subject.stage_harness(self, approach_samples=2)
        v = h.driver
        scopes = []
        send = v.send

        def observe(lines, **kwargs):
            phase = (kwargs.get("expected") or {}).get("phase")
            scopes.append((phase, getattr(v, "neutral_snapshot_approach", False)))
            return send(lines, **kwargs)

        with patch.object(v, "send", observe):
            v.stage()
        self.assertTrue(all(scope for phase, scope in scopes if phase in ("neutral", "approach", "neutral_at_contact")))
        self.assertFalse(next(scope for phase, scope in scopes if phase == "acquire"))
        self.assertFalse(v.neutral_snapshot_approach)
        self.assertIsNone(v.motion_started)


if __name__ == "__main__":
    unittest.main()
