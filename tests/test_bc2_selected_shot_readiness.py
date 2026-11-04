"""Focused attachment-dependent shot preparation; no native/headset acceptance."""
import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('shot_readiness_fixtures',
    ROOT / 'tests/test_bc2_fixed_container_simulation_plan.py')
fixed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixed)
d = fixed.d


class SelectedShotReadinessTests(unittest.TestCase):
    MS = 1_000_000

    class Driver(fixed.SelectedAmmoDriverTests.Driver):
        require_shot_attachment_ready = True

    @staticmethod
    def unavailable(s):
        s['sight'].update(status='geometry_unavailable', contact=None)

    def response(self, now, nonce=1):
        _, s = fixed.SelectedAmmoDriverTests.response(self, now, nonce)
        # Refresh the synthetic contact along with its already-refreshed frame.
        s['native_deadline_qpc'] = s['native_deadline_ns']//100
        evidence = dict(generation=s['input']['generation'],
                        deadlineQpc=s['native_deadline_qpc'], deadlineNs=s['native_deadline_ns'])
        s['sight'].update(evidence, preferredByDistance=False)
        s['sight']['contact'].update(evidence, valid=True, previewValid=True,
                                     contactDistanceMeters=0.5)
        return None, s

    missing = staticmethod(fixed.SelectedAmmoDriverTests.missing)

    def harness(self, missing=False, right_squeeze=0.):
        h, future, initial = fixed.SelectedAmmoDriverTests.harness(self, missing, right_squeeze)
        # The shared harness initially models a held left hand; this observer's
        # real starting simulator state is neutral after sight release.
        h.state['controls']['trigR'] = 0.
        h.driver.initial_sim = copy.deepcopy(h.state)
        return h, future, initial

    def prepared(self, pending=False):
        h, future, _ = self.harness()
        if pending:
            future['mutate'] = self.unavailable
        s, _, _ = h.driver.fresh_available()
        h.driver.reference = s
        return h, future, s

    def test_native_valid_without_contact_is_incomplete_and_immutable(self):
        h, _, s = self.prepared(pending=True)
        s['sight']['native'] = dict(valid=True, physicalItem=s['sight']['family']['physicalItem'])
        before = copy.deepcopy(s)
        self.assertEqual(s['fixedContainer']['view']['snapshot']['counts']['loaded'], 1)
        self.assertTrue(s['sight']['native']['valid'])
        self.assertIsNone(s['sight']['contact'])
        self.assertFalse(h.driver.selected_shot_ready(s, h.now))
        self.assertEqual(s, before)

    def test_current_ready_needs_no_proximity_preference_or_grip_change(self):
        h, _, s = self.prepared()
        self.assertFalse(s['sight']['preferredByDistance'])
        self.assertGreater(s['sight']['contact']['contactDistanceMeters'], 0.08)
        self.assertTrue(h.driver.selected_shot_ready(s, h.now))
        h.driver.wait_pre_shot_ready()
        self.assertEqual(h.command_writes, [])
        self.assertEqual(h.driver.commands, 0)

    def test_missing_contact_or_preview_is_incomplete(self):
        mutations = (
            self.unavailable,
            lambda s: s['sight'].update(contact=None),
            lambda s: s['sight']['contact'].update(valid=False),
            lambda s: s['sight']['contact'].update(previewValid=False),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.harness_context():
                h, _, s = self.prepared()
                mutation(s)
                self.assertFalse(h.driver.selected_shot_ready(s, h.now))
                self.assertEqual(h.command_writes, [])

    def harness_context(self):
        # Each subcase installs its own deterministic transport clock and memory.
        # Keep those patches scoped instead of leaving older clocks nested.
        from contextlib import ExitStack
        stack = ExitStack()
        previous = self.enterContext
        self.enterContext = stack.enter_context
        stack.callback(setattr, self, 'enterContext', previous)
        return stack

    def test_stale_generation_and_mismatched_original_deadline_are_strict(self):
        mutations = (
            lambda s: s['sight'].update(generation=s['input']['generation']-1),
            lambda s: s['sight']['contact'].update(generation=s['input']['generation']-1),
            lambda s: s['sight'].update(deadlineQpc=s['native_deadline_qpc']+1),
            lambda s: s['sight'].update(deadlineNs=s['native_deadline_ns']+1),
            lambda s: s['sight']['contact'].update(deadlineQpc=s['native_deadline_qpc']+1),
            lambda s: s['sight']['contact'].update(deadlineNs=s['native_deadline_ns']+1),
            lambda s: s['sight']['native'].update(physicalItem=999),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.harness_context():
                h, _, s = self.prepared()
                mutation(s)
                with self.assertRaisesRegex(d.transport.REFUSAL, 'mismatch'):
                    h.driver.send(['trigger r 1'])
                self.assertEqual(h.command_writes, [])
                self.assertEqual(h.driver.commands, 0)

    def test_incomplete_wait_then_current_source_authorizes_only_one_press(self):
        h, future, s = self.prepared(pending=True)
        v, original = h.driver, copy.deepcopy(s)
        clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
        future['mutate'] = None
        start = h.now
        v.send(['trigger r 1'])
        self.assertEqual(len(h.command_writes), 1)
        self.assertGreater(h.command_writes[0][0], start)
        self.assertTrue(h.command_writes[0][1].startswith(b'trigger r 1\n'))
        self.assertEqual(v.commands, 2)  # Trigger plus ordinary transport sentinel.
        self.assertEqual(s, original)
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
        events = [e for e in v.artifacts.events if e['type'].startswith('selected_shot_')]
        self.assertEqual([e['type'] for e in events], ['selected_shot_readiness_pending', 'selected_shot_ready'])
        self.assertFalse(events[0]['commandAuthority'])
        self.assertEqual(events[0]['sourceDeadlineNs'], v.snapshot_source_deadline(original))
        self.assertEqual(events[1]['sourceDeadlineNs'], v.snapshot_source_deadline(v.latest_snapshot))
        self.assertLess(h.command_writes[0][0], events[1]['sourceDeadlineNs'])
        with self.assertRaises(d.transport.REFUSAL):
            v.send(['trigger r 1'])
        self.assertEqual(len(h.command_writes), 1)

    def test_incomplete_wait_keeps_driver_and_stage_deadlines(self):
        for bound in ('deadline', 'stage_deadline'):
            with self.subTest(bound=bound), self.harness_context():
                h, _, s = self.prepared(pending=True)
                v = h.driver
                setattr(v, bound, h.now+150*self.MS)
                clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
                original = copy.deepcopy(s)
                with self.assertRaisesRegex(d.transport.REFUSAL, 'deadline|maximumGestureNs'):
                    v.send(['trigger r 1'])
                self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
                self.assertEqual(s, original)
                self.assertEqual(h.command_writes, [])

    def test_pending_wait_retains_response_and_outer_deadlines(self):
        h, _, _ = self.prepared(pending=True)
        v = h.driver
        # Real Driver.__init__ retains its initial simulator state. A neutral
        # timeout can wait for a late diagnostic within the existing outer bound.
        v.state = copy.deepcopy(h.state)
        clocks = v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started
        h.response_after = None
        with self.assertRaisesRegex(d.transport.REFUSAL, 'deadline|maximumGestureNs'):
            v.send(['trigger r 1'])
        self.assertEqual(h.command_writes, [])
        self.assertEqual((v.deadline, v.stage_deadline, v.hard_deadline, v.demand_started), clocks)
        timeouts = [e for e in v.artifacts.events if e['type'] == 'snapshot_response_timeout']
        self.assertEqual(len(timeouts), 1)
        self.assertEqual(timeouts[0]['responseDeadlineNs'], v.last_request+d.transport.SNAPSHOT_TIMEOUT)
        held = [e for e in v.artifacts.events if e['type'] == 'snapshot_neutral_observation_hold']
        self.assertEqual(len(held), 1)
        self.assertFalse(held[0]['commandAuthority'])
        self.assertEqual(held[0]['observationDeadlineNs'], min(clocks[:2]))

    def test_pending_wait_rejects_owner_family_binding_and_mapping_changes(self):
        mutations = (
            lambda s: s['owner'].update(weapon=999),
            lambda s: s['equipment'].update(persistence=999),
            lambda s: s['sight']['family'].update(persistentId='changed'),
            lambda s: s['fixedContainer']['view']['binding'].update(switching=999),
            lambda s: s['mapping']['weaponWorldMeters'][3].__setitem__(0, 0.1),
            lambda s: s['fixedContainer']['view']['snapshot']['counts'].update(loaded=0),
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.harness_context():
                h, future, _ = self.prepared(pending=True)
                future['mutate'] = mutation
                with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                    h.driver.send(['trigger r 1'])
                self.assertEqual(h.command_writes, [])

    def test_expired_ready_source_is_diagnostic_then_new_source_is_required(self):
        h, _, s = self.prepared()
        v, original = h.driver, copy.deepcopy(s)
        h.now = v.snapshot_source_deadline(s)
        with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
            v.selected_shot_ready(s, h.now)
        v.send(['trigger r 1'])
        self.assertEqual(s, original)
        events = [e for e in v.artifacts.events if e['type'] == 'selected_shot_readiness_pending']
        self.assertEqual(events[0]['reason'], 'original_source_expired')
        self.assertGreater(v.latest_snapshot['input']['generation'], s['input']['generation'])
        self.assertEqual(len(h.command_writes), 1)

    def test_publication_rechecks_readiness_and_original_source_expiry(self):
        for failure in ('contact', 'expiry'):
            with self.subTest(failure=failure), self.harness_context():
                h, _, s = self.prepared()
                v = h.driver
                wait = v.wait_pre_shot_ready
                def after_wait():
                    wait()
                    if failure == 'contact':
                        self.unavailable(v.latest_snapshot)
                    else:
                        h.now = v.snapshot_source_deadline(v.latest_snapshot)
                with patch.object(v, 'wait_pre_shot_ready', side_effect=after_wait):
                    with self.assertRaises((d.transport.REFUSAL, d.planner.Refusal)):
                        v.send(['trigger r 1'])
                self.assertEqual(h.command_writes, [])

    def test_atomic_publication_keeps_original_source_deadline(self):
        h, _, s = self.prepared()
        v = h.driver
        deadline = v.snapshot_source_deadline(s)
        def expire(path, raw, **options):
            if path == v.command_path:
                h.now = deadline
            h.write(path, raw, **options)
        with patch.object(d.transport, 'atomic', side_effect=expire):
            with self.assertRaisesRegex(d.transport.REFUSAL, 'original command publication deadline'):
                v.send(['trigger r 1'])
        self.assertEqual(h.command_writes, [])
        self.assertEqual(v.commands, 0)
        self.assertEqual(h.driver_publication_deadline, deadline)

    def test_readiness_rejects_external_input_or_command_owner(self):
        for changed in ('input', 'capture', 'owner'):
            with self.subTest(changed=changed), self.harness_context():
                h, _, _ = self.prepared(pending=True)
                if changed == 'input':
                    h.state['controls']['trigR'] = 1.
                elif changed == 'capture':
                    h.state['captureSeq'] += 1
                else:
                    h.files[h.driver.command_path] = b'foreign\n'
                with self.assertRaises(d.transport.REFUSAL):
                    h.driver.send(['trigger r 1'])
                self.assertEqual(h.command_writes, [])

    def test_generic_selected_ammo_remains_opt_out(self):
        h, _, s = self.prepared(pending=True)
        v = h.driver
        v.__class__ = fixed.SelectedAmmoDriverTests.Driver
        self.assertFalse(v.require_shot_attachment_ready)
        v.send(['trigger r 1'])
        self.assertEqual(len(h.command_writes), 1)
        self.assertFalse(any(e['type'].startswith('selected_shot_') for e in v.artifacts.events))

    def test_one_press_release_does_not_require_post_shot_sight_readiness(self):
        h, _, _ = fixed.SelectedAmmoDriverTests.shot_harness(self)
        self.assertEqual(h.driver.commands, 4)
        self.assertEqual(len(h.command_writes), 2)
        self.assertTrue(h.command_writes[0][1].startswith(b'trigger r 1\n'))
        self.assertTrue(h.command_writes[1][1].startswith(b'trigger r 0\n'))
        self.assertEqual(len([e for e in h.driver.artifacts.events if e['type'] == 'selected_shot_ready']), 1)


if __name__ == '__main__':
    unittest.main()
