"""Portable exact-content replay of explicitly authored magazine geometry.

Authored policies preserve failed strict-inference evidence. All replay inputs
are embedded metadata plus exact installed resource hashes; no private report,
reviewer workstation, native process or online service is consulted.
"""
import copy
import json
import struct
import bc2_firearm_magazine_batch as batch
import bc2_authored_magazine_geometry as g
import bc2_body_ammo_assets as ammo
import bc2_body_equipment_assets as equipment
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical, identity
from bc2_mesh_geometry import metadata, geometry, position_bytes
from inspect_bc2_mesh_asset import bone_hash, sha
from bc2_asset_resource_identity import profiles_match


def require(value, message):
    batch.require(value, message)


def embed_profile(profile):
    """One-time authoring export; numeric geometry is unchanged by embedding."""
    if profile.get('status') != 'explicit_mod_authored_geometry':
        return copy.deepcopy(profile)
    original = profile['geometry']['design']['mod_authored_policy']
    packet = batch.read_pin(profile['mod_policy_source'])
    candidate, pose = choose(packet, original)
    c = {k: copy.deepcopy(candidate[k]) for k in (
        'bone', 'parent', 'children', 'reference_members', 'local_bounds',
        'extent_m', 'triangles', 'mixed_triangles', 'reference_attached_item')}
    c['poses'] = [copy.deepcopy(pose)]
    c['design_proposals'] = [dict(copy.deepcopy(p), pose_index=0)
        for p in candidate['design_proposals']
        if p['pose_index'] == original['pose_index'] and p['axis'] == original['entry_axis']]
    content = {k: profile[k] for k in (
        'mesh_sha256', 'lod_sha256', 'skeleton_sha256', 'static_clip_sha256', 'rig_fingerprint')}
    content['reload_clip_sha256'] = profile['reload_clip']['sha256']
    evidence = dict(schema='fvr.bc2.embedded_mod_magazine_evidence.v1',
        source_packet_sha256=profile['mod_policy_source']['sha256'],
        preview={'sha256': packet['preview']['sha256']}, candidates=[c],
        content_identity=content, installed_frame_replay_required=True)
    evidence['evidence_digest'] = batch.descriptors.digest(evidence)
    review = copy.deepcopy(original)
    review['original_review_digest'] = review.pop('review_digest')
    review['pose_index'] = 0
    review['source_packet'] = dict(sha256=evidence['source_packet_sha256'], artifact='magazines45-measured-source-packet')
    review['embedded_evidence_digest'] = evidence['evidence_digest']
    review['review_digest'] = batch.descriptors.digest(review)
    out = copy.deepcopy(profile)
    out['mod_policy_evidence'] = evidence
    out['mod_policy_source'] = review['source_packet']
    out['geometry'] = geometry_for(evidence, review)
    out['role_evidence']['mod_policy_review_digest'] = review['review_digest']
    out.pop('profile_digest')
    out['profile_digest'] = batch.descriptors.digest(out)
    validate_profile(out)
    return out


def validate_profile(profile):
    """Pure, portable verification; never resolves a provenance path."""
    batch.validate_geometry(profile)
    if profile.get('status') != 'explicit_mod_authored_geometry':
        return
    require(profile.get('strict_inference_passed') is False, 'Authored policy cannot promote strict inference')
    evidence = profile.get('mod_policy_evidence', {})
    require(evidence.get('schema') == 'fvr.bc2.embedded_mod_magazine_evidence.v1'
        and evidence.get('evidence_digest') == batch.descriptors.digest({k:v for k,v in evidence.items() if k != 'evidence_digest'})
        and evidence.get('installed_frame_replay_required') is True, 'Embedded authored evidence changed')
    review = profile['geometry']['design']['mod_authored_policy']
    require(review.get('embedded_evidence_digest') == evidence['evidence_digest']
        and review['source_packet']['sha256'] == evidence['source_packet_sha256']
        and profile['mod_policy_source']['sha256'] == evidence['source_packet_sha256'], 'Embedded review/source join changed')
    content = {k:profile[k] for k in ('mesh_sha256','lod_sha256','skeleton_sha256','static_clip_sha256','rig_fingerprint')}
    content['reload_clip_sha256'] = profile['reload_clip']['sha256']
    require(content == evidence['content_identity'], 'Embedded policy coordinate content differs')
    candidate, _ = choose(evidence, review)
    require(profile['geometry'] == geometry_for(evidence, review), 'Authored geometry differs from exact selected source/design')
    require(profile['mod_authored_members'] == candidate['reference_members'], 'Authored visible assembly changed')
    require(profile['bones']['magazine'] == candidate['bone'] and
        profile['role_evidence']['mod_policy_review_digest'] == review['review_digest'], 'Authored root/review differs')
    parents = {candidate['bone']}
    for m in profile['mod_authored_members']:
        require(m['parent'] in parents and m['bone'] not in parents, 'Authored assembly parent order')
        g.inverse_rigid(g.matrix(m['item_from_bone']))
        parents.add(m['bone'])


def validate_installed_sample(profile, blobs):
    """Re-evaluate the reviewed complete frame from exact installed bytes."""
    validate_profile(profile)
    hashes = dict(mesh='mesh_sha256', lod='lod_sha256', skeleton='skeleton_sha256', static='static_clip_sha256')
    for label, field in hashes.items():
        require(sha(blobs[label]) == profile[field], 'Installed authored policy content changed: '+label)
    require(sha(blobs['reload']) == profile['reload_clip']['sha256'], 'Installed reviewed reload content changed')
    sk = Skeleton(blobs['skeleton']); ref = Clip(blobs['static']); clip = Clip(blobs['reload'])
    require(sk.fingerprint == profile['rig_fingerprint'], 'Installed authored rig differs')
    review = profile['geometry']['design']['mod_authored_policy']
    candidate, expected = choose(profile['mod_policy_evidence'], review)
    root = candidate['bone']; members = [root, *[m['bone'] for m in candidate['reference_members']]]
    require(sk.names[sk.parents[sk.names.index(root)]] == 'jntWpn_1', 'Installed magazine parent differs')
    pose = clip.evaluate(sk, review['frame_seconds'], [root, 'LeftHand', *g.FINGERS])['weapon_relative']
    item, wrist = g.matrix(pose[root]), g.matrix(pose['LeftHand'])
    observed = dict(item_from_hand=g.multiply(wrist, g.inverse_rigid(item)),
        wrist_from_fingers={n:g.multiply(g.matrix(pose[n]),g.inverse_rigid(wrist)) for n in g.FINGERS})
    for name in ('item_from_hand',):
        require(max(abs(a-b) for a,b in zip(observed[name],expected[name])) <= 1e-7, 'Installed reviewed wrist frame differs')
    for name in g.FINGERS:
        require(max(abs(a-b) for a,b in zip(observed['wrist_from_fingers'][name],expected['wrist_from_fingers'][name])) <= 1e-7, 'Installed reviewed finger differs')
    closed = ref.evaluate(sk, 0., members)['weapon_relative']; inverse = g.inverse_rigid(closed[root])
    require(max(abs(a-b) for a,b in zip(closed[root],candidate['reference_attached_item'])) <= 1e-7, 'Installed attachment reference differs')
    parts = g.rigid_parts(blobs['lod'],metadata(blobs['mesh'])[0],sk,strict_assembly_skin=True)
    points=[];triangles=0
    for name in members:
        part=parts[name]; require(part['mixed_triangles']==0,'Installed policy part has mixed ownership')
        transform=g.multiply(closed[name],inverse)
        points.extend(g.point(p,transform) for p in part['points']);triangles+=part['triangles']
    actual_bounds=g.bounds(points)
    require(triangles==candidate['triangles'] and max(abs(a-b) for actual,expected_bound in zip(actual_bounds,candidate['local_bounds']) for a,b in zip(actual,expected_bound))<=1e-7, 'Installed policy surface envelope differs')


def derive_reviewed_cache(game, pool, reviewed, resource_content=False):
    source=reviewed.get('mod_policy_source',{})
    require(set(source)=={'profile','mesh','lod','skeleton','static','reload'},'Complete portable magazine sources required')
    observed=copy.deepcopy(source);blobs={}
    for name in ('mesh','lod','skeleton','static','reload'):
        ref=source[name];archive=pool(game/ref['archive'])
        if not resource_content:require(archive.index_sha256==ref['archive_index_sha256'],'Authored source archive index differs')
        raw=archive.read_selected([ref['resource']])[ref['resource']]
        require(sha(raw)==ref['sha256'],'Authored source content differs')
        blobs[name]=raw;observed[name]['archive_index_sha256']=archive.index_sha256
    validate_installed_sample(source['profile'],blobs)
    row,record=_bake_policy_magazine(source['profile'],observed,blobs)
    require(profiles_match([row],[reviewed],resource_content),'Installed authored magazine catalog differs')
    return row,record


def derive_reviewed_display(game, pool, reviewed, resource_content=False):
    profile=reviewed['mod_policy_profile'];validate_profile(profile)
    a=pool(game/reviewed['configuration_archive']);cfg=reviewed['authored_configuration']
    if not resource_content:require(a.index_sha256==reviewed['configuration_archive_index_sha256'],'Authored display configuration archive differs')
    for ref in [cfg]+cfg['mesh_references']:
        raw=a.read_selected([ref['resource']])[ref['resource']]
        require(sha(raw)==ref['sha256'],'Authored display DBX content differs')
    sources=[dict(archive=s['archive'],mesh=s['mesh'],expected_metadata_sha256=s['mesh_sha256'],
        **({} if resource_content else dict(expected_index_sha256=s['archive_index_sha256']))) for s in reviewed['sources']]
    # LOD hashes are verified by the catalog's resource wrapper in content mode,
    # and below in exact-archive mode as well.
    for s in reviewed['sources']:
        archive=pool(game/s['archive']);name=s['mesh']+'_lod0_data.res'
        require(sha(archive.read_selected([name])[name])==s['lod_sha256'],'Authored display LOD differs')
    binding=dict(animation_source=dict(archive=reviewed['closed_archive'],archive_index_sha256=reviewed['closed_archive_index_sha256']),
        animation_chain=dict(animation_resource=reviewed['closed_clip']),animation_sha256=reviewed['closed_clip_sha256'])
    sk_archive=pool(game/ammo.SKELETON_ARCHIVE);raw=sk_archive.read_selected([ammo.SKELETON_RESOURCE])[ammo.SKELETON_RESOURCE]
    require(sha(raw)==ammo.SKELETON_SHA,'Authored display skeleton differs')
    row,record=sampled_body(game,pool,dict(asset=reviewed['asset'],meshes=sources,authored_configuration=cfg),binding,profile,Skeleton(raw))
    row.update(configuration_archive=reviewed['configuration_archive'],configuration_archive_index_sha256=a.index_sha256,
        closed_archive=reviewed['closed_archive'],closed_archive_index_sha256=pool(game/reviewed['closed_archive']).index_sha256)
    require(profiles_match([row],[reviewed],resource_content),'Installed authored display catalog differs')
    return row,record



def choose(packet, review):
    require(review.get('schema') == 'fvr.bc2.mod_authored_magazine_review.v1', 'Explicit mod policy review required')
    require(review.get('review_digest') == batch.descriptors.digest({k: v for k, v in review.items() if k != 'review_digest'}), 'Policy review digest changed')
    require(review['native_verified'] is False and review['headset_tested'] is False and (review['measured_stable_contact_claim'] is False), 'Authored policy cannot promote measurements')
    require(packet['preview']['sha256'] == review['preview_sha256'], 'Different measured geometry preview')
    candidate = [c for c in packet['candidates'] if c['bone'] == review['magazine_bone']]
    require(len(candidate) == 1, 'Exact reviewed magazine root absent')
    c = candidate[0]
    require(c['parent'] == 'jntWpn_1' and c['mixed_triangles'] == 0 and (c['triangles'] >= 24), 'Magazine policy requires independent rigid geometry directly owned by weapon')
    require(len(c['reference_members']) <= 8, 'Weighted assembly member bound')
    require(type(review['pose_index']) is int and 0 <= review['pose_index'] < len(c['poses']), 'Exact source pose index')
    pose = c['poses'][review['pose_index']]
    require(pose['time_seconds'] == review['frame_seconds'] and batch.descriptors.digest(pose) == review['pose_digest'], 'Selected complete authored frame changed')
    require(set(pose['wrist_from_fingers']) == set(g.FINGERS), 'Complete own-asset finger hierarchy required')
    for transform in [c['reference_attached_item'], pose['item_from_hand'], *pose['wrist_from_fingers'].values()]:
        g.inverse_rigid(g.matrix(transform))
    axis = review['entry_axis']
    sign = review['inward_sign']
    require(type(axis) is int and axis in (0, 1, 2) and (type(sign) is int) and (sign in (-1, 1)), 'Explicit feed-face policy')
    require(0.015 <= c['extent_m'][axis] <= 0.4 and min(c['extent_m']) >= 0.005, 'Finite measured policy envelope')
    return (c, pose)


def geometry_for(packet, review):
    c, pose = choose(packet, review)
    axis, sign = (review['entry_axis'], review['inward_sign'])
    if review['carry_policy'] == 'measured_lower_third_design':
        proposals = [p for p in c['design_proposals'] if p['pose_index'] == review['pose_index'] and p['axis'] == axis and ('geometry' in p)]
        require(len(proposals) == 1, 'Selected geometry-based carry/rail proposal absent')
        result = copy.deepcopy(proposals[0]['geometry'])
        require(result['design']['inward_direction'][axis] == sign, 'Selected feed face differs from measured design')
    elif review['carry_policy'] == 'exact_same_frame_short_box':
        lo, hi = c['local_bounds']
        tip = [(a + b) / 2 for a, b in zip(lo, hi)]
        tip[axis] = hi[axis] if sign > 0 else lo[axis]
        inward = [0.0, 0.0, 0.0]
        inward[axis] = float(sign)
        short = min((n for n in range(3) if n != axis), key=lambda n: c['extent_m'][n])
        across = [0.0, 0.0, 0.0]
        across[short] = 1.0
        insertion = g.frame(across, g.cross(inward, across), inward, tip)
        entry = g.multiply(insertion, c['reference_attached_item'])
        entry[12:15] = g.sub(entry[12:15], g.scale(entry[8:11], g.UX['travel_m']))
        result = dict(attached_item=c['reference_attached_item'], item_from_hand=pose['item_from_hand'], wrist_from_fingers=pose['wrist_from_fingers'], item_from_insertion=insertion, weapon_from_entry=entry, measured=dict(bone_local_extent_m=c['extent_m'], bounds_m=c['local_bounds'], part_triangle_count=c['triangles']), design=dict(status='experimental_geometry_based_estimate', carry='complete own-clip wrist and fingers at the reviewed exact frame', rail='explicit mod rail through the center of the measured receiver-facing bounding face', long_axis=axis, short_axis=short, inward_direction=inward, authored_reload_grasp=False, native_insertion_trajectory=False, ux_defaults=g.UX))
    else:
        raise ValueError('Unknown reviewed carry policy')
    result['measured']['closed_part_frame'] = 'exact evaluated authored reference at t=0; mod attachment policy, not native closed-pose proof'
    result['design']['mod_authored_policy'] = review
    result['design']['authored_reload_grasp'] = False
    return result


def header(profiles):
    for p in profiles:
        validate_profile(p)
    result = g.cpp_header(dict(schema='fvr.bc2.authored_magazine_geometry', schema_version=1, profiles=profiles), {p['native_asset_name'] for p in profiles})

    def number(x):
        s = format(x, '.9g')
        return s + ('f' if '.' in s or 'e' in s else '.f')

    def matrix(values):
        return 'math::Matrix4{{{' + ','.join(('{{' + ','.join((number(v) for v in values[n:n + 4])) + '}}' for n in range(0, 16, 4))) + '}}}'
    for index, p in enumerate(sorted(profiles, key=lambda p: (p['native_asset_name'], p['weapon']['resource'][:-4]))):
        members = p.get('mod_authored_members', [])
        if not members:
            continue
        needle = f'auto& g=out[{index}];g.asset=' + json.dumps(p['native_asset_name']) + ';g.mesh=' + json.dumps(p['configured_mesh_path']) + ';'
        lines = [f'g.assemblyCount={len(members)};']
        for i, m in enumerate(members):
            lines.append(f'g.assembly[{i}]={{' + json.dumps(m['bone']) + ',' + json.dumps(m['parent']) + ',' + matrix(m['item_from_bone']) + '};')
        require(result.count(needle) == 1, 'Exact geometry output insertion point')
        result = result.replace(needle, needle + '\n  ' + '\n  '.join(lines))
    return result


def pack_row(row, records):
    return ammo.text(row['asset']) + ammo.text(row['mesh']) + ammo.text(row['part']) + struct.pack('<QI', row['rig_fingerprint'], len(row['sections'])) + b''.join(records)


def section_record(name, vertices, indices, index_size=2):
    return ammo.text(name) + struct.pack('<5I', len(vertices), len(indices), index_size, 0, 0) + vertices + indices


def baked_section(name, vertices, indices):
    skin = b''.join((vertices[i] for i in indices))
    positions = b''.join((vertices[i][:12] for i in indices))
    return dict(section=name, count=len(indices), stride=20, palette_size=1, part_palette_index=0, position='Float3', vertex_skin_hash=f'{ammo.fnv(skin):016x}', position_hash=f'{ammo.fnv(positions):016x}', part_triangles=len(indices) // 3)


def _bake_policy_magazine(profile, source, blobs):
    """Bake the explicitly reviewed visible parts at the exact reference time."""
    validate_profile(profile)
    sk = Skeleton(blobs['skeleton'])
    clip = Clip(blobs['static'])
    root = profile['bones']['magazine']
    names = [root, *[m['bone'] for m in profile['mod_authored_members']]]
    closed = clip.evaluate(sk, 0.0, names)
    inverse_root = g.inverse_rigid(closed['weapon_relative'][root])
    batch.require(max((abs(a - b) for a, b in zip(closed['weapon_relative'][root], profile['geometry']['attached_item']))) < 1e-07, 'Authored attachment reference changed')
    transforms = {n: g.multiply(closed['weapon_relative'][n], inverse_root) for n in names}
    for member in profile['mod_authored_members']:
        batch.require(max((abs(a - b) for a, b in zip(transforms[member['bone']], member['item_from_bone']))) < 1e-07, 'Authored visible member reference changed')
    known = {bone_hash(n): n for n in sk.names}
    inverse = {n: canonical(b['InverseWorldTransform']) for n, b in zip(sk.names, sk.bones)}
    lod = metadata(blobs['mesh'])[0]
    data = blobs['lod']
    sections = geometry(data, lod, known)
    vb = sum((s['vertices'] * s['stride'] for s in sections))
    ib = sum((s['triangles'] * 6 for s in sections))
    start = len(data) - vb - ib
    rows = []
    records = []
    for s in sections:
        if s['name'].endswith('_ZOnly') or not set(s['bone_names']) & set(names):
            continue
        size = position_bytes(s)
        half = size == 8
        rawids = struct.unpack_from('<' + 'H' * (s['triangles'] * 3), data, start + vb + s['first_index'] * 2)
        vertices = []
        for offset in range(0, len(rawids), 3):
            tri = []
            owners = []
            for vertex in rawids[offset:offset + 3]:
                at = start + s['vertex_offset'] + vertex * s['stride']
                ids = data[at + size:at + size + 4]
                weights = data[at + size + 4:at + size + 8]
                batch.require(len(weights) == 4 and sum((w > 0 for w in weights)) == 1 and (255 in weights), 'Policy magazine requires rigid triangle weights')
                name = known[lod['palette'][s['palette'][ids[weights.index(255)]]]]
                owners.append(name)
                if name in transforms:
                    p = struct.unpack_from('<3e' if half else '<3f', data, at)
                    local = g.point((p[0], p[1], -p[2]), g.multiply(inverse[name], transforms[name]))
                    batch.require(max(map(abs, local)) <= 1.5, 'Authored magazine point bound')
                    tri.append(struct.pack('<3f8B', local[0], local[1], -local[2], 0, 0, 0, 0, 255, 0, 0, 0))
            batch.require(len(set(owners)) == 1, 'Policy magazine rejects cross-part triangles')
            if owners[0] in transforms:
                vertices += tri
        if not vertices:
            continue
        indices = list(range(len(vertices)))
        index_size = 2 if len(vertices) <= 65536 else 4
        batch.require(len(vertices) <= 32768 * 3, 'Policy magazine section bound')
        rows.append(baked_section(s['name'], vertices, indices))
        records.append(section_record(s['name'], b''.join(vertices), struct.pack('<' + ('H' if index_size == 2 else 'I') * len(indices), *indices), index_size))
    batch.require(0 < len(rows) <= 8 and sum((r['part_triangles'] for r in rows)) <= 32768, 'Policy magazine row bound')
    row = dict(asset=profile['native_asset_name'], mesh=profile['configured_mesh_path'], part=root, rig_fingerprint=int(sk.fingerprint.split(':')[1], 16), inverse_bind=identity(), sections=rows, mesh_sha256=sha(blobs['mesh']), lod_sha256=sha(blobs['lod']), mod_authored_magazine=True, render_only=True, native_admission=False, geometry_profile_digest=profile['profile_digest'], mod_policy_review_digest=profile['geometry']['design']['mod_authored_policy']['review_digest'], mod_policy_source=source, closed_pose='sampled_authored_mod_reference', reference_time_seconds=0.0)
    return (row, pack_row(row, records))


def sampled_body(game, pool, spec, binding, profile, skeleton):
    """Independent authored display policy; does not alter strict body.composite."""
    validate_profile(profile)
    ss = binding['animation_source']
    archive = pool(game / ss['archive'])
    name = binding['animation_chain']['animation_resource']
    raw = archive.read_selected([name])[name]
    batch.require(sha(raw) == binding['animation_sha256'] == profile['static_clip_sha256'], 'Sampled body source changed')
    clip = Clip(raw)
    known = {bone_hash(n): n for n in skeleton.names}
    inverse = {n: canonical(b['InverseWorldTransform']) for n, b in zip(skeleton.names, skeleton.bones)}
    decoded = []
    sources = []
    required = {'RightHand', 'jntWpn_Flash'}
    for s in spec['meshes']:
        a = pool(game / s['archive'])
        mesh = s['mesh']
        names = [mesh + '.res', mesh + '_lod0_data.res']
        blobs = a.read_selected(names)
        if s.get('expected_metadata_sha256'):
            batch.require(sha(blobs[names[0]]) == s['expected_metadata_sha256'], 'Sampled body mesh differs')
        if s.get('expected_index_sha256'):
            batch.require(a.index_sha256 == s['expected_index_sha256'], 'Sampled body archive differs')
        lod = metadata(blobs[names[0]])[0]
        data = blobs[names[1]]
        sections = geometry(data, lod, known)
        vb = sum((r['vertices'] * r['stride'] for r in sections))
        ib = sum((r['triangles'] * 6 for r in sections))
        start = len(data) - vb - ib
        for section in sections:
            if section['name'].endswith('_ZOnly'):
                continue
            required.update(section['bone_names'])
            decoded.append((data, section, lod, start, vb))
        sources.append(dict(archive=s['archive'], archive_index_sha256=a.index_sha256, mesh=mesh, mesh_sha256=sha(blobs[names[0]]), lod_sha256=sha(data)))
    closed = clip.evaluate(skeleton, 0.0, sorted(required))
    batch.require(all((v in ('static_authored_pose', 'decoded_spline_candidate') for v in closed['bone_evaluation_status'].values())), 'Unsupported authored display frame')
    vertices = []
    indices = []
    for data, section, lod, start, vb in decoded:
        baked = equipment.bake_section(data, section, lod, known, inverse, closed['weapon_relative'], start)
        ids = struct.unpack_from('<' + 'H' * (section['triangles'] * 3), data, start + vb + section['first_index'] * 2)
        batch.require(ids and max(ids) < len(baked), 'Authored body indices invalid')
        offset = len(vertices)
        indices.extend((i + offset for i in ids))
        vertices.extend(baked)
    batch.require(0 < len(indices) // 3 <= 32768 and len(vertices) <= 131072, 'Authored body bounds')
    cfg = spec['authored_configuration']
    part = 'body_holstered_' + batch.hashlib.sha256(cfg['resource'].encode()).hexdigest()[:16]
    section = baked_section('closed_configuration', vertices, indices)
    index_size = 2 if len(vertices) <= 65536 else 4
    row = dict(asset=spec['asset'], mesh=profile['configured_mesh_path'], part=part, rig_fingerprint=int(skeleton.fingerprint.split(':')[1], 16), inverse_bind=identity(), sections=[section], display_only=True, configuration_path=cfg['resource'][:-4], configured_meshes=sorted((s['mesh'] for s in sources)), authored_configuration=cfg, closed_clip=name, closed_clip_sha256=sha(raw), closed_pose='sampled_authored_mod_reference', sources=sources, display_anchor_evaluation={n: closed['bone_evaluation_status'][n] for n in ('RightHand', 'jntWpn_Flash')}, display_anchor_time_seconds=0, display_anchor_not_native_grip_calibration=True, mod_authored_display=True, mod_policy_review_digest=profile['geometry']['design']['mod_authored_policy']['review_digest'], mod_policy_profile=profile, reference_bone_evaluation=closed['bone_evaluation_status'], holster_from_weapon=equipment.holster_transform(closed['weapon_relative']['RightHand'][12:15], closed['weapon_relative']['jntWpn_Flash'][12:15]), grip=closed['weapon_relative']['RightHand'][12:15], muzzle=closed['weapon_relative']['jntWpn_Flash'][12:15])
    return (row, pack_row(row, [section_record('closed_configuration', b''.join(vertices), struct.pack('<' + ('H' if index_size == 2 else 'I') * len(indices), *indices), index_size)]))
