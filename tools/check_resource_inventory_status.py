"""Terminal runtime status for the combined input-only trial, not a trace audit."""
import argparse
import hashlib
import json
from pathlib import Path


def check(trace, manual_pump):
    result = dict(schema='fvr.bc2.resource_inventory_status.v1', runtime_completed=False,
                  callback_evidence_verified=False, headset_verified=False)
    try:
        game = trace['gameplay']; flow = game['reload_flow']; probe = game['resource_inventory_probe']
        first, returned, inventory, shell = (probe[k] for k in ('first_magazine', 'full_return', 'inventory', 'shell'))
        native = flow['ammo_resource_hands']
        checks = dict(
            ordinary_input=probe['input_only'] is True and probe['persistent_consumers'] is True,
            driver_completed=probe['phase'] == 11 and probe['failure'] == 0 and probe['completed'] is True,
            requested_mode=probe['manual_pump'] is manual_pump,
            first_magazine=first['actual_consumer_completed'] is True and first['phase'] == 12 and first['failure'] == 0 and first['completed_cycles'] == 2,
            return_after_holster=returned['actual_consumer_completed'] is True and returned['phase'] == 12 and returned['failure'] == 0 and returned['completed_cycles'] == 1,
            inventory_completed=inventory['phase'] == 17 and inventory['failure'] == 0 and inventory['completed'] is True,
            shell_completed=shell['actual_consumer_completed'] is True and shell['phase'] == 7 and shell['failure'] == 0 and shell['submitted'] == shell['completed'] == shell['requested_rounds'] == 1,
            native_magazine_operations=native['ordinary_input_build'] is True and native['calls'] == native['completed'] == 6 and native['rejected'] == native['pending'] == 0,
            detached=trace['hooks_disabled'] is True and flow['drained'] is True and flow['in_flight'] == 0)
        if manual_pump:
            pump, cycle = probe['physical_pump'], flow['native_cycle_candidate']
            support = probe['shell_support']
            checks.update(
                physical_pump=pump['phase'] == 10 and pump['failure'] == 0 and pump['completed'] is True and pump['completed_cycles'] == pump['requested_cycles'] == 1,
                native_pump=cycle['failure'] == 0 and cycle['releases'] == cycle['acknowledgements'] == 1 and cycle['blocks_fire'] is False,
                immediate_support=support['returned'] is True and support['token'] > 0 and support['claim'] > 0,
                rifle_shot=probe['rifle_fire_ns'] > 0 and probe['rifle_shot_sequence'] > 0 and probe['rifle_shot_observed_ns'] >= probe['rifle_fire_ns'])
        result.update(checks=checks, runtime_completed=all(checks.values()), phase=probe['phase'], failure=probe['failure'])
    except (KeyError, TypeError, ValueError) as error:
        result.update(checks={'well_formed_status': False}, error=str(error))
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trace', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--manual-pump', action='store_true')
    args = ap.parse_args()
    if args.output.exists() or args.output.resolve() == args.trace.resolve():
        ap.error('Preserve existing evidence; select a new output')
    raw = args.trace.read_bytes()
    result = check(json.loads(raw.decode('utf-8-sig')), args.manual_pump)
    result['trace_sha256'] = hashlib.sha256(raw).hexdigest()
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))
    return 0 if result['runtime_completed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
