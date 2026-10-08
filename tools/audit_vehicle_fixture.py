"""Audit the bounded PBLB driver control/camera fixture without process access."""
import argparse
import hashlib
import json
from pathlib import Path
from audit_vehicle_camera import audit as audit_camera, camera_rows


def audit(trace, receiver):
    game = trace['gameplay']
    errors = []

    def check(condition, message):
        if not condition:
            errors.append(message)

    check(receiver.get('vehicle_fixture') is True and receiver.get('consumed_pairs') == 240,
          'The complete vehicle fixture was not delivered')
    check(receiver.get('async_timeouts') == 0 and receiver.get('gpu_busy') == 0,
          'Stereo delivery reported timeouts or GPU contention')
    for name in ('vehicle_route_rejects', 'vehicle_unsupported_samples',
                 'vehicle_cache_readback_failures', 'vehicle_interact_samples', 'mismatches'):
        check(game.get(name) == 0, name + ' is nonzero or missing')
    check(game.get('boat_camera_views') == 240, 'Expected 240 seat camera plans')
    check(game.get('boat_driver_commits', 0) > 0, 'No native driver commits')
    check(game.get('vehicle_throttle_nonzero_commits', 0) > 0 and
          game.get('vehicle_steer_nonzero_commits', 0) > 0, 'Missing native throttle/steer pulse')
    check(game.get('vehicle_last_throttle') == game.get('vehicle_last_steer') == 0,
          'Fixture did not finish neutral')
    rows = game.get('vehicle_gather_evidence', [])
    check(5 <= len(rows) <= 16, 'Missing bounded native gather evidence')
    identity = None
    phases = []
    previous_sequence = 0
    for row in rows:
        owner = tuple(row[k] for k in ('entity', 'entry', 'soldier', 'weak',
                                       'actor_generation', 'seat_generation', 'space'))
        if identity is None:
            identity = owner
        check(owner == identity and all(owner), 'Gather ownership changed')
        check(row['entity'] == game['vehicle_entity'] and row['entry'] == game['vehicle_entry'],
              'Gather and published seat differ')
        check(row['axis_mask'] == 3 and row['permissions'] & 0x11 == 0x11,
              'Gather did not authorize both native driver axes')
        check(row['readback_verified'] is True and row['written'] == row['observed'],
              'Native cache did not retain the exact committed values')
        check(row['input_sequence'] > previous_sequence, 'Gather input sequence is not increasing')
        previous_sequence = row['input_sequence']
        values = row['observed']
        phase = None
        for name, expected in [('neutral', (0, 0)), ('throttle', ((.25 - .18) / .82, 0)),
                               ('steer', (0, (.40 - .18) / .82))]:
            if all(abs(a - b) < 1e-6 for a, b in zip(values, expected)):
                phase = name
        check(phase is not None, 'Unexpected driver command')
        if not phases or phases[-1] != phase:
            phases.append(phase)
    check(phases == ['neutral', 'throttle', 'neutral', 'steer', 'neutral'],
          'Missing ordered pulse and neutral-release sequence')
    try:
        camera = audit_camera(camera_rows(trace))
    except (AssertionError, KeyError, ValueError, TypeError) as error:
        camera = {'passed': False, 'error': str(error)}
        errors.append('Camera: ' + str(error))
    return dict(schema='fvr.bc2.vehicle_fixture.v1', passed=not errors, errors=errors,
                phases=phases, native_gather_records=len(rows), camera=camera,
                consumed_pairs=receiver.get('consumed_pairs'),
                max_pair_latency_us=receiver.get('async_max_latency_us'),
                pairs_after_50ms=receiver.get('async_completed_after_50ms'),
                native_driver_input_verified=not errors,
                driving_physics_verified=False, headset_tested=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('receiver', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = audit(json.loads(args.trace.read_text()), json.loads(args.receiver.read_text()))
    except (KeyError, ValueError, TypeError, IndexError) as error:
        result = {'passed': False, 'errors': ['Malformed capture: ' + str(error)]}
    result['sources'] = [dict(path=str(p.resolve()), sha256=hashlib.sha256(p.read_bytes()).hexdigest())
                         for p in (args.trace, args.receiver)]
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({k: result[k] for k in ('passed', 'errors')}))
    raise SystemExit(0 if result['passed'] else 1)
