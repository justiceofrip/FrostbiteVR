"""Offline geometric grasp/loading-area candidates from actual skinned mesh triangles.

Reports derived contacts and ray intersections, never exports original mesh data.
Joint centers are fingertip proxies; no finger surface or loading socket is invented.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct
import numpy as np
import inspect_bc2_mesh_asset as assets
import weapon_profile_pipeline as profiles


def triangle_closest(point,triangles):
    point=np.asarray(point,float);triangles=np.asarray(triangles,float)
    if point.shape!=(3,) or triangles.ndim!=3 or triangles.shape[1:]!=(3,3) or not len(triangles) or not np.isfinite(triangles).all() or not np.isfinite(point).all():raise ValueError('Finite point and triangles required')
    a,b,c=triangles[:,0],triangles[:,1],triangles[:,2];ab=b-a;ac=c-a
    normals=np.cross(ab,ac);area=np.einsum('ij,ij->i',normals,normals);valid=area>1e-20
    if not valid.any():raise ValueError('All triangles degenerate')
    projected=point-normals*(np.einsum('ij,ij->i',point-a,normals)/np.where(valid,area,1))[:,None]
    v=projected-a;aa=np.einsum('ij,ij->i',ab,ab);bb=np.einsum('ij,ij->i',ab,ac);cc=np.einsum('ij,ij->i',ac,ac)
    av=np.einsum('ij,ij->i',ab,v);cv=np.einsum('ij,ij->i',ac,v);den=aa*cc-bb*bb
    u=(cc*av-bb*cv)/np.where(valid,den,1);w=(aa*cv-bb*av)/np.where(valid,den,1)
    inside=valid&(u>=-1e-10)&(w>=-1e-10)&(u+w<=1+1e-10)
    options=[projected];distances=[np.where(inside,np.sum((projected-point)**2,axis=1),np.inf)]
    for start,end in ((a,b),(b,c),(c,a)):
        edge=end-start;length=np.einsum('ij,ij->i',edge,edge)
        t=np.clip(np.einsum('ij,ij->i',point-start,edge)/np.where(length>0,length,1),0,1)
        contact=start+t[:,None]*edge;options.append(contact);distances.append(np.where(valid,np.sum((contact-point)**2,axis=1),np.inf))
    distance=np.stack(distances);which,index=np.unravel_index(np.argmin(distance),distance.shape)
    normal=normals[index]/np.sqrt(area[index])
    return {'distance_m':float(np.sqrt(distance[which,index])),'point':options[which][index].tolist(),
            'triangle_index':int(index),'unoriented_normal':normal.tolist()}


def ray_hits(origin,direction,triangles,maximum=1):
    origin=np.asarray(origin,float);direction=np.asarray(direction,float);triangles=np.asarray(triangles,float)
    if origin.shape!=(3,) or direction.shape!=(3,) or triangles.ndim!=3 or triangles.shape[1:]!=(3,3) or not np.isfinite(origin).all() or not np.isfinite(direction).all() or not np.isfinite(triangles).all() or not math.isfinite(maximum) or maximum<=0:raise ValueError('Invalid ray geometry')
    length=np.linalg.norm(direction)
    if length<=0:raise ValueError('Ray direction is zero')
    direction=direction/length;a=triangles[:,0];edge1=triangles[:,1]-a;edge2=triangles[:,2]-a
    p=np.cross(np.broadcast_to(direction,edge2.shape),edge2);det=np.einsum('ij,ij->i',edge1,p);valid=np.abs(det)>1e-12
    inv=np.where(valid,1/np.where(valid,det,1),0);tvec=origin-a;u=np.einsum('ij,ij->i',tvec,p)*inv
    q=np.cross(tvec,edge1);v=(q@direction)*inv;t=np.einsum('ij,ij->i',edge2,q)*inv
    indices=np.flatnonzero(valid&(u>=-1e-8)&(v>=-1e-8)&(u+v<=1+1e-8)&(t>=0)&(t<=maximum))
    indices=indices[np.argsort(t[indices])]
    return [{'distance_m':float(t[i]),'point':(origin+t[i]*direction).tolist(),'triangle_index':int(i)} for i in indices]


def coplanar_patch(triangles,seed,tolerance=1e-5):
    triangles=np.asarray(triangles,float)
    if triangles.ndim!=3 or triangles.shape[1:]!=(3,3) or not 0<=seed<len(triangles) or tolerance<=0:raise ValueError('Invalid patch geometry')
    normals=np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0]);length=np.linalg.norm(normals,axis=1)
    if length[seed]<=1e-12:raise ValueError('Degenerate patch seed')
    normal=normals[seed]/length[seed];origin=triangles[seed,0]
    eligible=(length>1e-12)&(np.abs(normals@normal)>=length*.999999)&(np.max(np.abs((triangles-origin)@normal),axis=1)<=tolerance)
    edge_owners={};triangle_edges={}
    for index in np.flatnonzero(eligible):
        keys=[tuple(np.round(p/tolerance).astype(np.int64)) for p in triangles[index]]
        edges=[tuple(sorted((keys[a],keys[b]))) for a,b in ((0,1),(1,2),(2,0))];triangle_edges[int(index)]=edges
        for edge in edges:edge_owners.setdefault(edge,[]).append(int(index))
    seen={seed};queue=[seed]
    while queue:
        at=queue.pop()
        for edge in triangle_edges[at]:
            for other in edge_owners[edge]:
                if other not in seen:seen.add(other);queue.append(other)
    selected=triangles[sorted(seen)];points=selected.reshape(-1,3);area=length[sorted(seen)]/2
    boundary=sum(sum(owner in seen for owner in edge_owners[e])==1 for i in seen for e in triangle_edges[i])
    return {'triangle_count':len(seen),'triangle_indices':sorted(seen),'area_m2':float(area.sum()),
            'bounds_minimum':points.min(axis=0).tolist(),'bounds_maximum':points.max(axis=0).tolist(),
            'area_centroid':np.average(selected.mean(axis=1),axis=0,weights=area).tolist(),
            'unoriented_normal':normal.tolist(),'boundary_edge_count':boundary,
            'weld_tolerance_m':tolerance,'aperture_verified':False}


def private_geometry(archive,prefix):
    names=[prefix+'.res',prefix+'_lod0_data.res'];raw=archive.read_selected(names)
    metadata=assets.mesh_data(raw[names[1]]);data=raw[names[1]];start=len(data)-metadata['vertex_bytes']-metadata['index_bytes']
    geometry=[]
    for section in metadata['sections']:
        at=start+section['vertex_offset'];vertices=[];weights=[];ids=[]
        for i in range(section['vertices']):
            offset=at+i*section['stride'];vertices.append(struct.unpack_from('<3e',data,offset))
            slots=data[offset+8:offset+12];weight=data[offset+12:offset+16]
            ids.append([section['skin_palette_ids'][slot] if value else section['skin_palette_ids'][0] for slot,value in zip(slots,weight)])
            weights.append([value/255 for value in weight])
        indices=np.frombuffer(data,dtype='<u2',count=section['triangles']*3,offset=start+metadata['vertex_bytes']+section['first_index']*2).reshape(-1,3)
        geometry.append({'name':section['name'],'vertices':np.asarray(vertices),'weights':np.asarray(weights),'bone_ids':np.asarray(ids),'indices':indices})
    return geometry,raw


def skin_sections(geometry,skin_names,row,asset_units=1):
    units=row['units_per_meter']
    if not math.isfinite(units) or units<=0 or not math.isfinite(asset_units) or asset_units<=0:raise ValueError('Positive units required')
    bones={b['name']:b for b in row['native_weapon_bones']};weapon_inverse=np.linalg.inv(profiles.matrix(row['native']))
    matrices={}
    for identifier,name in skin_names.items():
        bone=bones[name]
        if bone['hidden']:
            # Receiver geometry never consumes hidden shell influences.
            matrices[identifier]=None;continue
        matrices[identifier]=profiles.matrix(bone['inverse_bind'])@profiles.matrix(bone['native'])@weapon_inverse
    output=[]
    for section in geometry:
        if section['name'].endswith('_ZOnly'):continue
        points=section['vertices']*units/asset_units*np.array([1,1,-1]);points=np.c_[points,np.ones(len(points))]
        result=np.zeros((len(points),4));hidden=False
        for slot in range(4):
            for identifier in np.unique(section['bone_ids'][:,slot]):
                mask=(section['bone_ids'][:,slot]==identifier)&(section['weights'][:,slot]>0)
                if not mask.any():continue
                matrix=matrices[int(identifier)]
                if matrix is None:hidden=True;continue
                result[mask]+=(points[mask]@matrix)*section['weights'][mask,slot,None]
        if hidden:continue
        output.append({'name':section['name'],'triangles':result[section['indices'],:3]/units})
    return output


def describe_contact(contact,sections):
    index=contact['triangle_index']
    for section in sections:
        if index<len(section['triangles']):return dict(contact,section=section['name'],section_triangle_index=index)
        index-=len(section['triangles'])
    raise ValueError('Contact outside sections')


def analyze(native,observations,asset_evidence,archive,prefix):
    geometry,raw=private_geometry(archive,prefix)
    expected={r['name']:r['sha256'] for r in asset_evidence['resources']}
    for name,value in raw.items():
        if assets.sha(value)!=expected.get(name):raise ValueError('Resource differs from proven mesh evidence')
    skin_names={r['palette_id']:r['matched_native_name'] for r in asset_evidence['mesh_set']['skin_map']}
    ammo_section_names={s['name'] for s in asset_evidence['geometry']['sections'] if s.get('material','').endswith(('/Ammo_Brass','/Ammo_Plastic'))}
    if any(not name for name in skin_names.values()):raise ValueError('Unresolved mesh skin names')
    rows=native['gameplay']['rig_publication']['weapon_profile_samples'];results=[]
    for group in observations['groups']:
        measured=group['observations'];transfers=[r for r in group['native_transfer_neighbors'] if r['wrapper_offset']==60 and r['previous_visible_pose']]
        terminal_rows={r['previous_visible_pose']['row'] for r in transfers};contacts=[];receiver=[]
        for observation in measured:
            row=rows[observation['row']]
            if row['capture_sequence']!=observation['capture_sequence'] or row['asset_name']!=group['asset_name']:raise ValueError('Observation row identity mismatch')
            sections=skin_sections(geometry,skin_names,row,observations['asset_units_per_meter'])
            ammo=[s for s in sections if s['name'] in ammo_section_names]
            body=[s for s in sections if s['name'] not in ammo_section_names]
            if len(ammo)!=2 or not body:raise ValueError('Expected proven shell and receiver sections')
            ammo_tri=np.concatenate([s['triangles'] for s in ammo]);body_tri=np.concatenate([s['triangles'] for s in body])
            joints={b['name']:b for b in row['native_left_hand_bones']}
            if not row.get('left_hand_bones_complete'):continue
            inv_weapon=np.linalg.inv(profiles.matrix(row['native']));probes={}
            for name in ('LeftHandThumb3','LeftHandIndex3','LeftHandMiddle3'):
                joint=joints.get(name)
                if not joint or joint['hidden']:continue
                point=(profiles.matrix(joint['native'])@inv_weapon)[3,:3]/row['units_per_meter']
                probes[name]=dict(describe_contact(triangle_closest(point,ammo_tri),ammo),joint_center_in_weapon=point.tolist())
            center=profiles.matrix(observation['shell_center_in_weapon'])[3,:3]
            item={'row':observation['row'],'captured_ms':observation['captured_ms'],'terminal_before_transfer':observation['row'] in terminal_rows,
                'joint_surface_probes':probes,'shell_center_in_weapon':observation['shell_center_in_weapon'],
                'observed_center_frame_in_wrist':observation['shell_center_in_hand_bones'].get('LeftHand'),
                'native_phases':observation['native_phases']}
            if all(name in probes for name in ('LeftHandThumb3','LeftHandIndex3')):
                item['pinch_proxy_score_m']=max(probes[name]['distance_m'] for name in ('LeftHandThumb3','LeftHandIndex3'))
            contacts.append(item)
            if observation['row'] not in terminal_rows:continue
            nearest=describe_contact(triangle_closest(center,body_tri),body)
            scans=[]
            # Actual two-sided triangle intersections on a fixed documented grid,
            # not generated aperture vertices or an invented collision primitive.
            for dx in np.linspace(-.025,.025,11):
                for dz in np.linspace(-.045,.045,19):
                    origin=center+np.array([dx,-.08,dz]);hits=ray_hits(origin,[0,1,0],body_tri,.16)
                    scans.append({'dx_m':float(dx),'dz_m':float(dz),'first_hit':describe_contact(hits[0],body) if hits else None})
            prior=[o for o in measured if 0<observation['captured_ms']-o['captured_ms']<=250]
            approach=None
            prior=[o for o in prior if np.linalg.norm(center-profiles.matrix(o['shell_center_in_weapon'])[3,:3])>=.01]
            if prior:
                previous=max(prior,key=lambda o:o['captured_ms']);delta=center-profiles.matrix(previous['shell_center_in_weapon'])[3,:3];length=np.linalg.norm(delta)
                if length>1e-6:approach={'previous_row':previous['row'],'distance_m':float(length),'direction':(delta/length).tolist(),
                    'sample_delta_ms':observation['captured_ms']-previous['captured_ms'],'minimum_motion_for_direction_m':.01,'verified_rail_axis':False}
            receiver.append({'row':observation['row'],'captured_ms':observation['captured_ms'],'center_in_weapon':center.tolist(),
                'nearest_receiver_surface':nearest,'coplanar_surface_patch':coplanar_patch(body_tri,nearest['triangle_index']),
                'underside_rays':scans,'last_observed_motion':approach,
                'aperture_topology_verified':False,'loading_socket_verified':False})
        ranked=sorted((c for c in contacts if 'pinch_proxy_score_m' in c and not c['terminal_before_transfer']),key=lambda c:c['pinch_proxy_score_m'])
        if ranked and ranked[0]['observed_center_frame_in_wrist'] is not None:
            ranked[0]['item_from_hand_candidate']=profiles.matrix(np.linalg.inv(profiles.matrix(ranked[0]['observed_center_frame_in_wrist']))).reshape(-1).tolist()
            ranked[0]['item_frame_definition']='Shell asset bounds center, canonical asset basis; row-vector hand pose in item coordinates'
            ranked[0]['candidate_scope']='Authored pre-insertion hand placement for preview; no seat/socket or native ammunition authority'
        results.append({'actor':group['actor'],'weapon':group['weapon'],'space':group['space'],
            'contact_observations':contacts,'ranked_grasp_observation_rows':[c['row'] for c in ranked],
            'best_authored_grasp_observation':ranked[0] if ranked else None,'receiver_surface_observations':receiver})
    return {'schema':'fvr.bc2.reload_contact_candidates','schema_version':1,'native_written':False,'groups':results,
        'limitations':['Terminal finger-joint centers are anatomical proxies, not captured fingertip skin contact surfaces.',
          'Grasp candidates are individual authored observations; whole-cycle wrist stability is not an eligibility gate.',
          'Actual asset triangles are transformed through captured palettes under the declared asset/native bind-space correspondence.',
          'Two-sided ray intersections describe mesh surfaces, not native collision shapes or validated loading-port topology.',
          'No contact primitive, runtime reload gate or loading socket is enabled.']}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--observations',type=Path,required=True)
    p.add_argument('--asset-evidence',type=Path,required=True);p.add_argument('--archive',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();observations=profiles.load_json(a.observations);evidence=profiles.load_json(a.asset_evidence)
    if profiles.digest(a.native)!=observations['sources']['native']['sha256']:raise ValueError('Native trace hash differs from observations')
    prefix=evidence['resources'][0]['name'].removesuffix('.res');result=analyze(profiles.load_json(a.native),observations,evidence,assets.Archive(a.archive),prefix)
    result['sources']={name:{'path':str(path.resolve()),'sha256':profiles.digest(path)} for name,path in [('native',a.native),('observations',a.observations),('asset_evidence',a.asset_evidence)]}
    result['analyzer_sha256']=profiles.digest(__file__);a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf8')
    print(json.dumps({'output':str(a.output),'groups':len(result['groups']),'best_grasp_rows':[g['ranked_grasp_observation_rows'][:3] for g in result['groups']]}))

if __name__=='__main__':main()
