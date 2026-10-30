"""Exact underbarrel configuration and measured sight enrollment, offline only.

Pairs require authored AltDeployId, complete first-person mesh references and
the two absolute animation roles. Runtime still proves reciprocal 33/36 routes,
current inventory ownership and linked persistence. No gun-name geometry aliases.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

SCHEMA = 'fvr.bc2.underbarrel_enrollment.v1'


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode()).hexdigest()


def read(path):
    raw = Path(path).read_bytes()
    require(len(raw) <= 64 * 1024 * 1024, 'Input bound')
    return json.loads(raw), hashlib.sha256(raw).hexdigest()


def collect(game, catalog_paths, roster_path):
    """Only requested catalog configurations and their exact DBX references."""
    from bc2_magazine_contact_batch import ArchivePool
    from bc2_weapon_config_pipeline import parse, named, scalar, guid, Resolver
    from bc2_weapon_mesh_bindings import reference_resource
    from bc2_authored_grip_bindings import fields, receipt
    roster, roster_hash = read(roster_path)
    enrolled = {(r['configuration'] + '.dbx', r['identity'][1], r['identity'][2].lower()) for r in roster['rows']}
    configurations = {}
    pins = []
    for catalog_path in catalog_paths:
        catalog, source_hash = read(catalog_path)
        catalog = catalog.get('configuration', catalog)
        require(not catalog['parse_errors'], 'Catalog parse errors')
        pool = ArchivePool(game, catalog['archive'], catalog['index_sha256'])
        archive = pool(game / catalog['archive'])
        entries = {e.name.casefold(): e for e in archive.entries if e.flags == 65536 and e.name.endswith('.dbx')}
        documents = {}
        def load(names):
            wanted = {n.casefold() for n in names} - documents.keys()
            require(len(documents) + len(wanted) <= 512, 'Document closure bound')
            require(wanted <= entries.keys(), 'Missing exact DBX reference')
            for name, raw in archive.read_selected([entries[n].name for n in sorted(wanted)]).items():
                documents[name.casefold()] = parse(name, raw)
        selected = [r for r in catalog['resolved_weapons'] if r['weapon_class'] == 'wcUgl' or
                    (r['weapon_class'] == 'wcAssault' and
                     (r['resource'], r['resource_sha256'], r['instance_guid'].lower()) in enrolled)]
        load([r['resource'] for r in selected])
        refs = []
        for row in selected:
            doc = documents[row['resource'].casefold()]
            require(doc.sha256 == row['resource_sha256'], 'Catalog configuration content changed')
            node = doc.instances[guid(row['instance_guid'])]
            refs.append(named(node, 'Persistence').attributes['ref'])
            refs += [named(s, 'AnimTree1p').attributes['ref'] for s in named(node, 'WeaponStates').children]
        load([reference_resource(r) for r in refs if r != 'null'])
        resolver = Resolver(documents.values())
        role_refs = []
        for row in selected:
            doc = documents[row['resource'].casefold()]; node = doc.instances[guid(row['instance_guid'])]
            for state in named(node, 'WeaponStates').children:
                tree_doc, tree = resolver.resolve(doc, named(state, 'AnimTree1p').attributes['ref'], 'Animation.SpecificAnimTreeData')
                role_refs += [r for r in fields(tree, 'ReplacmentAnimations')
                              if reference_resource(r).rsplit('/', 1)[-1].casefold() in ('handsikpose.dbx', '1p_altdeploy.dbx')]
        load([reference_resource(r) for r in role_refs])
        resolver = Resolver(documents.values())
        for row in selected:
            doc = documents[row['resource'].casefold()]; node = doc.instances[guid(row['instance_guid'])]
            persistence = named(node, 'Persistence').attributes['ref']
            persistent_id = ''
            if persistence != 'null':
                pd, pn = resolver.resolve(doc, persistence, 'GameSharedResources.PersistentWeapon')
                persistent_id = scalar(named(pn, 'Id'), 'string')
            states = []
            for index, state in enumerate(named(node, 'WeaponStates').children):
                td, tree = resolver.resolve(doc, named(state, 'AnimTree1p').attributes['ref'], 'Animation.SpecificAnimTreeData')
                roles = {}
                for ref in fields(tree, 'ReplacmentAnimations'):
                    path = reference_resource(ref)
                    if path.rsplit('/', 1)[-1].casefold() not in ('handsikpose.dbx', '1p_altdeploy.dbx'):
                        continue
                    ad = documents[path.casefold()]
                    an = ad.instances[guid(ref.rsplit('/', 1)[-1])]
                    name = scalar(named(an, 'Name'), 'string')
                    role = name.rsplit('/', 1)[-1].casefold()
                    if role not in ('handsikpose', '1p_altdeploy'):
                        continue
                    require(role not in roles, 'Ambiguous sight animation role')
                    if an.attributes['type'] != 'Animation.AnimationAsset':
                        continue
                    roles[role] = dict(resource=name + '.res', asset=receipt(ad, an),
                                       skeleton_reference=named(an, 'Skeleton').attributes['ref'])
                states.append(dict(index=index, meshes=sorted(fields(state, 'Meshes1p')), roles=roles,
                                   tree=receipt(td, tree)))
            result = dict(asset=row['native_name'], configuration=scalar(named(node, 'WeaponAssetName'), 'string'),
                          weapon_class=row['weapon_class'], source=receipt(doc, node),
                          alt_deploy_id=scalar(named(node, 'AltDeployId'), 'int32'),
                          persistence_reference=persistence, persistent_id=persistent_id, states=states)
            key = result['configuration']
            require(key not in configurations or configurations[key] == result, 'Conflicting exact configuration')
            configurations[key] = result
        pins.append(dict(path=str(catalog_path), sha256=source_hash, archive=catalog['archive'],
                         index_sha256=archive.index_sha256, selected_documents=len(documents)))
    return dict(schema=SCHEMA, roster_sha256=roster_hash, catalog_sources=pins,
                configurations=sorted(configurations.values(), key=lambda r: r['configuration']))


def pair_configurations(source):
    require(source.get('schema') == SCHEMA, 'Enrollment schema')
    rows = source['configurations']
    require(len(rows) <= 512 and len({r['configuration'] for r in rows}) == len(rows), 'Duplicate/bounded configurations')
    pairs = []
    for primary in rows:
        if primary['weapon_class'] == 'wcUgl' or not primary['persistent_id']:
            continue
        for secondary in rows:
            if secondary['weapon_class'] != 'wcUgl' or primary['alt_deploy_id'] <= 0 or primary['alt_deploy_id'] != secondary['alt_deploy_id']:
                continue
            if len(primary['states']) != 1 or len(secondary['states']) != 1:
                continue
            a, b = primary['states'][0], secondary['states'][0]
            if not a['meshes'] or a['meshes'] != b['meshes']:
                continue
            if any(set(s['roles']) != {'handsikpose', '1p_altdeploy'} for s in (a, b)):
                continue
            skeletons = {r['skeleton_reference'] for s in (a, b) for r in s['roles'].values()}
            if len(skeletons) != 1:
                continue
            pairs.append(dict(primary=primary['configuration'], secondary=secondary['configuration'],
                              primary_asset=primary['asset'], secondary_asset=secondary['asset'],
                              persistent_id=primary['persistent_id'], secondary_action=33, primary_action=36,
                              native_accepted=False, linked_variant_eligible=True,
                              geometry_key=digest(dict(meshes=a['meshes'], primary_roles=a['roles'], secondary_roles=b['roles'])),
                              authored_persistence_shared=primary['persistence_reference'] == secondary['persistence_reference']))
    return pairs


def family_header(source):
    pairs = pair_configurations(source)
    # Keep the legacy order for the existing measured profiles and fixtures.
    legacy = [('XM8_sp_s', '40mmgl'), ('AEK971_sp', '40mmgl'), ('XM8_sp', '40mmgl')]
    selected = []
    for assets in legacy:
        matches = [p for p in pairs if (p['primary_asset'], p['secondary_asset']) == assets]
        # Scoped/unscoped SP pairs have the same authored mesh as MP variants;
        # their exact SP launcher is the prior retained native/geometry source.
        matches = [p for p in matches if '/SP_' in p['secondary']]
        require(len(matches) == 1, 'Legacy exact pair missing or ambiguous')
        selected.append(matches[0])
    selected += [p for p in pairs if p not in selected]
    require(len(selected) <= 512, 'Family bound')
    lines = ['// Generated by bc2_underbarrel_enrollment.py; no native/headset claim.',
             'inline constexpr std::array<WeaponModeFamilyProfile,' + str(len(selected)) + '> WeaponModeFamilies{{']
    for p in selected:
        accepted = p is selected[0]
        fields = [p['primary_asset'], p['primary'], p['secondary_asset'], p['secondary'], p['persistent_id']]
        lines.append(' {' + ','.join(json.dumps(v) for v in fields) + ',33,36,' + str(accepted).lower() + ',true},')
    lines.append('}};')
    return '\n'.join(lines) + '\n', selected


def coordinate_key(primary, secondary):
    """Exact weapon mesh and four absolute animation asset identities, no aliases."""
    a, b = primary['states'][0], secondary['states'][0]
    meshes = [r for r in a['meshes'] if r.casefold().startswith('objects/weapons/handheld/')]
    require(len(meshes) == 1, 'Unique exact handheld mesh required')
    roles = [s['roles'][role] for s in (a, b) for role in ('handsikpose', '1p_altdeploy')]
    return digest(dict(mesh=meshes[0], roles=roles))


def measure_pair(game, source, pair):
    """Measure the unique rigid hinge in both native AltDeploy transitions.

    Multiple moving rigid parts remain an explicit unsupported assembly. The
    existing XM320 opposed-leaf adapter keeps its independent retained evidence.
    """
    import math
    from bc2_magazine_contact_batch import ArchivePool
    from bc2_authored_magazine_geometry import exact_resource, rigid_parts, multiply, inverse_rigid
    from bc2_mesh_geometry import metadata, geometry
    from bc2_weapon_animation_pipeline import Clip, Skeleton
    from bc2_authored_sight_geometry import measure, axis_angle
    from inspect_bc2_mesh_asset import bone_hash, sha
    rows = {r['configuration']: r for r in source['configurations']}
    a, b = rows[pair['primary']], rows[pair['secondary']]
    common_pin = source['catalog_sources'][-1]
    pool = ArchivePool(game, common_pin['archive'], common_pin['index_sha256'])
    common = pool(game / common_pin['archive'])
    roles = [r['states'][0]['roles'] for r in (a, b)]
    skeleton_ref = roles[0]['handsikpose']['skeleton_reference']
    skeleton = Skeleton(exact_resource(common, skeleton_ref.rsplit('/', 1)[0] + '.res', 'GrannyModel'))
    archives = [pool(game / ('Dist/win32/async/weapon/' + r['configuration'].rsplit('/', 1)[-1].casefold() + '-00.fbrb')) for r in (a, b)]
    clips = [[Clip(exact_resource(archive, role[k]['resource'], 'GrannyAnimation')) for k in ('handsikpose', '1p_altdeploy')]
             for archive, role in zip(archives, roles)]
    mesh_ref = next(r for r in a['states'][0]['meshes'] if r.casefold().startswith('objects/weapons/handheld/'))
    mesh_name = mesh_ref.rsplit('/', 1)[0]
    raw = exact_resource(archives[0], mesh_name + '.res', 'SkinnedMeshSet')
    lod = metadata(raw)[0]
    data = exact_resource(archives[0], mesh_name + '_lod0_data.res', 'MeshData')
    parts = rigid_parts(data, lod, skeleton)
    sections = geometry(data, lod, {bone_hash(n): n for n in skeleton.names})
    weapon = 'jntWpn_1'
    names = sorted(set(parts) | {skeleton.names[skeleton.parents[skeleton.names.index(b)]]
                                for b in parts if skeleton.parents[skeleton.names.index(b)] >= 0})
    frames = [c[0].evaluate(skeleton, 0, names, weapon=weapon)['weapon_relative'] for c in clips]
    candidates, rejected, moving = [], [], []
    for bone, part in parts.items():
        index = skeleton.names.index(bone); parent_index = skeleton.parents[index]
        if bone == weapon or parent_index < 0 or part['triangles'] < 12:
            continue
        parent = skeleton.names[parent_index]
        local = [multiply(f[bone], inverse_rigid(f[parent])) for f in frames]
        axis, angle = axis_angle(multiply(local[1], inverse_rigid(local[0])))
        if angle > .05:
            moving.append(dict(bone=bone, parent=parent, angle=angle, axis=axis, triangles=part['triangles']))
        if axis is None or not .2 <= angle <= math.pi:
            continue
        try:
            result = measure(skeleton, parts, sections, clips[0][0], clips[1][0], clips[1][1], clips[0][1], bone, weapon,max_travel_radians=math.pi,max_static_seconds=15)
            candidates.append(result)
        except ValueError as exc:
            rejected.append(dict(bone=bone, reason=str(exc)))
    result = dict(coordinate_key=coordinate_key(a, b), primary=a['configuration'], secondary=b['configuration'],
                  mesh_path=next(e.name[:-4] for e in archives[0].entries if e.name.casefold() == mesh_name + '.res'),
                  mesh_reference=mesh_ref, mesh_sha256=sha(raw), lod_sha256=sha(data), skeleton_sha256=skeleton.sha256,
                  roles=roles, clip_hashes=[[c.sha256 for c in mode] for mode in clips],
                  archives=[dict(path=str(ar.path.relative_to(game).as_posix()), index_sha256=ar.index_sha256) for ar in archives],
                  candidates=candidates, rejected=rejected, moving_parts=moving, native_accepted=False, headset_accepted=False)
    result['digest'] = digest(result)
    return result


def verify_variants(game, source, measurements):
    from bc2_magazine_contact_batch import ArchivePool
    from bc2_authored_magazine_geometry import exact_resource
    from inspect_bc2_mesh_asset import sha
    _, pairs = family_header(source)
    rows = {r['configuration']: r for r in source['configurations']}
    targets = {p[k] for p in pairs for k in ('primary', 'secondary')}
    pin = source['catalog_sources'][-1]
    pool = ArchivePool(game, pin['archive'], pin['index_sha256'])
    receipts = []
    for name in sorted(targets):
        row = rows[name];state = row['states'][0]
        mesh_ref = next(r for r in state['meshes'] if r.casefold().startswith('objects/weapons/handheld/'))
        mesh = mesh_ref.rsplit('/', 1)[0]
        path = 'Dist/win32/async/weapon/' + name.rsplit('/', 1)[-1].casefold() + '-00.fbrb'
        archive = pool(game / path)
        resources = [(mesh + '.res', 'SkinnedMeshSet'), (mesh + '_lod0_data.res', 'MeshData')]
        resources += [(state['roles'][k]['resource'], 'GrannyAnimation') for k in ('handsikpose', '1p_altdeploy')]
        # Batch the exact selection into the ephemeral cache, one archive pass.
        wanted = {n.casefold() for n, _ in resources}
        entries = [e.name for e in archive.entries if e.flags == 65536 and e.name.casefold() in wanted]
        require(len(entries) == len(resources), 'Missing variant coordinate resource')
        archive.read_selected(entries)
        hashes = [sha(exact_resource(archive, n, kind)) for n, kind in resources]
        receipts.append(dict(configuration=name, archive=path, index_sha256=archive.index_sha256,
                             mesh_sha256=hashes[0], lod_sha256=hashes[1], clip_hashes=hashes[2:]))
    result = dict(schema='fvr.bc2.underbarrel_variant_coordinates.v1', rows=receipts)
    result['digest'] = digest(result)
    return result


def sight_header(source, measurements, variants):
    """Rebind only byte-identical configured coordinate producers."""
    _, pairs = family_header(source)
    rows = {r['configuration']: r for r in source['configurations']}
    require(variants.get('digest') == digest({k: v for k, v in variants.items() if k != 'digest'}), 'Variant coordinate receipt changed')
    variants = {r['configuration']: r for r in variants['rows']}
    measured = {}
    for result in measurements:
        require(result['digest'] == digest({k: v for k, v in result.items() if k != 'digest'}), 'Sight measurement changed')
        require(result['coordinate_key'] not in measured, 'Duplicate sight measurement')
        measured[result['coordinate_key']] = result
    xm8_key = coordinate_key(rows[pairs[0]['primary']], rows[pairs[0]['secondary']])
    lines = ['// Generated exact coordinate-source joins; native/headset acceptance is separate.']
    profiles = []; coverage = []
    for index, pair in enumerate(pairs):
        key = coordinate_key(rows[pair['primary']], rows[pair['secondary']])
        result = measured.get(key)
        verified = bool(result)
        if result:
            for mode, name in enumerate((pair['primary'], pair['secondary'])):
                receipt = variants.get(name)
                verified = verified and bool(receipt) and receipt['mesh_sha256'] == result['mesh_sha256'] and receipt['lod_sha256'] == result['lod_sha256'] and receipt['clip_hashes'] == result['clip_hashes'][mode]
        if key == xm8_key and verified:
            profiles.append('{&WeaponModeFamilies[' + str(index) + '],SightAssemblyKind::OpposedLeaves,"jntWpn_9","jntWpn_11","jntWpn_1",nullptr,1.570796327f,' + str(index == 0).lower() + ',&Xm320OpposedSightGeometry,true}')
            status = 'retained_exact_xm320_coordinate_sources'
        elif verified and len(result['candidates']) == 1 and not result['rejected']:
            g = result['candidates'][0]; symbol = 'UnderbarrelSight' + str(index)
            require(g['rig_fingerprint'].startswith('fnv1a64:'), 'Rig fingerprint')
            chain = g['bone_chain']
            lines.append('inline constexpr std::array<std::string_view,' + str(len(chain)) + '> ' + symbol + 'Chain{' + ','.join(map(json.dumps, chain)) + '};')
            def literal(v):
                t = format(v, '.10g')
                return t + ('' if '.' in t or 'e' in t else '.0') + 'f'
            def vector(v):
                return '{' + ','.join(map(literal, v)) + '}'
            travel = literal(g['mechanism']['settled_travel_rad'])
            fields = [result['mesh_path'], pair['primary_asset'], pair['primary'], pair['secondary_asset'], pair['secondary']]
            lines.append('inline const AuthoredSightGeometry ' + symbol + '{' + ','.join(map(json.dumps, fields)) + ',' + symbol + 'Chain,0x' + g['rig_fingerprint'].split(':')[1] + 'ull,' + ','.join(vector(v) for v in (*g['bounds_part_m'], g['mechanism']['axis_part'])) + ',' + travel + '};')
            profiles.append('{&WeaponModeFamilies[' + str(index) + '],SightAssemblyKind::Subtree,' + json.dumps(chain[0]) + ',"",' + json.dumps(chain[-1]) + ',&' + symbol + ',' + travel + ',false,nullptr,true}')
            status = 'measured_unique_rigid_mode_hinge'
        else:
            status = 'measurement_or_assembly_review_required'
        coverage.append(dict(family_index=index, primary=pair['primary'], secondary=pair['secondary'], status=status))
    lines.append('inline const std::array<SightAdapterProfile,' + str(len(profiles)) + '> SightAdapterProfiles{{\n ' + ',\n '.join(profiles) + '\n}};')
    return '\n'.join(lines) + '\n', coverage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path)
    parser.add_argument('--catalog', type=Path, action='append')
    parser.add_argument('--roster', type=Path)
    parser.add_argument('--source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--sights', action='store_true')
    parser.add_argument('--verify-variants', action='store_true')
    args = parser.parse_args()
    source = read(args.source)[0] if args.source else collect(args.game, args.catalog, args.roster)
    header, pairs = family_header(source)
    args.output.mkdir(parents=True, exist_ok=True)
    source['pairs'] = pairs
    source['scope'] = 'Offline exact configuration enrollment; runtime still requires linked persistence, reciprocal 33/36, owner and current snapshot checks. Sight geometry is separately enrolled.'
    (args.output / 'enrollment.json').write_text(json.dumps(source, indent=2) + '\n', encoding='utf-8')
    (args.output / 'WeaponModeFamilies.generated.h').write_text(header, encoding='utf-8')
    if args.measure:
        rows = {r['configuration']: r for r in source['configurations']}
        done = set()
        for pair in pairs:
            if pair['secondary_asset'] != '40mmgl':
                continue
            key = coordinate_key(rows[pair['primary']], rows[pair['secondary']])
            if key in done:
                continue
            done.add(key)
            result = measure_pair(args.game, source, pair)
            (args.output / ('sight-' + key + '.json')).write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print(json.dumps(dict(asset=pair['primary_asset'], candidates=[r['bone'] for r in result['candidates']], rejected=result['rejected'])), flush=True)
    if args.sights:
        measurements = [read(p)[0] for p in sorted(args.output.glob('sight-*.json')) if p.name != 'sight-coverage.json']
        if args.verify_variants:
            variants = verify_variants(args.game, source, measurements)
            (args.output / 'variant-coordinates.json').write_text(json.dumps(variants, indent=2) + '\n', encoding='utf-8')
        else:
            variants = read(args.output / 'variant-coordinates.json')[0]
        sight, coverage = sight_header(source, measurements, variants)
        (args.output / 'SightAdapters.generated.h').write_text(sight, encoding='utf-8')
        (args.output / 'sight-coverage.json').write_text(json.dumps(coverage, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(configurations=len(source['configurations']), families=len(pairs), native_accepted=1)))


if __name__ == '__main__':
    sys.dont_write_bytecode = True
    main()
