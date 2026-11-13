"""Deterministic assembled trigger transport; no game or physical pulse proof."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as transport_tests
import test_bc2_fixed_preparation as preparation_tests
import test_bc2_fixed_postreload_observation as postreload_tests
import test_bc2_magazine_bolt_stimulus as magazine_tests

d = transport_tests.d
MS, NS = 1_000_000, 1_000_000_000


class RuntimeHarness(transport_tests.Harness):
    """Keep actual driver send/ACK/owner/cleanup, fake only OS and runtime IO."""
    def __init__(self, test, *, ack_ms=8, supported=True, timer_ms=50, state_ms=0):
        super().__init__(test, ack_ms=ack_ms)
        self.origin = self.now
        self.parse_due = self.ack_due = self.expire_due = None
        self.ack_raw = None
        self.owner_writes = []
        self.timer_ms, self.freeze_frames = timer_ms, False
        v = self.driver
        v.monitor, v.motion_started = False, None
        v.reference = v.latest_snapshot = None
        v.held_left = v.held_right = v.held_trigger = False
        v.timed_trigger, v.timed_trigger_supported = None, supported
        v.state = self.state
        self.state['controls'] = dict(gripL=0, gripR=0, trigL=0, trigR=0,
                                     buttons=0, stickL=[0, 0], stickR=[0, 0])
        v.initial_sim = copy.deepcopy(self.state)
        self.state_period = state_ms * MS
        self.next_state_due = self.now + self.state_period
        self.published_state = copy.deepcopy(self.state)
        if self.state_period:
            v.read_state = lambda: copy.deepcopy(self.published_state)
        self.admissions = []
        v.validate_publication = lambda lines, expected, cleanup: self.admissions.append((list(lines), cleanup))

    def write(self, path, raw, **kwargs):
        super().write(path, raw, **kwargs)
        if path.name == 'live-driver-owner.json':
            self.owner_writes.append(json.loads(raw))
        if path == self.driver.command_path:
            self.parse_due = self.now + 2 * MS
            self.ack_due = self.now + self.ack_ms * MS

    def sleep(self, seconds):
        self.now += round(seconds * NS)
        if self.parse_due is not None and self.now >= self.parse_due:
            batch = self.files[self.driver.command_path].decode('ascii').splitlines()
            for line in batch:
                if line == d.TIMED_TRIGGER_COMMAND:
                    self.state['controls']['trigR'] = 1
                    self.expire_due = self.now + self.timer_ms * MS
                elif line.startswith('trigger r '):
                    self.state['controls']['trigR'] = float(line.split()[-1])
                    # Numeric zero intentionally leaves the runtime timer armed.
            self.state.update(cmdSeq=self.state['cmdSeq'] + len(batch), lastCmd=batch[-1])
            self.ack_raw = (f"seq={self.state['cmdSeq']} frame={self.state['frame']} error=-\n"
                            f"last={' | '.join(batch)}\n").encode('ascii')
            self.parse_due = None
        if not self.freeze_frames:
            self.state.update(frame=100 + (self.now-self.origin)//(5*MS),
                              endFrames=99 + (self.now-self.origin)//(5*MS))
            if self.expire_due is not None and self.now >= self.expire_due:
                self.state['controls']['trigR'] = 0
                self.expire_due = None
        if self.ack_due is not None and self.now >= self.ack_due and self.ack_raw is not None:
            self.files[self.driver.ack_path] = self.ack_raw
            self.ack_due = None
        if self.state_period and self.now >= self.next_state_due:
            self.published_state = copy.deepcopy(self.state)
            self.next_state_due = self.origin + ((self.now-self.origin)//self.state_period+1)*self.state_period
        if self.on_sleep:
            self.on_sleep()

    def batches(self):
        return [raw.decode('ascii').splitlines()[:-1] for _, raw in self.command_writes]


class TimedTriggerTests(unittest.TestCase):
    @staticmethod
    def retained_target(h):
        h.state['controls']['gripR'] = 1
        h.published_state['controls']['gripR'] = 1
        h.driver.held_right = True
        target = copy.deepcopy({key: h.state[key] for key in ('head', 'handL', 'handR', 'controls')})
        target.update(commands=['trigger r 1'], fire=True)
        target['controls']['trigR'] = 1
        return target

    def test_retained_gun_timer_survives_delayed_ack_and_natural_neutral_read(self):
        for ack_ms, state_ms in ((88, 0), (8, 50)):
            with self.subTest(ack_ms=ack_ms, state_ms=state_ms):
                h = RuntimeHarness(self, ack_ms=ack_ms, state_ms=state_ms)
                target = self.retained_target(h)
                v = h.driver
                result = v.single_trigger_pulse(source_deadline=h.now+30*MS,
                    retained_grips={'gripR': 1}, expected=target)
                proof = result['pulseTransport']
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
                self.assertEqual(proof['neutralState']['controls'], dict(target['controls'], trigR=0))
                self.assertEqual(proof['expectedNeutralControls']['gripR'], 1)
                self.assertEqual(proof['runtimeSha256'], d.TIMED_TRIGGER_RUNTIME_SHA256)
                self.assertEqual(proof['pressDeadlineNs']-proof['pressStartedNs'], 100*MS)
                self.assertEqual(proof['observationDeadlineNs'], proof['pressCommittedNs']+250*MS)
                self.assertIsNone(result['releaseWrittenNs'])
                self.assertIsNone(proof['physicalDurationNs'])
                self.assertTrue(v.held_right)
                self.assertFalse(v.pending_command or v.timed_trigger or v.held_trigger)
                with self.assertRaisesRegex(d.REFUSAL, 'already used'):
                    v.single_trigger_pulse(retained_grips={'gripR': 1}, expected=target)
                self.assertEqual(len(h.batches()), 1)

    def test_retained_grips_refuse_wrong_runtime_unsettled_or_unowned_controls(self):
        for fault in ('runtime', 'pending', 'unsafe', 'claim', 'left', 'trigger', 'button', 'stick',
                      'arbitrary', 'partial', 'target'):
            with self.subTest(fault=fault):
                h = RuntimeHarness(self, supported=fault != 'runtime')
                target = self.retained_target(h)
                v, retained = h.driver, {'gripR': 1}
                if fault == 'pending':
                    v.pending_command = True
                elif fault == 'unsafe':
                    v.unsafe_owner = True
                elif fault == 'claim':
                    v.held_right = False
                elif fault in ('left', 'trigger', 'button', 'stick'):
                    key = dict(left='gripL', trigger='trigR', button='buttons', stick='stickL')[fault]
                    h.state['controls'][key] = [1, 0] if fault == 'stick' else 1
                elif fault == 'arbitrary':
                    retained['buttons'] = 1
                elif fault == 'partial':
                    retained['gripR'] = .5
                elif fault == 'target':
                    target['handR']['foreign'] = True
                with self.assertRaises(d.REFUSAL):
                    v.single_trigger_pulse(retained_grips=retained, expected=target)
                self.assertEqual(h.batches(), [])
                self.assertIsNone(v.timed_trigger)

    def test_default_timer_route_still_refuses_a_retained_grip(self):
        h = RuntimeHarness(self)
        self.retained_target(h)
        with self.assertRaises(d.REFUSAL):
            h.driver.single_trigger_pulse()
        self.assertEqual(h.batches(), [])

    def test_retained_grip_or_pose_change_during_ack_cannot_settle(self):
        for fault in ('grip', 'pose', 'claim', 'writer'):
            with self.subTest(fault=fault):
                h = RuntimeHarness(self, ack_ms=88)
                target = self.retained_target(h)
                def change():
                    if fault == 'grip':
                        h.state['controls']['gripR'] = 0
                    elif fault == 'pose':
                        h.state['handR']['foreign'] = True
                    elif fault == 'claim':
                        h.driver.held_right = False
                    else:
                        h.files[h.driver.command_path] = b'foreign\n'
                h.on_sleep = change
                with self.assertRaises(d.REFUSAL):
                    h.driver.single_trigger_pulse(retained_grips={'gripR': 1}, expected=target)
                self.assertTrue(h.driver.pending_command)
                self.assertTrue(h.driver.timed_trigger)
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_retained_grip_late_ack_leaves_pending_custody_but_runtime_releases(self):
        h = RuntimeHarness(self, ack_ms=120)
        target = self.retained_target(h)
        with self.assertRaises(d.REFUSAL):
            h.driver.single_trigger_pulse(retained_grips={'gripR': 1}, expected=target)
        self.assertTrue(h.driver.pending_command and h.driver.timed_trigger)
        self.assertEqual(h.state['controls']['trigR'], 0)
        self.assertEqual(h.state['controls']['gripR'], 1)
        self.assertTrue(json.loads(h.files[h.driver.run/'live-driver-owner.json'])['pending'])
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_bolt_cleanup_settles_only_exact_late_timer_ack_and_neutral(self):
        for fault in (None, 'pending', 'owner', 'grip'):
            with self.subTest(fault=fault):
                h = RuntimeHarness(self, ack_ms=300 if fault == 'pending' else 120)
                target = self.retained_target(h)
                v = h.driver
                with self.assertRaises(d.REFUSAL):
                    v.single_trigger_pulse(retained_grips={'gripR': 1}, expected=target)
                if fault == 'owner':
                    h.files[v.run/'live-driver-owner.json'] = b'foreign'
                    with self.assertRaises(d.REFUSAL):
                        magazine_tests.d.MagazineBoltDriver.cleanup_owned(v)
                    self.assertTrue(v.pending_command and v.timed_trigger)
                elif fault == 'grip':
                    h.state['controls']['gripR'] = 0
                    # The shared read-only cleanup rejects this as proof.
                    v.settle_pending_for_release()
                    self.assertTrue(v.timed_trigger)
                    self.assertIsNone(getattr(v, 'timed_trigger_cleanup', None))
                else:
                    result = magazine_tests.d.MagazineBoltDriver.cleanup_owned(v)
                    if fault == 'pending':
                        self.assertIn('unsettled', result)
                        self.assertTrue(v.pending_command and v.timed_trigger)
                    else:
                        self.assertIn('original shot remains refused', result)
                        self.assertFalse(v.pending_command or v.timed_trigger or v.held_trigger)
                        proof = v.timed_trigger_cleanup
                        self.assertTrue(proof['originalActionRemainsRefused'])
                        self.assertFalse(proof['commandAuthority'])
                        self.assertEqual(proof['neutralState']['controls']['gripR'], 1)
                        self.assertEqual(proof['cleanupDeadlineNs'], h.origin+200*MS)
                self.assertTrue(v.held_right)
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_assembled_delayed_ack_uses_one_bounded_runtime_batch(self):
        h = RuntimeHarness(self, ack_ms=88)
        v = h.driver
        result = v.single_trigger_pulse(source_deadline=h.now+30*MS)
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
        self.assertEqual(h.admissions, [(['trigger r 1'], False)])
        self.assertIsNone(result['releaseWrittenNs'])
        proof = result['pulseTransport']
        self.assertEqual(proof['configuredMilliseconds'], 50)
        self.assertIsNone(proof['physicalDurationNs'])
        self.assertFalse(proof['nativeDeliveryProved'])
        self.assertLess(proof['neutralObservedNs'], proof['observationDeadlineNs'])
        self.assertEqual(proof['pressDeadlineNs'], proof['pressStartedNs']+100*MS)
        self.assertLess(proof['pressCommittedNs'], proof['pressDeadlineNs'])
        self.assertEqual(proof['observationDeadlineNs'], proof['pressCommittedNs']+250*MS)
        self.assertEqual(h.driver_publication_deadline, proof['pressStartedNs']+30*MS)
        self.assertFalse(v.pending_command or v.held_trigger or v.timed_trigger)
        owner = json.loads(h.files[v.run/'live-driver-owner.json'])
        self.assertNotIn('timedTrigger', owner)
        self.assertFalse(owner['pending'])
        self.assertTrue(all(item['pending'] and item.get('timedTrigger')
                            for item in h.owner_writes[:-1]))
        self.assertFalse(h.owner_writes[-1]['pending'])
        with self.assertRaisesRegex(d.REFUSAL, 'already used'):
            v.single_trigger_pulse()
        self.assertEqual(len(h.batches()), 1)

    def test_20hz_state_can_observe_natural_release_after_down_deadline(self):
        h = RuntimeHarness(self, state_ms=50)
        v = h.driver
        clocks = v.deadline, v.hard_deadline
        result = v.single_trigger_pulse(source_deadline=h.now+30*MS)
        proof = result['pulseTransport']
        self.assertEqual(proof['pressCommittedNs']-proof['pressStartedNs'], 50*MS)
        self.assertEqual(proof['neutralObservedNs']-proof['pressStartedNs'], 100*MS)
        self.assertEqual(proof['observationDeadlineNs'], proof['pressCommittedNs']+250*MS)
        self.assertEqual((v.deadline, v.hard_deadline), clocks)
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
        self.assertIsNone(result['releaseWrittenNs'])
        self.assertIsNone(proof['physicalDurationNs'])
        self.assertFalse(proof['nativeDeliveryProved'])
        self.assertFalse(v.pending_command or v.held_trigger or v.timed_trigger)

    def test_release_observation_starts_at_settled_down_without_clock_renewal(self):
        h = RuntimeHarness(self, state_ms=50)
        v = h.driver
        send = v.send
        def delayed_send_return(*args, **kwargs):
            result = send(*args, **kwargs)
            h.sleep(.040)
            return result
        v.send = delayed_send_return
        proof = v.single_trigger_pulse()['pulseTransport']
        self.assertEqual(proof['pressCommittedNs']-proof['pressStartedNs'], 50*MS)
        self.assertEqual(proof['observationDeadlineNs'], proof['pressCommittedNs']+250*MS)

    def test_release_observation_is_capped_by_original_driver_deadline(self):
        for deadline_ms, succeeds in ((120, True), (95, False)):
            with self.subTest(deadline_ms=deadline_ms):
                h = RuntimeHarness(self, state_ms=50)
                v = h.driver
                v.deadline = h.now+deadline_ms*MS
                clocks = v.deadline, v.hard_deadline
                if succeeds:
                    proof = v.single_trigger_pulse()['pulseTransport']
                    self.assertEqual(proof['observationDeadlineNs'], clocks[0])
                else:
                    with self.assertRaises(d.REFUSAL):
                        v.single_trigger_pulse()
                    self.assertTrue(v.timed_trigger and v.held_trigger)
                    self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
                    self.assertFalse(v.pending_command)
                self.assertEqual((v.deadline, v.hard_deadline), clocks)
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_unknown_runtime_keeps_numeric_press_release(self):
        h = RuntimeHarness(self, supported=False)
        result = h.driver.single_trigger_pulse()
        self.assertEqual(h.batches(), [['trigger r 1'], ['trigger r 0']])
        self.assertNotIn('pulseTransport', result)
        self.assertLessEqual(result['releaseWrittenNs']-result['pressWrittenNs'], 100*MS)

    def test_ack_without_natural_expiry_fails_and_zero_preserves_timer_custody(self):
        h = RuntimeHarness(self, timer_ms=400)
        v = h.driver
        with self.assertRaises(d.REFUSAL):
            v.single_trigger_pulse()
        self.assertEqual(h.now, v.last_commit+250*MS)
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
        self.assertTrue(v.timed_trigger)
        self.assertIn('custody retained', v.release_after_failure())
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND], ['trigger r 0']])
        owner = json.loads(h.files[v.run/'live-driver-owner.json'])
        self.assertTrue(owner['pending'])
        self.assertTrue(owner['timedTrigger']['numericRelease'])
        self.assertTrue(all(item['pending'] for item in h.owner_writes))
        self.assertFalse(v.held_trigger)
        with self.assertRaisesRegex(d.REFUSAL, 'custody'):
            v.send(['trigger r 1'])
        with self.assertRaisesRegex(d.REFUSAL, 'unsettled'):
            v.observe_timed_trigger_neutral()
        self.assertIn('already acknowledged', v.release_after_failure())
        self.assertEqual(len(h.batches()), 2)

    def test_no_advancing_frame_keeps_pending_owner_and_no_retry(self):
        h = RuntimeHarness(self)
        h.freeze_frames = True
        v = h.driver
        with self.assertRaises(d.REFUSAL):
            v.single_trigger_pulse()
        self.assertTrue(v.pending_command)
        self.assertIn('no settled', v.release_after_failure())
        self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
        self.assertTrue(v.timed_trigger)
        self.assertEqual(len(h.batches()), 1)

    def test_ack_plus_one_old_neutral_does_not_retire_timer(self):
        h = RuntimeHarness(self)
        v = h.driver
        original = v.observe_timed_trigger_neutral
        seen = []
        def raced_observation():
            h.state.update(frame=v.timed_trigger['ackFrame']+1)
            h.state['controls']['trigR'] = 0
            # A subsequent frame applies the pending pull; the old low cannot
            # clear custody before this application, even with advanced endFrames.
            def after_sleep():
                seen.append(v.timed_trigger is not None)
                if h.expire_due is not None and h.now < h.expire_due:
                    h.state['controls']['trigR'] = 1
            h.on_sleep = after_sleep
            return original()
        v.observe_timed_trigger_neutral = raced_observation
        result = v.single_trigger_pulse()
        self.assertTrue(seen)
        self.assertTrue(all(seen))
        self.assertGreaterEqual(result['pulseTransport']['neutralState']['frame'],
                                result['pulseTransport']['ackFrame']+2)

    def test_changed_other_control_or_writer_never_clears_custody(self):
        for fault in ('control', 'writer'):
            with self.subTest(fault=fault):
                h = RuntimeHarness(self)
                v = h.driver
                original = v.observe_timed_trigger_neutral
                def corrupt():
                    if fault == 'control':
                        h.state['controls']['gripR'] = 1
                    else:
                        h.files[v.command_path] = b'foreign\n'
                    return original()
                v.observe_timed_trigger_neutral = corrupt
                with self.assertRaises(d.REFUSAL):
                    v.single_trigger_pulse()
                self.assertTrue(v.timed_trigger)
                if fault == 'writer':
                    self.assertTrue(v.unsafe_owner)
                    v.release_after_failure()
                    self.assertEqual(len(h.batches()), 1)

    def test_source_expiry_refuses_before_any_runtime_timer_is_armed(self):
        h = RuntimeHarness(self)
        with self.assertRaisesRegex(d.REFUSAL, 'lease expired'):
            h.driver.single_trigger_pulse(source_deadline=h.now)
        self.assertEqual(h.batches(), [])
        self.assertIsNone(h.driver.timed_trigger)

    def test_unpublished_timer_rolls_back_without_false_hold_or_future_timer(self):
        h = RuntimeHarness(self)
        with patch.object(d, 'publish_command', side_effect=OSError('unpublished')), \
                self.assertRaisesRegex(OSError, 'unpublished'):
            h.driver.single_trigger_pulse()
        self.assertEqual(h.batches(), [])
        self.assertIsNone(h.driver.timed_trigger)
        self.assertFalse(h.driver.pending_command or h.driver.held_trigger)
        self.assertFalse(json.loads(h.files[h.driver.run/'live-driver-owner.json'])['pending'])

    def test_late_ack_with_natural_neutral_retires_only_cleanup_custody(self):
        h = RuntimeHarness(self, ack_ms=120)
        v = h.driver
        with self.assertRaises(d.REFUSAL):
            v.single_trigger_pulse()
        self.assertTrue(v.pending_command)
        deadlines = v.deadline, v.stage_deadline, v.hard_deadline
        self.assertIn('original action remains refused', v.release_after_failure())
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
        self.assertFalse(v.pending_command or v.held_trigger or v.timed_trigger)
        self.assertFalse(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline), deadlines)
        with self.assertRaisesRegex(d.REFUSAL, 'already used'):
            v.single_trigger_pulse()

    def saved_pending_preparation(self):
        """Normal87/01's failed phase and exact saved pre-zero seq/frame tuple."""
        h = RuntimeHarness(self, ack_ms=120)
        v = h.driver
        h.now = h.origin = 640333113407600
        v.deadline, v.hard_deadline = h.now+9*NS, h.now+10*NS
        v.last_write, v.last_commit = h.now-60*MS, h.now-50*MS
        v.sequence, v.sentinel = 67, 'initial'
        h.state.update(cmdSeq=67, frame=8636, endFrames=8635, captureSeq=4)
        v.capture_sequence = 4
        h.freeze_frames = True
        original_read = v.read_state
        def delayed_read():
            if h.now >= h.origin+99*MS:
                h.now = 640333213757900
            return original_read()
        v.read_state = delayed_read
        with patch.object(d.uuid, 'uuid4') as nonce:
            nonce.return_value.hex = 'c83e7b5d3b454121a5a322396c5444ad'
            with self.assertRaisesRegex(d.ReadDeadlineExceeded,
                    'original acknowledgement/read deadline exceeded') as failure:
                v.single_trigger_pulse(source_deadline=640333190230400)
        v.read_state = original_read
        self.assertEqual(h.now, 640333213757900)
        self.assertTrue(v.pending_command)
        h.state.update(cmdSeq=69, lastCmd=v.sentinel, frame=8643, endFrames=8642)
        h.state['controls']['trigR'] = 0
        h.files[v.ack_path] = (
            'seq=69 frame=8639 error=-\r\n'
            'last=trigger r pull 50 | note root-feed-c83e7b5d3b454121a5a322396c5444ad\r\n').encode('ascii')
        h.ack_due = h.parse_due = h.expire_due = None
        return h, failure.exception

    def test_saved_preparation_neutral_before_zero_keeps_failure_and_no_action_authority(self):
        h, failure = self.saved_pending_preparation()
        v = h.driver
        clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.last_commit
        self.assertIn('original action remains refused', v.release_after_failure())
        self.assertEqual(str(failure), 'original acknowledgement/read deadline exceeded')
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.last_commit), clocks)
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
        proof, = [e for e in v.artifacts.events if e['type']=='timed_trigger_cleanup_neutral_observed']
        self.assertEqual((proof['timer']['sequence'], proof['timer']['ackFrame'],
                          proof['neutralState']['frame'], proof['neutralState']['endFrames']),
                         (69, 8639, 8643, 8642))
        self.assertTrue(proof['originalActionRemainsRefused'])
        self.assertFalse(proof['commandAuthority'] or proof['nativeAcceptance'] or proof['headsetAcceptance'])
        self.assertLess(proof['neutralObservedNs'], proof['cleanupDeadlineNs'])
        self.assertFalse(v.pending_command or v.held_trigger or v.timed_trigger)
        owner = json.loads(h.files[v.run/'live-driver-owner.json'])
        self.assertFalse(owner['pending'])
        self.assertNotIn('timedTrigger', owner)
        self.assertIsNone(getattr(v, 'selected_timed_release', None))
        with self.assertRaisesRegex(d.REFUSAL, 'already used'):
            v.single_trigger_pulse()

    def test_cleanup_neutral_rejects_missing_mismatched_or_masked_timer_proof(self):
        for fault in ('missing_runtime', 'runtime', 'sequence', 'sentinel', 'hash', 'duration',
                      'numeric_zero', 'before_frame', 'before_end', 'ack_plus_one', 'stale_end',
                      'not_neutral', 'other_control', 'basis', 'capture', 'missing_binding'):
            with self.subTest(fault=fault):
                h, _ = self.saved_pending_preparation()
                v = h.driver
                timer = v.timed_trigger
                if fault == 'missing_runtime': del timer['runtimeSha256']
                elif fault == 'runtime': timer['runtimeSha256'] = 'unverified'
                elif fault == 'sequence': timer['sequence'] += 2
                elif fault == 'sentinel': timer['sentinel'] = 'note foreign'
                elif fault == 'hash': timer['commandSha256'] = 'other'
                elif fault == 'duration': timer['configuredMilliseconds'] = 100
                elif fault == 'numeric_zero': timer['numericRelease'] = True
                elif fault == 'before_frame': timer['beforeFrame'] -= 1
                elif fault == 'before_end': timer['beforeEndFrames'] -= 1
                elif fault == 'ack_plus_one': h.state['frame'] = 8640
                elif fault == 'stale_end': h.state['endFrames'] = 8635
                elif fault == 'not_neutral': h.state['controls']['trigR'] = 1
                elif fault == 'other_control': h.state['controls']['gripR'] = 1
                elif fault == 'basis': h.state['head'] = dict(changed=True)
                elif fault == 'capture': h.state['captureSeq'] += 1
                elif fault == 'missing_binding': del timer['sentinel']
                v.save_owner(True)
                v.settle_pending_for_release()
                self.assertIsNotNone(v.timed_trigger)
                self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
                self.assertIsNone(getattr(v, 'timed_trigger_cleanup', None))
                self.assertFalse(any(e['type']=='timed_trigger_cleanup_neutral_observed'
                                     for e in v.artifacts.events))
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_cleanup_neutral_rejects_replaced_owner_command_ack_or_sentinel(self):
        for fault in ('owner', 'command', 'ack', 'sentinel', 'ack_frame'):
            with self.subTest(fault=fault):
                h, _ = self.saved_pending_preparation()
                v = h.driver
                if fault == 'owner': h.files[v.run/'live-driver-owner.json'] = b'foreign'
                elif fault == 'command': h.files[v.command_path] = b'foreign'
                elif fault == 'ack': h.files[v.ack_path] = b'seq=69 frame=8639 error=-\nlast=foreign\n'
                elif fault == 'sentinel': h.state['lastCmd'] = 'note foreign'
                elif fault == 'ack_frame':
                    v.timed_trigger['ackFrame'] = 8638
                    v.save_owner(True)
                if fault == 'ack':
                    v.settle_pending_for_release()
                else:
                    with self.assertRaises(d.REFUSAL):
                        v.settle_pending_for_release()
                self.assertIsNotNone(v.timed_trigger)
                self.assertIsNone(getattr(v, 'timed_trigger_cleanup', None))
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_cleanup_proof_archive_or_owner_failure_retains_timer(self):
        for fault in ('archive', 'owner'):
            with self.subTest(fault=fault):
                h, _ = self.saved_pending_preparation()
                v = h.driver
                event, save = v.artifacts.event, v.save_owner
                def archive(value, cleanup=False):
                    if fault == 'archive' and value['type']=='timed_trigger_cleanup_neutral_observed':
                        raise OSError('proof archive failed')
                    return event(value, cleanup=cleanup)
                def owner(pending, **kwargs):
                    if fault == 'owner' and v.timed_trigger is None:
                        raise OSError('owner retirement failed')
                    return save(pending, **kwargs)
                with patch.object(v.artifacts, 'event', archive), patch.object(v, 'save_owner', owner), \
                        self.assertRaises(OSError):
                    v.release_after_failure()
                self.assertTrue(v.timed_trigger and v.held_trigger)
                self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
                self.assertIsNone(getattr(v, 'timed_trigger_cleanup', None))
                self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_cleanup_proof_uses_unchanged_existing_settlement_deadline(self):
        h, _ = self.saved_pending_preparation()
        v = h.driver
        until = min(v.hard_deadline-150*MS, h.now+100*MS)
        clocks = v.deadline, v.stage_deadline, v.hard_deadline
        event = v.artifacts.event
        def delayed_archive(value, cleanup=False):
            event(value, cleanup=cleanup)
            if value['type']=='timed_trigger_cleanup_neutral_observed':
                h.now = until
        with patch.object(v.artifacts, 'event', delayed_archive), self.assertRaises(d.ReadDeadlineExceeded):
            v.release_after_failure()
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline), clocks)
        self.assertTrue(v.timed_trigger and v.held_trigger)
        self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_cancellation_leaves_only_owned_zero_cleanup_and_no_new_press(self):
        h = RuntimeHarness(self)
        v = h.driver
        with patch.object(v, 'observe_timed_trigger_neutral', side_effect=KeyboardInterrupt), \
                self.assertRaises(KeyboardInterrupt):
            v.single_trigger_pulse()
        v.release_after_failure()
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND], ['trigger r 0']])
        self.assertTrue(v.timed_trigger)
        self.assertFalse(v._timed_trigger_arm)
        with self.assertRaises(d.REFUSAL):
            v.single_trigger_pulse()

    def test_unpublished_zero_does_not_claim_ack_or_discard_timer(self):
        h = RuntimeHarness(self, timer_ms=400)
        v = h.driver
        with self.assertRaises(d.REFUSAL):
            v.single_trigger_pulse()
        with patch.object(d, 'publish_command', side_effect=OSError('zero unpublished')), \
                self.assertRaisesRegex(OSError, 'zero unpublished'):
            v.release_after_failure()
        self.assertFalse(v.timed_trigger['numericRelease'])
        self.assertTrue(v.held_trigger)
        self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
        self.assertIn('custody retained', v.release_after_failure())
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND], ['trigger r 0']])

    def test_natural_expiry_owner_save_failure_retains_cleanup_custody(self):
        h = RuntimeHarness(self)
        v = h.driver
        save = v.save_owner
        def fail_retirement(pending, **kwargs):
            if not pending and v.timed_trigger is None:
                raise OSError('owner retirement failed')
            return save(pending, **kwargs)
        with patch.object(v, 'save_owner', side_effect=fail_retirement), \
                self.assertRaisesRegex(OSError, 'retirement failed'):
            v.single_trigger_pulse()
        self.assertTrue(v.timed_trigger and v.held_trigger)
        self.assertTrue(json.loads(h.files[v.run/'live-driver-owner.json'])['pending'])
        self.assertIn('custody retained', v.release_after_failure())
        self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND], ['trigger r 0']])

    def test_runtime_command_cannot_bypass_shared_shot_admission(self):
        h = RuntimeHarness(self)
        with self.assertRaisesRegex(d.REFUSAL, 'shared admitted'):
            h.driver.send([d.TIMED_TRIGGER_COMMAND])
        self.assertEqual(h.batches(), [])

    def test_final_admission_still_receives_numeric_press_and_can_refuse(self):
        h = RuntimeHarness(self)
        def refuse(lines, expected, cleanup):
            self.assertEqual(lines, ['trigger r 1'])
            raise d.REFUSAL('current native owner/config/source changed')
        h.driver.validate_publication = refuse
        with self.assertRaisesRegex(d.REFUSAL, 'native owner'):
            h.driver.single_trigger_pulse()
        self.assertEqual(h.batches(), [])
        self.assertIsNone(h.driver.timed_trigger)

    def test_full_preparation_uses_real_selected_admission_and_shared_transport(self):
        p = preparation_tests.d
        with tempfile.TemporaryDirectory() as temp, patch.dict(globals(), d=p.transport), \
                patch.object(transport_tests, 'd', p.transport):
            h, path = self.preparation_harness(temp)
            v = h.driver
            result = v.prepare(path)
            self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])
            self.assertEqual(result['beforeCounts']['loaded'], 1)
            self.assertEqual(result['afterCounts']['loaded'], 0)
            self.assertFalse(result['nativeShotObserved'])
            self.assertIsNone(result['releaseWrittenNs'])
            self.assertEqual(result['pulseTransport']['configuredMilliseconds'], 50)
            self.assertFalse(v.held_trigger or v.timed_trigger)

    def preparation_harness(self, temp, ack_ms=88):
        p = preparation_tests.d
        h = RuntimeHarness(self, ack_ms=ack_ms)
        v = h.driver
        v.__class__ = p.PreparationDriver
        del v.validate_publication
        v.run = Path(temp)
        v.manifest = dict(source_sha256='source')
        v.active_plan = v.demand_reference = None
        v.demand_finished, v.demand_started = True, h.now
        before = preparation_tests.snapshot()
        captures = []
        for eye in ('left', 'right'):
            path = v.run/(eye+'.png')
            path.write_bytes(eye.encode())
            h.files[path] = eye.encode()
            captures.append(dict(eye=eye, path=str(path), bytes=len(eye), sha256=d.digest(eye.encode())))
        clearance = dict(schema='bc2vr.root-fire-clearance.v1', rootReviewedVisibleCorridor=True,
            host=v.host, owner=before['owner'], configurationPath=before['selected']['configurationPath'],
            rig=before['rig'], reviewedNs=h.now-1, referenceHead=before['input']['referenceHead'],
            rightAim=before['input']['hands'][1]['aim'], captures=captures,
            **{key: copy.deepcopy(h.state[key]) for key in ('head', 'handL', 'handR', 'controls')})
        path = v.run/'clearance.json'
        h.json_files[path], h.files[path] = clearance, d.json_bytes(clearance)
        generation = 1
        def fresh():
            nonlocal generation
            current = preparation_tests.fixture.fixed.refresh(before, h.now, generation)
            if generation > 1:
                current['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
            generation += 1
            v.latest_snapshot = current
            return current, b'{}', h.now
        v.fresh_available = fresh
        return h, path

    def test_preparation_main_keeps_failed_result_and_consumed_attempt_after_cleanup(self):
        p = preparation_tests.d
        with tempfile.TemporaryDirectory() as temp, patch.dict(globals(), d=p.transport), \
                patch.object(transport_tests, 'd', p.transport):
            h, path = self.preparation_harness(temp, ack_ms=120)
            v = h.driver
            v.__init__ = lambda args: None
            v.close = lambda: None
            class ExistingDriver:
                def __new__(cls):
                    return v
            with patch.object(p, 'PreparationDriver', ExistingDriver), patch('builtins.print'):
                code = p.main(['--run', str(v.run), '--fire-clearance', str(path),
                               '--root-authorized-ordinary-input'])
            result = v.artifacts.saved['result.json']
            self.assertEqual((code, result['status']), (2, 'refused'))
            self.assertEqual(result['error'], 'actual stage exceeded maximumGestureNs')
            self.assertIn('original action remains refused', result['release'])
            self.assertFalse(result['timedTriggerPending'] or result['reloadCompleted']
                             or result['nativeAcceptance'] or result['headsetAcceptance'])
            self.assertNotIn('releaseError', result)
            self.assertNotIn('pulse', result)
            attempt, = v.run.glob('fixed-preparation-attempt-*.json')
            self.assertGreater(attempt.stat().st_size, 0)
            with self.assertRaises(FileExistsError):
                attempt.open('xb')
            with self.assertRaisesRegex(d.REFUSAL, 'already used'):
                v.single_trigger_pulse()
            self.assertEqual(h.batches(), [[d.TIMED_TRIGGER_COMMAND]])

    def test_full_post_reload_uses_closed_receipt_and_shared_transport(self):
        t = postreload_tests.d
        with tempfile.TemporaryDirectory() as temp, patch.dict(globals(), d=t), \
                patch.object(transport_tests, 'd', t):
            h = RuntimeHarness(self, ack_ms=8)
            v = h.driver
            del v.validate_publication
            h.now = h.origin = NS+1200*MS
            v.run = Path(temp)
            v.manifest = dict(source_sha256='source')
            v.profiles = None
            plan, completed, before = postreload_tests.fixed_fixture()
            path = v.run/'close'/'result.json'
            for name, value in ((v.run/'live-driver-owner.json', dict(artifactDirectory=str(path.parent))),
                                (path, completed), (path.parent/'stimulus-01.json', plan)):
                h.json_files[name], h.files[name] = value, d.json_bytes(value)
            generation = 150
            def fresh():
                nonlocal generation
                h.sleep(.020)
                generation += 1
                current = postreload_tests.renew(before, h.now, generation)
                if getattr(v, 'post_reload_phase', None) == 'observe':
                    current['fixedContainer']['view']['snapshot']['counts']['loaded'] = 0
                    current['fixedContainer']['tracking'].update(step=0, blocksFire=True)
                    current['weapon_actions_blocked'] = True
                v.admit_snapshot(current, h.now, current['nonce'])
                v.latest_snapshot = current
                return current, b'{}', h.now
            v.fresh = fresh
            # Before closure-specific admission starts, the generic fixture is
            # validated by post_reload_ready in the real caller itself.
            original_admit = v.admit_snapshot
            v.admit_snapshot = lambda s, n, nonce: (original_admit(s, n, nonce)
                if getattr(v, 'post_reload_closure', None) is not None else None)
            result = v.post_reload_shot(path)
            self.assertEqual(h.batches(), [['grip l 0', 'trigger r 0'], [d.TIMED_TRIGGER_COMMAND]])
            self.assertEqual(result['schema'], 'bc2vr.fixed-post-reload-pulse.v1')
            self.assertEqual(result['afterCounts']['loaded'], 0)
            self.assertFalse(result['nativeShotObserved'])
            self.assertIsNone(result['releaseWrittenNs'])
            self.assertFalse(v.held_trigger or v.timed_trigger)


class TimedNativeAuditTests(unittest.TestCase):
    def boundary(self):
        flow, boundary = preparation_tests.preparation_native()
        start, end = boundary['pressWrittenNs'], boundary['releaseCommittedNs']
        controls = dict(trigR=0, trigL=0, gripL=0, gripR=0)
        boundary['host'] = dict(pid=1)
        boundary['releaseWrittenNs'] = None
        boundary['pulseTransport'] = dict(backend='runtime_timed', command=d.TIMED_TRIGGER_COMMAND,
            runtimeSha256=d.TIMED_TRIGGER_RUNTIME_SHA256, configuredMilliseconds=50,
            publicationStartedNs=start-1, pressStartedNs=start-2, driverDeadlineNs=start+NS,
            pressDeadlineNs=start+100*MS-2, pressCommittedNs=start+8*MS,
            observationDeadlineNs=start+258*MS,
            ackFrame=10, beforeFrame=9, beforeEndFrames=8, neutralObservedNs=end,
            expectedNeutralControls=controls, physicalDurationNs=None, nativeDeliveryProved=False,
            neutralState=dict(pid=1, runtime='xr-sim', frame=13, endFrames=12,
                sessionState='FOCUSED', errors=0, lastCmdError='', controls=copy.deepcopy(controls)))
        return flow, boundary

    def test_timed_neutral_evidence_and_separate_native_three_copy_fire(self):
        flow, boundary = self.boundary()
        result = d.audit_post_reload_shot(flow, boundary)
        self.assertEqual(result['roundsObserved'], 1)
        self.assertFalse(result['nativeAcceptance'])

    def test_neutral_after_100ms_keeps_original_native_fire_window(self):
        flow, boundary = self.boundary()
        boundary['releaseCommittedNs'] = boundary['pressWrittenNs']+120*MS
        boundary['pulseTransport']['neutralObservedNs'] = boundary['releaseCommittedNs']
        self.assertTrue(d.audit_post_reload_shot(flow, boundary)['nativeShotObserved'])
        for row in flow['records']:
            if row['before']['loaded'] != row['after']['loaded']:
                row.update(begin_ns=boundary['pressWrittenNs']+105*MS,
                           end_ns=boundary['pressWrittenNs']+110*MS)
        with self.assertRaisesRegex(d.REFUSAL, 'during the pulse'):
            d.audit_post_reload_shot(flow, boundary)

    def test_audit_requires_exact_original_down_and_release_phase_bounds(self):
        for fault in ('late_down', 'widened_down', 'shortened_down', 'widened_release',
                      'shortened_release', 'ignored_driver_deadline', 'late_release', 'before_down'):
            with self.subTest(fault=fault):
                flow, boundary = self.boundary()
                pulse = boundary['pulseTransport']
                if fault == 'late_down': pulse['pressCommittedNs'] = pulse['pressDeadlineNs']
                elif fault == 'widened_down': pulse['pressDeadlineNs'] += 1
                elif fault == 'shortened_down': pulse['pressDeadlineNs'] -= 1
                elif fault == 'widened_release': pulse['observationDeadlineNs'] += 1
                elif fault == 'shortened_release': pulse['observationDeadlineNs'] -= 1
                elif fault == 'ignored_driver_deadline': pulse['driverDeadlineNs'] = pulse['observationDeadlineNs']-1
                elif fault == 'late_release':
                    boundary['releaseCommittedNs'] = pulse['neutralObservedNs'] = pulse['observationDeadlineNs']
                else:
                    boundary['releaseCommittedNs'] = pulse['neutralObservedNs'] = pulse['pressCommittedNs']-1
                with self.assertRaisesRegex(d.REFUSAL, 'bounded fresh neutral'):
                    d.audit_post_reload_shot(flow, boundary)

    def test_parser_ack_claims_stale_neutral_wrong_runtime_or_missing_fire_refuse(self):
        for fault in ('ack_plus_one', 'wrong_pin', 'late_neutral', 'duration_claim', 'written_release', 'missing_fire'):
            with self.subTest(fault=fault):
                flow, b = self.boundary()
                pulse = b['pulseTransport']
                if fault == 'ack_plus_one': pulse['neutralState']['frame'] = pulse['ackFrame']+1
                elif fault == 'wrong_pin': pulse['runtimeSha256'] = 'unknown'
                elif fault == 'late_neutral': pulse['observationDeadlineNs'] = b['releaseCommittedNs']
                elif fault == 'duration_claim': pulse['physicalDurationNs'] = 50*MS
                elif fault == 'written_release': b['releaseWrittenNs'] = b['releaseCommittedNs']
                else: flow['records'] = [r for r in flow['records'] if r['before']['loaded'] == r['after']['loaded']]
                with self.assertRaises(d.REFUSAL):
                    d.audit_post_reload_shot(flow, b)


if __name__ == '__main__':
    unittest.main()
