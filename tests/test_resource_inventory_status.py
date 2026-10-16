import copy
import sys
import unittest
import json
import subprocess
import tempfile
import uuid
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



def three_pump_two_shell_trace():
    trace = completed_trace()
    game = trace['gameplay']; probe = game['resource_inventory_probe']; flow = game['reload_flow']
    probe.update(requested_shells=2, shell_loaded_before=8, shell_reserve_before=24)
    probe['physical_pump'].update(scenario=5, completed_cycles=3, requested_cycles=3,
        cycles=[dict(loaded_after_shot=n, reserve=24, support_returned=True) for n in (7, 6, 5)])
    flow['native_cycle_candidate'].update(releases=3, recovery_releases=0, acknowledgements=3)
    probe['shell'].update(submitted=2, completed=2, requested_rounds=2, loaded_before=5, reserve_before=24,
        rounds=[dict(number=1, acquired=1, completed_ns=100, first_neutral_input=0, last_neutral_input=0,
                     grab_input=10, loaded=6, reserve=23),
                dict(number=2, acquired=2, completed_ns=200, first_neutral_input=11, last_neutral_input=12,
                     grab_input=13, loaded=7, reserve=22)])
    identity = dict(weapon=0x20000, firing=[0x30000, 0x40000, 0x50000])
    transactions = []
    for index in range(2):
        base = dict(identity=identity, verified=True, all_three_held=True)
        transactions.append(dict(request=index+1, item_id=1, item_generation=index+1, resolved=True, units=1,
            submitted_ns=50+100*index, resolved_ns=100+100*index,
            before=dict(base, loaded=5+index, reserve=24-index),
            after=dict(base, loaded=6+index, reserve=23-index),
            ack=dict(identity=identity, verified=True, operation=3, status=1, server_invocation=10+index)))
    game['physical_reload'] = dict(transaction_dropped=0, dropped=0, transactions=transactions)
    flow['native_abort_cleanup'] = dict(context_writes=False, ammo_writes=False, active=False,
        cycle_history=[dict(claimed_mask=7, completed_mask=7, failure=0, initial_loaded=7,
                            initial_reserve=22, terminal_ns=300, deadline_ns=400)])
    flow['expired_control_probe'] = dict(enabled=True, drained=True, exercised=True, passed=True, state='passed',
        submissions=1, replay_accepted=False, fresh_accepted=True, debt_preserved=True, input_preserved=True)
    return trace


class ThreePumpTwoShellStatusTests(unittest.TestCase):
    def test_shell_item_identity_includes_generation(self):
        trace = three_pump_two_shell_trace()
        rows = trace['gameplay']['physical_reload']['transactions']
        self.assertEqual(rows[0]['item_id'], rows[1]['item_id'])
        self.assertNotEqual(rows[0]['item_generation'], rows[1]['item_generation'])
        self.assertTrue(check(trace, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        for value in [1, 0, -1, True, '2', None]:
            with self.subTest(invalid_generation=value):
                changed = copy.deepcopy(trace)
                changed['gameplay']['physical_reload']['transactions'][1]['item_generation'] = value
                self.assertFalse(check(changed, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        for key in ['item_id', 'item_generation']:
            with self.subTest(missing_key=key):
                changed = copy.deepcopy(trace)
                del changed['gameplay']['physical_reload']['transactions'][1][key]
                self.assertFalse(check(changed, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])

    def test_explicit_mode_and_all_receipts_required(self):
        trace = three_pump_two_shell_trace()
        self.assertTrue(check(trace, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        self.assertFalse(check(trace, True)['runtime_completed'])
        self.assertFalse(check(completed_trace(), True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        self.assertFalse(check(trace, True, empty_pump=True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        self.assertFalse(check(trace, True, interrupted_pump=True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        for path, value in [
            (('resource_inventory_probe', 'requested_shells'), 1),
            (('resource_inventory_probe', 'physical_pump', 'cycles', 2, 'loaded_after_shot'), 6),
            (('resource_inventory_probe', 'shell', 'rounds', 1, 'loaded'), 8),
            (('resource_inventory_probe', 'shell', 'rounds', 1, 'reserve'), 21),
            (('resource_inventory_probe', 'shell', 'rounds', 1, 'first_neutral_input'), 10),
            (('physical_reload', 'transactions', 1, 'resolved'), False),
            (('physical_reload', 'transactions', 1, 'item_generation'), 1),
            (('physical_reload', 'transactions', 1, 'before', 'loaded'), 5),
            (('physical_reload', 'transactions', 1, 'after', 'loaded'), 8),
            (('physical_reload', 'transactions', 1, 'ack', 'verified'), False),
            (('reload_flow', 'native_abort_cleanup', 'cycle_history', 0, 'completed_mask'), 3),
            (('reload_flow', 'native_abort_cleanup', 'ammo_writes'), True),
            (('reload_flow', 'expired_control_probe', 'exercised'), False),
            (('reload_flow', 'expired_control_probe', 'passed'), False),
            (('reload_flow', 'expired_control_probe', 'submissions'), 0),
            (('reload_flow', 'expired_control_probe', 'replay_accepted'), True),
            (('reload_flow', 'expired_control_probe', 'fresh_accepted'), False),
            (('reload_flow', 'expired_control_probe', 'input_preserved'), False),
        ]:
            with self.subTest(path=path):
                bad = copy.deepcopy(trace); target = bad['gameplay']
                for part in path[:-1]:
                    target = target[part]
                target[path[-1]] = value
                self.assertFalse(check(bad, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])
        for section in ['physical_reload', 'resource_inventory_probe']:
            bad = copy.deepcopy(trace); del bad['gameplay'][section]
            self.assertFalse(check(bad, True, three_pump_two_shell=True, expired_control=True)['runtime_completed'])



class UnreachablePumpHandStatusTests(unittest.TestCase):
    def trace(self):
        trace = three_pump_two_shell_trace()
        game = trace['gameplay']
        game['resource_inventory_probe']['pump_unreachable_hand_probe'] = True
        game['reload_flow']['expired_control_probe'] = dict(enabled=False)
        game['rig_publication'] = dict(packing_failures=0, source_changes=0,
            pump_presentation=dict(unreachable_hand_pairs=3, unreachable_hand_copies=6,
                mechanical_without_hand=4, reach_rejects=4, pose_rejects=0, source_rejects=0,
                copies=30, pairs=15))
        return trace

    def test_actual_pose_pair_marker_required_and_expired_trial_separate(self):
        trace = self.trace()
        result = check(trace, True, three_pump_two_shell=True, pump_unreachable_hand=True)
        self.assertTrue(result['runtime_completed'])
        self.assertNotIn('expired_control_exercised', result['checks'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True)['runtime_completed'])
        self.assertFalse(check(trace, True, pump_unreachable_hand=True)['runtime_completed'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True,
                               expired_control=True, pump_unreachable_hand=True)['runtime_completed'])
        for path, value in [
            (('resource_inventory_probe', 'pump_unreachable_hand_probe'), False),
            (('rig_publication', 'pump_presentation', 'unreachable_hand_pairs'), 0),
            (('rig_publication', 'pump_presentation', 'unreachable_hand_pairs'), True),
            (('rig_publication', 'pump_presentation', 'unreachable_hand_copies'), 5),
            (('rig_publication', 'pump_presentation', 'reach_rejects'), 0),
            (('rig_publication', 'pump_presentation', 'source_rejects'), 1),
            (('rig_publication', 'packing_failures'), 1),
            (('rig_publication', 'source_changes'), 1),
            (('reload_flow', 'expired_control_probe', 'enabled'), True),
            (('physical_reload', 'transactions', 1, 'after', 'loaded'), 8),
            (('resource_inventory_probe', 'physical_pump', 'cycles', 2, 'loaded_after_shot'), 6),
        ]:
            with self.subTest(path=path):
                changed=copy.deepcopy(trace); target=changed['gameplay']
                for key in path[:-1]: target=target[key]
                target[path[-1]]=value
                self.assertFalse(check(changed, True, three_pump_two_shell=True,
                                       pump_unreachable_hand=True)['runtime_completed'])
        missing=copy.deepcopy(trace)
        del missing['gameplay']['rig_publication']['pump_presentation']['unreachable_hand_pairs']
        self.assertFalse(check(missing, True, three_pump_two_shell=True,
                              pump_unreachable_hand=True)['runtime_completed'])

class ExtendedPumpHandStatusTests(unittest.TestCase):
    def trace(self):
        trace = UnreachablePumpHandStatusTests().trace()
        trace['gameplay']['rig_publication']['pump_presentation'] = dict(
            attempts=5, poses=5, copies=10, pairs=5, extended_hand_pairs=3, extended_hand_copies=6,
            extended_hand_error_samples=3, extended_hand_max_error_m=0., extended_hand_max_scale=1.36,
            authored_hand_poses=5, generic_hand_poses=0, mechanical_without_hand=0, reach_rejects=0,
            unreachable_hand_pairs=0, unreachable_hand_copies=0, pose_rejects=0, source_rejects=0, fallbacks=0)
        return trace

    def test_extended_mode_is_explicit_and_old_mechanical_fallback_is_insufficient(self):
        trace = self.trace()
        result = check(trace, True, three_pump_two_shell=True, pump_extended_hand=True)
        self.assertTrue(result['runtime_completed'])
        self.assertTrue(result['pump_extended_hand'])
        self.assertTrue(result['checks']['paired_extended_hand_error_bounded'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True)['runtime_completed'])
        self.assertFalse(check(trace, True, pump_extended_hand=True)['runtime_completed'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True, pump_extended_hand=True,
                               pump_unreachable_hand=True)['runtime_completed'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True, pump_extended_hand=True,
                               expired_control=True)['runtime_completed'])
        self.assertFalse(check(UnreachablePumpHandStatusTests().trace(), True,
                               three_pump_two_shell=True, pump_extended_hand=True)['runtime_completed'])

    def test_measured_error_scale_and_same_pair_sample_count_required(self):
        for field, value in [
            ('extended_hand_pairs', 0), ('extended_hand_pairs', True), ('extended_hand_copies', 5),
            ('extended_hand_error_samples', 2), ('extended_hand_error_samples', True),
            ('extended_hand_max_error_m', .00101), ('extended_hand_max_error_m', -.00001),
            ('extended_hand_max_error_m', float('nan')), ('extended_hand_max_error_m', float('inf')),
            ('extended_hand_max_error_m', True), ('extended_hand_max_scale', 1),
            ('extended_hand_max_scale', float('nan')), ('extended_hand_max_scale', True),
            ('mechanical_without_hand', 1), ('reach_rejects', 1), ('pose_rejects', 1),
            ('source_rejects', 1), ('fallbacks', 1), ('poses', 4), ('authored_hand_poses', 0),
            ('copies', 5), ('pairs', 2),
        ]:
            with self.subTest(field=field, value=value):
                trace = self.trace()
                trace['gameplay']['rig_publication']['pump_presentation'][field] = value
                self.assertFalse(check(trace, True, three_pump_two_shell=True,
                                       pump_extended_hand=True)['runtime_completed'])
        for field in ('extended_hand_pairs', 'extended_hand_copies', 'extended_hand_error_samples',
                      'extended_hand_max_error_m', 'extended_hand_max_scale'):
            with self.subTest(missing=field):
                trace = self.trace()
                del trace['gameplay']['rig_publication']['pump_presentation'][field]
                self.assertFalse(check(trace, True, three_pump_two_shell=True,
                                       pump_extended_hand=True)['runtime_completed'])

    def test_source_fresh_fixture_and_native_counts_remain_required(self):
        for path, value in [
            (('resource_inventory_probe', 'pump_unreachable_hand_probe'), False),
            (('rig_publication', 'packing_failures'), 1),
            (('rig_publication', 'source_changes'), 1),
            (('reload_flow', 'expired_control_probe', 'enabled'), True),
            (('physical_reload', 'transactions', 1, 'after', 'loaded'), 8),
            (('physical_reload', 'transactions', 1, 'item_generation'), 1),
            (('resource_inventory_probe', 'physical_pump', 'cycles', 2, 'loaded_after_shot'), 6),
            (('reload_flow', 'native_abort_cleanup', 'cycle_history', 0, 'completed_mask'), 3),
        ]:
            with self.subTest(path=path):
                trace = self.trace(); target = trace['gameplay']
                for key in path[:-1]:
                    target = target[key]
                target[path[-1]] = value
                self.assertFalse(check(trace, True, three_pump_two_shell=True,
                                       pump_extended_hand=True)['runtime_completed'])


class WholeReceiverGapStatusTests(unittest.TestCase):
    def evidence(self):
        trace=three_pump_two_shell_trace();game=trace['gameplay'];probe=game['resource_inventory_probe']
        pump=probe['physical_pump'];flow=game['reload_flow']
        gap=dict(completed=True, failed=False, input_and_tracking_withheld=True, native_authority=False,
                 original_deadline_ns=1_100_000_000,last_pregap_publish_after_ns=1_010_000_000,
                 last_pregap_deadline_upper_ns=1_110_000_000,begin_ns=1_020_000_000,
                 return_ns=6_120_000_000,first_return_ns=6_130_000_000,
                 input=150,last_pregap_input=152,first_return_input=250,
                 old_space=10,new_space=11,cycle=1,shot=1,loaded=7,reserve=24,capacity=8,travel_m=.04)
        pump.update(scenario=6,receiver_gap_scenario=True,receiver_missing_samples=450,receiver_converging_samples=440,
                    receiver_converging_first_ns=1_120_000_000,receiver_converging_last_ns=6_119_000_000,
                    followed_tracking_rebase=True,partial_gun_claim=12,recovery_gun_claim=22,
                    neutral_sequence=250,neutral_ns=6_200_000_000,
                    receiver_return_neutral=dict(sequence=250,space=11,observed_ns=6_130_000_000,
                        deadline_ns=6_230_000_000,accepted_ns=6_200_000_000))
        for index,row in enumerate(pump['cycles']):
            row.update(cycle=index+1,shot=index+1,recovery=index==0,recovery_grant=8,guard_epoch=2,
                       grant_neutral_sequence=270,grant_neutral_observed_ns=6_340_000_000,
                       grant_neutral_deadline_ns=6_440_000_000,source_sequence=320+100*index,
                       requested_ns=7_000_000_000+2_000_000_000*index,ready_ns=7_100_000_000+2_000_000_000*index,
                       support_observed_ns=7_110_000_000+2_000_000_000*index,rear_pair=True,verified_pairs=30)
        probe['origin_observation']=dict(started=True,verified=True,full_stroke_verified=True,native_authority=False,
            old_space=10,new_space=11,cycle=1,shot=1,loaded=7,reserve=24,verified_ns=6_150_000_000)
        probe.update(input_rows_dropped=0,effective_input=[dict(now_ns=6_200_000_000,left_squeeze=0,right_squeeze=0,trigger=0)])
        flow['native_cycle_candidate'].update(recovery_releases=1,releases=2)
        flow['expired_control_probe']=dict(enabled=False)
        return trace,gap

    def check_gap(self,trace,gap):
        return check(trace,True,three_pump_two_shell=True,pump_receiver_gap=True,receiver_gap=gap)

    def test_whole_gap_is_distinct_from_partial_tracking_and_expiry(self):
        trace,gap=self.evidence();result=self.check_gap(trace,gap)
        self.assertTrue(result['runtime_completed'],result)
        self.assertFalse(self.check_gap(trace,None)['runtime_completed'])
        self.assertFalse(self.check_gap(three_pump_two_shell_trace(),gap)['runtime_completed'])
        self.assertFalse(check(trace,True,three_pump_two_shell=True,expired_control=True,receiver_gap=gap)['runtime_completed'])
        self.assertFalse(check(trace,True,three_pump_two_shell=True,expired_control=True,pump_receiver_gap=True,receiver_gap=gap)['runtime_completed'])

    def test_incomplete_gap_writes_failure_json_instead_of_crashing(self):
        # Completed receiver pause does not imply any completed native cycle.
        for count in (0,1,2):
            trace,gap=self.evidence();pump=trace['gameplay']['resource_inventory_probe']['physical_pump']
            pump['cycles']=pump['cycles'][:count];pump['completed_cycles']=count
            result=self.check_gap(trace,gap)
            self.assertFalse(result['runtime_completed'])
            self.assertFalse(result['checks']['fresh_full_stroke_before_next_shot'])
        trace,gap=self.evidence();probe=trace['gameplay']['resource_inventory_probe']
        probe.update(phase=12,failure=19,completed=False)
        probe['physical_pump'].update(phase=11,failure=13,completed=False,completed_cycles=0,cycles=[])
        # mkdir's ordinary inherited permissions also work under Windows test
        # sandboxes where mkdtemp's private0700 ACL cannot be reopened.
        root=Path(tempfile.gettempdir())/('fvr-status-'+uuid.uuid4().hex);root.mkdir()
        source=root/'trace.json';receiver=root/'receiver.json';target=root/'status.json'
        try:
            source.write_text(json.dumps(trace));receiver.write_text(json.dumps(gap))
            checker=Path(__file__).resolve().parents[1]/'tools/check_resource_inventory_status.py'
            run=subprocess.run([sys.executable,'-B',str(checker),'--trace',str(source),'--output',str(target),
                '--manual-pump','--three-pump-two-shell','--pump-receiver-gap','--receiver-gap',str(receiver)],
                capture_output=True,text=True)
            self.assertEqual(run.returncode,2,run.stderr)
            status=json.loads(target.read_text());self.assertFalse(status['runtime_completed'])
            self.assertEqual(status['failure'],19)
            self.assertFalse(status['checks']['fresh_full_stroke_before_next_shot'])
        finally:
            for path in (source,receiver,target):path.unlink(missing_ok=True)
            root.rmdir()

    def test_short_gap_restamping_and_unobserved_return_fail(self):
        for field,value in [('completed',False),('failed',True),('input_and_tracking_withheld',False),
                            ('native_authority',True),('return_ns',6_109_999_999),
                            ('last_pregap_deadline_upper_ns',1_109_000_000),('last_pregap_input',250),
                            ('first_return_input',151),('first_return_ns',6_110_000_000),('new_space',10),
                            ('travel_m',.07),('cycle',2),('shot',2),('loaded',6),('reserve',25)]:
            with self.subTest(field=field):
                trace,gap=self.evidence();gap[field]=value
                self.assertFalse(self.check_gap(trace,gap)['runtime_completed'])
        for field in self.evidence()[1]:
            with self.subTest(missing=field):
                trace,gap=self.evidence();del gap[field]
                self.assertFalse(self.check_gap(trace,gap)['runtime_completed'])

    def test_exact_debt_and_fresh_full_stroke_required(self):
        for path,value in [
            (('physical_pump','receiver_converging_samples'),0),
            (('physical_pump','receiver_converging_first_ns'),5_900_000_000),
            (('physical_pump','receiver_converging_last_ns'),6_200_000_000),
            (('physical_pump','interruption_observed'),False),
            (('physical_pump','followed_tracking_rebase'),False),
            (('physical_pump','recovery_gun_claim'),12),
            (('physical_pump','neutral_sequence'),249),
            (('physical_pump','receiver_return_neutral','sequence'),249),
            (('physical_pump','receiver_return_neutral','space'),10),
            (('physical_pump','receiver_return_neutral','observed_ns'),1_010_000_000),
            (('physical_pump','receiver_return_neutral','deadline_ns'),6_200_000_000),
            (('physical_pump','cycles',0,'grant_neutral_observed_ns'),6_120_000_000),
            (('physical_pump','cycles',0,'recovery'),False),
            (('physical_pump','cycles',0,'rear_pair'),False),
            (('physical_pump','cycles',0,'grant_neutral_sequence'),321),
            (('physical_pump','cycles',1,'shot'),1),
            (('physical_pump','cycles',1,'requested_ns'),7_000_000_000),
            (('origin_observation','verified'),False),
            (('origin_observation','full_stroke_verified'),False),
            (('input_rows_dropped',),1),
            (('effective_input',0,'now_ns'),2_000_000_000),
            (('effective_input',0,'right_squeeze'),1),
            (('effective_input',0,'trigger'),1),
            (('origin_observation','old_space'),9),
            (('origin_observation','loaded'),6),
        ]:
            with self.subTest(path=path):
                trace,gap=self.evidence();target=trace['gameplay']['resource_inventory_probe']
                for key in path[:-1]:target=target[key]
                target[path[-1]]=value
                self.assertFalse(self.check_gap(trace,gap)['runtime_completed'])


class ShellFocusLossStatusTests(unittest.TestCase):
    def fixture(self):
        trace = three_pump_two_shell_trace(); game = trace['gameplay']; p = game['resource_inventory_probe']; f = game['reload_flow']
        p['full_return'] = dict(actual_consumer_completed=False, phase=0, failure=0, completed_cycles=0)
        p['inventory'].update(phase=21, completed=False)
        p['shell_support'] = dict(returned=False, token=0, claim=0)
        p.update(rifle_fire_ns=0, rifle_shot_sequence=0, rifle_shot_observed_ns=0, rifle_support_required=False)
        f['ammo_resource_hands'].update(calls=4, completed=4); f['expired_control_probe'] = dict(enabled=False)
        target = dict(space=5, cycle=1, loaded=7, reserve=22, capacity=8, completed_observed_ns=2_000_000_000)
        p['shell_focus_loss'] = dict(enabled=True, completed=True, failed=False, native_authority=False, target=target,
            first_lost_input=102, last_lost_input=130, return_input=131,
            first_lost_observed_ns=2_020_000_000, first_lost_ns=2_021_000_000, first_lost_deadline_ns=2_120_000_000,
            last_lost_observed_ns=2_510_000_000, return_observed_ns=2_520_000_000,
            return_ns=2_522_000_000, return_deadline_ns=2_620_000_000,
            stable_start_ns=2_540_000_000, stable_last_ns=5_540_000_000, stable_samples=61)
        f['native_abort_cleanup']['cycle_history'][0].update(cycle=1, armed_ns=2_021_100_000,
            terminal_ns=2_023_000_000, deadline_ns=2_102_000_000)
        receiver = dict(completed=True, failed=False, native_authority=False, tracking_withheld=False,
            begin_ns=2_011_000_000, first_lost_ns=2_020_100_000, last_lost_ns=2_519_000_000,
            return_ns=2_521_000_000, first_lost_input=101, return_input=131, lost_samples=30,
            witness=dict(target, input=100, observed_ns=2_010_000_000, deadline_ns=2_110_000_000))
        return trace, receiver

    def evaluate(self, trace, receipt):
        return check(trace, True, three_pump_two_shell=True, shell_focus_loss=True, shell_focus_receipt=receipt)

    def test_focus_needs_real_loss_original_bound_and_neutral_dwell(self):
        trace, receipt = self.fixture(); result = self.evaluate(trace, receipt)
        self.assertTrue(result['runtime_completed'], result)
        self.assertFalse(result['headset_verified']); self.assertFalse(result['callback_evidence_verified'])
        self.assertNotIn('return_after_holster', result['checks']); self.assertNotIn('immediate_support', result['checks'])
        for field, value in [('completed', False), ('first_lost_input', 0), ('return_input', 130),
                             ('return_deadline_ns', 2_900_000_000), ('stable_last_ns', 5_539_999_999),
                             ('stable_samples', 29), ('native_authority', True)]:
            bad = copy.deepcopy(trace); bad['gameplay']['resource_inventory_probe']['shell_focus_loss'][field] = value
            self.assertFalse(self.evaluate(bad, receipt)['runtime_completed'], field)
        for field, value in [('tracking_withheld', True), ('completed', False), ('return_ns', 2_021_000_000)]:
            bad = copy.deepcopy(receipt); bad[field] = value
            self.assertFalse(self.evaluate(trace, bad)['runtime_completed'], field)
        bad = copy.deepcopy(trace); bad['gameplay']['reload_flow']['native_abort_cleanup']['cycle_history'][0]['deadline_ns'] = 2_130_000_000
        self.assertFalse(self.evaluate(bad, receipt)['runtime_completed'])
        bad = copy.deepcopy(trace); bad['gameplay']['resource_inventory_probe']['shell_focus_loss']['target']['loaded'] = 8
        self.assertFalse(self.evaluate(bad, receipt)['runtime_completed'])
        self.assertFalse(self.evaluate(trace, None)['runtime_completed'])
        self.assertFalse(check(trace, True, three_pump_two_shell=True, expired_control=True,
            shell_focus_loss=True, shell_focus_receipt=receipt)['runtime_completed'])

    def test_ordinary_mode_does_not_claim_focus_coverage(self):
        trace, receipt = self.fixture()
        self.assertFalse(check(trace, True, three_pump_two_shell=True)['runtime_completed'])
        self.assertFalse(self.evaluate(three_pump_two_shell_trace(), receipt)['runtime_completed'])
        self.assertTrue(check(three_pump_two_shell_trace(), True, three_pump_two_shell=True,
            expired_control=True)['runtime_completed'])


if __name__ == '__main__':
    unittest.main()
