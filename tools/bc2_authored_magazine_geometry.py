"""Experimental shared magazine geometry from exact authored bindings and skins.

No native processes, ammo changes, copied per-weapon contacts or runtime enabling.
Closed transforms/skin ownership are measured; grasp/rail are explicit VR design.
"""
from __future__ import annotations
import argparse,collections,json,math,statistics,struct
from pathlib import Path
from inspect_bc2_mesh_asset import Archive,bone_hash,sha
from bc2_mesh_geometry import metadata,geometry
from bc2_weapon_animation_pipeline import Clip,Skeleton,canonical,identity,inverse_rigid,multiply
from bc2_weapon_config_pipeline import parse,named,scalar,guid
from bc2_weapon_mesh_bindings import reference_resource
from bc2_authored_grip_bindings import digest,path_key,matrix,read_json,checked_hash

FINGERS=[f'LeftHand{digit}{n}' for digit in ('Thumb','Index','Middle','Ring','Pinky') for n in (1,2,3)]
UX={'travel_m':.1,'capture_m':.07,'release_m':.15,'post_capture_travel_m':.035,'seat_tolerance_m':.004,
    'pull_m':.09,'max_step_m':.07,'capture_angle_rad':math.pi/4,'release_angle_rad':math.pi*5/12,
    'max_step_angle_rad':math.pi/3,'alignment_ns':120000000,'seat_dwell_ns':60000000,
    'max_sample_gap_ns':100000000,'max_guided_ns':5000000000,'lower_third_fraction':1/3,'end_band_m':.003}
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def sub(a,b):return [x-y for x,y in zip(a,b)]
def add(a,b):return [x+y for x,y in zip(a,b)]
def scale(a,s):return [x*s for x in a]
def norm(a):return math.sqrt(dot(a,a))
def unit(a):
    length=norm(a)
    if not math.isfinite(length) or length<1e-8:raise ValueError('Degenerate direction')
    return scale(a,1/length)
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def point(p,m):
    matrix(m);r=[sum((*p,1.)[k]*m[k*4+j] for k in range(4)) for j in range(3)]
    if not all(math.isfinite(x) for x in r):raise ValueError('Nonfinite transformed point')
    return r
def frame(x,y,z,origin):
    return matrix([*x,0.,*y,0.,*z,0.,*origin,1.])
def bounds(points):
    if not points or not all(math.isfinite(x) for p in points for x in p):raise ValueError('Invalid part points')
    return [min(p[i] for p in points) for i in range(3)],[max(p[i] for p in points) for i in range(3)]

def rigid_parts(mesh_data,lod,skeleton,strict_assembly_skin=False):
    """Decode already-validated skin triangles into canonical bone-local points."""
    known={bone_hash(n):n for n in skeleton.names}
    if len(known)!=len(skeleton.names):raise ValueError('Rig bone-name hash collision')
    sections=geometry(mesh_data,lod,known)
    if sum(s['vertices'] for s in sections)>100000 or sum(s['triangles'] for s in sections)>200000:raise ValueError('Total geometry extraction bound')
    vb=sum(s['vertices']*s['stride'] for s in sections);ib=sum(s['triangles']*6 for s in sections);start=len(mesh_data)-vb-ib
    inverse={name:canonical(b['InverseWorldTransform']) for name,b in zip(skeleton.names,skeleton.bones)}
    parts={};mixed=collections.Counter()
    for s in sections:
        vertices=[];owners=[];raw=[];influences=[]
        for n in range(s['vertices']):
            at=start+s['vertex_offset']+n*s['stride'];half=s['stride'] in (16,48);posbytes=8 if half else 12
            p=struct.unpack_from('<3e' if half else '<3f',mesh_data,at)
            ids=mesh_data[at+posbytes:at+posbytes+4];weights=mesh_data[at+posbytes+4:at+posbytes+8]
            active=[k for k,w in enumerate(weights) if w]
            influences.append({known[lod['palette'][s['palette'][ids[k]]]] for k in active} if strict_assembly_skin else set())
            owner=None
            if len(active)==1 and weights[active[0]]==255:
                h=lod['palette'][s['palette'][ids[active[0]]]];owner=known.get(h)
            owners.append(owner);raw.append(mesh_data[at:at+posbytes+8])
            # Raw mesh Z is reflected BEFORE the canonical inverse bind.
            vertices.append(point([p[0],p[1],-p[2]],inverse[owner]) if owner else None)
        indices=struct.unpack_from('<'+'H'*(s['triangles']*3),mesh_data,start+vb+s['first_index']*2)
        selected={}
        for n in range(0,len(indices),3):
            tri=indices[n:n+3];names={owners[i] for i in tri}
            if len(names)!=1 or None in names:
                affected=set().union(*(influences[i] for i in tri)) if strict_assembly_skin else names
                for name in affected:
                    if name:mixed[name]+=1
                continue
            name=next(iter(names));selected.setdefault(name,[]).extend(tri)
        for name,indices_for_part in selected.items():
            unique=sorted(set(indices_for_part));entry=parts.setdefault(name,{'points':[],'sections':[],'triangles':0})
            entry['points'].extend(vertices[i] for i in unique);entry['triangles']+=len(indices_for_part)//3
            entry['sections'].append({'name':s['name'],'triangles':len(indices_for_part)//3,'vertices':len(unique),
               'vertex_skin_sha256':sha(b''.join(raw[i] for i in unique)),
               'indices_sha256':sha(struct.pack('<'+'H'*len(indices_for_part),*indices_for_part)),
               'bone_hash':f'{bone_hash(name):08x}'})
    if strict_assembly_skin:
        for name in mixed:parts.setdefault(name,{'points':[],'sections':[],'triangles':0})
    for name,p in parts.items():
        p['mixed_triangles']=mixed[name];p['minimum'],p['maximum']=bounds(p['points']) if p['points'] else ([0.,0.,0.],[0.,0.,0.])
        p['extent']=[b-a for a,b in zip(p['minimum'],p['maximum'])]
    return parts

def role_candidates(parts,skeleton,closed,reload_clip,weapon='jntWpn_1',assemblies=None):
    """Conservative geometric candidates, not semantic/native authority."""
    parents={n:skeleton.names[p] if p>=0 else None for n,p in zip(skeleton.names,skeleton.parents)}
    children=collections.Counter(v for v in parents.values() if v)
    candidates=[];rejected=[]
    for name,part in parts.items():
        reasons=[];extent=sorted(part['extent']);curves=reload_clip.tracks.get(name)
        displacement=0.
        if curves:displacement=norm([max(v[k] for v in curves[0].controls)-min(v[k] for v in curves[0].controls) for k in range(3)])
        if parents.get(name)!=weapon or (children[name] and name not in (assemblies or {})):reasons.append('not a rigid direct leaf of the weapon root')
        if part['mixed_triangles']:reasons.append('cross-part weighted triangles')
        if part['triangles']<24 or not (.015<=extent[0]<=.1 and .06<=extent[-1]<=.4 and extent[-1]>2*extent[0]):reasons.append('outside explicit box-magazine shape candidate bounds')
        if displacement<.04:reasons.append('no substantial authored reload-position control displacement')
        if closed.get('bone_evaluation_status',{}).get(name)!='static_authored_pose':reasons.append('closed authored part transform is not constant')
        row={'bone':name,'parent':parents.get(name),'child_count':children[name],'bone_hash':f'{bone_hash(name):08x}',
             'extent_m':part['extent'],'triangles':part['triangles'],'reload_control_extent_m':displacement,
             'role':'geometric_detachable_magazine_candidate','native_role_verified':False}
        if name in (assemblies or {}):row['rigid_assembly_digest']=assemblies[name]['assembly_digest']
        if reasons:row['reasons']=reasons;rejected.append(row)
        else:candidates.append(row)
    return candidates,rejected

def design(part,attached,body_points,left_in_weapon,fingers_in_weapon,entry_axis=None):
    """Reusable lower-third palm placement and body-facing linear assist rail."""
    matrix(attached);matrix(left_in_weapon)
    if set(fingers_in_weapon)!=set(FINGERS):raise ValueError('Complete static left finger set required')
    lo,hi=bounds(part['points']);extent=sub(hi,lo);long=max(range(3),key=lambda n:extent[n]) if entry_axis is None else entry_axis;short=min(range(3) if entry_axis is None else (n for n in range(3) if n!=long),key=lambda n:extent[n])
    if long==short or extent[long]<.06 or extent[short]<.015:raise ValueError('Degenerate magazine extent')
    tips=[]
    for end in (lo[long],hi[long]):
        band=[p for p in part['points'] if abs(p[long]-end)<=UX['end_band_m']]
        tip=[statistics.median(p[k] for p in band) for k in range(3)];tip[long]=end
        weapon_tip=point(tip,attached)
        if not body_points:raise ValueError('Exact rigid weapon body geometry required')
        distance=min(norm(sub(weapon_tip,b)) for b in body_points)
        tips.append((distance,tip))
    if abs(tips[0][0]-tips[1][0])<.01:raise ValueError('Magazine insertion end is geometrically ambiguous')
    inward=[0.,0.,0.];inward[long]=1. if tips[1][0]<tips[0][0] else -1.
    tip=min(tips,key=lambda t:t[0])[1]
    across=[0.,0.,0.];across[short]=1.
    insertion=frame(across,cross(inward,across),inward,tip)
    entry=multiply(insertion,attached)
    entry[12:15]=sub(entry[12:15],scale(entry[8:11],UX['travel_m']))
    # Use this asset's own authored hand/finger shape. Reorient the PALM frame,
    # not a copied per-weapon wrist offset, to the lower third of the magazine.
    hand_inverse=inverse_rigid(left_in_weapon)
    finger_local={n:multiply(matrix(v),hand_inverse) for n,v in fingers_in_weapon.items()}
    knuckles=[finger_local[f'LeftHand{d}1'][12:15] for d in ('Index','Middle','Ring','Pinky')]
    palm_center=[sum(p[k] for p in knuckles)/4 for k in range(3)]
    palm_x=unit(sub(knuckles[0],knuckles[-1]));palm_y=unit(sub(palm_center,scale(palm_x,dot(palm_center,palm_x))))
    palm_z=unit(cross(palm_x,palm_y));palm_frame=frame(palm_x,palm_y,palm_z,palm_center)
    hand_in_item=multiply(left_in_weapon,inverse_rigid(attached));center=[(a+b)/2 for a,b in zip(lo,hi)]
    side=1. if hand_in_item[12+short]>=center[short] else -1.
    lower_end=lo[long] if inward[long]>0 else hi[long]
    target_center=list(center);target_center[long]=lower_end+inward[long]*extent[long]*UX['lower_third_fraction']
    target_center[short]=(hi[short] if side>0 else lo[short])
    # Stack the fingers along the magazine, with the index toward the feed end.
    # Wrist-to-knuckles runs around its broad side. Aligning that direction to
    # the long axis instead makes the magazine project lengthwise from the fist.
    target_z=scale(across,-side);target_x=inward;target_y=unit(cross(target_z,target_x))
    target_palm=frame(target_x,target_y,target_z,target_center)
    item_from_hand=multiply(inverse_rigid(palm_frame),target_palm)
    return {'attached_item':attached,'item_from_insertion':insertion,'weapon_from_entry':entry,
        'item_from_hand':item_from_hand,'wrist_from_fingers':finger_local,
        'measured':{'closed_part_frame':'exact static authored controls','bone_local_extent_m':extent,'bounds_m':[lo,hi],
                    'part_triangle_count':part['triangles'],'part_sections':part['sections']},
        'design':{'status':'experimental_geometry_based_estimate','carry':'asset-authored finger shape; finger row follows magazine length, index toward feed end; palm at lower-third side',
                  'rail':'long bone-local extent axis; nearer rigid weapon-body end; median3mm end-band landmark',
                  'end_distances_to_body_m':[p[0] for p in tips],'long_axis':long,'short_axis':short,'inward_direction':inward,
                  'authored_reload_grasp':False,'native_insertion_trajectory':False,'ux_defaults':UX}}

PAIRED_GRASP={'sample_hz':60,'window_seconds':.2,'min_item_motion_m':.1,
              'max_wrist_translation_m':.01,'max_wrist_angle_rad':.15,
              'max_finger_angle_rad':.05,'max_contact_distance_m':.05}

def grasp_separation(a,b):
    matrix(a);matrix(b);inverse_rigid(a);inverse_rigid(b)
    distance=norm(sub(a[12:15],b[12:15]))
    trace=sum(a[r*4+c]*b[r*4+c] for r in range(3) for c in range(3))
    return distance,math.acos(max(-1.,min(1.,(trace-1.)/2)))

def paired_reload_grasp(clip,skeleton,magazine,attached,part,diagnostics=None):
    """One authored frame's wrist/item/fingers; no mixed-pose transplant.

    Stable relative motion identifies an experimental contact frame. The full
    relative transform is kept together, so moving the VR wrist preserves the
    authored magazine/finger relation. Runtime interpolation remains unverified.
    """
    if not .2<=clip.duration<=10:raise ValueError('Paired grasp clip duration bound')
    names=['LeftHand',magazine,*FINGERS]
    if len(set(names))!=17:raise ValueError('Distinct wrist, magazine and fingers required')
    parents={n:skeleton.names[p] if p>=0 else None for n,p in zip(skeleton.names,skeleton.parents)}
    for digit in ('Thumb','Index','Middle','Ring','Pinky'):
        for n in (1,2,3):
            if parents.get(f'LeftHand{digit}{n}')!=('LeftHand' if n==1 else f'LeftHand{digit}{n-1}'):
                raise ValueError('Paired grasp finger hierarchy differs')
    lo,hi=bounds(part['points']);samples=[]
    count=math.ceil(clip.duration*PAIRED_GRASP['sample_hz'])
    for n in range(count+1):
        t=min(n/PAIRED_GRASP['sample_hz'],clip.duration)
        pose=clip.evaluate(skeleton,t,names);rel=pose['weapon_relative']
        wrist=matrix(rel['LeftHand']);item=matrix(rel[magazine])
        hand=multiply(wrist,inverse_rigid(item))
        fingers={name:multiply(matrix(rel[name]),inverse_rigid(wrist)) for name in FINGERS}
        for transform in [hand,*fingers.values()]:inverse_rigid(transform)
        center=[sum(point(fingers['LeftHand'+digit+'1'][12:15],hand)[k]
                    for digit in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
        contact_distance=norm([max(lo[k]-center[k],0.,center[k]-hi[k]) for k in range(3)])
        samples.append({'time':t,'hand':hand,'fingers':fingers,'contact_distance':contact_distance,
                        'motion':grasp_separation(item,attached)[0],'item':item})
    width=round(PAIRED_GRASP['window_seconds']*PAIRED_GRASP['sample_hz']);windows=[];coupled=[]
    if diagnostics is not None:diagnostics.update(bone=magazine,criteria=dict(PAIRED_GRASP),sample_count=len(samples),windows=[],runtime_admitted=False)
    for begin in range(len(samples)-width):
        group=samples[begin:begin+width+1];center=group[width//2]
        if group[-1]['time']-group[0]['time']<PAIRED_GRASP['window_seconds']-1e-8:continue
        errors=[grasp_separation(s['hand'],center['hand']) for s in group]
        translation=max(e[0] for e in errors);angle=max(e[1] for e in errors)
        finger_angle=max(grasp_separation(s['fingers'][bone],center['fingers'][bone])[1] for s in group for bone in FINGERS)
        contact=max(s['contact_distance'] for s in group);motion=min(s['motion'] for s in group)
        if diagnostics is not None:
            travel=grasp_separation(group[0]['item'],group[-1]['item'])[0]
            metrics={'wrist_translation_m':translation,'wrist_angle_rad':angle,'finger_angle_rad':finger_angle,'contact_distance_m':contact}
            failed=[key for key,value in metrics.items() if value>=PAIRED_GRASP['max_'+key]]
            if motion<=PAIRED_GRASP['min_item_motion_m'] and travel<=4*PAIRED_GRASP['max_wrist_translation_m']:failed.append('item_motion')
            diagnostics['windows'].append(dict(begin_seconds=group[0]['time'],end_seconds=group[-1]['time'],metrics=metrics,minimum_item_motion_m=motion,co_motion_travel_m=travel,failed=failed))
        if (translation>=PAIRED_GRASP['max_wrist_translation_m'] or
            angle>=PAIRED_GRASP['max_wrist_angle_rad'] or finger_angle>=PAIRED_GRASP['max_finger_angle_rad'] or
            contact>=PAIRED_GRASP['max_contact_distance_m']):continue
        evidence={'begin_seconds':group[0]['time'],'end_seconds':group[-1]['time'],
            'time_seconds':center['time'],'max_wrist_translation_m':translation,'max_wrist_angle_rad':angle,
            'max_finger_angle_rad':finger_angle,'max_knuckle_distance_to_bounds_m':contact,'minimum_item_motion_m':motion}
        candidate=(translation,angle,center['time'],center,evidence)
        if motion>PAIRED_GRASP['min_item_motion_m']:windows.append(candidate)
        else:
            travel=grasp_separation(group[0]['item'],group[-1]['item'])[0]
            if travel>4*PAIRED_GRASP['max_wrist_translation_m']:
                evidence['co_motion_travel_m']=travel;coupled.append(candidate)
    legacy=bool(windows)
    eligible=windows if legacy else coupled
    if diagnostics is not None:
        diagnostics['eligible_windows']=len(eligible)
        diagnostics['failure_counts']=dict(collections.Counter(reason for w in diagnostics['windows'] for reason in w['failed']))
    if not eligible:raise ValueError('No stable authored magazine contact interval')
    best=min(eligible,key=lambda row:row[:3]);center=best[3]
    result={'item_from_hand':center['hand'],'wrist_from_fingers':center['fingers']}
    result['receipt']={'source':'same_frame_authored_reload_contact_candidate','clip_sha256':clip.sha256,
        'skeleton_sha256':skeleton.sha256,'rig_fingerprint':skeleton.fingerprint,'magazine_bone':magazine,
        'sample_count':len(samples),'eligible_windows':len(eligible),'window':best[4],
        'criteria':dict(PAIRED_GRASP),'runtime_interpolation_verified':False,'active_native_animation_verified':False,
        'grasp_digest':digest(result)}
    if not legacy:
        result['receipt']['selection_mode']='stable_relative_co_motion'
        result['receipt']['co_motion_error_multiple']=4
        result['receipt']['entry_probe']=initial_withdrawal(samples,attached,part)
    return result


def _initial_withdrawal_probe(samples,attached,maximum):
    baseline_distance,baseline_angle=grasp_separation(samples[0]['item'],attached)
    if baseline_distance>.01 or baseline_angle>PAIRED_GRASP['max_wrist_angle_rad']:raise ValueError('Authored reload baseline differs from closed part')
    inverse=inverse_rigid(attached);probe=[]
    for sample in samples:
        distance,angle=grasp_separation(sample['item'],attached)
        if distance>.1:break
        if .02<=distance<=maximum and angle<PAIRED_GRASP['max_wrist_angle_rad']:
            position=multiply(sample['item'],inverse)[12:15]
            if probe and sample['time']-probe[-1]['time_seconds']>2/PAIRED_GRASP['sample_hz']+1e-8:break
            probe.append({'time_seconds':sample['time'],'position_in_closed_item':position,'angle_rad':angle,'distance_m':distance})
    if len(probe)<2:raise ValueError('No bounded initial withdrawal witness')
    displacement=sub(probe[-1]['position_in_closed_item'],probe[0]['position_in_closed_item'])
    if norm(displacement)<=2*PAIRED_GRASP['max_wrist_translation_m']:raise ValueError('Insufficient initial withdrawal travel')
    axis=unit(displacement)
    if any(dot(unit(p['position_in_closed_item']),axis)<.99 for p in probe):raise ValueError('Initial withdrawal is not a consistent direction')
    return {'source':'same_exact_authored_reload_before_first100mm_excursion','samples':probe,'outward_in_item':axis,
            'direction_method':'first_to_last_probe_displacement',
            'maximum_probe_m':maximum,'native_trajectory_verified':False}


def initial_withdrawal(samples,attached,part):
    """Bound withdrawal along measured travel, not across magazine thickness.

    Existing successful witnesses retain their exact data. Thin rigid parts may
    need a larger axial probe; this must fit the measured projected part extent
    and still end before the first100mm excursion. Direction, rotation, cadence,
    travel and later independent receiver-near-end checks remain mandatory.
    """
    lo,hi=bounds(part['points']);extent=sub(hi,lo)
    legacy_maximum=min(PAIRED_GRASP['min_item_motion_m'],min(extent))
    try:return _initial_withdrawal_probe(samples,attached,legacy_maximum)
    except ValueError as exc:
        if str(exc) not in ('No bounded initial withdrawal witness','Insufficient initial withdrawal travel'):raise
    candidate=_initial_withdrawal_probe(samples,attached,PAIRED_GRASP['min_item_motion_m'])
    projected=sum(abs(axis)*size for axis,size in zip(candidate['outward_in_item'],extent))
    maximum=min(PAIRED_GRASP['min_item_motion_m'],projected)
    result=_initial_withdrawal_probe(samples,attached,maximum)
    # Recompute projection for the final bounded witness; never retain a wider
    # exploratory direction if shortening the probe changes its support extent.
    actual=sum(abs(axis)*size for axis,size in zip(result['outward_in_item'],extent))
    if any(p['distance_m']>actual+1e-9 for p in result['samples']):raise ValueError('Withdrawal exceeds projected rigid part extent')
    result.update(maximum_probe_basis='projected_rigid_extent_along_measured_withdrawal',
                  legacy_maximum_probe_m=legacy_maximum,projected_extent_m=actual)
    return result


def select_withdrawal_entry_end(ends,outward):
    """Resolve only a receiver-near tie with independently measured direction."""
    if len(ends)!=6 or {(e['axis'],e['sign']) for e in ends}!={(a,s) for a in range(3) for s in (-1,1)}:
        raise ValueError('Complete unique principal endpoint evidence required')
    if any(type(e['distance_to_body_m']) not in (int,float) or not math.isfinite(e['distance_to_body_m']) or e['distance_to_body_m']<0 for e in ends):
        raise ValueError('Invalid receiver endpoint distances')
    ordered=sorted(ends,key=lambda e:e['distance_to_body_m']);best=ordered[0];resolved_tie=False
    if ordered[1]['distance_to_body_m']-best['distance_to_body_m']<.01:
        # A rectangular magazine's side/end corners can be equally close to
        # receiver geometry. Only the independently measured initial stroke
        # chooses among those existing near endpoints; a farther axis cannot win.
        aligned=[e for e in ordered if e['distance_to_body_m']-best['distance_to_body_m']<.01 and -outward[e['axis']]*e['sign']>=.95]
        if len(aligned)!=1:raise ValueError('Receiver-near tie has no unique measured withdrawal axis')
        best=aligned[0];opposite=next(e for e in ordered if e['axis']==best['axis'] and e['sign']==-best['sign'])
        if opposite['distance_to_body_m']-best['distance_to_body_m']<.01:raise ValueError('Measured axis has no unique receiver-near end')
        resolved_tie=True
    agreement=-outward[best['axis']]*best['sign']
    if agreement<.95:raise ValueError('Receiver end disagrees with authored withdrawal')
    return best,agreement,resolved_tie


def entry_axis_from_withdrawal(part,attached,body_points,probe):
    """Unique receiver-near end corroborated by actual authored withdrawal."""
    if not body_points:raise ValueError('Exact rigid weapon body geometry required')
    outward=unit(probe['outward_in_item']);lo,hi=bounds(part['points']);ends=[]
    for axis in range(3):
        for sign,end in ((-1,lo[axis]),(1,hi[axis])):
            band=[p for p in part['points'] if abs(p[axis]-end)<=UX['end_band_m']]
            tip=[statistics.median(p[k] for p in band) for k in range(3)];tip[axis]=end
            distance=min(norm(sub(point(tip,attached),b)) for b in body_points)
            ends.append({'axis':axis,'sign':sign,'distance_to_body_m':distance,'tip_in_item':tip})
    ends.sort(key=lambda e:e['distance_to_body_m']);best,agreement,resolved_tie=select_withdrawal_entry_end(ends,outward)
    return {'axis':best['axis'],'inward_sign':best['sign'],'direction_agreement':agreement,'ends':ends,
            'source':'receiver_near_tie_resolved_by_authored_withdrawal' if resolved_tie else 'unique_body_near_end_and_authored_initial_withdrawal','native_axis_verified':False}


def apply_paired_reload_grasp(geometry,paired):
    # Only the coupled carry pose changes. Closed item and insertion geometry
    # deliberately remain the original independently derived transforms.
    geometry['item_from_hand']=paired['item_from_hand']
    geometry['wrist_from_fingers']=paired['wrist_from_fingers']
    geometry['design']['authored_reload_grasp']=True
    geometry['design']['carry']='same authored reload frame wrist-to-magazine and complete finger hierarchy; experimental paired pose'
    geometry['design']['paired_reload_grasp']=paired['receipt']


def contact_proposal(part,attached,body_points,closed,paired=None):
    """Static support fingers are not a prerequisite for a complete reload pair.

    The paired wrist and all fingers are transferred together into the same
    measured closed item frame for the shared geometry designer. Closed item
    ownership and rail construction remain independent of the grasp source.
    """
    if paired is None:
        if any(closed.get('bone_evaluation_status',{}).get(n)!='static_authored_pose' for n in ['LeftHand',*FINGERS]):
            raise ValueError('Static complete authored finger shape unavailable')
        hand=closed['weapon_relative']['LeftHand']
        fingers={n:closed['weapon_relative'][n] for n in FINGERS}
    else:
        hand=multiply(matrix(paired['item_from_hand']),matrix(attached))
        fingers={n:multiply(matrix(paired['wrist_from_fingers'][n]),hand) for n in FINGERS}
    entry=None
    if paired is not None and paired['receipt'].get('selection_mode')=='stable_relative_co_motion':
        entry=entry_axis_from_withdrawal(part,attached,body_points,paired['receipt']['entry_probe'])
    proposal=design(part,attached,body_points,hand,fingers,entry['axis'] if entry else None)
    if entry:
        proposal['design']['long_axis']=max(range(3),key=lambda n:part['extent'][n])
        proposal['design']['entry_axis']=entry['axis'];proposal['design']['entry_evidence']=entry
        proposal['design']['rail']='receiver-near principal end corroborated by authored initial withdrawal; linear VR assist estimate'
    if paired is not None:apply_paired_reload_grasp(proposal,paired)
    return proposal

def select_paired_role(candidates,parts,closed,clip,skeleton,diagnostics=None):
    """Resolve geometric ambiguity only with unique complete same-frame contact.

    A bolt and a magazine can both satisfy size/motion bounds. No name or bone
    number chooses their role. Every input already passed geometric ownership;
    this additional test never widens those bounds or admits an unpaired part.
    """
    if not 2<=len(candidates)<=128 or len({r['bone'] for r in candidates})!=len(candidates):
        raise ValueError('Paired role candidate bounds/identity')
    qualified=[];rejected=[]
    for role in candidates:
        bone=role['bone']
        detail={} if diagnostics is not None else None
        if detail is not None:diagnostics.append(detail)
        try:paired=paired_reload_grasp(clip,skeleton,bone,closed['weapon_relative'][bone],parts[bone],detail)
        except (ValueError,KeyError) as exc:
            rejected.append({**role,'reasons':['no complete stable paired contact: '+str(exc)]});continue
        qualified.append((role,paired))
    if len(qualified)!=1:raise ValueError('Complete paired magazine role is not unique: '+','.join(r['bone'] for r,_ in qualified))
    role,paired=qualified[0]
    return {**role,'ambiguity_resolution':'unique_complete_stable_paired_contact','geometric_candidate_count':len(candidates)},paired,rejected

def exact_archive(game,relative):
    path_key(relative);path=(game/relative).resolve()
    if not path.is_relative_to(game.resolve()):raise ValueError('Archive escapes game root')
    return Archive(path)
def exact_resource(archive,name,kind,expected=None):
    entries=[e for e in archive.entries if e.flags==65536 and path_key(e.name)==path_key(name)]
    if len(entries)!=1 or entries[0].kind!=kind:raise ValueError('Exact archive resource/type absent or ambiguous: '+name+' expected '+kind+' observed '+str([e.kind for e in entries]))
    data=archive.read_selected([entries[0].name])[entries[0].name]
    if expected and sha(data)!=expected:raise ValueError('Exact resource content changed')
    return data
def bound_reference(reference,receipt):
    if path_key(reference_resource(reference))!=path_key(receipt['resource']) or guid(reference.rsplit('/',1)[1])!=guid(receipt['instance_guid']):
        raise ValueError('Exact bound reference resource/GUID differs')
    return True
def mesh_weapon_matches(weapon,profile):
    return (weapon['native_name']==profile['native_asset_name'] and guid(weapon['instance_guid'])==guid(profile['weapon']['instance_guid']) and
            path_key(weapon['resource'])==path_key(profile['weapon']['resource']) and weapon['resource_sha256']==profile['weapon']['sha256'])

def derive(game,bindings,mesh_bindings,assets,evidence_sink=None,paired_grasp_assets=(),mechanism_sink=None,rigid_assembly_assets=(),reviewed_contacts=()):
    if bindings.get('schema') not in ('fvr.bc2.authored_grip_bindings','fvr.bc2.authored_reload_reference_bindings') or bindings.get('schema_version')!=1:raise ValueError('Grip binding schema')
    if bindings['schema']=='fvr.bc2.authored_reload_reference_bindings' and set(paired_grasp_assets)!=set(assets):raise ValueError('Reload-only references require complete paired grasp extraction')
    if mesh_bindings.get('schema')!='fvr.bc2.authored_weapon_mesh_bindings':raise ValueError('Mesh binding schema')
    if not assets or len(assets)>128 or len(bindings['profiles'])>2048:raise ValueError('Asset selection bound')
    if not set(paired_grasp_assets)<=set(assets):raise ValueError('Paired grasp assets must be explicitly inspected')
    if not set(rigid_assembly_assets)<=set(paired_grasp_assets):raise ValueError('Rigid assembly requires explicitly paired contact assets')
    from bc2_reviewed_magazine_contact import index_reviews,verify_context,select_reviewed_pair
    reviews=index_reviews(reviewed_contacts,set(paired_grasp_assets))
    if not set(reviews)<={p['weapon']['resource'] for p in bindings['profiles'] if p['native_asset_name'] in assets}:
        raise ValueError('Reviewed contact exact configuration absent from selected bindings')
    output=[];gaps=[];assembly_reports=[];reviewed_candidates=[]
    for profile in bindings['profiles']:
        if profile['native_asset_name'] not in assets:continue
        assembly_report=None
        if profile['native_asset_name'] in rigid_assembly_assets:
            assembly_report={'asset':profile['native_asset_name'],'weapon':profile['weapon'],'state_index':profile['state_index'],'assemblies':[],'rejected_assemblies':[],'contacts':[],'runtime_admitted':False}
            assembly_reports.append(assembly_report)
        try:
            if digest({k:v for k,v in profile.items() if k!='binding_digest'})!=profile['binding_digest']:raise ValueError('Grip binding digest mismatch')
            selected=[m for m in profile['meshes'] if m['configured_mesh_path']==profile['configured_mesh_path']]
            if len(selected)!=1:raise ValueError('Exact selected geometry mesh ambiguous')
            mesh=selected[0];variant=mesh['geometry']
            if variant is None:raise ValueError('Mesh geometry unavailable')
            matches=[m for w in mesh_bindings['weapons'] if mesh_weapon_matches(w,profile)
                     for s in w['states'] if s['index']==profile['state_index'] for m in s['meshes'] if m.get('mesh_resource')==mesh['mesh_resource']]
            if len(matches)!=1:raise ValueError('Exact mesh source identity')
            sources=[s for s in matches[0]['geometry_sources'] if s['geometry_status']=='derived']
            if not sources:raise ValueError('No measured mesh source')
            source=min(sources,key=lambda s:s['archive']);archive=exact_archive(game,source['archive'])
            if archive.index_sha256!=source['index_sha256']:raise ValueError('Mesh source archive index changed')
            mesh_raw=exact_resource(archive,mesh['mesh_resource'],'SkinnedMeshSet',variant['mesh_sha256'])
            lods=[l for l in variant['lods'] if l['lod']==0]
            if len(lods)!=1:raise ValueError('Exact LOD0 binding required')
            lod_data=exact_resource(archive,lods[0]['data_resource'],'MeshData',lods[0]['data_sha256'])
            sk_source=profile['skeleton_source'];sk_archive=exact_archive(game,sk_source['archive'])
            if sk_archive.index_sha256!=sk_source['archive_index_sha256']:raise ValueError('Skeleton source index changed')
            skeleton=Skeleton(exact_resource(sk_archive,profile['animation_chain']['skeleton_resource'],'GrannyModel',profile['skeleton_sha256']))
            if skeleton.fingerprint!=profile['rig_fingerprint']:raise ValueError('Rig fingerprint changed')
            parents={n:skeleton.names[p] if p>=0 else None for n,p in zip(skeleton.names,skeleton.parents)}
            for digit in ('Thumb','Index','Middle','Ring','Pinky'):
                for n in (1,2,3):
                    if parents.get(f'LeftHand{digit}{n}')!=('LeftHand' if n==1 else f'LeftHand{digit}{n-1}'):
                        raise ValueError('Authored finger topology differs')
            clip_source=profile['animation_source'];clip_archive=exact_archive(game,clip_source['archive'])
            if clip_archive.index_sha256!=clip_source['archive_index_sha256']:raise ValueError('Static animation source index changed')
            static=Clip(exact_resource(clip_archive,profile['animation_chain']['animation_resource'],'GrannyAnimation',profile['animation_sha256']))
            names=[n for n in skeleton.names if n in static.tracks and n.startswith(('jntWpn','LeftHand'))]
            closed=static.evaluate(skeleton,0.,names)
            tree=profile['animation_chain']['tree'];tree_doc=parse(tree['resource'],exact_resource(sk_archive,tree['resource'],'<non-resource>',tree['sha256']))
            tree_instance=tree_doc.instances[tree['instance_guid']];refs=[]
            for child in named(tree_instance,'ReplacmentAnimations').children:
                ref=child.attributes['ref'];path,identifier=ref.rsplit('/',1)
                refs.append((path+'.dbx',guid(identifier)))
            # Typed graph resolves role from AnimationAsset.Name, not list index.
            entries={path_key(e.name):e for e in sk_archive.entries if e.flags==65536}
            docs=sk_archive.read_selected([entries[path_key(p)].name for p,g in refs]);reload_assets=[]
            for path,g in refs:
                doc=parse(entries[path_key(path)].name,docs[entries[path_key(path)].name]);asset=doc.instances[g]
                name=scalar(named(asset,'Name'),'string')
                if name.rsplit('/',1)[-1].lower()=='1p_reload':
                    if asset.attributes['type']!='Animation.AnimationAsset':raise ValueError('Reload role is not absolute authored animation')
                    skeleton_ref=named(asset,'Skeleton').attributes['ref'];bound=profile['animation_chain']['skeleton_asset']
                    bound_reference(skeleton_ref,bound)
                    skeleton_doc=parse(bound['resource'],exact_resource(sk_archive,bound['resource'],'<non-resource>',bound['sha256']))
                    skeleton_asset=skeleton_doc.instances[guid(bound['instance_guid'])]
                    if skeleton_asset.attributes['type']!='Animation.SkeletonAsset' or scalar(named(skeleton_asset,'Name'),'string')+'.res'!=profile['animation_chain']['skeleton_resource']:
                        raise ValueError('Reload skeleton typed Name differs from bound model')
                    reload_assets.append((name+'.res',doc.sha256,g,doc.resource,skeleton_ref))
            if len(reload_assets)!=1:raise ValueError('Exact authored reload role missing/ambiguous')
            reload_name,reload_doc_hash,reload_guid,reload_document,reload_skeleton_ref=reload_assets[0]
            reload_raw=exact_resource(clip_archive,reload_name,'GrannyAnimation');reload_clip=Clip(reload_raw)
            parts=rigid_parts(lod_data,metadata(mesh_raw)[0],skeleton,assembly_report is not None)
            if mechanism_sink is not None:
                mechanism_sink(profile,parts,closed,reload_clip,skeleton,{
                    'resource':reload_name,'sha256':reload_clip.sha256,'archive':clip_source['archive'],
                    'asset_document':reload_document,'asset_document_sha256':reload_doc_hash,'asset_guid':reload_guid,
                    'skeleton_reference':reload_skeleton_ref,'mesh_sha256':variant['mesh_sha256'],'lod_sha256':lods[0]['data_sha256'],
                    'skin_sections':geometry(lod_data,metadata(mesh_raw)[0],{bone_hash(n):n for n in skeleton.names})})
            assemblies={}
            if assembly_report is not None:
                from bc2_magazine_assembly import derive_assembly
                parts=dict(parts)
                for bone in skeleton.names:
                    if parents.get(bone)!='jntWpn_1' or bone not in parents.values():continue
                    try:
                        merged,assembly=derive_assembly(parts,skeleton,closed,reload_clip,bone)
                        parts[bone]=merged;assemblies[bone]=assembly;assembly_report['assemblies'].append(assembly)
                    except (ValueError,KeyError,TypeError) as exc:assembly_report['rejected_assemblies'].append({'bone':bone,'reason':str(exc)})
            candidates,rejected=role_candidates(parts,skeleton,closed,reload_clip,assemblies=assemblies)
            if assembly_report is not None:assembly_report.update(role_candidates=candidates,rejected_parts=rejected)
            if evidence_sink is not None:evidence_sink(profile,parts,closed,candidates)
            paired=None
            review=reviews.get(profile['weapon']['resource'])
            if review is not None:
                verify_context(review,profile,reload_clip,skeleton,reload_name,variant['mesh_sha256'],lods[0]['data_sha256'])
                role,paired,contact_rejections=select_reviewed_pair(review,candidates,parts,closed,reload_clip,skeleton)
                rejected.extend(contact_rejections)
            elif len(candidates)>1 and profile['native_asset_name'] in paired_grasp_assets:
                role,paired,contact_rejections=select_paired_role(candidates,parts,closed,reload_clip,skeleton,assembly_report['contacts'] if assembly_report is not None else None)
                rejected.extend(contact_rejections)
            else:
                if len(candidates)!=1:raise ValueError('Magazine geometric role candidate is not unique: '+','.join(c['bone'] for c in candidates))
                role=candidates[0]
            bone=role['bone'];part=parts[bone]
            weapon_points=[point(p,closed['weapon_relative']['jntWpn_1']) for p in parts['jntWpn_1']['points']]
            if paired is None and profile['native_asset_name'] in paired_grasp_assets:
                detail={} if assembly_report is not None else None
                if detail is not None:assembly_report['contacts'].append(detail)
                paired=paired_reload_grasp(reload_clip,skeleton,bone,closed['weapon_relative'][bone],part,detail)
            contact=None
            if review is not None:
                contact=dict(schema='fvr.bc2.reviewed-contact-candidate.v1',native_asset_name=profile['native_asset_name'],weapon=dict(profile['weapon']),
                    grip_binding_digest=profile['binding_digest'],configured_mesh_path=profile['configured_mesh_path'],
                    mesh_sha256=variant['mesh_sha256'],lod_sha256=lods[0]['data_sha256'],skeleton_sha256=skeleton.sha256,rig_fingerprint=skeleton.fingerprint,
                    static_clip_sha256=static.sha256,reload_clip=dict(resource=reload_name,sha256=reload_clip.sha256),role_evidence=role,
                    bones=dict(weapon='jntWpn_1',magazine=bone,wrist='LeftHand',fingers=FINGERS),closed_item=closed['weapon_relative'][bone],pair=paired,
                    rail_status='unresolved',runtime_admitted=False,native_role_verified=False)
                if bone in assemblies:contact['assembly']=assemblies[bone]
                reviewed_candidates.append(contact)
            try:proposal=contact_proposal(part,closed['weapon_relative'][bone],weapon_points,closed,paired)
            except (ValueError,KeyError,TypeError) as exc:
                if contact is not None:
                    contact['rail_failure']=str(exc);contact['candidate_digest']=digest(contact)
                raise
            if contact is not None:
                contact['rail_status']='separately_derived_experimental_geometry';contact['candidate_digest']=digest(contact)
            row={'native_asset_name':profile['native_asset_name'],'configured_mesh_path':profile['configured_mesh_path'],
                 'weapon':dict(profile['weapon']),
                 'grip_binding_digest':profile['binding_digest'],'mesh_sha256':variant['mesh_sha256'],'lod_sha256':lods[0]['data_sha256'],
                 'skeleton_sha256':skeleton.sha256,'rig_fingerprint':skeleton.fingerprint,'static_clip_sha256':static.sha256,
                 'reload_clip':{'resource':reload_name,'sha256':reload_clip.sha256,'archive':clip_source['archive'],'archive_index_sha256':clip_archive.index_sha256,
                    'asset_document':reload_document,'asset_document_sha256':reload_doc_hash,'asset_guid':reload_guid,
                    'asset_archive':sk_source['archive'],'asset_archive_index_sha256':sk_archive.index_sha256,'skeleton_reference':reload_skeleton_ref},
                 'role_evidence':role,'rejected_parts':rejected,'bones':{'weapon':'jntWpn_1','magazine':bone,'wrist':'LeftHand','fingers':FINGERS},
                 'geometry':proposal,'runtime_admitted':False,'status':'experimental_geometry_ready_for_review'}
            if bone in assemblies:row['assembly']=assemblies[bone]
            row['profile_digest']=digest(row);output.append(row)
            if assembly_report is not None:assembly_report['profile_digest']=row['profile_digest']
        except (ValueError,KeyError,TypeError) as exc:
            gaps.append({'native_asset_name':profile['native_asset_name'],'reason':str(exc)})
            if assembly_report is not None:assembly_report['failure']=str(exc)
    result={'schema':'fvr.bc2.authored_magazine_geometry','schema_version':1,'profiles':output,'gaps':gaps,
            'runtime_admission':False,'geometry_based_grasp_not_native_calibration':True}
    if rigid_assembly_assets:result['assembly_reports']=assembly_reports
    if reviews:result['reviewed_contact_candidates']=reviewed_candidates
    return result

def cpp_header(result,assets):
    if result.get('schema')!='fvr.bc2.authored_magazine_geometry' or result.get('schema_version')!=1:raise ValueError('Magazine geometry schema')
    if not assets:raise ValueError('Explicit experimental header asset allowlist required')
    profiles=[p for p in result['profiles'] if p['native_asset_name'] in assets]
    if set(p['native_asset_name'] for p in profiles)!=set(assets):raise ValueError('Requested experimental asset unresolved')
    # Old evidence can still generate a legacy builtin header, but it cannot
    # stand in for an exact same-name variant in the runtime selector.
    def configuration_path(p):
        if 'weapon' not in p:return ''
        w=p['weapon'];checked_hash(w['sha256']);guid(w['instance_guid'])
        resource=w['resource']
        if (not isinstance(resource,str) or not resource.endswith('.dbx') or '\0' in resource or
            '\\' in resource or resource.startswith('/') or ':' in resource or
            any(c in ('','.', '..') for c in resource.split('/'))):
            raise ValueError('Invalid exact weapon configuration resource')
        return resource[:-4]
    keys=[(p['native_asset_name'],configuration_path(p)) for p in profiles]
    if len(set(keys))!=len(keys):raise ValueError('Ambiguous experimental asset configuration')
    for asset,path in keys:
        if not path and sum(a==asset for a,_ in keys)>1:raise ValueError('Ambiguous legacy experimental asset')
    def number(v):
        if not math.isfinite(v):raise ValueError('Nonfinite generated value')
        s=format(v,'.9g');return s+('f' if '.' in s or 'e' in s else '.f')
    def transform(values):
        matrix(values);return 'math::Matrix4{{{'+','.join('{{'+','.join(number(v) for v in values[n:n+4])+'}}' for n in range(0,16,4))+'}}}'
    lines=['// Private experimental geometry. Carry/rail are design estimates, not native animation calibration.',
           '#pragma once','#include "Bc2MagazineGeometryProfile.h"','#include <array>',
           'namespace fvr::bc2::generated {',f'inline const std::array<MagazineGeometryProfile,{len(profiles)}> ExperimentalMagazineGeometry = [] {{',
           f' std::array<MagazineGeometryProfile,{len(profiles)}> out{{}};']
    for n,p in enumerate(sorted(profiles,key=lambda x:(x['native_asset_name'],configuration_path(x)))):
        clean={k:v for k,v in p.items() if k!='profile_digest'}
        if digest(clean)!=p['profile_digest']:raise ValueError('Experimental profile digest mismatch')
        if p.get('runtime_admitted') is not False or p['role_evidence'].get('native_role_verified') is not False:raise ValueError('Experimental source claims native admission')
        if p['geometry']['design']['status']!='experimental_geometry_based_estimate':raise ValueError('Missing estimate provenance')
        if p['geometry']['design']['ux_defaults']!=UX:raise ValueError('Generated interaction defaults differ from documented design')
        for key in ('mesh_sha256','lod_sha256','skeleton_sha256','static_clip_sha256','profile_digest'):checked_hash(p[key])
        if p['bones']['fingers']!=FINGERS:raise ValueError('Exact authored finger topology required')
        if p['geometry']['design'].get('authored_reload_grasp'):
            paired=p['geometry']['design'].get('paired_reload_grasp',{})
            if paired.get('selection_mode') not in (None,'stable_relative_co_motion','reviewed_authored_frame'):raise ValueError('Unknown paired contact selection')
            if paired.get('selection_mode')=='reviewed_authored_frame':
                from bc2_reviewed_magazine_contact import validate_export
                validate_export(p,paired)
            if paired.get('selection_mode')=='stable_relative_co_motion':
                entry=p['geometry']['design'].get('entry_evidence',{})
                probe=paired.get('entry_probe',{})
                if (paired.get('co_motion_error_multiple')!=4 or paired['window'].get('co_motion_travel_m',0)<=4*PAIRED_GRASP['max_wrist_translation_m'] or
                    paired.get('criteria')!=PAIRED_GRASP or probe.get('source')!='same_exact_authored_reload_before_first100mm_excursion' or
                    probe.get('direction_method')!='first_to_last_probe_displacement' or not 2<=len(probe.get('samples',[]))<=601 or
                    entry.get('source') not in ('unique_body_near_end_and_authored_initial_withdrawal','receiver_near_tie_resolved_by_authored_withdrawal') or entry.get('direction_agreement',0)<.95 or
                    entry.get('native_axis_verified') is not False or paired.get('entry_probe',{}).get('native_trajectory_verified') is not False):
                    raise ValueError('Incomplete co-motion contact/entry evidence')
                direction=unit(sub(probe['samples'][-1]['position_in_closed_item'],probe['samples'][0]['position_in_closed_item']))
                if entry.get('source')=='receiver_near_tie_resolved_by_authored_withdrawal':
                    selected,agreement,resolved=select_withdrawal_entry_end(entry.get('ends',[]),direction)
                    if not resolved or selected['axis']!=entry.get('axis') or selected['sign']!=entry.get('inward_sign') or abs(agreement-entry.get('direction_agreement',0))>1e-6:
                        raise ValueError('Incomplete independently resolved receiver-near tie')
                if (norm(sub(direction,probe['outward_in_item']))>1e-6 or entry.get('axis') not in (0,1,2) or entry.get('inward_sign') not in (-1,1) or
                    abs(entry['direction_agreement']+direction[entry['axis']]*entry['inward_sign'])>1e-6):
                    raise ValueError('Incoherent co-motion entry direction')
                geometry=p['geometry'];expected_axis=[0.,0.,0.];expected_axis[entry['axis']]=float(entry['inward_sign'])
                insertion=matrix(geometry['item_from_insertion'])
                expected_entry=multiply(insertion,matrix(geometry['attached_item']))
                expected_entry[12:15]=sub(expected_entry[12:15],scale(expected_entry[8:11],UX['travel_m']))
                if (geometry['design'].get('entry_axis')!=entry['axis'] or
                    norm(sub(geometry['design'].get('inward_direction',[]),expected_axis))>1e-6 or
                    len(geometry['design'].get('inward_direction',[]))!=3 or
                    norm(sub(insertion[8:11],expected_axis))>1e-6 or
                    max(abs(a-b) for a,b in zip(matrix(geometry['weapon_from_entry']),expected_entry))>1e-6):
                    raise ValueError('Co-motion receipt does not describe emitted rail')
            try:checked_hash(paired.get('clip_sha256'))
            except (ValueError,TypeError):raise ValueError('Paired grasp source/transform coherence differs') from None
            if (paired.get('source')!='same_frame_authored_reload_contact_candidate' or
                paired.get('clip_sha256')!=p.get('reload_clip',{}).get('sha256') or
                paired.get('skeleton_sha256')!=p['skeleton_sha256'] or paired.get('rig_fingerprint')!=p['rig_fingerprint'] or
                paired.get('magazine_bone')!=p['bones']['magazine'] or
                paired.get('runtime_interpolation_verified') is not False or paired.get('active_native_animation_verified') is not False or
                paired.get('grasp_digest')!=digest({k:p['geometry'][k] for k in ('item_from_hand','wrist_from_fingers')})):
                raise ValueError('Paired grasp source/transform coherence differs')
        g=p['geometry'];bones=p['bones'];identifier=int(p['profile_digest'][:16],16) or 1
        lines+=[' {',f'  auto& g=out[{n}];g.asset={json.dumps(p["native_asset_name"])};g.mesh={json.dumps(p["configured_mesh_path"])};',
                f'  g.configurationPath={json.dumps(configuration_path(p))};',
                '  g.meshKind=SelectedMeshKind::Unknown;g.rigFingerprint=0x'+p['rig_fingerprint'].split(':')[1]+'ULL;',
                '  auto& c=g.interaction;auto& p=c.insertion;',f'  p.id=0x{identifier:016x}ULL;p.revision=1;',
                '  p.family=interaction::ReloadInsertionFamily::Magazine;p.orientation=interaction::ReloadInsertionOrientation::Keyed;',
                '  p.approach=interaction::ReloadInsertionApproach::RailContact;',
                '  p.itemFromHand='+transform(g['item_from_hand'])+';',
                '  p.itemFromInsertion='+transform(g['item_from_insertion'])+';',
                '  p.weaponFromEntry='+transform(g['weapon_from_entry'])+';']
        for member,key in (('travelMeters','travel_m'),('captureDistanceMeters','capture_m'),('releaseDistanceMeters','release_m'),
                           ('postCaptureTravelMeters','post_capture_travel_m'),('seatToleranceMeters','seat_tolerance_m'),
                           ('captureAngleRadians','capture_angle_rad'),('releaseAngleRadians','release_angle_rad'),
                           ('maxStepMeters','max_step_m'),('maxStepRadians','max_step_angle_rad')):
            lines.append('  p.'+member+'='+number(UX[key])+';')
        for member,key in (('alignmentNs','alignment_ns'),('seatDwellNs','seat_dwell_ns'),('maxSampleGapNs','max_sample_gap_ns'),('maxGuidedNs','max_guided_ns')):
            lines.append('  p.'+member+'='+str(UX[key])+'LL;')
        lines+=[f'  c.removalContact={{0x{(identifier^0x47454f4d45545259) or 1:016x}ULL,1}};',
                '  c.pullMeters='+number(UX['pull_m'])+';c.maxPullStepMeters='+number(UX['max_step_m'])+';',
                '  g.bones={'+','.join(json.dumps(bones[k]) for k in ('weapon','magazine','wrist'))+',{'+','.join(json.dumps(f) for f in FINGERS)+'}};',
                '  g.attachedItem='+transform(g['attached_item'])+';']
        for i,name in enumerate(FINGERS):lines.append(f'  g.wristFromFinger[{i}]='+transform(g['wrist_from_fingers'][name])+';')
        if 'assembly' in p:
            from bc2_magazine_assembly import validate_receipt
            validate_receipt(p['assembly'],p)
            lines.append(f"  g.assemblyCount={len(p['assembly']['members'])};")
            for i,member in enumerate(p['assembly']['members']):
                lines.append(f'  g.assembly[{i}]={{'+json.dumps(member['bone'])+','+json.dumps(member['parent'])+','+transform(member['item_from_bone'])+'};')
        lines+=[' }']
    lines+=[' return out;','}();','}'];return '\n'.join(lines)+'\n'

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--game',required=True,type=Path)
    p.add_argument('--grip-bindings',required=True,type=Path);p.add_argument('--mesh-bindings',required=True,type=Path)
    p.add_argument('--asset',required=True,action='append');p.add_argument('--output',required=True,type=Path)
    p.add_argument('--header',type=Path,help='Optional private experimental registry')
    p.add_argument('--header-asset',action='append',default=[],help='Explicit registry allowlist, independent of inspection assets')
    p.add_argument('--paired-reload-grasp',action='append',default=[],help='Explicit experimental same-frame authored carry pose asset; default keeps geometric estimate')
    p.add_argument('--rigid-assembly',action='append',default=[],help='Explicit complete rigid child-subtree extraction; paired contact remains mandatory')
    p.add_argument('--reviewed-contacts',type=Path,help='Explicit exact semantic root and complete authored frame annotations; never native admission')
    args=p.parse_args(argv);bindings,bh=read_json(args.grip_bindings);meshes,mh=read_json(args.mesh_bindings)
    reviews,rh=read_json(args.reviewed_contacts) if args.reviewed_contacts else ([],None)
    result=derive(args.game,bindings,meshes,set(args.asset),paired_grasp_assets=set(args.paired_reload_grasp),rigid_assembly_assets=set(args.rigid_assembly),reviewed_contacts=reviews);result['input_sha256']={'grip_bindings':bh,'mesh_bindings':mh}
    if rh is not None:result['input_sha256']['reviewed_contacts']=rh
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    if args.header:
        data=cpp_header(result,set(args.header_asset));args.header.parent.mkdir(parents=True,exist_ok=True);args.header.write_text(data,encoding='utf-8')
    print(json.dumps({'profiles':len(result['profiles']),'gaps':result['gaps']}));return 0
if __name__=='__main__':raise SystemExit(main())
