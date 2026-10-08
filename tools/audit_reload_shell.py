"""Offline shell bind and authored hand observations; never emits a reload profile.

New captures may supply inverse_bind and an inclusive native left-hand subtree.
Legacy/incomplete captures remain explicit missing evidence. The asset-to-native
basis is declared, not inferred from a visually plausible result.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import itertools
import math
from pathlib import Path
import json
import numpy as np
import audit_reload_geometry as geometry
import weapon_mechanism_pipeline as mechanisms
import weapon_profile_pipeline as profiles


BASIS=np.diag([1.,1.,-1.,1.])


def positive(value,label):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or value<=0:
        raise ValueError(label+' must be finite and positive')
    return float(value)


def vector(value,label):
    result=np.asarray(value,dtype=float)
    if result.shape!=(3,) or not np.isfinite(result).all():raise ValueError(label+' must contain three finite values')
    return result


def metric(matrix,units):
    result=profiles.matrix(matrix).copy();result[3,:3]/=units;return result


def landmarks(binding):
    if binding.get('schema')!='fvr.bc2.reload_shell_binding' or binding.get('schema_version')!=1:
        raise ValueError('Unsupported shell binding evidence')
    skin=binding['shell_skin_binding'];candidate=binding['asset_space_landmark_candidates']
    if not skin.get('all_vertices_rigid',skin.get('all_128_vertices_rigid')) or not skin.get('native_bone_name'):
        raise ValueError('Rigid named shell binding required')
    if not binding.get('native_mesh_owner',{}).get('identity_coherent'):
        raise ValueError('Missing prior live mesh ownership evidence')
    center=vector(candidate['bounds_center'],'asset center');extent=vector(candidate['extent'],'asset extent')
    if np.any(extent<=0):raise ValueError('Positive shell bounds required')
    axis=candidate['unsigned_long_axis_index']
    if isinstance(axis,bool) or not isinstance(axis,int) or not 0<=axis<3:raise ValueError('Invalid asset long axis')
    if int(np.argmax(extent))!=axis:raise ValueError('Axis disagrees with asset bounds')
    endpoints=np.asarray(candidate['endpoint_bounds_centers'],dtype=float)
    expected=np.stack([center.copy(),center.copy()]);expected[0,axis]-=extent[axis]/2;expected[1,axis]+=extent[axis]/2
    if endpoints.shape!=(2,3) or not np.isfinite(endpoints).all() or not np.allclose(endpoints,expected,atol=1e-10,rtol=0):
        raise ValueError('Endpoint centers disagree with asset bounds')
    corners=np.asarray([center+np.asarray(sign)*extent/2 for sign in itertools.product((-1,1),repeat=3)])
    return skin['native_bone_name'],center,extent,endpoints,corners


def map_asset_bounds(center,endpoints,corners,inverse_bind,units,asset_units):
    """Row-vector chain: raw asset RH -> canonical Z reflection -> bone local."""
    inverse_bind=profiles.matrix(inverse_bind)
    scale=positive(units,'units_per_meter')/positive(asset_units,'asset_units_per_meter')
    def transform(points):
        raw=np.asarray(points,dtype=float)*scale
        canonical=np.c_[raw,np.ones(len(raw))]@BASIS
        return (canonical@inverse_bind)[:,:3]/units
    local_center=transform([center])[0];local_ends=transform(endpoints);local_corners=transform(corners)
    axis=local_ends[1]-local_ends[0];length=np.linalg.norm(axis)
    if not np.isfinite(length) or length<=0:raise ValueError('Degenerate transformed shell axis')
    # A proper center frame follows the native shell bone orientation; the asset
    # reflection acts on coordinates, never masquerades as a proper rotation.
    frame=np.eye(4);frame[3,:3]=(np.r_[center*scale,1]@BASIS)[:3]
    local_frame=profiles.matrix(frame@inverse_bind)
    return {'center_m':local_center.tolist(),'endpoint_bounds_centers_m':local_ends.tolist(),
            'asset_aabb_corners_in_bone_m':local_corners.tolist(),
            'unsigned_long_axis':(axis/length).tolist(),'axis_length_m':float(length),
            'center_frame_in_bone':metric(local_frame,units).reshape(-1).tolist()},local_frame


def hand_snapshot(row):
    captured=row.get('left_hand_bones_captured',False)
    if not isinstance(captured,bool):raise ValueError('Explicit hand capture state must be boolean')
    if not captured:
        if row.get('native_left_hand_bones') or row.get('left_hand_bones_complete',False) or row.get('left_hand_bones_dropped',0):raise ValueError('Uncaptured hand carries contradictory evidence')
        return 'not_captured',{}
    if row.get('left_hand_bones_complete') is not True or row.get('left_hand_bones_dropped',0)!=0:
        return 'incomplete',{}
    adapted=dict(row);adapted['native']=row['native_left_wrist']
    adapted['bone_roles']={'weapon_root':row.get('bone_roles',{}).get('left_wrist')}
    adapted['native_weapon_bones']=row.get('native_left_hand_bones')
    adapted['weapon_bones_complete']=True;adapted['weapon_bones_dropped']=0
    _,valid=mechanisms.snapshot(adapted,profiles.Policy())
    nodes={node['name']:node for node in row['native_left_hand_bones']}
    result={}
    for name,bone in valid.items():
        node=nodes[name]
        if node.get('inverse_bind') is not None:profiles.matrix(node['inverse_bind'])
        result[name]={'world':None if bone['hidden'] else profiles.matrix(node['native']),'parent_name':node['parent_name']}
    return 'complete',result


def phase_records(trace):
    result=defaultdict(list)
    for record in trace.get('gameplay',{}).get('reload_flow',{}).get('records',[]):
        if record.get('kind')!=0 or not record.get('finished') or not record.get('identity_retained'):continue
        before,after=record.get('before',{}),record.get('after',{})
        keys=('soldier','weapon','actor_generation','space','firing','wrapper_offset')
        if any(before.get(k)!=after.get(k) for k in keys):continue
        identity=tuple(after.get(k) for k in keys[:4])
        if any(v is None for v in identity):continue
        stamp=record.get('end_tick_ms')
        if isinstance(stamp,bool) or not isinstance(stamp,(int,float)) or not math.isfinite(stamp):continue
        result[identity].append(record)
    return result


def phase_for(records,row):
    key=(row['actor'],row['weapon'],row['owner_generation'],row['space']);latest={}
    for record in records.get(key,[]):
        age=row['captured_ms']-record['end_tick_ms']
        if not 0<=age<=250:continue
        after=record['after'];branch=(after.get('firing'),after.get('wrapper_offset'))
        if branch not in latest or latest[branch]['end_tick_ms']<record['end_tick_ms']:latest[branch]=record
    return [{'wrapper_offset':r['after']['wrapper_offset'],'firing':r['after']['firing'],
             'state':r['after'].get('current'),'loaded':r['after'].get('loaded'),'reserve':r['after'].get('reserve'),
             'update_id':r.get('id'),'age_ms':row['captured_ms']-r['end_tick_ms'],
             'temporal_match':'most_recent_prior_same_owner_update_not_atomic'} for _,r in sorted(latest.items())]


def transfer_neighbors(trace,group):
    result=[];identity=group['identity'];key=(identity['actor'],identity['weapon'],identity['owner_generation'],identity['space'])
    for record in trace.get('gameplay',{}).get('reload_flow',{}).get('records',[]):
        if record.get('kind')!=2 or not record.get('finished') or not record.get('identity_retained'):continue
        before,after=record.get('before',{}),record.get('after',{})
        keys=('soldier','weapon','actor_generation','space')
        if tuple(before.get(k) for k in keys)!=key or tuple(after.get(k) for k in keys)!=key:continue
        if before.get('firing')!=after.get('firing') or before.get('wrapper_offset')!=after.get('wrapper_offset'):continue
        if any(not isinstance(state.get(k),int) or isinstance(state.get(k),bool) for state in (before,after) for k in ('loaded','reserve')):continue
        if after['loaded']-before['loaded']!=1 or after['reserve']-before['reserve']!=-1:continue
        stamp=record.get('end_tick_ms')
        if isinstance(stamp,bool) or not isinstance(stamp,(int,float)) or not math.isfinite(stamp):continue
        candidates=group['observations']
        previous=[o for o in candidates if 0<=stamp-o['captured_ms']<=250]
        following=[o for o in candidates if 0<=o['captured_ms']-stamp<=250]
        def neighbor(values,prior):
            if not values:return None
            obs=(max if prior else min)(values,key=lambda o:o['captured_ms'])
            return {'row':obs['row'],'captured_ms':obs['captured_ms'],'relative_to_transfer_ms':obs['captured_ms']-stamp,
                    'shell_center_in_weapon':obs['shell_center_in_weapon'],'hand_capture_status':obs['hand_capture_status']}
        result.append({'id':record.get('id'),'tick_ms':stamp,'wrapper_offset':after.get('wrapper_offset'),
            'firing':after.get('firing'),'loaded_before':before['loaded'],'loaded_after':after['loaded'],
            'reserve_before':before['reserve'],'reserve_after':after['reserve'],
            'previous_visible_pose':neighbor(previous,True),'following_visible_pose':neighbor(following,False),
            'verified_seated_pose':False})
    return result


def join_current_mesh_capture(capture,trace,result,expected_path):
    if not capture.get('read_only') or capture.get('native_calls') is not False or capture.get('process_writes') is not False:
        raise ValueError('Expected read-only mesh ownership report')
    if capture.get('pid')!=trace.get('pid'):raise ValueError('Mesh report and native trace PID mismatch')
    owner=capture['initial_owner'];weapons=[w for w in capture['weapons'] if w.get('asset_name')==result['asset_name']]
    if len(weapons)!=1:raise ValueError('Mesh report item identity ambiguous')
    weapon=weapons[0];links=weapon.get('mesh_links',{})
    if not links.get('identity_coherent') or links.get('weapon')!=weapon['weapon'] or links.get('data')!=weapon['data'] or weapon['weapon'] not in owner['items']:
        raise ValueError('Mesh report ownership mismatch')
    matches=[(state,mesh) for state in links.get('states',[]) for mesh in state.get('meshes',[]) if mesh.get('asset_path')==expected_path]
    if len(matches)!=1:raise ValueError('Exact mesh path missing or ambiguous')
    state,mesh=matches[0]
    for group in result['groups']:
        matches_identity=group['actor']==owner['actor'] and group['weapon']==weapon['weapon']
        group['mesh_capture_join']={'same_pid_actor_and_weapon':matches_identity,
            'weapon_selected_in_mesh_snapshot':owner['selected_weapon']==weapon['weapon'],
            'weapon_pose_captured_in_native_trace':True,'mesh_address':mesh['address'],
            'state_address':state['state_address'],'asset_path':expected_path,
            'atomic_resource_continuity_verified':False,'visible_skin_section_verified':False}
    return {'pid':capture['pid'],'utc':capture['utc'],'actor':owner['actor'],'weapon':weapon['weapon'],
            'matching_native_groups':sum(g['mesh_capture_join']['same_pid_actor_and_weapon'] for g in result['groups']),
            'limitation':'Separate snapshots match process/actor/item pointers; this does not establish render-section visibility or uninterrupted resource identity.'}


def analyze(trace,binding,asset,asset_units=1.,explicit_window=None):
    if binding.get('expected_asset_name')!=asset:raise ValueError('Asset identity does not match mesh evidence')
    expected_skeletons=binding.get('expected_skeleton_fingerprints',[])
    if not expected_skeletons:raise ValueError('Mesh evidence requires a named native skeleton fingerprint')
    bone_name,center,extent,endpoints,corners=landmarks(binding)
    positive(asset_units,'asset_units_per_meter')
    rows=trace.get('gameplay',{}).get('rig_publication',{}).get('weapon_profile_samples',[])
    windows=geometry.reload_windows(trace);phases=phase_records(trace)
    groups={};counts=Counter();issues=[]
    for index,row in enumerate(rows):
        if row.get('asset_name')!=asset:continue
        counts['matching_rows']+=1
        try:
            key=geometry.identity(row);stamp=row.get('captured_ms')
            if row['skeleton'] not in expected_skeletons:raise ValueError('Captured skeleton does not match mesh evidence')
            if isinstance(stamp,bool) or not isinstance(stamp,(int,float)) or not math.isfinite(stamp):raise ValueError('Finite capture timestamp required')
            if row.get('attachment_pending') is not False:counts['attachment_pending']+=1;continue
            root,bones=mechanisms.snapshot(row,profiles.Policy())
            if bone_name not in bones:raise ValueError('Bound shell bone missing from captured weapon')
            node=next(b for b in row['native_weapon_bones'] if b['name']==bone_name)
            if node.get('inverse_bind') is None:counts['missing_inverse_bind']+=1;continue
            inverse_bind=profiles.matrix(node['inverse_bind']);units=positive(row['units_per_meter'],'units_per_meter')
            sequence=row.get('capture_sequence')
            if isinstance(sequence,bool) or not isinstance(sequence,int) or sequence<=0:raise ValueError('Positive capture sequence required')
            hand_status,hand=hand_snapshot(row)
            topology=tuple(sorted((name,b['parent_name']) for name,b in bones.items()))
            if key not in groups:
                mapped,_=map_asset_bounds(center,endpoints,corners,inverse_bind,units,asset_units)
                groups[key]={'identity':{**dict(zip(('asset_name','skeleton','actor','weapon','owner_generation','space','capture_episode'),key)),
                    'capture_provenance':json.loads(key[7]),'pose_asset_binding_verified':False,'usable_as_verified_grasp':False},
                    'units_per_meter':units,'inverse_bind':inverse_bind,'mapped':mapped,'topology':topology,
                    'sequence':0,'last_stamp':-1,'observations':[],'counts':Counter(),'hand_topology':None}
            group=groups[key]
            if sequence<=group['sequence'] or stamp<=group['last_stamp']:raise ValueError('Sequence or timestamp rollback within identity')
            if topology!=group['topology'] or units!=group['units_per_meter']:raise ValueError('Topology or units changed within identity')
            if not np.allclose(inverse_bind,group['inverse_bind'],atol=1e-6,rtol=0):raise ValueError('Shell inverse bind changed within identity')
            if hand_status=='complete':
                current=tuple(sorted((name,b['parent_name']) for name,b in hand.items()))
                if group['hand_topology'] is not None and current!=group['hand_topology']:raise ValueError('Hand topology changed within identity')
                group['hand_topology']=current
            group['sequence']=sequence;group['last_stamp']=stamp;group['counts']['validated_rows']+=1
            group['counts']['hand_'+hand_status]+=1
            if bones[bone_name]['hidden']:group['counts']['hidden_shell_rows']+=1;continue
            window=explicit_window or windows.get((row['actor'],row['weapon'],row['owner_generation'],row['space']))
            if window is None:group['counts']['missing_native_reload_window']+=1;continue
            if not window[0]<=stamp<=window[1]:group['counts']['outside_reload_window']+=1;continue
            shell_world=profiles.matrix(node['native']);weapon_world=profiles.matrix(row['native']);wrist_world=profiles.matrix(row['native_left_wrist'])
            _,local_center=map_asset_bounds(center,endpoints,corners,inverse_bind,units,asset_units)
            center_world=profiles.matrix(local_center@shell_world)
            relatives={row.get('bone_roles',{}).get('left_wrist','left_wrist'):metric(center_world@np.linalg.inv(wrist_world),units)}
            for name,value in hand.items():
                if value['world'] is not None:relatives[name]=metric(center_world@np.linalg.inv(value['world']),units)
            group['observations'].append({'row':index,'captured_ms':stamp,'capture_sequence':sequence,
                'shell_center_in_weapon':metric(center_world@np.linalg.inv(weapon_world),units).reshape(-1).tolist(),
                'shell_center_in_hand_bones':{name:matrix.reshape(-1).tolist() for name,matrix in relatives.items()},
                'hand_capture_status':hand_status,'native_phases':phase_for(phases,row)})
        except (ValueError,TypeError,KeyError,np.linalg.LinAlgError) as exc:
            issues.append({'row':index,'reason':str(exc)})
    output=[]
    for group in groups.values():
        observations=group['observations'];relations={}
        names=sorted({n for obs in observations for n in obs['shell_center_in_hand_bones']})
        for name in names:
            samples=[{'row':o['row'],'captured_ms':o['captured_ms'],'matrix':profiles.matrix(o['shell_center_in_hand_bones'][name])} for o in observations if name in o['shell_center_in_hand_bones']]
            relations[name]={'motion':geometry.motion([s['matrix'] for s in samples]),
                'stable_observed_runs':geometry.stable_runs(samples),'verified_grasp':False}
        output.append({**group['identity'],'units_per_meter':group['units_per_meter'],
            'shell_inverse_bind':group['inverse_bind'].reshape(-1).tolist(),
            'mapped_asset_bounds_in_shell_bone':group['mapped'],'counts':dict(group['counts']),
            'hand_relations':relations,'observations':observations,
            'native_transfer_neighbors':transfer_neighbors(trace,group)})
    return {'schema':'fvr.bc2.reload_shell_observations','schema_version':1,'asset_name':asset,
        'shell_bone':bone_name,'asset_units_per_meter':asset_units,'counts':dict(counts),'issues':issues,'groups':output,
        'basis':{'asset_source':'declared native RH asset bind coordinates','capture_source':'canonical LH row-vector matrices',
                 'source_to_canonical_diagonal':[1,1,-1,1],'live_vertex_space_correspondence_verified':False},
        'prior_live_mesh_owner':binding['native_mesh_owner'],'native_insertion_profile':None,
        'limitations':['Mapped local geometry is conditional on the declared asset/native bind-space correspondence; no renderer vertex capture proves it yet.',
          'Uncollapsed native bone state does not prove a visible shell draw.',
          'Stable hand/finger relations are observations, not authored grasp or loading-port sockets.',
          'Native phase annotations use preceding same-owner updates within250ms, not atomic pose/update snapshots.',
          'The prior live mesh capture may describe an inactive inventory item; it does not prove current render selection.']}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True);parser.add_argument('--binding',type=Path,required=True)
    parser.add_argument('--mesh-links',type=Path,help='Optional current read-only mesh ownership capture for exact process/actor/item join')
    parser.add_argument('--asset',required=True);parser.add_argument('--asset-units-per-meter',type=float,default=1)
    parser.add_argument('--window-ms',type=float,nargs=2);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(argv)
    if args.window_ms and (not all(math.isfinite(v) and v>=0 for v in args.window_ms) or args.window_ms[1]<=args.window_ms[0]):parser.error('Increasing finite window required')
    binding=profiles.load_json(args.binding)
    for name in ('asset_evidence','live_mesh_links'):
        source=binding['sources'][name]
        if profiles.digest(source['path'])!=source['sha256']:raise ValueError('Binding evidence source hash mismatch: '+name)
    asset_evidence=profiles.load_json(binding['sources']['asset_evidence']['path'])
    binding['expected_asset_name']=asset_evidence.get('native_capture',{}).get('asset_name')
    binding['expected_skeleton_fingerprints']=asset_evidence.get('native_capture',{}).get('skeleton_fingerprints',[])
    trace=profiles.load_json(args.native)
    result=analyze(trace,binding,args.asset,args.asset_units_per_meter,args.window_ms)
    if args.mesh_links:
        result['current_mesh_capture']=join_current_mesh_capture(profiles.load_json(args.mesh_links),trace,result,binding['native_mesh_owner']['asset_path'])
    result['sources']={'native':{'path':str(args.native.resolve()),'sha256':profiles.digest(args.native)},
        'binding':{'path':str(args.binding.resolve()),'sha256':profiles.digest(args.binding)},
        'analyzer_sha256':profiles.digest(__file__)}
    if args.mesh_links:result['sources']['current_mesh_capture']={'path':str(args.mesh_links.resolve()),'sha256':profiles.digest(args.mesh_links)}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf8')
    print(json.dumps({'groups':len(result['groups']),'issues':len(result['issues']),'counts':result['counts'],'native_insertion_profile':None}))
    return bool(result['issues'])

if __name__=='__main__':raise SystemExit(main())
