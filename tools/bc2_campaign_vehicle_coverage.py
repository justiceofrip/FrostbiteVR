"""Bounded offline campaign-seat coverage audit; never opens a process.

The catalog is authored evidence, not a whitelist for native adapters. Existing
three-snapshot native captures can establish read-only joins, not gameplay or
an exact vehicle blueprint. No observation is promoted to campaign acceptance.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re

MAX_JSON = 8 * 1024 * 1024
SCHEMA = 'fvr.bc2.campaign-vehicle-catalog.v1'
CAPTURE_SCHEMAS = ('fvr.private.campaign_seat_readonly396.v1',
                   'fvr.private.attached_seat_readonly398.v1')
REQUIRED = ('exact_native_identity', 'stereo_camera', 'mission_permissions',
            'controls', 'tracking_recovery', 'menu_return', 'exit_or_scripted_handoff',
            'death_checkpoint_return', 'infantry_return')


def need(okay, message):
    if not okay:
        raise ValueError(message)


def load(path):
    def pairs(items):
        out = {}
        for key, value in items:
            need(key not in out, 'duplicate JSON key: ' + key)
            out[key] = value
        return out
    raw = Path(path).read_bytes()
    need(len(raw) <= MAX_JSON, 'JSON exceeds 8 MiB')
    return json.loads(raw.decode('utf-8-sig'), object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')))


def relative(value):
    need(isinstance(value, str) and value and '\\' not in value and ':' not in value
         and not value.startswith('/') and all(p not in ('', '.', '..') for p in value.split('/')),
         'canonical relative path required')
    return value


def resource_key(value):
    return relative(value).casefold()


def sha(value):
    need(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'SHA256 required')
    return value


def validate_catalog(catalog):
    need(catalog.get('schema') == SCHEMA and catalog.get('complete_campaign_inventory') is False,
         'authored partial-inventory schema required')
    need(catalog.get('native_admission_granted') is False, 'catalog cannot grant native admission')
    sources = catalog['sources']
    need(1 <= len(sources) <= 16 and len({s['path'] for s in sources}) == len(sources), 'source set')
    for s in sources:
        relative(s['path']); sha(s['sha256'])
    ids, routes = set(), set()
    need(1 <= len(catalog['cases']) <= 64, 'case count')
    for row in catalog['cases']:
        need(re.fullmatch('[a-z0-9_-]{1,80}', row['id']) and row['id'] not in ids, 'duplicate/invalid case')
        ids.add(row['id']); resource_key(row['level']); resource_key(row['asset'])
        need(type(row['authored_forced_seat']) is int and 0 <= row['authored_forced_seat'] < 64, 'authored seat')
        need(row['runtime_slot_verified'] is False, 'authored ordering is not a native slot proof')
        need(row['adapter'] in ('generic_native_seat', 'pbl_exact', 'attached_soldier_pending', 'uav_pending', 'unclassified'), 'adapter')
        need(row['campaign_accepted'] is False and row['headset_accepted'] is False, 'catalog contains no campaign acceptance')
        need(set(REQUIRED).issubset(row['required_checks']) and len(set(row['required_checks'])) == len(row['required_checks']), 'required checks lost or duplicated')
        need(row['restrictions'] and row['missing_native_evidence'], 'explicit restrictions and gaps required')
        for trigger in row['human_trigger_guids']:
            need(re.fullmatch('[0-9a-f-]{36}', trigger), 'trigger GUID')
            key = (resource_key(row['level']), trigger)
            need(key not in routes, 'authored route assigned twice'); routes.add(key)
    need(len(routes) == catalog['human_force_entry_count'], 'authored route count differs')
    return catalog


def verify_authored(catalog, folder):
    """Reconcile every retained human edge with the catalog; no name matching."""
    validate_catalog(catalog)
    for pin in catalog['sources']:
        path = Path(folder) / relative(pin['path'])
        need(path.is_file() and path.stat().st_size <= MAX_JSON, 'authored source bound: ' + str(path))
        need(hashlib.sha256(path.read_bytes()).hexdigest() == pin['sha256'], 'authored source changed: ' + str(path))
    raw = load(Path(folder) / 'campaign-human-evidence.json')['human_force_entry_rows']
    expected = {}
    for row in catalog['cases']:
        for guid in row['human_trigger_guids']:
            expected[(resource_key(row['level']), guid)] = row
    need(len(raw) == len(expected), 'human force-entry scan has missing/new rows')
    seen = set()
    for event in raw:
        key = (resource_key(event['level'].removesuffix('.dbx')), event['trigger_guid'])
        need(key in expected and key not in seen, 'unassigned/duplicate authored human route')
        seen.add(key); row = expected[key]
        need(any(f == ['/instance/PlayerType', 'string', 'SSHumanPlayer'] for f in event['player']['fields']), 'route does not target human')
        need(any(e['event'] == 'ForcePlayerEnterVehicle' for e in event['incoming']), 'forced entry event missing')
        need(int(event['seat_raw'], 16) == row['authored_forced_seat'], 'authored seat differs')
        assets = {resource_key(v['asset'].rsplit('/', 1)[0] + '.dbx') for v in event['vehicles']}
        need(assets == {resource_key(row['asset'])}, 'authored asset differs')
    return True


def audit_capture(capture, expected_slot=None):
    """Audit the existing bounded RPM helper, including its actual refusals."""
    need(capture.get('schema') in CAPTURE_SCHEMAS, 'unknown native capture schema')
    need(capture.get('read_only') is True and all(capture.get(k) == 0 for k in ('game_writes', 'native_calls', 'os_input')), 'not read-only evidence')
    need(capture.get('input_authority') is False and capture.get('vehicle_motion_tested') is False, 'capture cannot claim controls')
    need(capture.get('complete') is True and capture.get('snapshot_count') == 3
         and capture.get('coherent_snapshots') == 3 and capture.get('route_stable_across_snapshots') is True,
         'three coherent snapshots required')
    need(capture.get('input_verified') is True and capture.get('campaign_consumer_verified') is True, 'native consumer proof missing')
    need(capture.get('budget_exhausted') is False and capture.get('failed_reads') == 0, 'read failure/budget exhaustion')
    for field, limit in (('read_operations', 100000), ('read_bytes', 8 * 1024 * 1024), ('elapsed_ms', 10000)):
        need(type(capture.get(field)) is int and 0 <= capture[field] <= limit, 'capture bound: ' + field)
    need(type(capture.get('pid')) is int and capture['pid'] > 0 and int(capture['process_creation']) > 0, 'process identity')
    sha(capture['installed_sha256'])
    proofs = capture['native_input_proofs']
    need(len(proofs) == 13 and len({p['rva'] for p in proofs}) == 13, 'exact thirteen native input proofs required')
    need(all(p['equal'] is True and sha(p['disk_sha256']) == sha(p['live_sha256']) for p in proofs), 'native code mismatch')
    rows = capture['snapshots']; need(len(rows) == 3, 'snapshot count')
    keys, attached, admitted = [], [], []
    previous_end = previous_read = 0
    for index, row in enumerate(rows):
        need(row['index'] == index and row['error'] == '' and all(row[k] is True for k in ('owner_stable', 'route_stable', 'seat_details_identity_stable')), 'unstable snapshot')
        before, after = row['before'], row['after']
        need(before == after and before['status'] == 0, 'route changed')
        key = tuple(before[k] for k in ('actor', 'controlled', 'entry', 'router', 'cache', 'player', 'weak', 'slot', 'fingerprint'))
        need(all(type(v) is int and v > 0 for v in key[:7]) and type(key[7]) is int and key[7] >= 0 and key[8] > 0, 'native owner identity')
        need(expected_slot is None or before['slot'] == expected_slot, 'observed slot differs from requested scenario')
        need(0 <= before['permissions'] <= 0x7ff, 'permission mask')
        need(len(before['routes']) <= 50 and len({v[0] for v in before['routes']}) == len(before['routes']), 'route set')
        need(all(type(a) is int and type(b) is int and 0 <= a < 50 and 0 <= b <= 76 for a, b in before['routes']), 'route bounds')
        need(row['start_ms'] <= row['end_ms'] and row['end_ms'] - row['start_ms'] <= 2000, 'per-snapshot duration')
        need(row['read_begin'] <= row['read_end'] and row['read_end'] - row['read_begin'] <= 32768, 'per-snapshot reads')
        need(row['start_ms'] >= previous_end and row['read_begin'] >= previous_read
             and row['read_end'] <= capture['read_operations'], 'overlapping or out-of-order snapshots')
        previous_end, previous_read = row['end_ms'], row['read_end']
        keys.append(key)
        for field in ('seat_details_before', 'seat_details_after'):
            detail = row[field]
            need(detail['complete'] is True and detail['collection_valid'] is True
                 and detail['input_authority'] is False and detail['atomic'] is False, 'seat details incomplete')
            evidence = detail.get('attached_soldier', {})
            need(evidence.get('input_authority', False) is False and evidence.get('production_enabled', False) is False, 'read-only attachment cannot grant controls')
            attached.append(evidence.get('ownership_joined') is True)
        admitted.append(row['profile_revalidated'] is True and row['profile'] is not None)
    need(all(k == keys[0] for k in keys), 'native seat changed across snapshots')
    need(capture['admitted_snapshots'] == sum(admitted)
         and capture['profile_admitted_three_times'] is all(admitted), 'profile summary mismatch')
    return {'read_only_joins_verified': True, 'native_slot': keys[0][7],
            'generic_profile_admitted': all(admitted), 'attached_ownership_joined': all(attached),
            'exact_blueprint_verified': False, 'controls_tested': False,
            'campaign_accepted': False,
            'limitation': 'Helper has no exact loaded blueprint identity or control response; scenario label is not proof.'}


def report(catalog, authored_verified=False, captures=None):
    validate_catalog(catalog); captures = captures or {}
    need(set(captures).issubset(r['id'] for r in catalog['cases']), 'unknown case evidence')
    rows = []
    for case in catalog['cases']:
        r = dict(case)
        r['read_only_observation'] = audit_capture(captures[case['id']], case['authored_forced_seat']) if case['id'] in captures else None
        r['checks_remaining'] = list(case['required_checks'])
        rows.append(r)
    return {'schema': 'fvr.bc2.campaign-vehicle-coverage.v1', 'authored_sources_verified': authored_verified,
            'cases': rows, 'case_count': len(rows), 'human_force_entry_count': catalog['human_force_entry_count'],
            'complete_campaign_inventory': False, 'campaign_accepted_count': 0,
            'native_admission_granted': False, 'headset_tested': False}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog', type=Path, default=Path(__file__).resolve().parents[1] / 'data/bc2/campaign-vehicle-coverage.json')
    p.add_argument('--authored-root', type=Path)
    p.add_argument('--capture', action='append', default=[], metavar='CASE_ID=CAPTURE_JSON')
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    try:
        catalog = load(args.catalog); captures = {}; inputs = [args.catalog]
        for item in args.capture:
            key, path = item.split('=', 1); need(key not in captures, 'duplicate case capture')
            captures[key] = load(path); inputs.append(Path(path))
        result = report(catalog, verify_authored(catalog, args.authored_root) if args.authored_root else False, captures)
        result['inputs'] = [{'path': str(v), 'sha256': hashlib.sha256(v.read_bytes()).hexdigest()} for v in inputs]
        text = json.dumps(result, indent=2, allow_nan=False) + '\n'
        if args.output:
            need(not args.output.exists(), 'preserve previous report')
            with args.output.open('x', encoding='utf-8') as out:
                out.write(text)
        else:
            print(text, end='')
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        p.exit(2, 'Vehicle coverage refused: ' + str(error) + '\n')


if __name__ == '__main__':
    raise SystemExit(main())
