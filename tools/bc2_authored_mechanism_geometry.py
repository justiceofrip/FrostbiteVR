"""Offline exact authored part motion/contact data; no semantic/native admission."""
from __future__ import annotations
import math,json,argparse
from pathlib import Path
import bc2_authored_magazine_geometry as mag
import bc2_authored_grip_bindings as grip
from bc2_magazine_contact_batch import ArchivePool,readers,filter_inputs,refresh_meshes,read

MOTION={'sample_hz':30,'max_duration_s':15,'max_parts':128,'hinge_translation_m':.002,
        'rotation_active_rad':.2,'slider_rotation_rad':.02,'slider_translation_m':.01,'axis_residual_m':.002,'axis_cosine':.99}

def axis_angle(m):
    mag.matrix(m);mag.inverse_rigid(m)
    angle=math.acos(max(-1.,min(1.,(m[0]+m[5]+m[10]-1)/2)))
    if angle<1e-5 or abs(math.pi-angle)<.01:return None,angle
    axis=mag.unit([m[6]-m[9],m[8]-m[2],m[1]-m[4]])
    return axis,angle

def classify(frames):
    """Part relative to its ACTUAL parent; a moving ancestor is not a hinge."""
    if not 2<=len(frames)<=452:raise ValueError('Motion sample bound')
    base=frames[0];inverse=mag.inverse_rigid(base);deltas=[mag.multiply(f,inverse) for f in frames]
    rotations=[axis_angle(d) for d in deltas];distances=[mag.norm(d[12:15]) for d in deltas]
    maxmove=max(distances);maxangle=max(a for _,a in rotations);peak=max(range(len(frames)),key=lambda n:rotations[n][1])
    kind='static';axis=None;residual=0.
    if maxangle>=MOTION['rotation_active_rad'] and maxmove<=MOTION['hinge_translation_m']:
        axis=rotations[peak][0]
        reliable=[a for a,t in rotations if t>=.05 and a is not None]
        if axis is not None and reliable and all(abs(mag.dot(a,axis))>=MOTION['axis_cosine'] for a in reliable):kind='hinge_candidate'
        else:kind='general_rigid_rotation'
    elif maxmove>=MOTION['slider_translation_m'] and maxangle<=MOTION['slider_rotation_rad']:
        axis=mag.unit(deltas[max(range(len(frames)),key=lambda n:distances[n])][12:15])
        residual=max(mag.norm(mag.sub(d[12:15],mag.scale(axis,mag.dot(axis,d[12:15])))) for d in deltas)
        kind='slider_candidate' if residual<=MOTION['axis_residual_m'] else 'general_rigid_translation'
    elif maxmove>=.0005 or maxangle>=.003:kind='general_rigid_motion'
    peak=peak if maxangle>=.003 else distances.index(maxmove)
    return {'kind':kind,'max_translation_m':maxmove,'max_rotation_rad':maxangle,'axis_in_baseline_part':axis,'peak_sample':peak,
            'axis_line_residual_m':residual,'baseline_parent_from_part':base,'peak_parent_from_part':frames[peak],
            'baseline_is_native_closed':False,'semantic_role':None}

def paired_contacts(samples,bone,part):
    """Stable wrist/item/fingers from ONE frame, for moving rigid mechanisms."""
    width=round(.2*MOTION['sample_hz']);lo,hi=mag.bounds(part['points']);observed=[]
    for sample in samples:
        rel=sample['weapon_relative'];wrist=rel['LeftHand'];item=rel[bone]
        hand=mag.multiply(wrist,mag.inverse_rigid(item));fingers={n:mag.multiply(rel[n],mag.inverse_rigid(wrist)) for n in mag.FINGERS}
        center=[sum(mag.point(fingers['LeftHand'+d+'1'][12:15],hand)[k] for d in ('Index','Middle','Ring','Pinky'))/4 for k in range(3)]
        contact=mag.norm([max(lo[k]-center[k],0.,center[k]-hi[k]) for k in range(3)])
        observed.append({'time':sample['time_seconds'],'hand':hand,'fingers':fingers,'item':item,'contact':contact})
    candidates=[]
    for begin in range(len(observed)-width):
        group=observed[begin:begin+width+1];middle=group[width//2]
        errors=[mag.grasp_separation(s['hand'],middle['hand']) for s in group]
        translation=max(x[0] for x in errors);angle=max(x[1] for x in errors)
        finger=max(mag.grasp_separation(s['fingers'][n],middle['fingers'][n])[1] for n in mag.FINGERS for s in group)
        distance=max(s['contact'] for s in group);move,rotation=mag.grasp_separation(observed[0]['item'],middle['item'])
        if group[-1]['time']-group[0]['time']<.2-1e-8 or (move<.04 and rotation<.2):continue
        if translation>=.01 or angle>=.15 or finger>=.05 or distance>=.05:continue
        candidates.append((translation,angle,middle['time'],{'begin_s':group[0]['time'],'end_s':group[-1]['time'],'time_s':middle['time'],
            'part_from_wrist':middle['hand'],'wrist_from_fingers':middle['fingers'],'weapon_from_part':middle['item'],
            'max_relative_translation_m':translation,'max_relative_angle_rad':angle,'max_finger_angle_rad':finger,'knuckle_bounds_distance_m':distance}))
    return {'windows':len(candidates),'pose':min(candidates,key=lambda x:x[:3])[3] if candidates else None,
            'kind':'same_frame_authored_contact_candidate','native_contact_verified':False}

def analyze(profile,parts,closed,clip,skeleton,receipt):
    if not .2<=clip.duration<=MOTION['max_duration_s'] or not 0<len(parts)<=MOTION['max_parts']:raise ValueError('Mechanism clip/part bound')
    parents={n:skeleton.names[p] if p>=0 else None for n,p in zip(skeleton.names,skeleton.parents)}
    valid=[];gaps=[]
    for name in sorted(parts):
        try:clip.evaluate(skeleton,0,[name]+([parents[name]] if parents[name] else []));valid.append(name)
        except (ValueError,KeyError) as e:gaps.append({'bone':name,'reason':str(e)})
    names=set(valid)|{parents[n] for n in valid if parents[n]};hands=['LeftHand',*mag.FINGERS];hand_valid=True
    try:clip.evaluate(skeleton,0,hands);names.update(hands)
    except (ValueError,KeyError) as e:hand_valid=False;gaps.append({'hand':'left','reason':str(e)})
    count=math.ceil(clip.duration*MOTION['sample_hz']);samples=[clip.evaluate(skeleton,min(i/MOTION['sample_hz'],clip.duration),sorted(names)) for i in range(count+1)]
    output=[]
    for name in valid:
        part=parts[name];parent=parents[name]
        frames=[mag.multiply(s['weapon_relative'][name],mag.inverse_rigid(s['weapon_relative'][parent])) if parent else s['weapon_relative'][name] for s in samples]
        motion=classify(frames)
        weighted_sections=[s['name'] for s in receipt.get('skin_sections',[]) if name in s['bone_names'] and s['multi_weight_vertices']]
        independent=part['mixed_triangles']==0 and not weighted_sections
        record={'bone':name,'parent':parent,'direct_weapon_child':parent=='jntWpn_1','triangles':part['triangles'],
            'mixed_triangles':part['mixed_triangles'],'weighted_sections':weighted_sections,'independent_rigid_geometry':independent,'extent_m':part['extent'],
            'bounds_m':[part['minimum'],part['maximum']],'sections':part['sections'],'motion':motion,
            'weapon_from_baseline':samples[0]['weapon_relative'][name],
            'static_authored_closed_candidate':closed['weapon_relative'].get(name) if closed.get('bone_evaluation_status',{}).get(name)=='static_authored_pose' else None,
            'contact':paired_contacts(samples,name,part) if hand_valid and motion['kind']!='static' and part['triangles']>=24 and independent else None,
            'peak_time_s':samples[motion['peak_sample']]['time_seconds'],
            'weapon_from_peak':samples[motion['peak_sample']]['weapon_relative'][name]}
        if not record['independent_rigid_geometry']:record['presentation_limit']='mixed/cross-part triangles require deforming or coupled-subtree treatment'
        output.append(record)
    unrepresented=sorted({n for s in receipt.get('skin_sections',[]) for n in s['bone_names'] if n and n not in parts})
    result={'asset':profile['native_asset_name'],'configured_mesh':profile['configured_mesh_path'],'binding_digest':profile['binding_digest'],
        'weapon_configuration':profile['weapon'],'state_index':profile['state_index'],'source':receipt,
        'skeleton_sha256':skeleton.sha256,'rig_fingerprint':skeleton.fingerprint,'parts':output,'gaps':gaps,'skin_bones_without_rigid_part':unrepresented,
        'duration_s':clip.duration,'sample_count':len(samples),'sample_hz':MOTION['sample_hz'],'criteria':MOTION,
        'runtime_interpolation_verified':False,'native_mechanism_semantics_verified':False,'runtime_admitted':False}
    result['digest']=grip.digest(result);return result

def bind_cached(result,profile,receipt):
    # Numeric work may be shared by identical content; exact config/path joins
    # always remain this invocation's own, never the first matching variant.
    value={**result,'asset':profile['native_asset_name'],'configured_mesh':profile['configured_mesh_path'],
           'weapon_configuration':profile['weapon'],'state_index':profile['state_index'],
           'binding_digest':profile['binding_digest'],'source':receipt}
    value['digest']=grip.digest({k:v for k,v in value.items() if k!='digest'})
    return value

def coverage(result,magazine):
    models={}
    for row in result['mechanisms']:
        key=row['configured_mesh'];model=models.setdefault(key,{
            'configured_mesh':key,'asset_names':[],'definitions':[],'source_hashes':{},'parts':[],
            'native_manual_reload_enabled':False,'headset_tested':False})
        if row['asset'] not in model['asset_names']:model['asset_names'].append(row['asset'])
        model['definitions'].append({'asset':row['asset'],'configuration':row['weapon_configuration'],
                                     'state_index':row['state_index'],'binding_digest':row['binding_digest'],'mechanism_digest':row['digest']})
        model['source_hashes']={k:row['source'][k] for k in ('mesh_sha256','lod_sha256','sha256','asset_document_sha256')}
        model['parts']=[{'bone':p['bone'],'parent':p['parent'],'triangles':p['triangles'],
            'independent_rigid_geometry':p['independent_rigid_geometry'],'motion':p['motion']['kind'],
            'max_translation_m':p['motion']['max_translation_m'],'max_rotation_rad':p['motion']['max_rotation_rad'],
            'contact_windows':p['contact']['windows'] if p['contact'] else 0,
            'static_closed_frame':p['static_authored_closed_candidate'] is not None,'semantic_role':None} for p in row['parts']]
        model['missing_tracks']=row['gaps'];model['unrepresented_skin_bones']=row['skin_bones_without_rigid_part']
    for model in models.values():
        model['asset_names'].sort();names=set(model['asset_names'])
        model['authored_ammo']=[{'asset':w['native_name'],'resource':w['resource'],'instance_guid':w['instance_guid'],
           'fields':{k:v['value'] for k,v in w['fields'].items() if k in ('FireLogic.ReloadType','FireLogic.ReloadTime','FireLogic.ReloadThreshold','Ammo.MagazineCapacity')}}
           for w in result['configurations']['resolved_weapons'] if w['native_name'] in names]
        model['strict_magazine_gaps']=[g for g in magazine['gaps'] if g['native_asset_name'] in names]
        model['geometry_role_candidates']=sum(p['native_asset_name'] in names for p in magazine['profiles'])
    return {'schema':'fvr.bc2.authored_mechanism_coverage.v1','model_count':len(models),
        'native_asset_name_count':len({a for m in models.values() for a in m['asset_names']}),
        'configuration_count':len(result['configurations']['resolved_weapons']),
        'state_record_count':len(result['mechanisms']),'independent_content_jobs':result['independent_content_jobs'],
        'models':list(models.values()),'errors':result['errors'],'reference_gaps':result['reference_gaps'],
        'limits':['Authored spline interpolation is decoded but not native playback verified.',
                  'Part contact candidates use a separate30Hz mechanism sampler; they do not pass the stricter magazine60Hz/minimum100mm carried-motion gate.',
                  'ReloadType=rtMagazine means native bulk transfer, not a removable-box or belt mechanism classification.',
                  'No native hold, stage control, refill, restoration, runtime admission, or headset test is established.']}

def run(game,metadata,inventory,poses,assets,output):
    config,mesh=filter_inputs(metadata['configuration'],metadata['mesh_bindings'],assets)
    pool=ArchivePool(game,config['archive'],config['index_sha256']);rows=[]
    with readers(pool):
        mesh=refresh_meshes(config,inventory,pool,game)
        bindings=grip.load(game,Path(config['archive']),config,mesh,poses,reload_references_only=True)
        # Preserve every exact config join. Mechanism interpolation is reused for
        # identical mesh/clip/skeleton content, not mistaken for native identity.
        cache={};errors=[]
        def sink(profile,parts,closed,clip,skeleton,receipt):
            key=(receipt['mesh_sha256'],receipt['lod_sha256'],clip.sha256,skeleton.sha256,profile['animation_sha256'])
            try:
                if key not in cache:cache[key]=analyze(profile,parts,closed,clip,skeleton,receipt)
                rows.append(bind_cached(cache[key],profile,receipt))
            except (ValueError,KeyError,TypeError) as e:errors.append({'asset':profile['native_asset_name'],'binding_digest':profile['binding_digest'],'reason':str(e)})
        magazine=mag.derive(game,bindings,mesh,assets,paired_grasp_assets=assets,mechanism_sink=sink)
    result={'schema':'fvr.bc2.authored_mechanism_geometry.v1','configurations':config,'mechanisms':rows,'errors':errors,
            'reference_gaps':bindings['gaps'],'runtime_admission':False,'independent_content_jobs':len(cache),'assets_requested':sorted(assets)}
    output.mkdir(parents=True,exist_ok=True)
    for name,data in (('mechanisms.json',result),('magazine-candidates.json',magazine),('reference-bindings.json',bindings),('mesh-bindings.json',mesh),('coverage.json',coverage(result,magazine))):
        (output/name).write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
    return result

def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('game','metadata','inventory','hand-poses','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--asset',action='append',required=True);a=p.parse_args(argv)
    metadata,mh=read(a.metadata);inventory,ih=read(a.inventory);poses,ph=read(a.hand_poses)
    result=run(a.game,metadata,inventory,poses,set(a.asset),a.output)
    (a.output/'inputs.json').write_text(json.dumps({'sha256':{'metadata':mh,'inventory':ih,'poses':ph},'assets':sorted(set(a.asset))},indent=2)+'\n')
    print(json.dumps({'mechanism_records':len(result['mechanisms']),'independent_content_jobs':result['independent_content_jobs'],
                     'errors':len(result['errors']),'reference_gaps':len(result['reference_gaps'])}));return 0
if __name__=='__main__':raise SystemExit(main())
