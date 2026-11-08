"""Bounded passive post-shot settling; synthetic checks do not prove native success."""
import copy
from contextlib import ExitStack
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_fixed_container_simulation_plan as fixed
import test_bc2_fixed_preparation as preparation_tests

d = fixed.d
preparation = preparation_tests.d
MS = 1_000_000
REFUSALS = (d.transport.REFUSAL, d.planner.Refusal,
            preparation.transport.REFUSAL, preparation.fixed.Refusal)


class FixedSettlingTests(unittest.TestCase):
    class Driver(fixed.SelectedAmmoDriverTests.Driver):
        # Reuse production opt-in, keeping the existing synthetic family fixture.
        post_shot_request_limit = preparation.PreparationDriver.post_shot_request_limit
        pace_post_shot_refresh = preparation.PreparationDriver.pace_post_shot_refresh

    response = fixed.SelectedAmmoDriverTests.response
    missing = staticmethod(fixed.SelectedAmmoDriverTests.missing)
    harness = fixed.SelectedAmmoDriverTests.harness

    def harness_context(self):
        stack = ExitStack()
        previous = self.enterContext
        self.enterContext = stack.enter_context
        stack.callback(setattr, self, 'enterContext', previous)
        return stack

    def released(self):
        h, future, _ = self.harness(missing=False, right_squeeze=0.)
        v, advance = h.driver, h.sleep
        h.state['controls']['trigR'] = 0.

        def advance_commands(seconds):
            sequence = h.state['cmdSeq']
            frame, end = h.state['frame'], h.state['endFrames']
            advance(seconds)
            if h.state['cmdSeq'] != sequence:
                batch = h.files[v.command_path].decode('ascii').splitlines()
                h.state.update(frame=frame+3, endFrames=end+3)
                for command in batch:
                    if command.startswith('trigger r '):
                        h.state['controls']['trigR'] = float(command.split()[-1])
                h.files[v.ack_path] = (
                    f"seq={h.state['cmdSeq']} frame={frame+1} error=-\n"
                    f"last={' | '.join(batch)}\n").encode('ascii')

        self.enterContext(patch.object(d.transport.time, 'sleep', advance_commands))
        ready, _, now = v.fresh_available()
        before = preparation.baseline(ready, now)
        v.reference = ready
        v.send(['trigger r 1'])
        press = v.last_write
        v.pause_until(h.now+25*MS)
        v.send(['trigger r 0'])
        self.assertLessEqual(v.last_write-press, 100*MS)
        v.stage_deadline = min(v.deadline, v.last_commit+preparation.PreparationDriver.post_shot_observation_ns)
        v.pause_until(v.last_commit+100*MS)
        return h, future, before

    @staticmethod
    def expired(s):
        header = {key: s[key] for key in ('schema', 'nonce', 'observed_ns')}
        s.clear()
        s.update(header, valid=False, invalid_reason='shot_expired')

    def recovery(self, h, future, *, mutate=None):
        responses = []

        def respond(s):
            self.assertIsNone(h.driver.latest_snapshot)
            if s['observed_ns']-h.driver.last_commit < 681*MS:
                fixed.SelectedAmmoDriverTests.native_view_missing(s)
            else:
                s['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                if mutate:
                    mutate(s)
            responses.append(copy.deepcopy(s))

        future['mutate'] = respond
        return responses

    def assert_no_renewal(self, h, frozen):
        v = h.driver
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), frozen[0])
        self.assertEqual(h.command_writes, frozen[1])
        self.assertEqual(v.reference, frozen[2])
        self.assertEqual(v.commands, 4)
        self.assertFalse(v.post_shot_observing)

    @staticmethod
    def freeze(h):
        v = h.driver
        return ((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started),
                copy.deepcopy(h.command_writes), copy.deepcopy(v.reference))

    def test_fresh_same_owner_recovery_after_600ms_is_admitted_without_input(self):
        h, future, before = self.released()
        v, frozen = h.driver, self.freeze(h)
        responses = self.recovery(h, future)
        after, _, now = v.fresh_available()
        self.assertGreater(now-v.last_commit, 600*MS)
        self.assertLess(now, v.stage_deadline)
        self.assertGreater(len(responses), 3)
        self.assertEqual(preparation.after_shot(after, now, before), dict(loaded=0, reserve=7, capacity=1))
        self.assertIs(v.latest_snapshot, after)
        self.assertLess(now, v.snapshot_source_deadline(after))
        self.assert_no_renewal(h, frozen)
        events = [e for e in v.artifacts.events if e['type'] == 'selected_ammo_post_shot_unpublished']
        self.assertTrue(events)
        self.assertTrue(all(e['commandAuthority'] is False for e in events))
        with self.assertRaisesRegex(d.transport.REFUSAL, 'already attempted'):
            v.fresh_available()
        with self.assertRaises(REFUSALS):
            v.send(['trigger r 1'])
        self.assertEqual(h.command_writes, frozen[1])

    def test_continuous_absence_or_expiry_is_bounded_and_cadenced(self):
        for missing in (fixed.SelectedAmmoDriverTests.native_view_missing, self.expired):
            with self.subTest(missing=missing), self.harness_context():
                h, future, _ = self.released()
                v, frozen, index = h.driver, self.freeze(h), h.driver.snapshot_index
                future['mutate'] = missing
                with self.assertRaises(REFUSALS):
                    v.fresh_available()
                self.assertIsNone(v.latest_snapshot)
                self.assertLessEqual(h.now-v.last_commit, d.NS+MS)
                self.assertLessEqual(v.snapshot_index-index, 10)
                timings = [value for name, value in v.artifacts.saved.items()
                           if name.startswith('request-') and name.endswith('-timing.json')
                           and int(name.split('-')[1]) > index]
                self.assertGreater(len(timings), 3)
                self.assertTrue(all(b['requestStartedNs']-a['requestStartedNs'] >= d.transport.SNAPSHOT_PERIOD
                                    for a, b in zip(timings, timings[1:])))
                self.assertTrue(all(t['responseDeadlineNs']-t['requestStartedNs'] == d.transport.SNAPSHOT_TIMEOUT
                                    for t in timings))
                self.assert_no_renewal(h, frozen)

    def test_original_overall_deadline_wins_before_recovery(self):
        h, future, _ = self.released()
        v = h.driver
        v.deadline = v.stage_deadline = v.last_commit+450*MS
        frozen = self.freeze(h)
        self.recovery(h, future)
        with self.assertRaises(REFUSALS):
            v.fresh_available()
        self.assertLessEqual(h.now, v.deadline+MS)
        self.assertIsNone(v.latest_snapshot)
        self.assert_no_renewal(h, frozen)

    def test_response_timeout_does_not_inherit_the_one_second_budget(self):
        h, future, _ = self.released()
        v, frozen = h.driver, self.freeze(h)
        h.response_after = d.transport.SNAPSHOT_TIMEOUT+MS
        future['mutate'] = lambda s: s['fixedContainer']['view']['snapshot']['counts'].update(loaded=0)
        with self.assertRaises(REFUSALS):
            v.fresh_available()
        # Existing neutral-observation handling may await a late diagnostic
        # before asking for a fresh response. Each response still expires at
        # its original deadline; the diagnostic never becomes action evidence.
        timeouts = [e for e in v.artifacts.events if e['type'] == 'snapshot_response_timeout']
        self.assertTrue(timeouts)
        for event in timeouts:
            self.assertEqual(event['budgetNs'], d.transport.SNAPSHOT_TIMEOUT)
            self.assertEqual(event['responseDeadlineNs'],
                             event['requestStartedNs']+d.transport.SNAPSHOT_TIMEOUT)
            self.assertGreater(event['elapsedNs'], d.transport.SNAPSHOT_TIMEOUT)
        self.assertLessEqual(h.now-v.last_commit, d.NS+MS)
        self.assertIsNone(v.latest_snapshot)
        self.assert_no_renewal(h, frozen)

    def test_delayed_replacement_still_requires_current_idle_exact_identity_and_custody(self):
        faults = (
            lambda s: s['owner'].update(weapon=999),
            lambda s: s['sight']['family'].update(persistentId='changed'),
            lambda s: s['fixedContainer']['view']['binding'].update(switching=999),
            lambda s: s['fixedContainer']['view']['identity'].update(serverItem=999),
            lambda s: s['fixedContainer']['tracking']['gun']['token'].update(id=999),
            lambda s: s['fixedContainer']['tracking']['gun'].update(deadlineNs=s['observed_ns']),
            lambda s: s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns']),
            lambda s: s['fixedContainer']['view']['snapshot'].update(coherentIdleVerified=False),
            lambda s: s['fixedContainer']['view'].update(phase=2),
            lambda s: s['fixedContainer'].update(pendingRequest={}),
            lambda s: s.update(shot_input_generation=1),
            lambda s: s['input']['hands'][1].update(trigger=1.),
        )
        for fault in faults:
            with self.subTest(fault=fault), self.harness_context():
                h, future, _ = self.released()
                frozen = self.freeze(h)
                self.recovery(h, future, mutate=fault)
                with self.assertRaises(REFUSALS):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.latest_snapshot)
                self.assert_no_renewal(h, frozen)

    def test_settling_does_not_make_wrong_final_counts_a_success(self):
        for counts in (dict(loaded=1, reserve=7), dict(loaded=0, reserve=6)):
            with self.subTest(counts=counts), self.harness_context():
                h, future, before = self.released()
                frozen = self.freeze(h)
                self.recovery(h, future, mutate=lambda s: s['fixedContainer']['view']['snapshot']['counts'].update(counts))
                after, _, now = h.driver.fresh_available()
                with self.assertRaises(REFUSALS):
                    preparation.after_shot(after, now, before)
                self.assert_no_renewal(h, frozen)

    def test_custody_loss_while_waiting_refuses_without_another_request_or_command(self):
        for fault in ('writer', 'capture', 'input', 'pending'):
            with self.subTest(fault=fault), self.harness_context():
                h, future, _ = self.released()
                v, frozen = h.driver, self.freeze(h)

                def change(s):
                    fixed.SelectedAmmoDriverTests.native_view_missing(s)
                    if fault == 'writer':
                        h.files[v.command_path] = b'foreign\n'
                    elif fault == 'capture':
                        h.state['captureSeq'] += 1
                    elif fault == 'input':
                        h.state['controls']['trigR'] = 1.
                    else:
                        v.pending_command = True

                future['mutate'] = change
                index = v.snapshot_index
                with self.assertRaises(REFUSALS):
                    v.fresh_available()
                self.assertEqual(v.snapshot_index-index, 1)
                self.assertIsNone(v.latest_snapshot)
                self.assert_no_renewal(h, frozen)

    def test_unopted_selected_observer_still_refuses_after_three_requests(self):
        h, future, _ = self.released()
        v = h.driver
        v.__class__ = fixed.SelectedAmmoDriverTests.Driver
        frozen, index = self.freeze(h), v.snapshot_index
        future['mutate'] = fixed.SelectedAmmoDriverTests.native_view_missing
        with self.assertRaisesRegex(d.transport.REFUSAL, 'refresh request budget exhausted'):
            v.fresh_available()
        self.assertEqual(v.snapshot_index-index, 3)
        self.assertIsNone(v.latest_snapshot)
        self.assert_no_renewal(h, frozen)


class PreparationSettlingPolicyTests(unittest.TestCase):
    def test_prepare_anchors_once_at_release_and_clips_original_driver_deadline(self):
        for remaining in (450*MS, 2*d.NS):
            with self.subTest(remaining=remaining), tempfile.TemporaryDirectory() as temp, \
                    patch.object(preparation.time, 'perf_counter_ns', return_value=preparation_tests.NOW):
                v, path, sent, _ = preparation_tests.FixedPreparationTests().pulse_driver(Path(temp))
                original_deadline = v.deadline = preparation_tests.NOW+remaining
                result = v.prepare(path)
                self.assertEqual(v.deadline, original_deadline)
                self.assertEqual(v.stage_deadline, min(original_deadline, result['releaseCommittedNs']+d.NS))
                self.assertEqual(sent, ['trigger r 1', 'trigger r 0'])
                self.assertEqual(v.post_shot_request_limit, d.NS//d.transport.SNAPSHOT_PERIOD)

if __name__ == '__main__':
    unittest.main()
