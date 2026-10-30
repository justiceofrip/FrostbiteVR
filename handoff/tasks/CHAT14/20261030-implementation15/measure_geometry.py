"""Exact M60_sp geometry measurements; reads two pinned archives, exports facts only."""
from pathlib import Path
import hashlib, json, math, sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_bc2_mesh_asset import Archive, sha, bone_hash
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical, matrix, inverse_rigid, multiply
from bc2_mesh_geometry import metadata, geometry
import bc2_authored_magazine_geometry as mag
import bc2_authored_mechanism_geometry as mech
from bc2_authored_linear_mechanism import projection_extrema

GAME = Path(r'G:\New folder (2)\Battlefield Bad Company 2')
OLD = HERE.parent / '20261029-work12'
PROOF = json.loads((OLD / 'proof.json').read_text(encoding='utf-8'))
JOBS = json.loads((OLD / 'jobs.json').read_text(encoding='utf-8'))
MESH = JOBS['models'][0]['mesh_resource']
LOD = MESH[:-4] + '_lod0_data.res'
SKEL = 'Characters/Skeletons/ske01.res'
CLIP = PROOF['authored_reload_animation']['resource']


def profile():
    data = json.loads((ROOT / PROOF['profile_source']).read_text(encoding='utf-8'))
    def walk(v):
        if isinstance(v, dict):
            if v.get('native_asset_name') == 'M60_sp' and v.get('source_weapons'):
                yield v
            else:
                for x in v.values(): yield from walk(x)
        elif isinstance(v, list):
            for x in v: yield from walk(x)
    p, = walk(data)
    assert p['binding_digest'] == PROOF['profile_binding_digest']
    assert p['configured_mesh_path'] + '.res' == MESH
    assert p['animation_chain']['skeleton_resource'] == SKEL
    return p


def vround(v):
    if isinstance(v, float): return round(v, 10)
    if isinstance(v, list): return [vround(x) for x in v]
    if isinstance(v, tuple): return [vround(x) for x in v]
    if isinstance(v, dict): return {k:vround(x) for k,x in v.items()}
    return v


def main():
    p = profile()
    a = Archive(GAME / PROOF['archive'])
    b = Archive(GAME / PROOF['authored_reload_animation']['archive'])
    assert a.index_sha256 == PROOF['archive_index_sha256']
    assert b.index_sha256 == PROOF['authored_reload_animation']['archive_index_sha256']
    sr = a.read_selected([SKEL])[SKEL]
    raw = b.read_selected([MESH, LOD, CLIP])
    assert sha(sr) == p['skeleton_sha256']
    assert sha(raw[CLIP]) == PROOF['authored_reload_animation']['sha256']
    sk, clip = Skeleton(sr), Clip(raw[CLIP])
    assert sk.fingerprint == p['rig_fingerprint']
    assert len(clip.tracks) == 72 and not clip.unsupported
    names = [n for n in clip.tracks if n.startswith('jntWpn')]
    assert len(names) == 21
    parents = {n:sk.names[sk.parents[sk.names.index(n)]] for n in names}
    lod, = metadata(raw[MESH])
    skin = geometry(raw[LOD], lod, {bone_hash(n):n for n in sk.names})
    assert all(s['multi_weight_vertices'] == 0 for s in skin)
    parts = mag.rigid_parts(raw[LOD], lod, sk, strict_assembly_skin=True)
    assert len(parts) == 14 and all(v['mixed_triangles'] == 0 for v in parts.values())
    materials = {s['name']:lod['materials'][0][i] for i,s in enumerate(skin)}
    allnames = sorted(set(names + ['LeftHand', *mag.FINGERS]))
    times = [min(i/30, clip.duration) for i in range(math.ceil(clip.duration*30)+1)]
    samples = [clip.evaluate(sk,t,allnames) for t in times]
    rows = []
    for name in names:
        part = parts.get(name)
        local = [canonical(matrix(*(c.evaluate(t) for c in clip.tracks[name]))) for t in times]
        motion = mech.classify(local)
        row = dict(bone=name, index=sk.names.index(name), parent=parents[name],
                   mesh_weighted=part is not None, semantic_role=None, motion=motion,
                   peak_time_s=times[motion['peak_sample']],
                   baseline_weapon_from_part=samples[0]['weapon_relative'][name])
        if part:
            row.update(bounds_part_m=[part['minimum'],part['maximum']], extent_m=part['extent'],
                visible_triangles=sum(s['triangles'] for s in part['sections'] if not s['name'].endswith('_ZOnly')),
                depth_triangles=sum(s['triangles'] for s in part['sections'] if s['name'].endswith('_ZOnly')),
                mixed_triangles=part['mixed_triangles'],
                materials=sorted({materials[s['name']] for s in part['sections'] if not s['name'].endswith('_ZOnly')}))
            if motion['kind'] != 'static':
                contact = mech.paired_contacts(samples,name,part)
                selected = contact['pose']
                row['stable_left_contact'] = None
                if selected:
                    wrist = selected['part_from_wrist']
                    center = [sum(mag.point(selected['wrist_from_fingers']['LeftHand'+d+'1'][12:15],wrist)[k]
                                  for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
                    nearest = min(part['points'],key=lambda q:math.dist(q,center))
                    row['stable_left_contact'] = {
                        k:selected[k] for k in ('begin_s','end_s','time_s','part_from_wrist',
                            'max_relative_translation_m','max_relative_angle_rad',
                            'max_finger_angle_rad','knuckle_bounds_distance_m')}
                    row['stable_left_contact'].update(qualifying_windows=contact['windows'],
                        knuckle_centroid_part_m=center,nearest_mesh_vertex_part_m=nearest,
                        knuckle_to_mesh_vertex_m=math.dist(center,nearest))
                closest=[]
                for s in samples:
                    rel=s['weapon_relative'];inv=inverse_rigid(rel[name])
                    center=[sum(mag.point(rel['LeftHand'+d+'1'][12:15],inv)[k]
                                for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
                    lo,hi=part['minimum'],part['maximum']
                    distance=math.hypot(*(max(lo[k]-center[k],0.,center[k]-hi[k]) for k in range(3)))
                    closest.append((distance,s['time_seconds'],center,multiply(rel['LeftHand'],inv)))
                distance,time,center,wrist=min(closest,key=lambda q:q[:2])
                nearest=min(part['points'],key=lambda q:math.dist(q,center))
                row['closest_left_contact_sample']=dict(time_s=time,knuckle_bounds_distance_m=distance,
                    knuckle_centroid_part_m=center,nearest_mesh_vertex_part_m=nearest,
                    knuckle_to_mesh_vertex_m=math.dist(center,nearest),part_from_wrist=wrist,
                    sustained_grasp_verified=False)
        if name=='jntWpn_4':
            curve=clip.tracks[name][0];origin=curve.evaluate(0)
            ext=projection_extrema(curve,(0.,0.,-1.),origin,clip.duration)
            row['rail']=dict(baseline_origin_weapon_m=[origin[0],origin[1],-origin[2]],
                axis_weapon=[0,0,1],relative_to_clip_t0=True,decoded_extrema=ext,
                travel_m=ext['maximum_m']-ext['minimum_m'],
                transverse_control_hull_m=max(math.hypot(v[0]-origin[0],v[1]-origin[1]) for v in curve.controls),
                orientation_controls_identical=len(set(clip.tracks[name][1].controls))==1)
        if name=='jntWpn_9':
            q=clip.tracks[name][1];angles=[-2*math.atan2(c[0],c[3]) for c in q.controls]
            sampled=[-2*math.atan2(q.evaluate(t)[0],q.evaluate(t)[3]) for t in times]
            row['hinge']=dict(pivot_weapon_m=local[0][12:15],axis_weapon=[-1,0,0],
                positive_open_sampled_rad=[-max(sampled),-min(sampled)],
                positive_open_control_hull_rad=[-max(angles),-min(angles)],
                position_controls_identical=len(set(clip.tracks[name][0].controls))==1,
                max_off_axis_quaternion_component=max(abs(c[k]) for c in q.controls for k in (1,2)),
                angular_samples=[dict(time_s=t,angle_rad=-sampled[i]) for i,t in enumerate(times)
                                 if i%15==0 or i==len(times)-1])
        rows.append(row)
    result=dict(schema='fvr.bc2.exact_m60_geometry.v1',evidence_level='offline_installed_asset_measurement',
        source_proof_sha256=sha((OLD/'proof.json').read_bytes()),source_jobs_sha256=sha((OLD/'jobs.json').read_bytes()),
        selector=JOBS['models'][0]['definitions'][0]['selector'],binding_digest=p['binding_digest'],
        archive_indices={PROOF['archive']:a.index_sha256,PROOF['authored_reload_animation']['archive']:b.index_sha256},
        resources={SKEL:sha(sr),**{n:sha(v) for n,v in raw.items()}},rig_fingerprint=sk.fingerprint,
        inverse_bind_max_error=sk.inverse_bind_max_error,coordinate_convention='canonical reflected-Z row-vector metres; weapon frame jntWpn_1',
        duration_s=clip.duration,sample_hz=30,sample_count=len(samples),weapon_track_count=len(names),
        weighted_parts=len(parts),native_admission=False,runtime_enabled=False,
        runtime_interpolation_verified=False,roles={k:'unsupported' for k in ('cover','latch','container','feed','charge')},
        limits=['Numeric bone names do not name semantic mechanisms; materials name presentation surfaces only.',
                'Clip t0 is a baseline, not native closed/attached/ready authority.',
                'Hinge ranges and contact windows sample the offline decoder; slider extrema are analytic decoder extrema.',
                'Contact uses knuckle centroid, AABB proximity and nearest mesh vertex; no controller grip or reach tolerance is authored.',
                'No sampled animation timestamp is a native stage, command, acknowledgement or gameplay permission.'],
        reader_hashes={n:sha((ROOT/'tools'/n).read_bytes()) for n in (
            'inspect_bc2_mesh_asset.py','bc2_weapon_animation_pipeline.py','bc2_granny_resource.py',
            'bc2_granny_curves.py','bc2_mesh_geometry.py','bc2_authored_magazine_geometry.py',
            'bc2_authored_mechanism_geometry.py','bc2_authored_linear_mechanism.py')},parts=rows)
    output=HERE/'geometry-evidence.json'
    with output.open('x',encoding='utf-8') as f: json.dump(vround(result),f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps({'passed':True,'output':str(output),'bytes':output.stat().st_size,
        'weighted_parts':len(parts),'no_weight_tracks':[r['bone'] for r in rows if not r['mesh_weighted']]}))


if __name__=='__main__':main()
