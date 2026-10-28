"""Terminal runtime status for the combined input-only trial, not a trace audit."""
import argparse
import hashlib
import json
import math
from pathlib import Path

def check_campaign_magazine(trace):
    """Driver/cleanup status only; independent native transaction audit remains required."""
    result = dict(schema='fvr.bc2.campaign_magazine_status.v1', runtime_completed=False,
                  native_reload_accepted=False, independent_receipt_audit_required=True,
                  callback_evidence_verified=False, headset_verified=False)
    try:
        game=trace['gameplay']; probe=game['resource_inventory_probe']; flow=game['reload_flow']
        first=probe['first_magazine']; native=flow['ammo_resource_hands']
        checks=dict(
            exact_mode=probe['campaign_magazine'] is True and probe['magazine_only'] is True
                and probe['magazine_postfire'] is True and probe['support_only'] is False
                and probe['manual_pump'] is False,
            ordinary_input=probe['input_only'] is True and probe['persistent_consumers'] is True,
            driver_completed=probe['phase']==11 and probe['failure']==0 and probe['completed'] is True,
            partial_ready=probe['partial_magazine_ready'] is True and probe['partial_loaded']>0,
            two_cycles=first['actual_consumer_completed'] is True and first['phase']==12
                and first['failure']==0 and first['completed_cycles']==2,
            fresh_support=probe['support_reacquired'] is True and probe['rifle_support_required'] is True
                and probe['rifle_support_claim']>probe['initial_support_claim']>0,
            one_post_reload_shot=probe['rifle_shot_count']==1 and probe['rifle_shot_sequence']>0,
            four_operations=native['ordinary_input_build'] is True
                and native['calls']==native['completed']==4 and native['rejected']==native['pending']==0,
            detached=trace['hooks_disabled'] is True and flow['drained'] is True and flow['in_flight']==0)
        result.update(checks=checks,runtime_completed=all(checks.values()))
    except (KeyError,TypeError,ValueError) as error:
        result.update(checks={'well_formed_status':False},error=str(error))
    return result


def check(trace, manual_pump, interrupted_pump=False, empty_pump=False, three_pump_two_shell=False,
          expired_control=False, pump_unreachable_hand=False, pump_extended_hand=False, pump_receiver_gap=False, receiver_gap=None, shell_focus_loss=False, shell_focus_receipt=None):
    result = dict(schema='fvr.bc2.resource_inventory_status.v2', runtime_completed=False,
                  mechanical_runtime_completed=False, presentation_continuity_verified=False,
                  callback_evidence_verified=False, headset_verified=False,
                  interrupted_pump=interrupted_pump, three_pump_two_shell=three_pump_two_shell,
                  expired_control=expired_control, pump_unreachable_hand=pump_unreachable_hand, pump_extended_hand=pump_extended_hand,
                  pump_receiver_gap=pump_receiver_gap, shell_focus_loss=shell_focus_loss)
    try:
        game = trace['gameplay']; flow = game['reload_flow']; probe = game['resource_inventory_probe']
        first, returned, inventory, shell = (probe[k] for k in ('first_magazine', 'full_return', 'inventory', 'shell'))
        native = flow['ammo_resource_hands']
        expected_pumps = 3 if three_pump_two_shell else 8 if empty_pump else 1
        expected_shells = 2 if three_pump_two_shell else 1
        checks = dict(
            compatible_sequence=(not empty_pump or (manual_pump and not interrupted_pump))
                and (not three_pump_two_shell or (manual_pump and not interrupted_pump and not empty_pump)),
            explicit_diagnostic_mode=(sum((expired_control, pump_unreachable_hand, pump_extended_hand, pump_receiver_gap, shell_focus_loss)) == int(three_pump_two_shell)),
            requested_reach_mode=probe.get('pump_unreachable_hand_probe', False) is (pump_unreachable_hand or pump_extended_hand),
            ordinary_input=probe['input_only'] is True and probe['persistent_consumers'] is True,
            driver_completed=probe['phase'] == 11 and probe['failure'] == 0 and probe['completed'] is True,
            requested_mode=probe['manual_pump'] is manual_pump,
            first_magazine=(first['actual_consumer_completed'] is False and first['phase'] == 0 and first['failure'] == 0 and first['completed_cycles'] == 0) if empty_pump else
                (first['actual_consumer_completed'] is True and first['phase'] == 12 and first['failure'] == 0 and first['completed_cycles'] == 2),
            return_after_holster=(returned['actual_consumer_completed'] is False and returned['phase'] == returned['failure'] == returned['completed_cycles'] == 0) if shell_focus_loss else
                (returned['actual_consumer_completed'] is True and returned['phase'] == 12 and returned['failure'] == 0 and returned['completed_cycles'] == 1),
            inventory_completed=inventory['phase'] == (21 if shell_focus_loss else 17) and inventory['failure'] == 0 and inventory['completed'] is (not shell_focus_loss),
            shell_completed=shell['actual_consumer_completed'] is True and shell['phase'] == 7 and shell['failure'] == 0
                and shell['submitted'] == shell['completed'] == shell['requested_rounds'] == expected_shells
                and probe.get('requested_shells', 1) == expected_shells,
            native_magazine_operations=native['ordinary_input_build'] is True and native['calls'] == native['completed'] == (4 if shell_focus_loss else 2 if empty_pump else 6) and native['rejected'] == native['pending'] == 0,
            detached=trace['hooks_disabled'] is True and flow['drained'] is True and flow['in_flight'] == 0)
        if manual_pump:
            pump, cycle = probe['physical_pump'], flow['native_cycle_candidate']
            revisit = probe.get('pump_revisit', {})
            revisit_enabled = revisit.get('enabled') is True
            support = probe['shell_support']
            regrips = pump.get('pre_shot_regrips', 0)
            result['pre_shot_regrips'] = regrips
            checks['bounded_pre_shot_regrip'] = (type(regrips) is int and 0 <= regrips <= 1)
            pauses = pump.get('native_pause_regrips', 0)
            result['native_pause_regrips'] = pauses
            checks['bounded_native_pause_regrip'] = (type(pauses) is int and 0 <= pauses <= 1)
            if pauses:
                recovered = [row for row in pump['cycles'] if row.get('recovery') is True]
                checks['native_pause_recovery'] = (not interrupted_pump and len(recovered) == pauses
                    and cycle.get('held_suspensions', 0) > 0 and cycle.get('recovery_enabled') == 1
                    and cycle.get('guarded_updates', 0) > 0 and cycle.get('recovery_releases') == pauses
                    and all(row.get('recovery_grant', 0) > 0 and row.get('guard_epoch', 0) > 0
                            and row.get('grant_neutral_sequence', 0) >= pump['neutral_sequence'] > 0
                            and row.get('support_returned') is True for row in recovered))
            if regrips == 1:
                checks['fresh_support_after_pre_shot_loss'] = (
                    0 < pump.get('pre_shot_loss_sequence', 0) < pump['initial_support_source']
                    and 0 < pump.get('pre_shot_loss_ns', 0) < pump.get('initial_support_ns', 0))
            checks.update(
                physical_pump=pump['phase'] == 10 and pump['failure'] == 0 and pump['completed'] is True and pump['completed_cycles'] == pump['requested_cycles'] == expected_pumps,
                native_pump=cycle['failure'] == 0 and cycle['releases'] + cycle.get('recovery_releases', 0) == cycle['acknowledgements'] == expected_pumps + int(revisit_enabled) and cycle['blocks_fire'] is False,
                immediate_support=support['returned'] is True and support['token'] > 0 and support['claim'] > 0,
                rifle_shot=probe['rifle_fire_ns'] > 0 and probe['rifle_shot_sequence'] > 0 and probe['rifle_shot_observed_ns'] >= probe['rifle_fire_ns'])
            if revisit_enabled:
                second = revisit['physical_pump']
                checks['post_holster_pump'] = (not empty_pump and not interrupted_pump
                    and revisit['completed'] is True and second['completed'] is True
                    and second['phase'] == 10 and second['failure'] == 0
                    and second['completed_cycles'] == second['requested_cycles'] == 1
                    and len(second['cycles']) == 1
                    and second['cycles'][0]['loaded_after_shot'] == revisit['loaded_before'] - 1
                    and second['cycles'][0]['reserve'] == revisit['reserve_before']
                    and second['cycles'][0]['support_returned'] is True
                    and second['initial_support_claim'] > 0 and second['initial_gun_claim'] > 0
                    and second['support_adopted_without_release'] is True)
            if empty_pump:
                cycles = pump['cycles']
                checks['eight_conserved_shots'] = (len(cycles) == 8 and probe['shell_loaded_before'] == 8
                    and [row['loaded_after_shot'] for row in cycles] == list(range(7, -1, -1))
                    and all(row['reserve'] == probe['shell_reserve_before'] and row['support_returned'] is True for row in cycles))
                checks['empty_tube_manual_shell'] = (shell['loaded_before'] == 0
                    and len(shell['rounds']) == 1 and shell['rounds'][0]['loaded'] == 1
                    and shell['rounds'][0]['reserve'] == probe['shell_reserve_before'] - 1)
            if three_pump_two_shell:
                cycles, rounds = pump['cycles'], shell['rounds']
                checks['three_conserved_shots'] = (not revisit_enabled and pump['scenario'] == (6 if pump_receiver_gap else 5)
                    and probe['shell_loaded_before'] == 8 and probe['shell_reserve_before'] == 24
                    and len(cycles) == 3 and [row['loaded_after_shot'] for row in cycles] == [7, 6, 5]
                    and all(row['reserve'] == 24 and row['support_returned'] is True for row in cycles))
                checks['two_distinct_shell_gestures'] = (shell['loaded_before'] == 5 and shell['reserve_before'] == 24
                    and len(rounds) == 2 and [row['number'] for row in rounds] == [1, 2]
                    and [row['loaded'] for row in rounds] == [6, 7] and [row['reserve'] for row in rounds] == [23, 22]
                    and 0 < rounds[0]['completed_ns'] < rounds[1]['completed_ns']
                    and 0 < rounds[0]['grab_input'] < rounds[1]['first_neutral_input']
                    <= rounds[1]['last_neutral_input'] < rounds[1]['grab_input']
                    and 0 < rounds[0]['acquired'] < rounds[1]['acquired'])
                physical = game['physical_reload']; transactions = physical['transactions']
                checks['two_exact_shell_transactions'] = (physical['transaction_dropped'] == physical['dropped'] == 0
                    and len(transactions) == 2 and len({row['request'] for row in transactions}) == 2
                    and len({(row['item_id'], row['item_generation']) for row in transactions}) == 2
                    and all(type(row['item_id']) is int and row['item_id'] > 0
                        and type(row['item_generation']) is int and row['item_generation'] > 0 for row in transactions)
                    and all(row['resolved'] is True and row['units'] == 1 and row['request'] > 0
                        and 0 < row['submitted_ns'] < row['resolved_ns']
                        and row['before']['verified'] is True and row['after']['verified'] is True
                        and row['before']['all_three_held'] is True and row['after']['all_three_held'] is True
                        and row['before']['identity'] == row['after']['identity'] == row['ack']['identity']
                        == transactions[0]['before']['identity']
                        and row['before']['loaded'] == 5 + index and row['after']['loaded'] == 6 + index
                        and row['before']['reserve'] == 24 - index and row['after']['reserve'] == 23 - index
                        and row['ack']['verified'] is True and row['ack']['operation'] == 3 and row['ack']['status'] == 1
                        and row['ack']['server_invocation'] > 0 for index, row in enumerate(transactions)))
                stop = flow['native_abort_cleanup']; history = stop['cycle_history']
                checks['two_shell_native_stop'] = (stop['context_writes'] is False and stop['ammo_writes'] is False
                    and stop['active'] is False and len(history) == 1
                    and history[0]['claimed_mask'] == history[0]['completed_mask'] == 7
                    and history[0]['failure'] == 0 and history[0]['initial_loaded'] == 7
                    and history[0]['initial_reserve'] == 22 and history[0]['terminal_ns'] < history[0]['deadline_ns'])
                expired = flow['expired_control_probe']
                if expired_control:
                    checks['expired_control_exercised'] = (expired['enabled'] is True and expired['drained'] is True
                        and expired['exercised'] is True and expired['passed'] is True and expired['state'] == 'passed'
                        and expired['submissions'] == 1 and expired['replay_accepted'] is False
                        and expired['fresh_accepted'] is True and expired['debt_preserved'] is True
                        and expired['input_preserved'] is True)
                if pump_unreachable_hand:
                    # The bit follows the immutable mechanical-only pose into
                    # both real pack callbacks; generic pump pairs are insufficient.
                    rig = game['rig_publication']; presentation = rig['pump_presentation']
                    pairs, copies = presentation['unreachable_hand_pairs'], presentation['unreachable_hand_copies']
                    checks['fresh_unreachable_hand_packed'] = (
                        type(pairs) is int and pairs > 0 and type(copies) is int and copies >= 2*pairs
                        and presentation['mechanical_without_hand'] >= presentation['reach_rejects'] > 0
                        and presentation['copies'] >= copies and presentation['pairs'] >= pairs
                        and presentation['pose_rejects'] == presentation['source_rejects'] == 0
                        and rig['packing_failures'] == rig['source_changes'] == 0)
                    checks['no_expired_control_in_reach_trial'] = expired['enabled'] is False
                if pump_extended_hand:
                    # Same raised-gun input fixture; this mode requires actual
                    # extended-hand output and paired measurements, not393's
                    # mechanically correct but detached-hand fallback.
                    rig = game['rig_publication']; presentation = rig['pump_presentation']
                    pairs, copies = presentation['extended_hand_pairs'], presentation['extended_hand_copies']
                    samples = presentation['extended_hand_error_samples']
                    error, scale = presentation['extended_hand_max_error_m'], presentation['extended_hand_max_scale']
                    checks['fresh_extended_hand_packed'] = (
                        type(pairs) is int and pairs > 0 and type(copies) is int and copies >= 2*pairs
                        and presentation['copies'] >= copies and presentation['pairs'] >= pairs
                        and presentation['attempts'] == presentation['poses'] >= pairs
                        and presentation['authored_hand_poses'] + presentation['generic_hand_poses'] >= pairs
                        and presentation['mechanical_without_hand'] == presentation['reach_rejects'] == 0
                        and presentation['pose_rejects'] == presentation['source_rejects'] == presentation['fallbacks'] == 0
                        and rig['packing_failures'] == rig['source_changes'] == 0)
                    checks['paired_extended_hand_error_bounded'] = (
                        type(samples) is int and samples == pairs
                        and type(error) in (int, float) and math.isfinite(error) and 0 <= error <= .001
                        and type(scale) in (int, float) and math.isfinite(scale) and scale > 1)
                    checks['no_expired_control_in_reach_trial'] = expired['enabled'] is False
        if pump_receiver_gap:
            gap = receiver_gap or {}
            pump, cycle = probe['physical_pump'], flow['native_cycle_candidate']
            rows = pump['cycles']; origin = probe['origin_observation']
            checks['whole_receiver_gap'] = (
                gap.get('completed') is True and gap.get('failed') is False
                and gap.get('input_and_tracking_withheld') is True and gap.get('native_authority') is False
                and 0 < gap['original_deadline_ns'] <= gap['last_pregap_deadline_upper_ns']
                and gap['last_pregap_publish_after_ns'] <= gap['begin_ns'] < gap['return_ns'] <= gap['first_return_ns']
                and gap['last_pregap_deadline_upper_ns'] == gap['last_pregap_publish_after_ns'] + 100_000_000
                and gap['return_ns'] - gap['last_pregap_deadline_upper_ns'] >= 5_000_000_000
                and 0 < gap['input'] <= gap['last_pregap_input'] < gap['first_return_input']
                and 0 < gap['old_space'] < gap['new_space'] == gap['old_space'] + 1
                and .03 <= gap['travel_m'] <= .055 and (gap['loaded'], gap['reserve'], gap['capacity']) == (7,24,8))
            checks['debt_observed_through_gap'] = (
                pump['scenario'] == 6 and pump['receiver_gap_scenario'] is True
                and pump['receiver_missing_samples'] >= pump['receiver_converging_samples'] > 0
                and gap['begin_ns'] <= pump['receiver_converging_first_ns'] < pump['receiver_converging_last_ns'] < gap['first_return_ns']
                and pump['receiver_converging_last_ns'] - pump['receiver_converging_first_ns'] >= 4_800_000_000
                and pump['interruption_observed'] is True and pump['followed_tracking_rebase'] is True
                and pump['partial_gun_claim'] > 0 and pump['recovery_gun_claim'] > 0
                and pump['partial_gun_claim'] != pump['recovery_gun_claim']
                and pump['partial_pairs'] > 0 and .03 <= pump['partial_travel_m'] <= .055)
            checks['new_space_retains_exact_debt'] = (len(rows) == 3
                and origin['started'] is True and origin['verified'] is True and origin['full_stroke_verified'] is True and origin['native_authority'] is False
                and origin['old_space'] == gap['old_space'] and origin['new_space'] == gap['new_space']
                and origin['cycle'] == gap['cycle'] == rows[0]['cycle']
                and origin['shot'] == gap['shot'] == rows[0]['shot']
                and (origin['loaded'],origin['reserve']) == (7,24)
                and origin['verified_ns'] >= gap['first_return_ns'])
            boundary = pump.get('receiver_return_neutral', {})
            checks['fresh_return_neutral_boundary'] = (bool(boundary)
                and gap['first_return_input'] <= boundary['sequence'] == pump['neutral_sequence']
                and boundary['space'] == gap['new_space']
                and gap['return_ns'] <= boundary['observed_ns'] <= boundary['accepted_ns'] == pump['neutral_ns'] < boundary['deadline_ns']
                and 0 < boundary['deadline_ns'] - boundary['observed_ns'] <= 100_000_000)
            checks['fresh_full_stroke_before_next_shot'] = (len(rows) == 3
                and cycle['recovery_enabled'] == 1 and cycle['guarded_updates'] > 0
                and cycle['recovery_releases'] == 1 and cycle['releases'] == 2
                and rows[0]['recovery'] is True and all(row['recovery'] is False for row in rows[1:])
                and rows[0]['recovery_grant'] > 0 and rows[0]['guard_epoch'] > 0
                and gap['first_return_input'] <= pump['neutral_sequence'] <= rows[0]['grant_neutral_sequence'] < rows[0]['source_sequence']
                and bool(boundary) and rows[0]['grant_neutral_observed_ns'] >= boundary['observed_ns']
                and 0 < rows[0]['grant_neutral_deadline_ns'] - rows[0]['grant_neutral_observed_ns'] <= 100_000_000
                and gap['first_return_ns'] <= pump['neutral_ns'] < rows[0]['requested_ns'] < rows[0]['ready_ns']
                and rows[0]['rear_pair'] is True and rows[0]['verified_pairs'] > 0
                and rows[0]['ready_ns'] <= rows[0]['support_observed_ns'] < rows[1]['requested_ns']
                and all(b['cycle'] == a['cycle']+1 and b['shot'] == a['shot']+1 for a,b in zip(rows,rows[1:])))
            inputs=probe['effective_input']
            checks['no_scripted_input_during_absence'] = (len(rows) == 3 and probe['input_rows_dropped'] == 0
                and not any(gap['last_pregap_deadline_upper_ns'] <= row['now_ns'] < gap['first_return_ns'] for row in inputs)
                and all(row['trigger'] == 0 for row in inputs if gap['begin_ns'] <= row['now_ns'] <= rows[0]['ready_ns'])
                and any(row['left_squeeze'] == row['right_squeeze'] == row['trigger'] == 0
                        for row in inputs if gap['first_return_ns'] <= row['now_ns'] <= pump['neutral_ns']))
            checks['no_other_gap_fixture'] = flow['expired_control_probe']['enabled'] is False
        if shell_focus_loss:
            focus = probe['shell_focus_loss']; receiver = shell_focus_receipt or {}; witness = receiver['witness']
            target = focus['target']; stop = flow['native_abort_cleanup']['cycle_history']
            checks['no_post_shell_support_or_rifle_episode'] = (probe['shell_support']['returned'] is False
                and probe['shell_support']['token'] == probe['shell_support']['claim'] == 0
                and probe['rifle_fire_ns'] == probe['rifle_shot_sequence'] == probe['rifle_shot_observed_ns'] == 0
                and probe.get('rifle_support_required', False) is False)
            # Explicit shorter episode. Its untouched child states are evidence
            # that no post-loss support claim, rifle return or shot was forged.
            checks['original_return_unexercised'] = checks.pop('return_after_holster')
            checks['inventory_stopped_at_other_held'] = checks.pop('inventory_completed')
            checks.pop('immediate_support'); checks.pop('rifle_shot')
            checks['focused_loss_exercised'] = (focus['enabled'] is True and focus['completed'] is True
                and focus['failed'] is False and focus['native_authority'] is False
                and receiver['completed'] is True and receiver['failed'] is False
                and receiver['tracking_withheld'] is False and receiver['native_authority'] is False
                and receiver['lost_samples'] >= 2 and receiver['first_lost_ns'] - receiver['begin_ns'] >= 0
                and receiver['return_ns'] - receiver['first_lost_ns'] >= 500_000_000
                and witness['space'] == target['space'] and witness['cycle'] == target['cycle']
                and (witness['loaded'], witness['reserve'], witness['capacity']) == (7, 22, 8)
                and (target['loaded'], target['reserve'], target['capacity']) == (7, 22, 8)
                and witness['completed_observed_ns'] == target['completed_observed_ns']
                and game['physical_reload']['transactions'][-1]['resolved_ns'] <= target['completed_observed_ns']
                <= witness['observed_ns'] <= receiver['begin_ns'] < witness['deadline_ns']
                and 0 < witness['deadline_ns'] - witness['observed_ns'] <= 100_000_000
                and witness['input'] < receiver['first_lost_input'] <= focus['first_lost_input']
                <= focus['last_lost_input'] < focus['return_input']
                and receiver['return_input'] <= focus['return_input']
                and target['completed_observed_ns'] < focus['first_lost_observed_ns']
                <= focus['first_lost_ns'] < focus['first_lost_deadline_ns']
                and focus['first_lost_deadline_ns'] - focus['first_lost_observed_ns'] <= 100_000_000
                and focus['last_lost_observed_ns'] < focus['return_observed_ns']
                <= focus['return_ns'] < focus['return_deadline_ns']
                and focus['return_deadline_ns'] - focus['return_observed_ns'] <= 100_000_000
                and focus['first_lost_observed_ns'] - target['completed_observed_ns'] <= 200_000_000)
            checks['focus_stop_keeps_original_bound'] = (len(stop) == 1 and stop[0]['cycle'] == target['cycle']
                and focus['first_lost_ns'] <= stop[0]['armed_ns'] < stop[0]['terminal_ns']
                < stop[0]['deadline_ns'] <= focus['first_lost_deadline_ns']
                and stop[0]['deadline_ns'] - stop[0]['armed_ns'] <= 200_000_000
                and stop[0]['terminal_ns'] < focus['return_observed_ns'])
            checks['neutral_return_three_seconds'] = (focus['return_ns'] <= focus['stable_start_ns']
                and focus['stable_last_ns'] - focus['stable_start_ns'] >= 3_000_000_000
                and focus['stable_samples'] >= 30)
            checks['focus_has_no_other_input_probe'] = (flow['expired_control_probe']['enabled'] is False
                and probe.get('pump_unreachable_hand_probe', False) is False and probe['physical_pump']['scenario'] == 5)
        if probe.get('rifle_support_required'):
            checks['rifle_support'] = (probe['rifle_support_claim'] > 0
                and probe['rifle_support_source'] > 0 and probe['rifle_support_released'] is True)
        if interrupted_pump:
            pump, cycle = probe['physical_pump'], flow['native_cycle_candidate']
            completed = pump['cycles']
            checks.update(
                interruption_mode=manual_pump is True and pump['scenario'] in (3, 4),
                held_support_at_shot=pump['initial_support_claim'] > 0 and pump['initial_gun_claim'] > 0
                    and pump['initial_support_source'] > 0 and pump['support_adopted_without_release'] is True,
                actual_partial_stroke=0.03 <= pump['partial_travel_m'] <= 0.055
                    and pump['partial_pairs'] > 0 and pump['partial_source_sequence'] > 0,
                interrupted_tracking=pump['interruption_observed'] is True
                    and pump['lost_started_ns'] > 0
                    and pump['lost_ended_ns'] - pump['lost_started_ns'] >= 500_000_000
                    and pump['lost_last_sequence'] >= pump['lost_first_sequence'] > 0
                    and pump['neutral_sequence'] > pump['lost_last_sequence'],
                native_recovery=cycle['recovery_enabled'] == 1 and cycle['guarded_updates'] > 0
                    and cycle['recovery_releases'] == 1 and cycle['releases'] == 0,
                recovered_outcome=len(completed) == 1 and completed[0]['recovery'] is True
                    and completed[0]['recovery_grant'] > 0 and completed[0]['guard_epoch'] > 0
                    and completed[0]['grant_neutral_sequence'] >= pump['neutral_sequence']
                    and completed[0]['support_returned'] is True)
            if pump['scenario'] == 4:
                checks['natural_aim_handoff'] = 0.15 <= pump['support_correction_rad'] <= 1.0
                checks['natural_rifle_support'] = probe.get('rifle_support_required') is True and checks.get('rifle_support') is True
        mechanical_completed = all(checks.values())
        presentation_verified = all(
            magazine.get('presentation_continuity_verified') is True
            and type(magazine.get('presentation_fallbacks_during_interaction')) is int
            and magazine['presentation_fallbacks_during_interaction'] == 0
            and magazine.get('first_presentation_fallback_ns') == 0
            for magazine in ((first,) if shell_focus_loss else (returned,) if empty_pump else (first, returned)))
        checks['magazine_presentation_continuity'] = presentation_verified
        result.update(checks=checks, runtime_completed=all(checks.values()),
                      mechanical_runtime_completed=mechanical_completed,
                      presentation_continuity_verified=presentation_verified,
                      phase=probe['phase'], failure=probe['failure'])
        if manual_pump:
            result['native_pause_recovery_status'] = ('not_needed' if pauses == 0 else
                'completed_with_release_and_regrip' if result['runtime_completed'] else 'failed')
            result['pre_shot_recovery_status'] = ('not_needed' if regrips == 0 else
                'completed' if result['runtime_completed'] else 'failed')
    except (KeyError, IndexError, TypeError, ValueError) as error:
        result.update(checks={'well_formed_status': False}, error=str(error))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trace', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--manual-pump', action='store_true')
    ap.add_argument('--campaign-magazine', action='store_true')
    ap.add_argument('--interrupted-pump', action='store_true')
    ap.add_argument('--empty-pump', action='store_true')
    ap.add_argument('--three-pump-two-shell', action='store_true')
    ap.add_argument('--expired-control', action='store_true')
    ap.add_argument('--pump-unreachable-hand', action='store_true')
    ap.add_argument('--pump-extended-hand', action='store_true')
    ap.add_argument('--pump-receiver-gap', action='store_true')
    ap.add_argument('--receiver-gap', type=Path)
    ap.add_argument('--shell-focus-loss', action='store_true')
    ap.add_argument('--shell-focus-receipt', type=Path)
    args = ap.parse_args()
    if args.campaign_magazine and any((args.manual_pump,args.interrupted_pump,args.empty_pump,
            args.three_pump_two_shell,args.expired_control,args.pump_unreachable_hand,
            args.pump_extended_hand,args.pump_receiver_gap,args.shell_focus_loss)):
        ap.error('Single-weapon campaign episode cannot combine other fixtures')
    if args.output.exists() or args.output.resolve() == args.trace.resolve():
        ap.error('Preserve existing evidence; select a new output')
    if args.pump_receiver_gap != bool(args.receiver_gap):
        ap.error('--pump-receiver-gap requires exactly one --receiver-gap receipt')
    if args.shell_focus_loss != bool(args.shell_focus_receipt):
        ap.error('--shell-focus-loss requires exactly one --shell-focus-receipt')
    focus_raw = args.shell_focus_receipt.read_bytes() if args.shell_focus_receipt else None
    gap_raw = args.receiver_gap.read_bytes() if args.receiver_gap else None
    raw = args.trace.read_bytes()
    result = check_campaign_magazine(json.loads(raw.decode('utf-8-sig'))) if args.campaign_magazine else check(json.loads(raw.decode('utf-8-sig')), args.manual_pump, args.interrupted_pump, args.empty_pump, args.three_pump_two_shell,
                   args.expired_control, args.pump_unreachable_hand, args.pump_extended_hand, args.pump_receiver_gap,
                   json.loads(gap_raw.decode('utf-8-sig')) if gap_raw else None, args.shell_focus_loss,
                   json.loads(focus_raw.decode('utf-8-sig')) if focus_raw else None)
    result['trace_sha256'] = hashlib.sha256(raw).hexdigest()
    if gap_raw: result['receiver_gap_sha256'] = hashlib.sha256(gap_raw).hexdigest()
    if focus_raw: result['shell_focus_receipt_sha256'] = hashlib.sha256(focus_raw).hexdigest()
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['runtime_completed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
