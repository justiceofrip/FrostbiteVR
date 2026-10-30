"""Bounded root-owned completion of exact firearm display and supply caches.

Planning reads reviewed metadata and the existing cache only. Installed extraction
must be launched by the coordinator after StorageBudget admission. Public outputs
contain metadata; BC2PROP1 geometry stays in the private output directory.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import contextlib

import bc2_body_ammo_assets as ammo
import bc2_configured_body_assets as body
import bc2_firearm_magazine_batch as batch
import bc2_body_equipment_assets as equipment
import bc2_authored_magazine_policy as policy
import bc2_visibility_descriptors as visibility
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical, identity
from bc2_mesh_geometry import metadata, geometry
from bc2_magazine_contact_batch import ArchivePool
from inspect_bc2_mesh_asset import bone_hash, sha
from bc2_asset_resource_identity import profiles_match

SCHEMA = 'fvr.bc2.firearm_cache_completion.v1'
BASELINE_SHA = '2e420c02ea639cd2b1087e762dca25fc16ae62db6f6cea7e1154e97cf3faf749'
MAX_ROWS = 512
MAX_BYTES = 64 * 1024 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def digest(value):
    return batch.descriptors.digest(value)


def cache_key(row):
    return tuple(row[k] for k in ('asset', 'mesh', 'part', 'rig_fingerprint'))


def config_key(row):
    return row['resource'], row.get('sha256', row.get('resource_sha256')), row['instance_guid'].lower()


def roster_key(row):
    return row['configuration'] + '.dbx', row['identity'][1], row['identity'][2].lower()


def record_slices(payload, rows):
    """Validate record framing and preserve every existing byte, including padding."""
    require(payload[:8] == ammo.MAGIC and len(payload) <= MAX_BYTES, 'Cache magic/byte bound')
    require(len(rows) <= MAX_ROWS and struct.unpack_from('<I', payload, 8)[0] == len(rows), 'Cache count differs')
    at = 12
    result = []

    def string():
        nonlocal at
        require(at + 2 <= len(payload), 'Truncated cache string')
        size = struct.unpack_from('<H', payload, at)[0]
        at += 2
        require(0 < size <= 512 and at + size <= len(payload), 'Cache string bound')
        value = payload[at:at + size].decode('ascii')
        at += size
        return value

    for row in rows:
        start = at
        require(tuple(string() for _ in range(3)) == cache_key(row)[:3], 'Cache/catalog key differs')
        require(at + 12 <= len(payload), 'Truncated cache record')
        rig, count = struct.unpack_from('<QI', payload, at)
        at += 12
        require(rig == row['rig_fingerprint'] and count == len(row['sections']), 'Cache rig/sections differ')
        for section in row['sections']:
            require(string() == section['section'], 'Cache section name differs')
            require(at + 20 <= len(payload), 'Truncated cache section')
            nv, ni, index_size, vo, io = struct.unpack_from('<5I', payload, at)
            at += 20
            require(index_size in (2, 4) and ni % index_size == 0 and vo <= nv and io <= ni,
                    'Cache section ranges differ')
            require(at + nv + ni <= len(payload), 'Truncated cache geometry')
            at += nv + ni
        result.append(payload[start:at])
    require(at == len(payload), 'Trailing cache bytes')
    return result


def collect(root, recovery):
    paths = dict(
        roster=root/'handoff/tasks/WEAPONS/20261030-roster43/roster.json',
        baseline=root/'profiles/weapons45-magazines/body-ammo-assets.json',
        shotgun=root/'handoff/tasks/WEAPONS/20261030-shotgun44/equipment.json',
        samples=root/'handoff/tasks/WEAPONS/20261030-shotgun44/samples.json',
        fixed=root/'profiles/weapons46-special/body-ammo-assets.json',
        fixed_geometry=root/'profiles/weapons46-special/geometry.json',
        fixed_native=root/'profiles/weapons46-special/exact-native.json',
        actions=recovery/'action379-batched/actions.json',
        catalog=recovery/'manual-reload-catalog-20261003-candidate/manual-reload-catalog.json',
        inventory=recovery/'mesh312-research/inventory.json')
    sources = {k: batch.pin(v) for k, v in paths.items()}
    docs = {k: read(v) for k, v in paths.items()}
    bindings = {}
    for index, pair in enumerate(docs['actions']['inputs']):
        sources['bindings_' + str(index)] = pair['bindings']
        for binding in batch.read_pin(pair['bindings'])['profiles']:
            bindings.setdefault(config_key(binding['weapon']), binding)
    return sources, docs, bindings


def prepare(root, recovery, baseline_cache):
    sources, docs, bindings = collect(root, recovery)
    baseline = docs['baseline']['profiles']
    payload = baseline_cache.read_bytes()
    require(len(baseline) == 204 and hashlib.sha256(payload).hexdigest() == BASELINE_SHA,
            'Reviewed204 baseline cache changed')
    record_slices(payload, baseline)
    existing = {cache_key(row) for row in baseline}
    configurations = {config_key(c): c for c in docs['catalog']['configurations']}
    for c in docs['fixed_native']['records']:
        configurations.setdefault(config_key(c), c)
    missing = []
    covered = []
    for roster in docs['roster']['rows']:
        key = roster_key(roster)
        found = [r for r in baseline if r['asset'] == roster['asset'] and r.get('configuration_path')
                 and config_key(r['authored_configuration']) == key]
        if found:
            require(len(found) == 1, 'Ambiguous exact baseline configuration')
            covered.append(dict(configuration=roster['configuration'], identity=roster['identity'],
                                cache_key=list(cache_key(found[0]))))
            continue
        require(key in configurations, 'Missing exact configuration: ' + str(key))
        c = configurations[key]
        b = bindings.get(key)
        if b is None:
            models = [m for m in docs['fixed_geometry']['models']
                      if any(s['resource'] == key[0] and s['sha256'] == key[1]
                             for s in m['source']['configuration_closure'])]
            require(len(models) == 1, 'Missing exact reference binding: ' + str(key))
            source = models[0]['source']; ref = source['reference']
            b = dict(native_asset_name=roster['asset'],
                     weapon=dict(resource=key[0], sha256=key[1], instance_guid=key[2], type='GameSharedResources.SoldierWeaponData'),
                     meshes=[source['mesh_binding']], animation_source=ref['source'],
                     animation_chain=dict(animation_resource=ref['resource']), animation_sha256=ref['sha256'],
                     skeleton_sha256=source['skeleton_sha256'])
        compact_binding = {k: b[k] for k in ('weapon', 'meshes', 'animation_source', 'animation_chain',
                                            'animation_sha256', 'skeleton_sha256')}
        group = digest(dict(asset=roster['asset'], meshes=b['meshes'], closed_clip_sha256=b['animation_sha256']))
        missing.append(dict(roster=roster, configuration=c, binding=compact_binding,
                            content_group=group, alias_eligible=roster['mechanism'] == 'tube_pump'))
    shell_jobs = {}
    groups = {g['mesh']: g for g in docs['shotgun']['groups']}
    sample_sources = {s['mesh']: s['source'] for s in docs['samples']['records']}
    for p in docs['shotgun']['profiles']:
        group = groups[p['mesh']]; shell = group['shell']; source = sample_sources[p['mesh']]
        key = (p['asset'], p['mesh'], shell['bone'], int(p['rig_fingerprint'].split(':')[1], 16))
        if key not in existing:
            shell_jobs.setdefault(key, dict(asset=p['asset'], mesh=p['mesh'], part=shell['bone'],
                rig_fingerprint=key[3], archive=source['mesh_source']['archive'],
                archive_index_sha256=source['mesh_source']['archive_index_sha256'],
                mesh_sha256=source['mesh_sha256'], lod_sha256=source['lod_sha256'],
                hidden_other_rounds=shell['descendants'], carried_parts=[shell['bone']]))
    fixed = [r for r in docs['fixed']['profiles'] if cache_key(r) not in existing]
    planned_groups = {(x['content_group'] if x['alias_eligible'] else str(roster_key(x['roster']))) for x in missing}
    naive_rows = len(baseline) + len(missing) + len(shell_jobs) + len(fixed)
    planned_rows = len(baseline) + len(planned_groups) + len(shell_jobs) + len(fixed)
    require(planned_rows <= MAX_ROWS, 'Even exact alias plan exceeds cache row bound')
    plan = dict(schema=SCHEMA, sources=sources, baseline_cache=batch.pin(baseline_cache),
        baseline_rows=len(baseline), baseline_bytes=len(payload), covered_bodies=covered,
        missing_bodies=missing, missing_shells=list(shell_jobs.values()), missing_fixed=fixed,
        summary=dict(roster_rows=len(docs['roster']['rows']), covered_exact_bodies=len(covered),
                     missing_exact_bodies=len(missing), missing_shell_rows=len(shell_jobs),
                     missing_fixed_rows=len(fixed), naive_cache_rows=naive_rows,
                     planned_cache_rows=planned_rows, maximum_rows=MAX_ROWS, maximum_bytes=MAX_BYTES,
                     requested_storage_bytes=128 * 1024 * 1024),
        alias_policy='Only exact same-asset configured mesh/closed clip content, with byte-equal evaluated geometry; every configuration is independently pinned and replayed.',
        native_execution=False, headset_acceptance=False)
    plan['plan_digest'] = digest(plan)
    return plan


def make_display_policy(spec, binding, failure):
    require(failure.startswith('Composite required closed controls not static:'),
            'Only decoded varying closed controls permit explicit display policy')
    result = dict(schema='fvr.bc2.firearm_display_policy.v1',
                  configuration=copy.deepcopy(spec['authored_configuration']),
                  closed_clip=binding['animation_chain']['animation_resource'],
                  closed_clip_sha256=binding['animation_sha256'], skeleton_sha256=ammo.SKELETON_SHA,
                  frame_seconds=0.0, purpose='Render-only evaluated closed reference for body display',
                  strict_static_passed=False, native_verified=False, headset_tested=False,
                  strict_failure_kind='decoded_varying_closed_controls')
    result['policy_digest'] = digest(result)
    return result


def validate_display_policy(review, cfg, clip_name, clip_sha):
    require(review.get('schema') == 'fvr.bc2.firearm_display_policy.v1'
            and review.get('policy_digest') == digest({k: v for k, v in review.items() if k != 'policy_digest'}),
            'Exact firearm display policy digest differs')
    require(review['configuration'] == cfg and review['closed_clip'] == clip_name
            and review['closed_clip_sha256'] == clip_sha and review['skeleton_sha256'] == ammo.SKELETON_SHA,
            'Firearm display policy identity differs')
    require(review['frame_seconds'] == 0.0 and review['strict_static_passed'] is False
            and review['native_verified'] is False and review['headset_tested'] is False
            and review['strict_failure_kind'] == 'decoded_varying_closed_controls',
            'Firearm display policy cannot claim static/native evidence')


def sampled_firearm_body(game, pool, spec, binding, base_mesh, skeleton, review, resource_content=False):
    """Use the authored-display baker's encoding with an independent display policy.

    No magazine interaction profile is fabricated, and strict composite remains
    unchanged. Every weighted bone must be evaluable at the explicit frame.
    """
    cfg = spec['authored_configuration']; source = binding['animation_source']
    name = binding['animation_chain']['animation_resource']
    validate_display_policy(review, cfg, name, binding['animation_sha256'])
    archive = pool(game/source['archive'])
    if not resource_content:
        require(archive.index_sha256 == source['archive_index_sha256'], 'Display clip archive differs')
    raw = archive.read_selected([name])[name]
    require(sha(raw) == binding['animation_sha256'], 'Display clip content differs')
    known = {bone_hash(n): n for n in skeleton.names}
    inverse = {n: canonical(b['InverseWorldTransform']) for n, b in zip(skeleton.names, skeleton.bones)}
    decoded = []; sources = []; required = {'RightHand', 'jntWpn_Flash'}
    for s in spec['meshes']:
        archive = pool(game/s['archive']); mesh = s['mesh']
        names = [mesh+'.res', mesh+'_lod0_data.res']; blobs = archive.read_selected(names)
        if not resource_content and s.get('expected_index_sha256'):
            require(archive.index_sha256 == s['expected_index_sha256'], 'Display mesh archive differs')
        if s.get('expected_metadata_sha256'):
            require(sha(blobs[names[0]]) == s['expected_metadata_sha256'], 'Display mesh content differs')
        if s.get('expected_lod_sha256'):
            require(sha(blobs[names[1]]) == s['expected_lod_sha256'], 'Display LOD content differs')
        lod = metadata(blobs[names[0]])[0]; data = blobs[names[1]]
        sections = geometry(data, lod, known)
        vb = sum(r['vertices']*r['stride'] for r in sections)
        ib = sum(r['triangles']*6 for r in sections); start = len(data)-vb-ib
        require(start >= 0, 'Display geometry range')
        for section in sections:
            if section['name'].endswith('_ZOnly'):
                continue
            required.update(section['bone_names']); decoded.append((data, section, lod, start, vb))
        sources.append(dict(archive=s['archive'], archive_index_sha256=archive.index_sha256,
                            mesh=mesh, mesh_sha256=sha(blobs[names[0]]), lod_sha256=sha(data)))
    closed = Clip(raw).evaluate(skeleton, 0., sorted(required))
    require(all(v in ('static_authored_pose', 'decoded_spline_candidate')
                for v in closed['bone_evaluation_status'].values()), 'Unresolved display bone')
    require(any(v == 'decoded_spline_candidate' for n, v in closed['bone_evaluation_status'].items()
                if n not in ('RightHand', 'jntWpn_Flash')), 'Explicit varying display policy has no varying control')
    vertices = []; indices = []
    for data, section, lod, start, vb in decoded:
        baked = equipment.bake_section(data, section, lod, known, inverse, closed['weapon_relative'], start)
        ids = struct.unpack_from('<'+'H'*(section['triangles']*3), data, start+vb+section['first_index']*2)
        require(ids and max(ids) < len(baked), 'Display index bound')
        offset = len(vertices); indices.extend(i+offset for i in ids); vertices.extend(baked)
    require(0 < len(indices)//3 <= 32768 and len(vertices) <= 131072, 'Display geometry bound')
    part = 'body_holstered_'+hashlib.sha256(cfg['resource'].encode()).hexdigest()[:16]
    section = policy.baked_section('closed_configuration', vertices, indices)
    row = dict(asset=spec['asset'], mesh=base_mesh, part=part,
        rig_fingerprint=int(skeleton.fingerprint.split(':')[1],16), inverse_bind=identity(), sections=[section],
        display_only=True, configuration_path=cfg['resource'][:-4], configured_meshes=sorted(s['mesh'] for s in sources),
        authored_configuration=cfg, closed_clip=name, closed_clip_sha256=sha(raw),
        closed_pose='sampled_authored_mod_reference', sources=sources,
        display_anchor_evaluation={n:closed['bone_evaluation_status'][n] for n in ('RightHand','jntWpn_Flash')},
        display_anchor_time_seconds=0., display_anchor_not_native_grip_calibration=True,
        mod_authored_firearm_display=True, firearm_display_policy=review,
        reference_bone_evaluation=closed['bone_evaluation_status'],
        holster_from_weapon=equipment.holster_transform(closed['weapon_relative']['RightHand'][12:15],
                                                      closed['weapon_relative']['jntWpn_Flash'][12:15]),
        grip=closed['weapon_relative']['RightHand'][12:15], muzzle=closed['weapon_relative']['jntWpn_Flash'][12:15])
    size = 2 if len(vertices) <= 65536 else 4
    record = policy.pack_row(row, [policy.section_record('closed_configuration', b''.join(vertices),
        struct.pack('<'+('H' if size == 2 else 'I')*len(indices), *indices), size)])
    return row, record


def reviewed_spec(row):
    return dict(asset=row['asset'], authored_configuration=row['authored_configuration'],
        meshes=[dict(archive=s['archive'], mesh=s['mesh'], expected_index_sha256=s['archive_index_sha256'],
                     expected_metadata_sha256=s['mesh_sha256'], expected_lod_sha256=s['lod_sha256']) for s in row['sources']])


def reviewed_binding(row):
    return dict(weapon=row['authored_configuration'],
                animation_source=dict(archive=row['closed_archive'], archive_index_sha256=row['closed_archive_index_sha256']),
                animation_chain=dict(animation_resource=row['closed_clip']), animation_sha256=row['closed_clip_sha256'])


def verify_configuration(game, pool, row, resource_content=False):
    from bc2_weapon_config_pipeline import parse, guid, named, scalar
    archive = pool(game/row['configuration_archive'])
    if not resource_content:
        require(archive.index_sha256 == row['configuration_archive_index_sha256'], 'Configuration archive differs')
    cfg = row['authored_configuration']
    require(cfg['resource'] == row['configuration_path']+'.dbx', 'Exact configuration path differs')
    documents = {}
    for ref in [cfg]+cfg['mesh_references']:
        raw = archive.read_selected([ref['resource']])[ref['resource']]
        require(sha(raw) == ref['sha256'], 'Configuration DBX differs')
        documents[ref['resource']] = parse(ref['resource'], raw)
    native = documents[cfg['resource']].instances.get(guid(cfg['instance_guid']))
    require(native is not None and native.attributes.get('type') == 'GameSharedResources.SoldierWeaponData'
            and scalar(named(native,'Name'),'string') == row['asset'], 'Exact configuration GUID/name differs')
    states = named(native,'WeaponStates').children
    require(len(states) == 1 and [i.attributes['ref'] for i in named(states[0],'Meshes1p').children if i.tag == 'item']
            == [ref['reference'] for ref in cfg['mesh_references']], 'Exact configured mesh references differ')
    for ref in cfg['mesh_references']:
        instance = documents[ref['resource']].instances.get(guid(ref['instance_guid']))
        require(instance is not None and instance.attributes.get('type','').endswith('.SkinnedMeshAsset'),
                'Exact configured mesh GUID differs')
    return archive.index_sha256


def derive_reviewed_display(game, pool, reviewed, resource_content=False):
    """Portable normal-catalog hook; requires only embedded pins and installed bytes."""
    configuration_index = verify_configuration(game, pool, reviewed, resource_content)
    raw = pool(game/ammo.SKELETON_ARCHIVE).read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
    require(sha(raw) == ammo.SKELETON_SHA, 'Display skeleton differs')
    row, record = sampled_firearm_body(game, pool, reviewed_spec(reviewed), reviewed_binding(reviewed),
        reviewed['mesh'], Skeleton(raw), reviewed['firearm_display_policy'], resource_content)
    row.update(configuration_archive=reviewed['configuration_archive'], configuration_archive_index_sha256=configuration_index,
               closed_archive=reviewed['closed_archive'], closed_archive_index_sha256=pool(game/reviewed['closed_archive']).index_sha256)
    expected = {k:v for k,v in reviewed.items() if k != 'configured_aliases'}
    require(profiles_match([row], [expected], resource_content), 'Portable sampled firearm display differs')
    return row, record


@contextlib.contextmanager
def shared_readers(pool):
    old = (ammo.archive, visibility.Archive)
    try:
        ammo.archive = lambda game, path: pool(game/path)
        visibility.Archive = pool
        yield
    finally:
        ammo.archive, visibility.Archive = old


def extract(args):
    from bc2_firearm_body_aliases import add_alias, verify_aliases
    plan = read(args.manifest)
    require(plan['schema'] == SCHEMA and plan['plan_digest'] == digest({k:v for k,v in plan.items() if k != 'plan_digest'}), 'Plan digest differs')
    docs = {k:batch.read_pin(v) for k,v in plan['sources'].items()}
    baseline_path = Path(plan['baseline_cache']['path']); payload = baseline_path.read_bytes()
    require(sha(payload) == plan['baseline_cache']['sha256'] == BASELINE_SHA, 'Baseline cache changed')
    rows = copy.deepcopy(docs['baseline']['profiles']); baseline_records = record_slices(payload, rows)
    output = args.output.resolve(); game = args.game.resolve()
    require(not output.is_relative_to(game), 'Private cache output cannot be game installation')
    require(not (output/'body-ammo.fvrprop').exists() and not (output/'extraction-receipt.json').exists(), 'Preserve prior extraction output')
    output.mkdir(parents=True, exist_ok=True)
    first = plan['missing_bodies'][0]['configuration']
    pool = ArchivePool(game, first['origin_archive'], first['origin_index_sha256'])
    records = []; groups = {}; residuals = []; joined = []; failures = []
    known_keys = {cache_key(r) for r in rows}
    with shared_readers(pool):
        raw = pool(game/ammo.SKELETON_ARCHIVE).read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
        require(sha(raw) == ammo.SKELETON_SHA, 'Skeleton changed')
        skeleton = Skeleton(raw); known = {bone_hash(n):n for n in skeleton.names}
        inverse = {n:canonical(b['InverseWorldTransform']) for n,b in zip(skeleton.names,skeleton.bones)}
        for job in [*plan['missing_shells'], *plan['missing_fixed']]:
            print(json.dumps(dict(stage='supply', asset=job['asset'], part=job['part'])), flush=True)
            fresh, packed = ammo.derive(game, [(job['asset'], job['archive'], job['mesh'], job['part'])])
            row = fresh[0]
            for k in ('mesh_sha256', 'lod_sha256', 'rig_fingerprint'):
                require(row[k] == job[k], 'Supply exact content differs: '+k)
            if 'sections' in job:
                require(row == job, 'Fixed supply catalog replay differs')
            record = body.compact_cache_sections(packed, fresh)[12:]
            require(cache_key(row) not in known_keys, 'Duplicate supply key')
            known_keys.add(cache_key(row)); rows.append(row); records.append(record)
        for job in plan['missing_bodies']:
            roster = job['roster']; c = job['configuration']; b = job['binding']
            print(json.dumps(dict(stage='body', asset=roster['asset'], configuration=roster['configuration'])), flush=True)
            try:
                configured = dict(archive=c['origin_archive'], index_sha256=c['origin_index_sha256'],
                    resolved_weapons=[dict(c, weapon_states=c.get('authored_states', c.get('weapon_states')))])
                specs, excluded = visibility.specs_from_configuration(game, configured, classes=(c['weapon_class'],), inventory=docs['inventory'])
                require(len(specs) == 1 and not excluded, 'Exact configured mesh resolution: '+str(excluded))
                spec = specs[0]; spec['authored_configuration'].update(instance_guid=b['weapon']['instance_guid'], type=b['weapon']['type'])
                mesh = b['meshes'][0]['configured_mesh_path']
                require(mesh in [s['mesh'] for s in spec['meshes']], 'Base mesh outside exact configured set')
                try:
                    row, record = body.composite(game, pool, spec, b, dict(configured_mesh_path=mesh), skeleton, known, inverse)
                except ValueError as exc:
                    failure = str(exc); review = make_display_policy(spec, b, failure)
                    failures.append(dict(configuration=roster['configuration'], strict_failure=failure, policy_digest=review['policy_digest']))
                    row, record = sampled_firearm_body(game, pool, spec, b, mesh, skeleton, review)
                row.update(configuration_archive=c['origin_archive'], configuration_archive_index_sha256=c['origin_index_sha256'],
                           closed_archive=b['animation_source']['archive'], closed_archive_index_sha256=b['animation_source']['archive_index_sha256'])
                record = body.compact_cache_sections(ammo.MAGIC+struct.pack('<I',1)+record, [row])[12:]
                group = job['content_group'] if job['alias_eligible'] else None
                if group in groups:
                    canonical_row, canonical_record = groups[group]
                    add_alias(canonical_row, canonical_record, row, record)
                    joined.append(dict(configuration=roster['configuration'], identity=roster['identity'], cache_key=list(cache_key(canonical_row)), exact_alias=True))
                else:
                    require(cache_key(row) not in known_keys, 'Duplicate body cache key')
                    known_keys.add(cache_key(row)); rows.append(row); records.append(record)
                    if group:
                        groups[group] = (row, record)
                    joined.append(dict(configuration=roster['configuration'], identity=roster['identity'], cache_key=list(cache_key(row)), exact_alias=False))
            except (ValueError, KeyError, TypeError, OSError) as exc:
                residuals.append(dict(configuration=roster['configuration'], identity=roster['identity'], reason=str(exc)))
        # Independently replay every alias's own closed configuration before export.
        for row, record in groups.values():
            if row.get('configured_aliases'):
                verify_aliases(game, pool, row, record)
    full = ammo.MAGIC+struct.pack('<I',len(rows))+payload[12:]+b''.join(records)
    require(len(rows) <= MAX_ROWS and len(full) <= MAX_BYTES, 'Complete cache exceeds512rows/64MiB')
    require(record_slices(full, rows)[:len(baseline_records)] == baseline_records, 'Baseline record bytes changed')
    catalog = dict(docs['baseline'], profiles=rows)
    write(output/'body-ammo-assets.json', catalog)
    (output/'body-ammo.fvrprop').write_bytes(full)
    receipt = dict(schema='fvr.bc2.firearm_cache_completion_receipt.v1', plan_digest=plan['plan_digest'],
        baseline_pin=plan['baseline_cache'], baseline_records_byte_exact=True, catalog_rows=len(rows), cache_bytes=len(full),
        exact_roster_bodies=len(plan['covered_bodies'])+len(joined), expected_roster_bodies=plan['summary']['roster_rows'],
        supply_added=len(plan['missing_shells'])+len(plan['missing_fixed']), body_added=len(records)-len(plan['missing_shells'])-len(plan['missing_fixed']),
        exact_aliases=sum(len(r.get('configured_aliases',[])) for r in rows), selected_resource_bytes=pool.bytes,
        body_joins=joined, strict_failures=failures, residuals=residuals, complete=not residuals,
        cache_pin=batch.pin(output/'body-ammo.fvrprop'), native_execution=False, headset_acceptance=False)
    write(output/'extraction-receipt.json', receipt)
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('body_joins','strict_failures','residuals')}))
    return 0 if receipt['complete'] else 2


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('plan')
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--recovery', type=Path, required=True)
    p.add_argument('--baseline-cache', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('extract')
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'plan':
        result = prepare(args.root, args.recovery, args.baseline_cache)
        write(args.output, result)
        print(json.dumps(result['summary']))
        return 0
    return extract(args)


if __name__ == '__main__':
    raise SystemExit(main())
