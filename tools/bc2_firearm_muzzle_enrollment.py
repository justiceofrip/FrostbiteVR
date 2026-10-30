"""Enroll exact firearm muzzle origins through the existing native shot contract.

Reads pinned cached bindings and bounded selected installed DBX/animation sources.
Outputs derived metadata only. A flash reference is an origin, not a barrel-axis
measurement; generated rows never authorize rotating the native firing matrix.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct

from bc2_authored_grip_bindings import digest, checked_hash, matrix, path_key
from bc2_firearm_body_aliases import expand_equipment_rows
from bc2_magazine_contact_batch import ArchivePool
from bc2_weapon_animation_pipeline import Clip, Skeleton
from bc2_weapon_config_pipeline import Resolver, guid, named, parse, scalar

SCHEMA = 'fvr.bc2.firearm_muzzle_enrollment.v1'
CONVENTION = 'canonical reflected-Z row-vector metres; named bone relative to jntWpn_1'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    raw = Path(path).read_bytes()
    require(len(raw) <= 64 * 1024 * 1024, 'Metadata input exceeds bound')
    return json.loads(raw), hashlib.sha256(raw).hexdigest()


def pinned(source, root):
    path = root / source['path']
    value, actual = read(path)
    require(actual == checked_hash(source['sha256']), 'Pinned metadata changed: ' + str(path))
    return value


def weapon_key(row):
    return path_key(row['resource']), checked_hash(row['sha256']), guid(row['instance_guid'])


def support_headers(path, root, seen=None):
    """Read the exact compiled aggregate, including its explicit leaf includes."""
    seen = set() if seen is None else seen
    path = path.resolve()
    require(path.is_relative_to(root.resolve()), 'Support include leaves source tree')
    if path in seen:
        return []
    seen.add(path)
    text = path.read_text(encoding='utf-8-sig')
    rows = []
    for asset, mesh, binding, clip, skeleton, rig in re.findall(
            r'AuthoredSupportProfile\{"([^"]+)","([^"]+)","([^"]+)","([^"]+)","([^"]+)",0x([0-9a-f]+)ull', text):
        rows.append(dict(asset=asset, mesh=mesh, binding_digest=checked_hash(binding),
                         clip_sha256=checked_hash(clip), skeleton_sha256=checked_hash(skeleton),
                         rig_fingerprint='fnv1a64:' + rig))
    for include in re.findall(r'^#include "([^"]+)"', text, re.M):
        candidate = path.parent / include
        if candidate.is_file():
            rows.extend(support_headers(candidate, root, seen))
    return rows


def vector(node):
    v = node.value
    require(not node.children and v.kind == 7 and v.width == 4 and v.count == 4 and
            isinstance(v.data, bytes) and len(v.data) == 16, 'Expected explicit padded float32 vector')
    values = list(struct.unpack('>4f', v.data))
    require(all(math.isfinite(x) for x in values) and values[3] == 0, 'Nonfinite vector or nonzero padding')
    return values[:3]


def shot_fields(function):
    shot = named(function, 'Shot')
    result = {k: vector(named(shot, k)) for k in ('InitialSpeed', 'InitialDirection', 'InitialPosition')}
    for key, kind in (('InheritWeaponSpeedAmount', 'float32'), ('NumberOfBulletsPerShell', 'int32'),
                      ('NumberOfBulletsPerShot', 'int32'), ('ActiveForceSpawnToCamera', 'bool'),
                      ('ForceSpawnToCamera', 'bool'), ('RelativeTargetAiming', 'bool')):
        result[key] = scalar(named(shot, key), kind)
    for key in ('MuzzleExplosion', 'ProjectileData'):
        node = named(shot, key)
        require(not node.children and node.value.kind == 2 and isinstance(node.value.data, str), 'Typed reference required')
        result[key] = node.attributes.get('ref', node.value.data)
    return result


def ordinary(fields):
    """The already verified normal-shot ABI; pellet counts remain exact data."""
    speed = fields['InitialSpeed']
    return (fields['InitialDirection'] == [0, 0, 1] and fields['InitialPosition'] == [0, 0, 0] and
            speed[:2] == [0, 0] and math.isfinite(speed[2]) and speed[2] > 0 and
            fields['InheritWeaponSpeedAmount'] == 0 and
            0 < fields['NumberOfBulletsPerShell'] <= 256 and 0 < fields['NumberOfBulletsPerShot'] <= 256 and
            not fields['ForceSpawnToCamera'] and not fields['RelativeTargetAiming'] and
            fields['MuzzleExplosion'] in ('null', '') and fields['ProjectileData'] not in ('null', ''))


def origin_reference(binding, evaluated=None):
    require(binding['binding_digest'] == digest({k: v for k, v in binding.items() if k != 'binding_digest'}),
            'Source binding digest changed')
    require(binding['coordinate_convention'] == CONVENTION and binding['weapon_bone'] == 'jntWpn_1' and
            binding['state_count'] == 1 and binding['state_index'] == 0, 'Exact sole-state weapon frame required')
    topology = binding['rig_topology']
    names = [b['name'] for b in topology]
    require(names.count('jntWpn_Flash') == 1 and names.count('jntWpn_1') == 1, 'Exact unique flash and weapon bones required')
    index, root = names.index('jntWpn_Flash'), names.index('jntWpn_1')
    visited = set()
    while index != root:
        require(0 <= index < len(topology) and index not in visited, 'Flash is not in the weapon subtree')
        visited.add(index)
        index = topology[index]['parent_index']
    static = binding['static_parts_and_fingers'].get('jntWpn_Flash')
    if static is None:
        require(evaluated is not None, 'Exact decoded flash reference missing')
        require(evaluated['animation_sha256'] == binding['animation_sha256'] and
                evaluated['skeleton_sha256'] == binding['skeleton_sha256'] and
                evaluated['rig_fingerprint'] == binding['rig_fingerprint'] and evaluated['reference_time_seconds'] == 0,
                'Decoded flash source identity differs')
        transform = matrix(evaluated['flash_in_weapon'])
        status = 'exact_authored_initial_reference_live_relation_required'
    else:
        transform = matrix(static)
        status = 'static_authored_reference_live_relation_required'
    result = dict(source_binding_digest=binding['binding_digest'], animation_sha256=binding['animation_sha256'],
                  skeleton_sha256=binding['skeleton_sha256'], rig_fingerprint=binding['rig_fingerprint'],
                  flash_bone='jntWpn_Flash', weapon_bone='jntWpn_1', coordinate_convention=CONVENTION,
                  flash_in_weapon=transform, status=status, direction_claim='none', native_orientation='preserve')
    result['origin_digest'] = digest(result)
    return result


def assemble(roster, bindings, supports, bodies, firing, evaluated):
    profiles, gaps = [], []
    seen = set()
    body_map = {(b['asset'], b['configuration_path']): b for b in expand_equipment_rows(bodies['profiles'])
                if b.get('configuration_path')}
    for row in roster['rows']:
        key = row['asset'], row['configuration']
        require(key not in seen, 'Duplicate roster identity')
        seen.add(key)
        try:
            body = body_map[key]
            identity = (path_key(row['configuration'] + '.dbx'), row['identity'][1], guid(row['identity'][2]))
            matches = [b for b in bindings.values() if b['native_asset_name'] == row['asset'] and
                       weapon_key(b['weapon']) == identity and b['configured_mesh_path'] == body['mesh']]
            require(matches, 'Missing exact source binding')
            # Several pinned archives can contain identical authored content.
            # Collapse only the full configuration/coordinate signature, never
            # the asset name or a convenient source directory.
            signature = lambda b: [b[k] for k in ('weapon','configured_mesh_path','animation_sha256',
                'skeleton_sha256','rig_fingerprint','rig_topology','static_parts_and_fingers')]
            require(all(signature(b) == signature(matches[0]) for b in matches), 'Ambiguous exact source binding')
            for b in matches:origin_reference(b, evaluated.get(b['animation_sha256']))
            b = min(matches, key=lambda b:b['binding_digest'])
            choices = [s for s in supports if s['asset'] == row['asset'] and s['mesh'] == b['configured_mesh_path'] and
                       s['rig_fingerprint'] == b['rig_fingerprint'] and s['clip_sha256'] == b['animation_sha256'] and
                       s['skeleton_sha256'] == b['skeleton_sha256']]
            require(len(choices) == 1, 'Missing or ambiguous exact compiled support source')
            s = choices[0]
            origin = origin_reference(b, evaluated.get(b['animation_sha256']))
            shot = firing[row['configuration']]
            require(weapon_key(shot['weapon']) == identity and shot['asset'] == row['asset'], 'Firing configuration identity differs')
            require(ordinary(shot['fields']), 'Shot fields are outside the verified normal-shot contract')
            out = dict(asset=row['asset'], configuration_path=row['configuration'], mesh_path=b['configured_mesh_path'],
                       configuration_sha256=identity[1], configuration_guid=identity[2],
                       support_binding_digest=s['binding_digest'], source_binding_digest=b['binding_digest'],
                       equivalent_source_binding_digests=sorted(b['binding_digest'] for b in matches),
                       clip_sha256=b['animation_sha256'], skeleton_sha256=b['skeleton_sha256'],
                       rig_fingerprint=b['rig_fingerprint'], origin=origin, firing=shot,
                       admission='Candidate', native_verified=False, headset_accepted=False)
            out['profile_digest'] = digest(out)
            profiles.append(out)
        except (ValueError, KeyError) as exc:
            gaps.append(dict(asset=row['asset'], configuration_path=row['configuration'], reason=str(exc)))
    return dict(schema=SCHEMA, profiles=profiles, gaps=gaps, summary=dict(configurations=len(roster['rows']),
                enrolled_configurations=len(profiles), asset_names=len({p['asset'] for p in profiles})),
                native_verified=False, headset_accepted=False,
                scope='Exact authored muzzle origins with live relation and normal-shot checks; native direction, spread and recoil preserved.')


def header(document):
    def number(x):
        value = format(x, '.10g')
        return value + ('f' if '.' in value or 'e' in value else '.f')
    def vec(values):
        return '{' + ','.join(number(v) for v in values) + '}'
    lines = ['// Derived source metadata; preserve native firing orientation.', '#pragma once',
             '#include "Bc2MuzzleAdmission.h"', 'namespace fvr::bc2::generated_muzzle {',
             'inline constexpr std::array<MuzzleEnrollmentProfile,%d> MuzzleEnrollments{{' % len(document['profiles'])]
    for p in document['profiles']:
        f = p['firing']['fields'];o = p['origin'];m = o['flash_in_weapon']
        strings = [p['asset'], p['configuration_path'], p['mesh_path'], p['support_binding_digest'], o['origin_digest'],
                   p['configuration_sha256'], p['firing']['function']['sha256'], 'profiles/weapons54-muzzle/enrollment.json']
        transform = 'math::Matrix4{{{' + ','.join('{{' + ','.join(number(x) for x in m[i:i+4]) + '}}' for i in range(0,16,4)) + '}}}'
        lines.append('MuzzleEnrollmentProfile{' + ','.join(json.dumps(x) for x in strings) + ',0x' +
                     p['rig_fingerprint'].split(':')[1] + 'ull,' + transform + ',MuzzleAdmission::Candidate,' +
                     ','.join(vec(f[k]) for k in ('InitialSpeed','InitialDirection','InitialPosition')) + ',' +
                     number(f['InheritWeaponSpeedAmount']) + ',' + str(f['NumberOfBulletsPerShell']) + ',' +
                     str(f['NumberOfBulletsPerShot']) + ',' + str(f['ActiveForceSpawnToCamera']).lower() + ',' +
                     json.dumps(p['clip_sha256']) + ',' + json.dumps(p['skeleton_sha256']) + '},')
    return '\n'.join(lines + ['}};', '}']) + '\n'


class InstalledSources:
    def __init__(self, game):
        self.game = game
        self.pool = ArchivePool(game, 'Dist/win32/levels/mp_common/level-00.fbrb',
                               '6ec10fba796c14d2a05f86d3079e819c38a4aaab3ec9205d7345bd2869554965')

    def read(self, source, names):
        path_key(source['archive'])
        archive = self.pool(self.game / source['archive'])
        require(archive.index_sha256 == source['archive_index_sha256'], 'Archive index changed')
        by_name = {e.name.casefold(): e.name for e in archive.entries if e.flags == 65536}
        require(all(path_key(n) in by_name for n in names), 'Selected exact resource absent')
        return {name.casefold(): data for name, data in archive.read_selected([by_name[path_key(n)] for n in sorted(set(names))]).items()}

    def decode(self, binding):
        values = []
        for prefix, cls in (('animation', Clip), ('skeleton', Skeleton)):
            name = binding['animation_chain'][prefix + '_resource']
            raw = self.read(binding[prefix + '_source'], [name])[path_key(name)]
            require(hashlib.sha256(raw).hexdigest() == binding[prefix + '_sha256'], 'Decoded resource content changed')
            values.append(cls(raw))
        clip, skeleton = values
        require(skeleton.fingerprint == binding['rig_fingerprint'] and skeleton.metadata()['bones'] == binding['rig_topology'],
                'Decoded rig topology differs')
        pose = clip.evaluate(skeleton, 0., ['jntWpn_Flash'])
        return dict(animation_sha256=clip.sha256, skeleton_sha256=skeleton.sha256,
                    rig_fingerprint=skeleton.fingerprint, reference_time_seconds=0.,
                    flash_in_weapon=pose['weapon_relative']['jntWpn_Flash'])


def extract_firing(installed, selected, configurations):
    grouped = {}
    for b, source in selected:
        key = source['archive'], source['archive_index_sha256']
        group = grouped.setdefault(key, dict(source=source, bindings=[], names=set()))
        group['bindings'].append(b);group['names'].add(b['weapon']['resource'])
        for c in configurations:
            if c['resource_sha256'] == b['weapon']['sha256'] and path_key(c['resource']) == path_key(b['weapon']['resource']):
                group['names'].update([c['firing_resource'], c['function_resource']])
    result = {}
    for group in grouped.values():
        docs = {k: parse(k, v) for k,v in installed.read(group['source'], group['names']).items()}
        # Known catalog references are batched above. Opening-only references
        # resolve through their exact DBX chain with at most two extra reads.
        for field, expected in (('WeaponFiring','GameSharedResources.SoldierWeaponData'),
                                ('PrimaryFire','GameSharedResources.WeaponFiringData')):
            wanted = set()
            for doc in docs.values():
                for instance in doc.instances.values():
                    if instance.attributes['type'] == expected:
                        ref = named(instance,field).attributes.get('ref','')
                        if '/' in ref:
                            resource = ref.rsplit('/',1)[0] + '.dbx'
                            if path_key(resource) not in docs:wanted.add(resource)
            if wanted:
                docs.update({k:parse(k,v) for k,v in installed.read(group['source'],wanted).items()})
        resolver = Resolver(docs.values())
        for b in group['bindings']:
            doc = docs[path_key(b['weapon']['resource'])]
            require(doc.sha256 == b['weapon']['sha256'], 'Installed weapon configuration changed')
            weapon = doc.instances[guid(b['weapon']['instance_guid'])]
            require(weapon.attributes['type'] == 'GameSharedResources.SoldierWeaponData' and
                    scalar(named(weapon,'Name'),'string') == b['native_asset_name'], 'Exact installed weapon identity differs')
            fd, firing = resolver.resolve(doc,named(weapon,'WeaponFiring').attributes.get('ref'),'GameSharedResources.WeaponFiringData')
            sd, function = resolver.resolve(fd,named(firing,'PrimaryFire').attributes.get('ref'),'GameSharedResources.FiringFunctionData')
            receipt = lambda d,i:dict(resource=d.resource,sha256=d.sha256,instance_guid=guid(i.attributes['guid']))
            record = dict(asset=b['native_asset_name'],weapon=b['weapon'],
                source=group['source'],firing=receipt(fd,firing),function=receipt(sd,function),fields=shot_fields(function))
            key = b['weapon']['resource'][:-4]
            if key in result:
                require(all(result[key][k] == record[k] for k in ('asset','weapon','firing','function','fields')),
                        'Conflicting exact configuration firing closure across archives')
            result[key] = record
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--source-metadata', type=Path)
    parser.add_argument('--source-metadata-sha256')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    root = Path(__file__).resolve().parents[1]
    manifest, manifest_hash = read(args.manifest)
    documents = {k: pinned(p,root) for k,p in manifest['sources'].items()}
    roster, bodies = documents['roster'], documents['bodies']
    configurations = documents['catalog']['configurations']
    bindings, sources = {}, {}
    for name, document in documents.items():
        if not name.startswith('bindings_'):continue
        for b in document['profiles']:
            bindings[b['binding_digest']] = b
            source = b.get('configuration_source') or (dict(archive=document['archive'],archive_index_sha256=document['archive_index_sha256']) if 'archive' in document else None)
            if source:sources[b['binding_digest']] = source
    support_files = set()
    supports = support_headers(root / manifest['support_header'],root,support_files)
    roster_keys = {(r['asset'],path_key(r['configuration'] + '.dbx'),r['identity'][1]) for r in roster['rows']}
    selected = [b for b in bindings.values() if (b['native_asset_name'],path_key(b['weapon']['resource']),b['weapon']['sha256']) in roster_keys]
    # Identical cached binding receipts can occur in several source documents.
    require(len(selected) <= 256, 'Exact roster binding count exceeds bound')
    if args.game:
        installed = InstalledSources(args.game)
        firing = extract_firing(installed,[(b,sources[b['binding_digest']]) for b in selected],configurations)
        evaluated = {}
        for b in selected:
            if 'jntWpn_Flash' not in b['static_parts_and_fingers'] and b['animation_sha256'] not in evaluated:
                evaluated[b['animation_sha256']] = installed.decode(b)
        extraction = dict(schema='fvr.bc2.muzzle_source_metadata.v1',manifest_sha256=manifest_hash,
                          firing=firing,evaluated=evaluated,selected_resource_bytes=installed.pool.bytes,
                          raw_assets_exported=False,native_execution=False)
    else:
        require(args.source_metadata is not None and args.source_metadata_sha256 is not None,
                'Offline replay requires an explicit source metadata path/hash')
        extraction = pinned(dict(path=str(args.source_metadata),sha256=args.source_metadata_sha256),root)
        require(extraction['manifest_sha256'] == manifest_hash, 'Extraction manifest changed')
        firing,evaluated = extraction['firing'],extraction['evaluated']
    result = assemble(roster,bindings,supports,bodies,firing,evaluated)
    result['manifest_sha256'] = manifest_hash
    result['compiled_support_sources'] = {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in sorted(support_files)}
    args.output.mkdir(parents=True,exist_ok=True)
    for name, value in [('source-metadata.json',extraction),('enrollment.json',result),('Muzzle.generated.h',header(result))]:
        text = value if isinstance(value,str) else json.dumps(value,indent=2,allow_nan=False)+'\n'
        (args.output/name).write_text(text,encoding='utf-8')
    print(json.dumps(result['summary'] | dict(gaps=len(result['gaps']))))
    return 0 if not result['gaps'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
