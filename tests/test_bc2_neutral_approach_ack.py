"""Actual83/06-shaped host read delay; synthetic transport evidence only."""
import copy
import unittest
from unittest.mock import patch

import test_bc2_snapshot_readiness as readiness

d, MS = readiness.d, readiness.MS


def setup(test):
    h = readiness.harness(test, approach=True)
    v = h.driver
    h.now = v.last_request + 69 * MS
    v.last_write, v.last_commit = h.now - 60 * MS, h.now - 50 * MS
    h.state['handL']['followsHead'] = False
    v.state = copy.deepcopy(h.state)
    v.initial_sim = copy.deepcopy(h.state)
    target = dict(phase='approach', grip=0, positionMeters=[.046, 1.4874, -.6766],
                  yawPitchRollDegrees=[0., 0., 0.])
    target['commands'] = ['hand l grip pose ' + ' '.join(
        f'{x:.9f}' for x in target['positionMeters'] + target['yawPitchRollDegrees']), 'grip l 0']
    return h, target


def stall_read(test, h, *, stall_ms=190.1849, change=None):
    v = h.driver
    done = False

    def read(path, maximum):
        nonlocal done
        if path == v.command_path and len(h.command_writes) == 1 and not done:
            done = True
            # ACK state was sampled first. Native applies the command and
            # observes its response while this OS read occupies the driver.
            h.sleep(.010)
            h.sleep(stall_ms / 1000.)
            _, response = readiness.neutral_fixture(v.last_request + 94_856_300, v.pending_nonce)
            h.publish(response)
            if change:
                change(h)
        return h.files[path]

    test.enterContext(patch.object(d, 'read_bytes', read))


class NeutralApproachAckTests(unittest.TestCase):
    def test_actual83_06_read_stall_settles_once_then_discards_and_reacquires(self):
        h, target = setup(self)
        v = h.driver
        deadlines = v.stage_deadline, v.deadline, v.hard_deadline
        request_start = v.last_request
        stall_read(self, h)
        v.send(target['commands'], expected=target)
        self.assertEqual(len(h.command_writes), 1)
        self.assertEqual(v.commands, 3)
        self.assertFalse(v.pending_command)
        self.assertLess(v.last_commit, h.command_writes[0][0] + 250 * MS)
        self.assertGreater(h.now, request_start + 250 * MS)
        self.assertIsNone(v.latest_snapshot)
        self.assertIsNotNone(v.snapshot_readiness)
        self.assertEqual(v.snapshot_readiness['deadlineNs'], min(deadlines[:2]))
        events = [e['type'] for e in v.artifacts.events]
        self.assertLess(events.index('command_commit'), events.index('snapshot_response_timeout'))
        v.pause_until(h.now)
        self.assertIsNone(v.snapshot_readiness)
        self.assertLess(h.now, v.snapshot_source_deadline(v.latest_snapshot))
        self.assertGreater(v.latest_snapshot['shot_input_generation'],
                           (request_start + 94_856_300) // MS)
        self.assertGreaterEqual(v.latest_snapshot['input_evidence']['observedNs'], v.last_write)
        self.assertEqual(len(h.command_writes), 1)
        discarded = next(e for e in v.artifacts.events if e['type'] == 'snapshot_late_response_discarded')
        self.assertFalse(discarded['commandAuthority'])
        self.assertEqual((v.stage_deadline, v.deadline, v.hard_deadline), deadlines)

    def test_held_or_active_commands_keep_original_snapshot_refusal(self):
        cases = ['held_left', 'held_right', 'held_trigger', 'held_buttons',
                 'active_grip', 'unplanned', 'active_controls', 'nonapproach']
        for case in cases:
            with self.subTest(case=case):
                h, target = setup(self)
                v = h.driver
                if case.startswith('held_'):
                    setattr(v, case, {'a'} if case == 'held_buttons' else True)
                elif case == 'active_grip':
                    target['grip'], target['commands'][1] = 1, 'grip l 1'
                elif case == 'unplanned':
                    target = None
                elif case == 'active_controls':
                    h.state['controls']['trigR'] = .1
                elif case == 'nonapproach':
                    v.neutral_snapshot_approach = False
                stall_read(self, h)
                with self.assertRaisesRegex(d.REFUSAL, 'response not received within request budget'):
                    v.send(target['commands'] if target else ['grip l 0'], expected=target)
                self.assertEqual(len(h.command_writes), 1)
                self.assertTrue(v.pending_command)
                self.assertIsNone(v.snapshot_readiness)

    def test_original_ack_deadline_still_refuses_slow_os_read(self):
        h, target = setup(self)
        stall_read(self, h, stall_ms=251)
        with self.assertRaisesRegex(d.ReadDeadlineExceeded, 'original acknowledgement/read deadline'):
            h.driver.send(target['commands'], expected=target)
        self.assertTrue(h.driver.pending_command)
        self.assertEqual(len(h.command_writes), 1)

    def test_original_shorter_stage_deadline_is_not_extended(self):
        h, target = setup(self)
        original = h.driver.stage_deadline = h.now + 120 * MS
        stall_read(self, h)
        with self.assertRaisesRegex(d.ReadDeadlineExceeded, 'original acknowledgement/read deadline'):
            h.driver.send(target['commands'], expected=target)
        self.assertEqual(h.driver.stage_deadline, original)
        self.assertTrue(h.driver.pending_command)

    def test_exact_writer_sentinel_and_ack_remain_required(self):
        for kind in ('writer', 'sentinel', 'ack'):
            with self.subTest(kind=kind):
                h, target = setup(self)
                def change(h):
                    v = h.driver
                    if kind == 'writer':
                        h.files[v.command_path] = b'other writer\n'
                    elif kind == 'sentinel':
                        h.state['lastCmd'] = 'note another writer'
                    else:
                        h.files[v.ack_path] = h.files[v.ack_path].replace(b'last=', b'last=other ')
                stall_read(self, h, change=change)
                with self.assertRaisesRegex(d.REFUSAL, 'concurrent command writer|sentinel mismatch|batch text mismatch'):
                    h.driver.send(target['commands'], expected=target)
                self.assertTrue(h.driver.pending_command)
                self.assertEqual(len(h.command_writes), 1)


if __name__ == '__main__':
    unittest.main()
