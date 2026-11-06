"""Real driver timing with synthetic transport: publication and release ACK have separate bounds."""
import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import test_bc2_feed_driver_observation as transport
import test_bc2_feed_postrelease_observation as postshot

d = transport.d
NS, MS = d.NS, 1_000_000

class PostshotDeadlineTests(unittest.TestCase):
    def scenario(self, *, press_ack=44, release_ack=62, press_publish=8,
                 release_publish=9, interfere=False):
        # Actual filesystem use is limited to the run-local single-attempt marker.
        run = Path(self.enterContext(tempfile.TemporaryDirectory()))
        h = transport.Harness(self)
        v = h.driver
        v.run = run
        v.motion_started, v.monitor, v.pending_nonce = None, False, None
        v.held_left = v.held_trigger = False
        h.state['controls'] = dict(gripL=0., trigR=0.)
        v.initial_sim = copy.deepcopy(h.state)
        v.manifest = dict(source_sha256='synthetic')
        v.profiles, _, closure = postshot.FeedPostreloadShotTests.ready(h.now, 1)
        result_path = run / 'close' / 'result.json'
        closed = dict(schema='bc2vr.reload-stimulus-result.v1', status='ordinary_stimulus_completed',
            stimulusCompleted=True, boundaryChecksPassed=True, observerStopped=True,
            observationIncomplete=False, leftGripHeld=False, pendingCommand=False,
            plans=[dict(family='feed', stage='CloseCover')],
            boundaries=dict(feed_released=dict(receiptCounterAdvanced=True, lifetime=2)))
        plan = dict(schema='bc2vr.reload-stimulus.v1', family='feed', stage='CloseCover', mapping=closure['mapping'])
        sources = {run / 'live-driver-owner.json': dict(artifactDirectory=str(result_path.parent)),
                   result_path: closed, result_path.parent / 'stimulus-01.json': plan}
        self.enterContext(patch.object(d, 'child', side_effect=lambda root, p: p))
        self.enterContext(patch.object(d, 'read_json', side_effect=lambda p, *a: (copy.deepcopy(sources[p]), b'{}')))
        generation = [10]

        def fresh():
            h.now += 20 * MS
            _, s, _ = postshot.FeedPostreloadShotTests.ready(h.now, generation[0])
            s['input']['generation'] = s['shot_input_generation'] = generation[0]
            s['input_evidence']['sequence'] = s['feed']['input']['sequence'] = generation[0]
            generation[0] += 1
            v.latest_snapshot = s
            return s, b'{}', h.now

        def write(path, raw, **kwargs):
            if path == v.command_path:
                commands = raw.decode('ascii').splitlines()[:-1]
                if commands == ['trigger r 1']:
                    h.now += press_publish * MS
                    h.ack_ms = press_ack
                elif commands == ['trigger r 0']:
                    h.now += release_publish * MS
                    h.ack_ms = release_ack
                    if interfere:
                        h.files[path] = b'other writer\n'
                else:
                    h.ack_ms = 5
            return h.write(path, raw, **kwargs)

        def sleep(seconds):
            old_sequence, old_frame = h.state['cmdSeq'], h.state['frame']
            h.sleep(seconds)
            if h.state['cmdSeq'] != old_sequence:
                batch = h.files[v.command_path].decode('ascii').splitlines()
                h.state.update(frame=old_frame + 2, endFrames=old_frame + 1)
                h.files[v.ack_path] = (f"seq={h.state['cmdSeq']} frame={old_frame + 1} error=-\n"
                                      f"last={' | '.join(batch)}\n").encode('ascii')
                for line in batch:
                    if line.startswith('trigger r '):
                        h.state['controls']['trigR'] = float(line.split()[-1])

        self.enterContext(patch.object(d, 'atomic', side_effect=write))
        self.enterContext(patch.object(d.time, 'sleep', side_effect=sleep))
        v.fresh = fresh
        return h, v, result_path

    @staticmethod
    def commands(h):
        return [raw.decode('ascii').splitlines()[:-1] for _, raw in h.command_writes]

    def test_actual462_late_postframe_release_ack_does_not_expire_short_pulse(self):
        h, v, path = self.scenario()
        boundary = v.post_reload_shot(path)
        self.assertEqual(boundary['releaseWrittenNs'] - boundary['pressWrittenNs'], 53 * MS)
        self.assertEqual(boundary['releaseCommittedNs'] - boundary['pressWrittenNs'], 115 * MS)
        self.assertEqual(self.commands(h), [['grip l 0', 'trigger r 0'], ['trigger r 1'], ['trigger r 0']])
        self.assertFalse(v.held_trigger or v.pending_command)
        self.assertFalse(boundary['nativeShotObserved'])
        with self.assertRaises(FileExistsError):
            v.post_reload_shot(path)
        self.assertEqual(len(h.command_writes), 3)

    def test_late_up_is_published_and_settled_before_truthful_overlong_refusal(self):
        h, v, path = self.scenario(release_publish=70, release_ack=5)
        with self.assertRaisesRegex(d.REFUSAL, 'pulse publication exceeded 100ms; released; no retry'):
            v.post_reload_shot(path)
        self.assertEqual(self.commands(h)[-1], ['trigger r 0'])
        self.assertFalse(v.held_trigger or v.pending_command)
        self.assertNotIn('post-reload-shot.json', v.artifacts.saved)
        with self.assertRaises(FileExistsError):
            v.post_reload_shot(path)
        self.assertEqual(len(h.command_writes), 3)

    def test_release_ack_budget_is_still_bounded_and_cleanup_settles_exact_up(self):
        h, v, path = self.scenario(release_ack=275)
        with self.assertRaisesRegex(d.REFUSAL, 'actual stage exceeded maximumGestureNs'):
            v.post_reload_shot(path)
        self.assertTrue(v.pending_command)
        v.release_after_failure()
        self.assertFalse(v.held_trigger or v.pending_command)
        self.assertEqual(len(h.command_writes), 3)
        self.assertNotIn('post-reload-shot.json', v.artifacts.saved)

    def test_press_timeout_keeps_failure_but_cleanup_still_releases_owned_down(self):
        h, v, path = self.scenario(press_ack=110, release_ack=5)
        with self.assertRaisesRegex(d.REFUSAL, 'actual stage exceeded maximumGestureNs'):
            v.post_reload_shot(path)
        self.assertEqual(v.release_after_failure(), 'ordinary owned controller release acknowledged')
        self.assertFalse(v.held_trigger or v.pending_command)
        self.assertEqual(self.commands(h).count(['trigger r 1']), 1)
        self.assertEqual(self.commands(h)[-1], ['trigger r 0'])

    def test_release_window_cannot_renew_expired_down_source(self):
        h, v, path = self.scenario(press_publish=90)
        with self.assertRaisesRegex(d.REFUSAL, 'original command publication deadline'):
            v.post_reload_shot(path)
        self.assertEqual(self.commands(h), [['grip l 0', 'trigger r 0']])
        self.assertFalse(v.pending_command or v.held_trigger)

    def test_release_window_cannot_bypass_owner_interference(self):
        h, v, path = self.scenario(interfere=True)
        with self.assertRaisesRegex(d.REFUSAL, 'another writer'):
            v.post_reload_shot(path)
        self.assertTrue(v.unsafe_owner)
        self.assertEqual(self.commands(h).count(['trigger r 1']), 1)
        self.assertEqual(len(h.command_writes), 2)
        self.assertNotIn('post-reload-shot.json', v.artifacts.saved)

    def test_native_audit_admits_delayed_ack_without_extending_fire_window(self):
        flow, boundary = postshot.FeedPostreloadShotTests.native()
        boundary['releaseCommittedNs'] = boundary['pressWrittenNs'] + 120 * MS
        self.assertTrue(d.audit_post_reload_shot(flow, boundary)['nativeShotObserved'])
        for row in flow['records']:
            if row['before']['loaded'] != row['after']['loaded']:
                row.update(begin_ns=boundary['pressWrittenNs'] + 105 * MS,
                           end_ns=boundary['pressWrittenNs'] + 110 * MS)
        with self.assertRaisesRegex(d.REFUSAL, 'during the pulse'):
            d.audit_post_reload_shot(flow, boundary)

    def test_native_audit_refuses_overlong_publication_and_ack_intervals(self):
        for fault in ('publication', 'ack'):
            with self.subTest(fault=fault):
                flow, boundary = postshot.FeedPostreloadShotTests.native()
                if fault == 'publication':
                    boundary['releaseWrittenNs'] = boundary['pressWrittenNs'] + 101 * MS
                    boundary['releaseCommittedNs'] = boundary['releaseWrittenNs'] + MS
                else:
                    boundary['releaseCommittedNs'] = boundary['releaseWrittenNs'] + 251 * MS
                with self.assertRaisesRegex(d.REFUSAL, 'outside original bound'):
                    d.audit_post_reload_shot(flow, boundary)


if __name__ == '__main__':
    unittest.main()
