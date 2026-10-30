"""Bounded exact-source fixed-container geometry authoring; never native admission.

The sample command is an installed-resource extraction job and must be run by
the coordinator after StorageBudget. It writes only derived JSON, never assets.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
import bc2_authored_action_geometry as action
import bc2_authored_magazine_geometry as mag
import bc2_authored_grip_bindings as grip
from bc2_authored_archive_set import AuthoredArchiveSet
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip, canonical
from bc2_weapon_config_pipeline import Resolver, parse, named, guid, effective_weapon
from bc2_weapon_mesh_bindings import reference_resource
from bc2_mesh_geometry import metadata, geometry, position_bytes
from bc2_body_ammo_assets import fnv
from inspect_bc2_mesh_asset import bone_hash

ROOT = Path(__file__).resolve().parents[1]
RECOVERY = Path('G:/Unity/VRMMO/bc2vr-recovery')
HANDOFF = ROOT / 'handoff/tasks/WEAPONS/20261030-special46'
COMMON = {'archive': 'Dist/win32/levels/mp_common/level-00.fbrb',
          'archive_index_sha256': '6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965'}
SOURCES = {
    'MP412': (RECOVERY / 'pistol-sniper-shotgun-mechanisms-candidate/batch/reference-bindings.json',
              'MP412', {'archive': 'Dist/win32/async/weapon/mec_hg_mp412rex-00.fbrb',
                        'archive_index_sha256': 'acda9b6d001d6343ca433cc8038e12468f5852d3a441fc861386975673fb325c'}),
    'M1Garand': (ROOT / 'handoff/tasks/WEAPONS/20261030-support43/m1-reference-bindings.json', 'Garand', COMMON),
    'Type5': (RECOVERY / 'overnight426/smg/opening-level-reference-bindings.json',
              'Garand', {'archive': 'Dist/win32/levels/sp_01/level-00.fbrb',
                         'archive_index_sha256': '9693feb53337f1a83c49d725eab25d146e91469394c3b93e7bf62214ecc8a44f'}),
}
RIGHT_FINGERS = [n.replace('LeftHand', 'RightHand') for n in mag.FINGERS]


def load(path):
    raw = Path(path).read_bytes()
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('JSON size bound')
    return json.loads(raw.decode('utf-8-sig'))


def write_new(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf8') as stream:
        stream.write(json.dumps(value, indent=2, allow_nan=False) + '\n')


def source_meshes(profiles, source):
    # Explicit source location, exact resource bytes checked again by context.
    rows = []
    for p in profiles:
        rows.append({'native_name': p['native_asset_name'], 'resource': p['weapon']['resource'],
                     'instance_guid': p['weapon']['instance_guid'], 'resource_sha256': p['weapon']['sha256'],
                     'states': [{'index': p['state_index'], 'meshes': [
                         {**m, 'geometry_sources': [{'geometry_status': 'derived', 'archive': source['archive'],
                                                    'index_sha256': source['archive_index_sha256']}]}
                         for m in p['meshes']]}]})
    return {'schema': 'fvr.bc2.authored_weapon_mesh_bindings', 'schema_version': 1, 'weapons': rows}


def native_closure(game, pool, profiles):
    sources = []
    for p in profiles:
        for s in [p.get('configuration_source', p['skeleton_source']), *p.get('reference_sources', [])]:
            if s not in sources:
                sources.append(s)
    archives = []
    for s in sources:
        archive = pool(game / s['archive'])
        if archive.index_sha256 != s['archive_index_sha256']:
            raise ValueError('Configuration closure archive changed')
        archives.append((s['archive'], archive))
    aset = AuthoredArchiveSet(archives)
    docs = {}
    receipts = {}
    def read(names):
        missing = {grip.path_key(n) for n in names} - docs.keys()
        if not missing:
            return
        blobs = aset.read(missing)
        if set(blobs) != missing:
            raise ValueError('Configuration closure absent')
        for key, (raw, receipt) in blobs.items():
            docs[key] = parse(receipt['resource'], raw)
            receipts[key] = {**receipt, 'sha256': hashlib.sha256(raw).hexdigest()}
    read([p['weapon']['resource'] for p in profiles])
    weapons = []
    for p in profiles:
        doc = docs[grip.path_key(p['weapon']['resource'])]
        if doc.sha256 != p['weapon']['sha256']:
            raise ValueError('Configuration content changed')
        weapons.append((doc, doc.instances[guid(p['weapon']['instance_guid'])]))
    read([reference_resource(named(w, 'WeaponFiring').attributes['ref']) for d, w in weapons])
    resolver = Resolver(list(docs.values()))
    firing = [resolver.resolve(d, named(w, 'WeaponFiring').attributes['ref'],
                               'GameSharedResources.WeaponFiringData') for d, w in weapons]
    read([reference_resource(named(w, 'PrimaryFire').attributes['ref']) if '/' in named(w, 'PrimaryFire').attributes['ref'] else d.resource
          for d, w in firing])
    resolver = Resolver(list(docs.values()))
    return [effective_weapon(resolver, d, w) for d, w in weapons], list(receipts.values())


def cache_metadata(ctx, p, pool, game):
    """Same section hashes as the installed body-ammo cache, without payload."""
    source = ctx['receipt']['mesh_source']
    archive = pool(game / source['archive'])
    mesh = next(m for m in p['meshes'] if m['configured_mesh_path'] == p['configured_mesh_path'])
    lodspec = next(x for x in mesh['geometry']['lods'] if x['lod'] == 0)
    raw = mag.exact_resource(archive, mesh['mesh_resource'], 'SkinnedMeshSet', mesh['geometry']['mesh_sha256'])
    data = mag.exact_resource(archive, lodspec['data_resource'], 'MeshData', lodspec['data_sha256'])
    sk = ctx['skeleton']; known = {bone_hash(n): n for n in sk.names}
    lod = metadata(raw)[0]; sections = geometry(data, lod, known)
    vb = sum(s['vertices'] * s['stride'] for s in sections)
    ib = sum(s['triangles'] * 6 for s in sections)
    start = len(data) - vb - ib
    output = {}
    for part in ctx['parts']:
        rows = []
        for s in sections:
            if s['name'].endswith('_ZOnly') or part not in s['bone_names']:
                continue
            palette = [known[lod['palette'][n]] for n in s['palette']]
            pi = palette.index(part); pb = position_bytes(s)
            indices = struct.unpack_from('<' + 'H' * (s['triangles'] * 3), data, start + vb + s['first_index'] * 2)
            skin, positions, owners = bytearray(), bytearray(), []
            for n in indices:
                at = start + s['vertex_offset'] + n * s['stride']; v = data[at:at + pb + 8]
                skin += v; positions += v[:pb]; weights = v[pb + 4:pb + 8]
                if sum(w > 0 for w in weights) != 1 or 255 not in weights:
                    raise ValueError('Supply cache needs rigid authored skin')
                owners.append(v[pb + weights.index(255)])
            count = sum(owners[n] == pi for n in range(0, len(owners), 3)
                        if len(set(owners[n:n + 3])) == 1)
            if any(len(set(owners[n:n + 3])) != 1 for n in range(0, len(owners), 3)):
                raise ValueError('Supply cache cross-part triangle')
            if count:
                rows.append(dict(section=s['name'], count=len(indices), stride=s['stride'], palette_size=len(palette),
                                 part_palette_index=pi, position=s['position_format'], vertex_skin_hash=f'{fnv(skin):016x}',
                                 position_hash=f'{fnv(positions):016x}', part_triangles=count))
        output[part] = dict(asset=p['native_asset_name'], archive=source['archive'],
                            archive_index_sha256=source['archive_index_sha256'], mesh=p['configured_mesh_path'],
                            mesh_sha256=ctx['receipt']['mesh_sha256'], lod_sha256=ctx['receipt']['lod_sha256'], part=part,
                            rig_fingerprint=int(sk.fingerprint.split(':')[1], 16),
                            inverse_bind=canonical(sk.bones[sk.names.index(part)]['InverseWorldTransform']), sections=rows)
    return output


def sample(game, output, selected=None):
    pool = ArchivePool(game, COMMON['archive'], COMMON['archive_index_sha256'])
    result = {'schema': 'fvr.bc2.fixed_container_samples.v1', 'sample_hz': 30, 'models': [], 'inputs': [],
              'native_checked': False, 'headset_accepted': False}
    for label, (path, asset, mesh_source) in SOURCES.items():
        if selected and label not in selected:
            continue
        document = load(path)
        result['inputs'].append({'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        profiles = [p for p in document['profiles'] if p['native_asset_name'] == asset]
        if len(profiles) != (2 if label == 'M1Garand' else 1):
            raise ValueError('Exact configuration multiplicity changed')
        native, closure = native_closure(game, pool, profiles)
        meshes = source_meshes(profiles, mesh_source)
        p = profiles[0]; ctx = action.context(game, p, meshes, pool)
        actions = mag.bound_action_assets(p, ctx['graph_archive'], ['1p_reload'], ctx['graph_dependencies'])
        if set(actions) != {'1p_reload'}:
            raise ValueError('Exact reload role required')
        act = actions['1p_reload']; raw, source = mag.selected_animation_resource(ctx['sources'], act['resource'])
        clip = Clip(raw); sk = ctx['skeleton']; names = sorted(set(ctx['parts']) | {'jntWpn_1', 'LeftHand', 'RightHand', *mag.FINGERS, *RIGHT_FINGERS})
        if not .2 <= clip.duration <= 15 or len(names) > 150:
            raise ValueError('Sample duration or part count bound')
        parents = {n: sk.names[sk.parents[sk.names.index(n)]] for n in ctx['parts']}
        names = sorted(set(names) | set(parents.values()))
        frames = []; gaps = []
        def evaluate(clip, t, stage):
            try:
                return clip.evaluate(sk, t, names)['weapon_relative']
            except ValueError:
                values = {}
                for name in names:
                    try:
                        values[name] = clip.evaluate(sk, t, [name])['weapon_relative'][name]
                    except ValueError as error:
                        gaps.append({'stage': stage, 'bone': name, 'time_s': t, 'reason': str(error)})
                return values
        for i in range(math.ceil(clip.duration * 30) + 1):
            t = min(i / 30, clip.duration)
            frames.append({'time_s': t, 'weapon_relative': evaluate(clip, t, 'reload')})
        reference = evaluate(ctx['reference'], 0, 'reference')
        parts = {n: {k: part[k] for k in ('triangles', 'mixed_triangles', 'minimum', 'maximum', 'extent', 'sections')}
                 for n, part in ctx['parts'].items()}
        for n, part in parts.items():
            part['parent'] = parents[n]
            part['bone_index'] = sk.names.index(n)
            part['center_local_m'] = [(x + y) / 2 for x, y in zip(part['minimum'], part['maximum'])]
        result['models'].append(dict(label=label, native= native, configuration_closure=closure,
            bindings=[{'weapon': q['weapon'], 'state_index': q['state_index'], 'binding_digest': q['binding_digest']} for q in profiles],
            mesh=p['configured_mesh_path'], mesh_binding=p['meshes'][0], rig_fingerprint=sk.fingerprint, skeleton_sha256=sk.sha256,
            reload={**act, **source, 'sha256': clip.sha256, 'duration_s': clip.duration, 'tree': p['animation_chain']['tree']},
            reference={'resource':p['animation_chain']['animation_resource'], 'sha256':ctx['reference'].sha256,
                       'source':p['animation_source'], 'frames':reference},
            parts=parts, frames=frames, gaps=gaps, cache_metadata=cache_metadata(ctx, p, pool, game)))
        write_new(output.with_name(output.stem + '-' + label + output.suffix), result['models'][-1])
        print(json.dumps({'sampled': label, 'frames': len(frames), 'parts': len(parts), 'gaps': len(gaps),
                          'source_bytes_cached': pool.bytes}), flush=True)
    write_new(output, result)


def rigid(value):
    """Reject reflection, scale, malformed affine columns, and nonfinite data."""
    mag.matrix(value)
    mag.inverse_rigid(value)
    if any(abs(value[k]) > 1e-5 for k in (3, 7, 11)) or abs(value[15] - 1) > 1e-5:
        raise ValueError('Improper affine frame')
    determinant = mag.dot(value[:3], mag.cross(value[4:7], value[8:11]))
    if abs(determinant - 1) > 2e-4:
        raise ValueError('Improper rigid frame')
    return value


def at(model, index, bone):
    if not 0 <= index < len(model['frames']):
        raise ValueError('Reviewed sample outside source clip')
    try:
        return rigid(model['frames'][index]['weapon_relative'][bone])
    except KeyError as error:
        raise ValueError('Reviewed source frame is absent; do not substitute') from error


def contact(model, index, bone, hand='LeftHand'):
    finger_names = mag.FINGERS if hand == 'LeftHand' else RIGHT_FINGERS
    wrist = at(model, index, hand)
    inverse_wrist = mag.inverse_rigid(wrist)
    pose = mag.multiply(wrist, mag.inverse_rigid(at(model, index, bone)))
    fingers = {n.replace(hand, 'LeftHand'): mag.multiply(at(model, index, n), inverse_wrist) for n in finger_names}
    bounds = model['parts'][bone]
    center = [sum(mag.point(at(model, index, hand + d + '1')[12:15],
                           mag.inverse_rigid(at(model, index, bone)))[k]
                  for d in ('Index', 'Middle', 'Ring', 'Pinky')) / 4 for k in range(3)]
    distance = mag.norm([max(bounds['minimum'][k] - center[k], 0., center[k] - bounds['maximum'][k]) for k in range(3)])
    return {'part_from_wrist': pose, 'wrist_from_fingers': fingers, 'sample': index,
            'time_s': model['frames'][index]['time_s'], 'bone': bone, 'source_hand': hand, 'knuckle_bounds_distance_m': distance,
            'claim': 'fixed mod grip from one exact authored reload frame; no native or continuous contact calibration'}


def policy_motion(operation, kind):
    return dict(operation=operation, kind=kind, measured=True, returnToStart=False,
                pathToleranceMeters=.012, orientationToleranceRadians=.35,
                maxStepMeters=.06, maxStepRadians=.5, endpointDwellNs=60000000,
                maximumGestureNs=10000000000, maxInputGapNs=100000000)


def placement(model, bone, source, destination, grasp, operation='SeatFeed'):
    motion = policy_motion(operation, 'FreePlacement')
    motion['placement'] = {'weaponFromSource': at(model, source, bone),
                           'weaponFromDestination': at(model, destination, bone),
                           'sourceFromGrip': grasp['part_from_wrist']}
    return motion


def hinge(model, bone, start, end, grasp):
    from bc2_authored_mechanism_geometry import axis_angle
    first, last = at(model, start, bone), at(model, end, bone)
    delta = mag.multiply(last, mag.inverse_rigid(first))
    axis, angle = axis_angle(delta)
    if axis is None or not .2 < angle < math.pi or mag.norm(mag.sub(first[12:15], last[12:15])) > .002:
        raise ValueError('Reviewed segment is not a bounded hinge')
    # Relative axis is in the first part's coordinates. Construct an exact
    # right-handed motion frame with local +Z along that measured axis.
    z = mag.unit([sum(axis[k] * first[k * 4 + j] for k in range(3)) for j in range(3)])
    helper = [0., 1., 0.] if abs(z[1]) < .9 else [1., 0., 0.]
    x = mag.unit(mag.cross(helper, z)); y = mag.cross(z, x)
    frame = mag.frame(x, y, z, first[12:15])
    wrist = mag.multiply(grasp['part_from_wrist'], first)
    motion = policy_motion('OpenCover', 'Hinge')
    motion.update(weaponFromMotion=frame, motionFromContact=mag.multiply(wrist, mag.inverse_rigid(frame)),
                  travel=angle, completeFraction=.92, releaseFraction=.25)
    return motion


def along(frame, kind, travel):
    if kind == 'Linear':
        value = list(frame); value[12] += travel; return value
    turn = mag.identity(); c, s = math.cos(travel), math.sin(travel)
    turn[:6] = [c, s, 0., 0., -s, c]
    return mag.multiply(frame, turn)


def validate_motion(m):
    if not m['measured'] or m['kind'] not in ('Linear', 'Hinge', 'FreePlacement'):
        raise ValueError('Explicit physical motion required')
    for key in ('pathToleranceMeters', 'orientationToleranceRadians', 'maxStepMeters', 'maxStepRadians'):
        if not math.isfinite(m[key]) or m[key] <= 0:
            raise ValueError('Motion parameter invalid')
    if m['orientationToleranceRadians'] >= math.pi / 2 or m['maxStepRadians'] >= math.pi:
        raise ValueError('Motion angle bound')
    if not 0 < m['endpointDwellNs'] < m['maximumGestureNs'] or not 0 < m['maxInputGapNs'] <= m['maximumGestureNs']:
        raise ValueError('Gesture timing bound')
    if m['kind'] == 'FreePlacement':
        p = m['placement']
        for value in p.values():
            rigid(value)
        distance, angle = mag.grasp_separation(p['weaponFromSource'], p['weaponFromDestination'])
        if not (distance > 2 * m['pathToleranceMeters'] or angle > 2 * m['orientationToleranceRadians']):
            raise ValueError('Source and destination capture regions overlap')
        if not (distance > m['maxStepMeters'] or angle > m['maxStepRadians']):
            raise ValueError('Single-packet teleport can complete insertion')
    else:
        rigid(m['weaponFromMotion']); rigid(m['motionFromContact'])
        if not .5 < m['completeFraction'] <= 1 or not 0 < m['releaseFraction'] < m['completeFraction']:
            raise ValueError('Motion completion fraction bound')
        if m['kind'] == 'Hinge':
            if not m['maxStepRadians'] < abs(m['travel']) < math.pi:
                raise ValueError('Hinge angular travel bound')
            if math.hypot(*m['motionFromContact'][12:14]) <= m['pathToleranceMeters'] * 2:
                raise ValueError('Hinge contact is too close to axis')
        elif m['maxStepMeters'] >= abs(m['travel']) or m['pathToleranceMeters'] >= abs(m['travel']) * m['releaseFraction']:
            raise ValueError('Linear travel bound')


def cpp_float(value):
    value = format(value, '.9g')
    return value + ('f' if any(c in value for c in '.eE') else '.f')


def cpp_matrix(value):
    rigid(value)
    return 'math::Matrix4{{{' + ','.join('{' + ','.join(cpp_float(x) for x in value[n:n + 4]) + '}' for n in range(0, 16, 4)) + '}}}'


def cpp_header(document):
    rows = ['#pragma once', '// Exact authored source geometry and explicit mod policy; no native/headset qualification.',
            '#include "fvr/interaction/FixedContainerReload.h"', '#include "fvr/interaction/AuthoredHandPose.h"',
            '#include <array>', '#include <span>', '#include <string_view>', 'namespace fvr::bc2::generated {',
            'struct FixedContainerPartGeometry {',
            ' std::string_view bone; std::uint32_t boneIndex=0; math::Matrix4 centerInBone{};',
            ' math::Matrix4 weaponFromClosed{},weaponFromOpen{},supplyFromPart{};',
            ' std::array<math::Matrix4,4> weaponFromStepStart{},weaponFromStepEnd{};',
            ' bool supplyPart=false,coupledOpening=false;', '};',
            'struct FixedContainerEquipmentProfile {',
            ' std::string_view asset,configurationPath,configurationGuid,configurationSha256,mesh,meshSha256,skeletonSha256;',
            ' std::uint64_t rigFingerprint=0; interaction::FixedContainerProfile interaction{};',
            ' std::array<const interaction::AuthoredHandShape*,4> handShapes{};',
            ' std::span<const FixedContainerPartGeometry> parts{};',
            ' std::span<const std::string_view> hiddenSupplyBones{};',
            ' std::string_view supplyRoot; math::Matrix4 supplyCenterInRoot{};', '};']
    for i, model in enumerate(document['models']):
        prefix = f'FixedContainer{i}'
        for step, grasp in model['grasps'].items():
            name = prefix + step + 'Hand'
            rows.append('inline const interaction::AuthoredHandShape ' + name + '=[]{interaction::AuthoredHandShape out{};out.counts={3,3,3,3,3};')
            for j, bone in enumerate(mag.FINGERS):
                rows.append(f'out.wristFromJoint[{j // 3}][{j % 3}]=' + cpp_matrix(grasp['wrist_from_fingers'][bone]) + ';')
            rows.append('return out;}();')
        rows.append(f'inline const std::array<FixedContainerPartGeometry,{len(model["parts"])}> {prefix}Parts=[]' + '{')
        rows.append(f'std::array<FixedContainerPartGeometry,{len(model["parts"])}> out' + '{};')
        for n, part in enumerate(model['parts']):
            rows.append('{auto& p=out[' + str(n) + '];p.bone=' + json.dumps(part['bone']) + f';p.boneIndex={part["bone_index"]};')
            for key in ('centerInBone', 'weaponFromClosed', 'weaponFromOpen', 'supplyFromPart'):
                rows.append('p.' + key + '=' + cpp_matrix(part[key]) + ';')
            for key in ('weaponFromStepStart', 'weaponFromStepEnd'):
                for k, value in enumerate(part[key]):
                    rows.append(f'p.{key}[{k}]=' + cpp_matrix(value) + ';')
            rows.append('p.supplyPart=' + str(part['supplyPart']).lower() + ';p.coupledOpening=' + str(part['coupledOpening']).lower() + ';}')
        rows.append('return out;}();')
        rows.append(f'inline constexpr std::array<std::string_view,{len(model["supply_parts"])}> {prefix}Hidden' + '{' + ','.join(map(json.dumps, model['supply_parts'])) + '};')
        rows.append(f'inline const interaction::FixedContainerProfile {prefix}Interaction=[]' + '{interaction::FixedContainerProfile p{};')
        profile = model['interaction']
        rows.append(f'p.kind=interaction::FixedContainerKind::{profile["kind"]};p.id=0x{profile["id"]:016x}ull;p.revision={profile["revision"]};')
        for key in ('ammunitionKind', 'capacity', 'roundsPerInsertion', 'maxAgeNs'):
            rows.append(f'p.{key}={profile[key]};')
        for step, value in profile['geometry'].items():
            index = ('Open', 'EjectSpent', 'Insert', 'Close').index(step)
            rows.append('{auto& g=p.geometry[' + str(index) + '];')
            c = value['contact']; m = value['motion']
            rows.append('g.contact={' + str(c['role']) + 'u,' + cpp_float(c['radiusMeters']) + ',' + cpp_float(c['maxAngleRadians']) + '};auto& m=g.motion;')
            for key, v in m.items():
                if key in ('operation', 'kind'):
                    typ = 'FeedPhysicalOperation' if key == 'operation' else 'FeedMotionKind'
                    rows.append(f'm.{key}=interaction::{typ}::{v};')
                elif key == 'placement':
                    rows.extend('m.placement.' + k + '=' + cpp_matrix(frame) + ';' for k, frame in v.items())
                elif isinstance(v, list):
                    rows.append('m.' + key + '=' + cpp_matrix(v) + ';')
                elif isinstance(v, bool):
                    rows.append('m.' + key + '=' + str(v).lower() + ';')
                elif isinstance(v, int):
                    rows.append(f'm.{key}={v}ll;')
                else:
                    rows.append('m.' + key + '=' + cpp_float(v) + ';')
            rows.append('}')
        rows.append('return p;}();')
    count = sum(len(m['selectors']) for m in document['models'])
    rows.append(f'inline const std::array<FixedContainerEquipmentProfile,{count}> FixedContainerEquipment=[]' + '{')
    rows.append(f'std::array<FixedContainerEquipmentProfile,{count}> out' + '{};')
    index = 0
    for i, model in enumerate(document['models']):
        for selector in model['selectors']:
            rows.append('{auto& p=out[' + str(index) + '];')
            for key in ('asset', 'configurationPath', 'configurationGuid', 'configurationSha256', 'mesh', 'meshSha256', 'skeletonSha256'):
                rows.append('p.' + key + '=' + json.dumps(selector[key]) + ';')
            rows.append(f'p.rigFingerprint=0x{selector["rigFingerprint"]:016x}ull;p.interaction=FixedContainer{i}Interaction;')
            for step in model['grasps']:
                n = ('Open', 'EjectSpent', 'Insert', 'Close').index(step)
                rows.append(f'p.handShapes[{n}]=&FixedContainer{i}{step}Hand;')
            rows.append(f'p.parts=FixedContainer{i}Parts;p.hiddenSupplyBones=FixedContainer{i}Hidden;')
            rows.append('p.supplyRoot=' + json.dumps(model['supply_root']) + ';p.supplyCenterInRoot=' + cpp_matrix(model['supply_center']) + ';}')
            index += 1
    rows += ['return out;}();',
             'inline const FixedContainerEquipmentProfile* FindFixedContainerEquipment(std::string_view config,std::string_view mesh,std::uint64_t rig) noexcept {',
             ' for(const auto& p:FixedContainerEquipment)if(p.configurationPath==config&&p.mesh==mesh&&p.rigFingerprint==rig)return &p;',
             ' return nullptr;', '}', '}']
    return '\n'.join(rows) + '\n'


def mirrored_left(grasp):
    reflect = [-1., 1., 1., 1.]
    def mirror(value):
        return rigid([value[r * 4 + c] * reflect[r] * reflect[c] for r in range(4) for c in range(4)])
    return {**grasp, 'authored_part_from_wrist': grasp['part_from_wrist'],
            'part_from_wrist': mirror(grasp['part_from_wrist']),
            'wrist_from_fingers': {n: mirror(v) for n, v in grasp['wrist_from_fingers'].items()},
            'mod_policy': 'Mirror exact authored right grip and finger frames through clip-local X and wrist-local X for a left-hand clip grasp; no claim that source clip uses left hand.'}


def model_profile(model, policy):
    root = policy['supply_root']; supply_parts = policy['supply_parts']; label = model['label']
    start, finish, grasp_index = (policy[k] for k in ('insert_source_sample', 'insert_destination_sample', 'insert_grasp_sample'))
    grasp = contact(model, grasp_index, root, policy['source_hand'])
    if policy['source_hand'] == 'RightHand':
        grasp = mirrored_left(grasp)
    if label == 'MP412':
        # The hand holds the speedloader's metal body, not the bullet tips.
        hardware = contact(model, grasp_index, 'jntWpnwpnJnt_14')
        grasp['contact_surface_bone'] = hardware['bone']
        grasp['contact_surface_distance_m'] = hardware['knuckle_bounds_distance_m']
    else:
        grasp['contact_surface_bone'] = root
        grasp['contact_surface_distance_m'] = grasp['knuckle_bounds_distance_m']
    if grasp['contact_surface_distance_m'] > .04:
        raise ValueError('Reviewed grasp does not contact the actual supply assembly')
    motions = {'Insert': placement(model, root, start, finish, grasp)}
    grasps = {'Insert': grasp}
    if label == 'MP412':
        hand = contact(model, policy['mechanical_grasp_sample'], 'jntWpn_4')
        first, opened, eject_peak = (policy[k] for k in ('closed_sample', 'open_sample', 'eject_peak_sample'))
        for step, a, b, op, returns in [('Open', first, opened, 'OpenCover', False),
                                      ('EjectSpent', opened, eject_peak, 'WithdrawFeed', True),
                                      ('Close', opened, first, 'CloseCover', False)]:
            value = hinge(model, 'jntWpn_4', a, b, hand)
            value.update(operation=op, returnToStart=returns, maxStepRadians=.25)
            motions[step] = value; grasps[step] = hand
    for m in motions.values():
        validate_motion(m)
    capacity = model['native'][0]['fields']['Ammo.MagazineCapacity']['value']
    if capacity != policy['capacity'] or any(n['fields']['Ammo.MagazineCapacity']['value'] != capacity or
            n['fields']['FireLogic.ReloadType']['value'] != 'rtMagazine' for n in model['native']):
        raise ValueError('Fixed-container capacity or native bulk-transfer contract changed')
    if policy['rounds_per_insertion'] != capacity:
        raise ValueError('rtMagazine fills remaining capacity; single-round insertion is not authorized')
    model_id = fnv((label + '\0' + model['mesh'] + '\0' + model['reload']['sha256']).encode())
    interaction = {'kind': policy['kind'], 'id': model_id, 'revision': 1,
                   'ammunitionKind': (fnv((model['mesh'] + '/fixed-ammo').encode()) & 0xffffffff) or 1,
                   'capacity': capacity, 'roundsPerInsertion': capacity, 'maxAgeNs': 100000000, 'geometry': {}}
    for step, m in motions.items():
        role = (fnv((str(model_id) + '/' + step).encode()) & 0xffffffff) or 1
        interaction['geometry'][step] = {'contact': {'role': role, 'radiusMeters': .065, 'maxAngleRadians': .6}, 'motion': m}
    part_rows = []
    source_from_weapon = mag.inverse_rigid(at(model, grasp_index, root))
    reference = model['reference']['frames']
    for bone, part in model['parts'].items():
        if part['mixed_triangles']:
            raise ValueError('Full-part presentation cannot ignore mixed skin')
        closed = rigid(reference[bone]); opened = closed
        start_rows = [closed[:] for _ in range(4)]; end_rows = [closed[:] for _ in range(4)]
        coupled = bone in policy.get('coupled_opening_parts', [])
        supply_offset = mag.multiply(at(model, grasp_index, bone), source_from_weapon) if bone in supply_parts else mag.identity()
        if label == 'MP412':
            closed_barrel = at(model, policy['closed_sample'], 'jntWpn_4')
            open_barrel = at(model, policy['open_sample'], 'jntWpn_4')
            peak_barrel = at(model, policy['eject_peak_sample'], 'jntWpn_4')
            if coupled:
                opened = mag.multiply(closed, mag.multiply(mag.inverse_rigid(closed_barrel), open_barrel))
                peak = mag.multiply(closed, mag.multiply(mag.inverse_rigid(closed_barrel), peak_barrel))
                start_rows = [closed, opened, opened, opened]
                end_rows = [opened, peak, opened, closed]
            if bone in supply_parts:
                start_rows[2] = mag.multiply(supply_offset, at(model, start, root))
                end_rows[2] = mag.multiply(supply_offset, at(model, finish, root))
                if bone == root:
                    start_rows[3] = end_rows[2]
                    end_rows[3] = mag.multiply(end_rows[2], mag.multiply(mag.inverse_rigid(open_barrel), closed_barrel))
        else:
            opened = at(model, start, bone)
            start_rows[2] = opened; end_rows[2] = at(model, finish, bone)
            if bone in supply_parts:
                start_rows[2] = mag.multiply(supply_offset, at(model, start, root))
                end_rows[2] = mag.multiply(supply_offset, at(model, finish, root))
        center = mag.identity(); center[12:15] = part['center_local_m']
        part_rows.append({'bone': bone, 'bone_index': part['bone_index'], 'parent': part['parent'],
                          'centerInBone': center, 'weaponFromClosed': closed, 'weaponFromOpen': opened,
                          'supplyFromPart': supply_offset, 'weaponFromStepStart': start_rows, 'weaponFromStepEnd': end_rows,
                          'supplyPart': bone in supply_parts, 'coupledOpening': coupled,
                          'visible_triangles': sum(s['triangles'] for s in part['sections'] if not s['name'].endswith('_ZOnly')),
                          'geometry_sections': part['sections'], 'bounds_local_m': [part['minimum'], part['maximum']]})
    # Center the complete ammunition + metal clip/loader assembly, never one
    # arbitrary subpart. Only bounds and derived transforms are retained.
    points = []
    for part in part_rows:
        if not part['supplyPart']:
            continue
        lo, hi = part['bounds_local_m']
        points += [mag.point([x, y, z], part['supplyFromPart']) for x in (lo[0], hi[0])
                   for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]
    lo, hi = mag.bounds(points); center = mag.identity(); center[12:15] = [(a + b) / 2 for a, b in zip(lo, hi)]
    selectors = []
    for binding in model['bindings']:
        w = binding['weapon']
        selectors.append(dict(asset=model['native'][0]['native_name'], configurationPath=w['resource'][:-4],
                              configurationGuid=w['instance_guid'], configurationSha256=w['sha256'], mesh=model['mesh'],
                              meshSha256=model['mesh_binding']['geometry']['mesh_sha256'], skeletonSha256=model['skeleton_sha256'],
                              rigFingerprint=int(model['rig_fingerprint'].split(':')[1], 16), binding_digest=binding['binding_digest']))
    return dict(label=label, selectors=selectors, interaction=interaction, grasps=grasps, parts=part_rows,
                supply_root=root, supply_parts=supply_parts, supply_center=center, supply_bounds_m=[lo, hi],
                body_cache_entries=[model['cache_metadata'][n] for n in supply_parts], policy=policy,
                visibility={'hide_source_supply_bones_while_carried': supply_parts,
                            'override_whole_mechanical_parts': policy.get('coupled_opening_parts', []),
                            'hardware_only_while_carried': ['jntWpnwpnJnt_14'] if label == 'MP412' else [],
                            'live_rounds': 'Retain all native loaded rounds. No ejection/reset/second ammunition ledger.',
                            'partial_supply': 'One rigid group represents a supply of up to capacity; actual transfer is min(capacity-loaded,reserve). It does not visually enumerate individual live rounds.'},
                source={'reload': model['reload'], 'mesh_binding': model['mesh_binding'], 'skeleton_sha256': model['skeleton_sha256'],
                        'reference': {k:v for k,v in model['reference'].items() if k != 'frames'},
                        'configuration_closure': model['configuration_closure'], 'preserved_decoder_gaps': model['gaps']})


def derive(policy_path, output, verify=False):
    policy = load(policy_path)
    models = []
    input_receipts = []
    for label, rule in policy['models'].items():
        path = ROOT / rule['sample_path']
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != rule['sample_sha256']:
            raise ValueError('Reviewed sampled source changed')
        model = json.loads(raw)
        if model['label'] != label:
            raise ValueError('Model identity changed')
        models.append(model_profile(model, rule))
        input_receipts.append({'path': rule['sample_path'], 'sha256': rule['sample_sha256']})
    document = {'schema': 'fvr.bc2.fixed_container_geometry.v1', 'coordinate_convention': 'canonical row-vector metres; proper rigid frames',
                'models': models, 'inputs': input_receipts, 'authoring_policy_sha256': hashlib.sha256(Path(policy_path).read_bytes()).hexdigest(),
                'evidence_level': 'exact source geometry plus explicitly authored mod interaction policy; native/game/headset unverified',
                'native_checked': False, 'headset_accepted': False}
    catalog = {'schema': 'fvr.bc2.body_ammo_assets', 'version': 1,
               'profiles': [r for m in models for r in m['body_cache_entries']]}
    outputs = {'geometry.json': json.dumps(document, indent=2, allow_nan=False) + '\n',
               'body-ammo-assets.json': json.dumps(catalog, indent=2, allow_nan=False) + '\n',
               'FixedContainer.generated.h': cpp_header(document)}
    output.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        path = output / name
        if verify:
            if path.read_text(encoding='utf8') != value:
                raise ValueError('Generated artifact differs: ' + str(path))
        else:
            with path.open('x', encoding='utf8') as stream:
                stream.write(value)
    print(json.dumps({'models': len(models), 'selectors': sum(len(m['selectors']) for m in models),
                      'supply_parts': len(catalog['profiles']), 'verified': verify}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    s = sub.add_parser('sample'); s.add_argument('--game', type=Path, required=True); s.add_argument('--output', type=Path, required=True)
    s.add_argument('--model', choices=list(SOURCES), action='append')
    d = sub.add_parser('derive'); d.add_argument('--policy', type=Path, required=True); d.add_argument('--output', type=Path, required=True)
    d.add_argument('--verify', action='store_true')
    args = parser.parse_args()
    if args.command == 'sample':
        sample(args.game, args.output, args.model)
    elif args.command == 'derive':
        derive(args.policy, args.output, args.verify)


if __name__ == '__main__':
    main()
