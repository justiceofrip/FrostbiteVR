"""Complete rigid magazine subtrees from exact authored tracks and skin parts.

No native role, chamber state, contact, enrollment, or private prop-cache proof.
All child controls must be constant; samples only corroborate that proof. The
leaf path returns its original object and creates no new profile fields.
"""
from __future__ import annotations
import math
from bc2_weapon_animation_pipeline import inverse_rigid,multiply
from bc2_authored_grip_bindings import digest,checked_hash

LIMIT=8
TRANSLATION_TOLERANCE_M=1e-5
ANGLE_TOLERANCE_RAD=1e-4

def subtree(skeleton,root,weapon='jntWpn_1'):
    names=skeleton.names;parents=skeleton.parents
    if not 1<=len(names)<=1024 or len(names)!=len(parents) or len(set(names))!=len(names):
        raise ValueError('Assembly rig names/count invalid')
    for i in range(len(names)):
        seen=set();at=i
        while at!=-1:
            if type(at) is not int or not 0<=at<len(names) or at in seen:raise ValueError('Assembly rig parent cycle/range')
            seen.add(at);at=parents[at]
    if root not in names or weapon not in names or parents[names.index(root)]!=names.index(weapon):
        raise ValueError('Assembly root is not a direct weapon child')
    result=[];pending=[names.index(root)]
    while pending:
        parent=pending.pop(0)
        children=[i for i,p in enumerate(parents) if p==parent]
        result.extend(children);pending.extend(children)
        if len(result)>LIMIT:raise ValueError('Assembly descendant bound')
    return [names[i] for i in result]

def derive_assembly(parts,skeleton,closed,clip,root,weapon='jntWpn_1'):
    from bc2_authored_magazine_geometry import bounds,point,grasp_separation
    members=subtree(skeleton,root,weapon)
    if not members:return parts[root],None
    if not .2<=clip.duration<=10:raise ValueError('Assembly clip duration bound')
    checked_hash(clip.sha256);checked_hash(skeleton.sha256)
    names=[root,*members];relative=closed['weapon_relative']
    for name in names:
        if closed['bone_evaluation_status'].get(name)!='static_authored_pose':raise ValueError('Assembly closed member is not static: '+name)
        inverse_rigid(relative[name])
        if parts.get(name,{}).get('mixed_triangles',0):raise ValueError('Assembly mixed skin ownership: '+name)
    root_inverse=inverse_rigid(relative[root]);rows=[]
    for name in members:
        curves=clip.tracks.get(name)
        if not curves or len(curves)!=3:raise ValueError('Assembly child tracks absent/unsupported: '+name)
        if not all(c.controls and all(v==c.controls[0] for v in c.controls) for c in curves):
            raise ValueError('Assembly child has varying authored controls: '+name)
        rows.append({'bone':name,'parent':skeleton.names[skeleton.parents[skeleton.names.index(name)]],
                     'item_from_bone':multiply(relative[name],root_inverse),
                     'constant_local_controls':True,'triangles':parts.get(name,{}).get('triangles',0)})
    maxima={n:[0.,0.] for n in members};count=math.ceil(clip.duration*60)
    for i in range(count+1):
        sample=clip.evaluate(skeleton,min(i/60,clip.duration),names)['weapon_relative']
        inverse=inverse_rigid(sample[root])
        for row in rows:
            current=multiply(sample[row['bone']],inverse);inverse_rigid(current)
            distance,angle=grasp_separation(current,row['item_from_bone'])
            error=maxima[row['bone']];error[0]=max(error[0],distance);error[1]=max(error[1],angle)
            if distance>TRANSLATION_TOLERANCE_M or angle>ANGLE_TOLERANCE_RAD:
                raise ValueError('Assembly closed/reload relative transform differs: '+row['bone'])
    points=[];sections=[];triangles=0
    for name,transform in [(root,None),*((r['bone'],r['item_from_bone']) for r in rows)]:
        part=parts.get(name)
        if part:
            points.extend(point(p,transform) if transform else list(p) for p in part['points'])
            sections.extend(dict(s,assembly_bone=name) for s in part['sections']);triangles+=part['triangles']
    lo,hi=bounds(points)
    for row in rows:
        row['maximum_relative_error_m'],row['maximum_relative_error_rad']=maxima[row['bone']]
    receipt={'schema':'fvr.bc2.rigid_magazine_assembly','schema_version':1,'root':root,
             'members':rows,'subtree_complete':True,'sample_count':count+1,
             'rig_fingerprint':skeleton.fingerprint,'skeleton_sha256':skeleton.sha256,'reload_clip_sha256':clip.sha256,
             'translation_tolerance_m':TRANSLATION_TOLERANCE_M,'angle_tolerance_rad':ANGLE_TOLERANCE_RAD,
             'runtime_admitted':False,'native_role_verified':False,'body_prop_cache_verified':False}
    receipt['assembly_digest']=digest(receipt)
    return {'points':points,'sections':sections,'triangles':triangles,'mixed_triangles':0,
            'minimum':lo,'maximum':hi,'extent':[b-a for a,b in zip(lo,hi)]},receipt

def validate_receipt(receipt,profile):
    """Emission checks do not upgrade authored evidence to native authority."""
    from bc2_authored_magazine_geometry import matrix
    if (receipt.get('schema')!='fvr.bc2.rigid_magazine_assembly' or receipt.get('schema_version')!=1 or
        receipt.get('assembly_digest')!=digest({k:v for k,v in receipt.items() if k!='assembly_digest'}) or
        receipt.get('root')!=profile['bones']['magazine'] or receipt.get('subtree_complete') is not True or
        receipt.get('runtime_admitted') is not False or receipt.get('native_role_verified') is not False or
        receipt.get('body_prop_cache_verified') is not False or
        receipt.get('rig_fingerprint')!=profile['rig_fingerprint'] or receipt.get('skeleton_sha256')!=profile['skeleton_sha256'] or
        receipt.get('reload_clip_sha256')!=profile.get('reload_clip',{}).get('sha256') or
        receipt.get('translation_tolerance_m')!=TRANSLATION_TOLERANCE_M or receipt.get('angle_tolerance_rad')!=ANGLE_TOLERANCE_RAD or
        not 13<=receipt.get('sample_count',0)<=601):raise ValueError('Assembly receipt/source coherence differs')
    members=receipt['members']
    if not 1<=len(members)<=LIMIT:raise ValueError('Assembly descendant bound')
    parents={receipt['root']};roles={profile['bones'][k] for k in ('weapon','magazine','wrist')}|set(profile['bones']['fingers'])
    for row in members:
        name=row['bone']
        if (not isinstance(name,str) or not name or '\0' in name or name in roles or row['parent'] not in parents or
            row.get('constant_local_controls') is not True or
            not 0<=row.get('maximum_relative_error_m',math.inf)<=TRANSLATION_TOLERANCE_M or
            not 0<=row.get('maximum_relative_error_rad',math.inf)<=ANGLE_TOLERANCE_RAD):
            raise ValueError('Assembly member invalid')
        transform=matrix(row['item_from_bone']);inverse_rigid(transform)
        if math.hypot(*transform[12:15])>1.5:raise ValueError('Assembly member translation bound')
        roles.add(name);parents.add(name)
