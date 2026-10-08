"""Strict offline M95 physical-bolt audit. Reads saved evidence only; never opens a process."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
import audit_physical_pump_probe as common

SCHEMA = 'fvr.bc2.physical_bolt_audit.v2'
ASSET = 'Objects/Weapons/Handheld/BU_sni_M95/SP_sni_M95'
PROFILE = 0x4d3935424f4c5401
FINGERPRINT = 0xa7f219a1426216ab
STROKE, UNLOCK = .1484906094, -.7456515125
PHYSICAL = ('actor', 'actor_generation', 'equipment_generation', 'space', 'item',
            'item_generation', 'mechanism', 'mechanism_generation')
NATIVE = ('player', 'soldier', 'weak', 'weapon', 'actor_generation', 'space')
STATE = ('current', 'previous', 'next', 'timer', 'loaded', 'reserve', 'flags_a8')


def positive(value):
    return type(value) is int and value > 0


def finite(value):
    return type(value) in (int, float) and math.isfinite(value)


def byte_array(value, size):
    if not isinstance(value, list) or len(value) != size or any(type(v) is not int or not 0 <= v <= 255 for v in value):
        raise ValueError('Missing or malformed original byte array')
    return bytes(value)


def f32_equal(a, b):
    return finite(a) and finite(b) and struct.pack('<f', a) == struct.pack('<f', b)


def snapshot_timer_matches(raw, reported):
    # The native-flow JSON serializer in stock235 uses the stream's six
    # significant digits. Raw snapshot bytes and restore_fields_matched carry
    # the exact native projection; do not demand a binary round trip from that
    # deliberately shorter decimal representation, or add a generic epsilon.
    return finite(raw) and finite(reported) and (f32_equal(raw, reported) or float(format(raw, '.6g')) == reported)


def unheld(r):
    return all(r.get(k) is False for k in ('hold_requested', 'hold_applied', 'hold_restored', 'hold_unexpected_native_write')) and \
        r.get('hold_original_delta_bits') == r.get('hold_before_restore_bits') == 0


def context(r, scratch=True, fire_released=True):
    a, b = r.get('context_before') or {}, r.get('context_after') or {}
    x, y = byte_array(a.get('bytes'), 48), byte_array(b.get('bytes'), 48)
    delta, multiplier = struct.unpack_from('<f', x, 24)[0], struct.unpack_from('<f', x, 32)[0]
    flags = int.from_bytes(x[44:48], 'little')
    return a.get('decoded_valid') is True and b.get('decoded_valid') is True and \
        0 < delta <= .10000001 and 0 < multiplier <= 4 and all(v <= 1 for v in x[36:41]) and \
        (x[:20] == y[:20] and x[24:] == y[24:] if scratch else x == y) and \
        flags in ((0,) if fire_released else (0, 1)) and not x[38] and not x[40] and (not fire_released or not x[39])


def native_identity(r, c, branch):
    n = c['native']
    if len(n['firing']) != 3 or len(set(n['firing'])) != 3 or not all(positive(v) for v in n['firing']):
        return False
    for b in (r.get('before') or {}, r.get('after') or {}):
        if not all(b.get(k) == n[k] for k in NATIVE) or b.get('equip_generation') != n['equipment_generation'] or \
           b.get('branch') != branch or b.get('firing') != n['firing'][branch] or b.get('wrapper_offset') != (60, 64, 16)[branch]:
            return False
        if not all(b.get(k) == (n[k] if branch == 2 else 0) for k in ('server_player', 'server_soldier', 'server_item')):
            return False
        if not positive(b.get('snapshot_sequence')) or not all(type(b.get(k)) is int and 0 <= b[k] <= 15 for k in ('current', 'previous', 'next')) or \
           not finite(b.get('timer')) or not 0 <= b['timer'] <= 3600 or b.get('flags_a8', 24) & 24:
            return False
    return r['before']['snapshot_sequence'] == r['after']['snapshot_sequence'] and r['before'].get('soldier_flags') == r['after'].get('soldier_flags')


def basic(r, c, branch):
    return native_identity(r, c, branch) and r.get('finished') is True and r.get('identity_retained') is True and \
        positive(r.get('id')) and positive(r.get('native_invocation')) and positive(r.get('thread')) and \
        r.get('caller', 0) >= 0x10000 and r.get('context', 0) >= 0x10000 and 0 < r.get('begin_ns', 0) < r.get('end_ns', 0)


def update(r, c, branch, released=True):
    return common.own_update(r) and r.get('parent') == 0 and basic(r, c, branch) and context(r, fire_released=released)


def restore(r, c, branch, rewind=False, progression=False):
    if branch >= 2 or r.get('kind') != 3 or r.get('depth') != 1 or not basic(r, c, branch) or not unheld(r) or \
       any(r.get(k) != 0 for k in ('parent', 'update', 'native_parent', 'native_update')) or \
       r.get('context_before') is not None or r.get('context_after') is not None or r.get('restore_fields_matched') is not True:
        return False
    x, y = r.get('snapshot_before') or {}, r.get('snapshot_after') or {}
    raw = byte_array(x.get('bytes'), 64)
    if raw != byte_array(y.get('bytes'), 64) or x.get('decoded_valid') is not True or y.get('decoded_valid') is not True:
        return False
    a, b = r['before'], r['after']
    decoded = dict(current=struct.unpack_from('<I', raw, 0)[0], next=struct.unpack_from('<I', raw, 4)[0],
                   timer=struct.unpack_from('<f', raw, 8)[0], loaded=struct.unpack_from('<i', raw, 24)[0], reserve=struct.unpack_from('<i', raw, 28)[0])
    if any(x.get(k) != y.get(k) or (not snapshot_timer_matches(decoded[k], b.get(k)) or not snapshot_timer_matches(decoded[k], x.get(k)) if k == 'timer' else decoded[k] != b.get(k) or decoded[k] != x.get(k)) for k in decoded) or \
       b['previous'] != a['current'] or any(a[k] != b[k] for k in ('loaded', 'reserve', 'flags_a8')):
        return False
    if progression:
        # Reviewed CyclePreHoldSnapshotProgression: exact native snapshot may
        # cross the original post-shot wait/bolt boundary in either direction.
        # This proves no Commit or Ready; the caller must establish no prior
        # hold on ANY branch and the retained preceding original state.
        phase = lambda s: 0 < s['timer'] and ((s['current'], s['next']) == (6, 7) and s['timer'] <= .10000001 or
                                             (s['current'], s['next']) == (8, 1) and s['timer'] <= 2.300001)
        return a['current'] != b['current'] and phase(a) and phase(b) and a['loaded'] > 0
    if rewind:
        return (a['current'], a['previous'], a['next']) in ((8, 7, 1), (8, 8, 1)) and \
            0 < a['timer'] <= 2.300001 and (b['current'], b['previous'], b['next']) == (6, 8, 7) and 0 < b['timer'] <= .10000001
    return a['current'] == b['current'] and a['next'] == b['next']


def held(r, c, branch, restored_previous=None):
    if not update(r, c, branch) or not all(r.get(k) is True for k in ('hold_requested', 'hold_applied', 'hold_restored')) or \
       r.get('hold_unexpected_native_write') is not False or r.get('hold_before_restore_bits') != 0:
        return False
    a, b = r['before'], r['after']
    raw = byte_array(r['context_before']['bytes'], 48)
    return a['current'] == 8 and a['next'] == 1 and (a['previous'] == 7 or branch < 2 and a['previous'] == restored_previous) and \
        0 < a['timer'] <= 2.300001 and a['loaded'] == c['loaded'] > 0 and a['reserve'] == c['reserve'] and \
        all(a[k] == b[k] for k in STATE) and int.from_bytes(raw[24:28], 'little') == r.get('hold_original_delta_bits')


def baseline(doc):
    if not (doc.get('passed') is True and doc.get('read_only') is True and doc.get('process_writes') is False and
            doc.get('native_calls') is False and doc.get('input_or_focus_changes') is False and
            doc.get('executable_sha256') == common.EXECUTABLE_SHA256 and doc.get('rejected') == []):
        return None
    rows = doc.get('samples', [])
    if len(rows) != 2 or not 0 < rows[1]['monotonic_ns'] - rows[0]['monotonic_ns'] <= 500000000:
        return None
    for r in rows:
        s = r['state']
        if r.get('asset_name') != 'M95_sp' or r.get('asset_path') != ASSET or r.get('identity_coherent') is not True or \
           (s['current'], s['next'], s['timer']) != (2, 2, 0) or not 0 < s['loaded'] <= 5 or s['reserve'] < 0:
            return None
    if not common.same_client_owner(rows[0]['client_owner'], rows[1]['client_owner']) or \
       any(rows[0][k] != rows[1][k] for k in ('server_player', 'server_soldier', 'server_item', 'server_firing', 'weapon_data', 'firing_data', 'ammo_address')) or \
       any(rows[0]['state'][k] != rows[1]['state'][k] for k in ('loaded', 'reserve')):
        return None
    return rows[1]


def recording(flow, first, last):
    keys = ('dropped', 'rejected', 'record_lock_drops', 'record_begin_lock_drops', 'record_end_lock_drops', 'owner_lock_drops', 'context_misses', 'nesting_misses')
    counters = {k: flow.get(k) for k in keys}
    start, stop = flow.get('start_ns', 0), flow.get('start_ns', 0) + flow.get('window_seconds', 0) * 1000000000
    journal = flow.get('observation_read_misses', {})
    rows = journal.get('rows', [])
    valid = journal.get('schema') == 1 and journal.get('drained') is True and journal.get('overflow') == 0 and \
        journal.get('count') == len(rows) == flow.get('read_misses', -1) + flow.get('server_read_misses', -1) and \
        0 <= len(rows) <= journal.get('capacity', -1) and all(type(r.get('now_ns')) is int and r['now_ns'] >= start for r in rows)
    within = [r for r in rows if first <= r.get('now_ns', 0) <= last]
    no_drops = all(v == 0 for v in counters.values()) and all(flow.get('completion_journal', {}).get(k) == 0 for k in ('pending', 'overflow', 'rejected_drain'))
    bounds = 0 < start <= first < last < stop
    return dict(bounded_trial_complete=bounds and valid and no_drops and not within,
                recorder_complete=no_drops and valid and not rows and flow.get('window_expired_calls') == 0,
                first_fire_ns=first, last_settle_ns=last, recording_start_ns=start, recording_end_exclusive_ns=stop,
                bounds_valid=bounds, journal_complete=valid, in_trial_read_misses=within,
                out_of_trial_read_miss_count=len(rows)-len(within), counters=counters,
                window_expired_calls=flow.get('window_expired_calls'))


def targets_and_custody(fixture, palette, c, event_times):
    rows = [r for r in palette['paired_targets'] if r.get('cycle') == c['cycle']]
    custody = [r for r in fixture['custody_events'] if event_times[5] <= r['now_ns'] <= event_times[16]]
    if len(rows) != 8 or len(custody) != 2 or [r['phase'] for r in custody] != [2, 3]:
        return False, {}
    enter, returned = custody
    for cr in custody:
        if not positive(cr['input_sequence']) or not 0 < cr['observed_ns'] <= cr['now_ns'] < cr['deadline_ns'] or \
           len(cr['released_ids']) != 2 or not any(positive(v) for v in cr['released_ids']):
            return False, {}
        for claim in (cr['gun'], cr['companion']):
            if claim is None:
                continue
            if not all(positive(claim[k]) for k in ('id', 'item', 'item_generation', 'contact', 'contact_generation', 'source_sequence')) or \
               claim['item'] != c['held']['item'] or claim['item_generation'] != c['held']['item_generation'] or \
               claim['source_sequence'] > cr['input_sequence'] or not cr['now_ns'] < claim['deadline_ns'] <= cr['deadline_ns'] or claim['id'] in cr['released_ids']:
                return False, {}
    eg, rg, rs = enter['gun'], returned['gun'], returned['companion']
    if enter['companion'] is not None or (eg['kind'], eg['hand'], eg['parent']) != (1, 0, 0) or \
       (rg['kind'], rg['hand'], rg['parent']) != (1, 1, 0) or rs is None or (rs['kind'], rs['hand'], rs['parent']) != (2, 0, rg['id']) or \
       eg['id'] not in returned['released_ids'] or len({eg['id'], rg['id'], rs['id']}) != 3 or \
       not event_times[5] <= enter['now_ns'] <= event_times[6] or not event_times[14] <= returned['now_ns'] <= event_times[15]:
        return False, {}
    phases = []
    for phase in range(2, 6):
        pair = [r for r in rows if r['mechanism_phase'] == phase]
        if len(pair) != 2 or {r['edge'] for r in pair} != {'first', 'last'}:
            return False, {}
        a, b = sorted(pair, key=lambda r: r['edge'])  # first, last
        if a['phase_pairs'] != b['phase_pairs'] or a['phase_pairs'] < 2 or \
           not a['packed_ns'] < b['packed_ns'] or not a['draw_serial'] < b['draw_serial'] or not a['source_sequence'] < b['source_sequence']:
            return False, {}
        for r in pair:
            if r['shot'] != c['shot'] or r['profile'] != PROFILE or r['revision'] != fixture['revision'] or r['rig_fingerprint'] != FINGERPRINT or \
               any(r[k] != c['held'][k] for k in PHYSICAL) or r['native_weapon'] != c['native']['weapon'] or \
               not all(positive(r[k]) for k in ('mechanism_claim', 'gun_claim', 'held_sequence', 'source_sequence', 'input_sequence', 'draw_serial')) or \
               (r['mechanism_hand'], r['gun_hand']) != (1, 0) or r['gun_claim'] != r['mechanism_parent'] or r['gun_claim'] != eg['id'] or \
               r['mechanism_claim'] == r['gun_claim'] or r['mechanism_claim'] in enter['released_ids'] or \
               not r['source_sequence'] <= r['input_sequence'] <= c['release_input_sequence'] or r['held_sequence'] > c['held']['sequence'] or \
               r['source_observed_ns'] > r['input_observed_ns'] or r['source_deadline_ns'] > r['held_deadline_ns'] or \
               not enter['now_ns'] <= r['packed_ns'] <= event_times[12] or \
               not all(0 < r[p+'observed_ns'] <= r['packed_ns'] < r[p+'deadline_ns'] for p in ('source_', 'input_', 'held_')) or \
               not finite(r['travel']) or not -.000001 <= r['travel'] <= STROKE+.000001 or not finite(r['rotation']):
                return False, {}
        phases.append((a, b))
    if any(phases[i][1]['packed_ns'] >= phases[i+1][0]['packed_ns'] for i in range(3)) or len({r['mechanism_claim'] for r in rows}) != 1:
        return False, {}
    # Endpoint margins are exactly the admitted geometry policy tolerances.
    u, rear, forward, lock = phases
    near = lambda a, b, tolerance: abs(a-b) <= tolerance + .000001
    geometry = all(r['travel'] <= .004001 for r in (*u, *lock)) and \
        near(u[0]['rotation'], 0, .07) and near(u[1]['rotation'], UNLOCK, .035) and \
        near(rear[0]['travel'], 0, .004) and near(rear[1]['travel'], STROKE, .004) and \
        near(forward[0]['travel'], STROKE, .004) and near(forward[1]['travel'], 0, .004) and \
        all(near(r['rotation'], UNLOCK, .035) for r in (*rear, *forward)) and \
        near(lock[0]['rotation'], UNLOCK, .035) and near(lock[1]['rotation'], 0, .035)
    return geometry, dict(enter_gun=eg['id'], mechanism=rows[0]['mechanism_claim'], returned_gun=rg['id'],
                          returned_support=rs['id'], return_ns=returned['now_ns'], paired_draws=[r['draw_serial'] for r in rows])


def audit_checked(trace, receiver, before, after, completion, inputs):
    game = trace['gameplay']; flow = game['reload_flow']; native = flow['native_cycle_candidate']
    fixture = game['physical_bolt_probe']; palette = game['rig_publication']['bolt_presentation']
    checks, details = {}, {}; count = fixture['requested']; cycles = fixture['cycles']; events = fixture['events']
    checks['finite_real_fixture_completed'] = type(count) is int and count in (1, 2) and fixture['phase'] == 17 and fixture['failure'] == 0 and fixture['completed'] == len(cycles) == count
    expected = [1, 2] + list(range(3, 17))*count + [17] if count in (1, 2) else []
    checks['ordered_fixture_events'] = [r['phase'] for r in events] == expected and all(r['failure'] == 0 for r in events) and \
        all(a['now_ns'] <= b['now_ns'] for a, b in zip(events, events[1:])) and fixture.get('event_drops') == fixture.get('custody_drops') == 0
    checks['measured_original_contacts'] = fixture['raw_matches'] > 0 and fixture['controller_poses'] > 0 and fixture['profile'] == PROFILE and \
        positive(fixture['revision']) and fixture['rig_fingerprint'] == FINGERPRINT and palette['contacts'] > 0
    checks['native_release_and_ready_counts'] = native['enabled'] is True and native['releases'] == native['acknowledgements'] == count and native['failure'] == 0
    checks['no_native_ammo_or_animation_writes'] = fixture.get('native_animation_handle_written') is False and fixture.get('native_ammo_written') is False
    checks['unadmitted_headset_status_preserved'] = native.get('admitted') is False and native.get('headset_verified') is False and \
        completion.get('headset_verified') is False and completion.get('manual_bolt_admitted') is False
    checks['paired_renderer_evidence_complete'] = palette['pairs'] > 0 and palette['copies'] >= 2*palette['pairs'] and \
        palette.get('evidence_drops') == palette.get('evidence_rejected') == 0 and len(palette['paired_targets']) == len(cycles)*8 and len(fixture['custody_events']) == len(cycles)*2
    checks['paired_targets_cover_requested_cycles'] = len(cycles) == count and len(palette['paired_targets']) == count*8 and len(fixture['custody_events']) == count*2
    checks['ordinary_neutral_receiver'] = receiver.get('physical_bolt_receiver') is True and receiver.get('consumed_pairs') == 240 and receiver.get('async_timeouts') == 0 and \
        type(receiver.get('native_tracking_transport_verified')) in (bool,int) and receiver.get('native_tracking_transport_verified') == 1 and receiver.get('headset_tested') is False
    for flag in ('m95_stock_shot_neutral', 'physical_pump_receiver', 'resource_inventory_receiver', 'resource_magazine_receiver', 'shot_probe_requested', 'vehicle_fixture', 'pump_hold_fixture'):
        checks['ordinary_neutral_receiver'] &= receiver.get(flag) is False
    neutral = bool(inputs)
    generations = {r.get('generation') for r in inputs}
    pose_keys = ('head', 'reference_head')
    valid_pose = lambda p: isinstance(p, list) and len(p) == 7 and all(finite(v) for v in p) and .99 <= sum(v*v for v in p[3:]) <= 1.01
    for index, r in enumerate(inputs):
        neutral &= positive(r.get('generation')) and positive(r.get('space')) and r.get('focused') is True and r.get('head_valid') is True and len(r.get('hands', [])) == 2 and all(valid_pose(r.get(k)) for k in pose_keys)
        for h in r.get('hands', []):
            neutral &= h.get('held') == h.get('touch_active') == h.get('touched') == 0 and h.get('active') == 127 and h.get('grip_tracked') is True and h.get('aim_tracked') is True and h.get('axes') == [0, 0, 0, 0] and all(valid_pose(h.get(k)) for k in ('grip', 'aim'))
        if index:
            neutral &= inputs[index-1]['generation'] < r['generation'] and 0 <= r['tick_ms'] - inputs[index-1]['tick_ms'] <= 250 and r['space'] == inputs[0]['space'] and all(r[k] == inputs[0][k] for k in pose_keys)
    checks['receiver_never_fabricates_actions'] = neutral
    checks['source_sequences_exist_in_transport'] = bool(inputs) and all(r[k] in generations for r in palette['paired_targets'] for k in ('source_sequence','input_sequence')) and \
        all(c['release_input_sequence'] in generations and c['held']['space'] == inputs[0]['space'] for c in cycles) and \
        all(r['input_sequence'] in generations and all(claim['source_sequence'] in generations for claim in (r['gun'],r['companion']) if claim) for r in fixture['custody_events'])
    checks['clean_shutdown_and_helpers'] = trace.get('hooks_disabled') is True and flow.get('drained') is True and flow.get('in_flight') == 0 and \
        all(completion.get(k) == 0 for k in ('trace_exit', 'receiver_exit', 'postflight_exit')) and completion.get('receiver_exited') is True and \
        completion.get('responding') is True and completion.get('game_exited') is False and completion.get('requested_cycles') == count
    a, b = baseline(before), baseline(after)
    checks['independent_m95_baselines'] = bool(a and b)
    checks['external_ammunition_conservation'] = bool(a and b) and before['pid'] == after['pid'] == trace['pid'] == completion['pid'] and \
        common.same_client_owner(a['client_owner'], b['client_owner']) and all(a[k] == b[k] for k in ('server_player', 'server_soldier', 'server_item', 'server_firing', 'weapon_data', 'firing_data', 'ammo_address')) and \
        b['state']['loaded'] == a['state']['loaded']-count > 0 and b['state']['reserve'] == a['state']['reserve']
    records = flow['records']; ids = [r['id'] for r in records]; invs = [r['native_invocation'] for r in records]
    unique = bool(records) and all(positive(n) for n in ids+invs) and len(set(ids)) == len(ids) and len(set(invs)) == len(invs)
    for thread in {r['thread'] for r in records}:
        ordered = sorted((r for r in records if r['thread'] == thread), key=lambda r:r['begin_ns'])
        unique &= all(x['native_invocation'] < y['native_invocation'] for x, y in zip(ordered, ordered[1:]))
    checks['independent_unique_native_lineage'] = unique
    parents = {r['native_invocation']:r for r in records if common.own_update(r)}
    hold_counts = [0, 0, 0]; used_holds = set(); cycle_details = []; native_ok = gesture_ok = bool(cycles)
    hand = game['hand_ownership']; support_ok = hand.get('events_dropped') == 0
    for i, c in enumerate(cycles):
        prefix = events[2+i*14:2+(i+1)*14]
        if [r['phase'] for r in prefix] != list(range(3,17)) or any(r['failure'] != 0 for r in prefix):
            native_ok = gesture_ok = False
            cycle_details.append(dict(cycle=c.get('cycle'),native_complete=False,gesture_complete=False,error='Missing or invalid completed-cycle event prefix'))
            continue
        times = {r['phase']:r['now_ns'] for r in events[2+i*14:2+(i+1)*14]}
        n, lease = c['native'], c['held']; first, release, ready = times[3], c['release_ns'], c['ready_ns']
        valid = 0 < first < release < ready <= times[16] and release < c['release_deadline_ns'] and ready < c['ready_deadline_ns'] and \
            c['native_ready'] is True and c['unchanged_ammunition'] is True and c['custody_returned'] is True and c['pairs'] > 0 and \
            all(positive(c[k]) for k in ('cycle', 'shot', 'request', 'release_input_sequence', 'ready_sequence')) and \
            0 < lease['observed_ns'] <= times[12] < lease['deadline_ns'] and c['loaded'] > 0 and c['reserve'] >= 0 and \
            lease['actor'] == (n['weak'] << 32) | n['soldier'] and lease['space'] == n['space'] and \
            all(positive(lease[k]) for k in PHYSICAL)
        if a:
            valid &= n['player'] == a['client_owner']['player'] and n['soldier'] == a['client_owner']['actor'] and n['weak'] == a['client_owner']['weak'] and \
                n['weapon'] == a['client_owner']['selected_weapon'] and n['firing'][2] == a['server_firing'] and \
                all(n[k] == a[k] for k in ('server_player', 'server_soldier', 'server_item')) and c['loaded'] == a['state']['loaded']-i-1 and c['reserve'] == a['state']['reserve']
        if i:
            valid &= c['cycle'] > cycles[i-1]['cycle'] and c['shot'] > cycles[i-1]['shot'] and first > cycles[i-1]['ready_ns'] and n == cycles[i-1]['native']
        branches = []
        first_hold_begin = min((r['begin_ns'] for r in records if r.get('hold_applied') is True and first <= r['begin_ns'] <= ready), default=0)
        for branch in range(3):
            cohort = sorted((r for r in records if r.get('before', {}).get('firing') == n['firing'][branch] and first <= r['begin_ns'] <= r['end_ns'] <= ready), key=lambda r:r['begin_ns'])
            shots = [r for r in cohort if common.own_update(r) and r['before']['loaded'] == c['loaded']+1 and r['after']['loaded'] == c['loaded']]
            good_shot = len(shots) == 1 and update(shots[0], c, branch, False) and unheld(shots[0]) and \
                (shots[0]['after']['current'], shots[0]['after']['previous'], shots[0]['after']['next']) in ((6,5,7),(7,6,7),(7,6,8),(8,7,1)) and \
                shots[0]['before']['reserve'] == shots[0]['after']['reserve'] == c['reserve'] and shots[0]['end_ns'] < release
            valid &= good_shot
            shot_end = shots[0]['end_ns'] if shots else first
            # Inspect every own update, not just the selected successful rows.
            valid &= all(update(r, c, branch, r.get('hold_applied') is True or r['begin_ns'] >= release) and (r.get('hold_applied') is True or unheld(r)) for r in cohort if r['kind'] == 0)
            valid &= not any(r['kind'] == 2 for r in cohort)
            holds = [r for r in cohort if r.get('hold_applied') is True]
            restorations = []; rewinds = []; progressions = []; validated_holds = set()
            held_started = False; restored_previous = None
            last_original = shots[0] if good_shot else None
            for r in cohort:
                if r['begin_ns'] < shot_end or r['begin_ns'] >= times[12]:
                    continue
                if r['kind'] == 3:
                    if restore(r, c, branch):
                        pre, post = r['before'], r['after']
                        hold_restore = (pre['current'],pre['next']) == (8,1) and (pre['previous'] == 7 or pre['previous'] == restored_previous) and 0 < pre['timer'] <= 2.300001 and 0 < post['timer'] <= 2.300001
                        pre_restore = not held_started and (pre['current'],pre['next']) == (6,7) and pre['previous'] in ((5,6,8) if rewinds else (5,6)) and 0 < pre['timer'] <= 30 and 0 < post['timer'] <= 30
                        valid &= hold_restore or pre_restore
                        restorations.append(r['native_invocation'])
                        restored_previous = post['previous'] if hold_restore else None
                    elif not held_started and r['end_ns'] <= first_hold_begin and restore(r, c, branch, progression=True) and \
                         last_original is not None and last_original['end_ns'] <= r['begin_ns'] and \
                         last_original['native_invocation'] < r['native_invocation'] and last_original['after']['current'] == r['before']['current'] and \
                         not any(u['begin_ns'] < r['end_ns'] and u['end_ns'] > r['begin_ns'] for u in cohort if common.own_update(u)):
                        progressions.append(dict(invocation=r['native_invocation'], preceding_invocation=last_original['native_invocation'],
                                                 before_current=r['before']['current'], after_current=r['after']['current'],
                                                 after_previous=r['after']['previous']))
                        restored_previous = r['after']['previous'] if r['after']['current'] == 8 else None
                        if r['after']['current'] == 6:
                            rewinds.append(r['native_invocation'])
                    else:
                        valid = False
                if r.get('hold_applied') is True:
                    proved = held(r, c, branch, restored_previous)
                    valid &= proved
                    if proved:
                        validated_holds.add(r['native_invocation'])
                    held_started = True
                if common.own_update(r) or r['kind'] == 3:
                    last_original = r
            valid &= bool(holds) and all(shot_end <= r['begin_ns'] < r['end_ns'] <= times[12] and r['native_invocation'] in validated_holds for r in holds)
            hold_counts[branch] += len(holds); used_holds.update(r['native_invocation'] for r in holds)
            tails = [r for r in cohort if r['kind'] == 1 and release <= r['begin_ns']]
            transitions = []
            for r in tails:
                parent = parents.get(r.get('native_parent'))
                valid &= basic(r, c, branch) and r.get('depth') == 2 and r.get('native_parent') == r.get('native_update') and \
                    bool(parent) and update(parent, c, branch) and unheld(parent) and unheld(r) and context(r, False) and common.direct_commit(r, parent)
                pre, post = r['before'], r['after']
                valid &= post['current'] == pre['next'] and post['previous'] == pre['current'] and post['loaded'] == c['loaded'] and post['reserve'] == c['reserve']
                transitions.append((pre['current'], post['current']))
            valid &= transitions == [(8,1),(1,2)]
            valid &= not holds or not tails or holds[-1]['end_ns'] <= tails[0]['begin_ns']
            for r in cohort:
                if r['kind'] != 3 or r['begin_ns'] < times[12]:
                    continue
                pre, post = r['before'], r['after']
                before_tail = bool(tails) and r['end_ns'] <= tails[0]['begin_ns']
                after_tail = len(tails) == 2 and r['begin_ns'] >= tails[-1]['end_ns']
                conserved_hold = before_tail and (pre['current'],pre['next']) == (8,1) and (pre['previous'] == 7 or pre['previous'] == restored_previous) and 0 < pre['timer'] <= 2.300001 and 0 < post['timer'] <= 2.300001
                conserved_ready = after_tail and (pre['current'],pre['next'],pre['timer'],post['timer']) == (2,2,0,0) and pre['previous'] in (1,2)
                valid &= restore(r,c,branch) and (conserved_hold or conserved_ready)
                if conserved_hold:
                    restored_previous = post['previous']
            idle = [r for r in cohort if common.own_update(r) and r['begin_ns'] >= release and update(r, c, branch) and unheld(r) and \
                    (r['after']['current'], r['after']['next'], r['after']['timer'], r['after']['loaded'], r['after']['reserve']) == (2,2,0,c['loaded'],c['reserve']) and \
                    (not tails or r['end_ns'] >= tails[-1]['end_ns'])]
            valid &= bool(idle)
            # Every original after-shot snapshot conserves ammunition through readiness.
            valid &= all(r['after']['loaded'] == c['loaded'] and r['after']['reserve'] == c['reserve'] for r in cohort if r['end_ns'] >= shot_end)
            branches.append(dict(branch=branch, shot_invocations=[r['native_invocation'] for r in shots], hold_count=len(holds),
                                 restore_invocations=restorations, prehold_rewinds=rewinds, prehold_progressions=progressions,
                                 tail_invocations=[r['native_invocation'] for r in tails], idle_update_invocations=[r['native_invocation'] for r in idle]))
        gestures, gesture_detail = targets_and_custody(fixture, palette, c, times)
        matches = []
        if gestures:
            for row in hand.get('events', []):
                left, right = row.get('left') or {}, row.get('right') or {}
                if row.get('reason') == 'bolt_support_return' and row.get('support_holding') is True and positive(row.get('support_token')) and \
                   left.get('id') == gesture_detail['returned_support'] and right.get('id') == gesture_detail['returned_gun'] and \
                   (left.get('kind'),right.get('kind'),left.get('prerequisite')) == (2,1,right.get('id')) and \
                   left.get('item') == right.get('item') == row.get('physical_item') == lease['item'] and \
                   left.get('generation') == right.get('generation') == row.get('equip_generation') == lease['equipment_generation'] and \
                   row.get('actor') == n['soldier'] and row.get('weak') == n['weak'] and row.get('rig_epoch') == lease['actor_generation'] and \
                   row.get('space') == n['space'] and row.get('weapon') == n['weapon'] and row.get('raw_generation') in generations and \
                   gesture_detail['return_ns'] <= row.get('now_ns',0) <= times[16] and \
                   all(0 < claim.get('input_generation',0) <= row['raw_generation'] and row['now_ns'] < claim.get('deadline_ns',0) for claim in (left,right)):
                    matches.append(row['serial'])
        support_ok &= len(matches) == 1
        gesture_detail['ordinary_support_hand_events'] = matches
        native_ok &= valid; gesture_ok &= gestures
        cycle_details.append(dict(cycle=c['cycle'], native_complete=bool(valid), gesture_complete=bool(gestures), branches=branches, custody=gesture_detail))
    checks['all_three_original_shots_holds_restores_tails'] = bool(native_ok) and len(cycles) == count
    checks['all_applied_holds_accounted_for'] = hold_counts == native['held'] and all(hold_counts) and used_holds == {r['native_invocation'] for r in records if r.get('hold_applied') is True}
    checks['ordered_actual_gesture_and_custody_return'] = bool(gesture_ok) and len(cycles) == count
    checks['ordinary_support_return_after_custody'] = bool(support_ok) and len(cycles) == count
    first = next((r['now_ns'] for r in events if r['phase'] == 3), 0); last = max((r['now_ns'] for r in events if r['phase'] == 16), default=0)
    bounds = recording(flow, first, last)
    checks['bounded_trial_recording_complete'] = bounds['bounded_trial_complete'] and len(cycles) == count
    checks['external_samples_bracket_trial'] = bool(a and b) and a['monotonic_ns'] < first and last < after['samples'][0]['monotonic_ns']
    details.update(cycles=cycle_details, held_rows_per_branch=hold_counts, recording=bounds,
                   fixture=dict(phase=fixture['phase'],failure=fixture['failure'],requested=count,completed=fixture['completed']),
                   retained_completed_cycles_native_complete=bool(native_ok), retained_completed_cycles_gesture_complete=bool(gesture_ok),
                   retained_completed_cycles_support_complete=bool(support_ok), native_failure=native.get('failure'),native_failure_invocation=native.get('failure_invocation'))
    return dict(schema=SCHEMA, passed=all(checks.values()), checks=checks, details=details, recorder_complete=bounds['recorder_complete'],
                bounded_trial_complete=checks['bounded_trial_recording_complete'], headset_verified=False, normal_manual_bolt_admitted=False)


def audit(trace, receiver, before, after, completion, inputs):
    try:
        if not all(isinstance(d, dict) for d in (trace, receiver, before, after, completion)) or not isinstance(inputs, list):
            raise ValueError('Expected evidence objects and raw input rows')
        return audit_checked(trace, receiver, before, after, completion, inputs)
    except (KeyError, TypeError, ValueError, IndexError, AttributeError, OverflowError, struct.error) as error:
        return dict(schema=SCHEMA, passed=False, checks={'well_formed_evidence':False}, error=str(error), recorder_complete=False,
                    bounded_trial_complete=False, headset_verified=False, normal_manual_bolt_admitted=False)


def mechanism_status(trace, requested, ordinary=False):
    """Terminal runtime status only; callback/pose validation remains a separate audit."""
    result = dict(schema='fvr.bc2.physical_bolt_status.v1', runtime_completed=False,
                  callback_evidence_verified=False, headset_verified=False)
    try:
        game = trace['gameplay']; probe = game['ordinary_bolt_input_probe' if ordinary else 'physical_bolt_probe']
        native = game['reload_flow']['native_cycle_candidate']
        result.update(requested=requested, completed=probe['completed'],
                      fixture_phase=probe['phase'], fixture_failure=probe['failure'],
                      native_phase=native['phase'], native_failure=native['failure'],
                      native_failure_invocation=native.get('failure_invocation'))
        checks = dict(
            requested_cycles_match=type(requested) is int and requested in (1, 2) and probe['requested'] == requested,
            fixture_completed=probe['phase'] == 17 and probe['failure'] == 0 and probe['completed'] == requested,
            native_completed=native['enabled'] is True and native['phase'] == 5 and native['failure'] == 0 and native['blocks_fire'] is False,
            native_outcomes=native['releases'] == native['acknowledgements'] == requested,
            detached=trace.get('hooks_disabled') is True and game['reload_flow'].get('drained') is True and game['reload_flow'].get('in_flight') == 0)
        if ordinary:
            checks.update(input_only=probe['input_only'] is True,
                          ordinary_references=probe['references_ready'] is True and probe['selected_mode_ready'] is True,
                          original_body_startup=probe['startup_draw']['phase'] == 4 and probe['startup_draw']['failure'] == 0 and probe['startup_draw']['gun_claim'] > 0,
                          recorded_input_edges=probe['input_edge_drops'] == 0 and len(probe['input_edges']) >= requested * 4,
                          no_explicit_fixture='physical_bolt_probe' not in game)
        result.update(ordinary_player_input=ordinary, checks=checks, runtime_completed=all(checks.values()))
    except (KeyError, TypeError, AttributeError) as error:
        result.update(error=str(error), checks={'well_formed_status':False})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', type=Path, required=True); parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runtime-status', type=int, choices=(1, 2),
                        help='Check terminal runtime status for this requested cycle count; does not audit callbacks')
    parser.add_argument('--ordinary-input', action='store_true', help='Require the ordinary player input route for runtime status')
    args = parser.parse_args()
    if args.ordinary_input and args.runtime_status is None:
        parser.error('--ordinary-input requires --runtime-status; it is not the finite-fixture callback audit')
    if args.output.exists():
        raise ValueError('Preserve earlier audit evidence; choose a new output file')
    if args.runtime_status is not None:
        source = args.run/'native-trace.json'
        try:
            result = mechanism_status(json.loads(source.read_text(encoding='utf-8-sig')), args.runtime_status, args.ordinary_input)
            result['trace_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
        except (OSError, ValueError) as error:
            result = dict(runtime_completed=False, callback_evidence_verified=False, headset_verified=False, error=str(error))
        args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(result))
        return 0 if result['runtime_completed'] else 2
    names = ('native-trace.json', 'receiver/result.json', 'before.json', 'after.json', 'completion.json', 'receiver/physical-bolt-input.jsonl')
    paths = [args.run/n for n in names]
    try:
        docs = [json.loads(p.read_text(encoding='utf-8-sig')) for p in paths[:-1]]
        inputs = [json.loads(line) for line in paths[-1].read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        result = audit(*docs, inputs)
    except (OSError, ValueError) as error:
        result = dict(schema=SCHEMA, passed=False, checks={'evidence_files_available':False}, error=str(error), recorder_complete=False,
                      bounded_trial_complete=False, headset_verified=False, normal_manual_bolt_admitted=False)
    result['source_sha256'] = {name:hashlib.sha256(path.read_bytes()).hexdigest() for name, path in zip(names, paths) if path.is_file()}
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(passed=result['passed'], failed=[k for k,v in result['checks'].items() if not v], output=str(args.output))))
    return 0 if result['passed'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
