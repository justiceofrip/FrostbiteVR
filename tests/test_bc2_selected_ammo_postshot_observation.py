"""Read-only post-shot expiry recovery; synthetic admission is not native proof."""
import copy
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import unittest

import test_bc2_fixed_container_simulation_plan as fixed

d = fixed.d
MS = 1_000_000


class SelectedAmmoPostshotObservationTests(unittest.TestCase):
    MS = MS
    Driver = fixed.SelectedAmmoDriverTests.Driver
    response = fixed.SelectedAmmoDriverTests.response
    missing = staticmethod(fixed.SelectedAmmoDriverTests.missing)
    harness = fixed.SelectedAmmoDriverTests.harness
    shot_harness = fixed.SelectedAmmoDriverTests.shot_harness

    def harness_context(self):
        stack = ExitStack()
        previous = self.enterContext
        self.enterContext = stack.enter_context
        stack.callback(setattr, self, 'enterContext', previous)
        return stack

    @staticmethod
    def expired(snapshot):
        # Actual run06 snapshot005 omits frame/ammo identity on shot_expired.
        header = {key: snapshot[key] for key in ('schema', 'nonce', 'observed_ns')}
        snapshot.clear()
        snapshot.update(header, valid=False, invalid_reason='shot_expired')

    def transition(self, future, mutation=None):
        responses = []

        def respond(snapshot):
            if not responses:
                self.expired(snapshot)
            else:
                snapshot['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                if mutation:
                    mutation(snapshot)
            responses.append(copy.deepcopy(snapshot))

        future['mutate'] = respond
        return responses

    def test_expired_then_fresh_empty_requires_full_admission_without_another_trigger(self):
        h, future, ready = self.shot_harness()
        v, original = h.driver, copy.deepcopy(ready)
        clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
        commands = copy.deepcopy(h.command_writes)
        responses = self.transition(future)
        result, _, _ = v.fresh_available()
        self.assertEqual(result['fixedContainer']['view']['snapshot']['counts'],
                         dict(loaded=0, reserve=7, capacity=1))
        self.assertIs(v.latest_snapshot, result)
        self.assertEqual(len(responses), 2)
        self.assertIn(d.transport.json_bytes(responses[0]), v.artifacts.saved.values())
        self.assertEqual(v.reference, original)
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
        self.assertEqual(h.command_writes, commands)
        self.assertEqual(v.commands, 4)
        held = [e for e in v.artifacts.events if e['type'] == 'selected_ammo_post_shot_expired']
        self.assertEqual(len(held), 1)
        self.assertFalse(held[0]['commandAuthority'])
        self.assertFalse(v.post_shot_observing)
        with self.assertRaisesRegex(d.transport.REFUSAL, 'already attempted'):
            v.fresh_available()
        with self.assertRaises(d.transport.REFUSAL):
            v.send(['trigger r 1'])
        self.assertEqual(h.command_writes, commands)

    def test_replacement_still_requires_exact_native_owner_binding_root_and_current_input(self):
        mutations = (
            lambda s: s['owner'].update(weapon=999),
            lambda s: s['sight']['family'].update(persistentId='changed'),
            lambda s: s['fixedContainer']['view']['binding'].update(switching=999),
            lambda s: s['fixedContainer']['view']['identity'].update(serverItem=999),
            lambda s: s['fixedContainer']['tracking']['gun']['token'].update(id=999),
            lambda s: s['fixedContainer']['view']['binding'].update(deadlineNs=s['observed_ns']),
            lambda s: s.update(shot_input_generation=1),
            lambda s: s['input']['hands'][1].update(trigger=1.),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.harness_context():
                h, future, ready = self.shot_harness()
                self.transition(future, mutation)
                commands = copy.deepcopy(h.command_writes)
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.fresh_available()
                self.assertIsNone(h.driver.latest_snapshot)
                self.assertIs(h.driver.reference, ready)
                self.assertEqual(h.command_writes, commands)

    def test_other_invalid_reasons_and_malformed_expiry_are_not_deferred(self):
        for fault in ('reason', 'valid', 'schema', 'future', 'preceding', 'nonce'):
            with self.subTest(fault=fault), self.harness_context():
                h, future, _ = self.shot_harness()
                v = h.driver

                def invalid(snapshot):
                    self.expired(snapshot)
                    if fault == 'reason':
                        snapshot['invalid_reason'] = 'body_admission_refused_or_expired'
                    elif fault == 'valid':
                        snapshot['valid'] = None
                    elif fault == 'schema':
                        snapshot['schema'] = 'different'
                    elif fault == 'future':
                        snapshot['observed_ns'] += 1
                    elif fault == 'preceding':
                        snapshot['observed_ns'] = v.last_request-1
                    else:
                        snapshot['nonce'] += 1

                future['mutate'] = invalid
                commands = copy.deepcopy(h.command_writes)
                with self.assertRaises(d.transport.REFUSAL):
                    v.fresh_available()
                self.assertFalse(any(e['type'] == 'selected_ammo_post_shot_expired'
                                     for e in v.artifacts.events))
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, commands)

    def test_expiry_outside_acknowledged_postshot_scope_is_not_deferred(self):
        h, _, initial = self.harness(missing=False, right_squeeze=0.)
        self.expired(initial)
        h.publish(initial)
        with self.assertRaisesRegex(d.transport.REFUSAL, 'native snapshot refused: shot_expired'):
            h.driver.fresh_available()
        self.assertEqual(h.command_writes, [])

    def test_expiry_keeps_request_limit_deadlines_and_exclusive_neutral_owner(self):
        for fault in ('requests', 'deadline', 'writer', 'capture', 'input', 'pending', 'response'):
            with self.subTest(fault=fault), self.harness_context():
                h, future, _ = self.shot_harness()
                v = h.driver

                def expire(snapshot):
                    self.expired(snapshot)
                    if fault == 'writer':
                        h.files[v.command_path] = b'foreign\n'
                    elif fault == 'capture':
                        h.state['captureSeq'] += 1
                    elif fault == 'input':
                        h.state['controls']['trigR'] = 1.
                    elif fault == 'pending':
                        v.pending_command = True

                future['mutate'] = expire
                if fault == 'deadline':
                    v.stage_deadline = h.now+70*MS
                elif fault == 'response':
                    h.response_after = d.transport.SNAPSHOT_TIMEOUT+MS
                clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
                index, commands = v.snapshot_index, copy.deepcopy(h.command_writes)
                with self.assertRaises(d.transport.REFUSAL):
                    v.fresh_available()
                self.assertLessEqual(v.snapshot_index-index, 1+d.transport.MAX_SNAPSHOT_REFRESHES)
                self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
                self.assertIsNone(v.latest_snapshot)
                self.assertEqual(h.command_writes, commands)

    @unittest.skipUnless(os.environ.get('BC2_SELECTED_AMMO_POSTSHOT_REPLAY'),
                         'optional retained actual452 fixed65 run06 expiry replay')
    def test_actual_run06_expiry_is_saved_then_waits_without_claiming_shot_or_ammo_success(self):
        folder = Path(os.environ['BC2_SELECTED_AMMO_POSTSHOT_REPLAY'])
        pins = {
            'snapshot-004.json': '499f7784f19cd2fa678b50e79d650fb39f2fdd0ec1c1e914e0c1b010417e93a6',
            'snapshot-005.json': 'fdba420662de562f80a51969b3362e6f9b12cb65e266eb9aa99d49e9f220d061',
            'events.jsonl': '72b39526b6d3f8df241c2715d2075f3f59620e975606f3ffd8db500b873c4aca',
            'request-005-timing.json': 'ad4ccf903859ebb280c96b94b2d04a3abe13260acda32855d9c18c64559b5101',
        }
        blobs = {name: (folder/name).read_bytes() for name in pins}
        self.assertEqual({name: hashlib.sha256(raw).hexdigest() for name, raw in blobs.items()}, pins)
        before, expired = [json.loads(blobs[name]) for name in ('snapshot-004.json', 'snapshot-005.json')]
        events = [json.loads(line) for line in blobs['events.jsonl'].splitlines()]
        commits = [e for e in events if e['type'] == 'command_commit']
        requests = [e for e in events if e['type'] == 'command_request']
        self.assertEqual([e['commands'][0] for e in requests], ['trigger r 1', 'trigger r 0'])
        self.assertEqual(len(commits), 2)
        h, _, _ = self.harness(missing=False, right_squeeze=0.)
        v = h.driver
        h.now = expired['observed_ns']+MS
        h.state = copy.deepcopy(commits[-1]['state'])
        v.state = v.initial_sim = copy.deepcopy(h.state)
        v.reference = v.latest_snapshot = before
        v.demand_finished, v.commands = True, 4
        v.demand_started = before['observed_ns']
        v.deadline, v.hard_deadline, v.stage_deadline = h.now+d.NS, h.now+2*d.NS, h.now+100*MS
        clocks = v.deadline, v.hard_deadline, v.stage_deadline, v.demand_started
        v.last_write, v.last_commit = commits[-1]['writtenNs'], commits[-1]['committedNs']
        v.sequence, v.sentinel, v.capture_sequence = h.state['cmdSeq'], h.state['lastCmd'], h.state['captureSeq']
        v.pending_batch = requests[-1]['commands']
        h.files[v.command_path] = ('\n'.join(v.pending_batch)+'\n').encode()
        v.command_hash = d.transport.digest(h.files[v.command_path])
        timing = json.loads(blobs['request-005-timing.json'])
        v.last_request, v.pending_nonce, v.snapshot_index = timing['requestStartedNs'], int(expired['nonce']), 5
        v.request_bytes = (str(v.pending_nonce)+'\n').encode()
        h.files[v.request_path] = v.request_bytes
        h.publish(expired)
        h.files[v.snapshot_path] = blobs['snapshot-005.json']
        h.response_after = None
        with self.assertRaisesRegex(d.transport.REFUSAL, 'maximumGestureNs|post-stage receipt observation timed out'):
            v.fresh_available()
        self.assertEqual(v.artifacts.saved['snapshot-005.json'], blobs['snapshot-005.json'])
        self.assertEqual((v.deadline, v.hard_deadline, v.stage_deadline, v.demand_started), clocks)
        self.assertEqual(v.snapshot_index, 6)
        self.assertIs(v.reference, before)
        self.assertIsNone(v.latest_snapshot)
        self.assertEqual(h.command_writes, [])
        self.assertEqual(len([e for e in v.artifacts.events
                              if e['type'] == 'selected_ammo_post_shot_expired']), 1)


if __name__ == '__main__':
    unittest.main()
