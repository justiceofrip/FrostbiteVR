"""Terminal runtime status for the combined input-only trial, not a trace audit."""
import argparse
import hashlib
import json
from pathlib import Path


def check(trace, manual_pump, interrupted_pump=False, empty_pump=False):
    result = dict(schema='fvr.bc2.resource_inventory_status.v2', runtime_completed=False,
                  mechanical_runtime_completed=False, presentation_continuity_verified=False,
                  callback_evidence_verified=False, headset_verified=False,
                  interrupted_pump=interrupted_pump)
    try:
        game = trace['gameplay']; flow = game['reload_flow']; probe = game['resource_inventory_probe']
        first, returned, inventory, shell = (probe[k] for k in ('first_magazine', 'full_return', 'inventory', 'shell'))
        native = flow['ammo_resource_hands']
        checks = dict(
            compatible_sequence=not empty_pump or (manual_pump and not interrupted_pump),
            ordinary_input=probe['input_only'] is True and probe['persistent_consumers'] is True,
            driver_completed=probe['phase'] == 11 and probe['failure'] == 0 and probe['completed'] is True,
            requested_mode=probe['manual_pump'] is manual_pump,
            first_magazine=(first['actual_consumer_completed'] is False and first['phase'] == 0 and first['failure'] == 0 and first['completed_cycles'] == 0) if empty_pump else
                (first['actual_consumer_completed'] is True and first['phase'] == 12 and first['failure'] == 0 and first['completed_cycles'] == 2),
            return_after_holster=returned['actual_consumer_completed'] is True and returned['phase'] == 12 and returned['failure'] == 0 and returned['completed_cycles'] == 1,
            inventory_completed=inventory['phase'] == 17 and inventory['failure'] == 0 and inventory['completed'] is True,
            shell_completed=shell['actual_consumer_completed'] is True and shell['phase'] == 7 and shell['failure'] == 0 and shell['submitted'] == shell['completed'] == shell['requested_rounds'] == 1,
            native_magazine_operations=native['ordinary_input_build'] is True and native['calls'] == native['completed'] == (2 if empty_pump else 6) and native['rejected'] == native['pending'] == 0,
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
                physical_pump=pump['phase'] == 10 and pump['failure'] == 0 and pump['completed'] is True and pump['completed_cycles'] == pump['requested_cycles'] == (8 if empty_pump else 1),
                native_pump=cycle['failure'] == 0 and cycle['releases'] + cycle.get('recovery_releases', 0) == cycle['acknowledgements'] == (8 if empty_pump else 1) + int(revisit_enabled) and cycle['blocks_fire'] is False,
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
            for magazine in ((returned,) if empty_pump else (first, returned)))
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
    except (KeyError, TypeError, ValueError) as error:
        result.update(checks={'well_formed_status': False}, error=str(error))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trace', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--manual-pump', action='store_true')
    ap.add_argument('--interrupted-pump', action='store_true')
    ap.add_argument('--empty-pump', action='store_true')
    args = ap.parse_args()
    if args.output.exists() or args.output.resolve() == args.trace.resolve():
        ap.error('Preserve existing evidence; select a new output')
    raw = args.trace.read_bytes()
    result = check(json.loads(raw.decode('utf-8-sig')), args.manual_pump, args.interrupted_pump, args.empty_pump)
    result['trace_sha256'] = hashlib.sha256(raw).hexdigest()
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['runtime_completed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
