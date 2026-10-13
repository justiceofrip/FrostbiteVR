import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_resource_inventory_status import check


def completed_trace():
    # Synthetic report contract, not native-game evidence.
    magazine = dict(actual_consumer_completed=True, phase=12, failure=0, completed_cycles=2,
                    presentation_continuity_verified=True, presentation_fallbacks_during_interaction=0,
                    first_presentation_fallback_ns=0)
    returned = dict(magazine, completed_cycles=1)
    pump = dict(phase=10, failure=0, completed=True, completed_cycles=1, requested_cycles=1,
                scenario=3, initial_support_claim=11, initial_gun_claim=12,
                initial_support_source=100, support_adopted_without_release=True,
                partial_travel_m=.04, partial_pairs=2, partial_source_sequence=150,
                interruption_observed=True, lost_started_ns=1_000_000_000,
                lost_ended_ns=1_500_000_000, lost_first_sequence=151,
                lost_last_sequence=199, neutral_sequence=200,
                cycles=[dict(recovery=True, recovery_grant=1, guard_epoch=1,
                             grant_neutral_sequence=200, support_returned=True)])
    probe = dict(input_only=True, persistent_consumers=True, phase=11, failure=0,
                 completed=True, manual_pump=True, first_magazine=magazine,
                 full_return=returned, inventory=dict(phase=17, failure=0, completed=True),
                 shell=dict(actual_consumer_completed=True, phase=7, failure=0,
                            submitted=1, completed=1, requested_rounds=1),
                 physical_pump=pump, shell_support=dict(returned=True, token=1, claim=2),
                 rifle_fire_ns=100, rifle_shot_sequence=301, rifle_shot_observed_ns=200)
    cycle = dict(failure=0, releases=0, recovery_releases=1, acknowledgements=1, blocks_fire=False,
                 recovery_enabled=1, guarded_updates=30)
    flow = dict(ammo_resource_hands=dict(ordinary_input_build=True, calls=6, completed=6,
                                        rejected=0, pending=0),
                native_cycle_candidate=cycle, drained=True, in_flight=0)
    return dict(gameplay=dict(reload_flow=flow, resource_inventory_probe=probe), hooks_disabled=True)


class ResourceStatusTests(unittest.TestCase):
    def test_revisit_requires_second_native_pump_and_support_return(self):
        trace = completed_trace()
        probe = trace['gameplay']['resource_inventory_probe']
        second = copy.deepcopy(probe['physical_pump'])
        second.update(scenario=5, cycles=[dict(loaded_after_shot=7, reserve=23, support_returned=True)])
        probe['pump_revisit'] = dict(enabled=True, completed=True, loaded_before=8,
                                    reserve_before=23, physical_pump=second)
        cycle = trace['gameplay']['reload_flow']['native_cycle_candidate']
        self.assertFalse(check(trace, True)['runtime_completed'])
        cycle.update(releases=2, recovery_releases=0, acknowledgements=2)
        self.assertTrue(check(trace, True)['runtime_completed'])
        for field, value in [('completed', False), ('initial_support_claim', 0),
                             ('support_adopted_without_release', False)]:
            bad = copy.deepcopy(trace)
            bad['gameplay']['resource_inventory_probe']['pump_revisit']['physical_pump'][field] = value
            self.assertFalse(check(bad, True)['runtime_completed'])
        for field, value in [('loaded_after_shot', 8), ('reserve', 22), ('support_returned', False)]:
            bad = copy.deepcopy(trace)
            bad['gameplay']['resource_inventory_probe']['pump_revisit']['physical_pump']['cycles'][0][field] = value
            self.assertFalse(check(bad, True)['runtime_completed'])
        self.assertFalse(check(trace, True, interrupted_pump=True)['runtime_completed'])

    def test_native_pause_recovery_is_explicit_and_needs_real_receipts(self):
        trace = completed_trace()
        pump = trace['gameplay']['resource_inventory_probe']['physical_pump']
        cycle = trace['gameplay']['reload_flow']['native_cycle_candidate']
        pump.update(scenario=5, native_pause_regrips=1)
        cycle['held_suspensions'] = 1
        result = check(trace, True)
        self.assertTrue(result['runtime_completed'])
        self.assertEqual(result['native_pause_recovery_status'], 'completed_with_release_and_regrip')
        self.assertFalse(result['headset_verified'])
        for field, value in [('native_pause_regrips', 2), ('native_pause_regrips', True),
                             ('neutral_sequence', 201)]:
            bad = copy.deepcopy(trace)
            bad['gameplay']['resource_inventory_probe']['physical_pump'][field] = value
            self.assertFalse(check(bad, True)['runtime_completed'])
        for field in ['held_suspensions', 'recovery_enabled', 'guarded_updates', 'recovery_releases']:
            bad = copy.deepcopy(trace)
            bad['gameplay']['reload_flow']['native_cycle_candidate'][field] = 0
            self.assertFalse(check(bad, True)['runtime_completed'])

    def test_eight_shots_need_empty_tube_and_each_support_return(self):
        trace = completed_trace()
        probe = trace['gameplay']['resource_inventory_probe']
        flow = trace['gameplay']['reload_flow']
        probe['first_magazine'].update(completed_cycles=0, phase=0, actual_consumer_completed=False)
        flow['ammo_resource_hands'].update(calls=2, completed=2)
        flow['native_cycle_candidate'].update(releases=8, recovery_releases=0, acknowledgements=8)
        probe.update(shell_loaded_before=8, shell_reserve_before=24)
        probe['physical_pump'].update(scenario=0, completed_cycles=8, requested_cycles=8,
            cycles=[dict(loaded_after_shot=n, reserve=24, support_returned=True) for n in range(7, -1, -1)])
        probe['shell'].update(loaded_before=0, rounds=[dict(loaded=1, reserve=23)])
        self.assertTrue(check(trace, True, empty_pump=True)['runtime_completed'])
        self.assertFalse(check(trace, True)['runtime_completed'])
        self.assertFalse(check(trace, True, True, True)['runtime_completed'])
        for index, field, value in [(7, 'loaded_after_shot', 1), (6, 'reserve', 23), (3, 'support_returned', False)]:
            bad = copy.deepcopy(trace)
            bad['gameplay']['resource_inventory_probe']['physical_pump']['cycles'][index][field] = value
            self.assertFalse(check(bad, True, empty_pump=True)['runtime_completed'])
        probe['shell']['rounds'][0]['loaded'] = 8
        self.assertFalse(check(trace, True, empty_pump=True)['runtime_completed'])

    def test_completed_interruption_remains_status_only(self):
        result = check(completed_trace(), True, True)
        self.assertTrue(result['runtime_completed'])
        self.assertFalse(result['callback_evidence_verified'])
        self.assertFalse(result['headset_verified'])

    def test_normal_completion_is_not_interruption_acceptance(self):
        trace = completed_trace()
        trace['gameplay']['resource_inventory_probe']['physical_pump']['scenario'] = 0
        self.assertTrue(check(trace, True)['runtime_completed'])
        self.assertFalse(check(trace, True, True)['runtime_completed'])

    def test_missing_or_old_recovery_proof_rejected(self):
        for field, value in [('recovery', False), ('recovery_grant', 0), ('guard_epoch', 0),
                             ('grant_neutral_sequence', 199), ('support_returned', False)]:
            with self.subTest(field=field):
                trace = completed_trace()
                trace['gameplay']['resource_inventory_probe']['physical_pump']['cycles'][0][field] = value
                self.assertFalse(check(trace, True, True)['runtime_completed'])

    def test_recovery_counts_must_be_distinct_and_retained(self):
        for field, value in [('recovery_releases', 0), ('recovery_releases', 2),
                             ('releases', 1), ('guarded_updates', 0), ('acknowledgements', 0)]:
            with self.subTest(field=field, value=value):
                trace = completed_trace()
                trace['gameplay']['reload_flow']['native_cycle_candidate'][field] = value
                self.assertFalse(check(trace, True, True)['runtime_completed'])

    def test_short_loss_or_missing_support_cannot_pass(self):
        for field, value in [('lost_ended_ns', 1_499_999_999), ('neutral_sequence', 199),
                             ('initial_support_claim', 0), ('support_adopted_without_release', False),
                             ('partial_pairs', 0), ('partial_travel_m', .08)]:
            with self.subTest(field=field):
                trace = completed_trace()
                trace['gameplay']['resource_inventory_probe']['physical_pump'][field] = value
                self.assertFalse(check(trace, True, True)['runtime_completed'])

    def test_existing_failure_preserved_and_missing_keys_rejected(self):
        trace = completed_trace()
        trace['gameplay']['resource_inventory_probe']['failure'] = 15
        result = check(trace, True, True)
        self.assertFalse(result['runtime_completed'])
        self.assertEqual(result['failure'], 15)
        broken = copy.deepcopy(trace)
        del broken['gameplay']['resource_inventory_probe']['physical_pump']['cycles']
        self.assertFalse(check(broken, True, True)['runtime_completed'])

    def test_pre_shot_retry_is_visible_and_requires_new_support(self):
        trace = completed_trace()
        pump = trace['gameplay']['resource_inventory_probe']['physical_pump']
        pump.update(pre_shot_regrips=1, pre_shot_loss_sequence=90,
                    pre_shot_loss_ns=1000, initial_support_ns=2000)
        result = check(trace, True, True)
        self.assertTrue(result['runtime_completed'])
        self.assertEqual(result['pre_shot_recovery_status'], 'completed')
        self.assertEqual(result['pre_shot_regrips'], 1)
        for field, value in [('pre_shot_regrips', 2), ('pre_shot_regrips', True),
                             ('pre_shot_loss_sequence', 100), ('pre_shot_loss_ns', 2000)]:
            bad = copy.deepcopy(trace)
            bad['gameplay']['resource_inventory_probe']['physical_pump'][field] = value
            self.assertFalse(check(bad, True, True)['runtime_completed'])
        trace['gameplay']['resource_inventory_probe']['failure'] = 19
        self.assertEqual(check(trace, True, True)['pre_shot_recovery_status'], 'failed')


if __name__ == '__main__':
    unittest.main()
