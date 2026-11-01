"""Exact measured underbarrel profiles; offline generation, never admission.

`prepare-xm8` reduces the retained report71 measurements to a portable input.
`prepare-an94` and `prepare-m16` normalize the pinned report72 packets.
`prepare-aek`, `prepare-hk416`, `prepare-f2000` and `prepare-aug` use report73.
`generate --family DIR [--family DIR ...] --output DIR` accepts that same
source/measurement contract for subsequent independently measured families.
No installed resources are read. Insertion currently uses FreePlacement;
the proposed bore rail is retained as unenforced policy, not silently enabled.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bc2_fixed_container_geometry as fixed

ROOT = Path(__file__).resolve().parents[1]
mag = fixed.mag
SOURCE_SCHEMA = 'fvr.bc2.underbarrel_reload_source.v1'
MEASUREMENT_SCHEMA = 'fvr.bc2.underbarrel_reload_measurements.v1'
STEPS = ('Open', 'EjectSpent', 'Insert', 'Close')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode()).hexdigest()


def pin(path):
    path = Path(path).resolve()
    return {'path': path.relative_to(ROOT).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def write_new(path, value, replace=False):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = value if isinstance(value, str) else json.dumps(value, indent=2, allow_nan=False) + '\n'
    with path.open('w' if replace else 'x', encoding='utf-8', newline='\n') as stream:
        stream.write(text)


def sha(value):
    require(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value), 'Complete SHA256 required')
    return value


def typed(value, kind):
    require(value['type'] == kind and value['resource'].endswith('.dbx') and
            re.fullmatch('[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', value['instance_guid']),
            'Incomplete typed source')
    sha(value['sha256'])


def reference(value):
    # Authored references are canonical lowercase; runtime selectors preserve
    # the exact typed resource spelling and never perform alias matching.
    return value['resource'][:-4].lower() + '/' + value['instance_guid']


def coordinate_tuple(chain):
    return {key: chain[key] for key in ('tree', 'reload', 'mesh', 'skeleton', 'mesh_skeleton',
            'skeleton_sha256', 'rig', 'clip_sha256', 'closed_sha256', 'mesh_sha256', 'lod_sha256')}


def validate_family(source, measured):
    """Reject partial or crossed exact-source tuples before emitting any profile."""
    try:
        require(source['schema'] == SOURCE_SCHEMA and measured['schema'] == MEASUREMENT_SCHEMA,
                'Measured family schema')
        require(source['family'] == measured['family'] and source['family'], 'Measured family identity')
        if 'measurements_sha256' in source:
            require(sha(source['measurements_sha256']) == digest(measured),
                    'Exact measured geometry pin mismatch')
        chain = source['chain']
        for key, kind in (('config', 'GameSharedResources.SoldierWeaponData'),
                          ('tree', 'Animation.SpecificAnimTreeData'), ('reload', 'Animation.AnimationAsset'),
                          ('mesh', 'Render.SkinnedMeshAsset'), ('skeleton', 'Animation.SkeletonAsset')):
            typed(chain[key], kind)
        for key in ('skeleton_sha256', 'clip_sha256', 'closed_sha256', 'mesh_sha256', 'lod_sha256',
                    'common_index_sha256', 'weapon_index_sha256'):
            sha(chain[key])
        require(re.fullmatch('fnv1a64:[0-9a-f]{16}', chain['rig']), 'Exact rig fingerprint')
        require(chain['mesh_skeleton'] == reference(chain['skeleton']), 'Mesh/skeleton tuple mismatch')
        require(measured['coordinates'] == coordinate_tuple(chain), 'Measurement coordinate tuple mismatch')
        require(source['state'] == 0 and source['binding_status'] == 'typed_reload_mesh_skeleton_proved',
                'Typed state0 reload/mesh/skeleton proof required')
        paths = source['configuration_paths']
        require(paths and len(paths) == len(set(paths)), 'Duplicate or empty family configuration set')
        rows = {r['configuration']: r for r in source['configurations']}
        variants = {r['configuration']: r for r in source['variants']}
        require(len(rows) == len(source['configurations']) and len(variants) == len(source['variants']),
                'Duplicate exact source configuration')
        require(set(paths) == set(rows) == set(variants), 'Incomplete exact configuration tuples')
        require(set(source['tuple_digests']) == set(paths), 'Incomplete exact source tuple pins')
        require(chain['config'] == rows[chain['config']['resource'][:-4]]['source'],
                'Representative configuration source mismatch')
        for name in paths:
            row, variant = rows[name], variants[name]
            require(source['tuple_digests'][name] == digest([row, variant, coordinate_tuple(chain)]),
                    'Exact source tuple pin mismatch')
            typed(row['source'], 'GameSharedResources.SoldierWeaponData')
            require(row['source']['resource'] == name + '.dbx' and row['weapon_class'] == 'wcUgl' and
                    row['asset'] in ('40mmgl', '40mmsg', '40mmsmk'), 'Exact underbarrel configuration required')
            require(len(row['states']) == 1, 'Unique measured state required')
            state = row['states'][0]
            handheld = [mesh for mesh in state['meshes'] if mesh.startswith('objects/weapons/handheld/')]
            require(state['index'] == source['state'] and state['tree'] == chain['tree'] and
                    handheld == [reference(chain['mesh'])], 'Configuration tree/mesh tuple mismatch')
            require(set(state['roles']) == {'handsikpose', '1p_altdeploy'} and all(
                role['skeleton_reference'] == chain['mesh_skeleton'] for role in state['roles'].values()),
                'Configuration skeleton tuple mismatch')
            require(variant['mesh_sha256'] == chain['mesh_sha256'] and
                    variant['lod_sha256'] == chain['lod_sha256'] and
                    variant['clip_hashes'] == source['pose_clip_hashes'] and
                    len(variant['clip_hashes']) == 2 and variant['clip_hashes'][0] == chain['closed_sha256'],
                    'Variant byte-coordinate tuple mismatch')
            require(variant['archive'].startswith('Dist/win32/') and variant['archive'].endswith('.fbrb'),
                    'Exact source archive required')
            sha(variant['index_sha256'])
            for value in variant['clip_hashes']:
                sha(value)
        for receipt in source['provenance']:
            require(receipt['path'], 'Source provenance path required')
            sha(receipt['sha256'])
        require(source['provenance'], 'Source provenance required')
        bones = source['skeleton_bones']
        require(bones and len({b['index'] for b in bones.values()}) == len(bones) and all(
                type(b['index']) is int and 0 <= b['index'] < 1024 for b in bones.values()),
                'Unique measured bone indices')
        require(1 <= len(measured['parts']) <= 16, 'Bounded runtime part count')
        for bone, spec in measured['parts'].items():
            require(spec['bone_index'] == bones[bone]['index'] and spec['parent'] == bones[bone]['parent'] and
                    spec['mixed_triangles'] == 0 and spec['visible_triangles'] > 0, 'Rigid measured part tuple mismatch')
            lo, hi = spec['bounds']
            require(len(lo) == len(hi) == 3 and all(math.isfinite(a) and math.isfinite(b) and a < b
                    for a, b in zip(lo, hi)), 'Measured part bounds required')
        p = source['policy']
        require(p['kind'] in ('SingleRoundBreech', 'SingleRoundMuzzle') and
                p['capacity'] == p['rounds_per_insertion'] == 1,
                'Single-round capacity contract')
        require(p['insertion_kind'] == 'FreePlacement' and p['rail_enforced'] is False,
                'Shared rail API is not implemented; unsupported insertion policy')
        require(p['spent_case_visual_removal_only'] is True and p['delete_live_ammunition'] is False,
                'Live ammunition must be retained')
        for key in ('opening_radius_m', 'supply_radius_m'):
            require(math.isfinite(p[key]) and 0 < p[key] <= .1, 'Explicit bounded mod acquisition tolerance')
        mechanism, supply = measured['mechanism'], measured['supply']
        fixed_tube = p['kind'] == 'SingleRoundMuzzle'
        require(mechanism['motion_kind'] == p['opening_kind'] and
                p['opening_kind'] in (('Fixed',) if fixed_tube else ('Hinge', 'Linear')),
                'Explicit measured opening mechanism required')
        render_parts = mechanism.get('render_parts', {})
        require(not render_parts or not fixed_tube, 'Fixed tube has no moving render assembly')
        require(not (set(render_parts) & (set(supply['parts']) | {mechanism['bone']})),
                'Moving render part cannot replace mechanism or ammunition')
        for bone, part in render_parts.items():
            require(bone in bones and part['motion_kind'] == 'Linear' and
                    part['semantic_role_proved'] is False, 'Explicit render-only measured part required')
            fixed.rigid(part['closed']); fixed.rigid(part['open'])
            require(max(abs(part['closed'][k] - part['open'][k]) for k in range(12)) < 1e-6 and
                    .06 < mag.norm(mag.sub(part['open'][12:15], part['closed'][12:15])) < 1,
                    'Render-only measured slider endpoints required')
        mechanism_parts = set() if fixed_tube else {mechanism['bone']} | set(render_parts)
        require(mechanism['bone'] in bones and supply['root'] in bones and supply['root'] in supply['parts'] and
                mechanism['bone'] not in supply['parts'] and
                set(measured['parts']) == mechanism_parts | set(supply['parts']) and
                len(supply['parts']) == len(set(supply['parts'])), 'Incomplete measured mechanical/supply assembly')
        if fixed_tube:
            require(mechanism['closed'] == mechanism['open'] and 'grasp' not in mechanism,
                    'Fixed muzzle tube must not fabricate opening or closing')
        for grasp in ([supply['grasp']] if fixed_tube else [mechanism['grasp'], supply['grasp']]):
            require(set(grasp['fingers_to_part']) == set(mag.FINGERS) and grasp['sample'] >= 0,
                    'Complete same-frame handshape required')
            fixed.rigid(grasp['hand_to_part'])
            for matrix in grasp['fingers_to_part'].values():
                fixed.rigid(matrix)
        for matrix in (mechanism['closed'], mechanism['open'], supply['source'], supply['destination'],
                       supply['seat_in_mechanism'], *supply['part_in_root'].values()):
            fixed.rigid(matrix)
        require(set(supply['part_in_root']) == set(supply['parts']), 'Complete frozen supply assembly required')
        require(supply['part_in_root'][supply['root']] == mag.identity(), 'Supply root must be identity')
        seated = mag.multiply(supply['seat_in_mechanism'], mechanism['open'])
        # Saved matrices have eight decimal places. Compare their entries;
        # acos(trace) magnifies that harmless rounding near zero rotation.
        require(max(abs(a - b) for a, b in zip(supply['destination'], seated)) < 1e-6,
                'Seated round does not match measured open mechanism')
        axis = measured['bore']['inward_part']
        require(len(axis) == 3 and abs(mag.norm(axis) - 1) < 1e-6, 'Measured unit bore axis required')
        return rows
    except (KeyError, TypeError, IndexError) as exc:
        raise ValueError('Incomplete measured family tuple: ' + str(exc)) from exc


def hand_shape(grasp):
    inverse = mag.inverse_rigid(grasp['hand_to_part'])
    return {'part_from_wrist': grasp['hand_to_part'], 'sample': grasp['sample'],
            'wrist_from_fingers': {bone: mag.multiply(frame, inverse)
                                   for bone, frame in grasp['fingers_to_part'].items()}}


def opening_motion(mechanism, grasp, reverse=False):
    first, last = (mechanism[k] for k in (('open', 'closed') if reverse else ('closed', 'open')))
    if mechanism['motion_kind'] == 'Hinge':
        samples = {'frames': [{'weapon_relative': {mechanism['bone']: frame}} for frame in (first, last)]}
        motion = fixed.hinge(samples, mechanism['bone'], 0, 1, grasp)
        motion['maxStepRadians'] = .25
    else:
        require(max(abs(first[k] - last[k]) for k in range(12)) < 1e-6,
                'Measured slider must preserve orientation')
        delta = mag.sub(last[12:15], first[12:15]); travel = mag.norm(delta)
        require(.06 < travel < 1, 'Measured slider travel outside reviewed policy bounds')
        x = mag.unit(delta); helper = [0., 1., 0.] if abs(x[1]) < .9 else [0., 0., 1.]
        z = mag.unit(mag.cross(x, helper)); y = mag.cross(z, x)
        frame = mag.frame(x, y, z, first[12:15])
        wrist = mag.multiply(grasp['part_from_wrist'], first)
        motion = fixed.policy_motion('OpenCover', 'Linear')
        motion.update(weaponFromMotion=frame, motionFromContact=mag.multiply(wrist, mag.inverse_rigid(frame)),
                      travel=travel, completeFraction=.92, releaseFraction=.25)
    motion['operation'] = 'CloseCover' if reverse else 'OpenCover'
    return motion


def model_profile(source, measured):
    rows = validate_family(source, measured)
    chain, policy = source['chain'], source['policy']
    mechanism, supply = measured['mechanism'], measured['supply']
    fixed_tube = policy['kind'] == 'SingleRoundMuzzle'
    supply_grasp = hand_shape(supply['grasp'])
    grasps = {'Insert': supply_grasp}
    motions = {}
    if not fixed_tube:
        grasp = hand_shape(mechanism['grasp'])
        grasps = {'Open': grasp, 'Insert': supply_grasp, 'Close': grasp}
        motions = {'Open': opening_motion(mechanism, grasp), 'Close': opening_motion(mechanism, grasp, reverse=True)}
    motion = fixed.policy_motion('SeatFeed', 'FreePlacement')
    motion['placement'] = {'weaponFromSource': supply['source'], 'weaponFromDestination': supply['destination'],
                           'sourceFromGrip': supply_grasp['part_from_wrist']}
    motions['Insert'] = motion
    for motion in motions.values():
        fixed.validate_motion(motion)
    model_id = fixed.fnv(('underbarrel/' + source['family'] + '/' + digest(measured)).encode())
    interaction = dict(kind=policy['kind'], id=model_id, revision=1, capacity=1, roundsPerInsertion=1,
                       ammunitionKind=(fixed.fnv((str(model_id) + '/round').encode()) & 0xffffffff) or 1,
                       maxAgeNs=100000000, geometry={})
    for step in grasps:
        interaction['geometry'][step] = {'contact': {
            'role': (fixed.fnv((str(model_id) + '/' + step).encode()) & 0xffffffff) or 1,
            'radiusMeters': policy['supply_radius_m'] if step == 'Insert' else policy['opening_radius_m'],
            'maxAngleRadians': .6}, 'motion': motions[step]}
    parts = []
    for bone, part in measured['parts'].items():
        is_supply = bone in supply['parts']
        offset = supply['part_in_root'][bone] if is_supply else mag.identity()
        seat = mag.multiply(offset, supply['seat_in_mechanism']) if is_supply else mag.identity()
        closed = mag.multiply(seat, mechanism['closed'])
        opened = mag.multiply(seat, mechanism['open'])
        if bone in mechanism.get('render_parts', {}):
            closed = mechanism['render_parts'][bone]['closed']
            opened = mechanism['render_parts'][bone]['open']
        start = [closed, opened, opened, opened]
        end = [opened, opened, opened, closed]
        if is_supply:
            start[2] = mag.multiply(offset, supply['source'])
            end[2] = mag.multiply(offset, supply['destination'])
        center = mag.identity()
        center[12:15] = [(a + b) / 2 for a, b in zip(*part['bounds'])]
        parts.append(dict(bone=bone, bone_index=part['bone_index'], centerInBone=center,
                          weaponFromClosed=closed, weaponFromOpen=opened, supplyFromPart=offset,
                          weaponFromStepStart=start, weaponFromStepEnd=end, supplyPart=is_supply,
                          coupledOpening=not fixed_tube))
    points = [mag.point([x, y, z], supply['part_in_root'][bone]) for bone in supply['parts']
              for x in (measured['parts'][bone]['bounds'][0][0], measured['parts'][bone]['bounds'][1][0])
              for y in (measured['parts'][bone]['bounds'][0][1], measured['parts'][bone]['bounds'][1][1])
              for z in (measured['parts'][bone]['bounds'][0][2], measured['parts'][bone]['bounds'][1][2])]
    lo, hi = mag.bounds(points)
    center = mag.identity(); center[12:15] = [(a + b) / 2 for a, b in zip(lo, hi)]
    selectors = [dict(asset=rows[name]['asset'], configurationPath=name,
                      configurationGuid=rows[name]['source']['instance_guid'],
                      configurationSha256=rows[name]['source']['sha256'], mesh=chain['mesh']['resource'][:-4],
                      meshSha256=chain['mesh_sha256'], skeletonSha256=chain['skeleton_sha256'],
                      rigFingerprint=int(chain['rig'].split(':')[1], 16)) for name in sorted(rows)]
    return dict(label=source['family'], selectors=selectors, interaction=interaction,
                grasps=grasps, parts=parts,
                supply_root=supply['root'], supply_parts=supply['parts'], supply_center=center,
                policy=policy, source_sha256=digest(source), measurements_sha256=digest(measured))


def cpp_header(models):
    # Reuse the established emitter and its types, without redeclaring them.
    body = fixed.cpp_header({'models': models})
    marker = 'inline const interaction::AuthoredHandShape '
    require(marker in body, 'Fixed-container emitter contract changed')
    body = body[body.index(marker):]
    body = re.sub(r'\b(FixedContainer\d+\w*|FixedContainerEquipment|FindFixedContainerEquipment)\b',
                  lambda m: ('FindUnderbarrelFixedContainerEquipment' if m[0].startswith('Find')
                             else 'Underbarrel' + m[0]), body)
    return ('#pragma once\n// Generated by bc2_underbarrel_reload_geometry.py; exact measured families only.\n'
            '// Explicit VR policy, FreePlacement insertion; bore rail is NOT enforced.\n'
            '// Unregistered: no native or headset acceptance, no live-ammunition deletion.\n'
            '#include "../weapons46-special/FixedContainer.generated.h"\n'
            'namespace fvr::bc2::generated {\n' + body)


def generate(families, output, verify=False, replace=False):
    require(not (verify and replace), 'Verify cannot replace outputs')
    models = [model_profile(fixed.load(Path(path) / 'source.json'),
                            fixed.load(Path(path) / 'measurements.json')) for path in families]
    require(models and len({m['label'] for m in models}) == len(models), 'Duplicate or empty measured families')
    models.sort(key=lambda m: m['label'])
    keys = [(s['configurationPath'], s['mesh'], s['rigFingerprint']) for m in models for s in m['selectors']]
    require(len(keys) == len(set(keys)), 'Duplicate exact runtime selector')
    outputs = {'FixedContainer.generated.h': cpp_header(models),
               'geometry.json': json.dumps({'schema': 'fvr.bc2.underbarrel_reload_geometry.v1',
                   'native_checked': False, 'headset_accepted': False, 'runtime_registered': False,
                   'models': models}, indent=2, allow_nan=False) + '\n'}
    for name, text in outputs.items():
        path = Path(output) / name
        if verify:
            require(path.read_text(encoding='utf-8') == text, 'Generated artifact differs: ' + name)
        else:
            write_new(path, text, replace)
    return {'families': len(models), 'configurations': len(keys), 'rail_enforced': False,
            'runtime_registered': False, 'verified': verify}


def prepare_xm8(reports, output, replace=False):
    """Report adapter only; all reusable generation consumes the portable schema."""
    reports, output = Path(reports), Path(output)
    paths = {name: reports / name for name in ('xm320-authored-loading-evidence.json',
        'launcher-contact-measurements.json', 'launcher-contact-geometry.json',
        'launcher-round-measurements.json', 'launcher-round-phases.json', 'underbarrel-family-catalog.json')}
    values = {name: fixed.load(path) for name, path in paths.items()}
    evidence = values['xm320-authored-loading-evidence.json']; chain = evidence['chain']
    contacts = values['launcher-contact-measurements.json']; rounds = values['launcher-round-measurements.json']
    review = values['launcher-contact-geometry.json']; phases = values['launcher-round-phases.json']
    catalog = values['underbarrel-family-catalog.json']
    enrolled_path = ROOT / 'profiles/weapons54-underbarrel/enrollment.json'
    variants_path = ROOT / 'profiles/weapons54-underbarrel/variant-coordinates.json'
    indices_path = ROOT / 'profiles/weapons46-special/geometry.json'
    for path in (enrolled_path, variants_path):
        expected = next(v for k, v in evidence['reusable_offline_coordinates']['source_hashes'].items()
                        if k.replace('\\', '/') == path.relative_to(ROOT).as_posix())
        require(pin(path)['sha256'] == expected, 'Retained enrollment coordinate source changed')
    require(pin(paths['launcher-round-measurements.json'])['sha256'] == phases['evidence']['sha256'],
            'Reviewed round measurements changed')
    names = evidence['reusable_offline_coordinates']['configurations']
    group = next(g for g in catalog['groups'] if g['id'] == 'XM8_XM320')
    require(set(names) == {catalog['configuration_prefix'] + n for n in group['configurations']} and
            len(names) == group['configuration_count'] == 11, 'Exact XM8 family catalog mismatch')
    index_model = next(m for m in fixed.load(indices_path)['models'] if m['label'] == 'MP412')
    require(all(s['skeletonSha256'] == chain['skeleton_sha256'] and
                s['rigFingerprint'] == int(chain['rig'].split(':')[1], 16) for s in index_model['selectors']),
            'Measured skeleton indices belong to another rig')
    selected_bones = ['jntWpn_12', 'jntWpn_13', 'jntWpnwpnJnt_14']
    bones = {p['bone']: {'index': p['bone_index'], 'parent': p['parent']} for p in index_model['parts']
             if p['bone'] in selected_bones}
    source = dict(schema=SOURCE_SCHEMA, family=group['id'], state=0,
        binding_status='typed_reload_mesh_skeleton_proved', chain=chain, configuration_paths=names,
        pose_clip_hashes=group['enrolled_pose_clip_hashes'],
        configurations=[r for r in fixed.load(enrolled_path)['configurations'] if r['configuration'] in names],
        variants=[r for r in fixed.load(variants_path)['rows'] if r['configuration'] in names], skeleton_bones=bones,
        provenance=[pin(p) for p in [*paths.values(), enrolled_path, variants_path, indices_path]],
        policy=dict(kind='SingleRoundBreech', capacity=1, rounds_per_insertion=1,
            opening_kind='Hinge',
            opening_radius_m=.035, supply_radius_m=.035, insertion_kind='FreePlacement', rail_enforced=False,
            spent_case_visual_removal_only=True, delete_live_ammunition=False,
            opening='Explicit mod wrist target from same-frame7 hand/part relation; 35 mm wrist tolerance, not live Thumb3 collision.',
            closing='Reverse the measured closed0/open20 hinge with the same mod grasp.',
            supply='Freeze measured terminal42 assembly13+14; use complete same-frame30 handshape.',
            insertion='FreePlacement to exact measured snapshot42 seat; proposed straight measured-bore rail awaits shared adapter.',
            visibility='Hide source round geometry; retain loaded live rounds. Empty/spent removal is visual only.',
            semantics='Part names and stock separation are not ammunition transactions or proved case/projectile semantics.',
            admission='Offline profile only; exact native source/rig, body-part cache and runtime admission remain separate.'))
    source['tuple_digests'] = {row['configuration']: digest([row,
        next(v for v in source['variants'] if v['configuration'] == row['configuration']), coordinate_tuple(chain)])
        for row in source['configurations']}
    mechanical_grasp = contacts['frames']['7']
    parts = {}
    for bone in selected_bones:
        data = evidence['part_geometry'][bone]
        parts[bone] = dict(bone_index=bones[bone]['index'], parent=bones[bone]['parent'],
            bounds=contacts['part_bounds'] if bone == selected_bones[0] else rounds['mesh'][bone]['bounds'],
            mixed_triangles=data['mixed_triangles'],
            visible_triangles=sum(s['triangles'] for s in data['sections'] if not s['name'].endswith('_ZOnly')),
            sections=data['sections'])
    s30, s42 = rounds['snapshots']['30'], rounds['snapshots']['42']
    measured = dict(schema=MEASUREMENT_SCHEMA, family=source['family'], coordinates=coordinate_tuple(chain),
        parts=parts, mechanism=dict(bone='jntWpn_12', motion_kind='Hinge', closed_sample=0, open_sample=20,
            closed=contacts['samples'][0]['part_in_weapon'], open=contacts['samples'][20]['part_in_weapon'],
            grasp=dict(sample=7, hand_to_part=mechanical_grasp['LeftHand'],
                fingers_to_part={k: mechanical_grasp[k] for k in mag.FINGERS},
                anatomical_proximity=contacts['samples'][7]['minimum'])),
        supply=dict(root='jntWpn_13', parts=selected_bones[1:], source_sample=30, seat_sample=42,
            assembly_sample=42, source=rounds['samples'][30]['part13_to_weapon'],
            destination=rounds['samples'][42]['part13_to_weapon'], seat_in_mechanism=s42['part13_to_barrel'],
            part_in_root={'jntWpn_13': mag.identity(), 'jntWpnwpnJnt_14': s42['part14_to_part13']},
            grasp=dict(sample=30, hand_to_part=s30['same_frame_hand_to_part13'],
                fingers_to_part=s30['fingers_to_part13'], anatomical_proximity=s30['contacts'][0])),
        bore=review['reviewed_bore_entry'],
        evidence_level='Exact offline geometry plus explicit VR policy; no native/game/headset qualification.')
    validate_family(source, measured)
    write_new(output / 'source.json', source, replace)
    write_new(output / 'measurements.json', measured, replace)
    return {'prepared_family': source['family'], 'configurations': len(names)}


def normalized_typed(value):
    return {key: value['guid' if key == 'instance_guid' else key]
            for key in ('resource', 'sha256', 'instance_guid', 'type')}


def report72_inputs(reports, family, names):
    """Read only retained JSON; never recover missing tuples from installed data."""
    paths = {name: Path(reports) / name for name in names}
    values = {name: fixed.load(path) for name, path in paths.items()}
    catalog_path = Path(reports).parent / 'coordinator-20261101-feedback71/underbarrel-family-catalog.json'
    catalog = fixed.load(catalog_path)
    group = next(g for g in catalog['groups'] if g['id'] == family)
    enrolled_path = ROOT / 'profiles/weapons54-underbarrel/enrollment.json'
    variants_path = ROOT / 'profiles/weapons54-underbarrel/variant-coordinates.json'
    for path in (enrolled_path, variants_path):
        receipt = next(r for r in catalog['sources'] if r['path'] == path.relative_to(ROOT).as_posix())
        require(pin(path)['sha256'] == receipt['sha256'], 'Catalog coordinate source changed')
    paths.update(catalog=catalog_path, enrollment=enrolled_path, variants=variants_path)
    configurations = [catalog['configuration_prefix'] + name for name in group['configurations']]
    rows = [r for r in fixed.load(enrolled_path)['configurations'] if r['configuration'] in configurations]
    variants = [r for r in fixed.load(variants_path)['rows'] if r['configuration'] in configurations]
    require(len(configurations) == group['configuration_count'] and
            {r['configuration'] for r in rows} == set(configurations), 'Incomplete retained family enrollment')
    return paths, values, group, configurations, rows, variants


def family_source(family, chain, group, names, rows, variants, paths, selected_bones, kind, opening_kind):
    topology_path = ROOT / 'profiles/opening426/reference-bindings.json'
    topology_profile = next(p for p in fixed.load(topology_path)['profiles']
                            if p['skeleton_sha256'] == chain['skeleton_sha256'] and p['rig_fingerprint'] == chain['rig'])
    topology = topology_profile['rig_topology']
    bones = {b['name']: dict(index=i, parent=topology[b['parent_index']]['name'] if b['parent_index'] >= 0 else None)
             for i, b in enumerate(topology) if b['name'] in selected_bones}
    require(set(bones) == set(selected_bones), 'Incomplete exact skeleton topology')
    policy = dict(kind=kind, capacity=1, rounds_per_insertion=1, opening_kind=opening_kind,
        opening_radius_m=.035, supply_radius_m=.035, insertion_kind='FreePlacement', rail_enforced=False,
        spent_case_visual_removal_only=True, delete_live_ammunition=False,
        contact='Same-frame anatomical wrist and 15-joint shape; explicit 35 mm mod acquisition tolerance, not glove collision.',
        insertion='FreePlacement to exact measured seat; bore rail remains unenforced pending shared adapter.',
        visibility='Hide supply geometry while absent; retain live loaded ammunition. No stock visibility or custody events inferred.',
        semantics='Render-part labels and stock motion do not establish ammunition transfer or spent-case identity.',
        admission='Offline profile only; current native identity, body-part cache and actual-game/headset acceptance remain separate.')
    source = dict(schema=SOURCE_SCHEMA, family=family, state=0, binding_status='typed_reload_mesh_skeleton_proved',
        chain=chain, configuration_paths=names, configurations=rows, variants=variants,
        pose_clip_hashes=group['enrolled_pose_clip_hashes'], skeleton_bones=bones,
        provenance=[pin(path) for path in [*paths.values(), topology_path]], policy=policy)
    source['tuple_digests'] = {r['configuration']: digest([r,
        next(v for v in variants if v['configuration'] == r['configuration']), coordinate_tuple(chain)]) for r in rows}
    return source


def prepare_an94(reports, output, replace=False):
    paths, values, group, names, rows, variants = report72_inputs(reports, 'AN94',
        ('an94-loading-geometry.json', 'an94-measurements.json'))
    review, data = values['an94-loading-geometry.json'], values['an94-measurements.json']
    require(review['done'] and review['configurations'] == len(names) == 11 and
            pin(paths['an94-measurements.json'])['sha256'] == review['artifacts']['an94-measurements.json'],
            'Reviewed AN94 measurement packet changed')
    require(not data['failures'] and not data['clip_summary']['unsupported_tracks'] and data['sample_count'] == 83,
            'Incomplete AN94 samples')
    joins = {r['configuration']: r for r in data['typed_chains']}
    require(len(joins) == len(data['typed_chains']) == len(names) and set(joins) == set(names),
            'Incomplete exact AN94 typed joins')
    first = joins[names[0]]
    chain = {key: normalized_typed(first[key]) for key in ('config', 'tree', 'reload', 'mesh', 'skeleton')}
    archives = {a['archive']: a for a in data['archives']}
    representative = archives[group['representative']['archive']]
    hashes = representative['hashes']
    require(hashes['clip'] == group['reload']['sha256'] and hashes['pose'] == group['enrolled_pose_clip_hashes'][0],
            'AN94 reload/closed pose mismatch')
    chain.update(mesh_skeleton=first['skeleton_reference'], skeleton_sha256=data['skeleton_sha256'], rig=data['rig'],
        clip_sha256=hashes['clip'], closed_sha256=hashes['pose'], mesh_sha256=hashes['mesh'], lod_sha256=hashes['lod'],
        common_index_sha256=data['common']['index_sha256'], weapon_index_sha256=representative['index_sha256'])
    for row in rows:
        joined = joins[row['configuration']]
        require(joined['state'] == 0 and normalized_typed(joined['config']) == row['source'] and all(
            normalized_typed(joined[key]) == chain[key] for key in ('tree', 'reload', 'mesh', 'skeleton')) and
            joined['skeleton_reference'] == chain['mesh_skeleton'] and
            joined['replacement_reference'] == reference(chain['reload']), 'Crossed AN94 typed join')
    require(data['existing_eight'] == variants, 'Retained AN94 coordinates changed')
    retained = {v['configuration'] for v in variants}
    for name in names:
        if name in retained:
            continue
        archive_name = 'Dist/win32/async/weapon/' + name.rsplit('/', 1)[1].lower() + '-00.fbrb'
        require(archive_name in archives, 'Missing exact AN94 archive coordinate closure')
        archive = archives[archive_name]
        require(archive['hashes'] == hashes and archive['index_sha256'] == representative['index_sha256'],
                'AN94 added archive coordinates differ')
        variants.append(dict(configuration=name, archive=archive_name, index_sha256=archive['index_sha256'],
            mesh_sha256=hashes['mesh'], lod_sha256=hashes['lod'], clip_hashes=[hashes['pose'], hashes['alt']]))
    mechanism_bone, root, child = 'jntWpn_1', 'jntWpnwpnJnt_16', 'jntWpn_17'
    source = family_source('AN94', chain, group, names, rows, variants, paths,
                           [mechanism_bone, root, child], 'SingleRoundMuzzle', 'Fixed')
    source['policy'].update(opening='Fixed launcher tube: no Open or Close gesture exists.',
        closing='No closure required for this measured fixed tube.',
        supply='Freeze measured rigid16+17 assembly; same-frame30 wrist and finger shape.',
        clearance='Measured static radial gap is not swept collision clearance or an enforced rail.')
    load = data['loading_geometry']
    require(load['launcher_skin_owner'] == mechanism_bone and load['child17_in_round16_max_component_spread'] == 0 and
            data['part_geometry'][mechanism_bone]['maximum_displacement_m'] == 0,
            'AN94 fixed tube or rigid supply assembly changed')
    phase = load['phase_frames']['30']; frames = phase['hand_round_and_finger_frames_weapon']
    inverse_root = mag.inverse_rigid(frames[root])
    parts = {}
    for bone in (root, child):
        part = data['part_geometry'][bone]
        parts[bone] = dict(bone_index=source['skeleton_bones'][bone]['index'], parent=part['parent'],
            bounds=[part['minimum'], part['maximum']], mixed_triangles=part['mixed_triangles'],
            visible_triangles=load['round_non_z_triangles'][bone], sections=part['sections'])
    fixed_frame = data['part_geometry'][mechanism_bone]['closed_part_in_weapon']
    measured = dict(schema=MEASUREMENT_SCHEMA, family='AN94', coordinates=coordinate_tuple(chain), parts=parts,
        mechanism=dict(bone=mechanism_bone, motion_kind='Fixed', closed=fixed_frame, open=fixed_frame),
        supply=dict(root=root, parts=[root, child], source_sample=30, seat_sample='HandsIKPose',
            assembly_sample='HandsIKPose', source=frames[root], destination=load['round_seat_in_weapon'],
            seat_in_mechanism=load['round_seat_in_weapon'],
            part_in_root={root: mag.identity(), child: load['child17_in_round16_closed']},
            grasp=dict(sample=30, hand_to_part=phase['hand_in_round'],
                fingers_to_part={bone: mag.multiply(frames[bone], inverse_root) for bone in mag.FINGERS},
                anatomical_proximity=load['contacts'][30]['minimum'])),
        bore=dict(inward_part=load['inward_axis_weapon'], mouth_center_part=load['muzzle_center_weapon'],
            mouth_frame=load['mouth_frame_weapon'], polygon_min_inradius_m=load['muzzle_polygon_minimum_edge_radius_m'],
            static_aligned_radial_clearance_m=load['aligned_radial_clearance_m'], rail_enforced=False,
            swept_collision_proved=False),
        evidence_level='Exact offline geometry plus explicit VR policy; no native/game/headset qualification.')
    validate_family(source, measured)
    write_new(Path(output) / 'source.json', source, replace)
    write_new(Path(output) / 'measurements.json', measured, replace)
    return {'prepared_family': source['family'], 'configurations': len(names)}


def prepare_m16(reports, output, replace=False):
    paths, values, group, names, rows, variants = report72_inputs(reports, 'M16A2',
        ('m16-loading-geometry.json', 'm16-binding.json', 'm16-measurements.json', 'm16-frames-aperture.json'))
    review, binding = values['m16-loading-geometry.json'], values['m16-binding.json']
    data, aperture = values['m16-measurements.json'], values['m16-frames-aperture.json']
    for name in ('m16-binding.json', 'm16-measurements.json', 'm16-frames-aperture.json'):
        require(pin(paths[name])['sha256'] == review['files'][name], 'Reviewed M16 packet changed: ' + name)
    require(review['done'] and review['configuration_count'] == len(names) == 18 and
            binding['family_catalog_sha256'] == pin(paths['catalog'])['sha256'] and
            {binding['configuration_prefix'] + n for n in binding['configurations']} == set(names) and
            binding['configuration_count'] == len(names), 'Exact M16 catalog mismatch')
    require(not data['failures'] and not binding['sample_failures'] and not binding['unsupported_tracks'] and
            data['sample_count'] == 76 and len(variants) == len(names), 'Incomplete M16 measurement/coordinate join')
    chain = {key: normalized_typed(binding['chain'][key]) for key in ('config', 'tree', 'reload', 'mesh', 'skeleton')}
    require(set(binding['chain']['skeleton_refs'].values()) == {reference(chain['skeleton'])} and
            binding['chain']['replacement_reload_ref'] == reference(chain['reload']) and
            binding['chain']['state_tree_ref'] == reference(chain['tree']) and
            binding['chain']['state_mesh_ref'] == reference(chain['mesh']), 'Crossed M16 typed chain')
    chain.update({key: binding[key] for key in ('skeleton_sha256', 'rig', 'clip_sha256', 'mesh_sha256',
                                              'lod_sha256', 'common_index_sha256', 'weapon_index_sha256')})
    chain.update(mesh_skeleton=reference(chain['skeleton']), closed_sha256=group['enrolled_pose_clip_hashes'][0])
    require(chain['clip_sha256'] == group['reload']['sha256'], 'M16 reload coordinates differ')
    barrel, root, child = 'jntWpn_7', 'jntWpnwpnJnt_16', 'jntWpn_17'
    source = family_source('M16A2', chain, group, names, rows, variants, paths,
                           [barrel, root, child], 'SingleRoundBreech', 'Linear')
    source['policy'].update(opening='Measured 95.11144 mm barrel slide with same-frame5 anatomical wrist/shape; 35 mm wrist tolerance.',
        closing='Reverse measured slide endpoints using the same captured grip; closure required.',
        supply='Freeze measured rigid16+17 assembly; same-frame25 wrist and complete finger shape.',
        clearance='Seated flange overlaps render bore; visual seating is not collision-clearance or chamber-lock proof.')
    opening, round_data = aperture['opening'], aperture['round']
    require(round_data['max_child_relation_matrix_deviation'] == 0 and
            round_data['case_part'] == root and round_data['nose_part'] == child and
            opening['plateau_travel_m'] == review['mechanism']['open_travel_m'], 'M16 measured assembly/slide mismatch')
    parts = {bone: dict(bone_index=source['skeleton_bones'][bone]['index'], parent=p['parent'],
        bounds=p['local_bounds'], mixed_triangles=0, visible_triangles=p['visible_triangles'], sections=p['sections'])
        for bone, p in data['parts'].items() if bone in (barrel, root, child)}
    require(all(p['visible_triangles'] == sum(s['rigid_triangles'] for s in p['sections']) for p in parts.values()),
            'M16 selected visible triangles are not entirely rigid')
    s5, s25 = data['snapshots']['5'], data['snapshots']['25']
    measured = dict(schema=MEASUREMENT_SCHEMA, family='M16A2', coordinates=coordinate_tuple(chain), parts=parts,
        mechanism=dict(bone=barrel, motion_kind='Linear', closed_sample=0, open_sample=7,
            closed=opening['closed_barrel_to_weapon'], open=opening['open_barrel_to_weapon'],
            grasp=dict(sample=5, hand_to_part=s5['hand_to_barrel'], fingers_to_part=s5['fingers_to_barrel'],
                anatomical_proximity=opening['contact'])),
        supply=dict(root=root, parts=[root, child], source_sample=25, seat_sample=45, assembly_sample=45,
            source=data['samples'][25]['case_to_weapon'], destination=data['samples'][45]['case_to_weapon'],
            seat_in_mechanism=round_data['terminal_case_to_barrel'],
            part_in_root={root: mag.identity(), child: round_data['nose_to_case']},
            grasp=dict(sample=25, hand_to_part=s25['hand_to_case'], fingers_to_part=s25['fingers_to_case'],
                anatomical_proximity=round_data['supply_contact'])),
        bore=dict(inward_part=aperture['inward_axis_barrel'], rear_mouth=aperture['rear_mouth'],
            derived_straight_entry=round_data['derived_straight_entry_case_to_barrel'],
            derived_straight_rail_m=round_data['derived_straight_rail_m'], rail_enforced=False,
            clearance_limit=aperture['clearance_limit']),
        evidence_level='Exact offline geometry plus explicit VR policy; no native/game/headset qualification.')
    validate_family(source, measured)
    write_new(Path(output) / 'source.json', source, replace)
    write_new(Path(output) / 'measurements.json', measured, replace)
    return {'prepared_family': source['family'], 'configurations': len(names)}


REPORT73_FAMILIES = {
    'aek': ('AEK_GP30', 11, 83, 'retained_family_coordinates'),
    'hk416': ('HK416', 9, 76, 'retained_nine'),
    'f2000': ('F2000', 11, 78, 'retained_eleven'),
    'aug': ('AUG_M203', 9, 81, 'retained_nine'),
}


def report73_inputs(reports, slug):
    """Close every new typed join against the retained, reviewed byte tuples."""
    family, count, samples, retained = REPORT73_FAMILIES[slug]
    names = [slug + '-loading-geometry.json', slug + '-measurements.json']
    if slug != 'aek':
        names.append(slug + '-frames-aperture.json')
    paths, values, group, configurations, rows, variants = report72_inputs(reports, family, names)
    review, data = (values[name] for name in names[:2])
    require(review['done'] and review['review_image_inspected'] and review['family'] == family and
            review['configuration_count'] == len(configurations) == count,
            'Exact report73 family review mismatch')
    for name, expected in review['files'].items():
        path = Path(reports) / name
        require(pin(path)['sha256'] == expected, 'Reviewed report73 packet changed: ' + name)
        paths[name] = path
    for name, expected in {**data.get('source_pins', {}), **data.get('decoder_related_tool_pins', {})}.items():
        path = ROOT / name.replace('\\', '/')
        require(pin(path)['sha256'] == expected, 'Report73 source pin changed: ' + name)
        paths[name] = path
    if 'catalog_sha256' in data:
        require(data['catalog_sha256'] == pin(paths['catalog'])['sha256'], 'Report73 catalog changed')
    decoder = ROOT / 'tools/bc2_granny_curves.py'
    require(pin(decoder)['sha256'] == data['decoder_sha256'] == review['pins']['decoder'],
            'Report73 decoder changed')
    paths['decoder'] = decoder
    require(not data['failures'] and not data['clip_summary']['unsupported_tracks'] and
            data['sample_count'] == len(data['samples']) == samples and
            [s['i'] for s in data['samples']] == list(range(samples)) and
            data[retained] == variants and len(variants) == count, 'Incomplete report73 measured coordinates')
    joins = {r['configuration']: r for r in data['typed_chains']}
    require(len(joins) == len(data['typed_chains']) == count and set(joins) == set(configurations),
            'Incomplete exact report73 typed joins')
    first = joins[configurations[0]]
    chain = {key: normalized_typed(first[key]) for key in ('config', 'tree', 'reload', 'mesh', 'skeleton')}
    archive = data['archives'][0] if slug == 'aek' else data['archive']
    hashes = archive['hashes']
    require(archive['archive'] == group['representative']['archive'] and
            archive['index_sha256'] == group['representative']['index_sha256'] and
            hashes['clip'] == group['reload']['sha256'] == review['pins']['reload'] and
            hashes['mesh'] == group['mesh_sha256'] == review['pins']['mesh'] and
            hashes['lod'] == group['lod0_sha256'] == review['pins']['lod0'] and
            [hashes['pose'], hashes['alt']] == group['enrolled_pose_clip_hashes'] and
            data['rig'] == review['pins']['rig'], 'Crossed report73 byte-coordinate tuple')
    chain.update(mesh_skeleton=first['skeleton_reference'], skeleton_sha256=data['skeleton_sha256'], rig=data['rig'],
        clip_sha256=hashes['clip'], closed_sha256=hashes['pose'], mesh_sha256=hashes['mesh'], lod_sha256=hashes['lod'],
        common_index_sha256=data['common']['index_sha256'], weapon_index_sha256=archive['index_sha256'])
    resources = {r['resource']: r['sha256'] for r in data['common']['selected_resources']}
    for row in rows:
        joined = joins[row['configuration']]
        require(joined['state'] == 0 and normalized_typed(joined['config']) == row['source'] and all(
            normalized_typed(joined[key]) == chain[key] for key in ('tree', 'reload', 'mesh', 'skeleton')) and
            joined['skeleton_reference'] == chain['mesh_skeleton'] and
            joined['replacement_reference'] == reference(chain['reload']), 'Crossed report73 typed join')
        for key in ('config', 'tree', 'reload', 'mesh', 'skeleton'):
            require(resources[joined[key]['resource']] == joined[key]['sha256'],
                    'Report73 typed source not in pinned common resources')
    return paths, values, group, configurations, rows, variants, chain, review, data


def report73_parts(source, data, bones):
    parts = {}
    for bone in bones:
        part = data['part_geometry'][bone]
        parts[bone] = dict(bone_index=source['skeleton_bones'][bone]['index'], parent=part['parent'],
            bounds=[part['minimum'], part['maximum']], mixed_triangles=part['mixed_triangles'],
            visible_triangles=sum(s['triangles'] for s in part['sections'] if not s['name'].endswith('_ZOnly')),
            sections=part['sections'])
    return parts


def snapshot_grasp(frames, bone, sample, proximity):
    inverse = mag.inverse_rigid(frames[bone])
    return dict(sample=sample, hand_to_part=mag.multiply(frames['LeftHand'], inverse),
        fingers_to_part={finger: mag.multiply(frames[finger], inverse) for finger in mag.FINGERS},
        anatomical_proximity=proximity)


def prepare_report73(reports, output, slug, replace=False):
    paths, values, group, names, rows, variants, chain, review, data = report73_inputs(reports, slug)
    family = group['id']
    if slug == 'aek':
        barrel, root, child = 'jntWpn_1', 'jntWpn_12', 'jntWpn_13'
        source = family_source(family, chain, group, names, rows, variants, paths,
            [barrel, root, child], 'SingleRoundMuzzle', 'Fixed')
        load = data['loading_geometry']
        require(load['launcher_skin_owner'] == barrel and load['child13_in_round12_max_component_spread'] == 0 and
                data['part_geometry'][barrel]['maximum_displacement_m'] == 0,
                'AEK fixed tube or rigid supply assembly changed')
        frames = load['phase_frames']['30']['hand_round_and_finger_frames_weapon']
        closed = data['part_geometry'][barrel]['closed_part_in_weapon']
        mechanism = dict(bone=barrel, motion_kind='Fixed', closed=closed, open=closed)
        supply = dict(root=root, parts=[root, child], source_sample=30, seat_sample='HandsIKPose',
            assembly_sample='HandsIKPose', source=frames[root], destination=load['round_seat_in_weapon'],
            seat_in_mechanism=mag.multiply(load['round_seat_in_weapon'], mag.inverse_rigid(closed)),
            part_in_root={root: mag.identity(), child: load['child13_in_round12_closed']},
            grasp=snapshot_grasp(frames, root, 30, load['contacts'][30]['minimum']))
        parts = report73_parts(source, data, [root, child])
        bore = dict(inward_part=load['inward_axis_weapon'], mouth_center_part=load['muzzle_center_weapon'],
            mouth_frame=load['mouth_frame_weapon'], polygon_min_inradius_m=load['muzzle_polygon_minimum_edge_radius_m'],
            static_aligned_radial_clearance_m=load['aligned_radial_clearance_m'], rail_enforced=False,
            swept_collision_proved=False)
        source['policy'].update(opening='Fixed launcher tube: no Open or Close gesture exists.',
            closing='No closure required for this measured fixed tube.',
            supply='Freeze exact HandsIKPose rigid12+13 assembly; same-frame30 wrist and complete finger shape.',
            clearance='Static radial gap and stock approach do not prove swept clearance; no bore rail is enforced.')
    else:
        aperture = values[slug + '-frames-aperture.json']
        opening, round_data = aperture['opening'], aperture['round']
        barrel, root, child = opening['part'], round_data['case'], round_data['nose']
        expected = {'hk416': ('jntWpn_9', 'jntWpn_12', 'jntWpn_13', 30),
                    'f2000': ('jntWpn_5', 'jntWpn_12', 'jntWpn_13', 12),
                    'aug': ('jntWpn_12', 'jntWpn_13', 'jntWpnwpnJnt_14', 27)}[slug]
        require((barrel, root, child) == expected[:3] and round_data['child_relation_max_component_deviation'] == 0 and
                opening['close_required'] and opening['open_travel_m'] == review['mechanism']['open_travel_m'],
                'Report73 distinct measured assembly/slide mismatch')
        selected_bones = [barrel, root, child] + (['jntWpn_9'] if slug == 'aug' else [])
        source = family_source(family, chain, group, names, rows, variants, paths,
            selected_bones, 'SingleRoundBreech', 'Linear')
        # Reload sample0 and open use the same decoded basis. HandsIKPose differs
        # by small measured rotations in HK416/AUG; do not relax linear validation
        # or silently zero either measured orientation to combine those clips.
        closed, opened = data['samples'][0]['parts'][barrel], data['samples'][expected[3]]['parts'][barrel]
        require(opened == opening['open_barrel_to_weapon'], 'Report73 open sample changed')
        open_sample, supply_sample = opening['contact_sample'], round_data['supply_sample']
        open_frames = aperture['snapshots'][str(open_sample)]['weapon_relative']
        supply_frames = aperture['snapshots'][str(supply_sample)]['weapon_relative']
        mechanism = dict(bone=barrel, motion_kind='Linear', closed_sample=0, open_sample=expected[3],
            closed=closed, open=opened,
            grasp=snapshot_grasp(open_frames, barrel, open_sample, opening['contact']))
        seat_key = {'hk416': 'terminal_case_to_barrel', 'f2000': 'canonical_seat_case_to_barrel',
                    'aug': 'seated_case_to_barrel'}[slug]
        seat = round_data[seat_key]
        seat_sample = 'HandsIKPose' if slug == 'f2000' else round_data['seat_sample']
        supply = dict(root=root, parts=[root, child], source_sample=supply_sample, seat_sample=seat_sample,
            assembly_sample=seat_sample, source=supply_frames[root], destination=mag.multiply(seat, opened),
            seat_in_mechanism=seat, part_in_root={root: mag.identity(), child: round_data['nose_to_case']},
            grasp=snapshot_grasp(supply_frames, root, supply_sample, round_data['supply_contact']))
        parts = report73_parts(source, data, selected_bones)
        if slug == 'aug':
            extra = aperture['additional_moving_part']
            require(extra['part'] == 'jntWpn_9' and extra['parent'] == 'jntWpn_1' and
                    extra['open_part_to_weapon'] == data['samples'][expected[3]]['parts']['jntWpn_9'],
                    'Crossed AUG independent render part')
            mechanism['render_parts'] = {'jntWpn_9': dict(motion_kind='Linear', semantic_role_proved=False,
                closed=extra['closed_part_to_weapon'], open=extra['open_part_to_weapon'],
                closed_sample='HandsIKPose', open_sample=expected[3],
                maximum_delta_difference_from_barrel_m=extra['maximum_delta_difference_from_barrel_m'])}
            source['policy']['additional_render_motion'] = (
                'Part9 has independent measured endpoints with no proved semantic role. '
                'Linear rendering must interpolate each part endpoint using recognized progress, not one barrel delta.')
        bore = dict(inward_part=aperture['aperture']['inward_axis_barrel'],
            mouth_frame=aperture['aperture']['mouth_frame_barrel'],
            polygon_min_inradius_m=aperture['aperture']['rear_polygon_inradius_m'],
            rail_enforced=False, swept_collision_proved=False,
            clearance_limit=aperture.get('clearance_limit', aperture.get('clearance')))
        source['policy'].update(opening='Exact reload0/open%d linear endpoints; same-frame5 wrist/shape and 35 mm wrist tolerance.' % expected[3],
            closing='Reverse measured reload endpoints with the same captured grip; closure required.',
            supply='Freeze measured rigid assembly; same-frame%d wrist and complete finger shape.' % supply_sample,
            clearance='Render seating does not establish swept clearance or chamber lock; no bore rail is enforced.')
    measured = dict(schema=MEASUREMENT_SCHEMA, family=family, coordinates=coordinate_tuple(chain),
        parts=parts, mechanism=mechanism, supply=supply, bore=bore,
        evidence_level='Exact offline geometry plus explicit VR policy; no native/game/headset qualification.')
    source['measurements_sha256'] = digest(measured)
    validate_family(source, measured)
    write_new(Path(output) / 'source.json', source, replace)
    write_new(Path(output) / 'measurements.json', measured, replace)
    return {'prepared_family': family, 'configurations': len(names)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    adapters = {'prepare-xm8': prepare_xm8, 'prepare-an94': prepare_an94, 'prepare-m16': prepare_m16}
    for slug in REPORT73_FAMILIES:
        adapters['prepare-' + slug] = lambda reports, output, replace=False, slug=slug: prepare_report73(
            reports, output, slug, replace)
    for name in adapters:
        prepare = commands.add_parser(name)
        prepare.add_argument('--reports', type=Path, required=True)
        prepare.add_argument('--output', type=Path, required=True)
        prepare.add_argument('--replace', action='store_true')
    generate_command = commands.add_parser('generate')
    generate_command.add_argument('--family', type=Path, action='append', required=True)
    generate_command.add_argument('--output', type=Path, required=True)
    generate_command.add_argument('--verify', action='store_true')
    generate_command.add_argument('--replace', action='store_true')
    args = parser.parse_args()
    result = (adapters[args.command](args.reports, args.output, args.replace) if args.command in adapters else
              generate(args.family, args.output, args.verify, args.replace))
    print(json.dumps(result))


if __name__ == '__main__':
    main()
