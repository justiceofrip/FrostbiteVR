"""Join derived BC2 metadata into reload calibration jobs; no asset/process access.

Native reload enum, physical work-plan family, authored animation channels and
current native capabilities are deliberately separate fields. No output enables
a feature, infers chamber contents, or exports game binary/vertex/curve arrays.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

SCHEMA = 'fvr.bc2.manual_reload_catalog'
FAMILIES = {'detachable_magazine', 'belt_feed', 'bolt_magazine', 'tube_pump',
            'revolver_cylinder', 'single_round_launcher', 'en_bloc_clip', 'unassigned'}
LIMITS = [
    'Authored native reload type is not a physical container classification.',
    'Physical families from the work plan remain proposals, not measured geometry.',
    'Animation names and archive cooccurrence identify inspection candidates, not handle/bolt roles.',
    'Decoded motion is authored animation evidence, never native state, chamber or ammunition authority.',
    'No current actor, equipment identity, rig selection, renderer or native operation is admitted.',
    'The common configuration archive is not guaranteed to contain every installed weapon variant.',
]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()


def text(value, label):
    if not isinstance(value, str) or not value or '\x00' in value:
        raise ValueError(f'{label}: nonempty text required')
    return value


def resource(value):
    value = text(value, 'resource').replace('\\', '/')
    if value.startswith('/') or ':' in value or '..' in PurePosixPath(value).parts:
        raise ValueError('resource must be an archive-relative path')
    return value


def sha(value):
    if not isinstance(value, str) or not re.fullmatch('[0-9a-fA-F]{64}', value):
        raise ValueError('SHA256 required')
    return value.lower()


def document(value, schema, list_key):
    if not isinstance(value, dict) or value.get('schema') != schema or value.get('schema_version') != 1:
        raise ValueError(f'unsupported {schema} schema')
    if not isinstance(value.get(list_key), list):
        raise ValueError(f'{list_key} array required')
    return value[list_key]


def weapon_key(row):
    return (row.get('origin_archive', '').casefold(), row.get('origin_index_sha256', ''),
            resource(row['resource']).casefold(), sha(row['resource_sha256']), text(row['instance_guid'], 'GUID').casefold())


def source_pair(config, bindings, inventory_hash):
    configurations = document(config, 'fvr.bc2.authored_weapon_configuration', 'resolved_weapons')
    rows = document(bindings, 'fvr.bc2.authored_weapon_mesh_bindings', 'weapons')
    if inventory_hash is not None and sha(bindings['inventory_sha256']) != sha(inventory_hash):
        raise ValueError('mesh binding belongs to a different inventory snapshot')
    origin = {}
    if 'archive' in config:
        origin = dict(origin_archive=resource(config['archive']), origin_index_sha256=sha(config['index_sha256']))
        if ('archive' in bindings and resource(bindings['archive']).casefold() != origin['origin_archive'].casefold() or
            'archive_index_sha256' in bindings and sha(bindings['archive_index_sha256']) != origin['origin_index_sha256']):
            raise ValueError('binding archive does not match configuration archive')
    return [dict(r, **origin) for r in configurations], [dict(r, **origin) for r in rows]


DISPATCH_FIELDS = ('FireLogic.FireLogicType', 'FireLogic.ReloadType', 'FireLogic.FireInputAction',
    'FireLogic.ReloadInputAction', 'FireLogic.BoltAction.HoldBoltActionUntilFireRelease',
    'FireLogic.BoltAction.HoldBoltActionUntilZoomRelease')
TIMING_FIELDS = ('FireLogic.ReloadTime', 'FireLogic.ReloadDelay', 'FireLogic.ReloadLogic',
    'FireLogic.ReloadTimeBulletsLeft', 'FireLogic.ReloadThreshold', 'FireLogic.PostReloadSequenceTime',
    'FireLogic.BoltAction.BoltActionDelay', 'FireLogic.BoltAction.BoltActionTime', 'FireLogic.RateOfFire')


def unique(rows, key, label):
    result = {}
    for row in rows:
        k = key(row)
        if k in result:
            raise ValueError(f'duplicate {label}')
        result[k] = row
    return result


def fields(row):
    result = {}
    for name, item in row.get('fields', {}).items():
        if not isinstance(item, dict) or 'value' not in item:
            raise ValueError('typed field requires value')
        if name.startswith(('FireLogic.', 'Ammo.', 'Shot.')):
            result[name] = dict(value=item['value'], encoding=item.get('encoding'), offset=item.get('offset'))
    canonical(result)  # Reject NaN/Infinity; retain actual decoded numeric types.
    return result


def value(row, name):
    return row.get('fields', {}).get(name, {}).get('value')


def native_strategy(row):
    return {'rtMagazine': 'pooled_magazine_transfer', 'rtSingleBullet': 'single_round_transfer'}.get(
        value(row, 'FireLogic.ReloadType'), 'unknown')


def animation_hints(name):
    # Lexical discovery hints ONLY. The file is not said to contain that motion.
    base = PurePosixPath(resource(name)).stem.casefold()
    return [label for label, token in [('reload', 'reload'), ('bolt', 'bolt'), ('charge', 'charg'),
                                     ('pump', 'pump'), ('rechamber', 'rechamber'), ('deploy', 'deploy'),
                                     ('grip_reference', 'ikpose')] if token in base]


def animation_evidence(batches):
    result = {}
    for batch in batches:
        for clip in document(batch, 'fvr.bc2.authored_animation_batch', 'clips'):
            archive = resource(clip.get('archive_relative', clip['archive']))
            index = sha(clip['archive_index_sha256'])
            name = resource(clip['resource'])
            key = (archive.casefold(), index, name.casefold())
            if key in result:
                raise ValueError('duplicate decoded animation observation')
            status = clip.get('status')
            if status not in ('decoded', 'partial', 'unsupported'):
                raise ValueError('unknown animation decode status')
            tracks = []
            for track in clip.get('tracks', []):
                motion = track.get('motion_status')
                if motion not in ('constant_controls', 'varying_controls_not_mechanism_proof', 'unsupported'):
                    raise ValueError('unknown authored track motion status')
                tracks.append(dict(name=text(track['name'], 'track name'),
                    position_curve_type=track.get('position_curve_type'),
                    orientation_curve_type=track.get('orientation_curve_type'),
                    scale_curve_type=track.get('scale_curve_type'),
                    payload_decoded=track.get('payload_decoded') is True, motion_status=motion))
            if len({t['name'] for t in tracks}) != len(tracks):
                raise ValueError('ambiguous named animation track')
            observation = dict(resource_sha256=sha(clip['resource_sha256']), status=status,
                duration_seconds=clip.get('duration_seconds'), tracks=tracks,
                native_state_proof=False, active_mesh_proof=False, mechanism_roles=[])
            canonical(observation)
            result[key] = observation
    return result


def hand_pose_evidence(batch):
    if batch is None:
        return {}
    if batch.get('schema') != 'fvr.bc2.authored_hand_pose_batch.v1' or not isinstance(batch.get('clips'), list):
        raise ValueError('unsupported authored hand pose batch')
    result = {}
    for clip in batch['clips']:
        key = (resource(clip['resource']).casefold(), sha(clip['sha256']))
        if key in result:
            raise ValueError('duplicate authored hand pose version')
        hands = clip.get('hands', {})
        result[key] = dict(status=clip.get('status'),
            sources=[dict(archive=resource(s['archive']), index_sha256=sha(s['index_sha256'])) for s in clip.get('sources', [])],
            hand_evaluation_status=hands.get('evaluation_status'),
            all_required_controls_constant=hands.get('all_required_controls_constant') is True,
            skeleton_resource=batch.get('skeleton', {}).get('resource'),
            skeleton_sha256=batch.get('skeleton', {}).get('resource_sha256'),
            rig_fingerprint=batch.get('skeleton', {}).get('rig_fingerprint'),
            transform_payload_in_separate_private_hand_pose_batch=True,
            active_native_mesh_binding=False, runtime_admission=False)
    return result


def part_summary(variant):
    rows = []
    for lod in variant.get('lods', []):
        for part in lod.get('candidate_parts', []):
            rows.append(dict(lod=lod['lod'], bone_hash=part.get('bone_hash'), name=part.get('candidate_name'),
                             sections=part.get('sections', []), role='unassigned_weighted_part',
                             vertex_influences=part.get('vertex_influences'),
                             single_weight=part.get('all_vertices_single_weight'),
                             coordinate_space='asset_bind', minimum=part.get('minimum'), maximum=part.get('maximum')))
    return rows


def catalog(config, bindings, inventory, plan, *, inventory_hash=None, common_metadata=(), animation_batches=(), hand_poses=None):
    configs, bound_rows = source_pair(config, bindings, inventory_hash)
    for common in common_metadata:
        if common.get('schema') not in ('fvr.bc2.lmg_common_metadata.v1', 'fvr.bc2.common_weapon_metadata.v1') or common.get('read_only') is not True or common.get('exported_assets') is not False:
            raise ValueError('unsupported common archive metadata')
        more, more_bound = source_pair(common['configuration'], common['mesh_bindings'], inventory_hash)
        configs.extend(more)
        bound_rows.extend(more_bound)
    bound = unique(bound_rows, weapon_key, 'mesh configuration binding')
    assets = unique(document(inventory, 'fvr.bc2.installed_weapon_inventory', 'asset_profiles'),
                    lambda a: resource(a['resource']).casefold(), 'mesh resource')
    plans = document(plan, 'fvr.weapon_family_work_plan', 'items')
    config_index = unique(configs, weapon_key, 'authored configuration')
    archives = unique(inventory.get('archives', []), lambda a: resource(a['archive']).casefold(), 'archive')
    decoded = animation_evidence(animation_batches)
    pose_evidence = hand_pose_evidence(hand_poses)
    if inventory_hash is not None:
        for batch in animation_batches:
            if 'inventory_sha256' in batch and sha(batch['inventory_sha256']) != sha(inventory_hash):
                raise ValueError('animation metadata belongs to a different inventory snapshot')
    used_decoded = set()
    proposals = defaultdict(list)
    for p in plans:
        family = p.get('family_proposal')
        if family not in FAMILIES:
            raise ValueError('unsupported work-plan family')
        for name in p.get('exact_mesh_names', []):
            proposals[PurePosixPath(resource(name)).name.casefold()].append(dict(
                label=text(p['label'], 'label'), family=family, action_variant=p.get('action_variant'),
                status='existing_work_plan_proposal_not_measured'))

    mesh_rows = []
    mesh_index = {}
    for key, asset in sorted(assets.items()):
        variants = []
        for variant in asset.get('geometry_variants', []):
            mesh_hash = sha(variant['sha256'])
            variants.append(dict(mesh_sha256=mesh_hash, parts=part_summary(variant),
                                 lods=[dict(lod=l['lod'], data_resource=resource(l['data_resource']),
                                            data_sha256=sha(l['data_sha256'])) for l in variant.get('lods', [])]))
        if len({v['mesh_sha256'] for v in variants}) != len(variants):
            raise ValueError('duplicate mesh variant')
        clips = {}
        source_gaps = []
        for source in asset.get('sources', []):
            archive_name = resource(source['archive'])
            archive = archives.get(archive_name.casefold())
            if archive is None:
                source_gaps.append(dict(archive=archive_name, reason='archive_metadata_missing'))
                continue
            if sha(archive['index_sha256']) != sha(source['index_sha256']):
                raise ValueError('mesh source archive index differs')
            for clip in archive.get('animations', []):
                name = resource(clip['name'])
                hints = animation_hints(name)
                if not hints:
                    continue
                entry = clips.setdefault(name.casefold(), dict(resource=name, kind=clip.get('kind'), hints=hints,
                    association='cooccurs_in_exact_mesh_archive_not_verified_animtree', archives=[],
                    decoded=False, motion_status='not_evaluated', track_names=[], mechanism_roles=[]))
                occurrence = dict(archive=archive_name, index_sha256=sha(archive['index_sha256']),
                                  kind=clip.get('kind'), bytes=clip.get('bytes'))
                evidence_key = (archive_name.casefold(), occurrence['index_sha256'], name.casefold())
                if evidence_key in decoded:
                    occurrence['authored_decode'] = decoded[evidence_key]
                    pose = pose_evidence.get((name.casefold(), decoded[evidence_key]['resource_sha256']))
                    if pose and any(p['archive'].casefold() == archive_name.casefold() and
                                    p['index_sha256'] == occurrence['index_sha256'] for p in pose['sources']):
                        occurrence['authored_hand_pose'] = pose
                    used_decoded.add(evidence_key)
                    # Per-archive decoded provenance is authoritative; summaries
                    # never erase unsupported/different content variants.
                    entry['decoded'] |= decoded[evidence_key]['status'] == 'decoded'
                    entry['motion_status'] = 'see_exact_archive_observations'
                    entry['track_names'] = sorted(set(entry['track_names']) |
                        {t['name'] for t in decoded[evidence_key]['tracks']})
                entry['archives'].append(occurrence)
        for clip in clips.values():
            clip['archives'] = sorted(clip['archives'], key=lambda x: x['archive'])
        row = dict(resource=asset['resource'], geometry_variants=variants,
                   family_proposals=proposals.get(PurePosixPath(key).name, []),
                   animation_candidates=sorted(clips.values(), key=lambda x: x['resource']),
                   source_gaps=source_gaps, configurations=[], runtime_enabled=False)
        mesh_rows.append(row)
        mesh_index[key] = row

    weapon_rows = []
    calibration = defaultdict(list)
    missing = []
    for key, row in sorted(config_index.items()):
        identifier = 'bc2:configuration:' + digest(canonical(key))[:24]
        b = bound.get(key)
        meshes = []
        gaps = []
        if b is None:
            gaps.append('exact_configuration_to_mesh_binding_missing')
        else:
            for state in b.get('states', []):
                for ref in state.get('meshes', []):
                    entry = dict(state=state['index'], reference=ref.get('reference'), status=ref.get('status'),
                                 geometry_status=ref.get('geometry_status'), resource=ref.get('mesh_resource'))
                    if ref.get('mesh_resource'):
                        mk = resource(ref['mesh_resource']).casefold()
                        asset = mesh_index.get(mk)
                        observed = {sha(x['mesh_sha256']) for x in ref.get('geometry_variants', [])}
                        available = {x['mesh_sha256'] for x in asset['geometry_variants']} if asset else set()
                        if not observed or not observed <= available:
                            entry['join_status'] = 'geometry_unavailable_or_hash_mismatch'
                            gaps.append('exact_geometry_unavailable')
                        else:
                            entry['join_status'] = 'exact_resource_and_geometry_hash'
                            entry['mesh_sha256'] = sorted(observed)
                            asset['configurations'].append(identifier)
                            for mh in sorted(observed):
                                calibration[(mk, mh)].append(dict(configuration=identifier, state=state['index']))
                    else:
                        entry['join_status'] = 'authored_mesh_unresolved'
                    meshes.append(entry)
        strategy = native_strategy(row)
        exceptions = ['native_enum_does_not_identify_physical_container']
        if row.get('weapon_class') == 'wcUgl':
            exceptions.append('underbarrel_mode_can_reference_parent_rifle_mesh_do_not_inherit_rifle_reload')
        if row.get('weapon_class') == 'wcLmg':
            exceptions.append('lmg_category_can_include_belt_or_magazine_feed')
        if row.get('weapon_class') == 'wcShotgun':
            exceptions.append('shotgun_category_can_include_tube_or_detachable_magazine')
        if value(row, 'Ammo.MagazineCapacity') in (-1, 0, 1):
            exceptions.append('unlimited_empty_or_single_capacity_does_not_prove_container_or_reusability')
        physical_proposals = []
        for mesh in meshes:
            if mesh.get('join_status') == 'exact_resource_and_geometry_hash':
                for proposal in mesh_index[mesh['resource'].casefold()]['family_proposals']:
                    physical_proposals.append(dict(**proposal, mesh_resource=mesh['resource'],
                        applies_to_selected_mode=False if row.get('weapon_class') == 'wcUgl' else None))
        result = dict(id=identifier, native_name=row['native_name'], weapon_class=row.get('weapon_class'),
            origin_archive=row.get('origin_archive'), origin_index_sha256=row.get('origin_index_sha256'),
            resource=row['resource'], resource_sha256=row['resource_sha256'], instance_guid=row['instance_guid'],
            firing_resource=row.get('firing_resource'), firing_sha256=row.get('firing_sha256'), firing_guid=row.get('firing_guid'),
            function_resource=row.get('function_resource'), function_sha256=row.get('function_sha256'),
            function_guid=row.get('function_guid'), native_strategy=strategy, fields=fields(row),
            authored_states=row.get('weapon_states', []), meshes=meshes, exceptions=exceptions,
            physical_family_proposals=physical_proposals,
            physical_family_status='unassigned_from_native_configuration', chamber_state='unknown',
            runtime_enabled=False, gaps=sorted(set(gaps)))
        weapon_rows.append(result)
        if gaps:
            missing.append(identifier)
    # Installed resources missing from the common config archive still need
    # actionable jobs. They must not vanish merely because a join is absent.
    for key, asset in mesh_index.items():
        for variant in asset['geometry_variants']:
            calibration.setdefault((key, variant['mesh_sha256']), [])
    jobs = []
    for (mesh, mesh_hash), members in sorted(calibration.items()):
        asset = mesh_index[mesh]
        jobs.append(dict(id='bc2:reload-calibration:' + digest(canonical([mesh, mesh_hash]))[:24],
            mesh_resource=asset['resource'], mesh_sha256=mesh_hash,
            configurations=sorted(members, key=lambda m: (m['configuration'], m['state'])),
            shared_data=['exact_mesh_parts', 'authored_animation_tracks', 'rig_fingerprint', 'part_coordinate_conversion'],
            configuration_binding='exact_authored_join' if members else 'missing_from_current_configuration_snapshot',
            independently_required=['mode_specific_active_mesh_provenance', 'supply_or_feed_part_role',
                'anatomical_grasp', 'insertion_frame_and_travel', 'native_family_configuration_and_current_owner'],
            execute_automatically=False, runtime_enabled=False))
    dispatch = {}
    for weapon in weapon_rows:
        shape = {name: weapon['fields'].get(name, {}).get('value') for name in DISPATCH_FIELDS}
        key = canonical(shape)
        group = dispatch.setdefault(key, dict(id='bc2:native-reload-family:' + digest(key)[:24],
            dispatch_fields=shape, missing_dispatch_fields=[k for k, v in shape.items() if v is None],
            implementation_proof_scope='executable_wide_dispatch_not_weapon_name',
            runtime_admitted=False, configurations=[]))
        group['configurations'].append(dict(configuration=weapon['id'],
            timing_fields={name: weapon['fields'].get(name) for name in TIMING_FIELDS},
            authored_capacity=weapon['fields'].get('Ammo.MagazineCapacity'),
            physical_family_proposals=weapon['physical_family_proposals']))
        weapon['native_family_group'] = group['id']
    group_by_config = {w['id']: w['native_family_group'] for w in weapon_rows}
    for job in jobs:
        job['native_family_groups'] = sorted({group_by_config[m['configuration']] for m in job['configurations']})
    return dict(schema=SCHEMA, schema_version=1, derived_metadata_only=True, exported_assets=False,
        configurations=weapon_rows, meshes=mesh_rows, calibration_jobs=jobs,
        native_family_groups=sorted(dispatch.values(), key=lambda g: g['id']),
        unmatched_animation_observations=[dict(archive=k[0], index_sha256=k[1], resource=k[2])
            for k in sorted(set(decoded) - used_decoded)],
        unresolved_configurations=missing, unbound_meshes=[m['resource'] for m in mesh_rows if not m['configurations']],
        summary=dict(configurations=len(weapon_rows), meshes=len(mesh_rows), calibration_jobs=len(jobs),
                     native_strategies=dict(sorted(Counter(w['native_strategy'] for w in weapon_rows).items())),
                     named_animation_candidates=sum(len(m['animation_candidates']) for m in mesh_rows),
                     joined_decoded_animation_observations=len(used_decoded),
                     native_family_groups=len(dispatch), runtime_enabled=0), limits=LIMITS)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('config', 'bindings', 'inventory', 'plan', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--common-metadata', type=Path, action='append', default=[],
                        help='Additional bounded common archive extraction; origins remain separate.')
    parser.add_argument('--animations', type=Path, action='append', default=[],
                        help='Shared animation decoder metadata, using exact relative archive/index/resource provenance.')
    parser.add_argument('--hand-poses', type=Path, help='Optional exact resource/content version authored hand-pose batch.')
    args = parser.parse_args(argv)
    blobs = {name: getattr(args, name).read_bytes() for name in ('config', 'bindings', 'inventory', 'plan')}
    data = {name: json.loads(blob) for name, blob in blobs.items()}
    common_blobs = [p.read_bytes() for p in args.common_metadata]
    animation_blobs = [p.read_bytes() for p in args.animations]
    hand_blob = args.hand_poses.read_bytes() if args.hand_poses else None
    result = catalog(**data, inventory_hash=digest(blobs['inventory']), common_metadata=[json.loads(b) for b in common_blobs],
                     animation_batches=[json.loads(b) for b in animation_blobs], hand_poses=json.loads(hand_blob) if hand_blob else None)
    result['input_sha256'] = {name: digest(blob) for name, blob in blobs.items()}
    result['input_sha256']['common_metadata'] = [digest(b) for b in common_blobs]
    result['input_sha256']['animations'] = [digest(b) for b in animation_blobs]
    result['input_sha256']['hand_poses'] = digest(hand_blob) if hand_blob else None
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    print(json.dumps(result['summary'], sort_keys=True))


if __name__ == '__main__':
    main()
