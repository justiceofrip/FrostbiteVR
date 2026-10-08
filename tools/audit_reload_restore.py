"""Audit bounded native reload traces offline; never attaches or changes ammo.

Classifies exact inspected restore callers, verifies copied source/output, and
finds ammo changes between top-level observed calls. A restore matching its source
is propagation evidence, never a native reload acknowledgement or authority proof.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

OWNER = ('player', 'soldier', 'weak', 'weapon', 'actor_generation', 'equip_generation', 'space')
IDENTITY = OWNER + ('firing', 'branch', 'wrapper_offset')
# Preferred-image VAs for the inspected build, not portable engine addresses.
RESTORE_ROUTES = {0x8BB438: ('decoded_snapshot_to_branch40', 1),
                  0x89216B: ('branch40_export_to_branch3c', 0),
                  0x7BF6C6: ('firing_backing_state_switch', None),
                  0x7BF733: ('firing_backing_state_f4', None),
                  0x7BF791: ('firing_backing_state_b4', None)}
FAILURE_COUNTERS = ('owner_lock_drops', 'record_lock_drops', 'read_misses',
                    'context_misses', 'nesting_misses', 'dropped', 'rejected')

def identity(b):
    return tuple(b[k] for k in IDENTITY) + tuple(b.get(k, 0) for k in ('server_player', 'server_soldier', 'server_item'))

def ammo(b):
    return [b['loaded'], b['reserve']]

def snapshot(value):
    if not isinstance(value, dict):
        raise ValueError('snapshot was not copied')
    data = value.get('bytes')
    if not isinstance(data, list) or len(data) != 64 or any(type(x) is not int or not 0 <= x <= 255 for x in data):
        raise ValueError('snapshot must contain exactly 64 bytes')
    raw = bytes(data)
    current, following, timer = struct.unpack_from('<IIf', raw)
    loaded, reserve = struct.unpack_from('<ii', raw, 0x18)
    if current > 15 or following > 15 or not math.isfinite(timer) or abs(timer) > 1000000 or any(x < -1 or x > 1000000 for x in (loaded, reserve)):
        raise ValueError('malformed snapshot projection')
    return raw, {'current': current, 'next': following, 'timer': timer, 'loaded': loaded, 'reserve': reserve}

def audit(trace, require_restore=False, require_branch40_restore=False, require_server_transfer=False, allow_diagnostic_hold=False, allow_diagnostic_round=False):
    flow = trace['gameplay']['reload_flow']
    rows = flow['records']
    errors = []
    def check(condition, message):
        if not condition:
            errors.append(message)
    check(flow.get('installed') is True and flow.get('started') is True and flow.get('drained') is True and flow.get('in_flight') == 0, 'observer did not install/start/drain cleanly')
    if allow_diagnostic_round:
        check(flow.get('observation_only') is False and flow.get('native_state_writes') is True and flow.get('diagnostic_round', {}).get('enabled') is True and flow.get('diagnostic_hold', {}).get('enabled') is False and flow.get('native_gate_enabled') is False and flow.get('authority_proven') is False, 'trace does not describe explicit diagnostic-only round mode')
    elif allow_diagnostic_hold:
        check(flow.get('observation_only') is False and flow.get('native_state_writes') is True and flow.get('diagnostic_hold', {}).get('enabled') is True and flow.get('native_gate_enabled') is False and flow.get('authority_proven') is False, 'trace does not describe explicit diagnostic-only hold mode')
    else:
        check(flow.get('observation_only') is True and flow.get('native_state_writes') is False and flow.get('native_gate_enabled') is False and flow.get('authority_proven') is False, 'trace does not describe observation-only mode')
    check(trace.get('hooks_disabled') is True, 'native hooks were not reported disabled')
    check(all(flow.get(k) == 0 for k in FAILURE_COUNTERS), 'observer lost or rejected evidence')
    check(flow.get('server_read_misses', 0) == 0, 'server callback ownership/read evidence lost')
    check(isinstance(rows, list) and bool(rows), 'no records')
    ids = [r['id'] for r in rows]
    check(ids == list(range(1, len(rows) + 1)), 'record identifiers are not unique and sequential')
    by_id = {r['id']: r for r in rows}
    counts = Counter(r['kind'] for r in rows)
    check(all(kind in range(4) for kind in counts), 'unknown event kind')
    matched = flow.get('matched', [])
    check(all(n < len(matched) and matched[n] == counts[n] for n in counts), 'matched counters disagree with records')
    restorations, transfers = [], []
    groups = defaultdict(list)
    for r in rows:
        tag = f"record {r['id']}"
        b, a = r['before'], r.get('after')
        valid = r.get('finished') is True and r.get('identity_retained') is True and isinstance(a, dict)
        check(valid, tag + ': incomplete or owner changed')
        if not valid:
            continue
        check(identity(b) == identity(a), tag + ': identity fields changed')
        check(all(b[k] > 0 for k in OWNER), tag + ': invalid owner identity')
        check(b['branch'] in (0, 1, 2) and b['wrapper_offset'] == {0: 0x3C, 1: 0x40, 2: 0x10}.get(b['branch']), tag + ': branch/offset mismatch')
        if b['branch'] == 2:
            check(all(b.get(k, 0) >= 0x10000 for k in ('server_player', 'server_soldier', 'server_item')), tag + ': missing explicit server identities')
            check(flow.get('server_binding_verified') is True, tag + ': server binding was not verified')
        check(r['begin_ns'] > 0 and r['end_ns'] >= r['begin_ns'], tag + ': invalid interval')
        for relation in ('parent', 'update'):
            if r.get(relation):
                parent = by_id.get(r[relation])
                okay = parent and parent['begin_ns'] <= r['begin_ns'] <= r['end_ns'] <= parent['end_ns'] and parent['thread'] == r['thread'] and identity(parent['before']) == identity(b)
                if relation == 'update':
                    okay = okay and parent['kind'] == 0
                else:
                    okay = okay and parent['depth'] < r['depth']
                check(okay, tag + ': invalid ' + relation + ' nesting')
        if not r.get('parent'):
            groups[identity(b)].append(r)
        if r['kind'] == 2:
            delta = [a['loaded'] - b['loaded'], a['reserve'] - b['reserve']]
            transfers.append({'record': r['id'], 'branch': b['branch'], 'firing': b['firing'], 'caller': r['caller'],
                              'update': r.get('update', 0), 'path': r.get('transfer_path'), 'before': ammo(b), 'after': ammo(a),
                              'single_conserved_round': delta == [1, -1]})
        if r['kind'] != 3:
            continue
        route, wanted_branch = RESTORE_ROUTES.get(r['caller'], ('unclassified_restore_caller', None))
        check(wanted_branch is None or wanted_branch == b['branch'], tag + ': inspected restore caller has unexpected branch')
        check(r.get('context', 0) >= 0x10000, tag + ': missing snapshot source address')
        projection = None
        source_matches = False
        try:
            raw, projection = snapshot(r.get('snapshot_before'))
            raw_after, _ = snapshot(r.get('snapshot_after'))
            source_matches = raw == raw_after and all(a[k] == projection[k] for k in ('current', 'next', 'loaded', 'reserve')) and math.isclose(a['timer'], projection['timer'], rel_tol=1e-5, abs_tol=1e-7) and a['previous'] == b['current']
        except (KeyError, TypeError, ValueError) as exc:
            errors.append(tag + ': ' + str(exc))
        check(source_matches, tag + ': restore source/output did not agree')
        check(r.get('restore_fields_matched') is source_matches, tag + ': runtime restore verdict disagrees with audit')
        delta = [a['loaded'] - b['loaded'], a['reserve'] - b['reserve']]
        restorations.append({'record': r['id'], 'branch': b['branch'], 'firing': b['firing'], 'caller': r['caller'],
                             'route': route, 'begin_ns': r['begin_ns'], 'end_ns': r['end_ns'],
                             'source_address': r.get('context'), 'source': projection,
                             'before': ammo(b), 'after': ammo(a), 'delta': delta,
                             'source_matches_output': source_matches,
                             'ammo_changed': delta != [0, 0],
                             'conserved_round_added': delta == [1, -1],
                             'conserved_round_removed': delta == [-1, 1]})
    gaps, overlaps = [], []
    for key, group in groups.items():
        group.sort(key=lambda r: (r['begin_ns'], r['id']))
        for previous, current in zip(group, group[1:]):
            if previous['end_ns'] > current['begin_ns']:
                overlaps.append([previous['id'], current['id']])
                continue
            if ammo(previous['after']) != ammo(current['before']):
                gaps.append({'branch': current['before']['branch'], 'firing': current['before']['firing'], 'previous_record': previous['id'], 'next_record': current['id'],
                             'from_ns': previous['end_ns'], 'to_ns': current['begin_ns'],
                             'before': ammo(previous['after']), 'after': ammo(current['before'])})
    server_transfers = [r for r in transfers if r['branch'] == 2]
    server_counts = Counter(r['kind'] for r in rows if r['before']['branch'] == 2)
    if server_counts:
        matched = flow.get('server_matched', [])
        check(all(n < len(matched) and matched[n] == server_counts[n] for n in server_counts), 'server matched counters disagree with records')
    if require_server_transfer:
        check(bool(server_transfers) and all(r['single_conserved_round'] for r in server_transfers), 'required conserved server shell transfer was not observed')
    if require_restore:
        check(bool(restorations), 'required restore events were not observed')
    if require_branch40_restore:
        check(any(r['route'] == 'decoded_snapshot_to_branch40' and r['ammo_changed'] and r['source_matches_output'] for r in restorations), 'required ammo-changing branch40 decode/restore was not observed')
    return {'schema': 'fvr.bc2.reload_restore_audit.v1', 'valid_observation': not errors, 'errors': errors,
            'authority_proven': False, 'manual_gate_enabled': False, 'headset_verified': False,
            'server_event_counts': dict(sorted(server_counts.items())), 'server_transfers': server_transfers,
            'server_publish_misses': flow.get('server_publish_misses', 0),
            'event_counts': dict(sorted(counts.items())), 'restores': restorations, 'transfers': transfers,
            'between_observed_calls_ammo_changes': gaps, 'overlapping_top_level_calls': overlaps,
            'branch40_decode_restore_ammo_changes': sum(r['route'] == 'decoded_snapshot_to_branch40' and r['ammo_changed'] and r['source_matches_output'] for r in restorations),
            'restore_round_reverts': [r for r in restorations if r['conserved_round_removed']],
            'unclassified_restore_callers': sorted({r['caller'] for r in restorations if r['route'] == 'unclassified_restore_caller'}),
            'limits': ['Between-call gaps identify unobserved changes, not the writer.', 'A copied snapshot source may originate outside these hooks; neither client branch is thereby authoritative.', 'A restored ammo reduction is a correction observation, not necessarily a defect.', 'Unknown restore callers stay unclassified; no native capability is enabled.']}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--require-restore', action='store_true')
    parser.add_argument('--require-branch40-restore', action='store_true')
    parser.add_argument('--require-server-transfer', action='store_true')
    args = parser.parse_args()
    raw = args.trace.read_bytes()
    try:
        result = audit(json.loads(raw), args.require_restore, args.require_branch40_restore, args.require_server_transfer)
    except (KeyError, TypeError, ValueError) as exc:
        result = {'schema': 'fvr.bc2.reload_restore_audit.v1', 'valid_observation': False, 'errors': ['Malformed trace: ' + str(exc)], 'authority_proven': False, 'manual_gate_enabled': False}
    result['source'] = {'path': str(args.trace.resolve()), 'sha256': hashlib.sha256(raw).hexdigest()}
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('valid_observation', 'errors')}))
    return 0 if result['valid_observation'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
