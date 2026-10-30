"""Exact five-config pistol evidence join; optional pinned-entry contact sampler.

Writes only this directory. Default reuses cached analytic rails/MP443 landmarks.
--game reads named, hashed resources into memory; never discovers archives,
extracts assets to disk, enables a registry, or assigns native/chamber semantics.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct
import sys
import zlib

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
WORKTREE = HERE.parents[3]
EVIDENCE = WORKTREE.parents[1]
sys.path.insert(0, str(WORKTREE / 'tools'))
import bc2_authored_linear_mechanism as linear
import bc2_authored_magazine_geometry as mag
from bc2_magazine_contact_batch import ArchivePool
from bc2_mesh_geometry import metadata, geometry, position_bytes
from bc2_weapon_animation_pipeline import Clip, Skeleton, canonical, inverse_rigid, multiply
from inspect_bc2_mesh_asset import bone_hash

PLAN = WORKTREE / 'handoff/tasks/CHAT14-16/20261030-familybatch36/input-plan.json'
ASSETS = ('M9', 'M9-3', 'M1911', 'MP443', 'MP443_sp')
REVIEWER = 'CHAT16 batch37 offline authored-data review'
MISSING_CONTACT = ('slideFromAuthoredWrist', 'ring3Part', 'nearestVertexPart',
                   'authoredLandmarkSeconds', 'ring3VertexDistanceMeters')
PREVIEWS = WORKTREE / 'reports/coordinator-20261030-pipeline34/pistol-role-review'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def digest(value):
    return sha(json.dumps(value, sort_keys=True, separators=(',', ':'), allow_nan=False).encode())


def require(value, message):
    if not value:
        raise ValueError(message)


def ref(source, pointer):
    return dict(source=source, pointer=pointer)


def at(value, pointer):
    for token in pointer.split('/')[1:]:
        value = value[int(token)] if isinstance(value, list) else value[token]
    return value


def write(name, value):
    path = HERE / name
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + '\n', encoding='utf-8')
    return dict(path=path.relative_to(EVIDENCE).as_posix(), sha256=sha(path.read_bytes()))


def content(row):
    return dict(mesh_sha256=row['source']['mesh_sha256'], lod_sha256=row['source']['lod_sha256'],
                skeleton_sha256=row['skeleton_sha256'], rig_fingerprint=row['rig_fingerprint'],
                clip_sha256=row['source']['sha256'], reference_clip_sha256=row['source']['reference_clip_sha256'])


def topology(binding, rail):
    bones = binding['rig_topology']
    names = [b['name'] for b in bones]
    root, part = names.index(rail['root']), names.index(rail['part'])
    require(bones[part]['parent_index'] == root, 'Rail is not direct child')
    require(all(b['parent_index'] != part for b in bones), 'Rail is not leaf')
    return dict(root=dict(bone=names[root], index=root),
                slide=dict(bone=names[part], index=part, parent=names[root], parentIndex=root))


def policy(rail, asset):
    low, high = (rail['decoded_travel'][k] for k in ('minimum_m', 'maximum_m'))
    span = high - low
    # Reviewed VR design: use the middle 80% of EACH actual decoder range.
    # Endpoint bands + hysteresis use 6%, retaining 4% signed-range reserve.
    return dict(name=asset + ' authored-rail candidate policy',
        sourceReference='CHAT16/20261030-batch37/role-policy.json: middle 80 percent of exact signed rail',
        revision=1, sourceDisplacementMeters=low + .10*span, strokeMeters=.80*span,
        frontToleranceMeters=.04*span, rearToleranceMeters=.04*span, hysteresisMeters=.02*span,
        contactRadiusMeters=.035, lateralToleranceMeters=.025, rotationToleranceRadians=.20,
        maxStepMeters=.20*span, endpointDwellNs=25000000, maximumStrokeNs=3000000000)


def preview(raw, lod, sk, clip, reference, row, rail):
    """Bounded CPU mesh preview using the shared mesh layout and skin decoder."""
    sections = geometry(raw, lod, {bone_hash(n): n for n in sk.names})
    names = {bone_hash(n): n for n in sk.names}
    inverse = {n: canonical(b['InverseWorldTransform']) for n, b in zip(sk.names, sk.bones)}
    vb = sum(s['vertices']*s['stride'] for s in sections)
    ib = sum(s['triangles']*6 for s in sections)
    start = len(raw)-vb-ib
    vertices, triangles, ranges = [], [], []
    for section in sections:
        if section['name'].endswith('_ZOnly'):
            continue
        offset, first_triangle = len(vertices), len(triangles)
        pbytes = position_bytes(section)
        for i in range(section['vertices']):
            pos = start+section['vertex_offset']+i*section['stride']
            p = struct.unpack_from('<3e' if pbytes == 8 else '<3f', raw, pos)
            ids, weights = raw[pos+pbytes:pos+pbytes+4], raw[pos+pbytes+4:pos+pbytes+8]
            slot, = [k for k, weight in enumerate(weights) if weight]
            require(weights[slot] == 255, 'Preview requires exact rigid vertex')
            owner = names[lod['palette'][section['palette'][ids[slot]]]]
            vertices.append((mag.point([p[0], p[1], -p[2]], inverse[owner]), owner))
        ids = struct.unpack_from('<'+'H'*(section['triangles']*3), raw, start+vb+section['first_index']*2)
        for i in range(0, len(ids), 3):
            indices = tuple(offset+k for k in ids[i:i+3])
            owners = {vertices[k][1] for k in indices}
            require(len(owners) == 1, 'Preview mixed triangle')
            triangles.append((indices, owners.pop()))
        slide_indices = [i-offset for i in range(offset, len(vertices)) if vertices[i][1] == rail['part']]
        ranges.append(dict(section=section['name'], vertex_offset_bytes=section['vertex_offset'],
            stride_bytes=section['stride'], first_index=section['first_index'],
            index_count=section['triangles']*3, decoded_vertex_start=offset, decoded_vertex_count=section['vertices'],
            decoded_triangle_start=first_triangle, decoded_triangle_count=section['triangles'],
            slide_section_vertex_indices=slide_indices))
    width, height = 1280, 720
    pixels = bytearray((22, 26, 34)*(width*height))
    depth = [-float('inf')]*(width*height)
    def put(x, y, color):
        if 0 <= x < width and 0 <= y < height:
            pixels[(y*width+x)*3:(y*width+x)*3+3] = bytes(color)
    # Five-column bitmap glyphs keep the preview dependency-free and labeled.
    encoded = {
        'A':'01110100011000111111100011000110001','B':'11110100011000111110100011000111110',
        'C':'01111100001000010000100001000001111','D':'11110100011000110001100011000111110',
        'E':'11111100001000011110100001000011111','F':'11111100001000011110100001000010000',
        'G':'01111100001000010111100011000101111','H':'10001100011000111111100011000110001',
        'I':'11111001000010000100001000010011111','J':'00111000100001000010000101001001100',
        'K':'10001100101010011000101001001010001','L':'10000100001000010000100001000011111',
        'M':'10001110111010110101100011000110001','N':'10001110011010110011100011000110001',
        'O':'01110100011000110001100011000101110','P':'11110100011000111110100001000010000',
        'Q':'01110100011000110001101011001001101','R':'11110100011000111110101001001010001',
        'S':'01111100001000001110000010000111110','T':'11111001000010000100001000010000100',
        'U':'10001100011000110001100011000101110','V':'10001100011000110001100010101000100',
        'W':'10001100011000110101101011101110001','X':'10001100010101000100010101000110001',
        'Y':'10001100010101000100001000010000100','Z':'11111000010001000100010001000011111',
        '0':'01110100011001110101110011000101110','1':'00100011000010000100001000010001110',
        '2':'01110100010000100010001000100011111','3':'11110000010000101110000010000111110',
        '4':'00010001100101010010111110001000010','5':'11111100001000011110000010000111110',
        '6':'01110100001000011110100011000101110','7':'11111000010001000100010000100001000',
        '8':'01110100011000101110100011000101110','9':'01110100011000101111000010000101110',
        '_':'00000000000000000000000000000011111','-':'00000000000000011111000000000000000',
        '.':'00000000000000000000000000011000110', ':':'00000001100011000000001100011000000'}
    def label(text, x, y, scale=2, color=(235, 240, 245)):
        for char in text.upper():
            for k, bit in enumerate(encoded.get(char, '0'*35)):
                if bit == '1':
                    for dx in range(scale):
                        for dy in range(scale): put(x+(k%5)*scale+dx, y+(k//5)*scale+dy, color)
            x += 6*scale
    camera = mag.unit((1., .45, .2))
    right = mag.unit(mag.cross((0, 1, 0), camera)); up = mag.cross(camera, right)
    def project(p): return (mag.dot(p, right), mag.dot(p, up), mag.dot(p, camera))
    peak = rail['decoded_travel']['maximum_time_seconds']
    worlds = []
    for src, time in ((reference, 0.), (clip, peak)):
        pose = src.evaluate(sk, time, sorted({owner for _, owner in vertices}))['weapon_relative']
        worlds.append([mag.point(p, pose[owner]) for p, owner in vertices])
    projected = [[project(p) for p in world] for world in worlds]
    # Fixed weapon framing; the reload's detached magazine can lie off-screen.
    framing = [p for pose in projected for p, (_, owner) in zip(pose, vertices) if owner != 'jntWpn_12']
    lo = [min(p[k] for p in framing) for k in range(2)]; hi = [max(p[k] for p in framing) for k in range(2)]
    factor = min(580/(hi[0]-lo[0]), 550/(hi[1]-lo[1]))
    for panel, (world, points) in enumerate(zip(worlds, projected)):
        sp = [(panel*640+320+(p[0]-(lo[0]+hi[0])/2)*factor,
               350-(p[1]-(lo[1]+hi[1])/2)*factor, p[2]) for p in points]
        for ids, owner in triangles:
            A, B, C = [sp[k] for k in ids]
            den = (B[1]-C[1])*(A[0]-C[0])+(C[0]-B[0])*(A[1]-C[1])
            if abs(den) < 1e-9: continue
            lx, hx = max(panel*640+5, int(min(A[0], B[0], C[0]))), min(panel*640+634, math.ceil(max(A[0], B[0], C[0])))
            ly, hy = max(70, int(min(A[1], B[1], C[1]))), min(650, math.ceil(max(A[1], B[1], C[1])))
            normal = mag.cross(mag.sub(world[ids[1]], world[ids[0]]), mag.sub(world[ids[2]], world[ids[0]]))
            norm = mag.norm(normal)
            shade = .55+.45*abs(mag.dot(normal, camera))/norm if norm else .65
            color = tuple(round(c*shade) for c in ((50,225,245) if owner == rail['part'] else (145,150,160)))
            for y in range(ly, hy+1):
                for x in range(lx, hx+1):
                    u = ((B[1]-C[1])*(x-C[0])+(C[0]-B[0])*(y-C[1]))/den
                    v = ((C[1]-A[1])*(x-C[0])+(A[0]-C[0])*(y-C[1]))/den; w = 1-u-v
                    if min(u, v, w) < -1e-8: continue
                    z = u*A[2]+v*B[2]+w*C[2]; at = y*width+x
                    if z > depth[at]: depth[at] = z; put(x, y, color)
        label(row['asset'] + (' STATIC REFERENCE' if panel == 0 else ' MAXIMUM RETRACTION'), panel*640+24, 22)
        label('CYAN JNTWPN_3 INDEX30 - PARENT JNTWPN_1 INDEX10', panel*640+24, 680, 2)
    scan = b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
    png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0))+chunk(b'IDAT', zlib.compress(scan, 6))+chunk(b'IEND', b'')
    require(len(png) < 4*1024*1024, 'Bounded preview size')
    PREVIEWS.mkdir(parents=True, exist_ok=True)
    output = PREVIEWS / (row['asset'].lower() + '.png')
    output.write_bytes(png)
    return dict(path=output.relative_to(EVIDENCE).as_posix(), sha256=sha(png),
        source_content=content(row), width=width, height=height, static_time_s=0., retracted_time_s=peak,
        candidate_bone=rail['part'], candidate_index=sk.names.index(rail['part']),
        parent=rail['root'], parent_index=sk.names.index(rail['root']), direct_child_leaf=True,
        part_bounds_local=rail['part_bounds_local'], visible_triangles=len(triangles), sections=ranges,
        legend='Cyan candidate slide, grey all other rigid pistol geometry; identical camera and scale.',
        native_playback=False, raw_assets_written=False)


def sample_group(game, pool, row, binding, rail, previews=False):
    """Read precisely mesh/LOD/skeleton/reference/reload, joined to cached hashes."""
    mesh, = [m for m in binding['meshes'] if m['configured_mesh_path'] == row['configured_mesh']]
    lod, = [l for l in mesh['geometry']['lods'] if l['lod'] == 0]
    ms, ss, rs = row['source']['mesh_source'], binding['skeleton_source'], binding['animation_source']
    cs = row['source']
    named = [
        (ms, mesh['mesh_resource'], 'SkinnedMeshSet', row['source']['mesh_sha256']),
        (ms, lod['data_resource'], 'MeshData', row['source']['lod_sha256']),
        (ss, binding['animation_chain']['skeleton_resource'], 'GrannyModel', row['skeleton_sha256']),
        (rs, binding['animation_chain']['animation_resource'], 'GrannyAnimation', rail['reference_clip_sha256']),
        (cs, row['source']['resource'], 'GrannyAnimation', rail['clip_sha256'])]
    raw, provenance, batches = {}, [], {}
    for src, resource, kind, expected in named:
        index = src['archive_index_sha256']
        key = (src['archive'], index)
        batches.setdefault(key, []).append((resource, kind, expected))
    for (archive, index), wanted in batches.items():
        source = pool(game / archive)
        require(source.index_sha256 == index, 'Pinned archive index changed: ' + archive)
        for resource, kind, expected in wanted:
            entry, = [e for e in source.entries if e.flags == 65536 and e.name == resource]
            require(entry.kind == kind, 'Pinned resource type changed')
        values = source.read_selected([name for name, _, _ in wanted])
        for resource, kind, expected in wanted:
            require(sha(values[resource]) == expected, 'Pinned resource bytes changed: ' + resource)
            raw[resource] = values[resource]
            provenance.append(dict(archive=archive, archive_index_sha256=index,
                                   resource=resource, kind=kind, sha256=expected))
    sk = Skeleton(raw[binding['animation_chain']['skeleton_resource']])
    clip, reference = Clip(raw[row['source']['resource']]), Clip(raw[binding['animation_chain']['animation_resource']])
    require(sk.fingerprint == row['rig_fingerprint'], 'Exact rig differs')
    fresh = linear.measure(clip, reference, sk, rail['part'], rail['root'],
                           lateral_tolerance_m=.001, rotation_tolerance_rad=math.radians(.05))
    require(all(fresh[k] == rail[k] for k in fresh), 'Cached analytic rail differs from shared helper')
    mesh_metadata = metadata(raw[mesh['mesh_resource']])[0]
    parts = mag.rigid_parts(raw[lod['data_resource']], mesh_metadata, sk,
                            strict_assembly_skin=True)
    part = parts[rail['part']]
    require(part['mixed_triangles'] == 0 and part['triangles'] == rail['rigid_triangles'], 'Part geometry differs')
    require(mag.bounds(part['points']) == tuple(rail['part_bounds_local']), 'Part bounds differ')
    # A bounded sampled contact search does NOT claim analytic contact extrema.
    # Include the actual analytic travel extrema in the 60Hz sample set.
    times = {min(i/60, clip.duration) for i in range(math.ceil(clip.duration*60)+1)}
    times.update(fresh['decoded_travel'][k] for k in ('minimum_time_seconds', 'maximum_time_seconds'))
    samples = []
    low, high = (fresh['decoded_travel'][k] for k in ('minimum_m', 'maximum_m'))
    for time in sorted(times):
        pose = clip.evaluate(sk, time, [rail['part'], 'LeftHand', *mag.FINGERS])['weapon_relative']
        inverse = inverse_rigid(pose[rail['part']])
        ring = mag.point(pose['LeftHandRing3'][12:15], inverse)
        nearest = min(part['points'], key=lambda p: math.dist(p, ring))
        fingers = []
        for finger in mag.FINGERS:
            point = mag.point(pose[finger][12:15], inverse)
            vertex = min(part['points'], key=lambda p: math.dist(p, point))
            fingers.append(dict(joint=finger, joint_part_m=point, vertex_part_m=vertex, distance_m=math.dist(point, vertex)))
        displacement = sum((pose[rail['part']][12+k]-fresh['reference_part'][12+k])*fresh['axis'][k] for k in range(3))
        if displacement < low + .20*(high-low):
            continue
        samples.append(dict(time_s=time, displacement_m=displacement,
            part_from_wrist=multiply(pose['LeftHand'], inverse), joint='LeftHandRing3',
            joint_part_m=ring, vertex_part_m=nearest, distance_m=math.dist(ring, nearest),
            nearest_finger=min(fingers, key=lambda f: f['distance_m'])))
    require(samples, 'No retracted authored sample')
    chosen = min(samples, key=lambda s: (s['distance_m'], s['time_s']))
    result = dict(method='sampled_60Hz_plus_analytic_travel_extrema_Ring3_nearest_rigid_vertex',
        sample_count=len(times), eligible_sample_count=len(samples), selection=chosen,
        nearest_five=sorted(samples, key=lambda s: (s['distance_m'], s['time_s']))[:5],
        resources=provenance, stable_controller_grasp_verified=False, analytic_contact_extrema=False,
        nearest_any_finger_sample=min(samples, key=lambda s: (s['nearest_finger']['distance_m'], s['time_s'])),
        limit='Transient same-frame authored landmark; not controller calibration, skin collision or native state.')
    if previews:
        result['preview'] = preview(raw[lod['data_resource']], mesh_metadata, sk, clip, reference, row, rail)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--game', type=Path, help='Optional exact pinned-entry sampler; no archive search')
    ap.add_argument('--previews', action='store_true', help='Two bounded labeled M9/M1911 PNGs in authorized report directory')
    args = ap.parse_args()
    require(not args.previews or args.game, '--previews requires --game')
    plan = json.loads(PLAN.read_text(encoding='utf-8'))
    needed = ('actions', 'pistol_bindings', 'm1911_bindings', 'mp443_geometry', 'mp443_roles')
    loaded, pins = {}, {}
    for name in needed:
        pin = plan['sources'][name]
        data = (EVIDENCE / pin['path']).read_bytes()
        require(sha(data) == pin['sha256'], 'Input plan pin changed: ' + name)
        loaded[name], pins[name] = json.loads(data), pin
    root_visual = {}
    visual_path = HERE / 'root-visual-review.json'
    if visual_path.exists():
        visual_bytes = visual_path.read_bytes()
        visual = json.loads(visual_bytes)
        require(visual['reviewer'] == 'root' and visual['roles_reviewed'] is True, 'Missing root visual review')
        for index, group in enumerate(visual['geometry_groups']):
            require(sha((EVIDENCE / group['preview']['path']).read_bytes()) == group['preview']['sha256'],
                    'Root-reviewed preview changed')
            root_visual[digest(group['content_hashes'])] = dict(index=index, review=group)
        pins['root_visual'] = dict(path=visual_path.relative_to(EVIDENCE).as_posix(), sha256=sha(visual_bytes))
    helper_hash = sha(Path(linear.__file__).read_bytes())
    require(helper_hash == loaded['mp443_geometry']['reader_hashes']['bc2_authored_linear_mechanism.py'],
            'Linear helper differs from reviewed cache producer')
    seed_geometry, seed_review = loaded['mp443_geometry'], loaded['mp443_roles']
    seed_row = loaded['actions']['records'][423]
    require(seed_review['geometry_sha256'] == pins['mp443_geometry']['sha256'], 'Role seed geometry differs')
    require(seed_review['semantic_review_status'] == 'root_reviewed_authored_slide_presentation', 'Unreviewed seed')
    seed = seed_review['authored_finger_landmarks'][1]
    ring, = [j for j in seed['nearest_four_joints'] if j['joint'] == 'LeftHandRing3']
    seed_content = content(seed_row)
    require(all(seed_geometry['rail'][k] == seed_row['linear_candidates'][0][k]
                for k in seed_geometry['rail']), 'Seed rail differs')
    require(all(expected in {v['sha256'] for v in seed_geometry['resources'].values()}
                for name, expected in seed_content.items() if name != 'rig_fingerprint'), 'Seed resource hashes differ')
    exact = [p for p in plan['configurations'] if p['kind'] == 'pistol_slide']
    require({p['asset'] for p in exact} == set(ASSETS) and len(exact) == 5, 'Exact five-config batch changed')
    exact.sort(key=lambda p: ASSETS.index(p['asset']))
    common = loaded['pistol_bindings']['profiles'][0]['skeleton_source']
    pool = ArchivePool(args.game, common['archive'], common['archive_index_sha256']) if args.game else None
    groups, configurations, reviews, annotations, pending = {}, [], [], [], []
    for item in exact:
        row = at(loaded['actions'], item['authored_anchor']['pointer'])
        require(row['asset'] == item['asset'] and row['weapon_configuration']['sha256'] == item['configuration_sha256']
                and row['weapon_configuration']['instance_guid'] == item['configuration_guid']
                and row['weapon_configuration']['resource'] == item['configuration_resource'], 'Exact config differs')
        binding_source = 'm1911_bindings' if row['asset'] == 'M1911' else 'pistol_bindings'
        binding_index, binding = next((i, b) for i, b in enumerate(loaded[binding_source]['profiles'])
            if b['native_asset_name'] == row['asset'] and b['weapon'] == row['weapon_configuration'])
        require(binding['binding_digest'] == row['binding_digest'], 'Exact binding differs')
        rail, = row['linear_candidates']
        role = topology(binding, rail)
        part, = [p for p in row['parts'] if p['bone'] == rail['part']]
        require(part['direct_weapon_child'] and part['independent_rigid_geometry'] and not part['mixed_triangles'],
                'Ambiguous rigid leaf geometry')
        require(rail['decoded_travel']['method'] == 'per_knot_polynomial_endpoints_and_stationary_points',
                'Sample extrema cannot replace analytic rail')
        hashes = content(row)
        key = digest(hashes)
        seed_match = hashes == seed_content
        reviewed_role = seed_match or key in root_visual
        if key not in groups:
            measured = dict(weaponFromStaticSlide=rail['reference_part'], rearward=rail['axis'],
                partBoundsMin=rail['part_bounds_local'][0], partBoundsMax=rail['part_bounds_local'][1],
                minimumDisplacementMeters=rail['decoded_travel']['minimum_m'],
                maximumDisplacementMeters=rail['decoded_travel']['maximum_m'], linearHelperSha256=helper_hash)
            contact = None
            if seed_match:
                contact = dict(method='reviewed_exact_content_join_of_MP443_SP_seed',
                    selection=dict(time_s=seed['time_s'], part_from_wrist=seed['part_from_wrist'], **ring),
                    source=ref('mp443_roles', '/authored_finger_landmarks/1'), stable_controller_grasp_verified=False)
            elif pool:
                contact = sample_group(args.game, pool, row, binding, rail, args.previews)
            if contact:
                selected = contact['selection']
                measured.update(slideFromAuthoredWrist=selected['part_from_wrist'], ring3Part=selected['joint_part_m'],
                    nearestVertexPart=selected['vertex_part_m'], authoredLandmarkSeconds=selected['time_s'],
                    ring3VertexDistanceMeters=selected['distance_m'])
            groups[key] = dict(content_hashes=hashes, analytic_rail=rail, roles=role,
                geometry=measured, contact=contact, slide_sections=part['sections'],
                authored_contact_window_count=part['contact']['windows'],
                missing_geometry_fields=[] if contact else list(MISSING_CONTACT))
        group = groups[key]
        require(group['analytic_rail'] == rail and group['roles'] == role and group['slide_sections'] == part['sections'],
                'Content-deduplicated geometry differs')
        index = len(configurations)
        config = dict(asset=row['asset'], weapon_configuration=row['weapon_configuration'],
            configured_mesh=row['configured_mesh'], rig_fingerprint=row['rig_fingerprint'],
            binding_digest=row['binding_digest'], geometry_group=key,
            authored_anchor=item['authored_anchor'], binding=ref(binding_source, f'/profiles/{binding_index}'),
            descriptor_key=item['descriptor_key'], descriptor_digest=item['descriptor_digest'],
            roles=role, missing_geometry_fields=group['missing_geometry_fields'],
            semantic_role_reviewed=reviewed_role, runtime_admitted=False, native_chamber_verified=False)
        if row['asset'] == 'M1911' and group['contact']:
            config['contact_quality'] = 'Ring3 landmark is distant from slide; numeric pose is measured but contact/grasp is unresolved.'
        configurations.append(config)
        pol = policy(rail, row['asset'])
        review = dict(asset=row['asset'], weapon_configuration=row['weapon_configuration'],
            configured_mesh=row['configured_mesh'], rig_fingerprint=row['rig_fingerprint'],
            roles=role, roles_reviewed=reviewed_role, policy_reviewed=True, reviewer=REVIEWER,
            semantic_role='pistol_slide' if reviewed_role else 'unreviewed_direct_child_linear_leaf',
            review_basis='Exact mesh/LOD/skeleton/rig/reload/reference hash equality with root-reviewed SP slide shell.'
                if seed_match else 'Topology and analytic rail verified; distinct mesh requires bounded visual semantic review.',
            source_role_review=ref('mp443_roles', '') if seed_match else
                ref('root_visual', '/geometry_groups/' + str(root_visual[key]['index'])) if key in root_visual else None,
            interaction_policy=pol,
            policy_basis='Explicit experimental VR policy, not authored/native tolerances: central 80% of signed analytic span; 4% endpoint + 2% hysteresis each end; 4% rail reserve.',
            nativeClosedVerified=False, nativeRetractedVerified=False, stableControllerGraspVerified=False,
            native_admission=False, headset_acceptance=False,
            blockers=([] if reviewed_role else ['Exact mesh semantic slide role requires visual review']) +
                ['Missing cached geometry field: ' + f for f in group['missing_geometry_fields']])
        if key in root_visual:
            review['review_basis'] = root_visual[key]['review']['finding']
            review['visual_reviewer'] = 'root'
            review['preview'] = root_visual[key]['review']['preview']
        if row['asset'] == 'M1911' and group['contact']:
            review['calibration_limitations'] = ['Ring3 is 32.25 mm from slide at its nearest sampled retracted pose; nearest other finger is Thumb2 at 11.22 mm. This is a mod-policy calibration limitation for later user testing; authored slide grasp is not verified.']
        reviews.append(review)
        if not group['missing_geometry_fields']:
            annotation = dict(kind='pistol_slide', descriptor_key=item['descriptor_key'],
                descriptor_digest=item['descriptor_digest'], native_class='SingleFireMagazineZeroBolt',
                configuration=ref('pistol_measured', f'/configurations/{index}/weapon_configuration'),
                asset=ref('pistol_measured', f'/configurations/{index}/asset'),
                meshPath=ref('pistol_measured', f'/configurations/{index}/configured_mesh'),
                rigFingerprint=ref('pistol_measured', f'/configurations/{index}/rig_fingerprint'),
                roles=ref('pistol_measured', f'/geometry_groups/{key}/roles'),
                geometry=ref('pistol_measured', f'/geometry_groups/{key}/geometry'),
                geometry_evidence=ref('pistol_measured', f'/configurations/{index}'),
                role_review=ref('pistol_roles', f'/configurations/{index}'), reviewer=REVIEWER,
                roles_reviewed=reviewed_role, policy_reviewed=True, interaction_policy=pol, enabled=False)
            (annotations if reviewed_role else pending).append(annotation)
    measured = dict(schema='fvr.bc2.pistol_batch37_measurements.v1', evidence_level='offline_authored_data',
        sources=pins, preparation_script_sha256=sha(Path(__file__).read_bytes()),
        linear_helper_sha256=helper_hash, geometry_groups=groups, configurations=configurations,
        named_resource_sampler=bool(pool), raw_assets_written=False, runtime_admission=False,
        limits=['Shared helper analytic rails bound only the decoded polynomial interpolator.',
                'Transient contact is not a stable controller grasp, native stop, lock, chamber or firing proof.',
                'Only numeric geometry is content-deduplicated; exact configurations retain separate identities.'])
    pins['pistol_measured'] = write('measured.json', measured)
    pins['pistol_roles'] = write('role-policy.json', dict(schema='fvr.bc2.pistol_batch37_role_policy_reviews.v1',
        configurations=reviews, runtime_admission=False, physical_reviewed=False))
    write('reviewed-annotations.json', dict(schema='fvr.bc2.mechanism_registry_annotations.v1',
        sources=pins, profiles=annotations))
    write('pending-annotations.json', dict(schema='fvr.bc2.mechanism_registry_annotations.v1',
        sources=pins, profiles=pending))
    print(json.dumps(dict(configurations=len(configurations), geometry_groups=len(groups),
        complete_geometry_rows=sum(not c['missing_geometry_fields'] for c in configurations),
        ready_annotation_rows=len(annotations), enabled_rows=0, named_resource_sampler=bool(pool),
        blockers={r['asset']: r['blockers'] for r in reviews if r['blockers']})))


if __name__ == '__main__':
    main()
