"""Explicit experimental path from the exact authored initial magazine withdrawal.

All matrices are canonical row-vector item-local to weapon-local, in metres.
The source is sampled seated-to-withdrawn; runtime consumes its exact reverse.
No fitted rail, receiver-nearest-end inference, pose offset or native admission.
"""
from __future__ import annotations
import math
from bc2_authored_grip_bindings import matrix,digest,checked_hash
from bc2_weapon_animation_pipeline import inverse_rigid,identity

LIMIT=32
CRITERIA={'sample_hz':60,'maximum_clip_seconds':10.,'withdrawal_distance_m':.1,
          'maximum_endpoint_m':.14,'maximum_arc_m':.5,'seated_prefix_m':.004,'minimum_segment_m':.0001,
          'maximum_segment_m':.07,'baseline_distance_m':.001,'baseline_angle_rad':.01,
          'maximum_rotation_from_seat_rad':1.,'maximum_step_angle_rad':math.pi/3,
          'maximum_points':LIMIT}

def distance(a,b):return math.dist(a[12:15],b[12:15])
def angle(a,b):
    trace=sum(a[4*i+j]*b[4*i+j] for i in range(3) for j in range(3))
    return math.acos(max(-1.,min(1.,(trace-1)/2)))
def rigid(a):
    a=matrix(a);inverse_rigid(a);return a

def shape(points,arcs):
    if not 2<=len(points)<=LIMIT or len(arcs)!=len(points) or arcs[0]!=0:raise ValueError('Sampled path point bound')
    if not all(math.isfinite(x) for x in arcs):raise ValueError('Sampled path nonfinite arc')
    points=[rigid(p) for p in points];seat=points[-1]
    if arcs[-1]>.5:raise ValueError('Sampled path arc bound')
    for n,p in enumerate(points):
        if math.dist(p[12:15],[0,0,0])>1.5:raise ValueError('Sampled path weapon-space bound')
        if not n:continue
        prior=points[n-1];length=distance(prior,p)
        if not .0001<=length<=.25 or abs(arcs[n]-arcs[n-1]-length)>1e-5:raise ValueError('Sampled path segment/arc mismatch')
        if angle(prior,p)>CRITERIA['maximum_step_angle_rad']:raise ValueError('Sampled path rotation step')
        if sum((seat[k]-p[k])*(p[k]-prior[k]) for k in range(12,15)) < -1e-9:raise ValueError('Sampled path withdrawal reverses or loops')
        if any(distance(q,p)<.0001 for q in points[:n]):raise ValueError('Sampled path repeated point')
    return points

def from_samples(samples,attached,source):
    """Keep exact source poses outside the existing 4mm seated volume.

    The complete prefix remains evidence, including sub-seat anticipation.
    Only translational repeats below the path resolution are omitted later.

    The exact static authored closed transform is the final runtime endpoint.
    Its independently measured reload-frame-zero agreement is mandatory.
    """
    attached=rigid(attached)
    if not 2<=len(samples)<=601:raise ValueError('Sampled withdrawal input bound')
    baseline=rigid(samples[0]['item'])
    if samples[0]['time']!=0 or distance(baseline,attached)>CRITERIA['baseline_distance_m'] or angle(baseline,attached)>CRITERIA['baseline_angle_rad']:
        raise ValueError('Sampled withdrawal exact seated baseline differs')
    selected=[{'time_seconds':0.,'weapon_from_item':attached,'source':'exact_static_closed'}]
    prefix=[];reached=False;left_seat=False
    for n,s in enumerate(samples):
        if abs(s['time']-n/60)>1e-8:raise ValueError('Sampled withdrawal cadence differs')
        item=rigid(s['item']);prefix.append({'time_seconds':s['time'],'weapon_from_item':item})
        offset=distance(item,attached)
        if angle(item,attached)>CRITERIA['maximum_rotation_from_seat_rad']:raise ValueError('Sampled withdrawal rotation bound')
        if not left_seat and offset<=CRITERIA['seated_prefix_m'] and angle(item,attached)<=CRITERIA['baseline_angle_rad']:
            continue
        left_seat=True
        step=distance(item,selected[-1]['weapon_from_item'])
        if step<CRITERIA['minimum_segment_m']:
            if angle(item,selected[-1]['weapon_from_item'])>CRITERIA['baseline_angle_rad']:
                raise ValueError('Sampled withdrawal has unresolved pure rotation')
            continue
        if step>CRITERIA['maximum_segment_m']:raise ValueError('Sampled withdrawal discontinuity')
        if len(selected)>=LIMIT:raise ValueError('Sampled withdrawal exceeds bounded path capacity')
        selected.append({'time_seconds':s['time'],'weapon_from_item':item,'source':'exact_reload_frame'})
        if offset>=CRITERIA['withdrawal_distance_m']:
            if offset>CRITERIA['maximum_endpoint_m']:raise ValueError('Sampled withdrawal endpoint bound')
            reached=True;break
    if not reached:raise ValueError('Sampled withdrawal insufficient measured travel')
    points=[s['weapon_from_item'] for s in reversed(selected)];arcs=[0.]
    for a,b in zip(points,points[1:]):arcs.append(arcs[-1]+distance(a,b))
    shape(points,arcs)
    receipt={'schema':'fvr.bc2.sampled-magazine-path.v1','criteria':dict(CRITERIA),'source':dict(source),
        'convention':'row_vector_item_local_to_weapon_local_metres','order':'withdrawn_entry_to_exact_seated',
        'closed_item':attached,'source_prefix':prefix,'withdrawal_samples':selected,
        'points':[{'weapon_from_item':pose,'arc_m':arc} for pose,arc in zip(points,arcs)],
        'travel_m':arcs[-1],'native_trajectory_verified':False,'runtime_admitted':False}
    receipt['path_digest']=digest(receipt);return receipt

def derive(clip,skeleton,bone,attached,source):
    if not .2<=clip.duration<=CRITERIA['maximum_clip_seconds']:raise ValueError('Sampled withdrawal clip duration bound')
    samples=[]
    for n in range(math.floor(clip.duration*60)+1):
        t=n/60;samples.append({'time':t,'item':clip.evaluate(skeleton,t,[bone])['weapon_relative'][bone]})
    return from_samples(samples,attached,source)

def validate(receipt,profile):
    if receipt.get('schema')!='fvr.bc2.sampled-magazine-path.v1' or receipt.get('criteria')!=CRITERIA:raise ValueError('Sampled path schema/criteria')
    if receipt.get('path_digest')!=digest({k:v for k,v in receipt.items() if k!='path_digest'}):raise ValueError('Sampled path digest mismatch')
    expected={'clip_sha256':profile['reload_clip']['sha256'],'skeleton_sha256':profile['skeleton_sha256'],
        'rig_fingerprint':profile['rig_fingerprint'],'magazine_bone':profile['bones']['magazine'],
        'static_clip_sha256':profile['static_clip_sha256'],'mesh_sha256':profile['mesh_sha256'],
        'lod_sha256':profile['lod_sha256'],'grip_binding_digest':profile['grip_binding_digest'],
        'weapon':profile['weapon']}
    if receipt.get('source')!=expected:raise ValueError('Sampled path exact source mismatch')
    for k in ('clip_sha256','skeleton_sha256','static_clip_sha256','mesh_sha256','lod_sha256','grip_binding_digest'):checked_hash(expected[k])
    if receipt.get('native_trajectory_verified') is not False or receipt.get('runtime_admitted') is not False:raise ValueError('Sampled path claims runtime/native admission')
    if receipt.get('convention')!='row_vector_item_local_to_weapon_local_metres' or receipt.get('order')!='withdrawn_entry_to_exact_seated':raise ValueError('Sampled path transform convention')
    if receipt['closed_item']!=profile['geometry']['attached_item']:raise ValueError('Sampled path seated endpoint mismatch')
    # Recompute selection from every exact source prefix frame. A rehashed
    # changed path, omitted frame, reverse order or arc cannot bypass validation.
    rebuilt=from_samples([{'time':x['time_seconds'],'item':x['weapon_from_item']} for x in receipt['source_prefix']],receipt['closed_item'],expected)
    if rebuilt!=receipt:raise ValueError('Sampled path differs from exact source prefix')
    return receipt

def proposal(part,attached,paired,path,ux):
    if not paired:raise ValueError('Sampled path requires exact paired authored hand pose')
    if path['criteria']['seated_prefix_m']!=ux['seat_tolerance_m']:raise ValueError('Sampled seated prefix must match existing seat tolerance')
    if path['travel_m']<=max(ux['pull_m'],ux['max_step_m'],ux['post_capture_travel_m']):raise ValueError('Sampled path too short for shared interaction thresholds')
    points=part['points'];lo=[min(p[k] for p in points) for k in range(3)];hi=[max(p[k] for p in points) for k in range(3)]
    return {'attached_item':attached,'item_from_insertion':identity(),'weapon_from_entry':path['points'][0]['weapon_from_item'],
        'item_from_hand':paired['item_from_hand'],'wrist_from_fingers':paired['wrist_from_fingers'],'sampled_path':path,
        'measured':{'closed_part_frame':'exact static authored controls','bone_local_extent_m':[b-a for a,b in zip(lo,hi)],
            'bounds_m':[lo,hi],'part_triangle_count':part['triangles'],'part_sections':part['sections']},
        'design':{'status':'experimental_geometry_based_estimate','carry':'same exact authored reload contact frame',
            'rail':'exact sampled initial withdrawal in reverse; no fitted insertion axis',
            'authored_reload_grasp':True,'paired_reload_grasp':paired['receipt'],'native_insertion_trajectory':False,'ux_defaults':ux}}
