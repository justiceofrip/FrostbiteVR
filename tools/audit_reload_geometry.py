"""Offline reload geometry evidence; never assigns ammo/socket roles from motion.

Uses native authored matrices only. No game/process access and no runtime profile
export. All attachment measurements remain unresolved semantic candidates.
"""
from __future__ import annotations
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import numpy as np
import weapon_profile_pipeline as profiles
import weapon_mechanism_pipeline as mechanisms


def identity(row):
    keys = ('asset_name', 'skeleton', 'actor', 'weapon', 'owner_generation', 'space', 'capture_episode')
    values = tuple(row.get(key) for key in keys)
    if any(value is None or value == '' or value == 0 for value in values):
        raise ValueError('complete asset, rig, actor/item/space and episode identity required')
    return values + (profiles.pose_asset_provenance(row)["cohort"],)


def reload_windows(trace):
    """Only matched native update records establish a reload interval."""
    groups = defaultdict(list)
    for record in trace.get('gameplay', {}).get('reload_flow', {}).get('records', []):
        if record.get('kind') != 0 or not record.get('finished') or not record.get('identity_retained'):
            continue
        before, after = record.get('before'), record.get('after')
        if not before or not after:
            continue
        key = tuple(before.get(k) for k in ('soldier', 'weapon', 'actor_generation', 'space'))
        groups[key].append(record)
    windows = {}
    for key, records in groups.items():
        active = [r for r in records if r['before']['current'] in (10, 11, 12) or r['after']['current'] in (10, 11, 12)]
        if not active:
            continue
        start = min(r['begin_tick_ms'] for r in active)
        end = max(r['end_tick_ms'] for r in active)
        ready = [r['end_tick_ms'] for r in records if r['end_tick_ms'] >= end and r['before']['current'] == 1 and r['after']['current'] == 2]
        if ready:
            end = min(ready)
        windows[key] = (start, end)
    return windows


def stable_runs(samples, tolerance_m=.003, tolerance_degrees=3., minimum_span_ms=150):
    """Find contiguous observed rigid wrist relations; never average a pose."""
    runs = []
    start = 0
    while start < len(samples):
        stop = start + 1
        first = samples[start]
        while stop < len(samples):
            previous, current = samples[stop-1], samples[stop]
            if current['row'] != previous['row'] + 1 or not 0 < current['captured_ms'] - previous['captured_ms'] <= 250:
                break
            position, angle = profiles.distance(first['matrix'], current['matrix'])
            if position > tolerance_m or angle > tolerance_degrees:
                break
            stop += 1
        if stop-start >= 3 and samples[stop-1]['captured_ms']-first['captured_ms'] >= minimum_span_ms:
            runs.append({'first_row':first['row'], 'last_row':samples[stop-1]['row'],
                         'sample_count':stop-start, 'span_ms':samples[stop-1]['captured_ms']-first['captured_ms'],
                         'observed_bone_from_wrist':first['matrix'].reshape(-1).tolist(),
                         'semantic_role':None, 'verified_attachment':False})
            start = stop
        else:
            start += 1
    return runs


def motion(values):
    if not values:
        return None
    first = values[0]
    deltas = [profiles.distance(first, value) for value in values]
    return {'sample_count':len(values),
            'maximum_translation_from_first_m':max(d[0] for d in deltas),
            'maximum_rotation_from_first_degrees':max(d[1] for d in deltas),
            'first_observed_matrix':first.reshape(-1).tolist()}


def analyze(trace, source=None, asset=None, explicit_window=None):
    publication = trace.get('gameplay', {}).get('rig_publication', {})
    rows = publication.get('weapon_profile_samples', [])
    windows = reload_windows(trace)
    groups = defaultdict(list)
    issues = []
    for index, row in enumerate(rows):
        if asset is not None and row.get('asset_name') != asset:
            continue
        try:
            key = identity(row)
            if row.get('attachment_pending') is not False:
                continue
            stamp = row.get('captured_ms')
            if isinstance(stamp, bool) or not isinstance(stamp, (int, float)) or not math.isfinite(stamp):
                raise ValueError('finite capture timestamp required')
            window = explicit_window or windows.get((row['actor'], row['weapon'], row['owner_generation'], row['space']))
            if window is None or not window[0] <= stamp <= window[1]:
                continue
            root, bones = mechanisms.snapshot(row, profiles.Policy())
            units = row['units_per_meter']
            weapon = profiles.matrix(row['native'])
            wrist_rel = {}
            for side in ('left', 'right'):
                wrist = profiles.matrix(row['native_'+side+'_wrist']) @ np.linalg.inv(weapon)
                wrist[3,:3] /= units
                wrist_rel[side] = profiles.matrix(wrist)
            sequence = row.get('capture_sequence')
            if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence <= 0:
                raise ValueError('positive capture_sequence required')
            groups[key].append({'row':index, 'sequence':sequence, 'captured_ms':stamp,
                                'bones':bones, 'wrists':wrist_rel, 'root':root})
        except (ValueError, TypeError, KeyError, np.linalg.LinAlgError) as exc:
            issues.append({'row':index, 'reason':str(exc)})
    output = []
    for key, frames in groups.items():
        accepted = []
        topology = None
        last_sequence = -1
        for frame in frames:
            current = sorted((name, b['parent_name']) for name,b in frame['bones'].items())
            if frame['sequence'] <= last_sequence or (topology is not None and current != topology):
                issues.append({'row':frame['row'], 'reason':'sequence rollback/duplicate or topology changed within identity'})
                continue
            last_sequence = frame['sequence'];topology = current;accepted.append(frame)
        if not accepted:
            continue
        summaries = []
        for name in sorted(accepted[0]['bones']):
            timeline = []
            hidden = 0
            for frame in accepted:
                bone = frame['bones'][name]
                if bone['hidden']:
                    hidden += 1
                    continue
                world = bone['transform']
                left = profiles.matrix(world @ np.linalg.inv(frame['wrists']['left']))
                right = profiles.matrix(world @ np.linalg.inv(frame['wrists']['right']))
                timeline.append({'row':frame['row'], 'captured_ms':frame['captured_ms'],
                                 'weapon':world, 'left':left, 'right':right})
            measured = motion([r['weapon'] for r in timeline])
            moving = bool(measured and (measured['maximum_translation_from_first_m']>.01 or measured['maximum_rotation_from_first_degrees']>3))
            item = {'name':name, 'parent_name':accepted[0]['bones'][name]['parent_name'],
                    'semantic_role':None, 'mesh_identity':None, 'native_profile_verified':False,
                    'hidden_samples':hidden, 'weapon_relative':measured,
                    'left_wrist_relative':motion([r['left'] for r in timeline]),
                    'right_wrist_relative':motion([r['right'] for r in timeline]),
                    'moves_during_reload':moving,
                    'left_relation_runs':stable_runs([{'row':r['row'],'captured_ms':r['captured_ms'],'matrix':r['left']} for r in timeline]) if moving else []}
            if moving:
                item['native_trajectory']=[{'row':r['row'],'captured_ms':r['captured_ms'],
                    'bone_from_weapon':r['weapon'].reshape(-1).tolist(),
                    'bone_from_left_wrist':r['left'].reshape(-1).tolist()} for r in timeline]
            summaries.append(item)
        output.append({'asset_name':key[0], 'skeleton':key[1], 'actor':key[2], 'weapon':key[3],
                       'owner_generation':key[4], 'space':key[5], 'capture_episode':key[6],
                       'samples':len(accepted), 'first_ms':accepted[0]['captured_ms'], 'last_ms':accepted[-1]['captured_ms'],
                       'pose_asset_binding_verified':False,'usable_as_verified_grasp':False,
                       'capture_provenance':json.loads(key[7]),
                       'root':accepted[0]['root'], 'bones':summaries})
    return {'schema':'fvr.reload_geometry_evidence', 'schema_version':1, 'source':source,
            'coordinate_convention':'row-vector canonical matrices, translations in metres',
            'window_source':'explicit_capture_bounds' if explicit_window else 'matched_native_reload_update_states',
            'explicit_window_ms':list(explicit_window) if explicit_window else None,
            'pose_asset_binding_verified':False,'usable_as_verified_grasp':False,
            'asset_label_scope':'selected configuration; sampled rig not bound to submitted mesh',
            'native_windows':[{'owner_key':list(k),'begin_ms':v[0],'end_ms':v[1]} for k,v in windows.items()],
            'groups':output, 'issues':issues, 'native_insertion_profile':None,
            'missing_evidence':['Ammo mesh/prop identity and skin-section/bone mapping',
              'Actual item landmark and hand grip role (including animated fingers)',
              'Verified insertion socket frame, axis/travel and seated item pose',
              'Physical ammo resource identity/generation and native stage authority'],
            'limitations':['Bone motion or wrist correlation does not establish a shell, magazine or socket role.',
              'A stable authored relation is an observation, not an installed profile or headset acceptance.',
              'A reload animation trajectory is not by itself the physical insertion rail.']}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--asset')
    parser.add_argument('--window-ms',type=float,nargs=2,metavar=('BEGIN','END'))
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    if args.window_ms and (not all(math.isfinite(x) and x>=0 for x in args.window_ms) or args.window_ms[1]<=args.window_ms[0]):
        parser.error('window requires finite increasing nonnegative bounds')
    trace=profiles.load_json(args.native)
    result=analyze(trace,{'path':str(args.native.resolve()),'sha256':profiles.digest(args.native)},args.asset,args.window_ms)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    print(json.dumps({'groups':len(result['groups']),'issues':len(result['issues']),'native_insertion_profile':None}))
    return 1 if result['issues'] else 0


if __name__=='__main__':
    raise SystemExit(main())