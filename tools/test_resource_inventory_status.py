import copy
import unittest
from check_resource_inventory_status import check


def completed():
    magazine = dict(actual_consumer_completed=True, phase=12, failure=0, completed_cycles=2,
                    presentation_continuity_verified=True, presentation_fallbacks_during_interaction=0,
                    first_presentation_fallback_ns=0)
    return dict(hooks_disabled=True, gameplay=dict(
        resource_inventory_probe=dict(input_only=True, persistent_consumers=True, phase=11, failure=0, completed=True,
            manual_pump=True, first_magazine=magazine, full_return={**magazine, 'completed_cycles':1},
            inventory=dict(phase=17, failure=0, completed=True),
            shell=dict(actual_consumer_completed=True, phase=7, failure=0, submitted=1, completed=1, requested_rounds=1),
            physical_pump=dict(phase=10, failure=0, completed=True, completed_cycles=1, requested_cycles=1),
            shell_support=dict(returned=True, token=3, claim=11),
            rifle_fire_ns=100, rifle_shot_sequence=5, rifle_shot_observed_ns=120),
        reload_flow=dict(drained=True, in_flight=0, record_begin_lock_drops=1,
            ammo_resource_hands=dict(ordinary_input_build=True, calls=6, completed=6, rejected=0, pending=0),
            native_cycle_candidate=dict(failure=0, releases=1, acknowledgements=1, blocks_fire=False))))


class ResourceStatusTests(unittest.TestCase):
    def test_visual_failure_remains_failed_without_losing_mechanical_result(self):
        trace = completed()
        magazine = trace['gameplay']['resource_inventory_probe']['first_magazine']
        magazine.update(presentation_continuity_verified=False,
                        presentation_fallbacks_during_interaction=1, first_presentation_fallback_ns=123)
        result = check(trace, True)
        self.assertTrue(result['mechanical_runtime_completed'])
        self.assertFalse(result['runtime_completed'])
        self.assertFalse(result['presentation_continuity_verified'])
        magazine['presentation_continuity_verified'] = True
        self.assertFalse(check(trace, True)['runtime_completed'])

    def test_missing_visual_evidence_cannot_claim_visual_acceptance(self):
        trace = completed()
        del trace['gameplay']['resource_inventory_probe']['first_magazine']['presentation_continuity_verified']
        result = check(trace, True)
        self.assertTrue(result['mechanical_runtime_completed'])
        self.assertFalse(result['runtime_completed'])

    def test_required_rifle_support_cannot_be_skipped(self):
        t = completed(); p = t['gameplay']['resource_inventory_probe']
        p.update(rifle_support_required=True, rifle_support_claim=3,
                 rifle_support_source=8, rifle_support_released=True)
        self.assertTrue(check(t, True)['runtime_completed'])
        for field in ('rifle_support_claim', 'rifle_support_source', 'rifle_support_released'):
            changed = copy.deepcopy(t); changed['gameplay']['resource_inventory_probe'][field] = 0
            self.assertFalse(check(changed, True)['runtime_completed'])

    def test_natural_interruption_requires_aim_and_support_evidence(self):
        t = completed(); p = t['gameplay']['resource_inventory_probe']
        p.update(rifle_support_required=True, rifle_support_claim=3,
                 rifle_support_source=8, rifle_support_released=True)
        pump = p['physical_pump']; pump.update(scenario=4, support_correction_rad=.35,
            initial_support_claim=2, initial_gun_claim=1, initial_support_source=10,
            support_adopted_without_release=True, partial_travel_m=.04, partial_pairs=2,
            partial_source_sequence=11, interruption_observed=True, lost_started_ns=100,
            lost_ended_ns=500000100, lost_first_sequence=12, lost_last_sequence=15,
            neutral_sequence=16, cycles=[dict(recovery=True, recovery_grant=1,
                guard_epoch=1, grant_neutral_sequence=16, support_returned=True)])
        t['gameplay']['reload_flow']['native_cycle_candidate'].update(releases=0,
            recovery_releases=1, recovery_enabled=1, guarded_updates=4)
        self.assertTrue(check(t, True, True)['runtime_completed'])
        for value in (0, .14, float('nan'), 1.1):
            changed = copy.deepcopy(t)
            changed['gameplay']['resource_inventory_probe']['physical_pump']['support_correction_rad'] = value
            self.assertFalse(check(changed, True, True)['runtime_completed'])
        p['rifle_support_required'] = False
        self.assertFalse(check(t, True, True)['runtime_completed'])
        pump['scenario'] = 3
        self.assertTrue(check(t, True, True)['runtime_completed'])

    def test_runtime_success_does_not_claim_complete_recording(self):
        r = check(completed(), True)
        self.assertTrue(r['runtime_completed'])
        self.assertFalse(r['callback_evidence_verified'])
        self.assertFalse(r['headset_verified'])

    def test_native_counter_cannot_replace_failed_component(self):
        for component in ('first_magazine', 'full_return', 'inventory', 'shell', 'physical_pump'):
            with self.subTest(component=component):
                t = completed(); t['gameplay']['resource_inventory_probe'][component]['failure'] = 1
                self.assertFalse(check(t, True)['runtime_completed'])

    def test_removal_only_reproduces_f2000_failure(self):
        t = completed(); p = t['gameplay']['resource_inventory_probe']
        p.update(phase=12, failure=15, completed=False)
        p['first_magazine'].update(phase=9, failure=4, completed_cycles=0, actual_consumer_completed=False)
        t['gameplay']['reload_flow']['ammo_resource_hands'].update(calls=1, completed=1)
        self.assertFalse(check(t, True)['runtime_completed'])

    def test_exact_requested_path_and_native_completion_required(self):
        t = completed()
        self.assertFalse(check(t, False)['runtime_completed'])
        for value in ('rejected', 'pending'):
            changed = copy.deepcopy(t); changed['gameplay']['reload_flow']['ammo_resource_hands'][value] = 1
            self.assertFalse(check(changed, True)['runtime_completed'])
        for field in ('releases', 'acknowledgements'):
            changed = copy.deepcopy(t); changed['gameplay']['reload_flow']['native_cycle_candidate'][field] = 0
            self.assertFalse(check(changed, True)['runtime_completed'])

    def test_shutdown_and_malformed_evidence_fail(self):
        t = completed(); t['hooks_disabled'] = False
        self.assertFalse(check(t, True)['runtime_completed'])
        for t in (None, {}, [], {'gameplay':None}):
            self.assertFalse(check(t, True)['runtime_completed'])


if __name__ == '__main__':
    unittest.main()
