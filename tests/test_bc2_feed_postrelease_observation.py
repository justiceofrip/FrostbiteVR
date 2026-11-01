"""Deterministic production driver/admission paths; no native/headset evidence.

Run: python -B -m unittest discover -s tests -p test_bc2_feed_postrelease_observation.py
Only filesystem, simulator state, and time use the existing in-memory transport.
"""
import copy
import unittest
from unittest.mock import patch

from test_bc2_feed_driver_observation import Harness, MS, NS, d, fixture


def released_fixture(now, nonce, *, missing=False, advanced=False):
    profiles, snapshot = fixture(now, nonce)
    profiles["profiles"][0]["geometry"] = dict(chainCount=0)
    snapshot["weapon_actions_blocked"] = True
    snapshot["input_evidence"]["released"] = [True, False]
    for hand in snapshot["input"]["hands"]:
        hand.update(squeeze=0., trigger=0., held=0, stickX=0., stickY=0.)
    snapshot["feedObservation"] = dict(
        observations="independent_atomics_not_native_proof",
        submit_attempts=2 if advanced else 1, receipts_accepted=2 if advanced else 1,
        latest_tick=dict(pending=False, claim=False))
    if not missing:
        snapshot["feed"] = dict(
            owner=copy.deepcopy(snapshot["owner"]), weapon=dict(id=4, generation=6),
            registrationKey=1, rigFingerprint=1, lifetime=2, revision=nonce,
            blocksFire=True, operation=5, chainCount=0, weaponFromChain=[], carriedPartsMask=0,
            supply=dict(resource=dict(id=20, generation=2), container=dict(id=4, generation=2)),
            input=copy.deepcopy(snapshot["input_evidence"]), mechanismInput=None,
            mechanismClaim=None, mechanismWristWorldMeters=None, supplyObject=None,
            **{key: d.planner.identity() for key in (
                "weaponFromPouch", "weaponFromCoverHinge", "pouchCarryWeaponWorldMeters",
                "chainCarryWeaponWorldMeters", "originalWeaponWorldMeters")})
    return profiles, snapshot


class FeedPostreleaseObservationTests(unittest.TestCase):
    def release_race(self, *, ack_ms=55, response_ms=45, fault=None):
        """Actual40 ordering: held15125 -> publish release -> released15127 -> ack.

        Synthetic owners/geometry keep this offline; the response reproduces
        snapshot020's pending, unclaimed, blocked, missing-feed presentation.
        """
        h = Harness(self, ack_ms=ack_ms)
        v = h.driver
        v.profiles, before = released_fixture(h.now, 1)
        before["input_evidence"].update(sequence=15125, released=[False, False])
        before["input"].update(generation=15125)
        before["shot_input_generation"] = 15125
        before["input"]["hands"][0]["squeeze"] = 1.
        before["feed"]["input"] = copy.deepcopy(before["input_evidence"])
        v.reference = copy.deepcopy(before)
        v.latest_snapshot = copy.deepcopy(before)
        v.plans = [dict(stage="DetachContainer")]
        h.state.update(head=dict(pos=[0, 0, 0], ypr=[0, 0, 0]),
                       handL=dict(pos=[0, 0, 0], ypr=[0, 0, 0], followsHead=False),
                       handR=dict(pos=[0, 0, 0], ypr=[0, 0, 0]))
        v.initial_sim = copy.deepcopy(h.state)
        v.pending_nonce, v.snapshot_index = 2, 2
        v.request_bytes = h.files[v.request_path] = b"2\n"
        v.last_request = h.now - 30 * MS
        h.publish(before)
        h.response_due, h.response_after = h.now + response_ms * MS, 20 * MS
        publish = h.publish
        h.responses_seen = []

        def response(transport):
            first = not h.responses_seen
            _, snapshot = released_fixture(transport["observed_ns"], transport["nonce"],
                                            missing=first, advanced=not first)
            snapshot["input_evidence"]["sequence"] = 15127
            snapshot["input"]["generation"] = snapshot["shot_input_generation"] = 15127
            if first:
                snapshot["feedObservation"]["submit_attempts"] = 2
                snapshot["feedObservation"]["latest_tick"]["pending"] = True
            else:
                snapshot["feed"]["input"] = copy.deepcopy(snapshot["input_evidence"])
            if fault:
                fault(snapshot)
            h.responses_seen.append(copy.deepcopy(snapshot))
            publish(snapshot)

        h.publish = response
        target = dict(phase="release", grip=0, positionMeters=[0., 0., 0.],
                      yawPitchRollDegrees=[0., 0., 0.],
                      commands=["hand l grip pose " + " ".join(["0.000000000"] * 6), "grip l 0"])
        return h, before, target

    @staticmethod
    def finish_release(h, target):
        h.driver.send(target["commands"], expected=target, final_release=True)
        h.driver.motion_started = None
        h.driver.stage_deadline = h.driver.deadline

    def test_actual40_release_response_before_ack_enters_observation_only(self):
        h, before, target = self.release_race()
        v = h.driver
        original = copy.deepcopy(v.latest_snapshot)
        request, stage_deadline = v.request_bytes, v.stage_deadline
        self.finish_release(h, target)
        self.assertEqual(len(h.responses_seen), 1)
        missing = h.responses_seen[0]
        self.assertLess(missing["observed_ns"], v.last_commit)
        self.assertGreater(missing["input_evidence"]["observedNs"], v.last_write)
        self.assertFalse(v.pending_command)
        self.assertFalse(v.monitor)
        self.assertEqual(v.pending_nonce, 2)
        self.assertEqual(v.request_bytes, request)
        self.assertEqual(v.latest_snapshot, original)
        self.assertNotIn("receiptCounterAdvanced", v.plans[-1])
        write, commands = v.last_write, v.commands
        v.observe_stage_receipt(before)
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], d.json_bytes(missing))
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual((v.last_write, v.commands), (write, commands))
        self.assertTrue(v.plans[-1]["receiptCounterAdvanced"])
        self.assertFalse(v.plans[-1]["nativeReceiptProof"])
        self.assertEqual(sum(e["type"] == "postrelease_feed_unpublished" for e in v.artifacts.events), 1)
        self.assertEqual(v.reference, original)

    def test_release_race_retains_owner_config_reference_input_and_nonce_refusals(self):
        faults = [lambda s: s["owner"].update(actorGeneration=99),
                  lambda s: s["selected"].update(configurationPath="other"),
                  lambda s: s["input"].update(trackingBasisGeneration=99),
                  lambda s: s["input_evidence"].update(released=[False, False]),
                  lambda s: s["input"]["hands"][0].update(squeeze=1.),
                  lambda s: s.update(nonce=s["nonce"] + 1)]
        for index, fault in enumerate(faults):
            with self.subTest(fault=index):
                h, before, target = self.release_race(fault=fault)
                self.finish_release(h, target)
                with self.assertRaises(d.REFUSAL):
                    h.driver.observe_stage_receipt(before)
                self.assertEqual(len(h.command_writes), 1)
                self.assertEqual(h.driver.latest_snapshot, before)
                self.assertNotIn("receiptCounterAdvanced", h.driver.plans[-1])

    def test_release_ack_cannot_rebase_expired_response(self):
        h, before, target = self.release_race(ack_ms=140)
        self.finish_release(h, target)
        expired = h.responses_seen[0]
        self.assertLess(expired["valid_until_ns"], h.now)
        with self.assertRaises(d.REFUSAL):
            h.driver.observe_stage_receipt(before)
        self.assertEqual(h.driver.artifacts.saved["snapshot-002.json"], d.json_bytes(expired))
        self.assertEqual(h.driver.latest_snapshot, before)
        self.assertIsNone(h.driver.snapshot_refresh_started)
        self.assertEqual(len(h.command_writes), 1)

    def test_final_release_keeps_prepublication_admission(self):
        h, _, target = self.release_race()
        h.driver.last_write = h.now - 10 * MS
        h.response_due = h.now + MS
        with self.assertRaisesRegex(d.REFUSAL, "without an admitted retained mechanism"):
            self.finish_release(h, target)
        self.assertEqual(h.command_writes, [])
        self.assertTrue(h.driver.monitor)

    def test_final_release_refuses_expired_source_before_publication(self):
        h, before, target = self.release_race()
        h.driver.latest_snapshot["valid_until_ns"] = h.now
        with self.assertRaisesRegex(d.REFUSAL, "stale snapshot"):
            self.finish_release(h, target)
        self.assertEqual(h.command_writes, [])
        self.assertTrue(h.driver.monitor)

    def test_nonfinal_release_and_held_motion_keep_command_admission(self):
        for grip, phase in ((0, "neutral_at_contact"), (1, "endpoint_dwell")):
            with self.subTest(phase=phase):
                h, _, target = self.release_race()
                target.update(phase=phase, grip=grip)
                target["commands"][-1] = f"grip l {grip}"
                with self.assertRaisesRegex(d.REFUSAL, "without an admitted retained mechanism"):
                    h.driver.send(target["commands"], expected=target)
                self.assertTrue(h.driver.monitor)
                self.assertTrue(h.driver.pending_command)

    def test_wrong_release_cannot_request_observation_handoff(self):
        faults = [lambda t: t.update(phase="endpoint_dwell"),
                  lambda t: t.update(grip=1),
                  lambda t: t["commands"].__setitem__(-1, "grip l 1"),
                  lambda t: t["commands"].__setitem__(0, "trigger r 1"),
                  lambda t: t["commands"].insert(0, "grip r 1"),
                  lambda t: t["positionMeters"].__setitem__(0, .01)]
        for index, fault in enumerate(faults):
            with self.subTest(fault=index):
                h, _, target = self.release_race()
                fault(target)
                with self.assertRaisesRegex(d.REFUSAL, "exact planned final ordinary release"):
                    self.finish_release(h, target)
                self.assertEqual(h.command_writes, [])
                self.assertTrue(h.driver.monitor)

    def test_failed_publication_does_not_enter_observation_mode(self):
        h, _, target = self.release_race()
        def fail_command(path, raw):
            if path == h.driver.command_path:
                raise OSError("synthetic publication failure")
            h.write(path, raw)
        with patch.object(d, "atomic", fail_command):
            with self.assertRaisesRegex(OSError, "publication failure"):
                self.finish_release(h, target)
        self.assertTrue(h.driver.monitor)
        self.assertEqual(h.command_writes, [])

    def test_release_ack_still_refuses_competing_writer_and_original_deadlines(self):
        for failure in ("writer", "ack", "stage", "driver"):
            with self.subTest(failure=failure):
                h, _, target = self.release_race(ack_ms=250 if failure == "ack" else 55)
                v = h.driver
                if failure == "stage":
                    v.stage_deadline = h.now + 20 * MS
                elif failure == "driver":
                    v.deadline = h.now + 20 * MS
                elif failure == "writer":
                    def compete():
                        if h.command_writes:
                            h.files[v.command_path] = b"other writer\n"
                    h.on_sleep = compete
                with self.assertRaises(d.REFUSAL):
                    self.finish_release(h, target)
                self.assertTrue(v.pending_command)
                self.assertEqual(len(h.command_writes), 1)
                self.assertEqual(v.latest_snapshot["nonce"], 1)
                self.assertNotIn("receiptCounterAdvanced", v.plans[-1])

    def harness(self, responses, delay_ms=20):
        h = Harness(self)
        v = h.driver
        v.profiles, before = released_fixture(NS, 1)
        v.reference = copy.deepcopy(before)
        v.latest_snapshot = copy.deepcopy(before)
        v.plans = [dict(stage="DetachContainer")]
        v.monitor, v.pending_nonce = False, None
        v.last_request = h.now - d.SNAPSHOT_PERIOD
        h.state.update(head=dict(pos=[0, 0, 0], ypr=[0, 0, 0]),
                       handL=dict(pos=[0, 0, 0], ypr=[0, 0, 0], followsHead=False),
                       handR=dict(pos=[0, 0, 0], ypr=[0, 0, 0]), controls=dict(gripL=0.))
        v.initial_sim = copy.deepcopy(h.state)
        # Exercise the real command acknowledgement/control bookkeeping first.
        v.send(["grip l 0"])
        v.motion_started = None
        v.stage_deadline = v.deadline
        h.command_writes.clear()
        h.response_after = delay_ms * MS
        publish = h.publish
        h.responses_seen = []

        def response(transport):
            index = len(h.responses_seen)
            mutate = responses[min(index, len(responses) - 1)]
            _, snapshot = released_fixture(transport["observed_ns"], transport["nonce"])
            mutate(snapshot)
            h.responses_seen.append(copy.deepcopy(snapshot))
            publish(snapshot)

        h.publish = response
        return h, before

    def assert_no_new_commands(self, h, original_write, original_commands):
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.last_write, original_write)
        self.assertEqual(h.driver.commands, original_commands)

    @staticmethod
    def missing(snapshot):
        snapshot["feed"] = None

    @staticmethod
    def advanced(snapshot):
        snapshot["feedObservation"].update(submit_attempts=2, receipts_accepted=2)

    def test_transient_gap_retained_then_same_owner_feed_recovers(self):
        h, before = self.harness([self.missing, self.advanced])
        v = h.driver
        write, commands, deadline = v.last_write, v.commands, v.stage_deadline
        original = copy.deepcopy(v.latest_snapshot)

        def unchanged_while_missing():
            if len(h.responses_seen) == 1:
                self.assertEqual(v.latest_snapshot, original)
                self.assert_no_new_commands(h, write, commands)

        h.on_sleep = unchanged_while_missing
        v.observe_stage_receipt(before)
        self.assertEqual(len(h.responses_seen), 2)
        missing = h.responses_seen[0]
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], d.json_bytes(missing))
        self.assertTrue(v.plans[-1]["receiptCounterAdvanced"])
        self.assertFalse(v.plans[-1]["nativeReceiptProof"])
        self.assertEqual(v.latest_snapshot, h.responses_seen[-1])
        self.assertEqual(v.reference, original)
        self.assertEqual(v.stage_deadline, deadline)
        self.assert_no_new_commands(h, write, commands)
        self.assertEqual(sum(e["type"] == "postrelease_feed_unpublished" for e in v.artifacts.events), 1)
        # The preserved missing frame still fails ordinary command admission.
        with self.assertRaisesRegex(d.REFUSAL, "without an admitted retained mechanism"):
            v.admit_snapshot(missing, missing["observed_ns"], missing["nonce"])

    def test_advanced_counters_with_persistent_missing_feed_never_succeed(self):
        def missing_with_counters(snapshot):
            self.missing(snapshot)
            self.advanced(snapshot)

        h, before = self.harness([missing_with_counters])
        v, original = h.driver, copy.deepcopy(h.driver.latest_snapshot)
        write, commands, started = v.last_write, v.commands, h.now
        with self.assertRaisesRegex(d.REFUSAL, "no observed accepted-receipt"):
            v.observe_stage_receipt(before)
        self.assertEqual(len(h.responses_seen), 4)
        self.assertLess(h.now - started, 600 * MS)
        self.assertEqual(v.latest_snapshot, original)
        self.assertNotIn("receiptCounterAdvanced", v.plans[-1])
        self.assertEqual(sum(name.startswith("snapshot-") for name in v.artifacts.saved), 4)
        self.assert_no_new_commands(h, write, commands)

    def test_slow_publication_cannot_extend_600ms_observation_deadline(self):
        h, before = self.harness([self.missing], delay_ms=220)
        v, started = h.driver, h.now
        write, commands, deadline = v.last_write, v.commands, v.stage_deadline
        with self.assertRaisesRegex(d.REFUSAL, "post-stage receipt observation timed out"):
            v.observe_stage_receipt(before)
        self.assertLessEqual(h.now - started, 600 * MS)
        self.assertEqual(len(h.responses_seen), 2)
        self.assertEqual(v.stage_deadline, deadline)
        self.assert_no_new_commands(h, write, commands)

    def test_expired_gap_remains_refused_without_rebasing_source_lease(self):
        h, before = self.harness([self.missing])
        h.response_stale = True
        v, original = h.driver, copy.deepcopy(h.driver.latest_snapshot)
        write, commands = v.last_write, v.commands
        with self.assertRaisesRegex(d.REFUSAL, "without an admitted retained mechanism"):
            v.observe_stage_receipt(before)
        self.assertEqual(len(h.responses_seen), 1)
        stale = h.responses_seen[0]
        self.assertLess(stale["valid_until_ns"], h.now)
        self.assertEqual(v.artifacts.saved["snapshot-002.json"], d.json_bytes(stale))
        self.assertEqual(v.latest_snapshot, original)
        self.assertIsNone(v.snapshot_refresh_started)
        self.assert_no_new_commands(h, write, commands)

    def test_wrong_equipment_reference_or_held_input_refuses_gap_immediately(self):
        faults = [
            lambda s: s["equipment"].update(data=99),
            lambda s: s["owner"].update(actorGeneration=99),
            lambda s: s["input"].update(trackingBasisGeneration=99),
            lambda s: s["input_evidence"].update(released=[False, False]),
            lambda s: s["input"]["hands"][0].update(squeeze=1.),
            lambda s: s["input"]["hands"][1].update(trigger=1.),
            lambda s: s["input"]["hands"][0]["grip"]["position"].__setitem__(0, .01),
            lambda s: s["input_evidence"].update(tracked=[False, True]),
            lambda s: s["feedObservation"]["latest_tick"].update(claim=True),
        ]
        for index, fault in enumerate(faults):
            with self.subTest(fault=index):
                def changed(snapshot):
                    self.missing(snapshot)
                    fault(snapshot)

                h, before = self.harness([changed])
                v = h.driver
                write, commands = v.last_write, v.commands
                with self.assertRaises(d.REFUSAL):
                    v.observe_stage_receipt(before)
                self.assertEqual(len(h.responses_seen), 1)
                self.assertIn("snapshot-002.json", v.artifacts.saved)
                self.assert_no_new_commands(h, write, commands)

    def test_held_motion_or_unacknowledged_release_cannot_wait(self):
        for attribute, value in (("motion_started", NS), ("held_left", True),
                                 ("held_trigger", True), ("pending_command", True),
                                 ("last_commit", None)):
            with self.subTest(attribute=attribute):
                h, before = self.harness([self.missing])
                v = h.driver
                setattr(v, attribute, value)
                write, commands = v.last_write, v.commands
                with self.assertRaisesRegex(d.REFUSAL, "requires acknowledged ordinary release"):
                    v.observe_stage_receipt(before)
                self.assertEqual(len(h.responses_seen), 1)
                self.assert_no_new_commands(h, write, commands)

    def test_recovered_feed_must_match_lifetime_and_pass_full_admission(self):
        faults = [lambda s: s["feed"].update(lifetime=3),
                  lambda s: s["feed"].update(registrationKey=2),
                  lambda s: s["feed"]["input"].update(deadlineNs=s["observed_ns"]),
                  lambda s: s["feed"].update(chainCount=1)]
        for index, fault in enumerate(faults):
            with self.subTest(fault=index):
                def invalid_recovery(snapshot):
                    self.advanced(snapshot)
                    fault(snapshot)

                h, before = self.harness([self.missing, invalid_recovery])
                v = h.driver
                write, commands = v.last_write, v.commands
                with self.assertRaises(d.REFUSAL):
                    v.observe_stage_receipt(before)
                self.assertEqual(len(h.responses_seen), 2)
                self.assertNotIn("receiptCounterAdvanced", v.plans[-1])
                self.assert_no_new_commands(h, write, commands)


if __name__ == "__main__":
    unittest.main()
