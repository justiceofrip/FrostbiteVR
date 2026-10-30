"""Bounded exact MP443 authored slide measurement; assets stay in memory."""
from pathlib import Path
import argparse, json, math, sys
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / 'tools'))
from inspect_bc2_mesh_asset import Archive, sha, bone_hash
from bc2_weapon_config_pipeline import parse, named, scalar, Resolver
from bc2_weapon_animation_pipeline import Skeleton, Clip, canonical, matrix, inverse_rigid, multiply
from bc2_mesh_geometry import metadata, geometry
import bc2_authored_magazine_geometry as mag
import bc2_authored_mechanism_geometry as mech
import bc2_authored_linear_mechanism as linear

GAME = Path(r'G:\New folder (2)\Battlefield Bad Company 2')
PREP_PATH = HERE.parent / '20261029-work12/preparation02.json'
PROFILE_PATH = ROOT / 'profiles/resource-enrollment295-pistol/reference-bindings.json'
PREP = json.loads(PREP_PATH.read_text(encoding='utf-8'))
PROFILE, = json.loads(PROFILE_PATH.read_text(encoding='utf-8'))['profiles']
MESH = PROFILE['configured_mesh_path'] + '.res'
LOD = MESH[:-4] + '_lod0_data.res'
SKEL = PROFILE['animation_chain']['skeleton_resource']
CLIP = 'Animations/Weapons/Handheld/RU_hg_MP443Grach/1P/1P_Reload.res'
COMPARE = PROFILE['animation_chain']['animation_resource']
SLIDE = 'jntWpn_3'

def archives():
    a = Archive(GAME / PREP['archive'])
    b = Archive(GAME / PROFILE['animation_source']['archive'])
    assert a.index_sha256 == PREP['archive_index_sha256']
    assert b.index_sha256 == PROFILE['animation_source']['archive_index_sha256']
    return a,b

def selected(a,b,names):
    result = {}; origins = {}
    for source in (a,b):
        present={e.name for e in source.entries if e.flags==65536}
        wanted=set(names)&present-result.keys()
        result.update(source.read_selected(wanted) if wanted else {})
        origins.update({n:str(source.path.relative_to(GAME)) for n in wanted})
    assert set(result)==set(names), sorted(set(names)-result.keys())
    return result,origins

def inspect():
    a,b=archives()
    records=[PROFILE['weapon'],PROFILE['animation_chain']['tree']]
    raw,origins=selected(a,b,[r['resource'] for r in records])
    for record in records:
        assert sha(raw[record['resource']])==record['sha256']
        doc=parse(record['resource'],raw[record['resource']])
        def walk(n,path):
            label=n.attributes.get('name') or n.tag
            p=path+'/'+label
            if n.attributes.get('ref'):
                print(json.dumps(dict(resource=record['resource'],path=p,reference=n.attributes['ref'])))
            for child in n.children:walk(child,p)
        walk(doc.instances[record['instance_guid']], '')
    print(json.dumps(dict(selected_resource_presence={n:[str(s.path.relative_to(GAME)) for s in (a,b) if any(e.name==n and e.flags==65536 for e in s.entries)] for n in (MESH,LOD,SKEL,CLIP,COMPARE)})))

def load():
    a,b=archives()
    chain=PROFILE['animation_chain'];mesh,=PROFILE['meshes']
    records=[PROFILE['weapon'],chain['tree'],chain['animation_asset'],chain['skeleton_asset']]
    names=[r['resource'] for r in records]+[mesh['mesh_document'],CLIP[:-4]+'.dbx',MESH,LOD,SKEL,CLIP,COMPARE]
    raw,origins=selected(a,b,names)
    for r in records:assert sha(raw[r['resource']])==r['sha256']
    assert PROFILE['weapon']['sha256']==PREP['authored_identity']['weapon']['sha256']
    assert sha(raw[mesh['mesh_document']])==mesh['mesh_document_sha256']
    assert sha(raw[MESH])==mesh['geometry']['mesh_sha256']
    assert sha(raw[LOD])==mesh['geometry']['lods'][0]['data_sha256']
    assert sha(raw[SKEL])==PROFILE['skeleton_sha256']
    assert sha(raw[COMPARE])==PROFILE['animation_sha256']
    assert sha(raw[CLIP])=='db48a882545cb352a0178a10e6838bc37d461c717c21b97d928b0b10040b2232'
    docs={n:parse(n,v) for n,v in raw.items() if n.endswith('.dbx')};resolver=Resolver(docs.values())
    wd=docs[PROFILE['weapon']['resource']];weapon=wd.instances[PROFILE['weapon']['instance_guid']]
    state,=named(weapon,'WeaponStates').children
    md,mi=resolver.resolve(wd,named(state,'Meshes1p').children[0].attributes['ref'],'Render.SkinnedMeshAsset')
    assert md.resource==mesh['mesh_document'] and scalar(named(mi,'Name'),'string')+'.res'==MESH
    td,tree=resolver.resolve(wd,named(state,'AnimTree1p').attributes['ref'],'Animation.SpecificAnimTreeData')
    sd,skeleton=resolver.resolve(td,named(tree,'Skeleton').attributes['ref'],'Animation.SkeletonAsset')
    assert scalar(named(skeleton,'Name'),'string')+'.res'==SKEL
    replacements=named(tree,'ReplacmentAnimations').children
    selections=[]
    for resource in (CLIP,COMPARE):
        doc=docs[resource[:-4]+'.dbx'];instance=doc.instances[doc.root.attributes['primaryInstance'].lower()]
        assert scalar(named(instance,'Name'),'string')+'.res'==resource
        cd,cs=resolver.resolve(doc,named(instance,'Skeleton').attributes['ref'],'Animation.SkeletonAsset')
        assert cd.resource==sd.resource and cs.attributes['guid']==skeleton.attributes['guid']
        matches=[(i,n.attributes['ref']) for i,n in enumerate(replacements) if n.attributes['ref'].lower().endswith('/'+instance.attributes['guid'].lower())]
        (index,reference),=matches
        result_slots=[]
        for result_ref in named(tree,'ResultAnimTrees').children:
            result=td.instances[result_ref.attributes['ref'].lower()]
            result_slots += [i for i,n in enumerate(named(result,'AnimationList').children) if n.attributes['ref'].lower()==reference.lower()]
        selections.append(dict(resource=resource,asset_sha256=doc.sha256,asset_guid=instance.attributes['guid'].lower(),replacement_index=index,result_animation_list_slots=result_slots))
    sk=Skeleton(raw[SKEL]);clip=Clip(raw[CLIP]);compare=Clip(raw[COMPARE])
    assert sk.fingerprint==PROFILE['rig_fingerprint'] and not clip.unsupported and not compare.unsupported
    lod,=metadata(raw[MESH]);skin=geometry(raw[LOD],lod,{bone_hash(n):n for n in sk.names})
    assert all(s['multi_weight_vertices']==0 for s in skin)
    parts=mag.rigid_parts(raw[LOD],lod,sk,strict_assembly_skin=True)
    assert all(p['mixed_triangles']==0 for p in parts.values()) and len(parts)==8
    return dict(a=a,b=b,raw=raw,origins=origins,selections=selections,sk=sk,clip=clip,compare=compare,lod=lod,skin=skin,parts=parts)

def main():
    data=load();sk,clip,compare,parts=data['sk'],data['clip'],data['compare'],data['parts']
    times=[min(i/30,clip.duration) for i in range(math.ceil(clip.duration*30)+1)]
    names=sorted(set(parts)|{'LeftHand',*mag.FINGERS})
    samples=[clip.evaluate(sk,t,names) for t in times]
    rows=[]
    for name,part in parts.items():
        local=[canonical(matrix(*(c.evaluate(t) for c in clip.tracks[name]))) for t in times]
        motion=mech.classify(local)
        rows.append(dict(bone=name,index=sk.names.index(name),parent=sk.names[sk.parents[sk.names.index(name)]],
            bounds_part_m=[part['minimum'],part['maximum']],extent_m=part['extent'],
            visible_triangles=sum(s['triangles'] for s in part['sections'] if not s['name'].endswith('_ZOnly')),
            depth_triangles=sum(s['triangles'] for s in part['sections'] if s['name'].endswith('_ZOnly')),
            mixed_triangles=part['mixed_triangles'],motion=motion,peak_time_s=times[motion['peak_sample']]))
    rail=linear.measure(clip,compare,sk,SLIDE)
    part=parts[SLIDE];contact=mech.paired_contacts(samples,SLIDE,part)
    static=rail['reference_part'];axis=rail['axis'];contacts=[]
    for sample in samples:
        rel=sample['weapon_relative'];inv=inverse_rigid(rel[SLIDE])
        center=[sum(mag.point(rel['LeftHand'+d+'1'][12:15],inv)[k] for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
        vertex=min(part['points'],key=lambda q:math.dist(q,center))
        distance=math.hypot(*(max(part['minimum'][k]-center[k],0.,center[k]-part['maximum'][k]) for k in range(3)))
        contacts.append(dict(time_s=sample['time_seconds'],knuckle_bounds_distance_m=distance,
            knuckle_centroid_part_m=center,nearest_mesh_vertex_part_m=vertex,knuckle_to_mesh_vertex_m=math.dist(vertex,center),
            part_from_wrist=multiply(rel['LeftHand'],inv),displacement_m=sum((rel[SLIDE][12+k]-static[12+k])*axis[k] for k in range(3))))
    nearest=min(contacts,key=lambda r:(r['knuckle_to_mesh_vertex_m'],r['time_s']))
    moving=[r for r in contacts if r['displacement_m']>.01]
    nearest_moving=min(moving,key=lambda r:(r['knuckle_to_mesh_vertex_m'],r['time_s']))
    selected=contact['pose']
    if selected:selected={k:v for k,v in selected.items() if k!='wrist_from_fingers'}
    track=clip.tracks[SLIDE]
    result=dict(schema='fvr.bc2.exact_mp443_slide_geometry.v1',evidence_level='offline_installed_asset_measurement',
        weapon=PREP['authored_identity']['weapon'],asset_name='MP443_sp',configured_mesh_path=PROFILE['configured_mesh_path'],
        source_preparation_sha256=sha(PREP_PATH.read_bytes()),profile_sha256=sha(PROFILE_PATH.read_bytes()),binding_digest=PROFILE['binding_digest'],
        archive_indices={str(s.path.relative_to(GAME)):s.index_sha256 for s in (data['a'],data['b'])},
        resources={n:dict(sha256=sha(v),archive=data['origins'][n]) for n,v in data['raw'].items()},
        exact_clip_correspondence=data['selections'],case_boundary_verified=False,
        case_limit='One configured reload replacement; tactical/empty/owed-chamber native case selection is not established.',
        rig_fingerprint=sk.fingerprint,inverse_bind_max_error=sk.inverse_bind_max_error,
        coordinate_convention='canonical reflected-Z row-vector metres; weapon frame jntWpn_1',
        duration_s=clip.duration,sample_hz=30,sample_count=len(samples),decoded_clip_count=2,parts=rows,rail=rail,
        rail_geometry=dict(pivot_kind='translation_origin; no rotational hinge',reference_origin_weapon_m=static[12:15],
            signed_decoder_range_m=[rail['decoded_travel']['minimum_m'],rail['decoded_travel']['maximum_m']],
            total_decoder_span_m=rail['decoded_travel']['maximum_m']-rail['decoded_travel']['minimum_m'],
            position_control_count=len(track[0].controls),orientation_controls_identical=len(set(track[1].controls))==1),
        contacts=dict(stable_window_count=contact['windows'],stable_pose=selected,nearest_sample=nearest,
            nearest_retracted_sample=nearest_moving,
            motion_window_samples=[r for r in contacts if 2.7<=r['time_s']<=3.4]),
        semantic_role='unreviewed_slider_candidate',native_admission=False,runtime_enabled=False,chamber_knowledge='Unknown',
        limits=['Authored rail has a signed forward overshoot; static comparison is not the minimum endpoint.',
                'Skin and spline evidence do not prove native closed, chamber, slide lock, fire readiness or acknowledgement.',
                'Left-hand landmarks and nearest vertex are geometric observations, not collision, reach or controller-grip tolerances.',
                'Semantic slide role requires root inspection of the bounded render.'],
        reader_hashes={n:sha((ROOT/'tools'/n).read_bytes()) for n in ('inspect_bc2_mesh_asset.py','bc2_weapon_config_pipeline.py',
            'bc2_weapon_animation_pipeline.py','bc2_granny_resource.py','bc2_granny_curves.py','bc2_mesh_geometry.py',
            'bc2_authored_magazine_geometry.py','bc2_authored_mechanism_geometry.py','bc2_authored_linear_mechanism.py')})
    output=HERE/'mp443-geometry.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps(dict(output=str(output),bytes=output.stat().st_size,stable_windows=contact['windows'],nearest=nearest,nearest_retracted=nearest_moving)))

def contact_supplement():
    data=load();sk,clip,part=data['sk'],data['clip'],data['parts'][SLIDE]
    rows=[]
    for time in (2.9,3.,3.092190252495596,3.1333333333333333,3.2):
        rel=clip.evaluate(sk,time,[SLIDE,'LeftHand',*mag.FINGERS])['weapon_relative'];inv=inverse_rigid(rel[SLIDE]);fingers=[]
        for name in mag.FINGERS:
            point=mag.point(rel[name][12:15],inv);vertex=min(part['points'],key=lambda p:math.dist(p,point))
            fingers.append(dict(joint=name,distance_m=math.dist(point,vertex),joint_part_m=point,vertex_part_m=vertex))
        rows.append(dict(time_s=time,part_from_wrist=multiply(rel['LeftHand'],inv),nearest_four_joints=sorted(fingers,key=lambda r:r['distance_m'])[:4]))
    result=dict(schema='fvr.bc2.mp443_slide_role_review.v1',geometry_sha256=sha((HERE/'mp443-geometry.json').read_bytes()),
        measurement_script_sha256=sha(Path(__file__).read_bytes()),semantic_review_status='awaiting_root_render_inspection',
        bone=SLIDE,authored_finger_landmarks=rows,stable_grasp_verified=False,
        contact_limit='Joint-to-nearest-vertex distance is a geometric landmark measurement, not skin collision or authored controller tolerance.',
        native_slide_role_verified=False,native_chamber_verified=False,runtime_admission=False)
    with (HERE/'mp443-role-review.json').open('x',encoding='utf-8') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
    print(json.dumps(dict(output=str(HERE/'mp443-role-review.json'),samples=len(rows))))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);group=ap.add_mutually_exclusive_group()
    group.add_argument('--inspect',action='store_true');group.add_argument('--contact-supplement',action='store_true');args=ap.parse_args()
    if args.inspect:inspect()
    elif args.contact_supplement:contact_supplement()
    else:main()
