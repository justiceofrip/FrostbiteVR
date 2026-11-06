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
        def fail_command(path, raw, **kwargs):
            if path == h.driver.command_path:
                raise OSError("synthetic publication failure")
            h.write(path, raw, **kwargs)
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


class FeedPostreloadShotTests(unittest.TestCase):
    @staticmethod
    def ready(now=NS, nonce=1):
        profiles, s = released_fixture(now, nonce)
        s["weapon_actions_blocked"] = s["feed"]["blocksFire"] = False
        s["feed"]["coverRadians"] = 0.
        s["feedObservation"]["latest_tick"].update(gate_failed=False, submit_refused=False)
        mapping = {k: copy.deepcopy(s[k]) for k in ("owner", "equipment", "rig", "physical_equipment_generation")}
        mapping["selected"] = {k: s["selected"][k] for k in
                               ("configurationPath", "configurationPathPointer", "inventory", "selectedSlot",
                                "soleConfiguredArray", "matchedMeshPath", "meshes")}
        return profiles, s, dict(mapping=mapping, lifetime=s["feed"]["lifetime"])

    def test_closed_cover_alone_cannot_hide_custody_claim_or_stale_owner(self):
        faults = (lambda s: s["feed"].update(carriedPartsMask=2),
                  lambda s: s["feed"].update(supplyObject={}),
                  lambda s: s["feed"].update(mechanismClaim={}),
                  lambda s: s["feed"].update(mechanismInput=s["input_evidence"]),
                  lambda s: s["feed"].update(lifetime=9),
                  lambda s: s["feed"].update(coverRadians=.01),
                  lambda s: s.update(weapon_actions_blocked=True),
                  lambda s: s["input"]["hands"][1].update(trigger=1),
                  lambda s: s["owner"].update(equipGeneration=99),
                  lambda s: s.update(valid_until_ns=NS))
        for fault in faults:
            with self.subTest(fault=fault):
                profiles, s, closure = self.ready()
                fault(s)
                with self.assertRaises(d.REFUSAL):
                    d.post_reload_ready(profiles, s, NS, closure, neutral=True)
        profiles, s, closure = self.ready()
        original = copy.deepcopy(s)
        d.post_reload_ready(profiles, s, NS, closure, neutral=True)
        self.assertEqual(s, original)

    @staticmethod
    def native(rounds=1):
        import struct
        _, s, closure = FeedPostreloadShotTests.ready()
        boundary = dict(schema="bc2vr.feed-post-reload-pulse.v1", status="native_shot_observation_pending",
                        owner=s["owner"], closure=closure, nativeAcceptance=False,
                        neutralWrittenNs=NS, pressWrittenNs=NS+200*MS, releaseWrittenNs=NS+240*MS,
                        releaseCommittedNs=NS+250*MS, observationEndNs=NS+500*MS)
        owner = dict(player=1, soldier=2, weak=3, weapon=4, actor_generation=5, equip_generation=6, space=7)
        rows = []
        for branch in range(3):
            steps = [(150, 200, 200, 2, 2, 0, 0)]
            steps += [(210+20*i, 200-i, 199-i, 2 if i == 0 else 9, 9, 1, 77+i) for i in range(rounds)]
            steps += [(350, 200-rounds, 200-rounds, 2, 2, 0, 0)]
            for begin, before, after, previous, state, flags, token in steps:
                b = dict(owner, branch=branch, firing=100+branch, loaded=before, reserve=487,
                         current=previous, next=previous, previous=2, timer=0., flags_a8=0,
                         snapshot_sequence=10, wrapper_offset=16 if branch == 2 else 0, soldier_flags=0,
                         server_player=201 if branch == 2 else 0, server_soldier=202 if branch == 2 else 0,
                         server_item=203 if branch == 2 else 0)
                a = dict(b, loaded=after, current=state, next=state)
                raw = bytearray(0x30)
                struct.pack_into("<fIf", raw, 0x18, .016, token, 1.)
                struct.pack_into("<I", raw, 0x2c, flags)
                context = dict(decoded_valid=True, input_flags=flags, raw_1c=token, bytes=list(raw),
                               delta_seconds=.016, reload_multiplier=1.)
                # Real writer: numeric guard bools; closed positive-ammo feed
                # still applies/restores a no-op pair. This is synthetic data.
                guards = {k: dict(requested=0, applied=0, restored=0, unexpected_write=0)
                          for k in ("fixed_input", "pistol_input", "cycle_guard")}
                guards["feed_input"] = dict(requested=1, applied=1, restored=1, unexpected_write=0,
                                             original=[0, flags], effective=[0, flags], before_restore=[0, flags])
                rows.append(dict(kind=0, depth=1, parent=0, update=0, native_parent=0,
                    thread=10+branch, caller=1234, context=5678, argument=0,
                    finished=True, identity_retained=True,
                    begin_ns=NS+begin*MS, end_ns=NS+(begin+5)*MS,
                    begin_tick_ms=1000+begin, end_tick_ms=1005+begin,
                    before=b, after=a, context_before=copy.deepcopy(context), context_after=copy.deepcopy(context),
                    transfer_path=None, hold_requested=False, hold_applied=False, **guards))
        rows.sort(key=lambda r: (r["begin_ns"], r["before"]["branch"]))
        for index, row in enumerate(rows, 1):
            row.update(id=index, native_invocation=100+index, native_update=100+index)
        flow = dict(started=True, drained=True, server_binding_verified=True, in_flight=0,
                    capacity=20480, window_seconds=20, start_ns=NS-NS//2,
                    dropped=0, record_lock_drops=0, record_begin_lock_drops=0, record_end_lock_drops=0,
                    owner_lock_drops=0, records=rows)
        return flow, boundary

    @staticmethod
    def event(flow, branch=2, index=0):
        return [r for r in flow["records"] if r["before"]["branch"] == branch
                and r["before"]["loaded"] != r["after"]["loaded"]][index]

    def test_matching_native_fire_is_observed_without_complete_capture_claim(self):
        flow, boundary = self.native()
        result = d.audit_post_reload_shot(flow, boundary)
        self.assertTrue(result["nativeShotObserved"])
        self.assertEqual((result["loadedBefore"], result["loadedAfter"], result["reserve"]), (200, 199, 487))
        self.assertEqual(result["roundsObserved"], 1)
        self.assertEqual(len(result["observedBounds"]), 3)
        self.assertFalse(result["completeWindow"])
        self.assertFalse(result["nativeAcceptance"])
        self.assertFalse(result["headsetAcceptance"])

    def test_one_ordinary_burst_with_unlocalized_drops_discloses_limited_evidence(self):
        flow, boundary = self.native(rounds=2)
        # Scaled recorder capacity: events and final ready rows survive; later
        # records did not. Lock counters carry no timestamps to localize them.
        flow.update(capacity=len(flow["records"]), dropped=3, record_lock_drops=5,
                    record_begin_lock_drops=2, record_end_lock_drops=3, owner_lock_drops=1)
        result = d.audit_post_reload_shot(flow, boundary)
        self.assertTrue(result["nativeShotObserved"])
        self.assertEqual((result["roundsObserved"], result["loadedAfter"]), (2, 198))
        self.assertEqual([[e["token"] for e in result["events"] if e["branch"] == n]
                          for n in range(3)], [[77, 78]] * 3)
        self.assertEqual(result["nativeDropCounters"], {k: flow[k] for k in result["nativeDropCounters"]})
        self.assertFalse(result["completeWindow"])
        self.assertFalse(result["nativeAcceptance"])

    def test_missing_events_mismatched_tokens_and_unexplained_counts_refuse(self):
        def duplicate(f, b):
            extra = copy.deepcopy(self.event(f))
            extra.update(id=99, begin_ns=NS+220*MS, end_ns=NS+225*MS)
            f["records"].append(extra)
        faults = (lambda f, b: f.update(records=[]),
                  lambda f, b: f.update(drained=False),
                  lambda f, b: f.update(records=[r for r in f["records"] if r["before"]["branch"] != 2]),
                  lambda f, b: self.event(f)["before"].update(equip_generation=99),
                  lambda f, b: self.event(f)["after"].update(server_item=999),
                  lambda f, b: self.event(f)["context_after"].update(raw_1c=78),
                  lambda f, b: self.event(f).update(hold_applied=True),
                  lambda f, b: self.event(f)["cycle_guard"].update(applied=1),
                  lambda f, b: self.event(f)["after"].update(loaded=198),
                  lambda f, b: self.event(f)["after"].update(reserve=486),
                  lambda f, b: self.event(f)["after"].update(current=2, next=2),
                  lambda f, b: b.update(releaseWrittenNs=b["pressWrittenNs"]+101*MS,
                                        releaseCommittedNs=b["pressWrittenNs"]+102*MS),
                  lambda f, b: b.update(releaseCommittedNs=b["releaseWrittenNs"]+251*MS), duplicate)
        for fault in faults:
            with self.subTest(fault=fault):
                flow, boundary = self.native()
                fault(flow, boundary)
                with self.assertRaises(d.REFUSAL):
                    d.audit_post_reload_shot(flow, boundary)

    def test_feed_filter_only_passes_exact_restored_unchanged_native_words(self):
        faults = (lambda r: r["feed_input"].update(effective=[1, 0]),
                  lambda r: r["feed_input"].update(restored=0),
                  lambda r: r["feed_input"].update(unexpected_write=1),
                  lambda r: r["feed_input"].update(before_restore=[0, 0]),
                  lambda r: r["context_after"]["bytes"].__setitem__(0x2c, 0))
        for fault in faults:
            with self.subTest(fault=fault):
                flow, boundary = self.native()
                fault(self.event(flow))
                with self.assertRaises(d.REFUSAL):
                    d.audit_post_reload_shot(flow, boundary)

    def test_drops_cannot_excuse_missing_event_or_post_ready_boundary(self):
        for missing in ("event", "post"):
            flow, boundary = self.native(rounds=2)
            if missing == "event":
                flow["records"].remove(self.event(flow, index=1))
            else:
                flow["records"] = [r for r in flow["records"] if r["begin_ns"] < NS+300*MS]
            flow.update(dropped=100, record_lock_drops=5, owner_lock_drops=2)
            with self.subTest(missing=missing), self.assertRaises(d.REFUSAL):
                d.audit_post_reload_shot(flow, boundary)

    def test_burst_tokens_must_be_unique_and_match_all_copies(self):
        for token in (77, 79):
            flow, boundary = self.native(rounds=2)
            event = self.event(flow, index=1)
            for name in ("context_before", "context_after"):
                event[name]["raw_1c"] = event[name]["bytes"][0x1c] = token
            with self.subTest(token=token), self.assertRaises(d.REFUSAL):
                d.audit_post_reload_shot(flow, boundary)

    def test_real_counts_without_a_native_fire_event_are_not_success(self):
        flow, boundary = self.native()
        flow["records"] = [r for r in flow["records"] if r["before"]["loaded"] == r["after"]["loaded"]]
        with self.assertRaisesRegex(d.REFUSAL, "native shot observation absent"):
            d.audit_post_reload_shot(flow, boundary)

    def test_single_pulse_rearms_then_releases_and_a_second_attempt_is_refused(self):
        import tempfile
        from test_bc2_feed_driver_observation import MemoryArtifacts
        with tempfile.TemporaryDirectory() as temp:
            v = d.Driver.__new__(d.Driver)
            v.run, v.artifacts = d.Path(temp), MemoryArtifacts()
            path = v.run / "close" / "result.json"
            v.host, v.manifest = dict(pid=7), dict(source_sha256="source")
            v.pending_command = v.unsafe_owner = v.held_left = v.held_right = v.held_trigger = False
            v.held_buttons, v.reference = set(), None
            v.deadline, v.stage_deadline = 10*NS, 10*NS
            clock, commands = [NS], []
            profiles, s, closure = self.ready()
            v.profiles = profiles
            closed = dict(schema="bc2vr.reload-stimulus-result.v1", status="ordinary_stimulus_completed",
                          stimulusCompleted=True, boundaryChecksPassed=True, observerStopped=True,
                          observationIncomplete=False, leftGripHeld=False, pendingCommand=False,
                          plans=[dict(family="feed", stage="CloseCover")],
                          boundaries=dict(feed_released=dict(receiptCounterAdvanced=True, lifetime=2)))
            plan = dict(schema="bc2vr.reload-stimulus.v1", family="feed", stage="CloseCover", mapping=closure["mapping"])
            sources = {v.run / "live-driver-owner.json": dict(artifactDirectory=str(path.parent)),
                       path: closed, path.parent / "stimulus-01.json": plan}
            generation = [10]
            def fresh():
                clock[0] += 20*MS
                _, current, _ = self.ready(clock[0], generation[0])
                current["input"]["generation"] = current["shot_input_generation"] = generation[0]
                current["input_evidence"]["sequence"] = generation[0]
                current["feed"]["input"]["sequence"] = generation[0]
                generation[0] += 1
                v.latest_snapshot = current
                return current, b"{}", clock[0]
            def send(lines, **kwargs):
                v.validate_publication(lines, None, False)
                clock[0] += 25*MS
                if "source_deadline" in kwargs:
                    d.require(clock[0] < kwargs["source_deadline"], "expired")
                v.last_write = clock[0]
                clock[0] += 5*MS
                v.last_commit = clock[0]
                v.remember_controls(lines)
                commands.append(lines)
            v.fresh, v.send = fresh, send
            v.pause_until = lambda until: clock.__setitem__(0, max(clock[0], until))
            with patch.object(d, "child", side_effect=lambda root, p: p), \
                 patch.object(d, "read_json", side_effect=lambda p, *a: (copy.deepcopy(sources[p]), b"{}")), \
                 patch.object(d.time, "perf_counter_ns", side_effect=lambda: clock[0]):
                for failure_field in ("error", "observationFailure", "diagnosticFailure"):
                    sources[path][failure_field] = "retained CloseCover failure"
                    with self.subTest(field=failure_field), self.assertRaises(d.REFUSAL):
                        v.post_reload_shot(path)
                    sources[path][failure_field] = None
                self.assertEqual(commands, [])
                result = v.post_reload_shot(path)
                self.assertEqual(commands, [["grip l 0", "trigger r 0"], ["trigger r 1"], ["trigger r 0"]])
                self.assertFalse(v.held_trigger)
                self.assertFalse(result["nativeShotObserved"])
                self.assertEqual(result["status"], "native_shot_observation_pending")
                with self.assertRaises(FileExistsError):
                    v.post_reload_shot(path)
                self.assertEqual(len(commands), 3)


if __name__ == "__main__":
    unittest.main()
